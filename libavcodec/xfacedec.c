/*
 * Copyright (c) 1990 James Ashton - Sydney University
 * Copyright (c) 2012 Stefano Sabatini
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
 * X-Face decoder, based on libcompface, by James Ashton.
 */

#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "xface.h"

static int pop_integer(BigInt *b, const ProbRange *pranges)
{
    uint8_t r;
    int i;

    /* extract the last byte into r, and shift right b by 8 bits */
    ff_big_div(b, 0, &r);

    i = 0;
    while (r < pranges->offset || r >= pranges->range + pranges->offset) {
        pranges++;
        i++;
    }
    ff_big_mul(b, pranges->range);
    ff_big_add(b, r - pranges->offset);
    return i;
}

static void pop_greys(BigInt *b, char *bitmap, int w, int h)
{
    if (w > 3) {
        w /= 2;
        h /= 2;
        pop_greys(b, bitmap,                       w, h);
        pop_greys(b, bitmap + w,                   w, h);
        pop_greys(b, bitmap + XFACE_WIDTH * h,     w, h);
        pop_greys(b, bitmap + XFACE_WIDTH * h + w, w, h);
    } else {
        w = pop_integer(b, ff_xface_probranges_2x2);
        if (w & 1) bitmap[0]               = 1;
        if (w & 2) bitmap[1]               = 1;
        if (w & 4) bitmap[XFACE_WIDTH]     = 1;
        if (w & 8) bitmap[XFACE_WIDTH + 1] = 1;
    }
}

static void decode_block(BigInt *b, char *bitmap, int w, int h, int level)
{
    switch (pop_integer(b, &ff_xface_probranges_per_level[level][0])) {
    case XFACE_COLOR_WHITE:
        return;
    case XFACE_COLOR_BLACK:
        pop_greys(b, bitmap, w, h);
        return;
    default:
        w /= 2;
        h /= 2;
        level++;
        decode_block(b, bitmap,                       w, h, level);
        decode_block(b, bitmap + w,                   w, h, level);
        decode_block(b, bitmap + h * XFACE_WIDTH,     w, h, level);
        decode_block(b, bitmap + w + h * XFACE_WIDTH, w, h, level);
        return;
    }
}

typedef struct XFaceContext {
    uint8_t bitmap[XFACE_PIXELS]; ///< image used internally for decoding
} XFaceContext;

static av_cold int xface_decode_init(AVCodecContext *avctx)
{
    if (avctx->width || avctx->height) {
        if (avctx->width != XFACE_WIDTH || avctx->height != XFACE_HEIGHT) {
            av_log(avctx, AV_LOG_ERROR,
                   "Size value %dx%d not supported, only accepts a size of %dx%d\n",
                   avctx->width, avctx->height, XFACE_WIDTH, XFACE_HEIGHT);
            return AVERROR(EINVAL);
        }
    }

    avctx->width   = XFACE_WIDTH;
    avctx->height  = XFACE_HEIGHT;
    avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;

    return 0;
}

static int xface_decode_frame(AVCodecContext *avctx,
                              void *data, int *got_frame,
                              AVPacket *avpkt)
{
    XFaceContext *xface = avctx->priv_data;
    int ret, i, j, k;
    uint8_t byte;
    BigInt b = {0};
    char *buf;
    int64_t c;
    AVFrame *frame = data;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (i = 0, k = 0; i < avpkt->size && avpkt->data[i]; i++) {
        c = avpkt->data[i];

        /* ignore invalid digits */
        if (c < XFACE_FIRST_PRINT || c > XFACE_LAST_PRINT)
            continue;

        if (++k > XFACE_MAX_DIGITS) {
            av_log(avctx, AV_LOG_WARNING,
                   "Buffer is longer than expected, truncating at byte %d\n", i);
            break;
        }
        ff_big_mul(&b, XFACE_PRINTS);
        ff_big_add(&b, c - XFACE_FIRST_PRINT);
    }

    /* decode image and put it in bitmap */
    memset(xface->bitmap, 0, XFACE_PIXELS);
    buf = xface->bitmap;
    decode_block(&b, buf,                         16, 16, 0);
    decode_block(&b, buf + 16,                    16, 16, 0);
    decode_block(&b, buf + 32,                    16, 16, 0);
    decode_block(&b, buf + XFACE_WIDTH * 16,      16, 16, 0);
    decode_block(&b, buf + XFACE_WIDTH * 16 + 16, 16, 16, 0);
    decode_block(&b, buf + XFACE_WIDTH * 16 + 32, 16, 16, 0);
    decode_block(&b, buf + XFACE_WIDTH * 32     , 16, 16, 0);
    decode_block(&b, buf + XFACE_WIDTH * 32 + 16, 16, 16, 0);
    decode_block(&b, buf + XFACE_WIDTH * 32 + 32, 16, 16, 0);

    ff_xface_generate_face(xface->bitmap, xface->bitmap);

    /* convert image from 1=black 0=white bitmap to MONOWHITE */
    buf = frame->data[0];
    for (i = 0, j = 0, k = 0, byte = 0; i < XFACE_PIXELS; i++) {
        byte += xface->bitmap[i];
        if (k == 7) {
            buf[j++] = byte;
            byte = k = 0;
        } else {
            k++;
            byte <<= 1;
        }
        if (j == XFACE_WIDTH/8) {
            j = 0;
            buf += frame->linesize[0];
        }
    }

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_xface_decoder = {
    .name           = "xface",
    .long_name      = NULL_IF_CONFIG_SMALL("X-face image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_XFACE,
    .priv_data_size = sizeof(XFaceContext),
    .init           = xface_decode_init,
    .decode         = xface_decode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_MONOWHITE, AV_PIX_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
