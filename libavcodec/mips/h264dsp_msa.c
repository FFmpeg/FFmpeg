/*
 * Copyright (c) 2015 - 2017 Parag Salasakar (Parag.Salasakar@imgtec.com)
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

static void avc_wgt_4x2_msa(uint8_t *data, int32_t stride,
                            int32_t log2_denom, int32_t src_weight,
                            int32_t offset_in)
{
    uint32_t tp0, tp1, offset_val;
    v16u8 zero = { 0 };
    v16u8 src0 = { 0 };
    v8i16 src0_r, tmp0, wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(src_weight);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    LW2(data, stride, tp0, tp1);
    INSERT_W2_UB(tp0, tp1, src0);
    src0_r = (v8i16) __msa_ilvr_b((v16i8) zero, (v16i8) src0);
    tmp0 = wgt * src0_r;
    tmp0 = __msa_adds_s_h(tmp0, offset);
    tmp0 = __msa_maxi_s_h(tmp0, 0);
    tmp0 = __msa_srlr_h(tmp0, denom);
    tmp0 = (v8i16) __msa_sat_u_h((v8u16) tmp0, 7);
    src0 = (v16u8) __msa_pckev_b((v16i8) tmp0, (v16i8) tmp0);
    ST_W2(src0, 0, 1, data, stride);
}

static void avc_wgt_4x4_msa(uint8_t *data, int32_t stride, int32_t log2_denom,
                            int32_t src_weight, int32_t offset_in)
{
    uint32_t tp0, tp1, tp2, tp3, offset_val;
    v16u8 src0 = { 0 };
    v8i16 src0_r, src1_r, tmp0, tmp1, wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(src_weight);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    LW4(data, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src0);
    UNPCK_UB_SH(src0, src0_r, src1_r);
    MUL2(wgt, src0_r, wgt, src1_r, tmp0, tmp1);
    ADDS_SH2_SH(tmp0, offset, tmp1, offset, tmp0, tmp1);
    MAXI_SH2_SH(tmp0, tmp1, 0);
    tmp0 = __msa_srlr_h(tmp0, denom);
    tmp1 = __msa_srlr_h(tmp1, denom);
    SAT_UH2_SH(tmp0, tmp1, 7);
    src0 = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_W4(src0, 0, 1, 2, 3, data, stride);
}

static void avc_wgt_4x8_msa(uint8_t *data, int32_t stride, int32_t log2_denom,
                            int32_t src_weight, int32_t offset_in)
{
    uint32_t tp0, tp1, tp2, tp3, offset_val;
    v16u8 src0 = { 0 }, src1 = { 0 };
    v8i16 src0_r, src1_r, src2_r, src3_r, tmp0, tmp1, tmp2, tmp3;
    v8i16 wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(src_weight);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    LW4(data, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src0);
    LW4(data + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src1);
    UNPCK_UB_SH(src0, src0_r, src1_r);
    UNPCK_UB_SH(src1, src2_r, src3_r);
    MUL4(wgt, src0_r, wgt, src1_r, wgt, src2_r, wgt, src3_r, tmp0, tmp1, tmp2,
         tmp3);
    ADDS_SH4_SH(tmp0, offset, tmp1, offset, tmp2, offset, tmp3, offset, tmp0,
                tmp1, tmp2, tmp3);
    MAXI_SH4_SH(tmp0, tmp1, tmp2, tmp3, 0);
    SRLR_H4_SH(tmp0, tmp1, tmp2, tmp3, denom);
    SAT_UH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, src0, src1);
    ST_W8(src0, src1, 0, 1, 2, 3, 0, 1, 2, 3, data, stride);
}

static void avc_wgt_8x4_msa(uint8_t *data, int32_t stride, int32_t log2_denom,
                            int32_t src_weight, int32_t offset_in)
{
    uint32_t offset_val;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 src0 = { 0 }, src1 = { 0 };
    v8i16 src0_r, src1_r, src2_r, src3_r, tmp0, tmp1, tmp2, tmp3;
    v8i16 wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(src_weight);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    LD4(data, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, src0);
    INSERT_D2_UB(tp2, tp3, src1);
    UNPCK_UB_SH(src0, src0_r, src1_r);
    UNPCK_UB_SH(src1, src2_r, src3_r);
    MUL4(wgt, src0_r, wgt, src1_r, wgt, src2_r, wgt, src3_r, tmp0, tmp1, tmp2,
         tmp3);
    ADDS_SH4_SH(tmp0, offset, tmp1, offset, tmp2, offset, tmp3, offset, tmp0,
                tmp1, tmp2, tmp3);
    MAXI_SH4_SH(tmp0, tmp1, tmp2, tmp3, 0);
    SRLR_H4_SH(tmp0, tmp1, tmp2, tmp3, denom);
    SAT_UH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, src0, src1);
    ST_D4(src0, src1, 0, 1, 0, 1, data, stride);
}

static void avc_wgt_8x8_msa(uint8_t *data, int32_t stride, int32_t log2_denom,
                            int32_t src_weight, int32_t offset_in)
{
    uint32_t offset_val;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 src0 = { 0 }, src1 = { 0 }, src2 = { 0 }, src3 = { 0 };
    v8i16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(src_weight);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    LD4(data, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, src0);
    INSERT_D2_UB(tp2, tp3, src1);
    LD4(data + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, src2);
    INSERT_D2_UB(tp2, tp3, src3);
    UNPCK_UB_SH(src0, src0_r, src1_r);
    UNPCK_UB_SH(src1, src2_r, src3_r);
    UNPCK_UB_SH(src2, src4_r, src5_r);
    UNPCK_UB_SH(src3, src6_r, src7_r);
    MUL4(wgt, src0_r, wgt, src1_r, wgt, src2_r, wgt, src3_r, tmp0, tmp1, tmp2,
         tmp3);
    MUL4(wgt, src4_r, wgt, src5_r, wgt, src6_r, wgt, src7_r, tmp4, tmp5, tmp6,
         tmp7);
    ADDS_SH4_SH(tmp0, offset, tmp1, offset, tmp2, offset, tmp3, offset, tmp0,
                tmp1, tmp2, tmp3);
    ADDS_SH4_SH(tmp4, offset, tmp5, offset, tmp6, offset, tmp7, offset, tmp4,
                tmp5, tmp6, tmp7);
    MAXI_SH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 0);
    SRLR_H8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom);
    SAT_UH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 7);
    PCKEV_B4_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, src0, src1,
                src2, src3);
    ST_D8(src0, src1, src2, src3, 0, 1, 0, 1, 0, 1, 0, 1, data, stride);
}

static void avc_wgt_8x16_msa(uint8_t *data, int32_t stride, int32_t log2_denom,
                             int32_t src_weight, int32_t offset_in)
{
    uint32_t offset_val, cnt;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 src0 = { 0 }, src1 = { 0 }, src2 = { 0 }, src3 = { 0 };
    v8i16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(src_weight);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    for (cnt = 2; cnt--;) {
        LD4(data, stride, tp0, tp1, tp2, tp3);
        INSERT_D2_UB(tp0, tp1, src0);
        INSERT_D2_UB(tp2, tp3, src1);
        LD4(data + 4 * stride, stride, tp0, tp1, tp2, tp3);
        INSERT_D2_UB(tp0, tp1, src2);
        INSERT_D2_UB(tp2, tp3, src3);
        UNPCK_UB_SH(src0, src0_r, src1_r);
        UNPCK_UB_SH(src1, src2_r, src3_r);
        UNPCK_UB_SH(src2, src4_r, src5_r);
        UNPCK_UB_SH(src3, src6_r, src7_r);
        MUL4(wgt, src0_r, wgt, src1_r, wgt, src2_r, wgt, src3_r, tmp0, tmp1,
             tmp2, tmp3);
        MUL4(wgt, src4_r, wgt, src5_r, wgt, src6_r, wgt, src7_r, tmp4, tmp5,
             tmp6, tmp7);
        ADDS_SH4_SH(tmp0, offset, tmp1, offset, tmp2, offset, tmp3, offset,
                    tmp0, tmp1, tmp2, tmp3);
        ADDS_SH4_SH(tmp4, offset, tmp5, offset, tmp6, offset, tmp7, offset,
                    tmp4, tmp5, tmp6, tmp7);
        MAXI_SH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 0);
        SRLR_H8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom);
        SAT_UH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 7);
        PCKEV_B4_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, src0, src1,
                    src2, src3);
        ST_D8(src0, src1, src2, src3, 0, 1, 0, 1, 0, 1, 0, 1, data, stride);
        data += 8 * stride;
    }
}

static void avc_biwgt_4x2_msa(uint8_t *src, uint8_t *dst, int32_t stride,
                              int32_t log2_denom, int32_t src_weight,
                              int32_t dst_weight, int32_t offset_in)
{
    uint32_t tp0, tp1;
    v16i8 src_wgt, dst_wgt, wgt, vec0;
    v16u8 src0 = { 0 }, dst0 = { 0 };
    v8i16 tmp0, denom, offset, max255 = __msa_ldi_h(255);

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (src_weight + dst_weight));

    src_wgt = __msa_fill_b(src_weight);
    dst_wgt = __msa_fill_b(dst_weight);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);

    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    LW2(src, stride, tp0, tp1);
    INSERT_W2_UB(tp0, tp1, src0);
    LW2(dst, stride, tp0, tp1);
    INSERT_W2_UB(tp0, tp1, dst0);
    XORI_B2_128_UB(src0, dst0);
    vec0 = (v16i8) __msa_ilvr_b((v16i8) dst0, (v16i8) src0);
    tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
    tmp0 >>= denom;
    tmp0 = __msa_maxi_s_h(tmp0, 0);
    tmp0 = __msa_min_s_h(max255, tmp0);
    dst0 = (v16u8) __msa_pckev_b((v16i8) tmp0, (v16i8) tmp0);
    ST_W2(dst0, 0, 1, dst, stride);
}

static void avc_biwgt_4x4_msa(uint8_t *src, uint8_t *dst, int32_t stride,
                              int32_t log2_denom, int32_t src_weight,
                              int32_t dst_weight, int32_t offset_in)
{
    uint32_t tp0, tp1, tp2, tp3;
    v16i8 src_wgt, dst_wgt, wgt, vec0, vec1;
    v16u8 src0, dst0;
    v8i16 tmp0, tmp1, denom, offset;

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (src_weight + dst_weight));

    src_wgt = __msa_fill_b(src_weight);
    dst_wgt = __msa_fill_b(dst_weight);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);

    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    LW4(src, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src0);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    XORI_B2_128_UB(src0, dst0);
    ILVRL_B2_SB(dst0, src0, vec0, vec1);
    tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
    tmp1 = __msa_dpadd_s_h(offset, wgt, vec1);
    tmp0 >>= denom;
    tmp1 >>= denom;
    CLIP_SH2_0_255(tmp0, tmp1);
    dst0 = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

static void avc_biwgt_4x8_msa(uint8_t *src, uint8_t *dst, int32_t stride,
                              int32_t log2_denom, int32_t src_weight,
                              int32_t dst_weight, int32_t offset_in)
{
    uint32_t tp0, tp1, tp2, tp3;
    v16i8 src_wgt, dst_wgt, wgt, vec0, vec1, vec2, vec3;
    v16u8 src0, src1, dst0, dst1;
    v8i16 tmp0, tmp1, tmp2, tmp3, denom, offset;

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (src_weight + dst_weight));

    src_wgt = __msa_fill_b(src_weight);
    dst_wgt = __msa_fill_b(dst_weight);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);
    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    LW4(src, stride, tp0, tp1, tp2, tp3);
    src += 4 * stride;
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src0);
    LW4(src, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src1);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    LW4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst1);
    XORI_B4_128_UB(src0, src1, dst0, dst1);
    ILVRL_B2_SB(dst0, src0, vec0, vec1);
    ILVRL_B2_SB(dst1, src1, vec2, vec3);
    tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
    tmp1 = __msa_dpadd_s_h(offset, wgt, vec1);
    tmp2 = __msa_dpadd_s_h(offset, wgt, vec2);
    tmp3 = __msa_dpadd_s_h(offset, wgt, vec3);
    SRA_4V(tmp0, tmp1, tmp2, tmp3, denom);
    CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, dst0, dst1);
    ST_W8(dst0, dst1, 0, 1, 2, 3, 0, 1, 2, 3, dst, stride);
}

static void avc_biwgt_8x4_msa(uint8_t *src, uint8_t *dst, int32_t stride,
                              int32_t log2_denom, int32_t src_weight,
                              int32_t dst_weight, int32_t offset_in)
{
    uint64_t tp0, tp1, tp2, tp3;
    v16i8 src_wgt, dst_wgt, wgt, vec0, vec1, vec2, vec3;
    v16u8 src0, src1, dst0, dst1;
    v8i16 tmp0, tmp1, tmp2, tmp3, denom, offset;

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (src_weight + dst_weight));

    src_wgt = __msa_fill_b(src_weight);
    dst_wgt = __msa_fill_b(dst_weight);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);

    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    LD4(src, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, src0);
    INSERT_D2_UB(tp2, tp3, src1);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    XORI_B4_128_UB(src0, src1, dst0, dst1);
    ILVRL_B2_SB(dst0, src0, vec0, vec1);
    ILVRL_B2_SB(dst1, src1, vec2, vec3);
    tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
    tmp1 = __msa_dpadd_s_h(offset, wgt, vec1);
    tmp2 = __msa_dpadd_s_h(offset, wgt, vec2);
    tmp3 = __msa_dpadd_s_h(offset, wgt, vec3);
    SRA_4V(tmp0, tmp1, tmp2, tmp3, denom);
    CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
}

static void avc_biwgt_8x8_msa(uint8_t *src, uint8_t *dst, int32_t stride,
                              int32_t log2_denom, int32_t src_weight,
                              int32_t dst_weight, int32_t offset_in)
{
    uint64_t tp0, tp1, tp2, tp3;
    v16i8 src_wgt, dst_wgt, wgt, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16u8 src0, src1, src2, src3, dst0, dst1, dst2, dst3;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom, offset;

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (src_weight + dst_weight));

    src_wgt = __msa_fill_b(src_weight);
    dst_wgt = __msa_fill_b(dst_weight);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);
    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    LD4(src, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, src0);
    INSERT_D2_UB(tp2, tp3, src1);
    LD4(src + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, src2);
    INSERT_D2_UB(tp2, tp3, src3);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst2);
    INSERT_D2_UB(tp2, tp3, dst3);
    XORI_B8_128_UB(src0, src1, src2, src3, dst0, dst1, dst2, dst3);
    ILVRL_B2_SB(dst0, src0, vec0, vec1);
    ILVRL_B2_SB(dst1, src1, vec2, vec3);
    ILVRL_B2_SB(dst2, src2, vec4, vec5);
    ILVRL_B2_SB(dst3, src3, vec6, vec7);
    tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
    tmp1 = __msa_dpadd_s_h(offset, wgt, vec1);
    tmp2 = __msa_dpadd_s_h(offset, wgt, vec2);
    tmp3 = __msa_dpadd_s_h(offset, wgt, vec3);
    tmp4 = __msa_dpadd_s_h(offset, wgt, vec4);
    tmp5 = __msa_dpadd_s_h(offset, wgt, vec5);
    tmp6 = __msa_dpadd_s_h(offset, wgt, vec6);
    tmp7 = __msa_dpadd_s_h(offset, wgt, vec7);
    SRA_4V(tmp0, tmp1, tmp2, tmp3, denom);
    SRA_4V(tmp4, tmp5, tmp6, tmp7, denom);
    CLIP_SH8_0_255(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, dst0, dst1);
    PCKEV_B2_UB(tmp5, tmp4, tmp7, tmp6, dst2, dst3);
    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

static void avc_biwgt_8x16_msa(uint8_t *src, uint8_t *dst, int32_t stride,
                               int32_t log2_denom, int32_t src_weight,
                               int32_t dst_weight, int32_t offset_in)
{
    uint8_t cnt;
    uint64_t tp0, tp1, tp2, tp3;
    v16i8 src_wgt, dst_wgt, wgt;
    v16u8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
    v8i16 denom, offset;

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (src_weight + dst_weight));

    src_wgt = __msa_fill_b(src_weight);
    dst_wgt = __msa_fill_b(dst_weight);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);
    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    for (cnt = 2; cnt--;) {
        LD4(src, stride, tp0, tp1, tp2, tp3);
        src += 4 * stride;
        INSERT_D2_UB(tp0, tp1, src0);
        INSERT_D2_UB(tp2, tp3, src1);
        LD4(src, stride, tp0, tp1, tp2, tp3);
        src += 4 * stride;
        INSERT_D2_UB(tp0, tp1, src2);
        INSERT_D2_UB(tp2, tp3, src3);
        LD4(dst, stride, tp0, tp1, tp2, tp3);
        INSERT_D2_UB(tp0, tp1, dst0);
        INSERT_D2_UB(tp2, tp3, dst1);
        LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
        INSERT_D2_UB(tp0, tp1, dst2);
        INSERT_D2_UB(tp2, tp3, dst3);
        XORI_B4_128_UB(src0, src1, src2, src3);
        XORI_B4_128_UB(dst0, dst1, dst2, dst3);
        ILVR_B4_SB(dst0, src0, dst1, src1, dst2, src2, dst3, src3,
                   vec0, vec2, vec4, vec6);
        ILVL_B4_SB(dst0, src0, dst1, src1, dst2, src2, dst3, src3,
                   vec1, vec3, vec5, vec7);

        temp0 = __msa_dpadd_s_h(offset, wgt, vec0);
        temp1 = __msa_dpadd_s_h(offset, wgt, vec1);
        temp2 = __msa_dpadd_s_h(offset, wgt, vec2);
        temp3 = __msa_dpadd_s_h(offset, wgt, vec3);
        temp4 = __msa_dpadd_s_h(offset, wgt, vec4);
        temp5 = __msa_dpadd_s_h(offset, wgt, vec5);
        temp6 = __msa_dpadd_s_h(offset, wgt, vec6);
        temp7 = __msa_dpadd_s_h(offset, wgt, vec7);

        SRA_4V(temp0, temp1, temp2, temp3, denom);
        SRA_4V(temp4, temp5, temp6, temp7, denom);
        CLIP_SH8_0_255(temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7);
        PCKEV_B4_UB(temp1, temp0, temp3, temp2, temp5, temp4, temp7, temp6,
                    dst0, dst1, dst2, dst3);
        ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
        dst += 8 * stride;
    }
}

#define AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_or_q3_org_in, p0_or_q0_org_in,          \
                                 q3_or_p3_org_in, p1_or_q1_org_in,          \
                                 p2_or_q2_org_in, q1_or_p1_org_in,          \
                                 p0_or_q0_out, p1_or_q1_out, p2_or_q2_out)  \
{                                                                           \
    v8i16 threshold;                                                        \
    v8i16 const3 = __msa_ldi_h(3);                                          \
                                                                            \
    threshold = (p0_or_q0_org_in) + (q3_or_p3_org_in);                      \
    threshold += (p1_or_q1_org_in);                                         \
                                                                            \
    (p0_or_q0_out) = threshold << 1;                                        \
    (p0_or_q0_out) += (p2_or_q2_org_in);                                    \
    (p0_or_q0_out) += (q1_or_p1_org_in);                                    \
    (p0_or_q0_out) = __msa_srari_h((p0_or_q0_out), 3);                      \
                                                                            \
    (p1_or_q1_out) = (p2_or_q2_org_in) + threshold;                         \
    (p1_or_q1_out) = __msa_srari_h((p1_or_q1_out), 2);                      \
                                                                            \
    (p2_or_q2_out) = (p2_or_q2_org_in) * const3;                            \
    (p2_or_q2_out) += (p3_or_q3_org_in);                                    \
    (p2_or_q2_out) += (p3_or_q3_org_in);                                    \
    (p2_or_q2_out) += threshold;                                            \
    (p2_or_q2_out) = __msa_srari_h((p2_or_q2_out), 3);                      \
}

/* data[-u32_img_width] = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2); */
#define AVC_LPF_P0_OR_Q0(p0_or_q0_org_in, q1_or_p1_org_in,   \
                         p1_or_q1_org_in, p0_or_q0_out)      \
{                                                            \
    (p0_or_q0_out) = (p0_or_q0_org_in) + (q1_or_p1_org_in);  \
    (p0_or_q0_out) += (p1_or_q1_org_in);                     \
    (p0_or_q0_out) += (p1_or_q1_org_in);                     \
    (p0_or_q0_out) = __msa_srari_h((p0_or_q0_out), 2);       \
}

