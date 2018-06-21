/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
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
#include "idctdsp_mips.h"

static void simple_idct_msa(int16_t *block)
{
    int32_t const_val;
    v8i16 weights = { 0, 22725, 21407, 19266, 16383, 12873, 8867, 4520 };
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 w1, w3, w5, w7;
    v8i16 const0, const1, const2, const3, const4, const5, const6, const7;
    v4i32 temp0_r, temp1_r, temp2_r, temp3_r;
    v4i32 temp0_l, temp1_l, temp2_l, temp3_l;
    v4i32 a0_r, a1_r, a2_r, a3_r, a0_l, a1_l, a2_l, a3_l;
    v4i32 b0_r, b1_r, b2_r, b3_r, b0_l, b1_l, b2_l, b3_l;
    v4i32 w2, w4, w6;
    v8i16 select_vec, temp;
    v8i16 zero = { 0 };
    v4i32 const_val0 = __msa_ldi_w(1);
    v4i32 const_val1 = __msa_ldi_w(1);

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    const_val0 <<= 10;
    const_val = 16383 * ((1 << 19) / 16383);
    const_val1 = __msa_insert_w(const_val0, 0, const_val);
    const_val1 = __msa_splati_w(const_val1, 0);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    select_vec = in1 | in2 | in3 | in4 | in5 | in6 | in7;
    select_vec = __msa_clti_u_h((v8u16) select_vec, 1);
    UNPCK_SH_SW(in0, a0_r, a0_l);
    UNPCK_SH_SW(in2, temp3_r, temp3_l);
    temp = in0 << 3;
    w2 = (v4i32) __msa_splati_h(weights, 2);
    w2 = (v4i32) __msa_ilvr_h(zero, (v8i16) w2);
    w4 = (v4i32) __msa_splati_h(weights, 4);
    w4 = (v4i32) __msa_ilvr_h(zero, (v8i16) w4);
    w6 = (v4i32) __msa_splati_h(weights, 6);
    w6 = (v4i32) __msa_ilvr_h(zero, (v8i16) w6);
    MUL2(a0_r, w4, a0_l, w4, a0_r, a0_l);
    ADD2(a0_r, const_val0, a0_l, const_val0, temp0_r, temp0_l);
    MUL4(w2, temp3_r, w2, temp3_l, w6, temp3_r, w6, temp3_l,
         temp1_r, temp1_l, temp2_r, temp2_l);
    BUTTERFLY_8(temp0_r, temp0_l, temp0_r, temp0_l,
                temp2_l, temp2_r, temp1_l, temp1_r,
                a0_r, a0_l, a1_r, a1_l, a2_l, a2_r, a3_l, a3_r);
    UNPCK_SH_SW(in4, temp0_r, temp0_l);
    UNPCK_SH_SW(in6, temp3_r, temp3_l);
    MUL2(temp0_r, w4, temp0_l, w4, temp0_r, temp0_l);
    MUL4(w2, temp3_r, w2, temp3_l, w6, temp3_r, w6, temp3_l,
         temp2_r, temp2_l, temp1_r, temp1_l);
    ADD2(a0_r, temp0_r, a0_l, temp0_l, a0_r, a0_l);
    SUB4(a1_r, temp0_r, a1_l, temp0_l, a2_r, temp0_r, a2_l, temp0_l,
         a1_r, a1_l, a2_r, a2_l);
    ADD4(a3_r, temp0_r, a3_l, temp0_l, a0_r, temp1_r, a0_l, temp1_l,
         a3_r, a3_l, a0_r, a0_l);
    SUB2(a1_r, temp2_r, a1_l, temp2_l, a1_r, a1_l);
    ADD2(a2_r, temp2_r, a2_l, temp2_l, a2_r, a2_l);
    SUB2(a3_r, temp1_r, a3_l, temp1_l, a3_r, a3_l);
    ILVRL_H2_SW(in1, in3, b3_r, b3_l);
    SPLATI_H4_SH(weights, 1, 3, 5, 7, w1, w3, w5, w7);
    ILVRL_H2_SW(in5, in7, temp0_r, temp0_l);
    ILVR_H4_SH(w1, w3, w3, -w7, w5, -w1, w7, -w5,
               const0, const1, const2, const3);
    ILVR_H2_SH(w5, w7, w7, w3, const4, const6);
    const5 = __msa_ilvod_h(-w1, -w5);
    const7 = __msa_ilvod_h(w3, -w1);
    DOTP_SH4_SW(b3_r, b3_r, b3_r, b3_r, const0, const1, const2, const3,
                b0_r, b1_r, b2_r, b3_r);
    DPADD_SH4_SW(temp0_r, temp0_r, temp0_r, temp0_r,
                 const4, const5, const6, const7, b0_r, b1_r, b2_r, b3_r);
    DOTP_SH4_SW(b3_l, b3_l, b3_l, b3_l, const0, const1, const2, const3,
                b0_l, b1_l, b2_l, b3_l);
    DPADD_SH4_SW(temp0_l, temp0_l, temp0_l, temp0_l,
                 const4, const5, const6, const7, b0_l, b1_l, b2_l, b3_l);
    BUTTERFLY_16(a0_r, a0_l, a1_r, a1_l, a2_r, a2_l, a3_r, a3_l,
                 b3_l, b3_r, b2_l, b2_r, b1_l, b1_r, b0_l, b0_r,
                 temp0_r, temp0_l, temp1_r, temp1_l,
                 temp2_r, temp2_l, temp3_r, temp3_l,
                 a3_l, a3_r, a2_l, a2_r, a1_l, a1_r, a0_l, a0_r);
    SRA_4V(temp0_r, temp0_l, temp1_r, temp1_l, 11);
    SRA_4V(temp2_r, temp2_l, temp3_r, temp3_l, 11);
    PCKEV_H4_SW(temp0_l, temp0_r, temp1_l, temp1_r,
                temp2_l, temp2_r, temp3_l, temp3_r,
                temp0_r, temp1_r, temp2_r, temp3_r);
    in0 = (v8i16) __msa_bmnz_v((v16u8) temp0_r, (v16u8) temp,
                               (v16u8) select_vec);
    in1 = (v8i16) __msa_bmnz_v((v16u8) temp1_r, (v16u8) temp,
                               (v16u8) select_vec);
    in2 = (v8i16) __msa_bmnz_v((v16u8) temp2_r, (v16u8) temp,
                               (v16u8) select_vec);
    in3 = (v8i16) __msa_bmnz_v((v16u8) temp3_r, (v16u8) temp,
                               (v16u8) select_vec);
    SRA_4V(a3_r, a3_l, a2_r, a2_l, 11);
    SRA_4V(a1_r, a1_l, a0_r, a0_l, 11);
    PCKEV_H4_SW(a0_l, a0_r, a1_l, a1_r, a2_l, a2_r, a3_l, a3_r,
                a0_r, a1_r, a2_r, a3_r);
    in4 = (v8i16) __msa_bmnz_v((v16u8) a3_r, (v16u8) temp, (v16u8) select_vec);
    in5 = (v8i16) __msa_bmnz_v((v16u8) a2_r, (v16u8) temp, (v16u8) select_vec);
    in6 = (v8i16) __msa_bmnz_v((v16u8) a1_r, (v16u8) temp, (v16u8) select_vec);
    in7 = (v8i16) __msa_bmnz_v((v16u8) a0_r, (v16u8) temp, (v16u8) select_vec);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);

    UNPCK_SH_SW(in0, a0_r, a0_l);
    UNPCK_SH_SW(in2, temp3_r, temp3_l);
    w2 = (v4i32) __msa_splati_h(weights, 2);
    w2 = (v4i32) __msa_ilvr_h(zero, (v8i16) w2);
    w4 = (v4i32) __msa_splati_h(weights, 4);
    w4 = (v4i32) __msa_ilvr_h(zero, (v8i16) w4);
    w6 = (v4i32) __msa_splati_h(weights, 6);
    w6 = (v4i32) __msa_ilvr_h(zero, (v8i16) w6);
    MUL2(a0_r, w4, a0_l, w4, a0_r, a0_l);
    ADD2(a0_r, const_val1, a0_l, const_val1, temp0_r, temp0_l);
    MUL4(w2, temp3_r, w2, temp3_l, w6, temp3_r, w6, temp3_l,
         temp1_r, temp1_l, temp2_r, temp2_l);
    BUTTERFLY_8(temp0_r, temp0_l, temp0_r, temp0_l,
                temp2_l, temp2_r, temp1_l, temp1_r,
                a0_r, a0_l, a1_r, a1_l, a2_l, a2_r, a3_l, a3_r);
    UNPCK_SH_SW(in4, temp0_r, temp0_l);
    UNPCK_SH_SW(in6, temp3_r, temp3_l);
    MUL2(temp0_r, w4, temp0_l, w4, temp0_r, temp0_l);
    MUL4(w2, temp3_r, w2, temp3_l, w6, temp3_r, w6, temp3_l,
         temp2_r, temp2_l, temp1_r, temp1_l);
    ADD2(a0_r, temp0_r, a0_l, temp0_l, a0_r, a0_l);
    SUB4(a1_r, temp0_r, a1_l, temp0_l, a2_r, temp0_r, a2_l, temp0_l,
         a1_r, a1_l, a2_r, a2_l);
    ADD4(a3_r, temp0_r, a3_l, temp0_l, a0_r, temp1_r, a0_l, temp1_l,
         a3_r, a3_l, a0_r, a0_l);
    SUB2(a1_r, temp2_r, a1_l, temp2_l, a1_r, a1_l);
    ADD2(a2_r, temp2_r, a2_l, temp2_l, a2_r, a2_l);
    SUB2(a3_r, temp1_r, a3_l, temp1_l, a3_r, a3_l);
    ILVRL_H2_SW(in1, in3, b3_r, b3_l);
    SPLATI_H4_SH(weights, 1, 3, 5, 7, w1, w3, w5, w7);
    ILVR_H4_SH(w1, w3, w3, -w7, w5, -w1, w7, -w5,
               const0, const1, const2, const3);
    DOTP_SH4_SW(b3_r, b3_r, b3_r, b3_r, const0, const1, const2, const3,
                b0_r, b1_r, b2_r, b3_r);
    DOTP_SH4_SW(b3_l, b3_l, b3_l, b3_l, const0, const1, const2, const3,
                b0_l, b1_l, b2_l, b3_l);
    ILVRL_H2_SW(in5, in7, temp0_r, temp0_l);
    ILVR_H2_SH(w5, w7, w7, w3, const4, const6);
    const5 = __msa_ilvod_h(-w1, -w5);
    const7 = __msa_ilvod_h(w3, -w1);
    DPADD_SH4_SW(temp0_r, temp0_r, temp0_r, temp0_r,
                 const4, const5, const6, const7, b0_r, b1_r, b2_r, b3_r);
    DPADD_SH4_SW(temp0_l, temp0_l, temp0_l, temp0_l,
                 const4, const5, const6, const7, b0_l, b1_l, b2_l, b3_l);
    BUTTERFLY_16(a0_r, a0_l, a1_r, a1_l, a2_r, a2_l, a3_r, a3_l,
                 b3_l, b3_r, b2_l, b2_r, b1_l, b1_r, b0_l, b0_r,
                 temp0_r, temp0_l, temp1_r, temp1_l,
                 temp2_r, temp2_l, temp3_r, temp3_l,
                 a3_l, a3_r, a2_l, a2_r, a1_l, a1_r, a0_l, a0_r);
    SRA_4V(temp0_r, temp0_l, temp1_r, temp1_l, 20);
    SRA_4V(temp2_r, temp2_l, temp3_r, temp3_l, 20);
    PCKEV_H4_SW(temp0_l, temp0_r, temp1_l, temp1_r, temp2_l, temp2_r,
                temp3_l, temp3_r, temp0_r, temp1_r, temp2_r, temp3_r);
    SRA_4V(a3_r, a3_l, a2_r, a2_l, 20);
    SRA_4V(a1_r, a1_l, a0_r, a0_l, 20);
    PCKEV_H4_SW(a0_l, a0_r, a1_l, a1_r, a2_l, a2_r, a3_l, a3_r,
                a0_r, a1_r, a2_r, a3_r);
    ST_SW8(temp0_r, temp1_r, temp2_r, temp3_r, a3_r, a2_r, a1_r, a0_r,
           block, 8);
}

