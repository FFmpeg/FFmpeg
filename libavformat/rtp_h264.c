/*
 * RTP H264 Protocol (RFC3984)
 * Copyright (c) 2006 Ryan Martell
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
* @file libavformat/rtp_h264.c
 * @brief H.264 / RTP Code (RFC3984)
 * @author Ryan Martell <rdm4@martellventures.com>
 *
 * @note Notes:
 * Notes:
 * This currently supports packetization mode:
 * Single Nal Unit Mode (0), or
 * Non-Interleaved Mode (1).  It currently does not support
 * Interleaved Mode (2). (This requires implementing STAP-B, MTAP16, MTAP24, FU-B packet types)
 *
 * @note TODO:
 * 1) RTCP sender reports for udp streams are required..
 *
 */

#include "libavutil/base64.h"
#include "libavutil/avstring.h"
#include "libavcodec/bitstream.h"
#include "avformat.h"
#include "mpegts.h"

#include <unistd.h>
#include "network.h"
#include <assert.h>

#include "rtpdec.h"
#include "rtp_h264.h"

/**
    RTP/H264 specific private data.
*/
struct PayloadContext {
    unsigned long cookie;       ///< sanity check, to make sure we get the pointer we're expecting.

    //sdp setup parameters
    uint8_t profile_idc;        ///< from the sdp setup parameters.
    uint8_t profile_iop;        ///< from the sdp setup parameters.
    uint8_t level_idc;          ///< from the sdp setup parameters.
    int packetization_mode;     ///< from the sdp setup parameters.
#ifdef DEBUG
    int packet_types_received[32];
#endif
};

#define MAGIC_COOKIE (0xdeadbeef)       ///< Cookie for the extradata; to verify we are what we think we are, and that we haven't been freed.
#define DEAD_COOKIE (0xdeaddead)        ///< Cookie for the extradata; once it is freed.

/* ---------------- private code */
static void sdp_parse_fmtp_config_h264(AVStream * stream,
                                       PayloadContext * h264_data,
                                       char *attr, char *value)
{
    AVCodecContext *codec = stream->codec;
    assert(codec->codec_id == CODEC_ID_H264);
    assert(h264_data != NULL);

    if (!strcmp(attr, "packetization-mode")) {
        av_log(codec, AV_LOG_DEBUG, "RTP Packetization Mode: %d\n", atoi(value));
        h264_data->packetization_mode = atoi(value);
        /*
           Packetization Mode:
           0 or not present: Single NAL mode (Only nals from 1-23 are allowed)
           1: Non-interleaved Mode: 1-23, 24 (STAP-A), 28 (FU-A) are allowed.
           2: Interleaved Mode: 25 (STAP-B), 26 (MTAP16), 27 (MTAP24), 28 (FU-A), and 29 (FU-B) are allowed.
         */
        if (h264_data->packetization_mode > 1)
            av_log(codec, AV_LOG_ERROR,
                   "Interleaved RTP mode is not supported yet.");
    } else if (!strcmp(attr, "profile-level-id")) {
        if (strlen(value) == 6) {
            char buffer[3];
            // 6 characters=3 bytes, in hex.
            uint8_t profile_idc;
            uint8_t profile_iop;
            uint8_t level_idc;

            buffer[0] = value[0]; buffer[1] = value[1]; buffer[2] = '\0';
            profile_idc = strtol(buffer, NULL, 16);
            buffer[0] = value[2]; buffer[1] = value[3];
            profile_iop = strtol(buffer, NULL, 16);
            buffer[0] = value[4]; buffer[1] = value[5];
            level_idc = strtol(buffer, NULL, 16);

            // set the parameters...
            av_log(codec, AV_LOG_DEBUG,
                   "RTP Profile IDC: %x Profile IOP: %x Level: %x\n",
                   profile_idc, profile_iop, level_idc);
            h264_data->profile_idc = profile_idc;
            h264_data->profile_iop = profile_iop;
            h264_data->level_idc = level_idc;
        }
    } else  if (!strcmp(attr, "sprop-parameter-sets")) {
        uint8_t start_sequence[]= { 0, 0, 1 };
        codec->extradata_size= 0;
        codec->extradata= NULL;

        while (*value) {
            char base64packet[1024];
            uint8_t decoded_packet[1024];
            uint32_t packet_size;
            char *dst = base64packet;

            while (*value && *value != ','
                   && (dst - base64packet) < sizeof(base64packet) - 1) {
                *dst++ = *value++;
            }
            *dst++ = '\0';

            if (*value == ',')
                value++;

            packet_size= av_base64_decode(decoded_packet, base64packet, sizeof(decoded_packet));
            if (packet_size) {
                uint8_t *dest= av_malloc(packet_size+sizeof(start_sequence)+codec->extradata_size);
                if(dest)
                {
                    if(codec->extradata_size)
                    {
                        // av_realloc?
                        memcpy(dest, codec->extradata, codec->extradata_size);
                        av_free(codec->extradata);
                    }

                    memcpy(dest+codec->extradata_size, start_sequence, sizeof(start_sequence));
                    memcpy(dest+codec->extradata_size+sizeof(start_sequence), decoded_packet, packet_size);

                    codec->extradata= dest;
                    codec->extradata_size+= sizeof(start_sequence)+packet_size;
                } else {
                    av_log(codec, AV_LOG_ERROR, "Unable to allocate memory for extradata!");
                }
            }
        }
        av_log(codec, AV_LOG_DEBUG, "Extradata set to %p (size: %d)!", codec->extradata, codec->extradata_size);
    }
}

