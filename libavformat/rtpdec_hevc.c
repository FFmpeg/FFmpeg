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
 *
 */

#include "libavutil/avstring.h"
#include "libavutil/base64.h"

#include "avformat.h"
#include "rtpdec.h"

#define RTP_HEVC_PAYLOAD_HEADER_SIZE  2
#define RTP_HEVC_FU_HEADER_SIZE       1
#define RTP_HEVC_DONL_FIELD_SIZE      2
#define HEVC_SPECIFIED_NAL_UNIT_TYPES 48

/* SDP out-of-band signaling data */
struct PayloadContext {
    int using_donl_field;
    int profile_id;
    uint8_t *sps, *pps, *vps, *sei;
    int sps_size, pps_size, vps_size, sei_size;
};

static const uint8_t start_sequence[] = { 0x00, 0x00, 0x00, 0x01 };

static av_cold PayloadContext *hevc_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static av_cold void hevc_free_context(PayloadContext *data)
{
    av_free(data);
}

static av_cold int hevc_init(AVFormatContext *ctx, int st_index,
                             PayloadContext *data)
{
    av_dlog(ctx, "hevc_init() for stream %d\n", st_index);

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
    /* profile-space: 0-3 */
    /* profile-id: 0-31 */
    if (!strcmp(attr, "profile-id")) {
        hevc_data->profile_id = atoi(value);
        av_dlog(s, "SDP: found profile-id: %d\n", hevc_data->profile_id);
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
    if (!strcmp(attr, "sprop-vps") || !strcmp(attr, "sprop-sps") ||
        !strcmp(attr, "sprop-pps") || !strcmp(attr, "sprop-sei")) {
        uint8_t **data_ptr;
        int *size_ptr;
        if (!strcmp(attr, "sprop-vps")) {
            data_ptr = &hevc_data->vps;
            size_ptr = &hevc_data->vps_size;
        } else if (!strcmp(attr, "sprop-sps")) {
            data_ptr = &hevc_data->sps;
            size_ptr = &hevc_data->sps_size;
        } else if (!strcmp(attr, "sprop-pps")) {
            data_ptr = &hevc_data->pps;
            size_ptr = &hevc_data->pps_size;
        } else if (!strcmp(attr, "sprop-sei")) {
            data_ptr = &hevc_data->sei;
            size_ptr = &hevc_data->sei_size;
        }

        while (*value) {
            char base64packet[1024];
            uint8_t decoded_packet[1024];
            int decoded_packet_size;
            char *dst = base64packet;

            while (*value && *value != ',' &&
                   (dst - base64packet) < sizeof(base64packet) - 1) {
                *dst++ = *value++;
            }
            *dst++ = '\0';

            if (*value == ',')
                value++;

            decoded_packet_size = av_base64_decode(decoded_packet, base64packet,
                                                   sizeof(decoded_packet));
            if (decoded_packet_size > 0) {
                uint8_t *tmp = av_realloc(*data_ptr, decoded_packet_size +
                                          sizeof(start_sequence) + *size_ptr);
                if (!tmp) {
                    av_log(s, AV_LOG_ERROR,
                           "Unable to allocate memory for extradata!\n");
                    return AVERROR(ENOMEM);
                }
                *data_ptr = tmp;

                memcpy(*data_ptr + *size_ptr, start_sequence,
                       sizeof(start_sequence));
                memcpy(*data_ptr + *size_ptr + sizeof(start_sequence),
                       decoded_packet, decoded_packet_size);

                *size_ptr += sizeof(start_sequence) + decoded_packet_size;
            }
        }
    }

    /* max-lsr, max-lps, max-cpb, max-dpb, max-br, max-tr, max-tc */
    /* max-fps */

    /* sprop-max-don-diff: 0-32767

         When the RTP stream depends on one or more other RTP
         streams (in this case tx-mode MUST be equal to "MSM" and
         MSM is in use), this parameter MUST be present and the
         value MUST be greater than 0.
    */
    if (!strcmp(attr, "sprop-max-don-diff")) {
        if (atoi(value) > 0)
            hevc_data->using_donl_field = 1;
        av_dlog(s, "Found sprop-max-don-diff in SDP, DON field usage is: %d\n",
                hevc_data->using_donl_field);
    }

    /* sprop-depack-buf-nalus: 0-32767 */
    if (!strcmp(attr, "sprop-depack-buf-nalus")) {
        if (atoi(value) > 0)
            hevc_data->using_donl_field = 1;
        av_dlog(s, "Found sprop-depack-buf-nalus in SDP, DON field usage is: %d\n",
                hevc_data->using_donl_field);
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

    if (st_index < 0)
        return 0;

    current_stream = ctx->streams[st_index];
    codec  = current_stream->codec;

    if (av_strstart(sdp_line_ptr, "framesize:", &sdp_line_ptr)) {
        char str_video_width[50];
        char *str_video_width_ptr = str_video_width;

        /*
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
        while (*sdp_line_ptr && *sdp_line_ptr != '-' &&
               (str_video_width_ptr - str_video_width) < sizeof(str_video_width) - 1)
            *str_video_width_ptr++ = *sdp_line_ptr++;
        /* add trailing zero byte */
        *str_video_width_ptr = '\0';

        /* determine the width value */
        codec->width   = atoi(str_video_width);
        /* jump beyond the "-" and determine the height value */
        codec->height  = atoi(sdp_line_ptr + 1);
    } else if (av_strstart(sdp_line_ptr, "fmtp:", &sdp_line_ptr)) {
        int ret = ff_parse_fmtp(ctx, current_stream, hevc_data, sdp_line_ptr,
                                hevc_sdp_parse_fmtp_config);
        if (hevc_data->vps_size || hevc_data->sps_size ||
            hevc_data->pps_size || hevc_data->sei_size) {
            av_freep(&codec->extradata);
            codec->extradata_size = hevc_data->vps_size + hevc_data->sps_size +
                                    hevc_data->pps_size + hevc_data->sei_size;
            codec->extradata = av_malloc(codec->extradata_size +
                                         FF_INPUT_BUFFER_PADDING_SIZE);
            if (!codec->extradata) {
                ret = AVERROR(ENOMEM);
                codec->extradata_size = 0;
            } else {
                int pos = 0;
                memcpy(codec->extradata + pos, hevc_data->vps, hevc_data->vps_size);
                pos += hevc_data->vps_size;
                memcpy(codec->extradata + pos, hevc_data->sps, hevc_data->sps_size);
                pos += hevc_data->sps_size;
                memcpy(codec->extradata + pos, hevc_data->pps, hevc_data->pps_size);
                pos += hevc_data->pps_size;
                memcpy(codec->extradata + pos, hevc_data->sei, hevc_data->sei_size);
                pos += hevc_data->sei_size;
                memset(codec->extradata + pos, 0, FF_INPUT_BUFFER_PADDING_SIZE);
            }

            av_freep(&hevc_data->vps);
            av_freep(&hevc_data->sps);
            av_freep(&hevc_data->pps);
            av_freep(&hevc_data->sei);
            hevc_data->vps_size = 0;
            hevc_data->sps_size = 0;
            hevc_data->pps_size = 0;
            hevc_data->sei_size = 0;
        }
        return ret;
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
    int res = 0;

    /* sanity check for size of input packet: 1 byte payload at least */
    if (len < RTP_HEVC_PAYLOAD_HEADER_SIZE + 1) {
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
    if (lid) {
        /* future scalable or 3D video coding extensions */
        avpriv_report_missing_feature(ctx, "Multi-layer HEVC coding\n");
        return AVERROR_PATCHWELCOME;
    }

    /* sanity check for correct temporal ID */
    if (!tid) {
        av_log(ctx, AV_LOG_ERROR, "Illegal temporal ID in RTP/HEVC packet\n");
        return AVERROR_INVALIDDATA;
    }

    /* sanity check for correct NAL unit type */
    if (nal_type > 50) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported (HEVC) NAL type (%d)\n", nal_type);
        return AVERROR_INVALIDDATA;
    }

    switch (nal_type) {
    /* aggregated packets (AP) */
    case 48:
        /* pass the HEVC payload header */
        buf += RTP_HEVC_PAYLOAD_HEADER_SIZE;
        len -= RTP_HEVC_PAYLOAD_HEADER_SIZE;

        /* pass the HEVC DONL field */
        if (rtp_hevc_ctx->using_donl_field) {
            buf += RTP_HEVC_DONL_FIELD_SIZE;
            len -= RTP_HEVC_DONL_FIELD_SIZE;
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
        /* sanity check for size of input packet: 1 byte payload at least */
        if (len < 1) {
            av_log(ctx, AV_LOG_ERROR,
                   "Too short RTP/HEVC packet, got %d bytes of NAL unit type %d\n",
                   len, nal_type);
            return AVERROR_INVALIDDATA;
        }

        /* create A/V packet */
        if ((res = av_new_packet(pkt, sizeof(start_sequence) + len)) < 0)
            return res;
        /* A/V packet: copy start sequence */
        memcpy(pkt->data, start_sequence, sizeof(start_sequence));
        /* A/V packet: copy NAL unit data */
        memcpy(pkt->data + sizeof(start_sequence), buf, len);

        break;
    /* fragmentation unit (FU) */
    case 49:
        /* pass the HEVC payload header */
        buf += RTP_HEVC_PAYLOAD_HEADER_SIZE;
        len -= RTP_HEVC_PAYLOAD_HEADER_SIZE;

        /*
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
        last_fragment  = buf[0] & 0x40;
        fu_type        = buf[0] & 0x3f;

        /* pass the HEVC FU header */
        buf += RTP_HEVC_FU_HEADER_SIZE;
        len -= RTP_HEVC_FU_HEADER_SIZE;

        /* pass the HEVC DONL field */
        if (rtp_hevc_ctx->using_donl_field) {
            buf += RTP_HEVC_DONL_FIELD_SIZE;
            len -= RTP_HEVC_DONL_FIELD_SIZE;
        }

        av_dlog(ctx, " FU type %d with %d bytes\n", fu_type, len);

        /* sanity check for size of input packet: 1 byte payload at least */
        if (len > 0) {
            new_nal_header[0] = (rtp_pl[0] & 0x81) | (fu_type << 1);
            new_nal_header[1] = rtp_pl[1];

            /* start fragment vs. subsequent fragments */
            if (first_fragment) {
                if (!last_fragment) {
                    /* create A/V packet which is big enough */
                    if ((res = av_new_packet(pkt, sizeof(start_sequence) + sizeof(new_nal_header) + len)) < 0)
                        return res;
                    /* A/V packet: copy start sequence */
                    memcpy(pkt->data, start_sequence, sizeof(start_sequence));
                    /* A/V packet: copy new NAL header */
                    memcpy(pkt->data + sizeof(start_sequence), new_nal_header, sizeof(new_nal_header));
                    /* A/V packet: copy NAL unit data */
                    memcpy(pkt->data + sizeof(start_sequence) + sizeof(new_nal_header), buf, len);
                } else {
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
            if (len < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Too short RTP/HEVC packet, got %d bytes of NAL unit type %d\n",
                       len, nal_type);
                res = AVERROR_INVALIDDATA;
            } else {
                res = AVERROR(EAGAIN);
            }
        }

        break;
    /* PACI packet */
    case 50:
        /* Temporal scalability control information (TSCI) */
        avpriv_report_missing_feature(ctx, "PACI packets for RTP/HEVC\n");
        res = AVERROR_PATCHWELCOME;
        break;
    }

    pkt->stream_index = st->index;

    return res;
}

RTPDynamicProtocolHandler ff_hevc_dynamic_handler = {
    .enc_name         = "H265",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_HEVC,
    .init             = hevc_init,
    .parse_sdp_a_line = hevc_parse_sdp_line,
    .alloc            = hevc_new_context,
    .free             = hevc_free_context,
    .parse_packet     = hevc_handle_packet
};
