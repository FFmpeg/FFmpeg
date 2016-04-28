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

#ifndef AVFILTER_COLORSPACEDSP_H
#define AVFILTER_COLORSPACEDSP_H

#include <stddef.h>
#include <stdint.h>

typedef void (*yuv2rgb_fn)(int16_t *rgb[3], ptrdiff_t rgb_stride,
                           uint8_t *yuv[3], ptrdiff_t yuv_stride[3],
                           int w, int h, const int16_t yuv2rgb_coeffs[3][3][8],
                           const int16_t yuv_offset[8]);
typedef void (*rgb2yuv_fn)(uint8_t *yuv[3], ptrdiff_t yuv_stride[3],
                           int16_t *rgb[3], ptrdiff_t rgb_stride,
                           int w, int h, const int16_t rgb2yuv_coeffs[3][3][8],
                           const int16_t yuv_offset[8]);
typedef void (*yuv2yuv_fn)(uint8_t *yuv_out[3], ptrdiff_t yuv_out_stride[3],
                           uint8_t *yuv_in[3], ptrdiff_t yuv_in_stride[3],
                           int w, int h, const int16_t yuv2yuv_coeffs[3][3][8],
                           const int16_t yuv_offset[2][8]);

typedef struct ColorSpaceDSPContext {
    yuv2rgb_fn yuv2rgb[3 /* 0: 8bit, 1: 10bit, 2: 12bit */][3 /* 0: 444, 1: 422, 2: 420 */];
    rgb2yuv_fn rgb2yuv[3 /* 0: 8bit, 1: 10bit, 2: 12bit */][3 /* 0: 444, 1: 422, 2: 420 */];
    yuv2yuv_fn yuv2yuv[3 /* in_depth */][3 /* out_depth */][3 /* 0: 444, 1: 422, 2: 420 */];

    void (*multiply3x3)(int16_t *data[3], ptrdiff_t stride,
                        int w, int h, const int16_t m[3][3][8]);
} ColorSpaceDSPContext;

void ff_colorspacedsp_init(ColorSpaceDSPContext *dsp);

/* internal */
void ff_colorspacedsp_x86_init(ColorSpaceDSPContext *dsp);

#endif /* AVFILTER_COLORSPACEDSP_H */
