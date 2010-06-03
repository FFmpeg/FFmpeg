/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/intreadwrite.h"
#include "libavutil/x86_cpu.h"
#include "libavutil/avutil.h"
#include "libavutil/bswap.h"
#include "libavutil/pixdesc.h"

#undef MOVNTQ
#undef PAVGB

//#undef HAVE_MMX2
//#define HAVE_AMD3DNOW
//#undef HAVE_MMX
//#undef ARCH_X86
#define DITHER1XBPP

#define FAST_BGR2YV12 // use 7 bit coefficients instead of 15 bit

#ifdef M_PI
#define PI M_PI
#else
#define PI 3.14159265358979323846
#endif

#define isPacked(x)         (       \
           (x)==PIX_FMT_PAL8        \
        || (x)==PIX_FMT_YUYV422     \
        || (x)==PIX_FMT_UYVY422     \
        || isAnyRGB(x)              \
    )

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
    {0.7152, 0.0722, 0.2126, -0.386, 0.5, -0.115, -0.454, -0.046, 0.5},
    {0.7152, 0.0722, 0.2126, -0.386, 0.5, -0.115, -0.454, -0.046, 0.5},
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5},
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5},
    {0.59  , 0.11  , 0.30  , -0.331, 0.5, -0.169, -0.421, -0.079, 0.5}, //FCC
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5},
    {0.587 , 0.114 , 0.299 , -0.331, 0.5, -0.169, -0.419, -0.081, 0.5}, //SMPTE 170M
    {0.701 , 0.087 , 0.212 , -0.384, 0.5  -0.116, -0.445, -0.055, 0.5}, //SMPTE 240M
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

#if ARCH_X86
DECLARE_ASM_CONST(8, uint64_t, bF8)=       0xF8F8F8F8F8F8F8F8LL;
DECLARE_ASM_CONST(8, uint64_t, bFC)=       0xFCFCFCFCFCFCFCFCLL;
DECLARE_ASM_CONST(8, uint64_t, w10)=       0x0010001000100010LL;
DECLARE_ASM_CONST(8, uint64_t, w02)=       0x0002000200020002LL;
DECLARE_ASM_CONST(8, uint64_t, bm00001111)=0x00000000FFFFFFFFLL;
DECLARE_ASM_CONST(8, uint64_t, bm00000111)=0x0000000000FFFFFFLL;
DECLARE_ASM_CONST(8, uint64_t, bm11111000)=0xFFFFFFFFFF000000LL;
DECLARE_ASM_CONST(8, uint64_t, bm01010101)=0x00FF00FF00FF00FFLL;

const DECLARE_ALIGNED(8, uint64_t, ff_dither4)[2] = {
        0x0103010301030103LL,
        0x0200020002000200LL,};

const DECLARE_ALIGNED(8, uint64_t, ff_dither8)[2] = {
        0x0602060206020602LL,
        0x0004000400040004LL,};

DECLARE_ASM_CONST(8, uint64_t, b16Mask)=   0x001F001F001F001FLL;
DECLARE_ASM_CONST(8, uint64_t, g16Mask)=   0x07E007E007E007E0LL;
DECLARE_ASM_CONST(8, uint64_t, r16Mask)=   0xF800F800F800F800LL;
DECLARE_ASM_CONST(8, uint64_t, b15Mask)=   0x001F001F001F001FLL;
DECLARE_ASM_CONST(8, uint64_t, g15Mask)=   0x03E003E003E003E0LL;
DECLARE_ASM_CONST(8, uint64_t, r15Mask)=   0x7C007C007C007C00LL;

DECLARE_ALIGNED(8, const uint64_t, ff_M24A)         = 0x00FF0000FF0000FFLL;
DECLARE_ALIGNED(8, const uint64_t, ff_M24B)         = 0xFF0000FF0000FF00LL;
DECLARE_ALIGNED(8, const uint64_t, ff_M24C)         = 0x0000FF0000FF0000LL;

#ifdef FAST_BGR2YV12
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YCoeff)   = 0x000000210041000DULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UCoeff)   = 0x0000FFEEFFDC0038ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2VCoeff)   = 0x00000038FFD2FFF8ULL;
#else
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YCoeff)   = 0x000020E540830C8BULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UCoeff)   = 0x0000ED0FDAC23831ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2VCoeff)   = 0x00003831D0E6F6EAULL;
#endif /* FAST_BGR2YV12 */
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YOffset)  = 0x1010101010101010ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UVOffset) = 0x8080808080808080ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_w1111)        = 0x0001000100010001ULL;

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toY1Coeff) = 0x0C88000040870C88ULL;
DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toY2Coeff) = 0x20DE4087000020DEULL;
DECLARE_ASM_CONST(8, uint64_t, ff_rgb24toY1Coeff) = 0x20DE0000408720DEULL;
DECLARE_ASM_CONST(8, uint64_t, ff_rgb24toY2Coeff) = 0x0C88408700000C88ULL;
DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toYOffset) = 0x0008400000084000ULL;

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toUV)[2][4] = {
    {0x38380000DAC83838ULL, 0xECFFDAC80000ECFFULL, 0xF6E40000D0E3F6E4ULL, 0x3838D0E300003838ULL},
    {0xECFF0000DAC8ECFFULL, 0x3838DAC800003838ULL, 0x38380000D0E33838ULL, 0xF6E4D0E30000F6E4ULL},
};

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toUVOffset)= 0x0040400000404000ULL;

#endif /* ARCH_X86 */

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

static av_always_inline void yuv2yuvX16inC_template(const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                                                    const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                                                    const int16_t **alpSrc, uint16_t *dest, uint16_t *uDest, uint16_t *vDest, uint16_t *aDest,
                                                    int dstW, int chrDstW, int big_endian)
{
    //FIXME Optimize (just quickly written not optimized..)
    int i;

    for (i = 0; i < dstW; i++) {
        int val = 1 << 10;
        int j;

        for (j = 0; j < lumFilterSize; j++)
            val += lumSrc[j][i] * lumFilter[j];

        if (big_endian) {
            AV_WB16(&dest[i], av_clip_uint16(val >> 11));
        } else {
            AV_WL16(&dest[i], av_clip_uint16(val >> 11));
        }
    }

    if (uDest) {
        for (i = 0; i < chrDstW; i++) {
            int u = 1 << 10;
            int v = 1 << 10;
            int j;

            for (j = 0; j < chrFilterSize; j++) {
                u += chrSrc[j][i       ] * chrFilter[j];
                v += chrSrc[j][i + VOFW] * chrFilter[j];
            }

            if (big_endian) {
                AV_WB16(&uDest[i], av_clip_uint16(u >> 11));
                AV_WB16(&vDest[i], av_clip_uint16(v >> 11));
            } else {
                AV_WL16(&uDest[i], av_clip_uint16(u >> 11));
                AV_WL16(&vDest[i], av_clip_uint16(v >> 11));
            }
        }
    }

    if (CONFIG_SWSCALE_ALPHA && aDest) {
        for (i = 0; i < dstW; i++) {
            int val = 1 << 10;
            int j;

            for (j = 0; j < lumFilterSize; j++)
                val += alpSrc[j][i] * lumFilter[j];

            if (big_endian) {
                AV_WB16(&aDest[i], av_clip_uint16(val >> 11));
            } else {
                AV_WL16(&aDest[i], av_clip_uint16(val >> 11));
            }
        }
    }
}

static inline void yuv2yuvX16inC(const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                                 const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                                 const int16_t **alpSrc, uint16_t *dest, uint16_t *uDest, uint16_t *vDest, uint16_t *aDest, int dstW, int chrDstW,
                                 enum PixelFormat dstFormat)
{
    if (isBE(dstFormat)) {
        yuv2yuvX16inC_template(lumFilter, lumSrc, lumFilterSize,
                               chrFilter, chrSrc, chrFilterSize,
                               alpSrc,
                               dest, uDest, vDest, aDest,
                               dstW, chrDstW, 1);
    } else {
        yuv2yuvX16inC_template(lumFilter, lumSrc, lumFilterSize,
                               chrFilter, chrSrc, chrFilterSize,
                               alpSrc,
                               dest, uDest, vDest, aDest,
                               dstW, chrDstW, 0);
    }
}