#define AVC_LPF_P1_OR_Q1(p0_or_q0_org_in, q0_or_p0_org_in,    \
                         p1_or_q1_org_in, p2_or_q2_org_in,    \
                         negate_tc_in, tc_in, p1_or_q1_out)   \
{                                                             \
    v8i16 clip3, temp;                                        \
                                                              \
    clip3 = (v8i16) __msa_aver_u_h((v8u16) p0_or_q0_org_in,   \
                                   (v8u16) q0_or_p0_org_in);  \
    temp = p1_or_q1_org_in << 1;                              \
    clip3 = clip3 - temp;                                     \
    clip3 = __msa_ave_s_h(p2_or_q2_org_in, clip3);            \
    CLIP_SH(clip3, negate_tc_in, tc_in);                      \
    p1_or_q1_out = p1_or_q1_org_in + clip3;                   \
}

#define AVC_LPF_P0Q0(q0_or_p0_org_in, p0_or_q0_org_in,          \
                     p1_or_q1_org_in, q1_or_p1_org_in,          \
                     negate_threshold_in, threshold_in,         \
                     p0_or_q0_out, q0_or_p0_out)                \
{                                                               \
    v8i16 q0_sub_p0, p1_sub_q1, delta;                          \
                                                                \
    q0_sub_p0 = q0_or_p0_org_in - p0_or_q0_org_in;              \
    p1_sub_q1 = p1_or_q1_org_in - q1_or_p1_org_in;              \
    q0_sub_p0 <<= 2;                                            \
    p1_sub_q1 += 4;                                             \
    delta = q0_sub_p0 + p1_sub_q1;                              \
    delta >>= 3;                                                \
                                                                \
    CLIP_SH(delta, negate_threshold_in, threshold_in);          \
                                                                \
    p0_or_q0_out = p0_or_q0_org_in + delta;                     \
    q0_or_p0_out = q0_or_p0_org_in - delta;                     \
                                                                \
    CLIP_SH2_0_255(p0_or_q0_out, q0_or_p0_out);                 \
}

#define AVC_LPF_H_CHROMA_422(src, stride, tc_val, alpha, beta, res)      \
{                                                                        \
    uint32_t load0, load1, load2, load3;                                 \
    v16u8 src0 = { 0 };                                                  \
    v16u8 src1 = { 0 };                                                  \
    v16u8 src2 = { 0 };                                                  \
    v16u8 src3 = { 0 };                                                  \
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;                            \
    v16u8 is_less_than, is_less_than_alpha, is_less_than_beta;           \
    v8i16 tc, q0_sub_p0, p1_sub_q1, delta;                               \
    v8i16 res0_r, res1_r;                                                \
    v16i8 zeros = { 0 };                                                 \
    v16u8 res0, res1;                                                    \
                                                                         \
    LW4((src - 2), stride, load0, load1, load2, load3);                  \
    src0 = (v16u8) __msa_insert_w((v4i32) src0, 0, load0);               \
    src1 = (v16u8) __msa_insert_w((v4i32) src1, 0, load1);               \
    src2 = (v16u8) __msa_insert_w((v4i32) src2, 0, load2);               \
    src3 = (v16u8) __msa_insert_w((v4i32) src3, 0, load3);               \
                                                                         \
    TRANSPOSE4x4_UB_UB(src0, src1, src2, src3, src0, src1, src2, src3);  \
                                                                         \
    p0_asub_q0 = __msa_asub_u_b(src2, src1);                             \
    p1_asub_p0 = __msa_asub_u_b(src1, src0);                             \
    q1_asub_q0 = __msa_asub_u_b(src2, src3);                             \
                                                                         \
    tc = __msa_fill_h(tc_val);                                           \
                                                                         \
    is_less_than_alpha = (p0_asub_q0 < alpha);                           \
    is_less_than_beta = (p1_asub_p0 < beta);                             \
    is_less_than = is_less_than_alpha & is_less_than_beta;               \
    is_less_than_beta = (q1_asub_q0 < beta);                             \
    is_less_than = is_less_than_beta & is_less_than;                     \
                                                                         \
    ILVR_B2_SH(src2, src1, src0, src3, q0_sub_p0, p1_sub_q1);            \
    HSUB_UB2_SH(q0_sub_p0, p1_sub_q1, q0_sub_p0, p1_sub_q1);             \
                                                                         \
    q0_sub_p0 <<= 2;                                                     \
    delta = q0_sub_p0 + p1_sub_q1;                                       \
    delta = __msa_srari_h(delta, 3);                                     \
                                                                         \
    CLIP_SH(delta, -tc, tc);                                             \
                                                                         \
    ILVR_B2_SH(zeros, src1, zeros, src2, res0_r, res1_r);                \
                                                                         \
    res0_r += delta;                                                     \
    res1_r -= delta;                                                     \
                                                                         \
    CLIP_SH2_0_255(res0_r, res1_r);                                      \
    PCKEV_B2_UB(res0_r, res0_r, res1_r, res1_r, res0, res1);             \
                                                                         \
    res0 = __msa_bmnz_v(src1, res0, is_less_than);                       \
    res1 = __msa_bmnz_v(src2, res1, is_less_than);                       \
                                                                         \
    res = (v16u8) __msa_ilvr_b((v16i8) res1, (v16i8) res0);              \
}

#define TRANSPOSE2x4_B_UB(in0, in1, out0, out1, out2, out3)  \
{                                                            \
    v16i8 zero_m = { 0 };                                    \
                                                             \
    out0 = (v16u8) __msa_ilvr_b((v16i8) in1, (v16i8) in0);   \
    out1 = (v16u8) __msa_sldi_b(zero_m, (v16i8) out0, 2);    \
    SLDI_B2_UB(zero_m, out1, zero_m, out2, 2, out2, out3);   \
}

#define AVC_LPF_H_2BYTE_CHROMA_422(src, stride, tc_val, alpha, beta, res)  \
{                                                                          \
    uint32_t load0, load1;                                                 \
    v16u8 src0 = { 0 };                                                    \
    v16u8 src1 = { 0 };                                                    \
    v16u8 src2 = { 0 };                                                    \
    v16u8 src3 = { 0 };                                                    \
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;                              \
    v16u8 is_less_than, is_less_than_alpha, is_less_than_beta;             \
    v8i16 tc, q0_sub_p0, p1_sub_q1, delta, res0_r, res1_r;                 \
    v16i8 zeros = { 0 };                                                   \
    v16u8 res0, res1;                                                      \
                                                                           \
    load0 = LW(src - 2);                                                   \
    load1 = LW(src - 2 + stride);                                          \
                                                                           \
    src0 = (v16u8) __msa_insert_w((v4i32) src0, 0, load0);                 \
    src1 = (v16u8) __msa_insert_w((v4i32) src1, 0, load1);                 \
                                                                           \
    TRANSPOSE2x4_B_UB(src0, src1, src0, src1, src2, src3);                 \
                                                                           \
    p0_asub_q0 = __msa_asub_u_b(src2, src1);                               \
    p1_asub_p0 = __msa_asub_u_b(src1, src0);                               \
    q1_asub_q0 = __msa_asub_u_b(src2, src3);                               \
                                                                           \
    tc = __msa_fill_h(tc_val);                                             \
                                                                           \
    is_less_than_alpha = (p0_asub_q0 < alpha);                             \
    is_less_than_beta = (p1_asub_p0 < beta);                               \
    is_less_than = is_less_than_alpha & is_less_than_beta;                 \
    is_less_than_beta = (q1_asub_q0 < beta);                               \
    is_less_than = is_less_than_beta & is_less_than;                       \
                                                                           \
    ILVR_B2_SH(src2, src1, src0, src3, q0_sub_p0, p1_sub_q1);              \
    HSUB_UB2_SH(q0_sub_p0, p1_sub_q1, q0_sub_p0, p1_sub_q1);               \
                                                                           \
    q0_sub_p0 <<= 2;                                                       \
    delta = q0_sub_p0 + p1_sub_q1;                                         \
    delta = __msa_srari_h(delta, 3);                                       \
    CLIP_SH(delta, -tc, tc);                                               \
                                                                           \
    ILVR_B2_SH(zeros, src1, zeros, src2, res0_r, res1_r);                  \
                                                                           \
    res0_r += delta;                                                       \
    res1_r -= delta;                                                       \
                                                                           \
    CLIP_SH2_0_255(res0_r, res1_r);                                        \
    PCKEV_B2_UB(res0_r, res0_r, res1_r, res1_r, res0, res1);               \
                                                                           \
    res0 = __msa_bmnz_v(src1, res0, is_less_than);                         \
    res1 = __msa_bmnz_v(src2, res1, is_less_than);                         \
                                                                           \
    res = (v16u8) __msa_ilvr_b((v16i8) res1, (v16i8) res0);                \
}