static void simple_idct_put_msa(uint8_t *dst, int32_t dst_stride,
                                int16_t *block)
{
    int32_t const_val;
    uint64_t tmp0, tmp1, tmp2, tmp3;
    v8i16 weights = { 0, 22725, 21407, 19266, 16383, 12873, 8867, 4520 };
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 w1, w3, w5, w7;
    v8i16 const0, const1, const2, const3, const4, const5, const6, const7;
    v4i32 temp0_r, temp1_r, temp2_r, temp3_r;
    v4i32 temp0_l, temp1_l, temp2_l, temp3_l;
    v4i32 a0_r, a1_r, a2_r, a3_r, a0_l, a1_l, a2_l, a3_l;
    v4i32 b0_r, b1_r, b2_r, b3_r, b0_l, b1_l, b2_l, b3_l;
    v4i32 w2, w4, w6;
    v8i16 select_vec, temp;
    v8i16 zero = { 0 };
    v4i32 const_val0 = __msa_ldi_w(1);
    v4i32 const_val1 = __msa_ldi_w(1);

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    const_val0 <<= 10;
    const_val = 16383 * ((1 << 19) / 16383);
    const_val1 = __msa_insert_w(const_val0, 0, const_val);
    const_val1 = __msa_splati_w(const_val1, 0);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    select_vec = in1 | in2 | in3 | in4 | in5 | in6 | in7;
    select_vec = __msa_clti_u_h((v8u16) select_vec, 1);
    UNPCK_SH_SW(in0, a0_r, a0_l);
    UNPCK_SH_SW(in2, temp3_r, temp3_l);
    temp = in0 << 3;
    w2 = (v4i32) __msa_splati_h(weights, 2);
    w2 = (v4i32) __msa_ilvr_h(zero, (v8i16) w2);
    w4 = (v4i32) __msa_splati_h(weights, 4);
    w4 = (v4i32) __msa_ilvr_h(zero, (v8i16) w4);
    w6 = (v4i32) __msa_splati_h(weights, 6);
    w6 = (v4i32) __msa_ilvr_h(zero, (v8i16) w6);
    MUL2(a0_r, w4, a0_l, w4, a0_r, a0_l);
    ADD2(a0_r, const_val0, a0_l, const_val0, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp1_r, temp1_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp2_r, temp2_l);
    BUTTERFLY_8(temp0_r, temp0_l, temp0_r, temp0_l,
                temp2_l, temp2_r, temp1_l, temp1_r,
                a0_r, a0_l, a1_r, a1_l, a2_l, a2_r, a3_l, a3_r);
    UNPCK_SH_SW(in4, temp0_r, temp0_l);
    UNPCK_SH_SW(in6, temp3_r, temp3_l);
    MUL2(temp0_r, w4, temp0_l, w4, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp2_r, temp2_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp1_r, temp1_l);
    ADD2(a0_r, temp0_r, a0_l, temp0_l, a0_r, a0_l);
    SUB2(a1_r, temp0_r, a1_l, temp0_l, a1_r, a1_l);
    SUB2(a2_r, temp0_r, a2_l, temp0_l, a2_r, a2_l);
    ADD2(a3_r, temp0_r, a3_l, temp0_l, a3_r, a3_l);
    ADD2(a0_r, temp1_r, a0_l, temp1_l, a0_r, a0_l);
    SUB2(a1_r, temp2_r, a1_l, temp2_l, a1_r, a1_l);
    ADD2(a2_r, temp2_r, a2_l, temp2_l, a2_r, a2_l);
    SUB2(a3_r, temp1_r, a3_l, temp1_l, a3_r, a3_l);
    ILVRL_H2_SW(in1, in3, b3_r, b3_l);
    SPLATI_H4_SH(weights, 1, 3, 5, 7, w1, w3, w5, w7);
    ILVRL_H2_SW(in5, in7, temp0_r, temp0_l);
    ILVR_H4_SH(w1, w3, w3, -w7, w5, -w1, w7, -w5,
               const0, const1, const2, const3);
    ILVR_H2_SH(w5, w7, w7, w3, const4, const6);
    const5 = __msa_ilvod_h(-w1, -w5);
    const7 = __msa_ilvod_h(w3, -w1);
    DOTP_SH4_SW(b3_r, b3_r, b3_r, b3_r, const0, const1, const2, const3,
                b0_r, b1_r, b2_r, b3_r);
    DPADD_SH4_SW(temp0_r, temp0_r, temp0_r, temp0_r,
                 const4, const5, const6, const7, b0_r, b1_r, b2_r, b3_r);
    DOTP_SH4_SW(b3_l, b3_l, b3_l, b3_l, const0, const1, const2, const3,
                b0_l, b1_l, b2_l, b3_l);
    DPADD_SH4_SW(temp0_l, temp0_l, temp0_l, temp0_l,
                 const4, const5, const6, const7, b0_l, b1_l, b2_l, b3_l);
    BUTTERFLY_16(a0_r, a0_l, a1_r, a1_l, a2_r, a2_l, a3_r, a3_l,
                 b3_l, b3_r, b2_l, b2_r, b1_l, b1_r, b0_l, b0_r,
                 temp0_r, temp0_l, temp1_r, temp1_l,
                 temp2_r, temp2_l, temp3_r, temp3_l,
                 a3_l, a3_r, a2_l, a2_r, a1_l, a1_r, a0_l, a0_r);
    SRA_4V(temp0_r, temp0_l, temp1_r, temp1_l, 11);
    SRA_4V(temp2_r, temp2_l, temp3_r, temp3_l, 11);
    PCKEV_H4_SW(temp0_l, temp0_r, temp1_l, temp1_r,
                temp2_l, temp2_r, temp3_l, temp3_r,
                temp0_r, temp1_r, temp2_r, temp3_r);
    in0 = (v8i16) __msa_bmnz_v((v16u8) temp0_r, (v16u8) temp,
                               (v16u8) select_vec);
    in1 = (v8i16) __msa_bmnz_v((v16u8) temp1_r, (v16u8) temp,
                               (v16u8) select_vec);
    in2 = (v8i16) __msa_bmnz_v((v16u8) temp2_r, (v16u8) temp,
                               (v16u8) select_vec);
    in3 = (v8i16) __msa_bmnz_v((v16u8) temp3_r, (v16u8) temp,
                               (v16u8) select_vec);
    SRA_4V(a3_r, a3_l, a2_r, a2_l, 11);
    SRA_4V(a1_r, a1_l, a0_r, a0_l, 11);
    PCKEV_H4_SW(a0_l, a0_r, a1_l, a1_r, a2_l, a2_r, a3_l, a3_r,
                a0_r, a1_r, a2_r, a3_r);
    in4 = (v8i16) __msa_bmnz_v((v16u8) a3_r, (v16u8) temp, (v16u8) select_vec);
    in5 = (v8i16) __msa_bmnz_v((v16u8) a2_r, (v16u8) temp, (v16u8) select_vec);
    in6 = (v8i16) __msa_bmnz_v((v16u8) a1_r, (v16u8) temp, (v16u8) select_vec);
    in7 = (v8i16) __msa_bmnz_v((v16u8) a0_r, (v16u8) temp, (v16u8) select_vec);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    UNPCK_SH_SW(in0, a0_r, a0_l);
    UNPCK_SH_SW(in2, temp3_r, temp3_l);
    w2 = (v4i32) __msa_splati_h(weights, 2);
    w2 = (v4i32) __msa_ilvr_h(zero, (v8i16) w2);
    w4 = (v4i32) __msa_splati_h(weights, 4);
    w4 = (v4i32) __msa_ilvr_h(zero, (v8i16) w4);
    w6 = (v4i32) __msa_splati_h(weights, 6);
    w6 = (v4i32) __msa_ilvr_h(zero, (v8i16) w6);
    MUL2(a0_r, w4, a0_l, w4, a0_r, a0_l);
    ADD2(a0_r, const_val1, a0_l, const_val1, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp1_r, temp1_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp2_r, temp2_l);
    BUTTERFLY_8(temp0_r, temp0_l, temp0_r, temp0_l,
                temp2_l, temp2_r, temp1_l, temp1_r,
                a0_r, a0_l, a1_r, a1_l, a2_l, a2_r, a3_l, a3_r);
    UNPCK_SH_SW(in4, temp0_r, temp0_l);
    UNPCK_SH_SW(in6, temp3_r, temp3_l);
    MUL2(temp0_r, w4, temp0_l, w4, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp2_r, temp2_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp1_r, temp1_l);
    ADD2(a0_r, temp0_r, a0_l, temp0_l, a0_r, a0_l);
    SUB2(a1_r, temp0_r, a1_l, temp0_l, a1_r, a1_l);
    SUB2(a2_r, temp0_r, a2_l, temp0_l, a2_r, a2_l);
    ADD2(a3_r, temp0_r, a3_l, temp0_l, a3_r, a3_l);
    ADD2(a0_r, temp1_r, a0_l, temp1_l, a0_r, a0_l);
    SUB2(a1_r, temp2_r, a1_l, temp2_l, a1_r, a1_l);
    ADD2(a2_r, temp2_r, a2_l, temp2_l, a2_r, a2_l);
    SUB2(a3_r, temp1_r, a3_l, temp1_l, a3_r, a3_l);
    ILVRL_H2_SW(in1, in3, b3_r, b3_l);
    SPLATI_H4_SH(weights, 1, 3, 5, 7, w1, w3, w5, w7);
    ILVR_H4_SH(w1, w3, w3, -w7, w5, -w1, w7, -w5,
               const0, const1, const2, const3);
    DOTP_SH4_SW(b3_r, b3_r, b3_r, b3_r, const0, const1, const2, const3,
                b0_r, b1_r, b2_r, b3_r);
    DOTP_SH4_SW(b3_l, b3_l, b3_l, b3_l, const0, const1, const2, const3,
                b0_l, b1_l, b2_l, b3_l);
    ILVRL_H2_SW(in5, in7, temp0_r, temp0_l);
    ILVR_H2_SH(w5, w7, w7, w3, const4, const6);
    const5 = __msa_ilvod_h(-w1, -w5);
    const7 = __msa_ilvod_h(w3, -w1);
    DPADD_SH4_SW(temp0_r, temp0_r, temp0_r, temp0_r,
                 const4, const5, const6, const7, b0_r, b1_r, b2_r, b3_r);
    DPADD_SH4_SW(temp0_l, temp0_l, temp0_l, temp0_l,
                 const4, const5, const6, const7, b0_l, b1_l, b2_l, b3_l);
    BUTTERFLY_16(a0_r, a0_l, a1_r, a1_l, a2_r, a2_l, a3_r, a3_l,
                 b3_l, b3_r, b2_l, b2_r, b1_l, b1_r, b0_l, b0_r,
                 temp0_r, temp0_l, temp1_r, temp1_l,
                 temp2_r, temp2_l, temp3_r, temp3_l,
                 a3_l, a3_r, a2_l, a2_r, a1_l, a1_r, a0_l, a0_r);
    SRA_4V(temp0_r, temp0_l, temp1_r, temp1_l, 20);
    SRA_4V(temp2_r, temp2_l, temp3_r, temp3_l, 20);
    SRA_4V(a3_r, a3_l, a2_r, a2_l, 20);
    SRA_4V(a1_r, a1_l, a0_r, a0_l, 20);
    PCKEV_H4_SW(temp0_l, temp0_r, temp1_l, temp1_r, temp2_l, temp2_r,
                temp3_l, temp3_r, temp0_r, temp1_r, temp2_r, temp3_r);
    PCKEV_H4_SW(a0_l, a0_r, a1_l, a1_r, a2_l, a2_r, a3_l, a3_r,
                a0_r, a1_r, a2_r, a3_r);
    temp0_r = (v4i32) CLIP_SH_0_255(temp0_r);
    temp1_r = (v4i32) CLIP_SH_0_255(temp1_r);
    temp2_r = (v4i32) CLIP_SH_0_255(temp2_r);
    temp3_r = (v4i32) CLIP_SH_0_255(temp3_r);
    PCKEV_B4_SW(temp0_r, temp0_r, temp1_r, temp1_r,
                temp2_r, temp2_r, temp3_r, temp3_r,
                temp0_r, temp1_r, temp2_r, temp3_r);
    tmp0 = __msa_copy_u_d((v2i64) temp0_r, 1);
    tmp1 = __msa_copy_u_d((v2i64) temp1_r, 1);
    tmp2 = __msa_copy_u_d((v2i64) temp2_r, 1);
    tmp3 = __msa_copy_u_d((v2i64) temp3_r, 1);
    SD4(tmp0, tmp1, tmp2, tmp3, dst, dst_stride);
    dst += 4 * dst_stride;
    a0_r = (v4i32) CLIP_SH_0_255(a0_r);
    a1_r = (v4i32) CLIP_SH_0_255(a1_r);
    a2_r = (v4i32) CLIP_SH_0_255(a2_r);
    a3_r = (v4i32) CLIP_SH_0_255(a3_r);
    PCKEV_B4_SW(a0_r, a0_r, a1_r, a1_r,
                a2_r, a2_r, a3_r, a3_r, a0_r, a1_r, a2_r, a3_r);
    tmp3 = __msa_copy_u_d((v2i64) a0_r, 1);
    tmp2 = __msa_copy_u_d((v2i64) a1_r, 1);
    tmp1 = __msa_copy_u_d((v2i64) a2_r, 1);
    tmp0 = __msa_copy_u_d((v2i64) a3_r, 1);
    SD4(tmp0, tmp1, tmp2, tmp3, dst, dst_stride);
    dst += 4 * dst_stride;
}

