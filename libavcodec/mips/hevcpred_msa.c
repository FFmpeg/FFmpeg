/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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

#include "libavcodec/hevcdec.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "hevcpred_mips.h"

static const int8_t intra_pred_angle_up[17] = {
    -32, -26, -21, -17, -13, -9, -5, -2, 0, 2, 5, 9, 13, 17, 21, 26, 32
};

static const int8_t intra_pred_angle_low[16] = {
    32, 26, 21, 17, 13, 9, 5, 2, 0, -2, -5, -9, -13, -17, -21, -26
};

#define HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,          \
                              mul_val_h0, mul_val_h1, mul_val_h2, mul_val_h3,  \
                              res0, res1, mul_val_b0, mul_val_b1, round)       \
{                                                                              \
    v8i16 res0_m, res1_m, res2_m, res3_m;                                      \
                                                                               \
    MUL4(mul_val_h0, vec0, mul_val_h2, vec0, mul_val_h0, vec1,                 \
         mul_val_h2, vec1, res0_m, res1_m, res2_m, res3_m);                    \
                                                                               \
    res0_m += mul_val_h1 * tmp0;                                               \
    res1_m += mul_val_h3 * tmp0;                                               \
    res2_m += mul_val_h1 * tmp0;                                               \
    res3_m += mul_val_h3 * tmp0;                                               \
                                                                               \
    res0_m += mul_val_b0 * src0_r;                                             \
    res1_m += mul_val_b0 * src0_l;                                             \
    res2_m += (mul_val_b0 - 1) * src0_r;                                       \
    res3_m += (mul_val_b0 - 1) * src0_l;                                       \
                                                                               \
    res0_m += mul_val_b1 * tmp1;                                               \
    res1_m += mul_val_b1 * tmp1;                                               \
    res2_m += (mul_val_b1 + 1) * tmp1;                                         \
    res3_m += (mul_val_b1 + 1) * tmp1;                                         \
                                                                               \
    SRARI_H4_SH(res0_m, res1_m, res2_m, res3_m, round);                        \
    PCKEV_B2_SH(res1_m, res0_m, res3_m, res2_m, res0, res1);                   \
}

static void hevc_intra_pred_vert_4x4_msa(const uint8_t *src_top,
                                         const uint8_t *src_left,
                                         uint8_t *dst, int32_t stride,
                                         int32_t flag)
{
    uint32_t col;
    uint32_t src_data;
    v8i16 vec0, vec1, vec2;
    v16i8 zero = { 0 };

    src_data = LW(src_top);
    SW4(src_data, src_data, src_data, src_data, dst, stride);

    if (0 == flag) {
        src_data = LW(src_left);

        vec2 = (v8i16) __msa_insert_w((v4i32) vec2, 0, src_data);

        vec0 = __msa_fill_h(src_left[-1]);
        vec1 = __msa_fill_h(src_top[0]);

        vec2 = (v8i16) __msa_ilvr_b(zero, (v16i8) vec2);
        vec2 -= vec0;
        vec2 >>= 1;
        vec2 += vec1;
        CLIP_SH_0_255(vec2);

        for (col = 0; col < 4; col++) {
            dst[stride * col] = (uint8_t) vec2[col];
        }
    }
}

static void hevc_intra_pred_vert_8x8_msa(const uint8_t *src_top,
                                         const uint8_t *src_left,
                                         uint8_t *dst, int32_t stride,
                                         int32_t flag)
{
    uint8_t *tmp_dst = dst;
    uint32_t row;
    uint16_t val0, val1, val2, val3;
    uint64_t src_data1;
    v8i16 vec0, vec1, vec2;
    v16i8 zero = { 0 };

    src_data1 = LD(src_top);

    for (row = 8; row--;) {
        SD(src_data1, tmp_dst);
        tmp_dst += stride;
    }

    if (0 == flag) {
        src_data1 = LD(src_left);

        vec2 = (v8i16) __msa_insert_d((v2i64) zero, 0, src_data1);

        vec0 = __msa_fill_h(src_left[-1]);
        vec1 = __msa_fill_h(src_top[0]);

        vec2 = (v8i16) __msa_ilvr_b(zero, (v16i8) vec2);
        vec2 -= vec0;
        vec2 >>= 1;
        vec2 += vec1;
        CLIP_SH_0_255(vec2);

        val0 = vec2[0];
        val1 = vec2[1];
        val2 = vec2[2];
        val3 = vec2[3];

        dst[0] = val0;
        dst[stride] = val1;
        dst[2 * stride] = val2;
        dst[3 * stride] = val3;

        val0 = vec2[4];
        val1 = vec2[5];
        val2 = vec2[6];
        val3 = vec2[7];

        dst[4 * stride] = val0;
        dst[5 * stride] = val1;
        dst[6 * stride] = val2;
        dst[7 * stride] = val3;
    }
}

static void hevc_intra_pred_vert_16x16_msa(const uint8_t *src_top,
                                           const uint8_t *src_left,
                                           uint8_t *dst, int32_t stride,
                                           int32_t flag)
{
    int32_t col;
    uint8_t *tmp_dst = dst;
    uint32_t row;
    v16u8 src;
    v8i16 vec0, vec1, vec2, vec3;

    src = LD_UB(src_top);

    for (row = 16; row--;) {
        ST_UB(src, tmp_dst);
        tmp_dst += stride;
    }

    if (0 == flag) {
        src = LD_UB(src_left);

        vec0 = __msa_fill_h(src_left[-1]);
        vec1 = __msa_fill_h(src_top[0]);

        UNPCK_UB_SH(src, vec2, vec3);
        SUB2(vec2, vec0, vec3, vec0, vec2, vec3);

        vec2 >>= 1;
        vec3 >>= 1;

        ADD2(vec2, vec1, vec3, vec1, vec2, vec3);
        CLIP_SH2_0_255(vec2, vec3);

        src = (v16u8) __msa_pckev_b((v16i8) vec3, (v16i8) vec2);

        for (col = 0; col < 16; col++) {
            dst[stride * col] = src[col];
        }
    }
}

static void hevc_intra_pred_horiz_4x4_msa(const uint8_t *src_top,
                                          const uint8_t *src_left,
                                          uint8_t *dst, int32_t stride,
                                          int32_t flag)
{
    uint32_t val0, val1, val2, val3;
    v16i8 src0;
    v8i16 src0_r, src_top_val, src_left_val;
    v16i8 zero = { 0 };

    val0 = src_left[0] * 0x01010101;
    val1 = src_left[1] * 0x01010101;
    val2 = src_left[2] * 0x01010101;
    val3 = src_left[3] * 0x01010101;
    SW4(val0, val1, val2, val3, dst, stride);

    if (0 == flag) {
        val0 = LW(src_top);
        src0 = (v16i8) __msa_insert_w((v4i32) src0, 0, val0);
        src_top_val = __msa_fill_h(src_top[-1]);
        src_left_val = __msa_fill_h(src_left[0]);

        src0_r = (v8i16) __msa_ilvr_b(zero, src0);

        src0_r -= src_top_val;
        src0_r >>= 1;
        src0_r += src_left_val;
        CLIP_SH_0_255(src0_r);
        src0 = __msa_pckev_b((v16i8) src0_r, (v16i8) src0_r);
        val0 = __msa_copy_s_w((v4i32) src0, 0);
        SW(val0, dst);
    }
}

static void hevc_intra_pred_horiz_8x8_msa(const uint8_t *src_top,
                                          const uint8_t *src_left,
                                          uint8_t *dst, int32_t stride,
                                          int32_t flag)
{
    uint64_t val0, val1, val2, val3;
    v16i8 src0;
    v8i16 src0_r, src_top_val, src_left_val;
    v16i8 zero = { 0 };

    val0 = src_left[0] * 0x0101010101010101;
    val1 = src_left[1] * 0x0101010101010101;
    val2 = src_left[2] * 0x0101010101010101;
    val3 = src_left[3] * 0x0101010101010101;
    SD4(val0, val1, val2, val3, dst, stride);

    val0 = src_left[4] * 0x0101010101010101;
    val1 = src_left[5] * 0x0101010101010101;
    val2 = src_left[6] * 0x0101010101010101;
    val3 = src_left[7] * 0x0101010101010101;
    SD4(val0, val1, val2, val3, dst + 4 * stride, stride);

    if (0 == flag) {
        val0 = LD(src_top);
        src0 = (v16i8) __msa_insert_d((v2i64) src0, 0, val0);
        src_top_val = __msa_fill_h(src_top[-1]);
        src_left_val = __msa_fill_h(src_left[0]);

        src0_r = (v8i16) __msa_ilvr_b(zero, src0);

        src0_r -= src_top_val;
        src0_r >>= 1;
        src0_r += src_left_val;
        CLIP_SH_0_255(src0_r);
        src0 = __msa_pckev_b((v16i8) src0_r, (v16i8) src0_r);
        val0 = __msa_copy_s_d((v2i64) src0, 0);
        SD(val0, dst);
    }
}

static void hevc_intra_pred_horiz_16x16_msa(const uint8_t *src_top,
                                            const uint8_t *src_left,
                                            uint8_t *dst, int32_t stride,
                                            int32_t flag)
{
    uint8_t *tmp_dst = dst;
    uint32_t row;
    uint8_t inp0, inp1, inp2, inp3;
    v16i8 src0, src1, src2, src3;
    v8i16 src0_r, src0_l, src_left_val, src_top_val;

    src_left_val = __msa_fill_h(src_left[0]);

    for (row = 4; row--;) {
        inp0 = src_left[0];
        inp1 = src_left[1];
        inp2 = src_left[2];
        inp3 = src_left[3];
        src_left += 4;

        src0 = __msa_fill_b(inp0);
        src1 = __msa_fill_b(inp1);
        src2 = __msa_fill_b(inp2);
        src3 = __msa_fill_b(inp3);

        ST_SB4(src0, src1, src2, src3, tmp_dst, stride);
        tmp_dst += (4 * stride);
    }

    if (0 == flag) {
        src0 = LD_SB(src_top);
        src_top_val = __msa_fill_h(src_top[-1]);

        UNPCK_UB_SH(src0, src0_r, src0_l);
        SUB2(src0_r, src_top_val, src0_l, src_top_val, src0_r, src0_l);

        src0_r >>= 1;
        src0_l >>= 1;

        ADD2(src0_r, src_left_val, src0_l, src_left_val, src0_r, src0_l);
        CLIP_SH2_0_255(src0_r, src0_l);
        src0 = __msa_pckev_b((v16i8) src0_l, (v16i8) src0_r);
        ST_SB(src0, dst);
    }
}

static void hevc_intra_pred_horiz_32x32_msa(const uint8_t *src_top,
                                            const uint8_t *src_left,
                                            uint8_t *dst, int32_t stride)
{
    uint32_t row;
    uint8_t inp0, inp1, inp2, inp3;
    v16i8 src0, src1, src2, src3;

    for (row = 0; row < 8; row++) {
        inp0 = src_left[row * 4];
        inp1 = src_left[row * 4 + 1];
        inp2 = src_left[row * 4 + 2];
        inp3 = src_left[row * 4 + 3];

        src0 = __msa_fill_b(inp0);
        src1 = __msa_fill_b(inp1);
        src2 = __msa_fill_b(inp2);
        src3 = __msa_fill_b(inp3);

        ST_SB2(src0, src0, dst, 16);
        dst += stride;
        ST_SB2(src1, src1, dst, 16);
        dst += stride;
        ST_SB2(src2, src2, dst, 16);
        dst += stride;
        ST_SB2(src3, src3, dst, 16);
        dst += stride;
    }
}

