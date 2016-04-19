/*
 * H.264 MP4 to Annex B byte stream format filter
 * Copyright (c) 2007 Benoit Fouet <benoit.fouet@free.fr>
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

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "bsf.h"

typedef struct H264BSFContext {
    uint8_t  length_size;
    uint8_t  first_idr;
    int      extradata_parsed;
} H264BSFContext;

static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size)
{
    uint32_t offset         = out->size;
    uint8_t nal_header_size = offset ? 3 : 4;
    int err;

    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
    if (err < 0)
        return err;

    if (sps_pps)
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset) {
        AV_WB32(out->data + sps_pps_size, 1);
    } else {
        (out->data + offset + sps_pps_size)[0] =
        (out->data + offset + sps_pps_size)[1] = 0;
        (out->data + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}

static int h264_extradata_to_annexb(AVBSFContext *ctx, const int padding)
{
    uint16_t unit_size;
    uint64_t total_size                 = 0;
    uint8_t *out                        = NULL, unit_nb, sps_done = 0,
             sps_seen                   = 0, pps_seen = 0;
    const uint8_t *extradata            = ctx->par_in->extradata + 4;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size

    if (length_size == 3)
        return AVERROR(EINVAL);

    /* retrieve sps and pps unit(s) */
    unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
    if (!unit_nb) {
        unit_nb = *extradata++; /* number of pps unit(s) */
        sps_done++;

        if (unit_nb)
            pps_seen = 1;
    } else {
        sps_seen = 1;
    }

    while (unit_nb--) {
        int err;

        unit_size   = AV_RB16(extradata);
        total_size += unit_size + 4;
        if (total_size > INT_MAX - padding ||
            extradata + 2 + unit_size > ctx->par_in->extradata +
            ctx->par_in->extradata_size) {
            av_free(out);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;

        if (!unit_nb && !sps_done++) {
            unit_nb = *extradata++; /* number of pps unit(s) */
            if (unit_nb)
                pps_seen = 1;
        }
    }

    if (out)
        memset(out + total_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    if (!sps_seen)
        av_log(ctx, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(ctx, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    av_freep(&ctx->par_out->extradata);
    ctx->par_out->extradata      = out;
    ctx->par_out->extradata_size = total_size;

    return length_size;
}

static int h264_mp4toannexb_init(AVBSFContext *ctx)
{
    H264BSFContext *s = ctx->priv_data;
    int extra_size = ctx->par_in->extradata_size;
    int ret;

    /* retrieve sps and pps NAL units from extradata */
    if (!extra_size                                               ||
        (extra_size >= 3 && AV_RB24(ctx->par_in->extradata) == 1) ||
        (extra_size >= 4 && AV_RB32(ctx->par_in->extradata) == 1)) {
        av_log(ctx, AV_LOG_VERBOSE,
               "The input looks like it is Annex B already\n");
    } else if (extra_size >= 6) {
        ret = h264_extradata_to_annexb(ctx, AV_INPUT_BUFFER_PADDING_SIZE);
        if (ret < 0)
            return ret;

        s->length_size      = ret;
        s->first_idr        = 1;
        s->extradata_parsed = 1;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Invalid extradata size: %d\n", extra_size);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int h264_mp4toannexb_filter(AVBSFContext *ctx, AVPacket *out)
{
    H264BSFContext *s = ctx->priv_data;

    AVPacket *in;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size    = 0;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int            buf_size;
    int ret = 0;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    /* nothing to filter */
    if (!s->extradata_parsed) {
        av_packet_move_ref(out, in);
        av_packet_free(&in);
        return 0;
    }

    buf      = in->data;
    buf_size = in->size;
    buf_end  = in->data + in->size;

    do {
        if (buf + s->length_size > buf_end)
            goto fail;

        if (s->length_size == 1) {
            nal_size = buf[0];
        } else if (s->length_size == 2) {
            nal_size = AV_RB16(buf);
        } else
            nal_size = AV_RB32(buf);

        buf += s->length_size;
        unit_type = *buf & 0x1f;

        if (buf + nal_size > buf_end || nal_size < 0)
            goto fail;

        /* prepend only to the first type 5 NAL unit of an IDR picture */
        if (s->first_idr && unit_type == 5) {
            if (alloc_and_copy(out,
                               ctx->par_out->extradata, ctx->par_out->extradata_size,
                               buf, nal_size) < 0)
                goto fail;
            s->first_idr = 0;
        } else {
            if (alloc_and_copy(out,
                               NULL, 0, buf, nal_size) < 0)
                goto fail;
            if (!s->first_idr && unit_type == 1)
                s->first_idr = 1;
        }

        buf        += nal_size;
        cumul_size += nal_size + s->length_size;
    } while (cumul_size < buf_size);

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);

    return ret;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_h264_mp4toannexb_bsf = {
    .name           = "h264_mp4toannexb",
    .priv_data_size = sizeof(H264BSFContext),
    .init           = h264_mp4toannexb_init,
    .filter         = h264_mp4toannexb_filter,
    .codec_ids      = codec_ids,
};