// return 0 on packet, no more left, 1 on packet, 1 on partial packet...
static int h264_handle_packet(AVFormatContext *ctx,
                              PayloadContext *data,
                              AVStream *st,
                              AVPacket * pkt,
                              uint32_t * timestamp,
                              const uint8_t * buf,
                              int len, int flags)
{
    uint8_t nal = buf[0];
    uint8_t type = (nal & 0x1f);
    int result= 0;
    uint8_t start_sequence[]= {0, 0, 1};

#ifdef DEBUG
    assert(data);
    assert(data->cookie == MAGIC_COOKIE);
#endif
    assert(buf);

    if (type >= 1 && type <= 23)
        type = 1;              // simplify the case. (these are all the nal types used internally by the h264 codec)
    switch (type) {
    case 0:                    // undefined;
        result= -1;
        break;

    case 1:
        av_new_packet(pkt, len+sizeof(start_sequence));
        memcpy(pkt->data, start_sequence, sizeof(start_sequence));
        memcpy(pkt->data+sizeof(start_sequence), buf, len);
#ifdef DEBUG
        data->packet_types_received[nal & 0x1f]++;
#endif
        break;

    case 24:                   // STAP-A (one packet, multiple nals)
        // consume the STAP-A NAL
        buf++;
        len--;
        // first we are going to figure out the total size....
        {
            int pass= 0;
            int total_length= 0;
            uint8_t *dst= NULL;

            for(pass= 0; pass<2; pass++) {
                const uint8_t *src= buf;
                int src_len= len;

                do {
                    uint16_t nal_size = AV_RB16(src); // this going to be a problem if unaligned (can it be?)

                    // consume the length of the aggregate...
                    src += 2;
                    src_len -= 2;

                    if (nal_size <= src_len) {
                        if(pass==0) {
                            // counting...
                            total_length+= sizeof(start_sequence)+nal_size;
                        } else {
                            // copying
                            assert(dst);
                            memcpy(dst, start_sequence, sizeof(start_sequence));
                            dst+= sizeof(start_sequence);
                            memcpy(dst, src, nal_size);
#ifdef DEBUG
                            data->packet_types_received[*src & 0x1f]++;
#endif
                            dst+= nal_size;
                        }
                    } else {
                        av_log(ctx, AV_LOG_ERROR,
                               "nal size exceeds length: %d %d\n", nal_size, src_len);
                    }

                    // eat what we handled...
                    src += nal_size;
                    src_len -= nal_size;

                    if (src_len < 0)
                        av_log(ctx, AV_LOG_ERROR,
                               "Consumed more bytes than we got! (%d)\n", src_len);
                } while (src_len > 2);      // because there could be rtp padding..

                if(pass==0) {
                    // now we know the total size of the packet (with the start sequences added)
                    av_new_packet(pkt, total_length);
                    dst= pkt->data;
                } else {
                    assert(dst-pkt->data==total_length);
                }
            }
        }
        break;

    case 25:                   // STAP-B
    case 26:                   // MTAP-16
    case 27:                   // MTAP-24
    case 29:                   // FU-B
        av_log(ctx, AV_LOG_ERROR,
               "Unhandled type (%d) (See RFC for implementation details\n",
               type);
        result= -1;
        break;

    case 28:                   // FU-A (fragmented nal)
        buf++;
        len--;                  // skip the fu_indicator
        {
            // these are the same as above, we just redo them here for clarity...
            uint8_t fu_indicator = nal;
            uint8_t fu_header = *buf;   // read the fu_header.
            uint8_t start_bit = fu_header >> 7;
//            uint8_t end_bit = (fu_header & 0x40) >> 6;
            uint8_t nal_type = (fu_header & 0x1f);
            uint8_t reconstructed_nal;

            // reconstruct this packet's true nal; only the data follows..
            reconstructed_nal = fu_indicator & (0xe0);  // the original nal forbidden bit and NRI are stored in this packet's nal;
            reconstructed_nal |= nal_type;

            // skip the fu_header...
            buf++;
            len--;

#ifdef DEBUG
            if (start_bit)
                data->packet_types_received[nal_type]++;
#endif
            if(start_bit) {
                // copy in the start sequence, and the reconstructed nal....
                av_new_packet(pkt, sizeof(start_sequence)+sizeof(nal)+len);
                memcpy(pkt->data, start_sequence, sizeof(start_sequence));
                pkt->data[sizeof(start_sequence)]= reconstructed_nal;
                memcpy(pkt->data+sizeof(start_sequence)+sizeof(nal), buf, len);
            } else {
                av_new_packet(pkt, len);
                memcpy(pkt->data, buf, len);
            }
        }
        break;

    case 30:                   // undefined
    case 31:                   // undefined
    default:
        av_log(ctx, AV_LOG_ERROR, "Undefined type (%d)", type);
        result= -1;
        break;
    }

    pkt->stream_index = st->index;

    return result;
}

