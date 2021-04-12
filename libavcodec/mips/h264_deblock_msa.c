/*
 * MIPS SIMD optimized H.264 deblocking code
 *
 * Copyright (c) 2020 Loongson Technology Corporation Limited
 *                    Gu Xiwei <guxiwei-hf@loongson.cn>
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
#include "h264dsp_mips.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "libavcodec/mips/h264dsp_mips.h"

#define h264_loop_filter_strength_iteration_msa(edges, step, mask_mv, dir, \
                                                d_idx, mask_dir)           \
do {                                                                       \
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
                ref_2 = LD_SB(ref_t + d_idx_12); \
                ref_3 = LD_SB(ref_t + d_idx_52); \
                ref_0 = LD_SB(ref_t + 12); \
                ref_1 = LD_SB(ref_t + 52); \
                ref_2 = (v16i8)__msa_ilvr_w((v4i32)ref_3, (v4i32)ref_2); \
                ref_0 = (v16i8)__msa_ilvr_w((v4i32)ref_0, (v4i32)ref_0); \
                ref_1 = (v16i8)__msa_ilvr_w((v4i32)ref_1, (v4i32)ref_1); \
                ref_3 = (v16i8)__msa_shf_h((v8i16)ref_2, 0x4e); \
                ref_0 -= ref_2; \
                ref_1 -= ref_3; \
                ref_0 = (v16i8)__msa_or_v((v16u8)ref_0, (v16u8)ref_1); \
\
                tmp_2 = LD_SH(mv_t + d_idx_x4_48);   \
                tmp_3 = LD_SH(mv_t + 48); \
                tmp_4 = LD_SH(mv_t + 208); \
                tmp_5 = tmp_2 - tmp_3; \
                tmp_6 = tmp_2 - tmp_4; \
                SAT_SH2_SH(tmp_5, tmp_6, 7); \
                tmp_0 = __msa_pckev_b((v16i8)tmp_6, (v16i8)tmp_5); \
                tmp_0 += cnst_1; \
                tmp_0 = (v16i8)__msa_subs_u_b((v16u8)tmp_0, (v16u8)cnst_0);\
                tmp_0 = (v16i8)__msa_sat_s_h((v8i16)tmp_0, 7); \
                tmp_0 = __msa_pckev_b(tmp_0, tmp_0); \
                out   = (v16i8)__msa_or_v((v16u8)ref_0, (v16u8)tmp_0); \
\
                tmp_2 = LD_SH(mv_t + 208 + d_idx_x4); \
                tmp_5 = tmp_2 - tmp_3; \
                tmp_6 = tmp_2 - tmp_4; \
                SAT_SH2_SH(tmp_5, tmp_6, 7); \
                tmp_1 = __msa_pckev_b((v16i8)tmp_6, (v16i8)tmp_5); \
                tmp_1 += cnst_1; \
                tmp_1 = (v16i8)__msa_subs_u_b((v16u8)tmp_1, (v16u8)cnst_0); \
                tmp_1 = (v16i8)__msa_sat_s_h((v8i16)tmp_1, 7); \
                tmp_1 = __msa_pckev_b(tmp_1, tmp_1); \
\
                tmp_1 = (v16i8)__msa_shf_h((v8i16)tmp_1, 0x4e); \
                out   = (v16i8)__msa_or_v((v16u8)out, (v16u8)tmp_1); \
                tmp_0 = (v16i8)__msa_shf_h((v8i16)out, 0x4e); \
                out   = (v16i8)__msa_min_u_b((v16u8)out, (v16u8)tmp_0); \
            } else { \
                ref_0 = LD_SB(ref_t + d_idx_12); \
                ref_3 = LD_SB(ref_t + 12); \
                tmp_2 = LD_SH(mv_t + d_idx_x4_48); \
                tmp_3 = LD_SH(mv_t + 48); \
                tmp_4 = tmp_3 - tmp_2; \
                tmp_1 = (v16i8)__msa_sat_s_h(tmp_4, 7); \
                tmp_1 = __msa_pckev_b(tmp_1, tmp_1); \
                tmp_1 += cnst_1; \
                out   = (v16i8)__msa_subs_u_b((v16u8)tmp_1, (v16u8)cnst_0); \
                out   = (v16i8)__msa_sat_s_h((v8i16)out, 7); \
                out   = __msa_pckev_b(out, out); \
                ref_0 = ref_3 - ref_0; \
                out   = (v16i8)__msa_or_v((v16u8)out, (v16u8)ref_0); \
            } \
        } \
        tmp_0 = LD_SB(nnz_t + 12); \
        tmp_1 = LD_SB(nnz_t + d_idx_12); \
        tmp_0 = (v16i8)__msa_or_v((v16u8)tmp_0, (v16u8)tmp_1); \
        tmp_0 = (v16i8)__msa_min_u_b((v16u8)tmp_0, (v16u8)cnst_2); \
        out   = (v16i8)__msa_min_u_b((v16u8)out, (v16u8)cnst_2); \
        tmp_0 = (v16i8)((v8i16)tmp_0 << 1); \
        tmp_0 = (v16i8)__msa_max_u_b((v16u8)out, (v16u8)tmp_0); \
        tmp_0 = __msa_ilvr_b(zero, tmp_0); \
        ST_D1(tmp_0, 0, bS_t + dir_x32); \
        ref_t += step; \
        mv_t  += step_x4; \
        nnz_t += step; \
        bS_t  += step; \
    } \
} while(0)

void ff_h264_loop_filter_strength_msa(int16_t bS[2][4][4], uint8_t nnz[40],
                                      int8_t ref[2][40], int16_t mv[2][40][2],
                                      int bidir, int edges, int step,
                                      int mask_mv0, int mask_mv1, int field)
{
    v16i8 out;
    v16i8 ref_0, ref_1, ref_2, ref_3;
    v16i8 tmp_0, tmp_1;
    v8i16 tmp_2, tmp_3, tmp_4, tmp_5, tmp_6;
    v16i8 cnst_0, cnst_1, cnst_2;
    v16i8 zero = { 0 };
    v16i8 one  = __msa_fill_b(0xff);
    if (field) {
        cnst_0 = (v16i8)__msa_fill_h(0x206);
        cnst_1 = (v16i8)__msa_fill_h(0x103);
        cnst_2 = (v16i8)__msa_fill_h(0x101);
    } else {
        cnst_0 = __msa_fill_b(0x6);
        cnst_1 = __msa_fill_b(0x3);
        cnst_2 = __msa_fill_b(0x1);
    }
    step  <<= 3;
    edges <<= 3;

    h264_loop_filter_strength_iteration_msa(edges, step, mask_mv1, 1, -8, zero);
    h264_loop_filter_strength_iteration_msa(32, 8, mask_mv0, 0, -1, one);

    LD_SB2((int8_t*)bS, 16, tmp_0, tmp_1);
    tmp_2 = (v8i16)__msa_ilvl_d((v2i64)tmp_0, (v2i64)tmp_0);
    tmp_3 = (v8i16)__msa_ilvl_d((v2i64)tmp_1, (v2i64)tmp_1);
    TRANSPOSE4x4_SH_SH(tmp_0, tmp_2, tmp_1, tmp_3, tmp_2, tmp_3, tmp_4, tmp_5);
    tmp_0 = (v16i8)__msa_ilvr_d((v2i64)tmp_3, (v2i64)tmp_2);
    tmp_1 = (v16i8)__msa_ilvr_d((v2i64)tmp_5, (v2i64)tmp_4);
    ST_SB2(tmp_0, tmp_1, (int8_t*)bS, 16);
}