static void hevc_intra_pred_dc_4x4_msa(const uint8_t *src_top,
                                       const uint8_t *src_left,
                                       uint8_t *dst, int32_t stride,
                                       int32_t flag)
{
    uint8_t *tmp_dst = dst;
    uint32_t addition = 0;
    uint32_t val0, val1, val2;
    v16i8 src = { 0 };
    v16u8 store;
    v16i8 zero = { 0 };
    v8u16 sum, vec0, vec1;

    val0 = LW(src_top);
    val1 = LW(src_left);
    INSERT_W2_SB(val0, val1, src);
    sum = __msa_hadd_u_h((v16u8) src, (v16u8) src);
    sum = (v8u16) __msa_hadd_u_w(sum, sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_srari_w((v4i32) sum, 3);
    addition = __msa_copy_u_w((v4i32) sum, 0);
    store = (v16u8) __msa_fill_b(addition);
    val0 = __msa_copy_u_w((v4i32) store, 0);
    SW4(val0, val0, val0, val0, dst, stride)

        if (0 == flag) {
        ILVR_B2_UH(zero, store, zero, src, vec0, vec1);

        vec1 += vec0;
        vec0 += vec0;
        vec1 += vec0;

        vec1 = (v8u16) __msa_srari_h((v8i16) vec1, 2);
        store = (v16u8) __msa_pckev_b((v16i8) vec1, (v16i8) vec1);
        val1 = (src_left[0] + 2 * addition + src_top[0] + 2) >> 2;
        store = (v16u8) __msa_insert_b((v16i8) store, 0, val1);
        val0 = __msa_copy_u_w((v4i32) store, 0);
        SW(val0, tmp_dst);

        val0 = src_left[1];
        val1 = src_left[2];
        val2 = src_left[3];

        addition *= 3;

        ADD2(val0, addition, val1, addition, val0, val1);
        val2 += addition;

        val0 += 2;
        val1 += 2;
        val2 += 2;
        val0 >>= 2;
        val1 >>= 2;
        val2 >>= 2;

        tmp_dst[stride * 1] = val0;
        tmp_dst[stride * 2] = val1;
        tmp_dst[stride * 3] = val2;
    }
}

static void hevc_intra_pred_dc_8x8_msa(const uint8_t *src_top,
                                       const uint8_t *src_left,
                                       uint8_t *dst, int32_t stride,
                                       int32_t flag)
{
    uint8_t *tmp_dst = dst;
    uint32_t row, col, val;
    uint32_t addition = 0;
    uint64_t val0, val1;
    v16u8 src = { 0 };
    v16u8 store;
    v8u16 sum, vec0, vec1;
    v16i8 zero = { 0 };

    val0 = LD(src_top);
    val1 = LD(src_left);
    INSERT_D2_UB(val0, val1, src);
    sum = __msa_hadd_u_h((v16u8) src, (v16u8) src);
    sum = (v8u16) __msa_hadd_u_w(sum, sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_pckev_w((v4i32) sum, (v4i32) sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_srari_w((v4i32) sum, 4);
    addition = __msa_copy_u_w((v4i32) sum, 0);
    store = (v16u8) __msa_fill_b(addition);
    val0 = __msa_copy_u_d((v2i64) store, 0);

    for (row = 8; row--;) {
        SD(val0, dst);
        dst += stride;
    }

    if (0 == flag) {
        ILVR_B2_UH(zero, store, zero, src, vec0, vec1);

        vec1 += vec0;
        vec0 += vec0;
        vec1 += vec0;
        vec1 = (v8u16) __msa_srari_h((v8i16) vec1, 2);
        store = (v16u8) __msa_pckev_b((v16i8) vec1, (v16i8) vec1);
        val = (src_left[0] + 2 * addition + src_top[0] + 2) >> 2;
        store = (v16u8) __msa_insert_b((v16i8) store, 0, val);
        val0 = __msa_copy_u_d((v2i64) store, 0);
        SD(val0, tmp_dst);

        val0 = LD(src_left);
        src = (v16u8) __msa_insert_d((v2i64) src, 0, val0);
        vec1 = (v8u16) __msa_ilvr_b(zero, (v16i8) src);
        vec0 = (v8u16) __msa_fill_h(addition);
        vec0 *= 3;
        vec1 += vec0;
        vec1 = (v8u16) __msa_srari_h((v8i16) vec1, 2);

        for (col = 1; col < 8; col++) {
            tmp_dst[stride * col] = vec1[col];
        }
    }
}

static void hevc_intra_pred_dc_16x16_msa(const uint8_t *src_top,
                                         const uint8_t *src_left,
                                         uint8_t *dst, int32_t stride,
                                         int32_t flag)
{
    uint8_t *tmp_dst = dst;
    uint32_t row, col, val;
    uint32_t addition = 0;
    v16u8 src_above1, store, src_left1;
    v8u16 sum, sum_above, sum_left;
    v8u16 vec0, vec1, vec2;
    v16i8 zero = { 0 };

    src_above1 = LD_UB(src_top);
    src_left1 = LD_UB(src_left);

    HADD_UB2_UH(src_above1, src_left1, sum_above, sum_left);
    sum = sum_above + sum_left;
    sum = (v8u16) __msa_hadd_u_w(sum, sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_pckev_w((v4i32) sum, (v4i32) sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_srari_w((v4i32) sum, 5);
    addition = __msa_copy_u_w((v4i32) sum, 0);
    store = (v16u8) __msa_fill_b(addition);

    for (row = 16; row--;) {
        ST_UB(store, dst);
        dst += stride;
    }

    if (0 == flag) {
        vec0 = (v8u16) __msa_ilvr_b(zero, (v16i8) store);
        ILVRL_B2_UH(zero, src_above1, vec1, vec2);
        ADD2(vec1, vec0, vec2, vec0, vec1, vec2);
        vec0 += vec0;
        ADD2(vec1, vec0, vec2, vec0, vec1, vec2);
        SRARI_H2_UH(vec1, vec2, 2);
        store = (v16u8) __msa_pckev_b((v16i8) vec2, (v16i8) vec1);
        val = (src_left[0] + 2 * addition + src_top[0] + 2) >> 2;
        store = (v16u8) __msa_insert_b((v16i8) store, 0, val);
        ST_UB(store, tmp_dst);

        ILVRL_B2_UH(zero, src_left1, vec1, vec2);
        vec0 = (v8u16) __msa_fill_h(addition);
        vec0 *= 3;
        ADD2(vec1, vec0, vec2, vec0, vec1, vec2);
        SRARI_H2_UH(vec1, vec2, 2);
        store = (v16u8) __msa_pckev_b((v16i8) vec2, (v16i8) vec1);

        for (col = 1; col < 16; col++) {
            tmp_dst[stride * col] = store[col];
        }
    }
}

static void hevc_intra_pred_dc_32x32_msa(const uint8_t *src_top,
                                         const uint8_t *src_left,
                                         uint8_t *dst, int32_t stride)
{
    uint32_t row;
    v16u8 src_above1, src_above2, store, src_left1, src_left2;
    v8u16 sum_above1, sum_above2;
    v8u16 sum_left1, sum_left2;
    v8u16 sum, sum_above, sum_left;

    LD_UB2(src_top, 16, src_above1, src_above2);
    LD_UB2(src_left, 16, src_left1, src_left2);
    HADD_UB2_UH(src_above1, src_above2, sum_above1, sum_above2);
    HADD_UB2_UH(src_left1, src_left2, sum_left1, sum_left2);
    sum_above = sum_above1 + sum_above2;
    sum_left = sum_left1 + sum_left2;
    sum = sum_above + sum_left;
    sum = (v8u16) __msa_hadd_u_w(sum, sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_pckev_w((v4i32) sum, (v4i32) sum);
    sum = (v8u16) __msa_hadd_u_d((v4u32) sum, (v4u32) sum);
    sum = (v8u16) __msa_srari_w((v4i32) sum, 6);
    store = (v16u8) __msa_splati_b((v16i8) sum, 0);

    for (row = 16; row--;) {
        ST_UB2(store, store, dst, 16);
        dst += stride;
        ST_UB2(store, store, dst, 16);
        dst += stride;
    }
}

static void hevc_intra_pred_plane_4x4_msa(const uint8_t *src_top,
                                          const uint8_t *src_left,
                                          uint8_t *dst, int32_t stride)
{
    uint32_t src0, src1;
    v16i8 src_vec0, src_vec1;
    v8i16 src_vec0_r, src1_r, tmp0, tmp1, mul_val1;
    v8i16 vec0, vec1, vec2, vec3, res0, res1, res2, res3;
    v8i16 mul_val0 = { 3, 2, 1, 0, 1, 2, 3, 4 };
    v16i8 zero = { 0 };

    src0 = LW(src_top);
    src1 = LW(src_left);

    mul_val1 = (v8i16) __msa_pckod_d((v2i64) mul_val0, (v2i64) mul_val0);

    src_vec0 = (v16i8) __msa_insert_w((v4i32) zero, 0, src0);
    src_vec1 = (v16i8) __msa_insert_w((v4i32) zero, 0, src1);

    ILVR_B2_SH(zero, src_vec0, zero, src_vec1, src_vec0_r, src1_r);
    SPLATI_H4_SH(src1_r, 0, 1, 2, 3, vec0, vec1, vec2, vec3);

    tmp0 = __msa_fill_h(src_top[4]);
    tmp1 = __msa_fill_h(src_left[4]);

    MUL4(mul_val0, vec0, mul_val0, vec1, mul_val0, vec2, mul_val0, vec3,
         res0, res1, res2, res3);

    res0 += mul_val1 * tmp0;
    res1 += mul_val1 * tmp0;
    res2 += mul_val1 * tmp0;
    res3 += mul_val1 * tmp0;

    res0 += 3 * src_vec0_r;
    res1 += 2 * src_vec0_r;
    res2 += src_vec0_r;
    res0 += tmp1;
    res1 += 2 * tmp1;
    res2 += 3 * tmp1;
    res3 += 4 * tmp1;

    PCKEV_D2_SH(res1, res0, res3, res2, res0, res1);
    SRARI_H2_SH(res0, res1, 3);
    src_vec0 = __msa_pckev_b((v16i8) res1, (v16i8) res0);
    ST_W4(src_vec0, 0, 1, 2, 3, dst, stride);
}

static void hevc_intra_pred_plane_8x8_msa(const uint8_t *src_top,
                                          const uint8_t *src_left,
                                          uint8_t *dst, int32_t stride)
{
    uint64_t src0, src1;
    v16i8 src_vec0, src_vec1, src_vec2, src_vec3;
    v8i16 src_vec0_r, src_vec1_r;
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v8i16 tmp0, tmp1, tmp2;
    v8i16 mul_val1 = { 1, 2, 3, 4, 5, 6, 7, 8 };
    v8i16 mul_val0 = { 7, 6, 5, 4, 3, 2, 1, 0 };
    v16i8 zero = { 0 };

    src0 = LD(src_top);
    src1 = LD(src_left);

    src_vec0 = (v16i8) __msa_insert_d((v2i64) zero, 0, src0);
    src_vec1 = (v16i8) __msa_insert_d((v2i64) zero, 0, src1);

    ILVR_B2_SH(zero, src_vec0, zero, src_vec1, src_vec0_r, src_vec1_r);
    SPLATI_H4_SH(src_vec1_r, 0, 1, 2, 3, vec0, vec1, vec2, vec3);
    SPLATI_H4_SH(src_vec1_r, 4, 5, 6, 7, vec4, vec5, vec6, vec7);

    tmp0 = __msa_fill_h(src_top[8]);
    tmp1 = __msa_fill_h(src_left[8]);

    MUL4(mul_val0, vec0, mul_val0, vec1, mul_val0, vec2, mul_val0, vec3,
         res0, res1, res2, res3);
    MUL4(mul_val0, vec4, mul_val0, vec5, mul_val0, vec6, mul_val0, vec7,
         res4, res5, res6, res7);

    tmp2 = mul_val1 * tmp0;
    res0 += tmp2;
    res1 += tmp2;
    res2 += tmp2;
    res3 += tmp2;
    res4 += tmp2;
    res5 += tmp2;
    res6 += tmp2;
    res7 += tmp2;

    res0 += 7 * src_vec0_r;
    res1 += 6 * src_vec0_r;
    res2 += 5 * src_vec0_r;
    res3 += 4 * src_vec0_r;
    res4 += 3 * src_vec0_r;
    res5 += 2 * src_vec0_r;
    res6 += src_vec0_r;

    res0 += tmp1;
    res1 += 2 * tmp1;
    res2 += 3 * tmp1;
    res3 += 4 * tmp1;
    res4 += 5 * tmp1;
    res5 += 6 * tmp1;
    res6 += 7 * tmp1;
    res7 += 8 * tmp1;

    SRARI_H4_SH(res0, res1, res2, res3, 4);
    SRARI_H4_SH(res4, res5, res6, res7, 4);
    PCKEV_B4_SB(res1, res0, res3, res2, res5, res4, res7, res6,
                src_vec0, src_vec1, src_vec2, src_vec3);

    ST_D8(src_vec0, src_vec1, src_vec2, src_vec3, 0, 1, 0, 1,
          0, 1, 0, 1, dst, stride);
}

static void hevc_intra_pred_plane_16x16_msa(const uint8_t *src_top,
                                            const uint8_t *src_left,
                                            uint8_t *dst, int32_t stride)
{
    v16u8 src0, src1;
    v8i16 src0_r, src1_r, src0_l, src1_l;
    v8i16 vec0, vec1;
    v8i16 res0, res1, tmp0, tmp1;
    v8i16 mul_val2, mul_val3;
    v8i16 mul_val1 = { 1, 2, 3, 4, 5, 6, 7, 8 };
    v8i16 mul_val0 = { 15, 14, 13, 12, 11, 10, 9, 8 };

    src0 = LD_UB(src_top);
    src1 = LD_UB(src_left);

    UNPCK_UB_SH(src0, src0_r, src0_l);
    UNPCK_UB_SH(src1, src1_r, src1_l);

    mul_val2 = mul_val0 - 8;
    mul_val3 = mul_val1 + 8;

    tmp0 = __msa_fill_h(src_top[16]);
    tmp1 = __msa_fill_h(src_left[16]);

    SPLATI_H2_SH(src1_r, 0, 1, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 15, 1, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 2, 3, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 13, 3, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 4, 5, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 11, 5, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 6, 7, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 9, 7, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 0, 1, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 7, 9, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 2, 3, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 5, 11, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 4, 5, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 3, 13, 5);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 6, 7, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 1, 15, 5);
    ST_SH2(res0, res1, dst, stride);
}

static void process_intra_upper_16x16_msa(const uint8_t *src_top,
                                          const uint8_t *src_left,
                                          uint8_t *dst, int32_t stride,
                                          uint8_t offset)
{
    v16i8 src0, src1;
    v8i16 src0_r, src1_r, src0_l, src1_l;
    v8i16 vec0, vec1, res0, res1;
    v8i16 tmp0, tmp1;
    v8i16 mul_val2, mul_val3;
    v8i16 mul_val1 = { 1, 2, 3, 4, 5, 6, 7, 8 };
    v8i16 mul_val0 = { 31, 30, 29, 28, 27, 26, 25, 24 };

    tmp0 = __msa_fill_h(src_top[32 - offset]);
    tmp1 = __msa_fill_h(src_left[32]);

    src0 = LD_SB(src_top);
    src1 = LD_SB(src_left);

    UNPCK_UB_SH(src0, src0_r, src0_l);
    UNPCK_UB_SH(src1, src1_r, src1_l);

    mul_val1 += offset;
    mul_val0 -= offset;
    mul_val2 = mul_val0 - 8;
    mul_val3 = mul_val1 + 8;

    SPLATI_H2_SH(src1_r, 0, 1, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 31, 1, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 2, 3, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 29, 3, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 4, 5, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 27, 5, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 6, 7, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 25, 7, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 0, 1, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 23, 9, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 2, 3, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 21, 11, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 4, 5, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 19, 13, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 6, 7, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 17, 15, 6);
    ST_SH2(res0, res1, dst, stride);
}

static void process_intra_lower_16x16_msa(const uint8_t *src_top,
                                          const uint8_t *src_left,
                                          uint8_t *dst, int32_t stride,
                                          uint8_t offset)
{
    v16i8 src0, src1;
    v8i16 src0_r, src1_r, src0_l, src1_l;
    v8i16 vec0, vec1, res0, res1, tmp0, tmp1;
    v8i16 mul_val2, mul_val3;
    v8i16 mul_val1 = { 1, 2, 3, 4, 5, 6, 7, 8 };
    v8i16 mul_val0 = { 31, 30, 29, 28, 27, 26, 25, 24 };

    tmp0 = __msa_fill_h(src_top[32 - offset]);
    tmp1 = __msa_fill_h(src_left[16]);

    src0 = LD_SB(src_top);
    src1 = LD_SB(src_left);

    UNPCK_UB_SH(src0, src0_r, src0_l);
    UNPCK_UB_SH(src1, src1_r, src1_l);

    mul_val1 += offset;
    mul_val0 -= offset;
    mul_val2 = mul_val0 - 8;
    mul_val3 = mul_val1 + 8;

    SPLATI_H2_SH(src1_r, 0, 1, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 15, 17, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 2, 3, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 13, 19, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 4, 5, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 11, 21, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_r, 6, 7, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 9, 23, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 0, 1, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 7, 25, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 2, 3, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 5, 27, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 4, 5, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 3, 29, 6);
    ST_SH2(res0, res1, dst, stride);
    dst += (2 * stride);

    SPLATI_H2_SH(src1_l, 6, 7, vec0, vec1);
    HEVC_PRED_PLANAR_16x2(src0_r, src0_l, tmp0, tmp1, vec0, vec1,
                          mul_val0, mul_val1, mul_val2, mul_val3,
                          res0, res1, 1, 31, 6);
    ST_SH2(res0, res1, dst, stride);
}

static void hevc_intra_pred_plane_32x32_msa(const uint8_t *src_top,
                                            const uint8_t *src_left,
                                            uint8_t *dst, int32_t stride)
{
    process_intra_upper_16x16_msa(src_top, src_left, dst, stride, 0);
    process_intra_upper_16x16_msa((src_top + 16), src_left,
                                  (dst + 16), stride, 16);
    dst += (16 * stride);
    src_left += 16;

    process_intra_lower_16x16_msa(src_top, src_left, dst, stride, 0);
    process_intra_lower_16x16_msa((src_top + 16), src_left,
                                  (dst + 16), stride, 16);
}

static void hevc_intra_pred_angular_upper_4width_msa(const uint8_t *src_top,
                                                     const uint8_t *src_left,
                                                     uint8_t *dst,
                                                     int32_t stride,
                                                     int32_t mode)
{
    int16_t inv_angle[] = { -256, -315, -390, -482, -630, -910, -1638, -4096 };
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 4;
    const uint8_t *ref;
    int32_t last;
    int32_t h_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t idx2, fact_val2, idx3, fact_val3;
    int32_t angle, angle_loop;
    int32_t inv_angle_val, offset;
    uint64_t tmp0;
    v16i8 top0, top1, top2, top3;
    v16i8 dst_val0;
    v16i8 zero = { 0 };
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 fact0, fact1, fact2, fact3, fact4, fact5, fact6, fact7;

    angle = intra_pred_angle_up[mode - 18];
    inv_angle_val = inv_angle[mode - 18];
    last = (angle) >> 3;
    angle_loop = angle;

    ref = src_top - 1;
    if (angle < 0 && last < -1) {
        inv_angle_val = inv_angle[mode - 18];

        tmp0 = LD(ref);
        SD(tmp0, ref_tmp);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = -1 + ((h_cnt * inv_angle_val + 128) >> 8);
            ref_tmp[h_cnt] = src_left[offset];
        }

        ref = ref_tmp;
    }

    idx0 = angle_loop >> 5;
    fact_val0 = angle_loop & 31;
    angle_loop += angle;

    idx1 = angle_loop >> 5;
    fact_val1 = angle_loop & 31;
    angle_loop += angle;

    idx2 = angle_loop >> 5;
    fact_val2 = angle_loop & 31;
    angle_loop += angle;

    idx3 = angle_loop >> 5;
    fact_val3 = angle_loop & 31;

    top0 = LD_SB(ref + idx0 + 1);
    top1 = LD_SB(ref + idx1 + 1);
    top2 = LD_SB(ref + idx2 + 1);
    top3 = LD_SB(ref + idx3 + 1);

    fact0 = __msa_fill_h(fact_val0);
    fact1 = __msa_fill_h(32 - fact_val0);

    fact2 = __msa_fill_h(fact_val1);
    fact3 = __msa_fill_h(32 - fact_val1);

    fact4 = __msa_fill_h(fact_val2);
    fact5 = __msa_fill_h(32 - fact_val2);

    fact6 = __msa_fill_h(fact_val3);
    fact7 = __msa_fill_h(32 - fact_val3);

    ILVR_D2_SH(fact2, fact0, fact6, fact4, fact0, fact2);
    ILVR_D2_SH(fact3, fact1, fact7, fact5, fact1, fact3);
    ILVR_B4_SH(zero, top0, zero, top1, zero, top2, zero, top3,
               diff0, diff2, diff4, diff6);
    SLDI_B4_SH(zero, diff0, zero, diff2, zero, diff4, zero, diff6, 2,
               diff1, diff3, diff5, diff7);
    ILVR_D2_SH(diff2, diff0, diff6, diff4, diff0, diff2);
    ILVR_D2_SH(diff3, diff1, diff7, diff5, diff1, diff3);
    MUL2(diff1, fact0, diff3, fact2, diff1, diff3);

    diff1 += diff0 * fact1;
    diff3 += diff2 * fact3;

    SRARI_H2_SH(diff1, diff3, 5);
    dst_val0 = __msa_pckev_b((v16i8) diff3, (v16i8) diff1);
    ST_W4(dst_val0, 0, 1, 2, 3, dst, stride);
}

static void hevc_intra_pred_angular_upper_8width_msa(const uint8_t *src_top,
                                                     const uint8_t *src_left,
                                                     uint8_t *dst,
                                                     int32_t stride,
                                                     int32_t mode)
{
    int16_t inv_angle[] = { -256, -315, -390, -482, -630, -910, -1638, -4096 };
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 8;
    const uint8_t *ref;
    const uint8_t *src_left_tmp = src_left - 1;
    int32_t last, offset;
    int32_t h_cnt, v_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t idx2, fact_val2, idx3, fact_val3;
    int32_t angle, angle_loop;
    int32_t inv_angle_val, inv_angle_val_loop;
    int32_t tmp0, tmp1, tmp2;
    v16i8 top0, top1, top2, top3;
    v16u8 dst_val0, dst_val1;
    v8i16 fact0, fact1, fact2, fact3, fact4, fact5, fact6, fact7;
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;

    angle = intra_pred_angle_up[mode - 18];
    inv_angle_val = inv_angle[mode - 18];
    last = (angle) >> 2;
    angle_loop = angle;

    ref = src_top - 1;
    if (last < -1) {
        inv_angle_val_loop = inv_angle_val * last;

        tmp0 = LW(ref);
        tmp1 = LW(ref + 4);
        tmp2 = LW(ref + 8);
        SW(tmp0, ref_tmp);
        SW(tmp1, ref_tmp + 4);
        SW(tmp2, ref_tmp + 8);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = (inv_angle_val_loop + 128) >> 8;
            ref_tmp[h_cnt] = src_left_tmp[offset];
            inv_angle_val_loop += inv_angle_val;
        }
        ref = ref_tmp;
    }

    for (v_cnt = 0; v_cnt < 2; v_cnt++) {
        idx0 = (angle_loop) >> 5;
        fact_val0 = (angle_loop) & 31;
        angle_loop += angle;

        idx1 = (angle_loop) >> 5;
        fact_val1 = (angle_loop) & 31;
        angle_loop += angle;

        idx2 = (angle_loop) >> 5;
        fact_val2 = (angle_loop) & 31;
        angle_loop += angle;

        idx3 = (angle_loop) >> 5;
        fact_val3 = (angle_loop) & 31;
        angle_loop += angle;

        top0 = LD_SB(ref + idx0 + 1);
        top1 = LD_SB(ref + idx1 + 1);
        top2 = LD_SB(ref + idx2 + 1);
        top3 = LD_SB(ref + idx3 + 1);

        fact0 = __msa_fill_h(fact_val0);
        fact1 = __msa_fill_h(32 - fact_val0);
        fact2 = __msa_fill_h(fact_val1);
        fact3 = __msa_fill_h(32 - fact_val1);
        fact4 = __msa_fill_h(fact_val2);
        fact5 = __msa_fill_h(32 - fact_val2);
        fact6 = __msa_fill_h(fact_val3);
        fact7 = __msa_fill_h(32 - fact_val3);

        UNPCK_UB_SH(top0, diff0, diff1);
        UNPCK_UB_SH(top1, diff2, diff3);
        UNPCK_UB_SH(top2, diff4, diff5);
        UNPCK_UB_SH(top3, diff6, diff7);

        SLDI_B4_SH(diff1, diff0, diff3, diff2, diff5, diff4, diff7, diff6, 2,
                   diff1, diff3, diff5, diff7);
        MUL4(diff1, fact0, diff3, fact2, diff5, fact4, diff7, fact6,
             diff1, diff3, diff5, diff7);

        diff1 += diff0 * fact1;
        diff3 += diff2 * fact3;
        diff5 += diff4 * fact5;
        diff7 += diff6 * fact7;

        SRARI_H4_SH(diff1, diff3, diff5, diff7, 5);
        PCKEV_B2_UB(diff3, diff1, diff7, diff5, dst_val0, dst_val1);
        ST_D4(dst_val0, dst_val1, 0, 1, 0, 1, dst, stride);
        dst += (4 * stride);
    }
}

static void hevc_intra_pred_angular_upper_16width_msa(const uint8_t *src_top,
                                                      const uint8_t *src_left,
                                                      uint8_t *dst,
                                                      int32_t stride,
                                                      int32_t mode)
{
    int16_t inv_angle[] = { -256, -315, -390, -482, -630, -910, -1638, -4096 };
    int32_t h_cnt, v_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t idx2, fact_val2, idx3, fact_val3;
    int32_t tmp0;
    int32_t angle, angle_loop, offset;
    int32_t inv_angle_val, inv_angle_val_loop;
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 16;
    const uint8_t *ref;
    const uint8_t *src_left_tmp = src_left - 1;
    int32_t last;
    v16u8 top0, top1, top2, top3, top4, top5, top6, top7;
    v16i8 dst0, dst1, dst2, dst3;
    v8i16 fact0, fact1, fact2, fact3, fact4, fact5, fact6, fact7;
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 diff8, diff9, diff10, diff11, diff12, diff13, diff14, diff15;

    angle = intra_pred_angle_up[mode - 18];
    inv_angle_val = inv_angle[mode - 18];
    last = angle >> 1;
    angle_loop = angle;

    ref = src_top - 1;
    if (last < -1) {
        inv_angle_val_loop = inv_angle_val * last;

        top0 = LD_UB(ref);
        tmp0 = LW(ref + 16);
        ST_UB(top0, ref_tmp);
        SW(tmp0, ref_tmp + 16);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = (inv_angle_val_loop + 128) >> 8;
            ref_tmp[h_cnt] = src_left_tmp[offset];
            inv_angle_val_loop += inv_angle_val;
        }
        ref = ref_tmp;
    }

    for (v_cnt = 4; v_cnt--;) {
        idx0 = (angle_loop) >> 5;
        fact_val0 = (angle_loop) & 31;
        angle_loop += angle;

        idx1 = (angle_loop) >> 5;
        fact_val1 = (angle_loop) & 31;
        angle_loop += angle;

        idx2 = (angle_loop) >> 5;
        fact_val2 = (angle_loop) & 31;
        angle_loop += angle;

        idx3 = (angle_loop) >> 5;
        fact_val3 = (angle_loop) & 31;
        angle_loop += angle;

        LD_UB2(ref + idx0 + 1, 16, top0, top1);
        LD_UB2(ref + idx1 + 1, 16, top2, top3);
        LD_UB2(ref + idx2 + 1, 16, top4, top5);
        LD_UB2(ref + idx3 + 1, 16, top6, top7);

        fact0 = __msa_fill_h(fact_val0);
        fact1 = __msa_fill_h(32 - fact_val0);
        fact2 = __msa_fill_h(fact_val1);
        fact3 = __msa_fill_h(32 - fact_val1);
        fact4 = __msa_fill_h(fact_val2);
        fact5 = __msa_fill_h(32 - fact_val2);
        fact6 = __msa_fill_h(fact_val3);
        fact7 = __msa_fill_h(32 - fact_val3);

        SLDI_B4_UB(top1, top0, top3, top2, top5, top4, top7, top6, 1,
                   top1, top3, top5, top7);
        UNPCK_UB_SH(top0, diff0, diff1);
        UNPCK_UB_SH(top1, diff2, diff3);
        UNPCK_UB_SH(top2, diff4, diff5);
        UNPCK_UB_SH(top3, diff6, diff7);
        UNPCK_UB_SH(top4, diff8, diff9);
        UNPCK_UB_SH(top5, diff10, diff11);
        UNPCK_UB_SH(top6, diff12, diff13);
        UNPCK_UB_SH(top7, diff14, diff15);

        MUL4(diff2, fact0, diff3, fact0, diff6, fact2, diff7, fact2,
             diff2, diff3, diff6, diff7);
        MUL4(diff10, fact4, diff11, fact4, diff14, fact6, diff15, fact6,
             diff10, diff11, diff14, diff15);

        diff2 += diff0 * fact1;
        diff3 += diff1 * fact1;
        diff6 += diff4 * fact3;
        diff7 += diff5 * fact3;
        diff10 += diff8 * fact5;
        diff11 += diff9 * fact5;
        diff14 += diff12 * fact7;
        diff15 += diff13 * fact7;

        SRARI_H4_SH(diff2, diff3, diff6, diff7, 5);
        SRARI_H4_SH(diff10, diff11, diff14, diff15, 5);
        PCKEV_B4_SB(diff3, diff2, diff7, diff6, diff11, diff10, diff15, diff14,
                    dst0, dst1, dst2, dst3);
        ST_SB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }
}

static void hevc_intra_pred_angular_upper_32width_msa(const uint8_t *src_top,
                                                      const uint8_t *src_left,
                                                      uint8_t *dst,
                                                      int32_t stride,
                                                      int32_t mode)
{
    int16_t inv_angle[] = { -256, -315, -390, -482, -630, -910, -1638, -4096 };
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp;
    const uint8_t *ref;
    const uint8_t *src_left_tmp = src_left - 1;
    int32_t h_cnt, v_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t tmp0, tmp1, tmp2, tmp3;
    int32_t angle, angle_loop;
    int32_t inv_angle_val, inv_angle_val_loop;
    int32_t last, offset;
    v16u8 top0, top1, top2, top3, top4, top5, top6, top7;
    v16i8 dst0, dst1, dst2, dst3;
    v8i16 fact0, fact1, fact2, fact3;
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 diff8, diff9, diff10, diff11, diff12, diff13, diff14, diff15;

    ref_tmp = ref_array + 32;

    angle = intra_pred_angle_up[mode - 18];
    inv_angle_val = inv_angle[mode - 18];
    last = angle;
    angle_loop = angle;

    ref = src_top - 1;
    if (last < -1) {
        inv_angle_val_loop = inv_angle_val * last;
        LD_UB2(ref, 16, top0, top1);
        tmp0 = ref[32];
        tmp1 = ref[33];
        tmp2 = ref[34];
        tmp3 = ref[35];

        ST_UB2(top0, top1, ref_tmp, 16);
        ref_tmp[32] = tmp0;
        ref_tmp[33] = tmp1;
        ref_tmp[34] = tmp2;
        ref_tmp[35] = tmp3;

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = (inv_angle_val_loop + 128) >> 8;
            ref_tmp[h_cnt] = src_left_tmp[offset];
            inv_angle_val_loop += inv_angle_val;
        }

        ref = ref_tmp;
    }

    for (v_cnt = 16; v_cnt--;) {
        idx0 = (angle_loop) >> 5;
        fact_val0 = (angle_loop) & 31;
        angle_loop += angle;

        idx1 = (angle_loop) >> 5;
        fact_val1 = (angle_loop) & 31;
        angle_loop += angle;

        top0 = LD_UB(ref + idx0 + 1);
        top4 = LD_UB(ref + idx1 + 1);
        top1 = LD_UB(ref + idx0 + 17);
        top5 = LD_UB(ref + idx1 + 17);
        top3 = LD_UB(ref + idx0 + 33);
        top7 = LD_UB(ref + idx1 + 33);

        fact0 = __msa_fill_h(fact_val0);
        fact1 = __msa_fill_h(32 - fact_val0);
        fact2 = __msa_fill_h(fact_val1);
        fact3 = __msa_fill_h(32 - fact_val1);

        top2 = top1;
        top6 = top5;

        SLDI_B4_UB(top1, top0, top3, top2, top5, top4, top7, top6, 1,
                   top1, top3, top5, top7);
        UNPCK_UB_SH(top0, diff0, diff1);
        UNPCK_UB_SH(top1, diff2, diff3);
        UNPCK_UB_SH(top2, diff4, diff5);
        UNPCK_UB_SH(top3, diff6, diff7);
        UNPCK_UB_SH(top4, diff8, diff9);
        UNPCK_UB_SH(top5, diff10, diff11);
        UNPCK_UB_SH(top6, diff12, diff13);
        UNPCK_UB_SH(top7, diff14, diff15);

        MUL4(diff2, fact0, diff3, fact0, diff6, fact0, diff7, fact0,
             diff2, diff3, diff6, diff7);
        MUL4(diff10, fact2, diff11, fact2, diff14, fact2, diff15, fact2,
             diff10, diff11, diff14, diff15);

        diff2 += diff0 * fact1;
        diff3 += diff1 * fact1;
        diff6 += diff4 * fact1;
        diff7 += diff5 * fact1;
        diff10 += diff8 * fact3;
        diff11 += diff9 * fact3;
        diff14 += diff12 * fact3;
        diff15 += diff13 * fact3;

        SRARI_H4_SH(diff2, diff3, diff6, diff7, 5);
        SRARI_H4_SH(diff10, diff11, diff14, diff15, 5);
        PCKEV_B4_SB(diff3, diff2, diff7, diff6, diff11, diff10, diff15, diff14,
                    dst0, dst1, dst2, dst3);

        ST_SB2(dst0, dst1, dst, 16);
        dst += stride;
        ST_SB2(dst2, dst3, dst, 16);
        dst += stride;
    }
}

static void hevc_intra_pred_angular_lower_4width_msa(const uint8_t *src_top,
                                                     const uint8_t *src_left,
                                                     uint8_t *dst,
                                                     int32_t stride,
                                                     int32_t mode)
{
    int16_t inv_angle[] = { -4096, -1638, -910, -630, -482, -390, -315 };
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 4;
    const uint8_t *ref;
    int32_t last, offset;
    int32_t h_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t idx2, fact_val2, idx3, fact_val3;
    int32_t angle, angle_loop, inv_angle_val;
    uint64_t tmp0;
    v16i8 dst_val0, dst_val1;
    v16u8 top0, top1, top2, top3;
    v16u8 zero = { 0 };
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 fact0, fact1, fact2, fact3, fact4, fact5, fact6, fact7;

    angle = intra_pred_angle_low[mode - 2];
    last = angle >> 3;
    angle_loop = angle;

    ref = src_left - 1;
    if (last < -1) {
        inv_angle_val = inv_angle[mode - 11];

        tmp0 = LD(ref);
        SD(tmp0, ref_tmp);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = -1 + ((h_cnt * inv_angle_val + 128) >> 8);
            ref_tmp[h_cnt] = src_top[offset];
        }

        ref = ref_tmp;
    }

    idx0 = angle_loop >> 5;
    fact_val0 = angle_loop & 31;
    angle_loop += angle;

    idx1 = angle_loop >> 5;
    fact_val1 = angle_loop & 31;
    angle_loop += angle;

    idx2 = angle_loop >> 5;
    fact_val2 = angle_loop & 31;
    angle_loop += angle;

    idx3 = angle_loop >> 5;
    fact_val3 = angle_loop & 31;

    top0 = LD_UB(ref + idx0 + 1);
    top1 = LD_UB(ref + idx1 + 1);
    top2 = LD_UB(ref + idx2 + 1);
    top3 = LD_UB(ref + idx3 + 1);

    fact0 = __msa_fill_h(fact_val0);
    fact1 = __msa_fill_h(32 - fact_val0);
    fact2 = __msa_fill_h(fact_val1);
    fact3 = __msa_fill_h(32 - fact_val1);
    fact4 = __msa_fill_h(fact_val2);
    fact5 = __msa_fill_h(32 - fact_val2);
    fact6 = __msa_fill_h(fact_val3);
    fact7 = __msa_fill_h(32 - fact_val3);

    ILVR_D2_SH(fact2, fact0, fact6, fact4, fact0, fact2);
    ILVR_D2_SH(fact3, fact1, fact7, fact5, fact1, fact3);
    ILVR_B4_SH(zero, top0, zero, top1, zero, top2, zero, top3,
               diff0, diff2, diff4, diff6);
    SLDI_B4_SH(zero, diff0, zero, diff2, zero, diff4, zero, diff6, 2,
               diff1, diff3, diff5, diff7);
    ILVR_D2_SH(diff2, diff0, diff6, diff4, diff0, diff2);
    ILVR_D2_SH(diff3, diff1, diff7, diff5, diff1, diff3);
    MUL2(diff1, fact0, diff3, fact2, diff1, diff3);

    diff1 += diff0 * fact1;
    diff3 += diff2 * fact3;

    SRARI_H2_SH(diff1, diff3, 5);
    PCKEV_B2_SB(diff1, diff1, diff3, diff3, dst_val0, dst_val1);

    diff0 = (v8i16) __msa_pckev_b(dst_val1, dst_val0);
    diff1 = (v8i16) __msa_pckod_b(dst_val1, dst_val0);

    diff2 = (v8i16) __msa_pckev_w((v4i32) diff1, (v4i32) diff0);

    dst_val0 = __msa_pckev_b((v16i8) diff2, (v16i8) diff2);
    dst_val1 = __msa_pckod_b((v16i8) diff2, (v16i8) diff2);

    ST_W2(dst_val0, 0, 1, dst, stride);
    ST_W2(dst_val1, 0, 1, dst + 2 * stride, stride);
}

