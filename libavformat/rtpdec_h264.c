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
 * @file
 * @brief H.264 / RTP Code (RFC3984)
 * @author Ryan Martell <rdm4@martellventures.com>
 *
 * @note Notes:
 * Notes:
 * This currently supports packetization mode:
 * Single Nal Unit Mode (0), or
 * Non-Interleaved Mode (1).  It currently does not support
 * Interleaved Mode (2). (This requires implementing STAP-B, MTAP16, MTAP24,
 *                        FU-B packet types)
 */

#include "libavutil/attributes.h"
#include "libavutil/base64.h"
#include "libavutil/avstring.h"
#include "libavcodec/get_bits.h"
#include "avformat.h"

#include "network.h"
#include <assert.h>

#include "rtpdec.h"
#include "rtpdec_formats.h"

struct PayloadContext {
    // sdp setup parameters
    uint8_t profile_idc;
    uint8_t profile_iop;
    uint8_t level_idc;
    int packetization_mode;
#ifdef DEBUG
    int packet_types_received[32];
#endif
};

#ifdef DEBUG
#define COUNT_NAL_TYPE(data, nal) data->packet_types_received[(nal) & 0x1f]++
#else
#define COUNT_NAL_TYPE(data, nal) do { } while (0)
#endif

static const uint8_t start_sequence[] = { 0, 0, 0, 1 };

