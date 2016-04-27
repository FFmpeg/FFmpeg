/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avutil.h"
#include "libavutil/bswap.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "config.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"

DECLARE_ALIGNED(8, static const uint8_t, dither_2x2_4)[2][8]={
{  1,   3,   1,   3,   1,   3,   1,   3, },
{  2,   0,   2,   0,   2,   0,   2,   0, },
};

DECLARE_ALIGNED(8, static const uint8_t, dither_2x2_8)[2][8]={
{  6,   2,   6,   2,   6,   2,   6,   2, },
{  0,   4,   0,   4,   0,   4,   0,   4, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_4x4_16)[4][8] = {
{  8,   4,  11,   7,   8,   4,  11,   7, },
{  2,  14,   1,  13,   2,  14,   1,  13, },
{ 10,   6,   9,   5,  10,   6,   9,   5, },
{  0,  12,   3,  15,   0,  12,   3,  15, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_32)[8][8] = {
{ 17,   9,  23,  15,  16,   8,  22,  14, },
{  5,  29,   3,  27,   4,  28,   2,  26, },
{ 21,  13,  19,  11,  20,  12,  18,  10, },
{  0,  24,   6,  30,   1,  25,   7,  31, },
{ 16,   8,  22,  14,  17,   9,  23,  15, },
{  4,  28,   2,  26,   5,  29,   3,  27, },
{ 20,  12,  18,  10,  21,  13,  19,  11, },
{  1,  25,   7,  31,   0,  24,   6,  30, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_73)[8][8] = {
{  0,  55,  14,  68,   3,  58,  17,  72, },
{ 37,  18,  50,  32,  40,  22,  54,  35, },
{  9,  64,   5,  59,  13,  67,   8,  63, },
{ 46,  27,  41,  23,  49,  31,  44,  26, },
{  2,  57,  16,  71,   1,  56,  15,  70, },
{ 39,  21,  52,  34,  38,  19,  51,  33, },
{ 11,  66,   7,  62,  10,  65,   6,  60, },
{ 48,  30,  43,  25,  47,  29,  42,  24, },
};

#if 1
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[8][8] = {
{117,  62, 158, 103, 113,  58, 155, 100, },
{ 34, 199,  21, 186,  31, 196,  17, 182, },
{144,  89, 131,  76, 141,  86, 127,  72, },
{  0, 165,  41, 206,  10, 175,  52, 217, },
{110,  55, 151,  96, 120,  65, 162, 107, },
{ 28, 193,  14, 179,  38, 203,  24, 189, },
{138,  83, 124,  69, 148,  93, 134,  79, },
{  7, 172,  48, 213,   3, 168,  45, 210, },
};
#elif 1
// tries to correct a gamma of 1.5
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[8][8] = {
{  0, 143,  18, 200,   2, 156,  25, 215, },
{ 78,  28, 125,  64,  89,  36, 138,  74, },
{ 10, 180,   3, 161,  16, 195,   8, 175, },
{109,  51,  93,  38, 121,  60, 105,  47, },
{  1, 152,  23, 210,   0, 147,  20, 205, },
{ 85,  33, 134,  71,  81,  30, 130,  67, },
{ 14, 190,   6, 171,  12, 185,   5, 166, },
{117,  57, 101,  44, 113,  54,  97,  41, },
};
#elif 1
// tries to correct a gamma of 2.0
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[8][8] = {
{  0, 124,   8, 193,   0, 140,  12, 213, },
{ 55,  14, 104,  42,  66,  19, 119,  52, },
{  3, 168,   1, 145,   6, 187,   3, 162, },
{ 86,  31,  70,  21,  99,  39,  82,  28, },
{  0, 134,  11, 206,   0, 129,   9, 200, },
{ 62,  17, 114,  48,  58,  16, 109,  45, },
{  5, 181,   2, 157,   4, 175,   1, 151, },
{ 95,  36,  78,  26,  90,  34,  74,  24, },
};
#else
// tries to correct a gamma of 2.5
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[8][8] = {
{  0, 107,   3, 187,   0, 125,   6, 212, },
{ 39,   7,  86,  28,  49,  11, 102,  36, },
{  1, 158,   0, 131,   3, 180,   1, 151, },
{ 68,  19,  52,  12,  81,  25,  64,  17, },
{  0, 119,   5, 203,   0, 113,   4, 195, },
{ 45,   9,  96,  33,  42,   8,  91,  30, },
{  2, 172,   1, 144,   2, 165,   0, 137, },
{ 77,  23,  60,  15,  72,  21,  56,  14, },
};
#endif

#define output_pixel(pos, val, bias, signedness) \
    if (big_endian) { \
        AV_WB16(pos, bias + av_clip_ ## signedness ## 16(val >> shift)); \
    } else { \
        AV_WL16(pos, bias + av_clip_ ## signedness ## 16(val >> shift)); \
    }

static av_always_inline void
yuv2plane1_16_c_template(const int32_t *src, uint16_t *dest, int dstW,
                         int big_endian, int output_bits)
{
    int i;
    int shift = 19 - output_bits;

    for (i = 0; i < dstW; i++) {
        int val = src[i] + (1 << (shift - 1));
        output_pixel(&dest[i], val, 0, uint);
    }
}

static av_always_inline void
yuv2planeX_16_c_template(const int16_t *filter, int filterSize,
                         const int32_t **src, uint16_t *dest, int dstW,
                         int big_endian, int output_bits)
{
    int i;
    int shift = 15 + 16 - output_bits;

    for (i = 0; i < dstW; i++) {
        int val = 1 << (30-output_bits);
        int j;

        /* range of val is [0,0x7FFFFFFF], so 31 bits, but with lanczos/spline
         * filters (or anything with negative coeffs, the range can be slightly
         * wider in both directions. To account for this overflow, we subtract
         * a constant so it always fits in the signed range (assuming a
         * reasonable filterSize), and re-add that at the end. */
        val -= 0x40000000;
        for (j = 0; j < filterSize; j++)
            val += src[j][i] * filter[j];

        output_pixel(&dest[i], val, 0x8000, int);
    }
}

#undef output_pixel

#define output_pixel(pos, val) \
    if (big_endian) { \
        AV_WB16(pos, av_clip_uintp2(val >> shift, output_bits)); \
    } else { \
        AV_WL16(pos, av_clip_uintp2(val >> shift, output_bits)); \
    }

static av_always_inline void
yuv2plane1_10_c_template(const int16_t *src, uint16_t *dest, int dstW,
                         int big_endian, int output_bits)
{
    int i;
    int shift = 15 - output_bits;

    for (i = 0; i < dstW; i++) {
        int val = src[i] + (1 << (shift - 1));
        output_pixel(&dest[i], val);
    }
}

static av_always_inline void
yuv2planeX_10_c_template(const int16_t *filter, int filterSize,
                         const int16_t **src, uint16_t *dest, int dstW,
                         int big_endian, int output_bits)
{
    int i;
    int shift = 11 + 16 - output_bits;

    for (i = 0; i < dstW; i++) {
        int val = 1 << (26-output_bits);
        int j;

        for (j = 0; j < filterSize; j++)
            val += src[j][i] * filter[j];

        output_pixel(&dest[i], val);
    }
}

#undef output_pixel

#define yuv2NBPS(bits, BE_LE, is_be, template_size, typeX_t) \
static void yuv2plane1_ ## bits ## BE_LE ## _c(const int16_t *src, \
                              uint8_t *dest, int dstW, \
                              const uint8_t *dither, int offset)\
{ \
    yuv2plane1_ ## template_size ## _c_template((const typeX_t *) src, \
                         (uint16_t *) dest, dstW, is_be, bits); \
}\
static void yuv2planeX_ ## bits ## BE_LE ## _c(const int16_t *filter, int filterSize, \
                              const int16_t **src, uint8_t *dest, int dstW, \
                              const uint8_t *dither, int offset)\
{ \
    yuv2planeX_## template_size ## _c_template(filter, \
                         filterSize, (const typeX_t **) src, \
                         (uint16_t *) dest, dstW, is_be, bits); \
}
yuv2NBPS( 9, BE, 1, 10, int16_t)
yuv2NBPS( 9, LE, 0, 10, int16_t)
yuv2NBPS(10, BE, 1, 10, int16_t)
yuv2NBPS(10, LE, 0, 10, int16_t)
yuv2NBPS(16, BE, 1, 16, int32_t)
yuv2NBPS(16, LE, 0, 16, int32_t)

static void yuv2planeX_8_c(const int16_t *filter, int filterSize,
                           const int16_t **src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset)
{
    int i;
    for (i=0; i<dstW; i++) {
        int val = dither[(i + offset) & 7] << 12;
        int j;
        for (j=0; j<filterSize; j++)
            val += src[j][i] * filter[j];

        dest[i]= av_clip_uint8(val>>19);
    }
}

static void yuv2plane1_8_c(const int16_t *src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset)
{
    int i;
    for (i=0; i<dstW; i++) {
        int val = (src[i] + dither[(i + offset) & 7]) >> 7;
        dest[i]= av_clip_uint8(val);
    }
}

static void yuv2nv12cX_c(SwsContext *c, const int16_t *chrFilter, int chrFilterSize,
                        const int16_t **chrUSrc, const int16_t **chrVSrc,
                        uint8_t *dest, int chrDstW)
{
    enum AVPixelFormat dstFormat = c->dstFormat;
    const uint8_t *chrDither = c->chrDither8;
    int i;

    if (dstFormat == AV_PIX_FMT_NV12)
        for (i=0; i<chrDstW; i++) {
            int u = chrDither[i & 7] << 12;
            int v = chrDither[(i + 3) & 7] << 12;
            int j;
            for (j=0; j<chrFilterSize; j++) {
                u += chrUSrc[j][i] * chrFilter[j];
                v += chrVSrc[j][i] * chrFilter[j];
            }

            dest[2*i]= av_clip_uint8(u>>19);
            dest[2*i+1]= av_clip_uint8(v>>19);
        }
    else
        for (i=0; i<chrDstW; i++) {
            int u = chrDither[i & 7] << 12;
            int v = chrDither[(i + 3) & 7] << 12;
            int j;
            for (j=0; j<chrFilterSize; j++) {
                u += chrUSrc[j][i] * chrFilter[j];
                v += chrVSrc[j][i] * chrFilter[j];
            }

            dest[2*i]= av_clip_uint8(v>>19);
            dest[2*i+1]= av_clip_uint8(u>>19);
        }
}

#define accumulate_bit(acc, val) \
    acc <<= 1; \
    acc |= (val) >= (128 + 110)
#define output_pixel(pos, acc) \
    if (target == AV_PIX_FMT_MONOBLACK) { \
        pos = acc; \
    } else { \
        pos = ~acc; \
    }

static av_always_inline void
yuv2mono_X_c_template(SwsContext *c, const int16_t *lumFilter,
                      const int16_t **lumSrc, int lumFilterSize,
                      const int16_t *chrFilter, const int16_t **chrUSrc,
                      const int16_t **chrVSrc, int chrFilterSize,
                      const int16_t **alpSrc, uint8_t *dest, int dstW,
                      int y, enum AVPixelFormat target)
{
    const uint8_t * const d128 = ff_dither_8x8_220[y&7];
    int i;
    unsigned acc = 0;

    for (i = 0; i < dstW; i += 2) {
        int j;
        int Y1 = 1 << 18;
        int Y2 = 1 << 18;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i]   * lumFilter[j];
            Y2 += lumSrc[j][i+1] * lumFilter[j];
        }
        Y1 >>= 19;
        Y2 >>= 19;
        if ((Y1 | Y2) & 0x100) {
            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
        }
        accumulate_bit(acc, Y1 + d128[(i + 0) & 7]);
        accumulate_bit(acc, Y2 + d128[(i + 1) & 7]);
        if ((i & 7) == 6) {
            output_pixel(*dest++, acc);
        }
    }

    if (i & 6) {
        output_pixel(*dest, acc);
    }
}

