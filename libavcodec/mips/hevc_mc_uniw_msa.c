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

#define HEVC_HV_UNIW_RND_CLIP4(in0, in1, in2, in3, wgt, offset, rnd,       \
                               out0, out1, out2, out3)                     \
{                                                                          \
    MUL4(in0, wgt, in1, wgt, in2, wgt, in3, wgt, out0, out1, out2, out3);  \
    SRAR_W4_SW(out0, out1, out2, out3, rnd);                               \
    ADD4(out0, offset, out1, offset, out2, offset, out3, offset,           \
         out0, out1, out2, out3);                                          \
    out0 = CLIP_SW_0_255(out0);                                            \
    out1 = CLIP_SW_0_255(out1);                                            \
    out2 = CLIP_SW_0_255(out2);                                            \
    out3 = CLIP_SW_0_255(out3);                                            \
}

#define HEVC_UNIW_RND_CLIP2(in0, in1, wgt, offset, rnd,              \
                            out0_r, out1_r, out0_l, out1_l)          \
{                                                                    \
    ILVR_H2_SW(in0, in0, in1, in1, out0_r, out1_r);                  \
    ILVL_H2_SW(in0, in0, in1, in1, out0_l, out1_l);                  \
    DOTP_SH4_SW(out0_r, out1_r, out0_l, out1_l, wgt, wgt, wgt, wgt,  \
                out0_r, out1_r, out0_l, out1_l);                     \
    SRAR_W4_SW(out0_r, out1_r, out0_l, out1_l, rnd);                 \
    ADD4(out0_r, offset, out1_r, offset,                             \
         out0_l, offset, out1_l, offset,                             \
         out0_r, out1_r, out0_l, out1_l);                            \
    out0_r = CLIP_SW_0_255(out0_r);                                  \
    out1_r = CLIP_SW_0_255(out1_r);                                  \
    out0_l = CLIP_SW_0_255(out0_l);                                  \
    out1_l = CLIP_SW_0_255(out1_l);                                  \
}

