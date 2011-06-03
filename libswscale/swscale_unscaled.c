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

static void fillPlane(uint8_t* plane, int stride, int width, int height, int y, uint8_t val)
{
    int i;
    uint8_t *ptr = plane + stride*y;
    for (i=0; i<height; i++) {
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
        for (i=0; i<srcSliceH; i++) {
            memcpy(dst, src, width);
            src += srcStride;
            dst += dstStride;
        }
    }
}

static int planarToNv12Wrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dstParam[], int dstStride[])
{
    uint8_t *dst = dstParam[1] + dstStride[1]*srcSliceY/2;

    copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->srcW,
              dstParam[0], dstStride[0]);

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

static void gray8aToPacked32(const uint8_t *src, uint8_t *dst, int num_pixels, const uint8_t *palette)
{
    int i;
    for (i=0; i<num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *)palette)[src[i<<1]] | (src[(i<<1)+1] << 24);
}

static void gray8aToPacked32_1(const uint8_t *src, uint8_t *dst, int num_pixels, const uint8_t *palette)
{
    int i;

    for (i=0; i<num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *)palette)[src[i<<1]] | src[(i<<1)+1];
}

static void gray8aToPacked24(const uint8_t *src, uint8_t *dst, int num_pixels, const uint8_t *palette)
{
    int i;

    for (i=0; i<num_pixels; i++) {
        //FIXME slow?
        dst[0]= palette[src[i<<1]*4+0];
        dst[1]= palette[src[i<<1]*4+1];
        dst[2]= palette[src[i<<1]*4+2];
        dst+= 3;
    }
}

