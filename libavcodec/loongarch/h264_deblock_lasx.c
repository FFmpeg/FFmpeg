/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Xiwei Gu <guxiwei-hf@loongson.cn>
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

#include "libavcodec/bit_depth_template.c"
#include "h264dsp_lasx.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

#define H264_LOOP_FILTER_STRENGTH_ITERATION_LASX(edges, step, mask_mv, dir, \
                                                 d_idx, mask_dir)           \
do {                                                                        \
    int b_idx = 0; \
    int step_x4 = step << 2; \
    int d_idx_12 = d_idx + 12; \
    int d_idx_52 = d_idx + 52; \
    int d_idx_x4 = d_idx << 2; \
    int d_idx_x4_48 = d_idx_x4 + 48; \
    int dir_x32  = dir * 32; \
    uint8_t *ref_t = (uint8_t*)ref; \
    uint8_t *mv_t  = (uint8_t*)mv; \
    uint8_t *nnz_t = (uint8_t*)nnz; \
    uint8_t *bS_t  = (uint8_t*)bS; \
    mask_mv <<= 3; \
    for (; b_idx < edges; b_idx += step) { \
        out &= mask_dir; \
        if (!(mask_mv & b_idx)) { \
            if (bidir) { \
                ref2 = __lasx_xvldx(ref_t, d_idx_12); \
                ref3 = __lasx_xvldx(ref_t, d_idx_52); \
                ref0 = __lasx_xvld(ref_t, 12); \
                ref1 = __lasx_xvld(ref_t, 52); \
                ref2 = __lasx_xvilvl_w(ref3, ref2); \
                ref0 = __lasx_xvilvl_w(ref0, ref0); \
                ref1 = __lasx_xvilvl_w(ref1, ref1); \
                ref3 = __lasx_xvshuf4i_w(ref2, 0xB1); \
                ref0 = __lasx_xvsub_b(ref0, ref2); \
                ref1 = __lasx_xvsub_b(ref1, ref3); \
                ref0 = __lasx_xvor_v(ref0, ref1); \
\
                tmp2 = __lasx_xvldx(mv_t, d_idx_x4_48);   \
                tmp3 = __lasx_xvld(mv_t, 48); \
                tmp4 = __lasx_xvld(mv_t, 208); \
                tmp5 = __lasx_xvld(mv_t + d_idx_x4, 208); \
                DUP2_ARG3(__lasx_xvpermi_q, tmp2, tmp2, 0x20, tmp5, tmp5, \
                          0x20, tmp2, tmp5); \
                tmp3 =  __lasx_xvpermi_q(tmp4, tmp3, 0x20); \
                tmp2 = __lasx_xvsub_h(tmp2, tmp3); \
                tmp5 = __lasx_xvsub_h(tmp5, tmp3); \
                DUP2_ARG2(__lasx_xvsat_h, tmp2, 7, tmp5, 7, tmp2, tmp5); \
                tmp0 = __lasx_xvpickev_b(tmp5, tmp2); \
                tmp0 = __lasx_xvpermi_d(tmp0, 0xd8); \
                tmp0 = __lasx_xvadd_b(tmp0, cnst_1); \
                tmp0 = __lasx_xvssub_bu(tmp0, cnst_0); \
                tmp0 = __lasx_xvsat_h(tmp0, 7); \
                tmp0 = __lasx_xvpickev_b(tmp0, tmp0); \
                tmp0 = __lasx_xvpermi_d(tmp0, 0xd8); \
                tmp1 = __lasx_xvpickod_d(tmp0, tmp0); \
                out = __lasx_xvor_v(ref0, tmp0); \
                tmp1 = __lasx_xvshuf4i_w(tmp1, 0xB1); \
                out = __lasx_xvor_v(out, tmp1); \
                tmp0 = __lasx_xvshuf4i_w(out, 0xB1); \
                out = __lasx_xvmin_bu(out, tmp0); \
            } else { \
                ref0 = __lasx_xvldx(ref_t, d_idx_12); \
                ref3 = __lasx_xvld(ref_t, 12); \
                tmp2 = __lasx_xvldx(mv_t, d_idx_x4_48); \
                tmp3 = __lasx_xvld(mv_t, 48); \
                tmp4 = __lasx_xvsub_h(tmp3, tmp2); \
                tmp1 = __lasx_xvsat_h(tmp4, 7); \
                tmp1 = __lasx_xvpickev_b(tmp1, tmp1); \
                tmp1 = __lasx_xvadd_b(tmp1, cnst_1); \
                out = __lasx_xvssub_bu(tmp1, cnst_0); \
                out = __lasx_xvsat_h(out, 7); \
                out = __lasx_xvpickev_b(out, out); \
                ref0 = __lasx_xvsub_b(ref3, ref0); \
                out = __lasx_xvor_v(out, ref0); \
            } \
        } \
        tmp0 = __lasx_xvld(nnz_t, 12); \
        tmp1 = __lasx_xvldx(nnz_t, d_idx_12); \
        tmp0 = __lasx_xvor_v(tmp0, tmp1); \
        tmp0 = __lasx_xvmin_bu(tmp0, cnst_2); \
        out  = __lasx_xvmin_bu(out, cnst_2); \
        tmp0 = __lasx_xvslli_h(tmp0, 1); \
        tmp0 = __lasx_xvmax_bu(out, tmp0); \
        tmp0 = __lasx_vext2xv_hu_bu(tmp0); \
        __lasx_xvstelm_d(tmp0, bS_t + dir_x32, 0, 0); \
        ref_t += step; \
        mv_t  += step_x4; \
        nnz_t += step; \
        bS_t  += step; \
    } \
} while(0)

