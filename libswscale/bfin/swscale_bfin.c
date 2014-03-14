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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/bfin/attributes.h"
#include "libswscale/swscale_internal.h"

int ff_bfin_uyvytoyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                       uint8_t *vdst, int width, int height,
                       int lumStride, int chromStride,
                       int srcStride) attribute_l1_text;

int ff_bfin_yuyvtoyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                       uint8_t *vdst, int width, int height,
                       int lumStride, int chromStride,
                       int srcStride) attribute_l1_text;

static int uyvytoyv12_unscaled(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dst[], int dstStride[])
{
    uint8_t *dsty = dst[0] + dstStride[0] * srcSliceY;
    uint8_t *dstu = dst[1] + dstStride[1] * srcSliceY / 2;
    uint8_t *dstv = dst[2] + dstStride[2] * srcSliceY / 2;
    const uint8_t *ip = src[0] + srcStride[0] * srcSliceY;
    int w = dstStride[0];

    ff_bfin_uyvytoyv12(ip, dsty, dstu, dstv, w, srcSliceH,
                       dstStride[0], dstStride[1], srcStride[0]);

    return srcSliceH;
}

static int yuyvtoyv12_unscaled(SwsContext *c, const uint8_t *src[],
                               int srcStride[], int srcSliceY, int srcSliceH,
                               uint8_t *dst[], int dstStride[])
{
    uint8_t *dsty = dst[0] + dstStride[0] * srcSliceY;
    uint8_t *dstu = dst[1] + dstStride[1] * srcSliceY / 2;
    uint8_t *dstv = dst[2] + dstStride[2] * srcSliceY / 2;
    const uint8_t *ip = src[0] + srcStride[0] * srcSliceY;
    int w = dstStride[0];

    ff_bfin_yuyvtoyv12(ip, dsty, dstu, dstv, w, srcSliceH,
                       dstStride[0], dstStride[1], srcStride[0]);

    return srcSliceH;
}

av_cold void ff_get_unscaled_swscale_bfin(SwsContext *c)
{
    if (c->dstFormat == AV_PIX_FMT_YUV420P && c->srcFormat == AV_PIX_FMT_UYVY422) {
        av_log(NULL, AV_LOG_VERBOSE,
               "selecting Blackfin optimized uyvytoyv12_unscaled\n");
        c->swscale = uyvytoyv12_unscaled;
    }
    if (c->dstFormat == AV_PIX_FMT_YUV420P && c->srcFormat == AV_PIX_FMT_YUYV422) {
        av_log(NULL, AV_LOG_VERBOSE,
               "selecting Blackfin optimized yuyvtoyv12_unscaled\n");
        c->swscale = yuyvtoyv12_unscaled;
    }
}
