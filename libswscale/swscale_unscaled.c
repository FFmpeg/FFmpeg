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

DECLARE_ALIGNED(8, static const uint8_t, dither_8x8_1)[8][8] = {
    {   0,  1,  0,  1,  0,  1,  0,  1,},
    {   1,  0,  1,  0,  1,  0,  1,  0,},
    {   0,  1,  0,  1,  0,  1,  0,  1,},
    {   1,  0,  1,  0,  1,  0,  1,  0,},
    {   0,  1,  0,  1,  0,  1,  0,  1,},
    {   1,  0,  1,  0,  1,  0,  1,  0,},
    {   0,  1,  0,  1,  0,  1,  0,  1,},
    {   1,  0,  1,  0,  1,  0,  1,  0,},
};
DECLARE_ALIGNED(8, static const uint8_t, dither_8x8_3)[8][8] = {
    {   1,  2,  1,  2,  1,  2,  1,  2,},
    {   3,  0,  3,  0,  3,  0,  3,  0,},
    {   1,  2,  1,  2,  1,  2,  1,  2,},
    {   3,  0,  3,  0,  3,  0,  3,  0,},
    {   1,  2,  1,  2,  1,  2,  1,  2,},
    {   3,  0,  3,  0,  3,  0,  3,  0,},
    {   1,  2,  1,  2,  1,  2,  1,  2,},
    {   3,  0,  3,  0,  3,  0,  3,  0,},
};
DECLARE_ALIGNED(8, static const uint8_t, dither_8x8_64)[8][8] = {
    {  18, 34, 30, 46, 17, 33, 29, 45,},
    {  50,  2, 62, 14, 49,  1, 61, 13,},
    {  26, 42, 22, 38, 25, 41, 21, 37,},
    {  58, 10, 54,  6, 57,  9, 53,  5,},
    {  16, 32, 28, 44, 19, 35, 31, 47,},
    {  48,  0, 60, 12, 51,  3, 63, 15,},
    {  24, 40, 20, 36, 27, 43, 23, 39,},
    {  56,  8, 52,  4, 59, 11, 55,  7,},
};
DECLARE_ALIGNED(8, static const uint8_t, dither_8x8_256)[8][8] = {
    {  72, 136, 120, 184,  68, 132, 116, 180,},
    { 200,   8, 248,  56, 196,   4, 244,  52,},
    { 104, 168,  88, 152, 100, 164,  84, 148,},
    { 232,  40, 216,  24, 228,  36, 212,  20,},
    {  64, 128, 102, 176,  76, 140, 124, 188,},
    { 192,   0, 240,  48, 204,  12, 252,  60,},
    {  96, 160,  80, 144, 108, 172,  92, 156,},
    { 224,  32, 208,  16, 236,  44, 220,  28,},
};

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

static void fill_plane9or10(uint8_t *plane, int stride, int width,
                            int height, int y, uint8_t val,
                            const int dst_depth, const int big_endian)
{
    int i, j;
    uint16_t *dst = (uint16_t *) (plane + stride * y);
#define FILL8TO9_OR_10(wfunc) \
    for (i = 0; i < height; i++) { \
        for (j = 0; j < width; j++) { \
            wfunc(&dst[j], (val << (dst_depth - 8)) |  \
                               (val >> (16 - dst_depth))); \
        } \
        dst += stride / 2; \
    }
    if (big_endian) {
        FILL8TO9_OR_10(AV_WB16);
    } else {
        FILL8TO9_OR_10(AV_WL16);
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
                        srcStride[1], srcStride[2], dstStride[1]);
    else
        interleaveBytes(src[2], src[1], dst, c->srcW / 2, srcSliceH / 2,
                        srcStride[2], srcStride[1], dstStride[1]);

    return srcSliceH;
}

