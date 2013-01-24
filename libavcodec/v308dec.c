/*
 * v308 decoder
 * Copyright (c) 2011 Carl Eugen Hoyos
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

static av_cold int v308_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_YUV444P;

    if (avctx->width & 1)
        av_log(avctx, AV_LOG_WARNING, "v308 requires width to be even.\n");

    avctx->coded_frame = avcodec_alloc_frame();

    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int v308_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    AVFrame *pic = avctx->coded_frame;
    const uint8_t *src = avpkt->data;
    uint8_t *y, *u, *v;
    int i, j;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    if (avpkt->size < 3 * avctx->height * avctx->width) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data.\n");
        return AVERROR(EINVAL);
    }

    pic->reference = 0;

    if (ff_get_buffer(avctx, pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate buffer.\n");
        return AVERROR(ENOMEM);
    }

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    y = pic->data[0];
    u = pic->data[1];
    v = pic->data[2];

    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < avctx->width; j++) {
            v[j] = *src++;
            y[j] = *src++;
            u[j] = *src++;
        }

        y += pic->linesize[0];
        u += pic->linesize[1];
        v += pic->linesize[2];
    }

    *got_frame = 1;
    *(AVFrame *)data = *pic;

    return avpkt->size;
}

static av_cold int v308_decode_close(AVCodecContext *avctx)
{
    if (avctx->coded_frame->data[0])
        avctx->release_buffer(avctx, avctx->coded_frame);

    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_v308_decoder = {
    .name         = "v308",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_V308,
    .init         = v308_decode_init,
    .decode       = v308_decode_frame,
    .close        = v308_decode_close,
    .capabilities = CODEC_CAP_DR1,
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed packed 4:4:4"),
};
