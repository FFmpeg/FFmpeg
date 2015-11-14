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

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "unary.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

static int dxtory_decode_v1_rgb(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size,
                                int id, int bpp)
{
    int h;
    uint8_t *dst;
    int ret;

    if (src_size < avctx->width * avctx->height * (int64_t)bpp) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = id;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    dst = pic->data[0];
    for (h = 0; h < avctx->height; h++) {
        memcpy(dst, src, avctx->width * bpp);
        src += avctx->width * bpp;
        dst += pic->linesize[0];
    }

    return 0;
}

static int dxtory_decode_v1_410(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    int h, w;
    uint8_t *Y1, *Y2, *Y3, *Y4, *U, *V;
    int ret;

    if (src_size < FFALIGN(avctx->width, 4) * FFALIGN(avctx->height, 4) * 9LL / 8) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV410P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y1 = pic->data[0];
    Y2 = pic->data[0] + pic->linesize[0];
    Y3 = pic->data[0] + pic->linesize[0] * 2;
    Y4 = pic->data[0] + pic->linesize[0] * 3;
    U  = pic->data[1];
    V  = pic->data[2];
    for (h = 0; h < avctx->height; h += 4) {
        for (w = 0; w < avctx->width; w += 4) {
            AV_COPY32U(Y1 + w, src);
            AV_COPY32U(Y2 + w, src + 4);
            AV_COPY32U(Y3 + w, src + 8);
            AV_COPY32U(Y4 + w, src + 12);
            U[w >> 2] = src[16] + 0x80;
            V[w >> 2] = src[17] + 0x80;
            src += 18;
        }
        Y1 += pic->linesize[0] << 2;
        Y2 += pic->linesize[0] << 2;
        Y3 += pic->linesize[0] << 2;
        Y4 += pic->linesize[0] << 2;
        U  += pic->linesize[1];
        V  += pic->linesize[2];
    }

    return 0;
}

static int dxtory_decode_v1_420(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    int h, w;
    uint8_t *Y1, *Y2, *U, *V;
    int ret;

    if (src_size < FFALIGN(avctx->width, 2) * FFALIGN(avctx->height, 2) * 3LL / 2) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y1 = pic->data[0];
    Y2 = pic->data[0] + pic->linesize[0];
    U  = pic->data[1];
    V  = pic->data[2];
    for (h = 0; h < avctx->height; h += 2) {
        for (w = 0; w < avctx->width; w += 2) {
            AV_COPY16(Y1 + w, src);
            AV_COPY16(Y2 + w, src + 2);
            U[w >> 1] = src[4] + 0x80;
            V[w >> 1] = src[5] + 0x80;
            src += 6;
        }
        Y1 += pic->linesize[0] << 1;
        Y2 += pic->linesize[0] << 1;
        U  += pic->linesize[1];
        V  += pic->linesize[2];
    }

    return 0;
}

static int dxtory_decode_v1_444(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    int h, w;
    uint8_t *Y, *U, *V;
    int ret;

    if (src_size < avctx->width * avctx->height * 3LL) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV444P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

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

static int dx2_decode_slice_565(GetBitContext *gb, int width, int height,
                                uint8_t *dst, int stride, int is_565)
{
    int x, y;
    int r, g, b;
    uint8_t lru[3][8];

    memcpy(lru[0], def_lru_555, 8 * sizeof(*def_lru));
    memcpy(lru[1], is_565 ? def_lru_565 : def_lru_555, 8 * sizeof(*def_lru));
    memcpy(lru[2], def_lru_555, 8 * sizeof(*def_lru));

    for (y = 0; y < height; y++) {
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

    return 0;
}

static int dxtory_decode_v2_565(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size, int is_565)
{
    GetByteContext gb;
    GetBitContext  gb2;
    int nslices, slice, slice_height;
    uint32_t off, slice_size;
    uint8_t *dst;
    int ret;

    bytestream2_init(&gb, src, src_size);
    nslices = bytestream2_get_le16(&gb);
    off = FFALIGN(nslices * 4 + 2, 16);
    if (src_size < off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!nslices || avctx->height % nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", nslices,
                              avctx->width, avctx->height);
        return AVERROR_PATCHWELCOME;
    }

    slice_height = avctx->height / nslices;
    avctx->pix_fmt = AV_PIX_FMT_RGB24;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    dst = pic->data[0];
    for (slice = 0; slice < nslices; slice++) {
        slice_size = bytestream2_get_le32(&gb);
        if (slice_size > src_size - off) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid slice size %"PRIu32" (only %"PRIu32" bytes left)\n",
                   slice_size, src_size - off);
            return AVERROR_INVALIDDATA;
        }
        if (slice_size <= 16) {
            av_log(avctx, AV_LOG_ERROR, "invalid slice size %"PRIu32"\n", slice_size);
            return AVERROR_INVALIDDATA;
        }

        if (AV_RL32(src + off) != slice_size - 16) {
            av_log(avctx, AV_LOG_ERROR,
                   "Slice sizes mismatch: got %"PRIu32" instead of %"PRIu32"\n",
                   AV_RL32(src + off), slice_size - 16);
        }
        if ((ret = init_get_bits8(&gb2, src + off + 16, slice_size - 16)) < 0)
            return ret;
        dx2_decode_slice_565(&gb2, avctx->width, slice_height, dst,
                             pic->linesize[0], is_565);

        dst += pic->linesize[0] * slice_height;
        off += slice_size;
    }

    return 0;
}

