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
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20
};

#define HEVC_UNIW_RND_CLIP2_MAX_SATU_H(in0_h, in1_h, wgt_w, offset_h, rnd_w,  \
                                       out0_h, out1_h)                        \
{                                                                             \
    v4i32 in0_r_m, in0_l_m, in1_r_m, in1_l_m;                                 \
                                                                              \
    ILVRL_H2_SW(in0_h, in0_h, in0_r_m, in0_l_m);                              \
    ILVRL_H2_SW(in1_h, in1_h, in1_r_m, in1_l_m);                              \
    DOTP_SH4_SW(in0_r_m, in1_r_m, in0_l_m, in1_l_m, wgt_w, wgt_w, wgt_w,      \
                wgt_w, in0_r_m, in1_r_m, in0_l_m, in1_l_m);                   \
    SRAR_W4_SW(in0_r_m, in1_r_m, in0_l_m, in1_l_m, rnd_w);                    \
    PCKEV_H2_SH(in0_l_m, in0_r_m, in1_l_m, in1_r_m, out0_h, out1_h);          \
    ADDS_SH2_SH(out0_h, offset_h, out1_h, offset_h, out0_h, out1_h);          \
    CLIP_SH2_0_255(out0_h, out1_h);                                           \
}

#define HEVC_UNIW_RND_CLIP4_MAX_SATU_H(in0_h, in1_h, in2_h, in3_h, wgt_w,  \
                                       offset_h, rnd_w, out0_h, out1_h,    \
                                       out2_h, out3_h)                     \
{                                                                          \
    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(in0_h, in1_h, wgt_w, offset_h, rnd_w,   \
                                   out0_h, out1_h);                        \
    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(in2_h, in3_h, wgt_w, offset_h, rnd_w,   \
                                   out2_h, out3_h);                        \
}

static void hevc_uniwgt_copy_4w_msa(uint8_t *src,
                                    int32_t src_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight,
                                    int32_t offset,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt, tp0, tp1, tp2, tp3;
    v16i8 zero = { 0 };
    v16u8 out0, out1;
    v16i8 src0 = { 0 }, src1 = { 0 };
    v8i16 dst0, dst1, dst2, dst3, offset_vec;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    if (2 == height) {
        v4i32 dst0_r, dst0_l;

        LW2(src, src_stride, tp0, tp1);
        INSERT_W2_SB(tp0, tp1, src0);
        dst0 = (v8i16) __msa_ilvr_b(zero, src0);
        dst0 <<= 6;

        ILVRL_H2_SW(dst0, dst0, dst0_r, dst0_l);
        DOTP_SH2_SW(dst0_r, dst0_l, weight_vec, weight_vec, dst0_r, dst0_l);
        SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
        dst0 = __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
        dst0 += offset_vec;
        CLIP_SH_0_255(dst0);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst0, (v16i8) dst0);
        ST_W2(out0, 0, 1, dst, dst_stride);
    } else if (4 == height) {
        LW4(src, src_stride, tp0, tp1, tp2, tp3);
        INSERT_W4_SB(tp0, tp1, tp2, tp3, src0);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        SLLI_2V(dst0, dst1, 6);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec,
                                       rnd_vec, dst0, dst1);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
        ST_W4(out0, 0, 1, 2, 3, dst, dst_stride);
    } else if (0 == (height % 8)) {
        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LW4(src, src_stride, tp0, tp1, tp2, tp3);
            src += 4 * src_stride;
            INSERT_W4_SB(tp0, tp1, tp2, tp3, src0);
            LW4(src, src_stride, tp0, tp1, tp2, tp3);
            src += 4 * src_stride;
            INSERT_W4_SB(tp0, tp1, tp2, tp3, src1);
            ILVRL_B2_SH(zero, src0, dst0, dst1);
            ILVRL_B2_SH(zero, src1, dst2, dst3);
            SLLI_4V(dst0, dst1, dst2, dst3, 6);
            HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                           offset_vec, rnd_vec, dst0, dst1,
                                           dst2, dst3);
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
            ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
            dst += 8 * dst_stride;
        }
    }
}

static void hevc_uniwgt_copy_6w_msa(uint8_t *src,
                                    int32_t src_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight,
                                    int32_t offset,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint64_t tp0, tp1, tp2, tp3;
    v16i8 zero = { 0 };
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, offset_vec;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD4(src, src_stride, tp0, tp1, tp2, tp3);
        src += (4 * src_stride);
        INSERT_D2_SB(tp0, tp1, src0);
        INSERT_D2_SB(tp2, tp3, src1);
        LD4(src, src_stride, tp0, tp1, tp2, tp3);
        src += (4 * src_stride);
        INSERT_D2_SB(tp0, tp1, src2);
        INSERT_D2_SB(tp2, tp3, src3);

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        ILVRL_B2_SH(zero, src3, dst6, dst7);

        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);

        ST_W2(out0, 0, 2, dst, dst_stride);
        ST_H2(out0, 2, 6, dst + 4, dst_stride);
        ST_W2(out1, 0, 2, dst + 2 * dst_stride, dst_stride);
        ST_H2(out1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
        dst += (4 * dst_stride);
        ST_W2(out2, 0, 2, dst, dst_stride);
        ST_H2(out2, 2, 6, dst + 4, dst_stride);
        ST_W2(out3, 0, 2, dst + 2 * dst_stride, dst_stride);
        ST_H2(out3, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_uniwgt_copy_8w_msa(uint8_t *src,
                                    int32_t src_stride,
                                    uint8_t *dst,
                                    int32_t dst_stride,
                                    int32_t height,
                                    int32_t weight,
                                    int32_t offset,
                                    int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint64_t tp0, tp1, tp2, tp3;
    v16i8 src0 = { 0 }, src1 = { 0 }, src2 = { 0 }, src3 = { 0 };
    v16i8 zero = { 0 };
    v16u8 out0, out1, out2, out3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, offset_vec;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    if (2 == height) {
        LD2(src, src_stride, tp0, tp1);
        INSERT_D2_SB(tp0, tp1, src0);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        SLLI_2V(dst0, dst1, 6);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec,
                                       rnd_vec, dst0, dst1);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
        ST_D2(out0, 0, 1, dst, dst_stride);
    } else if (4 == height) {
        LD4(src, src_stride, tp0, tp1, tp2, tp3);
        INSERT_D2_SB(tp0, tp1, src0);
        INSERT_D2_SB(tp2, tp3, src1);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
    } else if (6 == height) {
        LD4(src, src_stride, tp0, tp1, tp2, tp3);
        src += 4 * src_stride;
        INSERT_D2_SB(tp0, tp1, src0);
        INSERT_D2_SB(tp2, tp3, src1);
        LD2(src, src_stride, tp0, tp1);
        INSERT_D2_SB(tp0, tp1, src2);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_2V(dst4, dst5, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
    } else if (0 == height % 8) {
        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD4(src, src_stride, tp0, tp1, tp2, tp3);
            src += 4 * src_stride;
            INSERT_D2_SB(tp0, tp1, src0);
            INSERT_D2_SB(tp2, tp3, src1);
            LD4(src, src_stride, tp0, tp1, tp2, tp3);
            src += 4 * src_stride;
            INSERT_D2_SB(tp0, tp1, src2);
            INSERT_D2_SB(tp2, tp3, src3);

            ILVRL_B2_SH(zero, src0, dst0, dst1);
            ILVRL_B2_SH(zero, src1, dst2, dst3);
            ILVRL_B2_SH(zero, src2, dst4, dst5);
            ILVRL_B2_SH(zero, src3, dst6, dst7);
            SLLI_4V(dst0, dst1, dst2, dst3, 6);
            SLLI_4V(dst4, dst5, dst6, dst7, 6);
            HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                           offset_vec, rnd_vec, dst0, dst1,
                                           dst2, dst3);
            HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                           offset_vec, rnd_vec, dst4, dst5,
                                           dst6, dst7);
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
            PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
            ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1,
                  dst, dst_stride);
            dst += (8 * dst_stride);
        }
    }
}