static void avc_loopfilter_luma_intra_edge_hor_msa(uint8_t *data,
                                                   uint8_t alpha_in,
                                                   uint8_t beta_in,
                                                   uint32_t img_width)
{
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;
    v16u8 is_less_than, is_less_than_beta, is_less_than_alpha;
    v16u8 p1_org, p0_org, q0_org, q1_org;

    LD_UB4(data - (img_width << 1), img_width, p1_org, p0_org, q0_org, q1_org);

    p0_asub_q0 = __msa_asub_u_b(p0_org, q0_org);
    p1_asub_p0 = __msa_asub_u_b(p1_org, p0_org);
    q1_asub_q0 = __msa_asub_u_b(q1_org, q0_org);

    is_less_than_alpha = (p0_asub_q0 < alpha_in);
    is_less_than_beta = (p1_asub_p0 < beta_in);
    is_less_than = is_less_than_beta & is_less_than_alpha;
    is_less_than_beta = (q1_asub_q0 < beta_in);
    is_less_than = is_less_than_beta & is_less_than;

    if (!__msa_test_bz_v(is_less_than)) {
        v16u8 p2_asub_p0, q2_asub_q0, p0, q0, negate_is_less_than_beta;
        v8i16 p0_r = { 0 };
        v8i16 q0_r = { 0 };
        v8i16 p0_l = { 0 };
        v8i16 q0_l = { 0 };
        v16i8 zero = { 0 };
        v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;
        v8i16 p1_org_l, p0_org_l, q0_org_l, q1_org_l;
        v16u8 q2_org = LD_UB(data + (2 * img_width));
        v16u8 p2_org = LD_UB(data - (3 * img_width));
        v16u8 tmp_flag = (v16u8)__msa_fill_b((alpha_in >> 2) + 2);

        UNPCK_UB_SH(p1_org, p1_org_r, p1_org_l);
        UNPCK_UB_SH(p0_org, p0_org_r, p0_org_l);
        UNPCK_UB_SH(q0_org, q0_org_r, q0_org_l);

        tmp_flag = (p0_asub_q0 < tmp_flag);

        p2_asub_p0 = __msa_asub_u_b(p2_org, p0_org);
        is_less_than_beta = (p2_asub_p0 < beta_in);
        is_less_than_beta = is_less_than_beta & tmp_flag;
        negate_is_less_than_beta = __msa_xori_b(is_less_than_beta, 0xff);
        is_less_than_beta = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        q1_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) q1_org);
        q1_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) q1_org);

        /* combine and store */
        if (!__msa_test_bz_v(is_less_than_beta)) {
            v8i16 p3_org_l, p3_org_r;
            v16u8 p3_org = LD_UB(data - (img_width << 2));
            v16u8 p2, p1;
            v8i16 p2_r = { 0 };
            v8i16 p2_l = { 0 };
            v8i16 p1_r = { 0 };
            v8i16 p1_l = { 0 };

            ILVR_B2_SH(zero, p3_org, zero, p2_org, p3_org_r, p2_r);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_org_r, p0_org_r, q0_org_r, p1_org_r,
                                     p2_r, q1_org_r, p0_r, p1_r, p2_r);

            ILVL_B2_SH(zero, p3_org, zero, p2_org, p3_org_l, p2_l);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_org_l, p0_org_l, q0_org_l, p1_org_l,
                                     p2_l, q1_org_l, p0_l, p1_l, p2_l);

            PCKEV_B3_UB(p0_l, p0_r, p1_l, p1_r, p2_l, p2_r, p0, p1, p2);

            p0_org = __msa_bmnz_v(p0_org, p0, is_less_than_beta);
            p1_org = __msa_bmnz_v(p1_org, p1, is_less_than_beta);
            p2_org = __msa_bmnz_v(p2_org, p2, is_less_than_beta);

            ST_UB(p1_org, data - (2 * img_width));
            ST_UB(p2_org, data - (3 * img_width));
        }

        AVC_LPF_P0_OR_Q0(p0_org_r, q1_org_r, p1_org_r, p0_r);
        AVC_LPF_P0_OR_Q0(p0_org_l, q1_org_l, p1_org_l, p0_l);

        /* combine */
        p0 = (v16u8) __msa_pckev_b((v16i8) p0_l, (v16i8) p0_r);
        p0_org = __msa_bmnz_v(p0_org, p0, negate_is_less_than_beta);

        ST_UB(p0_org, data - img_width);

        /* if (tmpFlag && (unsigned)ABS(q2-q0) < thresholds->beta_in) */
        q2_asub_q0 = __msa_asub_u_b(q2_org, q0_org);
        is_less_than_beta = (q2_asub_q0 < beta_in);
        is_less_than_beta = is_less_than_beta & tmp_flag;
        negate_is_less_than_beta = __msa_xori_b(is_less_than_beta, 0xff);
        is_less_than_beta = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        /* combine and store */
        if (!__msa_test_bz_v(is_less_than_beta)) {
            v8i16 q3_org_r, q3_org_l;
            v16u8 q3_org = LD_UB(data + (3 * img_width));
            v16u8 q1, q2;
            v8i16 q2_r = { 0 };
            v8i16 q2_l = { 0 };
            v8i16 q1_r = { 0 };
            v8i16 q1_l = { 0 };

            ILVR_B2_SH(zero, q3_org, zero, q2_org, q3_org_r, q2_r);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(q3_org_r, q0_org_r, p0_org_r, q1_org_r,
                                     q2_r, p1_org_r, q0_r, q1_r, q2_r);

            ILVL_B2_SH(zero, q3_org, zero, q2_org, q3_org_l, q2_l);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(q3_org_l, q0_org_l, p0_org_l, q1_org_l,
                                     q2_l, p1_org_l, q0_l, q1_l, q2_l);

            PCKEV_B3_UB(q0_l, q0_r, q1_l, q1_r, q2_l, q2_r, q0, q1, q2);
            q0_org = __msa_bmnz_v(q0_org, q0, is_less_than_beta);
            q1_org = __msa_bmnz_v(q1_org, q1, is_less_than_beta);
            q2_org = __msa_bmnz_v(q2_org, q2, is_less_than_beta);

            ST_UB(q1_org, data + img_width);
            ST_UB(q2_org, data + 2 * img_width);
        }

        AVC_LPF_P0_OR_Q0(q0_org_r, p1_org_r, q1_org_r, q0_r);
        AVC_LPF_P0_OR_Q0(q0_org_l, p1_org_l, q1_org_l, q0_l);

        /* combine */
        q0 = (v16u8) __msa_pckev_b((v16i8) q0_l, (v16i8) q0_r);
        q0_org = __msa_bmnz_v(q0_org, q0, negate_is_less_than_beta);

        ST_UB(q0_org, data);
    }
}

static void avc_loopfilter_luma_intra_edge_ver_msa(uint8_t *data,
                                                   uint8_t alpha_in,
                                                   uint8_t beta_in,
                                                   uint32_t img_width)
{
    uint8_t *src = data - 4;
    v16u8 alpha, beta, p0_asub_q0;
    v16u8 is_less_than_alpha, is_less_than, is_less_than_beta;
    v16u8 p3_org, p2_org, p1_org, p0_org, q0_org, q1_org, q2_org, q3_org;
    v16u8 p1_asub_p0, q1_asub_q0;


    {
        v16u8 row0, row1, row2, row3, row4, row5, row6, row7;
        v16u8 row8, row9, row10, row11, row12, row13, row14, row15;

        LD_UB8(src, img_width, row0, row1, row2, row3, row4, row5, row6, row7);
        LD_UB8(src + (8 * img_width), img_width,
               row8, row9, row10, row11, row12, row13, row14, row15);

        TRANSPOSE16x8_UB_UB(row0, row1, row2, row3,
                            row4, row5, row6, row7,
                            row8, row9, row10, row11,
                            row12, row13, row14, row15,
                            p3_org, p2_org, p1_org, p0_org,
                            q0_org, q1_org, q2_org, q3_org);
    }

    p0_asub_q0 = __msa_asub_u_b(p0_org, q0_org);
    p1_asub_p0 = __msa_asub_u_b(p1_org, p0_org);
    q1_asub_q0 = __msa_asub_u_b(q1_org, q0_org);

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    is_less_than_alpha = (p0_asub_q0 < alpha);
    is_less_than_beta = (p1_asub_p0 < beta);
    is_less_than = is_less_than_beta & is_less_than_alpha;
    is_less_than_beta = (q1_asub_q0 < beta);
    is_less_than = is_less_than_beta & is_less_than;

    if (!__msa_test_bz_v(is_less_than)) {
        v8i16 p0_r = { 0 };
        v8i16 q0_r = { 0 };
        v8i16 p0_l = { 0 };
        v8i16 q0_l = { 0 };
        v16i8 zero = { 0 };
        v16u8 tmp_flag, p0, q0, p2_asub_p0, q2_asub_q0;
        v16u8 negate_is_less_than_beta;
        v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;
        v8i16 p1_org_l, p0_org_l, q0_org_l, q1_org_l;

        UNPCK_UB_SH(p1_org, p1_org_r, p1_org_l);
        UNPCK_UB_SH(p0_org, p0_org_r, p0_org_l);
        UNPCK_UB_SH(q0_org, q0_org_r, q0_org_l);
        UNPCK_UB_SH(q1_org, q1_org_r, q1_org_l);

        tmp_flag = alpha >> 2;
        tmp_flag = tmp_flag + 2;
        tmp_flag = (p0_asub_q0 < tmp_flag);

        p2_asub_p0 = __msa_asub_u_b(p2_org, p0_org);
        is_less_than_beta = (p2_asub_p0 < beta);
        is_less_than_beta = tmp_flag & is_less_than_beta;
        negate_is_less_than_beta = __msa_xori_b(is_less_than_beta, 0xff);
        is_less_than_beta = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        if (!__msa_test_bz_v(is_less_than_beta)) {
            v16u8 p2, p1;
            v8i16 p3_org_r, p3_org_l;
            v8i16 p2_l = { 0 };
            v8i16 p2_r = { 0 };
            v8i16 p1_l = { 0 };
            v8i16 p1_r = { 0 };

            ILVR_B2_SH(zero, p3_org, zero, p2_org, p3_org_r, p2_r);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_org_r, p0_org_r, q0_org_r, p1_org_r,
                                     p2_r, q1_org_r, p0_r, p1_r, p2_r);

            ILVL_B2_SH(zero, p3_org, zero, p2_org, p3_org_l, p2_l);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_org_l, p0_org_l, q0_org_l, p1_org_l,
                                         p2_l, q1_org_l, p0_l, p1_l, p2_l);

            PCKEV_B3_UB(p0_l, p0_r, p1_l, p1_r, p2_l, p2_r, p0, p1, p2);
            p0_org = __msa_bmnz_v(p0_org, p0, is_less_than_beta);
            p1_org = __msa_bmnz_v(p1_org, p1, is_less_than_beta);
            p2_org = __msa_bmnz_v(p2_org, p2, is_less_than_beta);
        }

        AVC_LPF_P0_OR_Q0(p0_org_r, q1_org_r, p1_org_r, p0_r);
        AVC_LPF_P0_OR_Q0(p0_org_l, q1_org_l, p1_org_l, p0_l);

        p0 = (v16u8) __msa_pckev_b((v16i8) p0_l, (v16i8) p0_r);
        p0_org = __msa_bmnz_v(p0_org, p0, negate_is_less_than_beta);

        q2_asub_q0 = __msa_asub_u_b(q2_org, q0_org);
        is_less_than_beta = (q2_asub_q0 < beta);

        is_less_than_beta = is_less_than_beta & tmp_flag;
        negate_is_less_than_beta = __msa_xori_b(is_less_than_beta, 0xff);

        is_less_than_beta = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        if (!__msa_test_bz_v(is_less_than_beta)) {
            v16u8 q1, q2;
            v8i16 q3_org_r, q3_org_l;
            v8i16 q1_l = { 0 };
            v8i16 q1_r = { 0 };
            v8i16 q2_l = { 0 };
            v8i16 q2_r = { 0 };

            ILVR_B2_SH(zero, q3_org, zero, q2_org, q3_org_r, q2_r);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(q3_org_r, q0_org_r, p0_org_r, q1_org_r,
                                     q2_r, p1_org_r, q0_r, q1_r, q2_r);

            ILVL_B2_SH(zero, q3_org, zero, q2_org, q3_org_l, q2_l);
            AVC_LPF_P0P1P2_OR_Q0Q1Q2(q3_org_l, q0_org_l, p0_org_l, q1_org_l,
                                     q2_l, p1_org_l, q0_l, q1_l, q2_l);

            PCKEV_B3_UB(q0_l, q0_r, q1_l, q1_r, q2_l, q2_r, q0, q1, q2);
            q0_org = __msa_bmnz_v(q0_org, q0, is_less_than_beta);
            q1_org = __msa_bmnz_v(q1_org, q1, is_less_than_beta);
            q2_org = __msa_bmnz_v(q2_org, q2, is_less_than_beta);
        }

        AVC_LPF_P0_OR_Q0(q0_org_r, p1_org_r, q1_org_r, q0_r);
        AVC_LPF_P0_OR_Q0(q0_org_l, p1_org_l, q1_org_l, q0_l);

        q0 = (v16u8) __msa_pckev_b((v16i8) q0_l, (v16i8) q0_r);
        q0_org = __msa_bmnz_v(q0_org, q0, negate_is_less_than_beta);

    {
        v8i16 tp0, tp1, tp2, tp3, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;

        ILVRL_B2_SH(p1_org, p2_org, tp0, tp2);
        ILVRL_B2_SH(q0_org, p0_org, tp1, tp3);
        ILVRL_B2_SH(q2_org, q1_org, tmp2, tmp5);

        ILVRL_H2_SH(tp1, tp0, tmp3, tmp4);
        ILVRL_H2_SH(tp3, tp2, tmp6, tmp7);

        src = data - 3;
        ST_W4(tmp3, 0, 1, 2, 3, src, img_width);
        ST_H4(tmp2, 0, 1, 2, 3, src + 4, img_width);
        src += 4 * img_width;
        ST_W4(tmp4, 0, 1, 2, 3, src, img_width);
        ST_H4(tmp2, 4, 5, 6, 7, src + 4, img_width);
        src += 4 * img_width;

        ST_W4(tmp6, 0, 1, 2, 3, src, img_width);
        ST_H4(tmp5, 0, 1, 2, 3, src + 4, img_width);
        src += 4 * img_width;
        ST_W4(tmp7, 0, 1, 2, 3, src, img_width);
        ST_H4(tmp5, 4, 5, 6, 7, src + 4, img_width);
    }
    }
}

