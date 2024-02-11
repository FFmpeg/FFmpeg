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

static void hevc_copy_4w_msa(const uint8_t *src, int32_t src_stride,
                             int16_t *dst, int32_t dst_stride,
                             int32_t height)
{
    v16i8 zero = { 0 };

    if (2 == height) {
        v16i8 src0, src1;
        v8i16 in0;

        LD_SB2(src, src_stride, src0, src1);

        src0 = (v16i8) __msa_ilvr_w((v4i32) src1, (v4i32) src0);
        in0 = (v8i16) __msa_ilvr_b(zero, src0);
        in0 <<= 6;
        ST_D2(in0, 0, 1, dst, dst_stride);
    } else if (4 == height) {
        v16i8 src0, src1, src2, src3;
        v8i16 in0, in1;

        LD_SB4(src, src_stride, src0, src1, src2, src3);

        ILVR_W2_SB(src1, src0, src3, src2, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, in0, in1);
        in0 <<= 6;
        in1 <<= 6;
        ST_D4(in0, in1, 0, 1, 0, 1, dst, dst_stride);
    } else if (0 == height % 8) {
        v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
        v8i16 in0, in1, in2, in3;
        uint32_t loop_cnt;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_SB8(src, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src += (8 * src_stride);

            ILVR_W4_SB(src1, src0, src3, src2, src5, src4, src7, src6,
                       src0, src1, src2, src3);
            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       in0, in1, in2, in3);
            SLLI_4V(in0, in1, in2, in3, 6);
            ST_D8(in0, in1, in2, in3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
            dst += (8 * dst_stride);
        }
    }
}

static void hevc_copy_6w_msa(const uint8_t *src, int32_t src_stride,
                             int16_t *dst, int32_t dst_stride,
                             int32_t height)
{
    uint32_t loop_cnt = (height >> 3);
    uint32_t res = height & 0x07;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    for (; loop_cnt--; ) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0, in1, in2, in3);
        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in4, in5, in6, in7);
        SLLI_4V(in0, in1, in2, in3, 6);
        SLLI_4V(in4, in5, in6, in7, 6);
        ST12x8_UB(in0, in1, in2, in3, in4, in5, in6, in7, dst, 2 * dst_stride);
        dst += (8 * dst_stride);
    }
    for (; res--; ) {
        uint64_t out0;
        uint32_t out1;
        src0 = LD_SB(src);
        src += src_stride;
        in0  = (v8i16)__msa_ilvr_b((v16i8) zero, (v16i8) src0);
        in0  = in0 << 6;
        out0 = __msa_copy_u_d((v2i64) in0, 0);
        out1 = __msa_copy_u_w((v4i32) in0, 2);
        SD(out0, dst);
        SW(out1, dst + 4);
        dst += dst_stride;
    }
}

static void hevc_copy_8w_msa(const uint8_t *src, int32_t src_stride,
                             int16_t *dst, int32_t dst_stride,
                             int32_t height)
{
    v16i8 zero = { 0 };

    if (2 == height) {
        v16i8 src0, src1;
        v8i16 in0, in1;

        LD_SB2(src, src_stride, src0, src1);

        ILVR_B2_SH(zero, src0, zero, src1, in0, in1);
        in0 <<= 6;
        in1 <<= 6;
        ST_SH2(in0, in1, dst, dst_stride);
    } else if (4 == height) {
        v16i8 src0, src1, src2, src3;
        v8i16 in0, in1, in2, in3;

        LD_SB4(src, src_stride, src0, src1, src2, src3);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0, in1, in2, in3);
        SLLI_4V(in0, in1, in2, in3, 6);
        ST_SH4(in0, in1, in2, in3, dst, dst_stride);
    } else if (6 == height) {
        v16i8 src0, src1, src2, src3, src4, src5;
        v8i16 in0, in1, in2, in3, in4, in5;

        LD_SB6(src, src_stride, src0, src1, src2, src3, src4, src5);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0, in1, in2, in3);
        ILVR_B2_SH(zero, src4, zero, src5, in4, in5);
        SLLI_4V(in0, in1, in2, in3, 6);
        in4 <<= 6;
        in5 <<= 6;
        ST_SH6(in0, in1, in2, in3, in4, in5, dst, dst_stride);
    } else if (0 == height % 8) {
        uint32_t loop_cnt;
        v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
        v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_SB8(src, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src += (8 * src_stride);

            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       in0, in1, in2, in3);
            ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                       in4, in5, in6, in7);
            SLLI_4V(in0, in1, in2, in3, 6);
            SLLI_4V(in4, in5, in6, in7, 6);
            ST_SH8(in0, in1, in2, in3, in4, in5, in6, in7, dst, dst_stride);
            dst += (8 * dst_stride);
        }
    }
}

static void hevc_copy_12w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    uint32_t loop_cnt;
    uint32_t res = height & 0x07;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0, in1, in0_r, in1_r, in2_r, in3_r;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_r, in1_r, in2_r, in3_r);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        ILVL_W2_SB(src1, src0, src3, src2, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, in0, in1);
        in0 <<= 6;
        in1 <<= 6;
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_D4(in0, in1, 0, 1, 0, 1, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in0_r, in1_r, in2_r, in3_r);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        ILVL_W2_SB(src5, src4, src7, src6, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, in0, in1);
        in0 <<= 6;
        in1 <<= 6;
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_D4(in0, in1, 0, 1, 0, 1, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
    for (; res--; ) {
        uint64_t out0;
        src0 = LD_SB(src);
        src += src_stride;
        in0_r = (v8i16)__msa_ilvr_b((v16i8) zero, (v16i8) src0);
        in0   = (v8i16)__msa_ilvl_b((v16i8) zero, (v16i8) src0);
        in0_r = in0_r << 6;
        in0   = in0 << 6;
        ST_UH(in0_r, dst);
        out0 = __msa_copy_u_d((v2i64) in0, 0);
        SD(out0, dst + 8);
        dst += dst_stride;
    }
}

static void hevc_copy_16w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    v16i8 zero = { 0 };

    if (4 == height) {
        v16i8 src0, src1, src2, src3;
        v8i16 in0_r, in1_r, in2_r, in3_r;
        v8i16 in0_l, in1_l, in2_l, in3_l;

        LD_SB4(src, src_stride, src0, src1, src2, src3);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_l, in1_l, in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
    } else if (12 == height) {
        v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
        v16i8 src8, src9, src10, src11;
        v8i16 in0_r, in1_r, in2_r, in3_r;
        v8i16 in0_l, in1_l, in2_l, in3_l;

        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);
        LD_SB4(src, src_stride, src8, src9, src10, src11);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_l, in1_l, in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
        dst += (4 * dst_stride);

        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in0_l, in1_l, in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
        dst += (4 * dst_stride);

        ILVR_B4_SH(zero, src8, zero, src9, zero, src10, zero, src11,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src8, zero, src9, zero, src10, zero, src11,
                   in0_l, in1_l, in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
    } else if (0 == (height % 8)) {
        uint32_t loop_cnt;
        v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
        v8i16 in0_r, in1_r, in2_r, in3_r, in0_l, in1_l, in2_l, in3_l;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6,
                   src7);
            src += (8 * src_stride);
            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, in0_r,
                       in1_r, in2_r, in3_r);
            ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, in0_l,
                       in1_l, in2_l, in3_l);
            SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
            SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
            ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
            ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
            dst += (4 * dst_stride);

            ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, in0_r,
                       in1_r, in2_r, in3_r);
            ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, in0_l,
                       in1_l, in2_l, in3_l);
            SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
            SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
            ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
            ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