static av_always_inline void
yuv2mono_2_c_template(SwsContext *c, const int16_t *buf[2],
                      const int16_t *ubuf[2], const int16_t *vbuf[2],
                      const int16_t *abuf[2], uint8_t *dest, int dstW,
                      int yalpha, int uvalpha, int y,
                      enum AVPixelFormat target)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1];
    const uint8_t * const d128 = ff_dither_8x8_220[y & 7];
    int  yalpha1 = 4096 - yalpha;
    int i;

    for (i = 0; i < dstW; i += 8) {
        int Y, acc = 0;

        Y = (buf0[i + 0] * yalpha1 + buf1[i + 0] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[0]);
        Y = (buf0[i + 1] * yalpha1 + buf1[i + 1] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[1]);
        Y = (buf0[i + 2] * yalpha1 + buf1[i + 2] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[2]);
        Y = (buf0[i + 3] * yalpha1 + buf1[i + 3] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[3]);
        Y = (buf0[i + 4] * yalpha1 + buf1[i + 4] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[4]);
        Y = (buf0[i + 5] * yalpha1 + buf1[i + 5] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[5]);
        Y = (buf0[i + 6] * yalpha1 + buf1[i + 6] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[6]);
        Y = (buf0[i + 7] * yalpha1 + buf1[i + 7] * yalpha) >> 19;
        accumulate_bit(acc, Y + d128[7]);

        output_pixel(*dest++, acc);
    }
}

static av_always_inline void
yuv2mono_1_c_template(SwsContext *c, const int16_t *buf0,
                      const int16_t *ubuf[2], const int16_t *vbuf[2],
                      const int16_t *abuf0, uint8_t *dest, int dstW,
                      int uvalpha, int y, enum AVPixelFormat target)
{
    const uint8_t * const d128 = ff_dither_8x8_220[y & 7];
    int i;

    for (i = 0; i < dstW; i += 8) {
        int acc = 0;

        accumulate_bit(acc, (buf0[i + 0] >> 7) + d128[0]);
        accumulate_bit(acc, (buf0[i + 1] >> 7) + d128[1]);
        accumulate_bit(acc, (buf0[i + 2] >> 7) + d128[2]);
        accumulate_bit(acc, (buf0[i + 3] >> 7) + d128[3]);
        accumulate_bit(acc, (buf0[i + 4] >> 7) + d128[4]);
        accumulate_bit(acc, (buf0[i + 5] >> 7) + d128[5]);
        accumulate_bit(acc, (buf0[i + 6] >> 7) + d128[6]);
        accumulate_bit(acc, (buf0[i + 7] >> 7) + d128[7]);

        output_pixel(*dest++, acc);
    }
}

#undef output_pixel
#undef accumulate_bit

#define YUV2PACKEDWRAPPER(name, base, ext, fmt) \
static void name ## ext ## _X_c(SwsContext *c, const int16_t *lumFilter, \
                                const int16_t **lumSrc, int lumFilterSize, \
                                const int16_t *chrFilter, const int16_t **chrUSrc, \
                                const int16_t **chrVSrc, int chrFilterSize, \
                                const int16_t **alpSrc, uint8_t *dest, int dstW, \
                                int y) \
{ \
    name ## base ## _X_c_template(c, lumFilter, lumSrc, lumFilterSize, \
                                  chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                                  alpSrc, dest, dstW, y, fmt); \
} \
 \