static inline void yuv2yuvXinC(const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                               const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                               const int16_t **alpSrc, uint8_t *dest, uint8_t *uDest, uint8_t *vDest, uint8_t *aDest, int dstW, int chrDstW)
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
                u += chrSrc[j][i] * chrFilter[j];
                v += chrSrc[j][i + VOFW] * chrFilter[j];
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

static inline void yuv2nv12XinC(const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                                const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                                uint8_t *dest, uint8_t *uDest, int dstW, int chrDstW, int dstFormat)
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

    if (!uDest)
        return;

    if (dstFormat == PIX_FMT_NV12)
        for (i=0; i<chrDstW; i++) {
            int u=1<<18;
            int v=1<<18;
            int j;
            for (j=0; j<chrFilterSize; j++) {
                u += chrSrc[j][i] * chrFilter[j];
                v += chrSrc[j][i + VOFW] * chrFilter[j];
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
                u += chrSrc[j][i] * chrFilter[j];
                v += chrSrc[j][i + VOFW] * chrFilter[j];
            }

            uDest[2*i]= av_clip_uint8(v>>19);
            uDest[2*i+1]= av_clip_uint8(u>>19);
        }
}

#define YSCALE_YUV_2_PACKEDX_NOCLIP_C(type,alpha) \
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
            U += chrSrc[j][i] * chrFilter[j];\
            V += chrSrc[j][i+VOFW] * chrFilter[j];\
        }\
        Y1>>=19;\
        Y2>>=19;\
        U >>=19;\
        V >>=19;\
        if (alpha) {\
            A1 = 1<<18;\
            A2 = 1<<18;\
            for (j=0; j<lumFilterSize; j++) {\
                A1 += alpSrc[j][i2  ] * lumFilter[j];\
                A2 += alpSrc[j][i2+1] * lumFilter[j];\
            }\
            A1>>=19;\
            A2>>=19;\
        }

#define YSCALE_YUV_2_PACKEDX_C(type,alpha) \
        YSCALE_YUV_2_PACKEDX_NOCLIP_C(type,alpha)\
        if ((Y1|Y2|U|V)&256) {\
            if (Y1>255)   Y1=255; \
            else if (Y1<0)Y1=0;   \
            if (Y2>255)   Y2=255; \
            else if (Y2<0)Y2=0;   \
            if (U>255)    U=255;  \
            else if (U<0) U=0;    \
            if (V>255)    V=255;  \
            else if (V<0) V=0;    \
        }\
        if (alpha && ((A1|A2)&256)) {\
            A1=av_clip_uint8(A1);\
            A2=av_clip_uint8(A2);\
        }

#define YSCALE_YUV_2_PACKEDX_FULL_C(rnd,alpha) \
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
            U += chrSrc[j][i     ] * chrFilter[j];\
            V += chrSrc[j][i+VOFW] * chrFilter[j];\
        }\
        Y >>=10;\
        U >>=10;\
        V >>=10;\
        if (alpha) {\
            A = rnd;\
            for (j=0; j<lumFilterSize; j++)\
                A += alpSrc[j][i     ] * lumFilter[j];\
            A >>=19;\
            if (A&256)\
                A = av_clip_uint8(A);\
        }

#define YSCALE_YUV_2_RGBX_FULL_C(rnd,alpha) \
    YSCALE_YUV_2_PACKEDX_FULL_C(rnd>>3,alpha)\
        Y-= c->yuv2rgb_y_offset;\
        Y*= c->yuv2rgb_y_coeff;\
        Y+= rnd;\
        R= Y + V*c->yuv2rgb_v2r_coeff;\
        G= Y + V*c->yuv2rgb_v2g_coeff + U*c->yuv2rgb_u2g_coeff;\
        B= Y +                          U*c->yuv2rgb_u2b_coeff;\
        if ((R|G|B)&(0xC0000000)) {\
            if (R>=(256<<22))   R=(256<<22)-1; \
            else if (R<0)R=0;   \
            if (G>=(256<<22))   G=(256<<22)-1; \
            else if (G<0)G=0;   \
            if (B>=(256<<22))   B=(256<<22)-1; \
            else if (B<0)B=0;   \
        }

