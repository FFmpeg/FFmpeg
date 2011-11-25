/*
 * Copyright (C) 2001-2011 Michael Niedermayer <michaelni@gmx.at>
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

/*
  supported Input formats: YV12, I420/IYUV, YUY2, UYVY, BGR32, BGR32_1, BGR24, BGR16, BGR15, RGB32, RGB32_1, RGB24, Y8/Y800, YVU9/IF09, PAL8
  supported output formats: YV12, I420/IYUV, YUY2, UYVY, {BGR,RGB}{1,4,8,15,16,24,32}, Y8/Y800, YVU9/IF09
  {BGR,RGB}{1,4,8,15,16} support dithering

  unscaled special converters (YV12=I420=IYUV, Y800=Y8)
  YV12 -> {BGR,RGB}{1,4,8,12,15,16,24,32}
  x -> x
  YUV9 -> YV12
  YUV9/YV12 -> Y800
  Y800 -> YUV9/YV12
  BGR24 -> BGR32 & RGB24 -> RGB32
  BGR32 -> BGR24 & RGB32 -> RGB24
  BGR15 -> BGR16
*/

/*
tested special converters (most are tested actually, but I did not write it down ...)
 YV12 -> BGR12/BGR16
 YV12 -> YV12
 BGR15 -> BGR16
 BGR16 -> BGR16
 YVU9 -> YV12

untested special converters
  YV12/I420 -> BGR15/BGR24/BGR32 (it is the yuv2rgb stuff, so it should be OK)
  YV12/I420 -> YV12/I420
  YUY2/BGR15/BGR24/BGR32/RGB24/RGB32 -> same format
  BGR24 -> BGR32 & RGB24 -> RGB32
  BGR32 -> BGR24 & RGB32 -> RGB24
  BGR24 -> YV12
*/

#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "config.h"
#include <assert.h>
#include "swscale.h"
#include "swscale_internal.h"
#include "rgb2rgb.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/cpu.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libavutil/bswap.h"
#include "libavutil/pixdesc.h"


#define RGB2YUV_SHIFT 15
#define BY ( (int)(0.114*219/255*(1<<RGB2YUV_SHIFT)+0.5))
#define BV (-(int)(0.081*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ( (int)(0.500*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ( (int)(0.587*219/255*(1<<RGB2YUV_SHIFT)+0.5))
#define GV (-(int)(0.419*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define GU (-(int)(0.331*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ( (int)(0.299*219/255*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ( (int)(0.500*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define RU (-(int)(0.169*224/255*(1<<RGB2YUV_SHIFT)+0.5))

/*
NOTES
Special versions: fast Y 1:1 scaling (no interpolation in y direction)

TODO
more intelligent misalignment avoidance for the horizontal scaler
write special vertical cubic upscale version
optimize C code (YV12 / minmax)
add support for packed pixel YUV input & output
add support for Y8 output
optimize BGR24 & BGR32
add BGR4 output support
write special BGR->BGR scaler
*/

DECLARE_ALIGNED(8, static const uint8_t, dither_2x2_4)[2][8]={
{  1,   3,   1,   3,   1,   3,   1,   3, },
{  2,   0,   2,   0,   2,   0,   2,   0, },
};

DECLARE_ALIGNED(8, static const uint8_t, dither_2x2_8)[2][8]={
{  6,   2,   6,   2,   6,   2,   6,   2, },
{  0,   4,   0,   4,   0,   4,   0,   4, },
};

DECLARE_ALIGNED(8, const uint8_t, dither_4x4_16)[4][8]={
{  8,   4,  11,   7,   8,   4,  11,   7, },
{  2,  14,   1,  13,   2,  14,   1,  13, },
{ 10,   6,   9,   5,  10,   6,   9,   5, },
{  0,  12,   3,  15,   0,  12,   3,  15, },
};

DECLARE_ALIGNED(8, const uint8_t, dither_8x8_32)[8][8]={
{ 17,   9,  23,  15,  16,   8,  22,  14, },
{  5,  29,   3,  27,   4,  28,   2,  26, },
{ 21,  13,  19,  11,  20,  12,  18,  10, },
{  0,  24,   6,  30,   1,  25,   7,  31, },
{ 16,   8,  22,  14,  17,   9,  23,  15, },
{  4,  28,   2,  26,   5,  29,   3,  27, },
{ 20,  12,  18,  10,  21,  13,  19,  11, },
{  1,  25,   7,  31,   0,  24,   6,  30, },
};

DECLARE_ALIGNED(8, const uint8_t, dither_8x8_73)[8][8]={
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
DECLARE_ALIGNED(8, const uint8_t, dither_8x8_220)[8][8]={
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
DECLARE_ALIGNED(8, const uint8_t, dither_8x8_220)[8][8]={
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
DECLARE_ALIGNED(8, const uint8_t, dither_8x8_220)[8][8]={
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
DECLARE_ALIGNED(8, const uint8_t, dither_8x8_220)[8][8]={
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
DECLARE_ALIGNED(8, const uint8_t, dither_8x8_128)[8][8] = {
{  36, 68, 60, 92, 34, 66, 58, 90,},
{ 100,  4,124, 28, 98,  2,122, 26,},
{  52, 84, 44, 76, 50, 82, 42, 74,},
{ 116, 20,108, 12,114, 18,106, 10,},
{  32, 64, 56, 88, 38, 70, 62, 94,},
{  96,  0,120, 24,102,  6,126, 30,},
{  48, 80, 40, 72, 54, 86, 46, 78,},
{ 112, 16,104,  8,118, 22,110, 14,},
};
DECLARE_ALIGNED(8, const uint8_t, ff_sws_pb_64)[8] =
{  64, 64, 64, 64, 64, 64, 64, 64 };

DECLARE_ALIGNED(8, const uint8_t, dithers)[8][8][8]={
{
  {   0,  1,  0,  1,  0,  1,  0,  1,},
  {   1,  0,  1,  0,  1,  0,  1,  0,},
  {   0,  1,  0,  1,  0,  1,  0,  1,},
  {   1,  0,  1,  0,  1,  0,  1,  0,},
  {   0,  1,  0,  1,  0,  1,  0,  1,},
  {   1,  0,  1,  0,  1,  0,  1,  0,},
  {   0,  1,  0,  1,  0,  1,  0,  1,},
  {   1,  0,  1,  0,  1,  0,  1,  0,},
},{
  {   1,  2,  1,  2,  1,  2,  1,  2,},
  {   3,  0,  3,  0,  3,  0,  3,  0,},
  {   1,  2,  1,  2,  1,  2,  1,  2,},
  {   3,  0,  3,  0,  3,  0,  3,  0,},
  {   1,  2,  1,  2,  1,  2,  1,  2,},
  {   3,  0,  3,  0,  3,  0,  3,  0,},
  {   1,  2,  1,  2,  1,  2,  1,  2,},
  {   3,  0,  3,  0,  3,  0,  3,  0,},
},{
  {   2,  4,  3,  5,  2,  4,  3,  5,},
  {   6,  0,  7,  1,  6,  0,  7,  1,},
  {   3,  5,  2,  4,  3,  5,  2,  4,},
  {   7,  1,  6,  0,  7,  1,  6,  0,},
  {   2,  4,  3,  5,  2,  4,  3,  5,},
  {   6,  0,  7,  1,  6,  0,  7,  1,},
  {   3,  5,  2,  4,  3,  5,  2,  4,},
  {   7,  1,  6,  0,  7,  1,  6,  0,},
},{
  {   4,  8,  7, 11,  4,  8,  7, 11,},
  {  12,  0, 15,  3, 12,  0, 15,  3,},
  {   6, 10,  5,  9,  6, 10,  5,  9,},
  {  14,  2, 13,  1, 14,  2, 13,  1,},
  {   4,  8,  7, 11,  4,  8,  7, 11,},
  {  12,  0, 15,  3, 12,  0, 15,  3,},
  {   6, 10,  5,  9,  6, 10,  5,  9,},
  {  14,  2, 13,  1, 14,  2, 13,  1,},
},{
  {   9, 17, 15, 23,  8, 16, 14, 22,},
  {  25,  1, 31,  7, 24,  0, 30,  6,},
  {  13, 21, 11, 19, 12, 20, 10, 18,},
  {  29,  5, 27,  3, 28,  4, 26,  2,},
  {   8, 16, 14, 22,  9, 17, 15, 23,},
  {  24,  0, 30,  6, 25,  1, 31,  7,},
  {  12, 20, 10, 18, 13, 21, 11, 19,},
  {  28,  4, 26,  2, 29,  5, 27,  3,},
},{
  {  18, 34, 30, 46, 17, 33, 29, 45,},
  {  50,  2, 62, 14, 49,  1, 61, 13,},
  {  26, 42, 22, 38, 25, 41, 21, 37,},
  {  58, 10, 54,  6, 57,  9, 53,  5,},
  {  16, 32, 28, 44, 19, 35, 31, 47,},
  {  48,  0, 60, 12, 51,  3, 63, 15,},
  {  24, 40, 20, 36, 27, 43, 23, 39,},
  {  56,  8, 52,  4, 59, 11, 55,  7,},
},{
  {  18, 34, 30, 46, 17, 33, 29, 45,},
  {  50,  2, 62, 14, 49,  1, 61, 13,},
  {  26, 42, 22, 38, 25, 41, 21, 37,},
  {  58, 10, 54,  6, 57,  9, 53,  5,},
  {  16, 32, 28, 44, 19, 35, 31, 47,},
  {  48,  0, 60, 12, 51,  3, 63, 15,},
  {  24, 40, 20, 36, 27, 43, 23, 39,},
  {  56,  8, 52,  4, 59, 11, 55,  7,},
},{
  {  36, 68, 60, 92, 34, 66, 58, 90,},
  { 100,  4,124, 28, 98,  2,122, 26,},
  {  52, 84, 44, 76, 50, 82, 42, 74,},
  { 116, 20,108, 12,114, 18,106, 10,},
  {  32, 64, 56, 88, 38, 70, 62, 94,},
  {  96,  0,120, 24,102,  6,126, 30,},
  {  48, 80, 40, 72, 54, 86, 46, 78,},
  { 112, 16,104,  8,118, 22,110, 14,},
}};

static const uint8_t flat64[8]={64,64,64,64,64,64,64,64};

const uint16_t dither_scale[15][16]={
{    2,    3,    3,    5,    5,    5,    5,    5,    5,    5,    5,    5,    5,    5,    5,    5,},
{    2,    3,    7,    7,   13,   13,   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,},
{    3,    3,    4,   15,   15,   29,   57,   57,   57,  113,  113,  113,  113,  113,  113,  113,},
{    3,    4,    4,    5,   31,   31,   61,  121,  241,  241,  241,  241,  481,  481,  481,  481,},
{    3,    4,    5,    5,    6,   63,   63,  125,  249,  497,  993,  993,  993,  993,  993, 1985,},
{    3,    5,    6,    6,    6,    7,  127,  127,  253,  505, 1009, 2017, 4033, 4033, 4033, 4033,},
{    3,    5,    6,    7,    7,    7,    8,  255,  255,  509, 1017, 2033, 4065, 8129,16257,16257,},
{    3,    5,    6,    8,    8,    8,    8,    9,  511,  511, 1021, 2041, 4081, 8161,16321,32641,},
{    3,    5,    7,    8,    9,    9,    9,    9,   10, 1023, 1023, 2045, 4089, 8177,16353,32705,},
{    3,    5,    7,    8,   10,   10,   10,   10,   10,   11, 2047, 2047, 4093, 8185,16369,32737,},
{    3,    5,    7,    8,   10,   11,   11,   11,   11,   11,   12, 4095, 4095, 8189,16377,32753,},
{    3,    5,    7,    9,   10,   12,   12,   12,   12,   12,   12,   13, 8191, 8191,16381,32761,},
{    3,    5,    7,    9,   10,   12,   13,   13,   13,   13,   13,   13,   14,16383,16383,32765,},
{    3,    5,    7,    9,   10,   12,   14,   14,   14,   14,   14,   14,   14,   15,32767,32767,},
{    3,    5,    7,    9,   11,   12,   14,   15,   15,   15,   15,   15,   15,   15,   16,65535,},
};

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
yuv2NBPS( 9, BE, 1, 10, int16_t);
yuv2NBPS( 9, LE, 0, 10, int16_t);
yuv2NBPS(10, BE, 1, 10, int16_t);
yuv2NBPS(10, LE, 0, 10, int16_t);
yuv2NBPS(16, BE, 1, 16, int32_t);
yuv2NBPS(16, LE, 0, 16, int32_t);

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
    enum PixelFormat dstFormat = c->dstFormat;
    const uint8_t *chrDither = c->chrDither8;
    int i;

    if (dstFormat == PIX_FMT_NV12)
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
        if (target == PIX_FMT_GRAY16BE) { \
            AV_WB16(pos, val); \
        } else { \
            AV_WL16(pos, val); \
        }

static av_always_inline void
yuv2gray16_X_c_template(SwsContext *c, const int16_t *lumFilter,
                        const int32_t **lumSrc, int lumFilterSize,
                        const int16_t *chrFilter, const int32_t **chrUSrc,
                        const int32_t **chrVSrc, int chrFilterSize,
                        const int32_t **alpSrc, uint16_t *dest, int dstW,
                        int y, enum PixelFormat target)
{
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int j;
        int Y1 = 1 << 14;
        int Y2 = 1 << 14;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i * 2]     * lumFilter[j];
            Y2 += lumSrc[j][i * 2 + 1] * lumFilter[j];
        }
        Y1 >>= 15;
        Y2 >>= 15;
        if ((Y1 | Y2) & 0x10000) {
            Y1 = av_clip_uint16(Y1);
            Y2 = av_clip_uint16(Y2);
        }
        output_pixel(&dest[i * 2 + 0], Y1);
        output_pixel(&dest[i * 2 + 1], Y2);
    }
}

static av_always_inline void
yuv2gray16_2_c_template(SwsContext *c, const int32_t *buf[2],
                        const int32_t *ubuf[2], const int32_t *vbuf[2],
                        const int32_t *abuf[2], uint16_t *dest, int dstW,
                        int yalpha, int uvalpha, int y,
                        enum PixelFormat target)
{
    int  yalpha1 = 4095 - yalpha;
    int i;
    const int32_t *buf0 = buf[0], *buf1 = buf[1];

    for (i = 0; i < (dstW >> 1); i++) {
        int Y1 = (buf0[i * 2    ] * yalpha1 + buf1[i * 2    ] * yalpha) >> 15;
        int Y2 = (buf0[i * 2 + 1] * yalpha1 + buf1[i * 2 + 1] * yalpha) >> 15;

        output_pixel(&dest[i * 2 + 0], Y1);
        output_pixel(&dest[i * 2 + 1], Y2);
    }
}

static av_always_inline void
yuv2gray16_1_c_template(SwsContext *c, const int32_t *buf0,
                        const int32_t *ubuf[2], const int32_t *vbuf[2],
                        const int32_t *abuf0, uint16_t *dest, int dstW,
                        int uvalpha, int y, enum PixelFormat target)
{
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int Y1 = (buf0[i * 2    ]+4)>>3;
        int Y2 = (buf0[i * 2 + 1]+4)>>3;

        output_pixel(&dest[i * 2 + 0], Y1);
        output_pixel(&dest[i * 2 + 1], Y2);
    }
}

#undef output_pixel

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

YUV2PACKED16WRAPPER(yuv2gray16,, LE, PIX_FMT_GRAY16LE);
YUV2PACKED16WRAPPER(yuv2gray16,, BE, PIX_FMT_GRAY16BE);

#define output_pixel(pos, acc) \
    if (target == PIX_FMT_MONOBLACK) { \
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
                      int y, enum PixelFormat target)
{
    const uint8_t * const d128=dither_8x8_220[y&7];
    uint8_t *g = c->table_gU[128] + c->table_gV[128];
    int i;
    int acc = 0;

    for (i = 0; i < dstW - 1; i += 2) {
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
        acc += acc + g[Y1 + d128[(i + 0) & 7]];
        acc += acc + g[Y2 + d128[(i + 1) & 7]];
        if ((i & 7) == 6) {
            output_pixel(*dest++, acc);
        }
    }
}

static av_always_inline void
yuv2mono_2_c_template(SwsContext *c, const int16_t *buf[2],
                      const int16_t *ubuf[2], const int16_t *vbuf[2],
                      const int16_t *abuf[2], uint8_t *dest, int dstW,
                      int yalpha, int uvalpha, int y,
                      enum PixelFormat target)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1];
    const uint8_t * const d128 = dither_8x8_220[y & 7];
    uint8_t *g = c->table_gU[128] + c->table_gV[128];
    int  yalpha1 = 4095 - yalpha;
    int i;

    for (i = 0; i < dstW - 7; i += 8) {
        int acc =    g[((buf0[i    ] * yalpha1 + buf1[i    ] * yalpha) >> 19) + d128[0]];
        acc += acc + g[((buf0[i + 1] * yalpha1 + buf1[i + 1] * yalpha) >> 19) + d128[1]];
        acc += acc + g[((buf0[i + 2] * yalpha1 + buf1[i + 2] * yalpha) >> 19) + d128[2]];
        acc += acc + g[((buf0[i + 3] * yalpha1 + buf1[i + 3] * yalpha) >> 19) + d128[3]];
        acc += acc + g[((buf0[i + 4] * yalpha1 + buf1[i + 4] * yalpha) >> 19) + d128[4]];
        acc += acc + g[((buf0[i + 5] * yalpha1 + buf1[i + 5] * yalpha) >> 19) + d128[5]];
        acc += acc + g[((buf0[i + 6] * yalpha1 + buf1[i + 6] * yalpha) >> 19) + d128[6]];
        acc += acc + g[((buf0[i + 7] * yalpha1 + buf1[i + 7] * yalpha) >> 19) + d128[7]];
        output_pixel(*dest++, acc);
    }
}