static void hevc_uniwgt_copy_12w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 offset_vec;
    v16i8 zero = { 0 };
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   dst0, dst1, dst2, dst3);

        ILVL_W2_SB(src1, src0, src3, src2, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, dst4, dst5);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_2V(dst4, dst5, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_uniwgt_copy_16w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3;
    v16i8 zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, offset_vec;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        ILVRL_B2_SH(zero, src3, dst6, dst7);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        ST_UB4(out0, out1, out2, out3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_uniwgt_copy_24w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3, out4, out5;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, offset_vec;
    v8i16 dst8, dst9, dst10, dst11;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src4, src5);
        LD_SB4(src + 16, src_stride, src2, src3, src6, src7);
        src += (4 * src_stride);

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVR_B2_SH(zero, src2, zero, src3, dst4, dst5);
        ILVRL_B2_SH(zero, src4, dst6, dst7);
        ILVRL_B2_SH(zero, src5, dst8, dst9);
        ILVR_B2_SH(zero, src6, zero, src7, dst10, dst11);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);
        SLLI_4V(dst8, dst9, dst10, dst11, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst8, dst9, dst10, dst11, weight_vec,
                                       offset_vec, rnd_vec, dst8, dst9, dst10,
                                       dst11);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        PCKEV_B3_UB(dst7, dst6, dst9, dst8, dst11, dst10, out3, out4, out5);
        ST_UB4(out0, out1, out3, out4, dst, dst_stride);
        ST_D4(out2, out5, 0, 1, 0, 1, dst + 16, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_uniwgt_copy_32w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3;
    v16i8 zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, offset_vec;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src1);
        LD_SB2(src + 16, src_stride, src2, src3);
        src += (2 * src_stride);

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        ILVRL_B2_SH(zero, src3, dst6, dst7);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        ST_UB2(out0, out1, dst, dst_stride);
        ST_UB2(out2, out3, dst + 16, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_uniwgt_copy_48w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3, out4, out5;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, offset_vec;
    v8i16 dst6, dst7, dst8, dst9, dst10, dst11;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB3(src, 16, src0, src1, src2);
        src += src_stride;
        LD_SB3(src, 16, src3, src4, src5);
        src += src_stride;

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        ILVRL_B2_SH(zero, src3, dst6, dst7);
        ILVRL_B2_SH(zero, src4, dst8, dst9);
        ILVRL_B2_SH(zero, src5, dst10, dst11);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);
        SLLI_4V(dst8, dst9, dst10, dst11, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst8, dst9, dst10, dst11, weight_vec,
                                       offset_vec, rnd_vec, dst8, dst9, dst10,
                                       dst11);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        PCKEV_B3_UB(dst7, dst6, dst9, dst8, dst11, dst10, out3, out4, out5);
        ST_UB2(out0, out1, dst, 16);
        ST_UB(out2, dst + 32);
        dst += dst_stride;
        ST_UB2(out3, out4, dst, 16);
        ST_UB(out5, dst + 32);
        dst += dst_stride;
    }
}

static void hevc_uniwgt_copy_64w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3, out4, out5, out6, out7;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 zero = { 0 };
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, offset_vec;
    v8i16 dst8, dst9, dst10, dst11, dst12, dst13, dst14, dst15;
    v4i32 weight_vec, rnd_vec;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_h(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB4(src, 16, src0, src1, src2, src3);
        src += src_stride;
        LD_SB4(src, 16, src4, src5, src6, src7);
        src += src_stride;

        ILVRL_B2_SH(zero, src0, dst0, dst1);
        ILVRL_B2_SH(zero, src1, dst2, dst3);
        ILVRL_B2_SH(zero, src2, dst4, dst5);
        ILVRL_B2_SH(zero, src3, dst6, dst7);
        ILVRL_B2_SH(zero, src4, dst8, dst9);
        ILVRL_B2_SH(zero, src5, dst10, dst11);
        ILVRL_B2_SH(zero, src6, dst12, dst13);
        ILVRL_B2_SH(zero, src7, dst14, dst15);
        SLLI_4V(dst0, dst1, dst2, dst3, 6);
        SLLI_4V(dst4, dst5, dst6, dst7, 6);
        SLLI_4V(dst8, dst9, dst10, dst11, 6);
        SLLI_4V(dst12, dst13, dst14, dst15, 6);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst8, dst9, dst10, dst11, weight_vec,
                                       offset_vec, rnd_vec, dst8, dst9, dst10,
                                       dst11);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst12, dst13, dst14, dst15, weight_vec,
                                       offset_vec, rnd_vec, dst12, dst13, dst14,
                                       dst15);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        PCKEV_B2_UB(dst9, dst8, dst11, dst10, out4, out5);
        PCKEV_B2_UB(dst13, dst12, dst15, dst14, out6, out7);
        ST_UB4(out0, out1, out2, out3, dst, 16);
        dst += dst_stride;
        ST_UB4(out4, out5, out6, out7, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_hz_uniwgt_8t_4w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 mask0, mask1, mask2, mask3, vec11, vec12, vec13, vec14, vec15;
    v8i16 filter_vec, dst01, dst23, dst45, dst67;
    v8i16 dst0, dst1, dst2, dst3, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[16]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B4_SB(src0, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src2, src3, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src4, src5, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src6, src7, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);
        dst01 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);
        dst23 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                  filt3);
        dst45 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                  filt3);
        dst67 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                  filt2, filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst01, dst23, dst45, dst67, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_uniwgt_8t_8w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0, mask1, mask2, mask3;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

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

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_uniwgt_8t_12w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 filter_vec;
    v8i16 dst01, dst23, dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

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

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        LD_SB4(src + 8, src_stride, src4, src5, src6, src7);
        src += (4 * src_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

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
        VSHF_B4_SB(src4, src5, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src6, src7, mask4, mask5, mask6, mask7,
                   vec4, vec5, vec6, vec7);
        dst01 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);
        dst23 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                  filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst01, dst23, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_uniwgt_8t_16w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0, mask1, mask2, mask3;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 8, src_stride, src1, src3);
        src += (2 * src_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

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

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_UB2(out0, out1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_uniwgt_8t_24w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = 16; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src += src_stride;
        LD_SB2(src, 16, src2, src3);
        src += src_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);
        dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                 filt2, filt3);

        VSHF_B4_SB(src2, src3, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst4, dst3, dst5, dst2, out0, out1, out2);
        ST_UB2(out0, out1, dst, dst_stride);
        ST_D2(out2, 0, 1, dst + 16, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_uniwgt_8t_32w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0, mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 filter_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = height >> 1; loop_cnt--;) {
        LD_SB4(src, 8, src0, src1, src2, src3);
        src += src_stride;
        LD_SB4(src, 8, src4, src5, src6, src7);
        src += src_stride;
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

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

        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);
        dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst6 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);
        dst7 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                 filt2, filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        ST_UB2(out0, out1, dst, 16);
        dst += dst_stride;
        ST_UB2(out2, out3, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_hz_uniwgt_8t_48w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = 64; loop_cnt--;) {
        LD_SB3(src, 16, src0, src1, src2);
        src3 = LD_SB(src + 40);
        src += src_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src1, src2, mask4, mask5, mask6, mask7,
                   vec12, vec13, vec14, vec15);
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);
        dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                 filt2, filt3);

        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_UB2(out0, out1, dst, 16);
        ST_UB(out2, dst + 32);
        dst += dst_stride;
    }
}

static void hevc_hz_uniwgt_8t_64w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt, cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 3;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = height; loop_cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        for (cnt = 2; cnt--;) {
            LD_SB2(src_tmp, 16, src0, src1);
            src2 = LD_SB(src_tmp + 24);
            src_tmp += 32;
            XORI_B3_128_SB(src0, src1, src2);

            VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                       vec4, vec5, vec6, vec7);
            VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                       vec8, vec9, vec10, vec11);
            VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                       vec12, vec13, vec14, vec15);
            dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);
            dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1,
                                     filt2, filt3);
            dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1,
                                     filt2, filt3);
            dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                     filt2, filt3);

            HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                           offset_vec, rnd_vec, dst0, dst1,
                                           dst2, dst3);

            PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
            ST_UB2(out0, out1, dst_tmp, 16);
            dst_tmp += 32;
        }

        src += src_stride;
        dst += dst_stride;
    }
}

static void hevc_vt_uniwgt_8t_4w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src9, src10, src11, src12, src13, src14;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src1110_r, src1211_r, src1312_r, src1413_r;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v16i8 src12111110, src14131312;
    v8i16 filter_vec, dst01, dst23, dst45, dst67;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 dst0, dst1, dst2, dst3, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= (3 * src_stride);


    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);

    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);

    ILVR_D3_SB(src21_r, src10_r, src43_r,
               src32_r, src65_r, src54_r, src2110, src4332, src6554);

    XORI_B3_128_SB(src2110, src4332, src6554);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride,
               src7, src8, src9, src10, src11, src12, src13, src14);
        src += (8 * src_stride);
        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        ILVR_B4_SB(src11, src10, src12, src11, src13, src12, src14, src13,
                   src1110_r, src1211_r, src1312_r, src1413_r);
        ILVR_D4_SB(src87_r, src76_r, src109_r, src98_r, src1211_r, src1110_r,
                   src1413_r, src1312_r,
                   src8776, src10998, src12111110, src14131312);
        XORI_B4_128_SB(src8776, src10998, src12111110, src14131312);
        dst01 = HEVC_FILT_8TAP_SH(src2110, src4332, src6554, src8776, filt0,
                                  filt1, filt2, filt3);
        dst23 = HEVC_FILT_8TAP_SH(src4332, src6554, src8776, src10998, filt0,
                                  filt1, filt2, filt3);
        dst45 = HEVC_FILT_8TAP_SH(src6554, src8776, src10998, src12111110,
                                  filt0, filt1, filt2, filt3);
        dst67 = HEVC_FILT_8TAP_SH(src8776, src10998, src12111110, src14131312,
                                  filt0, filt1, filt2, filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst01, dst23, dst45, dst67, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        src2110 = src10998;
        src4332 = src12111110;
        src6554 = src14131312;
        src6 = src14;
    }
}