#define YSCALE_YUV_2_GRAY16_C \
    for (i=0; i<(dstW>>1); i++) {\
        int j;\
        int Y1 = 1<<18;\
        int Y2 = 1<<18;\
        int U  = 1<<18;\
        int V  = 1<<18;\
        \
        const int i2= 2*i;\
        \
        for (j=0; j<lumFilterSize; j++) {\
            Y1 += lumSrc[j][i2] * lumFilter[j];\
            Y2 += lumSrc[j][i2+1] * lumFilter[j];\
        }\
        Y1>>=11;\
        Y2>>=11;\
        if ((Y1|Y2|U|V)&65536) {\
            if (Y1>65535)   Y1=65535; \
            else if (Y1<0)Y1=0;   \
            if (Y2>65535)   Y2=65535; \
            else if (Y2<0)Y2=0;   \
        }

#define YSCALE_YUV_2_RGBX_C(type,alpha) \
    YSCALE_YUV_2_PACKEDX_C(type,alpha)  /* FIXME fix tables so that clipping is not needed and then use _NOCLIP*/\
    r = (type *)c->table_rV[V];   \
    g = (type *)(c->table_gU[U] + c->table_gV[V]); \
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_PACKED2_C(type,alpha)   \
    for (i=0; i<(dstW>>1); i++) { \
        const int i2= 2*i;       \
        int Y1= (buf0[i2  ]*yalpha1+buf1[i2  ]*yalpha)>>19;           \
        int Y2= (buf0[i2+1]*yalpha1+buf1[i2+1]*yalpha)>>19;           \
        int U= (uvbuf0[i     ]*uvalpha1+uvbuf1[i     ]*uvalpha)>>19;  \
        int V= (uvbuf0[i+VOFW]*uvalpha1+uvbuf1[i+VOFW]*uvalpha)>>19;  \
        type av_unused *r, *b, *g;                                    \
        int av_unused A1, A2;                                         \
        if (alpha) {\
            A1= (abuf0[i2  ]*yalpha1+abuf1[i2  ]*yalpha)>>19;         \
            A2= (abuf0[i2+1]*yalpha1+abuf1[i2+1]*yalpha)>>19;         \
        }

#define YSCALE_YUV_2_GRAY16_2_C   \
    for (i=0; i<(dstW>>1); i++) { \
        const int i2= 2*i;       \
        int Y1= (buf0[i2  ]*yalpha1+buf1[i2  ]*yalpha)>>11;           \
        int Y2= (buf0[i2+1]*yalpha1+buf1[i2+1]*yalpha)>>11;

#define YSCALE_YUV_2_RGB2_C(type,alpha) \
    YSCALE_YUV_2_PACKED2_C(type,alpha)\
    r = (type *)c->table_rV[V];\
    g = (type *)(c->table_gU[U] + c->table_gV[V]);\
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_PACKED1_C(type,alpha) \
    for (i=0; i<(dstW>>1); i++) {\
        const int i2= 2*i;\
        int Y1= buf0[i2  ]>>7;\
        int Y2= buf0[i2+1]>>7;\
        int U= (uvbuf1[i     ])>>7;\
        int V= (uvbuf1[i+VOFW])>>7;\
        type av_unused *r, *b, *g;\
        int av_unused A1, A2;\
        if (alpha) {\
            A1= abuf0[i2  ]>>7;\
            A2= abuf0[i2+1]>>7;\
        }

#define YSCALE_YUV_2_GRAY16_1_C \
    for (i=0; i<(dstW>>1); i++) {\
        const int i2= 2*i;\
        int Y1= buf0[i2  ]<<1;\
        int Y2= buf0[i2+1]<<1;

#define YSCALE_YUV_2_RGB1_C(type,alpha) \
    YSCALE_YUV_2_PACKED1_C(type,alpha)\
    r = (type *)c->table_rV[V];\
    g = (type *)(c->table_gU[U] + c->table_gV[V]);\
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_PACKED1B_C(type,alpha) \
    for (i=0; i<(dstW>>1); i++) {\
        const int i2= 2*i;\
        int Y1= buf0[i2  ]>>7;\
        int Y2= buf0[i2+1]>>7;\
        int U= (uvbuf0[i     ] + uvbuf1[i     ])>>8;\
        int V= (uvbuf0[i+VOFW] + uvbuf1[i+VOFW])>>8;\
        type av_unused *r, *b, *g;\
        int av_unused A1, A2;\
        if (alpha) {\
            A1= abuf0[i2  ]>>7;\
            A2= abuf0[i2+1]>>7;\
        }

#define YSCALE_YUV_2_RGB1B_C(type,alpha) \
    YSCALE_YUV_2_PACKED1B_C(type,alpha)\
    r = (type *)c->table_rV[V];\
    g = (type *)(c->table_gU[U] + c->table_gV[V]);\
    b = (type *)c->table_bU[U];

#define YSCALE_YUV_2_MONO2_C \
    const uint8_t * const d128=dither_8x8_220[y&7];\
    uint8_t *g= c->table_gU[128] + c->table_gV[128];\
    for (i=0; i<dstW-7; i+=8) {\
        int acc;\
        acc =       g[((buf0[i  ]*yalpha1+buf1[i  ]*yalpha)>>19) + d128[0]];\
        acc+= acc + g[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19) + d128[1]];\
        acc+= acc + g[((buf0[i+2]*yalpha1+buf1[i+2]*yalpha)>>19) + d128[2]];\
        acc+= acc + g[((buf0[i+3]*yalpha1+buf1[i+3]*yalpha)>>19) + d128[3]];\
        acc+= acc + g[((buf0[i+4]*yalpha1+buf1[i+4]*yalpha)>>19) + d128[4]];\
        acc+= acc + g[((buf0[i+5]*yalpha1+buf1[i+5]*yalpha)>>19) + d128[5]];\
        acc+= acc + g[((buf0[i+6]*yalpha1+buf1[i+6]*yalpha)>>19) + d128[6]];\
        acc+= acc + g[((buf0[i+7]*yalpha1+buf1[i+7]*yalpha)>>19) + d128[7]];\
        ((uint8_t*)dest)[0]= c->dstFormat == PIX_FMT_MONOBLACK ? acc : ~acc;\
        dest++;\
    }

#define YSCALE_YUV_2_MONOX_C \
    const uint8_t * const d128=dither_8x8_220[y&7];\
    uint8_t *g= c->table_gU[128] + c->table_gV[128];\
    int acc=0;\
    for (i=0; i<dstW-1; i+=2) {\
        int j;\
        int Y1=1<<18;\
        int Y2=1<<18;\
\
        for (j=0; j<lumFilterSize; j++) {\
            Y1 += lumSrc[j][i] * lumFilter[j];\
            Y2 += lumSrc[j][i+1] * lumFilter[j];\
        }\
        Y1>>=19;\
        Y2>>=19;\
        if ((Y1|Y2)&256) {\
            if (Y1>255)   Y1=255;\
            else if (Y1<0)Y1=0;\
            if (Y2>255)   Y2=255;\
            else if (Y2<0)Y2=0;\
        }\
        acc+= acc + g[Y1+d128[(i+0)&7]];\
        acc+= acc + g[Y2+d128[(i+1)&7]];\
        if ((i&7)==6) {\
            ((uint8_t*)dest)[0]= c->dstFormat == PIX_FMT_MONOBLACK ? acc : ~acc;\
            dest++;\
        }\
    }

#define YSCALE_YUV_2_ANYRGB_C(func, func2, func_g16, func_monoblack)\
    switch(c->dstFormat) {\
    case PIX_FMT_RGB48BE:\
    case PIX_FMT_RGB48LE:\
        func(uint8_t,0)\
            ((uint8_t*)dest)[ 0]= r[Y1];\
            ((uint8_t*)dest)[ 1]= r[Y1];\
            ((uint8_t*)dest)[ 2]= g[Y1];\
            ((uint8_t*)dest)[ 3]= g[Y1];\
            ((uint8_t*)dest)[ 4]= b[Y1];\
            ((uint8_t*)dest)[ 5]= b[Y1];\
            ((uint8_t*)dest)[ 6]= r[Y2];\
            ((uint8_t*)dest)[ 7]= r[Y2];\
            ((uint8_t*)dest)[ 8]= g[Y2];\
            ((uint8_t*)dest)[ 9]= g[Y2];\
            ((uint8_t*)dest)[10]= b[Y2];\
            ((uint8_t*)dest)[11]= b[Y2];\
            dest+=12;\
        }\
        break;\
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
    case PIX_FMT_RGB565BE:\
    case PIX_FMT_RGB565LE:\
    case PIX_FMT_BGR565BE:\
    case PIX_FMT_BGR565LE:\
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
    case PIX_FMT_RGB555BE:\
    case PIX_FMT_RGB555LE:\
    case PIX_FMT_BGR555BE:\
    case PIX_FMT_BGR555LE:\
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
    case PIX_FMT_RGB444BE:\
    case PIX_FMT_RGB444LE:\
    case PIX_FMT_BGR444BE:\
    case PIX_FMT_BGR444LE:\
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
    case PIX_FMT_MONOBLACK:\
    case PIX_FMT_MONOWHITE:\
        {\
            func_monoblack\
        }\
        break;\
    case PIX_FMT_YUYV422:\
        func2\
            ((uint8_t*)dest)[2*i2+0]= Y1;\
            ((uint8_t*)dest)[2*i2+1]= U;\
            ((uint8_t*)dest)[2*i2+2]= Y2;\
            ((uint8_t*)dest)[2*i2+3]= V;\
        }                \
        break;\
    case PIX_FMT_UYVY422:\
        func2\
            ((uint8_t*)dest)[2*i2+0]= U;\
            ((uint8_t*)dest)[2*i2+1]= Y1;\
            ((uint8_t*)dest)[2*i2+2]= V;\
            ((uint8_t*)dest)[2*i2+3]= Y2;\
        }                \
        break;\
    case PIX_FMT_GRAY16BE:\
        func_g16\
            ((uint8_t*)dest)[2*i2+0]= Y1>>8;\
            ((uint8_t*)dest)[2*i2+1]= Y1;\
            ((uint8_t*)dest)[2*i2+2]= Y2>>8;\
            ((uint8_t*)dest)[2*i2+3]= Y2;\
        }                \
        break;\
    case PIX_FMT_GRAY16LE:\
        func_g16\
            ((uint8_t*)dest)[2*i2+0]= Y1;\
            ((uint8_t*)dest)[2*i2+1]= Y1>>8;\
            ((uint8_t*)dest)[2*i2+2]= Y2;\
            ((uint8_t*)dest)[2*i2+3]= Y2>>8;\
        }                \
        break;\
    }

static inline void yuv2packedXinC(SwsContext *c, const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                                  const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                                  const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    int i;
    YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGBX_C, YSCALE_YUV_2_PACKEDX_C(void,0), YSCALE_YUV_2_GRAY16_C, YSCALE_YUV_2_MONOX_C)
}