static void hevc_intra_pred_angular_lower_8width_msa(const uint8_t *src_top,
                                                     const uint8_t *src_left,
                                                     uint8_t *dst,
                                                     int32_t stride,
                                                     int32_t mode)
{
    int16_t inv_angle[] = { -4096, -1638, -910, -630, -482, -390, -315 };
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 8;
    const uint8_t *ref;
    const uint8_t *src_top_tmp = src_top - 1;
    uint8_t *dst_org;
    int32_t last, offset, tmp0, tmp1, tmp2;
    int32_t h_cnt, v_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t idx2, fact_val2, idx3, fact_val3;
    int32_t angle, angle_loop, inv_angle_val;
    v16i8 top0, top1, top2, top3;
    v16i8 dst_val0, dst_val1, dst_val2, dst_val3;
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 fact0, fact1, fact2, fact3, fact4, fact5, fact6, fact7;

    angle = intra_pred_angle_low[mode - 2];
    last = (angle) >> 2;
    angle_loop = angle;

    ref = src_left - 1;
    if (last < -1) {
        inv_angle_val = inv_angle[mode - 11];

        tmp0 = LW(ref);
        tmp1 = LW(ref + 4);
        tmp2 = LW(ref + 8);
        SW(tmp0, ref_tmp);
        SW(tmp1, ref_tmp + 4);
        SW(tmp2, ref_tmp + 8);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = (h_cnt * inv_angle_val + 128) >> 8;
            ref_tmp[h_cnt] = src_top_tmp[offset];
        }

        ref = ref_tmp;
    }

    for (v_cnt = 0; v_cnt < 2; v_cnt++) {
        dst_org = dst;

        idx0 = angle_loop >> 5;
        fact_val0 = angle_loop & 31;
        angle_loop += angle;

        idx1 = angle_loop >> 5;
        fact_val1 = angle_loop & 31;
        angle_loop += angle;

        idx2 = angle_loop >> 5;
        fact_val2 = angle_loop & 31;
        angle_loop += angle;

        idx3 = angle_loop >> 5;
        fact_val3 = angle_loop & 31;
        angle_loop += angle;

        top0 = LD_SB(ref + idx0 + 1);
        top1 = LD_SB(ref + idx1 + 1);
        top2 = LD_SB(ref + idx2 + 1);
        top3 = LD_SB(ref + idx3 + 1);

        fact0 = __msa_fill_h(fact_val0);
        fact1 = __msa_fill_h(32 - fact_val0);
        fact2 = __msa_fill_h(fact_val1);
        fact3 = __msa_fill_h(32 - fact_val1);
        fact4 = __msa_fill_h(fact_val2);
        fact5 = __msa_fill_h(32 - fact_val2);
        fact6 = __msa_fill_h(fact_val3);
        fact7 = __msa_fill_h(32 - fact_val3);

        UNPCK_UB_SH(top0, diff0, diff1);
        UNPCK_UB_SH(top1, diff2, diff3);
        UNPCK_UB_SH(top2, diff4, diff5);
        UNPCK_UB_SH(top3, diff6, diff7);
        SLDI_B4_SH(diff1, diff0, diff3, diff2, diff5, diff4, diff7, diff6, 2,
                   diff1, diff3, diff5, diff7);
        MUL4(diff1, fact0, diff3, fact2, diff5, fact4, diff7, fact6,
             diff1, diff3, diff5, diff7);

        diff1 += diff0 * fact1;
        diff3 += diff2 * fact3;
        diff5 += diff4 * fact5;
        diff7 += diff6 * fact7;

        SRARI_H4_SH(diff1, diff3, diff5, diff7, 5);
        PCKEV_B4_SB(diff1, diff1, diff3, diff3, diff5, diff5, diff7, diff7,
                    dst_val0, dst_val1, dst_val2, dst_val3);
        ILVR_B2_SH(dst_val1, dst_val0, dst_val3, dst_val2, diff0, diff1);
        ILVRL_H2_SH(diff1, diff0, diff3, diff4);
        ST_W8(diff3, diff4, 0, 1, 2, 3, 0, 1, 2, 3, dst_org, stride);
        dst += 4;
    }
}