static void avc_h_loop_filter_luma_mbaff_intra_msa(uint8_t *src, int32_t stride,
                                                   int32_t alpha_in,
                                                   int32_t beta_in)
{
    uint64_t load0, load1;
    uint32_t out0, out2;
    uint16_t out1, out3;
    v8u16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8u16 dst0_r, dst1_r, dst4_r, dst5_r;
    v8u16 dst2_x_r, dst2_y_r, dst3_x_r, dst3_y_r;
    v16u8 dst0, dst1, dst4, dst5, dst2_x, dst2_y, dst3_x, dst3_y;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v16u8 alpha, beta;
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0, p2_asub_p0, q2_asub_q0;
    v16u8 is_less_than, is_less_than_alpha, is_less_than_beta;
    v16u8 is_less_than_beta1, is_less_than_beta2;
    v16i8 src0 = { 0 };
    v16i8 src1 = { 0 };
    v16i8 src2 = { 0 };
    v16i8 src3 = { 0 };
    v16i8 src4 = { 0 };
    v16i8 src5 = { 0 };
    v16i8 src6 = { 0 };
    v16i8 src7 = { 0 };
    v16i8 zeros = { 0 };

    load0 = LD(src - 4);
    load1 = LD(src + stride - 4);
    src0 = (v16i8) __msa_insert_d((v2i64) src0, 0, load0);
    src1 = (v16i8) __msa_insert_d((v2i64) src1, 0, load1);

    load0 = LD(src + (2 * stride) - 4);
    load1 = LD(src + (3 * stride) - 4);
    src2 = (v16i8) __msa_insert_d((v2i64) src2, 0, load0);
    src3 = (v16i8) __msa_insert_d((v2i64) src3, 0, load1);

    load0 = LD(src + (4 * stride) - 4);
    load1 = LD(src + (5 * stride) - 4);
    src4 = (v16i8) __msa_insert_d((v2i64) src4, 0, load0);
    src5 = (v16i8) __msa_insert_d((v2i64) src5, 0, load1);

    load0 = LD(src + (6 * stride) - 4);
    load1 = LD(src + (7 * stride) - 4);
    src6 = (v16i8) __msa_insert_d((v2i64) src6, 0, load0);
    src7 = (v16i8) __msa_insert_d((v2i64) src7, 0, load1);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src7, src6,
               src0, src1, src2, src3);

    ILVR_H2_SH(src1, src0, src3, src2, tmp0, tmp2);
    ILVL_H2_SH(src1, src0, src3, src2, tmp1, tmp3);

    ILVR_W2_SB(tmp2, tmp0, tmp3, tmp1, src6, src3);
    ILVL_W2_SB(tmp2, tmp0, tmp3, tmp1, src1, src5);
    SLDI_B4_SB(zeros, src6, zeros, src1, zeros, src3, zeros, src5,
               8, src0, src2, src4, src7);

    p0_asub_q0 = __msa_asub_u_b((v16u8) src2, (v16u8) src3);
    p1_asub_p0 = __msa_asub_u_b((v16u8) src1, (v16u8) src2);
    q1_asub_q0 = __msa_asub_u_b((v16u8) src4, (v16u8) src3);

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    is_less_than_alpha = (p0_asub_q0 < alpha);
    is_less_than_beta = (p1_asub_p0 < beta);
    is_less_than = is_less_than_alpha & is_less_than_beta;
    is_less_than_beta = (q1_asub_q0 < beta);
    is_less_than = is_less_than & is_less_than_beta;

    alpha >>= 2;
    alpha += 2;

    is_less_than_alpha = (p0_asub_q0 < alpha);

    p2_asub_p0 = __msa_asub_u_b((v16u8) src0, (v16u8) src2);
    is_less_than_beta1 = (p2_asub_p0 < beta);
    q2_asub_q0 = __msa_asub_u_b((v16u8) src5, (v16u8) src3);
    is_less_than_beta2 = (q2_asub_q0 < beta);

    ILVR_B4_UH(zeros, src0, zeros, src1, zeros, src2, zeros, src3,
               src0_r, src1_r, src2_r, src3_r);
    ILVR_B4_UH(zeros, src4, zeros, src5, zeros, src6, zeros, src7,
               src4_r, src5_r, src6_r, src7_r);

    dst2_x_r = src1_r + src2_r + src3_r;
    dst2_x_r = src0_r + (2 * (dst2_x_r)) + src4_r;
    dst2_x_r = (v8u16) __msa_srari_h((v8i16) dst2_x_r, 3);
    dst1_r = src0_r + src1_r + src2_r + src3_r;
    dst1_r = (v8u16) __msa_srari_h((v8i16) dst1_r, 2);

    dst0_r = (2 * src6_r) + (3 * src0_r);
    dst0_r += src1_r + src2_r + src3_r;
    dst0_r = (v8u16) __msa_srari_h((v8i16) dst0_r, 3);
    dst2_y_r = (2 * src1_r) + src2_r + src4_r;
    dst2_y_r = (v8u16) __msa_srari_h((v8i16) dst2_y_r, 2);

    PCKEV_B2_UB(dst2_x_r, dst2_x_r, dst2_y_r, dst2_y_r, dst2_x, dst2_y);
    dst2_x = __msa_bmnz_v(dst2_y, dst2_x, is_less_than_beta1);

    dst3_x_r = src2_r + src3_r + src4_r;
    dst3_x_r = src1_r + (2 * dst3_x_r) + src5_r;
    dst3_x_r = (v8u16) __msa_srari_h((v8i16) dst3_x_r, 3);
    dst4_r = src2_r + src3_r + src4_r + src5_r;
    dst4_r = (v8u16) __msa_srari_h((v8i16) dst4_r, 2);

    dst5_r = (2 * src7_r) + (3 * src5_r);
    dst5_r += src4_r + src3_r + src2_r;
    dst5_r = (v8u16) __msa_srari_h((v8i16) dst5_r, 3);
    dst3_y_r = (2 * src4_r) + src3_r + src1_r;
    dst3_y_r = (v8u16) __msa_srari_h((v8i16) dst3_y_r, 2);

    PCKEV_B2_UB(dst3_x_r, dst3_x_r, dst3_y_r, dst3_y_r, dst3_x, dst3_y);
    dst3_x = __msa_bmnz_v(dst3_y, dst3_x, is_less_than_beta2);

    dst2_y_r = (2 * src1_r) + src2_r + src4_r;
    dst2_y_r = (v8u16) __msa_srari_h((v8i16) dst2_y_r, 2);
    dst3_y_r = (2 * src4_r) + src3_r + src1_r;
    dst3_y_r = (v8u16) __msa_srari_h((v8i16) dst3_y_r, 2);

    PCKEV_B2_UB(dst2_y_r, dst2_y_r, dst3_y_r, dst3_y_r, dst2_y, dst3_y);

    dst2_x = __msa_bmnz_v(dst2_y, dst2_x, is_less_than_alpha);
    dst3_x = __msa_bmnz_v(dst3_y, dst3_x, is_less_than_alpha);
    dst2_x = __msa_bmnz_v((v16u8) src2, dst2_x, is_less_than);
    dst3_x = __msa_bmnz_v((v16u8) src3, dst3_x, is_less_than);

    is_less_than = is_less_than_alpha & is_less_than;
    dst1 = (v16u8) __msa_pckev_b((v16i8) dst1_r, (v16i8) dst1_r);
    is_less_than_beta1 = is_less_than_beta1 & is_less_than;
    dst1 = __msa_bmnz_v((v16u8) src1, dst1, is_less_than_beta1);

    dst0 = (v16u8) __msa_pckev_b((v16i8) dst0_r, (v16i8) dst0_r);
    dst0 = __msa_bmnz_v((v16u8) src0, dst0, is_less_than_beta1);
    dst4 = (v16u8) __msa_pckev_b((v16i8) dst4_r, (v16i8) dst4_r);
    is_less_than_beta2 = is_less_than_beta2 & is_less_than;
    dst4 = __msa_bmnz_v((v16u8) src4, dst4, is_less_than_beta2);
    dst5 = (v16u8) __msa_pckev_b((v16i8) dst5_r, (v16i8) dst5_r);
    dst5 = __msa_bmnz_v((v16u8) src5, dst5, is_less_than_beta2);

    ILVR_B2_UB(dst1, dst0, dst3_x, dst2_x, dst0, dst1);
    dst2_x = (v16u8) __msa_ilvr_b((v16i8) dst5, (v16i8) dst4);
    ILVRL_H2_SH(dst1, dst0, tmp0, tmp1);
    ILVRL_H2_SH(zeros, dst2_x, tmp2, tmp3);

    ILVR_W2_UB(tmp2, tmp0, tmp3, tmp1, dst0, dst4);
    SLDI_B2_UB(zeros, dst0, zeros, dst4, 8, dst1, dst5);
    dst2_x = (v16u8) __msa_ilvl_w((v4i32) tmp2, (v4i32) tmp0);
    dst2_y = (v16u8) __msa_ilvl_w((v4i32) tmp3, (v4i32) tmp1);
    SLDI_B2_UB(zeros, dst2_x, zeros, dst2_y, 8, dst3_x, dst3_y);

    out0 = __msa_copy_u_w((v4i32) dst0, 0);
    out1 = __msa_copy_u_h((v8i16) dst0, 2);
    out2 = __msa_copy_u_w((v4i32) dst1, 0);
    out3 = __msa_copy_u_h((v8i16) dst1, 2);

    SW(out0, (src - 3));
    SH(out1, (src + 1));
    src += stride;
    SW(out2, (src - 3));
    SH(out3, (src + 1));
    src += stride;

    out0 = __msa_copy_u_w((v4i32) dst2_x, 0);
    out1 = __msa_copy_u_h((v8i16) dst2_x, 2);
    out2 = __msa_copy_u_w((v4i32) dst3_x, 0);
    out3 = __msa_copy_u_h((v8i16) dst3_x, 2);

    SW(out0, (src - 3));
    SH(out1, (src + 1));
    src += stride;
    SW(out2, (src - 3));
    SH(out3, (src + 1));
    src += stride;

    out0 = __msa_copy_u_w((v4i32) dst4, 0);
    out1 = __msa_copy_u_h((v8i16) dst4, 2);
    out2 = __msa_copy_u_w((v4i32) dst5, 0);
    out3 = __msa_copy_u_h((v8i16) dst5, 2);

    SW(out0, (src - 3));
    SH(out1, (src + 1));
    src += stride;
    SW(out2, (src - 3));
    SH(out3, (src + 1));
    src += stride;

    out0 = __msa_copy_u_w((v4i32) dst2_y, 0);
    out1 = __msa_copy_u_h((v8i16) dst2_y, 2);
    out2 = __msa_copy_u_w((v4i32) dst3_y, 0);
    out3 = __msa_copy_u_h((v8i16) dst3_y, 2);

    SW(out0, (src - 3));
    SH(out1, (src + 1));
    src += stride;
    SW(out2, (src - 3));
    SH(out3, (src + 1));
}

static void avc_loopfilter_cb_or_cr_intra_edge_hor_msa(uint8_t *data_cb_or_cr,
                                                       uint8_t alpha_in,
                                                       uint8_t beta_in,
                                                       uint32_t img_width)
{
    v16u8 alpha, beta;
    v16u8 is_less_than;
    v8i16 p0_or_q0, q0_or_p0;
    v16u8 p1_or_q1_org, p0_or_q0_org, q0_or_p0_org, q1_or_p1_org;
    v16i8 zero = { 0 };
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;
    v16u8 is_less_than_alpha, is_less_than_beta;
    v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    LD_UB4(data_cb_or_cr - (img_width << 1), img_width,
           p1_or_q1_org, p0_or_q0_org, q0_or_p0_org, q1_or_p1_org);

    p0_asub_q0 = __msa_asub_u_b(p0_or_q0_org, q0_or_p0_org);
    p1_asub_p0 = __msa_asub_u_b(p1_or_q1_org, p0_or_q0_org);
    q1_asub_q0 = __msa_asub_u_b(q1_or_p1_org, q0_or_p0_org);

    is_less_than_alpha = (p0_asub_q0 < alpha);
    is_less_than_beta = (p1_asub_p0 < beta);
    is_less_than = is_less_than_beta & is_less_than_alpha;
    is_less_than_beta = (q1_asub_q0 < beta);
    is_less_than = is_less_than_beta & is_less_than;

    is_less_than = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) is_less_than);

    if (!__msa_test_bz_v(is_less_than)) {
        ILVR_B4_SH(zero, p1_or_q1_org, zero, p0_or_q0_org, zero, q0_or_p0_org,
                   zero, q1_or_p1_org, p1_org_r, p0_org_r, q0_org_r, q1_org_r);
        AVC_LPF_P0_OR_Q0(p0_org_r, q1_org_r, p1_org_r, p0_or_q0);
        AVC_LPF_P0_OR_Q0(q0_org_r, p1_org_r, q1_org_r, q0_or_p0);
        PCKEV_B2_SH(zero, p0_or_q0, zero, q0_or_p0, p0_or_q0, q0_or_p0);

        p0_or_q0_org =
            __msa_bmnz_v(p0_or_q0_org, (v16u8) p0_or_q0, is_less_than);
        q0_or_p0_org =
            __msa_bmnz_v(q0_or_p0_org, (v16u8) q0_or_p0, is_less_than);

        ST_UB(q0_or_p0_org, data_cb_or_cr);
        ST_UB(p0_or_q0_org, data_cb_or_cr - img_width);
    }
}