static inline void yuv2rgbXinC_full(SwsContext *c, const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                                    const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
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

static void fillPlane(uint8_t* plane, int stride, int width, int height, int y, uint8_t val)
{
    int i;
    uint8_t *ptr = plane + stride*y;
    for (i=0; i<height; i++) {
        memset(ptr, val, width);
        ptr += stride;
    }
}

static inline void rgb48ToY(uint8_t *dst, const uint8_t *src, int width,
                            uint32_t *unused)
{
    int i;
    for (i = 0; i < width; i++) {
        int r = src[i*6+0];
        int g = src[i*6+2];
        int b = src[i*6+4];

        dst[i] = (RY*r + GY*g + BY*b + (33<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static inline void rgb48ToUV(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src1, const uint8_t *src2,
                             int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i = 0; i < width; i++) {
        int r = src1[6*i + 0];
        int g = src1[6*i + 2];
        int b = src1[6*i + 4];

        dstU[i] = (RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static inline void rgb48ToUV_half(uint8_t *dstU, uint8_t *dstV,
                                  const uint8_t *src1, const uint8_t *src2,
                                  int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i = 0; i < width; i++) {
        int r= src1[12*i + 0] + src1[12*i + 6];
        int g= src1[12*i + 2] + src1[12*i + 8];
        int b= src1[12*i + 4] + src1[12*i + 10];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT+1);
        dstV[i]= (RV*r + GV*g + BV*b + (257<<RGB2YUV_SHIFT)) >> (RGB2YUV_SHIFT+1);
    }
}

#define BGR2Y(type, name, shr, shg, shb, maskr, maskg, maskb, RY, GY, BY, S)\
static inline void name(uint8_t *dst, const uint8_t *src, long width, uint32_t *unused)\
{\
    int i;\
    for (i=0; i<width; i++) {\
        int b= (((const type*)src)[i]>>shb)&maskb;\
        int g= (((const type*)src)[i]>>shg)&maskg;\
        int r= (((const type*)src)[i]>>shr)&maskr;\
\
        dst[i]= (((RY)*r + (GY)*g + (BY)*b + (33<<((S)-1)))>>(S));\
    }\
}

BGR2Y(uint32_t, bgr32ToY,16, 0, 0, 0x00FF, 0xFF00, 0x00FF, RY<< 8, GY   , BY<< 8, RGB2YUV_SHIFT+8)
BGR2Y(uint32_t, rgb32ToY, 0, 0,16, 0x00FF, 0xFF00, 0x00FF, RY<< 8, GY   , BY<< 8, RGB2YUV_SHIFT+8)
BGR2Y(uint16_t, bgr16ToY, 0, 0, 0, 0x001F, 0x07E0, 0xF800, RY<<11, GY<<5, BY    , RGB2YUV_SHIFT+8)
BGR2Y(uint16_t, bgr15ToY, 0, 0, 0, 0x001F, 0x03E0, 0x7C00, RY<<10, GY<<5, BY    , RGB2YUV_SHIFT+7)
BGR2Y(uint16_t, rgb16ToY, 0, 0, 0, 0xF800, 0x07E0, 0x001F, RY    , GY<<5, BY<<11, RGB2YUV_SHIFT+8)
BGR2Y(uint16_t, rgb15ToY, 0, 0, 0, 0x7C00, 0x03E0, 0x001F, RY    , GY<<5, BY<<10, RGB2YUV_SHIFT+7)

static inline void abgrToA(uint8_t *dst, const uint8_t *src, long width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i];
    }
}

#define BGR2UV(type, name, shr, shg, shb, maska, maskr, maskg, maskb, RU, GU, BU, RV, GV, BV, S)\
static inline void name(uint8_t *dstU, uint8_t *dstV, const uint8_t *src, const uint8_t *dummy, long width, uint32_t *unused)\
{\
    int i;\
    for (i=0; i<width; i++) {\
        int b= (((const type*)src)[i]&maskb)>>shb;\
        int g= (((const type*)src)[i]&maskg)>>shg;\
        int r= (((const type*)src)[i]&maskr)>>shr;\
\
        dstU[i]= ((RU)*r + (GU)*g + (BU)*b + (257<<((S)-1)))>>(S);\
        dstV[i]= ((RV)*r + (GV)*g + (BV)*b + (257<<((S)-1)))>>(S);\
    }\
}\
static inline void name ## _half(uint8_t *dstU, uint8_t *dstV, const uint8_t *src, const uint8_t *dummy, long width, uint32_t *unused)\
{\
    int i;\
    for (i=0; i<width; i++) {\
        int pix0= ((const type*)src)[2*i+0];\
        int pix1= ((const type*)src)[2*i+1];\
        int g= (pix0&~(maskr|maskb))+(pix1&~(maskr|maskb));\
        int b= ((pix0+pix1-g)&(maskb|(2*maskb)))>>shb;\
        int r= ((pix0+pix1-g)&(maskr|(2*maskr)))>>shr;\
        g&= maskg|(2*maskg);\
\
        g>>=shg;\
\
        dstU[i]= ((RU)*r + (GU)*g + (BU)*b + (257<<(S)))>>((S)+1);\
        dstV[i]= ((RV)*r + (GV)*g + (BV)*b + (257<<(S)))>>((S)+1);\
    }\
}

BGR2UV(uint32_t, bgr32ToUV,16, 0, 0, 0xFF000000, 0xFF0000, 0xFF00,   0x00FF, RU<< 8, GU   , BU<< 8, RV<< 8, GV   , BV<< 8, RGB2YUV_SHIFT+8)
BGR2UV(uint32_t, rgb32ToUV, 0, 0,16, 0xFF000000,   0x00FF, 0xFF00, 0xFF0000, RU<< 8, GU   , BU<< 8, RV<< 8, GV   , BV<< 8, RGB2YUV_SHIFT+8)
BGR2UV(uint16_t, bgr16ToUV, 0, 0, 0,          0,   0x001F, 0x07E0,   0xF800, RU<<11, GU<<5, BU    , RV<<11, GV<<5, BV    , RGB2YUV_SHIFT+8)
BGR2UV(uint16_t, bgr15ToUV, 0, 0, 0,          0,   0x001F, 0x03E0,   0x7C00, RU<<10, GU<<5, BU    , RV<<10, GV<<5, BV    , RGB2YUV_SHIFT+7)
BGR2UV(uint16_t, rgb16ToUV, 0, 0, 0,          0,   0xF800, 0x07E0,   0x001F, RU    , GU<<5, BU<<11, RV    , GV<<5, BV<<11, RGB2YUV_SHIFT+8)
BGR2UV(uint16_t, rgb15ToUV, 0, 0, 0,          0,   0x7C00, 0x03E0,   0x001F, RU    , GU<<5, BU<<10, RV    , GV<<5, BV<<10, RGB2YUV_SHIFT+7)

static inline void palToY(uint8_t *dst, const uint8_t *src, long width, uint32_t *pal)
{
    int i;
    for (i=0; i<width; i++) {
        int d= src[i];

        dst[i]= pal[d] & 0xFF;
    }
}

static inline void palToUV(uint8_t *dstU, uint8_t *dstV,
                           const uint8_t *src1, const uint8_t *src2,
                           long width, uint32_t *pal)
{
    int i;
    assert(src1 == src2);
    for (i=0; i<width; i++) {
        int p= pal[src1[i]];

        dstU[i]= p>>8;
        dstV[i]= p>>16;
    }
}

static inline void monowhite2Y(uint8_t *dst, const uint8_t *src, long width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++) {
        int d= ~src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*255;
    }
}

static inline void monoblack2Y(uint8_t *dst, const uint8_t *src, long width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++) {
        int d= src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*255;
    }
}

//Note: we have C, MMX, MMX2, 3DNOW versions, there is no 3DNOW+MMX2 one
//Plain C versions
#if (!HAVE_MMX && !HAVE_ALTIVEC) || CONFIG_RUNTIME_CPUDETECT
#define COMPILE_C
#endif

#if ARCH_PPC
#if HAVE_ALTIVEC
#define COMPILE_ALTIVEC
#endif
#endif //ARCH_PPC

#if ARCH_X86

#if (HAVE_MMX && !HAVE_AMD3DNOW && !HAVE_MMX2) || CONFIG_RUNTIME_CPUDETECT
#define COMPILE_MMX
#endif

#if HAVE_MMX2 || CONFIG_RUNTIME_CPUDETECT
#define COMPILE_MMX2
#endif

#if (HAVE_AMD3DNOW && !HAVE_MMX2) || CONFIG_RUNTIME_CPUDETECT
#define COMPILE_3DNOW
#endif
#endif //ARCH_X86

#define COMPILE_TEMPLATE_MMX 0
#define COMPILE_TEMPLATE_MMX2 0
#define COMPILE_TEMPLATE_AMD3DNOW 0
#define COMPILE_TEMPLATE_ALTIVEC 0

#ifdef COMPILE_C
#define RENAME(a) a ## _C
#include "swscale_template.c"
#endif

#ifdef COMPILE_ALTIVEC
#undef RENAME
#undef COMPILE_TEMPLATE_ALTIVEC
#define COMPILE_TEMPLATE_ALTIVEC 1
#define RENAME(a) a ## _altivec
#include "swscale_template.c"
#endif

#if ARCH_X86

//MMX versions
#ifdef COMPILE_MMX
#undef RENAME
#undef COMPILE_TEMPLATE_MMX
#undef COMPILE_TEMPLATE_MMX2
#undef COMPILE_TEMPLATE_AMD3DNOW
#define COMPILE_TEMPLATE_MMX 1
#define COMPILE_TEMPLATE_MMX2 0
#define COMPILE_TEMPLATE_AMD3DNOW 0
#define RENAME(a) a ## _MMX
#include "swscale_template.c"
#endif

//MMX2 versions
#ifdef COMPILE_MMX2
#undef RENAME
#undef COMPILE_TEMPLATE_MMX
#undef COMPILE_TEMPLATE_MMX2
#undef COMPILE_TEMPLATE_AMD3DNOW
#define COMPILE_TEMPLATE_MMX 1
#define COMPILE_TEMPLATE_MMX2 1
#define COMPILE_TEMPLATE_AMD3DNOW 0
#define RENAME(a) a ## _MMX2
#include "swscale_template.c"
#endif

//3DNOW versions
#ifdef COMPILE_3DNOW
#undef RENAME
#undef COMPILE_TEMPLATE_MMX
#undef COMPILE_TEMPLATE_MMX2
#undef COMPILE_TEMPLATE_AMD3DNOW
#define COMPILE_TEMPLATE_MMX 1
#define COMPILE_TEMPLATE_MMX2 0
#define COMPILE_TEMPLATE_AMD3DNOW 1
#define RENAME(a) a ## _3DNow
#include "swscale_template.c"
#endif

#endif //ARCH_X86

SwsFunc ff_getSwsFunc(SwsContext *c)
{
#if CONFIG_RUNTIME_CPUDETECT
    int flags = c->flags;

#if ARCH_X86
    // ordered per speed fastest first
    if (flags & SWS_CPU_CAPS_MMX2) {
        sws_init_swScale_MMX2(c);
        return swScale_MMX2;
    } else if (flags & SWS_CPU_CAPS_3DNOW) {
        sws_init_swScale_3DNow(c);
        return swScale_3DNow;
    } else if (flags & SWS_CPU_CAPS_MMX) {
        sws_init_swScale_MMX(c);
        return swScale_MMX;
    } else {
        sws_init_swScale_C(c);
        return swScale_C;
    }

#else
#ifdef COMPILE_ALTIVEC
    if (flags & SWS_CPU_CAPS_ALTIVEC) {
        sws_init_swScale_altivec(c);
        return swScale_altivec;
    } else {
        sws_init_swScale_C(c);
        return swScale_C;
    }
#endif
    sws_init_swScale_C(c);
    return swScale_C;
#endif /* ARCH_X86 */
#else //CONFIG_RUNTIME_CPUDETECT
#if   COMPILE_TEMPLATE_MMX2
    sws_init_swScale_MMX2(c);
    return swScale_MMX2;
#elif COMPILE_TEMPLATE_AMD3DNOW
    sws_init_swScale_3DNow(c);
    return swScale_3DNow;
#elif COMPILE_TEMPLATE_MMX
    sws_init_swScale_MMX(c);
    return swScale_MMX;
#elif COMPILE_TEMPLATE_ALTIVEC
    sws_init_swScale_altivec(c);
    return swScale_altivec;
#else
    sws_init_swScale_C(c);
    return swScale_C;
#endif
#endif //!CONFIG_RUNTIME_CPUDETECT
}

static int planarToNv12Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;
    /* Copy Y plane */
    if (dstStride[0]==srcStride[0] && srcStride[0] > 0)
        memcpy(dst, src[0], srcSliceH*dstStride[0]);
    else {
        int i;
        const uint8_t *srcPtr= src[0];
        uint8_t *dstPtr= dst;
        for (i=0; i<srcSliceH; i++) {
            memcpy(dstPtr, srcPtr, c->srcW);
            srcPtr+= srcStride[0];
            dstPtr+= dstStride[0];
        }
    }
    dst = dstParam[1] + dstStride[1]*srcSliceY/2;
    if (c->dstFormat == PIX_FMT_NV12)
        interleaveBytes(src[1], src[2], dst, c->srcW/2, srcSliceH/2, srcStride[1], srcStride[2], dstStride[0]);
    else
        interleaveBytes(src[2], src[1], dst, c->srcW/2, srcSliceH/2, srcStride[2], srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int planarToYuy2Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;

    yv12toyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0], srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int planarToUyvyWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;

    yv12touyvy(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0], srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int yuv422pToYuy2Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                                int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;

    yuv422ptoyuy2(src[0],src[1],src[2],dst,c->srcW,srcSliceH,srcStride[0],srcStride[1],dstStride[0]);

    return srcSliceH;
}

