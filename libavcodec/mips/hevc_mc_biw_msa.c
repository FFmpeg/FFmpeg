/*
 * Copyright (c) 2015 - 2017 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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
#include "libavcodec/mips/hevcdsp_mips.h"
#include "libavcodec/mips/hevc_macros_msa.h"

static const uint8_t ff_hevc_mask_arr[16 * 2] __attribute__((aligned(0x40))) = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20
};

#define HEVC_BIW_RND_CLIP2(in0, in1, vec0, vec1, wgt, rnd, offset,  \
                           out0, out1)                              \
{                                                                   \
    v4i32 out0_r, out1_r, out0_l, out1_l;                           \
                                                                    \
    ILVR_H2_SW(in0, vec0, in1, vec1, out0_r, out1_r);               \
    ILVL_H2_SW(in0, vec0, in1, vec1, out0_l, out1_l);               \
                                                                    \
    out0_r = __msa_dpadd_s_w(offset, (v8i16) out0_r, (v8i16) wgt);  \
    out1_r = __msa_dpadd_s_w(offset, (v8i16) out1_r, (v8i16) wgt);  \
    out0_l = __msa_dpadd_s_w(offset, (v8i16) out0_l, (v8i16) wgt);  \
    out1_l = __msa_dpadd_s_w(offset, (v8i16) out1_l, (v8i16) wgt);  \
                                                                    \
    SRAR_W4_SW(out0_r, out1_r, out0_l, out1_l, rnd);                \
    PCKEV_H2_SH(out0_l, out0_r, out1_l, out1_r, out0, out1);        \
    CLIP_SH2_0_255(out0, out1);                                     \
}

#define HEVC_BIW_RND_CLIP4(in0, in1, in2, in3, vec0, vec1, vec2, vec3,       \
                           wgt, rnd, offset, out0, out1, out2, out3)         \
{                                                                            \
    HEVC_BIW_RND_CLIP2(in0, in1, vec0, vec1, wgt, rnd, offset, out0, out1);  \
    HEVC_BIW_RND_CLIP2(in2, in3, vec2, vec3, wgt, rnd, offset, out2, out3);  \
}

#define HEVC_BIW_RND_CLIP2_MAX_SATU(in0, in1, vec0, vec1, wgt, rnd,  \
                                    offset, out0, out1)              \
{                                                                    \
    v4i32 out0_r, out1_r, out0_l, out1_l;                            \
                                                                     \
    ILVR_H2_SW(in0, vec0, in1, vec1, out0_r, out1_r);                \
    ILVL_H2_SW(in0, vec0, in1, vec1, out0_l, out1_l);                \
    out0_r = __msa_dpadd_s_w(offset, (v8i16) out0_r, (v8i16) wgt);   \
    out1_r = __msa_dpadd_s_w(offset, (v8i16) out1_r, (v8i16) wgt);   \
    out0_l = __msa_dpadd_s_w(offset, (v8i16) out0_l, (v8i16) wgt);   \
    out1_l = __msa_dpadd_s_w(offset, (v8i16) out1_l, (v8i16) wgt);   \
    SRAR_W4_SW(out0_r, out1_r, out0_l, out1_l, rnd);                 \
    PCKEV_H2_SH(out0_l, out0_r, out1_l, out1_r, out0, out1);         \
    CLIP_SH2_0_255_MAX_SATU(out0, out1);                             \
}

#define HEVC_BIW_RND_CLIP4_MAX_SATU(in0, in1, in2, in3, vec0, vec1, vec2,  \
                                    vec3, wgt, rnd, offset, out0, out1,    \
                                    out2, out3)                            \
{                                                                          \
    HEVC_BIW_RND_CLIP2_MAX_SATU(in0, in1, vec0, vec1, wgt, rnd, offset,    \
                                out0, out1);                               \
    HEVC_BIW_RND_CLIP2_MAX_SATU(in2, in3, vec2, vec3, wgt, rnd, offset,    \
                                out2, out3);                               \
}

static void hevc_biwgt_copy_4w_msa(uint8_t *src0_ptr,
                                   int32_t src_stride,
                                   int16_t *src1_ptr,
                                   int32_t src2_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   int32_t height,
                                   int32_t weight0,
                                   int32_t weight1,
                                   int32_t offset0,
                                   int32_t offset1,
                                   int32_t rnd_val)
{
    uint32_t loop_cnt, tp0, tp1, tp2, tp3;
    uint64_t tpd0, tpd1, tpd2, tpd3;
    int32_t offset, weight;
    v16u8 out0, out1;
    v16i8 zero = { 0 };
    v16i8 src0 = { 0 }, src1 = { 0 };
    v8i16 in0 = { 0 }, in1 = { 0 }, in2 = { 0 }, in3 = { 0 };
    v8i16 dst0, dst1, dst2, dst3, weight_vec;
    v4i32 dst0_r, dst0_l, offset_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    if (2 == height) {
        LW2(src0_ptr, src_stride, tp0, tp1);
        INSERT_W2_SB(tp0, tp1, src0);
        LD2(src1_ptr, src2_stride, tpd0, tpd1);
        INSERT_D2_SH(tpd0, tpd1, in0);

        dst0 = (v8i16) __msa_ilvr_b(zero, src0);
        dst0 <<= 6;

        ILVRL_H2_SW(dst0, in0, dst0_r, dst0_l);
        dst0_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_r, weight_vec);
        dst0_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_l, weight_vec);
        SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
        dst0 = (v8i16) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
        dst0 = CLIP_SH_0_255_MAX_SATU(dst0);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst0, (v16i8) dst0);
        ST_W2(out0, 0, 1, dst, dst_stride);
    } else if (4 == height) {
        LW4(src0_ptr, src_stride, tp0, tp1, tp2, tp3);
        INSERT_W4_SB(tp0, tp1, tp2, tp3, src0);
        LD4(src1_ptr, src2_stride, tpd0, tpd1, tpd2, tpd3);
        INSERT_D2_SH(tpd0, tpd1, in0);
        INSERT_D2_SH(tpd2, tpd3, in1);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        SLLI_2V(dst0, dst1, 6);
        HEVC_BIW_RND_CLIP2_MAX_SATU(dst0, dst1, in0, in1, weight_vec, rnd_vec,
                                    offset_vec, dst0, dst1);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
        ST_W4(out0, 0, 1, 2, 3, dst, dst_stride);
    } else if (0 == height % 8) {
        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LW4(src0_ptr, src_stride, tp0, tp1, tp2, tp3);
            src0_ptr += 4 * src_stride;
            INSERT_W4_SB(tp0, tp1, tp2, tp3, src0);
            LW4(src0_ptr, src_stride, tp0, tp1, tp2, tp3);
            src0_ptr += 4 * src_stride;
            INSERT_W4_SB(tp0, tp1, tp2, tp3, src1);
            LD4(src1_ptr, src2_stride, tpd0, tpd1, tpd2, tpd3);
            src1_ptr += (4 * src2_stride);
            INSERT_D2_SH(tpd0, tpd1, in0);
            INSERT_D2_SH(tpd2, tpd3, in1);
            LD4(src1_ptr, src2_stride, tpd0, tpd1, tpd2, tpd3);
            src1_ptr += (4 * src2_stride);
            INSERT_D2_SH(tpd0, tpd1, in2);
            INSERT_D2_SH(tpd2, tpd3, in3);
            ILVRL_B2_SH(zero, src0, dst0, dst1);
            ILVRL_B2_SH(zero, src1, dst2, dst3);
            SLLI_4V(dst0, dst1, dst2, dst3, 6);
            HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3, in0, in1, in2,
                                        in3, weight_vec, rnd_vec, offset_vec,
                                        dst0, dst1, dst2, dst3);
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
            ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
            dst += (8 * dst_stride);
        }
    }
}

static void hevc_biwgt_copy_6w_msa(uint8_t *src0_ptr,
                                   int32_t src_stride,
                                   int16_t *src1_ptr,
                                   int32_t src2_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   int32_t height,
                                   int32_t weight0,
                                   int32_t weight1,
                                   int32_t offset0,
                                   int32_t offset1,
                                   int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 out0, out1;
    v16i8 zero = { 0 };
    v16i8 src0 = { 0 }, src1 = { 0 };
    v8i16 in0, in1, in2, in3;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD4(src0_ptr, src_stride, tp0, tp1, tp2, tp3);
        src0_ptr += (4 * src_stride);
        INSERT_D2_SB(tp0, tp1, src0);
        INSERT_D2_SB(tp2, tp3, src1);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3,
                                    in0, in1, in2, in3,
                                    weight_vec, rnd_vec, offset_vec,
                                    dst0, dst1, dst2, dst3);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_W2(out0, 0, 2, dst, dst_stride);
        ST_H2(out0, 2, 6, dst + 4, dst_stride);
        ST_W2(out1, 0, 2, dst + 2 * dst_stride, dst_stride);
        ST_H2(out1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_biwgt_copy_8w_msa(uint8_t *src0_ptr,
                                   int32_t src_stride,
                                   int16_t *src1_ptr,
                                   int32_t src2_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   int32_t height,
                                   int32_t weight0,
                                   int32_t weight1,
                                   int32_t offset0,
                                   int32_t offset1,
                                   int32_t rnd_val)
{
    uint64_t tp0, tp1, tp2, tp3;
    int32_t offset, weight;
    v16u8 out0, out1, out2;
    v16i8 zero = { 0 };
    v16i8 src0 = { 0 }, src1 = { 0 }, src2 = { 0 };
    v8i16 in0, in1, in2, in3, in4, in5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    if (2 == height) {
        LD2(src0_ptr, src_stride, tp0, tp1);
        INSERT_D2_SB(tp0, tp1, src0);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        SLLI_2V(dst0, dst1, 6);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1);

        out0 = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
        ST_D2(out0, 0, 1, dst, dst_stride);
    } else if (6 == height) {
        LD4(src0_ptr, src_stride, tp0, tp1, tp2, tp3);
        src0_ptr += 4 * src_stride;
        INSERT_D2_SB(tp0, tp1, src0);
        INSERT_D2_SB(tp2, tp3, src1);
        LD2(src0_ptr, src_stride, tp0, tp1);
        INSERT_D2_SB(tp0, tp1, src2);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        LD_SH6(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_2V(dst4, dst5, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3, in0, in1, in2, in3,
                                    weight_vec, rnd_vec, offset_vec, dst0, dst1,
                                    dst2, dst3);
        HEVC_BIW_RND_CLIP2_MAX_SATU(dst4, dst5, in4, in5, weight_vec, rnd_vec,
                                    offset_vec, dst4, dst5);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
    } else if (0 == height % 4) {
        uint32_t loop_cnt;

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD4(src0_ptr, src_stride, tp0, tp1, tp2, tp3);
            src0_ptr += (4 * src_stride);
            INSERT_D2_SB(tp0, tp1, src0);
            INSERT_D2_SB(tp2, tp3, src1);
            ILVRL_B2_SH(zero, src0, dst0, dst1);
            ILVRL_B2_SH(zero, src1, dst2, dst3);
            LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
            src1_ptr += (4 * src2_stride);

            SLLI_4V(dst0, dst1, dst2, dst3, 6);
            HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3, in0, in1, in2,
                                        in3, weight_vec, rnd_vec, offset_vec,
                                        dst0, dst1, dst2, dst3);
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
            ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

static void hevc_biwgt_copy_12w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16i8 zero = { 0 };
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = (16 >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        LD_SH4(src1_ptr + 8, src2_stride, in4, in5, in6, in7);
        src1_ptr += (4 * src2_stride);

        ILVR_D2_SH(in5, in4, in7, in6, in4, in5);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   dst0, dst1, dst2, dst3);

        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        ILVL_W2_SB(src1, src0, src3, src2, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, dst4, dst5);

        dst4 <<= 6;
        dst5 <<= 6;
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3, in0, in1, in2, in3,
                                    weight_vec, rnd_vec, offset_vec, dst0, dst1,
                                    dst2, dst3);
        HEVC_BIW_RND_CLIP2_MAX_SATU(dst4, dst5, in4, in5, weight_vec, rnd_vec,
                                    offset_vec, dst4, dst5);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_biwgt_copy_16w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16u8 out0, out1, out2, out3;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        LD_SH4(src1_ptr + 8, src2_stride, in4, in5, in6, in7);
        src1_ptr += (4 * src2_stride);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, tmp0, tmp1,
                   tmp2, tmp3);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, tmp4, tmp5,
                   tmp6, tmp7);
        SLLI_4V(tmp0, tmp1, tmp2, tmp3, 6);
        SLLI_4V(tmp4, tmp5, tmp6, tmp7, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(tmp0, tmp1, tmp4, tmp5, in0, in1, in4, in5,
                                    weight_vec, rnd_vec, offset_vec, tmp0, tmp1,
                                    tmp4, tmp5);
        HEVC_BIW_RND_CLIP4_MAX_SATU(tmp2, tmp3, tmp6, tmp7, in2, in3, in6, in7,
                                    weight_vec, rnd_vec, offset_vec, tmp2, tmp3,
                                    tmp6, tmp7);
        PCKEV_B2_UB(tmp4, tmp0, tmp5, tmp1, out0, out1);
        PCKEV_B2_UB(tmp6, tmp2, tmp7, tmp3, out2, out3);
        ST_UB4(out0, out1, out2, out3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_biwgt_copy_24w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16u8 out0, out1, out2, out3, out4, out5;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, dst9, dst10;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7, in8, in9, in10, in11, dst11;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = 8; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src4, src5);
        LD_SB4(src0_ptr + 16, src_stride, src2, src3, src6, src7);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        LD_SH4(src1_ptr + 8, src2_stride, in4, in5, in6, in7);
        LD_SH4(src1_ptr + 16, src2_stride, in8, in9, in10, in11);
        src1_ptr += (4 * src2_stride);

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVR_B2_SH(zero, src2, zero, src3, dst4, dst5);
        ILVRL_B2_SH(zero, src4, dst6, dst7);
        ILVRL_B2_SH(zero, src5, dst8, dst9);
        ILVR_B2_SH(zero, src6, zero, src7, dst10, dst11);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);
        SLLI_4V(dst8, dst9, dst10, dst11, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3, in0, in4, in1, in5,
                                    weight_vec, rnd_vec, offset_vec, dst0, dst1,
                                    dst2, dst3);
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst4, dst5, dst6, dst7, in8, in9, in2, in6,
                                    weight_vec, rnd_vec, offset_vec, dst4, dst5,
                                    dst6, dst7);
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst8, dst9, dst10, dst11, in3, in7, in10,
                                    in11, weight_vec, rnd_vec, offset_vec,
                                    dst8, dst9, dst10, dst11);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        PCKEV_B3_UB(dst7, dst6, dst9, dst8, dst11, dst10, out3, out4, out5);
        ST_UB4(out0, out1, out3, out4, dst, dst_stride);
        ST_D4(out2, out5, 0, 1, 0, 1, dst + 16, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_biwgt_copy_32w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16u8 out0, out1, out2, out3;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src0_ptr, 16, src0, src1);
        src0_ptr += src_stride;
        LD_SB2(src0_ptr, 16, src2, src3);
        src0_ptr += src_stride;
        LD_SH4(src1_ptr, 8, in0, in1, in2, in3);
        src1_ptr += src2_stride;
        LD_SH4(src1_ptr, 8, in4, in5, in6, in7);
        src1_ptr += src2_stride;

        ILVRL_B2_SH(zero, src0, tmp0, tmp4);
        ILVRL_B2_SH(zero, src1, tmp1, tmp5);
        ILVRL_B2_SH(zero, src2, tmp2, tmp6);
        ILVRL_B2_SH(zero, src3, tmp3, tmp7);
        SLLI_4V(tmp0, tmp1, tmp2, tmp3, 6);
        SLLI_4V(tmp4, tmp5, tmp6, tmp7, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(tmp0, tmp4, tmp1, tmp5, in0, in1, in2, in3,
                                    weight_vec, rnd_vec, offset_vec, tmp0, tmp4,
                                    tmp1, tmp5);
        HEVC_BIW_RND_CLIP4_MAX_SATU(tmp2, tmp6, tmp3, tmp7, in4, in5, in6, in7,
                                    weight_vec, rnd_vec, offset_vec, tmp2, tmp6,
                                    tmp3, tmp7);
        PCKEV_B2_UB(tmp4, tmp0, tmp5, tmp1, out0, out1);
        PCKEV_B2_UB(tmp6, tmp2, tmp7, tmp3, out2, out3);
        ST_UB2(out0, out1, dst, 16);
        dst += dst_stride;
        ST_UB2(out2, out3, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_biwgt_copy_48w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2;
    v16i8 zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, in0, in1, in2, in3, in4, in5;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = 64; loop_cnt--;) {
        LD_SB3(src0_ptr, 16, src0, src1, src2);
        src0_ptr += src_stride;
        LD_SH6(src1_ptr, 8, in0, in1, in2, in3, in4, in5);
        src1_ptr += src2_stride;

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_2V(dst4, dst5, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(dst0, dst1, dst2, dst3, in0, in1, in2, in3,
                                    weight_vec, rnd_vec, offset_vec, dst0, dst1,
                                    dst2, dst3);
        HEVC_BIW_RND_CLIP2_MAX_SATU(dst4, dst5, in4, in5, weight_vec, rnd_vec,
                                    offset_vec, dst4, dst5);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_UB2(out0, out1, dst, 16);
        ST_UB(out2, dst + 32);
        dst += dst_stride;
    }
}

static void hevc_biwgt_copy_64w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16u8 out0, out1, out2, out3;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB4(src0_ptr, 16, src0, src1, src2, src3);
        src0_ptr += src_stride;
        LD_SH8(src1_ptr, 8, in0, in1, in2, in3, in4, in5, in6, in7);
        src1_ptr += src2_stride;

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, tmp0, tmp1,
                   tmp2, tmp3);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, tmp4, tmp5,
                   tmp6, tmp7);
        SLLI_4V(tmp0, tmp1, tmp2, tmp3, 6);
        SLLI_4V(tmp4, tmp5, tmp6, tmp7, 6);
        HEVC_BIW_RND_CLIP4_MAX_SATU(tmp0, tmp4, tmp1, tmp5, in0, in1, in2, in3,
                                    weight_vec, rnd_vec, offset_vec, tmp0, tmp4,
                                    tmp1, tmp5);
        HEVC_BIW_RND_CLIP4_MAX_SATU(tmp2, tmp6, tmp3, tmp7, in4, in5, in6, in7,
                                    weight_vec, rnd_vec, offset_vec, tmp2, tmp6,
                                    tmp3, tmp7);
        PCKEV_B2_UB(tmp4, tmp0, tmp5, tmp1, out0, out1);
        PCKEV_B2_UB(tmp6, tmp2, tmp7, tmp3, out2, out3);
        ST_UB4(out0, out1, out2, out3, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_hz_biwgt_8t_4w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1;
    v8i16 in0, in1, in2, in3;
    v8i16 filter_vec, out0, out1;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);

    src0_ptr -= 3;
    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B4_SB(src0, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1);

        out0 = (v8i16) __msa_pckev_b((v16i8) out1, (v16i8) out0);
        ST_W4(out0, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_biwgt_8t_8w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 in0, in1, in2, in3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1, out2, out3);

        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_biwgt_8t_12w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, vec0, vec1, vec2, vec3;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v8i16 filt0, filt1, filt2, filt3, out0, out1, out2, out3;
    v8i16 dst0, dst1, dst2, dst3, in0, in1, in2, in3, filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;

    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset = (offset0 + offset1) << rnd_val;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = LD_SB(&ff_hevc_mask_arr[16]);
    mask5 = mask4 + 2;
    mask6 = mask4 + 4;
    mask7 = mask4 + 6;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst3 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3, in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec, out0, out1, out2,
                           out3);
        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);

        LD_SB4(src0_ptr + 8, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr + 8, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7, vec0, vec1, vec2,
                   vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src3, mask4, mask5, mask6, mask7, vec0, vec1, vec2,
                   vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1, weight_vec, rnd_vec,
                           offset_vec, out0, out1);
        out0 = (v8i16) __msa_pckev_b((v16i8) out1, (v16i8) out0);
        ST_W4(out0, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_biwgt_8t_16w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src0_ptr, 8, src0, src1);
        src0_ptr += src_stride;
        LD_SB2(src0_ptr, 8, src2, src3);
        src0_ptr += src_stride;
        LD_SH2(src1_ptr, 8, in0, in1);
        src1_ptr += src2_stride;
        LD_SH2(src1_ptr, 8, in2, in3);
        src1_ptr += src2_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1, out2, out3);

        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_SH2(out0, out1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_biwgt_8t_24w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint64_t dst_val0;
    int32_t offset, weight, constant;
    v16i8 src0, src1;
    v8i16 in0, in1, in2;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2;
    v4i32 dst2_r, dst2_l;
    v8i16 filter_vec, out0, out1, out2;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);

    src0_ptr = src0_ptr - 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    LD_SB2(src0_ptr, 16, src0, src1);
    src0_ptr += src_stride;
    LD_SH2(src1_ptr, 8, in0, in1);
    in2 = LD_SH(src1_ptr + 16);
    src1_ptr += src2_stride;
    XORI_B2_128_SB(src0, src1);

    for (loop_cnt = 31; loop_cnt--;) {
        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1);

        ILVRL_H2_SW(dst2, in2, dst2_r, dst2_l);
        dst2_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_r,
                                 (v8i16) weight_vec);
        dst2_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst2_r, dst2_l, rnd_vec);
        dst2_r = (v4i32) __msa_pckev_h((v8i16) dst2_l, (v8i16) dst2_r);
        out2 = CLIP_SH_0_255(dst2_r);

        LD_SB2(src0_ptr, 16, src0, src1);
        src0_ptr += src_stride;
        LD_SH2(src1_ptr, 8, in0, in1);
        in2 = LD_SH(src1_ptr + 16);
        src1_ptr += src2_stride;
        XORI_B2_128_SB(src0, src1);
        PCKEV_B2_SH(out1, out0, out2, out2, out0, out2);
        dst_val0 = __msa_copy_u_d((v2i64) out2, 0);
        ST_SH(out0, dst);
        SD(dst_val0, dst + 16);
        dst += dst_stride;
    }

    VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7, vec0, vec1, vec2, vec3);
    dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1, weight_vec, rnd_vec, offset_vec,
                       out0, out1);
    ILVRL_H2_SW(dst2, in2, dst2_r, dst2_l);
    dst2_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_r, (v8i16) weight_vec);
    dst2_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_l, (v8i16) weight_vec);
    SRAR_W2_SW(dst2_r, dst2_l, rnd_vec);
    dst2_r = (v4i32) __msa_pckev_h((v8i16) dst2_l, (v8i16) dst2_r);
    out2 = CLIP_SH_0_255(dst2_r);
    PCKEV_B2_SH(out1, out0, out2, out2, out0, out2);
    dst_val0 = __msa_copy_u_d((v2i64) out2, 0);
    ST_SH(out0, dst);
    SD(dst_val0, dst + 16);
    dst += dst_stride;
}

static void hevc_hz_biwgt_8t_32w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB2(src0_ptr, 16, src0, src1);
        src2 = LD_SB(src0_ptr + 24);
        src0_ptr += src_stride;
        LD_SH4(src1_ptr, 8, in0, in1, in2, in3);
        src1_ptr += src2_stride;

        XORI_B3_128_SB(src0, src1, src2);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1, out2, out3);

        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_SH2(out0, out1, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_hz_biwgt_8t_48w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = 64; loop_cnt--;) {
        LD_SB2(src0_ptr, 16, src0, src1);
        src2 = LD_SB(src0_ptr + 24);
        LD_SH4(src1_ptr, 8, in0, in1, in2, in3);
        XORI_B3_128_SB(src0, src1, src2);
        LD_SB2(src0_ptr + 32, 8, src3, src4);
        src0_ptr += src_stride;
        XORI_B2_128_SB(src3, src4);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3, in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1, out2, out3);

        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_SH2(out0, out1, dst, 16);

        LD_SH2(src1_ptr + 32, 8, in2, in3);
        src1_ptr += src2_stride;

        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1);

        out0 = (v8i16) __msa_pckev_b((v16i8) out1, (v16i8) out0);
        ST_SH(out0, dst + 32);
        dst += dst_stride;
    }
}

static void hevc_hz_biwgt_8t_64w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint8_t *src0_ptr_tmp;
    uint8_t *dst_tmp;
    int16_t *src1_ptr_tmp;
    uint32_t loop_cnt, cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = height; loop_cnt--;) {
        src0_ptr_tmp = src0_ptr;
        dst_tmp = dst;
        src1_ptr_tmp = src1_ptr;

        for (cnt = 2; cnt--;) {
            LD_SB2(src0_ptr_tmp, 16, src0, src1);
            src2 = LD_SB(src0_ptr_tmp + 24);
            src0_ptr_tmp += 32;
            LD_SH4(src1_ptr_tmp, 8, in0, in1, in2, in3);
            src1_ptr_tmp += 32;
            XORI_B3_128_SB(src0, src1, src2);

            VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);
            VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                       vec0, vec1, vec2, vec3);
            dst1 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);
            VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst2 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);
            VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst3 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);

            HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               out0, out1, out2, out3);

            PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
            ST_SH2(out0, out1, dst_tmp, 16);
            dst_tmp += 32;
        }

        src0_ptr += src_stride;
        src1_ptr += src2_stride;
        dst += dst_stride;

    }
}

static void hevc_vt_biwgt_8t_4w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, src13, src14;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src1110_r, src1211_r, src1312_r, src1413_r;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v16i8 src12111110, src14131312;
    v8i16 dst10, dst32, dst54, dst76;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v4i32 weight_vec, weight1_vec, offset_vec, rnd_vec, const_vec;

    src0_ptr -= (3 * src_stride);
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_w(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    weight1_vec = __msa_fill_w(weight1);
    offset_vec += const_vec * weight1_vec;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr += (7 * src_stride);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVR_D3_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r,
               src2110, src4332, src6554);
    XORI_B3_128_SB(src2110, src4332, src6554);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src0_ptr, src_stride,
               src7, src8, src9, src10, src11, src12, src13, src14);
        src0_ptr += (8 * src_stride);
        LD_SH8(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5, in6, in7);
        src1_ptr += (8 * src2_stride);

        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        ILVR_D2_SH(in5, in4, in7, in6, in2, in3);
        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        ILVR_B4_SB(src11, src10, src12, src11, src13, src12, src14, src13,
                   src1110_r, src1211_r, src1312_r, src1413_r);
        ILVR_D4_SB(src87_r, src76_r, src109_r, src98_r, src1211_r, src1110_r,
                   src1413_r, src1312_r,
                   src8776, src10998, src12111110, src14131312);
        XORI_B4_128_SB(src8776, src10998, src12111110, src14131312);

        DOTP_SB4_SH(src2110, src4332, src6554, src8776, filt0, filt0, filt0,
                    filt0, dst10, dst32, dst54, dst76);
        DPADD_SB4_SH(src4332, src6554, src8776, src10998, filt1, filt1, filt1,
                     filt1, dst10, dst32, dst54, dst76);
        DPADD_SB4_SH(src6554, src8776, src10998, src12111110, filt2, filt2,
                     filt2, filt2, dst10, dst32, dst54, dst76);
        DPADD_SB4_SH(src8776, src10998, src12111110, src14131312, filt3, filt3,
                     filt3, filt3, dst10, dst32, dst54, dst76);

        HEVC_BIW_RND_CLIP4(dst10, dst32, dst54, dst76,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1, out2, out3);

        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        src2110 = src10998;
        src4332 = src12111110;
        src6554 = src14131312;
        src6 = src14;
    }
}

static void hevc_vt_biwgt_8t_8w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, out0, out1, out2, out3;
    v4i32 weight_vec, weight1_vec, offset_vec, rnd_vec, const_vec;

    src0_ptr -= (3 * src_stride);
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_w(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    weight1_vec = __msa_fill_w(weight1);
    offset_vec += const_vec * weight1_vec;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src7, src8, src9, src10);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);

        DOTP_SB4_SH(src10_r, src21_r, src32_r, src43_r, filt0, filt0, filt0,
                    filt0, tmp0, tmp1, tmp2, tmp3);
        DPADD_SB4_SH(src32_r, src43_r, src54_r, src65_r, filt1, filt1, filt1,
                     filt1, tmp0, tmp1, tmp2, tmp3);
        DPADD_SB4_SH(src54_r, src65_r, src76_r, src87_r, filt2, filt2, filt2,
                     filt2, tmp0, tmp1, tmp2, tmp3);
        DPADD_SB4_SH(src76_r, src87_r, src98_r, src109_r, filt3, filt3, filt3,
                     filt3, tmp0, tmp1, tmp2, tmp3);

        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1, out2, out3);

        PCKEV_B2_SH(out1, out0, out3, out2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;
        src6 = src10;
    }
}

static void hevc_vt_biwgt_8t_12w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 tmp0, tmp1, tmp2;
    v16i8 src10_l, src32_l, src54_l, src76_l;
    v16i8 src21_l, src43_l, src65_l, src87_l;
    v16i8 src2110, src4332, src6554, src8776;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 out0, out1, out2, filter_vec;
    v4i32 dst2_r, dst2_l;
    v4i32 weight_vec, weight1_vec, offset_vec, rnd_vec, const_vec;

    src0_ptr -= (3 * src_stride);
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_w(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    weight1_vec = __msa_fill_w(weight1);
    offset_vec += const_vec * weight1_vec;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_l, src32_l, src54_l, src21_l);
    ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);
    ILVR_D3_SB(src21_l, src10_l, src43_l, src32_l, src65_l, src54_l,
               src2110, src4332, src6554);

    for (loop_cnt = 8; loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src7, src8);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2((src1_ptr + 8), src2_stride, in2, in3);
        src1_ptr += (2 * src2_stride);
        in2 = (v8i16) __msa_ilvr_d((v2i64) in3, (v2i64) in2);
        XORI_B2_128_SB(src7, src8);

        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        ILVL_B2_SB(src7, src6, src8, src7, src76_l, src87_l);
        src8776 = (v16i8) __msa_ilvr_d((v2i64) src87_l, (v2i64) src76_l);

        DOTP_SB3_SH(src10_r, src21_r, src2110, filt0, filt0, filt0,
                    tmp0, tmp1, tmp2);
        DPADD_SB2_SH(src32_r, src43_r, filt1, filt1, tmp0, tmp1);
        tmp2 = __msa_dpadd_s_h(tmp2, src4332, (v16i8) filt1);
        DPADD_SB2_SH(src54_r, src65_r, filt2, filt2, tmp0, tmp1);
        tmp2 = __msa_dpadd_s_h(tmp2, src6554, (v16i8) filt2);
        DPADD_SB2_SH(src76_r, src87_r, filt3, filt3, tmp0, tmp1);
        tmp2 = __msa_dpadd_s_h(tmp2, src8776, (v16i8) filt3);

        HEVC_BIW_RND_CLIP2(tmp0, tmp1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           out0, out1);

        ILVRL_H2_SW(tmp2, in2, dst2_r, dst2_l);
        dst2_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_r,
                                 (v8i16) weight_vec);
        dst2_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst2_r, dst2_l, rnd_vec);
        dst2_r = (v4i32) __msa_pckev_h((v8i16) dst2_l, (v8i16) dst2_r);
        out2 = CLIP_SH_0_255(dst2_r);
        PCKEV_B2_SH(out1, out0, out2, out2, out0, out2);
        ST_D2(out0, 0, 1, dst, dst_stride);
        ST_W2(out2, 0, 1, dst + 8, dst_stride);
        dst += (2 * dst_stride);

        src10_r = src32_r;
        src32_r = src54_r;
        src54_r = src76_r;
        src21_r = src43_r;
        src43_r = src65_r;
        src65_r = src87_r;
        src2110 = src4332;
        src4332 = src6554;
        src6554 = src8776;
        src6 = src8;
    }
}

static void hevc_vt_biwgt_8t_16multx2mult_msa(uint8_t *src0_ptr,
                                              int32_t src_stride,
                                              int16_t *src1_ptr,
                                              int32_t src2_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height,
                                              int32_t weight0,
                                              int32_t weight1,
                                              int32_t offset0,
                                              int32_t offset1,
                                              int32_t rnd_val,
                                              int32_t width)
{
    uint8_t *src0_ptr_tmp;
    int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt, cnt;
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v16i8 src10_l, src32_l, src54_l, src76_l;
    v16i8 src21_l, src43_l, src65_l, src87_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec;
    v8i16 out0, out1, out2, out3;
    v4i32 weight_vec, weight1_vec, offset_vec, rnd_vec, const_vec;

    src0_ptr -= (3 * src_stride);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_w(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    weight1_vec = __msa_fill_w(weight1);
    offset_vec += const_vec * weight1_vec;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    for (cnt = (width >> 4); cnt--;) {
        src0_ptr_tmp = src0_ptr;
        src1_ptr_tmp = src1_ptr;
        dst_tmp = dst;

        LD_SB7(src0_ptr_tmp, src_stride,
               src0, src1, src2, src3, src4, src5, src6);
        src0_ptr_tmp += (7 * src_stride);

        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
        ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
                   src10_r, src32_r, src54_r, src21_r);
        ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
        ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
                   src10_l, src32_l, src54_l, src21_l);
        ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);

        for (loop_cnt = (height >> 1); loop_cnt--;) {
            LD_SB2(src0_ptr_tmp, src_stride, src7, src8);
            src0_ptr_tmp += (2 * src_stride);
            LD_SH2(src1_ptr_tmp, src2_stride, in0, in1);
            LD_SH2((src1_ptr_tmp + 8), src2_stride, in2, in3);
            src1_ptr_tmp += (2 * src2_stride);

            XORI_B2_128_SB(src7, src8);
            ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
            ILVL_B2_SB(src7, src6, src8, src7, src76_l, src87_l);

            DOTP_SB4_SH(src10_r, src21_r, src10_l, src21_l, filt0, filt0,
                        filt0, filt0, tmp0, tmp1, tmp2, tmp3);
            DPADD_SB4_SH(src32_r, src43_r, src32_l, src43_l, filt1, filt1,
                         filt1, filt1, tmp0, tmp1, tmp2, tmp3);
            DPADD_SB4_SH(src54_r, src65_r, src54_l, src65_l, filt2, filt2,
                         filt2, filt2, tmp0, tmp1, tmp2, tmp3);
            DPADD_SB4_SH(src76_r, src87_r, src76_l, src87_l, filt3, filt3,
                         filt3, filt3, tmp0, tmp1, tmp2, tmp3);

            HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               out0, out1, out2, out3);

            PCKEV_B2_SH(out2, out0, out3, out1, out0, out1);
            ST_SH2(out0, out1, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);

            src10_r = src32_r;
            src32_r = src54_r;
            src54_r = src76_r;
            src21_r = src43_r;
            src43_r = src65_r;
            src65_r = src87_r;
            src10_l = src32_l;
            src32_l = src54_l;
            src54_l = src76_l;
            src21_l = src43_l;
            src43_l = src65_l;
            src65_l = src87_l;
            src6 = src8;
        }

        src0_ptr += 16;
        src1_ptr += 16;
        dst += 16;
    }
}

static void hevc_vt_biwgt_8t_16w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_vt_biwgt_8t_16multx2mult_msa(src0_ptr, src_stride,
                                      src1_ptr, src2_stride,
                                      dst, dst_stride, filter, height,
                                      weight0, weight1, offset0, offset1,
                                      rnd_val, 16);
}

static void hevc_vt_biwgt_8t_24w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_vt_biwgt_8t_16multx2mult_msa(src0_ptr, src_stride,
                                      src1_ptr, src2_stride,
                                      dst, dst_stride, filter, height,
                                      weight0, weight1, offset0, offset1,
                                      rnd_val, 16);
    hevc_vt_biwgt_8t_8w_msa(src0_ptr + 16, src_stride,
                            src1_ptr + 16, src2_stride,
                            dst + 16, dst_stride, filter, height,
                            weight0, weight1, offset0, offset1, rnd_val);
}

static void hevc_vt_biwgt_8t_32w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_vt_biwgt_8t_16multx2mult_msa(src0_ptr, src_stride,
                                      src1_ptr, src2_stride,
                                      dst, dst_stride, filter, height,
                                      weight0, weight1, offset0, offset1,
                                      rnd_val, 32);
}

static void hevc_vt_biwgt_8t_48w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_vt_biwgt_8t_16multx2mult_msa(src0_ptr, src_stride,
                                      src1_ptr, src2_stride,
                                      dst, dst_stride, filter, height,
                                      weight0, weight1, offset0, offset1,
                                      rnd_val, 48);
}

static void hevc_vt_biwgt_8t_64w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_vt_biwgt_8t_16multx2mult_msa(src0_ptr, src_stride,
                                      src1_ptr, src2_stride,
                                      dst, dst_stride, filter, height,
                                      weight0, weight1, offset0, offset1,
                                      rnd_val, 64);
}

static void hevc_hv_biwgt_8t_4w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter_x,
                                    const int8_t *filter_y,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint64_t tp0, tp1;
    int32_t offset, weight;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 in0 = { 0 }, in1 = { 0 };
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, weight_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst87;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10, dst32, dst54, dst76;
    v8i16 dst21, dst43, dst65, dst97, dst108, dst109, dst98;
    v4i32 offset_vec, rnd_vec, const_vec, dst0, dst1, dst2, dst3;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src0_ptr -= ((3 * src_stride) + 3);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;
    weight_vec = (v8i16) __msa_fill_w(weight);

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src3, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src4, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src5, mask0, mask1, mask2, mask3,
               vec8, vec9, vec10, vec11);
    VSHF_B4_SB(src3, src6, mask0, mask1, mask2, mask3,
               vec12, vec13, vec14, vec15);

    dst30 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                              filt3);
    dst41 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                              filt3);
    dst52 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                              filt3);
    dst63 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2,
                              filt3);

    ILVRL_H2_SH(dst41, dst30, dst10, dst43);
    ILVRL_H2_SH(dst52, dst41, dst21, dst54);
    ILVRL_H2_SH(dst63, dst52, dst32, dst65);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src7, src8, src9, src10);
        src0_ptr += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        LD2(src1_ptr, src2_stride, tp0, tp1);
        INSERT_D2_SH(tp0, tp1, in0);
        src1_ptr += (2 * src2_stride);
        LD2(src1_ptr, src2_stride, tp0, tp1);
        INSERT_D2_SH(tp0, tp1, in1);
        src1_ptr += (2 * src2_stride);

        VSHF_B4_SB(src7, src9, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src8, src10, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        dst97 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);
        dst108 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);

        dst76 = __msa_ilvr_h(dst97, dst66);
        ILVRL_H2_SH(dst108, dst97, dst87, dst109);
        dst66 = (v8i16) __msa_splati_d((v2i64) dst97, 1);
        dst98 = __msa_ilvr_h(dst66, dst108);

        dst0 = HEVC_FILT_8TAP(dst10, dst32, dst54, dst76, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        dst1 = HEVC_FILT_8TAP(dst21, dst43, dst65, dst87, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        dst2 = HEVC_FILT_8TAP(dst32, dst54, dst76, dst98, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        dst3 = HEVC_FILT_8TAP(dst43, dst65, dst87, dst109, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        SRA_4V(dst0, dst1, dst2, dst3, 6);
        PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp1, tmp3);
        ILVRL_H2_SH(tmp1, in0, tmp0, tmp1);
        ILVRL_H2_SH(tmp3, in1, tmp2, tmp3);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        CLIP_SW4_0_255_MAX_SATU(dst0, dst1, dst2, dst3);
        PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp0, tmp1);
        out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        dst10 = dst54;
        dst32 = dst76;
        dst54 = dst98;
        dst21 = dst65;
        dst43 = dst87;
        dst65 = dst109;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst108, 1);
    }
}

static void hevc_hv_biwgt_8t_8multx2mult_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter_x,
                                             const int8_t *filter_y,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val,
                                             int32_t width8mult)
{
    uint32_t loop_cnt, cnt;
    int32_t offset, weight;
    uint8_t *src0_ptr_tmp;
    int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, weight_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= ((3 * src_stride) + 3);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;
    weight_vec = (v8i16) __msa_fill_w(weight);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (cnt = width8mult; cnt--;) {
        src0_ptr_tmp = src0_ptr;
        src1_ptr_tmp = src1_ptr;
        dst_tmp = dst;

        LD_SB7(src0_ptr_tmp, src_stride,
               src0, src1, src2, src3, src4, src5, src6);
        src0_ptr_tmp += (7 * src_stride);

        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

        /* row 0 row 1 row 2 row 3 */
        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);

        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);
        dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                 filt2, filt3);

        /* row 4 row 5 row 6 */
        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);

        dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst6 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            LD_SB2(src0_ptr_tmp, src_stride, src7, src8);
            XORI_B2_128_SB(src7, src8);
            src0_ptr_tmp += 2 * src_stride;

            LD_SH2(src1_ptr_tmp, src2_stride, in0, in1);
            src1_ptr_tmp += (2 * src2_stride);

            ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1, dst10_r,
                       dst32_r, dst54_r, dst21_r);
            ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1, dst10_l,
                       dst32_l, dst54_l, dst21_l);
            ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
            ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

            VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst7 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);

            ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
            dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_l = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);

            dst0_r >>= 6;
            dst0_l >>= 6;

            /* row 8 */
            VSHF_B4_SB(src8, src8, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst8 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);

            ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
            dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst1_l = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);

            dst1_r >>= 6;
            dst1_l >>= 6;

            PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, tmp1, tmp3);
            ILVRL_H2_SH(tmp1, in0, tmp0, tmp1);
            ILVRL_H2_SH(tmp3, in1, tmp2, tmp3);
            dst0_r = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
            dst0_l = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
            dst1_r = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
            dst1_l = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
            SRAR_W4_SW(dst0_l, dst0_r, dst1_l, dst1_r, rnd_vec);
            CLIP_SW4_0_255_MAX_SATU(dst0_l, dst0_r, dst1_l, dst1_r);
            PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, tmp0, tmp1);
            out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
            ST_D2(out, 0, 1, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);

            dst0 = dst2;
            dst1 = dst3;
            dst2 = dst4;
            dst3 = dst5;
            dst4 = dst6;
            dst5 = dst7;
            dst6 = dst8;
        }

        src0_ptr += 8;
        src1_ptr += 8;
        dst += 8;
    }
}