static void avc_loopfilter_cb_or_cr_intra_edge_ver_msa(uint8_t *data_cb_or_cr,
                                                       uint8_t alpha_in,
                                                       uint8_t beta_in,
                                                       uint32_t img_width)
{
    v8i16 tmp1;
    v16u8 alpha, beta, is_less_than;
    v8i16 p0_or_q0, q0_or_p0;
    v16u8 p1_or_q1_org, p0_or_q0_org, q0_or_p0_org, q1_or_p1_org;
    v16i8 zero = { 0 };
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;
    v16u8 is_less_than_alpha, is_less_than_beta;
    v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;

    {
        v16u8 row0, row1, row2, row3, row4, row5, row6, row7;

        LD_UB8((data_cb_or_cr - 2), img_width,
               row0, row1, row2, row3, row4, row5, row6, row7);

        TRANSPOSE8x4_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                           p1_or_q1_org, p0_or_q0_org,
                           q0_or_p0_org, q1_or_p1_org);
    }

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    p0_asub_q0 = __msa_asub_u_b(p0_or_q0_org, q0_or_p0_org);
    p1_asub_p0 = __msa_asub_u_b(p1_or_q1_org, p0_or_q0_org);
    q1_asub_q0 = __msa_asub_u_b(q1_or_p1_org, q0_or_p0_org);

    is_less_than_alpha = (p0_asub_q0 < alpha);
    is_less_than_beta = (p1_asub_p0 < beta);
    is_less_than = is_less_than_beta & is_less_than_alpha;
    is_less_than_beta = (q1_asub_q0 < beta);
    is_less_than = is_less_than_beta & is_less_than;
    is_less_than = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) is_less_than);

    if (!__msa_test_bz_v(is_less_than)) {
        ILVR_B4_SH(zero, p1_or_q1_org, zero, p0_or_q0_org, zero, q0_or_p0_org,
                   zero, q1_or_p1_org, p1_org_r, p0_org_r, q0_org_r, q1_org_r);

        AVC_LPF_P0_OR_Q0(p0_org_r, q1_org_r, p1_org_r, p0_or_q0);
        AVC_LPF_P0_OR_Q0(q0_org_r, p1_org_r, q1_org_r, q0_or_p0);

        /* convert 16 bit output into 8 bit output */
        PCKEV_B2_SH(zero, p0_or_q0, zero, q0_or_p0, p0_or_q0, q0_or_p0);

        p0_or_q0_org =
            __msa_bmnz_v(p0_or_q0_org, (v16u8) p0_or_q0, is_less_than);
        q0_or_p0_org =
            __msa_bmnz_v(q0_or_p0_org, (v16u8) q0_or_p0, is_less_than);
        tmp1 = (v8i16) __msa_ilvr_b((v16i8) q0_or_p0_org, (v16i8) p0_or_q0_org);

        data_cb_or_cr -= 1;
        ST_H4(tmp1, 0, 1, 2, 3, data_cb_or_cr, img_width);
        data_cb_or_cr += 4 * img_width;
        ST_H4(tmp1, 4, 5, 6, 7, data_cb_or_cr, img_width);
    }
}

static void avc_loopfilter_luma_inter_edge_ver_msa(uint8_t *data,
                                                   uint8_t bs0, uint8_t bs1,
                                                   uint8_t bs2, uint8_t bs3,
                                                   uint8_t tc0, uint8_t tc1,
                                                   uint8_t tc2, uint8_t tc3,
                                                   uint8_t alpha_in,
                                                   uint8_t beta_in,
                                                   uint32_t img_width)
{
    v16u8 tmp_vec, bs = { 0 };

    tmp_vec = (v16u8) __msa_fill_b(bs0);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 0, (v4i32) tmp_vec);
    tmp_vec = (v16u8) __msa_fill_b(bs1);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 1, (v4i32) tmp_vec);
    tmp_vec = (v16u8) __msa_fill_b(bs2);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 2, (v4i32) tmp_vec);
    tmp_vec = (v16u8) __msa_fill_b(bs3);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 3, (v4i32) tmp_vec);

    if (!__msa_test_bz_v(bs)) {
        uint8_t *src = data - 4;
        v16u8 p3_org, p2_org, p1_org, p0_org, q0_org, q1_org, q2_org, q3_org;
        v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
        v16u8 is_less_than, is_less_than_beta, is_less_than_alpha;
        v16u8 is_bs_greater_than0;
        v16u8 tc = { 0 };
        v16i8 zero = { 0 };

        tmp_vec = (v16u8) __msa_fill_b(tc0);
        tc = (v16u8) __msa_insve_w((v4i32) tc, 0, (v4i32) tmp_vec);
        tmp_vec = (v16u8) __msa_fill_b(tc1);
        tc = (v16u8) __msa_insve_w((v4i32) tc, 1, (v4i32) tmp_vec);
        tmp_vec = (v16u8) __msa_fill_b(tc2);
        tc = (v16u8) __msa_insve_w((v4i32) tc, 2, (v4i32) tmp_vec);
        tmp_vec = (v16u8) __msa_fill_b(tc3);
        tc = (v16u8) __msa_insve_w((v4i32) tc, 3, (v4i32) tmp_vec);

        is_bs_greater_than0 = (zero < bs);

        {
            v16u8 row0, row1, row2, row3, row4, row5, row6, row7;
            v16u8 row8, row9, row10, row11, row12, row13, row14, row15;

            LD_UB8(src, img_width,
                   row0, row1, row2, row3, row4, row5, row6, row7);
            src += (8 * img_width);
            LD_UB8(src, img_width,
                   row8, row9, row10, row11, row12, row13, row14, row15);

            TRANSPOSE16x8_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                                row8, row9, row10, row11,
                                row12, row13, row14, row15,
                                p3_org, p2_org, p1_org, p0_org,
                                q0_org, q1_org, q2_org, q3_org);
        }

        p0_asub_q0 = __msa_asub_u_b(p0_org, q0_org);
        p1_asub_p0 = __msa_asub_u_b(p1_org, p0_org);
        q1_asub_q0 = __msa_asub_u_b(q1_org, q0_org);

        alpha = (v16u8) __msa_fill_b(alpha_in);
        beta = (v16u8) __msa_fill_b(beta_in);

        is_less_than_alpha = (p0_asub_q0 < alpha);
        is_less_than_beta = (p1_asub_p0 < beta);
        is_less_than = is_less_than_beta & is_less_than_alpha;
        is_less_than_beta = (q1_asub_q0 < beta);
        is_less_than = is_less_than_beta & is_less_than;
        is_less_than = is_less_than & is_bs_greater_than0;

        if (!__msa_test_bz_v(is_less_than)) {
            v16i8 negate_tc, sign_negate_tc;
            v16u8 p0, q0, p2_asub_p0, q2_asub_q0;
            v8i16 tc_r, tc_l, negate_tc_r, i16_negatetc_l;
            v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;
            v8i16 p1_org_l, p0_org_l, q0_org_l, q1_org_l;
            v8i16 p0_r, q0_r, p0_l, q0_l;

            negate_tc = zero - (v16i8) tc;
            sign_negate_tc = __msa_clti_s_b(negate_tc, 0);

            ILVRL_B2_SH(sign_negate_tc, negate_tc, negate_tc_r, i16_negatetc_l);

            UNPCK_UB_SH(tc, tc_r, tc_l);
            UNPCK_UB_SH(p1_org, p1_org_r, p1_org_l);
            UNPCK_UB_SH(p0_org, p0_org_r, p0_org_l);
            UNPCK_UB_SH(q0_org, q0_org_r, q0_org_l);

            p2_asub_p0 = __msa_asub_u_b(p2_org, p0_org);
            is_less_than_beta = (p2_asub_p0 < beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            if (!__msa_test_bz_v(is_less_than_beta)) {
                v16u8 p1;
                v8i16 p1_r = { 0 };
                v8i16 p1_l = { 0 };
                v8i16 p2_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) p2_org);
                v8i16 p2_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) p2_org);

                AVC_LPF_P1_OR_Q1(p0_org_r, q0_org_r, p1_org_r, p2_org_r,
                                 negate_tc_r, tc_r, p1_r);
                AVC_LPF_P1_OR_Q1(p0_org_l, q0_org_l, p1_org_l, p2_org_l,
                                 i16_negatetc_l, tc_l, p1_l);

                p1 = (v16u8) __msa_pckev_b((v16i8) p1_l, (v16i8) p1_r);
                p1_org = __msa_bmnz_v(p1_org, p1, is_less_than_beta);

                is_less_than_beta = __msa_andi_b(is_less_than_beta, 1);
                tc = tc + is_less_than_beta;
            }

            q2_asub_q0 = __msa_asub_u_b(q2_org, q0_org);
            is_less_than_beta = (q2_asub_q0 < beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            q1_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) q1_org);
            q1_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) q1_org);

            if (!__msa_test_bz_v(is_less_than_beta)) {
                v16u8 q1;
                v8i16 q1_r = { 0 };
                v8i16 q1_l = { 0 };
                v8i16 q2_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) q2_org);
                v8i16 q2_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) q2_org);

                AVC_LPF_P1_OR_Q1(p0_org_r, q0_org_r, q1_org_r, q2_org_r,
                                 negate_tc_r, tc_r, q1_r);
                AVC_LPF_P1_OR_Q1(p0_org_l, q0_org_l, q1_org_l, q2_org_l,
                                 i16_negatetc_l, tc_l, q1_l);

                q1 = (v16u8) __msa_pckev_b((v16i8) q1_l, (v16i8) q1_r);
                q1_org = __msa_bmnz_v(q1_org, q1, is_less_than_beta);

                is_less_than_beta = __msa_andi_b(is_less_than_beta, 1);
                tc = tc + is_less_than_beta;
            }

            {
                v8i16 threshold_r, negate_thresh_r;
                v8i16 threshold_l, negate_thresh_l;
                v16i8 negate_thresh, sign_negate_thresh;

                negate_thresh = zero - (v16i8) tc;
                sign_negate_thresh = __msa_clti_s_b(negate_thresh, 0);

                ILVR_B2_SH(zero, tc, sign_negate_thresh, negate_thresh,
                           threshold_r, negate_thresh_r);

                AVC_LPF_P0Q0(q0_org_r, p0_org_r, p1_org_r, q1_org_r,
                             negate_thresh_r, threshold_r, p0_r, q0_r);

                threshold_l = (v8i16) __msa_ilvl_b(zero, (v16i8) tc);
                negate_thresh_l = (v8i16) __msa_ilvl_b(sign_negate_thresh,
                                                       negate_thresh);

                AVC_LPF_P0Q0(q0_org_l, p0_org_l, p1_org_l, q1_org_l,
                             negate_thresh_l, threshold_l, p0_l, q0_l);
            }

            PCKEV_B2_UB(p0_l, p0_r, q0_l, q0_r, p0, q0);

            p0_org = __msa_bmnz_v(p0_org, p0, is_less_than);
            q0_org = __msa_bmnz_v(q0_org, q0, is_less_than);

        {
            v16i8 tp0, tp1, tp2, tp3;
            v8i16 tmp2, tmp5;
            v4i32 tmp3, tmp4, tmp6, tmp7;
            uint32_t out0, out2;
            uint16_t out1, out3;

            src = data - 3;

            ILVRL_B2_SB(p1_org, p2_org, tp0, tp2);
            ILVRL_B2_SB(q0_org, p0_org, tp1, tp3);
            ILVRL_B2_SH(q2_org, q1_org, tmp2, tmp5);

            ILVRL_H2_SW(tp1, tp0, tmp3, tmp4);
            ILVRL_H2_SW(tp3, tp2, tmp6, tmp7);

            out0 = __msa_copy_u_w(tmp3, 0);
            out1 = __msa_copy_u_h(tmp2, 0);
            out2 = __msa_copy_u_w(tmp3, 1);
            out3 = __msa_copy_u_h(tmp2, 1);

            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp3, 2);
            out1 = __msa_copy_u_h(tmp2, 2);
            out2 = __msa_copy_u_w(tmp3, 3);
            out3 = __msa_copy_u_h(tmp2, 3);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp4, 0);
            out1 = __msa_copy_u_h(tmp2, 4);
            out2 = __msa_copy_u_w(tmp4, 1);
            out3 = __msa_copy_u_h(tmp2, 5);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp4, 2);
            out1 = __msa_copy_u_h(tmp2, 6);
            out2 = __msa_copy_u_w(tmp4, 3);
            out3 = __msa_copy_u_h(tmp2, 7);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp6, 0);
            out1 = __msa_copy_u_h(tmp5, 0);
            out2 = __msa_copy_u_w(tmp6, 1);
            out3 = __msa_copy_u_h(tmp5, 1);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp6, 2);
            out1 = __msa_copy_u_h(tmp5, 2);
            out2 = __msa_copy_u_w(tmp6, 3);
            out3 = __msa_copy_u_h(tmp5, 3);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp7, 0);
            out1 = __msa_copy_u_h(tmp5, 4);
            out2 = __msa_copy_u_w(tmp7, 1);
            out3 = __msa_copy_u_h(tmp5, 5);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));

            out0 = __msa_copy_u_w(tmp7, 2);
            out1 = __msa_copy_u_h(tmp5, 6);
            out2 = __msa_copy_u_w(tmp7, 3);
            out3 = __msa_copy_u_h(tmp5, 7);

            src += img_width;
            SW(out0, src);
            SH(out1, (src + 4));
            src += img_width;
            SW(out2, src);
            SH(out3, (src + 4));
        }
        }
    }
}

