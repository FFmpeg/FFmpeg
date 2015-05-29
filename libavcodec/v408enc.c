/*
 * v408 encoder
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"

static av_cold int v408_encode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = av_frame_alloc();

    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int v408_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    uint8_t *y, *u, *v, *a;
    int i, j, ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width * avctx->height * 4)) < 0)
        return ret;
    dst = pkt->data;

    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    y = pic->data[0];
    u = pic->data[1];
    v = pic->data[2];
    a = pic->data[3];

    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < avctx->width; j++) {
           if (avctx->codec_id==AV_CODEC_ID_AYUV) {
                *dst++ = v[j];
                *dst++ = u[j];
                *dst++ = y[j];
                *dst++ = a[j];
            } else {
                *dst++ = u[j];
                *dst++ = y[j];
                *dst++ = v[j];
                *dst++ = a[j];
            }
        }
        y += pic->linesize[0];
        u += pic->linesize[1];
        v += pic->linesize[2];
        a += pic->linesize[3];
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int v408_encode_close(AVCodecContext *avctx)
{
    av_frame_free(&avctx->coded_frame);

    return 0;
}

#if CONFIG_AYUV_ENCODER
AVCodec ff_ayuv_encoder = {
    .name         = "ayuv",
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed packed MS 4:4:4:4"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_AYUV,
    .init         = v408_encode_init,
    .encode2      = v408_encode_frame,
    .close        = v408_encode_close,
    .pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE },
};
#endif
#if CONFIG_V408_ENCODER
AVCodec ff_v408_encoder = {
    .name         = "v408",
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed packed QT 4:4:4:4"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_V408,
    .init         = v408_encode_init,
    .encode2      = v408_encode_frame,
    .close        = v408_encode_close,
    .pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE },
};
#endif