static void hevc_hv_biwgt_8t_8w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter_x,
                                    const int8_t *filter_y,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 1);
}

static void hevc_hv_biwgt_8t_12w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint8_t *src0_ptr_tmp, *dst_tmp;
    int16_t *src1_ptr_tmp;
    int32_t offset, weight;
    uint64_t tp0, tp1;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v8i16 in0 = { 0 }, in1 = { 0 };
    v8i16 filter_vec, weight_vec, tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1, filt2, filt3, filt_h0, filt_h1, filt_h2, filt_h3;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, dsth7, dsth8;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l, dst21_l, dst43_l, dst65_l;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst87, dst10, dst32, dst54, dst76;
    v8i16 dst21, dst43, dst65, dst97, dst108, dst109, dst98, dst87_r, dst87_l;
    v4i32 offset_vec, rnd_vec, const_vec, dst0, dst1, dst2, dst3;

    src0_ptr -= ((3 * src_stride) + 3);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;
    weight_vec = (v8i16) __msa_fill_w(weight);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    src0_ptr_tmp = src0_ptr;
    src1_ptr_tmp = src1_ptr;
    dst_tmp = dst;

    LD_SB7(src0_ptr_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr_tmp += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3, vec12, vec13, vec14,
               vec15);
    dsth0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                              filt3);
    dsth1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                              filt3);
    dsth2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                              filt3);
    dsth3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                              filt2, filt3);
    VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    dsth4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                              filt3);
    dsth5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                              filt3);
    dsth6 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                              filt3);

    for (loop_cnt = 8; loop_cnt--;) {
        LD_SB2(src0_ptr_tmp, src_stride, src7, src8);
        src0_ptr_tmp += (2 * src_stride);
        XORI_B2_128_SB(src7, src8);

        LD_SH2(src1_ptr_tmp, src2_stride, in0, in1);
        src1_ptr_tmp += (2 * src2_stride);

        ILVR_H4_SH(dsth1, dsth0, dsth3, dsth2, dsth5, dsth4, dsth2, dsth1,
                   dst10_r, dst32_r, dst54_r, dst21_r);
        ILVL_H4_SH(dsth1, dsth0, dsth3, dsth2, dsth5, dsth4, dsth2, dsth1,
                   dst10_l, dst32_l, dst54_l, dst21_l);
        ILVR_H2_SH(dsth4, dsth3, dsth6, dsth5, dst43_r, dst65_r);
        ILVL_H2_SH(dsth4, dsth3, dsth6, dsth5, dst43_l, dst65_l);

        VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dsth7 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);

        ILVRL_H2_SH(dsth7, dsth6, dst76_r, dst76_l);
        dst0 = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                              filt_h1, filt_h2, filt_h3);
        dst1 = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l, filt_h0,
                              filt_h1, filt_h2, filt_h3);
        dst0 >>= 6;
        dst1 >>= 6;

        VSHF_B4_SB(src8, src8, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dsth8 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);

        ILVRL_H2_SH(dsth8, dsth7, dst87_r, dst87_l);
        dst2 = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r, filt_h0,
                              filt_h1, filt_h2, filt_h3);
        dst3 = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l, filt_h0,
                              filt_h1, filt_h2, filt_h3);
        dst2 >>= 6;
        dst3 >>= 6;

        PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp1, tmp3);
        ILVRL_H2_SH(tmp1, in0, tmp0, tmp1);
        ILVRL_H2_SH(tmp3, in1, tmp2, tmp3);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        SRAR_W4_SW(dst1, dst0, dst3, dst2, rnd_vec);
        CLIP_SW4_0_255_MAX_SATU(dst1, dst0, dst3, dst2);
        PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp0, tmp1);
        out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
        ST_D2(out, 0, 1, dst_tmp, dst_stride);
        dst_tmp += (2 * dst_stride);

        dsth0 = dsth2;
        dsth1 = dsth3;
        dsth2 = dsth4;
        dsth3 = dsth5;
        dsth4 = dsth6;
        dsth5 = dsth7;
        dsth6 = dsth8;
    }

    src0_ptr += 8;
    src1_ptr += 8;
    dst += 8;

    mask4 = LD_SB(ff_hevc_mask_arr + 16);
    mask5 = mask4 + 2;
    mask6 = mask4 + 4;
    mask7 = mask4 + 6;

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src3, mask4, mask5, mask6, mask7, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src4, mask4, mask5, mask6, mask7, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src5, mask4, mask5, mask6, mask7, vec8, vec9, vec10,
               vec11);
    VSHF_B4_SB(src3, src6, mask4, mask5, mask6, mask7, vec12, vec13, vec14,
               vec15);
    dst30 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                              filt3);
    dst41 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                              filt3);
    dst52 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                              filt3);
    dst63 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2,
                              filt3);
    ILVRL_H2_SH(dst41, dst30, dst10, dst43);
    ILVRL_H2_SH(dst52, dst41, dst21, dst54);
    ILVRL_H2_SH(dst63, dst52, dst32, dst65);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src7, src8, src9, src10);
        src0_ptr += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        LD2(src1_ptr, src2_stride, tp0, tp1);
        INSERT_D2_SH(tp0, tp1, in0);
        src1_ptr += (2 * src2_stride);
        LD2(src1_ptr, src2_stride, tp0, tp1);
        INSERT_D2_SH(tp0, tp1, in1);
        src1_ptr += (2 * src2_stride);

        VSHF_B4_SB(src7, src9, mask4, mask5, mask6, mask7, vec0, vec1, vec2,
                   vec3);
        VSHF_B4_SB(src8, src10, mask4, mask5, mask6, mask7, vec4, vec5, vec6,
                   vec7);
        dst97 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);
        dst108 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);

        dst76 = __msa_ilvr_h(dst97, dst66);
        ILVRL_H2_SH(dst108, dst97, dst87, dst109);
        dst66 = (v8i16) __msa_splati_d((v2i64) dst97, 1);
        dst98 = __msa_ilvr_h(dst66, dst108);

        dst0 = HEVC_FILT_8TAP(dst10, dst32, dst54, dst76, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        dst1 = HEVC_FILT_8TAP(dst21, dst43, dst65, dst87, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        dst2 = HEVC_FILT_8TAP(dst32, dst54, dst76, dst98, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        dst3 = HEVC_FILT_8TAP(dst43, dst65, dst87, dst109, filt_h0, filt_h1,
                              filt_h2, filt_h3);
        SRA_4V(dst0, dst1, dst2, dst3, 6);
        PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp1, tmp3);
        ILVRL_H2_SH(tmp1, in0, tmp0, tmp1);
        ILVRL_H2_SH(tmp3, in1, tmp2, tmp3);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        CLIP_SW4_0_255_MAX_SATU(dst0, dst1, dst2, dst3);
        PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp0, tmp1);
        out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        dst10 = dst54;
        dst32 = dst76;
        dst54 = dst98;
        dst21 = dst65;
        dst43 = dst87;
        dst65 = dst109;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst108, 1);
    }
}