static void avc_loopfilter_luma_inter_edge_hor_msa(uint8_t *data,
                                                   uint8_t bs0, uint8_t bs1,
                                                   uint8_t bs2, uint8_t bs3,
                                                   uint8_t tc0, uint8_t tc1,
                                                   uint8_t tc2, uint8_t tc3,
                                                   uint8_t alpha_in,
                                                   uint8_t beta_in,
                                                   uint32_t image_width)
{
    v16u8 tmp_vec;
    v16u8 bs = { 0 };

    tmp_vec = (v16u8) __msa_fill_b(bs0);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 0, (v4i32) tmp_vec);
    tmp_vec = (v16u8) __msa_fill_b(bs1);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 1, (v4i32) tmp_vec);
    tmp_vec = (v16u8) __msa_fill_b(bs2);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 2, (v4i32) tmp_vec);
    tmp_vec = (v16u8) __msa_fill_b(bs3);
    bs = (v16u8) __msa_insve_w((v4i32) bs, 3, (v4i32) tmp_vec);

    if (!__msa_test_bz_v(bs)) {
        v16u8 alpha, beta, is_less_than, is_less_than_beta;
        v16u8 p0, q0, p2_org, p1_org, p0_org, q0_org, q1_org, q2_org;
        v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;
        v16u8 is_less_than_alpha, is_bs_greater_than0;
        v8i16 p0_r, q0_r, p0_l, q0_l;
        v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;
        v8i16 p1_org_l, p0_org_l, q0_org_l, q1_org_l;
        v16i8 zero = { 0 };
        v16i8 tc = { 0 };

        tmp_vec = (v16u8) __msa_fill_b(tc0);
        tc = (v16i8) __msa_insve_w((v4i32) tc, 0, (v4i32) tmp_vec);
        tmp_vec = (v16u8) __msa_fill_b(tc1);
        tc = (v16i8) __msa_insve_w((v4i32) tc, 1, (v4i32) tmp_vec);
        tmp_vec = (v16u8) __msa_fill_b(tc2);
        tc = (v16i8) __msa_insve_w((v4i32) tc, 2, (v4i32) tmp_vec);
        tmp_vec = (v16u8) __msa_fill_b(tc3);
        tc = (v16i8) __msa_insve_w((v4i32) tc, 3, (v4i32) tmp_vec);

        alpha = (v16u8) __msa_fill_b(alpha_in);
        beta = (v16u8) __msa_fill_b(beta_in);

        LD_UB5(data - (3 * image_width), image_width,
               p2_org, p1_org, p0_org, q0_org, q1_org);

        is_bs_greater_than0 = ((v16u8) zero < bs);
        p0_asub_q0 = __msa_asub_u_b(p0_org, q0_org);
        p1_asub_p0 = __msa_asub_u_b(p1_org, p0_org);
        q1_asub_q0 = __msa_asub_u_b(q1_org, q0_org);

        is_less_than_alpha = (p0_asub_q0 < alpha);
        is_less_than_beta = (p1_asub_p0 < beta);
        is_less_than = is_less_than_beta & is_less_than_alpha;
        is_less_than_beta = (q1_asub_q0 < beta);
        is_less_than = is_less_than_beta & is_less_than;
        is_less_than = is_less_than & is_bs_greater_than0;

        if (!__msa_test_bz_v(is_less_than)) {
            v16i8 sign_negate_tc, negate_tc;
            v8i16 negate_tc_r, i16_negatetc_l, tc_l, tc_r;
            v16u8 p2_asub_p0, q2_asub_q0;

            q2_org = LD_UB(data + (2 * image_width));
            negate_tc = zero - tc;
            sign_negate_tc = __msa_clti_s_b(negate_tc, 0);

            ILVRL_B2_SH(sign_negate_tc, negate_tc, negate_tc_r, i16_negatetc_l);

            UNPCK_UB_SH(tc, tc_r, tc_l);
            UNPCK_UB_SH(p1_org, p1_org_r, p1_org_l);
            UNPCK_UB_SH(p0_org, p0_org_r, p0_org_l);
            UNPCK_UB_SH(q0_org, q0_org_r, q0_org_l);

            p2_asub_p0 = __msa_asub_u_b(p2_org, p0_org);
            is_less_than_beta = (p2_asub_p0 < beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            if (!__msa_test_bz_v(is_less_than_beta)) {
                v16u8 p1;
                v8i16 p1_r = { 0 };
                v8i16 p1_l = { 0 };
                v8i16 p2_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) p2_org);
                v8i16 p2_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) p2_org);

                AVC_LPF_P1_OR_Q1(p0_org_r, q0_org_r, p1_org_r, p2_org_r,
                                 negate_tc_r, tc_r, p1_r);
                AVC_LPF_P1_OR_Q1(p0_org_l, q0_org_l, p1_org_l, p2_org_l,
                                 i16_negatetc_l, tc_l, p1_l);

                p1 = (v16u8) __msa_pckev_b((v16i8) p1_l, (v16i8) p1_r);
                p1_org = __msa_bmnz_v(p1_org, p1, is_less_than_beta);
                ST_UB(p1_org, data - (2 * image_width));

                is_less_than_beta = __msa_andi_b(is_less_than_beta, 1);
                tc = tc + (v16i8) is_less_than_beta;
            }

            q2_asub_q0 = __msa_asub_u_b(q2_org, q0_org);
            is_less_than_beta = (q2_asub_q0 < beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            q1_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) q1_org);
            q1_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) q1_org);

            if (!__msa_test_bz_v(is_less_than_beta)) {
                v16u8 q1;
                v8i16 q1_r = { 0 };
                v8i16 q1_l = { 0 };
                v8i16 q2_org_r = (v8i16) __msa_ilvr_b(zero, (v16i8) q2_org);
                v8i16 q2_org_l = (v8i16) __msa_ilvl_b(zero, (v16i8) q2_org);

                AVC_LPF_P1_OR_Q1(p0_org_r, q0_org_r, q1_org_r, q2_org_r,
                                 negate_tc_r, tc_r, q1_r);
                AVC_LPF_P1_OR_Q1(p0_org_l, q0_org_l, q1_org_l, q2_org_l,
                                 i16_negatetc_l, tc_l, q1_l);

                q1 = (v16u8) __msa_pckev_b((v16i8) q1_l, (v16i8) q1_r);
                q1_org = __msa_bmnz_v(q1_org, q1, is_less_than_beta);
                ST_UB(q1_org, data + image_width);

                is_less_than_beta = __msa_andi_b(is_less_than_beta, 1);
                tc = tc + (v16i8) is_less_than_beta;
            }
            {
                v16i8 negate_thresh, sign_negate_thresh;
                v8i16 threshold_r, threshold_l;
                v8i16 negate_thresh_l, negate_thresh_r;

                negate_thresh = zero - tc;
                sign_negate_thresh = __msa_clti_s_b(negate_thresh, 0);

                ILVR_B2_SH(zero, tc, sign_negate_thresh, negate_thresh,
                           threshold_r, negate_thresh_r);
                AVC_LPF_P0Q0(q0_org_r, p0_org_r, p1_org_r, q1_org_r,
                             negate_thresh_r, threshold_r, p0_r, q0_r);

                threshold_l = (v8i16) __msa_ilvl_b(zero, tc);
                negate_thresh_l = (v8i16) __msa_ilvl_b(sign_negate_thresh,
                                                       negate_thresh);
                AVC_LPF_P0Q0(q0_org_l, p0_org_l, p1_org_l, q1_org_l,
                             negate_thresh_l, threshold_l, p0_l, q0_l);
            }

            PCKEV_B2_UB(p0_l, p0_r, q0_l, q0_r, p0, q0);

            p0_org = __msa_bmnz_v(p0_org, p0, is_less_than);
            q0_org = __msa_bmnz_v(q0_org, q0, is_less_than);

            ST_UB(p0_org, (data - image_width));
            ST_UB(q0_org, data);
        }
    }
}

static void avc_h_loop_filter_luma_mbaff_msa(uint8_t *in, int32_t stride,
                                             int32_t alpha_in, int32_t beta_in,
                                             int8_t *tc0)
{
    uint8_t *data = in;
    uint32_t out0, out1, out2, out3;
    uint64_t load;
    uint32_t tc_val;
    v16u8 alpha, beta;
    v16i8 inp0 = { 0 };
    v16i8 inp1 = { 0 };
    v16i8 inp2 = { 0 };
    v16i8 inp3 = { 0 };
    v16i8 inp4 = { 0 };
    v16i8 inp5 = { 0 };
    v16i8 inp6 = { 0 };
    v16i8 inp7 = { 0 };
    v16i8 src0, src1, src2, src3;
    v8i16 src4, src5, src6, src7;
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0, p2_asub_p0, q2_asub_q0;
    v16u8 is_less_than, is_less_than_alpha, is_less_than_beta;
    v16u8 is_less_than_beta1, is_less_than_beta2;
    v8i16 tc, tc_orig_r, tc_plus1;
    v16u8 is_tc_orig1, is_tc_orig2, tc_orig = { 0 };
    v8i16 p0_ilvr_q0, p0_add_q0, q0_sub_p0, p1_sub_q1;
    v8i16 src2_r, src3_r;
    v8i16 p2_r, p1_r, q2_r, q1_r;
    v16u8 p2, q2, p0, q0;
    v4i32 dst0, dst1;
    v16i8 zeros = { 0 };

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    if (tc0[0] < 0) {
        data += (2 * stride);
    } else {
        load = LD(data - 3);
        inp0 = (v16i8) __msa_insert_d((v2i64) inp0, 0, load);
        load = LD(data - 3 + stride);
        inp1 = (v16i8) __msa_insert_d((v2i64) inp1, 0, load);
        data += (2 * stride);
    }

    if (tc0[1] < 0) {
        data += (2 * stride);
    } else {
        load = LD(data - 3);
        inp2 = (v16i8) __msa_insert_d((v2i64) inp2, 0, load);
        load = LD(data - 3 + stride);
        inp3 = (v16i8) __msa_insert_d((v2i64) inp3, 0, load);
        data += (2 * stride);
    }

    if (tc0[2] < 0) {
        data += (2 * stride);
    } else {
        load = LD(data - 3);
        inp4 = (v16i8) __msa_insert_d((v2i64) inp4, 0, load);
        load = LD(data - 3 + stride);
        inp5 = (v16i8) __msa_insert_d((v2i64) inp5, 0, load);
        data += (2 * stride);
    }

    if (tc0[3] < 0) {
        data += (2 * stride);
    } else {
        load = LD(data - 3);
        inp6 = (v16i8) __msa_insert_d((v2i64) inp6, 0, load);
        load = LD(data - 3 + stride);
        inp7 = (v16i8) __msa_insert_d((v2i64) inp7, 0, load);
        data += (2 * stride);
    }

    ILVR_B4_SB(inp1, inp0, inp3, inp2, inp5, inp4, inp7, inp6,
               src0, src1, src2, src3);

    ILVR_H2_SH(src1, src0, src3, src2, src4, src6);
    ILVL_H2_SH(src1, src0, src3, src2, src5, src7);

    src0 = (v16i8) __msa_ilvr_w((v4i32) src6, (v4i32) src4);
    src1 = __msa_sldi_b(zeros, (v16i8) src0, 8);
    src2 = (v16i8) __msa_ilvl_w((v4i32) src6, (v4i32) src4);
    src3 = __msa_sldi_b(zeros, (v16i8) src2, 8);
    src4 = (v8i16) __msa_ilvr_w((v4i32) src7, (v4i32) src5);
    src5 = (v8i16) __msa_sldi_b(zeros, (v16i8) src4, 8);

    p0_asub_q0 = __msa_asub_u_b((v16u8) src2, (v16u8) src3);
    p1_asub_p0 = __msa_asub_u_b((v16u8) src1, (v16u8) src2);
    q1_asub_q0 = __msa_asub_u_b((v16u8) src4, (v16u8) src3);
    p2_asub_p0 = __msa_asub_u_b((v16u8) src0, (v16u8) src2);
    q2_asub_q0 = __msa_asub_u_b((v16u8) src5, (v16u8) src3);

    is_less_than_alpha = (p0_asub_q0 < alpha);
    is_less_than_beta = (p1_asub_p0 < beta);
    is_less_than = is_less_than_alpha & is_less_than_beta;
    is_less_than_beta = (q1_asub_q0 < beta);
    is_less_than = is_less_than_beta & is_less_than;

    is_less_than_beta1 = (p2_asub_p0 < beta);
    is_less_than_beta2 = (q2_asub_q0 < beta);

    p0_ilvr_q0 = (v8i16) __msa_ilvr_b((v16i8) src3, (v16i8) src2);
    p0_add_q0 = (v8i16) __msa_hadd_u_h((v16u8) p0_ilvr_q0, (v16u8) p0_ilvr_q0);
    p0_add_q0 = __msa_srari_h(p0_add_q0, 1);

    ILVR_B2_SH(zeros, src0, zeros, src1, p2_r, p1_r);
    p2_r += p0_add_q0;
    p2_r >>= 1;
    p2_r -= p1_r;
    ILVR_B2_SH(zeros, src5, zeros, src4, q2_r, q1_r);
    q2_r += p0_add_q0;
    q2_r >>= 1;
    q2_r -= q1_r;

    tc_val = LW(tc0);
    tc_orig = (v16u8) __msa_insert_w((v4i32) tc_orig, 0, tc_val);
    tc_orig = (v16u8) __msa_ilvr_b((v16i8) tc_orig, (v16i8) tc_orig);
    is_tc_orig1 = tc_orig;
    is_tc_orig2 = tc_orig;
    tc_orig_r = (v8i16) __msa_ilvr_b(zeros, (v16i8) tc_orig);
    tc = tc_orig_r;

    CLIP_SH(p2_r, -tc_orig_r, tc_orig_r);
    CLIP_SH(q2_r, -tc_orig_r, tc_orig_r);

    p2_r += p1_r;
    q2_r += q1_r;

    PCKEV_B2_UB(p2_r, p2_r, q2_r, q2_r, p2, q2);

    is_tc_orig1 = (zeros < is_tc_orig1);
    is_tc_orig2 = is_tc_orig1;
    is_tc_orig1 = is_less_than_beta1 & is_tc_orig1;
    is_tc_orig2 = is_less_than_beta2 & is_tc_orig2;
    is_tc_orig1 = is_less_than & is_tc_orig1;
    is_tc_orig2 = is_less_than & is_tc_orig2;

    p2 = __msa_bmnz_v((v16u8) src1, p2, is_tc_orig1);
    q2 = __msa_bmnz_v((v16u8) src4, q2, is_tc_orig2);

    q0_sub_p0 = __msa_hsub_u_h((v16u8) p0_ilvr_q0, (v16u8) p0_ilvr_q0);
    q0_sub_p0 <<= 2;
    p1_sub_q1 = p1_r - q1_r;
    q0_sub_p0 += p1_sub_q1;
    q0_sub_p0 = __msa_srari_h(q0_sub_p0, 3);

    tc_plus1 = tc + 1;
    is_less_than_beta1 = (v16u8) __msa_ilvr_b((v16i8) is_less_than_beta1,
                                              (v16i8) is_less_than_beta1);
    tc = (v8i16) __msa_bmnz_v((v16u8) tc, (v16u8) tc_plus1, is_less_than_beta1);
    tc_plus1 = tc + 1;
    is_less_than_beta2 = (v16u8) __msa_ilvr_b((v16i8) is_less_than_beta2,
                                              (v16i8) is_less_than_beta2);
    tc = (v8i16) __msa_bmnz_v((v16u8) tc, (v16u8) tc_plus1, is_less_than_beta2);

    CLIP_SH(q0_sub_p0, -tc, tc);

    ILVR_B2_SH(zeros, src2, zeros, src3, src2_r, src3_r);
    src2_r += q0_sub_p0;
    src3_r -= q0_sub_p0;

    CLIP_SH2_0_255(src2_r, src3_r);

    PCKEV_B2_UB(src2_r, src2_r, src3_r, src3_r, p0, q0);

    p0 = __msa_bmnz_v((v16u8) src2, p0, is_less_than);
    q0 = __msa_bmnz_v((v16u8) src3, q0, is_less_than);

    ILVR_B2_UB(p0, p2, q2, q0, p2, q2);

    ILVRL_H2_SW(q2, p2, dst0, dst1);

    data = in;

    out0 = __msa_copy_u_w(dst0, 0);
    out1 = __msa_copy_u_w(dst0, 1);
    out2 = __msa_copy_u_w(dst0, 2);
    out3 = __msa_copy_u_w(dst0, 3);

    if (tc0[0] < 0) {
        data += (2 * stride);
    } else {
        SW(out0, (data - 2));
        data += stride;
        SW(out1, (data - 2));
        data += stride;
    }

    if (tc0[1] < 0) {
        data += (2 * stride);
    } else {
        SW(out2, (data - 2));
        data += stride;
        SW(out3, (data - 2));
        data += stride;
    }

    out0 = __msa_copy_u_w(dst1, 0);
    out1 = __msa_copy_u_w(dst1, 1);
    out2 = __msa_copy_u_w(dst1, 2);
    out3 = __msa_copy_u_w(dst1, 3);

    if (tc0[2] < 0) {
        data += (2 * stride);
    } else {
        SW(out0, (data - 2));
        data += stride;
        SW(out1, (data - 2));
        data += stride;
    }

    if (tc0[3] >= 0) {
        SW(out2, (data - 2));
        data += stride;
        SW(out3, (data - 2));
    }
}