#define HEVC_UNIW_RND_CLIP4(in0, in1, in2, in3, wgt, offset, rnd,  \
                            out0_r, out1_r, out2_r, out3_r,        \
                            out0_l, out1_l, out2_l, out3_l)        \
{                                                                  \
    HEVC_UNIW_RND_CLIP2(in0, in1, wgt, offset, rnd,                \
                        out0_r, out1_r, out0_l, out1_l);           \
    HEVC_UNIW_RND_CLIP2(in2, in3, wgt, offset, rnd,                \
                        out2_r, out3_r, out2_l, out3_l);           \
}

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
    CLIP_SH2_0_255_MAX_SATU(out0_h, out1_h);                                  \
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
        dst0 = CLIP_SH_0_255_MAX_SATU(dst0);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst0, (v16i8) dst0);
        ST4x2_UB(out0, dst, dst_stride);
    } else if (4 == height) {
        LW4(src, src_stride, tp0, tp1, tp2, tp3);
        INSERT_W4_SB(tp0, tp1, tp2, tp3, src0);
        ILVRL_B2_SH(zero, src0, dst0, dst1);
        SLLI_2V(dst0, dst1, 6);
        HEVC_UNIW_RND_CLIP2_MAX_SATU_H(dst0, dst1, weight_vec, offset_vec,
                                       rnd_vec, dst0, dst1);
        out0 = (v16u8) __msa_pckev_b((v16i8) dst1, (v16i8) dst0);
        ST4x4_UB(out0, out0, 0, 1, 2, 3, dst, dst_stride);
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
            ST4x8_UB(out0, out1, dst, dst_stride);
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

        ST6x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
        ST6x4_UB(out2, out3, dst, dst_stride);
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
        ST8x2_UB(out0, dst, dst_stride);
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
        ST8x4_UB(out0, out1, dst, dst_stride);
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
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
        ST8x2_UB(out2, dst, dst_stride);
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
            ST8x4_UB(out0, out1, dst, dst_stride);
            dst += (4 * dst_stride);
            ST8x4_UB(out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
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
        ST12x4_UB(out0, out1, out2, dst, dst_stride);
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
        ST8x4_UB(out2, out5, dst + 16, dst_stride);
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
        ST4x8_UB(out0, out1, dst, dst_stride);
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
        ST8x4_UB(out0, out1, dst, dst_stride);
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
        ST8x4_UB(out0, out1, dst, dst_stride);
        ST4x4_UB(out2, out2, 0, 1, 2, 3, dst + 8, dst_stride);
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
        ST8x2_UB(out2, dst + 16, dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src9, src10, src11, src12, src13, src14;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src1110_r, src1211_r, src1312_r, src1413_r;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v16i8 src12111110, src14131312;
    v8i16 dst10, dst32, dst54, dst76;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

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

        HEVC_UNIW_RND_CLIP4(dst10, dst32, dst54, dst76,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST4x8_UB(dst0_r, dst1_r, dst, dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

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

        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v16i8 src10_l, src32_l, src54_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l, src109_l;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

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

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_l, src87_l, src98_l, src109_l);
        ILVR_D2_SB(src87_l, src76_l, src109_l, src98_l, src8776, src10998);

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
        tmp4 = const_vec;
        DPADD_SB4_SH(src2110, src4332, src6554, src8776,
                     filt0, filt1, filt2, filt3, tmp4, tmp4, tmp4, tmp4);
        tmp5 = const_vec;
        DPADD_SB4_SH(src4332, src6554, src8776, src10998,
                     filt0, filt1, filt2, filt3, tmp5, tmp5, tmp5, tmp5);

        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_UNIW_RND_CLIP2(tmp4, tmp5, weight_vec, offset_vec, rnd_vec,
                            dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);
        ST12x4_UB(dst0_r, dst1_r, dst2_r, dst, dst_stride);
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

static void hevc_vt_uniwgt_8t_16multx2mult_msa(uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride,
                                               const int8_t *filter,
                                               int32_t height,
                                               int32_t weight,
                                               int32_t offset,
                                               int32_t rnd_val,
                                               int32_t width)
{
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    int32_t loop_cnt, cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v16i8 src10_l, src32_l, src54_l, src76_l;
    v16i8 src21_l, src43_l, src65_l, src87_l;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight = weight & 0x0000FFFF;
    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
        ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
                   src10_r, src32_r, src54_r, src21_r);
        ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
        ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
                   src10_l, src32_l, src54_l, src21_l);
        ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);

        for (loop_cnt = (height >> 1); loop_cnt--;) {
            LD_SB2(src_tmp, src_stride, src7, src8);
            src_tmp += (2 * src_stride);
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

            HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                                weight_vec, offset_vec, rnd_vec,
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
    hevc_vt_uniwgt_8t_16multx2mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 16);
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
    hevc_vt_uniwgt_8t_16multx2mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 16);

    hevc_vt_uniwgt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride,
                             filter, height, weight, offset, rnd_val);
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
    hevc_vt_uniwgt_8t_16multx2mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 32);
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
    hevc_vt_uniwgt_8t_16multx2mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 48);
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
    hevc_vt_uniwgt_8t_16multx2mult_msa(src, src_stride, dst, dst_stride,
                                       filter, height, weight,
                                       offset, rnd_val, 64);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst87;
    v4i32 dst0_r, dst1_r, weight_vec, offset_vec, rnd_vec;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };
    v8u16 mask4 = { 0, 4, 1, 5, 2, 6, 3, 7 };

    src -= ((3 * src_stride) + 3);
    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W4_SW(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

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
        LD_SB2(src, src_stride, src7, src8);
        src += (2 * src_stride);
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
        MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
        SRAR_W2_SW(dst0_r, dst1_r, rnd_vec);
        ADD2(dst0_r, offset_vec, dst1_r, offset_vec, dst0_r, dst1_r);
        dst0_r = CLIP_SW_0_255(dst0_r);
        dst1_r = CLIP_SW_0_255(dst1_r);

        HEVC_PCK_SW_SB2(dst1_r, dst0_r, dst0_r);
        ST4x2_UB(dst0_r, dst, dst_stride);
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
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= ((3 * src_stride) + 3);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

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
            LD_SB2(src_tmp, src_stride, src7, src8);
            src_tmp += 2 * src_stride;
            XORI_B2_128_SB(src7, src8);

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

            HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst0_l, dst1_l,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst0_r, dst1_r, dst0_l, dst1_l);

            HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
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
    hevc_hv_uniwgt_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 8);
    hevc_hv_uniwgt_8t_4w_msa(src + 8, src_stride, dst + 8, dst_stride,
                             filter_x, filter_y, height, weight, offset,
                             rnd_val);
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
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v8i16 filt0, filt1;
    v16i8 src0, src1, vec0, vec1;
    v16i8 mask1;
    v8i16 dst0;
    v4i32 dst0_r, dst0_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    weight = weight & 0x0000FFFF;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

    ILVRL_H2_SW(dst0, dst0, dst0_r, dst0_l);
    DOTP_SH2_SW(dst0_r, dst0_l, weight_vec, weight_vec, dst0_r, dst0_l);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    ADD2(dst0_r, offset_vec, dst0_l, offset_vec, dst0_r, dst0_l);
    dst0_r = CLIP_SW_0_255(dst0_r);
    dst0_l = CLIP_SW_0_255(dst0_l);

    HEVC_PCK_SW_SB2(dst0_l, dst0_r, dst0_r);
    ST4x2_UB(dst0_r, dst, dst_stride);
    dst += (4 * dst_stride);
}

