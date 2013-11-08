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
 * X-Face encoder, based on libcompface, by James Ashton.
 */

#include "xface.h"
#include "avcodec.h"
#include "internal.h"

typedef struct XFaceContext {
    AVClass *class;
    uint8_t bitmap[XFACE_PIXELS]; ///< image used internally for decoding
    int max_line_len;             ///< max line length for compressed data
    int set_header;               ///< set X-Face header in the output
} XFaceContext;

static int all_same(char *bitmap, int w, int h)
{
    char val, *row;
    int x;

    val = *bitmap;
    while (h--) {
        row = bitmap;
        x = w;
        while (x--)
            if (*(row++) != val)
                return 0;
        bitmap += XFACE_WIDTH;
    }
    return 1;
}

static int all_black(char *bitmap, int w, int h)
{
    if (w > 3) {
        w /= 2;
        h /= 2;
        return (all_black(bitmap, w, h) && all_black(bitmap + w, w, h) &&
                all_black(bitmap + XFACE_WIDTH * h, w, h) &&
                all_black(bitmap + XFACE_WIDTH * h + w, w, h));
    } else {
        /* at least one pixel in the 2x2 grid is non-zero */
        return *bitmap || *(bitmap + 1) ||
               *(bitmap + XFACE_WIDTH) || *(bitmap + XFACE_WIDTH + 1);
    }
}

static int all_white(char *bitmap, int w, int h)
{
    return *bitmap == 0 && all_same(bitmap, w, h);
}

typedef struct {
    const ProbRange *prob_ranges[XFACE_PIXELS*2];
    int prob_ranges_idx;
} ProbRangesQueue;

static inline int pq_push(ProbRangesQueue *pq, const ProbRange *p)
{
    if (pq->prob_ranges_idx >= XFACE_PIXELS * 2 - 1)
        return -1;
    pq->prob_ranges[pq->prob_ranges_idx++] = p;
    return 0;
}

static void push_greys(ProbRangesQueue *pq, char *bitmap, int w, int h)
{
    if (w > 3) {
        w /= 2;
        h /= 2;
        push_greys(pq, bitmap,                       w, h);
        push_greys(pq, bitmap + w,                   w, h);
        push_greys(pq, bitmap + XFACE_WIDTH * h,     w, h);
        push_greys(pq, bitmap + XFACE_WIDTH * h + w, w, h);
    } else {
        const ProbRange *p = ff_xface_probranges_2x2 +
                 *bitmap +
            2 * *(bitmap + 1) +
            4 * *(bitmap + XFACE_WIDTH) +
            8 * *(bitmap + XFACE_WIDTH + 1);
        pq_push(pq, p);
    }
}

static void encode_block(char *bitmap, int w, int h, int level, ProbRangesQueue *pq)
{
    if (all_white(bitmap, w, h)) {
        pq_push(pq, &ff_xface_probranges_per_level[level][XFACE_COLOR_WHITE]);
    } else if (all_black(bitmap, w, h)) {
        pq_push(pq, &ff_xface_probranges_per_level[level][XFACE_COLOR_BLACK]);
        push_greys(pq, bitmap, w, h);
    } else {
        pq_push(pq, &ff_xface_probranges_per_level[level][XFACE_COLOR_GREY]);
        w /= 2;
        h /= 2;
        level++;
        encode_block(bitmap,                       w, h, level, pq);
        encode_block(bitmap + w,                   w, h, level, pq);
        encode_block(bitmap + h * XFACE_WIDTH,     w, h, level, pq);
        encode_block(bitmap + w + h * XFACE_WIDTH, w, h, level, pq);
    }
}

static av_cold int xface_encode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    return 0;
}

static void push_integer(BigInt *b, const ProbRange *prange)
{
    uint8_t r;

    ff_big_div(b, prange->range, &r);
    ff_big_mul(b, 0);
    ff_big_add(b, r + prange->offset);
}

static int xface_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *frame, int *got_packet)
{
    XFaceContext *xface = avctx->priv_data;
    ProbRangesQueue pq = {{ 0 }, 0};
    uint8_t bitmap_copy[XFACE_PIXELS];
    BigInt b = {0};
    int i, j, k, ret = 0;
    const uint8_t *buf;
    uint8_t *p;
    char intbuf[XFACE_MAX_DIGITS];

    if (avctx->width || avctx->height) {
        if (avctx->width != XFACE_WIDTH || avctx->height != XFACE_HEIGHT) {
            av_log(avctx, AV_LOG_ERROR,
                   "Size value %dx%d not supported, only accepts a size of %dx%d\n",
                   avctx->width, avctx->height, XFACE_WIDTH, XFACE_HEIGHT);
            return AVERROR(EINVAL);
        }
    }
    avctx->width  = XFACE_WIDTH;
    avctx->height = XFACE_HEIGHT;

    /* convert image from MONOWHITE to 1=black 0=white bitmap */
    buf = frame->data[0];
    i = j = 0;
    do {
        for (k = 0; k < 8; k++)
            xface->bitmap[i++] = (buf[j]>>(7-k))&1;
        if (++j == XFACE_WIDTH/8) {
            buf += frame->linesize[0];
            j = 0;
        }
    } while (i < XFACE_PIXELS);

    /* create a copy of bitmap */
    memcpy(bitmap_copy, xface->bitmap, XFACE_PIXELS);
    ff_xface_generate_face(xface->bitmap, bitmap_copy);

    encode_block(xface->bitmap,                         16, 16, 0, &pq);
    encode_block(xface->bitmap + 16,                    16, 16, 0, &pq);
    encode_block(xface->bitmap + 32,                    16, 16, 0, &pq);
    encode_block(xface->bitmap + XFACE_WIDTH * 16,      16, 16, 0, &pq);
    encode_block(xface->bitmap + XFACE_WIDTH * 16 + 16, 16, 16, 0, &pq);
    encode_block(xface->bitmap + XFACE_WIDTH * 16 + 32, 16, 16, 0, &pq);
    encode_block(xface->bitmap + XFACE_WIDTH * 32,      16, 16, 0, &pq);
    encode_block(xface->bitmap + XFACE_WIDTH * 32 + 16, 16, 16, 0, &pq);
    encode_block(xface->bitmap + XFACE_WIDTH * 32 + 32, 16, 16, 0, &pq);

    while (pq.prob_ranges_idx > 0)
        push_integer(&b, pq.prob_ranges[--pq.prob_ranges_idx]);

    /* write the inverted big integer in b to intbuf */
    i = 0;
    while (b.nb_words) {
        uint8_t r;
        ff_big_div(&b, XFACE_PRINTS, &r);
        intbuf[i++] = r + XFACE_FIRST_PRINT;
    }

    if ((ret = ff_alloc_packet2(avctx, pkt, i+2)) < 0)
        return ret;

    /* revert the number, and close the buffer */
    p = pkt->data;
    while (--i >= 0)
        *(p++) = intbuf[i];
    *(p++) = '\n';
    *(p++) = 0;

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int xface_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_xface_encoder = {
    .name           = "xface",
    .long_name      = NULL_IF_CONFIG_SMALL("X-face image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_XFACE,
    .priv_data_size = sizeof(XFaceContext),
    .init           = xface_encode_init,
    .close          = xface_encode_close,
    .encode2        = xface_encode_frame,
    .pix_fmts       = (const enum PixelFormat[]) { AV_PIX_FMT_MONOWHITE, AV_PIX_FMT_NONE },
};
