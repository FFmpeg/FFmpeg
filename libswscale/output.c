/*
 * Copyright (C) 2001-2012 Michael Niedermayer <michaelni@gmx.at>
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avutil.h"
#include "libavutil/avassert.h"
#include "libavutil/bswap.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"
#include "config.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"

DECLARE_ALIGNED(8, const uint8_t, ff_dither_2x2_4)[][8] = {
{  1,   3,   1,   3,   1,   3,   1,   3, },
{  2,   0,   2,   0,   2,   0,   2,   0, },
{  1,   3,   1,   3,   1,   3,   1,   3, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_2x2_8)[][8] = {
{  6,   2,   6,   2,   6,   2,   6,   2, },
{  0,   4,   0,   4,   0,   4,   0,   4, },
{  6,   2,   6,   2,   6,   2,   6,   2, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_4x4_16)[][8] = {
{  8,   4,  11,   7,   8,   4,  11,   7, },
{  2,  14,   1,  13,   2,  14,   1,  13, },
{ 10,   6,   9,   5,  10,   6,   9,   5, },
{  0,  12,   3,  15,   0,  12,   3,  15, },
{  8,   4,  11,   7,   8,   4,  11,   7, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_32)[][8] = {
{ 17,   9,  23,  15,  16,   8,  22,  14, },
{  5,  29,   3,  27,   4,  28,   2,  26, },
{ 21,  13,  19,  11,  20,  12,  18,  10, },
{  0,  24,   6,  30,   1,  25,   7,  31, },
{ 16,   8,  22,  14,  17,   9,  23,  15, },
{  4,  28,   2,  26,   5,  29,   3,  27, },
{ 20,  12,  18,  10,  21,  13,  19,  11, },
{  1,  25,   7,  31,   0,  24,   6,  30, },
{ 17,   9,  23,  15,  16,   8,  22,  14, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_73)[][8] = {
{  0,  55,  14,  68,   3,  58,  17,  72, },
{ 37,  18,  50,  32,  40,  22,  54,  35, },
{  9,  64,   5,  59,  13,  67,   8,  63, },
{ 46,  27,  41,  23,  49,  31,  44,  26, },
{  2,  57,  16,  71,   1,  56,  15,  70, },
{ 39,  21,  52,  34,  38,  19,  51,  33, },
{ 11,  66,   7,  62,  10,  65,   6,  60, },
{ 48,  30,  43,  25,  47,  29,  42,  24, },
{  0,  55,  14,  68,   3,  58,  17,  72, },
};

#if 1
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[][8] = {
{117,  62, 158, 103, 113,  58, 155, 100, },
{ 34, 199,  21, 186,  31, 196,  17, 182, },
{144,  89, 131,  76, 141,  86, 127,  72, },
{  0, 165,  41, 206,  10, 175,  52, 217, },
{110,  55, 151,  96, 120,  65, 162, 107, },
{ 28, 193,  14, 179,  38, 203,  24, 189, },
{138,  83, 124,  69, 148,  93, 134,  79, },
{  7, 172,  48, 213,   3, 168,  45, 210, },
{117,  62, 158, 103, 113,  58, 155, 100, },
};
#elif 1
// tries to correct a gamma of 1.5
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[][8] = {
{  0, 143,  18, 200,   2, 156,  25, 215, },
{ 78,  28, 125,  64,  89,  36, 138,  74, },
{ 10, 180,   3, 161,  16, 195,   8, 175, },
{109,  51,  93,  38, 121,  60, 105,  47, },
{  1, 152,  23, 210,   0, 147,  20, 205, },
{ 85,  33, 134,  71,  81,  30, 130,  67, },
{ 14, 190,   6, 171,  12, 185,   5, 166, },
{117,  57, 101,  44, 113,  54,  97,  41, },
{  0, 143,  18, 200,   2, 156,  25, 215, },
};
#elif 1
// tries to correct a gamma of 2.0
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[][8] = {
{  0, 124,   8, 193,   0, 140,  12, 213, },
{ 55,  14, 104,  42,  66,  19, 119,  52, },
{  3, 168,   1, 145,   6, 187,   3, 162, },
{ 86,  31,  70,  21,  99,  39,  82,  28, },
{  0, 134,  11, 206,   0, 129,   9, 200, },
{ 62,  17, 114,  48,  58,  16, 109,  45, },
{  5, 181,   2, 157,   4, 175,   1, 151, },
{ 95,  36,  78,  26,  90,  34,  74,  24, },
{  0, 124,   8, 193,   0, 140,  12, 213, },
};
#else
// tries to correct a gamma of 2.5
DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_220)[][8] = {
{  0, 107,   3, 187,   0, 125,   6, 212, },
{ 39,   7,  86,  28,  49,  11, 102,  36, },
{  1, 158,   0, 131,   3, 180,   1, 151, },
{ 68,  19,  52,  12,  81,  25,  64,  17, },
{  0, 119,   5, 203,   0, 113,   4, 195, },
{ 45,   9,  96,  33,  42,   8,  91,  30, },
{  2, 172,   1, 144,   2, 165,   0, 137, },
{ 77,  23,  60,  15,  72,  21,  56,  14, },
{  0, 107,   3, 187,   0, 125,   6, 212, },
};
#endif

#define IS_BE_LE 0
#define IS_BE_BE 1
/* ENDIAN_IDENTIFIER needs to be "BE" or "LE". */
#define IS_BE(ENDIAN_IDENTIFIER) IS_BE_ ## ENDIAN_IDENTIFIER

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
    int shift = 3;
    av_assert0(output_bits == 16);

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
    int shift = 15;
    av_assert0(output_bits == 16);

    for (i = 0; i < dstW; i++) {
        int val = 1 << (shift - 1);
        int j;

        /* range of val is [0,0x7FFFFFFF], so 31 bits, but with lanczos/spline
         * filters (or anything with negative coeffs, the range can be slightly
         * wider in both directions. To account for this overflow, we subtract
         * a constant so it always fits in the signed range (assuming a
         * reasonable filterSize), and re-add that at the end. */
        val -= 0x40000000;
        for (j = 0; j < filterSize; j++)
            val += src[j][i] * (unsigned)filter[j];

        output_pixel(&dest[i], val, 0x8000, int);
    }
}

static av_always_inline void
yuv2nv12cX_16_c_template(int big_endian, const uint8_t *chrDither,
                         const int16_t *chrFilter, int chrFilterSize,
                         const int16_t **chrUSrc, const int16_t **chrVSrc,
                         uint8_t *dest8, int chrDstW, int output_bits)
{
    uint16_t *dest = (uint16_t*)dest8;
    const int32_t **uSrc = (const int32_t **)chrUSrc;
    const int32_t **vSrc = (const int32_t **)chrVSrc;
    int shift = 15;
    int i, j;
    av_assert0(output_bits == 16);

    for (i = 0; i < chrDstW; i++) {
        int u = 1 << (shift - 1);
        int v = 1 << (shift - 1);

        /* See yuv2planeX_16_c_template for details. */
        u -= 0x40000000;
        v -= 0x40000000;
        for (j = 0; j < chrFilterSize; j++) {
            u += uSrc[j][i] * (unsigned)chrFilter[j];
            v += vSrc[j][i] * (unsigned)chrFilter[j];
        }

        output_pixel(&dest[2*i]  , u, 0x8000, int);
        output_pixel(&dest[2*i+1], v, 0x8000, int);
    }
}

static av_always_inline void
yuv2plane1_float_c_template(const int32_t *src, float *dest, int dstW)
{
    static const int big_endian = HAVE_BIGENDIAN;
    static const int shift = 3;
    static const float float_mult = 1.0f / 65535.0f;
    int i, val;
    uint16_t val_uint;

    for (i = 0; i < dstW; ++i){
        val = src[i] + (1 << (shift - 1));
        output_pixel(&val_uint, val, 0, uint);
        dest[i] = float_mult * (float)val_uint;
    }
}

static av_always_inline void
yuv2plane1_float_bswap_c_template(const int32_t *src, uint32_t *dest, int dstW)
{
    static const int big_endian = HAVE_BIGENDIAN;
    static const int shift = 3;
    static const float float_mult = 1.0f / 65535.0f;
    int i, val;
    uint16_t val_uint;

    for (i = 0; i < dstW; ++i){
        val = src[i] + (1 << (shift - 1));
        output_pixel(&val_uint, val, 0, uint);
        dest[i] = av_bswap32(av_float2int(float_mult * (float)val_uint));
    }
}

static av_always_inline void
yuv2planeX_float_c_template(const int16_t *filter, int filterSize, const int32_t **src,
                            float *dest, int dstW)
{
    static const int big_endian = HAVE_BIGENDIAN;
    static const int shift = 15;
    static const float float_mult = 1.0f / 65535.0f;
    int i, j, val;
    uint16_t val_uint;

    for (i = 0; i < dstW; ++i){
        val = (1 << (shift - 1)) - 0x40000000;
        for (j = 0; j < filterSize; ++j){
            val += src[j][i] * (unsigned)filter[j];
        }
        output_pixel(&val_uint, val, 0x8000, int);
        dest[i] = float_mult * (float)val_uint;
    }
}

static av_always_inline void
yuv2planeX_float_bswap_c_template(const int16_t *filter, int filterSize, const int32_t **src,
                            uint32_t *dest, int dstW)
{
    static const int big_endian = HAVE_BIGENDIAN;
    static const int shift = 15;
    static const float float_mult = 1.0f / 65535.0f;
    int i, j, val;
    uint16_t val_uint;

    for (i = 0; i < dstW; ++i){
        val = (1 << (shift - 1)) - 0x40000000;
        for (j = 0; j < filterSize; ++j){
            val += src[j][i] * (unsigned)filter[j];
        }
        output_pixel(&val_uint, val, 0x8000, int);
        dest[i] = av_bswap32(av_float2int(float_mult * (float)val_uint));
    }
}

#define yuv2plane1_float(template, dest_type, BE_LE) \
static void yuv2plane1_float ## BE_LE ## _c(const int16_t *src, uint8_t *dest, int dstW, \
                                            const uint8_t *dither, int offset) \
{ \
    template((const int32_t *)src, (dest_type *)dest, dstW); \
}

#define yuv2planeX_float(template, dest_type, BE_LE) \
static void yuv2planeX_float ## BE_LE ## _c(const int16_t *filter, int filterSize, \
                                            const int16_t **src, uint8_t *dest, int dstW, \
                                            const uint8_t *dither, int offset) \
{ \
    template(filter, filterSize, (const int32_t **)src, (dest_type *)dest, dstW); \
}

#if HAVE_BIGENDIAN
yuv2plane1_float(yuv2plane1_float_c_template,       float,    BE)
yuv2plane1_float(yuv2plane1_float_bswap_c_template, uint32_t, LE)
yuv2planeX_float(yuv2planeX_float_c_template,       float,    BE)
yuv2planeX_float(yuv2planeX_float_bswap_c_template, uint32_t, LE)
#else
yuv2plane1_float(yuv2plane1_float_c_template,       float,    LE)
yuv2plane1_float(yuv2plane1_float_bswap_c_template, uint32_t, BE)
yuv2planeX_float(yuv2planeX_float_c_template,       float,    LE)
yuv2planeX_float(yuv2planeX_float_bswap_c_template, uint32_t, BE)
#endif

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
        int val = 1 << (shift - 1);
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
yuv2NBPS(12, BE, 1, 10, int16_t)
yuv2NBPS(12, LE, 0, 10, int16_t)
yuv2NBPS(14, BE, 1, 10, int16_t)
yuv2NBPS(14, LE, 0, 10, int16_t)
yuv2NBPS(16, BE, 1, 16, int32_t)
yuv2NBPS(16, LE, 0, 16, int32_t)


static void yuv2nv12cX_16LE_c(enum AVPixelFormat dstFormat, const uint8_t *chrDither,
                              const int16_t *chrFilter, int chrFilterSize,
                              const int16_t **chrUSrc, const int16_t **chrVSrc,
                              uint8_t *dest8, int chrDstW)
{
    yuv2nv12cX_16_c_template(0, chrDither, chrFilter, chrFilterSize, chrUSrc, chrVSrc, dest8, chrDstW, 16);
}