static void hevc_vt_uniwgt_8t_8w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec;
    v8i16 dst0, dst1, dst2, dst3, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= (3 * src_stride);

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        dst0 = HEVC_FILT_8TAP_SH(src10_r, src32_r, src54_r, src76_r, filt0,
                                 filt1, filt2, filt3);
        dst1 = HEVC_FILT_8TAP_SH(src21_r, src43_r, src65_r, src87_r, filt0,
                                 filt1, filt2, filt3);
        dst2 = HEVC_FILT_8TAP_SH(src32_r, src54_r, src76_r, src98_r, filt0,
                                 filt1, filt2, filt3);
        dst3 = HEVC_FILT_8TAP_SH(src43_r, src65_r, src87_r, src109_r, filt0,
                                 filt1, filt2, filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
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

static void hevc_vt_uniwgt_8t_12w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src10_l, src32_l, src54_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l, src109_l;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 weight_vec_h, offset_vec, denom_vec, filter_vec;
    v4i32 weight_vec, rnd_vec;

    src -= (3 * src_stride);

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_l, src32_l, src54_l, src21_l);
    ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);
    ILVR_D3_SB(src21_l, src10_l, src43_l, src32_l, src65_l, src54_l,
               src2110, src4332, src6554);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_l, src87_l, src98_l, src109_l);
        ILVR_D2_SB(src87_l, src76_l, src109_l, src98_l, src8776, src10998);

        dst0 = HEVC_FILT_8TAP_SH(src10_r, src32_r, src54_r, src76_r, filt0,
                                 filt1, filt2, filt3);
        dst1 = HEVC_FILT_8TAP_SH(src21_r, src43_r, src65_r, src87_r, filt0,
                                 filt1, filt2, filt3);
        dst2 = HEVC_FILT_8TAP_SH(src32_r, src54_r, src76_r, src98_r, filt0,
                                 filt1, filt2, filt3);
        dst3 = HEVC_FILT_8TAP_SH(src43_r, src65_r, src87_r, src109_r, filt0,
                                 filt1, filt2, filt3);
        dst4 = HEVC_FILT_8TAP_SH(src2110, src4332, src6554, src8776, filt0,
                                 filt1, filt2, filt3);
        dst5 = HEVC_FILT_8TAP_SH(src4332, src6554, src8776, src10998, filt0,
                                 filt1, filt2, filt3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;
        src2110 = src6554;
        src4332 = src8776;
        src6554 = src10998;
        src6 = src10;
    }
}

static void hevc_vt_uniwgt_8t_16multx4mult_msa(uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride,
                                               const int8_t *filter,
                                               int32_t height,
                                               int32_t weight,
                                               int32_t offset,
                                               int32_t rnd_val,
                                               int32_t weightmul16)
{
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    int32_t loop_cnt, cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v16i8 src10_l, src32_l, src54_l, src76_l;
    v16i8 src21_l, src43_l, src65_l, src87_l;
    v16i8 src98_r, src109_r, src98_l, src109_l;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= (3 * src_stride);

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    for (cnt = weightmul16; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src7, src8, src9, src10);
            src_tmp += (4 * src_stride);
            XORI_B4_128_SB(src7, src8, src9, src10);

            ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
                       src10_r, src32_r, src54_r, src21_r);
            ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
            ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
                       src10_l, src32_l, src54_l, src21_l);
            ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);
            ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                       src76_r, src87_r, src98_r, src109_r);
            ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                       src76_l, src87_l, src98_l, src109_l);

            dst0 = HEVC_FILT_8TAP_SH(src10_r, src32_r, src54_r, src76_r, filt0,
                                     filt1, filt2, filt3);
            dst1 = HEVC_FILT_8TAP_SH(src10_l, src32_l, src54_l, src76_l, filt0,
                                     filt1, filt2, filt3);
            dst2 = HEVC_FILT_8TAP_SH(src21_r, src43_r, src65_r, src87_r, filt0,
                                     filt1, filt2, filt3);
            dst3 = HEVC_FILT_8TAP_SH(src21_l, src43_l, src65_l, src87_l, filt0,
                                     filt1, filt2, filt3);
            dst4 = HEVC_FILT_8TAP_SH(src32_r, src54_r, src76_r, src98_r, filt0,
                                     filt1, filt2, filt3);
            dst5 = HEVC_FILT_8TAP_SH(src32_l, src54_l, src76_l, src98_l, filt0,
                                     filt1, filt2, filt3);
            dst6 = HEVC_FILT_8TAP_SH(src43_r, src65_r, src87_r, src109_r, filt0,
                                     filt1, filt2, filt3);
            dst7 = HEVC_FILT_8TAP_SH(src43_l, src65_l, src87_l, src109_l, filt0,
                                     filt1, filt2, filt3);

            HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                           offset_vec, rnd_vec, dst0, dst1,
                                           dst2, dst3);
            HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                           offset_vec, rnd_vec, dst4, dst5,
                                           dst6, dst7);
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
            PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
            ST_UB4(out0, out1, out2, out3, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);

            src0 = src4;
            src1 = src5;
            src2 = src6;
            src3 = src7;
            src4 = src8;
            src5 = src9;
            src6 = src10;
        }

        src += 16;
        dst += 16;
    }
}

static void hevc_vt_uniwgt_8t_16w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_vt_uniwgt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 1);
}

static void hevc_vt_uniwgt_8t_24w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_vt_uniwgt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter, 32, weight,
                                       offset, rnd_val, 1);

    hevc_vt_uniwgt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride,
                             filter, 32, weight, offset, rnd_val);
}

static void hevc_vt_uniwgt_8t_32w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_vt_uniwgt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 2);
}

static void hevc_vt_uniwgt_8t_48w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_vt_uniwgt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter, 64, weight,
                                       offset, rnd_val, 3);
}

static void hevc_vt_uniwgt_8t_64w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_vt_uniwgt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 4);
}

static void hevc_hv_uniwgt_8t_4w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst97, dst108;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r, dst109_r;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r;
    v4i32 weight_vec, offset_vec, rnd_vec, const_128, denom_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src -= ((3 * src_stride) + 3);
    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);
    denom_vec = rnd_vec - 6;

    const_128 = __msa_ldi_w(128);
    const_128 *= weight_vec;
    offset_vec += __msa_srar_w(const_128, denom_vec);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    /* row 0 row 1 row 2 row 3 */
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

    ILVRL_H2_SH(dst41, dst30, dst10_r, dst43_r);
    ILVRL_H2_SH(dst52, dst41, dst21_r, dst54_r);
    ILVRL_H2_SH(dst63, dst52, dst32_r, dst65_r);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        VSHF_B4_SB(src7, src9, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src8, src10, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        dst97 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);
        dst108 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);

        dst76_r = __msa_ilvr_h(dst97, dst66);
        ILVRL_H2_SH(dst108, dst97, dst87_r, dst109_r);
        dst66 = (v8i16) __msa_splati_d((v2i64) dst97, 1);
        dst98_r = __msa_ilvr_h(dst66, dst108);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst2_r = HEVC_FILT_8TAP(dst32_r, dst54_r, dst76_r, dst98_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst3_r = HEVC_FILT_8TAP(dst43_r, dst65_r, dst87_r, dst109_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);

        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
        MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
        SRAR_W4_SW(dst0_r, dst1_r, dst2_r, dst3_r, rnd_vec);
        ADD2(dst0_r, offset_vec, dst1_r, offset_vec, dst0_r, dst1_r);
        ADD2(dst2_r, offset_vec, dst3_r, offset_vec, dst2_r, dst3_r);
        CLIP_SW4_0_255(dst0_r, dst1_r, dst2_r, dst3_r);
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst1_r);
        out = (v16u8) __msa_pckev_b((v16i8) dst1_r, (v16i8) dst0_r);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        dst10_r = dst54_r;
        dst32_r = dst76_r;
        dst54_r = dst98_r;
        dst21_r = dst65_r;
        dst43_r = dst87_r;
        dst65_r = dst109_r;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst108, 1);
    }
}