static void hevc_copy_24w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    uint32_t loop_cnt;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0_r, in1_r, in2_r, in3_r, in0_l, in1_l, in2_l, in3_l;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        LD_SB4((src + 16), src_stride, src4, src5, src6, src7);
        src += (4 * src_stride);
        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, in0_r, in1_r,
                   in2_r, in3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, in0_l, in1_l,
                   in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst + 8), dst_stride);
        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, in0_r, in1_r,
                   in2_r, in3_r);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        ST_SH4(in0_r, in1_r, in2_r, in3_r, (dst + 16), dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_copy_32w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    uint32_t loop_cnt;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0_r, in1_r, in2_r, in3_r, in0_l, in1_l, in2_l, in3_l;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4((src + 16), src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, in0_r, in1_r,
                   in2_r, in3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3, in0_l, in1_l,
                   in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in0_l, in1_r, in1_l, dst, 8);
        dst += dst_stride;
        ST_SH4(in2_r, in2_l, in3_r, in3_l, dst, 8);
        dst += dst_stride;

        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, in0_r, in1_r,
                   in2_r, in3_r);
        ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7, in0_l, in1_l,
                   in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in0_l, in1_r, in1_l, dst, 8);
        dst += dst_stride;
        ST_SH4(in2_r, in2_l, in3_r, in3_l, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_copy_48w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    uint32_t loop_cnt;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 src8, src9, src10, src11;
    v8i16 in0_r, in1_r, in2_r, in3_r, in4_r, in5_r;
    v8i16 in0_l, in1_l, in2_l, in3_l, in4_l, in5_l;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB3(src, 16, src0, src1, src2);
        src += src_stride;
        LD_SB3(src, 16, src3, src4, src5);
        src += src_stride;
        LD_SB3(src, 16, src6, src7, src8);
        src += src_stride;
        LD_SB3(src, 16, src9, src10, src11);
        src += src_stride;

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_l, in1_l, in2_l, in3_l);
        ILVR_B2_SH(zero, src4, zero, src5, in4_r, in5_r);
        ILVL_B2_SH(zero, src4, zero, src5, in4_l, in5_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        SLLI_4V(in4_r, in5_r, in4_l, in5_l, 6);
        ST_SH6(in0_r, in0_l, in1_r, in1_l, in2_r, in2_l, dst, 8);
        dst += dst_stride;
        ST_SH6(in3_r, in3_l, in4_r, in4_l, in5_r, in5_l, dst, 8);
        dst += dst_stride;

        ILVR_B4_SH(zero, src6, zero, src7, zero, src8, zero, src9,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src6, zero, src7, zero, src8, zero, src9,
                   in0_l, in1_l, in2_l, in3_l);
        ILVR_B2_SH(zero, src10, zero, src11, in4_r, in5_r);
        ILVL_B2_SH(zero, src10, zero, src11, in4_l, in5_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        SLLI_4V(in4_r, in5_r, in4_l, in5_l, 6);
        ST_SH6(in0_r, in0_l, in1_r, in1_l, in2_r, in2_l, dst, 8);
        dst += dst_stride;
        ST_SH6(in3_r, in3_l, in4_r, in4_l, in5_r, in5_l, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_copy_64w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    uint32_t loop_cnt;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0_r, in1_r, in2_r, in3_r, in0_l, in1_l, in2_l, in3_l;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB4(src, 16, src0, src1, src2, src3);
        src += src_stride;
        LD_SB4(src, 16, src4, src5, src6, src7);
        src += src_stride;

        ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                   in0_l, in1_l, in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in0_l, in1_r, in1_l, dst, 8);
        ST_SH4(in2_r, in2_l, in3_r, in3_l, (dst + 32), 8);
        dst += dst_stride;

        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in0_r, in1_r, in2_r, in3_r);
        ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in0_l, in1_l, in2_l, in3_l);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
        ST_SH4(in0_r, in0_l, in1_r, in1_l, dst, 8);
        ST_SH4(in2_r, in2_l, in3_r, in3_l, (dst + 32), 8);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_4w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    uint32_t res = (height & 0x07) >> 1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src -= 3;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

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
        VSHF_B4_SB(src4, src5, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        VSHF_B4_SB(src6, src7, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst3 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);

        ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);
    }
    for (; res--; ) {
        LD_SB2(src, src_stride, src0, src1);
        src += 2 * src_stride;
        XORI_B2_128_SB(src0, src1);
        VSHF_B4_SB(src0, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        ST_D2(dst0, 0, 1, dst, dst_stride);
        dst += 2 * dst_stride;
    }
}

static void hevc_hz_8t_8w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= 3;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
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

        ST_SH4(dst0, dst1, dst2, dst3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_8t_12w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    int64_t res0, res1, res2, res3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 filt0, filt1, filt2, filt3, dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;

    src -= 3;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = LD_SB(ff_hevc_mask_arr + 16);
    mask5 = mask4 + 2;
    mask6 = mask4 + 4;
    mask7 = mask4 + 6;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        LD_SB4(src + 8, src_stride, src4, src5, src6, src7);
        src += (4 * src_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        dst0 = const_vec;
        dst1 = const_vec;
        dst2 = const_vec;
        dst3 = const_vec;
        dst4 = const_vec;
        dst5 = const_vec;
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
        VSHF_B2_SB(src4, src5, src6, src7, mask4, mask4, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt0, filt0, dst4, dst5);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec2, vec3);
        VSHF_B2_SB(src4, src5, src6, src7, mask5, mask5, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt1, filt1, filt1, filt1, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt1, filt1, dst4, dst5);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2, vec3);
        VSHF_B2_SB(src4, src5, src6, src7, mask6, mask6, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt2, filt2, dst4, dst5);
        VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec2, vec3);
        VSHF_B2_SB(src4, src5, src6, src7, mask7, mask7, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt3, filt3, filt3, filt3, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt3, filt3, dst4, dst5);

        res0 = __msa_copy_s_d((v2i64) dst4, 0);
        res1 = __msa_copy_s_d((v2i64) dst4, 1);
        res2 = __msa_copy_s_d((v2i64) dst5, 0);
        res3 = __msa_copy_s_d((v2i64) dst5, 1);
        ST_SH4(dst0, dst1, dst2, dst3, dst, dst_stride);
        SD4(res0, res1, res2, res3, (dst + 8), dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_8t_16w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= 3;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 8, src_stride, src1, src3);
        src += (2 * src_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        dst0 = const_vec;
        dst1 = const_vec;
        dst2 = const_vec;
        dst3 = const_vec;
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt1, filt1, filt1, filt1, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt3, filt3, filt3, filt3, dst0,
                     dst1, dst2, dst3);

        ST_SH2(dst0, dst2, dst, dst_stride);
        ST_SH2(dst1, dst3, dst + 8, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_8t_24w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= 3;
    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src += src_stride;
        LD_SB2(src, 16, src2, src3);
        src += src_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);

        dst0 = const_vec;
        dst1 = const_vec;
        dst2 = const_vec;
        dst3 = const_vec;
        dst4 = const_vec;
        dst5 = const_vec;
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask4, vec0, vec1);
        VSHF_B2_SB(src1, src1, src2, src2, mask0, mask0, vec2, vec3);
        VSHF_B2_SB(src2, src3, src3, src3, mask4, mask0, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt0, filt0, dst4, dst5);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask5, vec0, vec1);
        VSHF_B2_SB(src1, src1, src2, src2, mask1, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src3, src3, src3, mask5, mask1, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt1, filt1, filt1, filt1, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt1, filt1, dst4, dst5);
        VSHF_B2_SB(src0, src0, src0, src1, mask2, mask6, vec0, vec1);
        VSHF_B2_SB(src1, src1, src2, src2, mask2, mask2, vec2, vec3);
        VSHF_B2_SB(src2, src3, src3, src3, mask6, mask2, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt2, filt2, dst4, dst5);
        VSHF_B2_SB(src0, src0, src0, src1, mask3, mask7, vec0, vec1);
        VSHF_B2_SB(src1, src1, src2, src2, mask3, mask3, vec2, vec3);
        VSHF_B2_SB(src2, src3, src3, src3, mask7, mask3, vec4, vec5);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt3, filt3, filt3, filt3, dst0,
                     dst1, dst2, dst3);
        DPADD_SB2_SH(vec4, vec5, filt3, filt3, dst4, dst5);

        ST_SH2(dst0, dst1, dst, 8);
        ST_SH(dst2, dst + 16);
        dst += dst_stride;
        ST_SH2(dst3, dst4, dst, 8);
        ST_SH(dst5, dst + 16);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_32w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= 3;
    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src2 = LD_SB(src + 24);
        src += src_stride;
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

        ST_SH4(dst0, dst1, dst2, dst3, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_48w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= 3;
    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB3(src, 16, src0, src1, src2);
        src3 = LD_SB(src + 40);
        src += src_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);

        dst0 = const_vec;
        dst1 = const_vec;
        dst2 = const_vec;
        dst3 = const_vec;
        dst4 = const_vec;
        dst5 = const_vec;
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask4, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src2, mask0, mask4, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask5, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src2, mask1, mask5, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt1, filt1, filt1, filt1, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src0, src1, mask2, mask6, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src2, mask2, mask6, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src0, src1, mask3, mask7, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src2, mask3, mask7, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt3, filt3, filt3, filt3, dst0,
                     dst1, dst2, dst3);
        ST_SH4(dst0, dst1, dst2, dst3, dst, 8);

        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec4, vec5);
        DPADD_SB2_SH(vec4, vec5, filt0, filt0, dst4, dst5);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec4, vec5);
        DPADD_SB2_SH(vec4, vec5, filt1, filt1, dst4, dst5);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec4, vec5);
        DPADD_SB2_SH(vec4, vec5, filt2, filt2, dst4, dst5);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec4, vec5);
        DPADD_SB2_SH(vec4, vec5, filt3, filt3, dst4, dst5);
        ST_SH2(dst4, dst5, (dst + 32), 8);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= 3;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB4(src, 16, src0, src1, src2, src3);
        src4 = LD_SB(src + 56);
        src += src_stride;
        XORI_B5_128_SB(src0, src1, src2, src3, src4);

        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        ST_SH(dst0, dst);

        VSHF_B4_SB(src0, src1, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst1 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        ST_SH(dst1, dst + 8);

        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst2 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        ST_SH(dst2, dst + 16);

        VSHF_B4_SB(src1, src2, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst3 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);
        ST_SH(dst3, dst + 24);

        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst4 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst4, dst4, dst4, dst4);
        ST_SH(dst4, dst + 32);

        VSHF_B4_SB(src2, src3, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst5 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst5, dst5, dst5, dst5);
        ST_SH(dst5, dst + 40);

        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst6 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst6, dst6, dst6, dst6);
        ST_SH(dst6, dst + 48);

        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst7 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst7, dst7, dst7, dst7);
        ST_SH(dst7, dst + 56);
        dst += dst_stride;
    }
}

