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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/bswap.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"
#include "config.h"
#include "swscale_internal.h"
#include "swscale.h"

DECLARE_ALIGNED(8, const uint8_t, ff_dither_8x8_128)[9][8] = {
    {  36, 68,  60, 92,  34, 66,  58, 90, },
    { 100,  4, 124, 28,  98,  2, 122, 26, },
    {  52, 84,  44, 76,  50, 82,  42, 74, },
    { 116, 20, 108, 12, 114, 18, 106, 10, },
    {  32, 64,  56, 88,  38, 70,  62, 94, },
    {  96,  0, 120, 24, 102,  6, 126, 30, },
    {  48, 80,  40, 72,  54, 86,  46, 78, },
    { 112, 16, 104,  8, 118, 22, 110, 14, },
    {  36, 68,  60, 92,  34, 66,  58, 90, },
};

DECLARE_ALIGNED(8, static const uint8_t, sws_pb_64)[8] = {
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
    int bits            = desc->comp[0].depth - 1;
    int sh              = bits - 4;

    if ((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth<16) {
        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1 - 4;
    }

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
    int sh              = desc->comp[0].depth - 1;

    if (sh<15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 : (desc->comp[0].depth - 1);
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1;
    }

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
    for (i = 0; i < width; i++) {
        dst[i] = ((int)(FFMIN(dst[i], 30189 << 4) * 4769U - (39057361 << 2))) >> 12;
    }
}

static void lumRangeFromJpeg16_c(int16_t *_dst, int width)
{
    int i;
    int32_t *dst = (int32_t *) _dst;
    for (i = 0; i < width; i++)
        dst[i] = (dst[i]*(14071/4) + (33561947<<4)/4)>>12;
}


#define DEBUG_SWSCALE_BUFFERS 0
#define DEBUG_BUFFERS(...)                      \
    if (DEBUG_SWSCALE_BUFFERS)                  \
        av_log(c, AV_LOG_DEBUG, __VA_ARGS__)

static int swscale(SwsContext *c, const uint8_t *src[],
                   int srcStride[], int srcSliceY, int srcSliceH,
                   uint8_t *dst[], int dstStride[],
                   int dstSliceY, int dstSliceH)
{
    const int scale_dst = dstSliceY > 0 || dstSliceH < c->dstH;

    /* load a few things into local vars to make the code more readable?
     * and faster */
    const int dstW                   = c->dstW;
    int dstH                         = c->dstH;

    const enum AVPixelFormat dstFormat = c->dstFormat;
    const int flags                  = c->flags;
    int32_t *vLumFilterPos           = c->vLumFilterPos;
    int32_t *vChrFilterPos           = c->vChrFilterPos;

    const int vLumFilterSize         = c->vLumFilterSize;
    const int vChrFilterSize         = c->vChrFilterSize;

    yuv2planar1_fn yuv2plane1        = c->yuv2plane1;
    yuv2planarX_fn yuv2planeX        = c->yuv2planeX;
    yuv2interleavedX_fn yuv2nv12cX   = c->yuv2nv12cX;
    yuv2packed1_fn yuv2packed1       = c->yuv2packed1;
    yuv2packed2_fn yuv2packed2       = c->yuv2packed2;
    yuv2packedX_fn yuv2packedX       = c->yuv2packedX;
    yuv2anyX_fn yuv2anyX             = c->yuv2anyX;
    const int chrSrcSliceY           =                srcSliceY >> c->chrSrcVSubSample;
    const int chrSrcSliceH           = AV_CEIL_RSHIFT(srcSliceH,   c->chrSrcVSubSample);
    int should_dither                = isNBPS(c->srcFormat) ||
                                       is16BPS(c->srcFormat);
    int lastDstY;

    /* vars which will change and which we need to store back in the context */
    int dstY         = c->dstY;
    int lastInLumBuf = c->lastInLumBuf;
    int lastInChrBuf = c->lastInChrBuf;

    int lumStart = 0;
    int lumEnd = c->descIndex[0];
    int chrStart = lumEnd;
    int chrEnd = c->descIndex[1];
    int vStart = chrEnd;
    int vEnd = c->numDesc;
    SwsSlice *src_slice = &c->slice[lumStart];
    SwsSlice *hout_slice = &c->slice[c->numSlice-2];
    SwsSlice *vout_slice = &c->slice[c->numSlice-1];
    SwsFilterDescriptor *desc = c->desc;

    int needAlpha = c->needAlpha;

    int hasLumHoles = 1;
    int hasChrHoles = 1;

    if (isPacked(c->srcFormat)) {
        src[1] =
        src[2] =
        src[3] = src[0];
        srcStride[1] =
        srcStride[2] =
        srcStride[3] = srcStride[0];
    }
    srcStride[1] *= 1 << c->vChrDrop;
    srcStride[2] *= 1 << c->vChrDrop;

    DEBUG_BUFFERS("swscale() %p[%d] %p[%d] %p[%d] %p[%d] -> %p[%d] %p[%d] %p[%d] %p[%d]\n",
                  src[0], srcStride[0], src[1], srcStride[1],
                  src[2], srcStride[2], src[3], srcStride[3],
                  dst[0], dstStride[0], dst[1], dstStride[1],
                  dst[2], dstStride[2], dst[3], dstStride[3]);
    DEBUG_BUFFERS("srcSliceY: %d srcSliceH: %d dstY: %d dstH: %d\n",
                  srcSliceY, srcSliceH, dstY, dstH);
    DEBUG_BUFFERS("vLumFilterSize: %d vChrFilterSize: %d\n",
                  vLumFilterSize, vChrFilterSize);

    if (dstStride[0]&15 || dstStride[1]&15 ||
        dstStride[2]&15 || dstStride[3]&15) {
        SwsContext *const ctx = c->parent ? c->parent : c;
        if (flags & SWS_PRINT_INFO &&
            !atomic_exchange_explicit(&ctx->stride_unaligned_warned, 1, memory_order_relaxed)) {
            av_log(c, AV_LOG_WARNING,
                   "Warning: dstStride is not aligned!\n"
                   "         ->cannot do aligned memory accesses anymore\n");
        }
    }

#if ARCH_X86
    if (   (uintptr_t)dst[0]&15 || (uintptr_t)dst[1]&15 || (uintptr_t)dst[2]&15
        || (uintptr_t)src[0]&15 || (uintptr_t)src[1]&15 || (uintptr_t)src[2]&15
        || dstStride[0]&15 || dstStride[1]&15 || dstStride[2]&15 || dstStride[3]&15
        || srcStride[0]&15 || srcStride[1]&15 || srcStride[2]&15 || srcStride[3]&15
    ) {
        SwsContext *const ctx = c->parent ? c->parent : c;
        int cpu_flags = av_get_cpu_flags();
        if (flags & SWS_PRINT_INFO && HAVE_MMXEXT && (cpu_flags & AV_CPU_FLAG_SSE2) &&
            !atomic_exchange_explicit(&ctx->stride_unaligned_warned,1, memory_order_relaxed)) {
            av_log(c, AV_LOG_WARNING, "Warning: data is not aligned! This can lead to a speed loss\n");
        }
    }
#endif

    if (scale_dst) {
        dstY         = dstSliceY;
        dstH         = dstY + dstSliceH;
        lastInLumBuf = -1;
        lastInChrBuf = -1;
    } else if (srcSliceY == 0) {
        /* Note the user might start scaling the picture in the middle so this
         * will not get executed. This is not really intended but works
         * currently, so people might do it. */
        dstY         = 0;
        lastInLumBuf = -1;
        lastInChrBuf = -1;
    }

    if (!should_dither) {
        c->chrDither8 = c->lumDither8 = sws_pb_64;
    }
    lastDstY = dstY;

    ff_init_vscale_pfn(c, yuv2plane1, yuv2planeX, yuv2nv12cX,
                   yuv2packed1, yuv2packed2, yuv2packedX, yuv2anyX, c->use_mmx_vfilter);

    ff_init_slice_from_src(src_slice, (uint8_t**)src, srcStride, c->srcW,
            srcSliceY, srcSliceH, chrSrcSliceY, chrSrcSliceH, 1);

    ff_init_slice_from_src(vout_slice, (uint8_t**)dst, dstStride, c->dstW,
            dstY, dstSliceH, dstY >> c->chrDstVSubSample,
            AV_CEIL_RSHIFT(dstSliceH, c->chrDstVSubSample), scale_dst);
    if (srcSliceY == 0) {
        hout_slice->plane[0].sliceY = lastInLumBuf + 1;
        hout_slice->plane[1].sliceY = lastInChrBuf + 1;
        hout_slice->plane[2].sliceY = lastInChrBuf + 1;
        hout_slice->plane[3].sliceY = lastInLumBuf + 1;

        hout_slice->plane[0].sliceH =
        hout_slice->plane[1].sliceH =
        hout_slice->plane[2].sliceH =
        hout_slice->plane[3].sliceH = 0;
        hout_slice->width = dstW;
    }

    for (; dstY < dstH; dstY++) {
        const int chrDstY = dstY >> c->chrDstVSubSample;
        int use_mmx_vfilter= c->use_mmx_vfilter;

        // First line needed as input
        const int firstLumSrcY  = FFMAX(1 - vLumFilterSize, vLumFilterPos[dstY]);
        const int firstLumSrcY2 = FFMAX(1 - vLumFilterSize, vLumFilterPos[FFMIN(dstY | ((1 << c->chrDstVSubSample) - 1), c->dstH - 1)]);
        // First line needed as input
        const int firstChrSrcY  = FFMAX(1 - vChrFilterSize, vChrFilterPos[chrDstY]);

        // Last line needed as input
        int lastLumSrcY  = FFMIN(c->srcH,    firstLumSrcY  + vLumFilterSize) - 1;
        int lastLumSrcY2 = FFMIN(c->srcH,    firstLumSrcY2 + vLumFilterSize) - 1;
        int lastChrSrcY  = FFMIN(c->chrSrcH, firstChrSrcY  + vChrFilterSize) - 1;
        int enough_lines;

        int i;
        int posY, cPosY, firstPosY, lastPosY, firstCPosY, lastCPosY;

        // handle holes (FAST_BILINEAR & weird filters)
        if (firstLumSrcY > lastInLumBuf) {

            hasLumHoles = lastInLumBuf != firstLumSrcY - 1;
            if (hasLumHoles) {
                hout_slice->plane[0].sliceY = firstLumSrcY;
                hout_slice->plane[3].sliceY = firstLumSrcY;
                hout_slice->plane[0].sliceH =
                hout_slice->plane[3].sliceH = 0;
            }

            lastInLumBuf = firstLumSrcY - 1;
        }
        if (firstChrSrcY > lastInChrBuf) {

            hasChrHoles = lastInChrBuf != firstChrSrcY - 1;
            if (hasChrHoles) {
                hout_slice->plane[1].sliceY = firstChrSrcY;
                hout_slice->plane[2].sliceY = firstChrSrcY;
                hout_slice->plane[1].sliceH =
                hout_slice->plane[2].sliceH = 0;
            }

            lastInChrBuf = firstChrSrcY - 1;
        }

        DEBUG_BUFFERS("dstY: %d\n", dstY);
        DEBUG_BUFFERS("\tfirstLumSrcY: %d lastLumSrcY: %d lastInLumBuf: %d\n",
                      firstLumSrcY, lastLumSrcY, lastInLumBuf);
        DEBUG_BUFFERS("\tfirstChrSrcY: %d lastChrSrcY: %d lastInChrBuf: %d\n",
                      firstChrSrcY, lastChrSrcY, lastInChrBuf);

        // Do we have enough lines in this slice to output the dstY line
        enough_lines = lastLumSrcY2 < srcSliceY + srcSliceH &&
                       lastChrSrcY < AV_CEIL_RSHIFT(srcSliceY + srcSliceH, c->chrSrcVSubSample);

        if (!enough_lines) {
            lastLumSrcY = srcSliceY + srcSliceH - 1;
            lastChrSrcY = chrSrcSliceY + chrSrcSliceH - 1;
            DEBUG_BUFFERS("buffering slice: lastLumSrcY %d lastChrSrcY %d\n",
                          lastLumSrcY, lastChrSrcY);
        }

        av_assert0((lastLumSrcY - firstLumSrcY + 1) <= hout_slice->plane[0].available_lines);
        av_assert0((lastChrSrcY - firstChrSrcY + 1) <= hout_slice->plane[1].available_lines);


        posY = hout_slice->plane[0].sliceY + hout_slice->plane[0].sliceH;
        if (posY <= lastLumSrcY && !hasLumHoles) {
            firstPosY = FFMAX(firstLumSrcY, posY);
            lastPosY = FFMIN(firstLumSrcY + hout_slice->plane[0].available_lines - 1, srcSliceY + srcSliceH - 1);
        } else {
            firstPosY = posY;
            lastPosY = lastLumSrcY;
        }

        cPosY = hout_slice->plane[1].sliceY + hout_slice->plane[1].sliceH;
        if (cPosY <= lastChrSrcY && !hasChrHoles) {
            firstCPosY = FFMAX(firstChrSrcY, cPosY);
            lastCPosY = FFMIN(firstChrSrcY + hout_slice->plane[1].available_lines - 1, AV_CEIL_RSHIFT(srcSliceY + srcSliceH, c->chrSrcVSubSample) - 1);
        } else {
            firstCPosY = cPosY;
            lastCPosY = lastChrSrcY;
        }

        ff_rotate_slice(hout_slice, lastPosY, lastCPosY);

        if (posY < lastLumSrcY + 1) {
            for (i = lumStart; i < lumEnd; ++i)
                desc[i].process(c, &desc[i], firstPosY, lastPosY - firstPosY + 1);
        }

        lastInLumBuf = lastLumSrcY;

        if (cPosY < lastChrSrcY + 1) {
            for (i = chrStart; i < chrEnd; ++i)
                desc[i].process(c, &desc[i], firstCPosY, lastCPosY - firstCPosY + 1);
        }

        lastInChrBuf = lastChrSrcY;

        if (!enough_lines)
            break;  // we can't output a dstY line so let's try with the next slice

#if HAVE_MMX_INLINE
        ff_updateMMXDitherTables(c, dstY);
#endif
        if (should_dither) {
            c->chrDither8 = ff_dither_8x8_128[chrDstY & 7];
            c->lumDither8 = ff_dither_8x8_128[dstY    & 7];
        }
        if (dstY >= c->dstH - 2) {
            /* hmm looks like we can't use MMX here without overwriting
             * this array's tail */
            ff_sws_init_output_funcs(c, &yuv2plane1, &yuv2planeX, &yuv2nv12cX,
                                     &yuv2packed1, &yuv2packed2, &yuv2packedX, &yuv2anyX);
            use_mmx_vfilter= 0;
            ff_init_vscale_pfn(c, yuv2plane1, yuv2planeX, yuv2nv12cX,
                           yuv2packed1, yuv2packed2, yuv2packedX, yuv2anyX, use_mmx_vfilter);
        }

        for (i = vStart; i < vEnd; ++i)
            desc[i].process(c, &desc[i], dstY, 1);
    }
    if (isPlanar(dstFormat) && isALPHA(dstFormat) && !needAlpha) {
        int offset = lastDstY - dstSliceY;
        int length = dstW;
        int height = dstY - lastDstY;

        if (is16BPS(dstFormat) || isNBPS(dstFormat)) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dstFormat);
            fillPlane16(dst[3], dstStride[3], length, height, offset,
                    1, desc->comp[3].depth,
                    isBE(dstFormat));
        } else if (is32BPS(dstFormat)) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dstFormat);
            fillPlane32(dst[3], dstStride[3], length, height, offset,
                    1, desc->comp[3].depth,
                    isBE(dstFormat), desc->flags & AV_PIX_FMT_FLAG_FLOAT);
        } else
            fillPlane(dst[3], dstStride[3], length, height, offset, 255);
    }

