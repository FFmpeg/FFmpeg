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

#define RGB2YUV_SHIFT 15
#define BY ( (int) (0.114 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define BV (-(int) (0.081 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define BU ( (int) (0.500 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define GY ( (int) (0.587 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define GV (-(int) (0.419 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define GU (-(int) (0.331 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define RY ( (int) (0.299 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define RV ( (int) (0.500 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define RU (-(int) (0.169 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))

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


static void fillPlane(uint8_t *plane, int stride, int width, int height, int y,
                      uint8_t val)
{
    int i;
    uint8_t *ptr = plane + stride * y;
    for (i = 0; i < height; i++) {
        memset(ptr, val, width);
        ptr += stride;
    }
}

static void fillPlane16(uint8_t *plane, int stride, int width, int height, int y,
                      int alpha, int bits)
{
    int i, j;
    uint8_t *ptr = plane + stride * y;
    int v = alpha ? -1 : (1<<bits);
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            AV_WN16(ptr+2*j, v);
        }
        ptr += stride;
    }
}

static void copyPlane(const uint8_t *src, int srcStride,
                      int srcSliceY, int srcSliceH, int width,
                      uint8_t *dst, int dstStride)
{
    dst += dstStride * srcSliceY;
    if (dstStride == srcStride && srcStride > 0) {
        memcpy(dst, src, srcSliceH * dstStride);
    } else {
        int i;
        for (i = 0; i < srcSliceH; i++) {
            memcpy(dst, src, width);
            src += srcStride;
            dst += dstStride;
        }
    }
}

static int planarToNv12Wrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t *dstParam[],
                               int dstStride[])
{
    uint8_t *dst = dstParam[1] + dstStride[1] * srcSliceY / 2;

    copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->srcW,
              dstParam[0], dstStride[0]);

    if (c->dstFormat == PIX_FMT_NV12)
        interleaveBytes(src[1], src[2], dst, c->srcW / 2, srcSliceH / 2,
                        srcStride[1], srcStride[2], dstStride[0]);
    else
        interleaveBytes(src[2], src[1], dst, c->srcW / 2, srcSliceH / 2,
                        srcStride[2], srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int planarToYuy2Wrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dstParam[], int dstStride[])
{
    uint8_t *dst = dstParam[0] + dstStride[0] * srcSliceY;

    yv12toyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0],
               srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int planarToUyvyWrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dstParam[], int dstStride[])
{
    uint8_t *dst = dstParam[0] + dstStride[0] * srcSliceY;

    yv12touyvy(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0],
               srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int yuv422pToYuy2Wrapper(SwsContext *c, const uint8_t *src[],
                                int srcStride[], int srcSliceY, int srcSliceH,
                                uint8_t *dstParam[], int dstStride[])
{
    uint8_t *dst = dstParam[0] + dstStride[0] * srcSliceY;

    yuv422ptoyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0],
                  srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int yuv422pToUyvyWrapper(SwsContext *c, const uint8_t *src[],
                                int srcStride[], int srcSliceY, int srcSliceH,
                                uint8_t *dstParam[], int dstStride[])
{
    uint8_t *dst = dstParam[0] + dstStride[0] * srcSliceY;

    yuv422ptouyvy(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0],
                  srcStride[1], dstStride[0]);

    return srcSliceH;
}

static int yuyvToYuv420Wrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dstParam[], int dstStride[])
{
    uint8_t *ydst = dstParam[0] + dstStride[0] * srcSliceY;
    uint8_t *udst = dstParam[1] + dstStride[1] * srcSliceY / 2;
    uint8_t *vdst = dstParam[2] + dstStride[2] * srcSliceY / 2;

    yuyvtoyuv420(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0],
                 dstStride[1], srcStride[0]);

    if (dstParam[3])
        fillPlane(dstParam[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);

    return srcSliceH;
}

static int yuyvToYuv422Wrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dstParam[], int dstStride[])
{
    uint8_t *ydst = dstParam[0] + dstStride[0] * srcSliceY;
    uint8_t *udst = dstParam[1] + dstStride[1] * srcSliceY;
    uint8_t *vdst = dstParam[2] + dstStride[2] * srcSliceY;

    yuyvtoyuv422(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0],
                 dstStride[1], srcStride[0]);

    return srcSliceH;
}

static int uyvyToYuv420Wrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dstParam[], int dstStride[])
{
    uint8_t *ydst = dstParam[0] + dstStride[0] * srcSliceY;
    uint8_t *udst = dstParam[1] + dstStride[1] * srcSliceY / 2;
    uint8_t *vdst = dstParam[2] + dstStride[2] * srcSliceY / 2;

    uyvytoyuv420(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0],
                 dstStride[1], srcStride[0]);

    if (dstParam[3])
        fillPlane(dstParam[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);

    return srcSliceH;
}

static int uyvyToYuv422Wrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dstParam[], int dstStride[])
{
    uint8_t *ydst = dstParam[0] + dstStride[0] * srcSliceY;
    uint8_t *udst = dstParam[1] + dstStride[1] * srcSliceY;
    uint8_t *vdst = dstParam[2] + dstStride[2] * srcSliceY;

    uyvytoyuv422(ydst, udst, vdst, src[0], c->srcW, srcSliceH, dstStride[0],
                 dstStride[1], srcStride[0]);

    return srcSliceH;
}

