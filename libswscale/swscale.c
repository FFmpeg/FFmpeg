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
#include "libavutil/intreadwrite.h"
#include "libavutil/cpu.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libavutil/bswap.h"
#include "libavutil/pixdesc.h"

#define DITHER1XBPP

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

static const double rgb2yuv_table[8][9]={
    {0.7152, 0.0722, 0.2126, -0.386, 0.5, -0.115, -0.454, -0.046, 0.5}, //ITU709
    {0.7152, 0.0722, 0.2126, -0.386, 0.5, -0.115, -0.454, -0.046, 0.5}, //ITU709
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5}, //DEFAULT / ITU601 / ITU624 / SMPTE 170M
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5}, //DEFAULT / ITU601 / ITU624 / SMPTE 170M
    {0.59  , 0.11  , 0.30  , -0.331, 0.5, -0.169, -0.421, -0.079, 0.5}, //FCC
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5}, //DEFAULT / ITU601 / ITU624 / SMPTE 170M
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5}, //DEFAULT / ITU601 / ITU624 / SMPTE 170M
    {0.701 , 0.087 , 0.212 , -0.384, 0.5, -0.116, -0.445, -0.055, 0.5}, //SMPTE 240M
};

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

static av_always_inline void
yuv2yuvX16_c_template(const int16_t *lumFilter, const int16_t **lumSrc,
                      int lumFilterSize, const int16_t *chrFilter,
                      const int16_t **chrUSrc, const int16_t **chrVSrc,
                      int chrFilterSize, const int16_t **alpSrc,
                      uint16_t *dest, uint16_t *uDest, uint16_t *vDest,
                      uint16_t *aDest, int dstW, int chrDstW,
                      int big_endian, int output_bits)
{
    //FIXME Optimize (just quickly written not optimized..)
    int i;
    int shift = 11 + 16 - output_bits;

#define output_pixel(pos, val) \
    if (big_endian) { \
        if (output_bits == 16) { \
            AV_WB16(pos, av_clip_uint16(val >> shift)); \
        } else { \
            AV_WB16(pos, av_clip_uintp2(val >> shift, output_bits)); \
        } \
    } else { \
        if (output_bits == 16) { \
            AV_WL16(pos, av_clip_uint16(val >> shift)); \
        } else { \
            AV_WL16(pos, av_clip_uintp2(val >> shift, output_bits)); \
        } \
    }
    for (i = 0; i < dstW; i++) {
        int val = 1 << (26-output_bits);
        int j;

        for (j = 0; j < lumFilterSize; j++)
            val += lumSrc[j][i] * lumFilter[j];

        output_pixel(&dest[i], val);
    }

    if (uDest) {
        for (i = 0; i < chrDstW; i++) {
            int u = 1 << (26-output_bits);
            int v = 1 << (26-output_bits);
            int j;

            for (j = 0; j < chrFilterSize; j++) {
                u += chrUSrc[j][i] * chrFilter[j];
                v += chrVSrc[j][i] * chrFilter[j];
            }

            output_pixel(&uDest[i], u);
            output_pixel(&vDest[i], v);
        }
    }

    if (CONFIG_SWSCALE_ALPHA && aDest) {
        for (i = 0; i < dstW; i++) {
            int val = 1 << (26-output_bits);
            int j;

            for (j = 0; j < lumFilterSize; j++)
                val += alpSrc[j][i] * lumFilter[j];

            output_pixel(&aDest[i], val);
        }
    }
#undef output_pixel
}

#define yuv2NBPS(bits, BE_LE, is_be) \
static void yuv2yuvX ## bits ## BE_LE ## _c(SwsContext *c, const int16_t *lumFilter, \
                              const int16_t **lumSrc, int lumFilterSize, \
                              const int16_t *chrFilter, const int16_t **chrUSrc, \
                              const int16_t **chrVSrc, \
                              int chrFilterSize, const int16_t **alpSrc, \
                              uint8_t *_dest, uint8_t *_uDest, uint8_t *_vDest, \
                              uint8_t *_aDest, int dstW, int chrDstW) \
{ \
    uint16_t *dest  = (uint16_t *) _dest,  *uDest = (uint16_t *) _uDest, \
             *vDest = (uint16_t *) _vDest, *aDest = (uint16_t *) _aDest; \
    yuv2yuvX16_c_template(lumFilter, lumSrc, lumFilterSize, \
                          chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                          alpSrc, \
                          dest, uDest, vDest, aDest, \
                          dstW, chrDstW, is_be, bits); \
}
yuv2NBPS( 9, BE, 1);
yuv2NBPS( 9, LE, 0);
yuv2NBPS(10, BE, 1);
yuv2NBPS(10, LE, 0);
yuv2NBPS(16, BE, 1);
yuv2NBPS(16, LE, 0);

static void yuv2yuvX_c(SwsContext *c, const int16_t *lumFilter,
                       const int16_t **lumSrc, int lumFilterSize,
                       const int16_t *chrFilter, const int16_t **chrUSrc,
                       const int16_t **chrVSrc,
                       int chrFilterSize, const int16_t **alpSrc,
                       uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                       uint8_t *aDest, int dstW, int chrDstW)
{
    //FIXME Optimize (just quickly written not optimized..)
    int i;
    for (i=0; i<dstW; i++) {
        int val=1<<18;
        int j;
        for (j=0; j<lumFilterSize; j++)
            val += lumSrc[j][i] * lumFilter[j];

        dest[i]= av_clip_uint8(val>>19);
    }

    if (uDest)
        for (i=0; i<chrDstW; i++) {
            int u=1<<18;
            int v=1<<18;
            int j;
            for (j=0; j<chrFilterSize; j++) {
                u += chrUSrc[j][i] * chrFilter[j];
                v += chrVSrc[j][i] * chrFilter[j];
            }

            uDest[i]= av_clip_uint8(u>>19);
            vDest[i]= av_clip_uint8(v>>19);
        }

    if (CONFIG_SWSCALE_ALPHA && aDest)
        for (i=0; i<dstW; i++) {
            int val=1<<18;
            int j;
            for (j=0; j<lumFilterSize; j++)
                val += alpSrc[j][i] * lumFilter[j];

            aDest[i]= av_clip_uint8(val>>19);
        }
}

static void yuv2yuv1_c(SwsContext *c, const int16_t *lumSrc,
                       const int16_t *chrUSrc, const int16_t *chrVSrc,
                       const int16_t *alpSrc,
                       uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                       uint8_t *aDest, int dstW, int chrDstW)
{
    int i;
    for (i=0; i<dstW; i++) {
        int val= (lumSrc[i]+64)>>7;
        dest[i]= av_clip_uint8(val);
    }

    if (uDest)
        for (i=0; i<chrDstW; i++) {
            int u=(chrUSrc[i]+64)>>7;
            int v=(chrVSrc[i]+64)>>7;
            uDest[i]= av_clip_uint8(u);
            vDest[i]= av_clip_uint8(v);
        }

    if (CONFIG_SWSCALE_ALPHA && aDest)
        for (i=0; i<dstW; i++) {
            int val= (alpSrc[i]+64)>>7;
            aDest[i]= av_clip_uint8(val);
        }
}

