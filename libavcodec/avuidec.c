/*
 * AVID Meridien decoder
 *
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

static av_cold int avui_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = PIX_FMT_YUVA422P;

    avctx->coded_frame = avcodec_alloc_frame();

    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int avui_decode_frame(AVCodecContext *avctx, void *data,
                             int *data_size, AVPacket *avpkt)
{
    AVFrame *pic = avctx->coded_frame;
    const uint8_t *src = avpkt->data;
    const uint8_t *srca = src + 2 * (avctx->height + 16) * avctx->width + 9;
    uint8_t *y, *u, *v, *a;
    int transparent = 0, i, j, k;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    if (avpkt->size < 2 * avctx->width * (avctx->height + 16) + 4) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data.\n");
        return AVERROR(EINVAL);
    }
    if (avpkt->size >= 4 * avctx->width * (avctx->height + 16) + 13)
        transparent = 1;

    pic->reference = 0;

    if (avctx->get_buffer(avctx, pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate buffer.\n");
        return AVERROR(ENOMEM);
    }

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    for (i = 0; i < 2; i++) {
        src  += avctx->width * 16;
        srca += avctx->width * 16;
        y = pic->data[0] + i * pic->linesize[0];
        u = pic->data[1] + i * pic->linesize[1];
        v = pic->data[2] + i * pic->linesize[2];
        a = pic->data[3] + i * pic->linesize[3];

        for (j = 0; j < (avctx->height + 1) >> 1; j++) {
            for (k = 0; k < (avctx->width + 1) >> 1; k++) {
                u[    k    ] = *src++;
                y[2 * k    ] = *src++;
                a[2 * k    ] = 0xFF - (transparent ? *srca++ : 0);
                srca++;
                v[    k    ] = *src++;
                y[2 * k + 1] = *src++;
                a[2 * k + 1] = 0xFF - (transparent ? *srca++ : 0);
                srca++;
            }

            y += 2 * pic->linesize[0];
            u += 2 * pic->linesize[1];
            v += 2 * pic->linesize[2];
            a += 2 * pic->linesize[3];
        }
        src  += 4;
        srca += 4;
    }
    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = *pic;

    return avpkt->size;
}

static av_cold int avui_decode_close(AVCodecContext *avctx)
{
    if (avctx->coded_frame->data[0])
        avctx->release_buffer(avctx, avctx->coded_frame);

    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_avui_decoder = {
    .name         = "avui",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = CODEC_ID_AVUI,
    .init         = avui_decode_init,
    .decode       = avui_decode_frame,
    .close        = avui_decode_close,
    .capabilities = CODEC_CAP_DR1,
    .long_name    = NULL_IF_CONFIG_SMALL("AVID Meridien"),
};
