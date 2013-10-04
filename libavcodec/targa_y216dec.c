/*
 * Pinnacle TARGA CineWave YUV16 decoder
 * Copyright (c) 2012 Carl Eugen Hoyos
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
#include "internal.h"

static av_cold int y216_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt             = AV_PIX_FMT_YUV422P16;
    avctx->bits_per_raw_sample = 14;

    return 0;
}

static int y216_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    AVFrame *pic = data;
    const uint16_t *src = (uint16_t *)avpkt->data;
    uint16_t *y, *u, *v, aligned_width = FFALIGN(avctx->width, 4);
    int i, j, ret;

    if (avpkt->size < 4 * avctx->height * aligned_width) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data.\n");
        return AVERROR(EINVAL);
    }

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    y = (uint16_t *)pic->data[0];
    u = (uint16_t *)pic->data[1];
    v = (uint16_t *)pic->data[2];

    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < avctx->width >> 1; j++) {
            u[    j    ] = src[4 * j    ] << 2 | src[4 * j    ] >> 14;
            y[2 * j    ] = src[4 * j + 1] << 2 | src[4 * j + 1] >> 14;
            v[    j    ] = src[4 * j + 2] << 2 | src[4 * j + 2] >> 14;
            y[2 * j + 1] = src[4 * j + 3] << 2 | src[4 * j + 3] >> 14;
        }

        y += pic->linesize[0] >> 1;
        u += pic->linesize[1] >> 1;
        v += pic->linesize[2] >> 1;
        src += aligned_width << 1;
    }

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_targa_y216_decoder = {
    .name         = "targa_y216",
    .long_name    = NULL_IF_CONFIG_SMALL("Pinnacle TARGA CineWave YUV16"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_TARGA_Y216,
    .init         = y216_decode_init,
    .decode       = y216_decode_frame,
    .capabilities = CODEC_CAP_DR1,
};