static void hevc_hv_uniwgt_8t_8multx2mult_msa(uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter_x,
                                              const int8_t *filter_y,
                                              int32_t height,
                                              int32_t weight,
                                              int32_t offset,
                                              int32_t rnd_val,
                                              int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v4i32 weight_vec, offset_vec, rnd_vec, const_128, denom_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= ((3 * src_stride) + 3);

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);
    denom_vec = rnd_vec - 6;

    const_128 = __msa_ldi_w(128);
    const_128 *= weight_vec;
    offset_vec += __msa_srar_w(const_128, denom_vec);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);
    SPLATI_W4_SW(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (cnt = width >> 3; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

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

        ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_r, dst32_r, dst54_r, dst21_r);
        ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
        ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_l, dst32_l, dst54_l, dst21_l);
        ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            LD_SB2(src_tmp, src_stride, src7, src8);
            src_tmp += 2 * src_stride;
            XORI_B2_128_SB(src7, src8);

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

            MUL2(dst0_r, weight_vec, dst0_l, weight_vec, dst0_r, dst0_l);
            MUL2(dst1_r, weight_vec, dst1_l, weight_vec, dst1_r, dst1_l);
            SRAR_W4_SW(dst0_r, dst1_r, dst0_l, dst1_l, rnd_vec);
            ADD2(dst0_r, offset_vec, dst0_l, offset_vec, dst0_r, dst0_l);
            ADD2(dst1_r, offset_vec, dst1_l, offset_vec, dst1_r, dst1_l);
            CLIP_SW4_0_255(dst0_r, dst1_r, dst0_l, dst1_l);

            PCKEV_H2_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r, dst1_r);
            dst0_r = (v4i32) __msa_pckev_b((v16i8) dst1_r, (v16i8) dst0_r);
            ST_D2(dst0_r, 0, 1, dst_tmp, dst_stride);
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

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_uniwgt_8t_8w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 8);
}

static void hevc_hv_uniwgt_8t_12w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint8_t *src_tmp, *dst_tmp;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst97, dst108;
    v8i16 filt0, filt1, filt2, filt3, filt_h0, filt_h1, filt_h2, filt_h3;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst10_l, dst32_l, dst54_l;
    v8i16 dst98_r, dst21_r, dst43_r, dst65_r, dst87_r, dst109_r;
    v8i16 dst76_l, filter_vec;
    v4i32 dst0_r, dst0_l, dst1_r, dst2_r, dst3_r;
    v4i32 weight_vec, offset_vec, rnd_vec, const_128, denom_vec;

    src -= ((3 * src_stride) + 3);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);
    denom_vec = rnd_vec - 6;

    const_128 = __msa_ldi_w(128);
    const_128 *= weight_vec;
    offset_vec += __msa_srar_w(const_128, denom_vec);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    src_tmp = src;
    dst_tmp = dst;

    LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src_tmp += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    /* row 0 row 1 row 2 row 3 */
    VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3, vec12, vec13, vec14,
               vec15);
    dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                             filt3);
    dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                             filt3);
    dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                             filt2, filt3);
    VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                             filt3);
    dst6 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                             filt3);

    for (loop_cnt = 16; loop_cnt--;) {
        src7 = LD_SB(src_tmp);
        src7 = (v16i8) __msa_xori_b((v16u8) src7, 128);
        src_tmp += src_stride;

        VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst7 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
        ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst0_l = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst0_r >>= 6;
        dst0_l >>= 6;

        MUL2(dst0_r, weight_vec, dst0_l, weight_vec, dst0_r, dst0_l);
        SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
        ADD2(dst0_r, offset_vec, dst0_l, offset_vec, dst0_r, dst0_l);
        CLIP_SW2_0_255(dst0_r, dst0_l);
        dst0_r = (v4i32) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
        out = (v16u8) __msa_pckev_b((v16i8) dst0_r, (v16i8) dst0_r);
        ST_D1(out, 0, dst_tmp);
        dst_tmp += dst_stride;

        dst0 = dst1;
        dst1 = dst2;
        dst2 = dst3;
        dst3 = dst4;
        dst4 = dst5;
        dst5 = dst6;
        dst6 = dst7;
    }

    src += 8;
    dst += 8;

    mask4 = LD_SB(ff_hevc_mask_arr + 16);
    mask5 = mask4 + 2;
    mask6 = mask4 + 4;
    mask7 = mask4 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
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
    ILVRL_H2_SH(dst41, dst30, dst10_r, dst43_r);
    ILVRL_H2_SH(dst52, dst41, dst21_r, dst54_r);
    ILVRL_H2_SH(dst63, dst52, dst32_r, dst65_r);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        VSHF_B4_SB(src7, src9, mask4, mask5, mask6, mask7, vec0, vec1, vec2,
                   vec3);
        VSHF_B4_SB(src8, src10, mask4, mask5, mask6, mask7, vec4, vec5, vec6,
                   vec7);
        dst97 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                  filt3);
        dst108 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);

        dst76_r = __msa_ilvr_h(dst97, dst66);
        ILVRL_H2_SH(dst108, dst97, dst87_r, dst109_r);
        dst66 = (v8i16) __msa_splati_d((v2i64) dst97, 1);
        dst98_r = __msa_ilvr_h(dst66, dst108);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst2_r = HEVC_FILT_8TAP(dst32_r, dst54_r, dst76_r, dst98_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst3_r = HEVC_FILT_8TAP(dst43_r, dst65_r, dst87_r, dst109_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);

        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
        MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
        SRAR_W4_SW(dst0_r, dst1_r, dst2_r, dst3_r, rnd_vec);
        ADD2(dst0_r, offset_vec, dst1_r, offset_vec, dst0_r, dst1_r);
        ADD2(dst2_r, offset_vec, dst3_r, offset_vec, dst2_r, dst3_r);
        CLIP_SW4_0_255(dst0_r, dst1_r, dst2_r, dst3_r);
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst1_r);
        out = (v16u8) __msa_pckev_b((v16i8) dst1_r, (v16i8) dst0_r);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        dst10_r = dst54_r;
        dst32_r = dst76_r;
        dst54_r = dst98_r;
        dst21_r = dst65_r;
        dst43_r = dst87_r;
        dst65_r = dst109_r;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst108, 1);
    }
}

static void hevc_hv_uniwgt_8t_16w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 16);
}

static void hevc_hv_uniwgt_8t_24w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 24);
}

static void hevc_hv_uniwgt_8t_32w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 32);
}

static void hevc_hv_uniwgt_8t_48w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 48);
}

static void hevc_hv_uniwgt_8t_64w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 64);
}

static void hevc_hz_uniwgt_4t_4x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v8i16 filt0, filt1;
    v16i8 src0, src1, vec0, vec1;
    v16i8 mask1;
    v8i16 dst0;
    v4i32 dst0_r, dst0_l;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);

    ILVRL_H2_SW(dst0, dst0, dst0_r, dst0_l);
    DOTP_SH2_SW(dst0_r, dst0_l, weight_vec, weight_vec, dst0_r, dst0_l);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    dst0 = __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
    dst0 = __msa_adds_s_h(dst0, offset_vec);
    CLIP_SH_0_255(dst0);
    out = (v16u8) __msa_pckev_b((v16i8) dst0, (v16i8) dst0);
    ST_W2(out, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);
}

static void hevc_hz_uniwgt_4t_4x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);

    src -= 1;

    /* rearranging filter */
    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec2, vec3);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);

    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1);

    out = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
}

static void hevc_hz_uniwgt_4t_4x8multiple_msa(uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height,
                                              int32_t weight,
                                              int32_t offset,
                                              int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask1, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec;
    v8i16 weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[16]);

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src4, src5, src4, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src7, src6, src7, mask0, mask1, vec6, vec7);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_uniwgt_4t_4w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    if (2 == height) {
        hevc_hz_uniwgt_4t_4x2_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (4 == height) {
        hevc_hz_uniwgt_4t_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (8 == height || 16 == height) {
        hevc_hz_uniwgt_4t_4x8multiple_msa(src, src_stride, dst, dst_stride,
                                          filter, height, weight,
                                          offset, rnd_val);
    }
}

static void hevc_hz_uniwgt_4t_6w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    v16u8 out0, out1, out2, out3;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec6, vec7);
    dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst5 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst6 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst7 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1, dst2, dst3);
    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst4, dst5, dst6, dst7);

    PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
    PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
    ST_W2(out0, 0, 2, dst, dst_stride);
    ST_H2(out0, 2, 6, dst + 4, dst_stride);
    ST_W2(out1, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
    dst += (4 * dst_stride);
    ST_W2(out2, 0, 2, dst, dst_stride);
    ST_H2(out2, 2, 6, dst + 4, dst_stride);
    ST_W2(out3, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out3, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
}

static void hevc_hz_uniwgt_4t_8x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v8i16 filt0, filt1, dst0, dst1;
    v16i8 src0, src1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);

    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1);

    out = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
    ST_D2(out, 0, 1, dst, dst_stride);
}

static void hevc_hz_uniwgt_4t_8x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0, mask1, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filt0, filt1, dst0, dst1, dst2, dst3;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1, dst2, dst3);

    PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
}