static void hevc_hv_biwgt_8t_16w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 2);
}

static void hevc_hv_biwgt_8t_24w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 3);
}

static void hevc_hv_biwgt_8t_32w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 4);
}

static void hevc_hv_biwgt_8t_48w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 6);
}

static void hevc_hv_biwgt_8t_64w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 8);
}

static void hevc_hz_biwgt_4t_4x2_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1;
    v8i16 in0, in1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);
    v16i8 mask1, vec0, vec1;
    v8i16 dst0;
    v4i32 dst0_r, dst0_l;
    v8i16 out0, filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB2(src0_ptr, src_stride, src0, src1);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    in0 = (v8i16) __msa_ilvr_d((v2i64) in1, (v2i64) in0);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);

    ILVRL_H2_SW(dst0, in0, dst0_r, dst0_l);
    dst0_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_r, (v8i16) weight_vec);
    dst0_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_l, (v8i16) weight_vec);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    dst0_r = (v4i32) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
    out0 = CLIP_SH_0_255(dst0_r);
    out0 = (v8i16) __msa_pckev_b((v16i8) out0, (v16i8) out0);
    ST_W2(out0, 0, 1, dst, dst_stride);
}

static void hevc_hz_biwgt_4t_4x4_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);
    v16i8 mask1;
    v8i16 dst0, dst1;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    /* rearranging filter */
    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
    ILVR_D2_SH(in1, in0, in3, in2, in0, in1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec0, vec1);
    dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0, dst1);

    dst0 = (v8i16) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
    ST_W4(dst0, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_hz_biwgt_4t_4x8multiple_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t weight, offset, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src0_ptr, src_stride,
               src0, src1, src2, src3, src4, src5, src6, src7);
        src0_ptr += (8 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        LD_SH4(src1_ptr, src2_stride, in4, in5, in6, in7);
        src1_ptr += (4 * src2_stride);
        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        ILVR_D2_SH(in5, in4, in7, in6, in2, in3);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src4, src5, src4, src5, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src6, src7, src6, src7, mask0, mask1, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_W8(dst0, dst1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_biwgt_4t_4w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    if (2 == height) {
        hevc_hz_biwgt_4t_4x2_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (4 == height) {
        hevc_hz_biwgt_4t_4x4_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (0 == (height % 8)) {
        hevc_hz_biwgt_4t_4x8multiple_msa(src0_ptr, src_stride,
                                         src1_ptr, src2_stride,
                                         dst, dst_stride, filter, height,
                                         weight0, weight1, offset0, offset1,
                                         rnd_val);
    }
}

static void hevc_hz_biwgt_4t_6w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_W2(dst0, 0, 2, dst, dst_stride);
        ST_H2(dst0, 2, 6, dst + 4, dst_stride);
        ST_W2(dst1, 0, 2, dst + 2 * dst_stride, dst_stride);
        ST_H2(dst1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_biwgt_4t_8x2_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1;
    v8i16 in0, in1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, vec0, vec1;
    v8i16 dst0, dst1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    LD_SB2(src0_ptr, src_stride, src0, src1);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    XORI_B2_128_SB(src0, src1);
    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
    dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0, dst1);

    dst0 = (v8i16) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
    ST_D2(dst0, 0, 1, dst, dst_stride);
}

static void hevc_hz_biwgt_4t_8x6_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t weight, offset, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 in0, in1, in2, in3, in4, in5;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    LD_SB6(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5);

    LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
    src1_ptr += (4 * src2_stride);
    LD_SH2(src1_ptr, src2_stride, in4, in5);
    XORI_B6_128_SB(src0, src1, src2, src3, src4, src5);
    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
    dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
    dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                       in0, in1, in2, in3,
                       weight_vec, rnd_vec, offset_vec,
                       dst0, dst1, dst2, dst3);
    HEVC_BIW_RND_CLIP2(dst4, dst5, in4, in5,
                       weight_vec, rnd_vec, offset_vec,
                       dst4, dst5);

    PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
    dst3 = (v8i16) __msa_pckev_b((v16i8) dst5, (v16i8) dst4);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(dst3, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_hz_biwgt_4t_8x4multiple_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_D4(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_biwgt_4t_8w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    if (2 == height) {
        hevc_hz_biwgt_4t_8x2_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (6 == height) {
        hevc_hz_biwgt_4t_8x6_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (0 == (height % 4)) {
        hevc_hz_biwgt_4t_8x4multiple_msa(src0_ptr, src_stride,
                                         src1_ptr, src2_stride,
                                         dst, dst_stride, filter, height,
                                         weight0, weight1, offset0, offset1,
                                         rnd_val);
    }
}

static void hevc_hz_biwgt_4t_12w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask2 = {
        8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
    };
    v16i8 mask1, mask3;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        LD_SH4(src1_ptr + 8, src2_stride, in4, in5, in6, in7);
        src1_ptr += (4 * src2_stride);
        ILVR_D2_SH(in5, in4, in7, in6, in4, in5);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
        dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src3, src2, src3, mask2, mask3, vec0, vec1);
        dst5 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);
        HEVC_BIW_RND_CLIP2(dst4, dst5, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4, dst5);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        dst3 = (v8i16) __msa_pckev_b((v16i8) dst5, (v16i8) dst4);
        ST_D4(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(dst3, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_biwgt_4t_16w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 vec0, vec1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src2, src4, src6);
        LD_SB4(src0_ptr + 8, src_stride, src1, src3, src5, src7);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in2, in4, in6);
        LD_SH4(src1_ptr + 8, src2_stride, in1, in3, in5, in7);
        src1_ptr += (4 * src2_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst6 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
        dst7 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_SH2(dst0, dst1, dst, dst_stride);
        dst += (2 * dst_stride);

        HEVC_BIW_RND_CLIP4(dst4, dst5, dst6, dst7,
                           in4, in5, in6, in7,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_SH2(dst0, dst1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_biwgt_4t_24w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 in0, in1, in2, in3, in4, in5;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = 16; loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src0, src2);
        LD_SB2(src0_ptr + 16, src_stride, src1, src3);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr, src2_stride, in0, in2);
        LD_SH2(src1_ptr + 8, src2_stride, in1, in3);
        LD_SH2(src1_ptr + 16, src2_stride, in4, in5);
        src1_ptr += (2 * src2_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src3, src2, src3, mask2, mask3, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_SH2(dst0, dst1, dst, dst_stride);

        /* 8 width */
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        HEVC_BIW_RND_CLIP2(dst0, dst1, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1);

        dst0 = (v8i16) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
        ST_D2(dst0, 0, 1, (dst + 16), dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_biwgt_4t_32w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, mask2, mask3;
    v8i16 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB2(src0_ptr, 16, src0, src1);
        src2 = LD_SB(src0_ptr + 24);
        src0_ptr += src_stride;
        LD_SH4(src1_ptr, 8, in0, in1, in2, in3);
        src1_ptr += src2_stride;
        XORI_B3_128_SB(src0, src1, src2);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
        dst1 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst2 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0, dst1, dst2, dst3);

        PCKEV_B2_SH(dst1, dst0, dst3, dst2, dst0, dst1);
        ST_SH2(dst0, dst1, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_vt_biwgt_4t_4x2_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t weight, offset, constant;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, dst10;
    v16i8 src10_r, src32_r, src21_r, src43_r, src2110, src4332;
    v4i32 dst10_r, dst10_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, out;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);
    LD_SB2(src0_ptr, src_stride, src3, src4);
    src0_ptr += (2 * src_stride);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    src1_ptr += (2 * src2_stride);

    in0 = (v8i16) __msa_ilvr_d((v2i64) in1, (v2i64) in0);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_r, (v2i64) src32_r);
    src4332 = (v16i8) __msa_xori_b((v16u8) src4332, 128);

    dst10 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);

    ILVRL_H2_SW(dst10, in0, dst10_r, dst10_l);
    dst10_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst10_r, (v8i16) weight_vec);
    dst10_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst10_l, (v8i16) weight_vec);
    SRAR_W2_SW(dst10_r, dst10_l, rnd_vec);
    dst10_r = (v4i32) __msa_pckev_h((v8i16) dst10_l, (v8i16) dst10_r);
    out = CLIP_SH_0_255(dst10_r);
    out = (v8i16) __msa_pckev_b((v16i8) out, (v16i8) out);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void hevc_vt_biwgt_4t_4x4_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t weight, offset, constant;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src2110, src4332, src6554;
    v8i16 dst10, dst32;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    LD_SB4(src0_ptr, src_stride, src3, src4, src5, src6);
    src0_ptr += (4 * src_stride);
    LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
    src1_ptr += (4 * src2_stride);
    ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
               src32_r, src43_r, src54_r, src65_r);
    ILVR_D2_SB(src43_r, src32_r, src65_r, src54_r, src4332, src6554);
    XORI_B2_128_SB(src4332, src6554);

    dst10 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
    dst32 = HEVC_FILT_4TAP_SH(src4332, src6554, filt0, filt1);

    HEVC_BIW_RND_CLIP2(dst10, dst32, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst10, dst32);

    dst10 = (v8i16) __msa_pckev_b((v16i8) dst32, (v16i8) dst10);
    ST_W4(dst10, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
}