static av_always_inline void
yuv2mono_1_c_template(SwsContext *c, const int16_t *buf0,
                      const int16_t *ubuf[2], const int16_t *vbuf[2],
                      const int16_t *abuf0, uint8_t *dest, int dstW,
                      int uvalpha, int y, enum PixelFormat target)
{
    const uint8_t * const d128 = dither_8x8_220[y & 7];
    uint8_t *g = c->table_gU[128] + c->table_gV[128];
    int i;

    for (i = 0; i < dstW - 7; i += 8) {
        int acc =    g[(buf0[i    ] >> 7) + d128[0]];
        acc += acc + g[(buf0[i + 1] >> 7) + d128[1]];
        acc += acc + g[(buf0[i + 2] >> 7) + d128[2]];
        acc += acc + g[(buf0[i + 3] >> 7) + d128[3]];
        acc += acc + g[(buf0[i + 4] >> 7) + d128[4]];
        acc += acc + g[(buf0[i + 5] >> 7) + d128[5]];
        acc += acc + g[(buf0[i + 6] >> 7) + d128[6]];
        acc += acc + g[(buf0[i + 7] >> 7) + d128[7]];
        output_pixel(*dest++, acc);
    }
}

#undef output_pixel

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

YUV2PACKEDWRAPPER(yuv2mono,, white, PIX_FMT_MONOWHITE);
YUV2PACKEDWRAPPER(yuv2mono,, black, PIX_FMT_MONOBLACK);

#define output_pixels(pos, Y1, U, Y2, V) \
    if (target == PIX_FMT_YUYV422) { \
        dest[pos + 0] = Y1; \
        dest[pos + 1] = U;  \
        dest[pos + 2] = Y2; \
        dest[pos + 3] = V;  \
    } else { \
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
                     int y, enum PixelFormat target)
{
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
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
                     enum PixelFormat target)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int  yalpha1 = 4095 - yalpha;
    int uvalpha1 = 4095 - uvalpha;
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;

        output_pixels(i * 4, Y1, U, Y2, V);
    }
}

static av_always_inline void
yuv2422_1_c_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum PixelFormat target)
{
    const int16_t *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < (dstW >> 1); i++) {
            int Y1 = buf0[i * 2]     >> 7;
            int Y2 = buf0[i * 2 + 1] >> 7;
            int U  = ubuf1[i]        >> 7;
            int V  = vbuf1[i]        >> 7;

            output_pixels(i * 4, Y1, U, Y2, V);
        }
    } else {
        for (i = 0; i < (dstW >> 1); i++) {
            int Y1 =  buf0[i * 2]          >> 7;
            int Y2 =  buf0[i * 2 + 1]      >> 7;
            int U  = (ubuf0[i] + ubuf1[i]) >> 8;
            int V  = (vbuf0[i] + vbuf1[i]) >> 8;

            output_pixels(i * 4, Y1, U, Y2, V);
        }
    }
}

#undef output_pixels

YUV2PACKEDWRAPPER(yuv2, 422, yuyv422, PIX_FMT_YUYV422);
YUV2PACKEDWRAPPER(yuv2, 422, uyvy422, PIX_FMT_UYVY422);

#define R_B ((target == PIX_FMT_RGB48LE || target == PIX_FMT_RGB48BE) ? R : B)
#define B_R ((target == PIX_FMT_RGB48LE || target == PIX_FMT_RGB48BE) ? B : R)
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
                       int y, enum PixelFormat target)
{
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int j;
        int Y1 = 0;
        int Y2 = 0;
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

        // 8bit: 12+15=27; 16-bit: 12+19=31
        Y1 >>= 14; // 10
        Y2 >>= 14;
        U  >>= 14;
        V  >>= 14;

        // 8bit: 27 -> 17bit, 16bit: 31 - 14 = 17bit
        Y1 -= c->yuv2rgb_y_offset;
        Y2 -= c->yuv2rgb_y_offset;
        Y1 *= c->yuv2rgb_y_coeff;
        Y2 *= c->yuv2rgb_y_coeff;
        Y1 += 1 << 13; // 21
        Y2 += 1 << 13;
        // 8bit: 17 + 13bit = 30bit, 16bit: 17 + 13bit = 30bit

        R = V * c->yuv2rgb_v2r_coeff;
        G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
        B =                            U * c->yuv2rgb_u2b_coeff;

        // 8bit: 30 - 22 = 8bit, 16bit: 30bit - 14 = 16bit
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
                       enum PixelFormat target)
{
    const int32_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int  yalpha1 = 4095 - yalpha;
    int uvalpha1 = 4095 - uvalpha;
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
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
                       int uvalpha, int y, enum PixelFormat target)
{
    const int32_t *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < (dstW >> 1); i++) {
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
        for (i = 0; i < (dstW >> 1); i++) {
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

YUV2PACKED16WRAPPER(yuv2, rgb48, rgb48be, PIX_FMT_RGB48BE);
YUV2PACKED16WRAPPER(yuv2, rgb48, rgb48le, PIX_FMT_RGB48LE);
YUV2PACKED16WRAPPER(yuv2, rgb48, bgr48be, PIX_FMT_BGR48BE);
YUV2PACKED16WRAPPER(yuv2, rgb48, bgr48le, PIX_FMT_BGR48LE);

static av_always_inline void
yuv2rgb_write(uint8_t *_dest, int i, int Y1, int Y2,
              int U, int V, int A1, int A2,
              const void *_r, const void *_g, const void *_b, int y,
              enum PixelFormat target, int hasAlpha)
{
    if (target == PIX_FMT_ARGB || target == PIX_FMT_RGBA ||
        target == PIX_FMT_ABGR || target == PIX_FMT_BGRA) {
        uint32_t *dest = (uint32_t *) _dest;
        const uint32_t *r = (const uint32_t *) _r;
        const uint32_t *g = (const uint32_t *) _g;
        const uint32_t *b = (const uint32_t *) _b;

#if CONFIG_SMALL
        int sh = hasAlpha ? ((target == PIX_FMT_RGB32_1 || target == PIX_FMT_BGR32_1) ? 0 : 24) : 0;

        dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1] + (hasAlpha ? A1 << sh : 0);
        dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2] + (hasAlpha ? A2 << sh : 0);
#else
        if (hasAlpha) {
            int sh = (target == PIX_FMT_RGB32_1 || target == PIX_FMT_BGR32_1) ? 0 : 24;

            dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1] + (A1 << sh);
            dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2] + (A2 << sh);
        } else {
            dest[i * 2 + 0] = r[Y1] + g[Y1] + b[Y1];
            dest[i * 2 + 1] = r[Y2] + g[Y2] + b[Y2];
        }
