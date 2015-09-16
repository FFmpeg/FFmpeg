/*
 * VP9 SIMD optimizations
 *
 * Copyright (c) 2013 Ronald S. Bultje <rsbultje gmail com>
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

#ifndef AVCODEC_X86_VP9DSP_INIT_H
#define AVCODEC_X86_VP9DSP_INIT_H

#define decl_fpel_func(avg, sz, bpp, opt) \
void ff_vp9_##avg##sz##bpp##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                   const uint8_t *src, ptrdiff_t src_stride, \
                                   int h, int mx, int my)

#define init_fpel_func(idx1, idx2, sz, type, bpp, opt) \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][0][0] = ff_vp9_##type##sz##bpp##_##opt

void ff_vp9dsp_init_16bpp_x86(VP9DSPContext *dsp, int bpp);

#endif /* AVCODEC_X86_VP9DSP_INIT_H */