static void name ## ext ## _2_c(SwsContext *c, const int16_t *buf[2], \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf[2], uint8_t *dest, int dstW, \
                                int yalpha, int uvalpha, int y) \
{ \
    name ## base ## _2_c_template(c, buf, ubuf, vbuf, abuf, \
                                  dest, dstW, yalpha, uvalpha, y, fmt); \
} \
 \
static void name ## ext ## _1_c(SwsContext *c, const int16_t *buf0, \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf0, uint8_t *dest, int dstW, \
                                int uvalpha, int y) \
{ \
    name ## base ## _1_c_template(c, buf0, ubuf, vbuf, \
                                  abuf0, dest, dstW, uvalpha, \
                                  y, fmt); \
}

YUV2PACKEDWRAPPER(yuv2mono,, white, AV_PIX_FMT_MONOWHITE)
YUV2PACKEDWRAPPER(yuv2mono,, black, AV_PIX_FMT_MONOBLACK)

#define output_pixels(pos, Y1, U, Y2, V) \
    if (target == AV_PIX_FMT_YUYV422) { \
        dest[pos + 0] = Y1; \
        dest[pos + 1] = U;  \
        dest[pos + 2] = Y2; \
        dest[pos + 3] = V;  \
    } else if (target == AV_PIX_FMT_YVYU422) { \
        dest[pos + 0] = Y1; \
        dest[pos + 1] = V;  \
        dest[pos + 2] = Y2; \
        dest[pos + 3] = U;  \
    } else { /* AV_PIX_FMT_UYVY422 */ \
        dest[pos + 0] = U;  \
        dest[pos + 1] = Y1; \
        dest[pos + 2] = V;  \
        dest[pos + 3] = Y2; \
    }

static av_always_inline void
yuv2422_X_c_template(SwsContext *c, const int16_t *lumFilter,
                     const int16_t **lumSrc, int lumFilterSize,
                     const int16_t *chrFilter, const int16_t **chrUSrc,
                     const int16_t **chrVSrc, int chrFilterSize,
                     const int16_t **alpSrc, uint8_t *dest, int dstW,
                     int y, enum AVPixelFormat target)
{
    int i;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int j;
        int Y1 = 1 << 18;
        int Y2 = 1 << 18;
        int U  = 1 << 18;
        int V  = 1 << 18;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i * 2]     * lumFilter[j];
            Y2 += lumSrc[j][i * 2 + 1] * lumFilter[j];
        }
        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }
        Y1 >>= 19;
        Y2 >>= 19;
        U  >>= 19;
        V  >>= 19;
        if ((Y1 | Y2 | U | V) & 0x100) {
            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);
        }
        output_pixels(4*i, Y1, U, Y2, V);
    }
}

static av_always_inline void
yuv2422_2_c_template(SwsContext *c, const int16_t *buf[2],
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf[2], uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum AVPixelFormat target)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int  yalpha1 = 4096 - yalpha;
    int uvalpha1 = 4096 - uvalpha;
    int i;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;

        Y1 = av_clip_uint8(Y1);
        Y2 = av_clip_uint8(Y2);
        U  = av_clip_uint8(U);
        V  = av_clip_uint8(V);

        output_pixels(i * 4, Y1, U, Y2, V);
    }
}

static av_always_inline void
yuv2422_1_c_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum AVPixelFormat target)
{
    const int16_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 = buf0[i * 2]     >> 7;
            int Y2 = buf0[i * 2 + 1] >> 7;
            int U  = ubuf0[i]        >> 7;
            int V  = vbuf0[i]        >> 7;

            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);

            output_pixels(i * 4, Y1, U, Y2, V);
        }
    } else {
        const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 =  buf0[i * 2]          >> 7;
            int Y2 =  buf0[i * 2 + 1]      >> 7;
            int U  = (ubuf0[i] + ubuf1[i]) >> 8;
            int V  = (vbuf0[i] + vbuf1[i]) >> 8;

            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);

            output_pixels(i * 4, Y1, U, Y2, V);
        }
    }
}

#undef output_pixels

YUV2PACKEDWRAPPER(yuv2, 422, yuyv422, AV_PIX_FMT_YUYV422)
YUV2PACKEDWRAPPER(yuv2, 422, yvyu422, AV_PIX_FMT_YVYU422)
YUV2PACKEDWRAPPER(yuv2, 422, uyvy422, AV_PIX_FMT_UYVY422)

#define R_B ((target == AV_PIX_FMT_RGB48LE || target == AV_PIX_FMT_RGB48BE) ? R : B)
#define B_R ((target == AV_PIX_FMT_RGB48LE || target == AV_PIX_FMT_RGB48BE) ? B : R)
#define output_pixel(pos, val) \
    if (isBE(target)) { \
        AV_WB16(pos, val); \
    } else { \
        AV_WL16(pos, val); \
    }

static av_always_inline void
yuv2rgb48_X_c_template(SwsContext *c, const int16_t *lumFilter,
                       const int32_t **lumSrc, int lumFilterSize,
                       const int16_t *chrFilter, const int32_t **chrUSrc,
                       const int32_t **chrVSrc, int chrFilterSize,
                       const int32_t **alpSrc, uint16_t *dest, int dstW,
                       int y, enum AVPixelFormat target)
{
    int i;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int j;
        int Y1 = -0x40000000;
        int Y2 = -0x40000000;
        int U  = -128 << 23; // 19
        int V  = -128 << 23;
        int R, G, B;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i * 2]     * lumFilter[j];
            Y2 += lumSrc[j][i * 2 + 1] * lumFilter[j];
        }
        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }

        // 8 bits: 12+15=27; 16 bits: 12+19=31
        Y1 >>= 14; // 10
        Y1 += 0x10000;
        Y2 >>= 14;
        Y2 += 0x10000;
        U  >>= 14;
        V  >>= 14;

        // 8 bits: 27 -> 17 bits, 16 bits: 31 - 14 = 17 bits
        Y1 -= c->yuv2rgb_y_offset;
        Y2 -= c->yuv2rgb_y_offset;
        Y1 *= c->yuv2rgb_y_coeff;
        Y2 *= c->yuv2rgb_y_coeff;
        Y1 += 1 << 13; // 21
        Y2 += 1 << 13;
        // 8 bits: 17 + 13 bits = 30 bits, 16 bits: 17 + 13 bits = 30 bits

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        // 8 bits: 30 - 22 = 8 bits, 16 bits: 30 bits - 14 = 16 bits
        output_pixel(&dest[0], av_clip_uintp2(R_B + Y1, 30) >> 14);
        output_pixel(&dest[1], av_clip_uintp2(  G + Y1, 30) >> 14);
        output_pixel(&dest[2], av_clip_uintp2(B_R + Y1, 30) >> 14);
        output_pixel(&dest[3], av_clip_uintp2(R_B + Y2, 30) >> 14);
        output_pixel(&dest[4], av_clip_uintp2(  G + Y2, 30) >> 14);
        output_pixel(&dest[5], av_clip_uintp2(B_R + Y2, 30) >> 14);
        dest += 6;
    }
}