static void hevc_vt_biwgt_4t_4x8multiple_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t weight, offset, constant;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src2110, src4332, src6554, src8776;
    v8i16 dst10, dst32, dst54, dst76;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB6(src0_ptr, src_stride, src3, src4, src5, src6, src7, src8);
        src0_ptr += (6 * src_stride);
        LD_SH8(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5, in6, in7);
        src1_ptr += (8 * src2_stride);

        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        ILVR_D2_SH(in5, in4, in7, in6, in2, in3);

        ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
                   src32_r, src43_r, src54_r, src65_r);
        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        ILVR_D3_SB(src43_r, src32_r, src65_r, src54_r, src87_r, src76_r,
                   src4332, src6554, src8776);
        XORI_B3_128_SB(src4332, src6554, src8776);

        dst10 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
        dst32 = HEVC_FILT_4TAP_SH(src4332, src6554, filt0, filt1);
        dst54 = HEVC_FILT_4TAP_SH(src6554, src8776, filt0, filt1);

        LD_SB2(src0_ptr, src_stride, src9, src2);
        src0_ptr += (2 * src_stride);
        ILVR_B2_SB(src9, src8, src2, src9, src98_r, src109_r);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src109_r, (v2i64) src98_r);
        src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

        dst76 = HEVC_FILT_4TAP_SH(src8776, src2110, filt0, filt1);
        HEVC_BIW_RND_CLIP4(dst10, dst32, dst54, dst76,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst10, dst32, dst54, dst76);

        PCKEV_B2_SH(dst32, dst10, dst76, dst54, dst10, dst32);
        ST_W8(dst10, dst32, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_vt_biwgt_4t_4w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    if (2 == height) {
        hevc_vt_biwgt_4t_4x2_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (4 == height) {
        hevc_vt_biwgt_4t_4x4_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (0 == (height % 8)) {
        hevc_vt_biwgt_4t_4x8multiple_msa(src0_ptr, src_stride,
                                         src1_ptr, src2_stride,
                                         dst, dst_stride, filter, height,
                                         weight0, weight1, offset0, offset1,
                                         rnd_val);
    }
}

static void hevc_vt_biwgt_4t_6w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src3, src4);
        src0_ptr += (2 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

        tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);

        LD_SB2(src0_ptr, src_stride, src1, src2);
        src0_ptr += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        tmp2 = HEVC_FILT_4TAP_SH(src32_r, src10_r, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src43_r, src21_r, filt0, filt1);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp2, tmp3);

        PCKEV_B2_SH(tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
        ST_W2(tmp0, 0, 2, dst, dst_stride);
        ST_H2(tmp0, 2, 6, dst + 4, dst_stride);
        ST_W2(tmp1, 0, 2, dst + 2 * dst_stride, dst_stride);
        ST_H2(tmp1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_vt_biwgt_4t_8x2_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, tmp0, tmp1;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    LD_SB2(src0_ptr, src_stride, src3, src4);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    XORI_B2_128_SB(src3, src4);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

    tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
    HEVC_BIW_RND_CLIP2(tmp0, tmp1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       tmp0, tmp1);

    tmp0 = (v8i16) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_D2(tmp0, 0, 1, dst, dst_stride);
}

static void hevc_vt_biwgt_4t_8x6_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1, in2, in3, in4, in5;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    LD_SB6(src0_ptr, src_stride, src3, src4, src5, src6, src7, src8);
    LD_SH6(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5);
    XORI_B6_128_SB(src3, src4, src5, src6, src7, src8);
    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
               src32_r, src43_r, src54_r, src65_r);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);

    tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
    tmp2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
    tmp3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
    tmp4 = HEVC_FILT_4TAP_SH(src54_r, src76_r, filt0, filt1);
    tmp5 = HEVC_FILT_4TAP_SH(src65_r, src87_r, filt0, filt1);
    HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                       in0, in1, in2, in3,
                       weight_vec, rnd_vec, offset_vec,
                       tmp0, tmp1, tmp2, tmp3);
    HEVC_BIW_RND_CLIP2(tmp4, tmp5, in4, in5,
                       weight_vec, rnd_vec, offset_vec,
                       tmp4, tmp5);

    PCKEV_B2_SH(tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    tmp3 = (v8i16) __msa_pckev_b((v16i8) tmp5, (v16i8) tmp4);
    ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(tmp3, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_vt_biwgt_4t_8x4multiple_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src3, src4);
        src0_ptr += (2 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

        tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);

        LD_SB2(src0_ptr, src_stride, src1, src2);
        src0_ptr += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        tmp2 = HEVC_FILT_4TAP_SH(src32_r, src10_r, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src43_r, src21_r, filt0, filt1);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp2, tmp3);

        PCKEV_B2_SH(tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_vt_biwgt_4t_8w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    if (2 == height) {
        hevc_vt_biwgt_4t_8x2_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (6 == height) {
        hevc_vt_biwgt_4t_8x6_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else {
        hevc_vt_biwgt_4t_8x4multiple_msa(src0_ptr, src_stride,
                                         src1_ptr, src2_stride,
                                         dst, dst_stride, filter, height,
                                         weight0, weight1, offset0, offset1,
                                         rnd_val);
    }
}

static void hevc_vt_biwgt_4t_12w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (1 * src_stride);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_l, (v2i64) src10_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src3, src4);
        src0_ptr += (2 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        LD_SH4(src1_ptr + 8, src2_stride, in4, in5, in6, in7);
        src1_ptr += (4 * src2_stride);
        ILVR_D2_SH(in5, in4, in7, in6, in4, in5);
        XORI_B2_128_SB(src3, src4);

        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_l, (v2i64) src32_l);

        tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        tmp4 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);

        LD_SB2(src0_ptr, src_stride, src5, src2);
        src0_ptr += (2 * src_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src54_l, src65_l);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src65_l, (v2i64) src54_l);

        tmp2 = HEVC_FILT_4TAP_SH(src32_r, src10_r, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src43_r, src21_r, filt0, filt1);
        tmp5 = HEVC_FILT_4TAP_SH(src4332, src2110, filt0, filt1);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp2, tmp3);
        HEVC_BIW_RND_CLIP2(tmp4, tmp5, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           tmp4, tmp5);

        PCKEV_B2_SH(tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
        tmp2 = (v8i16) __msa_pckev_b((v16i8) tmp5, (v16i8) tmp4);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(tmp2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_vt_biwgt_4t_16w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src3, src4);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2(src1_ptr + 8, src2_stride, in2, in3);
        src1_ptr += (2 * src2_stride);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        tmp2 = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);

        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_SH(tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
        ST_SH2(tmp0, tmp1, dst, dst_stride);
        dst += (2 * dst_stride);
        LD_SB2(src0_ptr, src_stride, src5, src2);
        src0_ptr += (2 * src_stride);

        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2(src1_ptr + 8, src2_stride, in2, in3);
        src1_ptr += (2 * src2_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);

        tmp0 = HEVC_FILT_4TAP_SH(src32_r, src10_r, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src43_r, src21_r, filt0, filt1);
        tmp2 = HEVC_FILT_4TAP_SH(src32_l, src10_l, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src43_l, src21_l, filt0, filt1);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp2, tmp3);

        PCKEV_B2_SH(tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
        ST_SH2(tmp0, tmp1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_vt_biwgt_4t_24w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10, src11;
    v8i16 in0, in1, in2, in3, in4, in5;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    /* 16width */
    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    /* 8width */
    LD_SB3(src0_ptr + 16, src_stride, src6, src7, src8);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src6, src7, src8);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        /* 16width */
        LD_SB2(src0_ptr, src_stride, src3, src4);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2(src1_ptr + 8, src2_stride, in2, in3);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        /* 8width */
        LD_SB2(src0_ptr + 16, src_stride, src9, src10);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr + 16, src2_stride, in4, in5);
        src1_ptr += (2 * src2_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        /* 16width */
        tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        tmp4 = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        tmp5 = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);
        /* 8width */
        tmp2 = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        /* 16width */
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp4, tmp5);
        /* 8width */
        HEVC_BIW_RND_CLIP2(tmp2, tmp3, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           tmp2, tmp3);
        /* 16width */
        PCKEV_B2_SH(tmp4, tmp0, tmp5, tmp1, tmp0, tmp1);
        /* 8width */
        tmp2 = (v8i16) __msa_pckev_b((v16i8) tmp3, (v16i8) tmp2);
        ST_SH2(tmp0, tmp1, dst, dst_stride);
        ST_D2(tmp2, 0, 1, dst + 16, dst_stride);
        dst += (2 * dst_stride);

        /* 16width */
        LD_SB2(src0_ptr, src_stride, src5, src2);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2(src1_ptr + 8, src2_stride, in2, in3);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);
        /* 8width */
        LD_SB2(src0_ptr + 16, src_stride, src11, src8);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr + 16, src2_stride, in4, in5);
        src1_ptr += (2 * src2_stride);
        XORI_B2_128_SB(src11, src8);
        ILVR_B2_SB(src11, src10, src8, src11, src76_r, src87_r);
        /* 16width */
        tmp0 = HEVC_FILT_4TAP_SH(src32_r, src10_r, filt0, filt1);
        tmp4 = HEVC_FILT_4TAP_SH(src32_l, src10_l, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src43_r, src21_r, filt0, filt1);
        tmp5 = HEVC_FILT_4TAP_SH(src43_l, src21_l, filt0, filt1);
        /* 8width */
        tmp2 = HEVC_FILT_4TAP_SH(src98_r, src76_r, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src109_r, src87_r, filt0, filt1);
        /* 16width */
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp4, tmp5);
        /* 8width */
        HEVC_BIW_RND_CLIP2(tmp2, tmp3, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           tmp2, tmp3);
        /* 16width */
        PCKEV_B2_SH(tmp4, tmp0, tmp5, tmp1, tmp0, tmp1);

        /* 8width */
        tmp2 = (v8i16) __msa_pckev_b((v16i8) tmp3, (v16i8) tmp2);
        ST_SH2(tmp0, tmp1, dst, dst_stride);
        ST_D2(tmp2, 0, 1, dst + 16, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_vt_biwgt_4t_32w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint8_t *dst_tmp = dst + 16;
    int32_t offset, weight, constant;
    v16i8 src0, src1, src2, src3, src4, src6, src7, src8, src9, src10;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v16i8 src10_l, src32_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src87_l, src109_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);
    constant = 128 * weight1;
    constant <<= 6;
    offset += constant;

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    /* 16width */
    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    /* next 16width */
    LD_SB3(src0_ptr + 16, src_stride, src6, src7, src8);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src6, src7, src8);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
    ILVL_B2_SB(src7, src6, src8, src7, src76_l, src87_l);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        /* 16width */
        LD_SB2(src0_ptr, src_stride, src3, src4);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2(src1_ptr + 8, src2_stride, in2, in3);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        /* 16width */
        tmp0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        tmp4 = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        tmp5 = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);
        /* 16width */
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           tmp0, tmp1, tmp4, tmp5);
        /* 16width */
        PCKEV_B2_SH(tmp4, tmp0, tmp5, tmp1, tmp0, tmp1);
        ST_SH2(tmp0, tmp1, dst, dst_stride);
        dst += (2 * dst_stride);

        src10_r = src32_r;
        src21_r = src43_r;
        src10_l = src32_l;
        src21_l = src43_l;
        src2 = src4;

        /* next 16width */
        LD_SB2(src0_ptr + 16, src_stride, src9, src10);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr + 16, src2_stride, in4, in5);
        LD_SH2(src1_ptr + 24, src2_stride, in6, in7);
        src1_ptr += (2 * src2_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        ILVL_B2_SB(src9, src8, src10, src9, src98_l, src109_l);
        /* next 16width */
        tmp2 = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        tmp6 = HEVC_FILT_4TAP_SH(src76_l, src98_l, filt0, filt1);
        tmp3 = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        tmp7 = HEVC_FILT_4TAP_SH(src87_l, src109_l, filt0, filt1);
        /* next 16width */
        HEVC_BIW_RND_CLIP4(tmp2, tmp3, tmp6, tmp7,
                           in4, in5, in6, in7,
                           weight_vec, rnd_vec, offset_vec,
                           tmp2, tmp3, tmp6, tmp7);

        /* next 16width */
        PCKEV_B2_SH(tmp6, tmp2, tmp7, tmp3, tmp2, tmp3);
        ST_SH2(tmp2, tmp3, dst_tmp, dst_stride);
        dst_tmp += (2 * dst_stride);

        src76_r = src98_r;
        src87_r = src109_r;
        src76_l = src98_l;
        src87_l = src109_l;
        src8 = src10;
    }
}