static void yuv2nv12X_c(SwsContext *c, const int16_t *lumFilter,
                        const int16_t **lumSrc, int lumFilterSize,
                        const int16_t *chrFilter, const int16_t **chrUSrc,
                        const int16_t **chrVSrc, int chrFilterSize,
                        const int16_t **alpSrc, uint8_t *dest, uint8_t *uDest,
                        uint8_t *vDest, uint8_t *aDest,
                        int dstW, int chrDstW)
{
    enum PixelFormat dstFormat = c->dstFormat;

    //FIXME Optimize (just quickly written not optimized..)
    int i;
    for (i=0; i<dstW; i++) {
        int val=1<<18;
        int j;
        for (j=0; j<lumFilterSize; j++)
            val += lumSrc[j][i] * lumFilter[j];

        dest[i]= av_clip_uint8(val>>19);
    }

    if (!uDest)
        return;

    if (dstFormat == PIX_FMT_NV12)
        for (i=0; i<chrDstW; i++) {
            int u=1<<18;
            int v=1<<18;
            int j;
            for (j=0; j<chrFilterSize; j++) {
                u += chrUSrc[j][i] * chrFilter[j];
                v += chrVSrc[j][i] * chrFilter[j];
            }

            uDest[2*i]= av_clip_uint8(u>>19);
            uDest[2*i+1]= av_clip_uint8(v>>19);
        }
    else
        for (i=0; i<chrDstW; i++) {
            int u=1<<18;
            int v=1<<18;
            int j;
            for (j=0; j<chrFilterSize; j++) {
                u += chrUSrc[j][i] * chrFilter[j];
                v += chrVSrc[j][i] * chrFilter[j];
            }

            uDest[2*i]= av_clip_uint8(v>>19);
            uDest[2*i+1]= av_clip_uint8(u>>19);
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
        const int i2 = 2 * i;

        for (j = 0; j < lumFilterSize; j++) {
            Y1 += lumSrc[j][i2]   * lumFilter[j];
            Y2 += lumSrc[j][i2+1] * lumFilter[j];
        }
        Y1 >>= 11;
        Y2 >>= 11;
        if ((Y1 | Y2) & 0x10000) {
            Y1 = av_clip_uint16(Y1);
            Y2 = av_clip_uint16(Y2);
        }
        output_pixel(&dest[2 * i2 + 0], Y1);
        output_pixel(&dest[2 * i2 + 2], Y2);
    }
}

static av_always_inline void
yuv2gray16_2_c_template(SwsContext *c, const uint16_t *buf0,
                        const uint16_t *buf1, const uint16_t *ubuf0,
                        const uint16_t *ubuf1, const uint16_t *vbuf0,
                        const uint16_t *vbuf1, const uint16_t *abuf0,
                        const uint16_t *abuf1, uint8_t *dest, int dstW,
                        int yalpha, int uvalpha, int y,
                        enum PixelFormat target)
{
    int  yalpha1 = 4095 - yalpha; \
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        const int i2 = 2 * i;
        int Y1 = (buf0[i2  ] * yalpha1 + buf1[i2  ] * yalpha) >> 11;
        int Y2 = (buf0[i2+1] * yalpha1 + buf1[i2+1] * yalpha) >> 11;

        output_pixel(&dest[2 * i2 + 0], Y1);
        output_pixel(&dest[2 * i2 + 2], Y2);
    }
}

static av_always_inline void
yuv2gray16_1_c_template(SwsContext *c, const uint16_t *buf0,
                        const uint16_t *ubuf0, const uint16_t *ubuf1,
                        const uint16_t *vbuf0, const uint16_t *vbuf1,
                        const uint16_t *abuf0, uint8_t *dest, int dstW,
                        int uvalpha, enum PixelFormat dstFormat,
                        int flags, int y, enum PixelFormat target)
{
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        const int i2 = 2 * i;
        int Y1 = buf0[i2  ] << 1;
        int Y2 = buf0[i2+1] << 1;

        output_pixel(&dest[2 * i2 + 0], Y1);
        output_pixel(&dest[2 * i2 + 2], Y2);
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
static void name ## ext ## _2_c(SwsContext *c, const uint16_t *buf0, \
                        const uint16_t *buf1, const uint16_t *ubuf0, \
                        const uint16_t *ubuf1, const uint16_t *vbuf0, \
                        const uint16_t *vbuf1, const uint16_t *abuf0, \
                        const uint16_t *abuf1, uint8_t *dest, int dstW, \
                        int yalpha, int uvalpha, int y) \
{ \
    name ## base ## _2_c_template(c, buf0, buf1, ubuf0, ubuf1, \
                          vbuf0, vbuf1, abuf0, abuf1, \
                          dest, dstW, yalpha, uvalpha, y, fmt); \
} \
 \
static void name ## ext ## _1_c(SwsContext *c, const uint16_t *buf0, \
                        const uint16_t *ubuf0, const uint16_t *ubuf1, \
                        const uint16_t *vbuf0, const uint16_t *vbuf1, \
                        const uint16_t *abuf0, uint8_t *dest, int dstW, \
                        int uvalpha, enum PixelFormat dstFormat, \
                        int flags, int y) \
{ \
    name ## base ## _1_c_template(c, buf0, ubuf0, ubuf1, vbuf0, \
                          vbuf1, abuf0, dest, dstW, uvalpha, \
                          dstFormat, flags, y, fmt); \
}