static av_always_inline void
yuv2rgb48_2_c_template(SwsContext *c, const int32_t *buf[2],
                       const int32_t *ubuf[2], const int32_t *vbuf[2],
                       const int32_t *abuf[2], uint16_t *dest, int dstW,
                       int yalpha, int uvalpha, int y,
                       enum AVPixelFormat target)
{
    const int32_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int  yalpha1 = 4096 - yalpha;
    int uvalpha1 = 4096 - uvalpha;
    int i;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha) >> 14;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha) >> 14;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha + (-128 << 23)) >> 14;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha + (-128 << 23)) >> 14;
        int R, G, B;

        Y1 -= c->yuv2rgb_y_offset;
        Y2 -= c->yuv2rgb_y_offset;
        Y1 *= c->yuv2rgb_y_coeff;
        Y2 *= c->yuv2rgb_y_coeff;
        Y1 += 1 << 13;
        Y2 += 1 << 13;

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        output_pixel(&dest[0], av_clip_uintp2(R_B + Y1, 30) >> 14);
        output_pixel(&dest[1], av_clip_uintp2(  G + Y1, 30) >> 14);
        output_pixel(&dest[2], av_clip_uintp2(B_R + Y1, 30) >> 14);
        output_pixel(&dest[3], av_clip_uintp2(R_B + Y2, 30) >> 14);
        output_pixel(&dest[4], av_clip_uintp2(  G + Y2, 30) >> 14);
        output_pixel(&dest[5], av_clip_uintp2(B_R + Y2, 30) >> 14);
        dest += 6;
    }
}

static av_always_inline void
yuv2rgb48_1_c_template(SwsContext *c, const int32_t *buf0,
                       const int32_t *ubuf[2], const int32_t *vbuf[2],
                       const int32_t *abuf0, uint16_t *dest, int dstW,
                       int uvalpha, int y, enum AVPixelFormat target)
{
    const int32_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 = (buf0[i * 2]    ) >> 2;
            int Y2 = (buf0[i * 2 + 1]) >> 2;
            int U  = (ubuf0[i] + (-128 << 11)) >> 2;
            int V  = (vbuf0[i] + (-128 << 11)) >> 2;
            int R, G, B;

            Y1 -= c->yuv2rgb_y_offset;
            Y2 -= c->yuv2rgb_y_offset;
            Y1 *= c->yuv2rgb_y_coeff;
            Y2 *= c->yuv2rgb_y_coeff;
            Y1 += 1 << 13;
            Y2 += 1 << 13;

            R = V * c->yuv2rgb_v2r_coeff;
            G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
            B =                            U * c->yuv2rgb_u2b_coeff;

            output_pixel(&dest[0], av_clip_uintp2(R_B + Y1, 30) >> 14);
            output_pixel(&dest[1], av_clip_uintp2(  G + Y1, 30) >> 14);
            output_pixel(&dest[2], av_clip_uintp2(B_R + Y1, 30) >> 14);
            output_pixel(&dest[3], av_clip_uintp2(R_B + Y2, 30) >> 14);
            output_pixel(&dest[4], av_clip_uintp2(  G + Y2, 30) >> 14);
            output_pixel(&dest[5], av_clip_uintp2(B_R + Y2, 30) >> 14);
            dest += 6;
        }
    } else {
        const int32_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 = (buf0[i * 2]    ) >> 2;
            int Y2 = (buf0[i * 2 + 1]) >> 2;
            int U  = (ubuf0[i] + ubuf1[i] + (-128 << 12)) >> 3;
            int V  = (vbuf0[i] + vbuf1[i] + (-128 << 12)) >> 3;
            int R, G, B;

            Y1 -= c->yuv2rgb_y_offset;
            Y2 -= c->yuv2rgb_y_offset;
            Y1 *= c->yuv2rgb_y_coeff;
            Y2 *= c->yuv2rgb_y_coeff;
            Y1 += 1 << 13;
            Y2 += 1 << 13;

            R = V * c->yuv2rgb_v2r_coeff;
            G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
            B =                            U * c->yuv2rgb_u2b_coeff;

            output_pixel(&dest[0], av_clip_uintp2(R_B + Y1, 30) >> 14);
            output_pixel(&dest[1], av_clip_uintp2(  G + Y1, 30) >> 14);
            output_pixel(&dest[2], av_clip_uintp2(B_R + Y1, 30) >> 14);
            output_pixel(&dest[3], av_clip_uintp2(R_B + Y2, 30) >> 14);
            output_pixel(&dest[4], av_clip_uintp2(  G + Y2, 30) >> 14);
            output_pixel(&dest[5], av_clip_uintp2(B_R + Y2, 30) >> 14);
            dest += 6;
        }
    }
}

#undef output_pixel
#undef r_b
#undef b_r

#define YUV2PACKED16WRAPPER(name, base, ext, fmt) \
static void name ## ext ## _X_c(SwsContext *c, const int16_t *lumFilter, \
                        const int16_t **_lumSrc, int lumFilterSize, \
                        const int16_t *chrFilter, const int16_t **_chrUSrc, \
                        const int16_t **_chrVSrc, int chrFilterSize, \
                        const int16_t **_alpSrc, uint8_t *_dest, int dstW, \
                        int y) \
{ \
    const int32_t **lumSrc  = (const int32_t **) _lumSrc, \
                  **chrUSrc = (const int32_t **) _chrUSrc, \
                  **chrVSrc = (const int32_t **) _chrVSrc, \
                  **alpSrc  = (const int32_t **) _alpSrc; \
    uint16_t *dest = (uint16_t *) _dest; \
    name ## base ## _X_c_template(c, lumFilter, lumSrc, lumFilterSize, \
                          chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                          alpSrc, dest, dstW, y, fmt); \
} \
 \
static void name ## ext ## _2_c(SwsContext *c, const int16_t *_buf[2], \
                        const int16_t *_ubuf[2], const int16_t *_vbuf[2], \
                        const int16_t *_abuf[2], uint8_t *_dest, int dstW, \
                        int yalpha, int uvalpha, int y) \
{ \
    const int32_t **buf  = (const int32_t **) _buf, \
                  **ubuf = (const int32_t **) _ubuf, \
                  **vbuf = (const int32_t **) _vbuf, \
                  **abuf = (const int32_t **) _abuf; \
    uint16_t *dest = (uint16_t *) _dest; \
    name ## base ## _2_c_template(c, buf, ubuf, vbuf, abuf, \
                          dest, dstW, yalpha, uvalpha, y, fmt); \
} \
 \
static void name ## ext ## _1_c(SwsContext *c, const int16_t *_buf0, \
                        const int16_t *_ubuf[2], const int16_t *_vbuf[2], \
                        const int16_t *_abuf0, uint8_t *_dest, int dstW, \
                        int uvalpha, int y) \
{ \
    const int32_t *buf0  = (const int32_t *)  _buf0, \
                 **ubuf  = (const int32_t **) _ubuf, \
                 **vbuf  = (const int32_t **) _vbuf, \
                  *abuf0 = (const int32_t *)  _abuf0; \
    uint16_t *dest = (uint16_t *) _dest; \
    name ## base ## _1_c_template(c, buf0, ubuf, vbuf, abuf0, dest, \
                                  dstW, uvalpha, y, fmt); \
}

YUV2PACKED16WRAPPER(yuv2, rgb48, rgb48be, AV_PIX_FMT_RGB48BE)
YUV2PACKED16WRAPPER(yuv2, rgb48, rgb48le, AV_PIX_FMT_RGB48LE)
YUV2PACKED16WRAPPER(yuv2, rgb48, bgr48be, AV_PIX_FMT_BGR48BE)
YUV2PACKED16WRAPPER(yuv2, rgb48, bgr48le, AV_PIX_FMT_BGR48LE)

/*
 * Write out 2 RGB pixels in the target pixel format. This function takes a
 * R/G/B LUT as generated by ff_yuv2rgb_c_init_tables(), which takes care of
 * things like endianness conversion and shifting. The caller takes care of
 * setting the correct offset in these tables from the chroma (U/V) values.
 * This function then uses the luminance (Y1/Y2) values to write out the
 * correct RGB values into the destination buffer.
 */