#if HAVE_MMXEXT_INLINE
    if (av_get_cpu_flags() & AV_CPU_FLAG_MMXEXT)
        __asm__ volatile ("sfence" ::: "memory");
#endif
    emms_c();

    /* store changed local vars back in the context */
    c->dstY         = dstY;
    c->lastInLumBuf = lastInLumBuf;
    c->lastInChrBuf = lastInChrBuf;

    return dstY - lastDstY;
}

av_cold void ff_sws_init_range_convert(SwsContext *c)
{
    c->lumConvertRange = NULL;
    c->chrConvertRange = NULL;
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
}

static av_cold void sws_init_swscale(SwsContext *c)
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
                c->hyscale_fast = ff_hyscale_fast_c;
                c->hcscale_fast = ff_hcscale_fast_c;
            }
        } else {
            c->hyScale = c->hcScale = hScale8To19_c;
        }
    } else {
        c->hyScale = c->hcScale = c->dstBpc > 14 ? hScale16To19_c
                                                 : hScale16To15_c;
    }

    ff_sws_init_range_convert(c);

    if (!(isGray(srcFormat) || isGray(c->dstFormat) ||
          srcFormat == AV_PIX_FMT_MONOBLACK || srcFormat == AV_PIX_FMT_MONOWHITE))
        c->needs_hcscale = 1;
}