static void hevc_hz_uniwgt_4t_8x6_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out0, out1, out2;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1;
    v16i8 vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;

    LD_SB6(src, src_stride, src0, src1, src2, src3, src4, src5);
    XORI_B6_128_SB(src0, src1, src2, src3, src4, src5);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec10, vec11);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);
    dst5 = HEVC_FILT_4TAP_SH(vec10, vec11, filt0, filt1);

    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1, dst2, dst3);

    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec, rnd_vec,
                                   dst4, dst5);

    PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_hz_uniwgt_4t_8x8multiple_msa(uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height,
                                              int32_t weight,
                                              int32_t offset,
                                              int32_t rnd_val)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec6, vec7);
        dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst4, dst5, dst6, dst7);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_uniwgt_4t_8w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    if (2 == height) {
        hevc_hz_uniwgt_4t_8x2_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (4 == height) {
        hevc_hz_uniwgt_4t_8x4_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (6 == height) {
        hevc_hz_uniwgt_4t_8x6_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else {
        hevc_hz_uniwgt_4t_8x8multiple_msa(src, src_stride, dst, dst_stride,
                                          filter, height, weight, offset,
                                          rnd_val);
    }
}

static void hevc_hz_uniwgt_4t_12w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask2 = { 8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
    };
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v16i8 mask3, vec11;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec8, vec9);
        VSHF_B2_SB(src2, src3, src2, src3, mask2, mask3, vec10, vec11);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec10, vec11, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_uniwgt_4t_16w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec6, vec7);
        dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst4, dst5, dst6, dst7);

        PCKEV_B4_UB(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    out0, out1, out2, out3);

        ST_UB4(out0, out1, out2, out3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_uniwgt_4t_24w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1;
    v16i8 mask0, mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = 16; loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 16, src_stride, src1, src3);
        src += (2 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src2, src3, src2, src3, mask2, mask3, vec6, vec7);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec2, vec3);
        dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);

        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_UB2(out0, out1, dst, dst_stride);
        ST_D2(out2, 0, 1, dst + 16, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_uniwgt_4t_32w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src2 = LD_SB(src + 24);
        src += src_stride;
        LD_SB2(src, 16, src3, src4);
        src5 = LD_SB(src + 24);
        src += src_stride;
        XORI_B6_128_SB(src0, src1, src2, src3, src4, src5);
        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec2, vec3);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec6, vec7);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src3, src4, src3, src4, mask2, mask3, vec2, vec3);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec6, vec7);
        dst4 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst4, dst5, dst6, dst7);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        ST_UB2(out0, out1, dst, 16);
        dst += dst_stride;
        ST_UB2(out2, out3, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_vt_uniwgt_4t_4x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src2110, src4332;
    v8i16 dst0;
    v4i32 dst0_r, dst0_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    dst0 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
    ILVRL_H2_SW(dst0, dst0, dst0_r, dst0_l);
    DOTP_SH2_SW(dst0_r, dst0_l, weight_vec, weight_vec, dst0_r, dst0_l);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    dst0 = __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
    dst0 = __msa_adds_s_h(dst0, offset_vec);
    CLIP_SH_0_255(dst0);
    out = (v16u8) __msa_pckev_b((v16i8) dst0, (v16i8) dst0);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_4x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src2110, src4332, src6554;
    v8i16 dst0, dst1;
    v8i16 filt0, filt1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
               src32_r, src43_r, src54_r, src65_r);
    ILVR_D3_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r,
               src2110, src4332, src6554);
    XORI_B3_128_SB(src2110, src4332, src6554);
    dst0 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(src4332, src6554, filt0, filt1);
    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1);

    out = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_4x8multiple_msa(uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height,
                                              int32_t weight,
                                              int32_t offset,
                                              int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src2110, src4332, src6554, src8776;
    v16i8 src10998;
    v8i16 dst0, dst1, dst2, dst3, filt0, filt1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);
        ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
                   src32_r, src43_r, src54_r, src65_r);
        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        ILVR_D4_SB(src43_r, src32_r, src65_r, src54_r, src87_r, src76_r,
                   src109_r, src98_r, src4332, src6554, src8776, src10998);
        XORI_B4_128_SB(src4332, src6554, src8776, src10998);
        dst0 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(src4332, src6554, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(src6554, src8776, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(src8776, src10998, filt0, filt1);

        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                       weight_vec, offset_vec, rnd_vec,
                                       dst0, dst1, dst2, dst3);

        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        src2 = src10;
        src2110 = src10998;
    }
}

static void hevc_vt_uniwgt_4t_4w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    if (2 == height) {
        hevc_vt_uniwgt_4t_4x2_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (4 == height) {
        hevc_vt_uniwgt_4t_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (0 == (height % 8)) {
        hevc_vt_uniwgt_4t_4x8multiple_msa(src, src_stride, dst, dst_stride,
                                          filter, height, weight, offset,
                                          rnd_val);
    }
}

static void hevc_vt_uniwgt_4t_6w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src54_r, src65_r, src76_r, src87_r, src98_r, src109_r;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
    XORI_B3_128_SB(src0, src1, src2);
    XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
    ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
    dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(src54_r, src76_r, filt0, filt1);
    dst5 = HEVC_FILT_4TAP_SH(src65_r, src87_r, filt0, filt1);
    dst6 = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
    dst7 = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);

    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1, dst2, dst3);
    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst4, dst5, dst6, dst7);

    PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
    PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
    ST_W2(out0, 0, 2, dst, dst_stride);
    ST_H2(out0, 2, 6, dst + 4, dst_stride);
    ST_W2(out1, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
    dst += (4 * dst_stride);
    ST_W2(out2, 0, 2, dst, dst_stride);
    ST_H2(out2, 2, 6, dst + 4, dst_stride);
    ST_W2(out3, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out3, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
}

static void hevc_vt_uniwgt_4t_8x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 dst0, dst1;
    v8i16 filt0, filt1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);

    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                                   dst0, dst1);

    out = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
    ST_D2(out, 0, 1, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_8x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src5, src6, src54_r, src65_r;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (3 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
    dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                   offset_vec, rnd_vec, dst0, dst1, dst2,
                                   dst3);
    PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_8x6_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filt0, filt1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    LD_SB6(src, src_stride, src3, src4, src5, src6, src7, src8);

    XORI_B3_128_SB(src0, src1, src2);
    XORI_B6_128_SB(src3, src4, src5, src6, src7, src8);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(src54_r, src76_r, filt0, filt1);
    dst5 = HEVC_FILT_4TAP_SH(src65_r, src87_r, filt0, filt1);
    HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                   offset_vec, rnd_vec, dst0, dst1, dst2, dst3);
    HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec, rnd_vec,
                                   dst4, dst5);
    PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_vt_uniwgt_4t_8x8mult_msa(uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst,
                                          int32_t dst_stride,
                                          const int8_t *filter,
                                          int32_t height,
                                          int32_t weight,
                                          int32_t offset,
                                          int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src54_r, src65_r, src76_r, src87_r, src98_r, src109_r;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(src54_r, src76_r, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(src65_r, src87_r, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        PCKEV_B2_UB(dst1, dst0, dst3, dst2, out0, out1);
        PCKEV_B2_UB(dst5, dst4, dst7, dst6, out2, out3);
        ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);

        src2 = src10;
        src10_r = src98_r;
        src21_r = src109_r;
    }
}

static void hevc_vt_uniwgt_4t_8w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    if (2 == height) {
        hevc_vt_uniwgt_4t_8x2_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (4 == height) {
        hevc_vt_uniwgt_4t_8x4_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else if (6 == height) {
        hevc_vt_uniwgt_4t_8x6_msa(src, src_stride, dst, dst_stride,
                                  filter, weight, offset, rnd_val);
    } else {
        hevc_vt_uniwgt_4t_8x8mult_msa(src, src_stride, dst, dst_stride,
                                      filter, height, weight, offset,
                                      rnd_val);
    }
}

static void hevc_vt_uniwgt_4t_12w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1, out2, out3, out4, out5;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332;
    v16i8 src54_r, src76_r, src98_r, src65_r, src87_r, src109_r;
    v16i8 src76_l, src98_l, src87_l, src109_l, src6554, src8776, src10998;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v8i16 dst9, dst10, dst11, filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= (1 * src_stride);

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_l, (v2i64) src10_l);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
        ILVRL_B2_SB(src3, src2, src32_r, src32_l);
        ILVRL_B2_SB(src4, src3, src43_r, src43_l);
        ILVRL_B2_SB(src5, src4, src54_r, src54_l);
        ILVRL_B2_SB(src6, src5, src65_r, src65_l);
        src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_l, (v2i64) src32_l);
        src6554 = (v16i8) __msa_ilvr_d((v2i64) src65_l, (v2i64) src54_l);
        dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(src4332, src6554, filt0, filt1);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst4, dst5, weight_vec, offset_vec,
                                       rnd_vec, dst4, dst5);
        PCKEV_B3_UB(dst1, dst0, dst3, dst2, dst5, dst4, out0, out1, out2);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        ILVRL_B2_SB(src7, src6, src76_r, src76_l);
        ILVRL_B2_SB(src8, src7, src87_r, src87_l);
        ILVRL_B2_SB(src9, src8, src98_r, src98_l);
        ILVRL_B2_SB(src10, src9, src109_r, src109_l);
        src8776 = (v16i8) __msa_ilvr_d((v2i64) src87_l, (v2i64) src76_l);
        src10998 = (v16i8) __msa_ilvr_d((v2i64) src109_l, (v2i64) src98_l);
        dst6 = HEVC_FILT_4TAP_SH(src54_r, src76_r, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(src65_r, src87_r, filt0, filt1);
        dst8 = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        dst9 = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        dst10 = HEVC_FILT_4TAP_SH(src6554, src8776, filt0, filt1);
        dst11 = HEVC_FILT_4TAP_SH(src8776, src10998, filt0, filt1);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst6, dst7, dst8, dst9, weight_vec,
                                       offset_vec, rnd_vec, dst6, dst7, dst8,
                                       dst9);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst10, dst11, weight_vec, offset_vec,
                                       rnd_vec, dst10, dst11);
        PCKEV_B3_UB(dst7, dst6, dst9, dst8, dst11, dst10, out3, out4, out5);
        ST_D4(out3, out4, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(out5, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        src2 = src10;
        src10_r = src98_r;
        src21_r = src109_r;
        src2110 = src10998;
    }
}

