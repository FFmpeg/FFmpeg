/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "colorspacedsp.h"

#define SS_W 0
#define SS_H 0

#define BIT_DEPTH 8
#include "colorspacedsp_template.c"

#undef BIT_DEPTH
#define BIT_DEPTH 10
#include "colorspacedsp_template.c"

#undef BIT_DEPTH
#define BIT_DEPTH 12
#include "colorspacedsp_template.c"

#undef SS_W
#undef SS_H

#define SS_W 1
#define SS_H 0

#undef BIT_DEPTH
#define BIT_DEPTH 8
#include "colorspacedsp_template.c"

#undef BIT_DEPTH
#define BIT_DEPTH 10
#include "colorspacedsp_template.c"

#undef BIT_DEPTH
#define BIT_DEPTH 12
#include "colorspacedsp_template.c"

#undef SS_W
#undef SS_H

#define SS_W 1
#define SS_H 1

#undef BIT_DEPTH
#define BIT_DEPTH 8
#include "colorspacedsp_template.c"

#undef BIT_DEPTH
#define BIT_DEPTH 10
#include "colorspacedsp_template.c"

#undef BIT_DEPTH
#define BIT_DEPTH 12
#include "colorspacedsp_template.c"

static void multiply3x3_c(int16_t *buf[3], ptrdiff_t stride,
                          int w, int h, const int16_t m[3][3][8])
{
    int y, x;
    int16_t *buf0 = buf[0], *buf1 = buf[1], *buf2 = buf[2];

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int v0 = buf0[x], v1 = buf1[x], v2 = buf2[x];

            buf0[x] = av_clip_int16((m[0][0][0] * v0 + m[0][1][0] * v1 +
                                     m[0][2][0] * v2 + 8192) >> 14);
            buf1[x] = av_clip_int16((m[1][0][0] * v0 + m[1][1][0] * v1 +
                                     m[1][2][0] * v2 + 8192) >> 14);
            buf2[x] = av_clip_int16((m[2][0][0] * v0 + m[2][1][0] * v1 +
                                     m[2][2][0] * v2 + 8192) >> 14);
        }

        buf0 += stride;
        buf1 += stride;
        buf2 += stride;
    }
}

void ff_colorspacedsp_init(ColorSpaceDSPContext *dsp)
{
#define init_yuv2rgb_fn(idx, bit) \
    dsp->yuv2rgb[idx][0] = yuv2rgb_444p##bit##_c; \
    dsp->yuv2rgb[idx][1] = yuv2rgb_422p##bit##_c; \
    dsp->yuv2rgb[idx][2] = yuv2rgb_420p##bit##_c

    init_yuv2rgb_fn(0,  8);
    init_yuv2rgb_fn(1, 10);
    init_yuv2rgb_fn(2, 12);

#define init_rgb2yuv_fn(idx, bit) \
    dsp->rgb2yuv[idx][0] = rgb2yuv_444p##bit##_c; \
    dsp->rgb2yuv[idx][1] = rgb2yuv_422p##bit##_c; \
    dsp->rgb2yuv[idx][2] = rgb2yuv_420p##bit##_c

    init_rgb2yuv_fn(0,  8);
    init_rgb2yuv_fn(1, 10);
    init_rgb2yuv_fn(2, 12);

#define init_yuv2yuv_fn(idx1, idx2, bit1, bit2) \
    dsp->yuv2yuv[idx1][idx2][0] = yuv2yuv_444p##bit1##to##bit2##_c; \
    dsp->yuv2yuv[idx1][idx2][1] = yuv2yuv_422p##bit1##to##bit2##_c; \
    dsp->yuv2yuv[idx1][idx2][2] = yuv2yuv_420p##bit1##to##bit2##_c
#define init_yuv2yuv_fns(idx1, bit1) \
    init_yuv2yuv_fn(idx1, 0, bit1,  8); \
    init_yuv2yuv_fn(idx1, 1, bit1, 10); \
    init_yuv2yuv_fn(idx1, 2, bit1, 12)

    init_yuv2yuv_fns(0,  8);
    init_yuv2yuv_fns(1, 10);
    init_yuv2yuv_fns(2, 12);

    dsp->multiply3x3 = multiply3x3_c;

    if (ARCH_X86)
        ff_colorspacedsp_x86_init(dsp);
}
