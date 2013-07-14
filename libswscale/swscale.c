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
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/bswap.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "config.h"
#include "rgb2rgb.h"
#include "swscale_internal.h"
#include "swscale.h"

DECLARE_ALIGNED(8, const uint8_t, dither_8x8_128)[8][8] = {
    {  36, 68,  60, 92,  34, 66,  58, 90, },
    { 100,  4, 124, 28,  98,  2, 122, 26, },
    {  52, 84,  44, 76,  50, 82,  42, 74, },
    { 116, 20, 108, 12, 114, 18, 106, 10, },
    {  32, 64,  56, 88,  38, 70,  62, 94, },
    {  96,  0, 120, 24, 102,  6, 126, 30, },
    {  48, 80,  40, 72,  54, 86,  46, 78, },
    { 112, 16, 104,  8, 118, 22, 110, 14, },
};

DECLARE_ALIGNED(8, const uint8_t, ff_sws_pb_64)[8] = {
    64, 64, 64, 64, 64, 64, 64, 64
};

static av_always_inline void fillPlane(uint8_t *plane, int stride, int width,
                                       int height, int y, uint8_t val)
{
    int i;
    uint8_t *ptr = plane + stride * y;
    for (i = 0; i < height; i++) {
        memset(ptr, val, width);
        ptr += stride;
    }
}

static void hScale16To19_c(SwsContext *c, int16_t *_dst, int dstW,
                           const uint8_t *_src, const int16_t *filter,
                           const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int i;
    int32_t *dst        = (int32_t *) _dst;
    const uint16_t *src = (const uint16_t *) _src;
    int bits            = desc->comp[0].depth_minus1;
    int sh              = bits - 4;

    if((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth_minus1<15)
        sh= 9;

    for (i = 0; i < dstW; i++) {
        int j;
        int srcPos = filterPos[i];
        int val    = 0;

        for (j = 0; j < filterSize; j++) {
            val += src[srcPos + j] * filter[filterSize * i + j];
        }
        // filter=14 bit, input=16 bit, output=30 bit, >> 11 makes 19 bit
        dst[i] = FFMIN(val >> sh, (1 << 19) - 1);
    }
}

static void hScale16To15_c(SwsContext *c, int16_t *dst, int dstW,
                           const uint8_t *_src, const int16_t *filter,
                           const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int i;
    const uint16_t *src = (const uint16_t *) _src;
    int sh              = desc->comp[0].depth_minus1;

    if(sh<15)
        sh= isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 : desc->comp[0].depth_minus1;

    for (i = 0; i < dstW; i++) {
        int j;
        int srcPos = filterPos[i];
        int val    = 0;

        for (j = 0; j < filterSize; j++) {
            val += src[srcPos + j] * filter[filterSize * i + j];
        }
        // filter=14 bit, input=16 bit, output=30 bit, >> 15 makes 15 bit
        dst[i] = FFMIN(val >> sh, (1 << 15) - 1);
    }
}

// bilinear / bicubic scaling
static void hScale8To15_c(SwsContext *c, int16_t *dst, int dstW,
                          const uint8_t *src, const int16_t *filter,
                          const int32_t *filterPos, int filterSize)
{
    int i;
    for (i = 0; i < dstW; i++) {
        int j;
        int srcPos = filterPos[i];
        int val    = 0;
        for (j = 0; j < filterSize; j++) {
            val += ((int)src[srcPos + j]) * filter[filterSize * i + j];
        }
        dst[i] = FFMIN(val >> 7, (1 << 15) - 1); // the cubic equation does overflow ...
    }
}

static void hScale8To19_c(SwsContext *c, int16_t *_dst, int dstW,
                          const uint8_t *src, const int16_t *filter,
                          const int32_t *filterPos, int filterSize)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    for (i = 0; i < dstW; i++) {
        int j;
        int srcPos = filterPos[i];
        int val    = 0;
        for (j = 0; j < filterSize; j++) {
            val += ((int)src[srcPos + j]) * filter[filterSize * i + j];
        }
        dst[i] = FFMIN(val >> 3, (1 << 19) - 1); // the cubic equation does overflow ...
    }
}

// FIXME all pal and rgb srcFormats could do this conversion as well
// FIXME all scalers more complex than bilinear could do half of this transform
static void chrRangeToJpeg_c(int16_t *dstU, int16_t *dstV, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = (FFMIN(dstU[i], 30775) * 4663 - 9289992) >> 12; // -264
        dstV[i] = (FFMIN(dstV[i], 30775) * 4663 - 9289992) >> 12; // -264
    }
}

static void chrRangeFromJpeg_c(int16_t *dstU, int16_t *dstV, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = (dstU[i] * 1799 + 4081085) >> 11; // 1469
        dstV[i] = (dstV[i] * 1799 + 4081085) >> 11; // 1469
    }
}

static void lumRangeToJpeg_c(int16_t *dst, int width)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = (FFMIN(dst[i], 30189) * 19077 - 39057361) >> 14;
}

static void lumRangeFromJpeg_c(int16_t *dst, int width)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = (dst[i] * 14071 + 33561947) >> 14;
}