static void gray8aToPacked32(const uint8_t *src, uint8_t *dst, int num_pixels,
                             const uint8_t *palette)
{
    int i;
    for (i = 0; i < num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *) palette)[src[i << 1]] | (src[(i << 1) + 1] << 24);
}

static void gray8aToPacked32_1(const uint8_t *src, uint8_t *dst, int num_pixels,
                               const uint8_t *palette)
{
    int i;

    for (i = 0; i < num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *) palette)[src[i << 1]] | src[(i << 1) + 1];
}

static void gray8aToPacked24(const uint8_t *src, uint8_t *dst, int num_pixels,
                             const uint8_t *palette)
{
    int i;

    for (i = 0; i < num_pixels; i++) {
        //FIXME slow?
        dst[0] = palette[src[i << 1] * 4 + 0];
        dst[1] = palette[src[i << 1] * 4 + 1];
        dst[2] = palette[src[i << 1] * 4 + 2];
        dst += 3;
    }
}

static int packed_16bpc_bswap(SwsContext *c, const uint8_t *src[],
                              int srcStride[], int srcSliceY, int srcSliceH,
                              uint8_t *dst[], int dstStride[])
{
    int i, j;
    int srcstr = srcStride[0] >> 1;
    int dststr = dstStride[0] >> 1;
    uint16_t       *dstPtr =       (uint16_t *) dst[0];
    const uint16_t *srcPtr = (const uint16_t *) src[0];
    int min_stride         = FFMIN(srcstr, dststr);

    for (i = 0; i < srcSliceH; i++) {
        for (j = 0; j < min_stride; j++) {
            dstPtr[j] = av_bswap16(srcPtr[j]);
        }
        srcPtr += srcstr;
        dstPtr += dststr;
    }

    return srcSliceH;
}

static int palToRgbWrapper(SwsContext *c, const uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH, uint8_t *dst[],
                           int dstStride[])
{
    const enum PixelFormat srcFormat = c->srcFormat;
    const enum PixelFormat dstFormat = c->dstFormat;
    void (*conv)(const uint8_t *src, uint8_t *dst, int num_pixels,
                 const uint8_t *palette) = NULL;
    int i;
    uint8_t *dstPtr = dst[0] + dstStride[0] * srcSliceY;
    const uint8_t *srcPtr = src[0];

    if (srcFormat == PIX_FMT_GRAY8A) {
        switch (dstFormat) {
        case PIX_FMT_RGB32  : conv = gray8aToPacked32; break;
        case PIX_FMT_BGR32  : conv = gray8aToPacked32; break;
        case PIX_FMT_BGR32_1: conv = gray8aToPacked32_1; break;
        case PIX_FMT_RGB32_1: conv = gray8aToPacked32_1; break;
        case PIX_FMT_RGB24  : conv = gray8aToPacked24; break;
        case PIX_FMT_BGR24  : conv = gray8aToPacked24; break;
        }
    } else if (usePal(srcFormat)) {
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
               av_get_pix_fmt_name(srcFormat), av_get_pix_fmt_name(dstFormat));
    else {
        for (i = 0; i < srcSliceH; i++) {
            conv(srcPtr, dstPtr, c->srcW, (uint8_t *) c->pal_rgb);
            srcPtr += srcStride[0];
            dstPtr += dstStride[0];
        }
    }

    return srcSliceH;
}

static void gbr24ptopacked24(const uint8_t *src[], int srcStride[],
                             uint8_t *dst, int dstStride, int srcSliceH,
                             int width)
{
    int x, h, i;
    for (h = 0; h < srcSliceH; h++) {
        uint8_t *dest = dst + dstStride * h;
        for (x = 0; x < width; x++) {
            *dest++ = src[0][x];
            *dest++ = src[1][x];
            *dest++ = src[2][x];
        }

        for (i = 0; i < 3; i++)
            src[i] += srcStride[i];
    }
}

static void gbr24ptopacked32(const uint8_t *src[], int srcStride[],
                             uint8_t *dst, int dstStride, int srcSliceH,
                             int alpha_first, int width)
{
    int x, h, i;
    for (h = 0; h < srcSliceH; h++) {
        uint8_t *dest = dst + dstStride * h;

        if (alpha_first) {
            for (x = 0; x < width; x++) {
                *dest++ = 0xff;
                *dest++ = src[0][x];
                *dest++ = src[1][x];
                *dest++ = src[2][x];
            }
        } else {
            for (x = 0; x < width; x++) {
                *dest++ = src[0][x];
                *dest++ = src[1][x];
                *dest++ = src[2][x];
                *dest++ = 0xff;
            }
        }

        for (i = 0; i < 3; i++)
            src[i] += srcStride[i];
    }
}

