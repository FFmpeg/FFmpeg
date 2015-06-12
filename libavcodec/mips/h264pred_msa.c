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

#include "libavutil/mips/generic_macros_msa.h"
#include "h264dsp_mips.h"

static void intra_predict_vert_8x8_msa(uint8_t *src, uint8_t *dst,
                                       int32_t dst_stride)
{
    uint32_t row;
    uint32_t src_data1, src_data2;

    src_data1 = LW(src);
    src_data2 = LW(src + 4);

    for (row = 8; row--;) {
        SW(src_data1, dst);
        SW(src_data2, (dst + 4));
        dst += dst_stride;
    }
}

static void intra_predict_vert_16x16_msa(uint8_t *src, uint8_t *dst,
                                         int32_t dst_stride)
{
    uint32_t row;
    v16u8 src0;

    src0 = LD_UB(src);

    for (row = 16; row--;) {
        ST_UB(src0, dst);
        dst += dst_stride;
    }
}

static void intra_predict_horiz_8x8_msa(uint8_t *src, int32_t src_stride,
                                        uint8_t *dst, int32_t dst_stride)
{
    uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

    out0 = src[0 * src_stride] * 0x0101010101010101;
    out1 = src[1 * src_stride] * 0x0101010101010101;
    out2 = src[2 * src_stride] * 0x0101010101010101;
    out3 = src[3 * src_stride] * 0x0101010101010101;
    out4 = src[4 * src_stride] * 0x0101010101010101;
    out5 = src[5 * src_stride] * 0x0101010101010101;
    out6 = src[6 * src_stride] * 0x0101010101010101;
    out7 = src[7 * src_stride] * 0x0101010101010101;

    SD4(out0, out1, out2, out3, dst, dst_stride);
    dst += (4 * dst_stride);
    SD4(out4, out5, out6, out7, dst, dst_stride);
}