static void chrRangeToJpeg16_c(int16_t *_dstU, int16_t *_dstV, int width)
{
    int i;
    int32_t *dstU = (int32_t *) _dstU;
    int32_t *dstV = (int32_t *) _dstV;
    for (i = 0; i < width; i++) {
        dstU[i] = (FFMIN(dstU[i], 30775 << 4) * 4663 - (9289992 << 4)) >> 12; // -264
        dstV[i] = (FFMIN(dstV[i], 30775 << 4) * 4663 - (9289992 << 4)) >> 12; // -264
    }
}

static void chrRangeFromJpeg16_c(int16_t *_dstU, int16_t *_dstV, int width)
{
    int i;
    int32_t *dstU = (int32_t *) _dstU;
    int32_t *dstV = (int32_t *) _dstV;
    for (i = 0; i < width; i++) {
        dstU[i] = (dstU[i] * 1799 + (4081085 << 4)) >> 11; // 1469
        dstV[i] = (dstV[i] * 1799 + (4081085 << 4)) >> 11; // 1469
    }
}

static void lumRangeToJpeg16_c(int16_t *_dst, int width)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    for (i = 0; i < width; i++)
        dst[i] = (FFMIN(dst[i], 30189 << 4) * 4769 - (39057361 << 2)) >> 12;
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
    unsigned int xpos = 0;
    for (i = 0; i < dstWidth; i++) {
        register unsigned int xx     = xpos >> 16;
        register unsigned int xalpha = (xpos & 0xFFFF) >> 9;
        dst[i] = (src[xx] << 7) + (src[xx + 1] - src[xx]) * xalpha;
        xpos  += xInc;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
        dst[i] = src[srcW-1]*128;
}

// *** horizontal scale Y line to temp buffer
static av_always_inline void hyscale(SwsContext *c, int16_t *dst, int dstWidth,
                                     const uint8_t *src_in[4],
                                     int srcW, int xInc,
                                     const int16_t *hLumFilter,
                                     const int32_t *hLumFilterPos,
                                     int hLumFilterSize,
                                     uint8_t *formatConvBuffer,
                                     uint32_t *pal, int isAlpha)
{
    void (*toYV12)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, int, uint32_t *) =
        isAlpha ? c->alpToYV12 : c->lumToYV12;
    void (*convertRange)(int16_t *, int) = isAlpha ? NULL : c->lumConvertRange;
    const uint8_t *src = src_in[isAlpha ? 3 : 0];

    if (toYV12) {
        toYV12(formatConvBuffer, src, src_in[1], src_in[2], srcW, pal);
        src = formatConvBuffer;
    } else if (c->readLumPlanar && !isAlpha) {
        c->readLumPlanar(formatConvBuffer, src_in, srcW, c->input_rgb2yuv_table);
        src = formatConvBuffer;
    } else if (c->readAlpPlanar && isAlpha) {
        c->readAlpPlanar(formatConvBuffer, src_in, srcW, NULL);
        src = formatConvBuffer;
    }

    if (!c->hyscale_fast) {
        c->hyScale(c, dst, dstWidth, src, hLumFilter,
                   hLumFilterPos, hLumFilterSize);
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
    unsigned int xpos = 0;
    for (i = 0; i < dstWidth; i++) {
        register unsigned int xx     = xpos >> 16;
        register unsigned int xalpha = (xpos & 0xFFFF) >> 9;
        dst1[i] = (src1[xx] * (xalpha ^ 127) + src1[xx + 1] * xalpha);
        dst2[i] = (src2[xx] * (xalpha ^ 127) + src2[xx + 1] * xalpha);
        xpos   += xInc;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) {
        dst1[i] = src1[srcW-1]*128;
        dst2[i] = src2[srcW-1]*128;
    }
}