static int yuv422pToUyvyWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                                int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;

    yuv422ptouyvy(src[0],src[1],src[2],dst,c->srcW,srcSliceH,srcStride[0],srcStride[1],dstStride[0]);

    return srcSliceH;
}

static int yuyvToYuv420Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *ydst=dstParam[0] + dstStride[0]*srcSliceY;
    uint8_t *udst=dstParam[1] + dstStride[1]*srcSliceY/2;
    uint8_t *vdst=dstParam[2] + dstStride[2]*srcSliceY/2;

    yuyvtoyuv420(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0], dstStride[1], srcStride[0]);

    if (dstParam[3])
        fillPlane(dstParam[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);

    return srcSliceH;
}

static int yuyvToYuv422Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *ydst=dstParam[0] + dstStride[0]*srcSliceY;
    uint8_t *udst=dstParam[1] + dstStride[1]*srcSliceY;
    uint8_t *vdst=dstParam[2] + dstStride[2]*srcSliceY;

    yuyvtoyuv422(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0], dstStride[1], srcStride[0]);

    return srcSliceH;
}

static int uyvyToYuv420Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *ydst=dstParam[0] + dstStride[0]*srcSliceY;
    uint8_t *udst=dstParam[1] + dstStride[1]*srcSliceY/2;
    uint8_t *vdst=dstParam[2] + dstStride[2]*srcSliceY/2;

    uyvytoyuv420(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0], dstStride[1], srcStride[0]);

    if (dstParam[3])
        fillPlane(dstParam[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);

    return srcSliceH;
}

static int uyvyToYuv422Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *ydst=dstParam[0] + dstStride[0]*srcSliceY;
    uint8_t *udst=dstParam[1] + dstStride[1]*srcSliceY;
    uint8_t *vdst=dstParam[2] + dstStride[2]*srcSliceY;

    uyvytoyuv422(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0], dstStride[1], srcStride[0]);

    return srcSliceH;
}

static int palToRgbWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                           int srcSliceH, uint8_t* dst[], int dstStride[])
{
    const enum PixelFormat srcFormat= c->srcFormat;
    const enum PixelFormat dstFormat= c->dstFormat;
    void (*conv)(const uint8_t *src, uint8_t *dst, long num_pixels,
                 const uint8_t *palette)=NULL;
    int i;
    uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;
    const uint8_t *srcPtr= src[0];

    if (usePal(srcFormat)) {
        switch (dstFormat) {
        case PIX_FMT_RGB32  : conv = sws_convertPalette8ToPacked32; break;
        case PIX_FMT_BGR32  : conv = sws_convertPalette8ToPacked32; break;
        case PIX_FMT_BGR32_1: conv = sws_convertPalette8ToPacked32; break;
        case PIX_FMT_RGB32_1: conv = sws_convertPalette8ToPacked32; break;
        case PIX_FMT_RGB24  : conv = sws_convertPalette8ToPacked24; break;
        case PIX_FMT_BGR24  : conv = sws_convertPalette8ToPacked24; break;
        }
    }

    if (!conv)
        av_log(c, AV_LOG_ERROR, "internal error %s -> %s converter\n",
               sws_format_name(srcFormat), sws_format_name(dstFormat));
    else {
        for (i=0; i<srcSliceH; i++) {
            conv(srcPtr, dstPtr, c->srcW, (uint8_t *) c->pal_rgb);
            srcPtr+= srcStride[0];
            dstPtr+= dstStride[0];
        }
    }

    return srcSliceH;
}

#define isRGBA32(x) (            \
           (x) == PIX_FMT_ARGB   \
        || (x) == PIX_FMT_RGBA   \
        || (x) == PIX_FMT_BGRA   \
        || (x) == PIX_FMT_ABGR   \
        )

