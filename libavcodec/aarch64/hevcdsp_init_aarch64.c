/*
 * Copyright (c) 2020 Reimar Döffinger
 * Copyright (c) 2023 xu fulong <839789740@qq.com>
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevcdsp.h"

void ff_hevc_v_loop_filter_chroma_8_neon(uint8_t *_pix, ptrdiff_t _stride,
                                         const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_v_loop_filter_chroma_10_neon(uint8_t *_pix, ptrdiff_t _stride,
                                          const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_v_loop_filter_chroma_12_neon(uint8_t *_pix, ptrdiff_t _stride,
                                          const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_h_loop_filter_chroma_8_neon(uint8_t *_pix, ptrdiff_t _stride,
                                         const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_h_loop_filter_chroma_10_neon(uint8_t *_pix, ptrdiff_t _stride,
                                          const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_h_loop_filter_chroma_12_neon(uint8_t *_pix, ptrdiff_t _stride,
                                          const int *_tc, const uint8_t *_no_p, const uint8_t *_no_q);
void ff_hevc_add_residual_4x4_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                     ptrdiff_t stride);
void ff_hevc_add_residual_4x4_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_4x4_12_neon(uint8_t *_dst, const int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_8x8_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                     ptrdiff_t stride);
void ff_hevc_add_residual_8x8_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_8x8_12_neon(uint8_t *_dst, const int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_16x16_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                       ptrdiff_t stride);
void ff_hevc_add_residual_16x16_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_add_residual_16x16_12_neon(uint8_t *_dst, const int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_add_residual_32x32_8_neon(uint8_t *_dst, const int16_t *coeffs,
                                       ptrdiff_t stride);
void ff_hevc_add_residual_32x32_10_neon(uint8_t *_dst, const int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_add_residual_32x32_12_neon(uint8_t *_dst, const int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_idct_4x4_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_32x32_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_32x32_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_4x4_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_10_neon(int16_t *coeffs);
void ff_hevc_transform_luma_4x4_neon_8(int16_t *coeffs);
void ff_hevc_sao_band_filter_8x8_8_neon(uint8_t *_dst, const uint8_t *_src,
                                  ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                  const int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height);
void ff_hevc_sao_edge_filter_16x16_8_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride_dst,
                                          const int16_t *sao_offset_val, int eo, int width, int height);
void ff_hevc_sao_edge_filter_8x8_8_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride_dst,
                                        const int16_t *sao_offset_val, int eo, int width, int height);
void ff_hevc_put_hevc_qpel_h4_8_neon(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
                                     intptr_t mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_h6_8_neon(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
                                     intptr_t mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_h8_8_neon(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
                                     intptr_t mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_h12_8_neon(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
                                      intptr_t mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_h16_8_neon(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
                                      intptr_t mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_uni_h4_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                         ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,
                                         int width);
void ff_hevc_put_hevc_qpel_uni_h6_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                         ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,
                                         int width);
void ff_hevc_put_hevc_qpel_uni_h8_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                         ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,
                                         int width);
void ff_hevc_put_hevc_qpel_uni_h12_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                          ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t
                                          my, int width);
void ff_hevc_put_hevc_qpel_uni_h16_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                          ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t
                                          my, int width);
void ff_hevc_put_hevc_qpel_bi_h4_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
                                        mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_bi_h6_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
                                        mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_bi_h8_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
                                        mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_bi_h12_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                         ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
                                         mx, intptr_t my, int width);
void ff_hevc_put_hevc_qpel_bi_h16_8_neon(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
                                         ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
                                         mx, intptr_t my, int width);

#define NEON8_FNPROTO(fn, args, ext) \
    void ff_hevc_put_hevc_##fn##4_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##6_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##8_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##12_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##16_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##24_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##32_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##48_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##64_8_neon##ext args; \

#define NEON8_FNPROTO_PARTIAL_4(fn, args, ext) \
    void ff_hevc_put_hevc_##fn##4_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##8_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##16_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##64_8_neon##ext args; \

#define NEON8_FNPROTO_PARTIAL_5(fn, args, ext) \
    void ff_hevc_put_hevc_##fn##4_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##8_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##16_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##32_8_neon##ext args; \
    void ff_hevc_put_hevc_##fn##64_8_neon##ext args; \

NEON8_FNPROTO(pel_uni_pixels, (uint8_t *_dst, ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, intptr_t mx, intptr_t my, int width),);

NEON8_FNPROTO(pel_uni_w_pixels, (uint8_t *_dst, ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width),);

NEON8_FNPROTO(epel_uni_w_v, (uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width),);

NEON8_FNPROTO_PARTIAL_4(qpel_uni_w_v, (uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width),);

NEON8_FNPROTO(epel_h, (int16_t *dst,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, intptr_t mx, intptr_t my, int width), _i8mm);

NEON8_FNPROTO(epel_uni_w_h, (uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width), _i8mm);

NEON8_FNPROTO(qpel_h, (int16_t *dst,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, intptr_t mx, intptr_t my, int width), _i8mm);

NEON8_FNPROTO(qpel_uni_w_h, (uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width), _i8mm);

NEON8_FNPROTO(epel_uni_w_hv, (uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width), _i8mm);

NEON8_FNPROTO_PARTIAL_5(qpel_uni_w_hv, (uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width), _i8mm);

#define NEON8_FNASSIGN(member, v, h, fn, ext) \
        member[1][v][h] = ff_hevc_put_hevc_##fn##4_8_neon##ext;  \
        member[2][v][h] = ff_hevc_put_hevc_##fn##6_8_neon##ext;  \
        member[3][v][h] = ff_hevc_put_hevc_##fn##8_8_neon##ext;  \
        member[4][v][h] = ff_hevc_put_hevc_##fn##12_8_neon##ext; \
        member[5][v][h] = ff_hevc_put_hevc_##fn##16_8_neon##ext; \
        member[6][v][h] = ff_hevc_put_hevc_##fn##24_8_neon##ext; \
        member[7][v][h] = ff_hevc_put_hevc_##fn##32_8_neon##ext; \
        member[8][v][h] = ff_hevc_put_hevc_##fn##48_8_neon##ext; \
        member[9][v][h] = ff_hevc_put_hevc_##fn##64_8_neon##ext;

#define NEON8_FNASSIGN_PARTIAL_4(member, v, h, fn, ext) \
        member[1][v][h] = ff_hevc_put_hevc_##fn##4_8_neon##ext;  \
        member[3][v][h] = ff_hevc_put_hevc_##fn##8_8_neon##ext;  \
        member[5][v][h] = ff_hevc_put_hevc_##fn##16_8_neon##ext; \
        member[7][v][h] = ff_hevc_put_hevc_##fn##64_8_neon##ext; \
        member[8][v][h] = ff_hevc_put_hevc_##fn##64_8_neon##ext; \
        member[9][v][h] = ff_hevc_put_hevc_##fn##64_8_neon##ext;

#define NEON8_FNASSIGN_PARTIAL_5(member, v, h, fn, ext) \
        member[1][v][h] = ff_hevc_put_hevc_##fn##4_8_neon##ext;  \
        member[3][v][h] = ff_hevc_put_hevc_##fn##8_8_neon##ext;  \
        member[5][v][h] = ff_hevc_put_hevc_##fn##16_8_neon##ext; \
        member[7][v][h] = ff_hevc_put_hevc_##fn##32_8_neon##ext; \
        member[9][v][h] = ff_hevc_put_hevc_##fn##64_8_neon##ext;

av_cold void ff_hevc_dsp_init_aarch64(HEVCDSPContext *c, const int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();
    if (!have_neon(cpu_flags)) return;

    if (bit_depth == 8) {
        c->hevc_h_loop_filter_chroma   = ff_hevc_h_loop_filter_chroma_8_neon;
        c->hevc_v_loop_filter_chroma   = ff_hevc_v_loop_filter_chroma_8_neon;
        c->add_residual[0]             = ff_hevc_add_residual_4x4_8_neon;
        c->add_residual[1]             = ff_hevc_add_residual_8x8_8_neon;
        c->add_residual[2]             = ff_hevc_add_residual_16x16_8_neon;
        c->add_residual[3]             = ff_hevc_add_residual_32x32_8_neon;
        c->idct[0]                     = ff_hevc_idct_4x4_8_neon;
        c->idct[1]                     = ff_hevc_idct_8x8_8_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_8_neon;
        c->idct[3]                     = ff_hevc_idct_32x32_8_neon;
        c->idct_dc[0]                  = ff_hevc_idct_4x4_dc_8_neon;
        c->idct_dc[1]                  = ff_hevc_idct_8x8_dc_8_neon;
        c->idct_dc[2]                  = ff_hevc_idct_16x16_dc_8_neon;
        c->idct_dc[3]                  = ff_hevc_idct_32x32_dc_8_neon;
        c->transform_4x4_luma          = ff_hevc_transform_luma_4x4_neon_8;
        c->sao_band_filter[0]          =
        c->sao_band_filter[1]          =
        c->sao_band_filter[2]          =
        c->sao_band_filter[3]          =
        c->sao_band_filter[4]          = ff_hevc_sao_band_filter_8x8_8_neon;
        c->sao_edge_filter[0]          = ff_hevc_sao_edge_filter_8x8_8_neon;
        c->sao_edge_filter[1]          =
        c->sao_edge_filter[2]          =
        c->sao_edge_filter[3]          =
        c->sao_edge_filter[4]          = ff_hevc_sao_edge_filter_16x16_8_neon;
        c->put_hevc_qpel[1][0][1]      = ff_hevc_put_hevc_qpel_h4_8_neon;
        c->put_hevc_qpel[2][0][1]      = ff_hevc_put_hevc_qpel_h6_8_neon;
        c->put_hevc_qpel[3][0][1]      = ff_hevc_put_hevc_qpel_h8_8_neon;
        c->put_hevc_qpel[4][0][1]      =
        c->put_hevc_qpel[6][0][1]      = ff_hevc_put_hevc_qpel_h12_8_neon;
        c->put_hevc_qpel[5][0][1]      =
        c->put_hevc_qpel[7][0][1]      =
        c->put_hevc_qpel[8][0][1]      =
        c->put_hevc_qpel[9][0][1]      = ff_hevc_put_hevc_qpel_h16_8_neon;
        c->put_hevc_qpel_uni[1][0][1]  = ff_hevc_put_hevc_qpel_uni_h4_8_neon;
        c->put_hevc_qpel_uni[2][0][1]  = ff_hevc_put_hevc_qpel_uni_h6_8_neon;
        c->put_hevc_qpel_uni[3][0][1]  = ff_hevc_put_hevc_qpel_uni_h8_8_neon;
        c->put_hevc_qpel_uni[4][0][1]  =
        c->put_hevc_qpel_uni[6][0][1]  = ff_hevc_put_hevc_qpel_uni_h12_8_neon;
        c->put_hevc_qpel_uni[5][0][1]  =
        c->put_hevc_qpel_uni[7][0][1]  =
        c->put_hevc_qpel_uni[8][0][1]  =
        c->put_hevc_qpel_uni[9][0][1]  = ff_hevc_put_hevc_qpel_uni_h16_8_neon;
        c->put_hevc_qpel_bi[1][0][1]   = ff_hevc_put_hevc_qpel_bi_h4_8_neon;
        c->put_hevc_qpel_bi[2][0][1]   = ff_hevc_put_hevc_qpel_bi_h6_8_neon;
        c->put_hevc_qpel_bi[3][0][1]   = ff_hevc_put_hevc_qpel_bi_h8_8_neon;
        c->put_hevc_qpel_bi[4][0][1]   =
        c->put_hevc_qpel_bi[6][0][1]   = ff_hevc_put_hevc_qpel_bi_h12_8_neon;
        c->put_hevc_qpel_bi[5][0][1]   =
        c->put_hevc_qpel_bi[7][0][1]   =
        c->put_hevc_qpel_bi[8][0][1]   =
        c->put_hevc_qpel_bi[9][0][1]   = ff_hevc_put_hevc_qpel_bi_h16_8_neon;

        NEON8_FNASSIGN(c->put_hevc_epel_uni, 0, 0, pel_uni_pixels,);
        NEON8_FNASSIGN(c->put_hevc_qpel_uni, 0, 0, pel_uni_pixels,);
        NEON8_FNASSIGN(c->put_hevc_epel_uni_w, 0, 0, pel_uni_w_pixels,);
        NEON8_FNASSIGN(c->put_hevc_qpel_uni_w, 0, 0, pel_uni_w_pixels,);
        NEON8_FNASSIGN(c->put_hevc_epel_uni_w, 1, 0, epel_uni_w_v,);
        NEON8_FNASSIGN_PARTIAL_4(c->put_hevc_qpel_uni_w, 1, 0, qpel_uni_w_v,);

        if (have_i8mm(cpu_flags)) {
            NEON8_FNASSIGN(c->put_hevc_epel, 0, 1, epel_h, _i8mm);
            NEON8_FNASSIGN(c->put_hevc_epel_uni_w, 0, 1, epel_uni_w_h ,_i8mm);
            NEON8_FNASSIGN(c->put_hevc_qpel, 0, 1, qpel_h, _i8mm);
            NEON8_FNASSIGN(c->put_hevc_qpel_uni_w, 0, 1, qpel_uni_w_h, _i8mm);
            NEON8_FNASSIGN(c->put_hevc_epel_uni_w, 1, 1, epel_uni_w_hv, _i8mm);
            NEON8_FNASSIGN_PARTIAL_5(c->put_hevc_qpel_uni_w, 1, 1, qpel_uni_w_hv, _i8mm);
        }

    }
    if (bit_depth == 10) {
        c->hevc_h_loop_filter_chroma   = ff_hevc_h_loop_filter_chroma_10_neon;
        c->hevc_v_loop_filter_chroma   = ff_hevc_v_loop_filter_chroma_10_neon;
        c->add_residual[0]             = ff_hevc_add_residual_4x4_10_neon;
        c->add_residual[1]             = ff_hevc_add_residual_8x8_10_neon;
        c->add_residual[2]             = ff_hevc_add_residual_16x16_10_neon;
        c->add_residual[3]             = ff_hevc_add_residual_32x32_10_neon;
        c->idct[0]                     = ff_hevc_idct_4x4_10_neon;
        c->idct[1]                     = ff_hevc_idct_8x8_10_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_10_neon;
        c->idct[3]                     = ff_hevc_idct_32x32_10_neon;
        c->idct_dc[0]                  = ff_hevc_idct_4x4_dc_10_neon;
        c->idct_dc[1]                  = ff_hevc_idct_8x8_dc_10_neon;
        c->idct_dc[2]                  = ff_hevc_idct_16x16_dc_10_neon;
        c->idct_dc[3]                  = ff_hevc_idct_32x32_dc_10_neon;
    }
    if (bit_depth == 12) {
        c->hevc_h_loop_filter_chroma   = ff_hevc_h_loop_filter_chroma_12_neon;
        c->hevc_v_loop_filter_chroma   = ff_hevc_v_loop_filter_chroma_12_neon;
        c->add_residual[0]             = ff_hevc_add_residual_4x4_12_neon;
        c->add_residual[1]             = ff_hevc_add_residual_8x8_12_neon;
        c->add_residual[2]             = ff_hevc_add_residual_16x16_12_neon;
        c->add_residual[3]             = ff_hevc_add_residual_32x32_12_neon;
    }
}