static void hevc_hz_uniwgt_4t_4x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, vec0, vec1;
    v8i16 dst0, dst1;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

    src -= 1;

    /* rearranging filter */
    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    weight = weight & 0x0000FFFF;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

    VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec0, vec1);
    dst1 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

    HEVC_UNIW_RND_CLIP2(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                        dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
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
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask1, vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST4x8_UB(dst0_r, dst1_r, dst, dst_stride);
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
                                  filter, height, weight, offset, rnd_val);
    } else if (4 == height) {
        hevc_hz_uniwgt_4t_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter, height, weight, offset, rnd_val);
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
    uint32_t loop_cnt;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);

        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_uniwgt_4t_8x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v8i16 filt0, filt1, dst0, dst1;
    v16i8 src0, src1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
    dst1 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

    HEVC_UNIW_RND_CLIP2(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                        dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST8x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hz_uniwgt_4t_8x6_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;

    LD_SB6(src, src_stride, src0, src1, src2, src3, src4, src5);
    LD_SB6(src, src_stride, src0, src1, src2, src3, src4, src5);
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

    HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                        weight_vec, offset_vec, rnd_vec,
                        dst0_r, dst1_r, dst2_r, dst3_r,
                        dst0_l, dst1_l, dst2_l, dst3_l);

    HEVC_UNIW_RND_CLIP2(dst4, dst5, weight_vec, offset_vec, rnd_vec,
                        dst4_r, dst5_r, dst4_l, dst5_l);

    HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                     dst2_l, dst2_r, dst3_l, dst3_r,
                     dst4_l, dst4_r, dst5_l, dst5_r, dst0_r, dst1_r, dst2_r);

    ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x2_UB(dst2_r, dst, dst_stride);
}