static int dx2_decode_slice_rgb(GetBitContext *gb, int width, int height,
                                uint8_t *dst, int stride)
{
    int x, y, i;
    uint8_t lru[3][8];

    for (i = 0; i < 3; i++)
        memcpy(lru[i], def_lru, 8 * sizeof(*def_lru));

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[x * 3 + 0] = decode_sym(gb, lru[0]);
            dst[x * 3 + 1] = decode_sym(gb, lru[1]);
            dst[x * 3 + 2] = decode_sym(gb, lru[2]);
        }

        dst += stride;
    }

    return 0;
}

static int dxtory_decode_v2_rgb(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    GetByteContext gb;
    GetBitContext  gb2;
    int nslices, slice, slice_height;
    uint32_t off, slice_size;
    uint8_t *dst;
    int ret;

    bytestream2_init(&gb, src, src_size);
    nslices = bytestream2_get_le16(&gb);
    off = FFALIGN(nslices * 4 + 2, 16);
    if (src_size < off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!nslices || avctx->height % nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", nslices,
                              avctx->width, avctx->height);
        return AVERROR_PATCHWELCOME;
    }

    slice_height = avctx->height / nslices;
    avctx->pix_fmt = AV_PIX_FMT_BGR24;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    dst = pic->data[0];
    for (slice = 0; slice < nslices; slice++) {
        slice_size = bytestream2_get_le32(&gb);
        if (slice_size > src_size - off) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid slice size %"PRIu32" (only %"PRIu32" bytes left)\n",
                   slice_size, src_size - off);
            return AVERROR_INVALIDDATA;
        }
        if (slice_size <= 16) {
            av_log(avctx, AV_LOG_ERROR, "invalid slice size %"PRIu32"\n",
                   slice_size);
            return AVERROR_INVALIDDATA;
        }

        if (AV_RL32(src + off) != slice_size - 16) {
            av_log(avctx, AV_LOG_ERROR,
                   "Slice sizes mismatch: got %"PRIu32" instead of %"PRIu32"\n",
                   AV_RL32(src + off), slice_size - 16);
        }
        if ((ret = init_get_bits8(&gb2, src + off + 16, slice_size - 16)) < 0)
            return ret;
        dx2_decode_slice_rgb(&gb2, avctx->width, slice_height, dst,
                             pic->linesize[0]);

        dst += pic->linesize[0] * slice_height;
        off += slice_size;
    }

    return 0;
}

static int dx2_decode_slice_410(GetBitContext *gb, int width, int height,
                                uint8_t *Y, uint8_t *U, uint8_t *V,
                                int ystride, int ustride, int vstride)
{
    int x, y, i, j;
    uint8_t lru[3][8];

    for (i = 0; i < 3; i++)
        memcpy(lru[i], def_lru, 8 * sizeof(*def_lru));

    for (y = 0; y < height; y += 4) {
        for (x = 0; x < width; x += 4) {
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    Y[x + i + j * ystride] = decode_sym(gb, lru[0]);
            U[x >> 2] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 2] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride << 2;
        U += ustride;
        V += vstride;
    }

    return 0;
}

