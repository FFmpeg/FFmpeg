/*
 * VC-1 and WMV3 decoder - DSP functions
 * Copyright (c) 2006 Konstantin Shishkov
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
 * VC-1 and WMV3 decoder
 *
 */

#ifndef AVCODEC_VC1DSP_H
#define AVCODEC_VC1DSP_H

#include "dsputil.h"

typedef struct VC1DSPContext {
    /* vc1 functions */
    void (*vc1_inv_trans_8x8)(DCTELEM *b);
    void (*vc1_inv_trans_8x4)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_inv_trans_4x8)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_inv_trans_4x4)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_inv_trans_8x8_dc)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_inv_trans_8x4_dc)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_inv_trans_4x8_dc)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_inv_trans_4x4_dc)(uint8_t *dest, int line_size, DCTELEM *block);
    void (*vc1_v_overlap)(uint8_t *src, int stride);
    void (*vc1_h_overlap)(uint8_t *src, int stride);
    void (*vc1_v_s_overlap)(DCTELEM *top,  DCTELEM *bottom);
    void (*vc1_h_s_overlap)(DCTELEM *left, DCTELEM *right);
    void (*vc1_v_loop_filter4)(uint8_t *src, int stride, int pq);
    void (*vc1_h_loop_filter4)(uint8_t *src, int stride, int pq);
    void (*vc1_v_loop_filter8)(uint8_t *src, int stride, int pq);
    void (*vc1_h_loop_filter8)(uint8_t *src, int stride, int pq);
    void (*vc1_v_loop_filter16)(uint8_t *src, int stride, int pq);
    void (*vc1_h_loop_filter16)(uint8_t *src, int stride, int pq);

    /* put 8x8 block with bicubic interpolation and quarterpel precision
     * last argument is actually round value instead of height
     */
    op_pixels_func put_vc1_mspel_pixels_tab[16];
    op_pixels_func avg_vc1_mspel_pixels_tab[16];

    /* This is really one func used in VC-1 decoding */
    h264_chroma_mc_func put_no_rnd_vc1_chroma_pixels_tab[3];
    h264_chroma_mc_func avg_no_rnd_vc1_chroma_pixels_tab[3];
} VC1DSPContext;

void ff_vc1dsp_init(VC1DSPContext* c);
void ff_vc1dsp_init_altivec(VC1DSPContext* c);
void ff_vc1dsp_init_mmx(VC1DSPContext* dsp);

#endif /* AVCODEC_VC1DSP_H */
