/*
 * RTP parser for HEVC/H.265 payload format (draft version 6)
 * Copyright (c) 2014 Thomas Volkert <thomas@homer-conferencing.com>
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

#include "libavutil/avstring.h"
#include "avformat.h"
#include "rtpdec.h"

#define RTP_HEVC_PAYLOAD_HEADER_SIZE                    2
#define RTP_HEVC_FU_HEADER_SIZE                         1
#define RTP_HEVC_DONL_FIELDS_SIZE                       2
#define HEVC_SPECIFIED_NAL_UNIT_TYPES                   48

struct PayloadContext {
    /* SDP out-of-band signaling data */
    int using_donl_field;
    int profile_id;
    /* debugging */
#ifdef DEBUG
    int packets_received[HEVC_SPECIFIED_NAL_UNIT_TYPES];
#endif
};

#ifdef DEBUG
#define COUNT_HEVC_NAL_TYPE(data, nal_type) if (nal_type < HEVC_SPECIFIED_NAL_UNIT_TYPES) data->packets_received[(nal_type)]++
#else
#define COUNT_HEVC_NAL_TYPE(data, nal_type) do { } while (0)
#endif

static const uint8_t start_sequence[] = { 0x00, 0x00, 0x00, 0x01 };

static PayloadContext *hevc_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static av_cold void hevc_free_context(PayloadContext *data)
{
#ifdef DEBUG
    int i;

    for (i = 0; i < HEVC_SPECIFIED_NAL_UNIT_TYPES; i++) {
        if (data->packets_received[i])
            av_log(NULL, AV_LOG_DEBUG, "Received %d packets of NAL unit type %d\n",
                data->packets_received[i], i);
    }
#endif

    av_free(data);
}

static av_cold int hevc_init(AVFormatContext *ctx, int st_index,
                             PayloadContext *data)
{
#ifdef DEBUG
    av_log(ctx, AV_LOG_DEBUG, "hevc_init() for stream %d\n", st_index);
#endif

    if (st_index < 0)
        return 0;

    ctx->streams[st_index]->need_parsing = AVSTREAM_PARSE_FULL;

    return 0;
}

static av_cold int hevc_sdp_parse_fmtp_config(AVFormatContext *s,
                                      AVStream *stream,
                                      PayloadContext *hevc_data,
                                      char *attr, char *value)
{

#ifdef DEBUG
    av_log(s, AV_LOG_DEBUG, "SDP: fmtp value %s is %s\n", attr, value);
#endif

    /* profile-space: 0-3 */
    /* profile-id: 0-31 */
    if (!strcmp(attr, "profile-id")) {
        hevc_data->profile_id = atoi(value);

#ifdef DEBUG
        av_log(s, AV_LOG_DEBUG, "SDP: found profile-id: %d\n", hevc_data->profile_id);
#endif
    }

    /* tier-flag: 0-1 */
    /* level-id: 0-255 */
    /* interop-constraints: [base16] */
    /* profile-compatibility-indicator: [base16] */
    /* sprop-sub-layer-id: 0-6, defines highest possible value for TID, default: 6 */
    /* recv-sub-layer-id: 0-6 */
    /* max-recv-level-id: 0-255 */
    /* tx-mode: MSM,SSM */
    /* sprop-vps: [base64] */
    /* sprop-sps: [base64] */
    /* sprop-pps: [base64] */
    /* sprop-sei: [base64] */
    /* max-lsr, max-lps, max-cpb, max-dpb, max-br, max-tr, max-tc */
    /* max-fps */

    /* sprop-max-don-diff: 0-32767

         When the RTP stream depends on one or more other RTP
         streams (in this case tx-mode MUST be equal to "MSM" and
         MSM is in use), this parameter MUST be present and the
         value MUST be greater than 0.
    */
    if (!strcmp(attr, "sprop-max-don-diff")) {
        if(atoi(value) > 0)
            hevc_data->using_donl_field = 1;

#ifdef DEBUG
        av_log(s, AV_LOG_DEBUG, "SDP: found sprop-max-don-diff in SDP, DON field usage is: %d\n", hevc_data->using_donl_field);
#endif
    }

    /* sprop-depack-buf-nalus: 0-32767 */
    if (!strcmp(attr, "sprop-depack-buf-nalus")) {
        if(atoi(value) > 0)
            hevc_data->using_donl_field = 1;

#ifdef DEBUG
        av_log(s, AV_LOG_DEBUG, "SDP: found sprop-depack-buf-nalus in SDP, DON field usage is: %d\n", hevc_data->using_donl_field);
#endif
    }

    /* sprop-depack-buf-bytes: 0-4294967295 */
    /* depack-buf-cap */
    /* sprop-segmentation-id: 0-3 */
    /* sprop-spatial-segmentation-idc: [base16] */
    /* dec-parallel-ca: */
    /* include-dph */

    return 0;
}