static void yuv2nv12cX_16BE_c(enum AVPixelFormat dstFormat, const uint8_t *chrDither,
                              const int16_t *chrFilter, int chrFilterSize,
                              const int16_t **chrUSrc, const int16_t **chrVSrc,
                              uint8_t *dest8, int chrDstW)
{
    yuv2nv12cX_16_c_template(1, chrDither, chrFilter, chrFilterSize, chrUSrc, chrVSrc, dest8, chrDstW, 16);
}

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

static void yuv2nv12cX_c(enum AVPixelFormat dstFormat, const uint8_t *chrDither,
                         const int16_t *chrFilter, int chrFilterSize,
                         const int16_t **chrUSrc, const int16_t **chrVSrc,
                         uint8_t *dest, int chrDstW)
{
    int i;

    if (!isSwappedChroma(dstFormat))
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


#define output_pixel(pos, val) \
    if (big_endian) { \
        AV_WB16(pos, av_clip_uintp2(val >> shift, output_bits) << output_shift); \
    } else { \
        AV_WL16(pos, av_clip_uintp2(val >> shift, output_bits) << output_shift); \
    }

static void yuv2p01xl1_c(const int16_t *src,
                         uint16_t *dest, int dstW,
                         int big_endian, int output_bits)
{
    int i;
    int shift = 15 - output_bits;
    int output_shift = 16 - output_bits;

    for (i = 0; i < dstW; i++) {
        int val = src[i] + (1 << (shift - 1));
        output_pixel(&dest[i], val);
    }
}

static void yuv2p01xlX_c(const int16_t *filter, int filterSize,
                         const int16_t **src, uint16_t *dest, int dstW,
                         int big_endian, int output_bits)
{
    int i, j;
    int shift = 11 + 16 - output_bits;
    int output_shift = 16 - output_bits;

    for (i = 0; i < dstW; i++) {
        int val = 1 << (shift - 1);

        for (j = 0; j < filterSize; j++)
            val += src[j][i] * filter[j];

        output_pixel(&dest[i], val);
    }
}

static void yuv2p01xcX_c(int big_endian, const uint8_t *chrDither,
                         const int16_t *chrFilter, int chrFilterSize,
                         const int16_t **chrUSrc, const int16_t **chrVSrc,
                         uint8_t *dest8, int chrDstW, int output_bits)
{
    uint16_t *dest = (uint16_t*)dest8;
    int i, j;
    int shift = 11 + 16 - output_bits;
    int output_shift = 16 - output_bits;

    for (i = 0; i < chrDstW; i++) {
        int u = 1 << (shift - 1);
        int v = 1 << (shift - 1);

        for (j = 0; j < chrFilterSize; j++) {
            u += chrUSrc[j][i] * chrFilter[j];
            v += chrVSrc[j][i] * chrFilter[j];
        }

        output_pixel(&dest[2*i]  , u);
        output_pixel(&dest[2*i+1], v);
    }
}

#undef output_pixel

#define yuv2p01x_wrapper(bits)                                                 \
    static void yuv2p0 ## bits ## l1_LE_c(const int16_t *src,                  \
                                          uint8_t *dest, int dstW,             \
                                          const uint8_t *dither, int offset)   \
    {                                                                          \
        yuv2p01xl1_c(src, (uint16_t*)dest, dstW, 0, bits);                     \
    }                                                                          \
                                                                               \
    static void yuv2p0 ## bits ## l1_BE_c(const int16_t *src,                  \
                                          uint8_t *dest, int dstW,             \
                                          const uint8_t *dither, int offset)   \
    {                                                                          \
        yuv2p01xl1_c(src, (uint16_t*)dest, dstW, 1, bits);                     \
    }                                                                          \
                                                                               \
    static void yuv2p0 ## bits ## lX_LE_c(const int16_t *filter,               \
                                          int filterSize, const int16_t **src, \
                                          uint8_t *dest, int dstW,             \
                                          const uint8_t *dither, int offset)   \
    {                                                                          \
        yuv2p01xlX_c(filter, filterSize, src, (uint16_t*)dest, dstW, 0, bits); \
    }                                                                          \
                                                                               \
    static void yuv2p0 ## bits ## lX_BE_c(const int16_t *filter,               \
                                          int filterSize, const int16_t **src, \
                                          uint8_t *dest, int dstW,             \
                                          const uint8_t *dither, int offset)   \
    {                                                                          \
        yuv2p01xlX_c(filter, filterSize, src, (uint16_t*)dest, dstW, 1, bits); \
    }                                                                          \
                                                                               \
    static void yuv2p0 ## bits ## cX_LE_c(enum AVPixelFormat dstFormat,        \
                                          const uint8_t *chrDither,            \
                                          const int16_t *chrFilter,            \
                                          int chrFilterSize,                   \
                                          const int16_t **chrUSrc,             \
                                          const int16_t **chrVSrc,             \
                                          uint8_t *dest8, int chrDstW)         \
    {                                                                          \
        yuv2p01xcX_c(0, chrDither, chrFilter, chrFilterSize, chrUSrc, chrVSrc, \
                     dest8, chrDstW, bits);                                    \
    }                                                                          \
                                                                               \
    static void yuv2p0 ## bits ## cX_BE_c(enum AVPixelFormat dstFormat,        \
                                          const uint8_t *chrDither,            \
                                          const int16_t *chrFilter,            \
                                          int chrFilterSize,                   \
                                          const int16_t **chrUSrc,             \
                                          const int16_t **chrVSrc,             \
                                          uint8_t *dest8, int chrDstW)         \
    {                                                                          \
        yuv2p01xcX_c(1, chrDither, chrFilter, chrFilterSize, chrUSrc, chrVSrc, \
                     dest8, chrDstW, bits);                                    \
    }

yuv2p01x_wrapper(10)
yuv2p01x_wrapper(12)

#define accumulate_bit(acc, val) \
    acc <<= 1; \
    acc |= (val) >= 234
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
    int err = 0;

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
        if (c->dither == SWS_DITHER_ED) {
            Y1 += (7*err + 1*c->dither_error[0][i] + 5*c->dither_error[0][i+1] + 3*c->dither_error[0][i+2] + 8 - 256)>>4;
            c->dither_error[0][i] = err;
            acc = 2*acc + (Y1 >= 128);
            Y1 -= 220*(acc&1);

            err = Y2 + ((7*Y1 + 1*c->dither_error[0][i+1] + 5*c->dither_error[0][i+2] + 3*c->dither_error[0][i+3] + 8 - 256)>>4);
            c->dither_error[0][i+1] = Y1;
            acc = 2*acc + (err >= 128);
            err -= 220*(acc&1);
        } else {
            accumulate_bit(acc, Y1 + d128[(i + 0) & 7]);
            accumulate_bit(acc, Y2 + d128[(i + 1) & 7]);
        }
        if ((i & 7) == 6) {
            output_pixel(*dest++, acc);
        }
    }
    c->dither_error[0][i] = err;

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
    av_assert2(yalpha  <= 4096U);

    if (c->dither == SWS_DITHER_ED) {
        int err = 0;
        unsigned acc = 0;
        for (i = 0; i < dstW; i +=2) {
            int Y;

            Y = (buf0[i + 0] * yalpha1 + buf1[i + 0] * yalpha) >> 19;
            Y += (7*err + 1*c->dither_error[0][i] + 5*c->dither_error[0][i+1] + 3*c->dither_error[0][i+2] + 8 - 256)>>4;
            c->dither_error[0][i] = err;
            acc = 2*acc + (Y >= 128);
            Y -= 220*(acc&1);

            err = (buf0[i + 1] * yalpha1 + buf1[i + 1] * yalpha) >> 19;
            err += (7*Y + 1*c->dither_error[0][i+1] + 5*c->dither_error[0][i+2] + 3*c->dither_error[0][i+3] + 8 - 256)>>4;
            c->dither_error[0][i+1] = Y;
            acc = 2*acc + (err >= 128);
            err -= 220*(acc&1);

            if ((i & 7) == 6)
                output_pixel(*dest++, acc);
        }
        c->dither_error[0][i] = err;
    } else {
        for (i = 0; i < dstW; i += 8) {
            int Y;
            unsigned acc = 0;

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
}

static av_always_inline void
yuv2mono_1_c_template(SwsContext *c, const int16_t *buf0,
                      const int16_t *ubuf[2], const int16_t *vbuf[2],
                      const int16_t *abuf0, uint8_t *dest, int dstW,
                      int uvalpha, int y, enum AVPixelFormat target)
{
    const uint8_t * const d128 = ff_dither_8x8_220[y & 7];
    int i;

    if (c->dither == SWS_DITHER_ED) {
        int err = 0;
        unsigned acc = 0;
        for (i = 0; i < dstW; i +=2) {
            int Y;

            Y = ((buf0[i + 0] + 64) >> 7);
            Y += (7*err + 1*c->dither_error[0][i] + 5*c->dither_error[0][i+1] + 3*c->dither_error[0][i+2] + 8 - 256)>>4;
            c->dither_error[0][i] = err;
            acc = 2*acc + (Y >= 128);
            Y -= 220*(acc&1);

            err = ((buf0[i + 1] + 64) >> 7);
            err += (7*Y + 1*c->dither_error[0][i+1] + 5*c->dither_error[0][i+2] + 3*c->dither_error[0][i+3] + 8 - 256)>>4;
            c->dither_error[0][i+1] = Y;
            acc = 2*acc + (err >= 128);
            err -= 220*(acc&1);

            if ((i & 7) == 6)
                output_pixel(*dest++, acc);
        }
        c->dither_error[0][i] = err;
    } else {
        for (i = 0; i < dstW; i += 8) {
            unsigned acc = 0;
            accumulate_bit(acc, ((buf0[i + 0] + 64) >> 7) + d128[0]);
            accumulate_bit(acc, ((buf0[i + 1] + 64) >> 7) + d128[1]);
            accumulate_bit(acc, ((buf0[i + 2] + 64) >> 7) + d128[2]);
            accumulate_bit(acc, ((buf0[i + 3] + 64) >> 7) + d128[3]);
            accumulate_bit(acc, ((buf0[i + 4] + 64) >> 7) + d128[4]);
            accumulate_bit(acc, ((buf0[i + 5] + 64) >> 7) + d128[5]);
            accumulate_bit(acc, ((buf0[i + 6] + 64) >> 7) + d128[6]);
            accumulate_bit(acc, ((buf0[i + 7] + 64) >> 7) + d128[7]);

            output_pixel(*dest++, acc);
        }
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
    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;

        if ((Y1 | Y2 | U | V) & 0x100) {
            Y1 = av_clip_uint8(Y1);
            Y2 = av_clip_uint8(Y2);
            U  = av_clip_uint8(U);
            V  = av_clip_uint8(V);
        }

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
            int Y1 = (buf0[i * 2    ]+64) >> 7;
            int Y2 = (buf0[i * 2 + 1]+64) >> 7;
            int U  = (ubuf0[i]       +64) >> 7;
            int V  = (vbuf0[i]       +64) >> 7;

            if ((Y1 | Y2 | U | V) & 0x100) {
                Y1 = av_clip_uint8(Y1);
                Y2 = av_clip_uint8(Y2);
                U  = av_clip_uint8(U);
                V  = av_clip_uint8(V);
            }

            output_pixels(i * 4, Y1, U, Y2, V);
        }
    } else {
        const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 = (buf0[i * 2    ]    + 64) >> 7;
            int Y2 = (buf0[i * 2 + 1]    + 64) >> 7;
            int U  = (ubuf0[i] + ubuf1[i]+128) >> 8;
            int V  = (vbuf0[i] + vbuf1[i]+128) >> 8;

            if ((Y1 | Y2 | U | V) & 0x100) {
                Y1 = av_clip_uint8(Y1);
                Y2 = av_clip_uint8(Y2);
                U  = av_clip_uint8(U);
                V  = av_clip_uint8(V);
            }

            output_pixels(i * 4, Y1, U, Y2, V);
        }
    }
}

#undef output_pixels

