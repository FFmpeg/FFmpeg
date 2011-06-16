/*
 * Copyright (c) 2003-2010 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 DSP functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_H264DSP_H
#define AVCODEC_H264DSP_H

#include <stdint.h>
#include "dsputil.h"

//typedef void (*h264_chroma_mc_func)(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int srcStride, int h, int x, int y);
typedef void (*h264_weight_func)(uint8_t *block, int stride, int log2_denom, int weight, int offset);
typedef void (*h264_biweight_func)(uint8_t *dst, uint8_t *src, int stride, int log2_denom, int weightd, int weights, int offset);

/**
 * Context for storing H.264 DSP functions
 */
typedef struct H264DSPContext{
    /* weighted MC */
    h264_weight_func weight_h264_pixels_tab[10];
    h264_biweight_func biweight_h264_pixels_tab[10];

    /* loop filter */
    void (*h264_v_loop_filter_luma)(uint8_t *pix/*align 16*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_luma)(uint8_t *pix/*align 4 */, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_luma_mbaff)(uint8_t *pix/*align 16*/, int stride, int alpha, int beta, int8_t *tc0);
    /* v/h_loop_filter_luma_intra: align 16 */
    void (*h264_v_loop_filter_luma_intra)(uint8_t *pix, int stride, int alpha, int beta);
    void (*h264_h_loop_filter_luma_intra)(uint8_t *pix, int stride, int alpha, int beta);
    void (*h264_h_loop_filter_luma_mbaff_intra)(uint8_t *pix/*align 16*/, int stride, int alpha, int beta);
    void (*h264_v_loop_filter_chroma)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_chroma)(uint8_t *pix/*align 4*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_chroma_mbaff)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta, int8_t *tc0);
    void (*h264_v_loop_filter_chroma_intra)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta);
    void (*h264_h_loop_filter_chroma_intra)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta);
    void (*h264_h_loop_filter_chroma_mbaff_intra)(uint8_t *pix/*align 8*/, int stride, int alpha, int beta);
    // h264_loop_filter_strength: simd only. the C version is inlined in h264.c
    void (*h264_loop_filter_strength)(int16_t bS[2][4][4], uint8_t nnz[40], int8_t ref[2][40], int16_t mv[2][40][2],
                                      int bidir, int edges, int step, int mask_mv0, int mask_mv1, int field);

    /* IDCT */
    void (*h264_idct_add)(uint8_t *dst/*align 4*/, DCTELEM *block/*align 16*/, int stride);
    void (*h264_idct8_add)(uint8_t *dst/*align 8*/, DCTELEM *block/*align 16*/, int stride);
    void (*h264_idct_dc_add)(uint8_t *dst/*align 4*/, DCTELEM *block/*align 16*/, int stride);
    void (*h264_idct8_dc_add)(uint8_t *dst/*align 8*/, DCTELEM *block/*align 16*/, int stride);

    void (*h264_idct_add16)(uint8_t *dst/*align 16*/, const int *blockoffset, DCTELEM *block/*align 16*/, int stride, const uint8_t nnzc[15*8]);
    void (*h264_idct8_add4)(uint8_t *dst/*align 16*/, const int *blockoffset, DCTELEM *block/*align 16*/, int stride, const uint8_t nnzc[15*8]);
    void (*h264_idct_add8)(uint8_t **dst/*align 16*/, const int *blockoffset, DCTELEM *block/*align 16*/, int stride, const uint8_t nnzc[15*8]);
    void (*h264_idct_add16intra)(uint8_t *dst/*align 16*/, const int *blockoffset, DCTELEM *block/*align 16*/, int stride, const uint8_t nnzc[15*8]);
    void (*h264_luma_dc_dequant_idct)(DCTELEM *output, DCTELEM *input/*align 16*/, int qmul);
    void (*h264_chroma_dc_dequant_idct)(DCTELEM *block, int qmul);
}H264DSPContext;

void ff_h264dsp_init(H264DSPContext *c, const int bit_depth);
void ff_h264dsp_init_arm(H264DSPContext *c, const int bit_depth);
void ff_h264dsp_init_ppc(H264DSPContext *c, const int bit_depth);
void ff_h264dsp_init_x86(H264DSPContext *c, const int bit_depth);

#endif /* AVCODEC_H264DSP_H */