static void hevc_vt_uniwgt_4t_16w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    int32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v16i8 src54_r, src54_l, src65_r, src65_l, src6;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);
        ILVRL_B2_SB(src3, src2, src32_r, src32_l);
        ILVRL_B2_SB(src4, src3, src43_r, src43_l);
        ILVRL_B2_SB(src5, src4, src54_r, src54_l);
        ILVRL_B2_SB(src6, src5, src65_r, src65_l);
        dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(src32_l, src54_l, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(src43_l, src65_l, filt0, filt1);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        PCKEV_B4_UB(dst4, dst0, dst5, dst1, dst6, dst2, dst7, dst3, out0, out1,
                    out2, out3);
        ST_UB4(out0, out1, out2, out3, dst, dst_stride);
        dst += (4 * dst_stride);

        src2 = src6;
        src10_r = src54_r;
        src21_r = src65_r;
        src10_l = src54_l;
        src21_l = src65_l;
    }
}

static void hevc_vt_uniwgt_4t_24w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3, out4, out5;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10, src11, src12, src13;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src87_r, src98_r, src109_r, src1110_r, src1211_r, src1312_r;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, dst9, dst10;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec, dst11;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    LD_SB3(src + 16, src_stride, src7, src8, src9);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    XORI_B3_128_SB(src7, src8, src9);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    ILVR_B2_SB(src8, src7, src9, src8, src87_r, src98_r);

    for (loop_cnt = 8; loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        LD_SB4(src + 16, src_stride, src10, src11, src12, src13);
        src += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);
        XORI_B4_128_SB(src10, src11, src12, src13);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        ILVRL_B2_SB(src5, src4, src54_r, src54_l);
        ILVRL_B2_SB(src6, src5, src65_r, src65_l);
        ILVR_B2_SB(src10, src9, src11, src10, src109_r, src1110_r);
        ILVR_B2_SB(src12, src11, src13, src12, src1211_r, src1312_r);
        dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(src32_l, src54_l, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(src43_l, src65_l, filt0, filt1);
        dst8 = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        dst9 = HEVC_FILT_4TAP_SH(src98_r, src1110_r, filt0, filt1);
        dst10 = HEVC_FILT_4TAP_SH(src109_r, src1211_r, filt0, filt1);
        dst11 = HEVC_FILT_4TAP_SH(src1110_r, src1312_r, filt0, filt1);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst8, dst9, dst10, dst11, weight_vec,
                                       offset_vec, rnd_vec, dst8, dst9, dst10,
                                       dst11);
        PCKEV_B4_UB(dst4, dst0, dst5, dst1, dst6, dst2, dst7, dst3, out0, out1,
                    out2, out3);
        PCKEV_B2_UB(dst9, dst8, dst11, dst10, out4, out5);
        ST_UB4(out0, out1, out2, out3, dst, dst_stride);
        ST_D4(out4, out5, 0, 1, 0, 1, dst + 16, dst_stride);
        dst += (4 * dst_stride);

        src2 = src6;
        src9 = src13;
        src10_r = src54_r;
        src21_r = src65_r;
        src10_l = src54_l;
        src21_l = src65_l;
        src87_r = src1211_r;
        src98_r = src1312_r;
    }
}

static void hevc_vt_uniwgt_4t_32w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 src10_l, src32_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, weight_vec_h, offset_vec, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= src_stride;

    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    weight *= 128;
    rnd_val -= 6;

    weight_vec_h = __msa_fill_h(weight);
    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val);

    weight_vec_h = __msa_srar_h(weight_vec_h, denom_vec);
    offset_vec = __msa_adds_s_h(offset_vec, weight_vec_h);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    LD_SB3(src + 16, src_stride, src5, src6, src7);
    src += (3 * src_stride);
    XORI_B6_128_SB(src0, src1, src2, src5, src6, src7);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    ILVR_B2_SB(src6, src5, src7, src6, src65_r, src76_r);
    ILVL_B2_SB(src6, src5, src7, src6, src65_l, src76_l);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        LD_SB2(src + 16, src_stride, src8, src9);
        src += (2 * src_stride);
        XORI_B4_128_SB(src3, src4, src8, src9);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        ILVRL_B2_SB(src8, src7, src87_r, src87_l);
        ILVRL_B2_SB(src9, src8, src98_r, src98_l);
        dst0 = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        dst3 = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(src65_r, src87_r, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(src65_l, src87_l, filt0, filt1);
        dst7 = HEVC_FILT_4TAP_SH(src76_l, src98_l, filt0, filt1);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst0, dst1, dst2, dst3, weight_vec,
                                       offset_vec, rnd_vec, dst0, dst1, dst2,
                                       dst3);
        HEVC_UNIW_RND_CLIP4_MAX_SATU_H(dst4, dst5, dst6, dst7, weight_vec,
                                       offset_vec, rnd_vec, dst4, dst5, dst6,
                                       dst7);
        PCKEV_B4_UB(dst2, dst0, dst3, dst1, dst6, dst4, dst7, dst5, out0, out1,
                    out2, out3);
        ST_UB2(out0, out2, dst, 16);
        dst += dst_stride;
        ST_UB2(out1, out3, dst, 16);
        dst += dst_stride;

        src2 = src4;
        src7 = src9;
        src10_r = src32_r;
        src21_r = src43_r;
        src10_l = src32_l;
        src21_l = src43_l;
        src65_r = src87_r;
        src76_r = src98_r;
        src65_l = src87_l;
        src76_l = src98_l;
    }
}

static void hevc_hv_uniwgt_4t_4x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filt_h0, filt_h1, filter_vec, tmp;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst20, dst31, dst42, dst10, dst32, dst21, dst43;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 dst0, dst1, weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
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
    MUL2(dst0, weight_vec, dst1, weight_vec, dst0, dst1);
    SRAR_W2_SW(dst0, dst1, rnd_vec);
    tmp = __msa_pckev_h((v8i16) dst1, (v8i16) dst0);
    tmp += offset_vec;
    CLIP_SH_0_255(tmp);
    out = (v16u8) __msa_pckev_b((v16i8) tmp, (v16i8) tmp);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void hevc_hv_uniwgt_4t_4x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec, tmp0, tmp1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst30, dst41, dst52, dst63, dst10, dst32, dst54, dst21, dst43, dst65;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 dst0, dst1, dst2, dst3, weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
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
    MUL2(dst0, weight_vec, dst1, weight_vec, dst0, dst1);
    MUL2(dst2, weight_vec, dst3, weight_vec, dst2, dst3);
    SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
    PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp0, tmp1);
    ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
    CLIP_SH2_0_255(tmp0, tmp1);
    out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_hv_uniwgt_4t_4multx8mult_msa(uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter_x,
                                              const int8_t *filter_y,
                                              int32_t height,
                                              int32_t weight,
                                              int32_t offset,
                                              int32_t rnd_val)
{
    uint32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filt_h0, filt_h1, filter_vec, tmp0, tmp1, tmp2, tmp3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst98_r, dst109_r, offset_vec, const_128, denom_vec;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask0, mask1, vec2, vec3);
    dst10 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst21 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    ILVRL_H2_SH(dst21, dst10, dst10_r, dst21_r);
    dst22 = (v8i16) __msa_splati_d((v2i64) dst21, 1);

    for (loop_cnt = height >> 3; loop_cnt--;) {
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);
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
        MUL2(dst0, weight_vec, dst1, weight_vec, dst0, dst1);
        MUL2(dst2, weight_vec, dst3, weight_vec, dst2, dst3);
        MUL2(dst4, weight_vec, dst5, weight_vec, dst4, dst5);
        MUL2(dst6, weight_vec, dst7, weight_vec, dst6, dst7);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, tmp0, tmp1,
                    tmp2, tmp3);
        ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
        ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
        CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_uniwgt_4t_4w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    if (2 == height) {
        hevc_hv_uniwgt_4t_4x2_msa(src, src_stride, dst, dst_stride,
                                  filter_x, filter_y, weight,
                                  offset, rnd_val);
    } else if (4 == height) {
        hevc_hv_uniwgt_4t_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter_x,filter_y, weight,
                                  offset, rnd_val);
    } else if (0 == (height % 8)) {
        hevc_hv_uniwgt_4t_4multx8mult_msa(src, src_stride, dst, dst_stride,
                                          filter_x, filter_y, height, weight,
                                          offset, rnd_val);
    }
}