static void hevc_vt_8t_4w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    int32_t res = (height & 0x07) >> 1;
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

    src -= (3 * src_stride);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1,
               src10_r, src32_r, src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVR_D3_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r,
               src2110, src4332, src6554);
    XORI_B3_128_SB(src2110, src4332, src6554);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB8(src, src_stride,
               src7, src8, src9, src10, src11, src12, src13, src14);
        src += (8 * src_stride);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        ILVR_B4_SB(src11, src10, src12, src11, src13, src12, src14, src13,
                   src1110_r, src1211_r, src1312_r, src1413_r);
        ILVR_D4_SB(src87_r, src76_r, src109_r, src98_r,
                   src1211_r, src1110_r, src1413_r, src1312_r,
                   src8776, src10998, src12111110, src14131312);
        XORI_B4_128_SB(src8776, src10998, src12111110, src14131312);

        dst10 = const_vec;
        DPADD_SB4_SH(src2110, src4332, src6554, src8776,
                     filt0, filt1, filt2, filt3, dst10, dst10, dst10, dst10);
        dst32 = const_vec;
        DPADD_SB4_SH(src4332, src6554, src8776, src10998,
                     filt0, filt1, filt2, filt3, dst32, dst32, dst32, dst32);
        dst54 = const_vec;
        DPADD_SB4_SH(src6554, src8776, src10998, src12111110,
                     filt0, filt1, filt2, filt3, dst54, dst54, dst54, dst54);
        dst76 = const_vec;
        DPADD_SB4_SH(src8776, src10998, src12111110, src14131312,
                     filt0, filt1, filt2, filt3, dst76, dst76, dst76, dst76);

        ST_D8(dst10, dst32, dst54, dst76, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);

        src2110 = src10998;
        src4332 = src12111110;
        src6554 = src14131312;
        src6 = src14;
    }
    for (; res--; ) {
        LD_SB2(src, src_stride, src7, src8);
        src += 2 * src_stride;
        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        src8776 = (v16i8)__msa_ilvr_d((v2i64) src87_r, src76_r);
        src8776 = (v16i8)__msa_xori_b((v16i8) src8776, 128);
        dst10 = const_vec;
        DPADD_SB4_SH(src2110, src4332, src6554, src8776,
                     filt0, filt1, filt2, filt3, dst10, dst10, dst10, dst10);
        ST_D2(dst10, 0, 1, dst, dst_stride);
        dst += 2 * dst_stride;
        src2110 = src4332;
        src4332 = src6554;
        src6554 = src8776;
        src6    = src8;
    }
}

static void hevc_vt_8t_8w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 filter_vec, const_vec;
    v8i16 filt0, filt1, filt2, filt3;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        dst0_r = const_vec;
        DPADD_SB4_SH(src10_r, src32_r, src54_r, src76_r,
                     filt0, filt1, filt2, filt3,
                     dst0_r, dst0_r, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB4_SH(src21_r, src43_r, src65_r, src87_r,
                     filt0, filt1, filt2, filt3,
                     dst1_r, dst1_r, dst1_r, dst1_r);
        dst2_r = const_vec;
        DPADD_SB4_SH(src32_r, src54_r, src76_r, src98_r,
                     filt0, filt1, filt2, filt3,
                     dst2_r, dst2_r, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB4_SH(src43_r, src65_r, src87_r, src109_r,
                     filt0, filt1, filt2, filt3,
                     dst3_r, dst3_r, dst3_r, dst3_r);

        ST_SH4(dst0_r, dst1_r, dst2_r, dst3_r, dst, dst_stride);
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

static void hevc_vt_8t_12w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src54_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l, src109_l;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v8i16 dst0_l, dst1_l;
    v8i16 filter_vec, const_vec;
    v8i16 filt0, filt1, filt2, filt3;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        dst0_r = const_vec;
        DPADD_SB4_SH(src10_r, src32_r, src54_r, src76_r,
                     filt0, filt1, filt2, filt3,
                     dst0_r, dst0_r, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB4_SH(src21_r, src43_r, src65_r, src87_r,
                     filt0, filt1, filt2, filt3,
                     dst1_r, dst1_r, dst1_r, dst1_r);
        dst2_r = const_vec;
        DPADD_SB4_SH(src32_r, src54_r, src76_r, src98_r,
                     filt0, filt1, filt2, filt3,
                     dst2_r, dst2_r, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB4_SH(src43_r, src65_r, src87_r, src109_r,
                     filt0, filt1, filt2, filt3,
                     dst3_r, dst3_r, dst3_r, dst3_r);
        dst0_l = const_vec;
        DPADD_SB4_SH(src2110, src4332, src6554, src8776,
                     filt0, filt1, filt2, filt3,
                     dst0_l, dst0_l, dst0_l, dst0_l);
        dst1_l = const_vec;
        DPADD_SB4_SH(src4332, src6554, src8776, src10998,
                     filt0, filt1, filt2, filt3,
                     dst1_l, dst1_l, dst1_l, dst1_l);

        ST_SH4(dst0_r, dst1_r, dst2_r, dst3_r, dst, dst_stride);
        ST_D4(dst0_l, dst1_l, 0, 1, 0, 1, dst + 8, dst_stride);
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

static void hevc_vt_8t_16multx4mult_msa(const uint8_t *src,
                                        int32_t src_stride,
                                        int16_t *dst,
                                        int32_t dst_stride,
                                        const int8_t *filter,
                                        int32_t height,
                                        int32_t width)
{
    const uint8_t *src_tmp;
    int16_t *dst_tmp;
    int32_t loop_cnt, cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src54_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l, src109_l;
    v8i16 dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filter_vec, const_vec;
    v8i16 filt0, filt1, filt2, filt3;

    src -= (3 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    for (cnt = width >> 4; cnt--;) {
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

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src7, src8, src9, src10);
            src_tmp += (4 * src_stride);
            XORI_B4_128_SB(src7, src8, src9, src10);
            ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                       src76_r, src87_r, src98_r, src109_r);
            ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
                       src76_l, src87_l, src98_l, src109_l);

            dst0_r = const_vec;
            DPADD_SB4_SH(src10_r, src32_r, src54_r, src76_r,
                         filt0, filt1, filt2, filt3,
                         dst0_r, dst0_r, dst0_r, dst0_r);
            dst1_r = const_vec;
            DPADD_SB4_SH(src21_r, src43_r, src65_r, src87_r,
                         filt0, filt1, filt2, filt3,
                         dst1_r, dst1_r, dst1_r, dst1_r);
            dst2_r = const_vec;
            DPADD_SB4_SH(src32_r, src54_r, src76_r, src98_r,
                         filt0, filt1, filt2, filt3,
                         dst2_r, dst2_r, dst2_r, dst2_r);
            dst3_r = const_vec;
            DPADD_SB4_SH(src43_r, src65_r, src87_r, src109_r,
                         filt0, filt1, filt2, filt3,
                         dst3_r, dst3_r, dst3_r, dst3_r);
            dst0_l = const_vec;
            DPADD_SB4_SH(src10_l, src32_l, src54_l, src76_l,
                         filt0, filt1, filt2, filt3,
                         dst0_l, dst0_l, dst0_l, dst0_l);
            dst1_l = const_vec;
            DPADD_SB4_SH(src21_l, src43_l, src65_l, src87_l,
                         filt0, filt1, filt2, filt3,
                         dst1_l, dst1_l, dst1_l, dst1_l);
            dst2_l = const_vec;
            DPADD_SB4_SH(src32_l, src54_l, src76_l, src98_l,
                         filt0, filt1, filt2, filt3,
                         dst2_l, dst2_l, dst2_l, dst2_l);
            dst3_l = const_vec;
            DPADD_SB4_SH(src43_l, src65_l, src87_l, src109_l,
                         filt0, filt1, filt2, filt3,
                         dst3_l, dst3_l, dst3_l, dst3_l);

            ST_SH4(dst0_r, dst1_r, dst2_r, dst3_r, dst_tmp, dst_stride);
            ST_SH4(dst0_l, dst1_l, dst2_l, dst3_l, dst_tmp + 8, dst_stride);
            dst_tmp += (4 * dst_stride);

            src10_r = src54_r;
            src32_r = src76_r;
            src54_r = src98_r;
            src21_r = src65_r;
            src43_r = src87_r;
            src65_r = src109_r;
            src10_l = src54_l;
            src32_l = src76_l;
            src54_l = src98_l;
            src21_l = src65_l;
            src43_l = src87_l;
            src65_l = src109_l;
            src6 = src10;
        }

        src += 16;
        dst += 16;
    }
}

static void hevc_vt_8t_16w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 16);
}