static av_always_inline void hcscale(SwsContext *c, int16_t *dst1,
                                     int16_t *dst2, int dstWidth,
                                     const uint8_t *src_in[4],
                                     int srcW, int xInc,
                                     const int16_t *hChrFilter,
                                     const int32_t *hChrFilterPos,
                                     int hChrFilterSize,
                                     uint8_t *formatConvBuffer, uint32_t *pal)
{
    const uint8_t *src1 = src_in[1], *src2 = src_in[2];
    if (c->chrToYV12) {
        uint8_t *buf2 = formatConvBuffer +
                        FFALIGN(srcW*2+78, 16);
        c->chrToYV12(formatConvBuffer, buf2, src_in[0], src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= buf2;
    } else if (c->readChrPlanar) {
        uint8_t *buf2 = formatConvBuffer +
                        FFALIGN(srcW*2+78, 16);
        c->readChrPlanar(formatConvBuffer, buf2, src_in, srcW, c->input_rgb2yuv_table);
        src1 = formatConvBuffer;
        src2 = buf2;
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

#define DEBUG_SWSCALE_BUFFERS 0
#define DEBUG_BUFFERS(...)                      \
    if (DEBUG_SWSCALE_BUFFERS)                  \
        av_log(c, AV_LOG_DEBUG, __VA_ARGS__)

static int swScale(SwsContext *c, const uint8_t *src[],
                   int srcStride[], int srcSliceY,
                   int srcSliceH, uint8_t *dst[], int dstStride[])
{
    /* load a few things into local vars to make the code more readable?
     * and faster */
    const int srcW                   = c->srcW;
    const int dstW                   = c->dstW;
    const int dstH                   = c->dstH;
    const int chrDstW                = c->chrDstW;
    const int chrSrcW                = c->chrSrcW;
    const int lumXInc                = c->lumXInc;
    const int chrXInc                = c->chrXInc;
    const enum AVPixelFormat dstFormat = c->dstFormat;
    const int flags                  = c->flags;
    int32_t *vLumFilterPos           = c->vLumFilterPos;
    int32_t *vChrFilterPos           = c->vChrFilterPos;
    int32_t *hLumFilterPos           = c->hLumFilterPos;
    int32_t *hChrFilterPos           = c->hChrFilterPos;
    int16_t *hLumFilter              = c->hLumFilter;
    int16_t *hChrFilter              = c->hChrFilter;
    int32_t *lumMmxFilter            = c->lumMmxFilter;
    int32_t *chrMmxFilter            = c->chrMmxFilter;
    const int vLumFilterSize         = c->vLumFilterSize;
    const int vChrFilterSize         = c->vChrFilterSize;
    const int hLumFilterSize         = c->hLumFilterSize;
    const int hChrFilterSize         = c->hChrFilterSize;
    int16_t **lumPixBuf              = c->lumPixBuf;
    int16_t **chrUPixBuf             = c->chrUPixBuf;
    int16_t **chrVPixBuf             = c->chrVPixBuf;
    int16_t **alpPixBuf              = c->alpPixBuf;
    const int vLumBufSize            = c->vLumBufSize;
    const int vChrBufSize            = c->vChrBufSize;
    uint8_t *formatConvBuffer        = c->formatConvBuffer;
    uint32_t *pal                    = c->pal_yuv;
    yuv2planar1_fn yuv2plane1        = c->yuv2plane1;
    yuv2planarX_fn yuv2planeX        = c->yuv2planeX;
    yuv2interleavedX_fn yuv2nv12cX   = c->yuv2nv12cX;
    yuv2packed1_fn yuv2packed1       = c->yuv2packed1;
    yuv2packed2_fn yuv2packed2       = c->yuv2packed2;
    yuv2packedX_fn yuv2packedX       = c->yuv2packedX;
    yuv2anyX_fn yuv2anyX             = c->yuv2anyX;
    const int chrSrcSliceY           =                srcSliceY >> c->chrSrcVSubSample;
    const int chrSrcSliceH           = FF_CEIL_RSHIFT(srcSliceH,   c->chrSrcVSubSample);
    int should_dither                = is9_OR_10BPS(c->srcFormat) ||
                                       is16BPS(c->srcFormat);
    int lastDstY;

    /* vars which will change and which we need to store back in the context */
    int dstY         = c->dstY;
    int lumBufIndex  = c->lumBufIndex;
    int chrBufIndex  = c->chrBufIndex;
    int lastInLumBuf = c->lastInLumBuf;
    int lastInChrBuf = c->lastInChrBuf;

    if (!usePal(c->srcFormat)) {
        pal = c->input_rgb2yuv_table;
    }

    if (isPacked(c->srcFormat)) {
        src[0] =
        src[1] =
        src[2] =
        src[3] = src[0];
        srcStride[0] =
        srcStride[1] =
        srcStride[2] =
        srcStride[3] = srcStride[0];
    }
    srcStride[1] <<= c->vChrDrop;
    srcStride[2] <<= c->vChrDrop;

    DEBUG_BUFFERS("swScale() %p[%d] %p[%d] %p[%d] %p[%d] -> %p[%d] %p[%d] %p[%d] %p[%d]\n",
                  src[0], srcStride[0], src[1], srcStride[1],
                  src[2], srcStride[2], src[3], srcStride[3],
                  dst[0], dstStride[0], dst[1], dstStride[1],
                  dst[2], dstStride[2], dst[3], dstStride[3]);
    DEBUG_BUFFERS("srcSliceY: %d srcSliceH: %d dstY: %d dstH: %d\n",
                  srcSliceY, srcSliceH, dstY, dstH);
    DEBUG_BUFFERS("vLumFilterSize: %d vLumBufSize: %d vChrFilterSize: %d vChrBufSize: %d\n",
                  vLumFilterSize, vLumBufSize, vChrFilterSize, vChrBufSize);

    if (dstStride[0]%16 !=0 || dstStride[1]%16 !=0 ||
        dstStride[2]%16 !=0 || dstStride[3]%16 != 0) {
        static int warnedAlready = 0; // FIXME maybe move this into the context
        if (flags & SWS_PRINT_INFO && !warnedAlready) {
            av_log(c, AV_LOG_WARNING,
                   "Warning: dstStride is not aligned!\n"
                   "         ->cannot do aligned memory accesses anymore\n");
            warnedAlready = 1;
        }
    }

    if (   (uintptr_t)dst[0]%16 || (uintptr_t)dst[1]%16 || (uintptr_t)dst[2]%16
        || (uintptr_t)src[0]%16 || (uintptr_t)src[1]%16 || (uintptr_t)src[2]%16
        || dstStride[0]%16 || dstStride[1]%16 || dstStride[2]%16 || dstStride[3]%16
        || srcStride[0]%16 || srcStride[1]%16 || srcStride[2]%16 || srcStride[3]%16
    ) {
        static int warnedAlready=0;
        int cpu_flags = av_get_cpu_flags();
        if (HAVE_MMXEXT && (cpu_flags & AV_CPU_FLAG_SSE2) && !warnedAlready){
            av_log(c, AV_LOG_WARNING, "Warning: data is not aligned! This can lead to a speedloss\n");
            warnedAlready=1;
        }
    }

    /* Note the user might start scaling the picture in the middle so this
     * will not get executed. This is not really intended but works
     * currently, so people might do it. */
    if (srcSliceY == 0) {
        lumBufIndex  = -1;
        chrBufIndex  = -1;
        dstY         = 0;
        lastInLumBuf = -1;
        lastInChrBuf = -1;
    }

    if (!should_dither) {
        c->chrDither8 = c->lumDither8 = ff_sws_pb_64;
    }
    lastDstY = dstY;

    for (; dstY < dstH; dstY++) {
        const int chrDstY = dstY >> c->chrDstVSubSample;
        uint8_t *dest[4]  = {
            dst[0] + dstStride[0] * dstY,
            dst[1] + dstStride[1] * chrDstY,
            dst[2] + dstStride[2] * chrDstY,
            (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? dst[3] + dstStride[3] * dstY : NULL,
        };
        int use_mmx_vfilter= c->use_mmx_vfilter;

        // First line needed as input
        const int firstLumSrcY  = FFMAX(1 - vLumFilterSize, vLumFilterPos[dstY]);
        const int firstLumSrcY2 = FFMAX(1 - vLumFilterSize, vLumFilterPos[FFMIN(dstY | ((1 << c->chrDstVSubSample) - 1), dstH - 1)]);
        // First line needed as input
        const int firstChrSrcY  = FFMAX(1 - vChrFilterSize, vChrFilterPos[chrDstY]);

        // Last line needed as input
        int lastLumSrcY  = FFMIN(c->srcH,    firstLumSrcY  + vLumFilterSize) - 1;
        int lastLumSrcY2 = FFMIN(c->srcH,    firstLumSrcY2 + vLumFilterSize) - 1;
        int lastChrSrcY  = FFMIN(c->chrSrcH, firstChrSrcY  + vChrFilterSize) - 1;
        int enough_lines;

        // handle holes (FAST_BILINEAR & weird filters)
        if (firstLumSrcY > lastInLumBuf)
            lastInLumBuf = firstLumSrcY - 1;
        if (firstChrSrcY > lastInChrBuf)
            lastInChrBuf = firstChrSrcY - 1;
        av_assert0(firstLumSrcY >= lastInLumBuf - vLumBufSize + 1);
        av_assert0(firstChrSrcY >= lastInChrBuf - vChrBufSize + 1);

        DEBUG_BUFFERS("dstY: %d\n", dstY);
        DEBUG_BUFFERS("\tfirstLumSrcY: %d lastLumSrcY: %d lastInLumBuf: %d\n",
                      firstLumSrcY, lastLumSrcY, lastInLumBuf);
        DEBUG_BUFFERS("\tfirstChrSrcY: %d lastChrSrcY: %d lastInChrBuf: %d\n",
                      firstChrSrcY, lastChrSrcY, lastInChrBuf);

        // Do we have enough lines in this slice to output the dstY line
        enough_lines = lastLumSrcY2 < srcSliceY + srcSliceH &&
                       lastChrSrcY < FF_CEIL_RSHIFT(srcSliceY + srcSliceH, c->chrSrcVSubSample);

        if (!enough_lines) {
            lastLumSrcY = srcSliceY + srcSliceH - 1;
            lastChrSrcY = chrSrcSliceY + chrSrcSliceH - 1;
            DEBUG_BUFFERS("buffering slice: lastLumSrcY %d lastChrSrcY %d\n",
                          lastLumSrcY, lastChrSrcY);
        }

        // Do horizontal scaling
        while (lastInLumBuf < lastLumSrcY) {
            const uint8_t *src1[4] = {
                src[0] + (lastInLumBuf + 1 - srcSliceY) * srcStride[0],
                src[1] + (lastInLumBuf + 1 - srcSliceY) * srcStride[1],
                src[2] + (lastInLumBuf + 1 - srcSliceY) * srcStride[2],
                src[3] + (lastInLumBuf + 1 - srcSliceY) * srcStride[3],
            };
            lumBufIndex++;
            av_assert0(lumBufIndex < 2 * vLumBufSize);
            av_assert0(lastInLumBuf + 1 - srcSliceY < srcSliceH);
            av_assert0(lastInLumBuf + 1 - srcSliceY >= 0);
            hyscale(c, lumPixBuf[lumBufIndex], dstW, src1, srcW, lumXInc,
                    hLumFilter, hLumFilterPos, hLumFilterSize,
                    formatConvBuffer, pal, 0);
            if (CONFIG_SWSCALE_ALPHA && alpPixBuf)
                hyscale(c, alpPixBuf[lumBufIndex], dstW, src1, srcW,
                        lumXInc, hLumFilter, hLumFilterPos, hLumFilterSize,
                        formatConvBuffer, pal, 1);
            lastInLumBuf++;
            DEBUG_BUFFERS("\t\tlumBufIndex %d: lastInLumBuf: %d\n",
                          lumBufIndex, lastInLumBuf);
        }
        while (lastInChrBuf < lastChrSrcY) {
            const uint8_t *src1[4] = {
                src[0] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[0],
                src[1] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[1],
                src[2] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[2],
                src[3] + (lastInChrBuf + 1 - chrSrcSliceY) * srcStride[3],
            };
            chrBufIndex++;
            av_assert0(chrBufIndex < 2 * vChrBufSize);
            av_assert0(lastInChrBuf + 1 - chrSrcSliceY < (chrSrcSliceH));
            av_assert0(lastInChrBuf + 1 - chrSrcSliceY >= 0);
            // FIXME replace parameters through context struct (some at least)

            if (c->needs_hcscale)
                hcscale(c, chrUPixBuf[chrBufIndex], chrVPixBuf[chrBufIndex],
                        chrDstW, src1, chrSrcW, chrXInc,
                        hChrFilter, hChrFilterPos, hChrFilterSize,
                        formatConvBuffer, pal);
            lastInChrBuf++;
            DEBUG_BUFFERS("\t\tchrBufIndex %d: lastInChrBuf: %d\n",
                          chrBufIndex, lastInChrBuf);
        }
        // wrap buf index around to stay inside the ring buffer
        if (lumBufIndex >= vLumBufSize)
            lumBufIndex -= vLumBufSize;
        if (chrBufIndex >= vChrBufSize)
            chrBufIndex -= vChrBufSize;
        if (!enough_lines)
            break;  // we can't output a dstY line so let's try with the next slice

#if HAVE_MMX_INLINE
        updateMMXDitherTables(c, dstY, lumBufIndex, chrBufIndex,
                              lastInLumBuf, lastInChrBuf);
#endif
        if (should_dither) {
            c->chrDither8 = dither_8x8_128[chrDstY & 7];
            c->lumDither8 = dither_8x8_128[dstY    & 7];
        }
        if (dstY >= dstH - 2) {
            /* hmm looks like we can't use MMX here without overwriting
             * this array's tail */
            ff_sws_init_output_funcs(c, &yuv2plane1, &yuv2planeX, &yuv2nv12cX,
                                     &yuv2packed1, &yuv2packed2, &yuv2packedX, &yuv2anyX);
            use_mmx_vfilter= 0;
        }

        {
            const int16_t **lumSrcPtr  = (const int16_t **)(void*) lumPixBuf  + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            const int16_t **chrUSrcPtr = (const int16_t **)(void*) chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **chrVSrcPtr = (const int16_t **)(void*) chrVPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **alpSrcPtr  = (CONFIG_SWSCALE_ALPHA && alpPixBuf) ?
                                         (const int16_t **)(void*) alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
            int16_t *vLumFilter = c->vLumFilter;
            int16_t *vChrFilter = c->vChrFilter;

            if (isPlanarYUV(dstFormat) ||
                (isGray(dstFormat) && !isALPHA(dstFormat))) { // YV12 like
                const int chrSkipMask = (1 << c->chrDstVSubSample) - 1;

                vLumFilter +=    dstY * vLumFilterSize;
                vChrFilter += chrDstY * vChrFilterSize;

//                 av_assert0(use_mmx_vfilter != (
//                                yuv2planeX == yuv2planeX_10BE_c
//                             || yuv2planeX == yuv2planeX_10LE_c
//                             || yuv2planeX == yuv2planeX_9BE_c
//                             || yuv2planeX == yuv2planeX_9LE_c
//                             || yuv2planeX == yuv2planeX_16BE_c
//                             || yuv2planeX == yuv2planeX_16LE_c
//                             || yuv2planeX == yuv2planeX_8_c) || !ARCH_X86);

                if(use_mmx_vfilter){
                    vLumFilter= (int16_t *)c->lumMmxFilter;
                    vChrFilter= (int16_t *)c->chrMmxFilter;
                }

                if (vLumFilterSize == 1) {
                    yuv2plane1(lumSrcPtr[0], dest[0], dstW, c->lumDither8, 0);
                } else {
                    yuv2planeX(vLumFilter, vLumFilterSize,
                               lumSrcPtr, dest[0],
                               dstW, c->lumDither8, 0);
                }

                if (!((dstY & chrSkipMask) || isGray(dstFormat))) {
                    if (yuv2nv12cX) {
                        yuv2nv12cX(c, vChrFilter,
                                   vChrFilterSize, chrUSrcPtr, chrVSrcPtr,
                                   dest[1], chrDstW);
                    } else if (vChrFilterSize == 1) {
                        yuv2plane1(chrUSrcPtr[0], dest[1], chrDstW, c->chrDither8, 0);
                        yuv2plane1(chrVSrcPtr[0], dest[2], chrDstW, c->chrDither8, 3);
                    } else {
                        yuv2planeX(vChrFilter,
                                   vChrFilterSize, chrUSrcPtr, dest[1],
                                   chrDstW, c->chrDither8, 0);
                        yuv2planeX(vChrFilter,
                                   vChrFilterSize, chrVSrcPtr, dest[2],
                                   chrDstW, c->chrDither8, use_mmx_vfilter ? (c->uv_offx2 >> 1) : 3);
                    }
                }

                if (CONFIG_SWSCALE_ALPHA && alpPixBuf) {
                    if(use_mmx_vfilter){
                        vLumFilter= (int16_t *)c->alpMmxFilter;
                    }
                    if (vLumFilterSize == 1) {
                        yuv2plane1(alpSrcPtr[0], dest[3], dstW,
                                   c->lumDither8, 0);
                    } else {
                        yuv2planeX(vLumFilter,
                                   vLumFilterSize, alpSrcPtr, dest[3],
                                   dstW, c->lumDither8, 0);
                    }
                }
            } else if (yuv2packedX) {
                av_assert1(lumSrcPtr  + vLumFilterSize - 1 < (const int16_t **)lumPixBuf  + vLumBufSize * 2);
                av_assert1(chrUSrcPtr + vChrFilterSize - 1 < (const int16_t **)chrUPixBuf + vChrBufSize * 2);
                if (c->yuv2packed1 && vLumFilterSize == 1 &&
                    vChrFilterSize <= 2) { // unscaled RGB
                    int chrAlpha = vChrFilterSize == 1 ? 0 : vChrFilter[2 * dstY + 1];
                    yuv2packed1(c, *lumSrcPtr, chrUSrcPtr, chrVSrcPtr,
                                alpPixBuf ? *alpSrcPtr : NULL,
                                dest[0], dstW, chrAlpha, dstY);
                } else if (c->yuv2packed2 && vLumFilterSize == 2 &&
                           vChrFilterSize == 2) { // bilinear upscale RGB
                    int lumAlpha = vLumFilter[2 * dstY + 1];
                    int chrAlpha = vChrFilter[2 * dstY + 1];
                    lumMmxFilter[2] =
                    lumMmxFilter[3] = vLumFilter[2 * dstY]    * 0x10001;
                    chrMmxFilter[2] =
                    chrMmxFilter[3] = vChrFilter[2 * chrDstY] * 0x10001;
                    yuv2packed2(c, lumSrcPtr, chrUSrcPtr, chrVSrcPtr,
                                alpPixBuf ? alpSrcPtr : NULL,
                                dest[0], dstW, lumAlpha, chrAlpha, dstY);
                } else { // general RGB
                    yuv2packedX(c, vLumFilter + dstY * vLumFilterSize,
                                lumSrcPtr, vLumFilterSize,
                                vChrFilter + dstY * vChrFilterSize,
                                chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                alpSrcPtr, dest[0], dstW, dstY);
                }
            } else {
                av_assert1(!yuv2packed1 && !yuv2packed2);
                yuv2anyX(c, vLumFilter + dstY * vLumFilterSize,
                         lumSrcPtr, vLumFilterSize,
                         vChrFilter + dstY * vChrFilterSize,
                         chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                         alpSrcPtr, dest, dstW, dstY);
            }
        }
    }
    if (isPlanar(dstFormat) && isALPHA(dstFormat) && !alpPixBuf) {
        int length = dstW;
        int height = dstY - lastDstY;

        if (is16BPS(dstFormat) || isNBPS(dstFormat)) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dstFormat);
            fillPlane16(dst[3], dstStride[3], length, height, lastDstY,
                    1, desc->comp[3].depth_minus1,
                    isBE(dstFormat));
        } else
            fillPlane(dst[3], dstStride[3], length, height, lastDstY, 255);
    }

#if HAVE_MMXEXT_INLINE
    if (av_get_cpu_flags() & AV_CPU_FLAG_MMXEXT)
        __asm__ volatile ("sfence" ::: "memory");
#endif
    emms_c();

    /* store changed local vars back in the context */
    c->dstY         = dstY;
    c->lumBufIndex  = lumBufIndex;
    c->chrBufIndex  = chrBufIndex;
    c->lastInLumBuf = lastInLumBuf;
    c->lastInChrBuf = lastInChrBuf;

    return dstY - lastDstY;
}

static av_cold void sws_init_swScale_c(SwsContext *c)
{
    enum AVPixelFormat srcFormat = c->srcFormat;

    ff_sws_init_output_funcs(c, &c->yuv2plane1, &c->yuv2planeX,
                             &c->yuv2nv12cX, &c->yuv2packed1,
                             &c->yuv2packed2, &c->yuv2packedX, &c->yuv2anyX);

    ff_sws_init_input_funcs(c);


    if (c->srcBpc == 8) {
        if (c->dstBpc <= 14) {
            c->hyScale = c->hcScale = hScale8To15_c;
            if (c->flags & SWS_FAST_BILINEAR) {
                c->hyscale_fast = hyscale_fast_c;
                c->hcscale_fast = hcscale_fast_c;
            }
        } else {
            c->hyScale = c->hcScale = hScale8To19_c;
        }
    } else {
        c->hyScale = c->hcScale = c->dstBpc > 14 ? hScale16To19_c
                                                 : hScale16To15_c;
    }

    if (c->srcRange != c->dstRange && !isAnyRGB(c->dstFormat)) {
        if (c->dstBpc <= 14) {
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
          srcFormat == AV_PIX_FMT_MONOBLACK || srcFormat == AV_PIX_FMT_MONOWHITE))
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

static int check_image_pointers(const uint8_t * const data[4], enum AVPixelFormat pix_fmt,
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

static void xyz12Torgb48(struct SwsContext *c, uint16_t *dst,
                         const uint16_t *src, int stride, int h)
{
    int xp,yp;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);

    for (yp=0; yp<h; yp++) {
        for (xp=0; xp+2<stride; xp+=3) {
            int x, y, z, r, g, b;

            if (desc->flags & AV_PIX_FMT_FLAG_BE) {
                x = AV_RB16(src + xp + 0);
                y = AV_RB16(src + xp + 1);
                z = AV_RB16(src + xp + 2);
            } else {
                x = AV_RL16(src + xp + 0);
                y = AV_RL16(src + xp + 1);
                z = AV_RL16(src + xp + 2);
            }

            x = c->xyzgamma[x>>4];
            y = c->xyzgamma[y>>4];
            z = c->xyzgamma[z>>4];

            // convert from XYZlinear to sRGBlinear
            r = c->xyz2rgb_matrix[0][0] * x +
                c->xyz2rgb_matrix[0][1] * y +
                c->xyz2rgb_matrix[0][2] * z >> 12;
            g = c->xyz2rgb_matrix[1][0] * x +
                c->xyz2rgb_matrix[1][1] * y +
                c->xyz2rgb_matrix[1][2] * z >> 12;
            b = c->xyz2rgb_matrix[2][0] * x +
                c->xyz2rgb_matrix[2][1] * y +
                c->xyz2rgb_matrix[2][2] * z >> 12;

            // limit values to 12-bit depth
            r = av_clip_c(r,0,4095);
            g = av_clip_c(g,0,4095);
            b = av_clip_c(b,0,4095);

            // convert from sRGBlinear to RGB and scale from 12bit to 16bit
            if (desc->flags & AV_PIX_FMT_FLAG_BE) {
                AV_WB16(dst + xp + 0, c->rgbgamma[r] << 4);
                AV_WB16(dst + xp + 1, c->rgbgamma[g] << 4);
                AV_WB16(dst + xp + 2, c->rgbgamma[b] << 4);
            } else {
                AV_WL16(dst + xp + 0, c->rgbgamma[r] << 4);
                AV_WL16(dst + xp + 1, c->rgbgamma[g] << 4);
                AV_WL16(dst + xp + 2, c->rgbgamma[b] << 4);
            }
        }
        src += stride;
        dst += stride;
    }
}

static void rgb48Toxyz12(struct SwsContext *c, uint16_t *dst,
                         const uint16_t *src, int stride, int h)
{
    int xp,yp;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);

    for (yp=0; yp<h; yp++) {
        for (xp=0; xp+2<stride; xp+=3) {
            int x, y, z, r, g, b;

            if (desc->flags & AV_PIX_FMT_FLAG_BE) {
                r = AV_RB16(src + xp + 0);
                g = AV_RB16(src + xp + 1);
                b = AV_RB16(src + xp + 2);
            } else {
                r = AV_RL16(src + xp + 0);
                g = AV_RL16(src + xp + 1);
                b = AV_RL16(src + xp + 2);
            }

            r = c->rgbgammainv[r>>4];
            g = c->rgbgammainv[g>>4];
            b = c->rgbgammainv[b>>4];

            // convert from sRGBlinear to XYZlinear
            x = c->rgb2xyz_matrix[0][0] * r +
                c->rgb2xyz_matrix[0][1] * g +
                c->rgb2xyz_matrix[0][2] * b >> 12;
            y = c->rgb2xyz_matrix[1][0] * r +
                c->rgb2xyz_matrix[1][1] * g +
                c->rgb2xyz_matrix[1][2] * b >> 12;
            z = c->rgb2xyz_matrix[2][0] * r +
                c->rgb2xyz_matrix[2][1] * g +
                c->rgb2xyz_matrix[2][2] * b >> 12;

            // limit values to 12-bit depth
            x = av_clip_c(x,0,4095);
            y = av_clip_c(y,0,4095);
            z = av_clip_c(z,0,4095);

            // convert from XYZlinear to X'Y'Z' and scale from 12bit to 16bit
            if (desc->flags & AV_PIX_FMT_FLAG_BE) {
                AV_WB16(dst + xp + 0, c->xyzgammainv[x] << 4);
                AV_WB16(dst + xp + 1, c->xyzgammainv[y] << 4);
                AV_WB16(dst + xp + 2, c->xyzgammainv[z] << 4);
            } else {
                AV_WL16(dst + xp + 0, c->xyzgammainv[x] << 4);
                AV_WL16(dst + xp + 1, c->xyzgammainv[y] << 4);
                AV_WL16(dst + xp + 2, c->xyzgammainv[z] << 4);
            }
        }
        src += stride;
        dst += stride;
    }
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
    const uint8_t *src2[4];
    uint8_t *dst2[4];
    uint8_t *rgb0_tmp = NULL;

    if (!srcSlice || !dstStride || !dst || !srcSlice) {
        av_log(c, AV_LOG_ERROR, "One of the input parameters to sws_scale() is NULL, please check the calling code\n");
        return 0;
    }
    memcpy(src2, srcSlice, sizeof(src2));
    memcpy(dst2, dst, sizeof(dst2));

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
            if (c->srcFormat == AV_PIX_FMT_PAL8) {
                p = ((const uint32_t *)(srcSlice[1]))[i];
                a = (p >> 24) & 0xFF;
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
            } else if (c->srcFormat == AV_PIX_FMT_GRAY8 || c->srcFormat == AV_PIX_FMT_GRAY8A) {
                r = g = b = i;
            } else {
                av_assert1(c->srcFormat == AV_PIX_FMT_BGR4_BYTE);
                b = ( i >> 3     ) * 255;
                g = ((i >> 1) & 3) * 85;
                r = ( i       & 1) * 255;
            }
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

            y = av_clip_uint8((RY * r + GY * g + BY * b + ( 33 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            u = av_clip_uint8((RU * r + GU * g + BU * b + (257 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            v = av_clip_uint8((RV * r + GV * g + BV * b + (257 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT);
            c->pal_yuv[i]= y + (u<<8) + (v<<16) + ((unsigned)a<<24);

            switch (c->dstFormat) {
            case AV_PIX_FMT_BGR32:
#if !HAVE_BIGENDIAN
            case AV_PIX_FMT_RGB24:
#endif
                c->pal_rgb[i]=  r + (g<<8) + (b<<16) + ((unsigned)a<<24);
                break;
            case AV_PIX_FMT_BGR32_1:
#if HAVE_BIGENDIAN
            case AV_PIX_FMT_BGR24:
#endif
                c->pal_rgb[i]= a + (r<<8) + (g<<16) + ((unsigned)b<<24);
                break;
            case AV_PIX_FMT_RGB32_1:
#if HAVE_BIGENDIAN
            case AV_PIX_FMT_RGB24:
#endif
                c->pal_rgb[i]= a + (b<<8) + (g<<16) + ((unsigned)r<<24);
                break;
            case AV_PIX_FMT_RGB32:
#if !HAVE_BIGENDIAN
            case AV_PIX_FMT_BGR24:
#endif
            default:
                c->pal_rgb[i]=  b + (g<<8) + (r<<16) + ((unsigned)a<<24);
            }
        }
    }

    if (c->src0Alpha && !c->dst0Alpha && isALPHA(c->dstFormat)) {
        uint8_t *base;
        int x,y;
        rgb0_tmp = av_malloc(FFABS(srcStride[0]) * srcSliceH + 32);
        if (!rgb0_tmp)
            return AVERROR(ENOMEM);

        base = srcStride[0] < 0 ? rgb0_tmp - srcStride[0] * (srcSliceH-1) : rgb0_tmp;
        for (y=0; y<srcSliceH; y++){
            memcpy(base + srcStride[0]*y, src2[0] + srcStride[0]*y, 4*c->srcW);
            for (x=c->src0Alpha-1; x<4*c->srcW; x+=4) {
                base[ srcStride[0]*y + x] = 0xFF;
            }
        }
        src2[0] = base;
    }

    if (c->srcXYZ && !(c->dstXYZ && c->srcW==c->dstW && c->srcH==c->dstH)) {
        uint8_t *base;
        rgb0_tmp = av_malloc(FFABS(srcStride[0]) * srcSliceH + 32);
        if (!rgb0_tmp)
            return AVERROR(ENOMEM);

        base = srcStride[0] < 0 ? rgb0_tmp - srcStride[0] * (srcSliceH-1) : rgb0_tmp;

        xyz12Torgb48(c, (uint16_t*)base, (const uint16_t*)src2[0], srcStride[0]/2, srcSliceH);
        src2[0] = base;
    }

    if (!srcSliceY && (c->flags & SWS_BITEXACT) && (c->flags & SWS_ERROR_DIFFUSION) && c->dither_error[0])
        for (i = 0; i < 4; i++)
            memset(c->dither_error[i], 0, sizeof(c->dither_error[0][0]) * (c->dstW+2));


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


    if (c->dstXYZ && !(c->srcXYZ && c->srcW==c->dstW && c->srcH==c->dstH)) {
        /* replace on the same data */
        rgb48Toxyz12(c, (uint16_t*)dst2[0], (const uint16_t*)dst2[0], dstStride[0]/2, ret);
    }

    av_free(rgb0_tmp);
    return ret;
}