static void intra_predict_horiz_16x16_msa(uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    uint32_t row;
    uint8_t inp0, inp1, inp2, inp3;
    v16u8 src0, src1, src2, src3;

    for (row = 4; row--;) {
        inp0 = src[0];
        src += src_stride;
        inp1 = src[0];
        src += src_stride;
        inp2 = src[0];
        src += src_stride;
        inp3 = src[0];
        src += src_stride;

        src0 = (v16u8) __msa_fill_b(inp0);
        src1 = (v16u8) __msa_fill_b(inp1);
        src2 = (v16u8) __msa_fill_b(inp2);
        src3 = (v16u8) __msa_fill_b(inp3);

        ST_UB4(src0, src1, src2, src3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void intra_predict_dc_8x8_msa(uint8_t *src_top, uint8_t *src_left,
                                     int32_t src_stride_left,
                                     uint8_t *dst, int32_t dst_stride,
                                     uint8_t is_above, uint8_t is_left)
{
    uint32_t row;
    uint32_t out, addition = 0;
    v16u8 src_above, store;
    v8u16 sum_above;
    v4u32 sum_top;
    v2u64 sum;

    if (is_left && is_above) {
        src_above = LD_UB(src_top);

        sum_above = __msa_hadd_u_h(src_above, src_above);
        sum_top = __msa_hadd_u_w(sum_above, sum_above);
        sum = __msa_hadd_u_d(sum_top, sum_top);
        addition = __msa_copy_u_w((v4i32) sum, 0);

        for (row = 0; row < 8; row++) {
            addition += src_left[row * src_stride_left];
        }

        addition = (addition + 8) >> 4;
        store = (v16u8) __msa_fill_b(addition);
    } else if (is_left) {
        for (row = 0; row < 8; row++) {
            addition += src_left[row * src_stride_left];
        }

        addition = (addition + 4) >> 3;
        store = (v16u8) __msa_fill_b(addition);
    } else if (is_above) {
        src_above = LD_UB(src_top);

        sum_above = __msa_hadd_u_h(src_above, src_above);
        sum_top = __msa_hadd_u_w(sum_above, sum_above);
        sum = __msa_hadd_u_d(sum_top, sum_top);
        sum = (v2u64) __msa_srari_d((v2i64) sum, 3);
        store = (v16u8) __msa_splati_b((v16i8) sum, 0);
    } else {
        store = (v16u8) __msa_ldi_b(128);
    }

    out = __msa_copy_u_w((v4i32) store, 0);

    for (row = 8; row--;) {
        SW(out, dst);
        SW(out, (dst + 4));
        dst += dst_stride;
    }
}

static void intra_predict_dc_16x16_msa(uint8_t *src_top, uint8_t *src_left,
                                       int32_t src_stride_left,
                                       uint8_t *dst, int32_t dst_stride,
                                       uint8_t is_above, uint8_t is_left)
{
    uint32_t row;
    uint32_t addition = 0;
    v16u8 src_above, store;
    v8u16 sum_above;
    v4u32 sum_top;
    v2u64 sum;

    if (is_left && is_above) {
        src_above = LD_UB(src_top);

        sum_above = __msa_hadd_u_h(src_above, src_above);
        sum_top = __msa_hadd_u_w(sum_above, sum_above);
        sum = __msa_hadd_u_d(sum_top, sum_top);
        sum_top = (v4u32) __msa_pckev_w((v4i32) sum, (v4i32) sum);
        sum = __msa_hadd_u_d(sum_top, sum_top);
        addition = __msa_copy_u_w((v4i32) sum, 0);

        for (row = 0; row < 16; row++) {
            addition += src_left[row * src_stride_left];
        }

        addition = (addition + 16) >> 5;
        store = (v16u8) __msa_fill_b(addition);
    } else if (is_left) {
        for (row = 0; row < 16; row++) {
            addition += src_left[row * src_stride_left];
        }

        addition = (addition + 8) >> 4;
        store = (v16u8) __msa_fill_b(addition);
    } else if (is_above) {
        src_above = LD_UB(src_top);

        sum_above = __msa_hadd_u_h(src_above, src_above);
        sum_top = __msa_hadd_u_w(sum_above, sum_above);
        sum = __msa_hadd_u_d(sum_top, sum_top);
        sum_top = (v4u32) __msa_pckev_w((v4i32) sum, (v4i32) sum);
        sum = __msa_hadd_u_d(sum_top, sum_top);
        sum = (v2u64) __msa_srari_d((v2i64) sum, 4);
        store = (v16u8) __msa_splati_b((v16i8) sum, 0);
    } else {
        store = (v16u8) __msa_ldi_b(128);
    }

    for (row = 16; row--;) {
        ST_UB(store, dst);
        dst += dst_stride;
    }
}

#define INTRA_PREDICT_VALDC_8X8_MSA(val)                         \
static void intra_predict_##val##dc_8x8_msa(uint8_t *dst,        \
                                            int32_t dst_stride)  \
{                                                                \
    uint32_t row, out;                                           \
    v16i8 store;                                                 \
                                                                 \
    store = __msa_ldi_b(val);                                    \
    out = __msa_copy_u_w((v4i32) store, 0);                      \
                                                                 \
    for (row = 8; row--;) {                                      \
        SW(out, dst);                                            \
        SW(out, (dst + 4));                                      \
        dst += dst_stride;                                       \
    }                                                            \
}

INTRA_PREDICT_VALDC_8X8_MSA(127);
INTRA_PREDICT_VALDC_8X8_MSA(129);

#define INTRA_PREDICT_VALDC_16X16_MSA(val)                         \
static void intra_predict_##val##dc_16x16_msa(uint8_t *dst,        \
                                              int32_t dst_stride)  \
{                                                                  \
    uint32_t row;                                                  \
    v16u8 store;                                                   \
                                                                   \
    store = (v16u8) __msa_ldi_b(val);                              \
                                                                   \
    for (row = 16; row--;) {                                       \
        ST_UB(store, dst);                                         \
        dst += dst_stride;                                         \
    }                                                              \
}

INTRA_PREDICT_VALDC_16X16_MSA(127);
INTRA_PREDICT_VALDC_16X16_MSA(129);

static void intra_predict_plane_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lpcnt;
    int32_t res, res0, res1, res2, res3;
    uint64_t out0, out1;
    v16i8 shf_mask = { 3, 5, 2, 6, 1, 7, 0, 8, 3, 5, 2, 6, 1, 7, 0, 8 };
    v8i16 short_multiplier = { 1, 2, 3, 4, 1, 2, 3, 4 };
    v4i32 int_multiplier = { 0, 1, 2, 3 };
    v16u8 src_top;
    v8i16 vec9, vec10, vec11;
    v4i32 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8;
    v2i64 sum;

    src_top = LD_UB(src - (stride + 1));
    src_top = (v16u8) __msa_vshf_b(shf_mask, (v16i8) src_top, (v16i8) src_top);

    vec9 = __msa_hsub_u_h(src_top, src_top);
    vec9 *= short_multiplier;
    vec8 = __msa_hadd_s_w(vec9, vec9);
    sum = __msa_hadd_s_d(vec8, vec8);

    res0 = __msa_copy_s_w((v4i32) sum, 0);

    res1 = (src[4 * stride - 1] - src[2 * stride - 1]) +
        2 * (src[5 * stride - 1] - src[stride - 1]) +
        3 * (src[6 * stride - 1] - src[-1]) +
        4 * (src[7 * stride - 1] - src[-stride - 1]);

    res0 *= 17;
    res1 *= 17;
    res0 = (res0 + 16) >> 5;
    res1 = (res1 + 16) >> 5;

    res3 = 3 * (res0 + res1);
    res2 = 16 * (src[7 * stride - 1] + src[-stride + 7] + 1);
    res = res2 - res3;

    vec8 = __msa_fill_w(res0);
    vec4 = __msa_fill_w(res);
    vec2 = __msa_fill_w(res1);
    vec5 = vec8 * int_multiplier;
    vec3 = vec8 * 4;

    for (lpcnt = 4; lpcnt--;) {
        vec0 = vec5;
        vec0 += vec4;
        vec1 = vec0 + vec3;
        vec6 = vec5;
        vec4 += vec2;
        vec6 += vec4;
        vec7 = vec6 + vec3;

        SRA_4V(vec0, vec1, vec6, vec7, 5);
        PCKEV_H2_SH(vec1, vec0, vec7, vec6, vec10, vec11);
        CLIP_SH2_0_255(vec10, vec11);
        PCKEV_B2_SH(vec10, vec10, vec11, vec11, vec10, vec11);

        out0 = __msa_copy_s_d((v2i64) vec10, 0);
        out1 = __msa_copy_s_d((v2i64) vec11, 0);
        SD(out0, src);
        src += stride;
        SD(out1, src);
        src += stride;

        vec4 += vec2;
    }
}