static void avc_loopfilter_cb_or_cr_inter_edge_hor_msa(uint8_t *data,
                                                       uint8_t bs0, uint8_t bs1,
                                                       uint8_t bs2, uint8_t bs3,
                                                       uint8_t tc0, uint8_t tc1,
                                                       uint8_t tc2, uint8_t tc3,
                                                       uint8_t alpha_in,
                                                       uint8_t beta_in,
                                                       uint32_t img_width)
{
    v16u8 alpha, beta;
    v8i16 tmp_vec;
    v8i16 bs = { 0 };
    v8i16 tc = { 0 };
    v16u8 p0, q0, p0_asub_q0, p1_asub_p0, q1_asub_q0;
    v16u8 is_less_than;
    v16u8 is_less_than_beta, is_less_than_alpha, is_bs_greater_than0;
    v8i16 p0_r, q0_r;
    v16u8 p1_org, p0_org, q0_org, q1_org;
    v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;
    v16i8 negate_tc, sign_negate_tc;
    v8i16 tc_r, negate_tc_r;
    v16i8 zero = { 0 };

    tmp_vec = (v8i16) __msa_fill_b(bs0);
    bs = __msa_insve_h(bs, 0, tmp_vec);
    tmp_vec = (v8i16) __msa_fill_b(bs1);
    bs = __msa_insve_h(bs, 1, tmp_vec);
    tmp_vec = (v8i16) __msa_fill_b(bs2);
    bs = __msa_insve_h(bs, 2, tmp_vec);
    tmp_vec = (v8i16) __msa_fill_b(bs3);
    bs = __msa_insve_h(bs, 3, tmp_vec);

    if (!__msa_test_bz_v((v16u8) bs)) {
        tmp_vec = (v8i16) __msa_fill_b(tc0);
        tc = __msa_insve_h(tc, 0, tmp_vec);
        tmp_vec = (v8i16) __msa_fill_b(tc1);
        tc = __msa_insve_h(tc, 1, tmp_vec);
        tmp_vec = (v8i16) __msa_fill_b(tc2);
        tc = __msa_insve_h(tc, 2, tmp_vec);
        tmp_vec = (v8i16) __msa_fill_b(tc3);
        tc = __msa_insve_h(tc, 3, tmp_vec);

        is_bs_greater_than0 = (v16u8) (zero < (v16i8) bs);

        alpha = (v16u8) __msa_fill_b(alpha_in);
        beta = (v16u8) __msa_fill_b(beta_in);

        LD_UB4(data - (img_width << 1), img_width,
               p1_org, p0_org, q0_org, q1_org);

        p0_asub_q0 = __msa_asub_u_b(p0_org, q0_org);
        p1_asub_p0 = __msa_asub_u_b(p1_org, p0_org);
        q1_asub_q0 = __msa_asub_u_b(q1_org, q0_org);

        is_less_than_alpha = (p0_asub_q0 < alpha);
        is_less_than_beta = (p1_asub_p0 < beta);
        is_less_than = is_less_than_beta & is_less_than_alpha;
        is_less_than_beta = (q1_asub_q0 < beta);
        is_less_than = is_less_than_beta & is_less_than;
        is_less_than = is_less_than & is_bs_greater_than0;

        is_less_than = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) is_less_than);

        if (!__msa_test_bz_v(is_less_than)) {
            negate_tc = zero - (v16i8) tc;
            sign_negate_tc = __msa_clti_s_b(negate_tc, 0);

            ILVR_B2_SH(zero, tc, sign_negate_tc, negate_tc, tc_r, negate_tc_r);

            ILVR_B4_SH(zero, p1_org, zero, p0_org, zero, q0_org, zero, q1_org,
                       p1_org_r, p0_org_r, q0_org_r, q1_org_r);

            AVC_LPF_P0Q0(q0_org_r, p0_org_r, p1_org_r, q1_org_r, negate_tc_r,
                         tc_r, p0_r, q0_r);

            PCKEV_B2_UB(zero, p0_r, zero, q0_r, p0, q0);

            p0_org = __msa_bmnz_v(p0_org, p0, is_less_than);
            q0_org = __msa_bmnz_v(q0_org, q0, is_less_than);

            ST_UB(q0_org, data);
            ST_UB(p0_org, (data - img_width));
        }
    }
}

static void avc_loopfilter_cb_or_cr_inter_edge_ver_msa(uint8_t *data,
                                                       uint8_t bs0, uint8_t bs1,
                                                       uint8_t bs2, uint8_t bs3,
                                                       uint8_t tc0, uint8_t tc1,
                                                       uint8_t tc2, uint8_t tc3,
                                                       uint8_t alpha_in,
                                                       uint8_t beta_in,
                                                       uint32_t img_width)
{
    uint8_t *src;
    v16u8 alpha, beta;
    v16u8 p0_asub_q0, p1_asub_p0, q1_asub_q0;
    v16u8 is_less_than, is_less_than_beta, is_less_than_alpha;
    v16u8 p0, q0;
    v8i16 p0_r = { 0 };
    v8i16 q0_r = { 0 };
    v16u8 p1_org, p0_org, q0_org, q1_org;
    v8i16 p1_org_r, p0_org_r, q0_org_r, q1_org_r;
    v16u8 is_bs_greater_than0;
    v8i16 tc_r, negate_tc_r;
    v16i8 negate_tc, sign_negate_tc;
    v16i8 zero = { 0 };
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7;
    v8i16 tmp1, tmp_vec, bs = { 0 };
    v8i16 tc = { 0 };

    tmp_vec = (v8i16) __msa_fill_b(bs0);
    bs = __msa_insve_h(bs, 0, tmp_vec);
    tmp_vec = (v8i16) __msa_fill_b(bs1);
    bs = __msa_insve_h(bs, 1, tmp_vec);
    tmp_vec = (v8i16) __msa_fill_b(bs2);
    bs = __msa_insve_h(bs, 2, tmp_vec);
    tmp_vec = (v8i16) __msa_fill_b(bs3);
    bs = __msa_insve_h(bs, 3, tmp_vec);

    if (!__msa_test_bz_v((v16u8) bs)) {
        tmp_vec = (v8i16) __msa_fill_b(tc0);
        tc = __msa_insve_h(tc, 0, tmp_vec);
        tmp_vec = (v8i16) __msa_fill_b(tc1);
        tc = __msa_insve_h(tc, 1, tmp_vec);
        tmp_vec = (v8i16) __msa_fill_b(tc2);
        tc = __msa_insve_h(tc, 2, tmp_vec);
        tmp_vec = (v8i16) __msa_fill_b(tc3);
        tc = __msa_insve_h(tc, 3, tmp_vec);

        is_bs_greater_than0 = (v16u8) (zero < (v16i8) bs);

        LD_UB8((data - 2), img_width,
               row0, row1, row2, row3, row4, row5, row6, row7);

        TRANSPOSE8x4_UB_UB(row0, row1, row2, row3,
                           row4, row5, row6, row7,
                           p1_org, p0_org, q0_org, q1_org);

        p0_asub_q0 = __msa_asub_u_b(p0_org, q0_org);
        p1_asub_p0 = __msa_asub_u_b(p1_org, p0_org);
        q1_asub_q0 = __msa_asub_u_b(q1_org, q0_org);

        alpha = (v16u8) __msa_fill_b(alpha_in);
        beta = (v16u8) __msa_fill_b(beta_in);

        is_less_than_alpha = (p0_asub_q0 < alpha);
        is_less_than_beta = (p1_asub_p0 < beta);
        is_less_than = is_less_than_beta & is_less_than_alpha;
        is_less_than_beta = (q1_asub_q0 < beta);
        is_less_than = is_less_than_beta & is_less_than;
        is_less_than = is_bs_greater_than0 & is_less_than;

        is_less_than = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) is_less_than);

        if (!__msa_test_bz_v(is_less_than)) {
            ILVR_B4_SH(zero, p1_org, zero, p0_org, zero, q0_org, zero, q1_org,
                       p1_org_r, p0_org_r, q0_org_r, q1_org_r);

            negate_tc = zero - (v16i8) tc;
            sign_negate_tc = __msa_clti_s_b(negate_tc, 0);

            ILVR_B2_SH(sign_negate_tc, negate_tc, zero, tc, negate_tc_r, tc_r);

            AVC_LPF_P0Q0(q0_org_r, p0_org_r, p1_org_r, q1_org_r, negate_tc_r,
                         tc_r, p0_r, q0_r);

            PCKEV_B2_UB(zero, p0_r, zero, q0_r, p0, q0);

            p0_org = __msa_bmnz_v(p0_org, p0, is_less_than);
            q0_org = __msa_bmnz_v(q0_org, q0, is_less_than);
            tmp1 = (v8i16) __msa_ilvr_b((v16i8) q0_org, (v16i8) p0_org);
            src = data - 1;
            ST_H4(tmp1, 0, 1, 2, 3, src, img_width);
            src += 4 * img_width;
            ST_H4(tmp1, 4, 5, 6, 7, src, img_width);
        }
    }
}

static void avc_h_loop_filter_chroma422_msa(uint8_t *src, int32_t stride,
                                            int32_t alpha_in, int32_t beta_in,
                                            int8_t *tc0)
{
    int32_t col, tc_val;
    v16u8 alpha, beta, res;

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    for (col = 0; col < 4; col++) {
        tc_val = (tc0[col] - 1) + 1;

        if (tc_val <= 0) {
            src += (4 * stride);
            continue;
        }

        AVC_LPF_H_CHROMA_422(src, stride, tc_val, alpha, beta, res);
        ST_H4(res, 0, 1, 2, 3, (src - 1), stride);
        src += (4 * stride);
    }
}

static void avc_h_loop_filter_chroma422_mbaff_msa(uint8_t *src, int32_t stride,
                                                  int32_t alpha_in,
                                                  int32_t beta_in,
                                                  int8_t *tc0)
{
    int32_t col, tc_val;
    int16_t out0, out1;
    v16u8 alpha, beta, res;

    alpha = (v16u8) __msa_fill_b(alpha_in);
    beta = (v16u8) __msa_fill_b(beta_in);

    for (col = 0; col < 4; col++) {
        tc_val = (tc0[col] - 1) + 1;

        if (tc_val <= 0) {
            src += 4 * stride;
            continue;
        }

        AVC_LPF_H_2BYTE_CHROMA_422(src, stride, tc_val, alpha, beta, res);

        out0 = __msa_copy_s_h((v8i16) res, 0);
        out1 = __msa_copy_s_h((v8i16) res, 1);

        SH(out0, (src - 1));
        src += stride;
        SH(out1, (src - 1));
        src += stride;
    }
}

void ff_h264_h_lpf_luma_inter_msa(uint8_t *data, int img_width,
                                  int alpha, int beta, int8_t *tc)
{
    uint8_t bs0 = 1;
    uint8_t bs1 = 1;
    uint8_t bs2 = 1;
    uint8_t bs3 = 1;

    if (tc[0] < 0)
        bs0 = 0;
    if (tc[1] < 0)
        bs1 = 0;
    if (tc[2] < 0)
        bs2 = 0;
    if (tc[3] < 0)
        bs3 = 0;

    avc_loopfilter_luma_inter_edge_ver_msa(data, bs0, bs1, bs2, bs3,
                                           tc[0], tc[1], tc[2], tc[3],
                                           alpha, beta, img_width);
}

void ff_h264_v_lpf_luma_inter_msa(uint8_t *data, int img_width,
                                  int alpha, int beta, int8_t *tc)
{

    uint8_t bs0 = 1;
    uint8_t bs1 = 1;
    uint8_t bs2 = 1;
    uint8_t bs3 = 1;

    if (tc[0] < 0)
        bs0 = 0;
    if (tc[1] < 0)
        bs1 = 0;
    if (tc[2] < 0)
        bs2 = 0;
    if (tc[3] < 0)
        bs3 = 0;

    avc_loopfilter_luma_inter_edge_hor_msa(data, bs0, bs1, bs2, bs3,
                                           tc[0], tc[1], tc[2], tc[3],
                                           alpha, beta, img_width);
}

void ff_h264_h_lpf_chroma_inter_msa(uint8_t *data, int img_width,
                                    int alpha, int beta, int8_t *tc)
{
    uint8_t bs0 = 1;
    uint8_t bs1 = 1;
    uint8_t bs2 = 1;
    uint8_t bs3 = 1;

    if (tc[0] < 0)
        bs0 = 0;
    if (tc[1] < 0)
        bs1 = 0;
    if (tc[2] < 0)
        bs2 = 0;
    if (tc[3] < 0)
        bs3 = 0;

    avc_loopfilter_cb_or_cr_inter_edge_ver_msa(data, bs0, bs1, bs2, bs3,
                                               tc[0], tc[1], tc[2], tc[3],
                                               alpha, beta, img_width);
}

void ff_h264_v_lpf_chroma_inter_msa(uint8_t *data, int img_width,
                                    int alpha, int beta, int8_t *tc)
{
    uint8_t bs0 = 1;
    uint8_t bs1 = 1;
    uint8_t bs2 = 1;
    uint8_t bs3 = 1;

    if (tc[0] < 0)
        bs0 = 0;
    if (tc[1] < 0)
        bs1 = 0;
    if (tc[2] < 0)
        bs2 = 0;
    if (tc[3] < 0)
        bs3 = 0;

    avc_loopfilter_cb_or_cr_inter_edge_hor_msa(data, bs0, bs1, bs2, bs3,
                                               tc[0], tc[1], tc[2], tc[3],
                                               alpha, beta, img_width);
}

void ff_h264_h_lpf_luma_intra_msa(uint8_t *data, int img_width,
                                  int alpha, int beta)
{
    avc_loopfilter_luma_intra_edge_ver_msa(data, (uint8_t) alpha,
                                           (uint8_t) beta,
                                           (unsigned int) img_width);
}

void ff_h264_v_lpf_luma_intra_msa(uint8_t *data, int img_width,
                                  int alpha, int beta)
{
    avc_loopfilter_luma_intra_edge_hor_msa(data, (uint8_t) alpha,
                                           (uint8_t) beta,
                                           (unsigned int) img_width);
}

void ff_h264_h_lpf_chroma_intra_msa(uint8_t *data, int img_width,
                                    int alpha, int beta)
{
    avc_loopfilter_cb_or_cr_intra_edge_ver_msa(data, (uint8_t) alpha,
                                               (uint8_t) beta,
                                               (unsigned int) img_width);
}