static av_cold int hevc_parse_sdp_line(AVFormatContext *ctx, int st_index,
                               PayloadContext *hevc_data, const char *line)
{
    AVStream *current_stream;
    AVCodecContext *codec;
    const char *sdp_line_ptr = line;

#ifdef DEBUG
    av_log(ctx, AV_LOG_DEBUG, "parse_hevc_sdp_line() got SDP line %s\n", line);
#endif

    if (st_index < 0)
        return 0;

    current_stream = ctx->streams[st_index];
    codec  = current_stream->codec;

    if (av_strstart(sdp_line_ptr, "framesize:", &sdp_line_ptr)) {
        char str_video_width[50];
        char *str_video_width_ptr = str_video_width;

        /**
         * parse "a=framesize:96 320-240"
         */
        /* ignore spaces */
        while (*sdp_line_ptr && *sdp_line_ptr == ' ')
            sdp_line_ptr++;
        /* ignore RTP payload ID */
        while (*sdp_line_ptr && *sdp_line_ptr != ' ')
            sdp_line_ptr++;
        /* ignore spaces */
        while (*sdp_line_ptr && *sdp_line_ptr == ' ')
            sdp_line_ptr++;
        /* extract the actual video resolution description */
        while (*sdp_line_ptr && *sdp_line_ptr != '-' && (str_video_width_ptr - str_video_width) < sizeof(str_video_width) - 1)
            *str_video_width_ptr++ = *sdp_line_ptr++;
        /* add trailing zero byte */
        *str_video_width_ptr = '\0';

        /* determine the width value */
        codec->width   = atoi(str_video_width);
        // jump beyond the "-" and determine the height value
        codec->height  = atoi(sdp_line_ptr + 1);
    } else if (av_strstart(sdp_line_ptr, "fmtp:", &sdp_line_ptr)) {
        return ff_parse_fmtp(ctx, current_stream, hevc_data, sdp_line_ptr, hevc_sdp_parse_fmtp_config);
    } else if (av_strstart(sdp_line_ptr, "cliprect:", &sdp_line_ptr)) {
        // could use this if we wanted.
    }

    return 0;
}

static int hevc_handle_packet(AVFormatContext *ctx, PayloadContext *rtp_hevc_ctx,
                              AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                              const uint8_t *buf, int len, uint16_t seq,
                              int flags)
{
    const uint8_t *rtp_pl = buf;
    int tid, lid, nal_type;
    int first_fragment, last_fragment, fu_type;
    uint8_t new_nal_header[2];
    int res=0;

    /* sanity check for size of input packet */
    if (len < 3 /* 2 bytes header and 1 byte payload at least */) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/HEVC packet, got %d bytes\n", len);
        return AVERROR_INVALIDDATA;
    }

    /*
      decode the HEVC payload header according to section 4 of draft version 6:

         0                   1
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |F|   Type    |  LayerId  | TID |
        +-------------+-----------------+

           Forbidden zero (F): 1 bit
           NAL unit type (Type): 6 bits
           NUH layer ID (LayerId): 6 bits
           NUH temporal ID plus 1 (TID): 3 bits
    */
    nal_type =  (buf[0] >> 1) & 0x3f;
    lid  = ((buf[0] << 5) & 0x20) | ((buf[1] >> 3) & 0x1f);
    tid  =   buf[1] & 0x07;

    /* sanity check for correct layer ID */
    if(lid){
        /* future scalable or 3D video coding extensions */
        av_log(ctx, AV_LOG_ERROR, "Multi-layer HEVC coding is not supported yet, a patch is welcome\n");
        return AVERROR_PATCHWELCOME;
    }

    /* sanity check for correct temporal ID */
    if(!tid){
        av_log(ctx, AV_LOG_ERROR, "Illegal temporal ID in RTP/HEVC packet\n");
        return AVERROR_INVALIDDATA;
    }

    /* sanity check for correct NAL unit type */
    if (nal_type > 50){
        av_log(ctx, AV_LOG_ERROR, "Unsupported (HEVC) NAL type (%d)\n", nal_type);
        return AVERROR_INVALIDDATA;
    }

#ifdef DEBUG
    av_log(ctx, AV_LOG_DEBUG, "hevc_handle_packet() got NAL type %d with %d bytes\n", nal_type, len);