/* {RGB,BGR}{15,16,24,32,32_1} -> {RGB,BGR}{15,16,24,32} */
static int rgbToRgbWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                           int srcSliceH, uint8_t* dst[], int dstStride[])
{
    const enum PixelFormat srcFormat= c->srcFormat;
    const enum PixelFormat dstFormat= c->dstFormat;
    const int srcBpp= (c->srcFormatBpp + 7) >> 3;
    const int dstBpp= (c->dstFormatBpp + 7) >> 3;
    const int srcId= c->srcFormatBpp >> 2; /* 1:0, 4:1, 8:2, 15:3, 16:4, 24:6, 32:8 */
    const int dstId= c->dstFormatBpp >> 2;
    void (*conv)(const uint8_t *src, uint8_t *dst, long src_size)=NULL;

#define CONV_IS(src, dst) (srcFormat == PIX_FMT_##src && dstFormat == PIX_FMT_##dst)

    if (isRGBA32(srcFormat) && isRGBA32(dstFormat)) {
        if (     CONV_IS(ABGR, RGBA)
              || CONV_IS(ARGB, BGRA)
              || CONV_IS(BGRA, ARGB)
              || CONV_IS(RGBA, ABGR)) conv = shuffle_bytes_3210;
        else if (CONV_IS(ABGR, ARGB)
              || CONV_IS(ARGB, ABGR)) conv = shuffle_bytes_0321;
        else if (CONV_IS(ABGR, BGRA)
              || CONV_IS(ARGB, RGBA)) conv = shuffle_bytes_1230;
        else if (CONV_IS(BGRA, RGBA)
              || CONV_IS(RGBA, BGRA)) conv = shuffle_bytes_2103;
        else if (CONV_IS(BGRA, ABGR)
              || CONV_IS(RGBA, ARGB)) conv = shuffle_bytes_3012;
    } else
    /* BGR -> BGR */
    if (  (isBGRinInt(srcFormat) && isBGRinInt(dstFormat))
       || (isRGBinInt(srcFormat) && isRGBinInt(dstFormat))) {
        switch(srcId | (dstId<<4)) {
        case 0x34: conv= rgb16to15; break;
        case 0x36: conv= rgb24to15; break;
        case 0x38: conv= rgb32to15; break;
        case 0x43: conv= rgb15to16; break;
        case 0x46: conv= rgb24to16; break;
        case 0x48: conv= rgb32to16; break;
        case 0x63: conv= rgb15to24; break;
        case 0x64: conv= rgb16to24; break;
        case 0x68: conv= rgb32to24; break;
        case 0x83: conv= rgb15to32; break;
        case 0x84: conv= rgb16to32; break;
        case 0x86: conv= rgb24to32; break;
        }
    } else if (  (isBGRinInt(srcFormat) && isRGBinInt(dstFormat))
             || (isRGBinInt(srcFormat) && isBGRinInt(dstFormat))) {
        switch(srcId | (dstId<<4)) {
        case 0x33: conv= rgb15tobgr15; break;
        case 0x34: conv= rgb16tobgr15; break;
        case 0x36: conv= rgb24tobgr15; break;
        case 0x38: conv= rgb32tobgr15; break;
        case 0x43: conv= rgb15tobgr16; break;
        case 0x44: conv= rgb16tobgr16; break;
        case 0x46: conv= rgb24tobgr16; break;
        case 0x48: conv= rgb32tobgr16; break;
        case 0x63: conv= rgb15tobgr24; break;
        case 0x64: conv= rgb16tobgr24; break;
        case 0x66: conv= rgb24tobgr24; break;
        case 0x68: conv= rgb32tobgr24; break;
        case 0x83: conv= rgb15tobgr32; break;
        case 0x84: conv= rgb16tobgr32; break;
        case 0x86: conv= rgb24tobgr32; break;
        }
    }

    if (!conv) {
        av_log(c, AV_LOG_ERROR, "internal error %s -> %s converter\n",
               sws_format_name(srcFormat), sws_format_name(dstFormat));
    } else {
        const uint8_t *srcPtr= src[0];
              uint8_t *dstPtr= dst[0];
        if ((srcFormat == PIX_FMT_RGB32_1 || srcFormat == PIX_FMT_BGR32_1) && !isRGBA32(dstFormat))
            srcPtr += ALT32_CORR;

        if ((dstFormat == PIX_FMT_RGB32_1 || dstFormat == PIX_FMT_BGR32_1) && !isRGBA32(srcFormat))
            dstPtr += ALT32_CORR;

        if (dstStride[0]*srcBpp == srcStride[0]*dstBpp && srcStride[0] > 0)
            conv(srcPtr, dstPtr + dstStride[0]*srcSliceY, srcSliceH*srcStride[0]);
        else {
            int i;
            dstPtr += dstStride[0]*srcSliceY;

            for (i=0; i<srcSliceH; i++) {
                conv(srcPtr, dstPtr, c->srcW*srcBpp);
                srcPtr+= srcStride[0];
                dstPtr+= dstStride[0];
            }
        }
    }
    return srcSliceH;
}

static int bgr24ToYv12Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                              int srcSliceH, uint8_t* dst[], int dstStride[])
{
    rgb24toyv12(
        src[0],
        dst[0]+ srcSliceY    *dstStride[0],
        dst[1]+(srcSliceY>>1)*dstStride[1],
        dst[2]+(srcSliceY>>1)*dstStride[2],
        c->srcW, srcSliceH,
        dstStride[0], dstStride[1], srcStride[0]);
    if (dst[3])
        fillPlane(dst[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);
    return srcSliceH;
}

static int yvu9ToYv12Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                             int srcSliceH, uint8_t* dst[], int dstStride[])
{
    int i;

    /* copy Y */
    if (srcStride[0]==dstStride[0] && srcStride[0] > 0)
        memcpy(dst[0]+ srcSliceY*dstStride[0], src[0], srcStride[0]*srcSliceH);
    else {
        const uint8_t *srcPtr= src[0];
        uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;

        for (i=0; i<srcSliceH; i++) {
            memcpy(dstPtr, srcPtr, c->srcW);
            srcPtr+= srcStride[0];
            dstPtr+= dstStride[0];
        }
    }

    if (c->dstFormat==PIX_FMT_YUV420P || c->dstFormat==PIX_FMT_YUVA420P) {
        planar2x(src[1], dst[1] + dstStride[1]*(srcSliceY >> 1), c->chrSrcW,
                 srcSliceH >> 2, srcStride[1], dstStride[1]);
        planar2x(src[2], dst[2] + dstStride[2]*(srcSliceY >> 1), c->chrSrcW,
                 srcSliceH >> 2, srcStride[2], dstStride[2]);
    } else {
        planar2x(src[1], dst[2] + dstStride[2]*(srcSliceY >> 1), c->chrSrcW,
                 srcSliceH >> 2, srcStride[1], dstStride[2]);
        planar2x(src[2], dst[1] + dstStride[1]*(srcSliceY >> 1), c->chrSrcW,
                 srcSliceH >> 2, srcStride[2], dstStride[1]);
    }
    if (dst[3])
        fillPlane(dst[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);
    return srcSliceH;
}

/* unscaled copy like stuff (assumes nearly identical formats) */
static int packedCopyWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                             int srcSliceH, uint8_t* dst[], int dstStride[])
{
    if (dstStride[0]==srcStride[0] && srcStride[0] > 0)
        memcpy(dst[0] + dstStride[0]*srcSliceY, src[0], srcSliceH*dstStride[0]);
    else {
        int i;
        const uint8_t *srcPtr= src[0];
        uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;
        int length=0;

        /* universal length finder */
        while(length+c->srcW <= FFABS(dstStride[0])
           && length+c->srcW <= FFABS(srcStride[0])) length+= c->srcW;
        assert(length!=0);

        for (i=0; i<srcSliceH; i++) {
            memcpy(dstPtr, srcPtr, length);
            srcPtr+= srcStride[0];
            dstPtr+= dstStride[0];
        }
    }
    return srcSliceH;
}