static int nv12ToPlanarWrapper(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t *dstParam[],
                               int dstStride[])
{
    uint8_t *dst1 = dstParam[1] + dstStride[1] * srcSliceY / 2;
    uint8_t *dst2 = dstParam[2] + dstStride[2] * srcSliceY / 2;

    copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->srcW,
              dstParam[0], dstStride[0]);

    if (c->srcFormat == AV_PIX_FMT_NV12)
        deinterleaveBytes(src[1], dst1, dst2,c->srcW / 2, srcSliceH / 2,
                          srcStride[1], dstStride[1], dstStride[2]);
    else
        deinterleaveBytes(src[1], dst2, dst1, c->srcW / 2, srcSliceH / 2,
                          srcStride[1], dstStride[2], dstStride[1]);

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
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    void (*conv)(const uint8_t *src, uint8_t *dst, int num_pixels,
                 const uint8_t *palette) = NULL;
    int i;
    uint8_t *dstPtr = dst[0] + dstStride[0] * srcSliceY;
    const uint8_t *srcPtr = src[0];

    if (srcFormat == AV_PIX_FMT_YA8) {
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
               sws_format_name(srcFormat), sws_format_name(dstFormat));
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

static int planarRgbToplanarRgbWrapper(SwsContext *c,
                                       const uint8_t *src[], int srcStride[],
                                       int srcSliceY, int srcSliceH,
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

/* {RGB,BGR}{15,16,24,32,32_1} -> {RGB,BGR}{15,16,24,32} */
typedef void (* rgbConvFn) (const uint8_t *, uint8_t *, int);
static rgbConvFn findRgbConvFn(SwsContext *c)
{
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    const int srcId = c->srcFormatBpp;
    const int dstId = c->dstFormatBpp;
    rgbConvFn conv = NULL;
    const AVPixFmtDescriptor *desc_src = av_pix_fmt_desc_get(srcFormat);
    const AVPixFmtDescriptor *desc_dst = av_pix_fmt_desc_get(dstFormat);

#define IS_NOT_NE(bpp, desc) \
    (((bpp + 7) >> 3) == 2 && \
     (!(desc->flags & AV_PIX_FMT_FLAG_BE) != !HAVE_BIGENDIAN))

    /* if this is non-native rgb444/555/565, don't handle it here. */
    if (IS_NOT_NE(srcId, desc_src) || IS_NOT_NE(dstId, desc_dst))
        return NULL;

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

    return conv;
}

/* {RGB,BGR}{15,16,24,32,32_1} -> {RGB,BGR}{15,16,24,32} */
static int rgbToRgbWrapper(SwsContext *c, const uint8_t *src[], int srcStride[],
                           int srcSliceY, int srcSliceH, uint8_t *dst[],
                           int dstStride[])

{
    const enum AVPixelFormat srcFormat = c->srcFormat;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    const int srcBpp = (c->srcFormatBpp + 7) >> 3;
    const int dstBpp = (c->dstFormatBpp + 7) >> 3;
    rgbConvFn conv = findRgbConvFn(c);

    if (!conv) {
        av_log(c, AV_LOG_ERROR, "internal error %s -> %s converter\n",
               sws_format_name(srcFormat), sws_format_name(dstFormat));
    } else {
        const uint8_t *srcPtr = src[0];
              uint8_t *dstPtr = dst[0];
        if ((srcFormat == AV_PIX_FMT_RGB32_1 || srcFormat == AV_PIX_FMT_BGR32_1) &&
            !isRGBA32(dstFormat))
            srcPtr += ALT32_CORR;

        if ((dstFormat == AV_PIX_FMT_RGB32_1 || dstFormat == AV_PIX_FMT_BGR32_1) &&
            !isRGBA32(srcFormat))
            dstPtr += ALT32_CORR;

        if (dstStride[0] * srcBpp == srcStride[0] * dstBpp && srcStride[0] > 0 &&
            !(srcStride[0] % srcBpp))
            conv(srcPtr, dstPtr + dstStride[0] * srcSliceY,
                 (srcSliceH - 1) * srcStride[0] + c->srcW * srcBpp);
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

#define clip9(x)  av_clip_uintp2(x,  9)
#define clip10(x) av_clip_uintp2(x, 10)
#define DITHER_COPY(dst, dstStride, wfunc, src, srcStride, rfunc, dithers, shift, clip) \
    for (i = 0; i < height; i++) { \
        const uint8_t *dither = dithers[i & 7]; \
        for (j = 0; j < length - 7; j += 8) { \
            wfunc(&dst[j + 0], clip((rfunc(&src[j + 0]) + dither[0]) >> shift)); \
            wfunc(&dst[j + 1], clip((rfunc(&src[j + 1]) + dither[1]) >> shift)); \
            wfunc(&dst[j + 2], clip((rfunc(&src[j + 2]) + dither[2]) >> shift)); \
            wfunc(&dst[j + 3], clip((rfunc(&src[j + 3]) + dither[3]) >> shift)); \
            wfunc(&dst[j + 4], clip((rfunc(&src[j + 4]) + dither[4]) >> shift)); \
            wfunc(&dst[j + 5], clip((rfunc(&src[j + 5]) + dither[5]) >> shift)); \
            wfunc(&dst[j + 6], clip((rfunc(&src[j + 6]) + dither[6]) >> shift)); \
            wfunc(&dst[j + 7], clip((rfunc(&src[j + 7]) + dither[7]) >> shift)); \
        } \
        for (; j < length; j++) \
            wfunc(&dst[j],     (rfunc(&src[j]) + dither[j & 7]) >> shift); \
        dst += dstStride; \
        src += srcStride; \
    }

static int planarCopyWrapper(SwsContext *c, const uint8_t *src[],
                             int srcStride[], int srcSliceY, int srcSliceH,
                             uint8_t *dst[], int dstStride[])
{
    const AVPixFmtDescriptor *desc_src = av_pix_fmt_desc_get(c->srcFormat);
    const AVPixFmtDescriptor *desc_dst = av_pix_fmt_desc_get(c->dstFormat);
    int plane, i, j;
    for (plane = 0; plane < 4; plane++) {
        int length = (plane == 0 || plane == 3) ? c->srcW  : AV_CEIL_RSHIFT(c->srcW,   c->chrDstHSubSample);
        int y =      (plane == 0 || plane == 3) ? srcSliceY: AV_CEIL_RSHIFT(srcSliceY, c->chrDstVSubSample);
        int height = (plane == 0 || plane == 3) ? srcSliceH: AV_CEIL_RSHIFT(srcSliceH, c->chrDstVSubSample);
        const uint8_t *srcPtr = src[plane];
        uint8_t *dstPtr = dst[plane] + dstStride[plane] * y;
        int shiftonly = plane == 1 || plane == 2 || (!c->srcRange && plane == 0);

        if (!dst[plane])
            continue;
        // ignore palette for GRAY8
        if (plane == 1 && !dst[2]) continue;
        if (!src[plane] || (plane == 1 && !src[2])) {
            int val = (plane == 3) ? 255 : 128;
            if (is16BPS(c->dstFormat))
                length *= 2;
            if (is9_OR_10BPS(c->dstFormat)) {
                fill_plane9or10(dst[plane], dstStride[plane],
                                length, height, y, val,
                                desc_dst->comp[plane].depth,
                                isBE(c->dstFormat));
            } else
                fillPlane(dst[plane], dstStride[plane], length, height, y,
                          val);
        } else {
            if (is9_OR_10BPS(c->srcFormat)) {
                const int src_depth = desc_src->comp[plane].depth;
                const int dst_depth = desc_dst->comp[plane].depth;
                const uint16_t *srcPtr2 = (const uint16_t *) srcPtr;

                if (is16BPS(c->dstFormat)) {
                    uint16_t *dstPtr2 = (uint16_t *) dstPtr;
#define COPY9_OR_10TO16(rfunc, wfunc) \
                    if (shiftonly) { \
                        for (i = 0; i < height; i++) { \
                            for (j = 0; j < length; j++) { \
                                int srcpx = rfunc(&srcPtr2[j]); \
                                wfunc(&dstPtr2[j], srcpx << (16 - src_depth)); \
                            } \
                            dstPtr2 += dstStride[plane] / 2; \
                            srcPtr2 += srcStride[plane] / 2; \
                        } \
                    } else { \
                        for (i = 0; i < height; i++) { \
                            for (j = 0; j < length; j++) { \
                                int srcpx = rfunc(&srcPtr2[j]); \
                                wfunc(&dstPtr2[j], (srcpx << (16 - src_depth)) | (srcpx >> (2 * src_depth - 16))); \
                            } \
                            dstPtr2 += dstStride[plane] / 2; \
                            srcPtr2 += srcStride[plane] / 2; \
                        } \
                    }
                    if (isBE(c->dstFormat)) {
                        if (isBE(c->srcFormat)) {
                            COPY9_OR_10TO16(AV_RB16, AV_WB16);
                        } else {
                            COPY9_OR_10TO16(AV_RL16, AV_WB16);
                        }
                    } else {
                        if (isBE(c->srcFormat)) {
                            COPY9_OR_10TO16(AV_RB16, AV_WL16);
                        } else {
                            COPY9_OR_10TO16(AV_RL16, AV_WL16);
                        }
                    }
                } else if (is9_OR_10BPS(c->dstFormat)) {
                    uint16_t *dstPtr2 = (uint16_t *) dstPtr;
#define COPY9_OR_10TO9_OR_10(loop) \
                    for (i = 0; i < height; i++) { \
                        for (j = 0; j < length; j++) { \
                            loop; \
                        } \
                        dstPtr2 += dstStride[plane] / 2; \
                        srcPtr2 += srcStride[plane] / 2; \
                    }
#define COPY9_OR_10TO9_OR_10_2(rfunc, wfunc) \
                    if (dst_depth > src_depth) { \
                        COPY9_OR_10TO9_OR_10(int srcpx = rfunc(&srcPtr2[j]); \
                            wfunc(&dstPtr2[j], (srcpx << 1) | (srcpx >> 9))); \
                    } else if (dst_depth < src_depth) { \
                        DITHER_COPY(dstPtr2, dstStride[plane] / 2, wfunc, \
                                    srcPtr2, srcStride[plane] / 2, rfunc, \
                                    dither_8x8_1, 1, clip9); \
                    } else { \
                        COPY9_OR_10TO9_OR_10(wfunc(&dstPtr2[j], rfunc(&srcPtr2[j]))); \
                    }
                    if (isBE(c->dstFormat)) {
                        if (isBE(c->srcFormat)) {
                            COPY9_OR_10TO9_OR_10_2(AV_RB16, AV_WB16);
                        } else {
                            COPY9_OR_10TO9_OR_10_2(AV_RL16, AV_WB16);
                        }
                    } else {
                        if (isBE(c->srcFormat)) {
                            COPY9_OR_10TO9_OR_10_2(AV_RB16, AV_WL16);
                        } else {
                            COPY9_OR_10TO9_OR_10_2(AV_RL16, AV_WL16);
                        }
                    }
                } else {
#define W8(a, b) { *(a) = (b); }
#define COPY9_OR_10TO8(rfunc) \
                    if (src_depth == 9) { \
                        DITHER_COPY(dstPtr,  dstStride[plane],   W8, \
                                    srcPtr2, srcStride[plane] / 2, rfunc, \
                                    dither_8x8_1, 1, av_clip_uint8); \
                    } else { \
                        DITHER_COPY(dstPtr,  dstStride[plane],   W8, \
                                    srcPtr2, srcStride[plane] / 2, rfunc, \
                                    dither_8x8_3, 2, av_clip_uint8); \
                    }
                    if (isBE(c->srcFormat)) {
                        COPY9_OR_10TO8(AV_RB16);
                    } else {
                        COPY9_OR_10TO8(AV_RL16);
                    }
                }
            } else if (is9_OR_10BPS(c->dstFormat)) {
                const int dst_depth = desc_dst->comp[plane].depth;
                uint16_t *dstPtr2 = (uint16_t *) dstPtr;

                if (is16BPS(c->srcFormat)) {
                    const uint16_t *srcPtr2 = (const uint16_t *) srcPtr;
#define COPY16TO9_OR_10(rfunc, wfunc) \
                    if (dst_depth == 9) { \
                        DITHER_COPY(dstPtr2, dstStride[plane] / 2, wfunc, \
                                    srcPtr2, srcStride[plane] / 2, rfunc, \
                                    ff_dither_8x8_128, 7, clip9); \
                    } else { \
                        DITHER_COPY(dstPtr2, dstStride[plane] / 2, wfunc, \
                                    srcPtr2, srcStride[plane] / 2, rfunc, \
                                    dither_8x8_64, 6, clip10); \
                    }
                    if (isBE(c->dstFormat)) {
                        if (isBE(c->srcFormat)) {
                            COPY16TO9_OR_10(AV_RB16, AV_WB16);
                        } else {
                            COPY16TO9_OR_10(AV_RL16, AV_WB16);
                        }
                    } else {
                        if (isBE(c->srcFormat)) {
                            COPY16TO9_OR_10(AV_RB16, AV_WL16);
                        } else {
                            COPY16TO9_OR_10(AV_RL16, AV_WL16);
                        }
                    }
                } else /* 8 bits */ {
#define COPY8TO9_OR_10(wfunc) \
                    if (shiftonly) { \
                        for (i = 0; i < height; i++) { \
                            for (j = 0; j < length; j++) { \
                                const int srcpx = srcPtr[j]; \
                                wfunc(&dstPtr2[j], srcpx << (dst_depth - 8)); \
                            } \
                            dstPtr2 += dstStride[plane] / 2; \
                            srcPtr  += srcStride[plane]; \
                        } \
                    } else { \
                        for (i = 0; i < height; i++) { \
                            for (j = 0; j < length; j++) { \
                                const int srcpx = srcPtr[j]; \
                                wfunc(&dstPtr2[j], (srcpx << (dst_depth - 8)) | (srcpx >> (16 - dst_depth))); \
                            } \
                            dstPtr2 += dstStride[plane] / 2; \
                            srcPtr  += srcStride[plane]; \
                        } \
                    }
                    if (isBE(c->dstFormat)) {
                        COPY8TO9_OR_10(AV_WB16);
                    } else {
                        COPY8TO9_OR_10(AV_WL16);
                    }
                }
            } else if (is16BPS(c->srcFormat) && !is16BPS(c->dstFormat)) {
                const uint16_t *srcPtr2 = (const uint16_t *) srcPtr;
#define COPY16TO8(rfunc) \
                    DITHER_COPY(dstPtr,  dstStride[plane],   W8, \
                                srcPtr2, srcStride[plane] / 2, rfunc, \
                                dither_8x8_256, 8, av_clip_uint8);
                if (isBE(c->srcFormat)) {
                    COPY16TO8(AV_RB16);
                } else {
                    COPY16TO8(AV_RL16);
                }
            } else if (!is16BPS(c->srcFormat) && is16BPS(c->dstFormat)) {
                for (i = 0; i < height; i++) {
                    for (j = 0; j < length; j++) {
                        dstPtr[ j << 1     ] = srcPtr[j];
                        dstPtr[(j << 1) + 1] = srcPtr[j];
                    }
                    srcPtr += srcStride[plane];
                    dstPtr += dstStride[plane];
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
                else if (desc_src->comp[0].depth == 1)
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
        c->swscale = planarToNv12Wrapper;
    }
    /* nv12_to_yv12 */
    if (dstFormat == AV_PIX_FMT_YUV420P &&
        (srcFormat == AV_PIX_FMT_NV12 || srcFormat == AV_PIX_FMT_NV21)) {
        c->swscale = nv12ToPlanarWrapper;
    }
    /* yuv2bgr */
    if ((srcFormat == AV_PIX_FMT_YUV420P || srcFormat == AV_PIX_FMT_YUV422P ||
         srcFormat == AV_PIX_FMT_YUVA420P) && isAnyRGB(dstFormat) &&
        !(flags & SWS_ACCURATE_RND) && !(dstH & 1)) {
        c->swscale = ff_yuv2rgb_get_func_ptr(c);
    }

    if (srcFormat == AV_PIX_FMT_YUV410P &&
        (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P) &&
        !(flags & SWS_BITEXACT)) {
        c->swscale = yvu9ToYv12Wrapper;
    }

    /* bgr24toYV12 */
    if (srcFormat == AV_PIX_FMT_BGR24 &&
        (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P) &&
        !(flags & SWS_ACCURATE_RND))
        c->swscale = bgr24ToYv12Wrapper;

    /* RGB/BGR -> RGB/BGR (no dither needed forms) */
    if (isAnyRGB(srcFormat) && isAnyRGB(dstFormat) && findRgbConvFn(c)
        && (!needsDither || (c->flags&(SWS_FAST_BILINEAR|SWS_POINT))))
        c->swscale = rgbToRgbWrapper;

    /* RGB to planar RGB */
    if ((srcFormat == AV_PIX_FMT_GBRP && dstFormat == AV_PIX_FMT_GBRAP) ||
        (srcFormat == AV_PIX_FMT_GBRAP && dstFormat == AV_PIX_FMT_GBRP))
        c->swscale = planarRgbToplanarRgbWrapper;

#define isByteRGB(f) (             \
        f == AV_PIX_FMT_RGB32   || \
        f == AV_PIX_FMT_RGB32_1 || \
        f == AV_PIX_FMT_RGB24   || \
        f == AV_PIX_FMT_BGR32   || \
        f == AV_PIX_FMT_BGR32_1 || \
        f == AV_PIX_FMT_BGR24)

    if (srcFormat == AV_PIX_FMT_GBRP && isPlanar(srcFormat) && isByteRGB(dstFormat))
        c->swscale = planarRgbToRgbWrapper;

    if (av_pix_fmt_desc_get(srcFormat)->comp[0].depth == 8 &&
        isPackedRGB(srcFormat) && dstFormat == AV_PIX_FMT_GBRP)
        c->swscale = rgbToPlanarRgbWrapper;

    /* bswap 16 bits per pixel/component packed formats */
    if (IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR444) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR48)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR555) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGR565) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_BGRA64) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GRAY16) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_YA16)   ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_GBRAP16)||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB444) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB48)  ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB555) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGB565) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_RGBA64) ||
        IS_DIFFERENT_ENDIANESS(srcFormat, dstFormat, AV_PIX_FMT_XYZ12))
        c->swscale = packed_16bpc_bswap;

    if ((usePal(srcFormat) && (
        dstFormat == AV_PIX_FMT_RGB32   ||
        dstFormat == AV_PIX_FMT_RGB32_1 ||
        dstFormat == AV_PIX_FMT_RGB24   ||
        dstFormat == AV_PIX_FMT_BGR32   ||
        dstFormat == AV_PIX_FMT_BGR32_1 ||
        dstFormat == AV_PIX_FMT_BGR24)))
        c->swscale = palToRgbWrapper;

    if (srcFormat == AV_PIX_FMT_YUV422P) {
        if (dstFormat == AV_PIX_FMT_YUYV422)
            c->swscale = yuv422pToYuy2Wrapper;
        else if (dstFormat == AV_PIX_FMT_UYVY422)
            c->swscale = yuv422pToUyvyWrapper;
    }

    /* LQ converters if -sws 0 or -sws 4*/
    if (c->flags&(SWS_FAST_BILINEAR|SWS_POINT)) {
        /* yv12_to_yuy2 */
        if (srcFormat == AV_PIX_FMT_YUV420P || srcFormat == AV_PIX_FMT_YUVA420P) {
            if (dstFormat == AV_PIX_FMT_YUYV422)
                c->swscale = planarToYuy2Wrapper;
            else if (dstFormat == AV_PIX_FMT_UYVY422)
                c->swscale = planarToUyvyWrapper;
        }
    }
    if (srcFormat == AV_PIX_FMT_YUYV422 &&
       (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P))
        c->swscale = yuyvToYuv420Wrapper;
    if (srcFormat == AV_PIX_FMT_UYVY422 &&
       (dstFormat == AV_PIX_FMT_YUV420P || dstFormat == AV_PIX_FMT_YUVA420P))
        c->swscale = uyvyToYuv420Wrapper;
    if (srcFormat == AV_PIX_FMT_YUYV422 && dstFormat == AV_PIX_FMT_YUV422P)
        c->swscale = yuyvToYuv422Wrapper;
    if (srcFormat == AV_PIX_FMT_UYVY422 && dstFormat == AV_PIX_FMT_YUV422P)
        c->swscale = uyvyToYuv422Wrapper;

    /* simple copy */
    if ( srcFormat == dstFormat ||
        (srcFormat == AV_PIX_FMT_YUVA420P && dstFormat == AV_PIX_FMT_YUV420P) ||
        (srcFormat == AV_PIX_FMT_YUV420P && dstFormat == AV_PIX_FMT_YUVA420P) ||
        (isPlanarYUV(srcFormat) && isGray(dstFormat)) ||
        (isPlanarYUV(dstFormat) && isGray(srcFormat)) ||
        (isGray(dstFormat) && isGray(srcFormat)) ||
        (isPlanarYUV(srcFormat) && isPlanarYUV(dstFormat) &&
         c->chrDstHSubSample == c->chrSrcHSubSample &&
         c->chrDstVSubSample == c->chrSrcVSubSample &&
         dstFormat != AV_PIX_FMT_NV12 && dstFormat != AV_PIX_FMT_NV21 &&
         dstFormat != AV_PIX_FMT_P010LE && dstFormat != AV_PIX_FMT_P010BE &&
         srcFormat != AV_PIX_FMT_NV12 && srcFormat != AV_PIX_FMT_NV21 &&
         srcFormat != AV_PIX_FMT_P010LE && srcFormat != AV_PIX_FMT_P010BE))
    {
        if (isPacked(c->srcFormat))
            c->swscale = packedCopyWrapper;
        else /* Planar YUV or gray */
            c->swscale = planarCopyWrapper;
    }

    if (ARCH_PPC)
        ff_get_unscaled_swscale_ppc(c);
}