static av_always_inline void
yuv2rgb_write(uint8_t *_dest, int i, unsigned Y1, unsigned Y2,
              unsigned A1, unsigned A2,
              const void *_r, const void *_g, const void *_b, int y,
              enum AVPixelFormat target, int hasAlpha)
{
    if (target == AV_PIX_FMT_ARGB || target == AV_PIX_FMT_RGBA ||
        target == AV_PIX_FMT_ABGR || target == AV_PIX_FMT_BGRA) {
        uint32_t *dest = (uint32_t *) _dest;
        const uint32_t *r = (const uint32_t *) _r;
        const uint32_t *g = (const uint32_t *) _g;
        const uint32_t *b = (const uint32_t *) _b;

#if CONFIG_SMALL
        int sh = hasAlpha ? ((target == AV_PIX_FMT_RGB32_1 || target == AV_PIX_FMT_BGR32_1) ? 0 : 24) : 0;

        dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1] + (hasAlpha ? A1 << sh : 0);
        dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2] + (hasAlpha ? A2 << sh : 0);
#else
        if (hasAlpha) {
            int sh = (target == AV_PIX_FMT_RGB32_1 || target == AV_PIX_FMT_BGR32_1) ? 0 : 24;

            dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1] + (A1 << sh);
            dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2] + (A2 << sh);
        } else {
            dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1];
            dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2];
        }
#endif
    } else if (target == AV_PIX_FMT_RGB24 || target == AV_PIX_FMT_BGR24) {
        uint8_t *dest = (uint8_t *) _dest;
        const uint8_t *r = (const uint8_t *) _r;
        const uint8_t *g = (const uint8_t *) _g;
        const uint8_t *b = (const uint8_t *) _b;

#define r_b ((target == AV_PIX_FMT_RGB24) ? r : b)
#define b_r ((target == AV_PIX_FMT_RGB24) ? b : r)
        dest[i * 6 + 0] = r_b[Y1];
        dest[i * 6 + 1] =   g[Y1];
        dest[i * 6 + 2] = b_r[Y1];
        dest[i * 6 + 3] = r_b[Y2];
        dest[i * 6 + 4] =   g[Y2];
        dest[i * 6 + 5] = b_r[Y2];
#undef r_b
#undef b_r
    } else if (target == AV_PIX_FMT_RGB565 || target == AV_PIX_FMT_BGR565 ||
               target == AV_PIX_FMT_RGB555 || target == AV_PIX_FMT_BGR555 ||
               target == AV_PIX_FMT_RGB444 || target == AV_PIX_FMT_BGR444) {
        uint16_t *dest = (uint16_t *) _dest;
        const uint16_t *r = (const uint16_t *) _r;
        const uint16_t *g = (const uint16_t *) _g;
        const uint16_t *b = (const uint16_t *) _b;
        int dr1, dg1, db1, dr2, dg2, db2;

        if (target == AV_PIX_FMT_RGB565 || target == AV_PIX_FMT_BGR565) {
            dr1 = dither_2x2_8[ y & 1     ][0];
            dg1 = dither_2x2_4[ y & 1     ][0];
            db1 = dither_2x2_8[(y & 1) ^ 1][0];
            dr2 = dither_2x2_8[ y & 1     ][1];
            dg2 = dither_2x2_4[ y & 1     ][1];
            db2 = dither_2x2_8[(y & 1) ^ 1][1];
        } else if (target == AV_PIX_FMT_RGB555 || target == AV_PIX_FMT_BGR555) {
            dr1 = dither_2x2_8[ y & 1     ][0];
            dg1 = dither_2x2_8[ y & 1     ][1];
            db1 = dither_2x2_8[(y & 1) ^ 1][0];
            dr2 = dither_2x2_8[ y & 1     ][1];
            dg2 = dither_2x2_8[ y & 1     ][0];
            db2 = dither_2x2_8[(y & 1) ^ 1][1];
        } else {
            dr1 = ff_dither_4x4_16[ y & 3     ][0];
            dg1 = ff_dither_4x4_16[ y & 3     ][1];
            db1 = ff_dither_4x4_16[(y & 3) ^ 3][0];
            dr2 = ff_dither_4x4_16[ y & 3     ][1];
            dg2 = ff_dither_4x4_16[ y & 3     ][0];
            db2 = ff_dither_4x4_16[(y & 3) ^ 3][1];
        }

        dest[i * 2 + 0] = r[Y1 + dr1] + g[Y1 + dg1] + b[Y1 + db1];
        dest[i * 2 + 1] = r[Y2 + dr2] + g[Y2 + dg2] + b[Y2 + db2];
    } else /* 8/4 bits */ {
        uint8_t *dest = (uint8_t *) _dest;
        const uint8_t *r = (const uint8_t *) _r;
        const uint8_t *g = (const uint8_t *) _g;
        const uint8_t *b = (const uint8_t *) _b;
        int dr1, dg1, db1, dr2, dg2, db2;

        if (target == AV_PIX_FMT_RGB8 || target == AV_PIX_FMT_BGR8) {
            const uint8_t * const d64 = ff_dither_8x8_73[y & 7];
            const uint8_t * const d32 = ff_dither_8x8_32[y & 7];
            dr1 = dg1 = d32[(i * 2 + 0) & 7];
            db1 =       d64[(i * 2 + 0) & 7];
            dr2 = dg2 = d32[(i * 2 + 1) & 7];
            db2 =       d64[(i * 2 + 1) & 7];
        } else {
            const uint8_t * const d64  = ff_dither_8x8_73 [y & 7];
            const uint8_t * const d128 = ff_dither_8x8_220[y & 7];
            dr1 = db1 = d128[(i * 2 + 0) & 7];
            dg1 =        d64[(i * 2 + 0) & 7];
            dr2 = db2 = d128[(i * 2 + 1) & 7];
            dg2 =        d64[(i * 2 + 1) & 7];
        }

        if (target == AV_PIX_FMT_RGB4 || target == AV_PIX_FMT_BGR4) {
            dest[i] = r[Y1 + dr1] + g[Y1 + dg1] + b[Y1 + db1] +
                    ((r[Y2 + dr2] + g[Y2 + dg2] + b[Y2 + db2]) << 4);
        } else {
            dest[i * 2 + 0] = r[Y1 + dr1] + g[Y1 + dg1] + b[Y1 + db1];
            dest[i * 2 + 1] = r[Y2 + dr2] + g[Y2 + dg2] + b[Y2 + db2];
        }
    }
}

static av_always_inline void
yuv2rgb_X_c_template(SwsContext *c, const int16_t *lumFilter,
                     const int16_t **lumSrc, int lumFilterSize,
                     const int16_t *chrFilter, const int16_t **chrUSrc,
                     const int16_t **chrVSrc, int chrFilterSize,
                     const int16_t **alpSrc, uint8_t *dest, int dstW,
                     int y, enum AVPixelFormat target, int hasAlpha)
{
    int i;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int j, A1, A2;
        int Y1 = 1 << 18;
        int Y2 = 1 << 18;
        int U  = 1 << 18;
        int V  = 1 << 18;
        const void *r, *g, *b;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i * 2]     * lumFilter[j];
            Y2 += lumSrc[j][i * 2 + 1] * lumFilter[j];
        }
        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }
        Y1 >>= 19;
        Y2 >>= 19;
        U  >>= 19;
        V  >>= 19;
        if ((Y1 | Y2 | U | V) & 0x100) {
            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);
        }
        if (hasAlpha) {
            A1 = 1 << 18;
            A2 = 1 << 18;
            for (j = 0; j < lumFilterSize; j++) {
                A1 += alpSrc[j][i * 2    ] * lumFilter[j];
                A2 += alpSrc[j][i * 2 + 1] * lumFilter[j];
            }
            A1 >>= 19;
            A2 >>= 19;
            if ((A1 | A2) & 0x100) {
                A1 = av_clip_uint8(A1);
                A2 = av_clip_uint8(A2);
            }
        }

        /* FIXME fix tables so that clipping is not needed and then use _NOCLIP*/
        r =  c->table_rV[V];
        g = (c->table_gU[U] + c->table_gV[V]);
        b =  c->table_bU[U];

        yuv2rgb_write(dest, i, Y1, Y2, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                      r, g, b, y, target, hasAlpha);
    }
}

