/*
 * Copyright (c) 2014 Seppo Tomperi <seppo.tomperi@vtt.fi>
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

#include "libavutil/attributes.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/hevcdsp.h"
#include "libavcodec/avcodec.h"
#include "hevcdsp_arm.h"

void ff_hevc_sao_band_filter_neon_8_wrapper(uint8_t *_dst, const uint8_t *_src,
                                  ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                  const int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height);
void ff_hevc_sao_edge_filter_neon_8_wrapper(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst, const int16_t *sao_offset_val,
                                  int eo, int width, int height);

void ff_hevc_v_loop_filter_luma_neon(uint8_t *_pix, ptrdiff_t _stride, int _beta, const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_h_loop_filter_luma_neon(uint8_t *_pix, ptrdiff_t _stride, int _beta, const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_v_loop_filter_chroma_neon(uint8_t *_pix, ptrdiff_t _stride, const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_h_loop_filter_chroma_neon(uint8_t *_pix, ptrdiff_t _stride, const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_add_residual_4x4_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                     ptrdiff_t stride);
void ff_hevc_add_residual_4x4_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_8x8_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                     ptrdiff_t stride);
void ff_hevc_add_residual_8x8_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_16x16_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                       ptrdiff_t stride);
void ff_hevc_add_residual_16x16_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_add_residual_32x32_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                       ptrdiff_t stride);
void ff_hevc_add_residual_32x32_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_idct_4x4_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_4x4_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_4x4_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_32x32_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_32x32_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_transform_luma_4x4_neon_8(int16_t *coeffs);

#define PUT_PIXELS(name) \
    void name(int16_t *dst, const uint8_t *src, \
                                ptrdiff_t srcstride, int height, \
                                intptr_t mx, intptr_t my, int width)
PUT_PIXELS(ff_hevc_put_pixels_w2_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w4_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w6_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w8_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w12_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w16_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w24_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w32_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w48_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w64_neon_8);
#undef PUT_PIXELS

static void (*put_hevc_qpel_neon[4][4])(int16_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                                   int height, int width);
static void (*put_hevc_qpel_uw_neon[4][4])(uint8_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t _srcstride,
                                   int width, int height, const int16_t *src2, ptrdiff_t src2stride);
void ff_hevc_put_qpel_neon_wrapper(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_bi_neon_wrapper(uint8_t *dst, ptrdiff_t dststride,
                                      const uint8_t *src, ptrdiff_t srcstride, const int16_t *src2,
                                       int height, intptr_t mx, intptr_t my, int width);
#define QPEL_FUNC(name) \
    void name(int16_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride, \
                                   int height, int width)

QPEL_FUNC(ff_hevc_put_qpel_v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_v3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1v3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2v3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3v3_neon_8);
#undef QPEL_FUNC

#define QPEL_FUNC_UW_PIX(name) \
    void name(uint8_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t _srcstride, \
                                   int height, intptr_t mx, intptr_t my, int width);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w4_neon_8);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w8_neon_8);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w16_neon_8);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w24_neon_8);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w32_neon_8);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w48_neon_8);
QPEL_FUNC_UW_PIX(ff_hevc_put_qpel_uw_pixels_w64_neon_8);
#undef QPEL_FUNC_UW_PIX

#define QPEL_FUNC_UW(name) \
    void name(uint8_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t _srcstride, \
              int width, int height, const int16_t* src2, ptrdiff_t src2stride);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_pixels_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_v3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1v3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2v3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3v3_neon_8);
#undef QPEL_FUNC_UW

void ff_hevc_sao_band_filter_neon_8(uint8_t *dst, const uint8_t *src, ptrdiff_t stride_dst, ptrdiff_t stride_src, int width, int height, int16_t *offset_table);

void ff_hevc_sao_band_filter_neon_8_wrapper(uint8_t *_dst, const uint8_t *_src,
                                  ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                  const int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height) {
    uint8_t *dst = _dst;
    const uint8_t *src = _src;
    int16_t offset_table[32] = {0};
    int k;

    for (k = 0; k < 4; k++) {
        offset_table[(k + sao_left_class) & 31] = sao_offset_val[k + 1];
    }

    ff_hevc_sao_band_filter_neon_8(dst, src, stride_dst, stride_src, width, height, offset_table);
}

void ff_hevc_sao_edge_filter_neon_8(uint8_t *dst, const uint8_t *src, ptrdiff_t stride_dst, ptrdiff_t stride_src, int width, int height,
                                    int a_stride, int b_stride, const int16_t *sao_offset_val, const uint8_t *edge_idx);

void ff_hevc_sao_edge_filter_neon_8_wrapper(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst, const int16_t *sao_offset_val,
                                  int eo, int width, int height) {
    static uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };
    static const int8_t pos[4][2][2] = {
        { { -1,  0 }, {  1, 0 } }, // horizontal
        { {  0, -1 }, {  0, 1 } }, // vertical
        { { -1, -1 }, {  1, 1 } }, // 45 degree
        { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    uint8_t *dst = _dst;
    const uint8_t *src = _src;
    int a_stride, b_stride;
    ptrdiff_t stride_src = (2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);

    a_stride = pos[eo][0][0] + pos[eo][0][1] * stride_src;
    b_stride = pos[eo][1][0] + pos[eo][1][1] * stride_src;

    ff_hevc_sao_edge_filter_neon_8(dst, src, stride_dst, stride_src, width, height, a_stride, b_stride, sao_offset_val, edge_idx);
}

void ff_hevc_put_qpel_neon_wrapper(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width) {

    put_hevc_qpel_neon[my][mx](dst, MAX_PB_SIZE, src, srcstride, height, width);
}

void ff_hevc_put_qpel_uni_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width) {

    put_hevc_qpel_uw_neon[my][mx](dst, dststride, src, srcstride, width, height, NULL, 0);
}

void ff_hevc_put_qpel_bi_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                                       const int16_t *src2,
                                       int height, intptr_t mx, intptr_t my, int width) {
    put_hevc_qpel_uw_neon[my][mx](dst, dststride, src, srcstride, width, height, src2, MAX_PB_SIZE);
}

av_cold void ff_hevc_dsp_init_neon(HEVCDSPContext *c, const int bit_depth)
{
    if (bit_depth == 8) {
        int x;
        c->hevc_v_loop_filter_luma     = ff_hevc_v_loop_filter_luma_neon;
        c->hevc_h_loop_filter_luma     = ff_hevc_h_loop_filter_luma_neon;
        c->hevc_v_loop_filter_chroma   = ff_hevc_v_loop_filter_chroma_neon;
        c->hevc_h_loop_filter_chroma   = ff_hevc_h_loop_filter_chroma_neon;
        c->sao_band_filter[0]          = ff_hevc_sao_band_filter_neon_8_wrapper;
        c->sao_band_filter[1]          = ff_hevc_sao_band_filter_neon_8_wrapper;
        c->sao_band_filter[2]          = ff_hevc_sao_band_filter_neon_8_wrapper;
        c->sao_band_filter[3]          = ff_hevc_sao_band_filter_neon_8_wrapper;
        c->sao_band_filter[4]          = ff_hevc_sao_band_filter_neon_8_wrapper;
        c->sao_edge_filter[0]          = ff_hevc_sao_edge_filter_neon_8_wrapper;
        c->sao_edge_filter[1]          = ff_hevc_sao_edge_filter_neon_8_wrapper;
        c->sao_edge_filter[2]          = ff_hevc_sao_edge_filter_neon_8_wrapper;
        c->sao_edge_filter[3]          = ff_hevc_sao_edge_filter_neon_8_wrapper;
        c->sao_edge_filter[4]          = ff_hevc_sao_edge_filter_neon_8_wrapper;
        c->add_residual[0]             = ff_hevc_add_residual_4x4_8_neon;
        c->add_residual[1]             = ff_hevc_add_residual_8x8_8_neon;
        c->add_residual[2]             = ff_hevc_add_residual_16x16_8_neon;
        c->add_residual[3]             = ff_hevc_add_residual_32x32_8_neon;
        c->idct_dc[0]                  = ff_hevc_idct_4x4_dc_8_neon;
        c->idct_dc[1]                  = ff_hevc_idct_8x8_dc_8_neon;
        c->idct_dc[2]                  = ff_hevc_idct_16x16_dc_8_neon;
        c->idct_dc[3]                  = ff_hevc_idct_32x32_dc_8_neon;
        c->idct[0]                     = ff_hevc_idct_4x4_8_neon;
        c->idct[1]                     = ff_hevc_idct_8x8_8_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_8_neon;
        c->idct[3]                     = ff_hevc_idct_32x32_8_neon;
        c->transform_4x4_luma          = ff_hevc_transform_luma_4x4_neon_8;
        put_hevc_qpel_neon[1][0]       = ff_hevc_put_qpel_v1_neon_8;
        put_hevc_qpel_neon[2][0]       = ff_hevc_put_qpel_v2_neon_8;
        put_hevc_qpel_neon[3][0]       = ff_hevc_put_qpel_v3_neon_8;
        put_hevc_qpel_neon[0][1]       = ff_hevc_put_qpel_h1_neon_8;
        put_hevc_qpel_neon[0][2]       = ff_hevc_put_qpel_h2_neon_8;
        put_hevc_qpel_neon[0][3]       = ff_hevc_put_qpel_h3_neon_8;
        put_hevc_qpel_neon[1][1]       = ff_hevc_put_qpel_h1v1_neon_8;
        put_hevc_qpel_neon[1][2]       = ff_hevc_put_qpel_h2v1_neon_8;
        put_hevc_qpel_neon[1][3]       = ff_hevc_put_qpel_h3v1_neon_8;
        put_hevc_qpel_neon[2][1]       = ff_hevc_put_qpel_h1v2_neon_8;
        put_hevc_qpel_neon[2][2]       = ff_hevc_put_qpel_h2v2_neon_8;
        put_hevc_qpel_neon[2][3]       = ff_hevc_put_qpel_h3v2_neon_8;
        put_hevc_qpel_neon[3][1]       = ff_hevc_put_qpel_h1v3_neon_8;
        put_hevc_qpel_neon[3][2]       = ff_hevc_put_qpel_h2v3_neon_8;
        put_hevc_qpel_neon[3][3]       = ff_hevc_put_qpel_h3v3_neon_8;
        put_hevc_qpel_uw_neon[1][0]      = ff_hevc_put_qpel_uw_v1_neon_8;
        put_hevc_qpel_uw_neon[2][0]      = ff_hevc_put_qpel_uw_v2_neon_8;
        put_hevc_qpel_uw_neon[3][0]      = ff_hevc_put_qpel_uw_v3_neon_8;
        put_hevc_qpel_uw_neon[0][1]      = ff_hevc_put_qpel_uw_h1_neon_8;
        put_hevc_qpel_uw_neon[0][2]      = ff_hevc_put_qpel_uw_h2_neon_8;
        put_hevc_qpel_uw_neon[0][3]      = ff_hevc_put_qpel_uw_h3_neon_8;
        put_hevc_qpel_uw_neon[1][1]      = ff_hevc_put_qpel_uw_h1v1_neon_8;
        put_hevc_qpel_uw_neon[1][2]      = ff_hevc_put_qpel_uw_h2v1_neon_8;
        put_hevc_qpel_uw_neon[1][3]      = ff_hevc_put_qpel_uw_h3v1_neon_8;
        put_hevc_qpel_uw_neon[2][1]      = ff_hevc_put_qpel_uw_h1v2_neon_8;
        put_hevc_qpel_uw_neon[2][2]      = ff_hevc_put_qpel_uw_h2v2_neon_8;
        put_hevc_qpel_uw_neon[2][3]      = ff_hevc_put_qpel_uw_h3v2_neon_8;
        put_hevc_qpel_uw_neon[3][1]      = ff_hevc_put_qpel_uw_h1v3_neon_8;
        put_hevc_qpel_uw_neon[3][2]      = ff_hevc_put_qpel_uw_h2v3_neon_8;
        put_hevc_qpel_uw_neon[3][3]      = ff_hevc_put_qpel_uw_h3v3_neon_8;
        for (x = 3; x < 10; x++) {
            if (x == 4) continue;
            c->put_hevc_qpel[x][1][0]         = ff_hevc_put_qpel_neon_wrapper;
            c->put_hevc_qpel[x][0][1]         = ff_hevc_put_qpel_neon_wrapper;
            c->put_hevc_qpel[x][1][1]         = ff_hevc_put_qpel_neon_wrapper;
            c->put_hevc_qpel_uni[x][1][0]     = ff_hevc_put_qpel_uni_neon_wrapper;
            c->put_hevc_qpel_uni[x][0][1]     = ff_hevc_put_qpel_uni_neon_wrapper;
            c->put_hevc_qpel_uni[x][1][1]     = ff_hevc_put_qpel_uni_neon_wrapper;
            c->put_hevc_qpel_bi[x][1][0]      = ff_hevc_put_qpel_bi_neon_wrapper;
            c->put_hevc_qpel_bi[x][0][1]      = ff_hevc_put_qpel_bi_neon_wrapper;
            c->put_hevc_qpel_bi[x][1][1]      = ff_hevc_put_qpel_bi_neon_wrapper;
        }
        c->put_hevc_qpel[0][0][0]  = ff_hevc_put_pixels_w2_neon_8;
        c->put_hevc_qpel[1][0][0]  = ff_hevc_put_pixels_w4_neon_8;
        c->put_hevc_qpel[2][0][0]  = ff_hevc_put_pixels_w6_neon_8;
        c->put_hevc_qpel[3][0][0]  = ff_hevc_put_pixels_w8_neon_8;
        c->put_hevc_qpel[4][0][0]  = ff_hevc_put_pixels_w12_neon_8;
        c->put_hevc_qpel[5][0][0]  = ff_hevc_put_pixels_w16_neon_8;
        c->put_hevc_qpel[6][0][0]  = ff_hevc_put_pixels_w24_neon_8;
        c->put_hevc_qpel[7][0][0]  = ff_hevc_put_pixels_w32_neon_8;
        c->put_hevc_qpel[8][0][0]  = ff_hevc_put_pixels_w48_neon_8;
        c->put_hevc_qpel[9][0][0]  = ff_hevc_put_pixels_w64_neon_8;

        c->put_hevc_qpel_uni[1][0][0]  = ff_hevc_put_qpel_uw_pixels_w4_neon_8;
        c->put_hevc_qpel_uni[3][0][0]  = ff_hevc_put_qpel_uw_pixels_w8_neon_8;
        c->put_hevc_qpel_uni[5][0][0]  = ff_hevc_put_qpel_uw_pixels_w16_neon_8;
        c->put_hevc_qpel_uni[6][0][0]  = ff_hevc_put_qpel_uw_pixels_w24_neon_8;
        c->put_hevc_qpel_uni[7][0][0]  = ff_hevc_put_qpel_uw_pixels_w32_neon_8;
        c->put_hevc_qpel_uni[8][0][0]  = ff_hevc_put_qpel_uw_pixels_w48_neon_8;
        c->put_hevc_qpel_uni[9][0][0]  = ff_hevc_put_qpel_uw_pixels_w64_neon_8;
    }

    if (bit_depth == 10) {
        c->add_residual[0] = ff_hevc_add_residual_4x4_10_neon;
        c->add_residual[1] = ff_hevc_add_residual_8x8_10_neon;
        c->add_residual[2] = ff_hevc_add_residual_16x16_10_neon;
        c->add_residual[3] = ff_hevc_add_residual_32x32_10_neon;

        c->idct_dc[0] = ff_hevc_idct_4x4_dc_10_neon;
        c->idct_dc[1] = ff_hevc_idct_8x8_dc_10_neon;
        c->idct_dc[2] = ff_hevc_idct_16x16_dc_10_neon;
        c->idct_dc[3] = ff_hevc_idct_32x32_dc_10_neon;

        c->idct[0] = ff_hevc_idct_4x4_10_neon;
        c->idct[1] = ff_hevc_idct_8x8_10_neon;
        c->idct[2] = ff_hevc_idct_16x16_10_neon;
        c->idct[3] = ff_hevc_idct_32x32_10_neon;
    }
}