void ff_h264_loop_filter_strength_lasx(int16_t bS[2][4][4], uint8_t nnz[40],
                                       int8_t ref[2][40], int16_t mv[2][40][2],
                                       int bidir, int edges, int step,
                                       int mask_mv0, int mask_mv1, int field)
{
    __m256i out;
    __m256i ref0, ref1, ref2, ref3;
    __m256i tmp0, tmp1;
    __m256i tmp2, tmp3, tmp4, tmp5;
    __m256i cnst_0, cnst_1, cnst_2;
    __m256i zero = __lasx_xvldi(0);
    __m256i one  = __lasx_xvnor_v(zero, zero);
    int64_t cnst3 = 0x0206020602060206, cnst4 = 0x0103010301030103;
    if (field) {
        cnst_0 = __lasx_xvreplgr2vr_d(cnst3);
        cnst_1 = __lasx_xvreplgr2vr_d(cnst4);
        cnst_2 = __lasx_xvldi(0x01);
    } else {
        DUP2_ARG1(__lasx_xvldi, 0x06, 0x03, cnst_0, cnst_1);
        cnst_2 = __lasx_xvldi(0x01);
    }
    step  <<= 3;
    edges <<= 3;

    H264_LOOP_FILTER_STRENGTH_ITERATION_LASX(edges, step, mask_mv1,
                                             1, -8, zero);
    H264_LOOP_FILTER_STRENGTH_ITERATION_LASX(32, 8, mask_mv0, 0, -1, one);

    DUP2_ARG2(__lasx_xvld, (int8_t*)bS, 0, (int8_t*)bS, 16, tmp0, tmp1);
    DUP2_ARG2(__lasx_xvilvh_d, tmp0, tmp0, tmp1, tmp1, tmp2, tmp3);
    LASX_TRANSPOSE4x4_H(tmp0, tmp2, tmp1, tmp3, tmp2, tmp3, tmp4, tmp5);
    __lasx_xvstelm_d(tmp2, (int8_t*)bS, 0, 0);
    __lasx_xvstelm_d(tmp3, (int8_t*)bS + 8, 0, 0);
    __lasx_xvstelm_d(tmp4, (int8_t*)bS + 16, 0, 0);
    __lasx_xvstelm_d(tmp5, (int8_t*)bS + 24, 0, 0);
}