static void intra_predict_plane_16x16_msa(uint8_t *src, int32_t stride)
{
    uint8_t lpcnt;
    int32_t res0, res1, res2, res3;
    uint64_t load0, load1;
    v16i8 shf_mask = { 7, 8, 6, 9, 5, 10, 4, 11, 3, 12, 2, 13, 1, 14, 0, 15 };
    v8i16 short_multiplier = { 1, 2, 3, 4, 5, 6, 7, 8 };
    v4i32 int_multiplier = { 0, 1, 2, 3 };
    v16u8 src_top = { 0 };
    v8i16 vec9, vec10;
    v4i32 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, res_add;

    load0 = LD(src - (stride + 1));
    load1 = LD(src - (stride + 1) + 9);

    INSERT_D2_UB(load0, load1, src_top);

    src_top = (v16u8) __msa_vshf_b(shf_mask, (v16i8) src_top, (v16i8) src_top);

    vec9 = __msa_hsub_u_h(src_top, src_top);
    vec9 *= short_multiplier;
    vec8 = __msa_hadd_s_w(vec9, vec9);
    res_add = (v4i32) __msa_hadd_s_d(vec8, vec8);

    res0 = __msa_copy_s_w(res_add, 0) + __msa_copy_s_w(res_add, 2);

    res1 = (src[8 * stride - 1] - src[6 * stride - 1]) +
        2 * (src[9 * stride - 1] - src[5 * stride - 1]) +
        3 * (src[10 * stride - 1] - src[4 * stride - 1]) +
        4 * (src[11 * stride - 1] - src[3 * stride - 1]) +
        5 * (src[12 * stride - 1] - src[2 * stride - 1]) +
        6 * (src[13 * stride - 1] - src[stride - 1]) +
        7 * (src[14 * stride - 1] - src[-1]) +
        8 * (src[15 * stride - 1] - src[-1 * stride - 1]);

    res0 *= 5;
    res1 *= 5;
    res0 = (res0 + 32) >> 6;
    res1 = (res1 + 32) >> 6;

    res3 = 7 * (res0 + res1);
    res2 = 16 * (src[15 * stride - 1] + src[-stride + 15] + 1);
    res2 -= res3;

    vec8 = __msa_fill_w(res0);
    vec4 = __msa_fill_w(res2);
    vec5 = __msa_fill_w(res1);
    vec6 = vec8 * 4;
    vec7 = vec8 * int_multiplier;

    for (lpcnt = 16; lpcnt--;) {
        vec0 = vec7;
        vec0 += vec4;
        vec1 = vec0 + vec6;
        vec2 = vec1 + vec6;
        vec3 = vec2 + vec6;

        SRA_4V(vec0, vec1, vec2, vec3, 5);
        PCKEV_H2_SH(vec1, vec0, vec3, vec2, vec9, vec10);
        CLIP_SH2_0_255(vec9, vec10);
        PCKEV_ST_SB(vec9, vec10, src);
        src += stride;

        vec4 += vec5;
    }
}

