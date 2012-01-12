/*
 * v410 encoder
 *
 * Copyright (c) 2011 Derek Buitenhuis
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

static av_cold int v410_encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v410 requires even width.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_frame = avcodec_alloc_frame();

    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int v410_encode_frame(AVCodecContext *avctx, uint8_t *buf,
                             int buf_size, void *data)
{
    AVFrame *pic = data;
    uint8_t *dst = buf;
    uint16_t *y, *u, *v;
    uint32_t val;
    int i, j;
    int output_size = 0;

    if (buf_size < avctx->width * avctx->height * 4) {
        av_log(avctx, AV_LOG_ERROR, "Out buffer is too small.\n");
        return AVERROR(ENOMEM);
    }

    avctx->coded_frame->reference = 0;
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    y = (uint16_t *)pic->data[0];
    u = (uint16_t *)pic->data[1];
    v = (uint16_t *)pic->data[2];

    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < avctx->width; j++) {
            val  = u[j] << 2;
            val |= y[j] << 12;
            val |= (uint32_t) v[j] << 22;
            AV_WL32(dst, val);
            dst += 4;
            output_size += 4;
        }
        y += pic->linesize[0] >> 1;
        u += pic->linesize[1] >> 1;
        v += pic->linesize[2] >> 1;
    }

    return output_size;
}

static av_cold int v410_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_v410_encoder = {
    .name         = "v410",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = CODEC_ID_V410,
    .init         = v410_encode_init,
    .encode       = v410_encode_frame,
    .close        = v410_encode_close,
    .pix_fmts     = (const enum PixelFormat[]){ PIX_FMT_YUV444P10, PIX_FMT_NONE },
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed 4:4:4 10-bit"),
};
