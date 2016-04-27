/*
 * Autodesk RLE Decoder
 * Copyright (C) 2005 The FFmpeg project
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Autodesk RLE Video Decoder by Konstantin Shishkov
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "internal.h"
#include "msrledec.h"

typedef struct AascContext {
    AVCodecContext *avctx;
    GetByteContext gb;
    AVFrame *frame;
} AascContext;

static av_cold int aasc_decode_init(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_BGR24;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int aasc_decode_frame(AVCodecContext *avctx,
                              void *data, int *got_frame,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AascContext *s     = avctx->priv_data;
    int compr, i, stride, ret;

    if (buf_size < 4)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    compr     = AV_RL32(buf);
    buf      += 4;
    buf_size -= 4;
    switch (compr) {
    case 0:
        stride = (avctx->width * 3 + 3) & ~3;
        if (buf_size < stride * avctx->height)
            return AVERROR_INVALIDDATA;
        for (i = avctx->height - 1; i >= 0; i--) {
            memcpy(s->frame->data[0] + i * s->frame->linesize[0], buf, avctx->width * 3);
            buf += stride;
        }
        break;
    case 1:
        bytestream2_init(&s->gb, buf, buf_size);
        ff_msrle_decode(avctx, s->frame, 8, &s->gb);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown compression type %d\n", compr);
        return AVERROR_INVALIDDATA;
    }

    *got_frame = 1;
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    /* report that the buffer was completely consumed */
    return avpkt->size;
}

static av_cold int aasc_decode_end(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_aasc_decoder = {
    .name           = "aasc",
    .long_name      = NULL_IF_CONFIG_SMALL("Autodesk RLE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AASC,
    .priv_data_size = sizeof(AascContext),
    .init           = aasc_decode_init,
    .close          = aasc_decode_end,
    .decode         = aasc_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