static void intra_predict_dc_4blk_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t src0, src1, src3, src2 = 0;
    uint32_t out0, out1, out2, out3;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h((v16u8) src_top, (v16u8) src_top);
    sum = __msa_hadd_u_w(add, add);
    src0 = __msa_copy_u_w((v4i32) sum, 0);
    src1 = __msa_copy_u_w((v4i32) sum, 1);

    for (lp_cnt = 0; lp_cnt < 4; lp_cnt++) {
        src0 += src[lp_cnt * stride - 1];
        src2 += src[(4 + lp_cnt) * stride - 1];
    }

    src0 = (src0 + 4) >> 3;
    src3 = (src1 + src2 + 4) >> 3;
    src1 = (src1 + 2) >> 2;
    src2 = (src2 + 2) >> 2;
    out0 = src0 * 0x01010101;
    out1 = src1 * 0x01010101;
    out2 = src2 * 0x01010101;
    out3 = src3 * 0x01010101;

    for (lp_cnt = 4; lp_cnt--;) {
        SW(out0, src);
        SW(out1, (src + 4));
        SW(out2, (src + 4 * stride));
        SW(out3, (src + 4 * stride + 4));
        src += stride;
    }
}

static void intra_predict_hor_dc_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t src0 = 0, src1 = 0;
    uint64_t out0, out1;

    for (lp_cnt = 0; lp_cnt < 4; lp_cnt++) {
        src0 += src[lp_cnt * stride - 1];
        src1 += src[(4 + lp_cnt) * stride - 1];
    }

    src0 = (src0 + 2) >> 2;
    src1 = (src1 + 2) >> 2;
    out0 = src0 * 0x0101010101010101;
    out1 = src1 * 0x0101010101010101;

    for (lp_cnt = 4; lp_cnt--;) {
        SD(out0, src);
        SD(out1, (src + 4 * stride));
        src += stride;
    }
}

