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
#include "libavcodec/mips/hevcdsp_mips.h"
#include "libavcodec/mips/hevc_macros_msa.h"

#define HEVC_BIW_RND_CLIP2(in0, in1, vec0, vec1, wgt, rnd, offset,  \
                           out0_r, out1_r, out0_l, out1_l)          \
{                                                                   \
    ILVR_H2_SW(in0, vec0, in1, vec1, out0_r, out1_r);               \
    ILVL_H2_SW(in0, vec0, in1, vec1, out0_l, out1_l);               \
                                                                    \
    out0_r = __msa_dpadd_s_w(offset, (v8i16) out0_r, (v8i16) wgt);  \
    out1_r = __msa_dpadd_s_w(offset, (v8i16) out1_r, (v8i16) wgt);  \
    out0_l = __msa_dpadd_s_w(offset, (v8i16) out0_l, (v8i16) wgt);  \
    out1_l = __msa_dpadd_s_w(offset, (v8i16) out1_l, (v8i16) wgt);  \
                                                                    \
    SRAR_W4_SW(out0_r, out1_r, out0_l, out1_l, rnd);                \
                                                                    \
    out0_r = CLIP_SW_0_255(out0_r);                                 \
    out1_r = CLIP_SW_0_255(out1_r);                                 \
    out0_l = CLIP_SW_0_255(out0_l);                                 \
    out1_l = CLIP_SW_0_255(out1_l);                                 \
}

#define HEVC_BIW_RND_CLIP4(in0, in1, in2, in3, vec0, vec1, vec2, vec3,  \
                           wgt, rnd, offset,                            \
                           out0_r, out1_r, out2_r, out3_r,              \
                           out0_l, out1_l, out2_l, out3_l)              \
{                                                                       \
    HEVC_BIW_RND_CLIP2(in0, in1, vec0, vec1, wgt, rnd, offset,          \
                       out0_r, out1_r, out0_l, out1_l)                  \
    HEVC_BIW_RND_CLIP2(in2, in3, vec2, vec3, wgt, rnd, offset,          \
                       out2_r, out3_r, out2_l, out3_l)                  \
}

#define HEVC_BI_RND_CLIP2(in0, in1, vec0, vec1, rnd_val, out0, out1)  \
{                                                                     \
    ADDS_SH2_SH(vec0, in0, vec1, in1, out0, out1);                    \
    SRARI_H2_SH(out0, out1, rnd_val);                                 \
    CLIP_SH2_0_255(out0, out1);                                       \
}