YUV2PACKEDWRAPPER(yuv2, 422, yuyv422, AV_PIX_FMT_YUYV422)
YUV2PACKEDWRAPPER(yuv2, 422, yvyu422, AV_PIX_FMT_YVYU422)
YUV2PACKEDWRAPPER(yuv2, 422, uyvy422, AV_PIX_FMT_UYVY422)

#define R_B ((target == AV_PIX_FMT_RGB48LE || target == AV_PIX_FMT_RGB48BE || target == AV_PIX_FMT_RGBA64LE || target == AV_PIX_FMT_RGBA64BE) ? R : B)
#define B_R ((target == AV_PIX_FMT_RGB48LE || target == AV_PIX_FMT_RGB48BE || target == AV_PIX_FMT_RGBA64LE || target == AV_PIX_FMT_RGBA64BE) ? B : R)
#define output_pixel(pos, val) \
    if (is_be) { \
        AV_WB16(pos, val); \
    } else { \
        AV_WL16(pos, val); \
    }

static av_always_inline void
yuv2ya16_X_c_template(SwsContext *c, const int16_t *lumFilter,
                        const int32_t **lumSrc, int lumFilterSize,
                        const int16_t *chrFilter, const int32_t **unused_chrUSrc,
                        const int32_t **unused_chrVSrc, int unused_chrFilterSize,
                        const int32_t **alpSrc, uint16_t *dest, int dstW,
                        int y, enum AVPixelFormat target,
                        int unused_hasAlpha, int unused_eightbytes, int is_be)
{
    int hasAlpha = !!alpSrc;
    int i;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = -0x40000000;
        int A = 0xffff;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * lumFilter[j];

        Y >>= 15;
        Y += (1<<3) + 0x8000;
        Y = av_clip_uint16(Y);

        if (hasAlpha) {
            A = -0x40000000 + (1<<14);
            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * lumFilter[j];

            A >>= 15;
            A += 0x8000;
            A = av_clip_uint16(A);
        }

        output_pixel(&dest[2 * i    ], Y);
        output_pixel(&dest[2 * i + 1], A);
    }
}

static av_always_inline void
yuv2ya16_2_c_template(SwsContext *c, const int32_t *buf[2],
                        const int32_t *unused_ubuf[2], const int32_t *unused_vbuf[2],
                        const int32_t *abuf[2], uint16_t *dest, int dstW,
                        int yalpha, int unused_uvalpha, int y,
                        enum AVPixelFormat target, int unused_hasAlpha,
                        int unused_eightbytes, int is_be)
{
    int hasAlpha = abuf && abuf[0] && abuf[1];
    const int32_t *buf0  = buf[0],  *buf1  = buf[1],
    *abuf0 = hasAlpha ? abuf[0] : NULL,
    *abuf1 = hasAlpha ? abuf[1] : NULL;
    int  yalpha1 = 4096 - yalpha;
    int i;

    av_assert2(yalpha  <= 4096U);

    for (i = 0; i < dstW; i++) {
        int Y = (buf0[i] * yalpha1 + buf1[i] * yalpha) >> 15;
        int A;

        Y = av_clip_uint16(Y);

        if (hasAlpha) {
            A = (abuf0[i] * yalpha1 + abuf1[i] * yalpha) >> 15;
            A = av_clip_uint16(A);
        }

        output_pixel(&dest[2 * i    ], Y);
        output_pixel(&dest[2 * i + 1], hasAlpha ? A : 65535);
    }
}

static av_always_inline void
yuv2ya16_1_c_template(SwsContext *c, const int32_t *buf0,
                        const int32_t *unused_ubuf[2], const int32_t *unused_vbuf[2],
                        const int32_t *abuf0, uint16_t *dest, int dstW,
                        int unused_uvalpha, int y, enum AVPixelFormat target,
                        int unused_hasAlpha, int unused_eightbytes, int is_be)
{
    int hasAlpha = !!abuf0;
    int i;

    for (i = 0; i < dstW; i++) {
        int Y = buf0[i] >> 3;/* 19 - 16 */
        int A;

        Y = av_clip_uint16(Y);

        if (hasAlpha) {
            A = abuf0[i] >> 3;
            if (A & 0x100)
                A = av_clip_uint16(A);
        }

        output_pixel(&dest[2 * i    ], Y);
        output_pixel(&dest[2 * i + 1], hasAlpha ? A : 65535);
    }
}

static av_always_inline void
yuv2rgba64_X_c_template(SwsContext *c, const int16_t *lumFilter,
                       const int32_t **lumSrc, int lumFilterSize,
                       const int16_t *chrFilter, const int32_t **chrUSrc,
                       const int32_t **chrVSrc, int chrFilterSize,
                       const int32_t **alpSrc, uint16_t *dest, int dstW,
                       int y, enum AVPixelFormat target, int hasAlpha, int eightbytes,
                       int is_be)
{
    int i;
    int A1 = 0xffff<<14, A2 = 0xffff<<14;

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int j;
        unsigned Y1 = -0x40000000;
        unsigned Y2 = -0x40000000;
        int U  = -(128 << 23); // 19
        int V  = -(128 << 23);
        int R, G, B;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i * 2]     * (unsigned)lumFilter[j];
            Y2 += lumSrc[j][i * 2 + 1] * (unsigned)lumFilter[j];
        }
        for (j = 0; j < chrFilterSize; j++) {;
            U += chrUSrc[j][i] * (unsigned)chrFilter[j];
            V += chrVSrc[j][i] * (unsigned)chrFilter[j];
        }

        if (hasAlpha) {
            A1 = -0x40000000;
            A2 = -0x40000000;
            for (j = 0; j < lumFilterSize; j++) {
                A1 += alpSrc[j][i * 2]     * (unsigned)lumFilter[j];
                A2 += alpSrc[j][i * 2 + 1] * (unsigned)lumFilter[j];
            }
            A1 >>= 1;
            A1 += 0x20002000;
            A2 >>= 1;
            A2 += 0x20002000;
        }

        // 8 bits: 12+15=27; 16 bits: 12+19=31
        Y1 = (int)Y1 >> 14; // 10
        Y1 += 0x10000;
        Y2 = (int)Y2 >> 14;
        Y2 += 0x10000;
        U  >>= 14;
        V  >>= 14;

        // 8 bits: 27 -> 17 bits, 16 bits: 31 - 14 = 17 bits
        Y1 -= c->yuv2rgb_y_offset;
        Y2 -= c->yuv2rgb_y_offset;
        Y1 *= c->yuv2rgb_y_coeff;
        Y2 *= c->yuv2rgb_y_coeff;
        Y1 += (1 << 13) - (1 << 29); // 21
        Y2 += (1 << 13) - (1 << 29);
        // 8 bits: 17 + 13 bits = 30 bits, 16 bits: 17 + 13 bits = 30 bits

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        // 8 bits: 30 - 22 = 8 bits, 16 bits: 30 bits - 14 = 16 bits
        output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + Y1) >> 14) + (1<<15), 16));
        output_pixel(&dest[1], av_clip_uintp2(((int)(  G + Y1) >> 14) + (1<<15), 16));
        output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + Y1) >> 14) + (1<<15), 16));
        if (eightbytes) {
            output_pixel(&dest[3], av_clip_uintp2(A1      , 30) >> 14);
            output_pixel(&dest[4], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[5], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[6], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[7], av_clip_uintp2(A2      , 30) >> 14);
            dest += 8;
        } else {
            output_pixel(&dest[3], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[4], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[5], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
            dest += 6;
        }
    }
}

static av_always_inline void
yuv2rgba64_2_c_template(SwsContext *c, const int32_t *buf[2],
                       const int32_t *ubuf[2], const int32_t *vbuf[2],
                       const int32_t *abuf[2], uint16_t *dest, int dstW,
                       int yalpha, int uvalpha, int y,
                       enum AVPixelFormat target, int hasAlpha, int eightbytes,
                       int is_be)
{
    const int32_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    int  yalpha1 = 4096 - yalpha;
    int uvalpha1 = 4096 - uvalpha;
    int i;
    int A1 = 0xffff<<14, A2 = 0xffff<<14;

    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        unsigned Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha) >> 14;
        unsigned Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha) >> 14;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha - (128 << 23)) >> 14;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha - (128 << 23)) >> 14;
        int R, G, B;

        Y1 -= c->yuv2rgb_y_offset;
        Y2 -= c->yuv2rgb_y_offset;
        Y1 *= c->yuv2rgb_y_coeff;
        Y2 *= c->yuv2rgb_y_coeff;
        Y1 += (1 << 13) - (1 << 29);
        Y2 += (1 << 13) - (1 << 29);

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        if (hasAlpha) {
            A1 = (abuf0[i * 2    ] * yalpha1 + abuf1[i * 2    ] * yalpha) >> 1;
            A2 = (abuf0[i * 2 + 1] * yalpha1 + abuf1[i * 2 + 1] * yalpha) >> 1;

            A1 += 1 << 13;
            A2 += 1 << 13;
        }

        output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + Y1) >> 14) + (1<<15), 16));
        output_pixel(&dest[1], av_clip_uintp2(((int)(  G + Y1) >> 14) + (1<<15), 16));
        output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + Y1) >> 14) + (1<<15), 16));
        if (eightbytes) {
            output_pixel(&dest[3], av_clip_uintp2(A1      , 30) >> 14);
            output_pixel(&dest[4], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[5], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[6], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[7], av_clip_uintp2(A2      , 30) >> 14);
            dest += 8;
        } else {
            output_pixel(&dest[3], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[4], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
            output_pixel(&dest[5], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
            dest += 6;
        }
    }
}

static av_always_inline void
yuv2rgba64_1_c_template(SwsContext *c, const int32_t *buf0,
                       const int32_t *ubuf[2], const int32_t *vbuf[2],
                       const int32_t *abuf0, uint16_t *dest, int dstW,
                       int uvalpha, int y, enum AVPixelFormat target,
                       int hasAlpha, int eightbytes, int is_be)
{
    const int32_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    int i;
    int A1 = 0xffff<<14, A2= 0xffff<<14;

    if (uvalpha < 2048) {
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            SUINT Y1 = (buf0[i * 2]    ) >> 2;
            SUINT Y2 = (buf0[i * 2 + 1]) >> 2;
            int U  = (ubuf0[i] - (128 << 11)) >> 2;
            int V  = (vbuf0[i] - (128 << 11)) >> 2;
            int R, G, B;

            Y1 -= c->yuv2rgb_y_offset;
            Y2 -= c->yuv2rgb_y_offset;
            Y1 *= c->yuv2rgb_y_coeff;
            Y2 *= c->yuv2rgb_y_coeff;
            Y1 += (1 << 13) - (1 << 29);
            Y2 += (1 << 13) - (1 << 29);

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] * (1 << 11);
                A2 = abuf0[i * 2 + 1] * (1 << 11);

                A1 += 1 << 13;
                A2 += 1 << 13;
            }

            R = V * c->yuv2rgb_v2r_coeff;
            G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
            B =                            U * c->yuv2rgb_u2b_coeff;

            output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + Y1) >> 14) + (1<<15), 16));
            output_pixel(&dest[1], av_clip_uintp2(((int)(  G + Y1) >> 14) + (1<<15), 16));
            output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + Y1) >> 14) + (1<<15), 16));
            if (eightbytes) {
                output_pixel(&dest[3], av_clip_uintp2(A1      , 30) >> 14);
                output_pixel(&dest[4], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[5], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[6], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[7], av_clip_uintp2(A2      , 30) >> 14);
                dest += 8;
            } else {
                output_pixel(&dest[3], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[4], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[5], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
                dest += 6;
            }
        }
    } else {
        const int32_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        int A1 = 0xffff<<14, A2 = 0xffff<<14;
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            SUINT Y1 = (buf0[i * 2]    ) >> 2;
            SUINT Y2 = (buf0[i * 2 + 1]) >> 2;
            int U  = (ubuf0[i] + ubuf1[i] - (128 << 12)) >> 3;
            int V  = (vbuf0[i] + vbuf1[i] - (128 << 12)) >> 3;
            int R, G, B;

            Y1 -= c->yuv2rgb_y_offset;
            Y2 -= c->yuv2rgb_y_offset;
            Y1 *= c->yuv2rgb_y_coeff;
            Y2 *= c->yuv2rgb_y_coeff;
            Y1 += (1 << 13) - (1 << 29);
            Y2 += (1 << 13) - (1 << 29);

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] * (1 << 11);
                A2 = abuf0[i * 2 + 1] * (1 << 11);

                A1 += 1 << 13;
                A2 += 1 << 13;
            }

            R = V * c->yuv2rgb_v2r_coeff;
            G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
            B =                            U * c->yuv2rgb_u2b_coeff;

            output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + Y1) >> 14) + (1<<15), 16));
            output_pixel(&dest[1], av_clip_uintp2(((int)(  G + Y1) >> 14) + (1<<15), 16));
            output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + Y1) >> 14) + (1<<15), 16));
            if (eightbytes) {
                output_pixel(&dest[3], av_clip_uintp2(A1      , 30) >> 14);
                output_pixel(&dest[4], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[5], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[6], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[7], av_clip_uintp2(A2      , 30) >> 14);
                dest += 8;
            } else {
                output_pixel(&dest[3], av_clip_uintp2(((int)(R_B + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[4], av_clip_uintp2(((int)(  G + Y2) >> 14) + (1<<15), 16));
                output_pixel(&dest[5], av_clip_uintp2(((int)(B_R + Y2) >> 14) + (1<<15), 16));
                dest += 6;
            }
        }
    }
}