/* ---------------- public code */
static PayloadContext *h264_new_extradata(void)
{
    PayloadContext *data =
        av_mallocz(sizeof(PayloadContext) +
                   FF_INPUT_BUFFER_PADDING_SIZE);

    if (data) {
        data->cookie = MAGIC_COOKIE;
    }

    return data;
}

static void h264_free_extradata(PayloadContext *data)
{
#ifdef DEBUG
    int ii;

    for (ii = 0; ii < 32; ii++) {
        if (data->packet_types_received[ii])
            av_log(NULL, AV_LOG_DEBUG, "Received %d packets of type %d\n",
                   data->packet_types_received[ii], ii);
    }
#endif

    assert(data);
    assert(data->cookie == MAGIC_COOKIE);

    // avoid stale pointers (assert)
    data->cookie = DEAD_COOKIE;

    // and clear out this...
    av_free(data);
}

static int parse_h264_sdp_line(AVFormatContext *s, int st_index,
                               PayloadContext *h264_data, const char *line)
{
    AVStream *stream = s->streams[st_index];
    AVCodecContext *codec = stream->codec;
    const char *p = line;

    assert(h264_data->cookie == MAGIC_COOKIE);

    if (av_strstart(p, "framesize:", &p)) {
        char buf1[50];
        char *dst = buf1;

        // remove the protocol identifier..
        while (*p && *p == ' ') p++; // strip spaces.
        while (*p && *p != ' ') p++; // eat protocol identifier
        while (*p && *p == ' ') p++; // strip trailing spaces.
        while (*p && *p != '-' && (buf1 - dst) < sizeof(buf1) - 1) {
            *dst++ = *p++;
        }
        *dst = '\0';

        // a='framesize:96 320-240'
        // set our parameters..
        codec->width = atoi(buf1);
        codec->height = atoi(p + 1); // skip the -
        codec->pix_fmt = PIX_FMT_YUV420P;
    } else if (av_strstart(p, "fmtp:", &p)) {
        char attr[256];
        char value[4096];

        // remove the protocol identifier..
        while (*p && *p == ' ') p++; // strip spaces.
        while (*p && *p != ' ') p++; // eat protocol identifier
        while (*p && *p == ' ') p++; // strip trailing spaces.

        /* loop on each attribute */
        while (rtsp_next_attr_and_value
               (&p, attr, sizeof(attr), value, sizeof(value))) {
            /* grab the codec extra_data from the config parameter of the fmtp line */
            sdp_parse_fmtp_config_h264(stream, h264_data, attr, value);
        }
    } else if (av_strstart(p, "cliprect:", &p)) {
        // could use this if we wanted.
    }

    av_set_pts_info(stream, 33, 1, 90000);      // 33 should be right, because the pts is 64 bit? (done elsewhere; this is a one time thing)

    return 0;                   // keep processing it the normal way...
}

/**
This is the structure for expanding on the dynamic rtp protocols (makes everything static. yay!)
*/
RTPDynamicProtocolHandler ff_h264_dynamic_handler = {
    "H264",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H264,
    parse_h264_sdp_line,
    h264_new_extradata,
    h264_free_extradata,
    h264_handle_packet
};