static void hevc_intra_pred_angular_lower_16width_msa(const uint8_t *src_top,
                                                      const uint8_t *src_left,
                                                      uint8_t *dst,
                                                      int32_t stride,
                                                      int32_t mode)
{
    int16_t inv_angle[] = { -4096, -1638, -910, -630, -482, -390, -315 };
    int32_t h_cnt, v_cnt, idx0, fact_val0, idx1, fact_val1;
    int32_t idx2, fact_val2, idx3, fact_val3, tmp0;
    v16i8 top0, top1, dst_val0, top2, top3, dst_val1;
    v16i8 top4, top5, dst_val2, top6, top7, dst_val3;
    v8i16 fact0, fact1, fact2, fact3, fact4, fact5, fact6, fact7;
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 diff8, diff9, diff10, diff11, diff12, diff13, diff14, diff15;
    int32_t angle, angle_loop, inv_angle_val, offset;
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 16;
    const uint8_t *ref, *src_top_tmp = src_top - 1;
    uint8_t *dst_org;
    int32_t last;

    angle = intra_pred_angle_low[mode - 2];
    last = (angle) >> 1;
    angle_loop = angle;

    ref = src_left - 1;
    if (last < -1) {
        inv_angle_val = inv_angle[mode - 11];

        top0 = LD_SB(ref);
        tmp0 = LW(ref + 16);
        ST_SB(top0, ref_tmp);
        SW(tmp0, ref_tmp + 16);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = (h_cnt * inv_angle_val + 128) >> 8;
            ref_tmp[h_cnt] = src_top_tmp[offset];
        }

        ref = ref_tmp;
    }

    for (v_cnt = 0; v_cnt < 4; v_cnt++) {
        dst_org = dst;

        idx0 = angle_loop >> 5;
        fact_val0 = angle_loop & 31;
        angle_loop += angle;

        idx1 = angle_loop >> 5;
        fact_val1 = angle_loop & 31;
        angle_loop += angle;

        idx2 = angle_loop >> 5;
        fact_val2 = angle_loop & 31;
        angle_loop += angle;

        idx3 = angle_loop >> 5;
        fact_val3 = angle_loop & 31;
        angle_loop += angle;

        LD_SB2(ref + idx0 + 1, 16, top0, top1);
        LD_SB2(ref + idx1 + 1, 16, top2, top3);
        LD_SB2(ref + idx2 + 1, 16, top4, top5);
        LD_SB2(ref + idx3 + 1, 16, top6, top7);

        fact0 = __msa_fill_h(fact_val0);
        fact1 = __msa_fill_h(32 - fact_val0);
        fact2 = __msa_fill_h(fact_val1);
        fact3 = __msa_fill_h(32 - fact_val1);
        fact4 = __msa_fill_h(fact_val2);
        fact5 = __msa_fill_h(32 - fact_val2);
        fact6 = __msa_fill_h(fact_val3);
        fact7 = __msa_fill_h(32 - fact_val3);

        SLDI_B4_SB(top1, top0, top3, top2, top5, top4, top7, top6, 1,
                   top1, top3, top5, top7);

        UNPCK_UB_SH(top0, diff0, diff1);
        UNPCK_UB_SH(top1, diff2, diff3);
        UNPCK_UB_SH(top2, diff4, diff5);
        UNPCK_UB_SH(top3, diff6, diff7);
        UNPCK_UB_SH(top4, diff8, diff9);
        UNPCK_UB_SH(top5, diff10, diff11);
        UNPCK_UB_SH(top6, diff12, diff13);
        UNPCK_UB_SH(top7, diff14, diff15);

        MUL4(diff2, fact0, diff3, fact0, diff6, fact2, diff7, fact2,
             diff2, diff3, diff6, diff7);
        MUL4(diff10, fact4, diff11, fact4, diff14, fact6, diff15, fact6,
             diff10, diff11, diff14, diff15);

        diff2 += diff0 * fact1;
        diff3 += diff1 * fact1;
        diff6 += diff4 * fact3;
        diff7 += diff5 * fact3;
        diff10 += diff8 * fact5;
        diff11 += diff9 * fact5;
        diff14 += diff12 * fact7;
        diff15 += diff13 * fact7;

        SRARI_H4_SH(diff2, diff3, diff6, diff7, 5);
        SRARI_H4_SH(diff10, diff11, diff14, diff15, 5);
        PCKEV_B4_SB(diff3, diff2, diff7, diff6, diff11, diff10, diff15, diff14,
                    dst_val0, dst_val1, dst_val2, dst_val3);
        ILVR_B2_SH(dst_val1, dst_val0, dst_val3, dst_val2, diff0, diff1);
        ILVL_B2_SH(dst_val1, dst_val0, dst_val3, dst_val2, diff2, diff3);
        ILVRL_H2_SH(diff1, diff0, diff4, diff5);
        ILVRL_H2_SH(diff3, diff2, diff6, diff7);
        ST_W8(diff4, diff5, 0, 1, 2, 3, 0, 1, 2, 3, dst_org, stride);
        dst_org += (8 * stride);
        ST_W8(diff6, diff7, 0, 1, 2, 3, 0, 1, 2, 3, dst_org, stride);
        dst += 4;
    }
}

