/*
 * HEVC video Decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#ifndef AVCODEC_HEVCDSP_H
#define AVCODEC_HEVCDSP_H

#include "get_bits.h"

struct SAOParams;

typedef struct HEVCDSPContext {
    void (*put_pcm)(uint8_t *_dst, ptrdiff_t _stride, int size,
                    GetBitContext *gb, int pcm_bit_depth);

    void (*transquant_bypass[4])(uint8_t *_dst, int16_t *coeffs, ptrdiff_t _stride);

    void (*transform_skip)(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride);

    void (*transform_4x4_luma_add)(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride);

    void (*transform_add[4])(uint8_t *dst, int16_t *coeffs, ptrdiff_t _stride);

    void (*sao_band_filter[4])( uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride, struct SAOParams *sao, int *borders, int width, int height, int c_idx);

    void (*sao_edge_filter[4])(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride,  struct SAOParams *sao, int *borders, int _width, int _height, int c_idx, uint8_t vert_edge, uint8_t horiz_edge, uint8_t diag_edge);


    void (*put_hevc_qpel[4][4])(int16_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                int width, int height, int16_t* mcbuffer);

    void (*put_hevc_epel[2][2])(int16_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                             int width, int height, int mx, int my, int16_t* mcbuffer);

    void (*put_unweighted_pred)(uint8_t *dst, ptrdiff_t dststride, int16_t *src, ptrdiff_t srcstride,
                                int width, int height);

    void (*put_weighted_pred_avg)(uint8_t *dst, ptrdiff_t dststride, int16_t *src1, int16_t *src2,
                                  ptrdiff_t srcstride, int width, int height);
    void (*weighted_pred)(uint8_t denom, int16_t wlxFlag, int16_t olxFlag, uint8_t *dst, ptrdiff_t dststride, int16_t *src,
                                  ptrdiff_t srcstride, int width, int height);
    void (*weighted_pred_avg)(uint8_t denom, int16_t wl0Flag, int16_t wl1Flag, int16_t ol0Flag, int16_t ol1Flag,
                                   uint8_t *dst, ptrdiff_t dststride, int16_t *src1, int16_t *src2,
                                   ptrdiff_t srcstride, int width, int height);
    void (*hevc_h_loop_filter_luma)(uint8_t *_pix, ptrdiff_t _stride, int *_beta, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_v_loop_filter_luma)(uint8_t *_pix, ptrdiff_t _stride, int *_beta, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_h_loop_filter_chroma)(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_v_loop_filter_chroma)(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_h_loop_filter_luma_c)(uint8_t *_pix, ptrdiff_t _stride, int *_beta, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_v_loop_filter_luma_c)(uint8_t *_pix, ptrdiff_t _stride, int *_beta, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_h_loop_filter_chroma_c)(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
    void (*hevc_v_loop_filter_chroma_c)(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
} HEVCDSPContext;

void ff_hevc_dsp_init(HEVCDSPContext *hpc, int bit_depth);

extern const int8_t ff_hevc_epel_filters[7][16];

#endif /* AVCODEC_HEVCDSP_H */