#endif
    } else if (target == PIX_FMT_RGB24 || target == PIX_FMT_BGR24) {
        uint8_t *dest = (uint8_t *) _dest;
        const uint8_t *r = (const uint8_t *) _r;
        const uint8_t *g = (const uint8_t *) _g;
        const uint8_t *b = (const uint8_t *) _b;

#define r_b ((target == PIX_FMT_RGB24) ? r : b)
#define b_r ((target == PIX_FMT_RGB24) ? b : r)

        dest[i * 6 + 0] = r_b[Y1];
        dest[i * 6 + 1] =   g[Y1];
        dest[i * 6 + 2] = b_r[Y1];
        dest[i * 6 + 3] = r_b[Y2];
        dest[i * 6 + 4] =   g[Y2];
        dest[i * 6 + 5] = b_r[Y2];
#undef r_b
#undef b_r
    } else if (target == PIX_FMT_RGB565 || target == PIX_FMT_BGR565 ||
               target == PIX_FMT_RGB555 || target == PIX_FMT_BGR555 ||
               target == PIX_FMT_RGB444 || target == PIX_FMT_BGR444) {
        uint16_t *dest = (uint16_t *) _dest;
        const uint16_t *r = (const uint16_t *) _r;
        const uint16_t *g = (const uint16_t *) _g;
        const uint16_t *b = (const uint16_t *) _b;
        int dr1, dg1, db1, dr2, dg2, db2;

        if (target == PIX_FMT_RGB565 || target == PIX_FMT_BGR565) {
            dr1 = dither_2x2_8[ y & 1     ][0];
            dg1 = dither_2x2_4[ y & 1     ][0];
            db1 = dither_2x2_8[(y & 1) ^ 1][0];
            dr2 = dither_2x2_8[ y & 1     ][1];
            dg2 = dither_2x2_4[ y & 1     ][1];
            db2 = dither_2x2_8[(y & 1) ^ 1][1];
        } else if (target == PIX_FMT_RGB555 || target == PIX_FMT_BGR555) {
            dr1 = dither_2x2_8[ y & 1     ][0];
            dg1 = dither_2x2_8[ y & 1     ][1];
            db1 = dither_2x2_8[(y & 1) ^ 1][0];
            dr2 = dither_2x2_8[ y & 1     ][1];
            dg2 = dither_2x2_8[ y & 1     ][0];
            db2 = dither_2x2_8[(y & 1) ^ 1][1];
        } else {
            dr1 = dither_4x4_16[ y & 3     ][0];
            dg1 = dither_4x4_16[ y & 3     ][1];
            db1 = dither_4x4_16[(y & 3) ^ 3][0];
            dr2 = dither_4x4_16[ y & 3     ][1];
            dg2 = dither_4x4_16[ y & 3     ][0];
            db2 = dither_4x4_16[(y & 3) ^ 3][1];
        }

        dest[i * 2 + 0] = r[Y1 + dr1] + g[Y1 + dg1] + b[Y1 + db1];
        dest[i * 2 + 1] = r[Y2 + dr2] + g[Y2 + dg2] + b[Y2 + db2];
    } else /* 8/4-bit */ {
        uint8_t *dest = (uint8_t *) _dest;
        const uint8_t *r = (const uint8_t *) _r;
        const uint8_t *g = (const uint8_t *) _g;
        const uint8_t *b = (const uint8_t *) _b;
        int dr1, dg1, db1, dr2, dg2, db2;

        if (target == PIX_FMT_RGB8 || target == PIX_FMT_BGR8) {
            const uint8_t * const d64 = dither_8x8_73[y & 7];
            const uint8_t * const d32 = dither_8x8_32[y & 7];
            dr1 = dg1 = d32[(i * 2 + 0) & 7];
            db1 =       d64[(i * 2 + 0) & 7];
            dr2 = dg2 = d32[(i * 2 + 1) & 7];
            db2 =       d64[(i * 2 + 1) & 7];
        } else {
            const uint8_t * const d64  = dither_8x8_73 [y & 7];
            const uint8_t * const d128 = dither_8x8_220[y & 7];
            dr1 = db1 = d128[(i * 2 + 0) & 7];
            dg1 =        d64[(i * 2 + 0) & 7];
            dr2 = db2 = d128[(i * 2 + 1) & 7];
            dg2 =        d64[(i * 2 + 1) & 7];
        }

        if (target == PIX_FMT_RGB4 || target == PIX_FMT_BGR4) {
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
                     int y, enum PixelFormat target, int hasAlpha)
{
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int j;
        int Y1 = 1 << 18;
        int Y2 = 1 << 18;
        int U  = 1 << 18;
        int V  = 1 << 18;
        int av_unused A1, A2;
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

        yuv2rgb_write(dest, i, Y1, Y2, U, V, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                      r, g, b, y, target, hasAlpha);
    }
}

static av_always_inline void
yuv2rgb_2_c_template(SwsContext *c, const int16_t *buf[2],
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf[2], uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum PixelFormat target, int hasAlpha)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    int  yalpha1 = 4095 - yalpha;
    int uvalpha1 = 4095 - uvalpha;
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;
        int A1, A2;
        const void *r =  c->table_rV[V],
                   *g = (c->table_gU[U] + c->table_gV[V]),
                   *b =  c->table_bU[U];

        if (hasAlpha) {
            A1 = (abuf0[i * 2    ] * yalpha1 + abuf1[i * 2    ] * yalpha) >> 19;
            A2 = (abuf0[i * 2 + 1] * yalpha1 + abuf1[i * 2 + 1] * yalpha) >> 19;
        }

        yuv2rgb_write(dest, i, Y1, Y2, U, V, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                      r, g, b, y, target, hasAlpha);
    }
}