void ff_h264_v_lpf_chroma_intra_msa(uint8_t *data, int img_width,
                                    int alpha, int beta)
{
    avc_loopfilter_cb_or_cr_intra_edge_hor_msa(data, (uint8_t) alpha,
                                               (uint8_t) beta,
                                               (unsigned int) img_width);
}

void ff_h264_h_loop_filter_chroma422_msa(uint8_t *src,
                                         int32_t ystride,
                                         int32_t alpha, int32_t beta,
                                         int8_t *tc0)
{
    avc_h_loop_filter_chroma422_msa(src, ystride, alpha, beta, tc0);
}

void ff_h264_h_loop_filter_chroma422_mbaff_msa(uint8_t *src,
                                               int32_t ystride,
                                               int32_t alpha,
                                               int32_t beta,
                                               int8_t *tc0)
{
    avc_h_loop_filter_chroma422_mbaff_msa(src, ystride, alpha, beta, tc0);
}

void ff_h264_h_loop_filter_luma_mbaff_msa(uint8_t *src,
                                          int32_t ystride,
                                          int32_t alpha,
                                          int32_t beta,
                                          int8_t *tc0)
{
    avc_h_loop_filter_luma_mbaff_msa(src, ystride, alpha, beta, tc0);
}

void ff_h264_h_loop_filter_luma_mbaff_intra_msa(uint8_t *src,
                                                int32_t ystride,
                                                int32_t alpha,
                                                int32_t beta)
{
    avc_h_loop_filter_luma_mbaff_intra_msa(src, ystride, alpha, beta);
}

void ff_weight_h264_pixels16_8_msa(uint8_t *src, ptrdiff_t stride,
                                   int height, int log2_denom,
                                   int weight_src, int offset_in)
{
    uint32_t offset_val;
    v16i8 zero = { 0 };
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 src0_l, src1_l, src2_l, src3_l, src0_r, src1_r, src2_r, src3_r;
    v8i16 src4_l, src5_l, src6_l, src7_l, src4_r, src5_r, src6_r, src7_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;
    v8i16 wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt = __msa_fill_h(weight_src);
    offset = __msa_fill_h(offset_val);
    denom = __msa_fill_h(log2_denom);

    LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, src0_r, src1_r,
               src2_r, src3_r);
    ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, src0_l, src1_l,
               src2_l, src3_l);
    ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, src4_r, src5_r,
               src6_r, src7_r);
    ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, src4_l, src5_l,
               src6_l, src7_l);
    MUL4(wgt, src0_r, wgt, src0_l, wgt, src1_r, wgt, src1_l, tmp0, tmp1, tmp2,
         tmp3);
    MUL4(wgt, src2_r, wgt, src2_l, wgt, src3_r, wgt, src3_l, tmp4, tmp5, tmp6,
         tmp7);
    MUL4(wgt, src4_r, wgt, src4_l, wgt, src5_r, wgt, src5_l, tmp8, tmp9, tmp10,
         tmp11);
    MUL4(wgt, src6_r, wgt, src6_l, wgt, src7_r, wgt, src7_l, tmp12, tmp13,
         tmp14, tmp15);
    ADDS_SH4_SH(tmp0, offset, tmp1, offset, tmp2, offset, tmp3, offset, tmp0,
                tmp1, tmp2, tmp3);
    ADDS_SH4_SH(tmp4, offset, tmp5, offset, tmp6, offset, tmp7, offset, tmp4,
                tmp5, tmp6, tmp7);
    ADDS_SH4_SH(tmp8, offset, tmp9, offset, tmp10, offset, tmp11, offset, tmp8,
                tmp9, tmp10, tmp11);
    ADDS_SH4_SH(tmp12, offset, tmp13, offset, tmp14, offset, tmp15, offset,
                tmp12, tmp13, tmp14, tmp15);
    MAXI_SH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 0);
    MAXI_SH8_SH(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, 0);
    SRLR_H8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom);
    SRLR_H8_SH(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, denom);
    SAT_UH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 7);
    SAT_UH8_SH(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, 7);
    PCKEV_B4_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, dst0, dst1,
                dst2, dst3);
    PCKEV_B4_UB(tmp9, tmp8, tmp11, tmp10, tmp13, tmp12, tmp15, tmp14, dst4,
                dst5, dst6, dst7);
    ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, src, stride);
    src += 8 * stride;

    if (16 == height) {
        LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, src0_r,
                   src1_r, src2_r, src3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, src0_l,
                   src1_l, src2_l, src3_l);
        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, src4_r,
                   src5_r, src6_r, src7_r);
        ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, src4_l,
                   src5_l, src6_l, src7_l);
        MUL4(wgt, src0_r, wgt, src0_l, wgt, src1_r, wgt, src1_l, tmp0, tmp1,
             tmp2, tmp3);
        MUL4(wgt, src2_r, wgt, src2_l, wgt, src3_r, wgt, src3_l, tmp4, tmp5,
             tmp6, tmp7);
        MUL4(wgt, src4_r, wgt, src4_l, wgt, src5_r, wgt, src5_l, tmp8, tmp9,
             tmp10, tmp11);
        MUL4(wgt, src6_r, wgt, src6_l, wgt, src7_r, wgt, src7_l, tmp12, tmp13,
             tmp14, tmp15);
        ADDS_SH4_SH(tmp0, offset, tmp1, offset, tmp2, offset, tmp3, offset,
                    tmp0, tmp1, tmp2, tmp3);
        ADDS_SH4_SH(tmp4, offset, tmp5, offset, tmp6, offset, tmp7, offset,
                    tmp4, tmp5, tmp6, tmp7);
        ADDS_SH4_SH(tmp8, offset, tmp9, offset, tmp10, offset, tmp11, offset,
                    tmp8, tmp9, tmp10, tmp11);
        ADDS_SH4_SH(tmp12, offset, tmp13, offset, tmp14, offset, tmp15, offset,
                    tmp12, tmp13, tmp14, tmp15);
        MAXI_SH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 0);
        MAXI_SH8_SH(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, 0);
        SRLR_H8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom);
        SRLR_H8_SH(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, denom);
        SAT_UH8_SH(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, 7);
        SAT_UH8_SH(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, 7);
        PCKEV_B4_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, dst0, dst1,
                    dst2, dst3);
        PCKEV_B4_UB(tmp9, tmp8, tmp11, tmp10, tmp13, tmp12, tmp15, tmp14, dst4,
                    dst5, dst6, dst7);
        ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, src, stride);
    }
}

void ff_weight_h264_pixels8_8_msa(uint8_t *src, ptrdiff_t stride,
                                  int height, int log2_denom,
                                  int weight_src, int offset)
{
    if (4 == height) {
        avc_wgt_8x4_msa(src, stride, log2_denom, weight_src, offset);
    } else if (8 == height) {
        avc_wgt_8x8_msa(src, stride, log2_denom, weight_src, offset);
    } else {
        avc_wgt_8x16_msa(src, stride, log2_denom, weight_src, offset);
    }
}

void ff_weight_h264_pixels4_8_msa(uint8_t *src, ptrdiff_t stride,
                                  int height, int log2_denom,
                                  int weight_src, int offset)
{
    if (2 == height) {
        avc_wgt_4x2_msa(src, stride, log2_denom, weight_src, offset);
    } else if (4 == height) {
        avc_wgt_4x4_msa(src, stride, log2_denom, weight_src, offset);
    } else {
        avc_wgt_4x8_msa(src, stride, log2_denom, weight_src, offset);
    }
}

void ff_biweight_h264_pixels16_8_msa(uint8_t *dst, uint8_t *src,
                                     ptrdiff_t stride, int height,
                                     int log2_denom, int weight_dst,
                                     int weight_src, int offset_in)
{
    v16i8 src_wgt, dst_wgt, wgt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;
    v8i16 denom, offset;

    offset_in = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in += (128 * (weight_src + weight_dst));

    src_wgt = __msa_fill_b(weight_src);
    dst_wgt = __msa_fill_b(weight_dst);
    offset = __msa_fill_h(offset_in);
    denom = __msa_fill_h(log2_denom + 1);

    wgt = __msa_ilvev_b(dst_wgt, src_wgt);

    LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += 8 * stride;
    LD_UB8(dst, stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    XORI_B8_128_UB(src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_UB(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_B4_SB(dst0, src0, dst1, src1, dst2, src2, dst3, src3, vec0, vec2, vec4,
               vec6);
    ILVL_B4_SB(dst0, src0, dst1, src1, dst2, src2, dst3, src3, vec1, vec3, vec5,
               vec7);
    ILVR_B4_SB(dst4, src4, dst5, src5, dst6, src6, dst7, src7, vec8, vec10,
               vec12, vec14);
    ILVL_B4_SB(dst4, src4, dst5, src5, dst6, src6, dst7, src7, vec9, vec11,
               vec13, vec15);
    tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
    tmp1 = __msa_dpadd_s_h(offset, wgt, vec1);
    tmp2 = __msa_dpadd_s_h(offset, wgt, vec2);
    tmp3 = __msa_dpadd_s_h(offset, wgt, vec3);
    tmp4 = __msa_dpadd_s_h(offset, wgt, vec4);
    tmp5 = __msa_dpadd_s_h(offset, wgt, vec5);
    tmp6 = __msa_dpadd_s_h(offset, wgt, vec6);
    tmp7 = __msa_dpadd_s_h(offset, wgt, vec7);
    tmp8 = __msa_dpadd_s_h(offset, wgt, vec8);
    tmp9 = __msa_dpadd_s_h(offset, wgt, vec9);
    tmp10 = __msa_dpadd_s_h(offset, wgt, vec10);
    tmp11 = __msa_dpadd_s_h(offset, wgt, vec11);
    tmp12 = __msa_dpadd_s_h(offset, wgt, vec12);
    tmp13 = __msa_dpadd_s_h(offset, wgt, vec13);
    tmp14 = __msa_dpadd_s_h(offset, wgt, vec14);
    tmp15 = __msa_dpadd_s_h(offset, wgt, vec15);
    SRA_4V(tmp0, tmp1, tmp2, tmp3, denom);
    SRA_4V(tmp4, tmp5, tmp6, tmp7, denom);
    SRA_4V(tmp8, tmp9, tmp10, tmp11, denom);
    SRA_4V(tmp12, tmp13, tmp14, tmp15, denom);
    CLIP_SH8_0_255(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
    CLIP_SH8_0_255(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15);
    PCKEV_B4_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, dst0, dst1,
                dst2, dst3);
    PCKEV_B4_UB(tmp9, tmp8, tmp11, tmp10, tmp13, tmp12, tmp15, tmp14, dst4,
                dst5, dst6, dst7);
    ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst, stride);
    dst += 8 * stride;

    if (16 == height) {
        LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
        LD_UB8(dst, stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
        XORI_B8_128_UB(src0, src1, src2, src3, src4, src5, src6, src7);
        XORI_B8_128_UB(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
        ILVR_B4_SB(dst0, src0, dst1, src1, dst2, src2, dst3, src3, vec0, vec2,
                   vec4, vec6);
        ILVL_B4_SB(dst0, src0, dst1, src1, dst2, src2, dst3, src3, vec1, vec3,
                   vec5, vec7);
        ILVR_B4_SB(dst4, src4, dst5, src5, dst6, src6, dst7, src7, vec8, vec10,
                   vec12, vec14);
        ILVL_B4_SB(dst4, src4, dst5, src5, dst6, src6, dst7, src7, vec9, vec11,
                   vec13, vec15);
        tmp0 = __msa_dpadd_s_h(offset, wgt, vec0);
        tmp1 = __msa_dpadd_s_h(offset, wgt, vec1);
        tmp2 = __msa_dpadd_s_h(offset, wgt, vec2);
        tmp3 = __msa_dpadd_s_h(offset, wgt, vec3);
        tmp4 = __msa_dpadd_s_h(offset, wgt, vec4);
        tmp5 = __msa_dpadd_s_h(offset, wgt, vec5);
        tmp6 = __msa_dpadd_s_h(offset, wgt, vec6);
        tmp7 = __msa_dpadd_s_h(offset, wgt, vec7);
        tmp8 = __msa_dpadd_s_h(offset, wgt, vec8);
        tmp9 = __msa_dpadd_s_h(offset, wgt, vec9);
        tmp10 = __msa_dpadd_s_h(offset, wgt, vec10);
        tmp11 = __msa_dpadd_s_h(offset, wgt, vec11);
        tmp12 = __msa_dpadd_s_h(offset, wgt, vec12);
        tmp13 = __msa_dpadd_s_h(offset, wgt, vec13);
        tmp14 = __msa_dpadd_s_h(offset, wgt, vec14);
        tmp15 = __msa_dpadd_s_h(offset, wgt, vec15);
        SRA_4V(tmp0, tmp1, tmp2, tmp3, denom);
        SRA_4V(tmp4, tmp5, tmp6, tmp7, denom);
        SRA_4V(tmp8, tmp9, tmp10, tmp11, denom);
        SRA_4V(tmp12, tmp13, tmp14, tmp15, denom);
        CLIP_SH8_0_255(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
        CLIP_SH8_0_255(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15);
        PCKEV_B4_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, dst0, dst1,
                    dst2, dst3);
        PCKEV_B4_UB(tmp9, tmp8, tmp11, tmp10, tmp13, tmp12, tmp15, tmp14, dst4,
                    dst5, dst6, dst7);
        ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst, stride);
    }
}

void ff_biweight_h264_pixels8_8_msa(uint8_t *dst, uint8_t *src,
                                    ptrdiff_t stride, int height,
                                    int log2_denom, int weight_dst,
                                    int weight_src, int offset)
{
    if (4 == height) {
        avc_biwgt_8x4_msa(src, dst, stride, log2_denom, weight_src, weight_dst,
                          offset);
    } else if (8 == height) {
        avc_biwgt_8x8_msa(src, dst, stride, log2_denom, weight_src, weight_dst,
                          offset);
    } else {
        avc_biwgt_8x16_msa(src, dst, stride, log2_denom, weight_src, weight_dst,
                           offset);
    }
}

void ff_biweight_h264_pixels4_8_msa(uint8_t *dst, uint8_t *src,
                                    ptrdiff_t stride, int height,
                                    int log2_denom, int weight_dst,
                                    int weight_src, int offset)
{
    if (2 == height) {
        avc_biwgt_4x2_msa(src, dst, stride, log2_denom, weight_src, weight_dst,
                          offset);
    } else if (4 == height) {
        avc_biwgt_4x4_msa(src, dst, stride, log2_denom, weight_src, weight_dst,
                          offset);
    } else {
        avc_biwgt_4x8_msa(src, dst, stride, log2_denom, weight_src, weight_dst,
                          offset);
    }
}