static int planarRgbToRgbWrapper(SwsContext *c, const uint8_t *src[],
                                 int srcStride[], int srcSliceY, int srcSliceH,
                                 uint8_t *dst[], int dstStride[])
{
    int alpha_first = 0;
    if (c->srcFormat != PIX_FMT_GBRP) {
        av_log(c, AV_LOG_ERROR, "unsupported planar RGB conversion %s -> %s\n",
               av_get_pix_fmt_name(c->srcFormat),
               av_get_pix_fmt_name(c->dstFormat));
        return srcSliceH;
    }

    switch (c->dstFormat) {
    case PIX_FMT_BGR24:
        gbr24ptopacked24((const uint8_t *[]) { src[1], src[0], src[2] },
                         (int []) { srcStride[1], srcStride[0], srcStride[2] },
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, c->srcW);
        break;

    case PIX_FMT_RGB24:
        gbr24ptopacked24((const uint8_t *[]) { src[2], src[0], src[1] },
                         (int []) { srcStride[2], srcStride[0], srcStride[1] },
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, c->srcW);
        break;

    case PIX_FMT_ARGB:
        alpha_first = 1;
    case PIX_FMT_RGBA:
        gbr24ptopacked32((const uint8_t *[]) { src[2], src[0], src[1] },
                         (int []) { srcStride[2], srcStride[0], srcStride[1] },
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, alpha_first, c->srcW);
        break;

    case PIX_FMT_ABGR:
        alpha_first = 1;
    case PIX_FMT_BGRA:
        gbr24ptopacked32((const uint8_t *[]) { src[1], src[0], src[2] },
                         (int []) { srcStride[1], srcStride[0], srcStride[2] },
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, alpha_first, c->srcW);
        break;

    default:
        av_log(c, AV_LOG_ERROR,
               "unsupported planar RGB conversion %s -> %s\n",
               av_get_pix_fmt_name(c->srcFormat),
               av_get_pix_fmt_name(c->dstFormat));
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
typedef void (* rgbConvFn) (const uint8_t *, uint8_t *, int);
static rgbConvFn findRgbConvFn(SwsContext *c)
{
    const enum PixelFormat srcFormat = c->srcFormat;
    const enum PixelFormat dstFormat = c->dstFormat;
    const int srcId = c->srcFormatBpp;
    const int dstId = c->dstFormatBpp;
    rgbConvFn conv = NULL;

#define IS_NOT_NE(bpp, fmt) \
    (((bpp + 7) >> 3) == 2 && \
     (!(av_pix_fmt_descriptors[fmt].flags & PIX_FMT_BE) != !HAVE_BIGENDIAN))

    /* if this is non-native rgb444/555/565, don't handle it here. */
    if (IS_NOT_NE(srcId, srcFormat) || IS_NOT_NE(dstId, dstFormat))
        return NULL;

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
    if ((isBGRinInt(srcFormat) && isBGRinInt(dstFormat)) ||
        (isRGBinInt(srcFormat) && isRGBinInt(dstFormat))) {
        switch (srcId | (dstId << 16)) {
        case 0x000F000C: conv = rgb12to15; break;
        case 0x000F0010: conv = rgb16to15; break;
        case 0x000F0018: conv = rgb24to15; break;
        case 0x000F0020: conv = rgb32to15; break;
        case 0x0010000F: conv = rgb15to16; break;
        case 0x00100018: conv = rgb24to16; break;
        case 0x00100020: conv = rgb32to16; break;
        case 0x0018000F: conv = rgb15to24; break;
        case 0x00180010: conv = rgb16to24; break;
        case 0x00180020: conv = rgb32to24; break;
        case 0x0020000F: conv = rgb15to32; break;
        case 0x00200010: conv = rgb16to32; break;
        case 0x00200018: conv = rgb24to32; break;
        }
    } else if ((isBGRinInt(srcFormat) && isRGBinInt(dstFormat)) ||
               (isRGBinInt(srcFormat) && isBGRinInt(dstFormat))) {
        switch (srcId | (dstId << 16)) {
        case 0x000C000C: conv = rgb12tobgr12; break;
        case 0x000F000F: conv = rgb15tobgr15; break;
        case 0x000F0010: conv = rgb16tobgr15; break;
        case 0x000F0018: conv = rgb24tobgr15; break;
        case 0x000F0020: conv = rgb32tobgr15; break;
        case 0x0010000F: conv = rgb15tobgr16; break;
        case 0x00100010: conv = rgb16tobgr16; break;
        case 0x00100018: conv = rgb24tobgr16; break;
        case 0x00100020: conv = rgb32tobgr16; break;
        case 0x0018000F: conv = rgb15tobgr24; break;
        case 0x00180010: conv = rgb16tobgr24; break;
        case 0x00180018: conv = rgb24tobgr24; break;
        case 0x00180020: conv = rgb32tobgr24; break;
        case 0x0020000F: conv = rgb15tobgr32; break;
        case 0x00200010: conv = rgb16tobgr32; break;
        case 0x00200018: conv = rgb24tobgr32; break;
        }
    }

    if ((dstFormat == PIX_FMT_RGB32_1 || dstFormat == PIX_FMT_BGR32_1) && !isRGBA32(srcFormat) && ALT32_CORR<0)
        return NULL;

    return conv;
}

/* {RGB,BGR}{15,16,24,32,32_1} -> {RGB,BGR}{15,16,24,32} */
static int rgbToRgbWrapper(SwsContext *c, const uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH, uint8_t *dst[],
                           int dstStride[])

{
    const enum PixelFormat srcFormat = c->srcFormat;
    const enum PixelFormat dstFormat = c->dstFormat;
    const int srcBpp = (c->srcFormatBpp + 7) >> 3;
    const int dstBpp = (c->dstFormatBpp + 7) >> 3;
    rgbConvFn conv = findRgbConvFn(c);

    if (!conv) {
        av_log(c, AV_LOG_ERROR, "internal error %s -> %s converter\n",
               av_get_pix_fmt_name(srcFormat), av_get_pix_fmt_name(dstFormat));
    } else {
        const uint8_t *srcPtr = src[0];
              uint8_t *dstPtr = dst[0];
        if ((srcFormat == PIX_FMT_RGB32_1 || srcFormat == PIX_FMT_BGR32_1) &&
            !isRGBA32(dstFormat))
            srcPtr += ALT32_CORR;

        if ((dstFormat == PIX_FMT_RGB32_1 || dstFormat == PIX_FMT_BGR32_1) &&
            !isRGBA32(srcFormat))
            dstPtr += ALT32_CORR;

        if (dstStride[0] * srcBpp == srcStride[0] * dstBpp && srcStride[0] > 0 &&
            !(srcStride[0] % srcBpp))
            conv(srcPtr, dstPtr + dstStride[0] * srcSliceY,
                 srcSliceH * srcStride[0]);
        else {
            int i;
            dstPtr += dstStride[0] * srcSliceY;

            for (i = 0; i < srcSliceH; i++) {
                conv(srcPtr, dstPtr, c->srcW * srcBpp);
                srcPtr += srcStride[0];
                dstPtr += dstStride[0];
            }
        }
    }
    return srcSliceH;
}

static int bgr24ToYv12Wrapper(SwsContext *c, const uint8_t *src[],
                              int srcStride[], int srcSliceY, int srcSliceH,
                              uint8_t *dst[], int dstStride[])
{
    rgb24toyv12(
        src[0],
        dst[0] +  srcSliceY       * dstStride[0],
        dst[1] + (srcSliceY >> 1) * dstStride[1],
        dst[2] + (srcSliceY >> 1) * dstStride[2],
        c->srcW, srcSliceH,
        dstStride[0], dstStride[1], srcStride[0]);
    if (dst[3])
        fillPlane(dst[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);
    return srcSliceH;
}

static int yvu9ToYv12Wrapper(SwsContext *c, const uint8_t *src[],
                             int srcStride[], int srcSliceY, int srcSliceH,
                             uint8_t *dst[], int dstStride[])
{
    copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->srcW,
              dst[0], dstStride[0]);

    planar2x(src[1], dst[1] + dstStride[1] * (srcSliceY >> 1), c->chrSrcW,
             srcSliceH >> 2, srcStride[1], dstStride[1]);
    planar2x(src[2], dst[2] + dstStride[2] * (srcSliceY >> 1), c->chrSrcW,
             srcSliceH >> 2, srcStride[2], dstStride[2]);
    if (dst[3])
        fillPlane(dst[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);
    return srcSliceH;
}

/* unscaled copy like stuff (assumes nearly identical formats) */
static int packedCopyWrapper(SwsContext *c, const uint8_t *src[],
                             int srcStride[], int srcSliceY, int srcSliceH,
                             uint8_t *dst[], int dstStride[])
{
    if (dstStride[0] == srcStride[0] && srcStride[0] > 0)
        memcpy(dst[0] + dstStride[0] * srcSliceY, src[0], srcSliceH * dstStride[0]);
    else {
        int i;
        const uint8_t *srcPtr = src[0];
        uint8_t *dstPtr = dst[0] + dstStride[0] * srcSliceY;
        int length = 0;

        /* universal length finder */
        while (length + c->srcW <= FFABS(dstStride[0]) &&
               length + c->srcW <= FFABS(srcStride[0]))
            length += c->srcW;
        assert(length != 0);

        for (i = 0; i < srcSliceH; i++) {
            memcpy(dstPtr, srcPtr, length);
            srcPtr += srcStride[0];
            dstPtr += dstStride[0];
        }
    }
    return srcSliceH;
}

#define DITHER_COPY(dst, dstStride, src, srcStride, bswap, dbswap)\
    uint16_t scale= dither_scale[dst_depth-1][src_depth-1];\
    int shift= src_depth-dst_depth + dither_scale[src_depth-2][dst_depth-1];\
    for (i = 0; i < height; i++) {\
        const uint8_t *dither= dithers[src_depth-9][i&7];\
        for (j = 0; j < length-7; j+=8){\
            dst[j+0] = dbswap((bswap(src[j+0]) + dither[0])*scale>>shift);\
            dst[j+1] = dbswap((bswap(src[j+1]) + dither[1])*scale>>shift);\
            dst[j+2] = dbswap((bswap(src[j+2]) + dither[2])*scale>>shift);\
            dst[j+3] = dbswap((bswap(src[j+3]) + dither[3])*scale>>shift);\
            dst[j+4] = dbswap((bswap(src[j+4]) + dither[4])*scale>>shift);\
            dst[j+5] = dbswap((bswap(src[j+5]) + dither[5])*scale>>shift);\
            dst[j+6] = dbswap((bswap(src[j+6]) + dither[6])*scale>>shift);\
            dst[j+7] = dbswap((bswap(src[j+7]) + dither[7])*scale>>shift);\
        }\
        for (; j < length; j++)\
            dst[j] = dbswap((bswap(src[j]) + dither[j&7])*scale>>shift);\
        dst += dstStride;\
        src += srcStride;\
    }

static int planarCopyWrapper(SwsContext *c, const uint8_t *src[],
                             int srcStride[], int srcSliceY, int srcSliceH,
                             uint8_t *dst[], int dstStride[])
{
    int plane, i, j;
    for (plane = 0; plane < 4; plane++) {
        int length = (plane == 0 || plane == 3) ? c->srcW  : -((-c->srcW  ) >> c->chrDstHSubSample);
        int y =      (plane == 0 || plane == 3) ? srcSliceY: -((-srcSliceY) >> c->chrDstVSubSample);
        int height = (plane == 0 || plane == 3) ? srcSliceH: -((-srcSliceH) >> c->chrDstVSubSample);
        const uint8_t *srcPtr = src[plane];
        uint8_t *dstPtr = dst[plane] + dstStride[plane] * y;
        int shiftonly= plane==1 || plane==2 || (!c->srcRange && plane==0);

        if (!dst[plane])
            continue;
        // ignore palette for GRAY8
        if (plane == 1 && !dst[2]) continue;
        if (!src[plane] || (plane == 1 && !src[2])) {
            if (is16BPS(c->dstFormat) || isNBPS(c->dstFormat)) {
                fillPlane16(dst[plane], dstStride[plane], length, height, y,
                        plane == 3, av_pix_fmt_descriptors[c->dstFormat].comp[plane].depth_minus1);
            } else {
                fillPlane(dst[plane], dstStride[plane], length, height, y,
                        (plane == 3) ? 255 : 128);
            }
        } else {
            if(isNBPS(c->srcFormat) || isNBPS(c->dstFormat)
               || (is16BPS(c->srcFormat) != is16BPS(c->dstFormat))
            ) {
                const int src_depth = av_pix_fmt_descriptors[c->srcFormat].comp[plane].depth_minus1 + 1;
                const int dst_depth = av_pix_fmt_descriptors[c->dstFormat].comp[plane].depth_minus1 + 1;
                const uint16_t *srcPtr2 = (const uint16_t *) srcPtr;
                uint16_t *dstPtr2 = (uint16_t*)dstPtr;

                if (dst_depth == 8) {
                    if(isBE(c->srcFormat) == HAVE_BIGENDIAN){
                        DITHER_COPY(dstPtr, dstStride[plane], srcPtr2, srcStride[plane]/2, , )
                    } else {
                        DITHER_COPY(dstPtr, dstStride[plane], srcPtr2, srcStride[plane]/2, av_bswap16, )
                    }
                } else if (src_depth == 8) {
                    for (i = 0; i < height; i++) {
                        #define COPY816(w)\
                        if(shiftonly){\
                            for (j = 0; j < length; j++)\
                                w(&dstPtr2[j], srcPtr[j]<<(dst_depth-8));\
                        }else{\
                            for (j = 0; j < length; j++)\
                                w(&dstPtr2[j], (srcPtr[j]<<(dst_depth-8)) |\
                                               (srcPtr[j]>>(2*8-dst_depth)));\
                        }
                        if(isBE(c->dstFormat)){
                            COPY816(AV_WB16)
                        } else {
                            COPY816(AV_WL16)
                        }
                        dstPtr2 += dstStride[plane]/2;
                        srcPtr  += srcStride[plane];
                    }
                } else if (src_depth <= dst_depth) {
                    for (i = 0; i < height; i++) {
#define COPY_UP(r,w) \
    if(shiftonly){\
        for (j = 0; j < length; j++){ \
            unsigned int v= r(&srcPtr2[j]);\
            w(&dstPtr2[j], v<<(dst_depth-src_depth));\
        }\
    }else{\
        for (j = 0; j < length; j++){ \
            unsigned int v= r(&srcPtr2[j]);\
            w(&dstPtr2[j], (v<<(dst_depth-src_depth)) | \
                        (v>>(2*src_depth-dst_depth)));\
        }\
    }
                        if(isBE(c->srcFormat)){
                            if(isBE(c->dstFormat)){
                                COPY_UP(AV_RB16, AV_WB16)
                            } else {
                                COPY_UP(AV_RB16, AV_WL16)
                            }
                        } else {
                            if(isBE(c->dstFormat)){
                                COPY_UP(AV_RL16, AV_WB16)
                            } else {
                                COPY_UP(AV_RL16, AV_WL16)
                            }
                        }
                        dstPtr2 += dstStride[plane]/2;
                        srcPtr2 += srcStride[plane]/2;
                    }
                } else {
                    if(isBE(c->srcFormat) == HAVE_BIGENDIAN){
                        if(isBE(c->dstFormat) == HAVE_BIGENDIAN){
                            DITHER_COPY(dstPtr2, dstStride[plane]/2, srcPtr2, srcStride[plane]/2, , )
                        } else {
                            DITHER_COPY(dstPtr2, dstStride[plane]/2, srcPtr2, srcStride[plane]/2, , av_bswap16)
                        }
                    }else{
                        if(isBE(c->dstFormat) == HAVE_BIGENDIAN){
                            DITHER_COPY(dstPtr2, dstStride[plane]/2, srcPtr2, srcStride[plane]/2, av_bswap16, )
                        } else {
                            DITHER_COPY(dstPtr2, dstStride[plane]/2, srcPtr2, srcStride[plane]/2, av_bswap16, av_bswap16)
                        }
                    }
                }
            } else if (is16BPS(c->srcFormat) && is16BPS(c->dstFormat) &&
                      isBE(c->srcFormat) != isBE(c->dstFormat)) {

                for (i = 0; i < height; i++) {
                    for (j = 0; j < length; j++)
                        ((uint16_t *) dstPtr)[j] = av_bswap16(((const uint16_t *) srcPtr)[j]);
                    srcPtr += srcStride[plane];
                    dstPtr += dstStride[plane];
                }
            } else if (dstStride[plane] == srcStride[plane] &&
                       srcStride[plane] > 0 && srcStride[plane] == length) {
                memcpy(dst[plane] + dstStride[plane] * y, src[plane],
                       height * dstStride[plane]);
            } else {
                if (is16BPS(c->srcFormat) && is16BPS(c->dstFormat))
                    length *= 2;
                else if (!av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1)
                    length >>= 3; // monowhite/black
                for (i = 0; i < height; i++) {
                    memcpy(dstPtr, srcPtr, length);
                    srcPtr += srcStride[plane];
                    dstPtr += dstStride[plane];
                }
            }
        }
    }
    return srcSliceH;
}


#define IS_DIFFERENT_ENDIANESS(src_fmt, dst_fmt, pix_fmt)          \
    ((src_fmt == pix_fmt ## BE && dst_fmt == pix_fmt ## LE) ||     \
     (src_fmt == pix_fmt ## LE && dst_fmt == pix_fmt ## BE))


void ff_get_unscaled_swscale(SwsContext *c)
{
    const enum PixelFormat srcFormat = c->srcFormat;
    const enum PixelFormat dstFormat = c->dstFormat;
    const int flags = c->flags;
    const int dstH = c->dstH;
    int needsDither;

    needsDither = isAnyRGB(dstFormat) &&
            c->dstFormatBpp < 24 &&
           (c->dstFormatBpp < c->srcFormatBpp || (!isAnyRGB(srcFormat)));

    /* yv12_to_nv12 */
    if ((srcFormat == PIX_FMT_YUV420P || srcFormat == PIX_FMT_YUVA420P) &&
        (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21)) {
        c->swScale = planarToNv12Wrapper;
    }
    /* yuv2bgr */
    if ((srcFormat == PIX_FMT_YUV420P || srcFormat == PIX_FMT_YUV422P ||
         srcFormat == PIX_FMT_YUVA420P) && isAnyRGB(dstFormat) &&
        !(flags & SWS_ACCURATE_RND) && !(dstH & 1)) {
        c->swScale = ff_yuv2rgb_get_func_ptr(c);
    }

    if (srcFormat == PIX_FMT_YUV410P &&
        (dstFormat == PIX_FMT_YUV420P || dstFormat == PIX_FMT_YUVA420P) &&
        !(flags & SWS_BITEXACT)) {
        c->swScale = yvu9ToYv12Wrapper;
    }

    /* bgr24toYV12 */
    if (srcFormat == PIX_FMT_BGR24 &&
        (dstFormat == PIX_FMT_YUV420P || dstFormat == PIX_FMT_YUVA420P) &&
        !(flags & SWS_ACCURATE_RND))
        c->swScale = bgr24ToYv12Wrapper;

    /* RGB/BGR -> RGB/BGR (no dither needed forms) */
    if (isAnyRGB(srcFormat) && isAnyRGB(dstFormat) && findRgbConvFn(c)
        && (!needsDither || (c->flags&(SWS_FAST_BILINEAR|SWS_POINT))))
        c->swScale= rgbToRgbWrapper;

#define isByteRGB(f) (\
        f == PIX_FMT_RGB32   ||\
        f == PIX_FMT_RGB32_1 ||\
        f == PIX_FMT_RGB24   ||\
        f == PIX_FMT_BGR32   ||\
        f == PIX_FMT_BGR32_1 ||\
        f == PIX_FMT_BGR24)

    if (isAnyRGB(srcFormat) && isPlanar(srcFormat) && isByteRGB(dstFormat))
        c->swScale = planarRgbToRgbWrapper;

    /* bswap 16 bits per pixel/component packed formats */
    if (IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_BGR444) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_BGR48)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_BGR555) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_BGR565) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_GRAY16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_RGB444) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_RGB48)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_RGB555) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, PIX_FMT_RGB565))
        c->swScale = packed_16bpc_bswap;

    if (usePal(srcFormat) && isByteRGB(dstFormat))
        c->swScale = palToRgbWrapper;

    if (srcFormat == PIX_FMT_YUV422P) {
        if (dstFormat == PIX_FMT_YUYV422)
            c->swScale = yuv422pToYuy2Wrapper;
        else if (dstFormat == PIX_FMT_UYVY422)
            c->swScale = yuv422pToUyvyWrapper;
    }

    /* LQ converters if -sws 0 or -sws 4*/
    if (c->flags&(SWS_FAST_BILINEAR|SWS_POINT)) {
        /* yv12_to_yuy2 */
        if (srcFormat == PIX_FMT_YUV420P || srcFormat == PIX_FMT_YUVA420P) {
            if (dstFormat == PIX_FMT_YUYV422)
                c->swScale = planarToYuy2Wrapper;
            else if (dstFormat == PIX_FMT_UYVY422)
                c->swScale = planarToUyvyWrapper;
        }
    }
    if (srcFormat == PIX_FMT_YUYV422 &&
       (dstFormat == PIX_FMT_YUV420P || dstFormat == PIX_FMT_YUVA420P))
        c->swScale = yuyvToYuv420Wrapper;
    if (srcFormat == PIX_FMT_UYVY422 &&
       (dstFormat == PIX_FMT_YUV420P || dstFormat == PIX_FMT_YUVA420P))
        c->swScale = uyvyToYuv420Wrapper;
    if (srcFormat == PIX_FMT_YUYV422 && dstFormat == PIX_FMT_YUV422P)
        c->swScale = yuyvToYuv422Wrapper;
    if (srcFormat == PIX_FMT_UYVY422 && dstFormat == PIX_FMT_YUV422P)
        c->swScale = uyvyToYuv422Wrapper;

