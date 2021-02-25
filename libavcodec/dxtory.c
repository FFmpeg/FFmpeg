/*
 * Dxtory decoder
 *
 * Copyright (c) 2011 Konstantin Shishkov
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

#include <inttypes.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "unary.h"
#include "thread.h"

static int64_t get_raw_size(enum AVPixelFormat fmt, int width, int height)
{
    switch (fmt) {
    case AV_PIX_FMT_RGB555LE:
    case AV_PIX_FMT_RGB565LE:
        return width * height * 2LL;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_YUV444P:
        return width * height * 3LL;
    case AV_PIX_FMT_YUV420P:
        return (int64_t)(width * height) + 2 * AV_CEIL_RSHIFT(width, 1) * AV_CEIL_RSHIFT(height, 1);
    case AV_PIX_FMT_YUV410P:
        return (int64_t)(width * height) + 2 * AV_CEIL_RSHIFT(width, 2) * AV_CEIL_RSHIFT(height, 2);
    }

    return 0;
}

static void do_vflip(AVCodecContext *avctx, AVFrame *pic, int vflip)
{
    if (!vflip)
        return;

    switch (pic->format) {
    case AV_PIX_FMT_YUV444P:
        pic->data[1] += (avctx->height - 1) * pic->linesize[1];
        pic->linesize[1] = -pic->linesize[1];
        pic->data[2] += (avctx->height - 1) * pic->linesize[2];
        pic->linesize[2] = -pic->linesize[2];
    case AV_PIX_FMT_RGB555LE:
    case AV_PIX_FMT_RGB565LE:
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB24:
        pic->data[0] += (avctx->height - 1) * pic->linesize[0];
        pic->linesize[0] = -pic->linesize[0];
        break;
    case AV_PIX_FMT_YUV410P:
        pic->data[0] += (avctx->height - 1) * pic->linesize[0];
        pic->linesize[0] = -pic->linesize[0];
        pic->data[1] += (AV_CEIL_RSHIFT(avctx->height, 2) - 1) * pic->linesize[1];
        pic->linesize[1] = -pic->linesize[1];
        pic->data[2] += (AV_CEIL_RSHIFT(avctx->height, 2) - 1) * pic->linesize[2];
        pic->linesize[2] = -pic->linesize[2];
        break;
    case AV_PIX_FMT_YUV420P:
        pic->data[0] += (avctx->height - 1) * pic->linesize[0];
        pic->linesize[0] = -pic->linesize[0];
        pic->data[1] += (AV_CEIL_RSHIFT(avctx->height, 1) - 1) * pic->linesize[1];
        pic->linesize[1] = -pic->linesize[1];
        pic->data[2] += (AV_CEIL_RSHIFT(avctx->height, 1) - 1) * pic->linesize[2];
        pic->linesize[2] = -pic->linesize[2];
        break;
    }
}

static int dxtory_decode_v1_rgb(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                int id, int bpp, uint32_t vflipped)
{
    ThreadFrame frame = { .f = pic };
    int h;
    uint8_t *dst;
    int ret;

    if (src_size < get_raw_size(id, avctx->width, avctx->height)) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = id;
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    do_vflip(avctx, pic, vflipped);

    dst = pic->data[0];
    for (h = 0; h < avctx->height; h++) {
        memcpy(dst, src, avctx->width * bpp);
        src += avctx->width * bpp;
        dst += pic->linesize[0];
    }

    do_vflip(avctx, pic, vflipped);

    return 0;
}

static int dxtory_decode_v1_410(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    ThreadFrame frame = { .f = pic };
    int h, w;
    uint8_t *Y1, *Y2, *Y3, *Y4, *U, *V;
    int height, width, hmargin, vmargin;
    int huvborder;
    int ret;

    if (src_size < get_raw_size(AV_PIX_FMT_YUV410P, avctx->width, avctx->height)) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV410P;
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    do_vflip(avctx, pic, vflipped);

    height = avctx->height & ~3;
    width  = avctx->width  & ~3;
    hmargin = avctx->width  - width;
    vmargin = avctx->height - height;
    huvborder = AV_CEIL_RSHIFT(avctx->width, 2) - 1;

    Y1 = pic->data[0];
    Y2 = pic->data[0] + pic->linesize[0];
    Y3 = pic->data[0] + pic->linesize[0] * 2;
    Y4 = pic->data[0] + pic->linesize[0] * 3;
    U  = pic->data[1];
    V  = pic->data[2];
    for (h = 0; h < height; h += 4) {
        for (w = 0; w < width; w += 4) {
            AV_COPY32U(Y1 + w, src);
            AV_COPY32U(Y2 + w, src + 4);
            AV_COPY32U(Y3 + w, src + 8);
            AV_COPY32U(Y4 + w, src + 12);
            U[w >> 2] = src[16] + 0x80;
            V[w >> 2] = src[17] + 0x80;
            src += 18;
        }
        if (hmargin) {
            for (w = 0; w < hmargin; w++) {
                Y1[width + w] = src[w];
                Y2[width + w] = src[w + hmargin * 1];
                Y3[width + w] = src[w + hmargin * 2];
                Y4[width + w] = src[w + hmargin * 3];
            }
            src += 4 * hmargin;
            U[huvborder] = src[0] + 0x80;
            V[huvborder] = src[1] + 0x80;
            src += 2;
        }
        Y1 += pic->linesize[0] * 4;
        Y2 += pic->linesize[0] * 4;
        Y3 += pic->linesize[0] * 4;
        Y4 += pic->linesize[0] * 4;
        U  += pic->linesize[1];
        V  += pic->linesize[2];
    }

    if (vmargin) {
        for (w = 0; w < width; w += 4) {
            AV_COPY32U(Y1 + w, src);
            if (vmargin > 1)
                AV_COPY32U(Y2 + w, src + 4);
            if (vmargin > 2)
                AV_COPY32U(Y3 + w, src + 8);
            src += 4 * vmargin;
            U[w >> 2] = src[0] + 0x80;
            V[w >> 2] = src[1] + 0x80;
            src += 2;
        }
        if (hmargin) {
            for (w = 0; w < hmargin; w++) {
                AV_COPY32U(Y1 + w, src);
                if (vmargin > 1)
                    AV_COPY32U(Y2 + w, src + 4);
                if (vmargin > 2)
                    AV_COPY32U(Y3 + w, src + 8);
                src += 4 * vmargin;
            }
            U[huvborder] = src[0] + 0x80;
            V[huvborder] = src[1] + 0x80;
            src += 2;
        }
    }

    do_vflip(avctx, pic, vflipped);

    return 0;
}

static int dxtory_decode_v1_420(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    ThreadFrame frame = { .f = pic };
    int h, w;
    uint8_t *Y1, *Y2, *U, *V;
    int height, width, hmargin, vmargin;
    int huvborder;
    int ret;

    if (src_size < get_raw_size(AV_PIX_FMT_YUV420P, avctx->width, avctx->height)) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    do_vflip(avctx, pic, vflipped);

    height = avctx->height & ~1;
    width  = avctx->width  & ~1;
    hmargin = avctx->width  - width;
    vmargin = avctx->height - height;
    huvborder = AV_CEIL_RSHIFT(avctx->width, 1) - 1;

    Y1 = pic->data[0];
    Y2 = pic->data[0] + pic->linesize[0];
    U  = pic->data[1];
    V  = pic->data[2];
    for (h = 0; h < height; h += 2) {
        for (w = 0; w < width; w += 2) {
            AV_COPY16(Y1 + w, src);
            AV_COPY16(Y2 + w, src + 2);
            U[w >> 1] = src[4] + 0x80;
            V[w >> 1] = src[5] + 0x80;
            src += 6;
        }
        if (hmargin) {
            Y1[width + 1] = src[0];
            Y2[width + 1] = src[1];
            U[huvborder] = src[2] + 0x80;
            V[huvborder] = src[3] + 0x80;
            src += 4;
        }
        Y1 += pic->linesize[0] * 2;
        Y2 += pic->linesize[0] * 2;
        U  += pic->linesize[1];
        V  += pic->linesize[2];
    }

    if (vmargin) {
        for (w = 0; w < width; w += 2) {
            AV_COPY16U(Y1 + w, src);
            U[w >> 1] = src[0] + 0x80;
            V[w >> 1] = src[1] + 0x80;
            src += 4;
        }
        if (hmargin) {
            Y1[w] = src[0];
            U[huvborder] = src[1] + 0x80;
            V[huvborder] = src[2] + 0x80;
            src += 3;
        }
    }

    do_vflip(avctx, pic, vflipped);

    return 0;
}

static int dxtory_decode_v1_444(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    ThreadFrame frame = { .f = pic };
    int h, w;
    uint8_t *Y, *U, *V;
    int ret;

    if (src_size < get_raw_size(AV_PIX_FMT_YUV444P, avctx->width, avctx->height)) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV444P;
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    do_vflip(avctx, pic, vflipped);

    Y = pic->data[0];
    U = pic->data[1];
    V = pic->data[2];
    for (h = 0; h < avctx->height; h++) {
        for (w = 0; w < avctx->width; w++) {
            Y[w] = *src++;
            U[w] = *src++ ^ 0x80;
            V[w] = *src++ ^ 0x80;
        }
        Y += pic->linesize[0];
        U += pic->linesize[1];
        V += pic->linesize[2];
    }

    do_vflip(avctx, pic, vflipped);

    return 0;
}

static const uint8_t def_lru[8] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xFF };
static const uint8_t def_lru_555[8] = { 0x00, 0x08, 0x10, 0x18, 0x1F };
static const uint8_t def_lru_565[8] = { 0x00, 0x08, 0x10, 0x20, 0x30, 0x3F };

static inline uint8_t decode_sym(GetBitContext *gb, uint8_t lru[8])
{
    uint8_t c, val;

    c = get_unary(gb, 0, 8);
    if (!c) {
        val = get_bits(gb, 8);
        memmove(lru + 1, lru, sizeof(*lru) * (8 - 1));
    } else {
        val = lru[c - 1];
        memmove(lru + 1, lru, sizeof(*lru) * (c - 1));
    }
    lru[0] = val;

    return val;
}

static int check_slice_size(AVCodecContext *avctx,
                            const uint8_t *src, int src_size,
                            int slice_size, int off)
{
    int cur_slice_size;

    if (slice_size > src_size - off) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid slice size %d (only %d bytes left)\n",
               slice_size, src_size - off);
        return AVERROR_INVALIDDATA;
    }
    if (slice_size <= 16) {
        av_log(avctx, AV_LOG_ERROR, "invalid slice size %d\n",
               slice_size);
        return AVERROR_INVALIDDATA;
    }

    cur_slice_size = AV_RL32(src + off);
    if (cur_slice_size != slice_size - 16) {
        av_log(avctx, AV_LOG_ERROR,
               "Slice sizes mismatch: got %d instead of %d\n",
               cur_slice_size, slice_size - 16);
    }

    return 0;
}

static int load_buffer(AVCodecContext *avctx,
                       const uint8_t *src, int src_size,
                       GetByteContext *gb,
                       int *nslices, int *off)
{
    bytestream2_init(gb, src, src_size);
    *nslices = bytestream2_get_le16(gb);
    *off = FFALIGN(*nslices * 4 + 2, 16);
    if (src_size < *off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!*nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", *nslices,
                              avctx->width, avctx->height);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static inline uint8_t decode_sym_565(GetBitContext *gb, uint8_t lru[8],
                                     int bits)
{
    uint8_t c, val;

    c = get_unary(gb, 0, bits);
    if (!c) {
        val = get_bits(gb, bits);
        memmove(lru + 1, lru, sizeof(*lru) * (6 - 1));
    } else {
        val = lru[c - 1];
        memmove(lru + 1, lru, sizeof(*lru) * (c - 1));
    }
    lru[0] = val;

    return val;
}

typedef int (*decode_slice_func)(GetBitContext *gb, AVFrame *frame,
                                 int line, int height, uint8_t lru[3][8]);

typedef void (*setup_lru_func)(uint8_t lru[3][8]);

static int dxtory_decode_v2(AVCodecContext *avctx, AVFrame *pic,
                            const uint8_t *src, int src_size,
                            decode_slice_func decode_slice,
                            setup_lru_func setup_lru,
                            enum AVPixelFormat fmt,
                            uint32_t vflipped)
{
    ThreadFrame frame = { .f = pic };
    GetByteContext gb, gb_check;
    GetBitContext  gb2;
    int nslices, slice, line = 0;
    uint32_t off, slice_size;
    uint64_t off_check;
    uint8_t lru[3][8];
    int ret;

    ret = load_buffer(avctx, src, src_size, &gb, &nslices, &off);
    if (ret < 0)
        return ret;

    off_check = off;
    gb_check = gb;
    for (slice = 0; slice < nslices; slice++) {
        slice_size = bytestream2_get_le32(&gb_check);

        if (slice_size <= 16 + (avctx->height * avctx->width / (8 * nslices)))
            return AVERROR_INVALIDDATA;
        off_check += slice_size;
    }

    if (off_check - avctx->discard_damaged_percentage*off_check/100 > src_size)
        return AVERROR_INVALIDDATA;

    avctx->pix_fmt = fmt;
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    do_vflip(avctx, pic, vflipped);

    for (slice = 0; slice < nslices; slice++) {
        slice_size = bytestream2_get_le32(&gb);

        setup_lru(lru);

        ret = check_slice_size(avctx, src, src_size, slice_size, off);
        if (ret < 0)
            return ret;

        if ((ret = init_get_bits8(&gb2, src + off + 16, slice_size - 16)) < 0)
            return ret;

        line += decode_slice(&gb2, pic, line, avctx->height - line, lru);

        off += slice_size;
    }

    if (avctx->height - line) {
        avpriv_request_sample(avctx, "Not enough slice data available");
    }

    do_vflip(avctx, pic, vflipped);

    return 0;
}

av_always_inline
static int dx2_decode_slice_5x5(GetBitContext *gb, AVFrame *frame,
                                int line, int left, uint8_t lru[3][8],
                                int is_565)
{
    int x, y;
    int r, g, b;
    int width    = frame->width;
    int stride   = frame->linesize[0];
    uint8_t *dst = frame->data[0] + stride * line;

    for (y = 0; y < left && get_bits_left(gb) >= 3 * width; y++) {
        for (x = 0; x < width; x++) {
            b = decode_sym_565(gb, lru[0], 5);
            g = decode_sym_565(gb, lru[1], is_565 ? 6 : 5);
            r = decode_sym_565(gb, lru[2], 5);
            dst[x * 3 + 0] = (r << 3) | (r >> 2);
            dst[x * 3 + 1] = is_565 ? (g << 2) | (g >> 4) : (g << 3) | (g >> 2);
            dst[x * 3 + 2] = (b << 3) | (b >> 2);
        }

        dst += stride;
    }

    return y;
}

static void setup_lru_555(uint8_t lru[3][8])
{
    memcpy(lru[0], def_lru_555, 8 * sizeof(*def_lru));
    memcpy(lru[1], def_lru_555, 8 * sizeof(*def_lru));
    memcpy(lru[2], def_lru_555, 8 * sizeof(*def_lru));
}

static void setup_lru_565(uint8_t lru[3][8])
{
    memcpy(lru[0], def_lru_555, 8 * sizeof(*def_lru));
    memcpy(lru[1], def_lru_565, 8 * sizeof(*def_lru));
    memcpy(lru[2], def_lru_555, 8 * sizeof(*def_lru));
}

static int dx2_decode_slice_555(GetBitContext *gb, AVFrame *frame,
                                int line, int left, uint8_t lru[3][8])
{
    return dx2_decode_slice_5x5(gb, frame, line, left, lru, 0);
}

static int dx2_decode_slice_565(GetBitContext *gb, AVFrame *frame,
                                int line, int left, uint8_t lru[3][8])
{
    return dx2_decode_slice_5x5(gb, frame, line, left, lru, 1);
}

static int dxtory_decode_v2_565(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size, int is_565,
                                uint32_t vflipped)
{
    enum AVPixelFormat fmt = AV_PIX_FMT_RGB24;
    if (is_565)
        return dxtory_decode_v2(avctx, pic, src, src_size,
                                dx2_decode_slice_565,
                                setup_lru_565,
                                fmt, vflipped);
    else
        return dxtory_decode_v2(avctx, pic, src, src_size,
                                dx2_decode_slice_555,
                                setup_lru_555,
                                fmt, vflipped);
}

static int dx2_decode_slice_rgb(GetBitContext *gb, AVFrame *frame,
                                int line, int left, uint8_t lru[3][8])
{
    int x, y;
    int width    = frame->width;
    int stride   = frame->linesize[0];
    uint8_t *dst = frame->data[0] + stride * line;

    for (y = 0; y < left && get_bits_left(gb) >= 3 * width; y++) {
        for (x = 0; x < width; x++) {
            dst[x * 3 + 0] = decode_sym(gb, lru[0]);
            dst[x * 3 + 1] = decode_sym(gb, lru[1]);
            dst[x * 3 + 2] = decode_sym(gb, lru[2]);
        }

        dst += stride;
    }

    return y;
}

static void default_setup_lru(uint8_t lru[3][8])
{
    int i;

    for (i = 0; i < 3; i++)
        memcpy(lru[i], def_lru, 8 * sizeof(*def_lru));
}

static int dxtory_decode_v2_rgb(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    return dxtory_decode_v2(avctx, pic, src, src_size,
                            dx2_decode_slice_rgb,
                            default_setup_lru,
                            AV_PIX_FMT_BGR24, vflipped);
}

static int dx2_decode_slice_410(GetBitContext *gb, AVFrame *frame,
                                int line, int left,
                                uint8_t lru[3][8])
{
    int x, y, i, j;
    int width   = frame->width;

    int ystride = frame->linesize[0];
    int ustride = frame->linesize[1];
    int vstride = frame->linesize[2];

    uint8_t *Y  = frame->data[0] + ystride * line;
    uint8_t *U  = frame->data[1] + (ustride >> 2) * line;
    uint8_t *V  = frame->data[2] + (vstride >> 2) * line;

    int h, w, hmargin, vmargin;
    int huvborder;

    h = frame->height & ~3;
    w = frame->width  & ~3;
    hmargin = frame->width  - w;
    vmargin = frame->height - h;
    huvborder = AV_CEIL_RSHIFT(frame->width, 2) - 1;

    for (y = 0; y < left - 3 && get_bits_left(gb) >= 18 * w / 4 + hmargin * 4 + (!!hmargin * 2); y += 4) {
        for (x = 0; x < w; x += 4) {
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    Y[x + i + j * ystride] = decode_sym(gb, lru[0]);
            U[x >> 2] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 2] = decode_sym(gb, lru[2]) ^ 0x80;
        }
        if (hmargin) {
            for (j = 0; j < 4; j++)
                for (i = 0; i < hmargin; i++)
                    Y[x + i + j * ystride] = decode_sym(gb, lru[0]);
            U[huvborder] = decode_sym(gb, lru[1]) ^ 0x80;
            V[huvborder] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride * 4;
        U += ustride;
        V += vstride;
    }

    if (vmargin && y + vmargin == left) {
        for (x = 0; x < width; x += 4) {
            for (j = 0; j < vmargin; j++)
                for (i = 0; i < 4; i++)
                    Y[x + i + j * ystride] = decode_sym(gb, lru[0]);
            U[x >> 2] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 2] = decode_sym(gb, lru[2]) ^ 0x80;
        }
        if (hmargin) {
            for (j = 0; j < vmargin; j++) {
                for (i = 0; i < hmargin; i++)
                    Y[x + i + j * ystride] = decode_sym(gb, lru[0]);
            }
            U[huvborder] = decode_sym(gb, lru[1]) ^ 0x80;
            V[huvborder] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        y += vmargin;
    }

    return y;
}


static int dxtory_decode_v2_410(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    return dxtory_decode_v2(avctx, pic, src, src_size,
                            dx2_decode_slice_410,
                            default_setup_lru,
                            AV_PIX_FMT_YUV410P, vflipped);
}

static int dx2_decode_slice_420(GetBitContext *gb, AVFrame *frame,
                                int line, int left,
                                uint8_t lru[3][8])
{
    int x, y;

    int width    = frame->width;

    int ystride = frame->linesize[0];
    int ustride = frame->linesize[1];
    int vstride = frame->linesize[2];

    uint8_t *Y  = frame->data[0] + ystride * line;
    uint8_t *U  = frame->data[1] + (ustride >> 1) * line;
    uint8_t *V  = frame->data[2] + (vstride >> 1) * line;

    int h, w, hmargin, vmargin;
    int huvborder;

    h = frame->height & ~1;
    w = frame->width  & ~1;
    hmargin = frame->width  - w;
    vmargin = frame->height - h;
    huvborder = AV_CEIL_RSHIFT(frame->width, 1) - 1;

    for (y = 0; y < left - 1 && get_bits_left(gb) >= 3 * w + hmargin * 4; y += 2) {
        for (x = 0; x < w; x += 2) {
            Y[x + 0 + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 0 + 1 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 + 1 * ystride] = decode_sym(gb, lru[0]);
            U[x >> 1] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 1] = decode_sym(gb, lru[2]) ^ 0x80;
        }
        if (hmargin) {
            Y[x + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 * ystride] = decode_sym(gb, lru[0]);
            U[huvborder] = decode_sym(gb, lru[1]) ^ 0x80;
            V[huvborder] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride * 2;
        U += ustride;
        V += vstride;
    }

    if (vmargin) {
        for (x = 0; x < width; x += 2) {
            Y[x + 0]  = decode_sym(gb, lru[0]);
            U[x >> 1] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 1] = decode_sym(gb, lru[2]) ^ 0x80;
        }
        if (hmargin) {
            Y[x]         = decode_sym(gb, lru[0]);
            U[huvborder] = decode_sym(gb, lru[1]) ^ 0x80;
            V[huvborder] = decode_sym(gb, lru[2]) ^ 0x80;
        }
    }

    return y;
}

static int dxtory_decode_v2_420(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    return dxtory_decode_v2(avctx, pic, src, src_size,
                            dx2_decode_slice_420,
                            default_setup_lru,
                            AV_PIX_FMT_YUV420P, vflipped);
}

static int dx2_decode_slice_444(GetBitContext *gb, AVFrame *frame,
                                int line, int left,
                                uint8_t lru[3][8])
{
    int x, y;

    int width   = frame->width;

    int ystride = frame->linesize[0];
    int ustride = frame->linesize[1];
    int vstride = frame->linesize[2];

    uint8_t *Y  = frame->data[0] + ystride * line;
    uint8_t *U  = frame->data[1] + ustride * line;
    uint8_t *V  = frame->data[2] + vstride * line;

    for (y = 0; y < left && get_bits_left(gb) >= 3 * width; y++) {
        for (x = 0; x < width; x++) {
            Y[x] = decode_sym(gb, lru[0]);
            U[x] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride;
        U += ustride;
        V += vstride;
    }

    return y;
}

static int dxtory_decode_v2_444(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                uint32_t vflipped)
{
    return dxtory_decode_v2(avctx, pic, src, src_size,
                            dx2_decode_slice_444,
                            default_setup_lru,
                            AV_PIX_FMT_YUV444P, vflipped);
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AVFrame *pic = data;
    const uint8_t *src = avpkt->data;
    uint32_t type;
    int vflipped, ret;

    if (avpkt->size < 16) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    type = AV_RB32(src);
    vflipped = !!(type & 0x20);

    switch (type) {
    case 0x01000021:
    case 0x01000001:
        ret = dxtory_decode_v1_rgb(avctx, pic, src + 16, avpkt->size - 16,
                                   AV_PIX_FMT_BGR24, 3, vflipped);
        break;
    case 0x01000029:
    case 0x01000009:
        ret = dxtory_decode_v2_rgb(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x02000021:
    case 0x02000001:
        ret = dxtory_decode_v1_420(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x02000029:
    case 0x02000009:
        ret = dxtory_decode_v2_420(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x03000021:
    case 0x03000001:
        ret = dxtory_decode_v1_410(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x03000029:
    case 0x03000009:
        ret = dxtory_decode_v2_410(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x04000021:
    case 0x04000001:
        ret = dxtory_decode_v1_444(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x04000029:
    case 0x04000009:
        ret = dxtory_decode_v2_444(avctx, pic, src + 16, avpkt->size - 16, vflipped);
        break;
    case 0x17000021:
    case 0x17000001:
        ret = dxtory_decode_v1_rgb(avctx, pic, src + 16, avpkt->size - 16,
                                   AV_PIX_FMT_RGB565LE, 2, vflipped);
        break;
    case 0x17000029:
    case 0x17000009:
        ret = dxtory_decode_v2_565(avctx, pic, src + 16, avpkt->size - 16, 1, vflipped);
        break;
    case 0x18000021:
    case 0x19000021:
    case 0x18000001:
    case 0x19000001:
        ret = dxtory_decode_v1_rgb(avctx, pic, src + 16, avpkt->size - 16,
                                   AV_PIX_FMT_RGB555LE, 2, vflipped);
        break;
    case 0x18000029:
    case 0x19000029:
    case 0x18000009:
    case 0x19000009:
        ret = dxtory_decode_v2_565(avctx, pic, src + 16, avpkt->size - 16, 0, vflipped);
        break;
    default:
        avpriv_request_sample(avctx, "Frame header %"PRIX32, type);
        return AVERROR_PATCHWELCOME;
    }

    if (ret)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

const AVCodec ff_dxtory_decoder = {
    .name           = "dxtory",
    .long_name      = NULL_IF_CONFIG_SMALL("Dxtory"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DXTORY,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
