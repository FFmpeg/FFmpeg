/*
 * v410 encoder
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
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"

static av_cold int v410_encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v410 requires width to be even.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->bits_per_coded_sample = 32;
    avctx->bit_rate = ff_guess_coded_bitrate(avctx);

    av_log(avctx, AV_LOG_WARNING, "This encoder is deprecated and will be removed.\n");

    return 0;
}

static int v410_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    const uint16_t *y, *u, *v;
    uint32_t val;
    int i, j, ret;

    ret = ff_get_encode_buffer(avctx, pkt, avctx->width * avctx->height * 4, 0);
    if (ret < 0)
        return ret;
    dst = pkt->data;

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
        }
        y += pic->linesize[0] >> 1;
        u += pic->linesize[1] >> 1;
        v += pic->linesize[2] >> 1;
    }

    *got_packet = 1;
    return 0;
}

const FFCodec ff_v410_encoder = {
    .p.name       = "v410",
    CODEC_LONG_NAME("Uncompressed 4:4:4 10-bit"),
    .p.type       = AVMEDIA_TYPE_VIDEO,
    .p.id         = AV_CODEC_ID_V410,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init         = v410_encode_init,
    FF_CODEC_ENCODE_CB(v410_encode_frame),
    .p.pix_fmts   = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV444P10, AV_PIX_FMT_NONE },
};