static av_always_inline void
yuv2rgba64_full_X_c_template(SwsContext *c, const int16_t *lumFilter,
                       const int32_t **lumSrc, int lumFilterSize,
                       const int16_t *chrFilter, const int32_t **chrUSrc,
                       const int32_t **chrVSrc, int chrFilterSize,
                       const int32_t **alpSrc, uint16_t *dest, int dstW,
                       int y, enum AVPixelFormat target, int hasAlpha,
                       int eightbytes, int is_be)
{
    int i;
    int A = 0xffff<<14;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y  = -0x40000000;
        int U  = -(128 << 23); // 19
        int V  = -(128 << 23);
        int R, G, B;

        for (j = 0; j < lumFilterSize; j++) {
            Y += lumSrc[j][i]  * (unsigned)lumFilter[j];
        }
        for (j = 0; j < chrFilterSize; j++) {;
            U += chrUSrc[j][i] * (unsigned)chrFilter[j];
            V += chrVSrc[j][i] * (unsigned)chrFilter[j];
        }

        if (hasAlpha) {
            A = -0x40000000;
            for (j = 0; j < lumFilterSize; j++) {
                A += alpSrc[j][i] * (unsigned)lumFilter[j];
            }
            A >>= 1;
            A += 0x20002000;
        }

        // 8bit: 12+15=27; 16-bit: 12+19=31
        Y  >>= 14; // 10
        Y += 0x10000;
        U  >>= 14;
        V  >>= 14;

        // 8bit: 27 -> 17bit, 16bit: 31 - 14 = 17bit
        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += (1 << 13) - (1<<29); // 21
        // 8bit: 17 + 13bit = 30bit, 16bit: 17 + 13bit = 30bit

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        // 8bit: 30 - 22 = 8bit, 16bit: 30bit - 14 = 16bit
        output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + (unsigned)Y)>>14) + (1<<15), 16));
        output_pixel(&dest[1], av_clip_uintp2(((int)(  G + (unsigned)Y)>>14) + (1<<15), 16));
        output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + (unsigned)Y)>>14) + (1<<15), 16));
        if (eightbytes) {
            output_pixel(&dest[3], av_clip_uintp2(A, 30) >> 14);
            dest += 4;
        } else {
            dest += 3;
        }
    }
}

static av_always_inline void
yuv2rgba64_full_2_c_template(SwsContext *c, const int32_t *buf[2],
                       const int32_t *ubuf[2], const int32_t *vbuf[2],
                       const int32_t *abuf[2], uint16_t *dest, int dstW,
                       int yalpha, int uvalpha, int y,
                       enum AVPixelFormat target, int hasAlpha, int eightbytes,
                       int is_be)
{
    const int32_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    int  yalpha1 = 4096 - yalpha;
    int uvalpha1 = 4096 - uvalpha;
    int i;
    int A = 0xffff<<14;

    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < dstW; i++) {
        int Y  = (buf0[i]     * yalpha1  + buf1[i]     * yalpha) >> 14;
        int U  = (ubuf0[i]   * uvalpha1 + ubuf1[i]     * uvalpha - (128 << 23)) >> 14;
        int V  = (vbuf0[i]   * uvalpha1 + vbuf1[i]     * uvalpha - (128 << 23)) >> 14;
        int R, G, B;

        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += (1 << 13) - (1 << 29);

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        if (hasAlpha) {
            A = (abuf0[i] * yalpha1 + abuf1[i] * yalpha) >> 1;

            A += 1 << 13;
        }

        output_pixel(&dest[0], av_clip_uintp2(((R_B + Y) >> 14) + (1<<15), 16));
        output_pixel(&dest[1], av_clip_uintp2(((  G + Y) >> 14) + (1<<15), 16));
        output_pixel(&dest[2], av_clip_uintp2(((B_R + Y) >> 14) + (1<<15), 16));
        if (eightbytes) {
            output_pixel(&dest[3], av_clip_uintp2(A, 30) >> 14);
            dest += 4;
        } else {
            dest += 3;
        }
    }
}

static av_always_inline void
yuv2rgba64_full_1_c_template(SwsContext *c, const int32_t *buf0,
                       const int32_t *ubuf[2], const int32_t *vbuf[2],
                       const int32_t *abuf0, uint16_t *dest, int dstW,
                       int uvalpha, int y, enum AVPixelFormat target,
                       int hasAlpha, int eightbytes, int is_be)
{
    const int32_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    int i;
    int A = 0xffff<<14;

    if (uvalpha < 2048) {
        for (i = 0; i < dstW; i++) {
            SUINT Y  = (buf0[i]) >> 2;
            int U  = (ubuf0[i] - (128 << 11)) >> 2;
            int V  = (vbuf0[i] - (128 << 11)) >> 2;
            int R, G, B;

            Y -= c->yuv2rgb_y_offset;
            Y *= c->yuv2rgb_y_coeff;
            Y += (1 << 13) - (1 << 29);

            if (hasAlpha) {
                A = abuf0[i] * (1 << 11);

                A += 1 << 13;
            }

            R = V * c->yuv2rgb_v2r_coeff;
            G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
            B =                            U * c->yuv2rgb_u2b_coeff;

            output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + Y) >> 14) + (1<<15), 16));
            output_pixel(&dest[1], av_clip_uintp2(((int)(  G + Y) >> 14) + (1<<15), 16));
            output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + Y) >> 14) + (1<<15), 16));
            if (eightbytes) {
                output_pixel(&dest[3], av_clip_uintp2(A, 30) >> 14);
                dest += 4;
            } else {
                dest += 3;
            }
        }
    } else {
        const int32_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        int A = 0xffff<<14;
        for (i = 0; i < dstW; i++) {
            SUINT Y  = (buf0[i]    ) >> 2;
            int U  = (ubuf0[i] + ubuf1[i] - (128 << 12)) >> 3;
            int V  = (vbuf0[i] + vbuf1[i] - (128 << 12)) >> 3;
            int R, G, B;

            Y -= c->yuv2rgb_y_offset;
            Y *= c->yuv2rgb_y_coeff;
            Y += (1 << 13) - (1 << 29);

            if (hasAlpha) {
                A = abuf0[i] * (1 << 11);

                A += 1 << 13;
            }

            R = V * c->yuv2rgb_v2r_coeff;
            G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
            B =                            U * c->yuv2rgb_u2b_coeff;

            output_pixel(&dest[0], av_clip_uintp2(((int)(R_B + Y) >> 14) + (1<<15), 16));
            output_pixel(&dest[1], av_clip_uintp2(((int)(  G + Y) >> 14) + (1<<15), 16));
            output_pixel(&dest[2], av_clip_uintp2(((int)(B_R + Y) >> 14) + (1<<15), 16));
            if (eightbytes) {
                output_pixel(&dest[3], av_clip_uintp2(A, 30) >> 14);
                dest += 4;
            } else {
                dest += 3;
            }
        }
    }
}

#undef output_pixel
#undef r_b
#undef b_r

#define YUV2PACKED16WRAPPER_EXT(name, base, ext, fmt, is_be, hasAlpha, eightbytes) \
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
                          alpSrc, dest, dstW, y, fmt, hasAlpha, eightbytes, is_be); \
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
                          dest, dstW, yalpha, uvalpha, y, fmt, hasAlpha, eightbytes, is_be); \
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
                                  dstW, uvalpha, y, fmt, hasAlpha, eightbytes, is_be); \
}
#define YUV2PACKED16WRAPPER(name, base, ext, base_fmt, endianness, hasAlpha, eightbytes) \
    YUV2PACKED16WRAPPER_EXT(name, base, ext, base_fmt ## endianness, IS_BE(endianness), hasAlpha, eightbytes)

YUV2PACKED16WRAPPER(yuv2, rgba64, rgb48be, AV_PIX_FMT_RGB48, BE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64, rgb48le, AV_PIX_FMT_RGB48, LE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64, bgr48be, AV_PIX_FMT_BGR48, BE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64, bgr48le, AV_PIX_FMT_BGR48, LE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64, rgba64be, AV_PIX_FMT_RGBA64, BE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, rgba64le, AV_PIX_FMT_RGBA64, LE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, rgbx64be, AV_PIX_FMT_RGBA64, BE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, rgbx64le, AV_PIX_FMT_RGBA64, LE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, bgra64be, AV_PIX_FMT_BGRA64, BE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, bgra64le, AV_PIX_FMT_BGRA64, LE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, bgrx64be, AV_PIX_FMT_BGRA64, BE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64, bgrx64le, AV_PIX_FMT_BGRA64, LE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, ya16, ya16be, AV_PIX_FMT_YA16, BE, 1, 0)
YUV2PACKED16WRAPPER(yuv2, ya16, ya16le, AV_PIX_FMT_YA16, LE, 1, 0)

YUV2PACKED16WRAPPER(yuv2, rgba64_full, rgb48be_full, AV_PIX_FMT_RGB48, BE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, rgb48le_full, AV_PIX_FMT_RGB48, LE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, bgr48be_full, AV_PIX_FMT_BGR48, BE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, bgr48le_full, AV_PIX_FMT_BGR48, LE, 0, 0)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, rgba64be_full, AV_PIX_FMT_RGBA64, BE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, rgba64le_full, AV_PIX_FMT_RGBA64, LE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, rgbx64be_full, AV_PIX_FMT_RGBA64, BE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, rgbx64le_full, AV_PIX_FMT_RGBA64, LE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, bgra64be_full, AV_PIX_FMT_BGRA64, BE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, bgra64le_full, AV_PIX_FMT_BGRA64, LE, 1, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, bgrx64be_full, AV_PIX_FMT_BGRA64, BE, 0, 1)
YUV2PACKED16WRAPPER(yuv2, rgba64_full, bgrx64le_full, AV_PIX_FMT_BGRA64, LE, 0, 1)

/*
 * Write out 2 RGB pixels in the target pixel format. This function takes a
 * R/G/B LUT as generated by ff_yuv2rgb_c_init_tables(), which takes care of
 * things like endianness conversion and shifting. The caller takes care of
 * setting the correct offset in these tables from the chroma (U/V) values.
 * This function then uses the luminance (Y1/Y2) values to write out the
 * correct RGB values into the destination buffer.
 */