static void hevc_intra_pred_angular_lower_32width_msa(const uint8_t *src_top,
                                                      const uint8_t *src_left,
                                                      uint8_t *dst,
                                                      int32_t stride,
                                                      int32_t mode)
{
    int16_t inv_angle[] = { -4096, -1638, -910, -630, -482, -390, -315 };
    int32_t h_cnt, v_cnt, idx0, fact_val0, idx1, fact_val1, tmp0;
    v16i8 top0, top1, dst_val0, top2, top3, dst_val1;
    v16i8 top4, top5, dst_val2, top6, top7, dst_val3;
    v8i16 fact0, fact1, fact2, fact3;
    v8i16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8i16 diff8, diff9, diff10, diff11, diff12, diff13, diff14, diff15;
    int32_t angle, angle_loop, inv_angle_val, offset;
    uint8_t ref_array[3 * 32 + 4];
    uint8_t *ref_tmp = ref_array + 32;
    const uint8_t *ref, *src_top_tmp = src_top - 1;
    uint8_t *dst_org;
    int32_t last;

    angle = intra_pred_angle_low[mode - 2];
    last = angle;
    angle_loop = angle;

    ref = src_left - 1;
    if (last < -1) {
        inv_angle_val = inv_angle[mode - 11];

        LD_SB2(ref, 16, top0, top1);
        tmp0 = LW(ref + 32);
        ST_SB2(top0, top1, ref_tmp, 16);
        SW(tmp0, ref_tmp + 32);

        for (h_cnt = last; h_cnt <= -1; h_cnt++) {
            offset = (h_cnt * inv_angle_val + 128) >> 8;
            ref_tmp[h_cnt] = src_top_tmp[offset];
        }

        ref = ref_tmp;
    }

    for (v_cnt = 0; v_cnt < 16; v_cnt++) {
        dst_org = dst;
        idx0 = angle_loop >> 5;
        fact_val0 = angle_loop & 31;
        angle_loop += angle;

        idx1 = angle_loop >> 5;
        fact_val1 = angle_loop & 31;
        angle_loop += angle;

        top0 = LD_SB(ref + idx0 + 1);
        top4 = LD_SB(ref + idx1 + 1);
        top1 = LD_SB(ref + idx0 + 17);
        top5 = LD_SB(ref + idx1 + 17);
        top3 = LD_SB(ref + idx0 + 33);
        top7 = LD_SB(ref + idx1 + 33);

        fact0 = __msa_fill_h(fact_val0);
        fact1 = __msa_fill_h(32 - fact_val0);
        fact2 = __msa_fill_h(fact_val1);
        fact3 = __msa_fill_h(32 - fact_val1);

        top2 = top1;
        top6 = top5;

        SLDI_B4_SB(top1, top0, top3, top2, top5, top4, top7, top6, 1,
                   top1, top3, top5, top7);

        UNPCK_UB_SH(top0, diff0, diff1);
        UNPCK_UB_SH(top1, diff2, diff3);
        UNPCK_UB_SH(top2, diff4, diff5);
        UNPCK_UB_SH(top3, diff6, diff7);
        UNPCK_UB_SH(top4, diff8, diff9);
        UNPCK_UB_SH(top5, diff10, diff11);
        UNPCK_UB_SH(top6, diff12, diff13);
        UNPCK_UB_SH(top7, diff14, diff15);

        MUL4(diff2, fact0, diff3, fact0, diff6, fact0, diff7, fact0,
             diff2, diff3, diff6, diff7);
        MUL4(diff10, fact2, diff11, fact2, diff14, fact2, diff15, fact2,
             diff10, diff11, diff14, diff15);

        diff2 += diff0 * fact1;
        diff3 += diff1 * fact1;
        diff6 += diff4 * fact1;
        diff7 += diff5 * fact1;
        diff10 += diff8 * fact3;
        diff11 += diff9 * fact3;
        diff14 += diff12 * fact3;
        diff15 += diff13 * fact3;

        SRARI_H4_SH(diff2, diff3, diff6, diff7, 5);
        SRARI_H4_SH(diff10, diff11, diff14, diff15, 5);
        PCKEV_B4_SB(diff3, diff2, diff7, diff6, diff11, diff10, diff15, diff14,
                    dst_val0, dst_val1, dst_val2, dst_val3);
        ILVRL_B2_SH(dst_val2, dst_val0, diff0, diff1);
        ILVRL_B2_SH(dst_val3, dst_val1, diff2, diff3);

        ST_H8(diff0, 0, 1, 2, 3, 4, 5, 6, 7, dst_org, stride)
        dst_org += (8 * stride);
        ST_H8(diff1, 0, 1, 2, 3, 4, 5, 6, 7, dst_org, stride)
        dst_org += (8 * stride);
        ST_H8(diff2, 0, 1, 2, 3, 4, 5, 6, 7, dst_org, stride)
        dst_org += (8 * stride);
        ST_H8(diff3, 0, 1, 2, 3, 4, 5, 6, 7, dst_org, stride)
        dst_org += (8 * stride);

        dst += 2;
    }
}

static void intra_predict_vert_32x32_msa(const uint8_t *src, uint8_t *dst,
                                         int32_t dst_stride)
{
    uint32_t row;
    v16u8 src1, src2;

    src1 = LD_UB(src);
    src2 = LD_UB(src + 16);

    for (row = 32; row--;) {
        ST_UB2(src1, src2, dst, 16);
        dst += dst_stride;
    }
}

void ff_hevc_intra_pred_planar_0_msa(uint8_t *dst,
                                     const uint8_t *src_top,
                                     const uint8_t *src_left,
                                     ptrdiff_t stride)
{
    hevc_intra_pred_plane_4x4_msa(src_top, src_left, dst, stride);
}

void ff_hevc_intra_pred_planar_1_msa(uint8_t *dst,
                                     const uint8_t *src_top,
                                     const uint8_t *src_left,
                                     ptrdiff_t stride)
{
    hevc_intra_pred_plane_8x8_msa(src_top, src_left, dst, stride);
}

void ff_hevc_intra_pred_planar_2_msa(uint8_t *dst,
                                     const uint8_t *src_top,
                                     const uint8_t *src_left,
                                     ptrdiff_t stride)
{
    hevc_intra_pred_plane_16x16_msa(src_top, src_left, dst, stride);
}

void ff_hevc_intra_pred_planar_3_msa(uint8_t *dst,
                                     const uint8_t *src_top,
                                     const uint8_t *src_left,
                                     ptrdiff_t stride)
{
    hevc_intra_pred_plane_32x32_msa(src_top, src_left, dst, stride);
}

void ff_hevc_intra_pred_dc_msa(uint8_t *dst, const uint8_t *src_top,
                               const uint8_t *src_left,
                               ptrdiff_t stride, int log2, int c_idx)
{
    switch (log2) {
    case 2:
        hevc_intra_pred_dc_4x4_msa(src_top, src_left, dst, stride, c_idx);
        break;

    case 3:
        hevc_intra_pred_dc_8x8_msa(src_top, src_left, dst, stride, c_idx);
        break;

    case 4:
        hevc_intra_pred_dc_16x16_msa(src_top, src_left, dst, stride, c_idx);
        break;

    case 5:
        hevc_intra_pred_dc_32x32_msa(src_top, src_left, dst, stride);
        break;
    }
}

void ff_pred_intra_pred_angular_0_msa(uint8_t *dst,
                                      const uint8_t *src_top,
                                      const uint8_t *src_left,
                                      ptrdiff_t stride, int c_idx, int mode)
{
    if (mode == 10) {
        hevc_intra_pred_horiz_4x4_msa(src_top, src_left, dst, stride, c_idx);
    } else if (mode == 26) {
        hevc_intra_pred_vert_4x4_msa(src_top, src_left, dst, stride, c_idx);
    } else if (mode >= 18) {
        hevc_intra_pred_angular_upper_4width_msa(src_top, src_left,
                                                 dst, stride, mode);
    } else {
        hevc_intra_pred_angular_lower_4width_msa(src_top, src_left,
                                                 dst, stride, mode);
    }
}

void ff_pred_intra_pred_angular_1_msa(uint8_t *dst,
                                      const uint8_t *src_top,
                                      const uint8_t *src_left,
                                      ptrdiff_t stride, int c_idx, int mode)
{
    if (mode == 10) {
        hevc_intra_pred_horiz_8x8_msa(src_top, src_left, dst, stride, c_idx);
    } else if (mode == 26) {
        hevc_intra_pred_vert_8x8_msa(src_top, src_left, dst, stride, c_idx);
    } else if (mode >= 18) {
        hevc_intra_pred_angular_upper_8width_msa(src_top, src_left,
                                                 dst, stride, mode);
    } else {
        hevc_intra_pred_angular_lower_8width_msa(src_top, src_left,
                                                 dst, stride, mode);
    }
}

void ff_pred_intra_pred_angular_2_msa(uint8_t *dst,
                                      const uint8_t *src_top,
                                      const uint8_t *src_left,
                                      ptrdiff_t stride, int c_idx, int mode)
{
    if (mode == 10) {
        hevc_intra_pred_horiz_16x16_msa(src_top, src_left, dst, stride, c_idx);
    } else if (mode == 26) {
        hevc_intra_pred_vert_16x16_msa(src_top, src_left, dst, stride, c_idx);
    } else if (mode >= 18) {
        hevc_intra_pred_angular_upper_16width_msa(src_top, src_left,
                                                  dst, stride, mode);
    } else {
        hevc_intra_pred_angular_lower_16width_msa(src_top, src_left,
                                                  dst, stride, mode);
    }
}

void ff_pred_intra_pred_angular_3_msa(uint8_t *dst,
                                      const uint8_t *src_top,
                                      const uint8_t *src_left,
                                      ptrdiff_t stride, int c_idx, int mode)
{
    if (mode == 10) {
        hevc_intra_pred_horiz_32x32_msa(src_top, src_left, dst, stride);
    } else if (mode == 26) {
        intra_predict_vert_32x32_msa(src_top, dst, stride);
    } else if (mode >= 18) {
        hevc_intra_pred_angular_upper_32width_msa(src_top, src_left,
                                                  dst, stride, mode);
    } else {
        hevc_intra_pred_angular_lower_32width_msa(src_top, src_left,
                                                  dst, stride, mode);
    }
}

