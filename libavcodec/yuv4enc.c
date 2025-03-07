/*
 * libquicktime yuv4 encoder
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

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"

static int yuv4_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    const uint8_t *y, *u, *v;
    int ret;

    ret = ff_get_encode_buffer(avctx, pkt, 6 * ((avctx->width  + 1) / 2)
                                             * ((avctx->height + 1) / 2), 0);
    if (ret < 0)
        return ret;
    dst = pkt->data;

    y = pic->data[0];
    u = pic->data[1];
    v = pic->data[2];

    for (int i = 0; i < avctx->height / 2; i++) {
        for (int j = 0; j < (avctx->width + 1) / 2; j++) {
            *dst++ = u[j] ^ 0x80;
            *dst++ = v[j] ^ 0x80;
            *dst++ = y[                   2 * j    ];
            *dst++ = y[                   2 * j + 1];
            *dst++ = y[pic->linesize[0] + 2 * j    ];
            *dst++ = y[pic->linesize[0] + 2 * j + 1];
        }
        y += 2 * pic->linesize[0];
        u +=     pic->linesize[1];
        v +=     pic->linesize[2];
    }

    if (avctx->height & 1) {
        for (int j = 0; j < (avctx->width + 1) / 2; j++) {
            *dst++ = u[j] ^ 0x80;
            *dst++ = v[j] ^ 0x80;
            *dst++ = y[2 * j    ];
            *dst++ = y[2 * j + 1];
            *dst++ = y[2 * j    ];
            *dst++ = y[2 * j + 1];
        }
    }

    *got_packet = 1;
    return 0;
}

const FFCodec ff_yuv4_encoder = {
    .p.name         = "yuv4",
    CODEC_LONG_NAME("Uncompressed packed 4:2:0"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_YUV4,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    FF_CODEC_ENCODE_CB(yuv4_encode_frame),
};