static av_always_inline void
yuv2rgb_write(uint8_t *_dest, int i, int Y1, int Y2,
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

            av_assert2((((r[Y1] + g[Y1] + b[Y1]) >> sh) & 0xFF) == 0);
            dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1] + (A1 << sh);
            dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2] + (A2 << sh);
        } else {
#if defined(ASSERT_LEVEL) && ASSERT_LEVEL > 1
            int sh = (target == AV_PIX_FMT_RGB32_1 || target == AV_PIX_FMT_BGR32_1) ? 0 : 24;

            av_assert2((((r[Y1] + g[Y1] + b[Y1]) >> sh) & 0xFF) == 0xFF);
#endif
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
            dr1 = ff_dither_2x2_8[ y & 1     ][0];
            dg1 = ff_dither_2x2_4[ y & 1     ][0];
            db1 = ff_dither_2x2_8[(y & 1) ^ 1][0];
            dr2 = ff_dither_2x2_8[ y & 1     ][1];
            dg2 = ff_dither_2x2_4[ y & 1     ][1];
            db2 = ff_dither_2x2_8[(y & 1) ^ 1][1];
        } else if (target == AV_PIX_FMT_RGB555 || target == AV_PIX_FMT_BGR555) {
            dr1 = ff_dither_2x2_8[ y & 1     ][0];
            dg1 = ff_dither_2x2_8[ y & 1     ][1];
            db1 = ff_dither_2x2_8[(y & 1) ^ 1][0];
            dr2 = ff_dither_2x2_8[ y & 1     ][1];
            dg2 = ff_dither_2x2_8[ y & 1     ][0];
            db2 = ff_dither_2x2_8[(y & 1) ^ 1][1];
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
    } else if (target == AV_PIX_FMT_X2RGB10 || target == AV_PIX_FMT_X2BGR10) {
        uint32_t *dest = (uint32_t *) _dest;
        const uint32_t *r = (const uint32_t *) _r;
        const uint32_t *g = (const uint32_t *) _g;
        const uint32_t *b = (const uint32_t *) _b;
        dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1];
        dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2];
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

        r =  c->table_rV[V + YUVRGB_TABLE_HEADROOM];
        g = (c->table_gU[U + YUVRGB_TABLE_HEADROOM] + c->table_gV[V + YUVRGB_TABLE_HEADROOM]);
        b =  c->table_bU[U + YUVRGB_TABLE_HEADROOM];

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
    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < ((dstW + 1) >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;
        int A1, A2;
        const void *r =  c->table_rV[V + YUVRGB_TABLE_HEADROOM],
                   *g = (c->table_gU[U + YUVRGB_TABLE_HEADROOM] + c->table_gV[V + YUVRGB_TABLE_HEADROOM]),
                   *b =  c->table_bU[U + YUVRGB_TABLE_HEADROOM];

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
            int Y1 = (buf0[i * 2    ] + 64) >> 7;
            int Y2 = (buf0[i * 2 + 1] + 64) >> 7;
            int U  = (ubuf0[i]        + 64) >> 7;
            int V  = (vbuf0[i]        + 64) >> 7;
            int A1, A2;
            const void *r =  c->table_rV[V + YUVRGB_TABLE_HEADROOM],
                       *g = (c->table_gU[U + YUVRGB_TABLE_HEADROOM] + c->table_gV[V + YUVRGB_TABLE_HEADROOM]),
                       *b =  c->table_bU[U + YUVRGB_TABLE_HEADROOM];

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] * 255 + 16384 >> 15;
                A2 = abuf0[i * 2 + 1] * 255 + 16384 >> 15;
                A1 = av_clip_uint8(A1);
                A2 = av_clip_uint8(A2);
            }

            yuv2rgb_write(dest, i, Y1, Y2, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                          r, g, b, y, target, hasAlpha);
        }
    } else {
        const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        for (i = 0; i < ((dstW + 1) >> 1); i++) {
            int Y1 = (buf0[i * 2    ]     +  64) >> 7;
            int Y2 = (buf0[i * 2 + 1]     +  64) >> 7;
            int U  = (ubuf0[i] + ubuf1[i] + 128) >> 8;
            int V  = (vbuf0[i] + vbuf1[i] + 128) >> 8;
            int A1, A2;
            const void *r =  c->table_rV[V + YUVRGB_TABLE_HEADROOM],
                       *g = (c->table_gU[U + YUVRGB_TABLE_HEADROOM] + c->table_gV[V + YUVRGB_TABLE_HEADROOM]),
                       *b =  c->table_bU[U + YUVRGB_TABLE_HEADROOM];

            if (hasAlpha) {
                A1 = (abuf0[i * 2    ] + 64) >> 7;
                A2 = (abuf0[i * 2 + 1] + 64) >> 7;
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

#define YUV2RGBWRAPPERX2(name, base, ext, fmt, hasAlpha) \
YUV2RGBWRAPPERX(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _2_c(SwsContext *c, const int16_t *buf[2], \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf[2], uint8_t *dest, int dstW, \
                                int yalpha, int uvalpha, int y) \
{ \
    name ## base ## _2_c_template(c, buf, ubuf, vbuf, abuf, \
                                  dest, dstW, yalpha, uvalpha, y, fmt, hasAlpha); \
}

#define YUV2RGBWRAPPER(name, base, ext, fmt, hasAlpha) \
YUV2RGBWRAPPERX2(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _1_c(SwsContext *c, const int16_t *buf0, \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf0, uint8_t *dest, int dstW, \
                                int uvalpha, int y) \
{ \
    name ## base ## _1_c_template(c, buf0, ubuf, vbuf, abuf0, dest, \
                                  dstW, uvalpha, y, fmt, hasAlpha); \
}

#if CONFIG_SMALL
YUV2RGBWRAPPER(yuv2rgb,,  32_1,  AV_PIX_FMT_RGB32_1,   CONFIG_SWSCALE_ALPHA && c->needAlpha)
YUV2RGBWRAPPER(yuv2rgb,,  32,    AV_PIX_FMT_RGB32,     CONFIG_SWSCALE_ALPHA && c->needAlpha)
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
YUV2RGBWRAPPER(yuv2, rgb, x2rgb10, AV_PIX_FMT_X2RGB10, 0)
YUV2RGBWRAPPER(yuv2, rgb, x2bgr10, AV_PIX_FMT_X2BGR10, 0)

static av_always_inline void yuv2rgb_write_full(SwsContext *c,
    uint8_t *dest, int i, int Y, int A, int U, int V,
    int y, enum AVPixelFormat target, int hasAlpha, int err[4])
{
    int R, G, B;
    int isrgb8 = target == AV_PIX_FMT_BGR8 || target == AV_PIX_FMT_RGB8;

    Y -= c->yuv2rgb_y_offset;
    Y *= c->yuv2rgb_y_coeff;
    Y += 1 << 21;
    R = (unsigned)Y + V*(unsigned)c->yuv2rgb_v2r_coeff;
    G = (unsigned)Y + V*(unsigned)c->yuv2rgb_v2g_coeff + U*(unsigned)c->yuv2rgb_u2g_coeff;
    B = (unsigned)Y +                                    U*(unsigned)c->yuv2rgb_u2b_coeff;
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
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_RGB4_BYTE:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB8:
    {
        int r,g,b;

        switch (c->dither) {
        case SWS_DITHER_NONE:
            if (isrgb8) {
                r = av_clip_uintp2(R >> 27, 3);
                g = av_clip_uintp2(G >> 27, 3);
                b = av_clip_uintp2(B >> 28, 2);
            } else {
                r = av_clip_uintp2(R >> 29, 1);
                g = av_clip_uintp2(G >> 28, 2);
                b = av_clip_uintp2(B >> 29, 1);
            }
            break;
        default:
        case SWS_DITHER_AUTO:
        case SWS_DITHER_ED:
            R >>= 22;
            G >>= 22;
            B >>= 22;
            R += (7*err[0] + 1*c->dither_error[0][i] + 5*c->dither_error[0][i+1] + 3*c->dither_error[0][i+2])>>4;
            G += (7*err[1] + 1*c->dither_error[1][i] + 5*c->dither_error[1][i+1] + 3*c->dither_error[1][i+2])>>4;
            B += (7*err[2] + 1*c->dither_error[2][i] + 5*c->dither_error[2][i+1] + 3*c->dither_error[2][i+2])>>4;
            c->dither_error[0][i] = err[0];
            c->dither_error[1][i] = err[1];
            c->dither_error[2][i] = err[2];
            r = R >> (isrgb8 ? 5 : 7);
            g = G >> (isrgb8 ? 5 : 6);
            b = B >> (isrgb8 ? 6 : 7);
            r = av_clip(r, 0, isrgb8 ? 7 : 1);
            g = av_clip(g, 0, isrgb8 ? 7 : 3);
            b = av_clip(b, 0, isrgb8 ? 3 : 1);
            err[0] = R - r*(isrgb8 ? 36 : 255);
            err[1] = G - g*(isrgb8 ? 36 : 85);
            err[2] = B - b*(isrgb8 ? 85 : 255);
            break;
        case SWS_DITHER_A_DITHER:
            if (isrgb8) {
  /* see http://pippin.gimp.org/a_dither/ for details/origin */
#define A_DITHER(u,v)   (((((u)+((v)*236))*119)&0xff))
                r = (((R >> 19) + A_DITHER(i,y)  -96)>>8);
                g = (((G >> 19) + A_DITHER(i + 17,y) - 96)>>8);
                b = (((B >> 20) + A_DITHER(i + 17*2,y) -96)>>8);
                r = av_clip_uintp2(r, 3);
                g = av_clip_uintp2(g, 3);
                b = av_clip_uintp2(b, 2);
            } else {
                r = (((R >> 21) + A_DITHER(i,y)-256)>>8);
                g = (((G >> 19) + A_DITHER(i + 17,y)-256)>>8);
                b = (((B >> 21) + A_DITHER(i + 17*2,y)-256)>>8);
                r = av_clip_uintp2(r, 1);
                g = av_clip_uintp2(g, 2);
                b = av_clip_uintp2(b, 1);
            }
            break;
        case SWS_DITHER_X_DITHER:
            if (isrgb8) {
  /* see http://pippin.gimp.org/a_dither/ for details/origin */
#define X_DITHER(u,v)   (((((u)^((v)*237))*181)&0x1ff)/2)
                r = (((R >> 19) + X_DITHER(i,y) - 96)>>8);
                g = (((G >> 19) + X_DITHER(i + 17,y) - 96)>>8);
                b = (((B >> 20) + X_DITHER(i + 17*2,y) - 96)>>8);
                r = av_clip_uintp2(r, 3);
                g = av_clip_uintp2(g, 3);
                b = av_clip_uintp2(b, 2);
            } else {
                r = (((R >> 21) + X_DITHER(i,y)-256)>>8);
                g = (((G >> 19) + X_DITHER(i + 17,y)-256)>>8);
                b = (((B >> 21) + X_DITHER(i + 17*2,y)-256)>>8);
                r = av_clip_uintp2(r, 1);
                g = av_clip_uintp2(g, 2);
                b = av_clip_uintp2(b, 1);
            }

            break;
        }

        if(target == AV_PIX_FMT_BGR4_BYTE) {
            dest[0] = r + 2*g + 8*b;
        } else if(target == AV_PIX_FMT_RGB4_BYTE) {
            dest[0] = b + 2*g + 8*r;
        } else if(target == AV_PIX_FMT_BGR8) {
            dest[0] = r + 8*g + 64*b;
        } else if(target == AV_PIX_FMT_RGB8) {
            dest[0] = b + 4*g + 32*r;
        } else
            av_assert2(0);
        break;}
    }
}

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
    int err[4] = {0};
    int A = 0; //init to silence warning

    if(   target == AV_PIX_FMT_BGR4_BYTE || target == AV_PIX_FMT_RGB4_BYTE
       || target == AV_PIX_FMT_BGR8      || target == AV_PIX_FMT_RGB8)
        step = 1;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 1<<9;
        int U = (1<<9)-(128 << 19);
        int V = (1<<9)-(128 << 19);

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
            A = 1 << 18;
            for (j = 0; j < lumFilterSize; j++) {
                A += alpSrc[j][i] * lumFilter[j];
            }
            A >>= 19;
            if (A & 0x100)
                A = av_clip_uint8(A);
        }
        yuv2rgb_write_full(c, dest, i, Y, A, U, V, y, target, hasAlpha, err);
        dest += step;
    }
    c->dither_error[0][i] = err[0];
    c->dither_error[1][i] = err[1];
    c->dither_error[2][i] = err[2];
}