static void hevc_hv_biwgt_4t_4x2_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint64_t tp0, tp1;
    int32_t offset, weight;
    v8i16 in0 = { 0 };
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, tmp, weight_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst20, dst31, dst42, dst10, dst32, dst21, dst43, tmp0, tmp1;
    v4i32 dst0, dst1, offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    LD_SB5(src0_ptr, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    VSHF_B2_SB(src0, src2, src0, src2, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src3, src1, src3, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src4, src2, src4, mask0, mask1, vec4, vec5);

    dst20 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst31 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst42 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

    ILVRL_H2_SH(dst31, dst20, dst10, dst32);
    ILVRL_H2_SH(dst42, dst31, dst21, dst43);

    dst0 = HEVC_FILT_4TAP(dst10, dst32, filt_h0, filt_h1);
    dst1 = HEVC_FILT_4TAP(dst21, dst43, filt_h0, filt_h1);
    dst0 >>= 6;
    dst1 >>= 6;
    dst0 = (v4i32) __msa_pckev_h((v8i16) dst1, (v8i16) dst0);

    LD2(src1_ptr, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in0);

    ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
    dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    SRAR_W2_SW(dst0, dst1, rnd_vec);
    tmp = __msa_pckev_h((v8i16) dst1, (v8i16) dst0);
    tmp = CLIP_SH_0_255_MAX_SATU(tmp);
    out = (v16u8) __msa_pckev_b((v16i8) tmp, (v16i8) tmp);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void hevc_hv_biwgt_4t_4x4_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint64_t tp0, tp1;
    int32_t offset, weight;
    v16u8 out;
    v8i16 in0 = { 0 }, in1 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, weight_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst30, dst41, dst52, dst63;
    v8i16 dst10, dst32, dst54, dst21, dst43, dst65;
    v4i32 offset_vec, rnd_vec, const_vec;
    v4i32 dst0, dst1, dst2, dst3;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B2_SB(src0, src3, src0, src3, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src4, src1, src4, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src5, src2, src5, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src6, src3, src6, mask0, mask1, vec6, vec7);

    dst30 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst41 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst52 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst63 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    ILVRL_H2_SH(dst41, dst30, dst10, dst43);
    ILVRL_H2_SH(dst52, dst41, dst21, dst54);
    ILVRL_H2_SH(dst63, dst52, dst32, dst65);
    dst0 = HEVC_FILT_4TAP(dst10, dst32, filt_h0, filt_h1);
    dst1 = HEVC_FILT_4TAP(dst21, dst43, filt_h0, filt_h1);
    dst2 = HEVC_FILT_4TAP(dst32, dst54, filt_h0, filt_h1);
    dst3 = HEVC_FILT_4TAP(dst43, dst65, filt_h0, filt_h1);
    SRA_4V(dst0, dst1, dst2, dst3, 6);
    PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp1, tmp3);

    LD2(src1_ptr, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in0);
    src1_ptr += (2 * src2_stride);
    LD2(src1_ptr, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in1);

    ILVRL_H2_SH(tmp1, in0, tmp0, tmp1);
    ILVRL_H2_SH(tmp3, in1, tmp2, tmp3);

    dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
    dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
    SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
    PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp0, tmp1);
    CLIP_SH2_0_255_MAX_SATU(tmp0, tmp1);
    out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_hv_biwgt_4t_4multx8mult_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter_x,
                                             const int8_t *filter_y,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint64_t tp0, tp1;
    int32_t offset, weight;
    v16u8 out0, out1;
    v8i16 in0 = { 0 }, in1 = { 0 }, in2 = { 0 }, in3 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, weight_vec;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst98_r, dst109_r;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask0, mask1, vec2, vec3);
    dst10 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst21 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    ILVRL_H2_SH(dst21, dst10, dst10_r, dst21_r);
    dst22 = (v8i16) __msa_splati_d((v2i64) dst21, 1);

    for (loop_cnt = height >> 3; loop_cnt--;) {
        LD_SB8(src0_ptr, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src0_ptr += (8 * src_stride);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
        VSHF_B2_SB(src3, src7, src3, src7, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src8, src4, src8, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src9, src5, src9, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src10, src6, src10, mask0, mask1, vec6, vec7);

        dst73 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst84 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst95 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst106 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        dst32_r = __msa_ilvr_h(dst73, dst22);
        ILVRL_H2_SH(dst84, dst73, dst43_r, dst87_r);
        ILVRL_H2_SH(dst95, dst84, dst54_r, dst98_r);
        ILVRL_H2_SH(dst106, dst95, dst65_r, dst109_r);
        dst22 = (v8i16) __msa_splati_d((v2i64) dst73, 1);
        dst76_r = __msa_ilvr_h(dst22, dst106);

        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in0);
        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in1);

        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in2);
        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in3);

        dst0 = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst1 = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst2 = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst3 = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst4 = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        dst5 = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        dst6 = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
        dst7 = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);
        SRA_4V(dst0, dst1, dst2, dst3, 6);
        SRA_4V(dst4, dst5, dst6, dst7, 6);
        PCKEV_H4_SW(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, dst0, dst1,
                    dst2, dst3);
        ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
        ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
        ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
        ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
        dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
        dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
        dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, tmp0, tmp1,
                    tmp2, tmp3);
        CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_biwgt_4t_4w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter_x,
                                    const int8_t *filter_y,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    if (2 == height) {
        hevc_hv_biwgt_4t_4x2_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter_x, filter_y,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (4 == height) {
        hevc_hv_biwgt_4t_4x4_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter_x, filter_y,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (0 == (height % 8)) {
        hevc_hv_biwgt_4t_4multx8mult_msa(src0_ptr, src_stride,
                                         src1_ptr, src2_stride,
                                         dst, dst_stride, filter_x, filter_y,
                                         height, weight0, weight1,
                                         offset0, offset1, rnd_val);
    }
}

static void hevc_hv_biwgt_4t_6w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter_x,
                                    const int8_t *filter_y,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    uint32_t tpw0, tpw1, tpw2, tpw3;
    uint64_t tp0, tp1;
    int32_t offset, weight;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 in0 = { 0 }, in1 = { 0 }, in2 = { 0 }, in3 = { 0 };
    v8i16 in4 = { 0 }, in5 = { 0 };
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, dsth7, dsth8, dsth9;
    v8i16 dsth10, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, weight_vec;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r, dst21_r, dst43_r;
    v8i16 dst65_r, dst87_r, dst109_r, dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst98_l, dst21_l, dst43_l, dst65_l, dst87_l, dst109_l;
    v8i16 dst1021_l, dst3243_l, dst5465_l, dst7687_l, dst98109_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v4i32 dst4_r, dst5_r, dst6_r, dst7_r;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

    LD_SB8(src0_ptr, src_stride, src3, src4, src5, src6, src7, src8, src9,
           src10);
    XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);

    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

    dsth3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dsth6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src9, src9, src9, src9, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src10, src10, src10, src10, mask0, mask1, vec6, vec7);

    dsth7 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth8 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth9 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dsth10 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
    ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
    ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
    ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);
    ILVRL_H2_SH(dsth7, dsth6, dst76_r, dst76_l);
    ILVRL_H2_SH(dsth8, dsth7, dst87_r, dst87_l);
    ILVRL_H2_SH(dsth9, dsth8, dst98_r, dst98_l);
    ILVRL_H2_SH(dsth10, dsth9, dst109_r, dst109_l);
    PCKEV_D2_SH(dst21_l, dst10_l, dst43_l, dst32_l, dst1021_l, dst3243_l);
    PCKEV_D2_SH(dst65_l, dst54_l, dst87_l, dst76_l, dst5465_l, dst7687_l);
    dst98109_l = (v8i16) __msa_pckev_d((v2i64) dst109_l, (v2i64) dst98_l);

    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst6_r = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
    dst7_r = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst1021_l, dst3243_l, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst3243_l, dst5465_l, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst5465_l, dst7687_l, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst7687_l, dst98109_l, filt_h0, filt_h1);
    SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
    SRA_4V(dst4_r, dst5_r, dst6_r, dst7_r, 6);
    SRA_4V(dst0_l, dst1_l, dst2_l, dst3_l, 6);
    PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0, dst1);
    PCKEV_H2_SW(dst5_r, dst4_r, dst7_r, dst6_r, dst2, dst3);

    LD2(src1_ptr, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in0);
    LD2(src1_ptr + 2 * src2_stride, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in1);

    LD2(src1_ptr + 4 * src2_stride, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in2);
    LD2(src1_ptr + 6 * src2_stride, src2_stride, tp0, tp1);
    INSERT_D2_SH(tp0, tp1, in3);

    ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
    ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
    ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
    ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
    dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
    dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
    dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
    dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
    dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
    dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
    SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
    SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
    PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, tmp0, tmp1,
                tmp2, tmp3);
    CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
    ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);

    PCKEV_H2_SW(dst1_l, dst0_l, dst3_l, dst2_l, dst4, dst5);

    LW4(src1_ptr + 4, src2_stride, tpw0, tpw1, tpw2, tpw3);
    src1_ptr += (4 * src2_stride);
    INSERT_W4_SH(tpw0, tpw1, tpw2, tpw3, in4);
    LW4(src1_ptr + 4, src2_stride, tpw0, tpw1, tpw2, tpw3);
    INSERT_W4_SH(tpw0, tpw1, tpw2, tpw3, in5);

    ILVRL_H2_SH(dst4, in4, tmp0, tmp1);
    ILVRL_H2_SH(dst5, in5, tmp2, tmp3);

    dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
    dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
    SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
    PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp4, tmp5);

    CLIP_SH2_0_255_MAX_SATU(tmp4, tmp5);
    out2 = (v16u8) __msa_pckev_b((v16i8) tmp5, (v16i8) tmp4);
    ST_H8(out2, 0, 1, 2, 3, 4, 5, 6, 7, dst + 4, dst_stride);
}