void ff_sws_init_scale(SwsContext *c)
{
    sws_init_swscale(c);

#if ARCH_PPC
    ff_sws_init_swscale_ppc(c);
#elif ARCH_X86
    ff_sws_init_swscale_x86(c);
#elif ARCH_AARCH64
    ff_sws_init_swscale_aarch64(c);
#elif ARCH_ARM
    ff_sws_init_swscale_arm(c);
#endif
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

static int check_image_pointers(const uint8_t * const data[4], enum AVPixelFormat pix_fmt,
                                const int linesizes[4])
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i;

    av_assert2(desc);

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
            r = av_clip_uintp2(r, 12);
            g = av_clip_uintp2(g, 12);
            b = av_clip_uintp2(b, 12);

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
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->dstFormat);

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
            x = av_clip_uintp2(x, 12);
            y = av_clip_uintp2(y, 12);
            z = av_clip_uintp2(z, 12);

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

static void update_palette(SwsContext *c, const uint32_t *pal)
{
    for (int i = 0; i < 256; i++) {
        int r, g, b, y, u, v, a = 0xff;
        if (c->srcFormat == AV_PIX_FMT_PAL8) {
            uint32_t p = pal[i];
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

static int scale_internal(SwsContext *c,
                          const uint8_t * const srcSlice[], const int srcStride[],
                          int srcSliceY, int srcSliceH,
                          uint8_t *const dstSlice[], const int dstStride[],
                          int dstSliceY, int dstSliceH);

static int scale_gamma(SwsContext *c,
                       const uint8_t * const srcSlice[], const int srcStride[],
                       int srcSliceY, int srcSliceH,
                       uint8_t * const dstSlice[], const int dstStride[],
                       int dstSliceY, int dstSliceH)
{
    int ret = scale_internal(c->cascaded_context[0],
                             srcSlice, srcStride, srcSliceY, srcSliceH,
                             c->cascaded_tmp, c->cascaded_tmpStride, 0, c->srcH);

    if (ret < 0)
        return ret;

    if (c->cascaded_context[2])
        ret = scale_internal(c->cascaded_context[1], (const uint8_t * const *)c->cascaded_tmp,
                             c->cascaded_tmpStride, srcSliceY, srcSliceH,
                             c->cascaded1_tmp, c->cascaded1_tmpStride, 0, c->dstH);
    else
        ret = scale_internal(c->cascaded_context[1], (const uint8_t * const *)c->cascaded_tmp,
                             c->cascaded_tmpStride, srcSliceY, srcSliceH,
                             dstSlice, dstStride, dstSliceY, dstSliceH);

    if (ret < 0)
        return ret;

    if (c->cascaded_context[2]) {
        ret = scale_internal(c->cascaded_context[2], (const uint8_t * const *)c->cascaded1_tmp,
                             c->cascaded1_tmpStride, c->cascaded_context[1]->dstY - ret,
                             c->cascaded_context[1]->dstY,
                             dstSlice, dstStride, dstSliceY, dstSliceH);
    }
    return ret;
}

static int scale_cascaded(SwsContext *c,
                          const uint8_t * const srcSlice[], const int srcStride[],
                          int srcSliceY, int srcSliceH,
                          uint8_t * const dstSlice[], const int dstStride[],
                          int dstSliceY, int dstSliceH)
{
    int ret = scale_internal(c->cascaded_context[0],
                             srcSlice, srcStride, srcSliceY, srcSliceH,
                             c->cascaded_tmp, c->cascaded_tmpStride,
                             0, c->cascaded_context[0]->dstH);
    if (ret < 0)
        return ret;
    ret = scale_internal(c->cascaded_context[1],
                         (const uint8_t * const * )c->cascaded_tmp, c->cascaded_tmpStride,
                         0, c->cascaded_context[0]->dstH,
                         dstSlice, dstStride, dstSliceY, dstSliceH);
    return ret;
}

static int scale_internal(SwsContext *c,
                          const uint8_t * const srcSlice[], const int srcStride[],
                          int srcSliceY, int srcSliceH,
                          uint8_t *const dstSlice[], const int dstStride[],
                          int dstSliceY, int dstSliceH)
{
    const int scale_dst = dstSliceY > 0 || dstSliceH < c->dstH;
    const int frame_start = scale_dst || !c->sliceDir;
    int i, ret;
    const uint8_t *src2[4];
    uint8_t *dst2[4];
    int macro_height_src = isBayer(c->srcFormat) ? 2 : (1 << c->chrSrcVSubSample);
    int macro_height_dst = isBayer(c->dstFormat) ? 2 : (1 << c->chrDstVSubSample);
    // copy strides, so they can safely be modified
    int srcStride2[4];
    int dstStride2[4];
    int srcSliceY_internal = srcSliceY;

    if (!srcStride || !dstStride || !dstSlice || !srcSlice) {
        av_log(c, AV_LOG_ERROR, "One of the input parameters to sws_scale() is NULL, please check the calling code\n");
        return AVERROR(EINVAL);
    }

    if ((srcSliceY  & (macro_height_src - 1)) ||
        ((srcSliceH & (macro_height_src - 1)) && srcSliceY + srcSliceH != c->srcH) ||
        srcSliceY + srcSliceH > c->srcH) {
        av_log(c, AV_LOG_ERROR, "Slice parameters %d, %d are invalid\n", srcSliceY, srcSliceH);
        return AVERROR(EINVAL);
    }

    if ((dstSliceY  & (macro_height_dst - 1)) ||
        ((dstSliceH & (macro_height_dst - 1)) && dstSliceY + dstSliceH != c->dstH) ||
        dstSliceY + dstSliceH > c->dstH) {
        av_log(c, AV_LOG_ERROR, "Slice parameters %d, %d are invalid\n", dstSliceY, dstSliceH);
        return AVERROR(EINVAL);
    }

    if (!check_image_pointers(srcSlice, c->srcFormat, srcStride)) {
        av_log(c, AV_LOG_ERROR, "bad src image pointers\n");
        return AVERROR(EINVAL);
    }
    if (!check_image_pointers((const uint8_t* const*)dstSlice, c->dstFormat, dstStride)) {
        av_log(c, AV_LOG_ERROR, "bad dst image pointers\n");
        return AVERROR(EINVAL);
    }

    // do not mess up sliceDir if we have a "trailing" 0-size slice
    if (srcSliceH == 0)
        return 0;

    if (c->gamma_flag && c->cascaded_context[0])
        return scale_gamma(c, srcSlice, srcStride, srcSliceY, srcSliceH,
                           dstSlice, dstStride, dstSliceY, dstSliceH);

    if (c->cascaded_context[0] && srcSliceY == 0 && srcSliceH == c->cascaded_context[0]->srcH)
        return scale_cascaded(c, srcSlice, srcStride, srcSliceY, srcSliceH,
                              dstSlice, dstStride, dstSliceY, dstSliceH);

    if (!srcSliceY && (c->flags & SWS_BITEXACT) && c->dither == SWS_DITHER_ED && c->dither_error[0])
        for (i = 0; i < 4; i++)
            memset(c->dither_error[i], 0, sizeof(c->dither_error[0][0]) * (c->dstW+2));

    if (usePal(c->srcFormat))
        update_palette(c, (const uint32_t *)srcSlice[1]);

    memcpy(src2,       srcSlice,  sizeof(src2));
    memcpy(dst2,       dstSlice,  sizeof(dst2));
    memcpy(srcStride2, srcStride, sizeof(srcStride2));
    memcpy(dstStride2, dstStride, sizeof(dstStride2));

    if (frame_start && !scale_dst) {
        if (srcSliceY != 0 && srcSliceY + srcSliceH != c->srcH) {
            av_log(c, AV_LOG_ERROR, "Slices start in the middle!\n");
            return AVERROR(EINVAL);
        }

        c->sliceDir = (srcSliceY == 0) ? 1 : -1;
    } else if (scale_dst)
        c->sliceDir = 1;

    if (c->src0Alpha && !c->dst0Alpha && isALPHA(c->dstFormat)) {
        uint8_t *base;
        int x,y;

        av_fast_malloc(&c->rgb0_scratch, &c->rgb0_scratch_allocated,
                       FFABS(srcStride[0]) * srcSliceH + 32);
        if (!c->rgb0_scratch)
            return AVERROR(ENOMEM);

        base = srcStride[0] < 0 ? c->rgb0_scratch - srcStride[0] * (srcSliceH-1) :
                                  c->rgb0_scratch;
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

        av_fast_malloc(&c->xyz_scratch, &c->xyz_scratch_allocated,
                       FFABS(srcStride[0]) * srcSliceH + 32);
        if (!c->xyz_scratch)
            return AVERROR(ENOMEM);

        base = srcStride[0] < 0 ? c->xyz_scratch - srcStride[0] * (srcSliceH-1) :
                                  c->xyz_scratch;

        xyz12Torgb48(c, (uint16_t*)base, (const uint16_t*)src2[0], srcStride[0]/2, srcSliceH);
        src2[0] = base;
    }

    if (c->sliceDir != 1) {
        // slices go from bottom to top => we flip the image internally
        for (i=0; i<4; i++) {
            srcStride2[i] *= -1;
            dstStride2[i] *= -1;
        }

        src2[0] += (srcSliceH - 1) * srcStride[0];
        if (!usePal(c->srcFormat))
            src2[1] += ((srcSliceH >> c->chrSrcVSubSample) - 1) * srcStride[1];
        src2[2] += ((srcSliceH >> c->chrSrcVSubSample) - 1) * srcStride[2];
        src2[3] += (srcSliceH - 1) * srcStride[3];
        dst2[0] += ( c->dstH                         - 1) * dstStride[0];
        dst2[1] += ((c->dstH >> c->chrDstVSubSample) - 1) * dstStride[1];
        dst2[2] += ((c->dstH >> c->chrDstVSubSample) - 1) * dstStride[2];
        dst2[3] += ( c->dstH                         - 1) * dstStride[3];

        srcSliceY_internal = c->srcH-srcSliceY-srcSliceH;
    }
    reset_ptr(src2, c->srcFormat);
    reset_ptr((void*)dst2, c->dstFormat);

    if (c->convert_unscaled) {
        int offset  = srcSliceY_internal;
        int slice_h = srcSliceH;

        // for dst slice scaling, offset the pointers to match the unscaled API
        if (scale_dst) {
            av_assert0(offset == 0);
            for (i = 0; i < 4 && src2[i]; i++) {
                if (!src2[i] || (i > 0 && usePal(c->srcFormat)))
                    break;
                src2[i] += (dstSliceY >> ((i == 1 || i == 2) ? c->chrSrcVSubSample : 0)) * srcStride2[i];
            }

            for (i = 0; i < 4 && dst2[i]; i++) {
                if (!dst2[i] || (i > 0 && usePal(c->dstFormat)))
                    break;
                dst2[i] -= (dstSliceY >> ((i == 1 || i == 2) ? c->chrDstVSubSample : 0)) * dstStride2[i];
            }
            offset  = dstSliceY;
            slice_h = dstSliceH;
        }

        ret = c->convert_unscaled(c, src2, srcStride2, offset, slice_h,
                                  dst2, dstStride2);
        if (scale_dst)
            dst2[0] += dstSliceY * dstStride2[0];
    } else {
        ret = swscale(c, src2, srcStride2, srcSliceY_internal, srcSliceH,
                      dst2, dstStride2, dstSliceY, dstSliceH);
    }

    if (c->dstXYZ && !(c->srcXYZ && c->srcW==c->dstW && c->srcH==c->dstH)) {
        uint16_t *dst16;

        if (scale_dst) {
            dst16 = (uint16_t *)dst2[0];
        } else {
            int dstY = c->dstY ? c->dstY : srcSliceY + srcSliceH;

            av_assert0(dstY >= ret);
            av_assert0(ret >= 0);
            av_assert0(c->dstH >= dstY);
            dst16 = (uint16_t*)(dst2[0] + (dstY - ret) * dstStride2[0]);
        }

        /* replace on the same data */
        rgb48Toxyz12(c, dst16, dst16, dstStride2[0]/2, ret);
    }

    /* reset slice direction at end of frame */
    if ((srcSliceY_internal + srcSliceH == c->srcH) || scale_dst)
        c->sliceDir = 0;

    return ret;
}

void sws_frame_end(struct SwsContext *c)
{
    av_frame_unref(c->frame_src);
    av_frame_unref(c->frame_dst);
    c->src_ranges.nb_ranges = 0;
}

int sws_frame_start(struct SwsContext *c, AVFrame *dst, const AVFrame *src)
{
    int ret, allocated = 0;

    ret = av_frame_ref(c->frame_src, src);
    if (ret < 0)
        return ret;

    if (!dst->buf[0]) {
        dst->width  = c->dstW;
        dst->height = c->dstH;
        dst->format = c->dstFormat;

        ret = av_frame_get_buffer(dst, 0);
        if (ret < 0)
            return ret;
        allocated = 1;
    }

    ret = av_frame_ref(c->frame_dst, dst);
    if (ret < 0) {
        if (allocated)
            av_frame_unref(dst);

        return ret;
    }

    return 0;
}

int sws_send_slice(struct SwsContext *c, unsigned int slice_start,
                   unsigned int slice_height)
{
    int ret;

    ret = ff_range_add(&c->src_ranges, slice_start, slice_height);
    if (ret < 0)
        return ret;

    return 0;
}

unsigned int sws_receive_slice_alignment(const struct SwsContext *c)
{
    if (c->slice_ctx)
        return c->slice_ctx[0]->dst_slice_align;

    return c->dst_slice_align;
}

int sws_receive_slice(struct SwsContext *c, unsigned int slice_start,
                      unsigned int slice_height)
{
    unsigned int align = sws_receive_slice_alignment(c);
    uint8_t *dst[4];

    /* wait until complete input has been received */
    if (!(c->src_ranges.nb_ranges == 1        &&
          c->src_ranges.ranges[0].start == 0 &&
          c->src_ranges.ranges[0].len == c->srcH))
        return AVERROR(EAGAIN);

    if ((slice_start > 0 || slice_height < c->dstH) &&
        (slice_start % align || slice_height % align)) {
        av_log(c, AV_LOG_ERROR,
               "Incorrectly aligned output: %u/%u not multiples of %u\n",
               slice_start, slice_height, align);
        return AVERROR(EINVAL);
    }

    if (c->slicethread) {
        int nb_jobs = c->slice_ctx[0]->dither == SWS_DITHER_ED ? 1 : c->nb_slice_ctx;
        int ret = 0;

        c->dst_slice_start  = slice_start;
        c->dst_slice_height = slice_height;

        avpriv_slicethread_execute(c->slicethread, nb_jobs, 0);

        for (int i = 0; i < c->nb_slice_ctx; i++) {
            if (c->slice_err[i] < 0) {
                ret = c->slice_err[i];
                break;
            }
        }

        memset(c->slice_err, 0, c->nb_slice_ctx * sizeof(*c->slice_err));

        return ret;
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(dst); i++) {
        ptrdiff_t offset = c->frame_dst->linesize[i] * (slice_start >> c->chrDstVSubSample);
        dst[i] = FF_PTR_ADD(c->frame_dst->data[i], offset);
    }

    return scale_internal(c, (const uint8_t * const *)c->frame_src->data,
                          c->frame_src->linesize, 0, c->srcH,
                          dst, c->frame_dst->linesize, slice_start, slice_height);
}

int sws_scale_frame(struct SwsContext *c, AVFrame *dst, const AVFrame *src)
{
    int ret;

    ret = sws_frame_start(c, dst, src);
    if (ret < 0)
        return ret;

    ret = sws_send_slice(c, 0, src->height);
    if (ret >= 0)
        ret = sws_receive_slice(c, 0, dst->height);

    sws_frame_end(c);

    return ret;
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
    if (c->nb_slice_ctx)
        c = c->slice_ctx[0];

    return scale_internal(c, srcSlice, srcStride, srcSliceY, srcSliceH,
                          dst, dstStride, 0, c->dstH);
}

void ff_sws_slice_worker(void *priv, int jobnr, int threadnr,
                         int nb_jobs, int nb_threads)
{
    SwsContext *parent = priv;
    SwsContext      *c = parent->slice_ctx[threadnr];

    const int slice_height = FFALIGN(FFMAX((parent->dst_slice_height + nb_jobs - 1) / nb_jobs, 1),
                                     c->dst_slice_align);
    const int slice_start  = jobnr * slice_height;
    const int slice_end    = FFMIN((jobnr + 1) * slice_height, parent->dst_slice_height);
    int err = 0;

    if (slice_end > slice_start) {
        uint8_t *dst[4] = { NULL };

        for (int i = 0; i < FF_ARRAY_ELEMS(dst) && parent->frame_dst->data[i]; i++) {
            const int vshift = (i == 1 || i == 2) ? c->chrDstVSubSample : 0;
            const ptrdiff_t offset = parent->frame_dst->linesize[i] *
                ((slice_start + parent->dst_slice_start) >> vshift);

            dst[i] = parent->frame_dst->data[i] + offset;
        }

        err = scale_internal(c, (const uint8_t * const *)parent->frame_src->data,
                             parent->frame_src->linesize, 0, c->srcH,
                             dst, parent->frame_dst->linesize,
                             parent->dst_slice_start + slice_start, slice_end - slice_start);
    }

    parent->slice_err[threadnr] = err;
}