static void simple_idct_add_msa(uint8_t *dst, int32_t dst_stride,
                                int16_t *block)
{
    int32_t const_val;
    uint64_t tmp0, tmp1, tmp2, tmp3;
    v8i16 weights = { 0, 22725, 21407, 19266, 16383, 12873, 8867, 4520 };
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 w1, w3, w5, w7;
    v8i16 const0, const1, const2, const3, const4, const5, const6, const7;
    v4i32 temp0_r, temp1_r, temp2_r, temp3_r;
    v4i32 temp4_r, temp5_r, temp6_r, temp7_r, temp8_r;
    v4i32 temp0_l, temp1_l, temp2_l, temp3_l;
    v4i32 temp4_l, temp5_l, temp6_l, temp7_l, temp8_l;
    v4i32 a0_r, a1_r, a2_r, a3_r, a0_l, a1_l, a2_l, a3_l;
    v4i32 b0_r, b1_r, b2_r, b3_r, b0_l, b1_l, b2_l, b3_l;
    v4i32 w2, w4, w6;
    v8i16 select_vec, temp;
    v8i16 zero = { 0 };
    v4i32 const_val0 = __msa_ldi_w(1);
    v4i32 const_val1 = __msa_ldi_w(1);

    const_val0 <<= 10;
    const_val = 16383 * ((1 << 19) / 16383);
    const_val1 = __msa_insert_w(const_val0, 0, const_val);
    const_val1 = __msa_splati_w(const_val1, 0);
    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);

    select_vec = in1 | in2 | in3 | in4 | in5 | in6 | in7;
    select_vec = __msa_clti_u_h((v8u16) select_vec, 1);
    UNPCK_SH_SW(in0, a0_r, a0_l);
    UNPCK_SH_SW(in2, temp3_r, temp3_l);
    ILVRL_H2_SW(in1, in3, b3_r, b3_l);
    UNPCK_SH_SW(in4, temp4_r, temp4_l);
    UNPCK_SH_SW(in6, temp7_r, temp7_l);
    ILVRL_H2_SW(in5, in7, temp8_r, temp8_l);
    temp = in0 << 3;
    SPLATI_H4_SH(weights, 1, 3, 5, 7, w1, w3, w5, w7);
    ILVR_H4_SH(w1, w3, w3, -w7, w5, -w1, w7, -w5,
               const0, const1, const2, const3);
    ILVR_H2_SH(w5, w7, w7, w3, const4, const6);
    const5 = __msa_ilvod_h(-w1, -w5);
    const7 = __msa_ilvod_h(w3, -w1);
    DOTP_SH4_SW(b3_r, b3_r, b3_r, b3_r, const0, const1, const2, const3,
                b0_r, b1_r, b2_r, b3_r);
    DPADD_SH4_SW(temp8_r, temp8_r, temp8_r, temp8_r,
                 const4, const5, const6, const7, b0_r, b1_r, b2_r, b3_r);
    DOTP_SH4_SW(b3_l, b3_l, b3_l, b3_l, const0, const1, const2, const3,
                b0_l, b1_l, b2_l, b3_l);
    DPADD_SH4_SW(temp8_l, temp8_l, temp8_l, temp8_l,
                 const4, const5, const6, const7, b0_l, b1_l, b2_l, b3_l);
    w2 = (v4i32) __msa_splati_h(weights, 2);
    w2 = (v4i32) __msa_ilvr_h(zero, (v8i16) w2);
    w4 = (v4i32) __msa_splati_h(weights, 4);
    w4 = (v4i32) __msa_ilvr_h(zero, (v8i16) w4);
    w6 = (v4i32) __msa_splati_h(weights, 6);
    w6 = (v4i32) __msa_ilvr_h(zero, (v8i16) w6);
    MUL2(a0_r, w4, a0_l, w4, a0_r, a0_l);
    ADD2(a0_r, const_val0, a0_l, const_val0, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp1_r, temp1_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp2_r, temp2_l);
    BUTTERFLY_8(temp0_r, temp0_l, temp0_r, temp0_l,
                temp2_l, temp2_r, temp1_l, temp1_r,
                a0_r, a0_l, a1_r, a1_l, a2_l, a2_r, a3_l, a3_r);
    MUL2(temp4_r, w4, temp4_l, w4, temp4_r, temp4_l);
    MUL2(temp7_r, w2, temp7_l, w2, temp6_r, temp6_l);
    MUL2(temp7_r, w6, temp7_l, w6, temp5_r, temp5_l);
    ADD2(a0_r, temp4_r, a0_l, temp4_l, a0_r, a0_l);
    SUB2(a1_r, temp4_r, a1_l, temp4_l, a1_r, a1_l);
    SUB2(a2_r, temp4_r, a2_l, temp4_l, a2_r, a2_l);
    ADD2(a3_r, temp4_r, a3_l, temp4_l, a3_r, a3_l);
    ADD2(a0_r, temp5_r, a0_l, temp5_l, a0_r, a0_l);
    SUB2(a1_r, temp6_r, a1_l, temp6_l, a1_r, a1_l);
    ADD2(a2_r, temp6_r, a2_l, temp6_l, a2_r, a2_l);
    SUB2(a3_r, temp5_r, a3_l, temp5_l, a3_r, a3_l);
    BUTTERFLY_16(a0_r, a0_l, a1_r, a1_l, a2_r, a2_l, a3_r, a3_l,
                 b3_l, b3_r, b2_l, b2_r, b1_l, b1_r, b0_l, b0_r,
                 temp0_r, temp0_l, temp1_r, temp1_l,
                 temp2_r, temp2_l, temp3_r, temp3_l,
                 a3_l, a3_r, a2_l, a2_r, a1_l, a1_r, a0_l, a0_r);
    SRA_4V(temp0_r, temp0_l, temp1_r, temp1_l, 11);
    SRA_4V(temp2_r, temp2_l, temp3_r, temp3_l, 11);
    PCKEV_H4_SW(temp0_l, temp0_r, temp1_l, temp1_r,
                temp2_l, temp2_r, temp3_l, temp3_r,
                temp0_r, temp1_r, temp2_r, temp3_r);
    in0 = (v8i16) __msa_bmnz_v((v16u8) temp0_r, (v16u8) temp,
                               (v16u8) select_vec);
    in1 = (v8i16) __msa_bmnz_v((v16u8) temp1_r, (v16u8) temp,
                               (v16u8) select_vec);
    in2 = (v8i16) __msa_bmnz_v((v16u8) temp2_r, (v16u8) temp,
                               (v16u8) select_vec);
    in3 = (v8i16) __msa_bmnz_v((v16u8) temp3_r, (v16u8) temp,
                               (v16u8) select_vec);
    SRA_4V(a3_r, a3_l, a2_r, a2_l, 11);
    SRA_4V(a1_r, a1_l, a0_r, a0_l, 11);
    PCKEV_H4_SW(a0_l, a0_r, a1_l, a1_r, a2_l, a2_r, a3_l, a3_r,
                a0_r, a1_r, a2_r, a3_r);
    in4 = (v8i16) __msa_bmnz_v((v16u8) a3_r, (v16u8) temp, (v16u8) select_vec);
    in5 = (v8i16) __msa_bmnz_v((v16u8) a2_r, (v16u8) temp, (v16u8) select_vec);
    in6 = (v8i16) __msa_bmnz_v((v16u8) a1_r, (v16u8) temp, (v16u8) select_vec);
    in7 = (v8i16) __msa_bmnz_v((v16u8) a0_r, (v16u8) temp, (v16u8) select_vec);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);

    UNPCK_SH_SW(in0, a0_r, a0_l);
    UNPCK_SH_SW(in2, temp3_r, temp3_l);
    MUL2(a0_r, w4, a0_l, w4, a0_r, a0_l);
    ADD2(a0_r, const_val1, a0_l, const_val1, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp1_r, temp1_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp2_r, temp2_l);
    BUTTERFLY_8(temp0_r, temp0_l, temp0_r, temp0_l,
                temp2_l, temp2_r, temp1_l, temp1_r,
                a0_r, a0_l, a1_r, a1_l, a2_l, a2_r, a3_l, a3_r);
    UNPCK_SH_SW(in4, temp0_r, temp0_l);
    UNPCK_SH_SW(in6, temp3_r, temp3_l);
    MUL2(temp0_r, w4, temp0_l, w4, temp0_r, temp0_l);
    MUL2(w2, temp3_r, w2, temp3_l, temp2_r, temp2_l);
    MUL2(w6, temp3_r, w6, temp3_l, temp1_r, temp1_l);
    ADD2(a0_r, temp0_r, a0_l, temp0_l, a0_r, a0_l);
    SUB2(a1_r, temp0_r, a1_l, temp0_l, a1_r, a1_l);
    SUB2(a2_r, temp0_r, a2_l, temp0_l, a2_r, a2_l);
    ADD2(a3_r, temp0_r, a3_l, temp0_l, a3_r, a3_l);
    ADD2(a0_r, temp1_r, a0_l, temp1_l, a0_r, a0_l);
    SUB2(a1_r, temp2_r, a1_l, temp2_l, a1_r, a1_l);
    ADD2(a2_r, temp2_r, a2_l, temp2_l, a2_r, a2_l);
    SUB2(a3_r, temp1_r, a3_l, temp1_l, a3_r, a3_l);
    ILVRL_H2_SW(in1, in3, b3_r, b3_l);
    ILVRL_H2_SW(in5, in7, temp0_r, temp0_l);
    DOTP_SH4_SW(b3_r, b3_r, b3_r, b3_r, const0, const1, const2, const3,
                b0_r, b1_r, b2_r, b3_r);
    DOTP_SH4_SW(b3_l, b3_l, b3_l, b3_l, const0, const1, const2, const3,
                b0_l, b1_l, b2_l, b3_l);
    DPADD_SH4_SW(temp0_r, temp0_r, temp0_r, temp0_r,
                 const4, const5, const6, const7, b0_r, b1_r, b2_r, b3_r);
    DPADD_SH4_SW(temp0_l, temp0_l, temp0_l, temp0_l,
                 const4, const5, const6, const7, b0_l, b1_l, b2_l, b3_l);
    BUTTERFLY_16(a0_r, a0_l, a1_r, a1_l, a2_r, a2_l, a3_r, a3_l,
                 b3_l, b3_r, b2_l, b2_r, b1_l, b1_r, b0_l, b0_r,
                 temp0_r, temp0_l, temp1_r, temp1_l,
                 temp2_r, temp2_l, temp3_r, temp3_l,
                 a3_l, a3_r, a2_l, a2_r, a1_l, a1_r, a0_l, a0_r);
    SRA_4V(temp0_r, temp0_l, temp1_r, temp1_l, 20);
    SRA_4V(temp2_r, temp2_l, temp3_r, temp3_l, 20);
    LD_SH4(dst, dst_stride, in0, in1, in2, in3);
    PCKEV_H4_SW(temp0_l, temp0_r, temp1_l, temp1_r, temp2_l, temp2_r,
                temp3_l, temp3_r, temp0_r, temp1_r, temp2_r, temp3_r);
    ILVR_B4_SW(zero, in0, zero, in1, zero, in2, zero, in3,
               temp0_l, temp1_l, temp2_l, temp3_l);
    temp0_r = (v4i32) ((v8i16) (temp0_r) + (v8i16) (temp0_l));
    temp1_r = (v4i32) ((v8i16) (temp1_r) + (v8i16) (temp1_l));
    temp2_r = (v4i32) ((v8i16) (temp2_r) + (v8i16) (temp2_l));
    temp3_r = (v4i32) ((v8i16) (temp3_r) + (v8i16) (temp3_l));
    temp0_r = (v4i32) CLIP_SH_0_255(temp0_r);
    temp1_r = (v4i32) CLIP_SH_0_255(temp1_r);
    temp2_r = (v4i32) CLIP_SH_0_255(temp2_r);
    temp3_r = (v4i32) CLIP_SH_0_255(temp3_r);
    PCKEV_B4_SW(temp0_r, temp0_r, temp1_r, temp1_r,
                temp2_r, temp2_r, temp3_r, temp3_r,
                temp0_r, temp1_r, temp2_r, temp3_r);
    tmp0 = __msa_copy_u_d((v2i64) temp0_r, 1);
    tmp1 = __msa_copy_u_d((v2i64) temp1_r, 1);
    tmp2 = __msa_copy_u_d((v2i64) temp2_r, 1);
    tmp3 = __msa_copy_u_d((v2i64) temp3_r, 1);
    SD4(tmp0, tmp1, tmp2, tmp3, dst, dst_stride);

    SRA_4V(a3_r, a3_l, a2_r, a2_l, 20);
    SRA_4V(a1_r, a1_l, a0_r, a0_l, 20);
    LD_SH4(dst + 4 * dst_stride, dst_stride, in4, in5, in6, in7);
    PCKEV_H4_SW(a0_l, a0_r, a1_l, a1_r, a2_l, a2_r, a3_l, a3_r,
                a0_r, a1_r, a2_r, a3_r);
    ILVR_B4_SW(zero, in4, zero, in5, zero, in6, zero, in7,
               a3_l, a2_l, a1_l, a0_l);
    a3_r = (v4i32) ((v8i16) (a3_r) + (v8i16) (a3_l));
    a2_r = (v4i32) ((v8i16) (a2_r) + (v8i16) (a2_l));
    a1_r = (v4i32) ((v8i16) (a1_r) + (v8i16) (a1_l));
    a0_r = (v4i32) ((v8i16) (a0_r) + (v8i16) (a0_l));
    a3_r = (v4i32) CLIP_SH_0_255(a3_r);
    a2_r = (v4i32) CLIP_SH_0_255(a2_r);
    a1_r = (v4i32) CLIP_SH_0_255(a1_r);
    a0_r = (v4i32) CLIP_SH_0_255(a0_r);
    PCKEV_B4_SW(a0_r, a0_r, a1_r, a1_r,
                a2_r, a2_r, a3_r, a3_r, a0_r, a1_r, a2_r, a3_r);
    tmp0 = __msa_copy_u_d((v2i64) a3_r, 1);
    tmp1 = __msa_copy_u_d((v2i64) a2_r, 1);
    tmp2 = __msa_copy_u_d((v2i64) a1_r, 1);
    tmp3 = __msa_copy_u_d((v2i64) a0_r, 1);
    SD4(tmp0, tmp1, tmp2, tmp3, dst + 4 * dst_stride, dst_stride);
}

void ff_simple_idct_msa(int16_t *block)
{
    simple_idct_msa(block);
}

void ff_simple_idct_put_msa(uint8_t *dst, ptrdiff_t dst_stride, int16_t *block)
{
    simple_idct_put_msa(dst, dst_stride, block);
}

void ff_simple_idct_add_msa(uint8_t *dst, ptrdiff_t dst_stride, int16_t *block)
{
    simple_idct_add_msa(dst, dst_stride, block);
}