static int sdp_parse_fmtp_config_h264(AVStream *stream,
                                      PayloadContext *h264_data,
                                      char *attr, char *value)
{
    AVCodecContext *codec = stream->codec;
    assert(codec->codec_id == AV_CODEC_ID_H264);
    assert(h264_data != NULL);

    if (!strcmp(attr, "packetization-mode")) {
        av_log(codec, AV_LOG_DEBUG, "RTP Packetization Mode: %d\n", atoi(value));
        h264_data->packetization_mode = atoi(value);
        /*
         * Packetization Mode:
         * 0 or not present: Single NAL mode (Only nals from 1-23 are allowed)
         * 1: Non-interleaved Mode: 1-23, 24 (STAP-A), 28 (FU-A) are allowed.
         * 2: Interleaved Mode: 25 (STAP-B), 26 (MTAP16), 27 (MTAP24), 28 (FU-A),
         *                      and 29 (FU-B) are allowed.
         */
        if (h264_data->packetization_mode > 1)
            av_log(codec, AV_LOG_ERROR,
                   "Interleaved RTP mode is not supported yet.\n");
    } else if (!strcmp(attr, "profile-level-id")) {
        if (strlen(value) == 6) {
            char buffer[3];
            // 6 characters=3 bytes, in hex.
            uint8_t profile_idc;
            uint8_t profile_iop;
            uint8_t level_idc;

            buffer[0]   = value[0];
            buffer[1]   = value[1];
            buffer[2]   = '\0';
            profile_idc = strtol(buffer, NULL, 16);
            buffer[0]   = value[2];
            buffer[1]   = value[3];
            profile_iop = strtol(buffer, NULL, 16);
            buffer[0]   = value[4];
            buffer[1]   = value[5];
            level_idc   = strtol(buffer, NULL, 16);

            av_log(codec, AV_LOG_DEBUG,
                   "RTP Profile IDC: %x Profile IOP: %x Level: %x\n",
                   profile_idc, profile_iop, level_idc);
            h264_data->profile_idc = profile_idc;
            h264_data->profile_iop = profile_iop;
            h264_data->level_idc   = level_idc;
        }
    } else if (!strcmp(attr, "sprop-parameter-sets")) {
        codec->extradata_size = 0;
        av_freep(&codec->extradata);

        while (*value) {
            char base64packet[1024];
            uint8_t decoded_packet[1024];
            int packet_size;
            char *dst = base64packet;

            while (*value && *value != ','
                   && (dst - base64packet) < sizeof(base64packet) - 1) {
                *dst++ = *value++;
            }
            *dst++ = '\0';

            if (*value == ',')
                value++;

            packet_size = av_base64_decode(decoded_packet, base64packet,
                                           sizeof(decoded_packet));
            if (packet_size > 0) {
                uint8_t *dest = av_malloc(packet_size + sizeof(start_sequence) +
                                          codec->extradata_size +
                                          FF_INPUT_BUFFER_PADDING_SIZE);
                if (!dest) {
                    av_log(codec, AV_LOG_ERROR,
                           "Unable to allocate memory for extradata!\n");
                    return AVERROR(ENOMEM);
                }
                if (codec->extradata_size) {
                    memcpy(dest, codec->extradata, codec->extradata_size);
                    av_free(codec->extradata);
                }

                memcpy(dest + codec->extradata_size, start_sequence,
                       sizeof(start_sequence));
                memcpy(dest + codec->extradata_size + sizeof(start_sequence),
                       decoded_packet, packet_size);
                memset(dest + codec->extradata_size + sizeof(start_sequence) +
                       packet_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

                codec->extradata       = dest;
                codec->extradata_size += sizeof(start_sequence) + packet_size;
            }
        }
        av_log(codec, AV_LOG_DEBUG, "Extradata set to %p (size: %d)!\n",
               codec->extradata, codec->extradata_size);
    }
    return 0;
}

// return 0 on packet, no more left, 1 on packet, 1 on partial packet
static int h264_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                              AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                              const uint8_t *buf, int len, uint16_t seq,
                              int flags)
{
    uint8_t nal;
    uint8_t type;
    int result = 0;

    if (!len) {
        av_log(ctx, AV_LOG_ERROR, "Empty H264 RTP packet\n");
        return AVERROR_INVALIDDATA;
    }
    nal  = buf[0];
    type = nal & 0x1f;

    assert(data);
    assert(buf);

    /* Simplify the case (these are all the nal types used internally by
     * the h264 codec). */
    if (type >= 1 && type <= 23)
        type = 1;
    switch (type) {
    case 0:                    // undefined, but pass them through
    case 1:
        av_new_packet(pkt, len + sizeof(start_sequence));
        memcpy(pkt->data, start_sequence, sizeof(start_sequence));
        memcpy(pkt->data + sizeof(start_sequence), buf, len);
        COUNT_NAL_TYPE(data, nal);
        break;

    case 24:                   // STAP-A (one packet, multiple nals)
        // consume the STAP-A NAL
        buf++;
        len--;
        // first we are going to figure out the total size
        {
            int pass         = 0;
            int total_length = 0;
            uint8_t *dst     = NULL;

            for (pass = 0; pass < 2; pass++) {
                const uint8_t *src = buf;
                int src_len        = len;

                while (src_len > 2) {
                    uint16_t nal_size = AV_RB16(src);

                    // consume the length of the aggregate
                    src     += 2;
                    src_len -= 2;

                    if (nal_size <= src_len) {
                        if (pass == 0) {
                            // counting
                            total_length += sizeof(start_sequence) + nal_size;
                        } else {
                            // copying
                            assert(dst);
                            memcpy(dst, start_sequence, sizeof(start_sequence));
                            dst += sizeof(start_sequence);
                            memcpy(dst, src, nal_size);
                            COUNT_NAL_TYPE(data, *src);
                            dst += nal_size;
                        }
                    } else {
                        av_log(ctx, AV_LOG_ERROR,
                               "nal size exceeds length: %d %d\n", nal_size, src_len);
                    }

                    // eat what we handled
                    src     += nal_size;
                    src_len -= nal_size;

                    if (src_len < 0)
                        av_log(ctx, AV_LOG_ERROR,
                               "Consumed more bytes than we got! (%d)\n", src_len);
                }

                if (pass == 0) {
                    /* now we know the total size of the packet (with the
                     * start sequences added) */
                    av_new_packet(pkt, total_length);
                    dst = pkt->data;
                } else {
                    assert(dst - pkt->data == total_length);
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
        result = AVERROR(ENOSYS);
        break;

    case 28:                   // FU-A (fragmented nal)
        buf++;
        len--;                 // skip the fu_indicator
        if (len > 1) {
            // these are the same as above, we just redo them here for clarity
            uint8_t fu_indicator      = nal;
            uint8_t fu_header         = *buf;
            uint8_t start_bit         = fu_header >> 7;
            uint8_t av_unused end_bit = (fu_header & 0x40) >> 6;
            uint8_t nal_type          = fu_header & 0x1f;
            uint8_t reconstructed_nal;

            // Reconstruct this packet's true nal; only the data follows.
            /* The original nal forbidden bit and NRI are stored in this
             * packet's nal. */
            reconstructed_nal  = fu_indicator & 0xe0;
            reconstructed_nal |= nal_type;

            // skip the fu_header
            buf++;
            len--;

            if (start_bit)
                COUNT_NAL_TYPE(data, nal_type);
            if (start_bit) {
                /* copy in the start sequence, and the reconstructed nal */
                av_new_packet(pkt, sizeof(start_sequence) + sizeof(nal) + len);
                memcpy(pkt->data, start_sequence, sizeof(start_sequence));
                pkt->data[sizeof(start_sequence)] = reconstructed_nal;
                memcpy(pkt->data + sizeof(start_sequence) + sizeof(nal), buf, len);
            } else {
                av_new_packet(pkt, len);
                memcpy(pkt->data, buf, len);
            }
        } else {
            av_log(ctx, AV_LOG_ERROR, "Too short data for FU-A H264 RTP packet\n");
            result = AVERROR_INVALIDDATA;
        }
        break;

    case 30:                   // undefined
    case 31:                   // undefined
    default:
        av_log(ctx, AV_LOG_ERROR, "Undefined type (%d)\n", type);
        result = AVERROR_INVALIDDATA;
        break;
    }

    pkt->stream_index = st->index;

    return result;
}

static PayloadContext *h264_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext) + FF_INPUT_BUFFER_PADDING_SIZE);
}

