/*
 * RTP parser for HEVC/H.265 payload format (draft version 6)
 * Copyright (c) 2014 Thomas Volkert <thomas@homer-conferencing.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/base64.h"

#include "avformat.h"
#include "rtpdec.h"
#include "rtpdec_formats.h"

#define RTP_HEVC_PAYLOAD_HEADER_SIZE  2
#define RTP_HEVC_FU_HEADER_SIZE       1
#define RTP_HEVC_DONL_FIELD_SIZE      2
#define RTP_HEVC_DOND_FIELD_SIZE      1
#define HEVC_SPECIFIED_NAL_UNIT_TYPES 48

/* SDP out-of-band signaling data */
struct PayloadContext {
    int using_donl_field;
    int profile_id;
    uint8_t *sps, *pps, *vps, *sei;
    int sps_size, pps_size, vps_size, sei_size;
};

static const uint8_t start_sequence[] = { 0x00, 0x00, 0x00, 0x01 };

static av_cold int hevc_sdp_parse_fmtp_config(AVFormatContext *s,
                                              AVStream *stream,
                                              PayloadContext *hevc_data,
                                              const char *attr, const char *value)
{
    /* profile-space: 0-3 */
    /* profile-id: 0-31 */
    if (!strcmp(attr, "profile-id")) {
        hevc_data->profile_id = atoi(value);
        av_log(s, AV_LOG_TRACE, "SDP: found profile-id: %d\n", hevc_data->profile_id);
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
        uint8_t **data_ptr = NULL;
        int *size_ptr = NULL;
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

        ff_h264_parse_sprop_parameter_sets(s, data_ptr,
                                           size_ptr, value);
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
        av_log(s, AV_LOG_TRACE, "Found sprop-max-don-diff in SDP, DON field usage is: %d\n",
                hevc_data->using_donl_field);
    }

    /* sprop-depack-buf-nalus: 0-32767 */
    if (!strcmp(attr, "sprop-depack-buf-nalus")) {
        if (atoi(value) > 0)
            hevc_data->using_donl_field = 1;
        av_log(s, AV_LOG_TRACE, "Found sprop-depack-buf-nalus in SDP, DON field usage is: %d\n",
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
    AVCodecParameters *par;
    const char *sdp_line_ptr = line;

    if (st_index < 0)
        return 0;

    current_stream = ctx->streams[st_index];
    par  = current_stream->codecpar;

    if (av_strstart(sdp_line_ptr, "framesize:", &sdp_line_ptr)) {
        ff_h264_parse_framesize(par, sdp_line_ptr);
    } else if (av_strstart(sdp_line_ptr, "fmtp:", &sdp_line_ptr)) {
        int ret = ff_parse_fmtp(ctx, current_stream, hevc_data, sdp_line_ptr,
                                hevc_sdp_parse_fmtp_config);
        if (hevc_data->vps_size || hevc_data->sps_size ||
            hevc_data->pps_size || hevc_data->sei_size) {
            av_freep(&par->extradata);
            par->extradata_size = hevc_data->vps_size + hevc_data->sps_size +
                                  hevc_data->pps_size + hevc_data->sei_size;
            par->extradata = av_malloc(par->extradata_size +
                                       AV_INPUT_BUFFER_PADDING_SIZE);
            if (!par->extradata) {
                ret = AVERROR(ENOMEM);
                par->extradata_size = 0;
            } else {
                int pos = 0;
                memcpy(par->extradata + pos, hevc_data->vps, hevc_data->vps_size);
                pos += hevc_data->vps_size;
                memcpy(par->extradata + pos, hevc_data->sps, hevc_data->sps_size);
                pos += hevc_data->sps_size;
                memcpy(par->extradata + pos, hevc_data->pps, hevc_data->pps_size);
                pos += hevc_data->pps_size;
                memcpy(par->extradata + pos, hevc_data->sei, hevc_data->sei_size);
                pos += hevc_data->sei_size;
                memset(par->extradata + pos, 0, AV_INPUT_BUFFER_PADDING_SIZE);
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
     * decode the HEVC payload header according to section 4 of draft version 6:
     *
     *    0                   1
     *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
     *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *   |F|   Type    |  LayerId  | TID |
     *   +-------------+-----------------+
     *
     *      Forbidden zero (F): 1 bit
     *      NAL unit type (Type): 6 bits
     *      NUH layer ID (LayerId): 6 bits
     *      NUH temporal ID plus 1 (TID): 3 bits
     */
    nal_type =  (buf[0] >> 1) & 0x3f;
    lid  = ((buf[0] << 5) & 0x20) | ((buf[1] >> 3) & 0x1f);
    tid  =   buf[1] & 0x07;

    /* sanity check for correct layer ID */
    if (lid) {
        /* future scalable or 3D video coding extensions */
        avpriv_report_missing_feature(ctx, "Multi-layer HEVC coding");
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

        break;
    /* aggregated packet (AP) - with two or more NAL units */
    case 48:
        /* pass the HEVC payload header */
        buf += RTP_HEVC_PAYLOAD_HEADER_SIZE;
        len -= RTP_HEVC_PAYLOAD_HEADER_SIZE;

        /* pass the HEVC DONL field */
        if (rtp_hevc_ctx->using_donl_field) {
            buf += RTP_HEVC_DONL_FIELD_SIZE;
            len -= RTP_HEVC_DONL_FIELD_SIZE;
        }

        res = ff_h264_handle_aggregated_packet(ctx, pkt, buf, len,
                                               rtp_hevc_ctx->using_donl_field ?
                                               RTP_HEVC_DOND_FIELD_SIZE : 0,
                                               NULL, 0);
        if (res < 0)
            return res;
        break;
    /* fragmentation unit (FU) */
    case 49:
        /* pass the HEVC payload header */
        buf += RTP_HEVC_PAYLOAD_HEADER_SIZE;
        len -= RTP_HEVC_PAYLOAD_HEADER_SIZE;

        /*
         *    decode the FU header
         *
         *     0 1 2 3 4 5 6 7
         *    +-+-+-+-+-+-+-+-+
         *    |S|E|  FuType   |
         *    +---------------+
         *
         *       Start fragment (S): 1 bit
         *       End fragment (E): 1 bit
         *       FuType: 6 bits
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

        av_log(ctx, AV_LOG_TRACE, " FU type %d with %d bytes\n", fu_type, len);

        if (len <= 0) {
            /* sanity check for size of input packet: 1 byte payload at least */
            av_log(ctx, AV_LOG_ERROR,
                   "Too short RTP/HEVC packet, got %d bytes of NAL unit type %d\n",
                   len, nal_type);
            return AVERROR_INVALIDDATA;
        }

        if (first_fragment && last_fragment) {
            av_log(ctx, AV_LOG_ERROR, "Illegal combination of S and E bit in RTP/HEVC packet\n");
            return AVERROR_INVALIDDATA;
        }

        new_nal_header[0] = (rtp_pl[0] & 0x81) | (fu_type << 1);
        new_nal_header[1] = rtp_pl[1];

        res = ff_h264_handle_frag_packet(pkt, buf, len, first_fragment,
                                         new_nal_header, sizeof(new_nal_header));

        break;
    /* PACI packet */
    case 50:
        /* Temporal scalability control information (TSCI) */
        avpriv_report_missing_feature(ctx, "PACI packets for RTP/HEVC");
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
    .need_parsing     = AVSTREAM_PARSE_FULL,
    .priv_data_size   = sizeof(PayloadContext),
    .parse_sdp_a_line = hevc_parse_sdp_line,
    .parse_packet     = hevc_handle_packet,
};