static void hevc_hv_biwgt_4t_8x2_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t weight, offset;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec, weight_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v8i16 in0, in1;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    LD_SB5(src0_ptr, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    LD_SH2(src1_ptr, src2_stride, in0, in1);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);

    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);
    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
    PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, tmp1, tmp3);

    ILVRL_H2_SH(tmp1, in0, tmp0, tmp1);
    ILVRL_H2_SH(tmp3, in1, tmp2, tmp3);

    dst0_r = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst0_l = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    dst1_r = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
    dst1_l = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
    SRAR_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, rnd_vec);
    PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, tmp0, tmp1);
    CLIP_SH2_0_255_MAX_SATU(tmp0, tmp1);
    out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_D2(out, 0, 1, dst, dst_stride);
}

static void hevc_hv_biwgt_4t_8multx4_msa(uint8_t *src0_ptr,
                                         int32_t src_stride,
                                         int16_t *src1_ptr,
                                         int32_t src2_stride,
                                         uint8_t *dst,
                                         int32_t dst_stride,
                                         const int8_t *filter_x,
                                         const int8_t *filter_y,
                                         int32_t weight0,
                                         int32_t weight1,
                                         int32_t offset0,
                                         int32_t offset1,
                                         int32_t rnd_val,
                                         int32_t width8mult)
{
    int32_t weight, offset;
    uint32_t cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, mask0, mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec, weight_vec;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, in0, in1, in2, in3;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;
    weight_vec = (v8i16) __msa_fill_w(weight);

    for (cnt = width8mult; cnt--;) {
        LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src0_ptr += 8;
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += 8;

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

        dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

        ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
        ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

        dsth3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dsth4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dsth5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dsth6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
        ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
        ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
        ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);

        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);

        SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
        SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);
        PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, dst0, dst1, dst2, dst3);

        ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
        ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
        ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
        ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
        dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
        dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
        dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    tmp0, tmp1, tmp2, tmp3);
        CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += 8;
    }
}

