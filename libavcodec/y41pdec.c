/*
 * y41p decoder
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

static av_cold int y41p_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt             = PIX_FMT_YUV411P;
    avctx->bits_per_raw_sample = 12;

    if (avctx->width & 7) {
        av_log(avctx, AV_LOG_WARNING, "y41p requires width to be divisible by 8.\n");
    }

    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int y41p_decode_frame(AVCodecContext *avctx, void *data,
                             int *data_size, AVPacket *avpkt)
{
    AVFrame *pic = avctx->coded_frame;
    uint8_t *src = avpkt->data;
    uint8_t *y, *u, *v;
    int i, j;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    if (avpkt->size < 1.5 * avctx->height * avctx->width) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data.\n");
        return AVERROR(EINVAL);
    }

    pic->reference = 0;

    if (avctx->get_buffer(avctx, pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate buffer.\n");
        return AVERROR(ENOMEM);
    }

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    for (i = avctx->height - 1; i >= 0 ; i--) {
        y = &pic->data[0][i * pic->linesize[0]];
        u = &pic->data[1][i * pic->linesize[1]];
        v = &pic->data[2][i * pic->linesize[2]];
        for (j = 0; j < avctx->width; j += 8) {
            *(u++) = *src++;
            *(y++) = *src++;
            *(v++) = *src++;
            *(y++) = *src++;

            *(u++) = *src++;
            *(y++) = *src++;
            *(v++) = *src++;
            *(y++) = *src++;

            *(y++) = *src++;
            *(y++) = *src++;
            *(y++) = *src++;
            *(y++) = *src++;
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = *pic;

    return avpkt->size;
}

static av_cold int y41p_decode_close(AVCodecContext *avctx)
{
    if (avctx->coded_frame->data[0])
        avctx->release_buffer(avctx, avctx->coded_frame);

    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_y41p_decoder = {
    .name         = "y41p",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = CODEC_ID_Y41P,
    .init         = y41p_decode_init,
    .decode       = y41p_decode_frame,
    .close        = y41p_decode_close,
    .capabilities = CODEC_CAP_DR1,
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed YUV 4:1:1 12-bit"),
};