static av_always_inline void
yuv2rgb_2_c_template(SwsContext *c, const int16_t *buf[2],
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf[2], uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum AVPixelFormat target, int hasAlpha)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    int  yalpha1 = 4096 - yalpha;
    int uvalpha1 = 4096 - uvalpha;
    int i;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;
        int A1, A2;
        const void *r, *g, *b;

        Y1 = av_clip_uint8(Y1);
        Y2 = av_clip_uint8(Y2);
        U  = av_clip_uint8(U);
        V  = av_clip_uint8(V);

        r =  c->table_rV[V];
        g = (c->table_gU[U] + c->table_gV[V]);
        b =  c->table_bU[U];

        if (hasAlpha) {
            A1 = (abuf0[i * 2    ] * yalpha1 + abuf1[i * 2    ] * yalpha) >> 19;
            A2 = (abuf0[i * 2 + 1] * yalpha1 + abuf1[i * 2 + 1] * yalpha) >> 19;
            A1 = av_clip_uint8(A1);
            A2 = av_clip_uint8(A2);
        }

        yuv2rgb_write(dest, i, Y1, Y2, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                      r, g, b, y, target, hasAlpha);
    }
}

static av_always_inline void
yuv2rgb_1_c_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum AVPixelFormat target,
                     int hasAlpha)
{
    const int16_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 = buf0[i * 2]     >> 7;
            int Y2 = buf0[i * 2 + 1] >> 7;
            int U  = ubuf0[i]        >> 7;
            int V  = vbuf0[i]        >> 7;
            int A1, A2;
            const void *r, *g, *b;

            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);

            r =  c->table_rV[V];
            g = (c->table_gU[U] + c->table_gV[V]);
            b =  c->table_bU[U];

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] >> 7;
                A2 = abuf0[i * 2 + 1] >> 7;
                A1 = av_clip_uint8(A1);
                A2 = av_clip_uint8(A2);
            }

            yuv2rgb_write(dest, i, Y1, Y2, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                          r, g, b, y, target, hasAlpha);
        }
    } else {
        const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 =  buf0[i * 2]          >> 7;
            int Y2 =  buf0[i * 2 + 1]      >> 7;
            int U  = (ubuf0[i] + ubuf1[i]) >> 8;
            int V  = (vbuf0[i] + vbuf1[i]) >> 8;
            int A1, A2;
            const void *r, *g, *b;

            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);

            r =  c->table_rV[V];
            g = (c->table_gU[U] + c->table_gV[V]);
            b =  c->table_bU[U];

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] >> 7;
                A2 = abuf0[i * 2 + 1] >> 7;
                A1 = av_clip_uint8(A1);
                A2 = av_clip_uint8(A2);
            }

            yuv2rgb_write(dest, i, Y1, Y2, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                          r, g, b, y, target, hasAlpha);
        }
    }
}

#define YUV2RGBWRAPPERX(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _X_c(SwsContext *c, const int16_t *lumFilter, \
                                const int16_t **lumSrc, int lumFilterSize, \
                                const int16_t *chrFilter, const int16_t **chrUSrc, \
                                const int16_t **chrVSrc, int chrFilterSize, \
                                const int16_t **alpSrc, uint8_t *dest, int dstW, \
                                int y) \
{ \
    name ## base ## _X_c_template(c, lumFilter, lumSrc, lumFilterSize, \
                                  chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                                  alpSrc, dest, dstW, y, fmt, hasAlpha); \
}
#define YUV2RGBWRAPPER(name, base, ext, fmt, hasAlpha) \
YUV2RGBWRAPPERX(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _2_c(SwsContext *c, const int16_t *buf[2], \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf[2], uint8_t *dest, int dstW, \
                                int yalpha, int uvalpha, int y) \
{ \
    name ## base ## _2_c_template(c, buf, ubuf, vbuf, abuf, \
                                  dest, dstW, yalpha, uvalpha, y, fmt, hasAlpha); \
} \
 \
static void name ## ext ## _1_c(SwsContext *c, const int16_t *buf0, \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf0, uint8_t *dest, int dstW, \
                                int uvalpha, int y) \
{ \
    name ## base ## _1_c_template(c, buf0, ubuf, vbuf, abuf0, dest, \
                                  dstW, uvalpha, y, fmt, hasAlpha); \
}

#if CONFIG_SMALL
YUV2RGBWRAPPER(yuv2rgb,,  32_1,  AV_PIX_FMT_RGB32_1,   CONFIG_SWSCALE_ALPHA && c->alpPixBuf)
YUV2RGBWRAPPER(yuv2rgb,,  32,    AV_PIX_FMT_RGB32,     CONFIG_SWSCALE_ALPHA && c->alpPixBuf)
#else
#if CONFIG_SWSCALE_ALPHA
YUV2RGBWRAPPER(yuv2rgb,, a32_1,  AV_PIX_FMT_RGB32_1,   1)
YUV2RGBWRAPPER(yuv2rgb,, a32,    AV_PIX_FMT_RGB32,     1)
#endif
YUV2RGBWRAPPER(yuv2rgb,, x32_1,  AV_PIX_FMT_RGB32_1,   0)
YUV2RGBWRAPPER(yuv2rgb,, x32,    AV_PIX_FMT_RGB32,     0)
#endif
YUV2RGBWRAPPER(yuv2, rgb, rgb24, AV_PIX_FMT_RGB24,   0)
YUV2RGBWRAPPER(yuv2, rgb, bgr24, AV_PIX_FMT_BGR24,   0)
YUV2RGBWRAPPER(yuv2rgb,,  16,    AV_PIX_FMT_RGB565,    0)
YUV2RGBWRAPPER(yuv2rgb,,  15,    AV_PIX_FMT_RGB555,    0)
YUV2RGBWRAPPER(yuv2rgb,,  12,    AV_PIX_FMT_RGB444,    0)
YUV2RGBWRAPPER(yuv2rgb,,   8,    AV_PIX_FMT_RGB8,      0)
YUV2RGBWRAPPER(yuv2rgb,,   4,    AV_PIX_FMT_RGB4,      0)
YUV2RGBWRAPPER(yuv2rgb,,   4b,   AV_PIX_FMT_RGB4_BYTE, 0)