static void hevc_hz_uniwgt_4t_8x4multiple_msa(uint8_t *src,
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
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);

        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
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
                                  filter, height, weight, offset, rnd_val);
    } else if (6 == height) {
        hevc_hz_uniwgt_4t_8x6_msa(src, src_stride, dst, dst_stride,
                                  filter, height, weight, offset, rnd_val);
    } else {
        hevc_hz_uniwgt_4t_8x4multiple_msa(src, src_stride, dst, dst_stride,
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
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask2 = { 8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
    };
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v16i8 mask3;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_UNIW_RND_CLIP2(dst4, dst5, weight_vec, offset_vec, rnd_vec,
                            dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);

        ST12x4_UB(dst0_r, dst1_r, dst2_r, dst, dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);

        HEVC_UNIW_RND_CLIP4(dst4, dst5, dst6, dst7,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);
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
    uint8_t *dst_tmp = dst + 16;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1;
    v8i16 dst0, dst1, dst2, dst3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        /* 16 width */
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 16, src_stride, src1, src3);
        src += (2 * src_stride);

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
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

        HEVC_UNIW_RND_CLIP2(dst0, dst1, weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst0_l, dst1_l);

        HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
        ST8x2_UB(dst0_r, dst_tmp, dst_stride);
        dst_tmp += (2 * dst_stride);
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
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3;
    v8i16 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    weight = weight & 0x0000FFFF;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src2 = LD_SB(src + 24);
        src += src_stride;

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, 16);
        dst += dst_stride;

        LD_SB2(src, 16, src0, src1);
        src2 = LD_SB(src + 24);
        src += src_stride;

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

        HEVC_UNIW_RND_CLIP4(dst0, dst1, dst2, dst3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_vt_uniwgt_4t_4x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src2110, src4332;
    v8i16 dst10;
    v4i32 dst0_r, dst0_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);
    LD_SB2(src, src_stride, src3, src4);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_r, (v2i64) src32_r);
    src4332 = (v16i8) __msa_xori_b((v16u8) src4332, 128);

    dst10 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);

    ILVRL_H2_SW(dst10, dst10, dst0_r, dst0_l);
    DOTP_SH2_SW(dst0_r, dst0_l, weight_vec, weight_vec, dst0_r, dst0_l);
    SRAR_W2_SW(dst0_r, dst0_l, rnd_vec);
    ADD2(dst0_r, offset_vec, dst0_l, offset_vec, dst0_r, dst0_l);
    dst0_r = CLIP_SW_0_255(dst0_r);
    dst0_l = CLIP_SW_0_255(dst0_l);

    HEVC_PCK_SW_SB2(dst0_l, dst0_r, dst0_r);
    ST4x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_4x4_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src2110, src4332, src6554;
    v8i16 dst10, dst32;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    LD_SB4(src, src_stride, src3, src4, src5, src6);
    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
               src32_r, src43_r, src54_r, src65_r);
    ILVR_D2_SB(src43_r, src32_r, src65_r, src54_r, src4332, src6554);
    XORI_B2_128_SB(src4332, src6554);

    dst10 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
    dst32 = const_vec;
    DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);
    HEVC_UNIW_RND_CLIP2(dst10, dst32, weight_vec, offset_vec, rnd_vec,
                        dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src2110, src4332, src6554, src8776;
    v8i16 dst10, dst32, dst54, dst76;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB6(src, src_stride, src3, src4, src5, src6, src7, src8);
        src += (6 * src_stride);
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

        LD_SB2(src, src_stride, src9, src2);
        src += (2 * src_stride);
        ILVR_B2_SB(src9, src8, src2, src9, src98_r, src109_r);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src109_r, (v2i64) src98_r);
        src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

        dst76 = const_vec;
        DPADD_SB2_SH(src8776, src2110, filt0, filt1, dst76, dst76);
        HEVC_UNIW_RND_CLIP4(dst10, dst32, dst54, dst76,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST4x8_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (8 * dst_stride);
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
                                  filter, height, weight, offset, rnd_val);
    } else if (4 == height) {
        hevc_vt_uniwgt_4t_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter, height, weight, offset, rnd_val);
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
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        src += (2 * src_stride);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);

        LD_SB2(src, src_stride, src1, src2);
        src += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        tmp2 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp3, tmp3);
        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);

        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_vt_uniwgt_4t_8x2_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst0_l, dst1_l;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    LD_SB2(src, src_stride, src3, src4);
    XORI_B2_128_SB(src3, src4);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

    tmp0 = const_vec;
    DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
    tmp1 = const_vec;
    DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
    HEVC_UNIW_RND_CLIP2(tmp0, tmp1, weight_vec, offset_vec, rnd_vec,
                        dst0_r, dst1_r, dst0_l, dst1_l);

    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST8x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_8x6_msa(uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      const int8_t *filter,
                                      int32_t height,
                                      int32_t weight,
                                      int32_t offset,
                                      int32_t rnd_val)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r;
    v16i8 src21_r, src43_r, src65_r, src87_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    LD_SB6(src, src_stride, src3, src4, src5, src6, src7, src8);
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
    HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                        weight_vec, offset_vec, rnd_vec,
                        dst0_r, dst1_r, dst2_r, dst3_r,
                        dst0_l, dst1_l, dst2_l, dst3_l);
    HEVC_UNIW_RND_CLIP2(tmp4, tmp5, weight_vec, offset_vec, rnd_vec,
                        dst4_r, dst5_r, dst4_l, dst5_l);

    HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                     dst2_l, dst2_r, dst3_l, dst3_r,
                     dst4_l, dst4_r, dst5_l, dst5_r, dst0_r, dst1_r, dst2_r);
    ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x2_UB(dst2_r, dst, dst_stride);
}