#define HEVC_BI_RND_CLIP4(in0, in1, in2, in3,                      \
                          vec0, vec1, vec2, vec3, rnd_val,         \
                          out0, out1, out2, out3)                  \
{                                                                  \
    HEVC_BI_RND_CLIP2(in0, in1, vec0, vec1, rnd_val, out0, out1);  \
    HEVC_BI_RND_CLIP2(in2, in3, vec2, vec3, rnd_val, out2, out3);  \
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
    int32_t offset, weight;
    v16i8 zero = { 0 };
    v4i32 weight_vec, offset_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    if (2 == height) {
        v16i8 src0, src1;
        v8i16 in0, in1, dst0;
        v4i32 dst0_r, dst0_l;

        LD_SB2(src0_ptr, src_stride, src0, src1);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        in0 = (v8i16) __msa_ilvr_d((v2i64) in1, (v2i64) in0);
        src0 = (v16i8) __msa_ilvr_w((v4i32) src1, (v4i32) src0);

        dst0 = (v8i16) __msa_ilvr_b(zero, src0);
        dst0 <<= 6;

        ILVRL_H2_SW(dst0, in0, dst0_r, dst0_l);
        dst0_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_r,
                                 (v8i16) weight_vec);
        dst0_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
        dst0_r = CLIP_SW_0_255(dst0_r);
        dst0_l = CLIP_SW_0_255(dst0_l);

        HEVC_PCK_SW_SB2(dst0_l, dst0_r, dst0_r);
        ST4x2_UB(dst0_r, dst, dst_stride);
    } else if (4 == height) {
        v16i8 src0, src1, src2, src3;
        v8i16 in0, in1, in2, in3;
        v8i16 dst0, dst1;
        v4i32 dst0_r, dst1_r, dst0_l, dst1_l;

        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        ILVR_W2_SB(src1, src0, src3, src2, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, dst0, dst1);
        dst0 <<= 6;
        dst1 <<= 6;
        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
    } else if (0 == height % 8) {
        uint32_t loop_cnt;
        v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
        v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
        v8i16 dst0, dst1, dst2, dst3;
        v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_SB8(src0_ptr, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src0_ptr += (8 * src_stride);
            LD_SH8(src1_ptr, src2_stride,
                   in0, in1, in2, in3, in4, in5, in6, in7);
            src1_ptr += (8 * src2_stride);

            ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
            ILVR_D2_SH(in5, in4, in7, in6, in2, in3);
            ILVR_W4_SB(src1, src0, src3, src2, src5, src4, src7, src6,
                       src0, src1, src2, src3);
            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       dst0, dst1, dst2, dst3);

            SLLI_4V(dst0, dst1, dst2, dst3, 6);
            HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                            dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST4x8_UB(dst0_r, dst1_r, dst, dst_stride);
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
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   dst0, dst1, dst2, dst3);

        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
    int32_t offset, weight;
    v16i8 zero = { 0 };
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    if (2 == height) {
        v16i8 src0, src1;
        v8i16 in0, in1, dst0, dst1;
        v4i32 dst0_r, dst1_r, dst0_l, dst1_l;

        LD_SB2(src0_ptr, src_stride, src0, src1);
        LD_SH2(src1_ptr, src2_stride, in0, in1);

        ILVR_B2_SH(zero, src0, zero, src1, dst0, dst1);

        dst0 <<= 6;
        dst1 <<= 6;
        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        ST8x2_UB(dst0_r, dst, dst_stride);
    } else if (6 == height) {
        v16i8 src0, src1, src2, src3, src4, src5;
        v8i16 in0, in1, in2, in3, in4, in5;
        v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
        v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
        v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

        LD_SB6(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5);
        LD_SH6(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   dst0, dst1, dst2, dst3);
        ILVR_B2_SH(zero, src4, zero, src5, dst4, dst5);

        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        dst4 <<= 6;
        dst5 <<= 6;
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_BIW_RND_CLIP2(dst4, dst5, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);
        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
        ST8x2_UB(dst2_r, dst, dst_stride);
    } else if (0 == height % 4) {
        uint32_t loop_cnt;
        v16i8 src0, src1, src2, src3;
        v8i16 in0, in1, in2, in3;
        v8i16 dst0, dst1, dst2, dst3;
        v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
            src0_ptr += (4 * src_stride);
            LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
            src1_ptr += (4 * src2_stride);
            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       dst0, dst1, dst2, dst3);

            SLLI_4V(dst0, dst1, dst2, dst3, 6);
            HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                            dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
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
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_BIW_RND_CLIP2(dst4, dst5, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);
        ST12x4_UB(dst0_r, dst1_r, dst2_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_biwgt_copy_16multx4mult_msa(uint8_t *src0_ptr,
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
                                             int32_t rnd_val,
                                             int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src0_ptr_tmp;
    int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    int32_t offset, weight;
    v16i8 zero = { 0 };
    v4i32 offset_vec, weight_vec, rnd_vec;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    for (cnt = (width >> 4); cnt--;) {
        src0_ptr_tmp = src0_ptr;
        src1_ptr_tmp = src1_ptr;
        dst_tmp = dst;

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            v16i8 src0, src1, src2, src3;
            v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
            v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
            v4i32 dst0_r, dst1_r, dst2_r, dst3_r;
            v4i32 dst0_l, dst1_l, dst2_l, dst3_l;

            LD_SB4(src0_ptr_tmp, src_stride, src0, src1, src2, src3);
            src0_ptr_tmp += (4 * src_stride);
            LD_SH4(src1_ptr_tmp, src2_stride, in0, in1, in2, in3);
            LD_SH4(src1_ptr_tmp + 8, src2_stride, in4, in5, in6, in7);
            src1_ptr_tmp += (4 * src2_stride);

            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       tmp0, tmp1, tmp2, tmp3);
            ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       tmp4, tmp5, tmp6, tmp7);

            SLLI_4V(tmp0, tmp1, tmp2, tmp3, 6);
            SLLI_4V(tmp4, tmp5, tmp6, tmp7, 6);
            HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                               in0, in1, in4, in5,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                            dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST_SW2(dst0_r, dst1_r, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);

            HEVC_BIW_RND_CLIP4(tmp2, tmp3, tmp6, tmp7,
                               in2, in3, in6, in7,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                            dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST_SW2(dst0_r, dst1_r, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);
        }

        src0_ptr += 16;
        src1_ptr += 16;
        dst += 16;
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
    hevc_biwgt_copy_16multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 16);
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
    hevc_biwgt_copy_16multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 16);
    hevc_biwgt_copy_8w_msa(src0_ptr + 16, src_stride,
                           src1_ptr + 16, src2_stride,
                           dst + 16, dst_stride, height, weight0,
                           weight1, offset0, offset1, rnd_val);
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
    hevc_biwgt_copy_16multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 32);
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
    hevc_biwgt_copy_16multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 48);
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
    hevc_biwgt_copy_16multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 64);
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
    int32_t offset, weight;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1;
    v8i16 in0, in1, in2, in3;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

    src0_ptr -= 3;
    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        VSHF_B4_SB(src2, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
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
    int32_t offset, weight;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 in0, in1, in2, in3;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l, dst2_r, dst3_r, dst2_l, dst3_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
    hevc_hz_biwgt_8t_8w_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                            dst, dst_stride, filter, height,
                            weight0, weight1, offset0, offset1, rnd_val);
    hevc_hz_biwgt_8t_4w_msa(src0_ptr + 8, src_stride, src1_ptr + 8, src2_stride,
                            dst + 8, dst_stride, filter, height,
                            weight0, weight1, offset0, offset1, rnd_val);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1;
    v8i16 in0, in1, in2;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2;
    v4i32 dst0_r, dst1_r, dst2_r, dst0_l, dst1_l, dst2_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src0_ptr = src0_ptr - 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        src0_ptr += src_stride;
        LD_SH2(src1_ptr, 8, in0, in1);
        in2 = LD_SH(src1_ptr + 16);
        src1_ptr += src2_stride;
        XORI_B2_128_SB(src0, src1);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        ILVRL_H2_SW(dst2, in2, dst2_r, dst2_l);
        dst2_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_r,
                                 (v8i16) weight_vec);
        dst2_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst2_r, dst2_l, rnd_vec);
        dst2_r = CLIP_SW_0_255(dst2_r);
        dst2_l = CLIP_SW_0_255(dst2_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        HEVC_PCK_SW_SB2(dst2_l, dst2_r, dst2_r);
        dst_val0 = __msa_copy_u_d((v2i64) dst2_r, 0);
        ST_SW(dst0_r, dst);
        SD(dst_val0, dst + 16);
        dst += dst_stride;
    }
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
    int32_t offset, weight;
    v16i8 src0, src1, src2;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, 16);
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
    int32_t offset, weight;
    uint64_t dst_val0;
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        LD_SB3(src0_ptr, 16, src0, src1, src2);
        src3 = LD_SB(src0_ptr + 40);
        src0_ptr += src_stride;
        LD_SH2(src1_ptr, 8, in0, in1);
        in2 = LD_SH(src1_ptr + 16);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        VSHF_B4_SB(src1, src2, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst3 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst4 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst4, dst4, dst4, dst4);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst5 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst5, dst5, dst5, dst5);

        HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        ILVRL_H2_SW(dst2, in2, dst2_r, dst2_l);
        dst2_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_r,
                                 (v8i16) weight_vec);
        dst2_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst2_r, dst2_l, rnd_vec);
        dst2_r = CLIP_SW_0_255(dst2_r);
        dst2_l = CLIP_SW_0_255(dst2_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        HEVC_PCK_SW_SB2(dst2_l, dst2_r, dst2_r);
        dst_val0 = __msa_copy_u_d((v2i64) dst2_r, 0);
        ST_SW(dst0_r, dst);
        SD(dst_val0, dst + 16);

        LD_SH2(src1_ptr + 24, 8, in3, in4);
        in5 = LD_SH(src1_ptr + 40);
        src1_ptr += src2_stride;

        HEVC_BIW_RND_CLIP2(dst3, dst4, in3, in4,
                           weight_vec, rnd_vec, offset_vec,
                           dst3_r, dst4_r, dst3_l, dst4_l);

        ILVRL_H2_SW(dst5, in5, dst5_r, dst5_l);
        dst5_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst5_r,
                                 (v8i16) weight_vec);
        dst5_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst5_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst5_r, dst5_l, rnd_vec);
        dst5_r = CLIP_SW_0_255(dst5_r);
        dst5_l = CLIP_SW_0_255(dst5_l);

        HEVC_PCK_SW_SB4(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r);
        HEVC_PCK_SW_SB2(dst3_l, dst3_r, dst3_r);
        dst_val0 = __msa_copy_u_d((v2i64) dst3_r, 0);
        SD(dst_val0, dst + 24);
        ST_SW(dst4_r, dst + 32);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 3;
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
            dst0 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst0, dst0, dst0, dst0);
            VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                       vec0, vec1, vec2, vec3);
            dst1 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst1, dst1, dst1, dst1);
            VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst2 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst2, dst2, dst2, dst2);
            VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst3 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst3, dst3, dst3, dst3);

            HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                            dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST_SW2(dst0_r, dst1_r, dst_tmp, 16);
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
    v4i32 dst10_r, dst32_r, dst54_r, dst76_r;
    v4i32 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (3 * src_stride);
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

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

        dst10 = const_vec;
        DPADD_SB4_SH(src2110, src4332, src6554, src8776, filt0, filt1,
                     filt2, filt3, dst10, dst10, dst10, dst10);
        dst32 = const_vec;
        DPADD_SB4_SH(src4332, src6554, src8776, src10998,
                     filt0, filt1, filt2, filt3, dst32, dst32, dst32, dst32);
        dst54 = const_vec;
        DPADD_SB4_SH(src6554, src8776, src10998, src12111110,
                     filt0, filt1, filt2, filt3, dst54, dst54, dst54, dst54);
        dst76 = const_vec;
        DPADD_SB4_SH(src8776, src10998, src12111110, src14131312,
                     filt0, filt1, filt2, filt3, dst76, dst76, dst76, dst76);

        HEVC_BIW_RND_CLIP4(dst10, dst32, dst54, dst76,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst10_r, dst32_r, dst54_r, dst76_r,
                           dst10_l, dst32_l, dst54_l, dst76_l);

        HEVC_PCK_SW_SB8(dst10_l, dst10_r, dst32_l, dst32_r,
                        dst54_l, dst54_r, dst76_l, dst76_r, dst10_r, dst54_r);
        ST4x8_UB(dst10_r, dst54_r, dst, dst_stride);
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
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (3 * src_stride);
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

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

        tmp0 = const_vec;
        DPADD_SB4_SH(src10_r, src32_r, src54_r, src76_r,
                     filt0, filt1, filt2, filt3, tmp0, tmp0, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB4_SH(src21_r, src43_r, src65_r, src87_r,
                     filt0, filt1, filt2, filt3, tmp1, tmp1, tmp1, tmp1);
        tmp2 = const_vec;
        DPADD_SB4_SH(src32_r, src54_r, src76_r, src98_r,
                     filt0, filt1, filt2, filt3, tmp2, tmp2, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB4_SH(src43_r, src65_r, src87_r, src109_r,
                     filt0, filt1, filt2, filt3, tmp3, tmp3, tmp3, tmp3);

        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst0_l, dst1_l, dst2_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (3 * src_stride);
    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

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

    for (loop_cnt = (height >> 1); loop_cnt--;) {
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

        tmp0 = const_vec;
        DPADD_SB4_SH(src10_r, src32_r, src54_r, src76_r,
                     filt0, filt1, filt2, filt3, tmp0, tmp0, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB4_SH(src21_r, src43_r, src65_r, src87_r,
                     filt0, filt1, filt2, filt3, tmp1, tmp1, tmp1, tmp1);
        tmp2 = const_vec;
        DPADD_SB4_SH(src2110, src4332, src6554, src8776,
                     filt0, filt1, filt2, filt3, tmp2, tmp2, tmp2, tmp2);

        HEVC_BIW_RND_CLIP2(tmp0, tmp1, in0, in1,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        ILVRL_H2_SW(tmp2, in2, dst2_r, dst2_l);
        dst2_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_r,
                                 (v8i16) weight_vec);
        dst2_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst2_l,
                                 (v8i16) weight_vec);
        SRAR_W2_SW(dst2_r, dst2_l, rnd_vec);
        dst2_r = CLIP_SW_0_255(dst2_r);
        dst2_l = CLIP_SW_0_255(dst2_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        HEVC_PCK_SW_SB2(dst2_l, dst2_r, dst2_r);
        ST8x2_UB(dst0_r, dst, dst_stride);
        ST4x2_UB(dst2_r, dst + 8, dst_stride);
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
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (3 * src_stride);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

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

            tmp0 = const_vec;
            DPADD_SB4_SH(src10_r, src32_r, src54_r, src76_r,
                         filt0, filt1, filt2, filt3, tmp0, tmp0, tmp0, tmp0);
            tmp1 = const_vec;
            DPADD_SB4_SH(src21_r, src43_r, src65_r, src87_r,
                         filt0, filt1, filt2, filt3, tmp1, tmp1, tmp1, tmp1);
            tmp2 = const_vec;
            DPADD_SB4_SH(src10_l, src32_l, src54_l, src76_l,
                         filt0, filt1, filt2, filt3, tmp2, tmp2, tmp2, tmp2);
            tmp3 = const_vec;
            DPADD_SB4_SH(src21_l, src43_l, src65_l, src87_l,
                         filt0, filt1, filt2, filt3, tmp3, tmp3, tmp3, tmp3);

            HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                            dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST_SW2(dst0_r, dst1_r, dst_tmp, dst_stride);
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
    int32_t offset;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst87;
    v4i32 dst0_r, dst1_r;
    v4i32 tmp1, tmp2;
    v4i32 weight_vec0, weight_vec1, offset_vec, rnd_vec;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };
    v8u16 mask4 = { 0, 4, 1, 5, 2, 6, 3, 7 };

    src0_ptr -= ((3 * src_stride) + 3);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W4_SW(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec0 = __msa_fill_w(weight0);
    weight_vec1 = __msa_fill_w(weight1);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB7(src0_ptr, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src0_ptr += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src3, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src4, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src5, mask0, mask1, mask2, mask3,
               vec8, vec9, vec10, vec11);
    VSHF_B4_SB(src3, src6, mask0, mask1, mask2, mask3,
               vec12, vec13, vec14, vec15);

    dst30 = const_vec;
    DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                 dst30, dst30, dst30, dst30);
    dst41 = const_vec;
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                 dst41, dst41, dst41, dst41);
    dst52 = const_vec;
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3,
                 dst52, dst52, dst52, dst52);
    dst63 = const_vec;
    DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2, filt3,
                 dst63, dst63, dst63, dst63);

    ILVR_H3_SH(dst41, dst30, dst52, dst41, dst63, dst52,
               dst10_r, dst21_r, dst32_r);
    dst43_r = __msa_ilvl_h(dst41, dst30);
    dst54_r = __msa_ilvl_h(dst52, dst41);
    dst65_r = __msa_ilvl_h(dst63, dst52);
    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = height >> 1; loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src7, src8);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr, src2_stride, in0, in1);
        src1_ptr += (2 * src2_stride);

        in0 = (v8i16) __msa_ilvr_d((v2i64) in1, (v2i64) in0);
        XORI_B2_128_SB(src7, src8);

        VSHF_B4_SB(src7, src8, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst87 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst87, dst87, dst87, dst87);
        dst76_r = __msa_ilvr_h(dst87, dst66);
        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst87_r = __msa_vshf_h((v8i16) mask4, dst87, dst87);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);

        dst0_r >>= 6;
        dst1_r >>= 6;

        ILVRL_H2_SW(in0, in0, tmp1, tmp2);
        tmp1 = __msa_dpadd_s_w(offset_vec, (v8i16) tmp1, (v8i16) weight_vec0);
        tmp2 = __msa_dpadd_s_w(offset_vec, (v8i16) tmp2, (v8i16) weight_vec0);
        tmp1 += dst0_r * weight_vec1;
        tmp2 += dst1_r * weight_vec1;
        SRAR_W2_SW(tmp1, tmp2, rnd_vec);
        tmp1 = CLIP_SW_0_255(tmp1);
        tmp2 = CLIP_SW_0_255(tmp2);

        HEVC_PCK_SW_SB2(tmp2, tmp1, tmp1);
        ST4x2_UB(tmp1, dst, dst_stride);
        dst += (2 * dst_stride);

        dst10_r = dst32_r;
        dst32_r = dst54_r;
        dst54_r = dst76_r;
        dst21_r = dst43_r;
        dst43_r = dst65_r;
        dst65_r = dst87_r;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst87, 1);
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
                                             int32_t width)
{
    uint32_t loop_cnt, cnt;
    int32_t offset;
    uint8_t *src0_ptr_tmp;
    int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v4i32 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v4i32 weight_vec0, weight_vec1, offset_vec, rnd_vec;

    src0_ptr -= ((3 * src_stride) + 3);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec0 = __msa_fill_w(weight0);
    weight_vec1 = __msa_fill_w(weight1);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W4_SW(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (cnt = width >> 3; cnt--;) {
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

        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        dst1 = const_vec;
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        dst2 = const_vec;
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        dst3 = const_vec;
        DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);

        /* row 4 row 5 row 6 */
        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);

        dst4 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst4, dst4, dst4, dst4);
        dst5 = const_vec;
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                     dst5, dst5, dst5, dst5);
        dst6 = const_vec;
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3,
                     dst6, dst6, dst6, dst6);

        ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_r, dst32_r, dst54_r, dst21_r);
        ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
        ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_l, dst32_l, dst54_l, dst21_l);
        ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            LD_SB2(src0_ptr_tmp, src_stride, src7, src8);
            XORI_B2_128_SB(src7, src8);
            src0_ptr_tmp += 2 * src_stride;

            LD_SH2(src1_ptr_tmp, src2_stride, in0, in1);
            src1_ptr_tmp += (2 * src2_stride);

            VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst7 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst7, dst7, dst7, dst7);

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

            dst8 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst8, dst8, dst8, dst8);

            ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
            dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst1_l = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);

            dst1_r >>= 6;
            dst1_l >>= 6;

            ILVRL_H2_SW(in0, in0, tmp0, tmp1);
            ILVRL_H2_SW(in1, in1, tmp2, tmp3);
            tmp0 = __msa_dpadd_s_w(offset_vec, (v8i16) tmp0,
                                   (v8i16) weight_vec0);
            tmp1 = __msa_dpadd_s_w(offset_vec, (v8i16) tmp1,
                                   (v8i16) weight_vec0);
            tmp2 = __msa_dpadd_s_w(offset_vec, (v8i16) tmp2,
                                   (v8i16) weight_vec0);
            tmp3 = __msa_dpadd_s_w(offset_vec, (v8i16) tmp3,
                                   (v8i16) weight_vec0);

            tmp0 += (dst0_r * weight_vec1);
            tmp1 += (dst0_l * weight_vec1);
            tmp2 += (dst1_r * weight_vec1);
            tmp3 += (dst1_l * weight_vec1);

            SRAR_W4_SW(tmp0, tmp1, tmp2, tmp3, rnd_vec);
            tmp0 = CLIP_SW_0_255(tmp0);
            tmp1 = CLIP_SW_0_255(tmp1);
            tmp2 = CLIP_SW_0_255(tmp2);
            tmp3 = CLIP_SW_0_255(tmp3);
            HEVC_PCK_SW_SB4(tmp1, tmp0, tmp3, tmp2, dst0_r);
            ST8x2_UB(dst0_r, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);

            dst10_r = dst32_r;
            dst32_r = dst54_r;
            dst54_r = dst76_r;
            dst10_l = dst32_l;
            dst32_l = dst54_l;
            dst54_l = dst76_l;
            dst21_r = dst43_r;
            dst43_r = dst65_r;
            dst65_r = dst87_r;
            dst21_l = dst43_l;
            dst43_l = dst65_l;
            dst65_l = dst87_l;
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
                                     offset1, rnd_val, 8);
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
    hevc_hv_biwgt_8t_8multx2mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride, filter_x, filter_y,
                                     height, weight0, weight1, offset0,
                                     offset1, rnd_val, 8);
    hevc_hv_biwgt_8t_4w_msa(src0_ptr + 8, src_stride,
                            src1_ptr + 8, src2_stride,
                            dst + 8, dst_stride, filter_x, filter_y,
                            height, weight0, weight1, offset0, offset1,
                            rnd_val);
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
                                     offset1, rnd_val, 16);
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
                                     offset1, rnd_val, 24);
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
                                     offset1, rnd_val, 32);
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
                                     offset1, rnd_val, 48);
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
                                     offset1, rnd_val, 64);
}