static int palToRgbWrapper(SwsContext *c, const uint8_t* src[], int srcStride[], int srcSliceY,
                           int srcSliceH, uint8_t* dst[], int dstStride[])
{
    const enum PixelFormat srcFormat= c->srcFormat;
    const enum PixelFormat dstFormat= c->dstFormat;
    void (*conv)(const uint8_t *src, uint8_t *dst, int num_pixels,
                 const uint8_t *palette)=NULL;
    int i;
    uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;
    const uint8_t *srcPtr= src[0];

    if (srcFormat == PIX_FMT_Y400A) {
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
    void (*conv)(const uint8_t *src, uint8_t *dst, int src_size)=NULL;

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
    copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->srcW,
              dst[0], dstStride[0]);

    planar2x(src[1], dst[1] + dstStride[1]*(srcSliceY >> 1), c->chrSrcW,
             srcSliceH >> 2, srcStride[1], dstStride[1]);
    planar2x(src[2], dst[2] + dstStride[2]*(srcSliceY >> 1), c->chrSrcW,
             srcSliceH >> 2, srcStride[2], dstStride[2]);
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
            if(is9_OR_10BPS(c->srcFormat)) {
                const int src_depth = av_pix_fmt_descriptors[c->srcFormat].comp[plane].depth_minus1+1;
                const int dst_depth = av_pix_fmt_descriptors[c->dstFormat].comp[plane].depth_minus1+1;
                const uint16_t *srcPtr2 = (const uint16_t*)srcPtr;

                if (is16BPS(c->dstFormat)) {
                    uint16_t *dstPtr2 = (uint16_t*)dstPtr;
#define COPY9_OR_10TO16(rfunc, wfunc) \
                    for (i = 0; i < height; i++) { \
                        for (j = 0; j < length; j++) { \
                            int srcpx = rfunc(&srcPtr2[j]); \
                            wfunc(&dstPtr2[j], (srcpx<<(16-src_depth)) | (srcpx>>(2*src_depth-16))); \
                        } \
                        dstPtr2 += dstStride[plane]/2; \
                        srcPtr2 += srcStride[plane]/2; \
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
                    uint16_t *dstPtr2 = (uint16_t*)dstPtr;
#define COPY9_OR_10TO9_OR_10(loop) \
                    for (i = 0; i < height; i++) { \
                        for (j = 0; j < length; j++) { \
                            loop; \
                        } \
                        dstPtr2 += dstStride[plane]/2; \
                        srcPtr2 += srcStride[plane]/2; \
                    }
#define COPY9_OR_10TO9_OR_10_2(rfunc, wfunc) \
                    if (dst_depth > src_depth) { \
                        COPY9_OR_10TO9_OR_10(int srcpx = rfunc(&srcPtr2[j]); \
                            wfunc(&dstPtr2[j], (srcpx << 1) | (srcpx >> 9))); \
                    } else if (dst_depth < src_depth) { \
                        COPY9_OR_10TO9_OR_10(wfunc(&dstPtr2[j], rfunc(&srcPtr2[j]) >> 1)); \
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
                    // FIXME Maybe dither instead.
#define COPY9_OR_10TO8(rfunc) \
                    for (i = 0; i < height; i++) { \
                        for (j = 0; j < length; j++) { \
                            dstPtr[j] = rfunc(&srcPtr2[j])>>(src_depth-8); \
                        } \
                        dstPtr  += dstStride[plane]; \
                        srcPtr2 += srcStride[plane]/2; \
                    }
                    if (isBE(c->srcFormat)) {
                        COPY9_OR_10TO8(AV_RB16);
                    } else {
                        COPY9_OR_10TO8(AV_RL16);
                    }
                }
            } else if(is9_OR_10BPS(c->dstFormat)) {
                const int dst_depth = av_pix_fmt_descriptors[c->dstFormat].comp[plane].depth_minus1+1;
                uint16_t *dstPtr2 = (uint16_t*)dstPtr;

                if (is16BPS(c->srcFormat)) {
                    const uint16_t *srcPtr2 = (const uint16_t*)srcPtr;
#define COPY16TO9_OR_10(rfunc, wfunc) \
                    for (i = 0; i < height; i++) { \
                        for (j = 0; j < length; j++) { \
                            wfunc(&dstPtr2[j], rfunc(&srcPtr2[j])>>(16-dst_depth)); \
                        } \
                        dstPtr2 += dstStride[plane]/2; \
                        srcPtr2 += srcStride[plane]/2; \
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
                } else /* 8bit */ {
#define COPY8TO9_OR_10(wfunc) \
                    for (i = 0; i < height; i++) { \
                        for (j = 0; j < length; j++) { \
                            const int srcpx = srcPtr[j]; \
                            wfunc(&dstPtr2[j], (srcpx<<(dst_depth-8)) | (srcpx >> (16-dst_depth))); \
                        } \
                        dstPtr2 += dstStride[plane]/2; \
                        srcPtr  += srcStride[plane]; \
                    }
                    if (isBE(c->dstFormat)) {
                        COPY8TO9_OR_10(AV_WB16);
                    } else {
                        COPY8TO9_OR_10(AV_WL16);
                    }
                }
            } else if(is16BPS(c->srcFormat) && !is16BPS(c->dstFormat)) {
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
                        ((uint16_t*)dstPtr)[j] = av_bswap16(((const uint16_t*)srcPtr)[j]);
                    srcPtr+= srcStride[plane];
                    dstPtr+= dstStride[plane];
                }
            } else if (dstStride[plane] == srcStride[plane] &&
                       srcStride[plane] > 0 && srcStride[plane] == length) {
                memcpy(dst[plane] + dstStride[plane]*y, src[plane],
                       height*dstStride[plane]);
            } else {
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
        && srcFormat != PIX_FMT_BGR48LE   && dstFormat != PIX_FMT_BGR48LE
        && srcFormat != PIX_FMT_BGR48BE   && dstFormat != PIX_FMT_BGR48BE
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

    if (ARCH_BFIN)
        ff_bfin_get_unscaled_swscale(c);
    if (HAVE_ALTIVEC)
        ff_swscale_get_unscaled_altivec(c);
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

static int check_image_pointers(uint8_t *data[4], enum PixelFormat pix_fmt,
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
int sws_scale(SwsContext *c, const uint8_t* const src[], const int srcStride[], int srcSliceY,
              int srcSliceH, uint8_t* const dst[], const int dstStride[])
{
    int i;
    const uint8_t* src2[4]= {src[0], src[1], src[2], src[3]};
    uint8_t* dst2[4]= {dst[0], dst[1], dst[2], dst[3]};

    // do not mess up sliceDir if we have a "trailing" 0-size slice
    if (srcSliceH == 0)
        return 0;

    if (!check_image_pointers(src, c->srcFormat, srcStride)) {
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
            } else if(c->srcFormat == PIX_FMT_GRAY8 || c->srcFormat == PIX_FMT_Y400A) {
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

/* Convert the palette to the same packed 32-bit format as the palette */
void sws_convertPalette8ToPacked32(const uint8_t *src, uint8_t *dst, int num_pixels, const uint8_t *palette)
{
    int i;

    for (i=0; i<num_pixels; i++)
        ((uint32_t *) dst)[i] = ((const uint32_t *) palette)[src[i]];
}

/* Palette format: ABCD -> dst format: ABC */
void sws_convertPalette8ToPacked24(const uint8_t *src, uint8_t *dst, int num_pixels, const uint8_t *palette)
{
    int i;

    for (i=0; i<num_pixels; i++) {
        //FIXME slow?
        dst[0]= palette[src[i]*4+0];
        dst[1]= palette[src[i]*4+1];
        dst[2]= palette[src[i]*4+2];
        dst+= 3;
    }
}
