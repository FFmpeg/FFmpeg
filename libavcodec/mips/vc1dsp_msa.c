/*
 * Loongson SIMD optimized vc1dsp
 *
 * Copyright (c) 2019 Loongson Technology Corporation Limited
 *                    gxw <guxiwei-hf@loongson.cn>
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

#include "vc1dsp_mips.h"
#include "constants.h"
#include "libavutil/mips/generic_macros_msa.h"

void ff_vc1_inv_trans_8x8_msa(int16_t block[64])
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v4i32 in_r0, in_r1, in_r2, in_r3, in_r4, in_r5, in_r6, in_r7;
    v4i32 in_l0, in_l1, in_l2, in_l3, in_l4, in_l5, in_l6, in_l7;
    v4i32 t_r1, t_r2, t_r3, t_r4, t_r5, t_r6, t_r7, t_r8;
    v4i32 t_l1, t_l2, t_l3, t_l4, t_l5, t_l6, t_l7, t_l8;
    v4i32 cnst_12 = {12, 12, 12, 12};
    v4i32 cnst_4 = {4, 4, 4, 4};
    v4i32 cnst_16 = {16, 16, 16, 16};
    v4i32 cnst_6 = {6, 6, 6, 6};
    v4i32 cnst_15 = {15, 15, 15, 15};
    v4i32 cnst_9 = {9, 9, 9, 9};
    v4i32 cnst_1 = {1, 1, 1, 1};
    v4i32 cnst_64 = {64, 64, 64, 64};

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    UNPCK_SH_SW(in0, in_r0, in_l0);
    UNPCK_SH_SW(in1, in_r1, in_l1);
    UNPCK_SH_SW(in2, in_r2, in_l2);
    UNPCK_SH_SW(in3, in_r3, in_l3);
    UNPCK_SH_SW(in4, in_r4, in_l4);
    UNPCK_SH_SW(in5, in_r5, in_l5);
    UNPCK_SH_SW(in6, in_r6, in_l6);
    UNPCK_SH_SW(in7, in_r7, in_l7);
    // First loop
    t_r1 = cnst_12 * (in_r0 + in_r4) + cnst_4;
    t_l1 = cnst_12 * (in_l0 + in_l4) + cnst_4;
    t_r2 = cnst_12 * (in_r0 - in_r4) + cnst_4;
    t_l2 = cnst_12 * (in_l0 - in_l4) + cnst_4;
    t_r3 = cnst_16 * in_r2 + cnst_6 * in_r6;
    t_l3 = cnst_16 * in_l2 + cnst_6 * in_l6;
    t_r4 = cnst_6 * in_r2 - cnst_16 * in_r6;
    t_l4 = cnst_6 * in_l2 - cnst_16 * in_l6;

    ADD4(t_r1, t_r3, t_l1, t_l3, t_r2, t_r4, t_l2, t_l4, t_r5, t_l5, t_r6, t_l6);
    SUB4(t_r2, t_r4, t_l2, t_l4, t_r1, t_r3, t_l1, t_l3, t_r7, t_l7, t_r8, t_l8);
    t_r1 = cnst_16 * in_r1 + cnst_15 * in_r3 + cnst_9 * in_r5 + cnst_4 * in_r7;
    t_l1 = cnst_16 * in_l1 + cnst_15 * in_l3 + cnst_9 * in_l5 + cnst_4 * in_l7;
    t_r2 = cnst_15 * in_r1 - cnst_4 * in_r3 - cnst_16 * in_r5 - cnst_9 * in_r7;
    t_l2 = cnst_15 * in_l1 - cnst_4 * in_l3 - cnst_16 * in_l5 - cnst_9 * in_l7;
    t_r3 = cnst_9 * in_r1 - cnst_16 * in_r3 + cnst_4 * in_r5 + cnst_15 * in_r7;
    t_l3 = cnst_9 * in_l1 - cnst_16 * in_l3 + cnst_4 * in_l5 + cnst_15 * in_l7;
    t_r4 = cnst_4 * in_r1 - cnst_9 * in_r3 + cnst_15 * in_r5 - cnst_16 * in_r7;
    t_l4 = cnst_4 * in_l1 - cnst_9 * in_l3 + cnst_15 * in_l5 - cnst_16 * in_l7;

    in_r0 = (t_r5 + t_r1) >> 3;
    in_l0 = (t_l5 + t_l1) >> 3;
    in_r1 = (t_r6 + t_r2) >> 3;
    in_l1 = (t_l6 + t_l2) >> 3;
    in_r2 = (t_r7 + t_r3) >> 3;
    in_l2 = (t_l7 + t_l3) >> 3;
    in_r3 = (t_r8 + t_r4) >> 3;
    in_l3 = (t_l8 + t_l4) >> 3;

    in_r4 = (t_r8 - t_r4) >> 3;
    in_l4 = (t_l8 - t_l4) >> 3;
    in_r5 = (t_r7 - t_r3) >> 3;
    in_l5 = (t_l7 - t_l3) >> 3;
    in_r6 = (t_r6 - t_r2) >> 3;
    in_l6 = (t_l6 - t_l2) >> 3;
    in_r7 = (t_r5 - t_r1) >> 3;
    in_l7 = (t_l5 - t_l1) >> 3;
    TRANSPOSE4x4_SW_SW(in_r0, in_r1, in_r2, in_r3, in_r0, in_r1, in_r2, in_r3);
    TRANSPOSE4x4_SW_SW(in_l0, in_l1, in_l2, in_l3, in_l0, in_l1, in_l2, in_l3);
    TRANSPOSE4x4_SW_SW(in_r4, in_r5, in_r6, in_r7, in_r4, in_r5, in_r6, in_r7);
    TRANSPOSE4x4_SW_SW(in_l4, in_l5, in_l6, in_l7, in_l4, in_l5, in_l6, in_l7);
    // Second loop
    t_r1 = cnst_12 * (in_r0 + in_l0) + cnst_64;
    t_l1 = cnst_12 * (in_r4 + in_l4) + cnst_64;
    t_r2 = cnst_12 * (in_r0 - in_l0) + cnst_64;
    t_l2 = cnst_12 * (in_r4 - in_l4) + cnst_64;
    t_r3 = cnst_16 * in_r2 + cnst_6 * in_l2;
    t_l3 = cnst_16 * in_r6 + cnst_6 * in_l6;
    t_r4 = cnst_6 * in_r2 - cnst_16 * in_l2;
    t_l4 = cnst_6 * in_r6 - cnst_16 * in_l6;

    ADD4(t_r1, t_r3, t_l1, t_l3, t_r2, t_r4, t_l2, t_l4, t_r5, t_l5, t_r6, t_l6);
    SUB4(t_r2, t_r4, t_l2, t_l4, t_r1, t_r3, t_l1, t_l3, t_r7, t_l7, t_r8, t_l8);
    t_r1 = cnst_16 * in_r1 + cnst_15 * in_r3 + cnst_9 * in_l1 + cnst_4 * in_l3;
    t_l1 = cnst_16 * in_r5 + cnst_15 * in_r7 + cnst_9 * in_l5 + cnst_4 * in_l7;
    t_r2 = cnst_15 * in_r1 - cnst_4 * in_r3 - cnst_16 * in_l1 - cnst_9 * in_l3;
    t_l2 = cnst_15 * in_r5 - cnst_4 * in_r7 - cnst_16 * in_l5 - cnst_9 * in_l7;
    t_r3 = cnst_9 * in_r1 - cnst_16 * in_r3 + cnst_4 * in_l1 + cnst_15 * in_l3;
    t_l3 = cnst_9 * in_r5 - cnst_16 * in_r7 + cnst_4 * in_l5 + cnst_15 * in_l7;
    t_r4 = cnst_4 * in_r1 - cnst_9 * in_r3 + cnst_15 * in_l1 - cnst_16 * in_l3;
    t_l4 = cnst_4 * in_r5 - cnst_9 * in_r7 + cnst_15 * in_l5 - cnst_16 * in_l7;

    in_r0 = (t_r5 + t_r1) >> 7;
    in_l0 = (t_l5 + t_l1) >> 7;
    in_r1 = (t_r6 + t_r2) >> 7;
    in_l1 = (t_l6 + t_l2) >> 7;
    in_r2 = (t_r7 + t_r3) >> 7;
    in_l2 = (t_l7 + t_l3) >> 7;
    in_r3 = (t_r8 + t_r4) >> 7;
    in_l3 = (t_l8 + t_l4) >> 7;

    in_r4 = (t_r8 - t_r4 + cnst_1) >> 7;
    in_l4 = (t_l8 - t_l4 + cnst_1) >> 7;
    in_r5 = (t_r7 - t_r3 + cnst_1) >> 7;
    in_l5 = (t_l7 - t_l3 + cnst_1) >> 7;
    in_r6 = (t_r6 - t_r2 + cnst_1) >> 7;
    in_l6 = (t_l6 - t_l2 + cnst_1) >> 7;
    in_r7 = (t_r5 - t_r1 + cnst_1) >> 7;
    in_l7 = (t_l5 - t_l1 + cnst_1) >> 7;
    PCKEV_H4_SH(in_l0, in_r0, in_l1, in_r1, in_l2, in_r2, in_l3, in_r3,
                in0, in1, in2, in3);
    PCKEV_H4_SH(in_l4, in_r4, in_l5, in_r5, in_l6, in_r6, in_l7, in_r7,
                in4, in5, in6, in7);
    ST_SH8(in0, in1, in2, in3, in4, in5, in6, in7, block, 8);
}

void ff_vc1_inv_trans_4x8_msa(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v4i32 in_r0, in_r1, in_r2, in_r3, in_r4, in_r5, in_r6, in_r7;
    v4i32 t1, t2, t3, t4, t5, t6, t7, t8;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 zero_m = { 0 };
    v4i32 cnst_17 = {17, 17, 17, 17};
    v4i32 cnst_22 = {22, 22, 22, 22};
    v4i32 cnst_10 = {10, 10, 10, 10};
    v4i32 cnst_12 = {12, 12, 12, 12};
    v4i32 cnst_64 = {64, 64, 64, 64};
    v4i32 cnst_16 = {16, 16, 16, 16};
    v4i32 cnst_15 = {15, 15, 15, 15};
    v4i32 cnst_4 = {4, 4, 4, 4};
    v4i32 cnst_6 = {6, 6, 6, 6};
    v4i32 cnst_9 = {9, 9, 9, 9};
    v4i32 cnst_1 = {1, 1, 1, 1};

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    UNPCK_R_SH_SW(in0, in_r0);
    UNPCK_R_SH_SW(in1, in_r1);
    UNPCK_R_SH_SW(in2, in_r2);
    UNPCK_R_SH_SW(in3, in_r3);
    UNPCK_R_SH_SW(in4, in_r4);
    UNPCK_R_SH_SW(in5, in_r5);
    UNPCK_R_SH_SW(in6, in_r6);
    UNPCK_R_SH_SW(in7, in_r7);
    // First loop
    TRANSPOSE4x4_SW_SW(in_r0, in_r1, in_r2, in_r3, in_r0, in_r1, in_r2, in_r3);
    TRANSPOSE4x4_SW_SW(in_r4, in_r5, in_r6, in_r7, in_r4, in_r5, in_r6, in_r7);
    t1 = cnst_17 * (in_r0 + in_r2) + cnst_4;
    t5 = cnst_17 * (in_r4 + in_r6) + cnst_4;
    t2 = cnst_17 * (in_r0 - in_r2) + cnst_4;
    t6 = cnst_17 * (in_r4 - in_r6) + cnst_4;
    t3 = cnst_22 * in_r1 + cnst_10 * in_r3;
    t7 = cnst_22 * in_r5 + cnst_10 * in_r7;
    t4 = cnst_22 * in_r3 - cnst_10 * in_r1;
    t8 = cnst_22 * in_r7 - cnst_10 * in_r5;

    in_r0 = (t1 + t3) >> 3;
    in_r4 = (t5 + t7) >> 3;
    in_r1 = (t2 - t4) >> 3;
    in_r5 = (t6 - t8) >> 3;
    in_r2 = (t2 + t4) >> 3;
    in_r6 = (t6 + t8) >> 3;
    in_r3 = (t1 - t3) >> 3;
    in_r7 = (t5 - t7) >> 3;
    TRANSPOSE4x4_SW_SW(in_r0, in_r1, in_r2, in_r3, in_r0, in_r1, in_r2, in_r3);
    TRANSPOSE4x4_SW_SW(in_r4, in_r5, in_r6, in_r7, in_r4, in_r5, in_r6, in_r7);
    PCKEV_H4_SH(in_r1, in_r0, in_r3, in_r2, in_r5, in_r4, in_r7, in_r6,
                in0, in1, in2, in3);
    ST_D8(in0, in1, in2, in3, 0, 1, 0, 1, 0, 1, 0, 1, block, 8);
    // Second loop
    t1 = cnst_12 * (in_r0 + in_r4) + cnst_64;
    t2 = cnst_12 * (in_r0 - in_r4) + cnst_64;
    t3 = cnst_16 * in_r2 + cnst_6 * in_r6;
    t4 = cnst_6 * in_r2 - cnst_16 * in_r6;
    t5 = t1 + t3, t6 = t2 + t4;
    t7 = t2 - t4, t8 = t1 - t3;
    t1 = cnst_16 * in_r1 + cnst_15 * in_r3 + cnst_9 * in_r5 + cnst_4 * in_r7;
    t2 = cnst_15 * in_r1 - cnst_4 * in_r3 - cnst_16 * in_r5 - cnst_9 * in_r7;
    t3 = cnst_9 * in_r1 - cnst_16 * in_r3 + cnst_4 * in_r5 + cnst_15 * in_r7;
    t4 = cnst_4 * in_r1 - cnst_9 * in_r3 + cnst_15 * in_r5 - cnst_16 * in_r7;
    LD_SW8(dest, linesize, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_B8_SW(zero_m, dst0, zero_m, dst1, zero_m, dst2, zero_m, dst3,
               zero_m, dst4, zero_m, dst5, zero_m, dst6, zero_m, dst7,
               dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_H4_SW(zero_m, dst0, zero_m, dst1, zero_m, dst2, zero_m, dst3,
               dst0, dst1, dst2, dst3);
    ILVR_H4_SW(zero_m, dst4, zero_m, dst5, zero_m, dst6, zero_m, dst7,
               dst4, dst5, dst6, dst7);
    in_r0 = (t5 + t1) >> 7;
    in_r1 = (t6 + t2) >> 7;
    in_r2 = (t7 + t3) >> 7;
    in_r3 = (t8 + t4) >> 7;
    in_r4 = (t8 - t4 + cnst_1) >> 7;
    in_r5 = (t7 - t3 + cnst_1) >> 7;
    in_r6 = (t6 - t2 + cnst_1) >> 7;
    in_r7 = (t5 - t1 + cnst_1) >> 7;
    ADD4(in_r0, dst0, in_r1, dst1, in_r2, dst2, in_r3, dst3,
         in_r0, in_r1, in_r2, in_r3);
    ADD4(in_r4, dst4, in_r5, dst5, in_r6, dst6, in_r7, dst7,
         in_r4, in_r5, in_r6, in_r7);
    CLIP_SW8_0_255(in_r0, in_r1, in_r2, in_r3, in_r4, in_r5, in_r6, in_r7);
    PCKEV_H4_SH(in_r1, in_r0, in_r3, in_r2, in_r5, in_r4, in_r7, in_r6,
                in0, in1, in2, in3);
    PCKEV_B2_SH(in1, in0, in3, in2, in0, in1);
    ST_W8(in0, in1, 0, 1, 2, 3, 0, 1, 2, 3, dest, linesize);
}

void ff_vc1_inv_trans_8x4_msa(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    v4i32 in0, in1, in2, in3, in4, in5, in6, in7;
    v4i32 t1, t2, t3, t4, t5, t6, t7, t8;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 zero_m = { 0 };
    v4i32 cnst_17 = {17, 17, 17, 17};
    v4i32 cnst_22 = {22, 22, 22, 22};
    v4i32 cnst_10 = {10, 10, 10, 10};
    v4i32 cnst_12 = {12, 12, 12, 12};
    v4i32 cnst_64 = {64, 64, 64, 64};
    v4i32 cnst_16 = {16, 16, 16, 16};
    v4i32 cnst_15 = {15, 15, 15, 15};
    v4i32 cnst_4 = {4, 4, 4, 4};
    v4i32 cnst_6 = {6, 6, 6, 6};
    v4i32 cnst_9 = {9, 9, 9, 9};

    LD_SW4(block, 8, t1, t2, t3, t4);
    UNPCK_SH_SW(t1, in0, in4);
    UNPCK_SH_SW(t2, in1, in5);
    UNPCK_SH_SW(t3, in2, in6);
    UNPCK_SH_SW(t4, in3, in7);
    TRANSPOSE4x4_SW_SW(in0, in1, in2, in3, in0, in1, in2, in3);
    TRANSPOSE4x4_SW_SW(in4, in5, in6, in7, in4, in5, in6, in7);
    // First loop
    t1 = cnst_12 * (in0 + in4) + cnst_4;
    t2 = cnst_12 * (in0 - in4) + cnst_4;
    t3 = cnst_16 * in2 + cnst_6 * in6;
    t4 = cnst_6 * in2 - cnst_16 * in6;
    t5 = t1 + t3, t6 = t2 + t4;
    t7 = t2 - t4, t8 = t1 - t3;
    t1 = cnst_16 * in1 + cnst_15 * in3 + cnst_9 * in5 + cnst_4 * in7;
    t2 = cnst_15 * in1 - cnst_4 * in3 - cnst_16 * in5 - cnst_9 * in7;
    t3 = cnst_9 * in1 - cnst_16 * in3 + cnst_4 * in5 + cnst_15 * in7;
    t4 = cnst_4 * in1 - cnst_9 * in3 + cnst_15 * in5 - cnst_16 * in7;
    in0 = (t5 + t1) >> 3;
    in1 = (t6 + t2) >> 3;
    in2 = (t7 + t3) >> 3;
    in3 = (t8 + t4) >> 3;
    in4 = (t8 - t4) >> 3;
    in5 = (t7 - t3) >> 3;
    in6 = (t6 - t2) >> 3;
    in7 = (t5 - t1) >> 3;
    TRANSPOSE4x4_SW_SW(in0, in1, in2, in3, in0, in1, in2, in3);
    TRANSPOSE4x4_SW_SW(in4, in5, in6, in7, in4, in5, in6, in7);
    PCKEV_H4_SW(in4, in0, in5, in1, in6, in2, in7, in3, t1, t2, t3, t4);
    ST_SW4(t1, t2, t3, t4, block, 8);
    // Second loop
    LD_SW4(dest, linesize, dst0, dst1, dst2, dst3);
    ILVR_B4_SW(zero_m, dst0, zero_m, dst1, zero_m, dst2, zero_m, dst3,
               dst0, dst1, dst2, dst3);
    ILVL_H4_SW(zero_m, dst0, zero_m, dst1, zero_m, dst2, zero_m, dst3,
               dst4, dst5, dst6, dst7);
    ILVR_H4_SW(zero_m, dst0, zero_m, dst1, zero_m, dst2, zero_m, dst3,
               dst0, dst1, dst2, dst3);
    // Right part
    t1 = cnst_17 * (in0 + in2) + cnst_64;
    t2 = cnst_17 * (in0 - in2) + cnst_64;
    t3 = cnst_22 * in1 + cnst_10 * in3;
    t4 = cnst_22 * in3 - cnst_10 * in1;
    in0 = (t1 + t3) >> 7;
    in1 = (t2 - t4) >> 7;
    in2 = (t2 + t4) >> 7;
    in3 = (t1 - t3) >> 7;
    ADD4(in0, dst0, in1, dst1, in2, dst2, in3, dst3, in0, in1, in2, in3);
    CLIP_SW4_0_255(in0, in1, in2, in3);
    // Left part
    t5 = cnst_17 * (in4 + in6) + cnst_64;
    t6 = cnst_17 * (in4 - in6) + cnst_64;
    t7 = cnst_22 * in5 + cnst_10 * in7;
    t8 = cnst_22 * in7 - cnst_10 * in5;
    in4 = (t5 + t7) >> 7;
    in5 = (t6 - t8) >> 7;
    in6 = (t6 + t8) >> 7;
    in7 = (t5 - t7) >> 7;
    ADD4(in4, dst4, in5, dst5, in6, dst6, in7, dst7, in4, in5, in6, in7);
    CLIP_SW4_0_255(in4, in5, in6, in7);
    PCKEV_H4_SW(in4, in0, in5, in1, in6, in2, in7, in3, in0, in1, in2, in3);
    PCKEV_B2_SW(in1, in0, in3, in2, in0, in1);
    ST_D4(in0, in1, 0, 1, 0, 1, dest, linesize);
}

static void put_vc1_mspel_mc_h_v_msa(uint8_t *dst, const uint8_t *src,
                                     ptrdiff_t stride, int hmode, int vmode,
                                     int rnd)
{
    v8i16 in_r0, in_r1, in_r2, in_r3, in_l0, in_l1, in_l2, in_l3;
    v8i16 t0, t1, t2, t3, t4, t5, t6, t7;
    v8i16 t8, t9, t10, t11, t12, t13, t14, t15;
    v8i16 cnst_para0, cnst_para1, cnst_para2, cnst_para3, cnst_r;
    static const int para_value[][4] = {{4, 53, 18, 3},
                                        {1, 9, 9, 1},
                                        {3, 18, 53, 4}};
    static const int shift_value[] = {0, 5, 1, 5};
    int shift = (shift_value[hmode] + shift_value[vmode]) >> 1;
    int r = (1 << (shift - 1)) + rnd - 1;
    cnst_r = __msa_fill_h(r);
    src -= 1, src -= stride;
    cnst_para0 = __msa_fill_h(para_value[vmode - 1][0]);
    cnst_para1 = __msa_fill_h(para_value[vmode - 1][1]);
    cnst_para2 = __msa_fill_h(para_value[vmode - 1][2]);
    cnst_para3 = __msa_fill_h(para_value[vmode - 1][3]);
    LD_SH4(src, stride, in_l0, in_l1, in_l2, in_l3);
    UNPCK_UB_SH(in_l0, in_r0, in_l0);
    UNPCK_UB_SH(in_l1, in_r1, in_l1);
    UNPCK_UB_SH(in_l2, in_r2, in_l2);
    UNPCK_UB_SH(in_l3, in_r3, in_l3);
    // row 0
    t0 = cnst_para1 * in_r1 + cnst_para2 * in_r2
         - cnst_para0 * in_r0 - cnst_para3 * in_r3;
    t8 = cnst_para1 * in_l1 + cnst_para2 * in_l2
         - cnst_para0 * in_l0 - cnst_para3 * in_l3;
    in_l0 = LD_SH(src + 4 * stride);
    UNPCK_UB_SH(in_l0, in_r0, in_l0);
    // row 1
    t1 = cnst_para1 * in_r2 + cnst_para2 * in_r3
         - cnst_para0 * in_r1 - cnst_para3 * in_r0;
    t9 = cnst_para1 * in_l2 + cnst_para2 * in_l3
         - cnst_para0 * in_l1 - cnst_para3 * in_l0;
    in_l1 = LD_SH(src + 5 * stride);
    UNPCK_UB_SH(in_l1, in_r1, in_l1);
    // row 2
    t2 = cnst_para1 * in_r3 + cnst_para2 * in_r0
         - cnst_para0 * in_r2 - cnst_para3 * in_r1;
    t10 = cnst_para1 * in_l3 + cnst_para2 * in_l0
          - cnst_para0 * in_l2 - cnst_para3 * in_l1;
    in_l2 = LD_SH(src + 6 * stride);
    UNPCK_UB_SH(in_l2, in_r2, in_l2);
    // row 3
    t3 = cnst_para1 * in_r0 + cnst_para2 * in_r1
         - cnst_para0 * in_r3 - cnst_para3 * in_r2;
    t11 = cnst_para1 * in_l0 + cnst_para2 * in_l1
          - cnst_para0 * in_l3 - cnst_para3 * in_l2;
    in_l3 = LD_SH(src + 7 * stride);
    UNPCK_UB_SH(in_l3, in_r3, in_l3);
    // row 4
    t4 = cnst_para1 * in_r1 + cnst_para2 * in_r2
         - cnst_para0 * in_r0 - cnst_para3 * in_r3;
    t12 = cnst_para1 * in_l1 + cnst_para2 * in_l2
          - cnst_para0 * in_l0 - cnst_para3 * in_l3;
    in_l0 = LD_SH(src + 8 * stride);
    UNPCK_UB_SH(in_l0, in_r0, in_l0);
    // row 5
    t5 = cnst_para1 * in_r2 + cnst_para2 * in_r3
         - cnst_para0 * in_r1 - cnst_para3 * in_r0;
    t13 = cnst_para1 * in_l2 + cnst_para2 * in_l3
          - cnst_para0 * in_l1 - cnst_para3 * in_l0;
    in_l1 = LD_SH(src + 9 * stride);
    UNPCK_UB_SH(in_l1, in_r1, in_l1);
    // row 6
    t6 = cnst_para1 * in_r3 + cnst_para2 * in_r0
         - cnst_para0 * in_r2 - cnst_para3 * in_r1;
    t14 = cnst_para1 * in_l3 + cnst_para2 * in_l0
          - cnst_para0 * in_l2 - cnst_para3 * in_l1;
    in_l2 = LD_SH(src + 10 * stride);
    UNPCK_UB_SH(in_l2, in_r2, in_l2);
    // row 7
    t7 = cnst_para1 * in_r0 + cnst_para2 * in_r1
         - cnst_para0 * in_r3 - cnst_para3 * in_r2;
    t15 = cnst_para1 * in_l0 + cnst_para2 * in_l1
          - cnst_para0 * in_l3 - cnst_para3 * in_l2;

    ADD4(t0, cnst_r, t1, cnst_r, t2, cnst_r, t3, cnst_r, t0, t1, t2, t3);
    ADD4(t4, cnst_r, t5, cnst_r, t6, cnst_r, t7, cnst_r, t4, t5, t6, t7);
    ADD4(t8, cnst_r, t9, cnst_r, t10, cnst_r, t11, cnst_r,
         t8, t9, t10, t11);
    ADD4(t12, cnst_r, t13, cnst_r, t14, cnst_r, t15, cnst_r,
         t12, t13, t14, t15);
    t0 >>= shift, t1 >>= shift, t2 >>= shift, t3 >>= shift;
    t4 >>= shift, t5 >>= shift, t6 >>= shift, t7 >>= shift;
    t8 >>= shift, t9 >>= shift, t10 >>= shift, t11 >>= shift;
    t12 >>= shift, t13 >>= shift, t14 >>= shift, t15 >>= shift;
    TRANSPOSE8x8_SH_SH(t0, t1, t2, t3, t4, t5, t6, t7,
                       t0, t1, t2, t3, t4, t5, t6, t7);
    TRANSPOSE8x8_SH_SH(t8, t9, t10, t11, t12, t13, t14, t15,
                       t8, t9, t10, t11, t12, t13, t14, t15);
    cnst_para0 = __msa_fill_h(para_value[hmode - 1][0]);
    cnst_para1 = __msa_fill_h(para_value[hmode - 1][1]);
    cnst_para2 = __msa_fill_h(para_value[hmode - 1][2]);
    cnst_para3 = __msa_fill_h(para_value[hmode - 1][3]);
    r = 64 - rnd;
    cnst_r = __msa_fill_h(r);
    // col 0 ~ 7
    t0 = cnst_para1 * t1 + cnst_para2 * t2 - cnst_para0 * t0 - cnst_para3 * t3;
    t1 = cnst_para1 * t2 + cnst_para2 * t3 - cnst_para0 * t1 - cnst_para3 * t4;
    t2 = cnst_para1 * t3 + cnst_para2 * t4 - cnst_para0 * t2 - cnst_para3 * t5;
    t3 = cnst_para1 * t4 + cnst_para2 * t5 - cnst_para0 * t3 - cnst_para3 * t6;
    t4 = cnst_para1 * t5 + cnst_para2 * t6 - cnst_para0 * t4 - cnst_para3 * t7;
    t5 = cnst_para1 * t6 + cnst_para2 * t7 - cnst_para0 * t5 - cnst_para3 * t8;
    t6 = cnst_para1 * t7 + cnst_para2 * t8 - cnst_para0 * t6 - cnst_para3 * t9;
    t7 = cnst_para1 * t8 + cnst_para2 * t9 - cnst_para0 * t7 - cnst_para3 * t10;
    ADD4(t0, cnst_r, t1, cnst_r, t2, cnst_r, t3, cnst_r, t0, t1, t2, t3);
    ADD4(t4, cnst_r, t5, cnst_r, t6, cnst_r, t7, cnst_r, t4, t5, t6, t7);
    t0 >>= 7, t1 >>= 7, t2 >>= 7, t3 >>= 7;
    t4 >>= 7, t5 >>= 7, t6 >>= 7, t7 >>= 7;
    TRANSPOSE8x8_SH_SH(t0, t1, t2, t3, t4, t5, t6, t7,
                       t0, t1, t2, t3, t4, t5, t6, t7);
    CLIP_SH8_0_255(t0, t1, t2, t3, t4, t5, t6, t7);
    PCKEV_B4_SH(t1, t0, t3, t2, t5, t4, t7, t6, t0, t1, t2, t3);
    ST_D8(t0, t1, t2, t3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

#define PUT_VC1_MSPEL_MC_MSA(hmode, vmode)                                    \
void ff_put_vc1_mspel_mc ## hmode ## vmode ## _msa(uint8_t *dst,              \
                                                const uint8_t *src,           \
                                                ptrdiff_t stride, int rnd)    \
{                                                                             \
    put_vc1_mspel_mc_h_v_msa(dst, src, stride, hmode, vmode, rnd);            \
}                                                                             \
void ff_put_vc1_mspel_mc ## hmode ## vmode ## _16_msa(uint8_t *dst,           \
                                                   const uint8_t *src,        \
                                                   ptrdiff_t stride, int rnd) \
{                                                                             \
    put_vc1_mspel_mc_h_v_msa(dst, src, stride, hmode, vmode, rnd);            \
    put_vc1_mspel_mc_h_v_msa(dst + 8, src + 8, stride, hmode, vmode, rnd);    \
    dst += 8 * stride, src += 8 * stride;                                     \
    put_vc1_mspel_mc_h_v_msa(dst, src, stride, hmode, vmode, rnd);            \
    put_vc1_mspel_mc_h_v_msa(dst + 8, src + 8, stride, hmode, vmode, rnd);    \
}

PUT_VC1_MSPEL_MC_MSA(1, 1);
PUT_VC1_MSPEL_MC_MSA(1, 2);
PUT_VC1_MSPEL_MC_MSA(1, 3);

PUT_VC1_MSPEL_MC_MSA(2, 1);
PUT_VC1_MSPEL_MC_MSA(2, 2);
PUT_VC1_MSPEL_MC_MSA(2, 3);

PUT_VC1_MSPEL_MC_MSA(3, 1);
PUT_VC1_MSPEL_MC_MSA(3, 2);
PUT_VC1_MSPEL_MC_MSA(3, 3);