static void hevc_vt_uniwgt_4t_8x4multiple_msa(uint8_t *src,
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
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        src += (2 * src_stride);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);

        LD_SB2(src, src_stride, src1, src2);
        src += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        tmp2 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp3, tmp3);
        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
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
                                  filter, height, weight, offset, rnd_val);
    } else if (6 == height) {
        hevc_vt_uniwgt_4t_8x6_msa(src, src_stride, dst, dst_stride,
                                  filter, height, weight, offset, rnd_val);
    } else {
        hevc_vt_uniwgt_4t_8x4multiple_msa(src, src_stride, dst, dst_stride,
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
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

    src -= (1 * src_stride);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_l, (v2i64) src10_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        src += (2 * src_stride);
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

        LD_SB2(src, src_stride, src5, src2);
        src += (2 * src_stride);
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
        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_UNIW_RND_CLIP2(tmp4, tmp5, weight_vec, offset_vec, rnd_vec,
                            dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                         dst2_l, dst2_r, dst3_l, dst3_r,
                         dst4_l, dst4_r, dst5_l, dst5_r,
                         dst0_r, dst1_r, dst2_r);
        ST12x4_UB(dst0_r, dst1_r, dst2_r, dst, dst_stride);
        dst += (4 * dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        src += (2 * src_stride);
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
        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);

        LD_SB2(src, src_stride, src5, src2);
        src += (2 * src_stride);
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
        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp2, tmp3,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10, src11;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l, dst4_l, dst5_l;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    LD_SB3(src + 16, src_stride, src6, src7, src8);
    src += (3 * src_stride);
    XORI_B3_128_SB(src6, src7, src8);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        LD_SB2(src + 16, src_stride, src9, src10);
        src += (2 * src_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp4 = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, tmp4, tmp4);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
        tmp5 = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, tmp5, tmp5);
        tmp2 = const_vec;
        DPADD_SB2_SH(src76_r, src98_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src87_r, src109_r, filt0, filt1, tmp3, tmp3);

        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_UNIW_RND_CLIP2(tmp2, tmp3, weight_vec, offset_vec, rnd_vec,
                            dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        HEVC_PCK_SW_SB4(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        ST8x2_UB(dst4_r, dst + 16, dst_stride);
        dst += (2 * dst_stride);

        LD_SB2(src, src_stride, src5, src2);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);
        LD_SB2(src + 16, src_stride, src11, src8);
        src += (2 * src_stride);
        XORI_B2_128_SB(src11, src8);
        ILVR_B2_SB(src11, src10, src8, src11, src76_r, src87_r);

        tmp0 = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, tmp0, tmp0);
        tmp4 = const_vec;
        DPADD_SB2_SH(src32_l, src10_l, filt0, filt1, tmp4, tmp4);
        tmp1 = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, tmp1, tmp1);
        tmp5 = const_vec;
        DPADD_SB2_SH(src43_l, src21_l, filt0, filt1, tmp5, tmp5);
        tmp2 = const_vec;
        DPADD_SB2_SH(src98_r, src76_r, filt0, filt1, tmp2, tmp2);
        tmp3 = const_vec;
        DPADD_SB2_SH(src109_r, src87_r, filt0, filt1, tmp3, tmp3);

        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_UNIW_RND_CLIP2(tmp2, tmp3, weight_vec, offset_vec, rnd_vec,
                            dst4_r, dst5_r, dst4_l, dst5_l);

        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        HEVC_PCK_SW_SB4(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        ST8x2_UB(dst4_r, dst + 16, dst_stride);
        dst += (2 * dst_stride);
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
    int32_t loop_cnt;
    uint8_t *dst_tmp = dst + 16;
    v16i8 src0, src1, src2, src3, src4, src6, src7, src8, src9, src10;
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

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;
    weight = weight & 0x0000FFFF;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    LD_SB3(src + 16, src_stride, src6, src7, src8);
    src += (3 * src_stride);
    XORI_B3_128_SB(src6, src7, src8);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
    ILVL_B2_SB(src7, src6, src8, src7, src76_l, src87_l);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        tmp0 = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, tmp0, tmp0);
        tmp4 = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, tmp4, tmp4);
        tmp1 = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, tmp1, tmp1);
        tmp5 = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, tmp5, tmp5);

        HEVC_UNIW_RND_CLIP4(tmp0, tmp1, tmp4, tmp5,
                            weight_vec, offset_vec, rnd_vec,
                            dst0_r, dst1_r, dst2_r, dst3_r,
                            dst0_l, dst1_l, dst2_l, dst3_l);
        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst2_l, dst2_r,
                        dst1_l, dst1_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST_SW2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);

        src10_r = src32_r;
        src21_r = src43_r;
        src10_l = src32_l;
        src21_l = src43_l;
        src2 = src4;

        LD_SB2(src + 16, src_stride, src9, src10);
        src += (2 * src_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        ILVL_B2_SB(src9, src8, src10, src9, src98_l, src109_l);

        tmp2 = const_vec;
        DPADD_SB2_SH(src76_r, src98_r, filt0, filt1, tmp2, tmp2);
        tmp6 = const_vec;
        DPADD_SB2_SH(src76_l, src98_l, filt0, filt1, tmp6, tmp6);
        tmp3 = const_vec;
        DPADD_SB2_SH(src87_r, src109_r, filt0, filt1, tmp3, tmp3);
        tmp7 = const_vec;
        DPADD_SB2_SH(src87_l, src109_l, filt0, filt1, tmp7, tmp7);

        HEVC_UNIW_RND_CLIP4(tmp2, tmp3, tmp6, tmp7,
                            weight_vec, offset_vec, rnd_vec,
                            dst4_r, dst5_r, dst6_r, dst7_r,
                            dst4_l, dst5_l, dst6_l, dst7_l);

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

static void hevc_hv_uniwgt_4t_4x2_msa(uint8_t *src,
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
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v4i32 dst0_r, dst1_r;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
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
    LD_SB2(src, src_stride, src3, src4);
    XORI_B2_128_SB(src3, src4);

    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    dst32_r = __msa_ilvr_h(dst3, dst2);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_r >>= 6;

    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    dst43_r = __msa_ilvr_h(dst4, dst3);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_r >>= 6;

    MUL2(dst0_r, weight_vec, dst1_r, weight_vec, dst0_r, dst1_r);
    SRAR_W2_SW(dst0_r, dst1_r, rnd_vec);
    ADD2(dst0_r, offset_vec, dst1_r, offset_vec, dst0_r, dst1_r);
    dst0_r = CLIP_SW_0_255(dst0_r);
    dst1_r = CLIP_SW_0_255(dst1_r);

    HEVC_PCK_SW_SB2(dst1_r, dst0_r, dst0_r);
    ST4x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hv_uniwgt_4t_4x4_msa(uint8_t *src,
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
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
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

    LD_SB4(src, src_stride, src3, src4, src5, src6);
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

    HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst2_r, dst3_r,
                           weight_vec, offset_vec, rnd_vec,
                           dst0_r, dst1_r, dst2_r, dst3_r);
    HEVC_PCK_SW_SB4(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r);
    ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
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
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, dst9;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
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
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        dst32_r = __msa_ilvr_h(dst3, dst2);
        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_r >>= 6;

        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
        dst43_r = __msa_ilvr_h(dst4, dst3);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_r >>= 6;

        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
        dst54_r = __msa_ilvr_h(dst5, dst4);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_r >>= 6;

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

        HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst2_r, dst3_r,
                               weight_vec, offset_vec, rnd_vec,
                               dst0_r, dst1_r, dst2_r, dst3_r);
        HEVC_PCK_SW_SB4(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r);
        ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        HEVC_HV_UNIW_RND_CLIP4(dst4_r, dst5_r, dst6_r, dst7_r,
                               weight_vec, offset_vec, rnd_vec,
                               dst4_r, dst5_r, dst6_r, dst7_r);
        HEVC_PCK_SW_SB4(dst5_r, dst4_r, dst7_r, dst6_r, dst0_r);
        ST4x4_UB(dst0_r, dst0_r, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
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
                                  filter_x, filter_y, height, weight,
                                  offset, rnd_val);
    } else if (4 == height) {
        hevc_hv_uniwgt_4t_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter_x, filter_y, height, weight,
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
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
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
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);

        /* row 3 */
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst0_r >>= 6;
        dst0_l >>= 6;

        /* row 4 */
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
        ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst1_r >>= 6;
        dst1_l >>= 6;

        /* row 5 */
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
        ILVRL_H2_SH(dst5, dst4, dst10_r, dst10_l);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst10_l, filt_h0, filt_h1);
        dst2_r >>= 6;
        dst2_l >>= 6;

        /* row 6 */
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);
        ILVRL_H2_SH(dst2, dst5, dst21_r, dst21_l);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst21_l, filt_h0, filt_h1);
        dst3_r >>= 6;
        dst3_l >>= 6;

        HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst0_l, dst1_l,
                               weight_vec, offset_vec, rnd_vec,
                               dst0_r, dst1_r, dst0_l, dst1_l);
        HEVC_HV_UNIW_RND_CLIP4(dst2_r, dst3_r, dst2_l, dst3_l,
                               weight_vec, offset_vec, rnd_vec,
                               dst2_r, dst3_r, dst2_l, dst3_l);
        HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
        ST6x4_UB(dst0_r, dst1_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hv_uniwgt_4t_8x2_msa(uint8_t *src,
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
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
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

    LD_SB2(src, src_stride, src3, src4);
    src += (2 * src_stride);
    XORI_B2_128_SB(src3, src4);

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

    HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst0_l, dst1_l,
                           weight_vec, offset_vec, rnd_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);
    HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
    ST8x2_UB(dst0_r, dst, dst_stride);
    dst += (2 * dst_stride);
}