static void hevc_vt_8t_24w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 16);
    hevc_vt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride,
                      filter, height);
}

static void hevc_vt_8t_32w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 32);
}

static void hevc_vt_8t_48w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 48);
}

static void hevc_vt_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 64);
}

static void hevc_hv_8t_4w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst97, dst108;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r, dst109_r;
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

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
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
        dst97 = const_vec;
        dst108 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst97, dst97, dst97, dst97);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                     dst108, dst108, dst108, dst108);

        dst76_r = __msa_ilvr_h(dst97, dst66);
        ILVRL_H2_SH(dst108, dst97, dst87_r, dst109_r);
        dst66 = (v8i16) __msa_splati_d((v2i64) dst97, 1);
        dst98_r = __msa_ilvr_h(dst66, dst108);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst2_r = HEVC_FILT_8TAP(dst32_r, dst54_r, dst76_r, dst98_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst3_r = HEVC_FILT_8TAP(dst43_r, dst65_r, dst87_r, dst109_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst2_r);
        ST_D4(dst0_r, dst2_r, 0, 1, 0, 1, dst, dst_stride);
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

static void hevc_hv_8t_8multx1mult_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t height, int32_t width)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src_tmp;
    int16_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v4i32 dst0_r, dst0_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= ((3 * src_stride) + 3);
    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (cnt = width >> 3; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);
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

        for (loop_cnt = height; loop_cnt--;) {
            src7 = LD_SB(src_tmp);
            src7 = (v16i8) __msa_xori_b((v16u8) src7, 128);
            src_tmp += src_stride;

            VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst7 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst7, dst7, dst7, dst7);

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

            dst0_r = (v4i32) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
            ST_SW(dst0_r, dst_tmp);
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
    }
}

static void hevc_hv_8t_8w_msa(const uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
{
    hevc_hv_8t_8multx1mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 8);
}

static void hevc_hv_8t_12w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    uint32_t loop_cnt;
    const uint8_t *src_tmp;
    int16_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 filt0, filt1, filt2, filt3, filt_h0, filt_h1, filt_h2, filt_h3;
    v8i16 filter_vec, const_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst97, dst108;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r, dst21_r, dst43_r;
    v8i16 dst65_r, dst87_r, dst109_r, dst10_l, dst32_l, dst54_l, dst76_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst2_r, dst3_r;

    src -= ((3 * src_stride) + 3);
    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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
    dst0 = const_vec;
    DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3, dst0, dst0,
                 dst0, dst0);
    dst1 = const_vec;
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3, dst1, dst1,
                 dst1, dst1);
    dst2 = const_vec;
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3, dst2,
                 dst2, dst2, dst2);
    dst3 = const_vec;
    DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2, filt3, dst3,
                 dst3, dst3, dst3);

    /* row 4 row 5 row 6 */
    VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    dst4 = const_vec;
    DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3, dst4, dst4,
                 dst4, dst4);
    dst5 = const_vec;
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3, dst5, dst5,
                 dst5, dst5);
    dst6 = const_vec;
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3, dst6,
                 dst6, dst6, dst6);

    for (loop_cnt = height; loop_cnt--;) {
        src7 = LD_SB(src_tmp);
        src7 = (v16i8) __msa_xori_b((v16u8) src7, 128);
        src_tmp += src_stride;

        VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst7 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3, dst7,
                     dst7, dst7, dst7);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
        ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst0_l = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst0_r >>= 6;
        dst0_l >>= 6;

        dst0_r = (v4i32) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
        ST_SW(dst0_r, dst_tmp);
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
    dst30 = const_vec;
    DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3, dst30,
                 dst30, dst30, dst30);
    dst41 = const_vec;
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3, dst41,
                 dst41, dst41, dst41);
    dst52 = const_vec;
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3, dst52,
                 dst52, dst52, dst52);
    dst63 = const_vec;
    DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2, filt3, dst63,
                 dst63, dst63, dst63);

    ILVRL_H2_SH(dst41, dst30, dst10_r, dst43_r);
    ILVRL_H2_SH(dst52, dst41, dst21_r, dst54_r);
    ILVRL_H2_SH(dst63, dst52, dst32_r, dst65_r);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        XORI_B4_128_SB(src7, src8, src9, src10);

        VSHF_B4_SB(src7, src9, mask4, mask5, mask6, mask7, vec0, vec1, vec2,
                   vec3);
        VSHF_B4_SB(src8, src10, mask4, mask5, mask6, mask7, vec4, vec5, vec6,
                   vec7);
        dst97 = const_vec;
        dst108 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3, dst97,
                     dst97, dst97, dst97);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3, dst108,
                     dst108, dst108, dst108);

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
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst2_r);
        ST_D4(dst0_r, dst2_r, 0, 1, 0, 1, dst, dst_stride);
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

static void hevc_hv_8t_16w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 16);
}

static void hevc_hv_8t_24w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 24);
}

static void hevc_hv_8t_32w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 32);
}

static void hevc_hv_8t_48w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 48);
}

static void hevc_hv_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 64);
}

static void hevc_hz_4t_4x2_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter)
{
    v8i16 filt0, filt1;
    v16i8 src0, src1;
    v16i8 mask1, vec0, vec1;
    v8i16 dst0;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

    ST_D2(dst0, 0, 1, dst, dst_stride);
}

static void hevc_hz_4t_4x4_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter)
{
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1, vec0, vec1;
    v8i16 dst0, dst1;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

    VSHF_B2_SB(src2, src3, src2, src3, mask0, mask1, vec0, vec1);
    dst1 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
}

static void hevc_hz_4t_4x8multiple_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 mask1, vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_4t_4w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    if (2 == height) {
        hevc_hz_4t_4x2_msa(src, src_stride, dst, dst_stride, filter);
    } else if (4 == height) {
        hevc_hz_4t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (0 == height % 8) {
        hevc_hz_4t_4x8multiple_msa(src, src_stride, dst, dst_stride,
                                   filter, height);
    }
}

static void hevc_hz_4t_6w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    uint32_t loop_cnt;
    uint64_t dst_val0, dst_val1, dst_val2, dst_val3;
    uint32_t dst_val_int0, dst_val_int1, dst_val_int2, dst_val_int3;
    v8i16 filt0, filt1, dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = 2; loop_cnt--;) {
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

        dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst1, 0);
        dst_val2 = __msa_copy_u_d((v2i64) dst2, 0);
        dst_val3 = __msa_copy_u_d((v2i64) dst3, 0);

        dst_val_int0 = __msa_copy_u_w((v4i32) dst0, 2);
        dst_val_int1 = __msa_copy_u_w((v4i32) dst1, 2);
        dst_val_int2 = __msa_copy_u_w((v4i32) dst2, 2);
        dst_val_int3 = __msa_copy_u_w((v4i32) dst3, 2);

        SD(dst_val0, dst);
        SW(dst_val_int0, dst + 4);
        dst += dst_stride;
        SD(dst_val1, dst);
        SW(dst_val_int1, dst + 4);
        dst += dst_stride;
        SD(dst_val2, dst);
        SW(dst_val_int2, dst + 4);
        dst += dst_stride;
        SD(dst_val3, dst);
        SW(dst_val_int3, dst + 4);
        dst += dst_stride;
    }
}

