/*
 * Copyright (C) 2010 David Conrad
 * Copyright (C) 2010 Ronald S. Bultje
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

/**
 * @file
 * VP8 compatible video decoder
 */

#ifndef AVCODEC_VP8DSP_H
#define AVCODEC_VP8DSP_H

#include <stddef.h>
#include <stdint.h>

typedef void (*vp8_mc_func)(uint8_t *dst/*align 8*/, ptrdiff_t dstStride,
                            uint8_t *src/*align 1*/, ptrdiff_t srcStride,
                            int h, int x, int y);

typedef struct VP8DSPContext {
    void (*vp8_luma_dc_wht)(int16_t block[4][4][16], int16_t dc[16]);
    void (*vp8_luma_dc_wht_dc)(int16_t block[4][4][16], int16_t dc[16]);
    void (*vp8_idct_add)(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
    void (*vp8_idct_dc_add)(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
    void (*vp8_idct_dc_add4y)(uint8_t *dst, int16_t block[4][16],
                              ptrdiff_t stride);
    void (*vp8_idct_dc_add4uv)(uint8_t *dst, int16_t block[4][16],
                               ptrdiff_t stride);

    // loop filter applied to edges between macroblocks
    void (*vp8_v_loop_filter16y)(uint8_t *dst, ptrdiff_t stride,
                                 int flim_E, int flim_I, int hev_thresh);
    void (*vp8_h_loop_filter16y)(uint8_t *dst, ptrdiff_t stride,
                                 int flim_E, int flim_I, int hev_thresh);
    void (*vp8_v_loop_filter8uv)(uint8_t *dstU, uint8_t *dstV, ptrdiff_t stride,
                                 int flim_E, int flim_I, int hev_thresh);
    void (*vp8_h_loop_filter8uv)(uint8_t *dstU, uint8_t *dstV, ptrdiff_t stride,
                                 int flim_E, int flim_I, int hev_thresh);

    // loop filter applied to inner macroblock edges
    void (*vp8_v_loop_filter16y_inner)(uint8_t *dst, ptrdiff_t stride,
                                       int flim_E, int flim_I, int hev_thresh);
    void (*vp8_h_loop_filter16y_inner)(uint8_t *dst, ptrdiff_t stride,
                                       int flim_E, int flim_I, int hev_thresh);
    void (*vp8_v_loop_filter8uv_inner)(uint8_t *dstU, uint8_t *dstV,
                                       ptrdiff_t stride,
                                       int flim_E, int flim_I, int hev_thresh);
    void (*vp8_h_loop_filter8uv_inner)(uint8_t *dstU, uint8_t *dstV,
                                       ptrdiff_t stride,
                                       int flim_E, int flim_I, int hev_thresh);

    void (*vp8_v_loop_filter_simple)(uint8_t *dst, ptrdiff_t stride, int flim);
    void (*vp8_h_loop_filter_simple)(uint8_t *dst, ptrdiff_t stride, int flim);

    /**
     * first dimension: width>>3, height is assumed equal to width
     * second dimension: 0 if no vertical interpolation is needed;
     *                   1 4-tap vertical interpolation filter (my & 1)
     *                   2 6-tap vertical interpolation filter (!(my & 1))
     * third dimension: same as second dimension, for horizontal interpolation
     * so something like put_vp8_epel_pixels_tab[width>>3][2*!!my-(my&1)][2*!!mx-(mx&1)](..., mx, my)
     */
    vp8_mc_func put_vp8_epel_pixels_tab[3][3][3];
    vp8_mc_func put_vp8_bilinear_pixels_tab[3][3][3];
} VP8DSPContext;

void ff_put_vp8_pixels16_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                           int h, int x, int y);
void ff_put_vp8_pixels8_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                          int h, int x, int y);
void ff_put_vp8_pixels4_c(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                          int h, int x, int y);

void ff_vp8dsp_init(VP8DSPContext *c, int vp7);
void ff_vp8dsp_init_x86(VP8DSPContext *c, int vp7);
void ff_vp8dsp_init_arm(VP8DSPContext *c, int vp7);
void ff_vp8dsp_init_ppc(VP8DSPContext *c);

#endif /* AVCODEC_VP8DSP_H */