static void h264_free_context(PayloadContext *data)
{
#ifdef DEBUG
    int ii;

    for (ii = 0; ii < 32; ii++) {
        if (data->packet_types_received[ii])
            av_log(NULL, AV_LOG_DEBUG, "Received %d packets of type %d\n",
                   data->packet_types_received[ii], ii);
    }
#endif

    av_free(data);
}

static av_cold int h264_init(AVFormatContext *s, int st_index,
                             PayloadContext *data)
{
    if (st_index < 0)
        return 0;
    s->streams[st_index]->need_parsing = AVSTREAM_PARSE_FULL;
    return 0;
}

static int parse_h264_sdp_line(AVFormatContext *s, int st_index,
                               PayloadContext *h264_data, const char *line)
{
    AVStream *stream;
    AVCodecContext *codec;
    const char *p = line;

    if (st_index < 0)
        return 0;

    stream = s->streams[st_index];
    codec  = stream->codec;

    if (av_strstart(p, "framesize:", &p)) {
        char buf1[50];
        char *dst = buf1;

        // remove the protocol identifier
        while (*p && *p == ' ')
            p++;                     // strip spaces.
        while (*p && *p != ' ')
            p++;                     // eat protocol identifier
        while (*p && *p == ' ')
            p++;                     // strip trailing spaces.
        while (*p && *p != '-' && (dst - buf1) < sizeof(buf1) - 1)
            *dst++ = *p++;
        *dst = '\0';

        // a='framesize:96 320-240'
        // set our parameters
        codec->width   = atoi(buf1);
        codec->height  = atoi(p + 1); // skip the -
    } else if (av_strstart(p, "fmtp:", &p)) {
        return ff_parse_fmtp(stream, h264_data, p, sdp_parse_fmtp_config_h264);
    } else if (av_strstart(p, "cliprect:", &p)) {
        // could use this if we wanted.
    }

    return 0;
}

RTPDynamicProtocolHandler ff_h264_dynamic_handler = {
    .enc_name         = "H264",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_H264,
    .init             = h264_init,
    .parse_sdp_a_line = parse_h264_sdp_line,
    .alloc            = h264_new_context,
    .free             = h264_free_context,
    .parse_packet     = h264_handle_packet
};
