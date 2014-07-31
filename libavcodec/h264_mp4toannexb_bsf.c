/*
 * H.264 MP4 to Annex B byte stream format filter
 * Copyright (c) 2007 Benoit Fouet <benoit.fouet@free.fr>
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

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avcodec.h"

typedef struct H264BSFContext {
    uint8_t  length_size;
    uint8_t  first_idr;
    uint8_t  idr_sps_pps_seen;
    int      extradata_parsed;
} H264BSFContext;

static int alloc_and_copy(uint8_t **poutbuf, int *poutbuf_size,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size)
{
    uint32_t offset         = *poutbuf_size;
    uint8_t nal_header_size = offset ? 3 : 4;
    int err;

    *poutbuf_size += sps_pps_size + in_size + nal_header_size;
    if ((err = av_reallocp(poutbuf,
                           *poutbuf_size + FF_INPUT_BUFFER_PADDING_SIZE)) < 0) {
        *poutbuf_size = 0;
        return err;
    }
    if (sps_pps)
        memcpy(*poutbuf + offset, sps_pps, sps_pps_size);
    memcpy(*poutbuf + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset) {
        AV_WB32(*poutbuf + sps_pps_size, 1);
    } else {
        (*poutbuf + offset + sps_pps_size)[0] =
        (*poutbuf + offset + sps_pps_size)[1] = 0;
        (*poutbuf + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}

static int h264_extradata_to_annexb(AVCodecContext *avctx, const int padding)
{
    uint16_t unit_size;
    uint64_t total_size                 = 0;
    uint8_t *out                        = NULL, unit_nb, sps_done = 0,
             sps_seen                   = 0, pps_seen = 0;
    const uint8_t *extradata            = avctx->extradata + 4;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size

    /* retrieve sps and pps unit(s) */
    unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
    if (!unit_nb) {
        goto pps;
    } else {
        sps_seen = 1;
    }

    while (unit_nb--) {
        int err;

        unit_size   = AV_RB16(extradata);
        total_size += unit_size + 4;
        if (total_size > INT_MAX - padding) {
            av_log(avctx, AV_LOG_ERROR,
                   "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if (extradata + 2 + unit_size > avctx->extradata + avctx->extradata_size) {
            av_log(avctx, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                   "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;
pps:
        if (!unit_nb && !sps_done++) {
            unit_nb = *extradata++; /* number of pps unit(s) */
            if (unit_nb)
                pps_seen = 1;
        }
    }

    if (out)
        memset(out + total_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    if (!sps_seen)
        av_log(avctx, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(avctx, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    av_free(avctx->extradata);
    avctx->extradata      = out;
    avctx->extradata_size = total_size;

    return length_size;
}

static int h264_mp4toannexb_filter(AVBitStreamFilterContext *bsfc,
                                   AVCodecContext *avctx, const char *args,
                                   uint8_t **poutbuf, int *poutbuf_size,
                                   const uint8_t *buf, int buf_size,
                                   int keyframe)
{
    H264BSFContext *ctx = bsfc->priv_data;
    int i;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size    = 0;
    const uint8_t *buf_end = buf + buf_size;
    int ret = 0;

    /* nothing to filter */
    if (!avctx->extradata || avctx->extradata_size < 6) {
        *poutbuf      = (uint8_t *)buf;
        *poutbuf_size = buf_size;
        return 0;
    }

    /* retrieve sps and pps NAL units from extradata */
    if (!ctx->extradata_parsed) {
        ret = h264_extradata_to_annexb(avctx, FF_INPUT_BUFFER_PADDING_SIZE);
        if (ret < 0)
            return ret;
        ctx->length_size      = ret;
        ctx->first_idr        = 1;
        ctx->idr_sps_pps_seen = 0;
        ctx->extradata_parsed = 1;
    }

    *poutbuf_size = 0;
    *poutbuf      = NULL;
    do {
        ret= AVERROR(EINVAL);
        if (buf + ctx->length_size > buf_end)
            goto fail;

        for (nal_size = 0, i = 0; i<ctx->length_size; i++)
            nal_size = (nal_size << 8) | buf[i];

        buf      += ctx->length_size;
        unit_type = *buf & 0x1f;

        if (buf + nal_size > buf_end || nal_size < 0)
            goto fail;

        if (ctx->first_idr && (unit_type == 7 || unit_type == 8))
            ctx->idr_sps_pps_seen = 1;

        /* if this is a new IDR picture following an IDR picture, reset the idr flag.
         * Just check first_mb_in_slice to be 0 as this is the simplest solution.
         * This could be checking idr_pic_id instead, but would complexify the parsing. */
        if (!ctx->first_idr && unit_type == 5 && (buf[1] & 0x80))
            ctx->first_idr = 1;

        /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
        if (ctx->first_idr && unit_type == 5 && !ctx->idr_sps_pps_seen) {
            if ((ret=alloc_and_copy(poutbuf, poutbuf_size,
                               avctx->extradata, avctx->extradata_size,
                               buf, nal_size)) < 0)
                goto fail;
            ctx->first_idr = 0;
        } else {
            if ((ret=alloc_and_copy(poutbuf, poutbuf_size,
                               NULL, 0, buf, nal_size)) < 0)
                goto fail;
            if (!ctx->first_idr && unit_type == 1) {
                ctx->first_idr = 1;
                ctx->idr_sps_pps_seen = 0;
            }
        }

        buf        += nal_size;
        cumul_size += nal_size + ctx->length_size;
    } while (cumul_size < buf_size);

    return 1;

fail:
    av_freep(poutbuf);
    *poutbuf_size = 0;
    return ret;
}

AVBitStreamFilter ff_h264_mp4toannexb_bsf = {
    .name           = "h264_mp4toannexb",
    .priv_data_size = sizeof(H264BSFContext),
    .filter         = h264_mp4toannexb_filter,
};