static void hevc_hz_4t_8x2multiple_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1, dst0, dst1;
    v16i8 src0, src1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src1);
        src += (2 * src_stride);

        XORI_B2_128_SB(src0, src1);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

        ST_SH2(dst0, dst1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_hz_4t_8x4multiple_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        ST_SH4(dst0, dst1, dst2, dst3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_4t_8w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    if (2 == height || 6 == height) {
        hevc_hz_4t_8x2multiple_msa(src, src_stride, dst, dst_stride,
                                   filter, height);
    } else {
        hevc_hz_4t_8x4multiple_msa(src, src_stride, dst, dst_stride,
                                   filter, height);
    }
}

static void hevc_hz_4t_12w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask1;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v16i8 mask3;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask2 = {
        8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
    };

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        ST_SH4(dst0, dst1, dst2, dst3, dst, dst_stride);
        ST_D4(dst4, dst5, 0, 1, 0, 1, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_4t_16w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v16i8 src4, src5, src6, src7;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        ST_SH4(dst0, dst2, dst4, dst6, dst, dst_stride);
        ST_SH4(dst1, dst3, dst5, dst7, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_4t_24w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    uint32_t loop_cnt;
    int16_t *dst_tmp = dst + 16;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1, mask00, mask11;
    v16i8 vec0, vec1;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;
    mask00 = mask0 + 8;
    mask11 = mask0 + 10;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        /* 16 width */
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 16, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

        VSHF_B2_SB(src0, src1, src0, src1, mask00, mask11, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

        VSHF_B2_SB(src2, src3, src2, src3, mask00, mask11, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        ST_SH2(dst0, dst1, dst, 8);
        dst += dst_stride;
        ST_SH2(dst2, dst3, dst, 8);
        dst += dst_stride;

        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

        VSHF_B2_SB(src4, src5, src4, src5, mask00, mask11, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

        VSHF_B2_SB(src6, src7, src6, src7, mask00, mask11, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        ST_SH2(dst0, dst1, dst, 8);
        dst += dst_stride;
        ST_SH2(dst2, dst3, dst, 8);
        dst += dst_stride;

        /* 8 width */
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec0, vec1);
        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst1 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst1, dst1);

        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        ST_SH4(dst0, dst1, dst2, dst3, dst_tmp, dst_stride);
        dst_tmp += (4 * dst_stride);
    }
}

static void hevc_hz_4t_32w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1, mask2, mask3;
    v8i16 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    mask1 = mask0 + 2;
    mask2 = mask0 + 8;
    mask3 = mask0 + 10;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src2 = LD_SB(src + 24);
        src += src_stride;

        XORI_B3_128_SB(src0, src1, src2);

        dst0 = const_vec;
        dst1 = const_vec;
        dst2 = const_vec;
        dst3 = const_vec;
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask2, vec0, vec1);
        VSHF_B2_SB(src1, src1, src2, src2, mask0, mask0, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, dst0,
                     dst1, dst2, dst3);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask3, vec0, vec1);
        VSHF_B2_SB(src1, src1, src2, src2, mask1, mask1, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt1, filt1, filt1, filt1, dst0,
                     dst1, dst2, dst3);
        ST_SH4(dst0, dst1, dst2, dst3, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_vt_4t_4x2_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src2110, src4332;
    v8i16 dst10;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);

    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    dst10 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);

    ST_D2(dst10, 0, 1, dst, dst_stride);
}

static void hevc_vt_4t_4x4_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src2110, src4332, src6554;
    v8i16 dst10, dst32;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
    ILVR_D3_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r,
               src2110, src4332, src6554);
    XORI_B3_128_SB(src2110, src4332, src6554);
    dst10 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
    dst32 = const_vec;
    DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);

    ST_D4(dst10, dst32, 0, 1, 0, 1, dst, dst_stride);
}

static void hevc_vt_4t_4x8_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v8i16 dst10, dst32, dst54, dst76;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
    src += (8 * src_stride);
    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
               src32_r, src43_r, src54_r, src65_r);
    ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9,
               src76_r, src87_r, src98_r, src109_r);
    ILVR_D4_SB(src43_r, src32_r, src65_r, src54_r, src87_r, src76_r, src109_r,
               src98_r, src4332, src6554, src8776, src10998);
    XORI_B4_128_SB(src4332, src6554, src8776, src10998);
    dst10 = const_vec;
    dst32 = const_vec;
    dst54 = const_vec;
    dst76 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
    DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);
    DPADD_SB2_SH(src6554, src8776, filt0, filt1, dst54, dst54);
    DPADD_SB2_SH(src8776, src10998, filt0, filt1, dst76, dst76);
    ST_D8(dst10, dst32, dst54, dst76, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
}

static void hevc_vt_4t_4x16_msa(const uint8_t *src, int32_t src_stride,
                                int16_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src2110, src4332, src6554, src8776;
    v16i8 src10998;
    v8i16 dst10, dst32, dst54, dst76, filt0, filt1, filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
    src += (8 * src_stride);
    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5, src32_r, src43_r,
               src54_r, src65_r);
    ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
               src87_r, src98_r, src109_r);
    ILVR_D4_SB(src43_r, src32_r, src65_r, src54_r, src87_r, src76_r, src109_r,
               src98_r, src4332, src6554, src8776, src10998);
    XORI_B4_128_SB(src4332, src6554, src8776, src10998);

    dst10 = const_vec;
    dst32 = const_vec;
    dst54 = const_vec;
    dst76 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
    DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);
    DPADD_SB2_SH(src6554, src8776, filt0, filt1, dst54, dst54);
    DPADD_SB2_SH(src8776, src10998, filt0, filt1, dst76, dst76);
    ST_D8(dst10, dst32, dst54, dst76, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
    dst += (8 * dst_stride);

    src2 = src10;
    src2110 = src10998;

    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
    src += (8 * src_stride);

    ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5, src32_r, src43_r,
               src54_r, src65_r);
    ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
               src87_r, src98_r, src109_r);
    ILVR_D4_SB(src43_r, src32_r, src65_r, src54_r, src87_r, src76_r, src109_r,
               src98_r, src4332, src6554, src8776, src10998);
    XORI_B4_128_SB(src4332, src6554, src8776, src10998);

    dst10 = const_vec;
    dst32 = const_vec;
    dst54 = const_vec;
    dst76 = const_vec;
    DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst10, dst10);
    DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst32, dst32);
    DPADD_SB2_SH(src6554, src8776, filt0, filt1, dst54, dst54);
    DPADD_SB2_SH(src8776, src10998, filt0, filt1, dst76, dst76);
    ST_D8(dst10, dst32, dst54, dst76, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
}

static void hevc_vt_4t_4w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    if (2 == height) {
        hevc_vt_4t_4x2_msa(src, src_stride, dst, dst_stride, filter);
    } else if (4 == height) {
        hevc_vt_4t_4x4_msa(src, src_stride, dst, dst_stride, filter, height);
    } else if (8 == height) {
        hevc_vt_4t_4x8_msa(src, src_stride, dst, dst_stride, filter, height);
    } else if (16 == height) {
        hevc_vt_4t_4x16_msa(src, src_stride, dst, dst_stride, filter, height);
    }
}

static void hevc_vt_4t_6w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    int32_t loop_cnt;
    int32_t res = height & 0x03;
    uint32_t dst_val_int0, dst_val_int1, dst_val_int2, dst_val_int3;
    uint64_t dst_val0, dst_val1, dst_val2, dst_val3;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);

        LD_SB2(src, src_stride, src1, src2);
        src += (2 * src_stride);
        XORI_B2_128_SB(src1, src2);
        ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);

        dst2_r = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst3_r, dst3_r);

        dst_val0 = __msa_copy_u_d((v2i64) dst0_r, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst1_r, 0);
        dst_val2 = __msa_copy_u_d((v2i64) dst2_r, 0);
        dst_val3 = __msa_copy_u_d((v2i64) dst3_r, 0);

        dst_val_int0 = __msa_copy_u_w((v4i32) dst0_r, 2);
        dst_val_int1 = __msa_copy_u_w((v4i32) dst1_r, 2);
        dst_val_int2 = __msa_copy_u_w((v4i32) dst2_r, 2);
        dst_val_int3 = __msa_copy_u_w((v4i32) dst3_r, 2);

        SD(dst_val0, dst);
        SW(dst_val_int0, dst + 4);
        dst += dst_stride;
        SD(dst_val1, dst);
        SW(dst_val_int1, dst + 4);
        dst += dst_stride;
        SD(dst_val2, dst);
        SW(dst_val_int2, dst + 4);
        dst += dst_stride;
        SD(dst_val3, dst);
        SW(dst_val_int3, dst + 4);
        dst += dst_stride;
    }
    if (res) {
        LD_SB2(src, src_stride, src3, src4);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);

        dst_val0 = __msa_copy_u_d((v2i64) dst0_r, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst1_r, 0);

        dst_val_int0 = __msa_copy_u_w((v4i32) dst0_r, 2);
        dst_val_int1 = __msa_copy_u_w((v4i32) dst1_r, 2);

        SD(dst_val0, dst);
        SW(dst_val_int0, dst + 4);
        dst += dst_stride;
        SD(dst_val1, dst);
        SW(dst_val_int1, dst + 4);
        dst += dst_stride;
    }
}