static void hevc_hz_biwgt_4t_4x2_msa(uint8_t *src0_ptr,
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
    int32_t offset, weight;
    v8i16 filt0, filt1;
    v16i8 src0, src1;
    v8i16 in0, in1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };
    v16i8 mask1, vec0, vec1;
    v8i16 dst0;
    v4i32 dst0_r, dst0_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB2(src0_ptr, src_stride, src0, src1);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    in0 = (v8i16) __msa_ilvr_d((v2i64) in1, (v2i64) in0);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

    ILVRL_H2_SW(dst0, in0, dst0_r, dst0_l);
    dst0_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_r, (v8i16) weight_vec);
    dst0_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_l, (v8i16) weight_vec);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    dst0_r = CLIP_SW_0_255(dst0_r);
    dst0_l = CLIP_SW_0_255(dst0_l);

    HEVC_PCK_SW_SB2(dst0_l, dst0_r, dst0_r);
    ST4x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hz_biwgt_4t_4x4_msa(uint8_t *src0_ptr,
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
    int32_t offset, weight;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };
    v16i8 mask1;
    v8i16 dst0, dst1;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    /* rearranging filter */
    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
    ILVR_D2_SH(in1, in0, in3, in2, in0, in1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec0, vec1);
    dst1 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
    HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
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
    int32_t weight, offset;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src4, src5, src4, src5, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src6, src7, src6, src7, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST4x8_UB(dst0_r, dst1_r, dst, dst_stride);
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
                                 dst, dst_stride, filter, height,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (4 == height) {
        hevc_hz_biwgt_4t_4x4_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter, height,
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
    int32_t offset, weight;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight;
    v8i16 filt0, filt1;
    v16i8 src0, src1;
    v8i16 in0, in1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, vec0, vec1;
    v8i16 dst0, dst1;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;

    LD_SB2(src0_ptr, src_stride, src0, src1);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    XORI_B2_128_SB(src0, src1);
    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
    dst1 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
    HEVC_BIW_RND_CLIP2(dst0, dst1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST8x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hz_biwgt_4t_8x6_msa(uint8_t *src0_ptr,
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
    int32_t weight, offset;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 in0, in1, in2, in3, in4, in5;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
    dst1 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
    HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                       in0, in1, in2, in3,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst2_r, dst3_r,
                       dst0_l, dst1_l, dst2_l, dst3_l);
    HEVC_BIW_RND_CLIP2(dst4, dst5, in4, in5,
                       weight_vec, rnd_vec, offset_vec,
                       dst4_r, dst5_r, dst4_l, dst5_l);

    HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                     dst2_l, dst2_r, dst3_l, dst3_r,
                     dst4_l, dst4_r, dst5_l, dst5_r, dst0_r, dst1_r, dst2_r);
    ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x2_UB(dst2_r, dst, dst_stride);
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
    int32_t offset, weight;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
                                 dst, dst_stride, filter, height,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (6 == height) {
        hevc_hz_biwgt_4t_8x6_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter, height,
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
    int32_t offset, weight;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask2 = {
        8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
    };
    v16i8 mask1, mask3;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src0, src1, src2, src3);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        LD_SH4(src1_ptr + 8, src2_stride, in4, in5, in6, in7);
        src1_ptr += (4 * src2_stride);
        ILVR_D2_SH(in5, in4, in7, in6, in4, in5);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
        VSHF_B2_SB(src2, src3, src2, src3, mask2, mask3, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_BIW_RND_CLIP2(dst4, dst5, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);
        ST12x4_UB(dst0_r, dst1_r, dst2_r, dst, dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 filt0, filt1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst6 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst6, dst6);
        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
        dst7 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst7, dst7);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);

        HEVC_BIW_RND_CLIP4(dst4, dst5, dst6, dst7,
                           in4, in5, in6, in7,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
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
    int32_t offset, weight;
    uint8_t *dst_tmp = dst + 16;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 in0, in1, in2, in3, in4, in5;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src0_ptr, src_stride, src0, src2);
        LD_SB2(src0_ptr + 16, src_stride, src1, src3);
        src0_ptr += (2 * src_stride);
        LD_SH2(src1_ptr, src2_stride, in0, in2);
        LD_SH2(src1_ptr + 8, src2_stride, in1, in3);
        LD_SH2(src1_ptr + 16, src2_stride, in4, in5);
        src1_ptr += (2 * src2_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src2, src3, src2, src3, mask2, mask3, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);
        /* 8 width */
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        HEVC_BIW_RND_CLIP2(dst0, dst1, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        ST8x2_UB(dst0_r, dst_tmp, dst_stride);
        dst_tmp += (2 * dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3;
    v8i16 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1;
    v8i16 in0, in1, in2, in3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        HEVC_BIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, 16);
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
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t weight, offset;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, dst10;
    v16i8 src10_r, src32_r, src21_r, src43_r, src2110, src4332;
    v4i32 dst10_r, dst10_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

    dst10 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);

    ILVRL_H2_SW(dst10, in0, dst10_r, dst10_l);
    dst10_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst10_r, (v8i16) weight_vec);
    dst10_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst10_l, (v8i16) weight_vec);
    SRAR_W2_SW(dst10_r, dst10_l, rnd_vec);
    dst10_r = CLIP_SW_0_255(dst10_r);
    dst10_l = CLIP_SW_0_255(dst10_l);

    HEVC_PCK_SW_SB2(dst10_l, dst10_r, dst10_r);
    ST4x2_UB(dst10_r, dst, dst_stride);
}