static av_always_inline void
yuv2rgb_full_2_c_template(SwsContext *c, const int16_t *buf[2],
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
    int step = (target == AV_PIX_FMT_RGB24 || target == AV_PIX_FMT_BGR24) ? 3 : 4;
    int err[4] = {0};
    int A = 0; // init to silcene warning

    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    if(   target == AV_PIX_FMT_BGR4_BYTE || target == AV_PIX_FMT_RGB4_BYTE
       || target == AV_PIX_FMT_BGR8      || target == AV_PIX_FMT_RGB8)
        step = 1;

    for (i = 0; i < dstW; i++) {
        int Y = ( buf0[i] * yalpha1  +  buf1[i] * yalpha             ) >> 10; //FIXME rounding
        int U = (ubuf0[i] * uvalpha1 + ubuf1[i] * uvalpha-(128 << 19)) >> 10;
        int V = (vbuf0[i] * uvalpha1 + vbuf1[i] * uvalpha-(128 << 19)) >> 10;

        if (hasAlpha) {
            A = (abuf0[i] * yalpha1 + abuf1[i] * yalpha + (1<<18)) >> 19;
            if (A & 0x100)
                A = av_clip_uint8(A);
        }

        yuv2rgb_write_full(c, dest, i, Y, A, U, V, y, target, hasAlpha, err);
        dest += step;
    }
    c->dither_error[0][i] = err[0];
    c->dither_error[1][i] = err[1];
    c->dither_error[2][i] = err[2];
}

static av_always_inline void
yuv2rgb_full_1_c_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum AVPixelFormat target,
                     int hasAlpha)
{
    const int16_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    int i;
    int step = (target == AV_PIX_FMT_RGB24 || target == AV_PIX_FMT_BGR24) ? 3 : 4;
    int err[4] = {0};

    if(   target == AV_PIX_FMT_BGR4_BYTE || target == AV_PIX_FMT_RGB4_BYTE
       || target == AV_PIX_FMT_BGR8      || target == AV_PIX_FMT_RGB8)
        step = 1;

    if (uvalpha < 2048) {
        int A = 0; //init to silence warning
        for (i = 0; i < dstW; i++) {
            int Y = buf0[i] * 4;
            int U = (ubuf0[i] - (128<<7)) * 4;
            int V = (vbuf0[i] - (128<<7)) * 4;

            if (hasAlpha) {
                A = (abuf0[i] + 64) >> 7;
                if (A & 0x100)
                    A = av_clip_uint8(A);
            }

            yuv2rgb_write_full(c, dest, i, Y, A, U, V, y, target, hasAlpha, err);
            dest += step;
        }
    } else {
        const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        int A = 0; //init to silence warning
        for (i = 0; i < dstW; i++) {
            int Y = buf0[i] * 4;
            int U = (ubuf0[i] + ubuf1[i] - (128<<8)) * 2;
            int V = (vbuf0[i] + vbuf1[i] - (128<<8)) * 2;

            if (hasAlpha) {
                A = (abuf0[i] + 64) >> 7;
                if (A & 0x100)
                    A = av_clip_uint8(A);
            }

            yuv2rgb_write_full(c, dest, i, Y, A, U, V, y, target, hasAlpha, err);
            dest += step;
        }
    }

    c->dither_error[0][i] = err[0];
    c->dither_error[1][i] = err[1];
    c->dither_error[2][i] = err[2];
}

#if CONFIG_SMALL
YUV2RGBWRAPPER(yuv2, rgb_full, bgra32_full, AV_PIX_FMT_BGRA,  CONFIG_SWSCALE_ALPHA && c->needAlpha)
YUV2RGBWRAPPER(yuv2, rgb_full, abgr32_full, AV_PIX_FMT_ABGR,  CONFIG_SWSCALE_ALPHA && c->needAlpha)
YUV2RGBWRAPPER(yuv2, rgb_full, rgba32_full, AV_PIX_FMT_RGBA,  CONFIG_SWSCALE_ALPHA && c->needAlpha)
YUV2RGBWRAPPER(yuv2, rgb_full, argb32_full, AV_PIX_FMT_ARGB,  CONFIG_SWSCALE_ALPHA && c->needAlpha)
#else
#if CONFIG_SWSCALE_ALPHA
YUV2RGBWRAPPER(yuv2, rgb_full, bgra32_full, AV_PIX_FMT_BGRA,  1)
YUV2RGBWRAPPER(yuv2, rgb_full, abgr32_full, AV_PIX_FMT_ABGR,  1)
YUV2RGBWRAPPER(yuv2, rgb_full, rgba32_full, AV_PIX_FMT_RGBA,  1)
YUV2RGBWRAPPER(yuv2, rgb_full, argb32_full, AV_PIX_FMT_ARGB,  1)
#endif
YUV2RGBWRAPPER(yuv2, rgb_full, bgrx32_full, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, xbgr32_full, AV_PIX_FMT_ABGR,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, rgbx32_full, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, xrgb32_full, AV_PIX_FMT_ARGB,  0)
#endif
YUV2RGBWRAPPER(yuv2, rgb_full, bgr24_full,  AV_PIX_FMT_BGR24, 0)
YUV2RGBWRAPPER(yuv2, rgb_full, rgb24_full,  AV_PIX_FMT_RGB24, 0)

YUV2RGBWRAPPER(yuv2, rgb_full, bgr4_byte_full,  AV_PIX_FMT_BGR4_BYTE, 0)
YUV2RGBWRAPPER(yuv2, rgb_full, rgb4_byte_full,  AV_PIX_FMT_RGB4_BYTE, 0)
YUV2RGBWRAPPER(yuv2, rgb_full, bgr8_full,   AV_PIX_FMT_BGR8,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, rgb8_full,   AV_PIX_FMT_RGB8,  0)

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
    int A = 0; // init to silence warning

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 1 << 9;
        int U = (1 << 9) - (128 << 19);
        int V = (1 << 9) - (128 << 19);
        int R, G, B;

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

            if (A & 0xF8000000)
                A =  av_clip_uintp2(A, 27);
        }

        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += 1 << (SH-1);
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
                dest16[3][i] = A >> (SH - 3);
        } else {
            dest[0][i] = G >> 22;
            dest[1][i] = B >> 22;
            dest[2][i] = R >> 22;
            if (hasAlpha)
                dest[3][i] = A >> 19;
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

static void
yuv2gbrp16_full_X_c(SwsContext *c, const int16_t *lumFilter,
                    const int16_t **lumSrcx, int lumFilterSize,
                    const int16_t *chrFilter, const int16_t **chrUSrcx,
                    const int16_t **chrVSrcx, int chrFilterSize,
                    const int16_t **alpSrcx, uint8_t **dest,
                    int dstW, int y)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->dstFormat);
    int i;
    int hasAlpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) && alpSrcx;
    uint16_t **dest16 = (uint16_t**)dest;
    const int32_t **lumSrc  = (const int32_t**)lumSrcx;
    const int32_t **chrUSrc = (const int32_t**)chrUSrcx;
    const int32_t **chrVSrc = (const int32_t**)chrVSrcx;
    const int32_t **alpSrc  = (const int32_t**)alpSrcx;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = -0x40000000;
        int U = -(128 << 23);
        int V = -(128 << 23);
        int R, G, B, A;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * (unsigned)lumFilter[j];

        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * (unsigned)chrFilter[j];
            V += chrVSrc[j][i] * (unsigned)chrFilter[j];
        }

        Y >>= 14;
        Y += 0x10000;
        U >>= 14;
        V >>= 14;

        if (hasAlpha) {
            A = -0x40000000;

            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * (unsigned)lumFilter[j];

            A >>= 1;
            A += 0x20002000;
        }

        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += (1 << 13) - (1 << 29);
        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        dest16[2][i] = av_clip_uintp2(((Y + R) >> 14) + (1<<15), 16);
        dest16[0][i] = av_clip_uintp2(((Y + G) >> 14) + (1<<15), 16);
        dest16[1][i] = av_clip_uintp2(((Y + B) >> 14) + (1<<15), 16);

        if (hasAlpha)
            dest16[3][i] = av_clip_uintp2(A, 30) >> 14;
    }
    if ((!isBE(c->dstFormat)) != (!HAVE_BIGENDIAN)) {
        for (i = 0; i < dstW; i++) {
            dest16[0][i] = av_bswap16(dest16[0][i]);
            dest16[1][i] = av_bswap16(dest16[1][i]);
            dest16[2][i] = av_bswap16(dest16[2][i]);
            if (hasAlpha)
                dest16[3][i] = av_bswap16(dest16[3][i]);
        }
    }
}

static void
yuv2gbrpf32_full_X_c(SwsContext *c, const int16_t *lumFilter,
                    const int16_t **lumSrcx, int lumFilterSize,
                    const int16_t *chrFilter, const int16_t **chrUSrcx,
                    const int16_t **chrVSrcx, int chrFilterSize,
                    const int16_t **alpSrcx, uint8_t **dest,
                    int dstW, int y)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->dstFormat);
    int i;
    int hasAlpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) && alpSrcx;
    uint32_t **dest32 = (uint32_t**)dest;
    const int32_t **lumSrc  = (const int32_t**)lumSrcx;
    const int32_t **chrUSrc = (const int32_t**)chrUSrcx;
    const int32_t **chrVSrc = (const int32_t**)chrVSrcx;
    const int32_t **alpSrc  = (const int32_t**)alpSrcx;
    static const float float_mult = 1.0f / 65535.0f;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = -0x40000000;
        int U = -(128 << 23);
        int V = -(128 << 23);
        int R, G, B, A;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * (unsigned)lumFilter[j];

        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * (unsigned)chrFilter[j];
            V += chrVSrc[j][i] * (unsigned)chrFilter[j];
        }

        Y >>= 14;
        Y += 0x10000;
        U >>= 14;
        V >>= 14;

        if (hasAlpha) {
            A = -0x40000000;

            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * (unsigned)lumFilter[j];

            A >>= 1;
            A += 0x20002000;
        }

        Y -= c->yuv2rgb_y_offset;
        Y *= c->yuv2rgb_y_coeff;
        Y += (1 << 13) - (1 << 29);
        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        R = av_clip_uintp2(((Y + R) >> 14) + (1<<15), 16);
        G = av_clip_uintp2(((Y + G) >> 14) + (1<<15), 16);
        B = av_clip_uintp2(((Y + B) >> 14) + (1<<15), 16);

        dest32[0][i] = av_float2int(float_mult * (float)G);
        dest32[1][i] = av_float2int(float_mult * (float)B);
        dest32[2][i] = av_float2int(float_mult * (float)R);
        if (hasAlpha)
            dest32[3][i] = av_float2int(float_mult * (float)(av_clip_uintp2(A, 30) >> 14));
    }
    if ((!isBE(c->dstFormat)) != (!HAVE_BIGENDIAN)) {
        for (i = 0; i < dstW; i++) {
            dest32[0][i] = av_bswap32(dest32[0][i]);
            dest32[1][i] = av_bswap32(dest32[1][i]);
            dest32[2][i] = av_bswap32(dest32[2][i]);
            if (hasAlpha)
                dest32[3][i] = av_bswap32(dest32[3][i]);
        }
    }
}

static void
yuv2ya8_1_c(SwsContext *c, const int16_t *buf0,
            const int16_t *ubuf[2], const int16_t *vbuf[2],
            const int16_t *abuf0, uint8_t *dest, int dstW,
            int uvalpha, int y)
{
    int hasAlpha = !!abuf0;
    int i;

    for (i = 0; i < dstW; i++) {
        int Y = (buf0[i] + 64) >> 7;
        int A;

        Y = av_clip_uint8(Y);

        if (hasAlpha) {
            A = (abuf0[i] + 64) >> 7;
            if (A & 0x100)
                A = av_clip_uint8(A);
        }

        dest[i * 2    ] = Y;
        dest[i * 2 + 1] = hasAlpha ? A : 255;
    }
}

static void
yuv2ya8_2_c(SwsContext *c, const int16_t *buf[2],
            const int16_t *ubuf[2], const int16_t *vbuf[2],
            const int16_t *abuf[2], uint8_t *dest, int dstW,
            int yalpha, int uvalpha, int y)
{
    int hasAlpha = abuf && abuf[0] && abuf[1];
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    int  yalpha1 = 4096 - yalpha;
    int i;

    av_assert2(yalpha  <= 4096U);

    for (i = 0; i < dstW; i++) {
        int Y = (buf0[i] * yalpha1 + buf1[i] * yalpha) >> 19;
        int A;

        Y = av_clip_uint8(Y);

        if (hasAlpha) {
            A = (abuf0[i] * yalpha1 + abuf1[i] * yalpha) >> 19;
            A = av_clip_uint8(A);
        }

        dest[i * 2    ] = Y;
        dest[i * 2 + 1] = hasAlpha ? A : 255;
    }
}