static void hevc_vt_4t_8x2_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 dst0_r, dst1_r;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    LD_SB2(src, src_stride, src3, src4);
    XORI_B2_128_SB(src3, src4);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    dst0_r = const_vec;
    DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
    dst1_r = const_vec;
    DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);

    ST_SH2(dst0_r, dst1_r, dst, dst_stride);
}

static void hevc_vt_4t_8x6_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 dst0_r, dst1_r;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    LD_SB2(src, src_stride, src3, src4);
    src += (2 * src_stride);
    XORI_B2_128_SB(src3, src4);

    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    dst0_r = const_vec;
    DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
    dst1_r = const_vec;
    DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);

    ST_SH2(dst0_r, dst1_r, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_SB2(src, src_stride, src1, src2);
    src += (2 * src_stride);
    XORI_B2_128_SB(src1, src2);

    ILVR_B2_SB(src1, src4, src2, src1, src10_r, src21_r);
    dst0_r = const_vec;
    DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst0_r, dst0_r);
    dst1_r = const_vec;
    DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst1_r, dst1_r);

    ST_SH2(dst0_r, dst1_r, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_SB2(src, src_stride, src3, src4);
    XORI_B2_128_SB(src3, src4);

    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    dst0_r = const_vec;
    DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
    dst1_r = const_vec;
    DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);

    ST_SH2(dst0_r, dst1_r, dst, dst_stride);
}

static void hevc_vt_4t_8x4multiple_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src21_r, src43_r, src54_r, src65_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
        dst0_r = const_vec;
        dst1_r = const_vec;
        dst2_r = const_vec;
        dst3_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);
        DPADD_SB2_SH(src32_r, src54_r, filt0, filt1, dst2_r, dst2_r);
        DPADD_SB2_SH(src43_r, src65_r, filt0, filt1, dst3_r, dst3_r);
        ST_SH4(dst0_r, dst1_r, dst2_r, dst3_r, dst, dst_stride);
        dst += (4 * dst_stride);

        src2 = src6;
        src10_r = src54_r;
        src21_r = src65_r;
    }
}

static void hevc_vt_4t_8w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    if (2 == height) {
        hevc_vt_4t_8x2_msa(src, src_stride, dst, dst_stride, filter);
    } else if (6 == height) {
        hevc_vt_4t_8x6_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        hevc_vt_4t_8x4multiple_msa(src, src_stride, dst, dst_stride,
                                   filter, height);
    }
}

static void hevc_vt_4t_12w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332;
    v16i8 src54_r, src65_r, src6554;
    v8i16 dst0_l, dst1_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= (1 * src_stride);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_l, (v2i64) src10_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        src += (2 * src_stride);
        LD_SB2(src, src_stride, src5, src6);
        src += (2 * src_stride);
        XORI_B2_128_SB(src3, src4);
        XORI_B2_128_SB(src5, src6);

        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_l, (v2i64) src32_l);
        ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
        ILVL_B2_SB(src5, src4, src6, src5, src54_l, src65_l);
        src6554 = (v16i8) __msa_ilvr_d((v2i64) src65_l, (v2i64) src54_l);

        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);
        dst2_r = const_vec;
        DPADD_SB2_SH(src32_r, src54_r, filt0, filt1, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB2_SH(src43_r, src65_r, filt0, filt1, dst3_r, dst3_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst0_l, dst0_l);
        dst1_l = const_vec;
        DPADD_SB2_SH(src4332, src6554, filt0, filt1, dst1_l, dst1_l);

        ST_SH4(dst0_r, dst1_r, dst2_r, dst3_r, dst, dst_stride);
        ST_D4(dst0_l, dst1_l, 0, 1, 0, 1, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        src2 = src6;
        src10_r = src54_r;
        src21_r = src65_r;
        src2110 = src6554;
    }
}

static void hevc_vt_4t_16w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v8i16 dst0_r, dst1_r, dst0_l, dst1_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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
        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, dst0_l, dst0_l);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, dst1_l, dst1_l);
        ST_SH2(dst0_r, dst0_l, dst, 8);
        dst += dst_stride;
        ST_SH2(dst1_r, dst1_l, dst, 8);
        dst += dst_stride;

        LD_SB2(src, src_stride, src5, src2);
        src += (2 * src_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);
        dst0_r = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst0_r, dst0_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src32_l, src10_l, filt0, filt1, dst0_l, dst0_l);
        dst1_r = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst1_r, dst1_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src43_l, src21_l, filt0, filt1, dst1_l, dst1_l);
        ST_SH2(dst0_r, dst0_l, dst, 8);
        dst += dst_stride;
        ST_SH2(dst1_r, dst1_l, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_vt_4t_24w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10, src11;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src21_l, src43_l;
    v8i16 dst0_l, dst1_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, dst0_l, dst0_l);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, dst1_l, dst1_l);
        dst2_r = const_vec;
        DPADD_SB2_SH(src76_r, src98_r, filt0, filt1, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB2_SH(src87_r, src109_r, filt0, filt1, dst3_r, dst3_r);

        ST_SH2(dst0_r, dst0_l, dst, 8);
        ST_SH(dst2_r, dst + 16);
        dst += dst_stride;
        ST_SH2(dst1_r, dst1_l, dst, 8);
        ST_SH(dst3_r, dst + 16);
        dst += dst_stride;

        LD_SB2(src, src_stride, src5, src2);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);

        LD_SB2(src + 16, src_stride, src11, src8);
        src += (2 * src_stride);
        XORI_B2_128_SB(src11, src8);
        ILVR_B2_SB(src11, src10, src8, src11, src76_r, src87_r);

        dst0_r = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst0_r, dst0_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src32_l, src10_l, filt0, filt1, dst0_l, dst0_l);
        dst1_r = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst1_r, dst1_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src43_l, src21_l, filt0, filt1, dst1_l, dst1_l);
        dst2_r = const_vec;
        DPADD_SB2_SH(src98_r, src76_r, filt0, filt1, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB2_SH(src109_r, src87_r, filt0, filt1, dst3_r, dst3_r);

        ST_SH2(dst0_r, dst0_l, dst, 8);
        ST_SH(dst2_r, dst + 16);
        dst += dst_stride;
        ST_SH2(dst1_r, dst1_l, dst, 8);
        ST_SH(dst3_r, dst + 16);
        dst += dst_stride;
    }
}

static void hevc_vt_4t_32w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10, src11;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src87_l, src109_l;
    v8i16 dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filt0, filt1;
    v8i16 filter_vec, const_vec;

    src -= src_stride;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        LD_SB2(src + 16, src_stride, src9, src10);
        src += (2 * src_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        ILVL_B2_SB(src9, src8, src10, src9, src98_l, src109_l);

        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src10_l, src32_l, filt0, filt1, dst0_l, dst0_l);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src21_l, src43_l, filt0, filt1, dst1_l, dst1_l);
        dst2_r = const_vec;
        DPADD_SB2_SH(src76_r, src98_r, filt0, filt1, dst2_r, dst2_r);
        dst2_l = const_vec;
        DPADD_SB2_SH(src76_l, src98_l, filt0, filt1, dst2_l, dst2_l);
        dst3_r = const_vec;
        DPADD_SB2_SH(src87_r, src109_r, filt0, filt1, dst3_r, dst3_r);
        dst3_l = const_vec;
        DPADD_SB2_SH(src87_l, src109_l, filt0, filt1, dst3_l, dst3_l);

        ST_SH4(dst0_r, dst0_l, dst2_r, dst2_l, dst, 8);
        dst += dst_stride;
        ST_SH4(dst1_r, dst1_l, dst3_r, dst3_l, dst, 8);
        dst += dst_stride;

        LD_SB2(src, src_stride, src5, src2);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);

        LD_SB2(src + 16, src_stride, src11, src8);
        src += (2 * src_stride);
        XORI_B2_128_SB(src11, src8);
        ILVR_B2_SB(src11, src10, src8, src11, src76_r, src87_r);
        ILVL_B2_SB(src11, src10, src8, src11, src76_l, src87_l);

        dst0_r = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst0_r, dst0_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src32_l, src10_l, filt0, filt1, dst0_l, dst0_l);
        dst1_r = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst1_r, dst1_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src43_l, src21_l, filt0, filt1, dst1_l, dst1_l);
        dst2_r = const_vec;
        DPADD_SB2_SH(src98_r, src76_r, filt0, filt1, dst2_r, dst2_r);
        dst2_l = const_vec;
        DPADD_SB2_SH(src98_l, src76_l, filt0, filt1, dst2_l, dst2_l);
        dst3_r = const_vec;
        DPADD_SB2_SH(src109_r, src87_r, filt0, filt1, dst3_r, dst3_r);
        dst3_l = const_vec;
        DPADD_SB2_SH(src109_l, src87_l, filt0, filt1, dst3_l, dst3_l);

        ST_SH4(dst0_r, dst0_l, dst2_r, dst2_l, dst, 8);
        dst += dst_stride;
        ST_SH4(dst1_r, dst1_l, dst3_r, dst3_l, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_hv_4t_4x2_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y)
{
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst20, dst31, dst42, dst10, dst32, dst21, dst43;
    v4i32 dst0, dst1;

    src -= (src_stride + 1);
    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    VSHF_B2_SB(src0, src2, src0, src2, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src3, src1, src3, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src4, src2, src4, mask0, mask1, vec4, vec5);

    dst20 = const_vec;
    dst31 = const_vec;
    dst42 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst20, dst20);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst31, dst31);
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst42, dst42);
    ILVRL_H2_SH(dst31, dst20, dst10, dst32);
    ILVRL_H2_SH(dst42, dst31, dst21, dst43);

    dst0 = HEVC_FILT_4TAP(dst10, dst32, filt_h0, filt_h1);
    dst1 = HEVC_FILT_4TAP(dst21, dst43, filt_h0, filt_h1);
    dst0 >>= 6;
    dst1 >>= 6;
    dst0 = (v4i32) __msa_pckev_h((v8i16) dst1, (v8i16) dst0);
    ST_D2(dst0, 0, 1, dst, dst_stride);
}

