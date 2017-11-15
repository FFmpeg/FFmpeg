/*
 * v308 encoder
 *
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"

static av_cold int v308_encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v308 requires width to be even.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->bits_per_coded_sample = 24;
    avctx->bit_rate = ff_guess_coded_bitrate(avctx);

    return 0;
}

static int v308_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    uint8_t *y, *u, *v;
    int i, j, ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width * avctx->height * 3, 0)) < 0)
        return ret;
    dst = pkt->data;

    y = pic->data[0];
    u = pic->data[1];
    v = pic->data[2];

    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < avctx->width; j++) {
            *dst++ = v[j];
            *dst++ = y[j];
            *dst++ = u[j];
        }
        y += pic->linesize[0];
        u += pic->linesize[1];
        v += pic->linesize[2];
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int v308_encode_close(AVCodecContext *avctx)
{
    return 0;
}

AVCodec ff_v308_encoder = {
    .name         = "v308",
    .long_name    = NULL_IF_CONFIG_SMALL("Uncompressed packed 4:4:4"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_V308,
    .init         = v308_encode_init,
    .encode2      = v308_encode_frame,
    .close        = v308_encode_close,
    .pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV444P, AV_PIX_FMT_NONE },
    .capabilities = AV_CODEC_CAP_INTRA_ONLY,
};