static av_always_inline void
yuv2rgb_full_X_c_template(SwsContext *c, const int16_t *lumFilter,
                          const int16_t **lumSrc, int lumFilterSize,
                          const int16_t *chrFilter, const int16_t **chrUSrc,
                          const int16_t **chrVSrc, int chrFilterSize,
                          const int16_t **alpSrc, uint8_t *dest,
                          int dstW, int y, enum AVPixelFormat target, int hasAlpha)
{
    int i;
    int step = (target == AV_PIX_FMT_RGB24 || target == AV_PIX_FMT_BGR24) ? 3 : 4;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 0;
        int U = -128 << 19;
        int V = -128 << 19;
        int R, G, B, A;

        for (j = 0; j < lumFilterSize; j++) {
            Y += lumSrc[j][i] * lumFilter[j];
        }
        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }
        Y >>= 10;
        U >>= 10;
        V >>= 10;
        if (hasAlpha) {
            A = 1 << 21;
            for (j = 0; j < lumFilterSize; j++) {
                A += alpSrc[j][i] * lumFilter[j];
            }
            A >>= 19;
            if (A & 0x100)
                A = av_clip_uint8(A);
        }
        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += 1 << 21;
        R = Y + V*c->yuv2rgb_v2r_coeff;
        G = Y + V*c->yuv2rgb_v2g_coeff + U*c->yuv2rgb_u2g_coeff;
        B = Y +                          U*c->yuv2rgb_u2b_coeff;
        if ((R | G | B) & 0xC0000000) {
            R = av_clip_uintp2(R, 30);
            G = av_clip_uintp2(G, 30);
            B = av_clip_uintp2(B, 30);
        }

        switch(target) {
        case AV_PIX_FMT_ARGB:
            dest[0] = hasAlpha ? A : 255;
            dest[1] = R >> 22;
            dest[2] = G >> 22;
            dest[3] = B >> 22;
            break;
        case AV_PIX_FMT_RGB24:
            dest[0] = R >> 22;
            dest[1] = G >> 22;
            dest[2] = B >> 22;
            break;
        case AV_PIX_FMT_RGBA:
            dest[0] = R >> 22;
            dest[1] = G >> 22;
            dest[2] = B >> 22;
            dest[3] = hasAlpha ? A : 255;
            break;
        case AV_PIX_FMT_ABGR:
            dest[0] = hasAlpha ? A : 255;
            dest[1] = B >> 22;
            dest[2] = G >> 22;
            dest[3] = R >> 22;
            dest += 4;
            break;
        case AV_PIX_FMT_BGR24:
            dest[0] = B >> 22;
            dest[1] = G >> 22;
            dest[2] = R >> 22;
            break;
        case AV_PIX_FMT_BGRA:
            dest[0] = B >> 22;
            dest[1] = G >> 22;
            dest[2] = R >> 22;
            dest[3] = hasAlpha ? A : 255;
            break;
        }
        dest += step;
    }
}

#if CONFIG_SMALL
YUV2RGBWRAPPERX(yuv2, rgb_full, bgra32_full, AV_PIX_FMT_BGRA,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf)
YUV2RGBWRAPPERX(yuv2, rgb_full, abgr32_full, AV_PIX_FMT_ABGR,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf)
YUV2RGBWRAPPERX(yuv2, rgb_full, rgba32_full, AV_PIX_FMT_RGBA,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf)
YUV2RGBWRAPPERX(yuv2, rgb_full, argb32_full, AV_PIX_FMT_ARGB,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf)
#else
#if CONFIG_SWSCALE_ALPHA
YUV2RGBWRAPPERX(yuv2, rgb_full, bgra32_full, AV_PIX_FMT_BGRA,  1)
YUV2RGBWRAPPERX(yuv2, rgb_full, abgr32_full, AV_PIX_FMT_ABGR,  1)
YUV2RGBWRAPPERX(yuv2, rgb_full, rgba32_full, AV_PIX_FMT_RGBA,  1)
YUV2RGBWRAPPERX(yuv2, rgb_full, argb32_full, AV_PIX_FMT_ARGB,  1)
#endif
YUV2RGBWRAPPERX(yuv2, rgb_full, bgrx32_full, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPERX(yuv2, rgb_full, xbgr32_full, AV_PIX_FMT_ABGR,  0)
YUV2RGBWRAPPERX(yuv2, rgb_full, rgbx32_full, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPERX(yuv2, rgb_full, xrgb32_full, AV_PIX_FMT_ARGB,  0)
#endif
YUV2RGBWRAPPERX(yuv2, rgb_full, bgr24_full,  AV_PIX_FMT_BGR24, 0)
YUV2RGBWRAPPERX(yuv2, rgb_full, rgb24_full,  AV_PIX_FMT_RGB24, 0)

static void
yuv2gbrp_full_X_c(SwsContext *c, const int16_t *lumFilter,
                  const int16_t **lumSrc, int lumFilterSize,
                  const int16_t *chrFilter, const int16_t **chrUSrc,
                  const int16_t **chrVSrc, int chrFilterSize,
                  const int16_t **alpSrc, uint8_t **dest,
                  int dstW, int y)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->dstFormat);
    int i;
    int hasAlpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) && alpSrc;
    uint16_t **dest16 = (uint16_t**)dest;
    int SH = 22 + 8 - desc->comp[0].depth;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 1 << 9;
        int U = (1 << 9) - (128 << 19);
        int V = (1 << 9) - (128 << 19);
        int R, G, B, A;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * lumFilter[j];

        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }

        Y >>= 10;
        U >>= 10;
        V >>= 10;

        if (hasAlpha) {
            A = 1 << 18;

            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * lumFilter[j];

            A >>= 19;

            if (A & 0x100)
                A = av_clip_uint8(A);
        }

        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += 1 << 21;
        R = Y + V * c->yuv2rgb_v2r_coeff;
        G = Y + V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B = Y +                            U * c->yuv2rgb_u2b_coeff;

        if ((R | G | B) & 0xC0000000) {
            R = av_clip_uintp2(R, 30);
            G = av_clip_uintp2(G, 30);
            B = av_clip_uintp2(B, 30);
        }

        if (SH != 22) {
            dest16[0][i] = G >> SH;
            dest16[1][i] = B >> SH;
            dest16[2][i] = R >> SH;
            if (hasAlpha)
                dest16[3][i] = A;
        } else {
            dest[0][i] = G >> 22;
            dest[1][i] = B >> 22;
            dest[2][i] = R >> 22;
            if (hasAlpha)
                dest[3][i] = A;
        }
    }
    if (SH != 22 && (!isBE(c->dstFormat)) != (!HAVE_BIGENDIAN)) {
        for (i = 0; i < dstW; i++) {
            dest16[0][i] = av_bswap16(dest16[0][i]);
            dest16[1][i] = av_bswap16(dest16[1][i]);
            dest16[2][i] = av_bswap16(dest16[2][i]);
            if (hasAlpha)
                dest16[3][i] = av_bswap16(dest16[3][i]);
        }
    }
}