#endif

    switch(nal_type)
    {
    /* aggregated packets (AP) */
    case 48:
        /* pass the HEVC payload header */
        buf += RTP_HEVC_PAYLOAD_HEADER_SIZE;
        len -= RTP_HEVC_PAYLOAD_HEADER_SIZE;

        /* pass the HEVC DONL fields */
        if(rtp_hevc_ctx->using_donl_field){
            buf += RTP_HEVC_DONL_FIELDS_SIZE;
            len -= RTP_HEVC_DONL_FIELDS_SIZE;
        }

        /* fall-through */
    /* video parameter set (VPS) */
    case 32:
    /* sequence parameter set (SPS) */
    case 33:
    /* picture parameter set (PPS) */
    case 34:
    /*  supplemental enhancement information (SEI) */
    case 39:
    /* single NAL unit packet */
    default:
        /* create A/V packet */
        if ((res = av_new_packet(pkt, sizeof(start_sequence) + len)) < 0)
            return res;
        /* A/V packet: copy start sequence */
        memcpy(pkt->data, start_sequence, sizeof(start_sequence));
        /* A/V packet: copy NAL unit data */
        memcpy(pkt->data + sizeof(start_sequence), buf, len);

        COUNT_HEVC_NAL_TYPE(rtp_hevc_ctx, nal_type);

        break;
    /* fragmentation unit (FU) */
    case 49:
        /* pass the HEVC payload header */
        buf += RTP_HEVC_PAYLOAD_HEADER_SIZE;
        len -= RTP_HEVC_PAYLOAD_HEADER_SIZE;

        /**
             decode the FU header

              0 1 2 3 4 5 6 7
             +-+-+-+-+-+-+-+-+
             |S|E|  FuType   |
             +---------------+

                Start fragment (S): 1 bit
                End fragment (E): 1 bit
                FuType: 6 bits
        */
        first_fragment = buf[0] & 0x80;
        last_fragment = buf[0] & 0x40;
        fu_type = buf[0] & 0x3f;

        /* pass the HEVC FU header */
        buf += RTP_HEVC_FU_HEADER_SIZE;
        len -= RTP_HEVC_FU_HEADER_SIZE;

        /* pass the HEVC DONL fields */
        if(rtp_hevc_ctx->using_donl_field){
            buf += RTP_HEVC_DONL_FIELDS_SIZE;
            len -= RTP_HEVC_DONL_FIELDS_SIZE;
        }

#ifdef DEBUG
        av_log(ctx, AV_LOG_DEBUG, " FU type %d with %d bytes\n", fu_type, len);
#endif

        if (len > 0) {
            new_nal_header[0] = (rtp_pl[0] & 0x81) | (fu_type << 1);
            new_nal_header[1] = rtp_pl[1];

            /* start fragment vs. subsequent fragments */
            if (first_fragment) {
                if(!last_fragment){
                    /* create A/V packet which is big enough */
                    if ((res = av_new_packet(pkt, sizeof(start_sequence) + sizeof(new_nal_header) + len)) < 0)
                        return res;
                    /* A/V packet: copy start sequence */
                    memcpy(pkt->data, start_sequence, sizeof(start_sequence));
                    /* A/V packet: copy new NAL header */
                    memcpy(pkt->data + sizeof(start_sequence), new_nal_header, sizeof(new_nal_header));
                    /* A/V packet: copy NAL unit data */
                    memcpy(pkt->data + sizeof(start_sequence) + sizeof(new_nal_header), buf, len);
                }else{
                    av_log(ctx, AV_LOG_ERROR, "Illegal combination of S and E bit in RTP/HEVC packet\n");
                    res = AVERROR_INVALIDDATA;
                }
            } else {
                /* create A/V packet */
                if ((res = av_new_packet(pkt, len)) < 0)
                    return res;
                /* A/V packet: copy NAL unit data */
                memcpy(pkt->data, buf, len);
            }
        } else {
            av_log(ctx, AV_LOG_ERROR, "Too short data for (HEVC) NAL unit, got %d bytes\n", len);
            res = AVERROR_INVALIDDATA;
        }

        if(!res){
            COUNT_HEVC_NAL_TYPE(rtp_hevc_ctx, fu_type);
        }

        break;
    /* PACI packet */
    case 50:
        /* Temporal scalability control information (TSCI) */
        av_log(ctx, AV_LOG_ERROR, "PACI packets for RTP/HEVC are not supported yet, a patch is welcome\n");
        res = AVERROR_PATCHWELCOME;
        break;
    }

    pkt->stream_index = st->index;

    return res;
}

RTPDynamicProtocolHandler ff_hevc_dynamic_handler = {
    .enc_name         = "HEVC",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_HEVC,
    .init             = hevc_init,
    .parse_sdp_a_line = hevc_parse_sdp_line,
    .alloc            = hevc_new_context,
    .free             = hevc_free_context,
    .parse_packet     = hevc_handle_packet
};

RTPDynamicProtocolHandler ff_h265_dynamic_handler = {
    .enc_name         = "H265",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_H265,
    .init             = hevc_init,
    .parse_sdp_a_line = hevc_parse_sdp_line,
    .alloc            = hevc_new_context,
    .free             = hevc_free_context,
    .parse_packet     = hevc_handle_packet
};