static void intra_predict_vert_dc_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t out0 = 0, out1 = 0;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;
    v4i32 res0, res1;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h(src_top, src_top);
    sum = __msa_hadd_u_w(add, add);
    sum = (v4u32) __msa_srari_w((v4i32) sum, 2);
    res0 = (v4i32) __msa_splati_b((v16i8) sum, 0);
    res1 = (v4i32) __msa_splati_b((v16i8) sum, 4);
    out0 = __msa_copy_u_w(res0, 0);
    out1 = __msa_copy_u_w(res1, 0);

    for (lp_cnt = 8; lp_cnt--;) {
        SW(out0, src);
        SW(out1, src + 4);
        src += stride;
    }
}

static void intra_predict_mad_cow_dc_l0t_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t src0, src1, src2 = 0;
    uint32_t out0, out1, out2;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h(src_top, src_top);
    sum = __msa_hadd_u_w(add, add);
    src0 = __msa_copy_u_w((v4i32) sum, 0);
    src1 = __msa_copy_u_w((v4i32) sum, 1);

    for (lp_cnt = 0; lp_cnt < 4; lp_cnt++) {
        src2 += src[lp_cnt * stride - 1];
    }
    src2 = (src0 + src2 + 4) >> 3;
    src0 = (src0 + 2) >> 2;
    src1 = (src1 + 2) >> 2;
    out0 = src0 * 0x01010101;
    out1 = src1 * 0x01010101;
    out2 = src2 * 0x01010101;

    for (lp_cnt = 4; lp_cnt--;) {
        SW(out2, src);
        SW(out1, src + 4);
        SW(out0, src + stride * 4);
        SW(out1, src + stride * 4 + 4);
        src += stride;
    }
}

static void intra_predict_mad_cow_dc_0lt_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t src0, src1, src2 = 0, src3;
    uint32_t out0, out1, out2, out3;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h(src_top, src_top);
    sum = __msa_hadd_u_w(add, add);
    src0 = __msa_copy_u_w((v4i32) sum, 0);
    src1 = __msa_copy_u_w((v4i32) sum, 1);

    for (lp_cnt = 0; lp_cnt < 4; lp_cnt++) {
        src2 += src[(4 + lp_cnt) * stride - 1];
    }

    src0 = (src0 + 2) >> 2;
    src3 = (src1 + src2 + 4) >> 3;
    src1 = (src1 + 2) >> 2;
    src2 = (src2 + 2) >> 2;

    out0 = src0 * 0x01010101;
    out1 = src1 * 0x01010101;
    out2 = src2 * 0x01010101;
    out3 = src3 * 0x01010101;

    for (lp_cnt = 4; lp_cnt--;) {
        SW(out0, src);
        SW(out1, src + 4);
        SW(out2, src + stride * 4);
        SW(out3, src + stride * 4 + 4);
        src += stride;
    }
}

static void intra_predict_mad_cow_dc_l00_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t src0 = 0;
    uint64_t out0, out1;

    for (lp_cnt = 0; lp_cnt < 4; lp_cnt++) {
        src0 += src[lp_cnt * stride - 1];
    }

    src0 = (src0 + 2) >> 2;
    out0 = src0 * 0x0101010101010101;
    out1 = 0x8080808080808080;

    for (lp_cnt = 4; lp_cnt--;) {
        SD(out0, src);
        SD(out1, src + stride * 4);
        src += stride;
    }
}

static void intra_predict_mad_cow_dc_0l0_8x8_msa(uint8_t *src, int32_t stride)
{
    uint8_t lp_cnt;
    uint32_t src0 = 0;
    uint64_t out0, out1;

    for (lp_cnt = 0; lp_cnt < 4; lp_cnt++) {
        src0 += src[(4 + lp_cnt) * stride - 1];
    }

    src0 = (src0 + 2) >> 2;

    out0 = 0x8080808080808080;
    out1 = src0 * 0x0101010101010101;

    for (lp_cnt = 4; lp_cnt--;) {
        SD(out0, src);
        SD(out1, src + stride * 4);
        src += stride;
    }
}