static void reset_ptr(const uint8_t *src[], enum AVPixelFormat format)
{
    if (!isALPHA(format))
        src[3] = NULL;
    if (!isPlanar(format)) {
        src[3] = src[2] = NULL;

        if (!usePal(format))
            src[1] = NULL;
    }
}

static int check_image_pointers(uint8_t *data[4], enum AVPixelFormat pix_fmt,
                                const int linesizes[4])
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
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
    int i;
    const uint8_t *src2[4] = { srcSlice[0], srcSlice[1], srcSlice[2], srcSlice[3] };
    uint8_t *dst2[4] = { dst[0], dst[1], dst[2], dst[3] };

    // do not mess up sliceDir if we have a "trailing" 0-size slice
    if (srcSliceH == 0)
        return 0;

    if (!check_image_pointers(srcSlice, c->srcFormat, srcStride)) {
        av_log(c, AV_LOG_ERROR, "bad src image pointers\n");
        return 0;
    }
    if (!check_image_pointers(dst, c->dstFormat, dstStride)) {
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
            int r, g, b, y, u, v;
            if (c->srcFormat == AV_PIX_FMT_PAL8) {
                uint32_t p = ((const uint32_t *)(srcSlice[1]))[i];
                r = (p >> 16) & 0xFF;
                g = (p >>  8) & 0xFF;
                b =  p        & 0xFF;
            } else if (c->srcFormat == AV_PIX_FMT_RGB8) {
                r = ( i >> 5     ) * 36;
                g = ((i >> 2) & 7) * 36;
                b = ( i       & 3) * 85;
            } else if (c->srcFormat == AV_PIX_FMT_BGR8) {
                b = ( i >> 6     ) * 85;
                g = ((i >> 3) & 7) * 36;
                r = ( i       & 7) * 36;
            } else if (c->srcFormat == AV_PIX_FMT_RGB4_BYTE) {
                r = ( i >> 3     ) * 255;
                g = ((i >> 1) & 3) * 85;
                b = ( i       & 1) * 255;
            } else if (c->srcFormat == AV_PIX_FMT_GRAY8 ||
                       c->srcFormat == AV_PIX_FMT_YA8) {
                r = g = b = i;
            } else {
                assert(c->srcFormat == AV_PIX_FMT_BGR4_BYTE);
                b = ( i >> 3     ) * 255;
                g = ((i >> 1) & 3) * 85;
                r = ( i       & 1) * 255;
            }
            y = av_clip_uint8((RY * r + GY * g + BY * b + ( 33 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            u = av_clip_uint8((RU * r + GU * g + BU * b + (257 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            v = av_clip_uint8((RV * r + GV * g + BV * b + (257 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            c->pal_yuv[i] = y + (u << 8) + (v << 16) + (0xFFU << 24);

            switch (c->dstFormat) {
            case AV_PIX_FMT_BGR32:
#if !HAVE_BIGENDIAN
            case AV_PIX_FMT_RGB24:
#endif
                c->pal_rgb[i] =  r + (g << 8) + (b << 16) + (0xFFU << 24);
                break;
            case AV_PIX_FMT_BGR32_1:
#if HAVE_BIGENDIAN
            case AV_PIX_FMT_BGR24:
#endif
                c->pal_rgb[i] = 0xFF + (r << 8) + (g << 16) + ((unsigned)b << 24);
                break;
            case AV_PIX_FMT_RGB32_1:
#if HAVE_BIGENDIAN
            case AV_PIX_FMT_RGB24:
#endif
                c->pal_rgb[i] = 0xFF + (b << 8) + (g << 16) + ((unsigned)r << 24);
                break;
            case AV_PIX_FMT_RGB32:
#if !HAVE_BIGENDIAN
            case AV_PIX_FMT_BGR24:
#endif
            default:
                c->pal_rgb[i] =  b + (g << 8) + (r << 16) + (0xFFU << 24);
            }
        }
    }

    // copy strides, so they can safely be modified
    if (c->sliceDir == 1) {
        // slices go from top to bottom
        int srcStride2[4] = { srcStride[0], srcStride[1], srcStride[2],
                              srcStride[3] };
        int dstStride2[4] = { dstStride[0], dstStride[1], dstStride[2],
                              dstStride[3] };

        reset_ptr(src2, c->srcFormat);
        reset_ptr((const uint8_t **) dst2, c->dstFormat);

        /* reset slice direction at end of frame */
        if (srcSliceY + srcSliceH == c->srcH)
            c->sliceDir = 0;

        return c->swscale(c, src2, srcStride2, srcSliceY, srcSliceH, dst2,
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
        reset_ptr((const uint8_t **) dst2, c->dstFormat);

        /* reset slice direction at end of frame */
        if (!srcSliceY)
            c->sliceDir = 0;

        return c->swscale(c, src2, srcStride2, c->srcH-srcSliceY-srcSliceH,
                          srcSliceH, dst2, dstStride2);
    }
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