static void hevc_hv_uniwgt_4t_6w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, dsth7, dsth8, dsth9;
    v8i16 dsth10, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r, dst21_r, dst43_r;
    v8i16 dst65_r, dst87_r, dst109_r, dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst98_l, dst21_l, dst43_l, dst65_l, dst87_l, dst109_l;
    v8i16 dst1021_l, dst3243_l, dst5465_l, dst7687_l, dst98109_l;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
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
    MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
    MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
    MUL2(dst4_r, weight_vec, dst5_r, weight_vec, dst4_r, dst5_r);
    MUL2(dst6_r, weight_vec, dst7_r, weight_vec, dst6_r, dst7_r);
    MUL2(dst0_l, weight_vec, dst1_l, weight_vec, dst0_l, dst1_l);
    MUL2(dst2_l, weight_vec, dst3_l, weight_vec, dst2_l, dst3_l);
    SRAR_W4_SW(dst0_r, dst1_r, dst2_r, dst3_r, rnd_vec);
    SRAR_W4_SW(dst4_r, dst5_r, dst6_r, dst7_r, rnd_vec);
    SRAR_W4_SW(dst0_l, dst1_l, dst2_l, dst3_l, rnd_vec);
    PCKEV_H2_SH(dst1_r, dst0_r, dst3_r, dst2_r, tmp0, tmp1);
    PCKEV_H2_SH(dst5_r, dst4_r, dst7_r, dst6_r, tmp2, tmp3);
    PCKEV_H2_SH(dst1_l, dst0_l, dst3_l, dst2_l, tmp4, tmp5);
    ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
    ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
    ADD2(tmp4, offset_vec, tmp5, offset_vec, tmp4, tmp5);
    CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
    CLIP_SH2_0_255(tmp4, tmp5);
    PCKEV_B3_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, out0, out1, out2);
    ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
    ST_H8(out2, 0, 1, 2, 3, 4, 5, 6, 7, dst + 4, dst_stride);
}

static void hevc_hv_uniwgt_4t_8x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 tmp0, tmp1;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
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
    MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
    MUL2(dst0_l, weight_vec, dst1_l, weight_vec, dst0_l, dst1_l);
    SRAR_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, rnd_vec);
    PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, tmp0, tmp1);
    ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
    CLIP_SH2_0_255(tmp0, tmp1);
    out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_D2(out, 0, 1, dst, dst_stride);
}

static void hevc_hv_uniwgt_4t_8multx4_msa(uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst,
                                          int32_t dst_stride,
                                          const int8_t *filter_x,
                                          const int8_t *filter_y,
                                          int32_t width8mult,
                                          int32_t weight,
                                          int32_t offset,
                                          int32_t rnd_val)
{
    uint32_t cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, mask0, mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    for (cnt = width8mult; cnt--;) {
        LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src += 8;
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);
        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
        ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
        ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
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
        MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
        MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
        MUL2(dst0_l, weight_vec, dst1_l, weight_vec, dst0_l, dst1_l);
        MUL2(dst2_l, weight_vec, dst3_l, weight_vec, dst2_l, dst3_l);
        SRAR_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, rnd_vec);
        SRAR_W4_SW(dst2_r, dst2_l, dst3_r, dst3_l, rnd_vec);
        PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, tmp0, tmp1, tmp2, tmp3);
        ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
        ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
        CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += 8;
    }
}

static void hevc_hv_uniwgt_4t_8x6_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v16i8 vec10, vec11, vec12, vec13, vec14, vec15, vec16, vec17;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst4_r, dst4_l, dst5_r, dst5_l, weight_vec, rnd_vec;
    v8i16 dst10_r, dst32_r, dst10_l, dst32_l;
    v8i16 dst21_r, dst43_r, dst21_l, dst43_l;
    v8i16 dst54_r, dst54_l, dst65_r, dst65_l;
    v8i16 dst76_r, dst76_l, dst87_r, dst87_l;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 offset_vec, const_128, denom_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    LD_SB4(src, src_stride, src5, src6, src7, src8);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);
    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec10, vec11);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec12, vec13);
    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec14, vec15);
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec16, vec17);
    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);
    dst5 = HEVC_FILT_4TAP_SH(vec10, vec11, filt0, filt1);
    dst6 = HEVC_FILT_4TAP_SH(vec12, vec13, filt0, filt1);
    dst7 = HEVC_FILT_4TAP_SH(vec14, vec15, filt0, filt1);
    dst8 = HEVC_FILT_4TAP_SH(vec16, vec17, filt0, filt1);
    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);
    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
    ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
    ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
    ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
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
    MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
    MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
    MUL2(dst4_r, weight_vec, dst5_r, weight_vec, dst4_r, dst5_r);
    MUL2(dst0_l, weight_vec, dst1_l, weight_vec, dst0_l, dst1_l);
    MUL2(dst2_l, weight_vec, dst3_l, weight_vec, dst2_l, dst3_l);
    MUL2(dst4_l, weight_vec, dst5_l, weight_vec, dst4_l, dst5_l);
    SRAR_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, rnd_vec);
    SRAR_W4_SW(dst2_r, dst2_l, dst3_r, dst3_l, rnd_vec);
    SRAR_W4_SW(dst4_r, dst4_l, dst5_r, dst5_l, rnd_vec);
    PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l, dst3_r,
                tmp0, tmp1, tmp2, tmp3);
    PCKEV_H2_SH(dst4_l, dst4_r, dst5_l, dst5_r, tmp4, tmp5);
    ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
    ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
    ADD2(tmp4, offset_vec, tmp5, offset_vec, tmp4, tmp5);
    CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
    CLIP_SH2_0_255(tmp4, tmp5);
    PCKEV_B3_UB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, out0, out1, out2);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_hv_uniwgt_4t_8multx4mult_msa(uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter_x,
                                              const int8_t *filter_y,
                                              int32_t height,
                                              int32_t weight,
                                              int32_t offset,
                                              int32_t rnd_val,
                                              int32_t width8mult)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, tmp0, tmp1, tmp2, tmp3;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    for (cnt = width8mult; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB3(src_tmp, src_stride, src0, src1, src2);
        src_tmp += (3 * src_stride);
        XORI_B3_128_SB(src0, src1, src2);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

        for (loop_cnt = height >> 2; loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
            src_tmp += (4 * src_stride);
            XORI_B4_128_SB(src3, src4, src5, src6);

            VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
            VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
            VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
            VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);
            dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
            dst4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
            dst5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
            dst6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
            ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
            ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
            ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
            ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
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
            MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
            MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
            MUL2(dst0_l, weight_vec, dst1_l, weight_vec, dst0_l, dst1_l);
            MUL2(dst2_l, weight_vec, dst3_l, weight_vec, dst2_l, dst3_l);
            SRAR_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, rnd_vec);
            SRAR_W4_SW(dst2_r, dst2_l, dst3_r, dst3_l, rnd_vec);
            PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                        dst3_r, tmp0, tmp1, tmp2, tmp3);
            ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
            ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
            CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
            PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
            ST_D4(out0, out1, 0, 1, 0, 1, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);

            dst10_r = dst54_r;
            dst10_l = dst54_l;
            dst21_r = dst65_r;
            dst21_l = dst65_l;
            dst2 = dst6;
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_uniwgt_4t_8w_msa(uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     const int8_t *filter_x,
                                     const int8_t *filter_y,
                                     int32_t height,
                                     int32_t weight,
                                     int32_t offset,
                                     int32_t rnd_val)
{

    if (2 == height) {
        hevc_hv_uniwgt_4t_8x2_msa(src, src_stride, dst, dst_stride,
                                  filter_x, filter_y, weight,
                                  offset, rnd_val);
    } else if (4 == height) {
        hevc_hv_uniwgt_4t_8multx4_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, 1, weight,
                                      offset, rnd_val);
    } else if (6 == height) {
        hevc_hv_uniwgt_4t_8x6_msa(src, src_stride, dst, dst_stride,
                                  filter_x, filter_y, weight,
                                  offset, rnd_val);
    } else if (0 == (height % 4)) {
        hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                          filter_x, filter_y, height, weight,
                                          offset, rnd_val, 1);
    }
}