static void
yuv2ya8_X_c(SwsContext *c, const int16_t *lumFilter,
            const int16_t **lumSrc, int lumFilterSize,
            const int16_t *chrFilter, const int16_t **chrUSrc,
            const int16_t **chrVSrc, int chrFilterSize,
            const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int hasAlpha = !!alpSrc;
    int i;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 1 << 18, A = 1 << 18;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * lumFilter[j];

        Y >>= 19;
        if (Y  & 0x100)
            Y = av_clip_uint8(Y);

        if (hasAlpha) {
            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * lumFilter[j];

            A >>= 19;

            if (A & 0x100)
                A = av_clip_uint8(A);
        }

        dest[2 * i    ] = Y;
        dest[2 * i + 1] = hasAlpha ? A : 255;
    }
}

static void
yuv2ayuv64le_X_c(SwsContext *c, const int16_t *lumFilter,
                 const int16_t **_lumSrc, int lumFilterSize,
                 const int16_t *chrFilter, const int16_t **_chrUSrc,
                 const int16_t **_chrVSrc, int chrFilterSize,
                 const int16_t **_alpSrc, uint8_t *dest, int dstW, int y)
{
    const int32_t **lumSrc  = (const int32_t **) _lumSrc,
                  **chrUSrc = (const int32_t **) _chrUSrc,
                  **chrVSrc = (const int32_t **) _chrVSrc,
                  **alpSrc  = (const int32_t **) _alpSrc;
    int hasAlpha = !!alpSrc;
    int i;

    for (i = 0; i < dstW; i++) {
        int Y = 1 << 14, U = 1 << 14;
        int V = 1 << 14, A = 1 << 14;
        int j;

        Y -= 0x40000000;
        U -= 0x40000000;
        V -= 0x40000000;
        A -= 0x40000000;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * (unsigned)lumFilter[j];

        for (j = 0; j < chrFilterSize; j++)
            U += chrUSrc[j][i] * (unsigned)chrFilter[j];

        for (j = 0; j < chrFilterSize; j++)
            V += chrVSrc[j][i] * (unsigned)chrFilter[j];

        if (hasAlpha)
            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * (unsigned)lumFilter[j];

        Y = 0x8000 + av_clip_int16(Y >> 15);
        U = 0x8000 + av_clip_int16(U >> 15);
        V = 0x8000 + av_clip_int16(V >> 15);
        A = 0x8000 + av_clip_int16(A >> 15);

        AV_WL16(dest + 8 * i, hasAlpha ? A : 65535);
        AV_WL16(dest + 8 * i + 2, Y);
        AV_WL16(dest + 8 * i + 4, U);
        AV_WL16(dest + 8 * i + 6, V);
    }
}

static void
yuv2xv30le_X_c(SwsContext *c, const int16_t *lumFilter,
               const int16_t **lumSrc, int lumFilterSize,
               const int16_t *chrFilter, const int16_t **chrUSrc,
               const int16_t **chrVSrc, int chrFilterSize,
               const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int i;
    for (i = 0; i < dstW; i++) {
        int Y = 1 << 16, U = 1 << 16, V = 1 << 16;
        int j;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * lumFilter[j];

        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }

        Y = av_clip_uintp2(Y >> 17, 10);
        U = av_clip_uintp2(U >> 17, 10);
        V = av_clip_uintp2(V >> 17, 10);

        AV_WL32(dest + 4 * i, U | Y << 10 | V << 20);
    }
}

static void
yuv2xv36le_X_c(SwsContext *c, const int16_t *lumFilter,
               const int16_t **lumSrc, int lumFilterSize,
               const int16_t *chrFilter, const int16_t **chrUSrc,
               const int16_t **chrVSrc, int chrFilterSize,
               const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int i;
    for (i = 0; i < dstW; i++) {
        int Y = 1 << 14, U = 1 << 14, V = 1 << 14, A = 255;
        int j;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * lumFilter[j];

        for (j = 0; j < chrFilterSize; j++) {
            U += chrUSrc[j][i] * chrFilter[j];
            V += chrVSrc[j][i] * chrFilter[j];
        }

        AV_WL16(dest + 8 * i + 2, av_clip_uintp2(Y >> 15, 12) << 4);
        AV_WL16(dest + 8 * i + 0, av_clip_uintp2(U >> 15, 12) << 4);
        AV_WL16(dest + 8 * i + 4, av_clip_uintp2(V >> 15, 12) << 4);
        AV_WL16(dest + 8 * i + 6, A);
    }
}

static void
yuv2vuyX_X_c(SwsContext *c, const int16_t *lumFilter,
             const int16_t **lumSrc, int lumFilterSize,
             const int16_t *chrFilter, const int16_t **chrUSrc,
             const int16_t **chrVSrc, int chrFilterSize,
             const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int i;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 1 << 18, U = 1 << 18;
        int V = 1 << 18, A = 255;

        for (j = 0; j < lumFilterSize; j++)
            Y += lumSrc[j][i] * lumFilter[j];

        for (j = 0; j < chrFilterSize; j++)
            U += chrUSrc[j][i] * chrFilter[j];

        for (j = 0; j < chrFilterSize; j++)
            V += chrVSrc[j][i] * chrFilter[j];

        Y >>= 19;
        U >>= 19;
        V >>= 19;

        if (Y  & 0x100)
            Y = av_clip_uint8(Y);
        if (U  & 0x100)
            U = av_clip_uint8(U);
        if (V  & 0x100)
            V = av_clip_uint8(V);

        if (alpSrc) {
            A = 1 << 18;

            for (j = 0; j < lumFilterSize; j++)
                A += alpSrc[j][i] * lumFilter[j];

            A >>= 19;

            if (A & 0x100)
                A = av_clip_uint8(A);
        }

        dest[4 * i    ] = V;
        dest[4 * i + 1] = U;
        dest[4 * i + 2] = Y;
        dest[4 * i + 3] = A;
    }
}

#define output_pixel(pos, val, bits) \
    AV_WL16(pos, av_clip_uintp2(val >> shift, bits) << output_shift);

#define yuv2y2xx_wrapper(bits)                                          \
    static void                                                         \
    yuv2y2 ## bits ## le_X_c(SwsContext *c, const int16_t *lumFilter,   \
                            const int16_t **lumSrc, int lumFilterSize,  \
                            const int16_t *chrFilter,                   \
                            const int16_t **chrUSrc,                    \
                            const int16_t **chrVSrc, int chrFilterSize, \
                            const int16_t **alpSrc,                     \
                            uint8_t *dest, int dstW, int y)             \
    {                                                                   \
        int i, j;                                                       \
        int shift = 11 + 16 - bits;                                     \
        int output_shift = 16 - bits;                                   \
        for (i = 0; i < ((dstW + 1) >> 1); i++) {                       \
            int Y1 = 1 << (shift - 1), Y2 = 1 << (shift - 1);           \
            int U  = 1 << (shift - 1), V  = 1 << (shift - 1);           \
                                                                        \
            for (j = 0; j < lumFilterSize; j++) {                       \
                Y1 += lumSrc[j][i * 2]     * lumFilter[j];              \
                Y2 += lumSrc[j][i * 2 + 1] * lumFilter[j];              \
            }                                                           \
                                                                        \
            for (j = 0; j < chrFilterSize; j++) {                       \
                U += chrUSrc[j][i] * chrFilter[j];                      \
                V += chrVSrc[j][i] * chrFilter[j];                      \
            }                                                           \
                                                                        \
            output_pixel(dest + 8 * i + 0, Y1, bits);                   \
            output_pixel(dest + 8 * i + 2,  U, bits);                   \
            output_pixel(dest + 8 * i + 4, Y2, bits);                   \
            output_pixel(dest + 8 * i + 6,  V, bits);                   \
        }                                                               \
    }

yuv2y2xx_wrapper(10)
yuv2y2xx_wrapper(12)