YUV2PACKEDWRAPPER(yuv2gray16,, LE, PIX_FMT_GRAY16LE);
YUV2PACKEDWRAPPER(yuv2gray16,, BE, PIX_FMT_GRAY16BE);

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
yuv2mono_2_c_template(SwsContext *c, const uint16_t *buf0,
                      const uint16_t *buf1, const uint16_t *ubuf0,
                      const uint16_t *ubuf1, const uint16_t *vbuf0,
                      const uint16_t *vbuf1, const uint16_t *abuf0,
                      const uint16_t *abuf1, uint8_t *dest, int dstW,
                      int yalpha, int uvalpha, int y,
                      enum PixelFormat target)
{
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
yuv2mono_1_c_template(SwsContext *c, const uint16_t *buf0,
                      const uint16_t *ubuf0, const uint16_t *ubuf1,
                      const uint16_t *vbuf0, const uint16_t *vbuf1,
                      const uint16_t *abuf0, uint8_t *dest, int dstW,
                      int uvalpha, enum PixelFormat dstFormat,
                      int flags, int y, enum PixelFormat target)
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
yuv2422_2_c_template(SwsContext *c, const uint16_t *buf0,
                     const uint16_t *buf1, const uint16_t *ubuf0,
                     const uint16_t *ubuf1, const uint16_t *vbuf0,
                     const uint16_t *vbuf1, const uint16_t *abuf0,
                     const uint16_t *abuf1, uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum PixelFormat target)
{
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
yuv2422_1_c_template(SwsContext *c, const uint16_t *buf0,
                     const uint16_t *ubuf0, const uint16_t *ubuf1,
                     const uint16_t *vbuf0, const uint16_t *vbuf1,
                     const uint16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, enum PixelFormat dstFormat,
                     int flags, int y, enum PixelFormat target)
{
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

#define r_b ((target == PIX_FMT_RGB48LE || target == PIX_FMT_RGB48BE) ? r : b)
#define b_r ((target == PIX_FMT_RGB48LE || target == PIX_FMT_RGB48BE) ? b : r)

static av_always_inline void
yuv2rgb48_X_c_template(SwsContext *c, const int16_t *lumFilter,
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
        const uint8_t *r, *g, *b;

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

        /* FIXME fix tables so that clipping is not needed and then use _NOCLIP*/
        r = (const uint8_t *) c->table_rV[V];
        g = (const uint8_t *)(c->table_gU[U] + c->table_gV[V]);
        b = (const uint8_t *) c->table_bU[U];

        dest[ 0] = dest[ 1] = r_b[Y1];
        dest[ 2] = dest[ 3] =   g[Y1];
        dest[ 4] = dest[ 5] = b_r[Y1];
        dest[ 6] = dest[ 7] = r_b[Y2];
        dest[ 8] = dest[ 9] =   g[Y2];
        dest[10] = dest[11] = b_r[Y2];
        dest += 12;
    }
}

static av_always_inline void
yuv2rgb48_2_c_template(SwsContext *c, const uint16_t *buf0,
                       const uint16_t *buf1, const uint16_t *ubuf0,
                       const uint16_t *ubuf1, const uint16_t *vbuf0,
                       const uint16_t *vbuf1, const uint16_t *abuf0,
                       const uint16_t *abuf1, uint8_t *dest, int dstW,
                       int yalpha, int uvalpha, int y,
                       enum PixelFormat target)
{
    int  yalpha1 = 4095 - yalpha;
    int uvalpha1 = 4095 - uvalpha;
    int i;

    for (i = 0; i < (dstW >> 1); i++) {
        int Y1 = (buf0[i * 2]     * yalpha1  + buf1[i * 2]     * yalpha)  >> 19;
        int Y2 = (buf0[i * 2 + 1] * yalpha1  + buf1[i * 2 + 1] * yalpha)  >> 19;
        int U  = (ubuf0[i]        * uvalpha1 + ubuf1[i]        * uvalpha) >> 19;
        int V  = (vbuf0[i]        * uvalpha1 + vbuf1[i]        * uvalpha) >> 19;
        const uint8_t *r = (const uint8_t *) c->table_rV[V],
                      *g = (const uint8_t *)(c->table_gU[U] + c->table_gV[V]),
                      *b = (const uint8_t *) c->table_bU[U];

        dest[ 0] = dest[ 1] = r_b[Y1];
        dest[ 2] = dest[ 3] =   g[Y1];
        dest[ 4] = dest[ 5] = b_r[Y1];
        dest[ 6] = dest[ 7] = r_b[Y2];
        dest[ 8] = dest[ 9] =   g[Y2];
        dest[10] = dest[11] = b_r[Y2];
        dest += 12;
    }
}

static av_always_inline void
yuv2rgb48_1_c_template(SwsContext *c, const uint16_t *buf0,
                       const uint16_t *ubuf0, const uint16_t *ubuf1,
                       const uint16_t *vbuf0, const uint16_t *vbuf1,
                       const uint16_t *abuf0, uint8_t *dest, int dstW,
                       int uvalpha, enum PixelFormat dstFormat,
                       int flags, int y, enum PixelFormat target)
{
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < (dstW >> 1); i++) {
            int Y1 = buf0[i * 2]     >> 7;
            int Y2 = buf0[i * 2 + 1] >> 7;
            int U  = ubuf1[i]        >> 7;
            int V  = vbuf1[i]        >> 7;
            const uint8_t *r = (const uint8_t *) c->table_rV[V],
                          *g = (const uint8_t *)(c->table_gU[U] + c->table_gV[V]),
                          *b = (const uint8_t *) c->table_bU[U];

            dest[ 0] = dest[ 1] = r_b[Y1];
            dest[ 2] = dest[ 3] =   g[Y1];
            dest[ 4] = dest[ 5] = b_r[Y1];
            dest[ 6] = dest[ 7] = r_b[Y2];
            dest[ 8] = dest[ 9] =   g[Y2];
            dest[10] = dest[11] = b_r[Y2];
            dest += 12;
        }
    } else {
        for (i = 0; i < (dstW >> 1); i++) {
            int Y1 =  buf0[i * 2]          >> 7;
            int Y2 =  buf0[i * 2 + 1]      >> 7;
            int U  = (ubuf0[i] + ubuf1[i]) >> 8;
            int V  = (vbuf0[i] + vbuf1[i]) >> 8;
            const uint8_t *r = (const uint8_t *) c->table_rV[V],
                          *g = (const uint8_t *)(c->table_gU[U] + c->table_gV[V]),
                          *b = (const uint8_t *) c->table_bU[U];

            dest[ 0] = dest[ 1] = r_b[Y1];
            dest[ 2] = dest[ 3] =   g[Y1];
            dest[ 4] = dest[ 5] = b_r[Y1];
            dest[ 6] = dest[ 7] = r_b[Y2];
            dest[ 8] = dest[ 9] =   g[Y2];
            dest[10] = dest[11] = b_r[Y2];
            dest += 12;
        }
    }
}

#undef r_b
#undef b_r

YUV2PACKEDWRAPPER(yuv2, rgb48, rgb48be, PIX_FMT_RGB48BE);
//YUV2PACKEDWRAPPER(yuv2, rgb48, rgb48le, PIX_FMT_RGB48LE);
YUV2PACKEDWRAPPER(yuv2, rgb48, bgr48be, PIX_FMT_BGR48BE);
//YUV2PACKEDWRAPPER(yuv2, rgb48, bgr48le, PIX_FMT_BGR48LE);