static av_always_inline void
yuv2rgb_1_c_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum PixelFormat target,
                     int hasAlpha)
{
    const int16_t *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < (dstW >> 1); i++) {
            int Y1 = buf0[i * 2]     >> 7;
            int Y2 = buf0[i * 2 + 1] >> 7;
            int U  = ubuf1[i]        >> 7;
            int V  = vbuf1[i]        >> 7;
            int A1, A2;
            const void *r =  c->table_rV[V],
                       *g = (c->table_gU[U] + c->table_gV[V]),
                       *b =  c->table_bU[U];

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] >> 7;
                A2 = abuf0[i * 2 + 1] >> 7;
            }

            yuv2rgb_write(dest, i, Y1, Y2, U, V, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
                          r, g, b, y, target, hasAlpha);
        }
    } else {
        for (i = 0; i < (dstW >> 1); i++) {
            int Y1 =  buf0[i * 2]          >> 7;
            int Y2 =  buf0[i * 2 + 1]      >> 7;
            int U  = (ubuf0[i] + ubuf1[i]) >> 8;
            int V  = (vbuf0[i] + vbuf1[i]) >> 8;
            int A1, A2;
            const void *r =  c->table_rV[V],
                       *g = (c->table_gU[U] + c->table_gV[V]),
                       *b =  c->table_bU[U];

            if (hasAlpha) {
                A1 = abuf0[i * 2    ] >> 7;
                A2 = abuf0[i * 2 + 1] >> 7;
            }

            yuv2rgb_write(dest, i, Y1, Y2, U, V, hasAlpha ? A1 : 0, hasAlpha ? A2 : 0,
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
YUV2RGBWRAPPER(yuv2rgb,,  32_1,  PIX_FMT_RGB32_1,   CONFIG_SWSCALE_ALPHA && c->alpPixBuf);
YUV2RGBWRAPPER(yuv2rgb,,  32,    PIX_FMT_RGB32,     CONFIG_SWSCALE_ALPHA && c->alpPixBuf);
#else
#if CONFIG_SWSCALE_ALPHA
YUV2RGBWRAPPER(yuv2rgb,, a32_1,  PIX_FMT_RGB32_1,   1);
YUV2RGBWRAPPER(yuv2rgb,, a32,    PIX_FMT_RGB32,     1);
#endif
YUV2RGBWRAPPER(yuv2rgb,, x32_1,  PIX_FMT_RGB32_1,   0);
YUV2RGBWRAPPER(yuv2rgb,, x32,    PIX_FMT_RGB32,     0);
#endif
YUV2RGBWRAPPER(yuv2, rgb, rgb24, PIX_FMT_RGB24,   0);
YUV2RGBWRAPPER(yuv2, rgb, bgr24, PIX_FMT_BGR24,   0);
YUV2RGBWRAPPER(yuv2rgb,,  16,    PIX_FMT_RGB565,    0);
YUV2RGBWRAPPER(yuv2rgb,,  15,    PIX_FMT_RGB555,    0);
YUV2RGBWRAPPER(yuv2rgb,,  12,    PIX_FMT_RGB444,    0);
YUV2RGBWRAPPER(yuv2rgb,,   8,    PIX_FMT_RGB8,      0);
YUV2RGBWRAPPER(yuv2rgb,,   4,    PIX_FMT_RGB4,      0);
YUV2RGBWRAPPER(yuv2rgb,,   4b,   PIX_FMT_RGB4_BYTE, 0);

static av_always_inline void
yuv2rgb_full_X_c_template(SwsContext *c, const int16_t *lumFilter,
                          const int16_t **lumSrc, int lumFilterSize,
                          const int16_t *chrFilter, const int16_t **chrUSrc,
                          const int16_t **chrVSrc, int chrFilterSize,
                          const int16_t **alpSrc, uint8_t *dest,
                          int dstW, int y, enum PixelFormat target, int hasAlpha)
{
    int i;
    int step = (target == PIX_FMT_RGB24 || target == PIX_FMT_BGR24) ? 3 : 4;

    for (i = 0; i < dstW; i++) {
        int j;
        int Y = 1<<9;
        int U = (1<<9)-(128 << 19);
        int V = (1<<9)-(128 << 19);
        int av_unused A;
        int R, G, B;

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
        case PIX_FMT_ARGB:
            dest[0] = hasAlpha ? A : 255;
            dest[1] = R >> 22;
            dest[2] = G >> 22;
            dest[3] = B >> 22;
            break;
        case PIX_FMT_RGB24:
            dest[0] = R >> 22;
            dest[1] = G >> 22;
            dest[2] = B >> 22;
            break;
        case PIX_FMT_RGBA:
            dest[0] = R >> 22;
            dest[1] = G >> 22;
            dest[2] = B >> 22;
            dest[3] = hasAlpha ? A : 255;
            break;
        case PIX_FMT_ABGR:
            dest[0] = hasAlpha ? A : 255;
            dest[1] = B >> 22;
            dest[2] = G >> 22;
            dest[3] = R >> 22;
            break;
        case PIX_FMT_BGR24:
            dest[0] = B >> 22;
            dest[1] = G >> 22;
            dest[2] = R >> 22;
            break;
        case PIX_FMT_BGRA:
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
YUV2RGBWRAPPERX(yuv2, rgb_full, bgra32_full, PIX_FMT_BGRA,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf);
YUV2RGBWRAPPERX(yuv2, rgb_full, abgr32_full, PIX_FMT_ABGR,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf);
YUV2RGBWRAPPERX(yuv2, rgb_full, rgba32_full, PIX_FMT_RGBA,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf);
YUV2RGBWRAPPERX(yuv2, rgb_full, argb32_full, PIX_FMT_ARGB,  CONFIG_SWSCALE_ALPHA && c->alpPixBuf);
#else
#if CONFIG_SWSCALE_ALPHA
YUV2RGBWRAPPERX(yuv2, rgb_full, bgra32_full, PIX_FMT_BGRA,  1);
YUV2RGBWRAPPERX(yuv2, rgb_full, abgr32_full, PIX_FMT_ABGR,  1);
YUV2RGBWRAPPERX(yuv2, rgb_full, rgba32_full, PIX_FMT_RGBA,  1);
YUV2RGBWRAPPERX(yuv2, rgb_full, argb32_full, PIX_FMT_ARGB,  1);
#endif
YUV2RGBWRAPPERX(yuv2, rgb_full, bgrx32_full, PIX_FMT_BGRA,  0);
YUV2RGBWRAPPERX(yuv2, rgb_full, xbgr32_full, PIX_FMT_ABGR,  0);
YUV2RGBWRAPPERX(yuv2, rgb_full, rgbx32_full, PIX_FMT_RGBA,  0);
YUV2RGBWRAPPERX(yuv2, rgb_full, xrgb32_full, PIX_FMT_ARGB,  0);
#endif
YUV2RGBWRAPPERX(yuv2, rgb_full, bgr24_full,  PIX_FMT_BGR24, 0);
YUV2RGBWRAPPERX(yuv2, rgb_full, rgb24_full,  PIX_FMT_RGB24, 0);

static av_always_inline void fillPlane(uint8_t* plane, int stride,
                                       int width, int height,
                                       int y, uint8_t val)
{
    int i;
    uint8_t *ptr = plane + stride*y;
    for (i=0; i<height; i++) {
        memset(ptr, val, width);
        ptr += stride;
    }
}

#define input_pixel(pos) (isBE(origin) ? AV_RB16(pos) : AV_RL16(pos))

#define r ((origin == PIX_FMT_BGR48BE || origin == PIX_FMT_BGR48LE) ? b_r : r_b)
#define b ((origin == PIX_FMT_BGR48BE || origin == PIX_FMT_BGR48LE) ? r_b : b_r)

static av_always_inline void
rgb48ToY_c_template(uint16_t *dst, const uint16_t *src, int width,
                    enum PixelFormat origin)
{
    int i;
    for (i = 0; i < width; i++) {
        unsigned int r_b = input_pixel(&src[i*3+0]);
        unsigned int   g = input_pixel(&src[i*3+1]);
        unsigned int b_r = input_pixel(&src[i*3+2]);

        dst[i] = (RY*r + GY*g + BY*b + (0x2001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb48ToUV_c_template(uint16_t *dstU, uint16_t *dstV,
                    const uint16_t *src1, const uint16_t *src2,
                    int width, enum PixelFormat origin)
{
    int i;
    assert(src1==src2);
    for (i = 0; i < width; i++) {
        int r_b = input_pixel(&src1[i*3+0]);
        int   g = input_pixel(&src1[i*3+1]);
        int b_r = input_pixel(&src1[i*3+2]);

        dstU[i] = (RU*r + GU*g + BU*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV*r + GV*g + BV*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb48ToUV_half_c_template(uint16_t *dstU, uint16_t *dstV,
                          const uint16_t *src1, const uint16_t *src2,
                          int width, enum PixelFormat origin)
{
    int i;
    assert(src1==src2);
    for (i = 0; i < width; i++) {
        int r_b = (input_pixel(&src1[6 * i + 0]) + input_pixel(&src1[6 * i + 3]) + 1) >> 1;
        int   g = (input_pixel(&src1[6 * i + 1]) + input_pixel(&src1[6 * i + 4]) + 1) >> 1;
        int b_r = (input_pixel(&src1[6 * i + 2]) + input_pixel(&src1[6 * i + 5]) + 1) >> 1;

        dstU[i]= (RU*r + GU*g + BU*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i]= (RV*r + GV*g + BV*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

#undef r
#undef b
#undef input_pixel

#define rgb48funcs(pattern, BE_LE, origin) \
static void pattern ## 48 ## BE_LE ## ToY_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused0, const uint8_t *unused1,\
                                    int width, uint32_t *unused) \
{ \
    const uint16_t *src = (const uint16_t *) _src; \
    uint16_t *dst = (uint16_t *) _dst; \
    rgb48ToY_c_template(dst, src, width, origin); \
} \
 \
static void pattern ## 48 ## BE_LE ## ToUV_c(uint8_t *_dstU, uint8_t *_dstV, \
                                    const uint8_t *unused0, const uint8_t *_src1, const uint8_t *_src2, \
                                    int width, uint32_t *unused) \
{ \
    const uint16_t *src1 = (const uint16_t *) _src1, \
                   *src2 = (const uint16_t *) _src2; \
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV; \
    rgb48ToUV_c_template(dstU, dstV, src1, src2, width, origin); \
} \
 \
static void pattern ## 48 ## BE_LE ## ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, \
                                    const uint8_t *unused0, const uint8_t *_src1, const uint8_t *_src2, \
                                    int width, uint32_t *unused) \
{ \
    const uint16_t *src1 = (const uint16_t *) _src1, \
                   *src2 = (const uint16_t *) _src2; \
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV; \
    rgb48ToUV_half_c_template(dstU, dstV, src1, src2, width, origin); \
}

rgb48funcs(rgb, LE, PIX_FMT_RGB48LE);
rgb48funcs(rgb, BE, PIX_FMT_RGB48BE);
rgb48funcs(bgr, LE, PIX_FMT_BGR48LE);
rgb48funcs(bgr, BE, PIX_FMT_BGR48BE);

#define input_pixel(i) ((origin == PIX_FMT_RGBA || origin == PIX_FMT_BGRA || \
                         origin == PIX_FMT_ARGB || origin == PIX_FMT_ABGR) ? AV_RN32A(&src[(i)*4]) : \
                        (isBE(origin) ? AV_RB16(&src[(i)*2]) : AV_RL16(&src[(i)*2])))

static av_always_inline void
rgb16_32ToY_c_template(int16_t *dst, const uint8_t *src,
                       int width, enum PixelFormat origin,
                       int shr,   int shg,   int shb, int shp,
                       int maskr, int maskg, int maskb,
                       int rsh,   int gsh,   int bsh, int S)
{
    const int ry = RY << rsh, gy = GY << gsh, by = BY << bsh,
              rnd = (32<<((S)-1)) + (1<<(S-7));
    int i;

    for (i = 0; i < width; i++) {
        int px = input_pixel(i) >> shp;
        int b = (px & maskb) >> shb;
        int g = (px & maskg) >> shg;
        int r = (px & maskr) >> shr;

        dst[i] = (ry * r + gy * g + by * b + rnd) >> ((S)-6);
    }
}

static av_always_inline void
rgb16_32ToUV_c_template(int16_t *dstU, int16_t *dstV,
                        const uint8_t *src, int width,
                        enum PixelFormat origin,
                        int shr,   int shg,   int shb, int shp,
                        int maskr, int maskg, int maskb,
                        int rsh,   int gsh,   int bsh, int S)
{
    const int ru = RU << rsh, gu = GU << gsh, bu = BU << bsh,
              rv = RV << rsh, gv = GV << gsh, bv = BV << bsh,
              rnd = (256<<((S)-1)) + (1<<(S-7));
    int i;

    for (i = 0; i < width; i++) {
        int px = input_pixel(i) >> shp;
        int b = (px & maskb) >> shb;
        int g = (px & maskg) >> shg;
        int r = (px & maskr) >> shr;

        dstU[i] = (ru * r + gu * g + bu * b + rnd) >> ((S)-6);
        dstV[i] = (rv * r + gv * g + bv * b + rnd) >> ((S)-6);
    }
}

static av_always_inline void
rgb16_32ToUV_half_c_template(int16_t *dstU, int16_t *dstV,
                             const uint8_t *src, int width,
                             enum PixelFormat origin,
                             int shr,   int shg,   int shb, int shp,
                             int maskr, int maskg, int maskb,
                             int rsh,   int gsh,   int bsh, int S)
{
    const int ru = RU << rsh, gu = GU << gsh, bu = BU << bsh,
              rv = RV << rsh, gv = GV << gsh, bv = BV << bsh,
              rnd = (256U<<(S)) + (1<<(S-6)), maskgx = ~(maskr | maskb);
    int i;

    maskr |= maskr << 1; maskb |= maskb << 1; maskg |= maskg << 1;
    for (i = 0; i < width; i++) {
        int px0 = input_pixel(2 * i + 0) >> shp;
        int px1 = input_pixel(2 * i + 1) >> shp;
        int b, r, g = (px0 & maskgx) + (px1 & maskgx);
        int rb = px0 + px1 - g;

        b = (rb & maskb) >> shb;
        if (shp || origin == PIX_FMT_BGR565LE || origin == PIX_FMT_BGR565BE ||
            origin == PIX_FMT_RGB565LE || origin == PIX_FMT_RGB565BE) {
            g >>= shg;
        } else {
            g = (g  & maskg) >> shg;
        }
        r = (rb & maskr) >> shr;

        dstU[i] = (ru * r + gu * g + bu * b + (unsigned)rnd) >> ((S)-6+1);
        dstV[i] = (rv * r + gv * g + bv * b + (unsigned)rnd) >> ((S)-6+1);
    }
}

#undef input_pixel

#define rgb16_32_wrapper(fmt, name, shr, shg, shb, shp, maskr, \
                         maskg, maskb, rsh, gsh, bsh, S) \
static void name ## ToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, \
                          int width, uint32_t *unused) \
{ \
    rgb16_32ToY_c_template((int16_t*)dst, src, width, fmt, \
                           shr, shg, shb, shp, \
                           maskr, maskg, maskb, rsh, gsh, bsh, S); \
} \
 \
static void name ## ToUV_c(uint8_t *dstU, uint8_t *dstV, \
                           const uint8_t *unused0, const uint8_t *src, const uint8_t *dummy, \
                           int width, uint32_t *unused) \
{ \
    rgb16_32ToUV_c_template((int16_t*)dstU, (int16_t*)dstV, src, width, fmt,  \
                            shr, shg, shb, shp, \
                            maskr, maskg, maskb, rsh, gsh, bsh, S); \
} \
 \
static void name ## ToUV_half_c(uint8_t *dstU, uint8_t *dstV, \
                                const uint8_t *unused0, const uint8_t *src, const uint8_t *dummy, \
                                int width, uint32_t *unused) \
{ \
    rgb16_32ToUV_half_c_template((int16_t*)dstU, (int16_t*)dstV, src, width, fmt, \
                                 shr, shg, shb, shp, \
                                 maskr, maskg, maskb, rsh, gsh, bsh, S); \
}

rgb16_32_wrapper(PIX_FMT_BGR32,    bgr32,  16, 0,  0, 0, 0xFF0000, 0xFF00,   0x00FF,  8, 0,  8, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_BGR32_1,  bgr321, 16, 0,  0, 8, 0xFF0000, 0xFF00,   0x00FF,  8, 0,  8, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_RGB32,    rgb32,   0, 0, 16, 0,   0x00FF, 0xFF00, 0xFF0000,  8, 0,  8, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_RGB32_1,  rgb321,  0, 0, 16, 8,   0x00FF, 0xFF00, 0xFF0000,  8, 0,  8, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_BGR565LE, bgr16le, 0, 0,  0, 0,   0x001F, 0x07E0,   0xF800, 11, 5,  0, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_BGR555LE, bgr15le, 0, 0,  0, 0,   0x001F, 0x03E0,   0x7C00, 10, 5,  0, RGB2YUV_SHIFT+7);
rgb16_32_wrapper(PIX_FMT_RGB565LE, rgb16le, 0, 0,  0, 0,   0xF800, 0x07E0,   0x001F,  0, 5, 11, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_RGB555LE, rgb15le, 0, 0,  0, 0,   0x7C00, 0x03E0,   0x001F,  0, 5, 10, RGB2YUV_SHIFT+7);
rgb16_32_wrapper(PIX_FMT_BGR565BE, bgr16be, 0, 0,  0, 0,   0x001F, 0x07E0,   0xF800, 11, 5,  0, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_BGR555BE, bgr15be, 0, 0,  0, 0,   0x001F, 0x03E0,   0x7C00, 10, 5,  0, RGB2YUV_SHIFT+7);
rgb16_32_wrapper(PIX_FMT_RGB565BE, rgb16be, 0, 0,  0, 0,   0xF800, 0x07E0,   0x001F,  0, 5, 11, RGB2YUV_SHIFT+8);
rgb16_32_wrapper(PIX_FMT_RGB555BE, rgb15be, 0, 0,  0, 0,   0x7C00, 0x03E0,   0x001F,  0, 5, 10, RGB2YUV_SHIFT+7);

static void gbr24pToY_c(uint16_t *dst, const uint8_t *gsrc, const uint8_t *bsrc, const uint8_t *rsrc,
                        int width, uint32_t *unused)
{
    int i;
    for (i = 0; i < width; i++) {
        unsigned int g   = gsrc[i];
        unsigned int b   = bsrc[i];
        unsigned int r   = rsrc[i];

        dst[i] = (RY*r + GY*g + BY*b + (0x801<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

static void gbr24pToUV_c(uint16_t *dstU, uint16_t *dstV,
                         const uint8_t *gsrc, const uint8_t *bsrc, const uint8_t *rsrc,
                         int width, enum PixelFormat origin)
{
    int i;
    for (i = 0; i < width; i++) {
        unsigned int g   = gsrc[i];
        unsigned int b   = bsrc[i];
        unsigned int r   = rsrc[i];

        dstU[i] = (RU*r + GU*g + BU*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
        dstV[i] = (RV*r + GV*g + BV*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

static void gbr24pToUV_half_c(uint16_t *dstU, uint16_t *dstV,
                         const uint8_t *gsrc, const uint8_t *bsrc, const uint8_t *rsrc,
                         int width, enum PixelFormat origin)
{
    int i;
    for (i = 0; i < width; i++) {
        unsigned int g   = gsrc[2*i] + gsrc[2*i+1];
        unsigned int b   = bsrc[2*i] + bsrc[2*i+1];
        unsigned int r   = rsrc[2*i] + rsrc[2*i+1];

        dstU[i] = (RU*r + GU*g + BU*b + (0x4001<<(RGB2YUV_SHIFT-6))) >> (RGB2YUV_SHIFT-6+1);
        dstV[i] = (RV*r + GV*g + BV*b + (0x4001<<(RGB2YUV_SHIFT-6))) >> (RGB2YUV_SHIFT-6+1);
    }
}

static void abgrToA_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i]<<6;
    }
}

static void rgbaToA_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i+3]<<6;
    }
}

static void palToA_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *pal)
{
    int i;
    for (i=0; i<width; i++) {
        int d= src[i];

        dst[i]= (pal[d] >> 24)<<6;
    }
}

static void palToY_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, long width, uint32_t *pal)
{
    int i;
    for (i=0; i<width; i++) {
        int d= src[i];

        dst[i]= (pal[d] & 0xFF)<<6;
    }
}

static void palToUV_c(uint16_t *dstU, int16_t *dstV,
                           const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                           int width, uint32_t *pal)
{
    int i;
    assert(src1 == src2);
    for (i=0; i<width; i++) {
        int p= pal[src1[i]];

        dstU[i]= (uint8_t)(p>> 8)<<6;
        dstV[i]= (uint8_t)(p>>16)<<6;
    }
}

static void monowhite2Y_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++) {
        int d= ~src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*16383;
    }
    if(width&7){
        int d= ~src[i];
        for(j=0; j<(width&7); j++)
            dst[8*i+j]= ((d>>(7-j))&1)*16383;
    }
}

static void monoblack2Y_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++) {
        int d= src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*16383;
    }
    if(width&7){
        int d= src[i];
        for(j=0; j<(width&7); j++)
            dst[8*i+j]= ((d>>(7-j))&1)*16383;
    }
}

//FIXME yuy2* can read up to 7 samples too much

static void yuy2ToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                      uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i];
}

static void yuy2ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[4*i + 1];
        dstV[i]= src1[4*i + 3];
    }
    assert(src1 == src2);
}

static void bswap16Y_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1, const uint8_t *unused2,  int width, uint32_t *unused)
{
    int i;
    const uint16_t *src = (const uint16_t *) _src;
    uint16_t *dst = (uint16_t *) _dst;
    for (i=0; i<width; i++) {
        dst[i] = av_bswap16(src[i]);
    }
}