#undef output_pixel

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

    if (isSemiPlanarYUV(dstFormat) && isDataInHighBits(dstFormat)) {
        if (desc->comp[0].depth == 10) {
            *yuv2plane1 = isBE(dstFormat) ? yuv2p010l1_BE_c : yuv2p010l1_LE_c;
            *yuv2planeX = isBE(dstFormat) ? yuv2p010lX_BE_c : yuv2p010lX_LE_c;
            *yuv2nv12cX = isBE(dstFormat) ? yuv2p010cX_BE_c : yuv2p010cX_LE_c;
        } else if (desc->comp[0].depth == 12) {
            *yuv2plane1 = isBE(dstFormat) ? yuv2p012l1_BE_c : yuv2p012l1_LE_c;
            *yuv2planeX = isBE(dstFormat) ? yuv2p012lX_BE_c : yuv2p012lX_LE_c;
            *yuv2nv12cX = isBE(dstFormat) ? yuv2p012cX_BE_c : yuv2p012cX_LE_c;
        } else
            av_assert0(0);
    } else if (is16BPS(dstFormat)) {
        *yuv2planeX = isBE(dstFormat) ? yuv2planeX_16BE_c  : yuv2planeX_16LE_c;
        *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_16BE_c  : yuv2plane1_16LE_c;
        if (isSemiPlanarYUV(dstFormat)) {
          *yuv2nv12cX = isBE(dstFormat) ? yuv2nv12cX_16BE_c : yuv2nv12cX_16LE_c;
        }
    } else if (isNBPS(dstFormat)) {
        if (desc->comp[0].depth == 9) {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_9BE_c  : yuv2planeX_9LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_9BE_c  : yuv2plane1_9LE_c;
        } else if (desc->comp[0].depth == 10) {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_10BE_c  : yuv2planeX_10LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_10BE_c  : yuv2plane1_10LE_c;
        } else if (desc->comp[0].depth == 12) {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_12BE_c  : yuv2planeX_12LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_12BE_c  : yuv2plane1_12LE_c;
        } else if (desc->comp[0].depth == 14) {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_14BE_c  : yuv2planeX_14LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_14BE_c  : yuv2plane1_14LE_c;
        } else
            av_assert0(0);
    } else if (dstFormat == AV_PIX_FMT_GRAYF32BE) {
        *yuv2planeX = yuv2planeX_floatBE_c;
        *yuv2plane1 = yuv2plane1_floatBE_c;
    } else if (dstFormat == AV_PIX_FMT_GRAYF32LE) {
        *yuv2planeX = yuv2planeX_floatLE_c;
        *yuv2plane1 = yuv2plane1_floatLE_c;
    } else {
        *yuv2plane1 = yuv2plane1_8_c;
        *yuv2planeX = yuv2planeX_8_c;
        if (isSemiPlanarYUV(dstFormat))
            *yuv2nv12cX = yuv2nv12cX_c;
    }

    if(c->flags & SWS_FULL_CHR_H_INT) {
        switch (dstFormat) {
            case AV_PIX_FMT_RGBA:
#if CONFIG_SMALL
                *yuv2packedX = yuv2rgba32_full_X_c;
                *yuv2packed2 = yuv2rgba32_full_2_c;
                *yuv2packed1 = yuv2rgba32_full_1_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->needAlpha) {
                    *yuv2packedX = yuv2rgba32_full_X_c;
                    *yuv2packed2 = yuv2rgba32_full_2_c;
                    *yuv2packed1 = yuv2rgba32_full_1_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2rgbx32_full_X_c;
                    *yuv2packed2 = yuv2rgbx32_full_2_c;
                    *yuv2packed1 = yuv2rgbx32_full_1_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_ARGB:
#if CONFIG_SMALL
                *yuv2packedX = yuv2argb32_full_X_c;
                *yuv2packed2 = yuv2argb32_full_2_c;
                *yuv2packed1 = yuv2argb32_full_1_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->needAlpha) {
                    *yuv2packedX = yuv2argb32_full_X_c;
                    *yuv2packed2 = yuv2argb32_full_2_c;
                    *yuv2packed1 = yuv2argb32_full_1_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2xrgb32_full_X_c;
                    *yuv2packed2 = yuv2xrgb32_full_2_c;
                    *yuv2packed1 = yuv2xrgb32_full_1_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_BGRA:
#if CONFIG_SMALL
                *yuv2packedX = yuv2bgra32_full_X_c;
                *yuv2packed2 = yuv2bgra32_full_2_c;
                *yuv2packed1 = yuv2bgra32_full_1_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->needAlpha) {
                    *yuv2packedX = yuv2bgra32_full_X_c;
                    *yuv2packed2 = yuv2bgra32_full_2_c;
                    *yuv2packed1 = yuv2bgra32_full_1_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2bgrx32_full_X_c;
                    *yuv2packed2 = yuv2bgrx32_full_2_c;
                    *yuv2packed1 = yuv2bgrx32_full_1_c;
                }
#endif /* !CONFIG_SMALL */
                break;
            case AV_PIX_FMT_ABGR:
#if CONFIG_SMALL
                *yuv2packedX = yuv2abgr32_full_X_c;
                *yuv2packed2 = yuv2abgr32_full_2_c;
                *yuv2packed1 = yuv2abgr32_full_1_c;
#else
#if CONFIG_SWSCALE_ALPHA
                if (c->needAlpha) {
                    *yuv2packedX = yuv2abgr32_full_X_c;
                    *yuv2packed2 = yuv2abgr32_full_2_c;
                    *yuv2packed1 = yuv2abgr32_full_1_c;
                } else
#endif /* CONFIG_SWSCALE_ALPHA */
                {
                    *yuv2packedX = yuv2xbgr32_full_X_c;
                    *yuv2packed2 = yuv2xbgr32_full_2_c;
                    *yuv2packed1 = yuv2xbgr32_full_1_c;
                }
#endif /* !CONFIG_SMALL */
                break;
        case AV_PIX_FMT_RGBA64LE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packedX = yuv2rgba64le_full_X_c;
                *yuv2packed2 = yuv2rgba64le_full_2_c;
                *yuv2packed1 = yuv2rgba64le_full_1_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packedX = yuv2rgbx64le_full_X_c;
                *yuv2packed2 = yuv2rgbx64le_full_2_c;
                *yuv2packed1 = yuv2rgbx64le_full_1_c;
            }
            break;
        case AV_PIX_FMT_RGBA64BE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packedX = yuv2rgba64be_full_X_c;
                *yuv2packed2 = yuv2rgba64be_full_2_c;
                *yuv2packed1 = yuv2rgba64be_full_1_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packedX = yuv2rgbx64be_full_X_c;
                *yuv2packed2 = yuv2rgbx64be_full_2_c;
                *yuv2packed1 = yuv2rgbx64be_full_1_c;
            }
            break;
        case AV_PIX_FMT_BGRA64LE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packedX = yuv2bgra64le_full_X_c;
                *yuv2packed2 = yuv2bgra64le_full_2_c;
                *yuv2packed1 = yuv2bgra64le_full_1_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packedX = yuv2bgrx64le_full_X_c;
                *yuv2packed2 = yuv2bgrx64le_full_2_c;
                *yuv2packed1 = yuv2bgrx64le_full_1_c;
            }
            break;
        case AV_PIX_FMT_BGRA64BE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packedX = yuv2bgra64be_full_X_c;
                *yuv2packed2 = yuv2bgra64be_full_2_c;
                *yuv2packed1 = yuv2bgra64be_full_1_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packedX = yuv2bgrx64be_full_X_c;
                *yuv2packed2 = yuv2bgrx64be_full_2_c;
                *yuv2packed1 = yuv2bgrx64be_full_1_c;
            }
            break;

        case AV_PIX_FMT_RGB24:
            *yuv2packedX = yuv2rgb24_full_X_c;
            *yuv2packed2 = yuv2rgb24_full_2_c;
            *yuv2packed1 = yuv2rgb24_full_1_c;
            break;
        case AV_PIX_FMT_BGR24:
            *yuv2packedX = yuv2bgr24_full_X_c;
            *yuv2packed2 = yuv2bgr24_full_2_c;
            *yuv2packed1 = yuv2bgr24_full_1_c;
            break;
        case AV_PIX_FMT_RGB48LE:
            *yuv2packedX = yuv2rgb48le_full_X_c;
            *yuv2packed2 = yuv2rgb48le_full_2_c;
            *yuv2packed1 = yuv2rgb48le_full_1_c;
            break;
        case AV_PIX_FMT_BGR48LE:
            *yuv2packedX = yuv2bgr48le_full_X_c;
            *yuv2packed2 = yuv2bgr48le_full_2_c;
            *yuv2packed1 = yuv2bgr48le_full_1_c;
            break;
        case AV_PIX_FMT_RGB48BE:
            *yuv2packedX = yuv2rgb48be_full_X_c;
            *yuv2packed2 = yuv2rgb48be_full_2_c;
            *yuv2packed1 = yuv2rgb48be_full_1_c;
            break;
        case AV_PIX_FMT_BGR48BE:
            *yuv2packedX = yuv2bgr48be_full_X_c;
            *yuv2packed2 = yuv2bgr48be_full_2_c;
            *yuv2packed1 = yuv2bgr48be_full_1_c;
            break;
        case AV_PIX_FMT_BGR4_BYTE:
            *yuv2packedX = yuv2bgr4_byte_full_X_c;
            *yuv2packed2 = yuv2bgr4_byte_full_2_c;
            *yuv2packed1 = yuv2bgr4_byte_full_1_c;
            break;
        case AV_PIX_FMT_RGB4_BYTE:
            *yuv2packedX = yuv2rgb4_byte_full_X_c;
            *yuv2packed2 = yuv2rgb4_byte_full_2_c;
            *yuv2packed1 = yuv2rgb4_byte_full_1_c;
            break;
        case AV_PIX_FMT_BGR8:
            *yuv2packedX = yuv2bgr8_full_X_c;
            *yuv2packed2 = yuv2bgr8_full_2_c;
            *yuv2packed1 = yuv2bgr8_full_1_c;
            break;
        case AV_PIX_FMT_RGB8:
            *yuv2packedX = yuv2rgb8_full_X_c;
            *yuv2packed2 = yuv2rgb8_full_2_c;
            *yuv2packed1 = yuv2rgb8_full_1_c;
            break;
        case AV_PIX_FMT_GBRP:
        case AV_PIX_FMT_GBRP9BE:
        case AV_PIX_FMT_GBRP9LE:
        case AV_PIX_FMT_GBRP10BE:
        case AV_PIX_FMT_GBRP10LE:
        case AV_PIX_FMT_GBRP12BE:
        case AV_PIX_FMT_GBRP12LE:
        case AV_PIX_FMT_GBRP14BE:
        case AV_PIX_FMT_GBRP14LE:
        case AV_PIX_FMT_GBRAP:
        case AV_PIX_FMT_GBRAP10BE:
        case AV_PIX_FMT_GBRAP10LE:
        case AV_PIX_FMT_GBRAP12BE:
        case AV_PIX_FMT_GBRAP12LE:
        case AV_PIX_FMT_GBRAP14BE:
        case AV_PIX_FMT_GBRAP14LE:
            *yuv2anyX = yuv2gbrp_full_X_c;
            break;
        case AV_PIX_FMT_GBRP16BE:
        case AV_PIX_FMT_GBRP16LE:
        case AV_PIX_FMT_GBRAP16BE:
        case AV_PIX_FMT_GBRAP16LE:
            *yuv2anyX = yuv2gbrp16_full_X_c;
            break;
        case AV_PIX_FMT_GBRPF32BE:
        case AV_PIX_FMT_GBRPF32LE:
        case AV_PIX_FMT_GBRAPF32BE:
        case AV_PIX_FMT_GBRAPF32LE:
            *yuv2anyX = yuv2gbrpf32_full_X_c;
            break;
        }
        if (!*yuv2packedX && !*yuv2anyX)
            goto YUV_PACKED;
    } else {
        YUV_PACKED:
        switch (dstFormat) {
        case AV_PIX_FMT_RGBA64LE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packed1 = yuv2rgba64le_1_c;
                *yuv2packed2 = yuv2rgba64le_2_c;
                *yuv2packedX = yuv2rgba64le_X_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packed1 = yuv2rgbx64le_1_c;
                *yuv2packed2 = yuv2rgbx64le_2_c;
                *yuv2packedX = yuv2rgbx64le_X_c;
            }
            break;
        case AV_PIX_FMT_RGBA64BE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packed1 = yuv2rgba64be_1_c;
                *yuv2packed2 = yuv2rgba64be_2_c;
                *yuv2packedX = yuv2rgba64be_X_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packed1 = yuv2rgbx64be_1_c;
                *yuv2packed2 = yuv2rgbx64be_2_c;
                *yuv2packedX = yuv2rgbx64be_X_c;
            }
            break;
        case AV_PIX_FMT_BGRA64LE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packed1 = yuv2bgra64le_1_c;
                *yuv2packed2 = yuv2bgra64le_2_c;
                *yuv2packedX = yuv2bgra64le_X_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packed1 = yuv2bgrx64le_1_c;
                *yuv2packed2 = yuv2bgrx64le_2_c;
                *yuv2packedX = yuv2bgrx64le_X_c;
            }
            break;
        case AV_PIX_FMT_BGRA64BE:
#if CONFIG_SWSCALE_ALPHA
            if (c->needAlpha) {
                *yuv2packed1 = yuv2bgra64be_1_c;
                *yuv2packed2 = yuv2bgra64be_2_c;
                *yuv2packedX = yuv2bgra64be_X_c;
            } else
#endif /* CONFIG_SWSCALE_ALPHA */
            {
                *yuv2packed1 = yuv2bgrx64be_1_c;
                *yuv2packed2 = yuv2bgrx64be_2_c;
                *yuv2packedX = yuv2bgrx64be_X_c;
            }
            break;
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
                if (c->needAlpha) {
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
                if (c->needAlpha) {
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
        case AV_PIX_FMT_X2RGB10LE:
        case AV_PIX_FMT_X2RGB10BE:
            *yuv2packed1 = yuv2x2rgb10_1_c;
            *yuv2packed2 = yuv2x2rgb10_2_c;
            *yuv2packedX = yuv2x2rgb10_X_c;
            break;
        case AV_PIX_FMT_X2BGR10LE:
        case AV_PIX_FMT_X2BGR10BE:
            *yuv2packed1 = yuv2x2bgr10_1_c;
            *yuv2packed2 = yuv2x2bgr10_2_c;
            *yuv2packedX = yuv2x2bgr10_X_c;
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
    case AV_PIX_FMT_YA8:
        *yuv2packed1 = yuv2ya8_1_c;
        *yuv2packed2 = yuv2ya8_2_c;
        *yuv2packedX = yuv2ya8_X_c;
        break;
    case AV_PIX_FMT_YA16LE:
        *yuv2packed1 = yuv2ya16le_1_c;
        *yuv2packed2 = yuv2ya16le_2_c;
        *yuv2packedX = yuv2ya16le_X_c;
        break;
    case AV_PIX_FMT_YA16BE:
        *yuv2packed1 = yuv2ya16be_1_c;
        *yuv2packed2 = yuv2ya16be_2_c;
        *yuv2packedX = yuv2ya16be_X_c;
        break;
    case AV_PIX_FMT_AYUV64LE:
        *yuv2packedX = yuv2ayuv64le_X_c;
        break;
    case AV_PIX_FMT_VUYA:
    case AV_PIX_FMT_VUYX:
        *yuv2packedX = yuv2vuyX_X_c;
        break;
    case AV_PIX_FMT_XV30LE:
        *yuv2packedX = yuv2xv30le_X_c;
        break;
    case AV_PIX_FMT_XV36LE:
        *yuv2packedX = yuv2xv36le_X_c;
        break;
    case AV_PIX_FMT_Y210LE:
        *yuv2packedX = yuv2y210le_X_c;
        break;
    case AV_PIX_FMT_Y212LE:
        *yuv2packedX = yuv2y212le_X_c;
        break;
    }
}