#define YSCALE_YUV_2_RGBX_C(type,alpha) \
    for (i=0; i<(dstW>>1); i++) {\
        int j;\
        int Y1 = 1<<18;\
        int Y2 = 1<<18;\
        int U  = 1<<18;\
        int V  = 1<<18;\
        int av_unused A1, A2;\
        type av_unused *r, *b, *g;\
        const int i2= 2*i;\
        \
        for (j=0; j<lumFilterSize; j++) {\
            Y1 += lumSrc[j][i2] * lumFilter[j];\
            Y2 += lumSrc[j][i2+1] * lumFilter[j];\
        }\
        for (j=0; j<chrFilterSize; j++) {\
            U += chrUSrc[j][i] * chrFilter[j];\
            V += chrVSrc[j][i] * chrFilter[j];\
        }\
        Y1>>=19;\
        Y2>>=19;\
        U >>=19;\
        V >>=19;\
        if ((Y1|Y2|U|V)&0x100) {\
            Y1 = av_clip_uint8(Y1); \
            Y2 = av_clip_uint8(Y2); \
            U  = av_clip_uint8(U); \
            V  = av_clip_uint8(V); \
        }\
        if (alpha) {\
            A1 = 1<<18;\
            A2 = 1<<18;\
            for (j=0; j<lumFilterSize; j++) {\
                A1 += alpSrc[j][i2  ] * lumFilter[j];\
                A2 += alpSrc[j][i2+1] * lumFilter[j];\
            }\
            A1>>=19;\
            A2>>=19;\
            if ((A1|A2)&0x100) {\
                A1 = av_clip_uint8(A1); \
                A2 = av_clip_uint8(A2); \
            }\
        }\
        /* FIXME fix tables so that clipping is not needed and then use _NOCLIP*/\
    r = (type *)c->table_rV[V];   \
    g = (type *)(c->table_gU[U] + c->table_gV[V]); \
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_RGBX_FULL_C(rnd,alpha) \
    for (i=0; i<dstW; i++) {\
        int j;\
        int Y = 0;\
        int U = -128<<19;\
        int V = -128<<19;\
        int av_unused A;\
        int R,G,B;\
        \
        for (j=0; j<lumFilterSize; j++) {\
            Y += lumSrc[j][i     ] * lumFilter[j];\
        }\
        for (j=0; j<chrFilterSize; j++) {\
            U += chrUSrc[j][i] * chrFilter[j];\
            V += chrVSrc[j][i] * chrFilter[j];\
        }\
        Y >>=10;\
        U >>=10;\
        V >>=10;\
        if (alpha) {\
            A = rnd;\
            for (j=0; j<lumFilterSize; j++)\
                A += alpSrc[j][i     ] * lumFilter[j];\
            A >>=19;\
            if (A&0x100)\
                A = av_clip_uint8(A);\
        }\
        Y-= c->yuv2rgb_y_offset;\
        Y*= c->yuv2rgb_y_coeff;\
        Y+= rnd;\
        R= Y + V*c->yuv2rgb_v2r_coeff;\
        G= Y + V*c->yuv2rgb_v2g_coeff + U*c->yuv2rgb_u2g_coeff;\
        B= Y +                          U*c->yuv2rgb_u2b_coeff;\
        if ((R|G|B)&(0xC0000000)) {\
            R = av_clip_uintp2(R, 30); \
            G = av_clip_uintp2(G, 30); \
            B = av_clip_uintp2(B, 30); \
        }

#define YSCALE_YUV_2_RGB2_C(type,alpha) \
    for (i=0; i<(dstW>>1); i++) { \
        const int i2= 2*i;       \
        int Y1= (buf0[i2  ]*yalpha1+buf1[i2  ]*yalpha)>>19;           \
        int Y2= (buf0[i2+1]*yalpha1+buf1[i2+1]*yalpha)>>19;           \
        int U= (ubuf0[i]*uvalpha1+ubuf1[i]*uvalpha)>>19;              \
        int V= (vbuf0[i]*uvalpha1+vbuf1[i]*uvalpha)>>19;              \
        type av_unused *r, *b, *g;                                    \
        int av_unused A1, A2;                                         \
        if (alpha) {\
            A1= (abuf0[i2  ]*yalpha1+abuf1[i2  ]*yalpha)>>19;         \
            A2= (abuf0[i2+1]*yalpha1+abuf1[i2+1]*yalpha)>>19;         \
        }\
    r = (type *)c->table_rV[V];\
    g = (type *)(c->table_gU[U] + c->table_gV[V]);\
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_RGB1_C(type,alpha) \
    for (i=0; i<(dstW>>1); i++) {\
        const int i2= 2*i;\
        int Y1= buf0[i2  ]>>7;\
        int Y2= buf0[i2+1]>>7;\
        int U= (ubuf1[i])>>7;\
        int V= (vbuf1[i])>>7;\
        type av_unused *r, *b, *g;\
        int av_unused A1, A2;\
        if (alpha) {\
            A1= abuf0[i2  ]>>7;\
            A2= abuf0[i2+1]>>7;\
        }\
    r = (type *)c->table_rV[V];\
    g = (type *)(c->table_gU[U] + c->table_gV[V]);\
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_RGB1B_C(type,alpha) \
    for (i=0; i<(dstW>>1); i++) {\
        const int i2= 2*i;\
        int Y1= buf0[i2  ]>>7;\
        int Y2= buf0[i2+1]>>7;\
        int U= (ubuf0[i] + ubuf1[i])>>8;\
        int V= (vbuf0[i] + vbuf1[i])>>8;\
        type av_unused *r, *b, *g;\
        int av_unused A1, A2;\
        if (alpha) {\
            A1= abuf0[i2  ]>>7;\
            A2= abuf0[i2+1]>>7;\
        }\
    r = (type *)c->table_rV[V];\
    g = (type *)(c->table_gU[U] + c->table_gV[V]);\
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_ANYRGB_C(func)\
    switch(c->dstFormat) {\
    case PIX_FMT_RGBA:\
    case PIX_FMT_BGRA:\
        if (CONFIG_SMALL) {\
            int needAlpha = CONFIG_SWSCALE_ALPHA && c->alpPixBuf;\
            func(uint32_t,needAlpha)\
                ((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1] + (needAlpha ? (A1<<24) : 0);\
                ((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2] + (needAlpha ? (A2<<24) : 0);\
            }\
        } else {\
            if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {\
                func(uint32_t,1)\
                    ((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1] + (A1<<24);\
                    ((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2] + (A2<<24);\
                }\
            } else {\
                func(uint32_t,0)\
                    ((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1];\
                    ((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2];\
                }\
            }\
        }\
        break;\
    case PIX_FMT_ARGB:\
    case PIX_FMT_ABGR:\
        if (CONFIG_SMALL) {\
            int needAlpha = CONFIG_SWSCALE_ALPHA && c->alpPixBuf;\
            func(uint32_t,needAlpha)\
                ((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1] + (needAlpha ? A1 : 0);\
                ((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2] + (needAlpha ? A2 : 0);\
            }\
        } else {\
            if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {\
                func(uint32_t,1)\
                    ((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1] + A1;\
                    ((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2] + A2;\
                }\
            } else {\
                func(uint32_t,0)\
                    ((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1];\
                    ((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2];\
                }\
            }\
        }                \
        break;\
    case PIX_FMT_RGB24:\
        func(uint8_t,0)\
            ((uint8_t*)dest)[0]= r[Y1];\
            ((uint8_t*)dest)[1]= g[Y1];\
            ((uint8_t*)dest)[2]= b[Y1];\
            ((uint8_t*)dest)[3]= r[Y2];\
            ((uint8_t*)dest)[4]= g[Y2];\
            ((uint8_t*)dest)[5]= b[Y2];\
            dest+=6;\
        }\
        break;\
    case PIX_FMT_BGR24:\
        func(uint8_t,0)\
            ((uint8_t*)dest)[0]= b[Y1];\
            ((uint8_t*)dest)[1]= g[Y1];\
            ((uint8_t*)dest)[2]= r[Y1];\
            ((uint8_t*)dest)[3]= b[Y2];\
            ((uint8_t*)dest)[4]= g[Y2];\
            ((uint8_t*)dest)[5]= r[Y2];\
            dest+=6;\
        }\
        break;\
    case PIX_FMT_RGB565:\
    case PIX_FMT_BGR565:\
        {\
            const int dr1= dither_2x2_8[y&1    ][0];\
            const int dg1= dither_2x2_4[y&1    ][0];\
            const int db1= dither_2x2_8[(y&1)^1][0];\
            const int dr2= dither_2x2_8[y&1    ][1];\
            const int dg2= dither_2x2_4[y&1    ][1];\
            const int db2= dither_2x2_8[(y&1)^1][1];\
            func(uint16_t,0)\
                ((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];\
                ((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];\
            }\
        }\
        break;\
    case PIX_FMT_RGB555:\
    case PIX_FMT_BGR555:\
        {\
            const int dr1= dither_2x2_8[y&1    ][0];\
            const int dg1= dither_2x2_8[y&1    ][1];\
            const int db1= dither_2x2_8[(y&1)^1][0];\
            const int dr2= dither_2x2_8[y&1    ][1];\
            const int dg2= dither_2x2_8[y&1    ][0];\
            const int db2= dither_2x2_8[(y&1)^1][1];\
            func(uint16_t,0)\
                ((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];\
                ((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];\
            }\
        }\
        break;\
    case PIX_FMT_RGB444:\
    case PIX_FMT_BGR444:\
        {\
            const int dr1= dither_4x4_16[y&3    ][0];\
            const int dg1= dither_4x4_16[y&3    ][1];\
            const int db1= dither_4x4_16[(y&3)^3][0];\
            const int dr2= dither_4x4_16[y&3    ][1];\
            const int dg2= dither_4x4_16[y&3    ][0];\
            const int db2= dither_4x4_16[(y&3)^3][1];\
            func(uint16_t,0)\
                ((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];\
                ((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];\
            }\
        }\
        break;\
    case PIX_FMT_RGB8:\
    case PIX_FMT_BGR8:\
        {\
            const uint8_t * const d64= dither_8x8_73[y&7];\
            const uint8_t * const d32= dither_8x8_32[y&7];\
            func(uint8_t,0)\
                ((uint8_t*)dest)[i2+0]= r[Y1+d32[(i2+0)&7]] + g[Y1+d32[(i2+0)&7]] + b[Y1+d64[(i2+0)&7]];\
                ((uint8_t*)dest)[i2+1]= r[Y2+d32[(i2+1)&7]] + g[Y2+d32[(i2+1)&7]] + b[Y2+d64[(i2+1)&7]];\
            }\
        }\
        break;\
    case PIX_FMT_RGB4:\
    case PIX_FMT_BGR4:\
        {\
            const uint8_t * const d64= dither_8x8_73 [y&7];\
            const uint8_t * const d128=dither_8x8_220[y&7];\
            func(uint8_t,0)\
                ((uint8_t*)dest)[i]= r[Y1+d128[(i2+0)&7]] + g[Y1+d64[(i2+0)&7]] + b[Y1+d128[(i2+0)&7]]\
                                 + ((r[Y2+d128[(i2+1)&7]] + g[Y2+d64[(i2+1)&7]] + b[Y2+d128[(i2+1)&7]])<<4);\
            }\
        }\
        break;\
    case PIX_FMT_RGB4_BYTE:\
    case PIX_FMT_BGR4_BYTE:\
        {\
            const uint8_t * const d64= dither_8x8_73 [y&7];\
            const uint8_t * const d128=dither_8x8_220[y&7];\
            func(uint8_t,0)\
                ((uint8_t*)dest)[i2+0]= r[Y1+d128[(i2+0)&7]] + g[Y1+d64[(i2+0)&7]] + b[Y1+d128[(i2+0)&7]];\
                ((uint8_t*)dest)[i2+1]= r[Y2+d128[(i2+1)&7]] + g[Y2+d64[(i2+1)&7]] + b[Y2+d128[(i2+1)&7]];\
            }\
        }\
        break;\
    }

static void yuv2packedX_c(SwsContext *c, const int16_t *lumFilter,
                          const int16_t **lumSrc, int lumFilterSize,
                          const int16_t *chrFilter, const int16_t **chrUSrc,
                          const int16_t **chrVSrc, int chrFilterSize,
                          const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int i;
    YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGBX_C)
}

static void yuv2rgbX_c_full(SwsContext *c, const int16_t *lumFilter,
                            const int16_t **lumSrc, int lumFilterSize,
                            const int16_t *chrFilter, const int16_t **chrUSrc,
                            const int16_t **chrVSrc, int chrFilterSize,
                            const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int i;
    int step= c->dstFormatBpp/8;
    int aidx= 3;

    switch(c->dstFormat) {
    case PIX_FMT_ARGB:
        dest++;
        aidx= 0;
    case PIX_FMT_RGB24:
        aidx--;
    case PIX_FMT_RGBA:
        if (CONFIG_SMALL) {
            int needAlpha = CONFIG_SWSCALE_ALPHA && c->alpPixBuf;
            YSCALE_YUV_2_RGBX_FULL_C(1<<21, needAlpha)
                dest[aidx]= needAlpha ? A : 255;
                dest[0]= R>>22;
                dest[1]= G>>22;
                dest[2]= B>>22;
                dest+= step;
            }
        } else {
            if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
                YSCALE_YUV_2_RGBX_FULL_C(1<<21, 1)
                    dest[aidx]= A;
                    dest[0]= R>>22;
                    dest[1]= G>>22;
                    dest[2]= B>>22;
                    dest+= step;
                }
            } else {
                YSCALE_YUV_2_RGBX_FULL_C(1<<21, 0)
                    dest[aidx]= 255;
                    dest[0]= R>>22;
                    dest[1]= G>>22;
                    dest[2]= B>>22;
                    dest+= step;
                }
            }
        }
        break;
    case PIX_FMT_ABGR:
        dest++;
        aidx= 0;
    case PIX_FMT_BGR24:
        aidx--;
    case PIX_FMT_BGRA:
        if (CONFIG_SMALL) {
            int needAlpha = CONFIG_SWSCALE_ALPHA && c->alpPixBuf;
            YSCALE_YUV_2_RGBX_FULL_C(1<<21, needAlpha)
                dest[aidx]= needAlpha ? A : 255;
                dest[0]= B>>22;
                dest[1]= G>>22;
                dest[2]= R>>22;
                dest+= step;
            }
        } else {
            if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
                YSCALE_YUV_2_RGBX_FULL_C(1<<21, 1)
                    dest[aidx]= A;
                    dest[0]= B>>22;
                    dest[1]= G>>22;
                    dest[2]= R>>22;
                    dest+= step;
                }
            } else {
                YSCALE_YUV_2_RGBX_FULL_C(1<<21, 0)
                    dest[aidx]= 255;
                    dest[0]= B>>22;
                    dest[1]= G>>22;
                    dest[2]= R>>22;
                    dest+= step;
                }
            }
        }
        break;
    default:
        assert(0);
    }
}