static void hevc_hv_4t_4x4_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filter_vec, const_vec;
    v8i16 dst30, dst41, dst52, dst63, dst10, dst32, dst54, dst21, dst43, dst65;
    v4i32 dst0, dst1, dst2, dst3;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B2_SB(src0, src3, src0, src3, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src4, src1, src4, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src5, src2, src5, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src6, src3, src6, mask0, mask1, vec6, vec7);

    dst30 = const_vec;
    dst41 = const_vec;
    dst52 = const_vec;
    dst63 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst30, dst30);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst41, dst41);
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst52, dst52);
    DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst63, dst63);

    ILVRL_H2_SH(dst41, dst30, dst10, dst43);
    ILVRL_H2_SH(dst52, dst41, dst21, dst54);
    ILVRL_H2_SH(dst63, dst52, dst32, dst65);

    dst0 = HEVC_FILT_4TAP(dst10, dst32, filt_h0, filt_h1);
    dst1 = HEVC_FILT_4TAP(dst21, dst43, filt_h0, filt_h1);
    dst2 = HEVC_FILT_4TAP(dst32, dst54, filt_h0, filt_h1);
    dst3 = HEVC_FILT_4TAP(dst43, dst65, filt_h0, filt_h1);
    SRA_4V(dst0, dst1, dst2, dst3, 6);
    PCKEV_H2_SW(dst1, dst0, dst3, dst2, dst0, dst2);
    ST_D4(dst0, dst2, 0, 1, 0, 1, dst, dst_stride);
}


static void hevc_hv_4t_4multx8mult_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r, dst109_r;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    src -= (src_stride + 1);
    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask0, mask1, vec2, vec3);
    dst10 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst10, dst10);
    dst21 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst21, dst21);
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

        dst73 = const_vec;
        dst84 = const_vec;
        dst95 = const_vec;
        dst106 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst73, dst73);
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst84, dst84);
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst95, dst95);
        DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst106, dst106);

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
        PCKEV_H4_SW(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    dst0, dst1, dst2, dst3);
        ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_4t_4w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter_x,
                              const int8_t *filter_y,
                              int32_t height)
{
    if (2 == height) {
        hevc_hv_4t_4x2_msa(src, src_stride, dst, dst_stride,
                           filter_x, filter_y);
    } else if (4 == height) {
        hevc_hv_4t_4x4_msa(src, src_stride, dst, dst_stride,
                           filter_x, filter_y);
    } else if (0 == (height % 8)) {
        hevc_hv_4t_4multx8mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height);
    }
}

static void hevc_hv_4t_6w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter_x,
                              const int8_t *filter_y,
                              int32_t height)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, dsth7, dsth8, dsth9;
    v8i16 dsth10, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r, dst21_r, dst43_r;
    v8i16 dst65_r, dst87_r, dst109_r, dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst98_l, dst21_l, dst43_l, dst65_l, dst87_l, dst109_l;
    v8i16 dst1021_l, dst3243_l, dst5465_l, dst7687_l, dst98109_l;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v4i32 dst0_l, dst1_l, dst2_l, dst3_l;

    src -= (src_stride + 1);
    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dsth0 = const_vec;
    dsth1 = const_vec;
    dsth2 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dsth0, dsth0);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dsth1, dsth1);
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dsth2, dsth2);

    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
    XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);

    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

    dsth3 = const_vec;
    dsth4 = const_vec;
    dsth5 = const_vec;
    dsth6 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dsth3, dsth3);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dsth4, dsth4);
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dsth5, dsth5);
    DPADD_SB2_SH(vec6, vec7, filt0, filt1, dsth6, dsth6);

    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src9, src9, src9, src9, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src10, src10, src10, src10, mask0, mask1, vec6, vec7);

    dsth7 = const_vec;
    dsth8 = const_vec;
    dsth9 = const_vec;
    dsth10 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dsth7, dsth7);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dsth8, dsth8);
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dsth9, dsth9);
    DPADD_SB2_SH(vec6, vec7, filt0, filt1, dsth10, dsth10);

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
    PCKEV_H2_SH(dst1_r, dst0_r, dst3_r, dst2_r, tmp0, tmp1);
    PCKEV_H2_SH(dst5_r, dst4_r, dst7_r, dst6_r, tmp2, tmp3);
    PCKEV_H2_SH(dst1_l, dst0_l, dst3_l, dst2_l, tmp4, tmp5);
    ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
    ST_W4(tmp4, 0, 1, 2, 3, dst + 4, dst_stride);
    dst += 4 * dst_stride;
    ST_D4(tmp2, tmp3, 0, 1, 0, 1, dst, dst_stride);
    ST_W4(tmp5, 0, 1, 2, 3, dst + 4, dst_stride);
}

static void hevc_hv_4t_8x2_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y)
{
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);
    dst3 = const_vec;
    DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst3, dst3);
    dst4 = const_vec;
    DPADD_SB2_SH(vec8, vec9, filt0, filt1, dst4, dst4);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);
    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
    PCKEV_H2_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r, dst1_r);
    ST_SW2(dst0_r, dst1_r, dst, dst_stride);
}

static void hevc_hv_4t_8multx4_msa(const uint8_t *src, int32_t src_stride,
                                   int16_t *dst, int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y, int32_t width8mult)
{
    int32_t cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, mask0, mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec, const_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (cnt = width8mult; cnt--;) {
        LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src += 8;
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

        dst0 = const_vec;
        dst1 = const_vec;
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);
        dst3 = const_vec;
        dst4 = const_vec;
        dst5 = const_vec;
        dst6 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst4, dst4);
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst5, dst5);
        DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst6, dst6);
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
        PCKEV_H2_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r, dst1_r);
        PCKEV_H2_SW(dst2_l, dst2_r, dst3_l, dst3_r, dst2_r, dst3_r);

        ST_SW4(dst0_r, dst1_r, dst2_r, dst3_r, dst, dst_stride);
        dst += 8;
    }
}

static void hevc_hv_4t_8x6_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v16i8 vec10, vec11, vec12, vec13, vec14, vec15, vec16, vec17;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst4_r, dst4_l, dst5_r, dst5_l;
    v8i16 dst10_r, dst32_r, dst10_l, dst32_l;
    v8i16 dst21_r, dst43_r, dst21_l, dst43_l;
    v8i16 dst54_r, dst54_l, dst65_r, dst65_l;
    v8i16 dst76_r, dst76_l, dst87_r, dst87_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);
    dst3 = const_vec;
    DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst3, dst3);
    dst4 = const_vec;
    DPADD_SB2_SH(vec8, vec9, filt0, filt1, dst4, dst4);
    dst5 = const_vec;
    DPADD_SB2_SH(vec10, vec11, filt0, filt1, dst5, dst5);
    dst6 = const_vec;
    DPADD_SB2_SH(vec12, vec13, filt0, filt1, dst6, dst6);
    dst7 = const_vec;
    DPADD_SB2_SH(vec14, vec15, filt0, filt1, dst7, dst7);
    dst8 = const_vec;
    DPADD_SB2_SH(vec16, vec17, filt0, filt1, dst8, dst8);

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

    PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r,
                dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r, dst2_r, dst3_r);
    PCKEV_H2_SW(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r, dst5_r);

    ST_SW2(dst0_r, dst1_r, dst, dst_stride);
    dst += (2 * dst_stride);
    ST_SW2(dst2_r, dst3_r, dst, dst_stride);
    dst += (2 * dst_stride);
    ST_SW2(dst4_r, dst5_r, dst, dst_stride);
}