static void bswap16UV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *_src1,
                        const uint8_t *_src2, int width, uint32_t *unused)
{
    int i;
    const uint16_t *src1 = (const uint16_t *) _src1,
                   *src2 = (const uint16_t *) _src2;
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV;
    for (i=0; i<width; i++) {
        dstU[i] = av_bswap16(src1[i]);
        dstV[i] = av_bswap16(src2[i]);
    }
}

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src+1, ...) would have 100% unaligned accesses. */
static void uyvyToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                      uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i+1];
}

static void uyvyToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[4*i + 0];
        dstV[i]= src1[4*i + 2];
    }
    assert(src1 == src2);
}

static av_always_inline void nvXXtoUV_c(uint8_t *dst1, uint8_t *dst2,
                                        const uint8_t *src, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dst1[i] = src[2*i+0];
        dst2[i] = src[2*i+1];
    }
}

static void nv12ToUV_c(uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused)
{
    nvXXtoUV_c(dstU, dstV, src1, width);
}

static void nv21ToUV_c(uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused)
{
    nvXXtoUV_c(dstV, dstU, src1, width);
}

#define input_pixel(pos) (isBE(origin) ? AV_RB16(pos) : AV_RL16(pos))

static void bgr24ToY_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,
                       int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src[i*3+0];
        int g= src[i*3+1];
        int r= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static void bgr24ToUV_c(int16_t *dstU, int16_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src1[3*i + 0];
        int g= src1[3*i + 1];
        int r= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
    assert(src1 == src2);
}

static void bgr24ToUV_half_c(int16_t *dstU, int16_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int r= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
    assert(src1 == src2);
}