/**
 * vertical bilinear scale YV12 to RGB
 */
static void yuv2packed2_c(SwsContext *c, const uint16_t *buf0,
                          const uint16_t *buf1, const uint16_t *ubuf0,
                          const uint16_t *ubuf1, const uint16_t *vbuf0,
                          const uint16_t *vbuf1, const uint16_t *abuf0,
                          const uint16_t *abuf1, uint8_t *dest, int dstW,
                          int yalpha, int uvalpha, int y)
{
    int  yalpha1=4095- yalpha;
    int uvalpha1=4095-uvalpha;
    int i;

    YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB2_C)
}

/**
 * YV12 to RGB without scaling or interpolating
 */
static void yuv2packed1_c(SwsContext *c, const uint16_t *buf0,
                          const uint16_t *ubuf0, const uint16_t *ubuf1,
                          const uint16_t *vbuf0, const uint16_t *vbuf1,
                          const uint16_t *abuf0, uint8_t *dest, int dstW,
                          int uvalpha, enum PixelFormat dstFormat,
                          int flags, int y)
{
    int i;

    if (uvalpha < 2048) {
        YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB1_C)
    } else {
        YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB1B_C)
    }
}

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
rgb48ToY_c_template(uint8_t *dst, const uint8_t *src, int width,
                    enum PixelFormat origin)
{
    int i;
    for (i = 0; i < width; i++) {
        int r_b = input_pixel(&src[i*6+0]) >> 8;
        int   g = input_pixel(&src[i*6+2]) >> 8;
        int b_r = input_pixel(&src[i*6+4]) >> 8;

        dst[i] = (RY*r + GY*g + BY*b + (33<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb48ToUV_c_template(uint8_t *dstU, uint8_t *dstV,
                    const uint8_t *src1, const uint8_t *src2,
                    int width, enum PixelFormat origin)
{
    int i;
    assert(src1==src2);
    for (i = 0; i < width; i++) {
        int r_b = input_pixel(&src1[i*6+0]) >> 8;
        int   g = input_pixel(&src1[i*6+2]) >> 8;
        int b_r = input_pixel(&src1[i*6+4]) >> 8;

        dstU[i] = (RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb48ToUV_half_c_template(uint8_t *dstU, uint8_t *dstV,
                          const uint8_t *src1, const uint8_t *src2,
                          int width, enum PixelFormat origin)
{
    int i;
    assert(src1==src2);
    for (i = 0; i < width; i++) {
        int r_b = (input_pixel(&src1[12*i + 0]) >> 8) + (input_pixel(&src1[12*i + 6]) >> 8);
        int   g = (input_pixel(&src1[12*i + 2]) >> 8) + (input_pixel(&src1[12*i + 8]) >> 8);
        int b_r = (input_pixel(&src1[12*i + 4]) >> 8) + (input_pixel(&src1[12*i + 10]) >> 8);

        dstU[i]= (RU*r + GU*g + BU*b + (257<<RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT+1);
        dstV[i]= (RV*r + GV*g + BV*b + (257<<RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT+1);
    }
}

#undef r
#undef b
#undef input_pixel

#define rgb48funcs(pattern, BE_LE, origin) \
static void pattern ## 48 ## BE_LE ## ToY_c(uint8_t *dst, const uint8_t *src, \
                                    int width, uint32_t *unused) \
{ \
    rgb48ToY_c_template(dst, src, width, origin); \
} \
 \
static void pattern ## 48 ## BE_LE ## ToUV_c(uint8_t *dstU, uint8_t *dstV, \
                                    const uint8_t *src1, const uint8_t *src2, \
                                    int width, uint32_t *unused) \
{ \
    rgb48ToUV_c_template(dstU, dstV, src1, src2, width, origin); \
} \
 \
static void pattern ## 48 ## BE_LE ## ToUV_half_c(uint8_t *dstU, uint8_t *dstV, \
                                    const uint8_t *src1, const uint8_t *src2, \
                                    int width, uint32_t *unused) \
{ \
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
rgb16_32ToY_c_template(uint8_t *dst, const uint8_t *src,
                       int width, enum PixelFormat origin,
                       int shr,   int shg,   int shb, int shp,
                       int maskr, int maskg, int maskb,
                       int rsh,   int gsh,   int bsh, int S)
{
    const int ry = RY << rsh, gy = GY << gsh, by = BY << bsh,
              rnd = 33 << (S - 1);
    int i;

    for (i = 0; i < width; i++) {
        int px = input_pixel(i) >> shp;
        int b = (px & maskb) >> shb;
        int g = (px & maskg) >> shg;
        int r = (px & maskr) >> shr;

        dst[i] = (ry * r + gy * g + by * b + rnd) >> S;
    }
}

static av_always_inline void
rgb16_32ToUV_c_template(uint8_t *dstU, uint8_t *dstV,
                        const uint8_t *src, int width,
                        enum PixelFormat origin,
                        int shr,   int shg,   int shb, int shp,
                        int maskr, int maskg, int maskb,
                        int rsh,   int gsh,   int bsh, int S)
{
    const int ru = RU << rsh, gu = GU << gsh, bu = BU << bsh,
              rv = RV << rsh, gv = GV << gsh, bv = BV << bsh,
              rnd = 257 << (S - 1);
    int i;

    for (i = 0; i < width; i++) {
        int px = input_pixel(i) >> shp;
        int b = (px & maskb) >> shb;
        int g = (px & maskg) >> shg;
        int r = (px & maskr) >> shr;

        dstU[i] = (ru * r + gu * g + bu * b + rnd) >> S;
        dstV[i] = (rv * r + gv * g + bv * b + rnd) >> S;
    }
}

static av_always_inline void
rgb16_32ToUV_half_c_template(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src, int width,
                             enum PixelFormat origin,
                             int shr,   int shg,   int shb, int shp,
                             int maskr, int maskg, int maskb,
                             int rsh,   int gsh,   int bsh, int S)
{
    const int ru = RU << rsh, gu = GU << gsh, bu = BU << bsh,
              rv = RV << rsh, gv = GV << gsh, bv = BV << bsh,
              rnd = 257 << S, maskgx = ~(maskr | maskb);
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

        dstU[i] = (ru * r + gu * g + bu * b + rnd) >> (S + 1);
        dstV[i] = (rv * r + gv * g + bv * b + rnd) >> (S + 1);
    }
}

#undef input_pixel

#define rgb16_32_wrapper(fmt, name, shr, shg, shb, shp, maskr, \
                         maskg, maskb, rsh, gsh, bsh, S) \
static void name ## ToY_c(uint8_t *dst, const uint8_t *src, \
                          int width, uint32_t *unused) \
{ \
    rgb16_32ToY_c_template(dst, src, width, fmt, shr, shg, shb, shp, \
                           maskr, maskg, maskb, rsh, gsh, bsh, S); \
} \
 \
static void name ## ToUV_c(uint8_t *dstU, uint8_t *dstV, \
                           const uint8_t *src, const uint8_t *dummy, \
                           int width, uint32_t *unused) \
{ \
    rgb16_32ToUV_c_template(dstU, dstV, src, width, fmt, shr, shg, shb, shp, \
                            maskr, maskg, maskb, rsh, gsh, bsh, S); \
} \
 \
static void name ## ToUV_half_c(uint8_t *dstU, uint8_t *dstV, \
                                const uint8_t *src, const uint8_t *dummy, \
                                int width, uint32_t *unused) \
{ \
    rgb16_32ToUV_half_c_template(dstU, dstV, src, width, fmt, shr, shg, shb, shp, \
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

static void abgrToA_c(uint8_t *dst, const uint8_t *src, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i];
    }
}

static void rgbaToA_c(uint8_t *dst, const uint8_t *src, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i+3];
    }
}

