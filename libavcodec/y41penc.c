/*
 * y41p encoder
 *
 * Copyright (c) 2012 Paul B Mahol
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

#include "avcodec.h"

static av_cold int y41p_encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 7) {
        av_log(avctx, AV_LOG_ERROR, "y41p requires width to be divisible by 8.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_frame = avcodec_alloc_frame();
    avctx->bits_per_coded_sample = 12;

    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int y41p_encode_frame(AVCodecContext *avctx, uint8_t *buf,
                             int buf_size, void *data)
{
    AVFrame *pic = data;
    uint8_t *dst = buf;
    uint8_t *y, *u, *v;
    int i, j;

    if (buf_size < avctx->width * avctx->height * 1.5) {
        av_log(avctx, AV_LOG_ERROR, "Out buffer is too small.\n");
        return AVERROR(ENOMEM);
    }

    avctx->coded_frame->reference = 0;
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    for (i = avctx->height - 1; i >= 0; i--) {
        y = &pic->data[0][i * pic->linesize[0]];
        u = &pic->data[1][i * pic->linesize[1]];
        v = &pic->data[2][i * pic->linesize[2]];
        for (j = 0; j < avctx->width; j += 8) {
            *(dst++) = *(u++);
            *(dst++) = *(y++);
            *(dst++) = *(v++);
            *(dst++) = *(y++);

            *(dst++) = *(u++);
            *(dst++) = *(y++);
            *(dst++) = *(v++);
            *(dst++) = *(y++);

            *(dst++) = *(y++);
            *(dst++) = *(y++);
            *(dst++) = *(y++);
            *(dst++) = *(y++);
        }
    }

    return avctx->width * avctx->height * 1.5;
}

static av_cold int y41p_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_y41p_encoder = {
    .name         = "y41p",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = CODEC_ID_Y41P,
    .init         = y41p_encode_init,
    .encode       = y41p_encode_frame,
    .close        = y41p_encode_close,
    .pix_fmts     = (const enum PixelFormat[]) { PIX_FMT_YUV411P,
                                                 PIX_FMT_NONE },
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed YUV 4:1:1 12-bit"),
};
