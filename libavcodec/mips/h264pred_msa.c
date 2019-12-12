/*
 * Copyright (c) 2015 - 2017 Shivraj Patil (Shivraj.Patil@imgtec.com)
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
    uint64_t out = LD(src);

    SD4(out, out, out, out, dst, dst_stride);
    dst += (4 * dst_stride);
    SD4(out, out, out, out, dst, dst_stride);
}

static void intra_predict_vert_16x16_msa(uint8_t *src, uint8_t *dst,
                                         int32_t dst_stride)
{
    v16u8 out = LD_UB(src);

    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);
    dst += (8 * dst_stride);
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);
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
    uint8_t inp0, inp1, inp2, inp3;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;

    inp0 = src[0 * src_stride];
    inp1 = src[1 * src_stride];
    inp2 = src[2 * src_stride];
    inp3 = src[3 * src_stride];
    src0 = (v16u8) __msa_fill_b(inp0);
    src1 = (v16u8) __msa_fill_b(inp1);
    src2 = (v16u8) __msa_fill_b(inp2);
    src3 = (v16u8) __msa_fill_b(inp3);
    inp0 = src[4 * src_stride];
    inp1 = src[5 * src_stride];
    inp2 = src[6 * src_stride];
    inp3 = src[7 * src_stride];
    src4 = (v16u8) __msa_fill_b(inp0);
    src5 = (v16u8) __msa_fill_b(inp1);
    src6 = (v16u8) __msa_fill_b(inp2);
    src7 = (v16u8) __msa_fill_b(inp3);
    inp0 = src[ 8 * src_stride];
    inp1 = src[ 9 * src_stride];
    inp2 = src[10 * src_stride];
    inp3 = src[11 * src_stride];
    src8 = (v16u8) __msa_fill_b(inp0);
    src9 = (v16u8) __msa_fill_b(inp1);
    src10 = (v16u8) __msa_fill_b(inp2);
    src11 = (v16u8) __msa_fill_b(inp3);
    inp0 = src[12 * src_stride];
    inp1 = src[13 * src_stride];
    inp2 = src[14 * src_stride];
    inp3 = src[15 * src_stride];
    src12 = (v16u8) __msa_fill_b(inp0);
    src13 = (v16u8) __msa_fill_b(inp1);
    src14 = (v16u8) __msa_fill_b(inp2);
    src15 = (v16u8) __msa_fill_b(inp3);

    ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7, dst, dst_stride);
    dst += (8 * dst_stride);
    ST_UB8(src8, src9, src10, src11, src12, src13, src14, src15,
           dst, dst_stride);
}

#define INTRA_PREDICT_VALDC_8X8_MSA(val)                                       \
static void intra_predict_##val##dc_8x8_msa(uint8_t *dst, int32_t dst_stride)  \
{                                                                              \
    v16i8 store = __msa_fill_b(val);                                           \
    uint64_t out = __msa_copy_u_d((v2i64) store, 0);                           \
                                                                               \
    SD4(out, out, out, out, dst, dst_stride);                                  \
    dst += (4 * dst_stride);                                                   \
    SD4(out, out, out, out, dst, dst_stride);                                  \
}

INTRA_PREDICT_VALDC_8X8_MSA(127);
INTRA_PREDICT_VALDC_8X8_MSA(129);

#define INTRA_PREDICT_VALDC_16X16_MSA(val)                            \
static void intra_predict_##val##dc_16x16_msa(uint8_t *dst,           \
                                              int32_t dst_stride)     \
{                                                                     \
    v16u8 out = (v16u8) __msa_fill_b(val);                            \
                                                                      \
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);  \
    dst += (8 * dst_stride);                                          \
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);  \
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
    v16u8 store0, store1;
    v8i16 vec9, vec10, vec11, vec12;
    v4i32 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, res_add;
    v4i32 reg0, reg1, reg2, reg3;

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

    for (lpcnt = 8; lpcnt--;) {
        vec0 = vec7;
        reg0 = vec7;
        vec0 += vec4;
        vec4 += vec5;
        reg0 += vec4;
        vec1 = vec0 + vec6;
        reg1 = reg0 + vec6;
        vec2 = vec1 + vec6;
        reg2 = reg1 + vec6;
        vec3 = vec2 + vec6;
        reg3 = reg2 + vec6;

        SRA_4V(vec0, vec1, vec2, vec3, 5);
        SRA_4V(reg0, reg1, reg2, reg3, 5);
        PCKEV_H2_SH(vec1, vec0, vec3, vec2, vec9, vec10);
        PCKEV_H2_SH(reg1, reg0, reg3, reg2, vec11, vec12);
        CLIP_SH2_0_255(vec9, vec10);
        CLIP_SH2_0_255(vec11, vec12);
        PCKEV_B2_UB(vec10, vec9, vec12, vec11, store0, store1);
        ST_UB2(store0, store1, src, stride);
        src += 2 * stride;

        vec4 += vec5;
    }
}

static void intra_predict_dc_4blk_8x8_msa(uint8_t *src, int32_t stride)
{
    uint32_t src0, src1, src3, src2;
    uint32_t out0, out1, out2, out3;
    uint64_t store0, store1;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h((v16u8) src_top, (v16u8) src_top);
    sum = __msa_hadd_u_w(add, add);
    src0 = __msa_copy_u_w((v4i32) sum, 0);
    src1 = __msa_copy_u_w((v4i32) sum, 1);
    src0 += src[0 * stride - 1];
    src0 += src[1 * stride - 1];
    src0 += src[2 * stride - 1];
    src0 += src[3 * stride - 1];
    src2  = src[4 * stride - 1];
    src2 += src[5 * stride - 1];
    src2 += src[6 * stride - 1];
    src2 += src[7 * stride - 1];
    src0 = (src0 + 4) >> 3;
    src3 = (src1 + src2 + 4) >> 3;
    src1 = (src1 + 2) >> 2;
    src2 = (src2 + 2) >> 2;
    out0 = src0 * 0x01010101;
    out1 = src1 * 0x01010101;
    out2 = src2 * 0x01010101;
    out3 = src3 * 0x01010101;
    store0 = ((uint64_t) out1 << 32) | out0;
    store1 = ((uint64_t) out3 << 32) | out2;

    SD4(store0, store0, store0, store0, src, stride);
    src += (4 * stride);
    SD4(store1, store1, store1, store1, src, stride);
}

static void intra_predict_hor_dc_8x8_msa(uint8_t *src, int32_t stride)
{
    uint32_t src0, src1;
    uint64_t out0, out1;

    src0  = src[0 * stride - 1];
    src0 += src[1 * stride - 1];
    src0 += src[2 * stride - 1];
    src0 += src[3 * stride - 1];
    src1  = src[4 * stride - 1];
    src1 += src[5 * stride - 1];
    src1 += src[6 * stride - 1];
    src1 += src[7 * stride - 1];
    src0 = (src0 + 2) >> 2;
    src1 = (src1 + 2) >> 2;
    out0 = src0 * 0x0101010101010101;
    out1 = src1 * 0x0101010101010101;

    SD4(out0, out0, out0, out0, src, stride);
    src += (4 * stride);
    SD4(out1, out1, out1, out1, src, stride);
}

static void intra_predict_vert_dc_8x8_msa(uint8_t *src, int32_t stride)
{
    uint64_t out0;
    v16i8 mask = { 0, 0, 0, 0, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 src_top, res0;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h(src_top, src_top);
    sum = __msa_hadd_u_w(add, add);
    sum = (v4u32) __msa_srari_w((v4i32) sum, 2);
    res0 = (v16u8) __msa_vshf_b(mask, (v16i8) sum, (v16i8) sum);
    out0 = __msa_copy_u_d((v2i64) res0, 0);

    SD4(out0, out0, out0, out0, src, stride);
    src += (4 * stride);
    SD4(out0, out0, out0, out0, src, stride);
}

static void intra_predict_mad_cow_dc_l0t_8x8_msa(uint8_t *src, int32_t stride)
{
    uint32_t src0, src1, src2;
    uint32_t out0, out1, out2;
    uint64_t store0, store1;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h(src_top, src_top);
    sum = __msa_hadd_u_w(add, add);
    src0 = __msa_copy_u_w((v4i32) sum, 0);
    src1 = __msa_copy_u_w((v4i32) sum, 1);

    src2  = src[0 * stride - 1];
    src2 += src[1 * stride - 1];
    src2 += src[2 * stride - 1];
    src2 += src[3 * stride - 1];
    src2 = (src0 + src2 + 4) >> 3;
    src0 = (src0 + 2) >> 2;
    src1 = (src1 + 2) >> 2;
    out0 = src0 * 0x01010101;
    out1 = src1 * 0x01010101;
    out2 = src2 * 0x01010101;
    store1 = ((uint64_t) out1 << 32);
    store0 = store1 | ((uint64_t) out2);
    store1 = store1 | ((uint64_t) out0);

    SD4(store0, store0, store0, store0, src, stride);
    src += (4 * stride);
    SD4(store1, store1, store1, store1, src, stride);
}

static void intra_predict_mad_cow_dc_0lt_8x8_msa(uint8_t *src, int32_t stride)
{
    uint32_t src0, src1, src2, src3;
    uint32_t out0, out1, out2, out3;
    uint64_t store0, store1;
    v16u8 src_top;
    v8u16 add;
    v4u32 sum;

    src_top = LD_UB(src - stride);
    add = __msa_hadd_u_h(src_top, src_top);
    sum = __msa_hadd_u_w(add, add);
    src0 = __msa_copy_u_w((v4i32) sum, 0);
    src1 = __msa_copy_u_w((v4i32) sum, 1);

    src2  = src[4 * stride - 1];
    src2 += src[5 * stride - 1];
    src2 += src[6 * stride - 1];
    src2 += src[7 * stride - 1];
    src0 = (src0 + 2) >> 2;
    src3 = (src1 + src2 + 4) >> 3;
    src1 = (src1 + 2) >> 2;
    src2 = (src2 + 2) >> 2;

    out0 = src0 * 0x01010101;
    out1 = src1 * 0x01010101;
    out2 = src2 * 0x01010101;
    out3 = src3 * 0x01010101;
    store0 = ((uint64_t) out1 << 32) | out0;
    store1 = ((uint64_t) out3 << 32) | out2;

    SD4(store0, store0, store0, store0, src, stride);
    src += (4 * stride);
    SD4(store1, store1, store1, store1, src, stride);
}

static void intra_predict_mad_cow_dc_l00_8x8_msa(uint8_t *src, int32_t stride)
{
    uint32_t src0;
    uint64_t out0, out1;

    src0  = src[0 * stride - 1];
    src0 += src[1 * stride - 1];
    src0 += src[2 * stride - 1];
    src0 += src[3 * stride - 1];
    src0 = (src0 + 2) >> 2;
    out0 = src0 * 0x0101010101010101;
    out1 = 0x8080808080808080;

    SD4(out0, out0, out0, out0, src, stride);
    src += (4 * stride);
    SD4(out1, out1, out1, out1, src, stride);
}

static void intra_predict_mad_cow_dc_0l0_8x8_msa(uint8_t *src, int32_t stride)
{
    uint32_t src0;
    uint64_t out0, out1;

    src0  = src[4 * stride - 1];
    src0 += src[5 * stride - 1];
    src0 += src[6 * stride - 1];
    src0 += src[7 * stride - 1];
    src0 = (src0 + 2) >> 2;

    out0 = 0x8080808080808080;
    out1 = src0 * 0x0101010101010101;

    SD4(out0, out0, out0, out0, src, stride);
    src += (4 * stride);
    SD4(out1, out1, out1, out1, src, stride);
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
    uint32_t addition = 0;
    v16u8 src_above, out;
    v8u16 sum_above;
    v4u32 sum_top;
    v2u64 sum;

    src_above = LD_UB(src_top);

    sum_above = __msa_hadd_u_h(src_above, src_above);
    sum_top = __msa_hadd_u_w(sum_above, sum_above);
    sum = __msa_hadd_u_d(sum_top, sum_top);
    sum_top = (v4u32) __msa_pckev_w((v4i32) sum, (v4i32) sum);
    sum = __msa_hadd_u_d(sum_top, sum_top);
    addition = __msa_copy_u_w((v4i32) sum, 0);
    addition += src_left[ 0 * stride];
    addition += src_left[ 1 * stride];
    addition += src_left[ 2 * stride];
    addition += src_left[ 3 * stride];
    addition += src_left[ 4 * stride];
    addition += src_left[ 5 * stride];
    addition += src_left[ 6 * stride];
    addition += src_left[ 7 * stride];
    addition += src_left[ 8 * stride];
    addition += src_left[ 9 * stride];
    addition += src_left[10 * stride];
    addition += src_left[11 * stride];
    addition += src_left[12 * stride];
    addition += src_left[13 * stride];
    addition += src_left[14 * stride];
    addition += src_left[15 * stride];
    addition = (addition + 16) >> 5;
    out = (v16u8) __msa_fill_b(addition);

    ST_UB8(out, out, out, out, out, out, out, out, dst, stride);
    dst += (8 * stride);
    ST_UB8(out, out, out, out, out, out, out, out, dst, stride);
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
    uint8_t *src_left = src - 1;
    uint8_t *dst = src;
    uint32_t addition;
    v16u8 out;

    addition  = src_left[ 0 * stride];
    addition += src_left[ 1 * stride];
    addition += src_left[ 2 * stride];
    addition += src_left[ 3 * stride];
    addition += src_left[ 4 * stride];
    addition += src_left[ 5 * stride];
    addition += src_left[ 6 * stride];
    addition += src_left[ 7 * stride];
    addition += src_left[ 8 * stride];
    addition += src_left[ 9 * stride];
    addition += src_left[10 * stride];
    addition += src_left[11 * stride];
    addition += src_left[12 * stride];
    addition += src_left[13 * stride];
    addition += src_left[14 * stride];
    addition += src_left[15 * stride];

    addition = (addition + 8) >> 4;
    out = (v16u8) __msa_fill_b(addition);

    ST_UB8(out, out, out, out, out, out, out, out, dst, stride);
    dst += (8 * stride);
    ST_UB8(out, out, out, out, out, out, out, out, dst, stride);
}

void ff_h264_intra_pred_dc_top_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    uint8_t *src_top = src - stride;
    uint8_t *dst = src;
    v16u8 src_above, out;
    v8u16 sum_above;
    v4u32 sum_top;
    v2u64 sum;

    src_above = LD_UB(src_top);

    sum_above = __msa_hadd_u_h(src_above, src_above);
    sum_top = __msa_hadd_u_w(sum_above, sum_above);
    sum = __msa_hadd_u_d(sum_top, sum_top);
    sum_top = (v4u32) __msa_pckev_w((v4i32) sum, (v4i32) sum);
    sum = __msa_hadd_u_d(sum_top, sum_top);
    sum = (v2u64) __msa_srari_d((v2i64) sum, 4);
    out = (v16u8) __msa_splati_b((v16i8) sum, 0);

    ST_UB8(out, out, out, out, out, out, out, out, dst, stride);
    dst += (8 * stride);
    ST_UB8(out, out, out, out, out, out, out, out, dst, stride);
}

void ff_h264_intra_pred_dc_128_8x8_msa(uint8_t *src, ptrdiff_t stride)
{
    uint64_t out;
    v16u8 store;

    store = (v16u8) __msa_fill_b(128);
    out = __msa_copy_u_d((v2i64) store, 0);

    SD4(out, out, out, out, src, stride);
    src += (4 * stride);
    SD4(out, out, out, out, src, stride);
}

void ff_h264_intra_pred_dc_128_16x16_msa(uint8_t *src, ptrdiff_t stride)
{
    v16u8 out;

    out = (v16u8) __msa_fill_b(128);

    ST_UB8(out, out, out, out, out, out, out, out, src, stride);
    src += (8 * stride);
    ST_UB8(out, out, out, out, out, out, out, out, src, stride);
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