static void palToY_c(uint8_t *dst, const uint8_t *src, int width, uint32_t *pal)
{
    int i;
    for (i=0; i<width; i++) {
        int d= src[i];

        dst[i]= pal[d] & 0xFF;
    }
}

static void palToUV_c(uint8_t *dstU, uint8_t *dstV,
                      const uint8_t *src1, const uint8_t *src2,
                      int width, uint32_t *pal)
{
    int i;
    assert(src1 == src2);
    for (i=0; i<width; i++) {
        int p= pal[src1[i]];

        dstU[i]= p>>8;
        dstV[i]= p>>16;
    }
}

static void monowhite2Y_c(uint8_t *dst, const uint8_t *src,
                          int width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++) {
        int d= ~src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*255;
    }
}

static void monoblack2Y_c(uint8_t *dst, const uint8_t *src,
                          int width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++) {
        int d= src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*255;
    }
}

//FIXME yuy2* can read up to 7 samples too much

static void yuy2ToY_c(uint8_t *dst, const uint8_t *src, int width,
                      uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i];
}

static void yuy2ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[4*i + 1];
        dstV[i]= src1[4*i + 3];
    }
    assert(src1 == src2);
}

static void LEToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                     const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[2*i + 1];
        dstV[i]= src2[2*i + 1];
    }
}

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src+1, ...) would have 100% unaligned accesses. */
static void uyvyToY_c(uint8_t *dst, const uint8_t *src, int width,
                      uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i+1];
}

static void uyvyToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[4*i + 0];
        dstV[i]= src1[4*i + 2];
    }
    assert(src1 == src2);
}

static void BEToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                     const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[2*i];
        dstV[i]= src2[2*i];
    }
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
                       const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused)
{
    nvXXtoUV_c(dstU, dstV, src1, width);
}

static void nv21ToUV_c(uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused)
{
    nvXXtoUV_c(dstV, dstU, src1, width);
}

#define input_pixel(pos) (isBE(origin) ? AV_RB16(pos) : AV_RL16(pos))

// FIXME Maybe dither instead.
static av_always_inline void
yuv9_OR_10ToUV_c_template(uint8_t *dstU, uint8_t *dstV,
                          const uint8_t *_srcU, const uint8_t *_srcV,
                          int width, enum PixelFormat origin, int depth)
{
    int i;
    const uint16_t *srcU = (const uint16_t *) _srcU;
    const uint16_t *srcV = (const uint16_t *) _srcV;

    for (i = 0; i < width; i++) {
        dstU[i] = input_pixel(&srcU[i]) >> (depth - 8);
        dstV[i] = input_pixel(&srcV[i]) >> (depth - 8);
    }
}

static av_always_inline void
yuv9_or_10ToY_c_template(uint8_t *dstY, const uint8_t *_srcY,
                         int width, enum PixelFormat origin, int depth)
{
    int i;
    const uint16_t *srcY = (const uint16_t*)_srcY;

    for (i = 0; i < width; i++)
        dstY[i] = input_pixel(&srcY[i]) >> (depth - 8);
}

#undef input_pixel

#define YUV_NBPS(depth, BE_LE, origin) \
static void BE_LE ## depth ## ToUV_c(uint8_t *dstU, uint8_t *dstV, \
                                     const uint8_t *srcU, const uint8_t *srcV, \
                                     int width, uint32_t *unused) \
{ \
    yuv9_OR_10ToUV_c_template(dstU, dstV, srcU, srcV, width, origin, depth); \
} \
static void BE_LE ## depth ## ToY_c(uint8_t *dstY, const uint8_t *srcY, \
                                    int width, uint32_t *unused) \
{ \
    yuv9_or_10ToY_c_template(dstY, srcY, width, origin, depth); \
}

YUV_NBPS( 9, LE, PIX_FMT_YUV420P9LE);
YUV_NBPS( 9, BE, PIX_FMT_YUV420P9BE);
YUV_NBPS(10, LE, PIX_FMT_YUV420P10LE);
YUV_NBPS(10, BE, PIX_FMT_YUV420P10BE);

static void bgr24ToY_c(uint8_t *dst, const uint8_t *src,
                       int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src[i*3+0];
        int g= src[i*3+1];
        int r= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (33<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
    }
}

static void bgr24ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src1[3*i + 0];
        int g= src1[3*i + 1];
        int r= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
        dstV[i]= (RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
    }
    assert(src1 == src2);
}

