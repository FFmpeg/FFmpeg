/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "libavutil/mips/generic_macros_msa.h"
#include "h263dsp_mips.h"

static const uint8_t h263_loop_filter_strength_msa[32] = {
    0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7,
    7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12
};

static void h263_h_loop_filter_msa(uint8_t *src, int32_t stride, int32_t qscale)
{
    int32_t strength = h263_loop_filter_strength_msa[qscale];
    v16u8 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 temp0, temp1, temp2;
    v8i16 diff0, diff2, diff4, diff6, diff8;
    v8i16 d0, a_d0, str_x2, str;

    src -= 2;
    LD_UB8(src, stride, in0, in1, in2, in3, in4, in5, in6, in7);
    TRANSPOSE8x4_UB_UB(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in3, in2, in1);

    temp0 = (v8i16) __msa_ilvr_b((v16i8) in0, (v16i8) in1);
    a_d0 = __msa_hsub_u_h((v16u8) temp0, (v16u8) temp0);
    temp2 = (v8i16) __msa_ilvr_b((v16i8) in2, (v16i8) in3);
    temp2 = __msa_hsub_u_h((v16u8) temp2, (v16u8) temp2);
    temp2 <<= 2;
    diff0 = a_d0 + temp2;
    diff2 = -(-diff0 >> 3);
    str_x2 = __msa_fill_h(-(strength << 1));
    temp0 = (str_x2 <= diff2);
    diff2 = (v8i16) __msa_bmz_v((v16u8) diff2, (v16u8) temp0, (v16u8) temp0);
    temp2 = str_x2 - diff2;
    str = __msa_fill_h(-strength);
    temp0 = (diff2 < str);
    diff2 = (v8i16) __msa_bmnz_v((v16u8) diff2, (v16u8) temp2, (v16u8) temp0);
    diff4 = diff0 >> 3;
    str_x2 = __msa_fill_h(strength << 1);
    temp0 = (diff4 <= str_x2);
    diff4 = (v8i16) __msa_bmz_v((v16u8) diff4, (v16u8) temp0, (v16u8) temp0);
    temp2 = str_x2 - diff4;
    str = __msa_fill_h(strength);
    temp0 = (str < diff4);
    diff4 = (v8i16) __msa_bmnz_v((v16u8) diff4, (v16u8) temp2, (v16u8) temp0);
    temp0 = __msa_clti_s_h(diff0, 0);
    d0 = (v8i16) __msa_bmnz_v((v16u8) diff4, (v16u8) diff2, (v16u8) temp0);
    diff2 = -diff2 >> 1;
    diff4 >>= 1;
    diff8 = (v8i16) __msa_bmnz_v((v16u8) diff4, (v16u8) diff2, (v16u8) temp0);
    diff6 = (-a_d0) >> 2;
    diff6 = -(diff6);
    temp2 = -diff8;
    temp0 = (diff6 < temp2);
    diff6 = (v8i16) __msa_bmnz_v((v16u8) diff6, (v16u8) temp2, (v16u8) temp0);
    diff2 = a_d0 >> 2;
    temp0 = (diff2 <= diff8);
    diff2 = (v8i16) __msa_bmz_v((v16u8) diff2, (v16u8) diff8, (v16u8) temp0);
    temp0 = __msa_clti_s_h(a_d0, 0);
    diff6 = (v8i16) __msa_bmz_v((v16u8) diff6, (v16u8) diff2, (v16u8) temp0);
    PCKEV_B2_SH(a_d0, diff6, a_d0, d0, diff6, d0);
    in0 = (v16u8) ((v16i8) in0 - (v16i8) diff6);
    in1 = (v16u8) ((v16i8) in1 + (v16i8) diff6);
    in3 = __msa_xori_b(in3, 128);
    in3 = (v16u8) __msa_adds_s_b((v16i8) in3, (v16i8) d0);
    in3 = __msa_xori_b(in3, 128);
    in2 = __msa_subsus_u_b(in2, (v16i8) d0);
    ILVR_B2_SH(in3, in0, in1, in2, temp0, temp1);
    in0 = (v16u8) __msa_ilvr_h(temp1, temp0);
    in3 = (v16u8) __msa_ilvl_h(temp1, temp0);
    ST_W8(in0, in3, 0, 1, 2, 3, 0, 1, 2, 3, src, stride);
}