static void hevc_hv_uniwgt_4t_12w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    uint32_t loop_cnt;
    uint8_t *src_tmp, *dst_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0, mask1, mask2, mask3;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec, tmp0, tmp1, tmp2, tmp3;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v8i16 dst76_r, dst98_r, dst87_r, dst109_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v8i16 offset_vec, const_128, denom_vec;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, weight_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    weight_vec = __msa_fill_w(weight);
    rnd_vec = __msa_fill_w(rnd_val);

    offset_vec = __msa_fill_h(offset);
    denom_vec = __msa_fill_h(rnd_val - 6);
    const_128 = __msa_fill_h((128 * weight));
    offset_vec += __msa_srar_h(const_128, denom_vec);

    src_tmp = src;
    dst_tmp = dst;

    LD_SB3(src_tmp, src_stride, src0, src1, src2);
    src_tmp += (3 * src_stride);
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
        LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
        src_tmp += (4 * src_stride);
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
        MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
        MUL2(dst2_r, weight_vec, dst3_r, weight_vec, dst2_r, dst3_r);
        MUL2(dst0_l, weight_vec, dst1_l, weight_vec, dst0_l, dst1_l);
        MUL2(dst2_l, weight_vec, dst3_l, weight_vec, dst2_l, dst3_l);
        SRAR_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, rnd_vec);
        SRAR_W4_SW(dst2_r, dst2_l, dst3_r, dst3_l, rnd_vec);
        PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, tmp0, tmp1, tmp2, tmp3);
        ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
        ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
        CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst_tmp, dst_stride);
        dst_tmp += (4 * dst_stride);

        dst10_r = dst54_r;
        dst10_l = dst54_l;
        dst21_r = dst65_r;
        dst21_l = dst65_l;
        dsth2 = dsth6;
    }

    src += 8;
    dst += 8;

    mask2 = LD_SB(ff_hevc_mask_arr + 16);
    mask3 = mask2 + 2;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask2, mask3, vec2, vec3);
    dst10 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst21 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    ILVRL_H2_SH(dst21, dst10, dst10_r, dst21_r);
    dst22 = (v8i16) __msa_splati_d((v2i64) dst21, 1);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9,
               src10);
        src += (8 * src_stride);
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
        MUL2(dst0, weight_vec, dst1, weight_vec, dst0, dst1);
        MUL2(dst2, weight_vec, dst3, weight_vec, dst2, dst3);
        MUL2(dst4, weight_vec, dst5, weight_vec, dst4, dst5);
        MUL2(dst6, weight_vec, dst7, weight_vec, dst6, dst7);
        SRAR_W4_SW(dst0, dst1, dst2, dst3, rnd_vec);
        SRAR_W4_SW(dst4, dst5, dst6, dst7, rnd_vec);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, tmp0, tmp1,
                    tmp2, tmp3);
        ADD2(tmp0, offset_vec, tmp1, offset_vec, tmp0, tmp1);
        ADD2(tmp2, offset_vec, tmp3, offset_vec, tmp2, tmp3);
        CLIP_SH4_0_255(tmp0, tmp1, tmp2, tmp3);
        PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_uniwgt_4t_16w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    if (4 == height) {
        hevc_hv_uniwgt_4t_8multx4_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, 2, weight, offset,
                                      rnd_val);
    } else {
        hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                          filter_x, filter_y, height, weight,
                                          offset, rnd_val, 2);
    }
}

static void hevc_hv_uniwgt_4t_24w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 3);
}

static void hevc_hv_uniwgt_4t_32w_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter_x,
                                      const int8_t *filter_y,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 4);
}

#define UNIWGT_MC_COPY(WIDTH)                                                \
void ff_hevc_put_hevc_uni_w_pel_pixels##WIDTH##_8_msa(uint8_t *dst,          \
                                                      ptrdiff_t dst_stride,  \
                                                      uint8_t *src,          \
                                                      ptrdiff_t src_stride,  \
                                                      int height,            \
                                                      int denom,             \
                                                      int weight,            \
                                                      int offset,            \
                                                      intptr_t mx,           \
                                                      intptr_t my,           \
                                                      int width)             \
{                                                                            \
    int shift = denom + 14 - 8;                                              \
    hevc_uniwgt_copy_##WIDTH##w_msa(src, src_stride, dst, dst_stride,        \
                                    height, weight, offset, shift);          \
}

UNIWGT_MC_COPY(4);
UNIWGT_MC_COPY(6);
UNIWGT_MC_COPY(8);
UNIWGT_MC_COPY(12);
UNIWGT_MC_COPY(16);
UNIWGT_MC_COPY(24);
UNIWGT_MC_COPY(32);
UNIWGT_MC_COPY(48);
UNIWGT_MC_COPY(64);

#undef UNIWGT_MC_COPY

#define UNI_W_MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                        \
void ff_hevc_put_hevc_uni_w_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,        \
                                                         ptrdiff_t            \
                                                         dst_stride,          \
                                                         uint8_t *src,        \
                                                         ptrdiff_t            \
                                                         src_stride,          \
                                                         int height,          \
                                                         int denom,           \
                                                         int weight,          \
                                                         int offset,          \
                                                         intptr_t mx,         \
                                                         intptr_t my,         \
                                                         int width)           \
{                                                                             \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];             \
    int shift = denom + 14 - 8;                                               \
                                                                              \
    hevc_##DIR1##_uniwgt_##TAP##t_##WIDTH##w_msa(src, src_stride, dst,        \
                                                 dst_stride, filter, height,  \
                                                 weight, offset, shift);      \
}

UNI_W_MC(qpel, h, 4, 8, hz, mx);
UNI_W_MC(qpel, h, 8, 8, hz, mx);
UNI_W_MC(qpel, h, 12, 8, hz, mx);
UNI_W_MC(qpel, h, 16, 8, hz, mx);
UNI_W_MC(qpel, h, 24, 8, hz, mx);
UNI_W_MC(qpel, h, 32, 8, hz, mx);
UNI_W_MC(qpel, h, 48, 8, hz, mx);
UNI_W_MC(qpel, h, 64, 8, hz, mx);

UNI_W_MC(qpel, v, 4, 8, vt, my);
UNI_W_MC(qpel, v, 8, 8, vt, my);
UNI_W_MC(qpel, v, 12, 8, vt, my);
UNI_W_MC(qpel, v, 16, 8, vt, my);
UNI_W_MC(qpel, v, 24, 8, vt, my);
UNI_W_MC(qpel, v, 32, 8, vt, my);
UNI_W_MC(qpel, v, 48, 8, vt, my);
UNI_W_MC(qpel, v, 64, 8, vt, my);

UNI_W_MC(epel, h, 4, 4, hz, mx);
UNI_W_MC(epel, h, 6, 4, hz, mx);
UNI_W_MC(epel, h, 8, 4, hz, mx);
UNI_W_MC(epel, h, 12, 4, hz, mx);
UNI_W_MC(epel, h, 16, 4, hz, mx);
UNI_W_MC(epel, h, 24, 4, hz, mx);
UNI_W_MC(epel, h, 32, 4, hz, mx);

UNI_W_MC(epel, v, 4, 4, vt, my);
UNI_W_MC(epel, v, 6, 4, vt, my);
UNI_W_MC(epel, v, 8, 4, vt, my);
UNI_W_MC(epel, v, 12, 4, vt, my);
UNI_W_MC(epel, v, 16, 4, vt, my);
UNI_W_MC(epel, v, 24, 4, vt, my);
UNI_W_MC(epel, v, 32, 4, vt, my);

#undef UNI_W_MC

#define UNI_W_MC_HV(PEL, WIDTH, TAP)                                          \
void ff_hevc_put_hevc_uni_w_##PEL##_hv##WIDTH##_8_msa(uint8_t *dst,           \
                                                      ptrdiff_t dst_stride,   \
                                                      uint8_t *src,           \
                                                      ptrdiff_t src_stride,   \
                                                      int height,             \
                                                      int denom,              \
                                                      int weight,             \
                                                      int offset,             \
                                                      intptr_t mx,            \
                                                      intptr_t my,            \
                                                      int width)              \
{                                                                             \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];                 \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];                 \
    int shift = denom + 14 - 8;                                               \
                                                                              \
    hevc_hv_uniwgt_##TAP##t_##WIDTH##w_msa(src, src_stride, dst, dst_stride,  \
                                           filter_x, filter_y,  height,       \
                                           weight, offset, shift);            \
}

UNI_W_MC_HV(qpel, 4, 8);
UNI_W_MC_HV(qpel, 8, 8);
UNI_W_MC_HV(qpel, 12, 8);
UNI_W_MC_HV(qpel, 16, 8);
UNI_W_MC_HV(qpel, 24, 8);
UNI_W_MC_HV(qpel, 32, 8);
UNI_W_MC_HV(qpel, 48, 8);
UNI_W_MC_HV(qpel, 64, 8);

UNI_W_MC_HV(epel, 4, 4);
UNI_W_MC_HV(epel, 6, 4);
UNI_W_MC_HV(epel, 8, 4);
UNI_W_MC_HV(epel, 12, 4);
UNI_W_MC_HV(epel, 16, 4);
UNI_W_MC_HV(epel, 24, 4);
UNI_W_MC_HV(epel, 32, 4);

#undef UNI_W_MC_HV