static void bgr24ToUV_half_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int r= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
        dstV[i]= (RV*r + GV*g + BV*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
    }
    assert(src1 == src2);
}

static void rgb24ToY_c(uint8_t *dst, const uint8_t *src, int width,
                       uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int r= src[i*3+0];
        int g= src[i*3+1];
        int b= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (33<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
    }
}

static void rgb24ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++) {
        int r= src1[3*i + 0];
        int g= src1[3*i + 1];
        int b= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
        dstV[i]= (RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
    }
}

static void rgb24ToUV_half_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++) {
        int r= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int b= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
        dstV[i]= (RV*r + GV*g + BV*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
    }
}


// bilinear / bicubic scaling
static void hScale_c(int16_t *dst, int dstW, const uint8_t *src,
                     int srcW, int xInc,
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
}

// *** horizontal scale Y line to temp buffer
static av_always_inline void hyscale(SwsContext *c, uint16_t *dst, int dstWidth,
                                     const uint8_t *src, int srcW, int xInc,
                                     const int16_t *hLumFilter,
                                     const int16_t *hLumFilterPos, int hLumFilterSize,
                                     uint8_t *formatConvBuffer,
                                     uint32_t *pal, int isAlpha)
{
    void (*toYV12)(uint8_t *, const uint8_t *, int, uint32_t *) = isAlpha ? c->alpToYV12 : c->lumToYV12;
    void (*convertRange)(int16_t *, int) = isAlpha ? NULL : c->lumConvertRange;

    if (toYV12) {
        toYV12(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }

    if (!c->hyscale_fast) {
        c->hScale(dst, dstWidth, src, srcW, xInc, hLumFilter, hLumFilterPos, hLumFilterSize);
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
}

static av_always_inline void hcscale(SwsContext *c, uint16_t *dst1, uint16_t *dst2, int dstWidth,
                                     const uint8_t *src1, const uint8_t *src2,
                                     int srcW, int xInc, const int16_t *hChrFilter,
                                     const int16_t *hChrFilterPos, int hChrFilterSize,
                                     uint8_t *formatConvBuffer, uint32_t *pal)
{
    if (c->chrToYV12) {
        uint8_t *buf2 = formatConvBuffer + FFALIGN(srcW, 16);
        c->chrToYV12(formatConvBuffer, buf2, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= buf2;
    }

    if (!c->hcscale_fast) {
        c->hScale(dst1, dstWidth, src1, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
        c->hScale(dst2, dstWidth, src2, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    } else { // fast bilinear upscale / crap downscale
        c->hcscale_fast(c, dst1, dst2, dstWidth, src1, src2, srcW, xInc);
    }

    if (c->chrConvertRange)
        c->chrConvertRange(dst1, dst2, dstWidth);
}

static av_always_inline void
find_c_packed_planar_out_funcs(SwsContext *c,
                               yuv2planar1_fn *yuv2yuv1,    yuv2planarX_fn *yuv2yuvX,
                               yuv2packed1_fn *yuv2packed1, yuv2packed2_fn *yuv2packed2,
                               yuv2packedX_fn *yuv2packedX)
{
    enum PixelFormat dstFormat = c->dstFormat;

    if (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21) {
        *yuv2yuvX     = yuv2nv12X_c;
    } else if (is16BPS(dstFormat)) {
        *yuv2yuvX     = isBE(dstFormat) ? yuv2yuvX16BE_c  : yuv2yuvX16LE_c;
    } else if (is9_OR_10BPS(dstFormat)) {
        if (av_pix_fmt_descriptors[dstFormat].comp[0].depth_minus1 == 8) {
            *yuv2yuvX = isBE(dstFormat) ? yuv2yuvX9BE_c :  yuv2yuvX9LE_c;
        } else {
            *yuv2yuvX = isBE(dstFormat) ? yuv2yuvX10BE_c : yuv2yuvX10LE_c;
        }
    } else {
        *yuv2yuv1     = yuv2yuv1_c;
        *yuv2yuvX     = yuv2yuvX_c;
    }
    if(c->flags & SWS_FULL_CHR_H_INT) {
        *yuv2packedX = yuv2rgbX_c_full;
    } else {
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
            //*yuv2packed1 = yuv2rgb48le_1_c;
            //*yuv2packed2 = yuv2rgb48le_2_c;
            //*yuv2packedX = yuv2rgb48le_X_c;
            //break;
        case PIX_FMT_RGB48BE:
            *yuv2packed1 = yuv2rgb48be_1_c;
            *yuv2packed2 = yuv2rgb48be_2_c;
            *yuv2packedX = yuv2rgb48be_X_c;
            break;
        case PIX_FMT_BGR48LE:
            //*yuv2packed1 = yuv2bgr48le_1_c;
            //*yuv2packed2 = yuv2bgr48le_2_c;
            //*yuv2packedX = yuv2bgr48le_X_c;
            //break;
        case PIX_FMT_BGR48BE:
            *yuv2packed1 = yuv2bgr48be_1_c;
            *yuv2packed2 = yuv2bgr48be_2_c;
            *yuv2packedX = yuv2bgr48be_X_c;
            break;
        default:
            *yuv2packed1 = yuv2packed1_c;
            *yuv2packed2 = yuv2packed2_c;
            *yuv2packedX = yuv2packedX_c;
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
    int16_t *vLumFilter= c->vLumFilter;
    int16_t *vChrFilter= c->vChrFilter;
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
    yuv2planar1_fn yuv2yuv1 = c->yuv2yuv1;
    yuv2planarX_fn yuv2yuvX = c->yuv2yuvX;
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

    if (dstStride[0]%8 !=0 || dstStride[1]%8 !=0 || dstStride[2]%8 !=0 || dstStride[3]%8 != 0) {
        static int warnedAlready=0; //FIXME move this into the context perhaps
        if (flags & SWS_PRINT_INFO && !warnedAlready) {
            av_log(c, AV_LOG_WARNING, "Warning: dstStride is not aligned!\n"
                   "         ->cannot do aligned memory accesses anymore\n");
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

    lastDstY= dstY;

    for (;dstY < dstH; dstY++) {
        unsigned char *dest =dst[0]+dstStride[0]*dstY;
        const int chrDstY= dstY>>c->chrDstVSubSample;
        unsigned char *uDest=dst[1]+dstStride[1]*chrDstY;
        unsigned char *vDest=dst[2]+dstStride[2]*chrDstY;
        unsigned char *aDest=(CONFIG_SWSCALE_ALPHA && alpPixBuf) ? dst[3]+dstStride[3]*dstY : NULL;

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
            const uint8_t *src1= src[0]+(lastInLumBuf + 1 - srcSliceY)*srcStride[0];
            const uint8_t *src2= src[3]+(lastInLumBuf + 1 - srcSliceY)*srcStride[3];
            lumBufIndex++;
            assert(lumBufIndex < 2*vLumBufSize);
            assert(lastInLumBuf + 1 - srcSliceY < srcSliceH);
            assert(lastInLumBuf + 1 - srcSliceY >= 0);
            hyscale(c, lumPixBuf[ lumBufIndex ], dstW, src1, srcW, lumXInc,
                    hLumFilter, hLumFilterPos, hLumFilterSize,
                    formatConvBuffer,
                    pal, 0);
            if (CONFIG_SWSCALE_ALPHA && alpPixBuf)
                hyscale(c, alpPixBuf[ lumBufIndex ], dstW, src2, srcW,
                        lumXInc, hLumFilter, hLumFilterPos, hLumFilterSize,
                        formatConvBuffer,
                        pal, 1);
            lastInLumBuf++;
            DEBUG_BUFFERS("\t\tlumBufIndex %d: lastInLumBuf: %d\n",
                               lumBufIndex,    lastInLumBuf);
        }
        while(lastInChrBuf < lastChrSrcY) {
            const uint8_t *src1= src[1]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[1];
            const uint8_t *src2= src[2]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[2];
            chrBufIndex++;
            assert(chrBufIndex < 2*vChrBufSize);
            assert(lastInChrBuf + 1 - chrSrcSliceY < (chrSrcSliceH));
            assert(lastInChrBuf + 1 - chrSrcSliceY >= 0);
            //FIXME replace parameters through context struct (some at least)

            if (c->needs_hcscale)
                hcscale(c, chrUPixBuf[chrBufIndex], chrVPixBuf[chrBufIndex],
                          chrDstW, src1, src2, chrSrcW, chrXInc,
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
        if (dstY >= dstH-2) {
            // hmm looks like we can't use MMX here without overwriting this array's tail
            find_c_packed_planar_out_funcs(c, &yuv2yuv1, &yuv2yuvX,
                                           &yuv2packed1, &yuv2packed2,
                                           &yuv2packedX);
        }

        {
            const int16_t **lumSrcPtr= (const int16_t **) lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            const int16_t **chrUSrcPtr= (const int16_t **) chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **chrVSrcPtr= (const int16_t **) chrVPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **alpSrcPtr= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? (const int16_t **) alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
            if (isPlanarYUV(dstFormat) || dstFormat==PIX_FMT_GRAY8) { //YV12 like
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if ((dstY&chrSkipMask) || isGray(dstFormat)) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
                if (c->yuv2yuv1 && vLumFilterSize == 1 && vChrFilterSize == 1) { // unscaled YV12
                    const int16_t *lumBuf = lumSrcPtr[0];
                    const int16_t *chrUBuf= chrUSrcPtr[0];
                    const int16_t *chrVBuf= chrVSrcPtr[0];
                    const int16_t *alpBuf= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? alpSrcPtr[0] : NULL;
                    yuv2yuv1(c, lumBuf, chrUBuf, chrVBuf, alpBuf, dest,
                                uDest, vDest, aDest, dstW, chrDstW);
                } else { //General YV12
                    yuv2yuvX(c,
                                vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                                vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr,
                                chrVSrcPtr, vChrFilterSize,
                                alpSrcPtr, dest, uDest, vDest, aDest, dstW, chrDstW);
                }
            } else {
                assert(lumSrcPtr  + vLumFilterSize - 1 < lumPixBuf  + vLumBufSize*2);
                assert(chrUSrcPtr + vChrFilterSize - 1 < chrUPixBuf + vChrBufSize*2);
                if (c->yuv2packed1 && vLumFilterSize == 1 && vChrFilterSize == 2) { //unscaled RGB
                    int chrAlpha= vChrFilter[2*dstY+1];
                    yuv2packed1(c, *lumSrcPtr, *chrUSrcPtr, *(chrUSrcPtr+1),
                                   *chrVSrcPtr, *(chrVSrcPtr+1),
                                   alpPixBuf ? *alpSrcPtr : NULL,
                                   dest, dstW, chrAlpha, dstFormat, flags, dstY);
                } else if (c->yuv2packed2 && vLumFilterSize == 2 && vChrFilterSize == 2) { //bilinear upscale RGB
                    int lumAlpha= vLumFilter[2*dstY+1];
                    int chrAlpha= vChrFilter[2*dstY+1];
                    lumMmxFilter[2]=
                    lumMmxFilter[3]= vLumFilter[2*dstY   ]*0x10001;
                    chrMmxFilter[2]=
                    chrMmxFilter[3]= vChrFilter[2*chrDstY]*0x10001;
                    yuv2packed2(c, *lumSrcPtr, *(lumSrcPtr+1), *chrUSrcPtr, *(chrUSrcPtr+1),
                                   *chrVSrcPtr, *(chrVSrcPtr+1),
                                   alpPixBuf ? *alpSrcPtr : NULL, alpPixBuf ? *(alpSrcPtr+1) : NULL,
                                   dest, dstW, lumAlpha, chrAlpha, dstY);
                } else { //general RGB
                    yuv2packedX(c,
                                   vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                   vChrFilter+dstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                   alpSrcPtr, dest, dstW, dstY);
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

    find_c_packed_planar_out_funcs(c, &c->yuv2yuv1, &c->yuv2yuvX,
                                   &c->yuv2packed1, &c->yuv2packed2,
                                   &c->yuv2packedX);

    c->hScale       = hScale_c;

    if (c->flags & SWS_FAST_BILINEAR) {
        c->hyscale_fast = hyscale_fast_c;
        c->hcscale_fast = hcscale_fast_c;
    }

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
        case PIX_FMT_YUV444P9BE:
        case PIX_FMT_YUV420P9BE: c->chrToYV12 = BE9ToUV_c; break;
        case PIX_FMT_YUV444P9LE:
        case PIX_FMT_YUV420P9LE: c->chrToYV12 = LE9ToUV_c; break;
        case PIX_FMT_YUV444P10BE:
        case PIX_FMT_YUV422P10BE:
        case PIX_FMT_YUV420P10BE: c->chrToYV12 = BE10ToUV_c; break;
        case PIX_FMT_YUV422P10LE:
        case PIX_FMT_YUV444P10LE:
        case PIX_FMT_YUV420P10LE: c->chrToYV12 = LE10ToUV_c; break;
        case PIX_FMT_YUV420P16BE:
        case PIX_FMT_YUV422P16BE:
        case PIX_FMT_YUV444P16BE: c->chrToYV12 = BEToUV_c; break;
        case PIX_FMT_YUV420P16LE:
        case PIX_FMT_YUV422P16LE:
        case PIX_FMT_YUV444P16LE: c->chrToYV12 = LEToUV_c; break;
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
        }
    }

    c->lumToYV12 = NULL;
    c->alpToYV12 = NULL;
    switch (srcFormat) {
    case PIX_FMT_YUV444P9BE:
    case PIX_FMT_YUV420P9BE: c->lumToYV12 = BE9ToY_c; break;
    case PIX_FMT_YUV444P9LE:
    case PIX_FMT_YUV420P9LE: c->lumToYV12 = LE9ToY_c; break;
    case PIX_FMT_YUV444P10BE:
    case PIX_FMT_YUV422P10BE:
    case PIX_FMT_YUV420P10BE: c->lumToYV12 = BE10ToY_c; break;
    case PIX_FMT_YUV444P10LE:
    case PIX_FMT_YUV422P10LE:
    case PIX_FMT_YUV420P10LE: c->lumToYV12 = LE10ToY_c; break;
    case PIX_FMT_YUYV422  :
    case PIX_FMT_YUV420P16BE:
    case PIX_FMT_YUV422P16BE:
    case PIX_FMT_YUV444P16BE:
    case PIX_FMT_Y400A    :
    case PIX_FMT_GRAY16BE : c->lumToYV12 = yuy2ToY_c; break;
    case PIX_FMT_UYVY422  :
    case PIX_FMT_YUV420P16LE:
    case PIX_FMT_YUV422P16LE:
    case PIX_FMT_YUV444P16LE:
    case PIX_FMT_GRAY16LE : c->lumToYV12 = uyvyToY_c;    break;
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
    }
    if (c->alpPixBuf) {
        switch (srcFormat) {
        case PIX_FMT_BGRA:
        case PIX_FMT_RGBA:  c->alpToYV12 = rgbaToA_c; break;
        case PIX_FMT_ABGR:
        case PIX_FMT_ARGB:  c->alpToYV12 = abgrToA_c; break;
        case PIX_FMT_Y400A: c->alpToYV12 = uyvyToY_c; break;
        }
    }

    if (c->srcRange != c->dstRange && !isAnyRGB(c->dstFormat)) {
        if (c->srcRange) {
            c->lumConvertRange = lumRangeFromJpeg_c;
            c->chrConvertRange = chrRangeFromJpeg_c;
        } else {
            c->lumConvertRange = lumRangeToJpeg_c;
            c->chrConvertRange = chrRangeToJpeg_c;
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
