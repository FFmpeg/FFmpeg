/*
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
 *
 * Blackfin software video scaler operations
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include "config.h"
#include <unistd.h>
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#if defined (__FDPIC__) && CONFIG_SRAM
#define L1CODE __attribute__ ((l1_text))
#else
#define L1CODE
#endif

int ff_bfin_uyvytoyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                       int width, int height,
                       int lumStride, int chromStride, int srcStride) L1CODE;

int ff_bfin_yuyvtoyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                       int width, int height,
                       int lumStride, int chromStride, int srcStride) L1CODE;

static int uyvytoyv12_unscaled(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dst[], int dstStride[])
{
    uint8_t *dsty = dst[0] + dstStride[0]*srcSliceY;
    uint8_t *dstu = dst[1] + dstStride[1]*srcSliceY/2;
    uint8_t *dstv = dst[2] + dstStride[2]*srcSliceY/2;
    uint8_t *ip   = src[0] + srcStride[0]*srcSliceY;
    int w         = dstStride[0];

    ff_bfin_uyvytoyv12(ip, dsty, dstu, dstv, w, srcSliceH,
                       dstStride[0], dstStride[1], srcStride[0]);

    return srcSliceH;
}

static int yuyvtoyv12_unscaled(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                               int srcSliceH, uint8_t* dst[], int dstStride[])
{
    uint8_t *dsty = dst[0] + dstStride[0]*srcSliceY;
    uint8_t *dstu = dst[1] + dstStride[1]*srcSliceY/2;
    uint8_t *dstv = dst[2] + dstStride[2]*srcSliceY/2;
    uint8_t *ip   = src[0] + srcStride[0]*srcSliceY;
    int w         = dstStride[0];

    ff_bfin_yuyvtoyv12(ip, dsty, dstu, dstv, w, srcSliceH,
                       dstStride[0], dstStride[1], srcStride[0]);

    return srcSliceH;
}


void ff_bfin_get_unscaled_swscale(SwsContext *c)
{
    SwsFunc swScale = c->swScale;

    if (c->dstFormat == PIX_FMT_YUV420P && c->srcFormat == PIX_FMT_UYVY422) {
        av_log (NULL, AV_LOG_VERBOSE, "selecting Blackfin optimized uyvytoyv12_unscaled\n");
        c->swScale = uyvytoyv12_unscaled;
    }
    if (c->dstFormat == PIX_FMT_YUV420P && c->srcFormat == PIX_FMT_YUYV422) {
        av_log (NULL, AV_LOG_VERBOSE, "selecting Blackfin optimized yuyvtoyv12_unscaled\n");
        c->swScale = yuyvtoyv12_unscaled;
    }
}
