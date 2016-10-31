/*
 * v410 decoder
 *
 * Copyright (c) 2011 Derek Buitenhuis
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"

static av_cold int v410_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt             = AV_PIX_FMT_YUV444P10;
    avctx->bits_per_raw_sample = 10;

    if (avctx->width & 1) {
        if (avctx->err_recognition & AV_EF_EXPLODE) {
            av_log(avctx, AV_LOG_ERROR, "v410 requires width to be even.\n");
            return AVERROR_INVALIDDATA;
        } else {
            av_log(avctx, AV_LOG_WARNING, "v410 requires width to be even, continuing anyway.\n");
        }
    }

    return 0;
}

static int v410_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    AVFrame *pic = data;
    uint8_t *src = avpkt->data;
    uint16_t *y, *u, *v;
    uint32_t val;
    int i, j, ret;

    if (avpkt->size < 4 * avctx->height * avctx->width) {
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
        for (j = 0; j < avctx->width; j++) {
            val = AV_RL32(src);

            u[j] = (val >>  2) & 0x3FF;
            y[j] = (val >> 12) & 0x3FF;
            v[j] = (val >> 22);

            src += 4;
        }

        y += pic->linesize[0] >> 1;
        u += pic->linesize[1] >> 1;
        v += pic->linesize[2] >> 1;
    }

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_v410_decoder = {
    .name         = "v410",
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed 4:4:4 10-bit"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_V410,
    .init         = v410_decode_init,
    .decode       = v410_decode_frame,
    .capabilities = AV_CODEC_CAP_DR1,
};