static void rgb24ToY_c(int16_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                       uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int r= src[i*3+0];
        int g= src[i*3+1];
        int b= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static void rgb24ToUV_c(int16_t *dstU, int16_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++) {
        int r= src1[3*i + 0];
        int g= src1[3*i + 1];
        int b= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
}

static void rgb24ToUV_half_c(int16_t *dstU, int16_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                                    const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++) {
        int r= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int b= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
}

static void planar_rgb_to_y(uint16_t *dst, const uint8_t *src[4], int width)
{
    int i;
    for (i = 0; i < width; i++) {
        int g = src[0][i];
        int b = src[1][i];
        int r = src[2][i];

        dst[i] = (RY*r + GY*g + BY*b + (0x801<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

static void planar_rgb16le_to_y(uint8_t *_dst, const uint8_t *_src[4], int width)
{
    int i;
    const uint16_t **src = (const uint16_t **) _src;
    uint16_t *dst = (uint16_t *) _dst;
    for (i = 0; i < width; i++) {
        int g = AV_RL16(src[0] + i);
        int b = AV_RL16(src[1] + i);
        int r = AV_RL16(src[2] + i);

        dst[i] = ((RY * r + GY * g + BY * b + (33 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
    }
}

static void planar_rgb16be_to_y(uint8_t *_dst, const uint8_t *_src[4], int width)
{
    int i;
    const uint16_t **src = (const uint16_t **) _src;
    uint16_t *dst = (uint16_t *) _dst;
    for (i = 0; i < width; i++) {
        int g = AV_RB16(src[0] + i);
        int b = AV_RB16(src[1] + i);
        int r = AV_RB16(src[2] + i);

        dst[i] = ((RY * r + GY * g + BY * b + (33 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
    }
}

static void planar_rgb_to_uv(uint16_t *dstU, uint16_t *dstV, const uint8_t *src[4], int width)
{
    int i;
    for (i = 0; i < width; i++) {
        int g = src[0][i];
        int b = src[1][i];
        int r = src[2][i];

        dstU[i] = (RU*r + GU*g + BU*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
        dstV[i] = (RV*r + GV*g + BV*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

static void planar_rgb16le_to_uv(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *_src[4], int width)
{
    int i;
    const uint16_t **src = (const uint16_t **) _src;
    uint16_t *dstU = (uint16_t *) _dstU;
    uint16_t *dstV = (uint16_t *) _dstV;
    for (i = 0; i < width; i++) {
        int g = AV_RL16(src[0] + i);
        int b = AV_RL16(src[1] + i);
        int r = AV_RL16(src[2] + i);

        dstU[i] = (RU * r + GU * g + BU * b + (257 << RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT + 1);
        dstV[i] = (RV * r + GV * g + BV * b + (257 << RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT + 1);
    }
}

static void planar_rgb16be_to_uv(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *_src[4], int width)
{
    int i;
    const uint16_t **src = (const uint16_t **) _src;
    uint16_t *dstU = (uint16_t *) _dstU;
    uint16_t *dstV = (uint16_t *) _dstV;
    for (i = 0; i < width; i++) {
        int g = AV_RB16(src[0] + i);
        int b = AV_RB16(src[1] + i);
        int r = AV_RB16(src[2] + i);

        dstU[i] = (RU * r + GU * g + BU * b + (257 << RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT + 1);
        dstV[i] = (RV * r + GV * g + BV * b + (257 << RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT + 1);
    }
}

static void hScale16To19_c(SwsContext *c, int16_t *_dst, int dstW, const uint8_t *_src,
                           const int16_t *filter,
                           const int16_t *filterPos, int filterSize)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    const uint16_t *src = (const uint16_t *) _src;
    int bits = av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;
    int sh = bits - 4;

    if((isAnyRGB(c->srcFormat) || c->srcFormat==PIX_FMT_PAL8) && av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1<15)
        sh= 9;

    for (i = 0; i < dstW; i++) {
        int j;
        int srcPos = filterPos[i];
        int val = 0;

        for (j = 0; j < filterSize; j++) {
            val += src[srcPos + j] * filter[filterSize * i + j];
        }
        // filter=14 bit, input=16 bit, output=30 bit, >> 11 makes 19 bit
        dst[i] = FFMIN(val >> sh, (1 << 19) - 1);
    }
}

static void hScale16To15_c(SwsContext *c, int16_t *dst, int dstW, const uint8_t *_src,
                           const int16_t *filter,
                           const int16_t *filterPos, int filterSize)
{
    int i;
    const uint16_t *src = (const uint16_t *) _src;
    int sh = av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;

    if(sh<15)
        sh= isAnyRGB(c->srcFormat) || c->srcFormat==PIX_FMT_PAL8 ? 13 : av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;

    for (i = 0; i < dstW; i++) {
        int j;
        int srcPos = filterPos[i];
        int val = 0;

        for (j = 0; j < filterSize; j++) {
            val += src[srcPos + j] * filter[filterSize * i + j];
        }
        // filter=14 bit, input=16 bit, output=30 bit, >> 15 makes 15 bit
        dst[i] = FFMIN(val >> sh, (1 << 15) - 1);
    }
}

// bilinear / bicubic scaling
static void hScale8To15_c(SwsContext *c, int16_t *dst, int dstW, const uint8_t *src,
                          const int16_t *filter, const int16_t *filterPos,
                          int filterSize)
{
    int i;
    for (i=0; i<dstW; i++) {
        int j;
        int srcPos= filterPos[i];
        int val=0;
        for (j=0; j<filterSize; j++) {
            val += ((int)src[srcPos + j])*filter[filterSize*i + j];
        }
        //filter += hFilterSize;
        dst[i] = FFMIN(val>>7, (1<<15)-1); // the cubic equation does overflow ...
        //dst[i] = val>>7;
    }
}

static void hScale8To19_c(SwsContext *c, int16_t *_dst, int dstW, const uint8_t *src,
                          const int16_t *filter, const int16_t *filterPos,
                          int filterSize)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    for (i=0; i<dstW; i++) {
        int j;
        int srcPos= filterPos[i];
        int val=0;
        for (j=0; j<filterSize; j++) {
            val += ((int)src[srcPos + j])*filter[filterSize*i + j];
        }
        //filter += hFilterSize;
        dst[i] = FFMIN(val>>3, (1<<19)-1); // the cubic equation does overflow ...
        //dst[i] = val>>7;
    }
}

//FIXME all pal and rgb srcFormats could do this convertion as well
//FIXME all scalers more complex than bilinear could do half of this transform
static void chrRangeToJpeg_c(int16_t *dstU, int16_t *dstV, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = (FFMIN(dstU[i],30775)*4663 - 9289992)>>12; //-264
        dstV[i] = (FFMIN(dstV[i],30775)*4663 - 9289992)>>12; //-264
    }
}
static void chrRangeFromJpeg_c(int16_t *dstU, int16_t *dstV, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = (dstU[i]*1799 + 4081085)>>11; //1469
        dstV[i] = (dstV[i]*1799 + 4081085)>>11; //1469
    }
}
static void lumRangeToJpeg_c(int16_t *dst, int width)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = (FFMIN(dst[i],30189)*19077 - 39057361)>>14;
}
static void lumRangeFromJpeg_c(int16_t *dst, int width)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = (dst[i]*14071 + 33561947)>>14;
}

static void chrRangeToJpeg16_c(int16_t *_dstU, int16_t *_dstV, int width)
{
    int i;
    int32_t *dstU = (int32_t *) _dstU;
    int32_t *dstV = (int32_t *) _dstV;
    for (i = 0; i < width; i++) {
        dstU[i] = (FFMIN(dstU[i],30775<<4)*4663 - (9289992<<4))>>12; //-264
        dstV[i] = (FFMIN(dstV[i],30775<<4)*4663 - (9289992<<4))>>12; //-264
    }
}
static void chrRangeFromJpeg16_c(int16_t *_dstU, int16_t *_dstV, int width)
{
    int i;
    int32_t *dstU = (int32_t *) _dstU;
    int32_t *dstV = (int32_t *) _dstV;
    for (i = 0; i < width; i++) {
        dstU[i] = (dstU[i]*1799 + (4081085<<4))>>11; //1469
        dstV[i] = (dstV[i]*1799 + (4081085<<4))>>11; //1469
    }
}
static void lumRangeToJpeg16_c(int16_t *_dst, int width)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    for (i = 0; i < width; i++)
        dst[i] = (FFMIN(dst[i],30189<<4)*4769 - (39057361<<2))>>12;
}
static void lumRangeFromJpeg16_c(int16_t *_dst, int width)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    for (i = 0; i < width; i++)
        dst[i] = (dst[i]*(14071/4) + (33561947<<4)/4)>>12;
}

static void hyscale_fast_c(SwsContext *c, int16_t *dst, int dstWidth,
                           const uint8_t *src, int srcW, int xInc)
{
    int i;
    unsigned int xpos=0;
    for (i=0;i<dstWidth;i++) {
        register unsigned int xx=xpos>>16;
        register unsigned int xalpha=(xpos&0xFFFF)>>9;
        dst[i]= (src[xx]<<7) + (src[xx+1] - src[xx])*xalpha;
        xpos+=xInc;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
        dst[i] = src[srcW-1]*128;
}

// *** horizontal scale Y line to temp buffer
static av_always_inline void hyscale(SwsContext *c, int16_t *dst, int dstWidth,
                                     const uint8_t *src_in[4], int srcW, int xInc,
                                     const int16_t *hLumFilter,
                                     const int16_t *hLumFilterPos, int hLumFilterSize,
                                     uint8_t *formatConvBuffer,
                                     uint32_t *pal, int isAlpha)
{
    void (*toYV12)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, int, uint32_t *) = isAlpha ? c->alpToYV12 : c->lumToYV12;
    void (*convertRange)(int16_t *, int) = isAlpha ? NULL : c->lumConvertRange;
    const uint8_t *src = src_in[isAlpha ? 3 : 0];

    if (toYV12) {
        toYV12(formatConvBuffer, src, src_in[1], src_in[2], srcW, pal);
        src= formatConvBuffer;
    } else if (c->readLumPlanar && !isAlpha) {
        c->readLumPlanar(formatConvBuffer, src_in, srcW);
        src = formatConvBuffer;
    }

    if (!c->hyscale_fast) {
        c->hyScale(c, dst, dstWidth, src, hLumFilter, hLumFilterPos, hLumFilterSize);
    } else { // fast bilinear upscale / crap downscale
        c->hyscale_fast(c, dst, dstWidth, src, srcW, xInc);
    }

    if (convertRange)
        convertRange(dst, dstWidth);
}

static void hcscale_fast_c(SwsContext *c, int16_t *dst1, int16_t *dst2,
                           int dstWidth, const uint8_t *src1,
                           const uint8_t *src2, int srcW, int xInc)
{
    int i;
    unsigned int xpos=0;
    for (i=0;i<dstWidth;i++) {
        register unsigned int xx=xpos>>16;
        register unsigned int xalpha=(xpos&0xFFFF)>>9;
        dst1[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
        dst2[i]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
        xpos+=xInc;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) {
        dst1[i] = src1[srcW-1]*128;
        dst2[i] = src2[srcW-1]*128;
    }
}

static av_always_inline void hcscale(SwsContext *c, int16_t *dst1, int16_t *dst2, int dstWidth,
                                     const uint8_t *src_in[4],
                                     int srcW, int xInc, const int16_t *hChrFilter,
                                     const int16_t *hChrFilterPos, int hChrFilterSize,
                                     uint8_t *formatConvBuffer, uint32_t *pal)
{
    const uint8_t *src1 = src_in[1], *src2 = src_in[2];
    if (c->chrToYV12) {
        uint8_t *buf2 = formatConvBuffer + FFALIGN(srcW*2+78, 16);
        c->chrToYV12(formatConvBuffer, buf2, src_in[0], src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= buf2;
    } else if (c->readChrPlanar) {
        uint8_t *buf2 = formatConvBuffer + FFALIGN(srcW*2+78, 16);
        c->readChrPlanar(formatConvBuffer, buf2, src_in, srcW);
        src1= formatConvBuffer;
        src2= buf2;
    }

    if (!c->hcscale_fast) {
        c->hcScale(c, dst1, dstWidth, src1, hChrFilter, hChrFilterPos, hChrFilterSize);
        c->hcScale(c, dst2, dstWidth, src2, hChrFilter, hChrFilterPos, hChrFilterSize);
    } else { // fast bilinear upscale / crap downscale
        c->hcscale_fast(c, dst1, dst2, dstWidth, src1, src2, srcW, xInc);
    }

    if (c->chrConvertRange)
        c->chrConvertRange(dst1, dst2, dstWidth);
}

static av_always_inline void
find_c_packed_planar_out_funcs(SwsContext *c,
                               yuv2planar1_fn *yuv2plane1, yuv2planarX_fn *yuv2planeX,
                               yuv2interleavedX_fn *yuv2nv12cX,
                               yuv2packed1_fn *yuv2packed1, yuv2packed2_fn *yuv2packed2,
                               yuv2packedX_fn *yuv2packedX)
{
    enum PixelFormat dstFormat = c->dstFormat;

    if (is16BPS(dstFormat)) {
        *yuv2planeX = isBE(dstFormat) ? yuv2planeX_16BE_c  : yuv2planeX_16LE_c;
        *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_16BE_c  : yuv2plane1_16LE_c;
    } else if (is9_OR_10BPS(dstFormat)) {
        if (av_pix_fmt_descriptors[dstFormat].comp[0].depth_minus1 == 8) {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_9BE_c  : yuv2planeX_9LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_9BE_c  : yuv2plane1_9LE_c;
        } else {
            *yuv2planeX = isBE(dstFormat) ? yuv2planeX_10BE_c  : yuv2planeX_10LE_c;
            *yuv2plane1 = isBE(dstFormat) ? yuv2plane1_10BE_c  : yuv2plane1_10LE_c;
        }
    } else {
        *yuv2plane1 = yuv2plane1_8_c;
        *yuv2planeX = yuv2planeX_8_c;
        if (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21)
            *yuv2nv12cX = yuv2nv12cX_c;
    }

    if(c->flags & SWS_FULL_CHR_H_INT) {
        switch (dstFormat) {
            case PIX_FMT_RGBA:
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
            case PIX_FMT_ARGB:
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
            case PIX_FMT_BGRA:
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
            case PIX_FMT_ABGR:
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
            case PIX_FMT_RGB24:
            *yuv2packedX = yuv2rgb24_full_X_c;
            break;
        case PIX_FMT_BGR24:
            *yuv2packedX = yuv2bgr24_full_X_c;
            break;
        }
        if(!*yuv2packedX)
            goto YUV_PACKED;
    } else {
        YUV_PACKED:
        switch (dstFormat) {
        case PIX_FMT_GRAY16BE:
            *yuv2packed1 = yuv2gray16BE_1_c;
            *yuv2packed2 = yuv2gray16BE_2_c;
            *yuv2packedX = yuv2gray16BE_X_c;
            break;
        case PIX_FMT_GRAY16LE:
            *yuv2packed1 = yuv2gray16LE_1_c;
            *yuv2packed2 = yuv2gray16LE_2_c;
            *yuv2packedX = yuv2gray16LE_X_c;
            break;
        case PIX_FMT_MONOWHITE:
            *yuv2packed1 = yuv2monowhite_1_c;
            *yuv2packed2 = yuv2monowhite_2_c;
            *yuv2packedX = yuv2monowhite_X_c;
            break;
        case PIX_FMT_MONOBLACK:
            *yuv2packed1 = yuv2monoblack_1_c;
            *yuv2packed2 = yuv2monoblack_2_c;
            *yuv2packedX = yuv2monoblack_X_c;
            break;
        case PIX_FMT_YUYV422:
            *yuv2packed1 = yuv2yuyv422_1_c;
            *yuv2packed2 = yuv2yuyv422_2_c;
            *yuv2packedX = yuv2yuyv422_X_c;
            break;
        case PIX_FMT_UYVY422:
            *yuv2packed1 = yuv2uyvy422_1_c;
            *yuv2packed2 = yuv2uyvy422_2_c;
            *yuv2packedX = yuv2uyvy422_X_c;
            break;
        case PIX_FMT_RGB48LE:
            *yuv2packed1 = yuv2rgb48le_1_c;
            *yuv2packed2 = yuv2rgb48le_2_c;
            *yuv2packedX = yuv2rgb48le_X_c;
            break;
        case PIX_FMT_RGB48BE:
            *yuv2packed1 = yuv2rgb48be_1_c;
            *yuv2packed2 = yuv2rgb48be_2_c;
            *yuv2packedX = yuv2rgb48be_X_c;
            break;
        case PIX_FMT_BGR48LE:
            *yuv2packed1 = yuv2bgr48le_1_c;
            *yuv2packed2 = yuv2bgr48le_2_c;
            *yuv2packedX = yuv2bgr48le_X_c;
            break;
        case PIX_FMT_BGR48BE:
            *yuv2packed1 = yuv2bgr48be_1_c;
            *yuv2packed2 = yuv2bgr48be_2_c;
            *yuv2packedX = yuv2bgr48be_X_c;
            break;
        case PIX_FMT_RGB32:
        case PIX_FMT_BGR32:
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
        case PIX_FMT_RGB32_1:
        case PIX_FMT_BGR32_1:
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
        case PIX_FMT_RGB24:
            *yuv2packed1 = yuv2rgb24_1_c;
            *yuv2packed2 = yuv2rgb24_2_c;
            *yuv2packedX = yuv2rgb24_X_c;
            break;
        case PIX_FMT_BGR24:
            *yuv2packed1 = yuv2bgr24_1_c;
            *yuv2packed2 = yuv2bgr24_2_c;
            *yuv2packedX = yuv2bgr24_X_c;
            break;
        case PIX_FMT_RGB565LE:
        case PIX_FMT_RGB565BE:
        case PIX_FMT_BGR565LE:
        case PIX_FMT_BGR565BE:
            *yuv2packed1 = yuv2rgb16_1_c;
            *yuv2packed2 = yuv2rgb16_2_c;
            *yuv2packedX = yuv2rgb16_X_c;
            break;
        case PIX_FMT_RGB555LE:
        case PIX_FMT_RGB555BE:
        case PIX_FMT_BGR555LE:
        case PIX_FMT_BGR555BE:
            *yuv2packed1 = yuv2rgb15_1_c;
            *yuv2packed2 = yuv2rgb15_2_c;
            *yuv2packedX = yuv2rgb15_X_c;
            break;
        case PIX_FMT_RGB444LE:
        case PIX_FMT_RGB444BE:
        case PIX_FMT_BGR444LE:
        case PIX_FMT_BGR444BE:
            *yuv2packed1 = yuv2rgb12_1_c;
            *yuv2packed2 = yuv2rgb12_2_c;
            *yuv2packedX = yuv2rgb12_X_c;
            break;
        case PIX_FMT_RGB8:
        case PIX_FMT_BGR8:
            *yuv2packed1 = yuv2rgb8_1_c;
            *yuv2packed2 = yuv2rgb8_2_c;
            *yuv2packedX = yuv2rgb8_X_c;
            break;
        case PIX_FMT_RGB4:
        case PIX_FMT_BGR4:
            *yuv2packed1 = yuv2rgb4_1_c;
            *yuv2packed2 = yuv2rgb4_2_c;
            *yuv2packedX = yuv2rgb4_X_c;
            break;
        case PIX_FMT_RGB4_BYTE:
        case PIX_FMT_BGR4_BYTE:
            *yuv2packed1 = yuv2rgb4b_1_c;
            *yuv2packed2 = yuv2rgb4b_2_c;
            *yuv2packedX = yuv2rgb4b_X_c;
            break;
        }
    }
}

#define DEBUG_SWSCALE_BUFFERS 0
#define DEBUG_BUFFERS(...) if (DEBUG_SWSCALE_BUFFERS) av_log(c, AV_LOG_DEBUG, __VA_ARGS__)

static int swScale(SwsContext *c, const uint8_t* src[],
                   int srcStride[], int srcSliceY,
                   int srcSliceH, uint8_t* dst[], int dstStride[])
{
    /* load a few things into local vars to make the code more readable? and faster */
    const int srcW= c->srcW;
    const int dstW= c->dstW;
    const int dstH= c->dstH;
    const int chrDstW= c->chrDstW;
    const int chrSrcW= c->chrSrcW;
    const int lumXInc= c->lumXInc;
    const int chrXInc= c->chrXInc;
    const enum PixelFormat dstFormat= c->dstFormat;
    const int flags= c->flags;
    int16_t *vLumFilterPos= c->vLumFilterPos;
    int16_t *vChrFilterPos= c->vChrFilterPos;
    int16_t *hLumFilterPos= c->hLumFilterPos;
    int16_t *hChrFilterPos= c->hChrFilterPos;
    int16_t *hLumFilter= c->hLumFilter;
    int16_t *hChrFilter= c->hChrFilter;
    int32_t *lumMmxFilter= c->lumMmxFilter;
    int32_t *chrMmxFilter= c->chrMmxFilter;
    int32_t av_unused *alpMmxFilter= c->alpMmxFilter;
    const int vLumFilterSize= c->vLumFilterSize;
    const int vChrFilterSize= c->vChrFilterSize;
    const int hLumFilterSize= c->hLumFilterSize;
    const int hChrFilterSize= c->hChrFilterSize;
    int16_t **lumPixBuf= c->lumPixBuf;
    int16_t **chrUPixBuf= c->chrUPixBuf;
    int16_t **chrVPixBuf= c->chrVPixBuf;
    int16_t **alpPixBuf= c->alpPixBuf;
    const int vLumBufSize= c->vLumBufSize;
    const int vChrBufSize= c->vChrBufSize;
    uint8_t *formatConvBuffer= c->formatConvBuffer;
    const int chrSrcSliceY= srcSliceY >> c->chrSrcVSubSample;
    const int chrSrcSliceH= -((-srcSliceH) >> c->chrSrcVSubSample);
    int lastDstY;
    uint32_t *pal=c->pal_yuv;
    int should_dither= isNBPS(c->srcFormat) || is16BPS(c->srcFormat);

    yuv2planar1_fn yuv2plane1 = c->yuv2plane1;
    yuv2planarX_fn yuv2planeX = c->yuv2planeX;
    yuv2interleavedX_fn yuv2nv12cX = c->yuv2nv12cX;
    yuv2packed1_fn yuv2packed1 = c->yuv2packed1;
    yuv2packed2_fn yuv2packed2 = c->yuv2packed2;
    yuv2packedX_fn yuv2packedX = c->yuv2packedX;

    /* vars which will change and which we need to store back in the context */
    int dstY= c->dstY;
    int lumBufIndex= c->lumBufIndex;
    int chrBufIndex= c->chrBufIndex;
    int lastInLumBuf= c->lastInLumBuf;
    int lastInChrBuf= c->lastInChrBuf;

    if (isPacked(c->srcFormat)) {
        src[0]=
        src[1]=
        src[2]=
        src[3]= src[0];
        srcStride[0]=
        srcStride[1]=
        srcStride[2]=
        srcStride[3]= srcStride[0];
    }
    srcStride[1]<<= c->vChrDrop;
    srcStride[2]<<= c->vChrDrop;

    DEBUG_BUFFERS("swScale() %p[%d] %p[%d] %p[%d] %p[%d] -> %p[%d] %p[%d] %p[%d] %p[%d]\n",
                  src[0], srcStride[0], src[1], srcStride[1], src[2], srcStride[2], src[3], srcStride[3],
                  dst[0], dstStride[0], dst[1], dstStride[1], dst[2], dstStride[2], dst[3], dstStride[3]);
    DEBUG_BUFFERS("srcSliceY: %d srcSliceH: %d dstY: %d dstH: %d\n",
                   srcSliceY,    srcSliceH,    dstY,    dstH);
    DEBUG_BUFFERS("vLumFilterSize: %d vLumBufSize: %d vChrFilterSize: %d vChrBufSize: %d\n",
                   vLumFilterSize,    vLumBufSize,    vChrFilterSize,    vChrBufSize);

    if (dstStride[0]%16 !=0 || dstStride[1]%16 !=0 || dstStride[2]%16 !=0 || dstStride[3]%16 != 0) {
        static int warnedAlready=0; //FIXME move this into the context perhaps
        if (flags & SWS_PRINT_INFO && !warnedAlready) {
            av_log(c, AV_LOG_WARNING, "Warning: dstStride is not aligned!\n"
                   "         ->cannot do aligned memory accesses anymore\n");
            warnedAlready=1;
        }
    }

    if ((int)dst[0]%16 || (int)dst[1]%16 || (int)dst[2]%16 || (int)src[0]%16 || (int)src[1]%16 || (int)src[2]%16
        || dstStride[0]%16 || dstStride[1]%16 || dstStride[2]%16 || dstStride[3]%16
        || srcStride[0]%16 || srcStride[1]%16 || srcStride[2]%16 || srcStride[3]%16
    ) {
        static int warnedAlready=0;
        int cpu_flags = av_get_cpu_flags();
        if (HAVE_MMX2 && (cpu_flags & AV_CPU_FLAG_SSE2) && !warnedAlready){
            av_log(c, AV_LOG_WARNING, "Warning: data is not aligned! This can lead to a speedloss\n");
            warnedAlready=1;
        }
    }

    /* Note the user might start scaling the picture in the middle so this
       will not get executed. This is not really intended but works
       currently, so people might do it. */
    if (srcSliceY ==0) {
        lumBufIndex=-1;
        chrBufIndex=-1;
        dstY=0;
        lastInLumBuf= -1;
        lastInChrBuf= -1;
    }

    if (!should_dither) {
        c->chrDither8 = c->lumDither8 = ff_sws_pb_64;
    }
    lastDstY= dstY;

    for (;dstY < dstH; dstY++) {
        const int chrDstY= dstY>>c->chrDstVSubSample;
        uint8_t *dest[4] = {
            dst[0] + dstStride[0] * dstY,
            dst[1] + dstStride[1] * chrDstY,
            dst[2] + dstStride[2] * chrDstY,
            (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? dst[3] + dstStride[3] * dstY : NULL,
        };
        int use_mmx_vfilter= c->use_mmx_vfilter;

        const int firstLumSrcY= vLumFilterPos[dstY]; //First line needed as input
        const int firstLumSrcY2= vLumFilterPos[FFMIN(dstY | ((1<<c->chrDstVSubSample) - 1), dstH-1)];
        const int firstChrSrcY= vChrFilterPos[chrDstY]; //First line needed as input
        int lastLumSrcY= firstLumSrcY + vLumFilterSize -1; // Last line needed as input
        int lastLumSrcY2=firstLumSrcY2+ vLumFilterSize -1; // Last line needed as input
        int lastChrSrcY= firstChrSrcY + vChrFilterSize -1; // Last line needed as input
        int enough_lines;

        //handle holes (FAST_BILINEAR & weird filters)
        if (firstLumSrcY > lastInLumBuf) lastInLumBuf= firstLumSrcY-1;
        if (firstChrSrcY > lastInChrBuf) lastInChrBuf= firstChrSrcY-1;
        assert(firstLumSrcY >= lastInLumBuf - vLumBufSize + 1);
        assert(firstChrSrcY >= lastInChrBuf - vChrBufSize + 1);

        DEBUG_BUFFERS("dstY: %d\n", dstY);
        DEBUG_BUFFERS("\tfirstLumSrcY: %d lastLumSrcY: %d lastInLumBuf: %d\n",
                         firstLumSrcY,    lastLumSrcY,    lastInLumBuf);
        DEBUG_BUFFERS("\tfirstChrSrcY: %d lastChrSrcY: %d lastInChrBuf: %d\n",
                         firstChrSrcY,    lastChrSrcY,    lastInChrBuf);

        // Do we have enough lines in this slice to output the dstY line
        enough_lines = lastLumSrcY2 < srcSliceY + srcSliceH && lastChrSrcY < -((-srcSliceY - srcSliceH)>>c->chrSrcVSubSample);

        if (!enough_lines) {
            lastLumSrcY = srcSliceY + srcSliceH - 1;
            lastChrSrcY = chrSrcSliceY + chrSrcSliceH - 1;
            DEBUG_BUFFERS("buffering slice: lastLumSrcY %d lastChrSrcY %d\n",
                                            lastLumSrcY, lastChrSrcY);
        }

        //Do horizontal scaling
        while(lastInLumBuf < lastLumSrcY) {
            const uint8_t *src1[4] = {
                src[0] + (lastInLumBuf + 1 - srcSliceY) * srcStride[0],
                src[1] + (lastInLumBuf + 1 - srcSliceY) * srcStride[1],
                src[2] + (lastInLumBuf + 1 - srcSliceY) * srcStride[2],
                src[3] + (lastInLumBuf + 1 - srcSliceY) * srcStride[3],
            };
            lumBufIndex++;
            assert(lumBufIndex < 2*vLumBufSize);
            assert(lastInLumBuf + 1 - srcSliceY < srcSliceH);
            assert(lastInLumBuf + 1 - srcSliceY >= 0);
            hyscale(c, lumPixBuf[ lumBufIndex ], dstW, src1, srcW, lumXInc,
                    hLumFilter, hLumFilterPos, hLumFilterSize,
                    formatConvBuffer,
                    pal, 0);
            if (CONFIG_SWSCALE_ALPHA && alpPixBuf)
                hyscale(c, alpPixBuf[ lumBufIndex ], dstW, src1, srcW,
                        lumXInc, hLumFilter, hLumFilterPos, hLumFilterSize,
                        formatConvBuffer,
                        pal, 1);
            lastInLumBuf++;
            DEBUG_BUFFERS("\t\tlumBufIndex %d: lastInLumBuf: %d\n",
                               lumBufIndex,    lastInLumBuf);
        }
        while(lastInChrBuf < lastChrSrcY) {
            const uint8_t *src1[4] = {
                src[0] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[0],
                src[1] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[1],
                src[2] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[2],
                src[3] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[3],
            };
            chrBufIndex++;
            assert(chrBufIndex < 2*vChrBufSize);
            assert(lastInChrBuf + 1 - chrSrcSliceY < (chrSrcSliceH));
            assert(lastInChrBuf + 1 - chrSrcSliceY >= 0);
            //FIXME replace parameters through context struct (some at least)

            if (c->needs_hcscale)
                hcscale(c, chrUPixBuf[chrBufIndex], chrVPixBuf[chrBufIndex],
                          chrDstW, src1, chrSrcW, chrXInc,
                          hChrFilter, hChrFilterPos, hChrFilterSize,
                          formatConvBuffer, pal);
            lastInChrBuf++;
            DEBUG_BUFFERS("\t\tchrBufIndex %d: lastInChrBuf: %d\n",
                               chrBufIndex,    lastInChrBuf);
        }
        //wrap buf index around to stay inside the ring buffer
        if (lumBufIndex >= vLumBufSize) lumBufIndex-= vLumBufSize;
        if (chrBufIndex >= vChrBufSize) chrBufIndex-= vChrBufSize;
        if (!enough_lines)
            break; //we can't output a dstY line so let's try with the next slice

#if HAVE_MMX
        updateMMXDitherTables(c, dstY, lumBufIndex, chrBufIndex, lastInLumBuf, lastInChrBuf);
#endif
        if (should_dither) {
            c->chrDither8 = dither_8x8_128[chrDstY & 7];
            c->lumDither8 = dither_8x8_128[dstY & 7];
        }
        if (dstY >= dstH-2) {
            // hmm looks like we can't use MMX here without overwriting this array's tail
            find_c_packed_planar_out_funcs(c, &yuv2plane1, &yuv2planeX,  &yuv2nv12cX,
                                           &yuv2packed1, &yuv2packed2, &yuv2packedX);
            use_mmx_vfilter= 0;
        }

        {
            const int16_t **lumSrcPtr= (const int16_t **) lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            const int16_t **chrUSrcPtr= (const int16_t **) chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **chrVSrcPtr= (const int16_t **) chrVPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **alpSrcPtr= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? (const int16_t **) alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
            int16_t *vLumFilter= c->vLumFilter;
            int16_t *vChrFilter= c->vChrFilter;

            if (isPlanarYUV(dstFormat) || dstFormat==PIX_FMT_GRAY8) { //YV12 like
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;

                vLumFilter +=    dstY * vLumFilterSize;
                vChrFilter += chrDstY * vChrFilterSize;

                av_assert0(use_mmx_vfilter != (
                               yuv2planeX == yuv2planeX_10BE_c
                            || yuv2planeX == yuv2planeX_10LE_c
                            || yuv2planeX == yuv2planeX_9BE_c
                            || yuv2planeX == yuv2planeX_9LE_c
                            || yuv2planeX == yuv2planeX_16BE_c
                            || yuv2planeX == yuv2planeX_16LE_c
                            || yuv2planeX == yuv2planeX_8_c) || !ARCH_X86);

                if(use_mmx_vfilter){
                    vLumFilter= c->lumMmxFilter;
                    vChrFilter= c->chrMmxFilter;
                }

                if (vLumFilterSize == 1) {
                    yuv2plane1(lumSrcPtr[0], dest[0], dstW, c->lumDither8, 0);
                } else {
                    yuv2planeX(vLumFilter, vLumFilterSize,
                               lumSrcPtr, dest[0], dstW, c->lumDither8, 0);
                }

                if (!((dstY&chrSkipMask) || isGray(dstFormat))) {
                    if (yuv2nv12cX) {
                        yuv2nv12cX(c, vChrFilter, vChrFilterSize, chrUSrcPtr, chrVSrcPtr, dest[1], chrDstW);
                    } else if (vChrFilterSize == 1) {
                        yuv2plane1(chrUSrcPtr[0], dest[1], chrDstW, c->chrDither8, 0);
                        yuv2plane1(chrVSrcPtr[0], dest[2], chrDstW, c->chrDither8, 3);
                    } else {
                        yuv2planeX(vChrFilter, vChrFilterSize,
                                   chrUSrcPtr, dest[1], chrDstW, c->chrDither8, 0);
                        yuv2planeX(vChrFilter, vChrFilterSize,
                                   chrVSrcPtr, dest[2], chrDstW, c->chrDither8, use_mmx_vfilter ? (c->uv_offx2 >> 1) : 3);
                    }
                }

                if (CONFIG_SWSCALE_ALPHA && alpPixBuf){
                    if(use_mmx_vfilter){
                        vLumFilter= c->alpMmxFilter;
                    }
                    if (vLumFilterSize == 1) {
                        yuv2plane1(alpSrcPtr[0], dest[3], dstW, c->lumDither8, 0);
                    } else {
                        yuv2planeX(vLumFilter, vLumFilterSize,
                                   alpSrcPtr, dest[3], dstW, c->lumDither8, 0);
                    }
                }
            } else {
                assert(lumSrcPtr  + vLumFilterSize - 1 < lumPixBuf  + vLumBufSize*2);
                assert(chrUSrcPtr + vChrFilterSize - 1 < chrUPixBuf + vChrBufSize*2);
                if (c->yuv2packed1 && vLumFilterSize == 1 && vChrFilterSize == 2) { //unscaled RGB
                    int chrAlpha = vChrFilter[2 * dstY + 1];
                    yuv2packed1(c, *lumSrcPtr, chrUSrcPtr, chrVSrcPtr,
                                alpPixBuf ? *alpSrcPtr : NULL,
                                dest[0], dstW, chrAlpha, dstY);
                } else if (c->yuv2packed2 && vLumFilterSize == 2 && vChrFilterSize == 2) { //bilinear upscale RGB
                    int lumAlpha = vLumFilter[2 * dstY + 1];
                    int chrAlpha = vChrFilter[2 * dstY + 1];
                    lumMmxFilter[2] =
                    lumMmxFilter[3] = vLumFilter[2 * dstY   ] * 0x10001;
                    chrMmxFilter[2] =
                    chrMmxFilter[3] = vChrFilter[2 * chrDstY] * 0x10001;
                    yuv2packed2(c, lumSrcPtr, chrUSrcPtr, chrVSrcPtr,
                                alpPixBuf ? alpSrcPtr : NULL,
                                dest[0], dstW, lumAlpha, chrAlpha, dstY);
                } else { //general RGB
                    yuv2packedX(c, vLumFilter + dstY * vLumFilterSize,
                                lumSrcPtr, vLumFilterSize,
                                vChrFilter + dstY * vChrFilterSize,
                                chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                alpSrcPtr, dest[0], dstW, dstY);
                }
            }
        }
    }

    if ((dstFormat == PIX_FMT_YUVA420P) && !alpPixBuf)
        fillPlane(dst[3], dstStride[3], dstW, dstY-lastDstY, lastDstY, 255);

#if HAVE_MMX2
    if (av_get_cpu_flags() & AV_CPU_FLAG_MMX2)
        __asm__ volatile("sfence":::"memory");
#endif
    emms_c();

    /* store changed local vars back in the context */
    c->dstY= dstY;
    c->lumBufIndex= lumBufIndex;
    c->chrBufIndex= chrBufIndex;
    c->lastInLumBuf= lastInLumBuf;
    c->lastInChrBuf= lastInChrBuf;

    return dstY - lastDstY;
}

static av_cold void sws_init_swScale_c(SwsContext *c)
{
    enum PixelFormat srcFormat = c->srcFormat;

    find_c_packed_planar_out_funcs(c, &c->yuv2plane1, &c->yuv2planeX,
                                   &c->yuv2nv12cX, &c->yuv2packed1, &c->yuv2packed2,
                                   &c->yuv2packedX);

    c->chrToYV12 = NULL;
    switch(srcFormat) {
        case PIX_FMT_YUYV422  : c->chrToYV12 = yuy2ToUV_c; break;
        case PIX_FMT_UYVY422  : c->chrToYV12 = uyvyToUV_c; break;
        case PIX_FMT_NV12     : c->chrToYV12 = nv12ToUV_c; break;
        case PIX_FMT_NV21     : c->chrToYV12 = nv21ToUV_c; break;
        case PIX_FMT_RGB8     :
        case PIX_FMT_BGR8     :
        case PIX_FMT_PAL8     :
        case PIX_FMT_BGR4_BYTE:
        case PIX_FMT_RGB4_BYTE: c->chrToYV12 = palToUV_c; break;
        case PIX_FMT_GBRP9LE:
        case PIX_FMT_GBRP10LE:
        case PIX_FMT_GBRP16LE:  c->readChrPlanar = planar_rgb16le_to_uv; break;
        case PIX_FMT_GBRP9BE:
        case PIX_FMT_GBRP10BE:
        case PIX_FMT_GBRP16BE:  c->readChrPlanar = planar_rgb16be_to_uv; break;
        case PIX_FMT_GBRP:      c->readChrPlanar = planar_rgb_to_uv; break;
#if HAVE_BIGENDIAN
        case PIX_FMT_YUV444P9LE:
        case PIX_FMT_YUV422P9LE:
        case PIX_FMT_YUV420P9LE:
        case PIX_FMT_YUV422P10LE:
        case PIX_FMT_YUV420P10LE:
        case PIX_FMT_YUV444P10LE:
        case PIX_FMT_YUV420P16LE:
        case PIX_FMT_YUV422P16LE:
        case PIX_FMT_YUV444P16LE: c->chrToYV12 = bswap16UV_c; break;
#else
        case PIX_FMT_YUV444P9BE:
        case PIX_FMT_YUV422P9BE:
        case PIX_FMT_YUV420P9BE:
        case PIX_FMT_YUV444P10BE:
        case PIX_FMT_YUV422P10BE:
        case PIX_FMT_YUV420P10BE:
        case PIX_FMT_YUV420P16BE:
        case PIX_FMT_YUV422P16BE:
        case PIX_FMT_YUV444P16BE: c->chrToYV12 = bswap16UV_c; break;
#endif
    }
    if (c->chrSrcHSubSample) {
        switch(srcFormat) {
        case PIX_FMT_RGB48BE : c->chrToYV12 = rgb48BEToUV_half_c; break;
        case PIX_FMT_RGB48LE : c->chrToYV12 = rgb48LEToUV_half_c; break;
        case PIX_FMT_BGR48BE : c->chrToYV12 = bgr48BEToUV_half_c; break;
        case PIX_FMT_BGR48LE : c->chrToYV12 = bgr48LEToUV_half_c; break;
        case PIX_FMT_RGB32   : c->chrToYV12 = bgr32ToUV_half_c;   break;
        case PIX_FMT_RGB32_1 : c->chrToYV12 = bgr321ToUV_half_c;  break;
        case PIX_FMT_BGR24   : c->chrToYV12 = bgr24ToUV_half_c;   break;
        case PIX_FMT_BGR565LE: c->chrToYV12 = bgr16leToUV_half_c; break;
        case PIX_FMT_BGR565BE: c->chrToYV12 = bgr16beToUV_half_c; break;
        case PIX_FMT_BGR555LE: c->chrToYV12 = bgr15leToUV_half_c; break;
        case PIX_FMT_BGR555BE: c->chrToYV12 = bgr15beToUV_half_c; break;
        case PIX_FMT_BGR32   : c->chrToYV12 = rgb32ToUV_half_c;   break;
        case PIX_FMT_BGR32_1 : c->chrToYV12 = rgb321ToUV_half_c;  break;
        case PIX_FMT_RGB24   : c->chrToYV12 = rgb24ToUV_half_c;   break;
        case PIX_FMT_RGB565LE: c->chrToYV12 = rgb16leToUV_half_c; break;
        case PIX_FMT_RGB565BE: c->chrToYV12 = rgb16beToUV_half_c; break;
        case PIX_FMT_RGB555LE: c->chrToYV12 = rgb15leToUV_half_c; break;
        case PIX_FMT_RGB555BE: c->chrToYV12 = rgb15beToUV_half_c; break;
        case PIX_FMT_GBR24P  : c->chrToYV12 = gbr24pToUV_half_c;  break;
        }
    } else {
        switch(srcFormat) {
        case PIX_FMT_RGB48BE : c->chrToYV12 = rgb48BEToUV_c; break;
        case PIX_FMT_RGB48LE : c->chrToYV12 = rgb48LEToUV_c; break;
        case PIX_FMT_BGR48BE : c->chrToYV12 = bgr48BEToUV_c; break;
        case PIX_FMT_BGR48LE : c->chrToYV12 = bgr48LEToUV_c; break;
        case PIX_FMT_RGB32   : c->chrToYV12 = bgr32ToUV_c;   break;
        case PIX_FMT_RGB32_1 : c->chrToYV12 = bgr321ToUV_c;  break;
        case PIX_FMT_BGR24   : c->chrToYV12 = bgr24ToUV_c;   break;
        case PIX_FMT_BGR565LE: c->chrToYV12 = bgr16leToUV_c; break;
        case PIX_FMT_BGR565BE: c->chrToYV12 = bgr16beToUV_c; break;
        case PIX_FMT_BGR555LE: c->chrToYV12 = bgr15leToUV_c; break;
        case PIX_FMT_BGR555BE: c->chrToYV12 = bgr15beToUV_c; break;
        case PIX_FMT_BGR32   : c->chrToYV12 = rgb32ToUV_c;   break;
        case PIX_FMT_BGR32_1 : c->chrToYV12 = rgb321ToUV_c;  break;
        case PIX_FMT_RGB24   : c->chrToYV12 = rgb24ToUV_c;   break;
        case PIX_FMT_RGB565LE: c->chrToYV12 = rgb16leToUV_c; break;
        case PIX_FMT_RGB565BE: c->chrToYV12 = rgb16beToUV_c; break;
        case PIX_FMT_RGB555LE: c->chrToYV12 = rgb15leToUV_c; break;
        case PIX_FMT_RGB555BE: c->chrToYV12 = rgb15beToUV_c; break;
//        case PIX_FMT_GBR24P  : c->chrToYV12 = gbr24pToUV_c;  break;
        }
    }

    c->lumToYV12 = NULL;
    c->alpToYV12 = NULL;
    switch (srcFormat) {
    case PIX_FMT_GBRP9LE:
    case PIX_FMT_GBRP10LE:
    case PIX_FMT_GBRP16LE: c->readLumPlanar = planar_rgb16le_to_y; break;
    case PIX_FMT_GBRP9BE:
    case PIX_FMT_GBRP10BE:
    case PIX_FMT_GBRP16BE: c->readLumPlanar = planar_rgb16be_to_y; break;
    case PIX_FMT_GBRP:     c->readLumPlanar = planar_rgb_to_y; break;
#if HAVE_BIGENDIAN
    case PIX_FMT_YUV444P9LE:
    case PIX_FMT_YUV422P9LE:
    case PIX_FMT_YUV420P9LE:
    case PIX_FMT_YUV422P10LE:
    case PIX_FMT_YUV420P10LE:
    case PIX_FMT_YUV444P10LE:
    case PIX_FMT_YUV420P16LE:
    case PIX_FMT_YUV422P16LE:
    case PIX_FMT_YUV444P16LE:
    case PIX_FMT_GRAY16LE: c->lumToYV12 = bswap16Y_c; break;
#else
    case PIX_FMT_YUV444P9BE:
    case PIX_FMT_YUV422P9BE:
    case PIX_FMT_YUV420P9BE:
    case PIX_FMT_YUV444P10BE:
    case PIX_FMT_YUV422P10BE:
    case PIX_FMT_YUV420P10BE:
    case PIX_FMT_YUV420P16BE:
    case PIX_FMT_YUV422P16BE:
    case PIX_FMT_YUV444P16BE:
    case PIX_FMT_GRAY16BE: c->lumToYV12 = bswap16Y_c; break;
#endif
    case PIX_FMT_YUYV422  :
    case PIX_FMT_Y400A    : c->lumToYV12 = yuy2ToY_c; break;
    case PIX_FMT_UYVY422  : c->lumToYV12 = uyvyToY_c;    break;
    case PIX_FMT_BGR24    : c->lumToYV12 = bgr24ToY_c;   break;
    case PIX_FMT_BGR565LE : c->lumToYV12 = bgr16leToY_c; break;
    case PIX_FMT_BGR565BE : c->lumToYV12 = bgr16beToY_c; break;
    case PIX_FMT_BGR555LE : c->lumToYV12 = bgr15leToY_c; break;
    case PIX_FMT_BGR555BE : c->lumToYV12 = bgr15beToY_c; break;
    case PIX_FMT_RGB24    : c->lumToYV12 = rgb24ToY_c;   break;
    case PIX_FMT_RGB565LE : c->lumToYV12 = rgb16leToY_c; break;
    case PIX_FMT_RGB565BE : c->lumToYV12 = rgb16beToY_c; break;
    case PIX_FMT_RGB555LE : c->lumToYV12 = rgb15leToY_c; break;
    case PIX_FMT_RGB555BE : c->lumToYV12 = rgb15beToY_c; break;
    case PIX_FMT_RGB8     :
    case PIX_FMT_BGR8     :
    case PIX_FMT_PAL8     :
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_RGB4_BYTE: c->lumToYV12 = palToY_c; break;
    case PIX_FMT_MONOBLACK: c->lumToYV12 = monoblack2Y_c; break;
    case PIX_FMT_MONOWHITE: c->lumToYV12 = monowhite2Y_c; break;
    case PIX_FMT_RGB32  : c->lumToYV12 = bgr32ToY_c;  break;
    case PIX_FMT_RGB32_1: c->lumToYV12 = bgr321ToY_c; break;
    case PIX_FMT_BGR32  : c->lumToYV12 = rgb32ToY_c;  break;
    case PIX_FMT_BGR32_1: c->lumToYV12 = rgb321ToY_c; break;
    case PIX_FMT_RGB48BE: c->lumToYV12 = rgb48BEToY_c; break;
    case PIX_FMT_RGB48LE: c->lumToYV12 = rgb48LEToY_c; break;
    case PIX_FMT_BGR48BE: c->lumToYV12 = bgr48BEToY_c; break;
    case PIX_FMT_BGR48LE: c->lumToYV12 = bgr48LEToY_c; break;
//    case PIX_FMT_GBR24P : c->lumToYV12 = gbr24pToY_c ; break;
    }
    if (c->alpPixBuf) {
        switch (srcFormat) {
        case PIX_FMT_BGRA:
        case PIX_FMT_RGBA:  c->alpToYV12 = rgbaToA_c; break;
        case PIX_FMT_ABGR:
        case PIX_FMT_ARGB:  c->alpToYV12 = abgrToA_c; break;
        case PIX_FMT_Y400A: c->alpToYV12 = uyvyToY_c; break;
        case PIX_FMT_PAL8 : c->alpToYV12 = palToA_c; break;
        }
    }


    if (c->srcBpc == 8) {
        if (c->dstBpc <= 10) {
            c->hyScale = c->hcScale = hScale8To15_c;
            if (c->flags & SWS_FAST_BILINEAR) {
                c->hyscale_fast = hyscale_fast_c;
                c->hcscale_fast = hcscale_fast_c;
            }
        } else {
            c->hyScale = c->hcScale = hScale8To19_c;
        }
    } else {
        c->hyScale = c->hcScale = c->dstBpc > 10 ? hScale16To19_c : hScale16To15_c;
    }

    if (c->srcRange != c->dstRange && !isAnyRGB(c->dstFormat)) {
        if (c->dstBpc <= 10) {
            if (c->srcRange) {
                c->lumConvertRange = lumRangeFromJpeg_c;
                c->chrConvertRange = chrRangeFromJpeg_c;
            } else {
                c->lumConvertRange = lumRangeToJpeg_c;
                c->chrConvertRange = chrRangeToJpeg_c;
            }
        } else {
            if (c->srcRange) {
                c->lumConvertRange = lumRangeFromJpeg16_c;
                c->chrConvertRange = chrRangeFromJpeg16_c;
            } else {
                c->lumConvertRange = lumRangeToJpeg16_c;
                c->chrConvertRange = chrRangeToJpeg16_c;
            }
        }
    }

    if (!(isGray(srcFormat) || isGray(c->dstFormat) ||
          srcFormat == PIX_FMT_MONOBLACK || srcFormat == PIX_FMT_MONOWHITE))
        c->needs_hcscale = 1;
}

SwsFunc ff_getSwsFunc(SwsContext *c)
{
    sws_init_swScale_c(c);

    if (HAVE_MMX)
        ff_sws_init_swScale_mmx(c);
    if (HAVE_ALTIVEC)
        ff_sws_init_swScale_altivec(c);

    return swScale;
}
