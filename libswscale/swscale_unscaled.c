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
#include "swscale.h"
#include "swscale_internal.h"
#include "rgb2rgb.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/cpu.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libavutil/bswap.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"

DECLARE_ALIGNED(8, static const uint8_t, dithers)[8][8][8]={
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

static const uint16_t dither_scale[15][16]={
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

    if (c->dstFormat == AV_PIX_FMT_NV12)
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
    int i, j, p;

    for (p = 0; p < 4; p++) {
        int srcstr = srcStride[p] / 2;
        int dststr = dstStride[p] / 2;
        uint16_t       *dstPtr =       (uint16_t *) dst[p];
        const uint16_t *srcPtr = (const uint16_t *) src[p];
        int min_stride         = FFMIN(FFABS(srcstr), FFABS(dststr));
        if(!dstPtr || !srcPtr)
            continue;
        for (i = 0; i < (srcSliceH >> c->chrDstVSubSample); i++) {
            for (j = 0; j < min_stride; j++) {
                dstPtr[j] = av_bswap16(srcPtr[j]);
            }
            srcPtr += srcstr;
            dstPtr += dststr;
        }
    }

    return srcSliceH;
}

static int palToRgbWrapper(SwsContext *c, const uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH, uint8_t *dst[],
                           int dstStride[])
{
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    void (*conv)(const uint8_t *src, uint8_t *dst, int num_pixels,
                 const uint8_t *palette) = NULL;
    int i;
    uint8_t *dstPtr = dst[0] + dstStride[0] * srcSliceY;
    const uint8_t *srcPtr = src[0];

    if (srcFormat == AV_PIX_FMT_GRAY8A) {
        switch (dstFormat) {
        case AV_PIX_FMT_RGB32  : conv = gray8aToPacked32; break;
        case AV_PIX_FMT_BGR32  : conv = gray8aToPacked32; break;
        case AV_PIX_FMT_BGR32_1: conv = gray8aToPacked32_1; break;
        case AV_PIX_FMT_RGB32_1: conv = gray8aToPacked32_1; break;
        case AV_PIX_FMT_RGB24  : conv = gray8aToPacked24; break;
        case AV_PIX_FMT_BGR24  : conv = gray8aToPacked24; break;
        }
    } else if (usePal(srcFormat)) {
        switch (dstFormat) {
        case AV_PIX_FMT_RGB32  : conv = sws_convertPalette8ToPacked32; break;
        case AV_PIX_FMT_BGR32  : conv = sws_convertPalette8ToPacked32; break;
        case AV_PIX_FMT_BGR32_1: conv = sws_convertPalette8ToPacked32; break;
        case AV_PIX_FMT_RGB32_1: conv = sws_convertPalette8ToPacked32; break;
        case AV_PIX_FMT_RGB24  : conv = sws_convertPalette8ToPacked24; break;
        case AV_PIX_FMT_BGR24  : conv = sws_convertPalette8ToPacked24; break;
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

static void gbr16ptopacked16(const uint16_t *src[], int srcStride[],
                             uint8_t *dst, int dstStride, int srcSliceH,
                             int alpha, int swap, int bpp, int width)
{
    int x, h, i;
    int scale_high = 16 - bpp, scale_low = (bpp - 8) * 2;
    for (h = 0; h < srcSliceH; h++) {
        uint16_t *dest = (uint16_t *)(dst + dstStride * h);
        uint16_t component;

        switch(swap) {
        case 3:
            if (alpha) {
                for (x = 0; x < width; x++) {
                    component = av_bswap16(src[0][x]);
                    *dest++ = av_bswap16(component << scale_high | component >> scale_low);
                    component = av_bswap16(src[1][x]);
                    *dest++ = av_bswap16(component << scale_high | component >> scale_low);
                    component = av_bswap16(src[2][x]);
                    *dest++ = av_bswap16(component << scale_high | component >> scale_low);
                    *dest++ = 0xffff;
                }
            } else {
                for (x = 0; x < width; x++) {
                    component = av_bswap16(src[0][x]);
                    *dest++ = av_bswap16(component << scale_high | component >> scale_low);
                    component = av_bswap16(src[1][x]);
                    *dest++ = av_bswap16(component << scale_high | component >> scale_low);
                    component = av_bswap16(src[2][x]);
                    *dest++ = av_bswap16(component << scale_high | component >> scale_low);
                }
            }
            break;
        case 2:
            if (alpha) {
                for (x = 0; x < width; x++) {
                    *dest++ = av_bswap16(src[0][x] << scale_high | src[0][x] >> scale_low);
                    *dest++ = av_bswap16(src[1][x] << scale_high | src[1][x] >> scale_low);
                    *dest++ = av_bswap16(src[2][x] << scale_high | src[2][x] >> scale_low);
                    *dest++ = 0xffff;
                }
            } else {
                for (x = 0; x < width; x++) {
                    *dest++ = av_bswap16(src[0][x] << scale_high | src[0][x] >> scale_low);
                    *dest++ = av_bswap16(src[1][x] << scale_high | src[1][x] >> scale_low);
                    *dest++ = av_bswap16(src[2][x] << scale_high | src[2][x] >> scale_low);
                }
            }
            break;
        case 1:
            if (alpha) {
                for (x = 0; x < width; x++) {
                    *dest++ = av_bswap16(src[0][x]) << scale_high | av_bswap16(src[0][x]) >> scale_low;
                    *dest++ = av_bswap16(src[1][x]) << scale_high | av_bswap16(src[1][x]) >> scale_low;
                    *dest++ = av_bswap16(src[2][x]) << scale_high | av_bswap16(src[2][x]) >> scale_low;
                    *dest++ = 0xffff;
                }
            } else {
                for (x = 0; x < width; x++) {
                    *dest++ = av_bswap16(src[0][x]) << scale_high | av_bswap16(src[0][x]) >> scale_low;
                    *dest++ = av_bswap16(src[1][x]) << scale_high | av_bswap16(src[1][x]) >> scale_low;
                    *dest++ = av_bswap16(src[2][x]) << scale_high | av_bswap16(src[2][x]) >> scale_low;
                }
            }
            break;
        default:
            if (alpha) {
                for (x = 0; x < width; x++) {
                    *dest++ = src[0][x] << scale_high | src[0][x] >> scale_low;
                    *dest++ = src[1][x] << scale_high | src[1][x] >> scale_low;
                    *dest++ = src[2][x] << scale_high | src[2][x] >> scale_low;
                    *dest++ = 0xffff;
                }
            } else {
                for (x = 0; x < width; x++) {
                    *dest++ = src[0][x] << scale_high | src[0][x] >> scale_low;
                    *dest++ = src[1][x] << scale_high | src[1][x] >> scale_low;
                    *dest++ = src[2][x] << scale_high | src[2][x] >> scale_low;
                }
            }
        }
        for (i = 0; i < 3; i++)
            src[i] += srcStride[i] >> 1;
    }
}

static int planarRgb16ToRgb16Wrapper(SwsContext *c, const uint8_t *src[],
                                     int srcStride[], int srcSliceY, int srcSliceH,
                                     uint8_t *dst[], int dstStride[])
{
    const uint16_t *src102[] = { (uint16_t *)src[1], (uint16_t *)src[0], (uint16_t *)src[2] };
    const uint16_t *src201[] = { (uint16_t *)src[2], (uint16_t *)src[0], (uint16_t *)src[1] };
    int stride102[] = { srcStride[1], srcStride[0], srcStride[2] };
    int stride201[] = { srcStride[2], srcStride[0], srcStride[1] };
    const AVPixFmtDescriptor *src_format = av_pix_fmt_desc_get(c->srcFormat);
    const AVPixFmtDescriptor *dst_format = av_pix_fmt_desc_get(c->dstFormat);
    int bits_per_sample = src_format->comp[0].depth_minus1 + 1;
    int swap = 0;
    if ( HAVE_BIGENDIAN && !(src_format->flags & AV_PIX_FMT_FLAG_BE) ||
        !HAVE_BIGENDIAN &&   src_format->flags & AV_PIX_FMT_FLAG_BE)
        swap++;
    if ( HAVE_BIGENDIAN && !(dst_format->flags & AV_PIX_FMT_FLAG_BE) ||
        !HAVE_BIGENDIAN &&   dst_format->flags & AV_PIX_FMT_FLAG_BE)
        swap += 2;

    if ((src_format->flags & (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB)) !=
        (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB) ||
        bits_per_sample <= 8) {
        av_log(c, AV_LOG_ERROR, "unsupported planar RGB conversion %s -> %s\n",
               src_format->name, dst_format->name);
        return srcSliceH;
    }
    switch (c->dstFormat) {
    case AV_PIX_FMT_BGR48LE:
    case AV_PIX_FMT_BGR48BE:
        gbr16ptopacked16(src102, stride102,
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, 0, swap, bits_per_sample, c->srcW);
        break;
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_RGB48BE:
        gbr16ptopacked16(src201, stride201,
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, 0, swap, bits_per_sample, c->srcW);
        break;
    case AV_PIX_FMT_RGBA64LE:
    case AV_PIX_FMT_RGBA64BE:
         gbr16ptopacked16(src201, stride201,
                          dst[0] + srcSliceY * dstStride[0], dstStride[0],
                          srcSliceH, 1, swap, bits_per_sample, c->srcW);
        break;
    case AV_PIX_FMT_BGRA64LE:
    case AV_PIX_FMT_BGRA64BE:
        gbr16ptopacked16(src102, stride102,
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, 1, swap, bits_per_sample, c->srcW);
        break;
    default:
        av_log(c, AV_LOG_ERROR,
               "unsupported planar RGB conversion %s -> %s\n",
               src_format->name, dst_format->name);
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
    const uint8_t *src102[] = { src[1], src[0], src[2] };
    const uint8_t *src201[] = { src[2], src[0], src[1] };
    int stride102[] = { srcStride[1], srcStride[0], srcStride[2] };
    int stride201[] = { srcStride[2], srcStride[0], srcStride[1] };

    if (c->srcFormat != AV_PIX_FMT_GBRP) {
        av_log(c, AV_LOG_ERROR, "unsupported planar RGB conversion %s -> %s\n",
               av_get_pix_fmt_name(c->srcFormat),
               av_get_pix_fmt_name(c->dstFormat));
        return srcSliceH;
    }

    switch (c->dstFormat) {
    case AV_PIX_FMT_BGR24:
        gbr24ptopacked24(src102, stride102,
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, c->srcW);
        break;

    case AV_PIX_FMT_RGB24:
        gbr24ptopacked24(src201, stride201,
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, c->srcW);
        break;

    case AV_PIX_FMT_ARGB:
        alpha_first = 1;
    case AV_PIX_FMT_RGBA:
        gbr24ptopacked32(src201, stride201,
                         dst[0] + srcSliceY * dstStride[0], dstStride[0],
                         srcSliceH, alpha_first, c->srcW);
        break;

    case AV_PIX_FMT_ABGR:
        alpha_first = 1;
    case AV_PIX_FMT_BGRA:
        gbr24ptopacked32(src102, stride102,
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

static int planarRgbToplanarRgbWrapper(SwsContext *c, const uint8_t *src[],
                                       int srcStride[], int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->srcW,
              dst[0], dstStride[0]);
    copyPlane(src[1], srcStride[1], srcSliceY, srcSliceH, c->srcW,
              dst[1], dstStride[1]);
    copyPlane(src[2], srcStride[2], srcSliceY, srcSliceH, c->srcW,
              dst[2], dstStride[2]);
    if (dst[3])
        fillPlane(dst[3], dstStride[3], c->srcW, srcSliceH, srcSliceY, 255);

    return srcSliceH;
}

static void packedtogbr24p(const uint8_t *src, int srcStride,
                           uint8_t *dst[], int dstStride[], int srcSliceH,
                           int alpha_first, int inc_size, int width)
{
    uint8_t *dest[3];
    int x, h;

    dest[0] = dst[0];
    dest[1] = dst[1];
    dest[2] = dst[2];

    if (alpha_first)
        src++;

    for (h = 0; h < srcSliceH; h++) {
        for (x = 0; x < width; x++) {
            dest[0][x] = src[0];
            dest[1][x] = src[1];
            dest[2][x] = src[2];

            src += inc_size;
        }
        src     += srcStride - width * inc_size;
        dest[0] += dstStride[0];
        dest[1] += dstStride[1];
        dest[2] += dstStride[2];
    }
}

static int rgbToPlanarRgbWrapper(SwsContext *c, const uint8_t *src[],
                                 int srcStride[], int srcSliceY, int srcSliceH,
                                 uint8_t *dst[], int dstStride[])
{
    int alpha_first = 0;
    int stride102[] = { dstStride[1], dstStride[0], dstStride[2] };
    int stride201[] = { dstStride[2], dstStride[0], dstStride[1] };
    uint8_t *dst102[] = { dst[1] + srcSliceY * dstStride[1],
                          dst[0] + srcSliceY * dstStride[0],
                          dst[2] + srcSliceY * dstStride[2] };
    uint8_t *dst201[] = { dst[2] + srcSliceY * dstStride[2],
                          dst[0] + srcSliceY * dstStride[0],
                          dst[1] + srcSliceY * dstStride[1] };

    switch (c->srcFormat) {
    case AV_PIX_FMT_RGB24:
        packedtogbr24p((const uint8_t *) src[0], srcStride[0], dst201,
                       stride201, srcSliceH, alpha_first, 3, c->srcW);
        break;
    case AV_PIX_FMT_BGR24:
        packedtogbr24p((const uint8_t *) src[0], srcStride[0], dst102,
                       stride102, srcSliceH, alpha_first, 3, c->srcW);
        break;
    case AV_PIX_FMT_ARGB:
        alpha_first = 1;
    case AV_PIX_FMT_RGBA:
        packedtogbr24p((const uint8_t *) src[0], srcStride[0], dst201,
                       stride201, srcSliceH, alpha_first, 4, c->srcW);
        break;
    case AV_PIX_FMT_ABGR:
        alpha_first = 1;
    case AV_PIX_FMT_BGRA:
        packedtogbr24p((const uint8_t *) src[0], srcStride[0], dst102,
                       stride102, srcSliceH, alpha_first, 4, c->srcW);
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
           (x) == AV_PIX_FMT_ARGB   \
        || (x) == AV_PIX_FMT_RGBA   \
        || (x) == AV_PIX_FMT_BGRA   \
        || (x) == AV_PIX_FMT_ABGR   \
        )

#define isRGBA64(x) (                \
           (x) == AV_PIX_FMT_RGBA64LE   \
        || (x) == AV_PIX_FMT_RGBA64BE   \
        || (x) == AV_PIX_FMT_BGRA64LE   \
        || (x) == AV_PIX_FMT_BGRA64BE   \
        )

#define isRGB48(x) (                \
           (x) == AV_PIX_FMT_RGB48LE   \
        || (x) == AV_PIX_FMT_RGB48BE   \
        || (x) == AV_PIX_FMT_BGR48LE   \
        || (x) == AV_PIX_FMT_BGR48BE   \
        )

/* {RGB,BGR}{15,16,24,32,32_1} -> {RGB,BGR}{15,16,24,32} */
typedef void (* rgbConvFn) (const uint8_t *, uint8_t *, int);
static rgbConvFn findRgbConvFn(SwsContext *c)
{
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    const int srcId = c->srcFormatBpp;
    const int dstId = c->dstFormatBpp;
    rgbConvFn conv = NULL;

#define IS_NOT_NE(bpp, desc) \
    (((bpp + 7) >> 3) == 2 && \
     (!(desc->flags & AV_PIX_FMT_FLAG_BE) != !HAVE_BIGENDIAN))

#define CONV_IS(src, dst) (srcFormat == AV_PIX_FMT_##src && dstFormat == AV_PIX_FMT_##dst)

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
    } else if (isRGB48(srcFormat) && isRGB48(dstFormat)) {
        if      (CONV_IS(RGB48LE, BGR48LE)
              || CONV_IS(BGR48LE, RGB48LE)
              || CONV_IS(RGB48BE, BGR48BE)
              || CONV_IS(BGR48BE, RGB48BE)) conv = rgb48tobgr48_nobswap;
        else if (CONV_IS(RGB48LE, BGR48BE)
              || CONV_IS(BGR48LE, RGB48BE)
              || CONV_IS(RGB48BE, BGR48LE)
              || CONV_IS(BGR48BE, RGB48LE)) conv = rgb48tobgr48_bswap;
    } else if (isRGBA64(srcFormat) && isRGB48(dstFormat)) {
        if      (CONV_IS(RGBA64LE, BGR48LE)
              || CONV_IS(BGRA64LE, RGB48LE)
              || CONV_IS(RGBA64BE, BGR48BE)
              || CONV_IS(BGRA64BE, RGB48BE)) conv = rgb64tobgr48_nobswap;
        else if (CONV_IS(RGBA64LE, BGR48BE)
              || CONV_IS(BGRA64LE, RGB48BE)
              || CONV_IS(RGBA64BE, BGR48LE)
              || CONV_IS(BGRA64BE, RGB48LE)) conv = rgb64tobgr48_bswap;
        else if (CONV_IS(RGBA64LE, RGB48LE)
              || CONV_IS(BGRA64LE, BGR48LE)
              || CONV_IS(RGBA64BE, RGB48BE)
              || CONV_IS(BGRA64BE, BGR48BE)) conv = rgb64to48_nobswap;
        else if (CONV_IS(RGBA64LE, RGB48BE)
              || CONV_IS(BGRA64LE, BGR48BE)
              || CONV_IS(RGBA64BE, RGB48LE)
              || CONV_IS(BGRA64BE, BGR48LE)) conv = rgb64to48_bswap;
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

    if ((dstFormat == AV_PIX_FMT_RGB32_1 || dstFormat == AV_PIX_FMT_BGR32_1) && !isRGBA32(srcFormat) && ALT32_CORR<0)
        return NULL;

    return conv;
}

/* {RGB,BGR}{15,16,24,32,32_1} -> {RGB,BGR}{15,16,24,32} */
static int rgbToRgbWrapper(SwsContext *c, const uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH, uint8_t *dst[],
                           int dstStride[])

{
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    const AVPixFmtDescriptor *desc_src = av_pix_fmt_desc_get(c->srcFormat);
    const AVPixFmtDescriptor *desc_dst = av_pix_fmt_desc_get(c->dstFormat);
    const int srcBpp = (c->srcFormatBpp + 7) >> 3;
    const int dstBpp = (c->dstFormatBpp + 7) >> 3;
    rgbConvFn conv = findRgbConvFn(c);

    if (!conv) {
        av_log(c, AV_LOG_ERROR, "internal error %s -> %s converter\n",
               av_get_pix_fmt_name(srcFormat), av_get_pix_fmt_name(dstFormat));
    } else {
        const uint8_t *srcPtr = src[0];
              uint8_t *dstPtr = dst[0];
        int src_bswap = IS_NOT_NE(c->srcFormatBpp, desc_src);
        int dst_bswap = IS_NOT_NE(c->dstFormatBpp, desc_dst);

        if ((srcFormat == AV_PIX_FMT_RGB32_1 || srcFormat == AV_PIX_FMT_BGR32_1) &&
            !isRGBA32(dstFormat))
            srcPtr += ALT32_CORR;

        if ((dstFormat == AV_PIX_FMT_RGB32_1 || dstFormat == AV_PIX_FMT_BGR32_1) &&
            !isRGBA32(srcFormat))
            dstPtr += ALT32_CORR;

        if (dstStride[0] * srcBpp == srcStride[0] * dstBpp && srcStride[0] > 0 &&
            !(srcStride[0] % srcBpp) && !dst_bswap && !src_bswap)
            conv(srcPtr, dstPtr + dstStride[0] * srcSliceY,
                 srcSliceH * srcStride[0]);
        else {
            int i, j;
            dstPtr += dstStride[0] * srcSliceY;

            for (i = 0; i < srcSliceH; i++) {
                if(src_bswap) {
                    for(j=0; j<c->srcW; j++)
                        ((uint16_t*)c->formatConvBuffer)[j] = av_bswap16(((uint16_t*)srcPtr)[j]);
                    conv(c->formatConvBuffer, dstPtr, c->srcW * srcBpp);
                }else
                    conv(srcPtr, dstPtr, c->srcW * srcBpp);
                if(dst_bswap)
                    for(j=0; j<c->srcW; j++)
                        ((uint16_t*)dstPtr)[j] = av_bswap16(((uint16_t*)dstPtr)[j]);
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
    ff_rgb24toyv12(
        src[0],
        dst[0] +  srcSliceY       * dstStride[0],
        dst[1] + (srcSliceY >> 1) * dstStride[1],
        dst[2] + (srcSliceY >> 1) * dstStride[2],
        c->srcW, srcSliceH,
        dstStride[0], dstStride[1], srcStride[0],
        c->input_rgb2yuv_table);
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
        av_assert1(length != 0);

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
    const AVPixFmtDescriptor *desc_src = av_pix_fmt_desc_get(c->srcFormat);
    const AVPixFmtDescriptor *desc_dst = av_pix_fmt_desc_get(c->dstFormat);
    int plane, i, j;
    for (plane = 0; plane < 4; plane++) {
        int length = (plane == 0 || plane == 3) ? c->srcW  : FF_CEIL_RSHIFT(c->srcW,   c->chrDstHSubSample);
        int y =      (plane == 0 || plane == 3) ? srcSliceY: FF_CEIL_RSHIFT(srcSliceY, c->chrDstVSubSample);
        int height = (plane == 0 || plane == 3) ? srcSliceH: FF_CEIL_RSHIFT(srcSliceH, c->chrDstVSubSample);
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
                        plane == 3, desc_dst->comp[plane].depth_minus1,
                        isBE(c->dstFormat));
            } else {
                fillPlane(dst[plane], dstStride[plane], length, height, y,
                        (plane == 3) ? 255 : 128);
            }
        } else {
            if(isNBPS(c->srcFormat) || isNBPS(c->dstFormat)
               || (is16BPS(c->srcFormat) != is16BPS(c->dstFormat))
            ) {
                const int src_depth = desc_src->comp[plane].depth_minus1 + 1;
                const int dst_depth = desc_dst->comp[plane].depth_minus1 + 1;
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
                    int orig_length = length;
                    for (i = 0; i < height; i++) {
                        if(isBE(c->srcFormat) == HAVE_BIGENDIAN &&
                           isBE(c->dstFormat) == HAVE_BIGENDIAN &&
                           shiftonly) {
                             unsigned shift = dst_depth - src_depth;
                             length = orig_length;
#if HAVE_FAST_64BIT
#define FAST_COPY_UP(shift) \
    for (j = 0; j < length - 3; j += 4) { \
        uint64_t v = AV_RN64A(srcPtr2 + j); \
        AV_WN64A(dstPtr2 + j, v << shift); \
    } \
    length &= 3;
#else
#define FAST_COPY_UP(shift) \
    for (j = 0; j < length - 1; j += 2) { \
        uint32_t v = AV_RN32A(srcPtr2 + j); \
        AV_WN32A(dstPtr2 + j, v << shift); \
    } \
    length &= 1;
#endif
                             switch (shift)
                             {
                             case 6: FAST_COPY_UP(6); break;
                             case 7: FAST_COPY_UP(7); break;
                             }
                        }
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
                else if (!desc_src->comp[0].depth_minus1)
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
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    const int flags = c->flags;
    const int dstH = c->dstH;
    int needsDither;

    needsDither = isAnyRGB(dstFormat) &&
            c->dstFormatBpp < 24 &&
           (c->dstFormatBpp < c->srcFormatBpp || (!isAnyRGB(srcFormat)));

    /* yv12_to_nv12 */
    if ((srcFormat == AV_PIX_FMT_YUV420P || srcFormat == AV_PIX_FMT_YUVA420P) &&
        (dstFormat == AV_PIX_FMT_NV12 || dstFormat == AV_PIX_FMT_NV21)) {
        c->swScale = planarToNv12Wrapper;
    }
    /* yuv2bgr */
    if ((srcFormat == AV_PIX_FMT_YUV420P || srcFormat == AV_PIX_FMT_YUV422P ||
         srcFormat == AV_PIX_FMT_YUVA420P) && isAnyRGB(dstFormat) &&
        !(flags & SWS_ACCURATE_RND) && c->dither != SWS_DITHER_ED && !(dstH & 1)) {
        c->swScale = ff_yuv2rgb_get_func_ptr(c);
    }

    if (srcFormat == AV_PIX_FMT_YUV410P &&
        (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P) &&
        !(flags & SWS_BITEXACT)) {
        c->swScale = yvu9ToYv12Wrapper;
    }

    /* bgr24toYV12 */
    if (srcFormat == AV_PIX_FMT_BGR24 &&
        (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P) &&
        !(flags & SWS_ACCURATE_RND))
        c->swScale = bgr24ToYv12Wrapper;

    /* RGB/BGR -> RGB/BGR (no dither needed forms) */
    if (isAnyRGB(srcFormat) && isAnyRGB(dstFormat) && findRgbConvFn(c)
        && (!needsDither || (c->flags&(SWS_FAST_BILINEAR|SWS_POINT))))
        c->swScale= rgbToRgbWrapper;

    if ((srcFormat == AV_PIX_FMT_GBRP && dstFormat == AV_PIX_FMT_GBRAP) ||
        (srcFormat == AV_PIX_FMT_GBRAP && dstFormat == AV_PIX_FMT_GBRP))
        c->swScale = planarRgbToplanarRgbWrapper;

#define isByteRGB(f) (             \
        f == AV_PIX_FMT_RGB32   || \
        f == AV_PIX_FMT_RGB32_1 || \
        f == AV_PIX_FMT_RGB24   || \
        f == AV_PIX_FMT_BGR32   || \
        f == AV_PIX_FMT_BGR32_1 || \
        f == AV_PIX_FMT_BGR24)

    if (srcFormat == AV_PIX_FMT_GBRP && isPlanar(srcFormat) && isByteRGB(dstFormat))
        c->swScale = planarRgbToRgbWrapper;

    if ((srcFormat == AV_PIX_FMT_GBRP9LE  || srcFormat == AV_PIX_FMT_GBRP9BE  ||
         srcFormat == AV_PIX_FMT_GBRP16LE || srcFormat == AV_PIX_FMT_GBRP16BE ||
         srcFormat == AV_PIX_FMT_GBRP10LE || srcFormat == AV_PIX_FMT_GBRP10BE ||
         srcFormat == AV_PIX_FMT_GBRP12LE || srcFormat == AV_PIX_FMT_GBRP12BE ||
         srcFormat == AV_PIX_FMT_GBRP14LE || srcFormat == AV_PIX_FMT_GBRP14BE) &&
        (dstFormat == AV_PIX_FMT_RGB48LE  || dstFormat == AV_PIX_FMT_RGB48BE  ||
         dstFormat == AV_PIX_FMT_BGR48LE  || dstFormat == AV_PIX_FMT_BGR48BE  ||
         dstFormat == AV_PIX_FMT_RGBA64LE || dstFormat == AV_PIX_FMT_RGBA64BE ||
         dstFormat == AV_PIX_FMT_BGRA64LE || dstFormat == AV_PIX_FMT_BGRA64BE))
        c->swScale = planarRgb16ToRgb16Wrapper;

    if (av_pix_fmt_desc_get(srcFormat)->comp[0].depth_minus1 == 7 &&
        isPackedRGB(srcFormat) && dstFormat == AV_PIX_FMT_GBRP)
        c->swScale = rgbToPlanarRgbWrapper;

    /* bswap 16 bits per pixel/component packed formats */
    if (IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR444) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR48)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGRA64) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR555) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR565) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GRAY16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRP9)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRP10) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRP12) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRP14) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRP16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRAP16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB444) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB48)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGBA64) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB555) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB565) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_XYZ12)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV420P9)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV420P10) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV420P12) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV420P14) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV420P16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV422P9)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV422P10) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV422P12) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV422P14) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV422P16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV444P9)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV444P10) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV444P12) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV444P14) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YUV444P16))
        c->swScale = packed_16bpc_bswap;

    if (usePal(srcFormat) && isByteRGB(dstFormat))
        c->swScale = palToRgbWrapper;

    if (srcFormat == AV_PIX_FMT_YUV422P) {
        if (dstFormat == AV_PIX_FMT_YUYV422)
            c->swScale = yuv422pToYuy2Wrapper;
        else if (dstFormat == AV_PIX_FMT_UYVY422)
            c->swScale = yuv422pToUyvyWrapper;
    }

    /* LQ converters if -sws 0 or -sws 4*/
    if (c->flags&(SWS_FAST_BILINEAR|SWS_POINT)) {
        /* yv12_to_yuy2 */
        if (srcFormat == AV_PIX_FMT_YUV420P || srcFormat == AV_PIX_FMT_YUVA420P) {
            if (dstFormat == AV_PIX_FMT_YUYV422)
                c->swScale = planarToYuy2Wrapper;
            else if (dstFormat == AV_PIX_FMT_UYVY422)
                c->swScale = planarToUyvyWrapper;
        }
    }
    if (srcFormat == AV_PIX_FMT_YUYV422 &&
       (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P))
        c->swScale = yuyvToYuv420Wrapper;
    if (srcFormat == AV_PIX_FMT_UYVY422 &&
       (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P))
        c->swScale = uyvyToYuv420Wrapper;
    if (srcFormat == AV_PIX_FMT_YUYV422 && dstFormat == AV_PIX_FMT_YUV422P)
        c->swScale = yuyvToYuv422Wrapper;
    if (srcFormat == AV_PIX_FMT_UYVY422 && dstFormat == AV_PIX_FMT_YUV422P)
        c->swScale = uyvyToYuv422Wrapper;

#define isPlanarGray(x) (isGray(x) && (x) != AV_PIX_FMT_GRAY8A)
    /* simple copy */
    if ( srcFormat == dstFormat ||
        (srcFormat == AV_PIX_FMT_YUVA420P && dstFormat == AV_PIX_FMT_YUV420P) ||
        (srcFormat == AV_PIX_FMT_YUV420P && dstFormat == AV_PIX_FMT_YUVA420P) ||
        (isPlanarYUV(srcFormat) && isPlanarGray(dstFormat)) ||
        (isPlanarYUV(dstFormat) && isPlanarGray(srcFormat)) ||
        (isPlanarGray(dstFormat) && isPlanarGray(srcFormat)) ||
        (isPlanarYUV(srcFormat) && isPlanarYUV(dstFormat) &&
         c->chrDstHSubSample == c->chrSrcHSubSample &&
         c->chrDstVSubSample == c->chrSrcVSubSample &&
         dstFormat != AV_PIX_FMT_NV12 && dstFormat != AV_PIX_FMT_NV21 &&
         srcFormat != AV_PIX_FMT_NV12 && srcFormat != AV_PIX_FMT_NV21))
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