static void hevc_hv_biwgt_4t_8x6_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t offset, weight;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec, weight_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v16i8 vec10, vec11, vec12, vec13, vec14, vec15, vec16, vec17;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, dsth7, dsth8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst4_r, dst4_l, dst5_r, dst5_l;
    v8i16 dst10_r, dst32_r, dst10_l, dst32_l;
    v8i16 dst21_r, dst43_r, dst21_l, dst43_l;
    v8i16 dst54_r, dst54_l, dst65_r, dst65_l;
    v8i16 dst76_r, dst76_l, dst87_r, dst87_l;
    v8i16 in0, in1, in2, in3, in4, in5;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    LD_SB5(src0_ptr, src_stride, src0, src1, src2, src3, src4);
    src0_ptr += (5 * src_stride);
    LD_SB4(src0_ptr, src_stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);

    LD_SH6(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec10, vec11);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec12, vec13);
    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec14, vec15);
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec16, vec17);

    dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dsth3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    dsth4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);
    dsth5 = HEVC_FILT_4TAP_SH(vec10, vec11, filt0, filt1);
    dsth6 = HEVC_FILT_4TAP_SH(vec12, vec13, filt0, filt1);
    dsth7 = HEVC_FILT_4TAP_SH(vec14, vec15, filt0, filt1);
    dsth8 = HEVC_FILT_4TAP_SH(vec16, vec17, filt0, filt1);

    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);
    ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
    ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
    ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
    ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);
    ILVRL_H2_SH(dsth7, dsth6, dst76_r, dst76_l);
    ILVRL_H2_SH(dsth8, dsth7, dst87_r, dst87_l);

    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst4_l = HEVC_FILT_4TAP(dst54_l, dst76_l, filt_h0, filt_h1);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst5_l = HEVC_FILT_4TAP(dst65_l, dst87_l, filt_h0, filt_h1);

    SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
    SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);
    SRA_4V(dst4_r, dst4_l, dst5_r, dst5_l, 6);
    PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l, dst3_r,
                dst0, dst1, dst2, dst3);

    ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
    ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
    ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
    ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
    dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
    dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
    dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
    dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
    dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
    dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
    SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
    SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
    PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                tmp0, tmp1, tmp2, tmp3);
    CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);

    PCKEV_H2_SW(dst4_l, dst4_r, dst5_l, dst5_r, dst0, dst1);
    ILVRL_H2_SH(dst0, in4, tmp0, tmp1);
    ILVRL_H2_SH(dst1, in5, tmp2, tmp3);
    dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
    dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
    dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
    dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
    SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
    PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp4, tmp5);
    CLIP_SH2_0_255_MAX_SATU(tmp4, tmp5);
    out2 = (v16u8) __msa_pckev_b((v16i8) tmp5, (v16i8) tmp4);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_hv_biwgt_4t_8multx4mult_msa(uint8_t *src0_ptr,
                                             int32_t src_stride,
                                             int16_t *src1_ptr,
                                             int32_t src2_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             const int8_t *filter_x,
                                             const int8_t *filter_y,
                                             int32_t height,
                                             int32_t weight0,
                                             int32_t weight1,
                                             int32_t offset0,
                                             int32_t offset1,
                                             int32_t rnd_val,
                                             int32_t width)
{
    uint32_t loop_cnt;
    uint32_t cnt;
    int32_t offset, weight;
    uint8_t *src0_ptr_tmp;
    int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l, weight_vec;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = (v8i16) __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;

    for (cnt = width >> 3; cnt--;) {
        src0_ptr_tmp = src0_ptr;
        src1_ptr_tmp = src1_ptr;
        dst_tmp = dst;

        LD_SB3(src0_ptr_tmp, src_stride, src0, src1, src2);
        src0_ptr_tmp += (3 * src_stride);
        XORI_B3_128_SB(src0, src1, src2);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

        ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
        ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

        for (loop_cnt = height >> 2; loop_cnt--;) {
            LD_SB4(src0_ptr_tmp, src_stride, src3, src4, src5, src6);
            src0_ptr_tmp += (4 * src_stride);
            LD_SH4(src1_ptr_tmp, src2_stride, in0, in1, in2, in3);
            src1_ptr_tmp += (4 * src2_stride);
            XORI_B4_128_SB(src3, src4, src5, src6);

            VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
            VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
            VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
            VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

            dsth3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
            dsth4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
            dsth5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
            dsth6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

            ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
            ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
            ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
            ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);

            dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
            dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
            dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
            dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
            dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
            dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
            dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
            dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);

            SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
            SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);
            PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                        dst3_r, dst0, dst1, dst2, dst3);
            ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
            ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
            ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
            ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
            dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
            dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
            dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
            dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
            dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
            dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
            dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
            dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
            SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
            SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
            PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                        tmp0, tmp1, tmp2, tmp3);
            CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
            PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
            ST_D4(out0, out1, 0, 1, 0, 1, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);

            dst10_r = dst54_r;
            dst10_l = dst54_l;
            dst21_r = dst65_r;
            dst21_l = dst65_l;
            dsth2 = dsth6;
        }

        src0_ptr += 8;
        dst += 8;
        src1_ptr += 8;
    }
}