static int planarCopyWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                             int srcSliceH, uint8_t* dst[], int dstStride[])
{
    int plane, i, j;
    for (plane=0; plane<4; plane++) {
        int length= (plane==0 || plane==3) ? c->srcW  : -((-c->srcW  )>>c->chrDstHSubSample);
        int y=      (plane==0 || plane==3) ? srcSliceY: -((-srcSliceY)>>c->chrDstVSubSample);
        int height= (plane==0 || plane==3) ? srcSliceH: -((-srcSliceH)>>c->chrDstVSubSample);
        const uint8_t *srcPtr= src[plane];
        uint8_t *dstPtr= dst[plane] + dstStride[plane]*y;

        if (!dst[plane]) continue;
        // ignore palette for GRAY8
        if (plane == 1 && !dst[2]) continue;
        if (!src[plane] || (plane == 1 && !src[2])) {
            if(is16BPS(c->dstFormat))
                length*=2;
            fillPlane(dst[plane], dstStride[plane], length, height, y, (plane==3) ? 255 : 128);
        } else {
            if(is16BPS(c->srcFormat) && !is16BPS(c->dstFormat)) {
                if (!isBE(c->srcFormat)) srcPtr++;
                for (i=0; i<height; i++) {
                    for (j=0; j<length; j++) dstPtr[j] = srcPtr[j<<1];
                    srcPtr+= srcStride[plane];
                    dstPtr+= dstStride[plane];
                }
            } else if(!is16BPS(c->srcFormat) && is16BPS(c->dstFormat)) {
                for (i=0; i<height; i++) {
                    for (j=0; j<length; j++) {
                        dstPtr[ j<<1   ] = srcPtr[j];
                        dstPtr[(j<<1)+1] = srcPtr[j];
                    }
                    srcPtr+= srcStride[plane];
                    dstPtr+= dstStride[plane];
                }
            } else if(is16BPS(c->srcFormat) && is16BPS(c->dstFormat)
                  && isBE(c->srcFormat) != isBE(c->dstFormat)) {

                for (i=0; i<height; i++) {
                    for (j=0; j<length; j++)
                        ((uint16_t*)dstPtr)[j] = bswap_16(((const uint16_t*)srcPtr)[j]);
                    srcPtr+= srcStride[plane];
                    dstPtr+= dstStride[plane];
                }
            } else if (dstStride[plane]==srcStride[plane] && srcStride[plane] > 0)
                memcpy(dst[plane] + dstStride[plane]*y, src[plane], height*dstStride[plane]);
            else {
                if(is16BPS(c->srcFormat) && is16BPS(c->dstFormat))
                    length*=2;
                for (i=0; i<height; i++) {
                    memcpy(dstPtr, srcPtr, length);
                    srcPtr+= srcStride[plane];
                    dstPtr+= dstStride[plane];
                }
            }
        }
    }
    return srcSliceH;
}

int ff_hardcodedcpuflags(void)
{
    int flags = 0;
#if   COMPILE_TEMPLATE_MMX2
    flags |= SWS_CPU_CAPS_MMX|SWS_CPU_CAPS_MMX2;
#elif COMPILE_TEMPLATE_AMD3DNOW
    flags |= SWS_CPU_CAPS_MMX|SWS_CPU_CAPS_3DNOW;
#elif COMPILE_TEMPLATE_MMX
    flags |= SWS_CPU_CAPS_MMX;
#elif COMPILE_TEMPLATE_ALTIVEC
    flags |= SWS_CPU_CAPS_ALTIVEC;
#elif ARCH_BFIN
    flags |= SWS_CPU_CAPS_BFIN;
#endif
    return flags;
}

void ff_get_unscaled_swscale(SwsContext *c)
{
    const enum PixelFormat srcFormat = c->srcFormat;
    const enum PixelFormat dstFormat = c->dstFormat;
    const int flags = c->flags;
    const int dstH = c->dstH;
    int needsDither;

    needsDither= isAnyRGB(dstFormat)
        &&  c->dstFormatBpp < 24
        && (c->dstFormatBpp < c->srcFormatBpp || (!isAnyRGB(srcFormat)));

    /* yv12_to_nv12 */
    if ((srcFormat == PIX_FMT_YUV420P || srcFormat == PIX_FMT_YUVA420P) && (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21)) {
        c->swScale= planarToNv12Wrapper;
    }
    /* yuv2bgr */
    if ((srcFormat==PIX_FMT_YUV420P || srcFormat==PIX_FMT_YUV422P || srcFormat==PIX_FMT_YUVA420P) && isAnyRGB(dstFormat)
        && !(flags & SWS_ACCURATE_RND) && !(dstH&1)) {
        c->swScale= ff_yuv2rgb_get_func_ptr(c);
    }

    if (srcFormat==PIX_FMT_YUV410P && (dstFormat==PIX_FMT_YUV420P || dstFormat==PIX_FMT_YUVA420P) && !(flags & SWS_BITEXACT)) {
        c->swScale= yvu9ToYv12Wrapper;
    }

    /* bgr24toYV12 */
    if (srcFormat==PIX_FMT_BGR24 && (dstFormat==PIX_FMT_YUV420P || dstFormat==PIX_FMT_YUVA420P) && !(flags & SWS_ACCURATE_RND))
        c->swScale= bgr24ToYv12Wrapper;

    /* RGB/BGR -> RGB/BGR (no dither needed forms) */
    if (   isAnyRGB(srcFormat)
        && isAnyRGB(dstFormat)
        && srcFormat != PIX_FMT_BGR8      && dstFormat != PIX_FMT_BGR8
        && srcFormat != PIX_FMT_RGB8      && dstFormat != PIX_FMT_RGB8
        && srcFormat != PIX_FMT_BGR4      && dstFormat != PIX_FMT_BGR4
        && srcFormat != PIX_FMT_RGB4      && dstFormat != PIX_FMT_RGB4
        && srcFormat != PIX_FMT_BGR4_BYTE && dstFormat != PIX_FMT_BGR4_BYTE
        && srcFormat != PIX_FMT_RGB4_BYTE && dstFormat != PIX_FMT_RGB4_BYTE
        && srcFormat != PIX_FMT_MONOBLACK && dstFormat != PIX_FMT_MONOBLACK
        && srcFormat != PIX_FMT_MONOWHITE && dstFormat != PIX_FMT_MONOWHITE
        && srcFormat != PIX_FMT_RGB48LE   && dstFormat != PIX_FMT_RGB48LE
        && srcFormat != PIX_FMT_RGB48BE   && dstFormat != PIX_FMT_RGB48BE
        && (!needsDither || (c->flags&(SWS_FAST_BILINEAR|SWS_POINT))))
        c->swScale= rgbToRgbWrapper;

    if ((usePal(srcFormat) && (
        dstFormat == PIX_FMT_RGB32   ||
        dstFormat == PIX_FMT_RGB32_1 ||
        dstFormat == PIX_FMT_RGB24   ||
        dstFormat == PIX_FMT_BGR32   ||
        dstFormat == PIX_FMT_BGR32_1 ||
        dstFormat == PIX_FMT_BGR24)))
        c->swScale= palToRgbWrapper;

    if (srcFormat == PIX_FMT_YUV422P) {
        if (dstFormat == PIX_FMT_YUYV422)
            c->swScale= yuv422pToYuy2Wrapper;
        else if (dstFormat == PIX_FMT_UYVY422)
            c->swScale= yuv422pToUyvyWrapper;
    }

    /* LQ converters if -sws 0 or -sws 4*/
    if (c->flags&(SWS_FAST_BILINEAR|SWS_POINT)) {
        /* yv12_to_yuy2 */
        if (srcFormat == PIX_FMT_YUV420P || srcFormat == PIX_FMT_YUVA420P) {
            if (dstFormat == PIX_FMT_YUYV422)
                c->swScale= planarToYuy2Wrapper;
            else if (dstFormat == PIX_FMT_UYVY422)
                c->swScale= planarToUyvyWrapper;
        }
    }
    if(srcFormat == PIX_FMT_YUYV422 && (dstFormat == PIX_FMT_YUV420P || dstFormat == PIX_FMT_YUVA420P))
        c->swScale= yuyvToYuv420Wrapper;
    if(srcFormat == PIX_FMT_UYVY422 && (dstFormat == PIX_FMT_YUV420P || dstFormat == PIX_FMT_YUVA420P))
        c->swScale= uyvyToYuv420Wrapper;
    if(srcFormat == PIX_FMT_YUYV422 && dstFormat == PIX_FMT_YUV422P)
        c->swScale= yuyvToYuv422Wrapper;
    if(srcFormat == PIX_FMT_UYVY422 && dstFormat == PIX_FMT_YUV422P)
        c->swScale= uyvyToYuv422Wrapper;

#ifdef COMPILE_ALTIVEC
    if ((c->flags & SWS_CPU_CAPS_ALTIVEC) &&
        !(c->flags & SWS_BITEXACT) &&
        srcFormat == PIX_FMT_YUV420P) {
        // unscaled YV12 -> packed YUV, we want speed
        if (dstFormat == PIX_FMT_YUYV422)
            c->swScale= yv12toyuy2_unscaled_altivec;
        else if (dstFormat == PIX_FMT_UYVY422)
            c->swScale= yv12touyvy_unscaled_altivec;
    }
#endif

    /* simple copy */
    if (  srcFormat == dstFormat
        || (srcFormat == PIX_FMT_YUVA420P && dstFormat == PIX_FMT_YUV420P)
        || (srcFormat == PIX_FMT_YUV420P && dstFormat == PIX_FMT_YUVA420P)
        || (isPlanarYUV(srcFormat) && isGray(dstFormat))
        || (isPlanarYUV(dstFormat) && isGray(srcFormat))
        || (isGray(dstFormat) && isGray(srcFormat))
        || (isPlanarYUV(srcFormat) && isPlanarYUV(dstFormat)
            && c->chrDstHSubSample == c->chrSrcHSubSample
            && c->chrDstVSubSample == c->chrSrcVSubSample
            && dstFormat != PIX_FMT_NV12 && dstFormat != PIX_FMT_NV21
            && srcFormat != PIX_FMT_NV12 && srcFormat != PIX_FMT_NV21))
    {
        if (isPacked(c->srcFormat))
            c->swScale= packedCopyWrapper;
        else /* Planar YUV or gray */
            c->swScale= planarCopyWrapper;
    }
#if ARCH_BFIN
    if (flags & SWS_CPU_CAPS_BFIN)
        ff_bfin_get_unscaled_swscale (c);
#endif
}