#define isPlanarGray(x) (isGray(x) && (x) != PIX_FMT_GRAY8A)
    /* simple copy */
    if ( srcFormat == dstFormat ||
        (srcFormat == PIX_FMT_YUVA420P && dstFormat == PIX_FMT_YUV420P) ||
        (srcFormat == PIX_FMT_YUV420P && dstFormat == PIX_FMT_YUVA420P) ||
        (isPlanarYUV(srcFormat) && isPlanarGray(dstFormat)) ||
        (isPlanarYUV(dstFormat) && isPlanarGray(srcFormat)) ||
        (isPlanarGray(dstFormat) && isPlanarGray(srcFormat)) ||
        (isPlanarYUV(srcFormat) && isPlanarYUV(dstFormat) &&
         c->chrDstHSubSample == c->chrSrcHSubSample &&
         c->chrDstVSubSample == c->chrSrcVSubSample &&
         dstFormat != PIX_FMT_NV12 && dstFormat != PIX_FMT_NV21 &&
         srcFormat != PIX_FMT_NV12 && srcFormat != PIX_FMT_NV21))
    {
        if (isPacked(c->srcFormat))
            c->swScale = packedCopyWrapper;
        else /* Planar YUV or gray */
            c->swScale = planarCopyWrapper;
    }

    if (ARCH_BFIN)
        ff_bfin_get_unscaled_swscale(c);
    if (HAVE_ALTIVEC)
        ff_swscale_get_unscaled_altivec(c);
}