static void hevc_hv_biwgt_4t_8w_msa(uint8_t *src0_ptr,
                                    int32_t src_stride,
                                    int16_t *src1_ptr,
                                    int32_t src2_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    const int8_t *filter_x,
                                    const int8_t *filter_y,
                                    int32_t height,
                                    int32_t weight0,
                                    int32_t weight1,
                                    int32_t offset0,
                                    int32_t offset1,
                                    int32_t rnd_val)
{
    if (2 == height) {
        hevc_hv_biwgt_4t_8x2_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter_x, filter_y,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (4 == height) {
        hevc_hv_biwgt_4t_8multx4_msa(src0_ptr, src_stride, src1_ptr,
                                     src2_stride, dst, dst_stride, filter_x,
                                     filter_y, weight0, weight1, offset0,
                                     offset1, rnd_val, 1);
    } else if (6 == height) {
        hevc_hv_biwgt_4t_8x6_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter_x, filter_y,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (0 == (height % 4)) {
        hevc_hv_biwgt_4t_8multx4mult_msa(src0_ptr, src_stride,
                                         src1_ptr, src2_stride,
                                         dst, dst_stride, filter_x, filter_y,
                                         height, weight0,
                                         weight1, offset0, offset1, rnd_val, 8);
    }
}

static void hevc_hv_biwgt_4t_12w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint64_t tp0, tp1;
    int32_t offset, weight;
    uint8_t *src0_ptr_tmp, *dst_tmp;
    int16_t *src1_ptr_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0, mask1, mask2, mask3;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, weight_vec;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v8i16 dst76_r, dst98_r, dst87_r, dst109_r;
    v8i16 in0 = { 0 }, in1 = { 0 }, in2 = { 0 }, in3 = { 0 };
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v4i32 offset_vec, rnd_vec, const_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_fill_w((128 * weight1));
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);
    offset_vec += const_vec;
    weight_vec = (v8i16) __msa_fill_w(weight);

    src0_ptr_tmp = src0_ptr;
    dst_tmp = dst;
    src1_ptr_tmp = src1_ptr;

    LD_SB3(src0_ptr_tmp, src_stride, src0, src1, src2);
    src0_ptr_tmp += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src0_ptr_tmp, src_stride, src3, src4, src5, src6);
        src0_ptr_tmp += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);

        LD_SH4(src1_ptr_tmp, src2_stride, in0, in1, in2, in3);
        src1_ptr_tmp += (4 * src2_stride);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

        dsth3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dsth4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dsth5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dsth6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
        ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
        ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
        ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);

        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);

        SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
        SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);
        PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, dst0, dst1, dst2, dst3);
        ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
        ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
        ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
        ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
        dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
        dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
        dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    tmp0, tmp1, tmp2, tmp3);
        CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst_tmp, dst_stride);
        dst_tmp += (4 * dst_stride);

        dst10_r = dst54_r;
        dst10_l = dst54_l;
        dst21_r = dst65_r;
        dst21_l = dst65_l;
        dsth2 = dsth6;
    }

    src0_ptr += 8;
    dst += 8;
    src1_ptr += 8;

    mask2 = LD_SB(ff_hevc_mask_arr + 16);
    mask3 = mask2 + 2;

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask2, mask3, vec2, vec3);

    dst10 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst21 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);

    ILVRL_H2_SH(dst21, dst10, dst10_r, dst21_r);
    dst22 = (v8i16) __msa_splati_d((v2i64) dst21, 1);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB8(src0_ptr, src_stride, src3, src4, src5, src6, src7, src8, src9,
               src10);
        src0_ptr += (8 * src_stride);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
        VSHF_B2_SB(src3, src7, src3, src7, mask2, mask3, vec0, vec1);
        VSHF_B2_SB(src4, src8, src4, src8, mask2, mask3, vec2, vec3);
        VSHF_B2_SB(src5, src9, src5, src9, mask2, mask3, vec4, vec5);
        VSHF_B2_SB(src6, src10, src6, src10, mask2, mask3, vec6, vec7);

        dst73 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst84 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst95 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst106 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        dst32_r = __msa_ilvr_h(dst73, dst22);
        ILVRL_H2_SH(dst84, dst73, dst43_r, dst87_r);
        ILVRL_H2_SH(dst95, dst84, dst54_r, dst98_r);
        ILVRL_H2_SH(dst106, dst95, dst65_r, dst109_r);
        dst22 = (v8i16) __msa_splati_d((v2i64) dst73, 1);
        dst76_r = __msa_ilvr_h(dst22, dst106);

        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in0);
        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in1);

        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in2);
        LD2(src1_ptr, src2_stride, tp0, tp1);
        src1_ptr += 2 * src2_stride;
        INSERT_D2_SH(tp0, tp1, in3);

        dst0 = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst1 = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst2 = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst3 = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst4 = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        dst5 = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        dst6 = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
        dst7 = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);

        SRA_4V(dst0, dst1, dst2, dst3, 6);
        SRA_4V(dst4, dst5, dst6, dst7, 6);
        PCKEV_H4_SW(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    dst0, dst1, dst2, dst3);
        ILVRL_H2_SH(dst0, in0, tmp0, tmp1);
        ILVRL_H2_SH(dst1, in1, tmp2, tmp3);
        ILVRL_H2_SH(dst2, in2, tmp4, tmp5);
        ILVRL_H2_SH(dst3, in3, tmp6, tmp7);
        dst0 = __msa_dpadd_s_w(offset_vec, tmp0, weight_vec);
        dst1 = __msa_dpadd_s_w(offset_vec, tmp1, weight_vec);
        dst2 = __msa_dpadd_s_w(offset_vec, tmp2, weight_vec);
        dst3 = __msa_dpadd_s_w(offset_vec, tmp3, weight_vec);
        dst4 = __msa_dpadd_s_w(offset_vec, tmp4, weight_vec);
        dst5 = __msa_dpadd_s_w(offset_vec, tmp5, weight_vec);
        dst6 = __msa_dpadd_s_w(offset_vec, tmp6, weight_vec);
        dst7 = __msa_dpadd_s_w(offset_vec, tmp7, weight_vec);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    tmp0, tmp1, tmp2, tmp3);
        CLIP_SH4_0_255_MAX_SATU(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_biwgt_4t_16w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    if (4 == height) {
        hevc_hv_biwgt_4t_8multx4_msa(src0_ptr, src_stride, src1_ptr,
                                     src2_stride, dst, dst_stride, filter_x,
                                     filter_y, weight0, weight1, offset0,
                                     offset1, rnd_val, 2);
    } else {
        hevc_hv_biwgt_4t_8multx4mult_msa(src0_ptr, src_stride, src1_ptr,
                                         src2_stride, dst, dst_stride,
                                         filter_x, filter_y, height, weight0,
                                         weight1, offset0, offset1, rnd_val, 16);
    }
}

static void hevc_hv_biwgt_4t_24w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_4t_8multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride,
                                     filter_x, filter_y, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 24);
}

static void hevc_hv_biwgt_4t_32w_msa(uint8_t *src0_ptr,
                                     int32_t src_stride,
                                     int16_t *src1_ptr,
                                     int32_t src2_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    hevc_hv_biwgt_4t_8multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride,
                                     filter_x, filter_y, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 32);
}

#define BI_W_MC_COPY(WIDTH)                                                  \
void ff_hevc_put_hevc_bi_w_pel_pixels##WIDTH##_8_msa(uint8_t *dst,           \
                                                     ptrdiff_t dst_stride,   \
                                                     uint8_t *src,           \
                                                     ptrdiff_t src_stride,   \
                                                     int16_t *src_16bit,     \
                                                     int height,             \
                                                     int denom,              \
                                                     int weight0,            \
                                                     int weight1,            \
                                                     int offset0,            \
                                                     int offset1,            \
                                                     intptr_t mx,            \
                                                     intptr_t my,            \
                                                     int width)              \
{                                                                            \
    int shift = 14 + 1 - 8;                                                  \
    int log2Wd = denom + shift - 1;                                          \
                                                                             \
    hevc_biwgt_copy_##WIDTH##w_msa(src, src_stride, src_16bit, MAX_PB_SIZE,  \
                                   dst, dst_stride, height,                  \
                                   weight0, weight1, offset0,                \
                                   offset1, log2Wd);                         \
}

BI_W_MC_COPY(4);
BI_W_MC_COPY(6);
BI_W_MC_COPY(8);
BI_W_MC_COPY(12);
BI_W_MC_COPY(16);
BI_W_MC_COPY(24);
BI_W_MC_COPY(32);
BI_W_MC_COPY(48);
BI_W_MC_COPY(64);

#undef BI_W_MC_COPY

#define BI_W_MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                         \
void ff_hevc_put_hevc_bi_w_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,         \
                                                        ptrdiff_t             \
                                                        dst_stride,           \
                                                        uint8_t *src,         \
                                                        ptrdiff_t             \
                                                        src_stride,           \
                                                        int16_t *src_16bit,   \
                                                        int height,           \
                                                        int denom,            \
                                                        int weight0,          \
                                                        int weight1,          \
                                                        int offset0,          \
                                                        int offset1,          \
                                                        intptr_t mx,          \
                                                        intptr_t my,          \
                                                        int width)            \
{                                                                             \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];             \
    int log2Wd = denom + 14 - 8;                                              \
                                                                              \
    hevc_##DIR1##_biwgt_##TAP##t_##WIDTH##w_msa(src, src_stride, src_16bit,   \
                                                MAX_PB_SIZE, dst, dst_stride, \
                                                filter, height, weight0,      \
                                                weight1, offset0, offset1,    \
                                                log2Wd);                      \
}

BI_W_MC(qpel, h, 4, 8, hz, mx);
BI_W_MC(qpel, h, 8, 8, hz, mx);
BI_W_MC(qpel, h, 12, 8, hz, mx);
BI_W_MC(qpel, h, 16, 8, hz, mx);
BI_W_MC(qpel, h, 24, 8, hz, mx);
BI_W_MC(qpel, h, 32, 8, hz, mx);
BI_W_MC(qpel, h, 48, 8, hz, mx);
BI_W_MC(qpel, h, 64, 8, hz, mx);

BI_W_MC(qpel, v, 4, 8, vt, my);
BI_W_MC(qpel, v, 8, 8, vt, my);
BI_W_MC(qpel, v, 12, 8, vt, my);
BI_W_MC(qpel, v, 16, 8, vt, my);
BI_W_MC(qpel, v, 24, 8, vt, my);
BI_W_MC(qpel, v, 32, 8, vt, my);
BI_W_MC(qpel, v, 48, 8, vt, my);
BI_W_MC(qpel, v, 64, 8, vt, my);

BI_W_MC(epel, h, 4, 4, hz, mx);
BI_W_MC(epel, h, 8, 4, hz, mx);
BI_W_MC(epel, h, 6, 4, hz, mx);
BI_W_MC(epel, h, 12, 4, hz, mx);
BI_W_MC(epel, h, 16, 4, hz, mx);
BI_W_MC(epel, h, 24, 4, hz, mx);
BI_W_MC(epel, h, 32, 4, hz, mx);

BI_W_MC(epel, v, 4, 4, vt, my);
BI_W_MC(epel, v, 8, 4, vt, my);
BI_W_MC(epel, v, 6, 4, vt, my);
BI_W_MC(epel, v, 12, 4, vt, my);
BI_W_MC(epel, v, 16, 4, vt, my);
BI_W_MC(epel, v, 24, 4, vt, my);
BI_W_MC(epel, v, 32, 4, vt, my);

#undef BI_W_MC

#define BI_W_MC_HV(PEL, WIDTH, TAP)                                         \
void ff_hevc_put_hevc_bi_w_##PEL##_hv##WIDTH##_8_msa(uint8_t *dst,          \
                                                     ptrdiff_t dst_stride,  \
                                                     uint8_t *src,          \
                                                     ptrdiff_t src_stride,  \
                                                     int16_t *src_16bit,    \
                                                     int height,            \
                                                     int denom,             \
                                                     int weight0,           \
                                                     int weight1,           \
                                                     int offset0,           \
                                                     int offset1,           \
                                                     intptr_t mx,           \
                                                     intptr_t my,           \
                                                     int width)             \
{                                                                           \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];               \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];               \
    int log2Wd = denom + 14 - 8;                                            \
                                                                            \
    hevc_hv_biwgt_##TAP##t_##WIDTH##w_msa(src, src_stride, src_16bit,       \
                                          MAX_PB_SIZE, dst, dst_stride,     \
                                          filter_x, filter_y, height,       \
                                          weight0, weight1, offset0,        \
                                          offset1, log2Wd);                 \
}

BI_W_MC_HV(qpel, 4, 8);
BI_W_MC_HV(qpel, 8, 8);
BI_W_MC_HV(qpel, 12, 8);
BI_W_MC_HV(qpel, 16, 8);
BI_W_MC_HV(qpel, 24, 8);
BI_W_MC_HV(qpel, 32, 8);
BI_W_MC_HV(qpel, 48, 8);
BI_W_MC_HV(qpel, 64, 8);

BI_W_MC_HV(epel, 4, 4);
BI_W_MC_HV(epel, 8, 4);
BI_W_MC_HV(epel, 6, 4);
BI_W_MC_HV(epel, 12, 4);
BI_W_MC_HV(epel, 16, 4);
BI_W_MC_HV(epel, 24, 4);
BI_W_MC_HV(epel, 32, 4);

#undef BI_W_MC_HV