static void reset_ptr(const uint8_t* src[], int format)
{
    if(!isALPHA(format))
        src[3]=NULL;
    if(!isPlanarYUV(format)) {
        src[3]=src[2]=NULL;

        if (!usePal(format))
            src[1]= NULL;
    }
}

/**
 * swscale wrapper, so we don't need to export the SwsContext.
 * Assumes planar YUV to be in YUV order instead of YVU.
 */
int sws_scale(SwsContext *c, const uint8_t* const src[], const int srcStride[], int srcSliceY,
              int srcSliceH, uint8_t* const dst[], const int dstStride[])
{
    int i;
    const uint8_t* src2[4]= {src[0], src[1], src[2], src[3]};
    uint8_t* dst2[4]= {dst[0], dst[1], dst[2], dst[3]};

    // do not mess up sliceDir if we have a "trailing" 0-size slice
    if (srcSliceH == 0)
        return 0;

    if (c->sliceDir == 0 && srcSliceY != 0 && srcSliceY + srcSliceH != c->srcH) {
        av_log(c, AV_LOG_ERROR, "Slices start in the middle!\n");
        return 0;
    }
    if (c->sliceDir == 0) {
        if (srcSliceY == 0) c->sliceDir = 1; else c->sliceDir = -1;
    }

    if (usePal(c->srcFormat)) {
        for (i=0; i<256; i++) {
            int p, r, g, b,y,u,v;
            if(c->srcFormat == PIX_FMT_PAL8) {
                p=((const uint32_t*)(src[1]))[i];
                r= (p>>16)&0xFF;
                g= (p>> 8)&0xFF;
                b=  p     &0xFF;
            } else if(c->srcFormat == PIX_FMT_RGB8) {
                r= (i>>5    )*36;
                g= ((i>>2)&7)*36;
                b= (i&3     )*85;
            } else if(c->srcFormat == PIX_FMT_BGR8) {
                b= (i>>6    )*85;
                g= ((i>>3)&7)*36;
                r= (i&7     )*36;
            } else if(c->srcFormat == PIX_FMT_RGB4_BYTE) {
                r= (i>>3    )*255;
                g= ((i>>1)&3)*85;
                b= (i&1     )*255;
            } else if(c->srcFormat == PIX_FMT_GRAY8) {
                r = g = b = i;
            } else {
                assert(c->srcFormat == PIX_FMT_BGR4_BYTE);
                b= (i>>3    )*255;
                g= ((i>>1)&3)*85;
                r= (i&1     )*255;
            }
            y= av_clip_uint8((RY*r + GY*g + BY*b + ( 33<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
            u= av_clip_uint8((RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
            v= av_clip_uint8((RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
            c->pal_yuv[i]= y + (u<<8) + (v<<16);

            switch(c->dstFormat) {
            case PIX_FMT_BGR32:
#if !HAVE_BIGENDIAN
            case PIX_FMT_RGB24:
#endif
                c->pal_rgb[i]=  r + (g<<8) + (b<<16);
                break;
            case PIX_FMT_BGR32_1:
#if HAVE_BIGENDIAN
            case PIX_FMT_BGR24:
#endif
                c->pal_rgb[i]= (r + (g<<8) + (b<<16)) << 8;
                break;
            case PIX_FMT_RGB32_1:
#if HAVE_BIGENDIAN
            case PIX_FMT_RGB24:
#endif
                c->pal_rgb[i]= (b + (g<<8) + (r<<16)) << 8;
                break;
            case PIX_FMT_RGB32:
#if !HAVE_BIGENDIAN
            case PIX_FMT_BGR24:
#endif
            default:
                c->pal_rgb[i]=  b + (g<<8) + (r<<16);
            }
        }
    }

    // copy strides, so they can safely be modified
    if (c->sliceDir == 1) {
        // slices go from top to bottom
        int srcStride2[4]= {srcStride[0], srcStride[1], srcStride[2], srcStride[3]};
        int dstStride2[4]= {dstStride[0], dstStride[1], dstStride[2], dstStride[3]};

        reset_ptr(src2, c->srcFormat);
        reset_ptr((const uint8_t**)dst2, c->dstFormat);

        /* reset slice direction at end of frame */
        if (srcSliceY + srcSliceH == c->srcH)
            c->sliceDir = 0;

        return c->swScale(c, src2, srcStride2, srcSliceY, srcSliceH, dst2, dstStride2);
    } else {
        // slices go from bottom to top => we flip the image internally
        int srcStride2[4]= {-srcStride[0], -srcStride[1], -srcStride[2], -srcStride[3]};
        int dstStride2[4]= {-dstStride[0], -dstStride[1], -dstStride[2], -dstStride[3]};

        src2[0] += (srcSliceH-1)*srcStride[0];
        if (!usePal(c->srcFormat))
            src2[1] += ((srcSliceH>>c->chrSrcVSubSample)-1)*srcStride[1];
        src2[2] += ((srcSliceH>>c->chrSrcVSubSample)-1)*srcStride[2];
        src2[3] += (srcSliceH-1)*srcStride[3];
        dst2[0] += ( c->dstH                      -1)*dstStride[0];
        dst2[1] += ((c->dstH>>c->chrDstVSubSample)-1)*dstStride[1];
        dst2[2] += ((c->dstH>>c->chrDstVSubSample)-1)*dstStride[2];
        dst2[3] += ( c->dstH                      -1)*dstStride[3];

        reset_ptr(src2, c->srcFormat);
        reset_ptr((const uint8_t**)dst2, c->dstFormat);

        /* reset slice direction at end of frame */
        if (!srcSliceY)
            c->sliceDir = 0;

        return c->swScale(c, src2, srcStride2, c->srcH-srcSliceY-srcSliceH, srcSliceH, dst2, dstStride2);
    }
}

#if LIBSWSCALE_VERSION_MAJOR < 1
int sws_scale_ordered(SwsContext *c, const uint8_t* const src[], int srcStride[], int srcSliceY,
                      int srcSliceH, uint8_t* dst[], int dstStride[])
{
    return sws_scale(c, src, srcStride, srcSliceY, srcSliceH, dst, dstStride);
}
#endif

/* Convert the palette to the same packed 32-bit format as the palette */
void sws_convertPalette8ToPacked32(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;

    for (i=0; i<num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *) palette)[src[i]];
}

/* Palette format: ABCD -> dst format: ABC */
void sws_convertPalette8ToPacked24(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;

    for (i=0; i<num_pixels; i++) {
        //FIXME slow?
        dst[0]= palette[src[i]*4+0];
        dst[1]= palette[src[i]*4+1];
        dst[2]= palette[src[i]*4+2];
        dst+= 3;
    }
}