static int dxtory_decode_v2_410(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    GetByteContext gb;
    GetBitContext  gb2;
    int nslices, slice, slice_height;
    int cur_y, next_y;
    uint32_t off, slice_size;
    uint8_t *Y, *U, *V;
    int ret;

    bytestream2_init(&gb, src, src_size);
    nslices = bytestream2_get_le16(&gb);
    off = FFALIGN(nslices * 4 + 2, 16);
    if (src_size < off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", nslices,
                              avctx->width, avctx->height);
        return AVERROR_PATCHWELCOME;
    }

    if ((avctx->width & 3) || (avctx->height & 3)) {
        avpriv_request_sample(avctx, "Frame dimensions %dx%d",
                              avctx->width, avctx->height);
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV410P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y = pic->data[0];
    U = pic->data[1];
    V = pic->data[2];

    cur_y  = 0;
    for (slice = 0; slice < nslices; slice++) {
        slice_size   = bytestream2_get_le32(&gb);
        next_y = ((slice + 1) * avctx->height) / nslices;
        slice_height = (next_y & ~3) - (cur_y & ~3);
        if (slice_size > src_size - off) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid slice size %"PRIu32" (only %"PRIu32" bytes left)\n",
                   slice_size, src_size - off);
            return AVERROR_INVALIDDATA;
        }
        if (slice_size <= 16) {
            av_log(avctx, AV_LOG_ERROR, "invalid slice size %"PRIu32"\n", slice_size);
            return AVERROR_INVALIDDATA;
        }

        if (AV_RL32(src + off) != slice_size - 16) {
            av_log(avctx, AV_LOG_ERROR,
                   "Slice sizes mismatch: got %"PRIu32" instead of %"PRIu32"\n",
                   AV_RL32(src + off), slice_size - 16);
        }
        if ((ret = init_get_bits8(&gb2, src + off + 16, slice_size - 16)) < 0)
            return ret;
        dx2_decode_slice_410(&gb2, avctx->width, slice_height, Y, U, V,
                             pic->linesize[0], pic->linesize[1],
                             pic->linesize[2]);

        Y += pic->linesize[0] *  slice_height;
        U += pic->linesize[1] * (slice_height >> 2);
        V += pic->linesize[2] * (slice_height >> 2);
        off += slice_size;
        cur_y   = next_y;
    }

    return 0;
}

static int dx2_decode_slice_420(GetBitContext *gb, int width, int height,
                                uint8_t *Y, uint8_t *U, uint8_t *V,
                                int ystride, int ustride, int vstride)
{
    int x, y, i;
    uint8_t lru[3][8];

    for (i = 0; i < 3; i++)
        memcpy(lru[i], def_lru, 8 * sizeof(*def_lru));

    for (y = 0; y < height; y+=2) {
        for (x = 0; x < width; x += 2) {
            Y[x + 0 + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 0 + 1 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 + 1 * ystride] = decode_sym(gb, lru[0]);
            U[x >> 1] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 1] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride << 1;
        U += ustride;
        V += vstride;
    }

    return 0;
}

static int dxtory_decode_v2_420(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    GetByteContext gb;
    GetBitContext  gb2;
    int nslices, slice, slice_height;
    int cur_y, next_y;
    uint32_t off, slice_size;
    uint8_t *Y, *U, *V;
    int ret;

    bytestream2_init(&gb, src, src_size);
    nslices = bytestream2_get_le16(&gb);
    off = FFALIGN(nslices * 4 + 2, 16);
    if (src_size < off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", nslices,
                              avctx->width, avctx->height);
        return AVERROR_PATCHWELCOME;
    }

    if ((avctx->width & 1) || (avctx->height & 1)) {
        avpriv_request_sample(avctx, "Frame dimensions %dx%d",
                              avctx->width, avctx->height);
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y = pic->data[0];
    U = pic->data[1];
    V = pic->data[2];

    cur_y  = 0;
    for (slice = 0; slice < nslices; slice++) {
        slice_size   = bytestream2_get_le32(&gb);
        next_y = ((slice + 1) * avctx->height) / nslices;
        slice_height = (next_y & ~1) - (cur_y & ~1);
        if (slice_size > src_size - off) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid slice size %"PRIu32" (only %"PRIu32" bytes left)\n",
                   slice_size, src_size - off);
            return AVERROR_INVALIDDATA;
        }
        if (slice_size <= 16) {
            av_log(avctx, AV_LOG_ERROR, "invalid slice size %"PRIu32"\n", slice_size);
            return AVERROR_INVALIDDATA;
        }

        if (AV_RL32(src + off) != slice_size - 16) {
            av_log(avctx, AV_LOG_ERROR,
                   "Slice sizes mismatch: got %"PRIu32" instead of %"PRIu32"\n",
                   AV_RL32(src + off), slice_size - 16);
        }
        if ((ret = init_get_bits8(&gb2, src + off + 16, slice_size - 16)) < 0)
            return ret;
        dx2_decode_slice_420(&gb2, avctx->width, slice_height, Y, U, V,
                             pic->linesize[0], pic->linesize[1],
                             pic->linesize[2]);

        Y += pic->linesize[0] *  slice_height;
        U += pic->linesize[1] * (slice_height >> 1);
        V += pic->linesize[2] * (slice_height >> 1);
        off += slice_size;
        cur_y   = next_y;
    }

    return 0;
}