static void hevc_vt_biwgt_4t_4x4_msa(uint8_t *src0_ptr,
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
    int32_t weight, offset;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src2110, src4332, src6554;
    v8i16 dst10, dst32;
    v4i32 dst10_r, dst32_r, dst10_l, dst32_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

    dst10 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
    dst32 = const_vec;
    DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);

    HEVC_BIW_RND_CLIP2(dst10, dst32, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst10_r, dst32_r, dst10_l, dst32_l);

    HEVC_PCK_SW_SB4(dst10_l, dst10_r, dst32_l, dst32_r, dst10_r);
    ST4x4_UB(dst10_r, dst10_r, 0, 1, 2, 3, dst, dst_stride);
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
    int32_t weight, offset;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src2110, src4332, src6554, src8776;
    v8i16 dst10, dst32, dst54, dst76;
    v4i32 dst10_r, dst32_r, dst54_r, dst76_r;
    v4i32 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

        dst10 = const_vec;
        DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
        dst32 = const_vec;
        DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);
        dst54 = const_vec;
        DPADD_SB2_SH(src6554, src8776, filt0, filt1, dst54, dst54);

        LD_SB2(src0_ptr, src_stride, src9, src2);
        src0_ptr += (2 * src_stride);
        ILVR_B2_SB(src9, src8, src2, src9, src98_r, src109_r);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src109_r, (v2i64) src98_r);
        src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

        dst76 = const_vec;
        DPADD_SB2_SH(src8776, src2110, filt0, filt1, dst76, dst76);
        HEVC_BIW_RND_CLIP4(dst10, dst32, dst54, dst76,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst10_r, dst32_r, dst54_r, dst76_r,
                           dst10_l, dst32_l, dst54_l, dst76_l);

        HEVC_PCK_SW_SB8(dst10_l, dst10_r, dst32_l, dst32_r,
                        dst54_l, dst54_r, dst76_l, dst76_r, dst10_r, dst54_r);
        ST4x8_UB(dst10_r, dst54_r, dst, dst_stride);
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
                                 dst, dst_stride, filter, height,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (4 == height) {
        hevc_vt_biwgt_4t_4x4_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter, height,
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);

        LD_SB2(src0_ptr, src_stride, src1, src2);
        src0_ptr += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        tmp2 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp3, tmp3);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, tmp0, tmp1;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

    tmp0 = const_vec;
    DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
    tmp1 = const_vec;
    DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
    HEVC_BIW_RND_CLIP2(tmp0, tmp1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST8x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_vt_biwgt_4t_8x6_msa(uint8_t *src0_ptr,
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 in0, in1, in2, in3, in4, in5;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

    tmp0 = const_vec;
    DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
    tmp1 = const_vec;
    DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
    tmp2 = const_vec;
    DPADD_SB2_SH(src32_r, src54_r, filt0, filt1, tmp2, tmp2);
    tmp3 = const_vec;
    DPADD_SB2_SH(src43_r, src65_r, filt0, filt1, tmp3, tmp3);
    tmp4 = const_vec;
    DPADD_SB2_SH(src54_r, src76_r, filt0, filt1, tmp4, tmp4);
    tmp5 = const_vec;
    DPADD_SB2_SH(src65_r, src87_r, filt0, filt1, tmp5, tmp5);
    HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                       in0, in1, in2, in3,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst2_r, dst3_r,
                       dst0_l, dst1_l, dst2_l, dst3_l);
    HEVC_BIW_RND_CLIP2(tmp4, tmp5, in4, in5,
                       weight_vec, rnd_vec, offset_vec,
                       dst4_r, dst5_r, dst4_l, dst5_l);

    HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                     dst2_l, dst2_r, dst3_l, dst3_r,
                     dst4_l, dst4_r, dst5_l, dst5_r, dst0_r, dst1_r, dst2_r);
    ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x2_UB(dst2_r, dst, dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);

        LD_SB2(src0_ptr, src_stride, src1, src2);
        src0_ptr += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        tmp2 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp3, tmp3);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
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
                                 dst, dst_stride, filter, height,
                                 weight0, weight1, offset0, offset1, rnd_val);
    } else if (6 == height) {
        hevc_vt_biwgt_4t_8x6_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter, height,
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

    src0_ptr -= (1 * src_stride);

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
        tmp4 = const_vec;
        DPADD_SB2_SH(src2110, src4332, filt0, filt1, tmp4, tmp4);

        LD_SB2(src0_ptr, src_stride, src5, src2);
        src0_ptr += (2 * src_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src54_l, src65_l);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src65_l, (v2i64) src54_l);

        tmp2 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp3, tmp3);
        tmp5 = const_vec;
        DPADD_SB2_SH(src4332, src2110, filt0, filt1, tmp5, tmp5);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_BIW_RND_CLIP2(tmp4, tmp5, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);
        ST12x4_UB(dst0_r, dst1_r, dst2_r, dst, dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 in0, in1, in2, in3;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
        tmp2 = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, tmp3, tmp3);

        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);
        LD_SB2(src0_ptr, src_stride, src5, src2);
        src0_ptr += (2 * src_stride);

        LD_SH2(src1_ptr, src2_stride, in0, in1);
        LD_SH2(src1_ptr + 8, src2_stride, in2, in3);
        src1_ptr += (2 * src2_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);

        tmp0 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp1, tmp1);
        tmp2 = const_vec;
        DPADD_SB2_SH(src32_l, src10_l, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src43_l, src21_l, filt0, filt1, tmp3, tmp3);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10, src11;
    v8i16 in0, in1, in2, in3, in4, in5;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp4 = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, tmp4, tmp4);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
        tmp5 = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, tmp5, tmp5);
        /* 8width */
        tmp2 = const_vec;
        DPADD_SB2_SH(src76_r, src98_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src87_r, src109_r, filt0, filt1, tmp3, tmp3);
        /* 16width */
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        /* 8width */
        HEVC_BIW_RND_CLIP2(tmp2, tmp3, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);
        /* 16width */
        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        /* 8width */
        HEVC_PCK_SW_SB4(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        ST8x2_UB(dst4_r, dst + 16, dst_stride);
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
        tmp0 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp0, tmp0);
        tmp4 = const_vec;
        DPADD_SB2_SH(src32_l, src10_l, filt0, filt1, tmp4, tmp4);
        tmp1 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp1, tmp1);
        tmp5 = const_vec;
        DPADD_SB2_SH(src43_l, src21_l, filt0, filt1, tmp5, tmp5);
        /* 8width */
        tmp2 = const_vec;
        DPADD_SB2_SH(src98_r, src76_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src109_r, src87_r, filt0, filt1, tmp3, tmp3);
        /* 16width */
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        /* 8width */
        HEVC_BIW_RND_CLIP2(tmp2, tmp3, in4, in5,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);
        /* 16width */
        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);

        /* 8width */
        HEVC_PCK_SW_SB4(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        ST8x2_UB(dst4_r, dst + 16, dst_stride);
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
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src6, src7, src8, src9, src10;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v16i8 src10_l, src32_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src87_l, src109_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l, dst6_l, dst7_l;

    src0_ptr -= src_stride;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
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
        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp4 = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, tmp4, tmp4);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
        tmp5 = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, tmp5, tmp5);
        /* 16width */
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);
        /* 16width */
        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
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
        tmp2 = const_vec;
        DPADD_SB2_SH(src76_r, src98_r, filt0, filt1, tmp2, tmp2);
        tmp6 = const_vec;
        DPADD_SB2_SH(src76_l, src98_l, filt0, filt1, tmp6, tmp6);
        tmp3 = const_vec;
        DPADD_SB2_SH(src87_r, src109_r, filt0, filt1, tmp3, tmp3);
        tmp7 = const_vec;
        DPADD_SB2_SH(src87_l, src109_l, filt0, filt1, tmp7, tmp7);
        /* next 16width */
        HEVC_BIW_RND_CLIP4(tmp2, tmp3, tmp6, tmp7,
                           in4, in5, in6, in7,
                           weight_vec, rnd_vec, offset_vec,
                           dst4_r, dst5_r, dst6_r, dst7_r,
                           dst4_l, dst5_l, dst6_l, dst7_l);

        /* next 16width */
        HEVC_PCK_SW_SB8(dst4_l, dst4_r, dst6_l, dst6_r,
                        dst5_l, dst5_r, dst7_l, dst7_r, dst4_r, dst5_r);
        ST_SW2(dst4_r, dst5_r, dst_tmp, dst_stride);
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
                                     int32_t height,
                                     int32_t weight0,
                                     int32_t weight1,
                                     int32_t offset0,
                                     int32_t offset1,
                                     int32_t rnd_val)
{
    int32_t offset, weight;
    v8i16 in0, in1;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst1_r, dst0_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);
    ILVR_H2_SH(dst1, dst0, dst2, dst1, dst10_r, dst21_r);

    LD_SB2(src0_ptr, src_stride, src3, src4);
    LD_SH2(src1_ptr, src2_stride, in0, in1);
    in0 = (v8i16) __msa_ilvr_d((v2i64) in1, (v2i64) in0);
    XORI_B2_128_SB(src3, src4);
    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
    dst32_r = __msa_ilvr_h(dst3, dst2);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_r >>= 6;
    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
    dst43_r = __msa_ilvr_h(dst4, dst3);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_r >>= 6;
    dst1_r = (v4i32) __msa_pckev_h((v8i16) dst1_r, (v8i16) dst0_r);

    ILVRL_H2_SW(dst1_r, in0, dst0_r, dst0_l);
    dst0_r = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_r, (v8i16) weight_vec);
    dst0_l = __msa_dpadd_s_w(offset_vec, (v8i16) dst0_l, (v8i16) weight_vec);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    dst0_r = CLIP_SW_0_255(dst0_r);
    dst0_l = CLIP_SW_0_255(dst0_l);

    HEVC_PCK_SW_SB2(dst0_l, dst0_r, dst0_r);
    ST4x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hv_biwgt_4t_4x4_msa(uint8_t *src0_ptr,
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
    int32_t offset, weight;
    v8i16 in0, in1, in2, in3;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 tmp0, tmp1;
    v4i32 dst0_l, dst1_l;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);
    ILVR_H2_SH(dst1, dst0, dst2, dst1, dst10_r, dst21_r);

    LD_SB4(src0_ptr, src_stride, src3, src4, src5, src6);
    LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
    ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
    XORI_B4_128_SB(src3, src4, src5, src6);
    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
    dst32_r = __msa_ilvr_h(dst3, dst2);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_r >>= 6;
    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
    dst43_r = __msa_ilvr_h(dst4, dst3);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_r >>= 6;
    /* row 5 */
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
    dst10_r = __msa_ilvr_h(dst5, dst4);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
    dst2_r >>= 6;
    /* row 6 */
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
    dst21_r = __msa_ilvr_h(dst2, dst5);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
    dst3_r >>= 6;
    PCKEV_H2_SH(dst1_r, dst0_r, dst3_r, dst2_r, tmp0, tmp1);
    HEVC_BIW_RND_CLIP2(tmp0, tmp1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
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
    int32_t offset, weight;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, dst9;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);
    ILVR_H2_SH(dst1, dst0, dst2, dst1, dst10_r, dst21_r);

    for (loop_cnt = height >> 3; loop_cnt--;) {
        LD_SB8(src0_ptr, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src0_ptr += (8 * src_stride);
        LD_SH8(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5, in6, in7);
        src1_ptr += (8 * src2_stride);
        ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
        ILVR_D2_SH(in5, in4, in7, in6, in2, in3);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
        /* row 3 */
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        dst32_r = __msa_ilvr_h(dst3, dst2);
        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_r >>= 6;
        /* row 4 */
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
        dst43_r = __msa_ilvr_h(dst4, dst3);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_r >>= 6;
        /* row 5 */
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
        dst54_r = __msa_ilvr_h(dst5, dst4);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_r >>= 6;
        /* row 6 */
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst6 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst6, dst6);
        dst65_r = __msa_ilvr_h(dst6, dst5);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst3_r >>= 6;
        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
        dst7 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst7, dst7);
        dst76_r = __msa_ilvr_h(dst7, dst6);
        dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        dst4_r >>= 6;
        VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec0, vec1);
        dst8 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst8, dst8);
        dst87_r = __msa_ilvr_h(dst8, dst7);
        dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        dst5_r >>= 6;
        VSHF_B2_SB(src9, src9, src9, src9, mask0, mask1, vec0, vec1);
        dst9 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst9, dst9);
        dst10_r = __msa_ilvr_h(dst9, dst8);
        dst6_r = HEVC_FILT_4TAP(dst76_r, dst10_r, filt_h0, filt_h1);
        dst6_r >>= 6;
        VSHF_B2_SB(src10, src10, src10, src10, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        dst21_r = __msa_ilvr_h(dst2, dst9);
        dst7_r = HEVC_FILT_4TAP(dst87_r, dst21_r, filt_h0, filt_h1);
        dst7_r >>= 6;
        PCKEV_H4_SH(dst1_r, dst0_r, dst3_r, dst2_r,
                    dst5_r, dst4_r, dst7_r, dst6_r, tmp0, tmp1, tmp2, tmp3);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST4x8_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (8 * dst_stride);
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
                                 height, weight0, weight1, offset0, offset1,
                                 rnd_val);
    } else if (4 == height) {
        hevc_hv_biwgt_4t_4x4_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter_x, filter_y,
                                 height, weight0, weight1, offset0, offset1,
                                 rnd_val);
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
    uint32_t loop_cnt;
    int32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src0_ptr, src_stride, src3, src4, src5, src6);
        src0_ptr += (4 * src_stride);
        LD_SH4(src1_ptr, src2_stride, in0, in1, in2, in3);
        src1_ptr += (4 * src2_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst0_r >>= 6;
        dst0_l >>= 6;

        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

        ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst1_r >>= 6;
        dst1_l >>= 6;

        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

        ILVRL_H2_SH(dst5, dst4, dst10_r, dst10_l);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst10_l, filt_h0, filt_h1);
        dst2_r >>= 6;
        dst2_l >>= 6;
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

        ILVRL_H2_SH(dst2, dst5, dst21_r, dst21_l);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst21_l, filt_h0, filt_h1);
        dst3_r >>= 6;
        dst3_l >>= 6;
        PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                    dst2_l, dst2_r, dst3_l, dst3_r, tmp0, tmp1, tmp2, tmp3);
        HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                           in0, in1, in2, in3,
                           weight_vec, rnd_vec, offset_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r,
                           dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hv_biwgt_4t_8x2_msa(uint8_t *src0_ptr,
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
    int32_t weight, offset;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v8i16 in0, in1;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 tmp0, tmp1;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    LD_SB2(src0_ptr, src_stride, src3, src4);

    LD_SH2(src1_ptr, src2_stride, in0, in1);
    XORI_B2_128_SB(src3, src4);

    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst0_r >>= 6;
    dst0_l >>= 6;
    tmp0 = __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);

    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst1_r >>= 6;
    dst1_l >>= 6;
    tmp1 = __msa_pckev_h((v8i16) dst1_l, (v8i16) dst1_r);

    HEVC_BIW_RND_CLIP2(tmp0, tmp1, in0, in1,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst0_l, dst1_l);
    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST8x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hv_biwgt_4t_8x6_msa(uint8_t *src0_ptr,
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
    uint32_t offset, weight;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst4_r, dst4_l, dst5_r, dst5_l;
    v8i16 dst10_r, dst32_r, dst10_l, dst32_l;
    v8i16 dst21_r, dst43_r, dst21_l, dst43_l;
    v8i16 dst54_r, dst54_l, dst65_r, dst65_l;
    v8i16 dst76_r, dst76_l, dst87_r, dst87_l;
    v8i16 in0, in1, in2, in3, in4, in5;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

    LD_SB3(src0_ptr, src_stride, src0, src1, src2);
    src0_ptr += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    LD_SB2(src0_ptr, src_stride, src3, src4);
    src0_ptr += (2 * src_stride);
    XORI_B2_128_SB(src3, src4);
    LD_SH6(src1_ptr, src2_stride, in0, in1, in2, in3, in4, in5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst0_r >>= 6;
    dst0_l >>= 6;
    tmp0 = __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);

    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst1_r >>= 6;
    dst1_l >>= 6;
    tmp1 = __msa_pckev_h((v8i16) dst1_l, (v8i16) dst1_r);

    LD_SB2(src0_ptr, src_stride, src5, src6);
    src0_ptr += (2 * src_stride);
    XORI_B2_128_SB(src5, src6);

    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

    ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
    dst2_r >>= 6;
    dst2_l >>= 6;
    tmp2 = __msa_pckev_h((v8i16) dst2_l, (v8i16) dst2_r);

    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
    dst6 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst6, dst6);

    ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);
    dst3_r >>= 6;
    dst3_l >>= 6;
    tmp3 = __msa_pckev_h((v8i16) dst3_l, (v8i16) dst3_r);

    HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                       in0, in1, in2, in3,
                       weight_vec, rnd_vec, offset_vec,
                       dst0_r, dst1_r, dst2_r, dst3_r,
                       dst0_l, dst1_l, dst2_l, dst3_l);

    HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                    dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
    ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
    dst += (4 * dst_stride);

    LD_SB2(src0_ptr, src_stride, src7, src8);
    XORI_B2_128_SB(src7, src8);

    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
    dst7 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst7, dst7);

    ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst4_l = HEVC_FILT_4TAP(dst54_l, dst76_l, filt_h0, filt_h1);
    dst4_r >>= 6;
    dst4_l >>= 6;
    tmp4 = __msa_pckev_h((v8i16) dst4_l, (v8i16) dst4_r);

    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec0, vec1);
    dst8 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst8, dst8);

    ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst5_l = HEVC_FILT_4TAP(dst65_l, dst87_l, filt_h0, filt_h1);
    dst5_r >>= 6;
    dst5_l >>= 6;
    tmp5 = __msa_pckev_h((v8i16) dst5_l, (v8i16) dst5_r);

    HEVC_BIW_RND_CLIP2(tmp4, tmp5, in4, in5,
                       weight_vec, rnd_vec, offset_vec,
                       dst4_r, dst5_r, dst4_l, dst5_l);

    HEVC_PCK_SW_SB4(dst4_l, dst4_r, dst5_l, dst5_r, dst2_r);
    ST8x2_UB(dst2_r, dst, dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 in0, in1, in2, in3;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src0_ptr -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    offset = (offset0 + offset1) << rnd_val;
    weight0 = weight0 & 0x0000FFFF;
    weight = weight0 | (weight1 << 16);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    offset_vec = __msa_fill_w(offset);
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val + 1);

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
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        dst1 = const_vec;
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

        for (loop_cnt = height >> 2; loop_cnt--;) {
            LD_SB4(src0_ptr_tmp, src_stride, src3, src4, src5, src6);
            src0_ptr_tmp += (4 * src_stride);
            LD_SH4(src1_ptr_tmp, src2_stride, in0, in1, in2, in3);
            src1_ptr_tmp += (4 * src2_stride);
            XORI_B4_128_SB(src3, src4, src5, src6);

            VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
            dst3 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

            ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
            dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
            dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
            dst0_r >>= 6;
            dst0_l >>= 6;

            VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
            dst4 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

            ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
            dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
            dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
            dst1_r >>= 6;
            dst1_l >>= 6;

            VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
            dst5 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

            ILVRL_H2_SH(dst5, dst4, dst10_r, dst10_l);
            dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
            dst2_l = HEVC_FILT_4TAP(dst32_l, dst10_l, filt_h0, filt_h1);
            dst2_r >>= 6;
            dst2_l >>= 6;

            VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
            dst2 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

            ILVRL_H2_SH(dst2, dst5, dst21_r, dst21_l);
            dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
            dst3_l = HEVC_FILT_4TAP(dst43_l, dst21_l, filt_h0, filt_h1);
            dst3_r >>= 6;
            dst3_l >>= 6;

            PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, tmp0, tmp1, tmp2, tmp3);
            HEVC_BIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                               in0, in1, in2, in3,
                               weight_vec, rnd_vec, offset_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r,
                               dst0_l, dst1_l, dst2_l, dst3_l);

            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                            dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST8x4_UB(dst0_r, dst1_r, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);
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
                                 height, weight0, weight1, offset0, offset1,
                                 rnd_val);
    } else if (6 == height) {
        hevc_hv_biwgt_4t_8x6_msa(src0_ptr, src_stride, src1_ptr, src2_stride,
                                 dst, dst_stride, filter_x, filter_y,
                                 height, weight0, weight1, offset0, offset1,
                                 rnd_val);
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
    hevc_hv_biwgt_4t_8multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride,
                                     filter_x, filter_y, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 8);

    hevc_hv_biwgt_4t_4w_msa(src0_ptr + 8, src_stride, src1_ptr + 8, src2_stride,
                            dst + 8, dst_stride, filter_x, filter_y,
                            height, weight0, weight1, offset0,
                            offset1, rnd_val);
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
    hevc_hv_biwgt_4t_8multx4mult_msa(src0_ptr, src_stride,
                                     src1_ptr, src2_stride,
                                     dst, dst_stride,
                                     filter_x, filter_y, height, weight0,
                                     weight1, offset0, offset1, rnd_val, 16);
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

