/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2013 - 2014 Pierre-Edouard Lepere
 *
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

#define MAX_PB_SIZE 64

typedef struct SAOParams {
    int offset_abs[3][4];   ///< sao_offset_abs
    int offset_sign[3][4];  ///< sao_offset_sign

    uint8_t band_position[3];   ///< sao_band_position

    int eo_class[3];        ///< sao_eo_class

    int16_t offset_val[3][5];   ///<SaoOffsetVal

    uint8_t type_idx[3];    ///< sao_type_idx
} SAOParams;

typedef struct HEVCDSPContext {
    void (*put_pcm)(uint8_t *_dst, ptrdiff_t _stride, int width, int height,
                    struct GetBitContext *gb, int pcm_bit_depth);

    void (*transform_add[4])(uint8_t *_dst, int16_t *coeffs, ptrdiff_t _stride);

    void (*transform_skip)(int16_t *coeffs, int16_t log2_size);

    void (*transform_rdpcm)(int16_t *coeffs, int16_t log2_size, int mode);

    void (*idct_4x4_luma)(int16_t *coeffs);

    void (*idct[4])(int16_t *coeffs, int col_limit);

    void (*idct_dc[4])(int16_t *coeffs);

    void (*sao_band_filter)(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,
                            struct SAOParams *sao, int *borders,
                            int width, int height, int c_idx);

    void (*sao_edge_filter[2])(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,
                               struct SAOParams *sao, int *borders, int _width,
                               int _height, int c_idx, uint8_t *vert_edge,
                               uint8_t *horiz_edge, uint8_t *diag_edge);

    void (*put_hevc_qpel[10][2][2])(int16_t *dst, uint8_t *src, ptrdiff_t srcstride,
                                    int height, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_qpel_uni[10][2][2])(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                        int height, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_qpel_uni_w[10][2][2])(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                          int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width);

    void (*put_hevc_qpel_bi[10][2][2])(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                       int16_t *src2,
                                       int height, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_qpel_bi_w[10][2][2])(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                         int16_t *src2,
                                         int height, int denom, int wx0, int wx1,
                                         int ox0, int ox1, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_epel[10][2][2])(int16_t *dst, uint8_t *src, ptrdiff_t srcstride,
                                    int height, intptr_t mx, intptr_t my, int width);

    void (*put_hevc_epel_uni[10][2][2])(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                        int height, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_epel_uni_w[10][2][2])(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                          int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_epel_bi[10][2][2])(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                       int16_t *src2,
                                       int height, intptr_t mx, intptr_t my, int width);
    void (*put_hevc_epel_bi_w[10][2][2])(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                         int16_t *src2,
                                         int height, int denom, int wx0, int ox0, int wx1,
                                         int ox1, intptr_t mx, intptr_t my, int width);

    void (*hevc_h_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                    int beta, int32_t *tc,
                                    uint8_t *no_p, uint8_t *no_q);
    void (*hevc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                    int beta, int32_t *tc,
                                    uint8_t *no_p, uint8_t *no_q);
    void (*hevc_h_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                      int32_t *tc, uint8_t *no_p, uint8_t *no_q);
    void (*hevc_v_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                      int32_t *tc, uint8_t *no_p, uint8_t *no_q);
    void (*hevc_h_loop_filter_luma_c)(uint8_t *pix, ptrdiff_t stride,
                                      int beta, int32_t *tc,
                                      uint8_t *no_p, uint8_t *no_q);
    void (*hevc_v_loop_filter_luma_c)(uint8_t *pix, ptrdiff_t stride,
                                      int beta, int32_t *tc,
                                      uint8_t *no_p, uint8_t *no_q);
    void (*hevc_h_loop_filter_chroma_c)(uint8_t *pix, ptrdiff_t stride,
                                        int32_t *tc, uint8_t *no_p,
                                        uint8_t *no_q);
    void (*hevc_v_loop_filter_chroma_c)(uint8_t *pix, ptrdiff_t stride,
                                        int32_t *tc, uint8_t *no_p,
                                        uint8_t *no_q);
} HEVCDSPContext;

void ff_hevc_dsp_init(HEVCDSPContext *hpc, int bit_depth);

extern const int8_t ff_hevc_epel_filters[7][4];
extern const int8_t ff_hevc_qpel_filters[3][16];

void ff_hevc_dsp_init_x86(HEVCDSPContext *c, const int bit_depth);

#endif /* AVCODEC_HEVCDSP_H */