static int dx2_decode_slice_444(GetBitContext *gb, int width, int height,
                                uint8_t *Y, uint8_t *U, uint8_t *V,
                                int ystride, int ustride, int vstride)
{
    int x, y, i;
    uint8_t lru[3][8];

    for (i = 0; i < 3; i++)
        memcpy(lru[i], def_lru, 8 * sizeof(*def_lru));

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            Y[x] = decode_sym(gb, lru[0]);
            U[x] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride;
        U += ustride;
        V += vstride;
    }

    return 0;
}

static int dxtory_decode_v2_444(AVCodecContext *avctx, AVFrame *pic,
                                const uint8_t *src, int src_size)
{
    GetByteContext gb;
    GetBitContext  gb2;
    int nslices, slice, slice_height;
    uint32_t off, slice_size;
    uint8_t *Y, *U, *V;
    int ret;

    bytestream2_init(&gb, src, src_size);
    nslices = bytestream2_get_le16(&gb);
    off = FFALIGN(nslices * 4 + 2, 16);
    if (src_size < off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!nslices || avctx->height % nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", nslices,
                              avctx->width, avctx->height);
        return AVERROR_PATCHWELCOME;
    }

    slice_height = avctx->height / nslices;

    avctx->pix_fmt = AV_PIX_FMT_YUV444P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y = pic->data[0];
    U = pic->data[1];
    V = pic->data[2];

    for (slice = 0; slice < nslices; slice++) {
        slice_size = bytestream2_get_le32(&gb);
        if (slice_size > src_size - off) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid slice size %"PRIu32" (only %"PRIu32" bytes left)\n",
                   slice_size, src_size - off);
            return AVERROR_INVALIDDATA;
        }
        if (slice_size <= 16) {
            av_log(avctx, AV_LOG_ERROR, "invalid slice size %"PRIu32"\n", slice_size);
            return AVERROR_INVALIDDATA;
        }

        if (AV_RL32(src + off) != slice_size - 16) {
            av_log(avctx, AV_LOG_ERROR,
                   "Slice sizes mismatch: got %"PRIu32" instead of %"PRIu32"\n",
                   AV_RL32(src + off), slice_size - 16);
        }
        if ((ret = init_get_bits8(&gb2, src + off + 16, slice_size - 16)) < 0)
            return ret;
        dx2_decode_slice_444(&gb2, avctx->width, slice_height, Y, U, V,
                             pic->linesize[0], pic->linesize[1],
                             pic->linesize[2]);

        Y += pic->linesize[0] * slice_height;
        U += pic->linesize[1] * slice_height;
        V += pic->linesize[2] * slice_height;
        off += slice_size;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AVFrame *pic = data;
    const uint8_t *src = avpkt->data;
    int ret;

    if (avpkt->size < 16) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    switch (AV_RB32(src)) {
    case 0x01000001:
        ret = dxtory_decode_v1_rgb(avctx, pic, src + 16, avpkt->size - 16,
                                   AV_PIX_FMT_BGR24, 3);
        break;
    case 0x01000009:
        ret = dxtory_decode_v2_rgb(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x02000001:
        ret = dxtory_decode_v1_420(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x02000009:
        ret = dxtory_decode_v2_420(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x03000001:
        ret = dxtory_decode_v1_410(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x03000009:
        ret = dxtory_decode_v2_410(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x04000001:
        ret = dxtory_decode_v1_444(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x04000009:
        ret = dxtory_decode_v2_444(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x17000001:
        ret = dxtory_decode_v1_rgb(avctx, pic, src + 16, avpkt->size - 16,
                                   AV_PIX_FMT_RGB565LE, 2);
        break;
    case 0x17000009:
        ret = dxtory_decode_v2_565(avctx, pic, src + 16, avpkt->size - 16, 1);
        break;
    case 0x18000001:
    case 0x19000001:
        ret = dxtory_decode_v1_rgb(avctx, pic, src + 16, avpkt->size - 16,
                                   AV_PIX_FMT_RGB555LE, 2);
        break;
    case 0x18000009:
    case 0x19000009:
        ret = dxtory_decode_v2_565(avctx, pic, src + 16, avpkt->size - 16, 0);
        break;
    default:
        avpriv_request_sample(avctx, "Frame header %"PRIX32, AV_RB32(src));
        return AVERROR_PATCHWELCOME;
    }

    if (ret)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_dxtory_decoder = {
    .name           = "dxtory",
    .long_name      = NULL_IF_CONFIG_SMALL("Dxtory"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DXTORY,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
