/*
 * Autodesk RLE Decoder
 * Copyright (C) 2005 The FFmpeg project
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

/**
 * @file
 * Autodesk RLE Video Decoder by Konstantin Shishkov
 */

#include <string.h>

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "msrledec.h"

typedef struct AascContext {
    AVCodecContext *avctx;
    GetByteContext gb;
    AVFrame *frame;

    uint32_t palette[AVPALETTE_COUNT];
    int palette_size;
} AascContext;

static av_cold int aasc_decode_init(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;
    uint8_t *ptr;
    int i;

    s->avctx = avctx;
    switch (avctx->bits_per_coded_sample) {
    case 8:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;

        ptr = avctx->extradata;
        s->palette_size = FFMIN(avctx->extradata_size, AVPALETTE_SIZE);
        for (i = 0; i < s->palette_size / 4; i++) {
            s->palette[i] = 0xFFU << 24 | AV_RL32(ptr);
            ptr += 4;
        }
        break;
    case 16:
        avctx->pix_fmt = AV_PIX_FMT_RGB555LE;
        break;
    case 24:
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth: %d\n", avctx->bits_per_coded_sample);
        return -1;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int aasc_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AascContext *s     = avctx->priv_data;
    int compr, i, stride, psize, ret;

    if (buf_size < 4) {
        av_log(avctx, AV_LOG_ERROR, "frame too short\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    compr     = AV_RL32(buf);
    buf      += 4;
    buf_size -= 4;
    psize = avctx->bits_per_coded_sample / 8;
    switch (avctx->codec_tag) {
    case MKTAG('A', 'A', 'S', '4'):
        bytestream2_init(&s->gb, buf - 4, buf_size + 4);
        ff_msrle_decode(avctx, s->frame, 8, &s->gb);
        break;
    case MKTAG('A', 'A', 'S', 'C'):
        switch (compr) {
        case 0:
            stride = (avctx->width * psize + psize) & ~psize;
            if (buf_size < stride * avctx->height)
                return AVERROR_INVALIDDATA;
            for (i = avctx->height - 1; i >= 0; i--) {
                memcpy(s->frame->data[0] + i * s->frame->linesize[0], buf, avctx->width * psize);
                buf += stride;
                buf_size -= stride;
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
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown FourCC: %X\n", avctx->codec_tag);
        return -1;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
        memcpy(s->frame->data[1], s->palette, s->palette_size);

    *got_frame = 1;
    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
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

const FFCodec ff_aasc_decoder = {
    .p.name         = "aasc",
    CODEC_LONG_NAME("Autodesk RLE"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AASC,
    .priv_data_size = sizeof(AascContext),
    .init           = aasc_decode_init,
    .close          = aasc_decode_end,
    FF_CODEC_DECODE_CB(aasc_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
