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
#include "encode.h"
#include "internal.h"

static av_cold int v408_encode_init(AVCodecContext *avctx)
{
    avctx->bits_per_coded_sample = 32;
    avctx->bit_rate = ff_guess_coded_bitrate(avctx);

    return 0;
}

static int v408_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    uint8_t *y, *u, *v, *a;
    int i, j, ret;

    ret = ff_get_encode_buffer(avctx, pkt, avctx->width * avctx->height * 4, 0);
    if (ret < 0)
        return ret;
    dst = pkt->data;

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

    *got_packet = 1;
    return 0;
}

static const enum AVPixelFormat pix_fmt[] = { AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE };

#if CONFIG_AYUV_ENCODER
const AVCodec ff_ayuv_encoder = {
    .name         = "ayuv",
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed packed MS 4:4:4:4"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_AYUV,
    .capabilities = AV_CODEC_CAP_DR1,
    .init         = v408_encode_init,
    .encode2      = v408_encode_frame,
    .pix_fmts     = pix_fmt,
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_V408_ENCODER
const AVCodec ff_v408_encoder = {
    .name         = "v408",
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed packed QT 4:4:4:4"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_V408,
    .capabilities = AV_CODEC_CAP_DR1,
    .init         = v408_encode_init,
    .encode2      = v408_encode_frame,
    .pix_fmts     = pix_fmt,
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