static void hevc_hv_uniwgt_4t_8x6_msa(uint8_t *src,
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
    v4i32 weight_vec, offset_vec, rnd_vec;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

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

    LD_SB2(src, src_stride, src3, src4);
    src += (2 * src_stride);
    XORI_B2_128_SB(src3, src4);

    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst0_r >>= 6;
    dst0_l >>= 6;

    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);
    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst1_r >>= 6;
    dst1_l >>= 6;

    LD_SB2(src, src_stride, src5, src6);
    src += (2 * src_stride);
    XORI_B2_128_SB(src5, src6);

    /* row 5 */
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);
    ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
    dst2_r >>= 6;
    dst2_l >>= 6;

    /* row 6 */
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
    dst6 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst6, dst6);
    ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);
    dst3_r >>= 6;
    dst3_l >>= 6;

    LD_SB2(src, src_stride, src7, src8);
    src += (2 * src_stride);
    XORI_B2_128_SB(src7, src8);

    /* row 7 */
    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
    dst7 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst7, dst7);
    ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst4_l = HEVC_FILT_4TAP(dst54_l, dst76_l, filt_h0, filt_h1);

    dst4_r >>= 6;
    dst4_l >>= 6;

    /* row 8 */
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec0, vec1);
    dst8 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst8, dst8);
    ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst5_l = HEVC_FILT_4TAP(dst65_l, dst87_l, filt_h0, filt_h1);
    dst5_r >>= 6;
    dst5_l >>= 6;

    HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst0_l, dst1_l,
                           weight_vec, offset_vec, rnd_vec,
                           dst0_r, dst1_r, dst0_l, dst1_l);
    HEVC_HV_UNIW_RND_CLIP4(dst2_r, dst3_r, dst2_l, dst3_l,
                           weight_vec, offset_vec, rnd_vec,
                           dst2_r, dst3_r, dst2_l, dst3_l);
    HEVC_HV_UNIW_RND_CLIP4(dst4_r, dst5_r, dst4_l, dst5_l,
                           weight_vec, offset_vec, rnd_vec,
                           dst4_r, dst5_r, dst4_l, dst5_l);
    HEVC_PCK_SW_SB12(dst0_l, dst0_r, dst1_l, dst1_r,
                     dst2_l, dst2_r, dst3_l, dst3_r,
                     dst4_l, dst4_r, dst5_l, dst5_r, dst0_r, dst1_r, dst2_r);
    ST8x4_UB(dst0_r, dst1_r, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x2_UB(dst2_r, dst, dst_stride);
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
                                              int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v4i32 weight_vec, offset_vec, rnd_vec;
    v4i32 dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    weight_vec = __msa_fill_w(weight);
    offset_vec = __msa_fill_w(offset);
    rnd_vec = __msa_fill_w(rnd_val);

    for (cnt = width >> 3; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB3(src_tmp, src_stride, src0, src1, src2);
        src_tmp += (3 * src_stride);
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
            LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
            src_tmp += (4 * src_stride);
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

            HEVC_HV_UNIW_RND_CLIP4(dst0_r, dst1_r, dst0_l, dst1_l,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst0_r, dst1_r, dst0_l, dst1_l);
            HEVC_HV_UNIW_RND_CLIP4(dst2_r, dst3_r, dst2_l, dst3_l,
                                   weight_vec, offset_vec, rnd_vec,
                                   dst2_r, dst3_r, dst2_l, dst3_l);
            HEVC_PCK_SW_SB8(dst0_l, dst0_r, dst1_l, dst1_r,
                            dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r);
            ST8x4_UB(dst0_r, dst1_r, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);
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
                                  filter_x, filter_y, height, weight,
                                  offset, rnd_val);
    } else if (6 == height) {
        hevc_hv_uniwgt_4t_8x6_msa(src, src_stride, dst, dst_stride,
                                  filter_x, filter_y, height, weight,
                                  offset, rnd_val);
    } else if (0 == (height % 4)) {
        hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                          filter_x, filter_y, height, weight,
                                          offset, rnd_val, 8);
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
    hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 8);
    hevc_hv_uniwgt_4t_4w_msa(src + 8, src_stride, dst + 8, dst_stride,
                             filter_x, filter_y, height, weight,
                             offset, rnd_val);
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
    hevc_hv_uniwgt_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                      filter_x, filter_y, height, weight,
                                      offset, rnd_val, 16);
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
                                      offset, rnd_val, 24);
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
                                      offset, rnd_val, 32);
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
                                                           ptrdiff_t          \
                                                           dst_stride,        \
                                                           uint8_t *src,      \
                                                           ptrdiff_t          \
                                                           src_stride,        \
                                                           int height,        \
                                                           int denom,         \
                                                           int weight,        \
                                                           int offset,        \
                                                           intptr_t mx,       \
                                                           intptr_t my,       \
                                                           int width)         \
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