static void h263_v_loop_filter_msa(uint8_t *src, int32_t stride, int32_t qscale)
{
    int32_t strength = h263_loop_filter_strength_msa[qscale];
    uint64_t res0, res1, res2, res3;
    v16u8 in0, in1, in2, in3;
    v8i16 temp0, temp2, diff0, diff2, diff4, diff6, diff8;
    v8i16 d0, a_d0, str_x2, str;

    src -= 2 * stride;
    LD_UB4(src, stride, in0, in3, in2, in1);
    temp0 = (v8i16) __msa_ilvr_b((v16i8) in0, (v16i8) in1);
    a_d0 = __msa_hsub_u_h((v16u8) temp0, (v16u8) temp0);
    temp2 = (v8i16) __msa_ilvr_b((v16i8) in2, (v16i8) in3);
    temp2 = __msa_hsub_u_h((v16u8) temp2, (v16u8) temp2);
    temp2 <<= 2;
    diff0 = a_d0 + temp2;
    diff2 = -(-diff0 >> 3);
    str_x2 = __msa_fill_h(-(strength << 1));
    temp0 = (str_x2 <= diff2);
    diff2 = (v8i16) __msa_bmz_v((v16u8) diff2, (v16u8) temp0, (v16u8) temp0);
    temp2 = str_x2 - diff2;
    str = __msa_fill_h(-strength);
    temp0 = (diff2 < str);
    diff2 = (v8i16) __msa_bmnz_v((v16u8) diff2, (v16u8) temp2, (v16u8) temp0);
    diff4 = diff0 >> 3;
    str_x2 = __msa_fill_h(strength << 1);
    temp0 = (diff4 <= str_x2);
    diff4 = (v8i16) __msa_bmz_v((v16u8) diff4, (v16u8) temp0, (v16u8) temp0);
    temp2 = str_x2 - diff4;
    str = __msa_fill_h(strength);
    temp0 = (str < diff4);
    diff4 = (v8i16) __msa_bmnz_v((v16u8) diff4, (v16u8) temp2, (v16u8) temp0);
    temp0 = __msa_clti_s_h(diff0, 0);
    d0 = (v8i16) __msa_bmnz_v((v16u8) diff4, (v16u8) diff2, (v16u8) temp0);
    diff2 = -diff2 >> 1;
    diff4 >>= 1;
    diff8 = (v8i16) __msa_bmnz_v((v16u8) diff4, (v16u8) diff2, (v16u8) temp0);
    diff6 = (-a_d0) >> 2;
    diff6 = -(diff6);
    temp2 = -diff8;
    temp0 = (diff6 < temp2);
    diff6 = (v8i16) __msa_bmnz_v((v16u8) diff6, (v16u8) temp2, (v16u8) temp0);
    diff2 = a_d0 >> 2;
    temp0 = (diff2 <= diff8);
    diff2 = (v8i16) __msa_bmz_v((v16u8) diff2, (v16u8) diff8, (v16u8) temp0);
    temp0 = __msa_clti_s_h(a_d0, 0);
    diff6 = (v8i16) __msa_bmz_v((v16u8) diff6, (v16u8) diff2, (v16u8) temp0);
    PCKEV_B2_SH(a_d0, diff6, a_d0, d0, diff6, d0);
    in0 = (v16u8) ((v16i8) in0 - (v16i8) diff6);
    in1 = (v16u8) ((v16i8) in1 + (v16i8) diff6);
    in3 = __msa_xori_b(in3, 128);
    in3 = (v16u8) __msa_adds_s_b((v16i8) in3, (v16i8) d0);
    in3 = __msa_xori_b(in3, 128);
    in2 = __msa_subsus_u_b(in2, (v16i8) d0);
    res0 = __msa_copy_u_d((v2i64) in0, 0);
    res1 = __msa_copy_u_d((v2i64) in3, 0);
    res2 = __msa_copy_u_d((v2i64) in2, 0);
    res3 = __msa_copy_u_d((v2i64) in1, 0);
    SD4(res0, res1, res2, res3, src, stride);
}

void ff_h263_h_loop_filter_msa(uint8_t *src, int32_t stride, int32_t q_scale)
{
    h263_h_loop_filter_msa(src, stride, q_scale);
}

void ff_h263_v_loop_filter_msa(uint8_t *src, int32_t stride, int32_t q_scale)
{
    h263_v_loop_filter_msa(src, stride, q_scale);
}
