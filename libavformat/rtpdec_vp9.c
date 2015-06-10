/*
 * RTP parser for VP9 payload format (draft version 0) - experimental
 * Copyright (c) 2015 Thomas Volkert <thomas@homer-conferencing.com>
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

#include "libavutil/intreadwrite.h"

#include "avio_internal.h"
#include "rtpdec_formats.h"

#define RTP_VP9_DESC_REQUIRED_SIZE 1

struct PayloadContext {
    AVIOContext *buf;
    uint32_t     timestamp;
};

static av_cold int vp9_init(AVFormatContext *ctx, int st_index,
                            PayloadContext *data)
{
    av_log(ctx, AV_LOG_WARNING,
           "RTP/VP9 support is still experimental\n");

    return 0;
}

static int vp9_handle_packet(AVFormatContext *ctx, PayloadContext *rtp_vp9_ctx,
                             AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, uint16_t seq,
                             int flags)
{
    int has_pic_id, has_layer_idc, has_ref_idc, has_ss_data, has_su_data;
    av_unused int pic_id = 0, non_key_frame = 0;
    av_unused int layer_temporal = -1, layer_spatial = -1, layer_quality = -1;
    int ref_fields = 0, has_ref_field_ext_pic_id = 0;
    int first_fragment, last_fragment;
    int rtp_m;
    int res = 0;

    /* drop data of previous packets in case of non-continuous (lossy) packet stream */
    if (rtp_vp9_ctx->buf && rtp_vp9_ctx->timestamp != *timestamp)
        ffio_free_dyn_buf(&rtp_vp9_ctx->buf);

    /* sanity check for size of input packet: 1 byte payload at least */
    if (len < RTP_VP9_DESC_REQUIRED_SIZE + 1) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/VP9 packet, got %d bytes\n", len);
        return AVERROR_INVALIDDATA;
    }

    /*
     *     decode the required VP9 payload descriptor according to section 4.2 of the spec.:
     *
     *      0 1 2 3 4 5 6 7
     *     +-+-+-+-+-+-+-+-+
     *     |I|L|F|B|E|V|U|-| (REQUIRED)
     *     +-+-+-+-+-+-+-+-+
     *
     *     I: PictureID present
     *     L: Layer indices present
     *     F: Reference indices present
     *     B: Start of VP9 frame
     *     E: End of picture
     *     V: Scalability Structure (SS) present
     *     U: Scalability Structure Update (SU) present
     */
    has_pic_id     = !!(buf[0] & 0x80);
    has_layer_idc  = !!(buf[0] & 0x40);
    has_ref_idc    = !!(buf[0] & 0x20);
    first_fragment = !!(buf[0] & 0x10);
    last_fragment  = !!(buf[0] & 0x08);
    has_ss_data    = !!(buf[0] & 0x04);
    has_su_data    = !!(buf[0] & 0x02);

    rtp_m = !!(flags & RTP_FLAG_MARKER);

    /* sanity check for markers: B should always be equal to the RTP M marker */
    if (last_fragment != rtp_m) {
        av_log(ctx, AV_LOG_ERROR, "Invalid combination of B and M marker (%d != %d)\n", last_fragment, rtp_m);
        return AVERROR_INVALIDDATA;
    }

    /* pass the extensions field */
    buf += RTP_VP9_DESC_REQUIRED_SIZE;
    len -= RTP_VP9_DESC_REQUIRED_SIZE;

    /*
     *         decode the 1-byte/2-byte picture ID:
     *
     *          0 1 2 3 4 5 6 7
     *         +-+-+-+-+-+-+-+-+
     *   I:    |M|PICTURE ID   | (RECOMMENDED)
     *         +-+-+-+-+-+-+-+-+
     *   M:    | EXTENDED PID  | (RECOMMENDED)
     *         +-+-+-+-+-+-+-+-+
     *
     *   M: The most significant bit of the first octet is an extension flag.
     *   PictureID:  8 or 16 bits including the M bit.
     */
    if (has_pic_id) {
        /* check for 1-byte or 2-byte picture index */
        if (buf[0] & 0x80) {
            if (len < 2) {
                av_log(ctx, AV_LOG_ERROR, "Too short RTP/VP9 packet\n");
                return AVERROR_INVALIDDATA;
            }
            pic_id = AV_RB16(buf) & 0x7fff;
            buf += 2;
            len -= 2;
        } else {
            pic_id = buf[0] & 0x7f;
            buf++;
            len--;
        }
    }

    /*
     *         decode layer indices
     *
     *          0 1 2 3 4 5 6 7
     *         +-+-+-+-+-+-+-+-+
     *   L:    | T | S | Q | R | (CONDITIONALLY RECOMMENDED)
     *         +-+-+-+-+-+-+-+-+
     *
     *   T, S and Q are 2-bit indices for temporal, spatial, and quality layers.
     *   If "F" is set in the initial octet, R is 2 bits representing the number
     *   of reference fields this frame refers to.
     */
    if (has_layer_idc) {
        if (len < 1) {
            av_log(ctx, AV_LOG_ERROR, "Too short RTP/VP9 packet\n");
            return AVERROR_INVALIDDATA;
        }
        layer_temporal = buf[0] & 0xC0;
        layer_spatial  = buf[0] & 0x30;
        layer_quality  = buf[0] & 0x0C;
        if (has_ref_idc) {
            ref_fields = buf[0] & 0x03;
            if (ref_fields)
                non_key_frame = 1;
        }
        buf++;
        len--;
    }

    /*
     *         decode the reference fields
     *
     *          0 1 2 3 4 5 6 7
     *         +-+-+-+-+-+-+-+-+              -\
     *   F:    | PID |X| RS| RQ| (OPTIONAL)    .
     *         +-+-+-+-+-+-+-+-+               . - R times
     *   X:    | EXTENDED PID  | (OPTIONAL)    .
     *         +-+-+-+-+-+-+-+-+              -/
     *
     *   PID:  The relative Picture ID referred to by this frame.
     *   RS and RQ:  The spatial and quality layer IDs.
     *   X: 1 if this layer index has an extended relative Picture ID.
     */
    if (has_ref_idc) {
        while (ref_fields) {
            if (len < 1) {
                av_log(ctx, AV_LOG_ERROR, "Too short RTP/VP9 packet\n");
                return AVERROR_INVALIDDATA;
            }

            has_ref_field_ext_pic_id = buf[0] & 0x10;

            /* pass ref. field */
            if (has_ref_field_ext_pic_id) {
                if (len < 2) {
                    av_log(ctx, AV_LOG_ERROR, "Too short RTP/VP9 packet\n");
                    return AVERROR_INVALIDDATA;
                }

                /* ignore ref. data */

                buf += 2;
                len -= 2;
            } else {

                /* ignore ref. data */

                buf++;
                len--;
            }
            ref_fields--;
        }
    }

    /*
     *         decode the scalability structure (SS)
     *
     *          0 1 2 3 4 5 6 7
     *         +-+-+-+-+-+-+-+-+
     *   V:    | PATTERN LENGTH|
     *         +-+-+-+-+-+-+-+-+                           -\
     *         | T | S | Q | R | (OPTIONAL)                 .
     *         +-+-+-+-+-+-+-+-+              -\            .
     *         | PID |X| RS| RQ| (OPTIONAL)    .            . - PAT. LEN. times
     *         +-+-+-+-+-+-+-+-+               . - R times  .
     *   X:    | EXTENDED PID  | (OPTIONAL)    .            .
     *         +-+-+-+-+-+-+-+-+              -/           -/
     *
     *   PID:  The relative Picture ID referred to by this frame.
     *   RS and RQ:  The spatial and quality layer IDs.
     *   X: 1 if this layer index has an extended relative Picture ID.
     */
    if (has_ss_data) {
        avpriv_report_missing_feature(ctx, "VP9 scalability structure data");
        return AVERROR(ENOSYS);
    }

    /*
     * decode the scalability update structure (SU)
     *
     *  spec. is tbd
     */
    if (has_su_data) {
        avpriv_report_missing_feature(ctx, "VP9 scalability update structure data");
        return AVERROR(ENOSYS);
    }

    /*
     * decode the VP9 payload header
     *
     *  spec. is tbd
     */
    //XXX: implement when specified

    /* sanity check: 1 byte payload as minimum */
    if (len < 1) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/VP9 packet\n");
        return AVERROR_INVALIDDATA;
    }

    /* start frame buffering with new dynamic buffer */
    if (!rtp_vp9_ctx->buf) {
        /* sanity check: a new frame should have started */
        if (first_fragment) {
            res = avio_open_dyn_buf(&rtp_vp9_ctx->buf);
            if (res < 0)
                return res;
            /* update the timestamp in the frame packet with the one from the RTP packet */
            rtp_vp9_ctx->timestamp = *timestamp;
        } else {
            /* frame not started yet, need more packets */
            return AVERROR(EAGAIN);
        }
    }

    /* write the fragment to the dyn. buffer */
    avio_write(rtp_vp9_ctx->buf, buf, len);

    /* do we need more fragments? */
    if (!last_fragment)
        return AVERROR(EAGAIN);

    /* close frame buffering and create resulting A/V packet */
    res = ff_rtp_finalize_packet(pkt, &rtp_vp9_ctx->buf, st->index);
    if (res < 0)
        return res;

    return 0;
}

RTPDynamicProtocolHandler ff_vp9_dynamic_handler = {
    .enc_name         = "VP9",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_VP9,
    .priv_data_size   = sizeof(PayloadContext),
    .init             = vp9_init,
    .parse_packet     = vp9_handle_packet
};