static void reset_ptr(const uint8_t *src[], int format)
{
    if (!isALPHA(format))
        src[3] = NULL;
    if (!isPlanar(format)) {
        src[3] = src[2] = NULL;

        if (!usePal(format))
            src[1] = NULL;
    }
}

static int check_image_pointers(const uint8_t * const data[4], enum PixelFormat pix_fmt,
                                const int linesizes[4])
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    int i;

    for (i = 0; i < 4; i++) {
        int plane = desc->comp[i].plane;
        if (!data[plane] || !linesizes[plane])
            return 0;
    }

    return 1;
}

/**
 * swscale wrapper, so we don't need to export the SwsContext.
 * Assumes planar YUV to be in YUV order instead of YVU.
 */
int attribute_align_arg sws_scale(struct SwsContext *c,
                                  const uint8_t * const srcSlice[],
                                  const int srcStride[], int srcSliceY,
                                  int srcSliceH, uint8_t *const dst[],
                                  const int dstStride[])
{
    int i, ret;
    const uint8_t *src2[4] = { srcSlice[0], srcSlice[1], srcSlice[2], srcSlice[3] };
    uint8_t *dst2[4] = { dst[0], dst[1], dst[2], dst[3] };
    uint8_t *rgb0_tmp = NULL;

    // do not mess up sliceDir if we have a "trailing" 0-size slice
    if (srcSliceH == 0)
        return 0;

    if (!check_image_pointers(srcSlice, c->srcFormat, srcStride)) {
        av_log(c, AV_LOG_ERROR, "bad src image pointers\n");
        return 0;
    }
    if (!check_image_pointers((const uint8_t* const*)dst, c->dstFormat, dstStride)) {
        av_log(c, AV_LOG_ERROR, "bad dst image pointers\n");
        return 0;
    }

    if (c->sliceDir == 0 && srcSliceY != 0 && srcSliceY + srcSliceH != c->srcH) {
        av_log(c, AV_LOG_ERROR, "Slices start in the middle!\n");
        return 0;
    }
    if (c->sliceDir == 0) {
        if (srcSliceY == 0) c->sliceDir = 1; else c->sliceDir = -1;
    }

    if (usePal(c->srcFormat)) {
        for (i = 0; i < 256; i++) {
            int p, r, g, b, y, u, v, a = 0xff;
            if (c->srcFormat == PIX_FMT_PAL8) {
                p = ((const uint32_t *)(srcSlice[1]))[i];
                a = (p >> 24) & 0xFF;
                r = (p >> 16) & 0xFF;
                g = (p >>  8) & 0xFF;
                b =  p        & 0xFF;
            } else if (c->srcFormat == PIX_FMT_RGB8) {
                r = ( i >> 5     ) * 36;
                g = ((i >> 2) & 7) * 36;
                b = ( i       & 3) * 85;
            } else if (c->srcFormat == PIX_FMT_BGR8) {
                b = ( i >> 6     ) * 85;
                g = ((i >> 3) & 7) * 36;
                r = ( i       & 7) * 36;
            } else if (c->srcFormat == PIX_FMT_RGB4_BYTE) {
                r = ( i >> 3     ) * 255;
                g = ((i >> 1) & 3) * 85;
                b = ( i       & 1) * 255;
            } else if (c->srcFormat == PIX_FMT_GRAY8 || c->srcFormat == PIX_FMT_GRAY8A) {
                r = g = b = i;
            } else {
                assert(c->srcFormat == PIX_FMT_BGR4_BYTE);
                b = ( i >> 3     ) * 255;
                g = ((i >> 1) & 3) * 85;
                r = ( i       & 1) * 255;
            }
            y = av_clip_uint8((RY * r + GY * g + BY * b + ( 33 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            u = av_clip_uint8((RU * r + GU * g + BU * b + (257 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            v = av_clip_uint8((RV * r + GV * g + BV * b + (257 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            c->pal_yuv[i]= y + (u<<8) + (v<<16) + (a<<24);

            switch (c->dstFormat) {
            case PIX_FMT_BGR32:
#if !HAVE_BIGENDIAN
            case PIX_FMT_RGB24:
#endif
                c->pal_rgb[i]=  r + (g<<8) + (b<<16) + (a<<24);
                break;
            case PIX_FMT_BGR32_1:
#if HAVE_BIGENDIAN
            case PIX_FMT_BGR24:
#endif
                c->pal_rgb[i]= a + (r<<8) + (g<<16) + (b<<24);
                break;
            case PIX_FMT_RGB32_1:
#if HAVE_BIGENDIAN
            case PIX_FMT_RGB24:
#endif
                c->pal_rgb[i]= a + (b<<8) + (g<<16) + (r<<24);
                break;
            case PIX_FMT_RGB32:
#if !HAVE_BIGENDIAN
            case PIX_FMT_BGR24:
#endif
            default:
                c->pal_rgb[i]=  b + (g<<8) + (r<<16) + (a<<24);
            }
        }
    }

    if (c->src0Alpha && !c->dst0Alpha && isALPHA(c->dstFormat)) {
        uint8_t *base;
        int x,y;
        rgb0_tmp = av_malloc(FFABS(srcStride[0]) * srcSliceH + 32);
        base = srcStride[0] < 0 ? rgb0_tmp - srcStride[0] * (srcSliceH-1) : rgb0_tmp;
        for (y=0; y<srcSliceH; y++){
            memcpy(base + srcStride[0]*y, src2[0] + srcStride[0]*y, 4*c->srcW);
            for (x=c->src0Alpha-1; x<4*c->srcW; x+=4) {
                base[ srcStride[0]*y + x] = 0xFF;
            }
        }
        src2[0] = base;
    }

    // copy strides, so they can safely be modified
    if (c->sliceDir == 1) {
        // slices go from top to bottom
        int srcStride2[4] = { srcStride[0], srcStride[1], srcStride[2],
                              srcStride[3] };
        int dstStride2[4] = { dstStride[0], dstStride[1], dstStride[2],
                              dstStride[3] };

        reset_ptr(src2, c->srcFormat);
        reset_ptr((void*)dst2, c->dstFormat);

        /* reset slice direction at end of frame */
        if (srcSliceY + srcSliceH == c->srcH)
            c->sliceDir = 0;

        ret = c->swScale(c, src2, srcStride2, srcSliceY, srcSliceH, dst2,
                          dstStride2);
    } else {
        // slices go from bottom to top => we flip the image internally
        int srcStride2[4] = { -srcStride[0], -srcStride[1], -srcStride[2],
                              -srcStride[3] };
        int dstStride2[4] = { -dstStride[0], -dstStride[1], -dstStride[2],
                              -dstStride[3] };

        src2[0] += (srcSliceH - 1) * srcStride[0];
        if (!usePal(c->srcFormat))
            src2[1] += ((srcSliceH >> c->chrSrcVSubSample) - 1) * srcStride[1];
        src2[2] += ((srcSliceH >> c->chrSrcVSubSample) - 1) * srcStride[2];
        src2[3] += (srcSliceH - 1) * srcStride[3];
        dst2[0] += ( c->dstH                         - 1) * dstStride[0];
        dst2[1] += ((c->dstH >> c->chrDstVSubSample) - 1) * dstStride[1];
        dst2[2] += ((c->dstH >> c->chrDstVSubSample) - 1) * dstStride[2];
        dst2[3] += ( c->dstH                         - 1) * dstStride[3];

        reset_ptr(src2, c->srcFormat);
        reset_ptr((void*)dst2, c->dstFormat);

        /* reset slice direction at end of frame */
        if (!srcSliceY)
            c->sliceDir = 0;

        ret = c->swScale(c, src2, srcStride2, c->srcH-srcSliceY-srcSliceH,
                          srcSliceH, dst2, dstStride2);
    }

    av_free(rgb0_tmp);
    return ret;
}

/* Convert the palette to the same packed 32-bit format as the palette */
void sws_convertPalette8ToPacked32(const uint8_t *src, uint8_t *dst,
                                   int num_pixels, const uint8_t *palette)
{
    int i;

    for (i = 0; i < num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *) palette)[src[i]];
}

/* Palette format: ABCD -> dst format: ABC */
void sws_convertPalette8ToPacked24(const uint8_t *src, uint8_t *dst,
                                   int num_pixels, const uint8_t *palette)
{
    int i;

    for (i = 0; i < num_pixels; i++) {
        //FIXME slow?
        dst[0] = palette[src[i] * 4 + 0];
        dst[1] = palette[src[i] * 4 + 1];
        dst[2] = palette[src[i] * 4 + 2];
        dst += 3;
    }
}