void ff_intra_pred_8_16x16_msa(HEVCLocalContext *lc, int x0, int y0, int c_idx)
{
    v16u8 vec0;
    const HEVCContext *const s = lc->parent;
    int i;
    int hshift = s->ps.sps->hshift[c_idx];
    int vshift = s->ps.sps->vshift[c_idx];
    int size_in_luma_h = 16 << hshift;
    int size_in_tbs_h = size_in_luma_h >> s->ps.sps->log2_min_tb_size;
    int size_in_luma_v = 16 << vshift;
    int size_in_tbs_v = size_in_luma_v >> s->ps.sps->log2_min_tb_size;
    int x = x0 >> hshift;
    int y = y0 >> vshift;
    int x_tb = (x0 >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask;
    int y_tb = (y0 >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask;

    int cur_tb_addr =
        s->ps.pps->min_tb_addr_zs[(y_tb) * (s->ps.sps->tb_mask + 2) + (x_tb)];

    ptrdiff_t stride = s->frame->linesize[c_idx] / sizeof(uint8_t);
    uint8_t *src = (uint8_t *) s->frame->data[c_idx] + x + y * stride;

    int min_pu_width = s->ps.sps->min_pu_width;

    enum IntraPredMode mode = c_idx ? lc->tu.intra_pred_mode_c :
        lc->tu.intra_pred_mode;
    uint32_t a;
    uint8_t left_array[2 * 32 + 1];
    uint8_t filtered_left_array[2 * 32 + 1];
    uint8_t top_array[2 * 32 + 1];
    uint8_t filtered_top_array[2 * 32 + 1];

    uint8_t *left = left_array + 1;
    uint8_t *top = top_array + 1;
    uint8_t *filtered_left = filtered_left_array + 1;
    uint8_t *filtered_top = filtered_top_array + 1;
    int cand_bottom_left = lc->na.cand_bottom_left
        && cur_tb_addr >
        s->ps.pps->min_tb_addr_zs[((y_tb + size_in_tbs_v) & s->ps.sps->tb_mask) *
                               (s->ps.sps->tb_mask + 2) + (x_tb - 1)];
    int cand_left = lc->na.cand_left;
    int cand_up_left = lc->na.cand_up_left;
    int cand_up = lc->na.cand_up;
    int cand_up_right = lc->na.cand_up_right
        && cur_tb_addr >
        s->ps.pps->min_tb_addr_zs[(y_tb - 1) * (s->ps.sps->tb_mask + 2) +
                               ((x_tb + size_in_tbs_h) & s->ps.sps->tb_mask)];

    int bottom_left_size =
        (((y0 + 2 * size_in_luma_v) >
          (s->ps.sps->height) ? (s->ps.sps->height) : (y0 +
                                                 2 * size_in_luma_v)) -
         (y0 + size_in_luma_v)) >> vshift;
    int top_right_size =
        (((x0 + 2 * size_in_luma_h) >
          (s->ps.sps->width) ? (s->ps.sps->width) : (x0 + 2 * size_in_luma_h)) -
         (x0 + size_in_luma_h)) >> hshift;

    if (s->ps.pps->constrained_intra_pred_flag == 1) {
        int size_in_luma_pu_v = ((size_in_luma_v) >> s->ps.sps->log2_min_pu_size);
        int size_in_luma_pu_h = ((size_in_luma_h) >> s->ps.sps->log2_min_pu_size);
        int on_pu_edge_x = !(x0 & ((1 << s->ps.sps->log2_min_pu_size) - 1));
        int on_pu_edge_y = !(y0 & ((1 << s->ps.sps->log2_min_pu_size) - 1));
        if (!size_in_luma_pu_h)
            size_in_luma_pu_h++;
        if (cand_bottom_left == 1 && on_pu_edge_x) {
            int x_left_pu = ((x0 - 1) >> s->ps.sps->log2_min_pu_size);
            int y_bottom_pu =
                ((y0 + size_in_luma_v) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_v) >
                 (s->ps.sps->min_pu_height -
                  y_bottom_pu) ? (s->ps.sps->min_pu_height -
                                  y_bottom_pu) : (size_in_luma_pu_v));
            cand_bottom_left = 0;
            for (i = 0; i < max; i += 2)
                cand_bottom_left |=
                    ((s->ref->tab_mvf[(x_left_pu) +
                                      (y_bottom_pu +
                                       i) * min_pu_width]).pred_flag ==
                     PF_INTRA);
        }
        if (cand_left == 1 && on_pu_edge_x) {
            int x_left_pu = ((x0 - 1) >> s->ps.sps->log2_min_pu_size);
            int y_left_pu = ((y0) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_v) >
                 (s->ps.sps->min_pu_height -
                  y_left_pu) ? (s->ps.sps->min_pu_height -
                                y_left_pu) : (size_in_luma_pu_v));
            cand_left = 0;
            for (i = 0; i < max; i += 2)
                cand_left |=
                    ((s->ref->tab_mvf[(x_left_pu) +
                                      (y_left_pu +
                                       i) * min_pu_width]).pred_flag ==
                     PF_INTRA);
        }
        if (cand_up_left == 1) {
            int x_left_pu = ((x0 - 1) >> s->ps.sps->log2_min_pu_size);
            int y_top_pu = ((y0 - 1) >> s->ps.sps->log2_min_pu_size);
            cand_up_left =
                (s->ref->tab_mvf[(x_left_pu) +
                                 (y_top_pu) * min_pu_width]).pred_flag ==
                PF_INTRA;
        }
        if (cand_up == 1 && on_pu_edge_y) {
            int x_top_pu = ((x0) >> s->ps.sps->log2_min_pu_size);
            int y_top_pu = ((y0 - 1) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_h) >
                 (s->ps.sps->min_pu_width -
                  x_top_pu) ? (s->ps.sps->min_pu_width -
                               x_top_pu) : (size_in_luma_pu_h));
            cand_up = 0;
            for (i = 0; i < max; i += 2)
                cand_up |=
                    ((s->ref->tab_mvf[(x_top_pu + i) +
                                      (y_top_pu) *
                                      min_pu_width]).pred_flag == PF_INTRA);
        }
        if (cand_up_right == 1 && on_pu_edge_y) {
            int y_top_pu = ((y0 - 1) >> s->ps.sps->log2_min_pu_size);
            int x_right_pu =
                ((x0 + size_in_luma_h) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_h) >
                 (s->ps.sps->min_pu_width -
                  x_right_pu) ? (s->ps.sps->min_pu_width -
                                 x_right_pu) : (size_in_luma_pu_h));
            cand_up_right = 0;
            for (i = 0; i < max; i += 2)
                cand_up_right |=
                    ((s->ref->tab_mvf[(x_right_pu + i) +
                                      (y_top_pu) *
                                      min_pu_width]).pred_flag == PF_INTRA);
        }

        vec0 = (v16u8) __msa_ldi_b(128);

        ST_UB4(vec0, vec0, vec0, vec0, left, 16);

        ST_UB4(vec0, vec0, vec0, vec0, top, 16);

        top[-1] = 128;
    }
    if (cand_up_left) {
        left[-1] = src[(-1) + stride * (-1)];
        top[-1] = left[-1];
    }
    if (cand_up) {
        vec0 = LD_UB(src - stride);
        ST_UB(vec0, top);
    }
    if (cand_up_right) {
        vec0 = LD_UB(src - stride + 16);
        ST_UB(vec0, (top + 16));

        do {
            uint32_t pix =
                ((src[(16 + top_right_size - 1) + stride * (-1)]) *
                 0x01010101U);
            for (i = 0; i < (16 - top_right_size); i += 4)
                ((((union unaligned_32 *) (top + 16 + top_right_size +
                                           i))->l) = (pix));
        } while (0);
    }
    if (cand_left)
        for (i = 0; i < 16; i++)
            left[i] = src[(-1) + stride * (i)];
    if (cand_bottom_left) {
        for (i = 16; i < 16 + bottom_left_size; i++)
            left[i] = src[(-1) + stride * (i)];
        do {
            uint32_t pix =
                ((src[(-1) + stride * (16 + bottom_left_size - 1)]) *
                 0x01010101U);
            for (i = 0; i < (16 - bottom_left_size); i += 4)
                ((((union unaligned_32 *) (left + 16 + bottom_left_size +
                                           i))->l) = (pix));
        } while (0);
    }

    if (s->ps.pps->constrained_intra_pred_flag == 1) {
        if (cand_bottom_left || cand_left || cand_up_left || cand_up
            || cand_up_right) {
            int size_max_x =
                x0 + ((2 * 16) << hshift) <
                s->ps.sps->width ? 2 * 16 : (s->ps.sps->width - x0) >> hshift;
            int size_max_y =
                y0 + ((2 * 16) << vshift) <
                s->ps.sps->height ? 2 * 16 : (s->ps.sps->height - y0) >> vshift;
            int j = 16 + (cand_bottom_left ? bottom_left_size : 0) - 1;
            if (!cand_up_right) {
                size_max_x = x0 + ((16) << hshift) < s->ps.sps->width ?
                    16 : (s->ps.sps->width - x0) >> hshift;
            }
            if (!cand_bottom_left) {
                size_max_y = y0 + ((16) << vshift) < s->ps.sps->height ?
                    16 : (s->ps.sps->height - y0) >> vshift;
            }
            if (cand_bottom_left || cand_left || cand_up_left) {
                while (j > -1
                       &&
                       !((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((j) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                    j--;
                if (!
                    ((s->ref->tab_mvf[(((x0 +
                                         ((-1) << hshift)) >> s->ps.sps->
                                        log2_min_pu_size)) + (((y0 + ((j)
                                                                      <<
                                                                      vshift))
                                                               >> s->ps.sps->
                                                               log2_min_pu_size))
                                      * min_pu_width]).pred_flag == PF_INTRA)) {
                    j = 0;
                    while (j < size_max_x
                           &&
                           !((s->ref->tab_mvf[(((x0 +
                                                 ((j) << hshift)) >> s->ps.sps->
                                                log2_min_pu_size)) + (((y0 +
                                                                        ((-1) <<
                                                                         vshift))
                                                                       >> s->
                                                                       ps.sps->
                                                                       log2_min_pu_size))
                                              * min_pu_width]).pred_flag ==
                             PF_INTRA))
                        j++;
                    for (i = j; i > (j) - (j + 1); i--)
                        if (!
                            ((s->ref->tab_mvf[(((x0 +
                                                 ((i -
                                                   1) << hshift)) >> s->ps.sps->
                                                log2_min_pu_size)) + (((y0 +
                                                                        ((-1) <<
                                                                         vshift))
                                                                       >> s->
                                                                       ps.sps->
                                                                       log2_min_pu_size))
                                              * min_pu_width]).pred_flag ==
                             PF_INTRA))
                            top[i - 1] = top[i];
                    left[-1] = top[-1];
                }
            } else {
                j = 0;
                while (j < size_max_x
                       &&
                       !((s->ref->tab_mvf[(((x0 +
                                             ((j) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 + ((-1)
                                                                          <<
                                                                          vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                    j++;
                if (j > 0)
                    if (x0 > 0) {
                        for (i = j; i > (j) - (j + 1); i--)
                            if (!
                                ((s->ref->tab_mvf[(((x0 +
                                                     ((i -
                                                       1) << hshift)) >>
                                                    s->ps.sps->log2_min_pu_size))
                                                  + (((y0 + ((-1)
                                                             << vshift))
                                                      >>
                                                      s->ps.sps->log2_min_pu_size))
                                                  *
                                                  min_pu_width]).pred_flag ==
                                 PF_INTRA))
                                top[i - 1] = top[i];
                    } else {
                        for (i = j; i > (j) - (j); i--)
                            if (!
                                ((s->ref->tab_mvf[(((x0 +
                                                     ((i -
                                                       1) << hshift)) >>
                                                    s->ps.sps->log2_min_pu_size))
                                                  + (((y0 + ((-1)
                                                             << vshift))
                                                      >>
                                                      s->ps.sps->log2_min_pu_size))
                                                  *
                                                  min_pu_width]).pred_flag ==
                                 PF_INTRA))
                                top[i - 1] = top[i];
                        top[-1] = top[0];
                    }
                left[-1] = top[-1];
            }
            left[-1] = top[-1];
            if (cand_bottom_left || cand_left) {
                a = ((left[-1]) * 0x01010101U);
                for (i = 0; i < (0) + (size_max_y); i += 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((i) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&left[i]))->l) = (a));
                    else
                        a = ((left[i + 3]) * 0x01010101U);
            }
            if (!cand_left) {
                vec0 = (v16u8) __msa_fill_b(left[-1]);

                ST_UB(vec0, left);
            }
            if (!cand_bottom_left) {

                vec0 = (v16u8) __msa_fill_b(left[15]);

                ST_UB(vec0, (left + 16));
            }
            if (x0 != 0 && y0 != 0) {
                a = ((left[size_max_y - 1]) * 0x01010101U);
                for (i = (size_max_y - 1);
                     i > (size_max_y - 1) - (size_max_y); i -= 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((i -
                                                                      3) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&left[i - 3]))->l) = (a));
                    else
                        a = ((left[i - 3]) * 0x01010101U);
                if (!
                    ((s->ref->tab_mvf[(((x0 +
                                         ((-1) << hshift)) >> s->ps.sps->
                                        log2_min_pu_size)) + (((y0 + ((-1)
                                                                      <<
                                                                      vshift))
                                                               >> s->ps.sps->
                                                               log2_min_pu_size))
                                      * min_pu_width]).pred_flag == PF_INTRA))
                    left[-1] = left[0];
            } else if (x0 == 0) {
                do {
                    uint32_t pix = ((0) * 0x01010101U);
                    for (i = 0; i < (size_max_y); i += 4)
                        ((((union unaligned_32 *) (left + i))->l) = (pix));
                } while (0);
            } else {
                a = ((left[size_max_y - 1]) * 0x01010101U);
                for (i = (size_max_y - 1);
                     i > (size_max_y - 1) - (size_max_y); i -= 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((i -
                                                                      3) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&left[i - 3]))->l) = (a));
                    else
                        a = ((left[i - 3]) * 0x01010101U);
            }
            top[-1] = left[-1];
            if (y0 != 0) {
                a = ((left[-1]) * 0x01010101U);
                for (i = 0; i < (0) + (size_max_x); i += 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((i) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 + ((-1)
                                                                          <<
                                                                          vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&top[i]))->l) = (a));
                    else
                        a = ((top[i + 3]) * 0x01010101U);
            }
        }
    }

    if (!cand_bottom_left) {
        if (cand_left) {
            vec0 = (v16u8) __msa_fill_b(left[15]);

            ST_UB(vec0, (left + 16));

        } else if (cand_up_left) {
            vec0 = (v16u8) __msa_fill_b(left[-1]);

            ST_UB2(vec0, vec0, left, 16);

            cand_left = 1;
        } else if (cand_up) {
            left[-1] = top[0];

            vec0 = (v16u8) __msa_fill_b(left[-1]);

            ST_UB2(vec0, vec0, left, 16);

            cand_up_left = 1;
            cand_left = 1;
        } else if (cand_up_right) {
            vec0 = (v16u8) __msa_fill_b(top[16]);

            ST_UB(vec0, top);

            left[-1] = top[16];

            ST_UB2(vec0, vec0, left, 16);

            cand_up = 1;
            cand_up_left = 1;
            cand_left = 1;
        } else {
            left[-1] = 128;
            vec0 = (v16u8) __msa_ldi_b(128);

            ST_UB2(vec0, vec0, top, 16);
            ST_UB2(vec0, vec0, left, 16);
        }
    }

    if (!cand_left) {
        vec0 = (v16u8) __msa_fill_b(left[16]);
        ST_UB(vec0, left);
    }
    if (!cand_up_left) {
        left[-1] = left[0];
    }
    if (!cand_up) {
        vec0 = (v16u8) __msa_fill_b(left[-1]);
        ST_UB(vec0, top);
    }
    if (!cand_up_right) {
        vec0 = (v16u8) __msa_fill_b(top[15]);
        ST_UB(vec0, (top + 16));
    }

    top[-1] = left[-1];


    if (!s->ps.sps->intra_smoothing_disabled_flag
        && (c_idx == 0 || s->ps.sps->chroma_format_idc == 3)) {
        if (mode != INTRA_DC && 16 != 4) {
            int intra_hor_ver_dist_thresh[] = { 7, 1, 0 };
            int min_dist_vert_hor =
                (((((int) (mode - 26U)) >=
                   0 ? ((int) (mode - 26U)) : (-((int) (mode - 26U))))) >
                 ((((int) (mode - 10U)) >=
                   0 ? ((int) (mode - 10U)) : (-((int) (mode - 10U)))))
                 ? ((((int) (mode - 10U)) >=
                     0 ? ((int) (mode - 10U)) : (-((int) (mode - 10U)))))
                 : ((((int) (mode - 26U)) >=
                     0 ? ((int) (mode - 26U)) : (-((int) (mode - 26U))))));
            if (min_dist_vert_hor > intra_hor_ver_dist_thresh[4 - 3]) {
                filtered_left[2 * 16 - 1] = left[2 * 16 - 1];
                filtered_top[2 * 16 - 1] = top[2 * 16 - 1];
                for (i = 2 * 16 - 2; i >= 0; i--)
                    filtered_left[i] = (left[i + 1] + 2 * left[i] +
                                        left[i - 1] + 2) >> 2;
                filtered_top[-1] =
                    filtered_left[-1] =
                    (left[0] + 2 * left[-1] + top[0] + 2) >> 2;
                for (i = 2 * 16 - 2; i >= 0; i--)
                    filtered_top[i] = (top[i + 1] + 2 * top[i] +
                                       top[i - 1] + 2) >> 2;
                left = filtered_left;
                top = filtered_top;
            }
        }
    }

    switch (mode) {
    case INTRA_PLANAR:
        s->hpc.pred_planar[4 - 2] ((uint8_t *) src, (uint8_t *) top,
                                   (uint8_t *) left, stride);
        break;
    case INTRA_DC:
        s->hpc.pred_dc((uint8_t *) src, (uint8_t *) top,
                       (uint8_t *) left, stride, 4, c_idx);
        break;
    default:
        s->hpc.pred_angular[4 - 2] ((uint8_t *) src, (uint8_t *) top,
                                    (uint8_t *) left, stride, c_idx, mode);
        break;
    }
}

void ff_intra_pred_8_32x32_msa(HEVCLocalContext *lc, int x0, int y0, int c_idx)
{
    v16u8 vec0, vec1;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 res0, res1, res2, res3;
    v8i16 mul_val0 = { 63, 62, 61, 60, 59, 58, 57, 56 };
    v8i16 mul_val1 = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const HEVCContext *const s = lc->parent;
    int i;
    int hshift = s->ps.sps->hshift[c_idx];
    int vshift = s->ps.sps->vshift[c_idx];
    int size_in_luma_h = 32 << hshift;
    int size_in_tbs_h = size_in_luma_h >> s->ps.sps->log2_min_tb_size;
    int size_in_luma_v = 32 << vshift;
    int size_in_tbs_v = size_in_luma_v >> s->ps.sps->log2_min_tb_size;
    int x = x0 >> hshift;
    int y = y0 >> vshift;
    int x_tb = (x0 >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask;
    int y_tb = (y0 >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask;

    int cur_tb_addr =
        s->ps.pps->min_tb_addr_zs[(y_tb) * (s->ps.sps->tb_mask + 2) + (x_tb)];

    ptrdiff_t stride = s->frame->linesize[c_idx] / sizeof(uint8_t);
    uint8_t *src = (uint8_t *) s->frame->data[c_idx] + x + y * stride;

    int min_pu_width = s->ps.sps->min_pu_width;

    enum IntraPredMode mode = c_idx ? lc->tu.intra_pred_mode_c :
        lc->tu.intra_pred_mode;
    uint32_t a;
    uint8_t left_array[2 * 32 + 1];
    uint8_t filtered_left_array[2 * 32 + 1];
    uint8_t top_array[2 * 32 + 1];
    uint8_t filtered_top_array[2 * 32 + 1];

    uint8_t *left = left_array + 1;
    uint8_t *top = top_array + 1;
    uint8_t *filtered_left = filtered_left_array + 1;
    uint8_t *filtered_top = filtered_top_array + 1;
    int cand_bottom_left = lc->na.cand_bottom_left
        && cur_tb_addr >
        s->ps.pps->min_tb_addr_zs[((y_tb + size_in_tbs_v) & s->ps.sps->tb_mask) *
                               (s->ps.sps->tb_mask + 2) + (x_tb - 1)];
    int cand_left = lc->na.cand_left;
    int cand_up_left = lc->na.cand_up_left;
    int cand_up = lc->na.cand_up;
    int cand_up_right = lc->na.cand_up_right
        && cur_tb_addr >
        s->ps.pps->min_tb_addr_zs[(y_tb - 1) * (s->ps.sps->tb_mask + 2) +
                               ((x_tb + size_in_tbs_h) & s->ps.sps->tb_mask)];

    int bottom_left_size =
        (((y0 + 2 * size_in_luma_v) >
          (s->ps.sps->height) ? (s->ps.sps->height) : (y0 +
                                                 2 * size_in_luma_v)) -
         (y0 + size_in_luma_v)) >> vshift;
    int top_right_size =
        (((x0 + 2 * size_in_luma_h) >
          (s->ps.sps->width) ? (s->ps.sps->width) : (x0 + 2 * size_in_luma_h)) -
         (x0 + size_in_luma_h)) >> hshift;

    if (s->ps.pps->constrained_intra_pred_flag == 1) {
        int size_in_luma_pu_v = ((size_in_luma_v) >> s->ps.sps->log2_min_pu_size);
        int size_in_luma_pu_h = ((size_in_luma_h) >> s->ps.sps->log2_min_pu_size);
        int on_pu_edge_x = !(x0 & ((1 << s->ps.sps->log2_min_pu_size) - 1));
        int on_pu_edge_y = !(y0 & ((1 << s->ps.sps->log2_min_pu_size) - 1));
        if (!size_in_luma_pu_h)
            size_in_luma_pu_h++;
        if (cand_bottom_left == 1 && on_pu_edge_x) {
            int x_left_pu = ((x0 - 1) >> s->ps.sps->log2_min_pu_size);
            int y_bottom_pu =
                ((y0 + size_in_luma_v) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_v) >
                 (s->ps.sps->min_pu_height -
                  y_bottom_pu) ? (s->ps.sps->min_pu_height -
                                  y_bottom_pu) : (size_in_luma_pu_v));
            cand_bottom_left = 0;
            for (i = 0; i < max; i += 2)
                cand_bottom_left |=
                    ((s->ref->tab_mvf[(x_left_pu) +
                                      (y_bottom_pu +
                                       i) * min_pu_width]).pred_flag ==
                     PF_INTRA);
        }
        if (cand_left == 1 && on_pu_edge_x) {
            int x_left_pu = ((x0 - 1) >> s->ps.sps->log2_min_pu_size);
            int y_left_pu = ((y0) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_v) >
                 (s->ps.sps->min_pu_height -
                  y_left_pu) ? (s->ps.sps->min_pu_height -
                                y_left_pu) : (size_in_luma_pu_v));
            cand_left = 0;
            for (i = 0; i < max; i += 2)
                cand_left |=
                    ((s->ref->tab_mvf[(x_left_pu) +
                                      (y_left_pu +
                                       i) * min_pu_width]).pred_flag ==
                     PF_INTRA);
        }
        if (cand_up_left == 1) {
            int x_left_pu = ((x0 - 1) >> s->ps.sps->log2_min_pu_size);
            int y_top_pu = ((y0 - 1) >> s->ps.sps->log2_min_pu_size);
            cand_up_left =
                (s->ref->tab_mvf[(x_left_pu) +
                                 (y_top_pu) * min_pu_width]).pred_flag ==
                PF_INTRA;
        }
        if (cand_up == 1 && on_pu_edge_y) {
            int x_top_pu = ((x0) >> s->ps.sps->log2_min_pu_size);
            int y_top_pu = ((y0 - 1) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_h) >
                 (s->ps.sps->min_pu_width -
                  x_top_pu) ? (s->ps.sps->min_pu_width -
                               x_top_pu) : (size_in_luma_pu_h));
            cand_up = 0;
            for (i = 0; i < max; i += 2)
                cand_up |=
                    ((s->ref->tab_mvf[(x_top_pu + i) +
                                      (y_top_pu) *
                                      min_pu_width]).pred_flag == PF_INTRA);
        }
        if (cand_up_right == 1 && on_pu_edge_y) {
            int y_top_pu = ((y0 - 1) >> s->ps.sps->log2_min_pu_size);
            int x_right_pu =
                ((x0 + size_in_luma_h) >> s->ps.sps->log2_min_pu_size);
            int max =
                ((size_in_luma_pu_h) >
                 (s->ps.sps->min_pu_width -
                  x_right_pu) ? (s->ps.sps->min_pu_width -
                                 x_right_pu) : (size_in_luma_pu_h));
            cand_up_right = 0;
            for (i = 0; i < max; i += 2)
                cand_up_right |=
                    ((s->ref->tab_mvf[(x_right_pu + i) +
                                      (y_top_pu) *
                                      min_pu_width]).pred_flag == PF_INTRA);
        }
        vec0 = (v16u8) __msa_ldi_b(128);

        ST_UB4(vec0, vec0, vec0, vec0, left, 16);
        ST_UB4(vec0, vec0, vec0, vec0, top, 16);

        top[-1] = 128;
    }
    if (cand_up_left) {
        left[-1] = src[(-1) + stride * (-1)];
        top[-1] = left[-1];
    }
    if (cand_up) {
        LD_UB2(src - stride, 16, vec0, vec1);
        ST_UB2(vec0, vec1, top, 16);
    }

    if (cand_up_right) {
        LD_UB2(src - stride + 32, 16, vec0, vec1);
        ST_UB2(vec0, vec1, (top + 32), 16);
        do {
            uint32_t pix =
                ((src[(32 + top_right_size - 1) + stride * (-1)]) *
                 0x01010101U);
            for (i = 0; i < (32 - top_right_size); i += 4)
                ((((union unaligned_32 *) (top + 32 + top_right_size +
                                           i))->l) = (pix));
        } while (0);
    }
    if (cand_left)
        for (i = 0; i < 32; i++)
            left[i] = src[(-1) + stride * (i)];
    if (cand_bottom_left) {
        for (i = 32; i < 32 + bottom_left_size; i++)
            left[i] = src[(-1) + stride * (i)];
        do {
            uint32_t pix =
                ((src[(-1) + stride * (32 + bottom_left_size - 1)]) *
                 0x01010101U);
            for (i = 0; i < (32 - bottom_left_size); i += 4)
                ((((union unaligned_32 *) (left + 32 + bottom_left_size +
                                           i))->l) = (pix));
        } while (0);
    }

    if (s->ps.pps->constrained_intra_pred_flag == 1) {
        if (cand_bottom_left || cand_left || cand_up_left || cand_up
            || cand_up_right) {
            int size_max_x =
                x0 + ((2 * 32) << hshift) <
                s->ps.sps->width ? 2 * 32 : (s->ps.sps->width - x0) >> hshift;
            int size_max_y =
                y0 + ((2 * 32) << vshift) <
                s->ps.sps->height ? 2 * 32 : (s->ps.sps->height - y0) >> vshift;
            int j = 32 + (cand_bottom_left ? bottom_left_size : 0) - 1;
            if (!cand_up_right) {
                size_max_x = x0 + ((32) << hshift) < s->ps.sps->width ?
                    32 : (s->ps.sps->width - x0) >> hshift;
            }
            if (!cand_bottom_left) {
                size_max_y = y0 + ((32) << vshift) < s->ps.sps->height ?
                    32 : (s->ps.sps->height - y0) >> vshift;
            }
            if (cand_bottom_left || cand_left || cand_up_left) {
                while (j > -1
                       &&
                       !((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((j) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                    j--;
                if (!
                    ((s->ref->tab_mvf[(((x0 +
                                         ((-1) << hshift)) >> s->ps.sps->
                                        log2_min_pu_size)) + (((y0 + ((j)
                                                                      <<
                                                                      vshift))
                                                               >> s->ps.sps->
                                                               log2_min_pu_size))
                                      * min_pu_width]).pred_flag == PF_INTRA)) {
                    j = 0;
                    while (j < size_max_x
                           &&
                           !((s->ref->tab_mvf[(((x0 +
                                                 ((j) << hshift)) >> s->ps.sps->
                                                log2_min_pu_size)) + (((y0 +
                                                                        ((-1) <<
                                                                         vshift))
                                                                       >> s->
                                                                       ps.sps->
                                                                       log2_min_pu_size))
                                              * min_pu_width]).pred_flag ==
                             PF_INTRA))
                        j++;
                    for (i = j; i > (j) - (j + 1); i--)
                        if (!
                            ((s->ref->tab_mvf[(((x0 +
                                                 ((i -
                                                   1) << hshift)) >> s->ps.sps->
                                                log2_min_pu_size)) + (((y0 +
                                                                        ((-1) <<
                                                                         vshift))
                                                                       >> s->
                                                                       ps.sps->
                                                                       log2_min_pu_size))
                                              * min_pu_width]).pred_flag ==
                             PF_INTRA))
                            top[i - 1] = top[i];
                    left[-1] = top[-1];
                }
            } else {
                j = 0;
                while (j < size_max_x
                       &&
                       !((s->ref->tab_mvf[(((x0 +
                                             ((j) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 + ((-1)
                                                                          <<
                                                                          vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                    j++;
                if (j > 0)
                    if (x0 > 0) {
                        for (i = j; i > (j) - (j + 1); i--)
                            if (!
                                ((s->ref->tab_mvf[(((x0 +
                                                     ((i -
                                                       1) << hshift)) >>
                                                    s->ps.sps->log2_min_pu_size))
                                                  + (((y0 + ((-1)
                                                             << vshift))
                                                      >>
                                                      s->ps.sps->log2_min_pu_size))
                                                  *
                                                  min_pu_width]).pred_flag ==
                                 PF_INTRA))
                                top[i - 1] = top[i];
                    } else {
                        for (i = j; i > (j) - (j); i--)
                            if (!
                                ((s->ref->tab_mvf[(((x0 +
                                                     ((i -
                                                       1) << hshift)) >>
                                                    s->ps.sps->log2_min_pu_size))
                                                  + (((y0 + ((-1)
                                                             << vshift))
                                                      >>
                                                      s->ps.sps->log2_min_pu_size))
                                                  *
                                                  min_pu_width]).pred_flag ==
                                 PF_INTRA))
                                top[i - 1] = top[i];
                        top[-1] = top[0];
                    }
                left[-1] = top[-1];
            }
            left[-1] = top[-1];
            if (cand_bottom_left || cand_left) {
                a = ((left[-1]) * 0x01010101U);
                for (i = 0; i < (0) + (size_max_y); i += 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((i) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&left[i]))->l) = (a));
                    else
                        a = ((left[i + 3]) * 0x01010101U);
            }
            if (!cand_left) {
                vec0 = (v16u8) __msa_fill_b(left[-1]);

                ST_UB2(vec0, vec0, left, 16);
            }
            if (!cand_bottom_left) {
                vec0 = (v16u8) __msa_fill_b(left[31]);

                ST_UB2(vec0, vec0, (left + 32), 16);
            }
            if (x0 != 0 && y0 != 0) {
                a = ((left[size_max_y - 1]) * 0x01010101U);
                for (i = (size_max_y - 1);
                     i > (size_max_y - 1) - (size_max_y); i -= 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((i -
                                                                      3) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&left[i - 3]))->l) = (a));
                    else
                        a = ((left[i - 3]) * 0x01010101U);
                if (!
                    ((s->ref->tab_mvf[(((x0 +
                                         ((-1) << hshift)) >> s->ps.sps->
                                        log2_min_pu_size)) + (((y0 + ((-1)
                                                                      <<
                                                                      vshift))
                                                               >> s->ps.sps->
                                                               log2_min_pu_size))
                                      * min_pu_width]).pred_flag == PF_INTRA))
                    left[-1] = left[0];
            } else if (x0 == 0) {
                do {
                    uint32_t pix = ((0) * 0x01010101U);
                    for (i = 0; i < (size_max_y); i += 4)
                        ((((union unaligned_32 *) (left + i))->l) = (pix));
                } while (0);
            } else {
                a = ((left[size_max_y - 1]) * 0x01010101U);
                for (i = (size_max_y - 1);
                     i > (size_max_y - 1) - (size_max_y); i -= 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((-1) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 +
                                                                    ((i -
                                                                      3) <<
                                                                     vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&left[i - 3]))->l) = (a));
                    else
                        a = ((left[i - 3]) * 0x01010101U);
            }
            top[-1] = left[-1];
            if (y0 != 0) {
                a = ((left[-1]) * 0x01010101U);
                for (i = 0; i < (0) + (size_max_x); i += 4)
                    if (!
                        ((s->ref->tab_mvf[(((x0 +
                                             ((i) << hshift)) >> s->ps.sps->
                                            log2_min_pu_size)) + (((y0 + ((-1)
                                                                          <<
                                                                          vshift))
                                                                   >> s->ps.sps->
                                                                   log2_min_pu_size))
                                          * min_pu_width]).pred_flag ==
                         PF_INTRA))
                        ((((union unaligned_32 *) (&top[i]))->l) = (a));
                    else
                        a = ((top[i + 3]) * 0x01010101U);
            }
        }
    }

    if (!cand_bottom_left) {
        if (cand_left) {
            vec0 = (v16u8) __msa_fill_b(left[31]);

            ST_UB2(vec0, vec0, (left + 32), 16);
        } else if (cand_up_left) {
            vec0 = (v16u8) __msa_fill_b(left[-1]);

            ST_UB4(vec0, vec0, vec0, vec0, left, 16);

            cand_left = 1;
        } else if (cand_up) {
            left[-1] = top[0];

            vec0 = (v16u8) __msa_fill_b(left[-1]);

            ST_UB4(vec0, vec0, vec0, vec0, left, 16);

            cand_up_left = 1;
            cand_left = 1;
        } else if (cand_up_right) {
            vec0 = (v16u8) __msa_fill_b(top[32]);

            ST_UB2(vec0, vec0, top, 16);

            left[-1] = top[32];

            ST_UB4(vec0, vec0, vec0, vec0, left, 16);

            cand_up = 1;
            cand_up_left = 1;
            cand_left = 1;
        } else {
            left[-1] = 128;

            vec0 = (v16u8) __msa_ldi_b(128);

            ST_UB4(vec0, vec0, vec0, vec0, top, 16);
            ST_UB4(vec0, vec0, vec0, vec0, left, 16);
        }
    }

    if (!cand_left) {
        vec0 = (v16u8) __msa_fill_b(left[32]);

        ST_UB2(vec0, vec0, left, 16);
    }
    if (!cand_up_left) {
        left[-1] = left[0];
    }
    if (!cand_up) {
        vec0 = (v16u8) __msa_fill_b(left[-1]);

        ST_UB2(vec0, vec0, top, 16);
    }
    if (!cand_up_right) {
        vec0 = (v16u8) __msa_fill_b(top[31]);

        ST_UB2(vec0, vec0, (top + 32), 16);
    }

    top[-1] = left[-1];


    if (!s->ps.sps->intra_smoothing_disabled_flag
        && (c_idx == 0 || s->ps.sps->chroma_format_idc == 3)) {
        if (mode != INTRA_DC && 32 != 4) {
            int intra_hor_ver_dist_thresh[] = { 7, 1, 0 };
            int min_dist_vert_hor =
                (((((int) (mode - 26U)) >=
                   0 ? ((int) (mode - 26U)) : (-((int) (mode - 26U))))) >
                 ((((int) (mode - 10U)) >=
                   0 ? ((int) (mode - 10U)) : (-((int) (mode - 10U)))))
                 ? ((((int) (mode - 10U)) >=
                     0 ? ((int) (mode - 10U)) : (-((int) (mode - 10U)))))
                 : ((((int) (mode - 26U)) >=
                     0 ? ((int) (mode - 26U)) : (-((int) (mode - 26U))))));
            if (min_dist_vert_hor > intra_hor_ver_dist_thresh[5 - 3]) {
                int threshold = 1 << (8 - 5);
                if (s->ps.sps->sps_strong_intra_smoothing_enable_flag
                    && c_idx == 0
                    && ((top[-1] + top[63] - 2 * top[31]) >=
                        0 ? (top[-1] + top[63] -
                             2 * top[31]) : (-(top[-1] + top[63] -
                                               2 * top[31]))) < threshold
                    && ((left[-1] + left[63] - 2 * left[31]) >=
                        0 ? (left[-1] + left[63] -
                             2 * left[31]) : (-(left[-1] + left[63] -
                                                2 * left[31]))) < threshold) {


                    filtered_top[-1] = top[-1];
                    filtered_top[63] = top[63];


                    for (i = 0; i < 63; i++) {
                        filtered_top[i] =
                            ((63 - i) * top[-1] + (i + 1) * top[63] + 32) >> 6;
                    }

                    tmp0 = __msa_fill_h(top[-1]);
                    tmp1 = __msa_fill_h(top[63]);

                    tmp2 = mul_val0 - 8;
                    tmp3 = mul_val0 - 16;
                    tmp4 = mul_val0 - 24;
                    tmp5 = mul_val1 + 8;
                    tmp6 = mul_val1 + 16;
                    tmp7 = mul_val1 + 24;

                    res0 = mul_val0 * tmp0;
                    res1 = tmp2 * tmp0;
                    res2 = tmp3 * tmp0;
                    res3 = tmp4 * tmp0;
                    res0 += mul_val1 * tmp1;
                    res1 += tmp5 * tmp1;
                    res2 += tmp6 * tmp1;
                    res3 += tmp7 * tmp1;

                    res0 = __msa_srari_h(res0, 6);
                    res1 = __msa_srari_h(res1, 6);
                    res2 = __msa_srari_h(res2, 6);
                    res3 = __msa_srari_h(res3, 6);

                    vec0 = (v16u8) __msa_pckev_b((v16i8) res1, (v16i8) res0);
                    vec1 = (v16u8) __msa_pckev_b((v16i8) res3, (v16i8) res2);

                    ST_UB2(vec0, vec1, filtered_top, 16);

                    res0 = mul_val0 - 32;
                    tmp2 = mul_val0 - 40;
                    tmp3 = mul_val0 - 48;
                    tmp4 = mul_val0 - 56;
                    res3 = mul_val1 + 32;
                    tmp5 = mul_val1 + 40;
                    tmp6 = mul_val1 + 48;
                    tmp7 = mul_val1 + 56;

                    res0 = res0 * tmp0;
                    res1 = tmp2 * tmp0;
                    res2 = tmp3 * tmp0;
                    res0 += res3 * tmp1;
                    res3 = tmp4 * tmp0;
                    res1 += tmp5 * tmp1;
                    res2 += tmp6 * tmp1;
                    res3 += tmp7 * tmp1;

                    res0 = __msa_srari_h(res0, 6);
                    res1 = __msa_srari_h(res1, 6);
                    res2 = __msa_srari_h(res2, 6);
                    res3 = __msa_srari_h(res3, 6);

                    vec0 = (v16u8) __msa_pckev_b((v16i8) res1, (v16i8) res0);
                    vec1 = (v16u8) __msa_pckev_b((v16i8) res3, (v16i8) res2);

                    ST_UB2(vec0, vec1, (filtered_top + 32), 16);

                    filtered_top[63] = top[63];

                    tmp0 = __msa_fill_h(left[-1]);
                    tmp1 = __msa_fill_h(left[63]);

                    tmp2 = mul_val0 - 8;
                    tmp3 = mul_val0 - 16;
                    tmp4 = mul_val0 - 24;
                    tmp5 = mul_val1 + 8;
                    tmp6 = mul_val1 + 16;
                    tmp7 = mul_val1 + 24;

                    res0 = mul_val0 * tmp0;
                    res1 = tmp2 * tmp0;
                    res2 = tmp3 * tmp0;
                    res3 = tmp4 * tmp0;
                    res0 += mul_val1 * tmp1;
                    res1 += tmp5 * tmp1;
                    res2 += tmp6 * tmp1;
                    res3 += tmp7 * tmp1;

                    res0 = __msa_srari_h(res0, 6);
                    res1 = __msa_srari_h(res1, 6);
                    res2 = __msa_srari_h(res2, 6);
                    res3 = __msa_srari_h(res3, 6);

                    vec0 = (v16u8) __msa_pckev_b((v16i8) res1, (v16i8) res0);
                    vec1 = (v16u8) __msa_pckev_b((v16i8) res3, (v16i8) res2);

                    ST_UB2(vec0, vec1, left, 16);

                    res0 = mul_val0 - 32;
                    tmp2 = mul_val0 - 40;
                    tmp3 = mul_val0 - 48;
                    tmp4 = mul_val0 - 56;
                    res3 = mul_val1 + 32;
                    tmp5 = mul_val1 + 40;
                    tmp6 = mul_val1 + 48;
                    tmp7 = mul_val1 + 56;

                    res0 = res0 * tmp0;
                    res1 = tmp2 * tmp0;
                    res2 = tmp3 * tmp0;
                    res0 += res3 * tmp1;
                    res3 = tmp4 * tmp0;
                    res1 += tmp5 * tmp1;
                    res2 += tmp6 * tmp1;
                    res3 += tmp7 * tmp1;

                    res0 = __msa_srari_h(res0, 6);
                    res1 = __msa_srari_h(res1, 6);
                    res2 = __msa_srari_h(res2, 6);
                    res3 = __msa_srari_h(res3, 6);

                    vec0 = (v16u8) __msa_pckev_b((v16i8) res1, (v16i8) res0);
                    vec1 = (v16u8) __msa_pckev_b((v16i8) res3, (v16i8) res2);

                    ST_UB2(vec0, vec1, (left + 32), 16);

                    left[63] = tmp1[0];

                    top = filtered_top;
                } else {
                    filtered_left[2 * 32 - 1] = left[2 * 32 - 1];
                    filtered_top[2 * 32 - 1] = top[2 * 32 - 1];
                    for (i = 2 * 32 - 2; i >= 0; i--)
                        filtered_left[i] = (left[i + 1] + 2 * left[i] +
                                            left[i - 1] + 2) >> 2;
                    filtered_top[-1] =
                        filtered_left[-1] =
                        (left[0] + 2 * left[-1] + top[0] + 2) >> 2;
                    for (i = 2 * 32 - 2; i >= 0; i--)
                        filtered_top[i] = (top[i + 1] + 2 * top[i] +
                                           top[i - 1] + 2) >> 2;
                    left = filtered_left;
                    top = filtered_top;
                }
            }
        }
    }

    switch (mode) {
    case INTRA_PLANAR:
        s->hpc.pred_planar[3] ((uint8_t *) src, (uint8_t *) top,
                               (uint8_t *) left, stride);
        break;
    case INTRA_DC:
        s->hpc.pred_dc((uint8_t *) src, (uint8_t *) top,
                       (uint8_t *) left, stride, 5, c_idx);
        break;
    default:
        s->hpc.pred_angular[3] ((uint8_t *) src, (uint8_t *) top,
                                (uint8_t *) left, stride, c_idx, mode);
        break;
    }
}
