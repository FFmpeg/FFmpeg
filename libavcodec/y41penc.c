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
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"

static av_cold int y41p_encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 7) {
        av_log(avctx, AV_LOG_ERROR, "y41p requires width to be divisible by 8.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->bits_per_coded_sample = 12;
    avctx->bit_rate = ff_guess_coded_bitrate(avctx);

    return 0;
}

static int y41p_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    const uint8_t *y, *u, *v;
    int i, j, ret;

    ret = ff_get_encode_buffer(avctx, pkt, avctx->width * avctx->height * 1.5, 0);
    if (ret < 0)
        return ret;

    dst = pkt->data;

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

    *got_packet = 1;
    return 0;
}

const FFCodec ff_y41p_encoder = {
    .p.name       = "y41p",
    CODEC_LONG_NAME("Uncompressed YUV 4:1:1 12-bit"),
    .p.type       = AVMEDIA_TYPE_VIDEO,
    .p.id         = AV_CODEC_ID_Y41P,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init         = y41p_encode_init,
    FF_CODEC_ENCODE_CB(y41p_encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_YUV411P),
};