#define BI_W_MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                          \
void ff_hevc_put_hevc_bi_w_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,          \
                                                          ptrdiff_t            \
                                                          dst_stride,          \
                                                          uint8_t *src,        \
                                                          ptrdiff_t            \
                                                          src_stride,          \
                                                          int16_t *src_16bit,  \
                                                          int height,          \
                                                          int denom,           \
                                                          int weight0,         \
                                                          int weight1,         \
                                                          int offset0,         \
                                                          int offset1,         \
                                                          intptr_t mx,         \
                                                          intptr_t my,         \
                                                          int width)           \
{                                                                              \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];              \
                                                                               \
    int shift = 14 + 1 - 8;                                                    \
    int log2Wd = denom + shift - 1;                                            \
                                                                               \
    hevc_##DIR1##_biwgt_##TAP##t_##WIDTH##w_msa(src, src_stride,               \
                                                src_16bit, MAX_PB_SIZE,        \
                                                dst, dst_stride,               \
                                                filter, height,                \
                                                weight0, weight1, offset0,     \
                                                offset1, log2Wd);              \
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

#define BI_W_MC_HV(PEL, DIR, WIDTH, TAP, DIR1)                                 \
void ff_hevc_put_hevc_bi_w_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,          \
                                                          ptrdiff_t            \
                                                          dst_stride,          \
                                                          uint8_t *src,        \
                                                          ptrdiff_t            \
                                                          src_stride,          \
                                                          int16_t *src_16bit,  \
                                                          int height,          \
                                                          int denom,           \
                                                          int weight0,         \
                                                          int weight1,         \
                                                          int offset0,         \
                                                          int offset1,         \
                                                          intptr_t mx,         \
                                                          intptr_t my,         \
                                                          int width)           \
{                                                                              \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];                  \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];                  \
                                                                               \
    int shift = 14 + 1 - 8;                                                    \
    int log2Wd = denom + shift - 1;                                            \
                                                                               \
    hevc_##DIR1##_biwgt_##TAP##t_##WIDTH##w_msa(src, src_stride,               \
                                                src_16bit, MAX_PB_SIZE,        \
                                                dst, dst_stride,               \
                                                filter_x, filter_y,            \
                                                height, weight0, weight1,      \
                                                offset0, offset1, log2Wd);     \
}

BI_W_MC_HV(qpel, hv, 4, 8, hv);
BI_W_MC_HV(qpel, hv, 8, 8, hv);
BI_W_MC_HV(qpel, hv, 12, 8, hv);
BI_W_MC_HV(qpel, hv, 16, 8, hv);
BI_W_MC_HV(qpel, hv, 24, 8, hv);
BI_W_MC_HV(qpel, hv, 32, 8, hv);
BI_W_MC_HV(qpel, hv, 48, 8, hv);
BI_W_MC_HV(qpel, hv, 64, 8, hv);

BI_W_MC_HV(epel, hv, 4, 4, hv);
BI_W_MC_HV(epel, hv, 8, 4, hv);
BI_W_MC_HV(epel, hv, 6, 4, hv);
BI_W_MC_HV(epel, hv, 12, 4, hv);
BI_W_MC_HV(epel, hv, 16, 4, hv);
BI_W_MC_HV(epel, hv, 24, 4, hv);
BI_W_MC_HV(epel, hv, 32, 4, hv);

#undef BI_W_MC_HV
