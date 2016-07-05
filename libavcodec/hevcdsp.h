/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#ifndef AVCODEC_HEVCDSP_H
#define AVCODEC_HEVCDSP_H

#include "get_bits.h"

typedef struct SAOParams {
    int offset_abs[3][4];   ///< sao_offset_abs
    int offset_sign[3][4];  ///< sao_offset_sign

    int band_position[3];   ///< sao_band_position

    int eo_class[3];        ///< sao_eo_class

    int offset_val[3][5];   ///<SaoOffsetVal

    uint8_t type_idx[3];    ///< sao_type_idx
} SAOParams;

typedef struct HEVCDSPContext {
    void (*put_pcm)(uint8_t *dst, ptrdiff_t stride, int size,
                    GetBitContext *gb, int pcm_bit_depth);

    void (*add_residual[4])(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride);

    void (*dequant)(int16_t *coeffs);
    void (*transform_4x4_luma)(int16_t *coeffs);
    void (*idct[4])(int16_t *coeffs, int col_limit);
    void (*idct_dc[4])(int16_t *coeffs);

    void (*sao_band_filter[4])(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                               struct SAOParams *sao, int *borders,
                               int width, int height, int c_idx);
    void (*sao_edge_filter[4])(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                               struct SAOParams *sao, int *borders, int width,
                               int height, int c_idx, uint8_t vert_edge,
                               uint8_t horiz_edge, uint8_t diag_edge);

    void (*put_hevc_qpel[2][2][8])(int16_t *dst, ptrdiff_t dststride, uint8_t *src,
                                   ptrdiff_t srcstride, int height,
                                   int mx, int my, int16_t *mcbuffer);
    void (*put_hevc_epel[2][2][8])(int16_t *dst, ptrdiff_t dststride, uint8_t *src,
                                   ptrdiff_t srcstride, int height,
                                   int mx, int my, int16_t *mcbuffer);

    void (*put_unweighted_pred[8])(uint8_t *dst, ptrdiff_t dststride, int16_t *src,
                                   ptrdiff_t srcstride, int height);
    void (*put_unweighted_pred_chroma[8])(uint8_t *dst, ptrdiff_t dststride, int16_t *src,
                                          ptrdiff_t srcstride, int height);
    void (*put_unweighted_pred_avg[8])(uint8_t *dst, ptrdiff_t dststride,
                                       int16_t *src1, int16_t *src2,
                                       ptrdiff_t srcstride, int height);
    void (*put_unweighted_pred_avg_chroma[8])(uint8_t *dst, ptrdiff_t dststride,
                                              int16_t *src1, int16_t *src2,
                                              ptrdiff_t srcstride, int height);
    void (*weighted_pred[8])(uint8_t denom, int16_t wlxFlag, int16_t olxFlag,
                             uint8_t *dst, ptrdiff_t dststride, int16_t *src,
                             ptrdiff_t srcstride, int height);
    void (*weighted_pred_chroma[8])(uint8_t denom, int16_t wlxFlag, int16_t olxFlag,
                                    uint8_t *dst, ptrdiff_t dststride, int16_t *src,
                                    ptrdiff_t srcstride, int height);
    void (*weighted_pred_avg[8])(uint8_t denom, int16_t wl0Flag, int16_t wl1Flag,
                                 int16_t ol0Flag, int16_t ol1Flag, uint8_t *dst,
                                 ptrdiff_t dststride, int16_t *src1, int16_t *src2,
                                 ptrdiff_t srcstride, int height);
    void (*weighted_pred_avg_chroma[8])(uint8_t denom, int16_t wl0Flag, int16_t wl1Flag,
                                        int16_t ol0Flag, int16_t ol1Flag, uint8_t *dst,
                                        ptrdiff_t dststride, int16_t *src1, int16_t *src2,
                                        ptrdiff_t srcstride, int height);

    void (*hevc_h_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                    int beta, int *tc,
                                    uint8_t *no_p, uint8_t *no_q);
    void (*hevc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                    int beta, int *tc,
                                    uint8_t *no_p, uint8_t *no_q);
    void (*hevc_h_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                      int *tc, uint8_t *no_p, uint8_t *no_q);
    void (*hevc_v_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                      int *tc, uint8_t *no_p, uint8_t *no_q);
    void (*hevc_h_loop_filter_luma_c)(uint8_t *pix, ptrdiff_t stride,
                                      int beta, int *tc,
                                      uint8_t *no_p, uint8_t *no_q);
    void (*hevc_v_loop_filter_luma_c)(uint8_t *pix, ptrdiff_t stride,
                                      int beta, int *tc,
                                      uint8_t *no_p, uint8_t *no_q);
    void (*hevc_h_loop_filter_chroma_c)(uint8_t *pix, ptrdiff_t stride,
                                        int *tc, uint8_t *no_p,
                                        uint8_t *no_q);
    void (*hevc_v_loop_filter_chroma_c)(uint8_t *pix, ptrdiff_t stride,
                                        int *tc, uint8_t *no_p,
                                        uint8_t *no_q);
} HEVCDSPContext;

void ff_hevc_dsp_init(HEVCDSPContext *hpc, int bit_depth);

void ff_hevc_dsp_init_x86(HEVCDSPContext *c, const int bit_depth);

extern const int16_t ff_hevc_epel_coeffs[7][16];
extern const int8_t ff_hevc_epel_coeffs8[7][16];
extern const int16_t ff_hevc_qpel_coeffs[3][8];
extern const int8_t ff_hevc_qpel_coeffs8[3][16];

#endif /* AVCODEC_HEVCDSP_H */