void ff_h264_intra_predict_plane_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_plane_8x8_msa(src, stride);
}

void ff_h264_intra_predict_dc_4blk_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_dc_4blk_8x8_msa(src, stride);
}

void ff_h264_intra_predict_hor_dc_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_hor_dc_8x8_msa(src, stride);
}

void ff_h264_intra_predict_vert_dc_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_vert_dc_8x8_msa(src, stride);
}

void ff_h264_intra_predict_mad_cow_dc_l0t_8x8_msa(uint8_t *src,
                                                  ptrdiff_t stride)
{
    intra_predict_mad_cow_dc_l0t_8x8_msa(src, stride);
}

void ff_h264_intra_predict_mad_cow_dc_0lt_8x8_msa(uint8_t *src,
                                                  ptrdiff_t stride)
{
    intra_predict_mad_cow_dc_0lt_8x8_msa(src, stride);
}

void ff_h264_intra_predict_mad_cow_dc_l00_8x8_msa(uint8_t *src,
                                                  ptrdiff_t stride)
{
    intra_predict_mad_cow_dc_l00_8x8_msa(src, stride);
}

void ff_h264_intra_predict_mad_cow_dc_0l0_8x8_msa(uint8_t *src,
                                                  ptrdiff_t stride)
{
    intra_predict_mad_cow_dc_0l0_8x8_msa(src, stride);
}

void ff_h264_intra_predict_plane_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_plane_16x16_msa(src, stride);
}

void ff_h264_intra_pred_vert_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *dst = src;

    intra_predict_vert_8x8_msa(src - stride, dst, stride);
}

void ff_h264_intra_pred_horiz_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *dst = src;

    intra_predict_horiz_8x8_msa(src - 1, stride, dst, stride);
}

void ff_h264_intra_pred_dc_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *src_top = src - stride;
    uint8_t *src_left = src - 1;
    uint8_t *dst = src;

    intra_predict_dc_16x16_msa(src_top, src_left, stride, dst, stride, 1, 1);
}

void ff_h264_intra_pred_vert_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *dst = src;

    intra_predict_vert_16x16_msa(src - stride, dst, stride);
}

void ff_h264_intra_pred_horiz_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *dst = src;

    intra_predict_horiz_16x16_msa(src - 1, stride, dst, stride);
}

void ff_h264_intra_pred_dc_left_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *src_top = src - stride;
    uint8_t *src_left = src - 1;
    uint8_t *dst = src;

    intra_predict_dc_16x16_msa(src_top, src_left, stride, dst, stride, 0, 1);
}

void ff_h264_intra_pred_dc_top_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *src_top = src - stride;
    uint8_t *src_left = src - 1;
    uint8_t *dst = src;

    intra_predict_dc_16x16_msa(src_top, src_left, stride, dst, stride, 1, 0);
}

void ff_h264_intra_pred_dc_128_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *src_top = src - stride;
    uint8_t *src_left = src - 1;
    uint8_t *dst = src;

    intra_predict_dc_8x8_msa(src_top, src_left, stride, dst, stride, 0, 0);
}

void ff_h264_intra_pred_dc_128_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *src_top = src - stride;
    uint8_t *src_left = src - 1;
    uint8_t *dst = src;

    intra_predict_dc_16x16_msa(src_top, src_left, stride, dst, stride, 0, 0);
}

void ff_vp8_pred8x8_127_dc_8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_127dc_8x8_msa(src, stride);
}

void ff_vp8_pred8x8_129_dc_8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_129dc_8x8_msa(src, stride);
}

void ff_vp8_pred16x16_127_dc_8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_127dc_16x16_msa(src, stride);
}

void ff_vp8_pred16x16_129_dc_8_msa(uint8_t *src, ptrdiff_t stride)
{
    intra_predict_129dc_16x16_msa(src, stride);
}