static void hevc_hv_4t_8multx4mult_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t height,
                                       int32_t width8mult)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src_tmp;
    int16_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (cnt = width8mult; cnt--;) {
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
            VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
            VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
            VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

            dst3 = const_vec;
            dst4 = const_vec;
            dst5 = const_vec;
            dst6 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
            DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst4, dst4);
            DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst5, dst5);
            DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst6, dst6);

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

            PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r,
                        dst0_r, dst1_r, dst2_r, dst3_r);

            ST_SW4(dst0_r, dst1_r, dst2_r, dst3_r, dst_tmp, dst_stride);
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

static void hevc_hv_4t_8w_msa(const uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter_x,
                              const int8_t *filter_y,
                              int32_t height)
{

    if (2 == height) {
        hevc_hv_4t_8x2_msa(src, src_stride, dst, dst_stride,
                           filter_x, filter_y);
    } else if (4 == height) {
        hevc_hv_4t_8multx4_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, 1);
    } else if (6 == height) {
        hevc_hv_4t_8x6_msa(src, src_stride, dst, dst_stride,
                           filter_x, filter_y);
    } else if (0 == (height % 4)) {
        hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 1);
    }
}

static void hevc_hv_4t_12w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    uint32_t loop_cnt;
    const uint8_t *src_tmp;
    int16_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0, mask1, mask2, mask3;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec, const_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst10, dst21, dst22, dst73;
    v8i16 dst84, dst95, dst106, dst76_r, dst98_r, dst87_r, dst109_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    src_tmp = src;
    dst_tmp = dst;

    LD_SB3(src_tmp, src_stride, src0, src1, src2);
    src_tmp += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    dst1 = const_vec;
    dst2 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
        src_tmp += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

        dst3 = const_vec;
        dst4 = const_vec;
        dst5 = const_vec;
        dst6 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst4, dst4);
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst5, dst5);
        DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst6, dst6);

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
        PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, dst0_r, dst1_r, dst2_r, dst3_r);
        ST_SW4(dst0_r, dst1_r, dst2_r, dst3_r, dst_tmp, dst_stride);
        dst_tmp += (4 * dst_stride);

        dst10_r = dst54_r;
        dst10_l = dst54_l;
        dst21_r = dst65_r;
        dst21_l = dst65_l;
        dst2 = dst6;
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
    dst10 = const_vec;
    dst21 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst10, dst10);
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst21, dst21);
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

        dst73 = const_vec;
        dst84 = const_vec;
        dst95 = const_vec;
        dst106 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst73, dst73);
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst84, dst84);
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst95, dst95);
        DPADD_SB2_SH(vec6, vec7, filt0, filt1, dst106, dst106);

        dst32_r = __msa_ilvr_h(dst73, dst22);
        ILVRL_H2_SH(dst84, dst73, dst43_r, dst87_r);
        ILVRL_H2_SH(dst95, dst84, dst54_r, dst98_r);
        ILVRL_H2_SH(dst106, dst95, dst65_r, dst109_r);
        dst22 = (v8i16) __msa_splati_d((v2i64) dst73, 1);
        dst76_r = __msa_ilvr_h(dst22, dst106);

        tmp0 = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        tmp1 = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        tmp2 = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        tmp3 = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        tmp4 = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        tmp5 = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        tmp6 = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
        tmp7 = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);

        SRA_4V(tmp0, tmp1, tmp2, tmp3, 6);
        SRA_4V(tmp4, tmp5, tmp6, tmp7, 6);
        PCKEV_H4_SW(tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6, tmp0, tmp1,
                    tmp2, tmp3);
        ST_D8(tmp0, tmp1, tmp2, tmp3, 0, 1, 0, 1, 0, 1, 0, 1, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_4t_16w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    if (4 == height) {
        hevc_hv_4t_8multx4_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, 2);
    } else {
        hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 2);
    }
}

static void hevc_hv_4t_24w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 3);
}

static void hevc_hv_4t_32w_msa(const uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 4);
}

#define MC_COPY(WIDTH)                                                    \
void ff_hevc_put_hevc_pel_pixels##WIDTH##_8_msa(int16_t *dst,             \
                                                const uint8_t *src,       \
                                                ptrdiff_t src_stride,     \
                                                int height,               \
                                                intptr_t mx,              \
                                                intptr_t my,              \
                                                int width)                \
{                                                                         \
    hevc_copy_##WIDTH##w_msa(src, src_stride, dst, MAX_PB_SIZE, height);  \
}

MC_COPY(4);
MC_COPY(6);
MC_COPY(8);
MC_COPY(12);
MC_COPY(16);
MC_COPY(24);
MC_COPY(32);
MC_COPY(48);
MC_COPY(64);

#undef MC_COPY

#define MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                          \
void ff_hevc_put_hevc_##PEL##_##DIR##WIDTH##_8_msa(int16_t *dst,          \
                                                   const uint8_t *src,    \
                                                   ptrdiff_t src_stride,  \
                                                   int height,            \
                                                   intptr_t mx,           \
                                                   intptr_t my,           \
                                                   int width)             \
{                                                                         \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR];             \
                                                                          \
    hevc_##DIR1##_##TAP##t_##WIDTH##w_msa(src, src_stride, dst,           \
                                          MAX_PB_SIZE, filter, height);   \
}

MC(qpel, h, 4, 8, hz, mx);
MC(qpel, h, 8, 8, hz, mx);
MC(qpel, h, 12, 8, hz, mx);
MC(qpel, h, 16, 8, hz, mx);
MC(qpel, h, 24, 8, hz, mx);
MC(qpel, h, 32, 8, hz, mx);
MC(qpel, h, 48, 8, hz, mx);
MC(qpel, h, 64, 8, hz, mx);

MC(qpel, v, 4, 8, vt, my);
MC(qpel, v, 8, 8, vt, my);
MC(qpel, v, 12, 8, vt, my);
MC(qpel, v, 16, 8, vt, my);
MC(qpel, v, 24, 8, vt, my);
MC(qpel, v, 32, 8, vt, my);
MC(qpel, v, 48, 8, vt, my);
MC(qpel, v, 64, 8, vt, my);

MC(epel, h, 4, 4, hz, mx);
MC(epel, h, 6, 4, hz, mx);
MC(epel, h, 8, 4, hz, mx);
MC(epel, h, 12, 4, hz, mx);
MC(epel, h, 16, 4, hz, mx);
MC(epel, h, 24, 4, hz, mx);
MC(epel, h, 32, 4, hz, mx);

MC(epel, v, 4, 4, vt, my);
MC(epel, v, 6, 4, vt, my);
MC(epel, v, 8, 4, vt, my);
MC(epel, v, 12, 4, vt, my);
MC(epel, v, 16, 4, vt, my);
MC(epel, v, 24, 4, vt, my);
MC(epel, v, 32, 4, vt, my);

#undef MC

#define MC_HV(PEL, WIDTH, TAP)                                          \
void ff_hevc_put_hevc_##PEL##_hv##WIDTH##_8_msa(int16_t *dst,           \
                                                const uint8_t *src,     \
                                                ptrdiff_t src_stride,   \
                                                int height,             \
                                                intptr_t mx,            \
                                                intptr_t my,            \
                                                int width)              \
{                                                                       \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx];               \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my];               \
                                                                        \
    hevc_hv_##TAP##t_##WIDTH##w_msa(src, src_stride, dst, MAX_PB_SIZE,  \
                                          filter_x, filter_y, height);  \
}

MC_HV(qpel, 4, 8);
MC_HV(qpel, 8, 8);
MC_HV(qpel, 12, 8);
MC_HV(qpel, 16, 8);
MC_HV(qpel, 24, 8);
MC_HV(qpel, 32, 8);
MC_HV(qpel, 48, 8);
MC_HV(qpel, 64, 8);

MC_HV(epel, 4, 4);
MC_HV(epel, 6, 4);
MC_HV(epel, 8, 4);
MC_HV(epel, 12, 4);
MC_HV(epel, 16, 4);
MC_HV(epel, 24, 4);
MC_HV(epel, 32, 4);

#undef MC_HV
