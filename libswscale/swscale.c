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

#include <assert.h>
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
    int i;
    int32_t *dst        = (int32_t *) _dst;
    const uint16_t *src = (const uint16_t *) _src;
    int bits            = av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;
    int sh              = bits - 4;

    if((isAnyRGB(c->srcFormat) || c->srcFormat==PIX_FMT_PAL8) && av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1<15)
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
    int i;
    const uint16_t *src = (const uint16_t *) _src;
    int sh              = av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;

    if(sh<15)
        sh= isAnyRGB(c->srcFormat) || c->srcFormat==PIX_FMT_PAL8 ? 13 : av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;

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

// FIXME all pal and rgb srcFormats could do this convertion as well
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
        c->readLumPlanar(formatConvBuffer, src_in, srcW);
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
        c->readChrPlanar(formatConvBuffer, buf2, src_in, srcW);
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
    const enum PixelFormat dstFormat = c->dstFormat;
    const int flags                  = c->flags;
    int32_t *vLumFilterPos           = c->vLumFilterPos;
    int32_t *vChrFilterPos           = c->vChrFilterPos;
    int32_t *hLumFilterPos           = c->hLumFilterPos;
    int32_t *hChrFilterPos           = c->hChrFilterPos;
    int16_t *vLumFilter              = c->vLumFilter;
    int16_t *vChrFilter              = c->vChrFilter;
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
    const int chrSrcSliceY           =     srcSliceY  >> c->chrSrcVSubSample;
    const int chrSrcSliceH           = -((-srcSliceH) >> c->chrSrcVSubSample);
    int should_dither                = is9_OR_10BPS(c->srcFormat) ||
                                       is16BPS(c->srcFormat);
    int lastDstY;

    /* vars which will change and which we need to store back in the context */
    int dstY         = c->dstY;
    int lumBufIndex  = c->lumBufIndex;
    int chrBufIndex  = c->chrBufIndex;
    int lastInLumBuf = c->lastInLumBuf;
    int lastInChrBuf = c->lastInChrBuf;

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
        assert(firstLumSrcY >= lastInLumBuf - vLumBufSize + 1);
        assert(firstChrSrcY >= lastInChrBuf - vChrBufSize + 1);

        DEBUG_BUFFERS("dstY: %d\n", dstY);
        DEBUG_BUFFERS("\tfirstLumSrcY: %d lastLumSrcY: %d lastInLumBuf: %d\n",
                      firstLumSrcY, lastLumSrcY, lastInLumBuf);
        DEBUG_BUFFERS("\tfirstChrSrcY: %d lastChrSrcY: %d lastInChrBuf: %d\n",
                      firstChrSrcY, lastChrSrcY, lastInChrBuf);

        // Do we have enough lines in this slice to output the dstY line
        enough_lines = lastLumSrcY2 < srcSliceY + srcSliceH &&
                       lastChrSrcY < -((-srcSliceY - srcSliceH) >> c->chrSrcVSubSample);

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
            assert(lumBufIndex < 2 * vLumBufSize);
            assert(lastInLumBuf + 1 - srcSliceY < srcSliceH);
            assert(lastInLumBuf + 1 - srcSliceY >= 0);
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
            assert(chrBufIndex < 2 * vChrBufSize);
            assert(lastInChrBuf + 1 - chrSrcSliceY < (chrSrcSliceH));
            assert(lastInChrBuf + 1 - chrSrcSliceY >= 0);
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

#if HAVE_MMX
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
                                     &yuv2packed1, &yuv2packed2, &yuv2packedX);
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
                    vLumFilter= c->lumMmxFilter;
                    vChrFilter= c->chrMmxFilter;
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
                        vLumFilter= c->alpMmxFilter;
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
            } else {
                assert(lumSrcPtr  + vLumFilterSize - 1 < lumPixBuf  + vLumBufSize * 2);
                assert(chrUSrcPtr + vChrFilterSize - 1 < chrUPixBuf + vChrBufSize * 2);
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
            }
        }
    }

    if (isPlanar(dstFormat) && isALPHA(dstFormat) && !alpPixBuf)
        fillPlane(dst[3], dstStride[3], dstW, dstY - lastDstY, lastDstY, 255);

#if HAVE_MMX2
    if (av_get_cpu_flags() & AV_CPU_FLAG_MMX2)
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
    enum PixelFormat srcFormat = c->srcFormat;

    ff_sws_init_output_funcs(c, &c->yuv2plane1, &c->yuv2planeX,
                             &c->yuv2nv12cX, &c->yuv2packed1,
                             &c->yuv2packed2, &c->yuv2packedX);

    ff_sws_init_input_funcs(c);


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
        c->hyScale = c->hcScale = c->dstBpc > 10 ? hScale16To19_c
                                                 : hScale16To15_c;
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