#define UNI_W_MC_HV(PEL, DIR, WIDTH, TAP, DIR1)                              \
void ff_hevc_put_hevc_uni_w_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,       \
                                                           ptrdiff_t         \
                                                           dst_stride,       \
                                                           uint8_t *src,     \
                                                           ptrdiff_t         \
                                                           src_stride,       \
                                                           int height,       \
                                                           int denom,        \
                                                           int weight,       \
                                                           int offset,       \
                                                           intptr_t mx,      \
                                                           intptr_t my,      \
                                                           int width)        \
{                                                                            \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];                \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];                \
    int shift = denom + 14 - 8;                                              \
                                                                             \
    hevc_##DIR1##_uniwgt_##TAP##t_##WIDTH##w_msa(src, src_stride, dst,       \
                                                 dst_stride, filter_x,       \
                                                 filter_y,  height, weight,  \
                                                 offset, shift);             \
}

UNI_W_MC_HV(qpel, hv, 4, 8, hv);
UNI_W_MC_HV(qpel, hv, 8, 8, hv);
UNI_W_MC_HV(qpel, hv, 12, 8, hv);
UNI_W_MC_HV(qpel, hv, 16, 8, hv);
UNI_W_MC_HV(qpel, hv, 24, 8, hv);
UNI_W_MC_HV(qpel, hv, 32, 8, hv);
UNI_W_MC_HV(qpel, hv, 48, 8, hv);
UNI_W_MC_HV(qpel, hv, 64, 8, hv);

UNI_W_MC_HV(epel, hv, 4, 4, hv);
UNI_W_MC_HV(epel, hv, 6, 4, hv);
UNI_W_MC_HV(epel, hv, 8, 4, hv);
UNI_W_MC_HV(epel, hv, 12, 4, hv);
UNI_W_MC_HV(epel, hv, 16, 4, hv);
UNI_W_MC_HV(epel, hv, 24, 4, hv);
UNI_W_MC_HV(epel, hv, 32, 4, hv);

#undef UNI_W_MC_HV