av_cold void ff_sws_init_output_funcs(SwsContext *c,
                                      yuv2planar1_fn *yuv2plane1,
                                      yuv2planarX_fn *yuv2planeX,
                                      yuv2interleavedX_fn *yuv2nv12cX,
                                      yuv2packed1_fn *yuv2packed1,
                                      yuv2packed2_fn *yuv2packed2,
                                      yuv2packedX_fn *yuv2packedX,
                                      yuv2anyX_fn *yuv2anyX)
{
    enum AVPixelFormat dstFormat = c->dstFormat;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dstFormat);

    if (is16BPS(dstFormat)) {
        *yuv2planeX = isBE(dstFormat) ? yuv2planeX_16BE_c  : yuv2planeX_16LE_c;
        *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_16BE_c  : yuv2plane1_16LE_c;
    } else if (is9_OR_10BPS(dstFormat)) {
        if (desc->comp[0].depth == 9) {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_9BE_c  : yuv2planeX_9LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_9BE_c  : yuv2plane1_9LE_c;
        } else {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_10BE_c  : yuv2planeX_10LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_10BE_c  : yuv2plane1_10LE_c;
        }
    } else {
        *yuv2plane1 = yuv2plane1_8_c;
        *yuv2planeX = yuv2planeX_8_c;
        if (dstFormat == AV_PIX_FMT_NV12 || dstFormat == AV_PIX_FMT_NV21)
            *yuv2nv12cX = yuv2nv12cX_c;
    }

    if(c->flags & SWS_FULL_CHR_H_INT) {
        switch (dstFormat) {
            case AV_PIX_FMT_RGBA:
#if CONFIG_SMALL
                *yuv2packedX = yuv2rgba32_full_X_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->alpPixBuf) {
                    *yuv2packedX = yuv2rgba32_full_X_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2rgbx32_full_X_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_ARGB:
#if CONFIG_SMALL
                *yuv2packedX = yuv2argb32_full_X_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->alpPixBuf) {
                    *yuv2packedX = yuv2argb32_full_X_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2xrgb32_full_X_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_BGRA:
#if CONFIG_SMALL
                *yuv2packedX = yuv2bgra32_full_X_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->alpPixBuf) {
                    *yuv2packedX = yuv2bgra32_full_X_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2bgrx32_full_X_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_ABGR:
#if CONFIG_SMALL
                *yuv2packedX = yuv2abgr32_full_X_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->alpPixBuf) {
                    *yuv2packedX = yuv2abgr32_full_X_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2xbgr32_full_X_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_RGB24:
            *yuv2packedX = yuv2rgb24_full_X_c;
            break;
        case AV_PIX_FMT_BGR24:
            *yuv2packedX = yuv2bgr24_full_X_c;
            break;
        case AV_PIX_FMT_GBRP:
        case AV_PIX_FMT_GBRP9BE:
        case AV_PIX_FMT_GBRP9LE:
        case AV_PIX_FMT_GBRP10BE:
        case AV_PIX_FMT_GBRP10LE:
        case AV_PIX_FMT_GBRP16BE:
        case AV_PIX_FMT_GBRP16LE:
        case AV_PIX_FMT_GBRAP:
            *yuv2anyX = yuv2gbrp_full_X_c;
            break;
        }
    } else {
        switch (dstFormat) {
        case AV_PIX_FMT_RGB48LE:
            *yuv2packed1 = yuv2rgb48le_1_c;
            *yuv2packed2 = yuv2rgb48le_2_c;
            *yuv2packedX = yuv2rgb48le_X_c;
            break;
        case AV_PIX_FMT_RGB48BE:
            *yuv2packed1 = yuv2rgb48be_1_c;
            *yuv2packed2 = yuv2rgb48be_2_c;
            *yuv2packedX = yuv2rgb48be_X_c;
            break;
        case AV_PIX_FMT_BGR48LE:
            *yuv2packed1 = yuv2bgr48le_1_c;
            *yuv2packed2 = yuv2bgr48le_2_c;
            *yuv2packedX = yuv2bgr48le_X_c;
            break;
        case AV_PIX_FMT_BGR48BE:
            *yuv2packed1 = yuv2bgr48be_1_c;
            *yuv2packed2 = yuv2bgr48be_2_c;
            *yuv2packedX = yuv2bgr48be_X_c;
            break;
        case AV_PIX_FMT_RGB32:
        case AV_PIX_FMT_BGR32:
#if CONFIG_SMALL
            *yuv2packed1 = yuv2rgb32_1_c;
            *yuv2packed2 = yuv2rgb32_2_c;
            *yuv2packedX = yuv2rgb32_X_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->alpPixBuf) {
                    *yuv2packed1 = yuv2rgba32_1_c;
                    *yuv2packed2 = yuv2rgba32_2_c;
                    *yuv2packedX = yuv2rgba32_X_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packed1 = yuv2rgbx32_1_c;
                    *yuv2packed2 = yuv2rgbx32_2_c;
                    *yuv2packedX = yuv2rgbx32_X_c;
                }
#endif /* !CONFIG_SMALL */
            break;
        case AV_PIX_FMT_RGB32_1:
        case AV_PIX_FMT_BGR32_1:
#if CONFIG_SMALL
                *yuv2packed1 = yuv2rgb32_1_1_c;
                *yuv2packed2 = yuv2rgb32_1_2_c;
                *yuv2packedX = yuv2rgb32_1_X_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->alpPixBuf) {
                    *yuv2packed1 = yuv2rgba32_1_1_c;
                    *yuv2packed2 = yuv2rgba32_1_2_c;
                    *yuv2packedX = yuv2rgba32_1_X_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packed1 = yuv2rgbx32_1_1_c;
                    *yuv2packed2 = yuv2rgbx32_1_2_c;
                    *yuv2packedX = yuv2rgbx32_1_X_c;
                }
#endif /* !CONFIG_SMALL */
                break;
        case AV_PIX_FMT_RGB24:
            *yuv2packed1 = yuv2rgb24_1_c;
            *yuv2packed2 = yuv2rgb24_2_c;
            *yuv2packedX = yuv2rgb24_X_c;
            break;
        case AV_PIX_FMT_BGR24:
            *yuv2packed1 = yuv2bgr24_1_c;
            *yuv2packed2 = yuv2bgr24_2_c;
            *yuv2packedX = yuv2bgr24_X_c;
            break;
        case AV_PIX_FMT_RGB565LE:
        case AV_PIX_FMT_RGB565BE:
        case AV_PIX_FMT_BGR565LE:
        case AV_PIX_FMT_BGR565BE:
            *yuv2packed1 = yuv2rgb16_1_c;
            *yuv2packed2 = yuv2rgb16_2_c;
            *yuv2packedX = yuv2rgb16_X_c;
            break;
        case AV_PIX_FMT_RGB555LE:
        case AV_PIX_FMT_RGB555BE:
        case AV_PIX_FMT_BGR555LE:
        case AV_PIX_FMT_BGR555BE:
            *yuv2packed1 = yuv2rgb15_1_c;
            *yuv2packed2 = yuv2rgb15_2_c;
            *yuv2packedX = yuv2rgb15_X_c;
            break;
        case AV_PIX_FMT_RGB444LE:
        case AV_PIX_FMT_RGB444BE:
        case AV_PIX_FMT_BGR444LE:
        case AV_PIX_FMT_BGR444BE:
            *yuv2packed1 = yuv2rgb12_1_c;
            *yuv2packed2 = yuv2rgb12_2_c;
            *yuv2packedX = yuv2rgb12_X_c;
            break;
        case AV_PIX_FMT_RGB8:
        case AV_PIX_FMT_BGR8:
            *yuv2packed1 = yuv2rgb8_1_c;
            *yuv2packed2 = yuv2rgb8_2_c;
            *yuv2packedX = yuv2rgb8_X_c;
            break;
        case AV_PIX_FMT_RGB4:
        case AV_PIX_FMT_BGR4:
            *yuv2packed1 = yuv2rgb4_1_c;
            *yuv2packed2 = yuv2rgb4_2_c;
            *yuv2packedX = yuv2rgb4_X_c;
            break;
        case AV_PIX_FMT_RGB4_BYTE:
        case AV_PIX_FMT_BGR4_BYTE:
            *yuv2packed1 = yuv2rgb4b_1_c;
            *yuv2packed2 = yuv2rgb4b_2_c;
            *yuv2packedX = yuv2rgb4b_X_c;
            break;
        }
    }
    switch (dstFormat) {
    case AV_PIX_FMT_MONOWHITE:
        *yuv2packed1 = yuv2monowhite_1_c;
        *yuv2packed2 = yuv2monowhite_2_c;
        *yuv2packedX = yuv2monowhite_X_c;
        break;
    case AV_PIX_FMT_MONOBLACK:
        *yuv2packed1 = yuv2monoblack_1_c;
        *yuv2packed2 = yuv2monoblack_2_c;
        *yuv2packedX = yuv2monoblack_X_c;
        break;
    case AV_PIX_FMT_YUYV422:
        *yuv2packed1 = yuv2yuyv422_1_c;
        *yuv2packed2 = yuv2yuyv422_2_c;
        *yuv2packedX = yuv2yuyv422_X_c;
        break;
    case AV_PIX_FMT_YVYU422:
        *yuv2packed1 = yuv2yvyu422_1_c;
        *yuv2packed2 = yuv2yvyu422_2_c;
        *yuv2packedX = yuv2yvyu422_X_c;
        break;
    case AV_PIX_FMT_UYVY422:
        *yuv2packed1 = yuv2uyvy422_1_c;
        *yuv2packed2 = yuv2uyvy422_2_c;
        *yuv2packedX = yuv2uyvy422_X_c;
        break;
    }
}
