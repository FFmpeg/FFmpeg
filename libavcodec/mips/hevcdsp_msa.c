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

static void hevc_copy_4w_msa(uint8_t *src, int32_t src_stride,
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
        ST8x2_UB(in0, dst, 2 * dst_stride);
    } else if (4 == height) {
        v16i8 src0, src1, src2, src3;
        v8i16 in0, in1;

        LD_SB4(src, src_stride, src0, src1, src2, src3);

        ILVR_W2_SB(src1, src0, src3, src2, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, in0, in1);
        in0 <<= 6;
        in1 <<= 6;
        ST8x4_UB(in0, in1, dst, 2 * dst_stride);
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
            ST8x8_UB(in0, in1, in2, in3, dst, 2 * dst_stride);
            dst += (8 * dst_stride);
        }
    }
}

static void hevc_copy_6w_msa(uint8_t *src, int32_t src_stride,
                             int16_t *dst, int32_t dst_stride,
                             int32_t height)
{
    uint32_t loop_cnt;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
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
}

static void hevc_copy_8w_msa(uint8_t *src, int32_t src_stride,
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

static void hevc_copy_12w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    uint32_t loop_cnt;
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
        ST8x4_UB(in0, in1, dst + 8, 2 * dst_stride);
        dst += (4 * dst_stride);

        ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                   in0_r, in1_r, in2_r, in3_r);
        SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
        ILVL_W2_SB(src5, src4, src7, src6, src0, src1);
        ILVR_B2_SH(zero, src0, zero, src1, in0, in1);
        in0 <<= 6;
        in1 <<= 6;
        ST_SH4(in0_r, in1_r, in2_r, in3_r, dst, dst_stride);
        ST8x4_UB(in0, in1, dst + 8, 2 * dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_copy_16multx8mult_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       int32_t height,
                                       int32_t width)
{
    uint8_t *src_tmp;
    int16_t *dst_tmp;
    uint32_t loop_cnt, cnt;
    v16i8 zero = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 in0_r, in1_r, in2_r, in3_r;
    v8i16 in0_l, in1_l, in2_l, in3_l;

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_SB8(src_tmp, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src_tmp += (8 * src_stride);

            ILVR_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       in0_r, in1_r, in2_r, in3_r);
            ILVL_B4_SH(zero, src0, zero, src1, zero, src2, zero, src3,
                       in0_l, in1_l, in2_l, in3_l);
            SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
            SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
            ST_SH4(in0_r, in1_r, in2_r, in3_r, dst_tmp, dst_stride);
            ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst_tmp + 8), dst_stride);
            dst_tmp += (4 * dst_stride);

            ILVR_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                       in0_r, in1_r, in2_r, in3_r);
            ILVL_B4_SH(zero, src4, zero, src5, zero, src6, zero, src7,
                       in0_l, in1_l, in2_l, in3_l);
            SLLI_4V(in0_r, in1_r, in2_r, in3_r, 6);
            SLLI_4V(in0_l, in1_l, in2_l, in3_l, 6);
            ST_SH4(in0_r, in1_r, in2_r, in3_r, dst_tmp, dst_stride);
            ST_SH4(in0_l, in1_l, in2_l, in3_l, (dst_tmp + 8), dst_stride);
            dst_tmp += (4 * dst_stride);
        }

        src += 16;
        dst += 16;
    }
}

static void hevc_copy_16w_msa(uint8_t *src, int32_t src_stride,
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
        hevc_copy_16multx8mult_msa(src, src_stride, dst, dst_stride,
                                   height, 16);
    }
}

static void hevc_copy_24w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    hevc_copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 16);
    hevc_copy_8w_msa(src + 16, src_stride, dst + 16, dst_stride, height);
}

static void hevc_copy_32w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    hevc_copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 32);
}

static void hevc_copy_48w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    hevc_copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 48);
}

static void hevc_copy_64w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              int32_t height)
{
    hevc_copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 64);
}

static void hevc_hz_8t_4w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

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

        ST8x8_UB(dst0, dst1, dst2, dst3, dst, 2 * dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_8t_8w_msa(uint8_t *src, int32_t src_stride,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

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

static void hevc_hz_8t_12w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_hz_8t_8w_msa(src, src_stride, dst, dst_stride, filter, height);
    hevc_hz_8t_4w_msa(src + 8, src_stride, dst + 8, dst_stride, filter, height);
}

static void hevc_hz_8t_16w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

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
        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst4 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst4, dst4, dst4, dst4);
        VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst5 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst5, dst5, dst5, dst5);
        VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst6 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst6, dst6, dst6, dst6);
        VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst7 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst7, dst7, dst7, dst7);

        ST_SH4(dst0, dst2, dst4, dst6, dst, dst_stride);
        ST_SH4(dst1, dst3, dst5, dst7, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_8t_24w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

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
        VSHF_B4_SB(src2, src3, mask4, mask5, mask6, mask7,
                   vec0, vec1, vec2, vec3);
        dst4 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst4, dst4, dst4, dst4);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst5 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst5, dst5, dst5, dst5);

        ST_SH2(dst0, dst1, dst, 8);
        ST_SH(dst2, dst + 16);
        dst += dst_stride;
        ST_SH2(dst3, dst4, dst, 8);
        ST_SH(dst5, dst + 16);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_32w_msa(uint8_t *src, int32_t src_stride,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

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

static void hevc_hz_8t_48w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec, const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

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

        ST_SH6(dst0, dst1, dst2, dst3, dst4, dst5, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_64w_msa(uint8_t *src, int32_t src_stride,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

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

static void hevc_vt_8t_4w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter, int32_t height)
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

        ST8x8_UB(dst10, dst32, dst54, dst76, dst, 2 * dst_stride);
        dst += (8 * dst_stride);

        src2110 = src10998;
        src4332 = src12111110;
        src6554 = src14131312;
        src6 = src14;
    }
}

static void hevc_vt_8t_8w_msa(uint8_t *src, int32_t src_stride,
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

static void hevc_vt_8t_12w_msa(uint8_t *src, int32_t src_stride,
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
        ST8x4_UB(dst0_l, dst1_l, dst + 8, 2 * dst_stride);
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

static void hevc_vt_8t_16multx4mult_msa(uint8_t *src,
                                        int32_t src_stride,
                                        int16_t *dst,
                                        int32_t dst_stride,
                                        const int8_t *filter,
                                        int32_t height,
                                        int32_t width)
{
    uint8_t *src_tmp;
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

static void hevc_vt_8t_16w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 16);
}

static void hevc_vt_8t_24w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 16);
    hevc_vt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride,
                      filter, height);
}

static void hevc_vt_8t_32w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 32);
}

static void hevc_vt_8t_48w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 48);
}

static void hevc_vt_8t_64w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 64);
}

static void hevc_hv_8t_4w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
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
    v4i32 dst0_r, dst1_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v16i8 mask0 = {
        0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20
    };
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

        dst0_r = (v4i32) __msa_pckev_h((v8i16) dst1_r, (v8i16) dst0_r);
        ST8x2_UB(dst0_r, dst, (2 * dst_stride));
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

static void hevc_hv_8t_8multx2mult_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t height, int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    int16_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

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

        ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_r, dst32_r, dst54_r, dst21_r);
        ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
        ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_l, dst32_l, dst54_l, dst21_l);
        ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            LD_SB2(src_tmp, src_stride, src7, src8);
            XORI_B2_128_SB(src7, src8);
            src_tmp += 2 * src_stride;

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

            dst0_r = (v4i32) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
            ST_SW(dst0_r, dst_tmp);
            dst_tmp += dst_stride;

            VSHF_B4_SB(src8, src8, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst8 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst8, dst8, dst8, dst8);

            ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
            dst6 = dst8;
            dst0_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_l = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_r >>= 6;
            dst0_l >>= 6;

            dst0_r = (v4i32) __msa_pckev_h((v8i16) dst0_l, (v8i16) dst0_r);
            ST_SW(dst0_r, dst_tmp);
            dst_tmp += dst_stride;

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
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_8t_8w_msa(uint8_t *src, int32_t src_stride,
                              int16_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 8);
}

static void hevc_hv_8t_12w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 8);

    hevc_hv_8t_4w_msa(src + 8, src_stride, dst + 8, dst_stride,
                      filter_x, filter_y, height);
}

static void hevc_hv_8t_16w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 16);
}

static void hevc_hv_8t_24w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 24);
}

static void hevc_hv_8t_32w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 32);
}

static void hevc_hv_8t_48w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 48);
}

static void hevc_hv_8t_64w_msa(uint8_t *src, int32_t src_stride,
                               int16_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 64);
}

static void hevc_hz_4t_4x2_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

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

    ST8x2_UB(dst0, dst, 2 * dst_stride);
}

static void hevc_hz_4t_4x4_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

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

    ST8x4_UB(dst0, dst1, dst, 2 * dst_stride);
}

static void hevc_hz_4t_4x8multiple_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

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

        ST8x8_UB(dst0, dst1, dst2, dst3, dst, 2 * dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hz_4t_4w_msa(uint8_t *src,
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

static void hevc_hz_4t_6w_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v16i8 vec0, vec1;
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

static void hevc_hz_4t_8x2multiple_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1, dst0, dst1;
    v16i8 src0, src1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
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

static void hevc_hz_4t_8x4multiple_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    uint32_t loop_cnt;
    v8i16 filt0, filt1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
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

static void hevc_hz_4t_8w_msa(uint8_t *src,
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

static void hevc_hz_4t_12w_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
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
        ST8x4_UB(dst4, dst5, dst + 8, 2 * dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hz_4t_16w_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
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

static void hevc_hz_4t_24w_msa(uint8_t *src,
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
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
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

static void hevc_hz_4t_32w_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1, mask2, mask3;
    v8i16 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1;
    v8i16 filter_vec, const_vec;

    src -= 1;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

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

        ST_SH4(dst0, dst1, dst2, dst3, dst, 8);
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

        ST_SH4(dst0, dst1, dst2, dst3, dst, 8);
        dst += dst_stride;
    }
}

static void hevc_vt_4t_4x2_msa(uint8_t *src,
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

    ST8x2_UB(dst10, dst, 2 * dst_stride);
}

static void hevc_vt_4t_4x4_msa(uint8_t *src,
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

    ST8x4_UB(dst10, dst32, dst, 2 * dst_stride);
}

static void hevc_vt_4t_4x8multiple_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src2110, src4332, src6554, src8776;
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

        ST8x8_UB(dst10, dst32, dst54, dst76, dst, 2 * dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_vt_4t_4w_msa(uint8_t *src,
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
    } else if (0 == (height % 8)) {
        hevc_vt_4t_4x8multiple_msa(src, src_stride, dst, dst_stride,
                                   filter, height);
    }
}

static void hevc_vt_4t_6w_msa(uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter,
                              int32_t height)
{
    int32_t loop_cnt;
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
}

static void hevc_vt_4t_8x2_msa(uint8_t *src,
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

static void hevc_vt_4t_8x6_msa(uint8_t *src,
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

static void hevc_vt_4t_8x4multiple_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter,
                                       int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5;
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

    for (loop_cnt = (height >> 2); loop_cnt--;) {
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

        LD_SB2(src, src_stride, src5, src2);
        src += (2 * src_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        dst0_r = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst1_r, dst1_r);

        ST_SH2(dst0_r, dst1_r, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void hevc_vt_4t_8w_msa(uint8_t *src,
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

static void hevc_vt_4t_12w_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter,
                               int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332;
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

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src3, src4);
        src += (2 * src_stride);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_l, (v2i64) src32_l);
        dst0_r = const_vec;
        DPADD_SB2_SH(src10_r, src32_r, filt0, filt1, dst0_r, dst0_r);
        dst1_r = const_vec;
        DPADD_SB2_SH(src21_r, src43_r, filt0, filt1, dst1_r, dst1_r);
        dst0_l = const_vec;
        DPADD_SB2_SH(src2110, src4332, filt0, filt1, dst0_l, dst0_l);

        LD_SB2(src, src_stride, src5, src2);
        src += (2 * src_stride);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src54_l, src65_l);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src65_l, (v2i64) src54_l);
        dst2_r = const_vec;
        DPADD_SB2_SH(src32_r, src10_r, filt0, filt1, dst2_r, dst2_r);
        dst3_r = const_vec;
        DPADD_SB2_SH(src43_r, src21_r, filt0, filt1, dst3_r, dst3_r);
        dst1_l = const_vec;
        DPADD_SB2_SH(src4332, src2110, filt0, filt1, dst1_l, dst1_l);

        ST_SH4(dst0_r, dst1_r, dst2_r, dst3_r, dst, dst_stride);
        ST8x4_UB(dst0_l, dst1_l, dst + 8, (2 * dst_stride));
        dst += (4 * dst_stride);
    }
}

static void hevc_vt_4t_16w_msa(uint8_t *src,
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

static void hevc_vt_4t_24w_msa(uint8_t *src,
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

static void hevc_vt_4t_32w_msa(uint8_t *src,
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

static void hevc_hv_4t_4x2_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y)
{
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst1_r;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;

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

    dst0_r = (v4i32) __msa_pckev_h((v8i16) dst1_r, (v8i16) dst0_r);
    ST8x2_UB(dst0_r, dst, 2 * dst_stride);
}

static void hevc_hv_4t_4x4_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y)
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

    dst10_r = __msa_ilvr_h(dst5, dst4);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
    dst2_r >>= 6;

    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

    dst21_r = __msa_ilvr_h(dst2, dst5);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
    dst3_r >>= 6;

    PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst1_r);
    ST8x4_UB(dst0_r, dst1_r, dst, 2 * dst_stride);
}


static void hevc_hv_4t_4multx8mult_msa(uint8_t *src,
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
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, dst9;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;

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

        PCKEV_H4_SW(dst1_r, dst0_r, dst3_r, dst2_r,
                    dst5_r, dst4_r, dst7_r, dst6_r,
                    dst0_r, dst1_r, dst2_r, dst3_r);
        ST8x8_UB(dst0_r, dst1_r, dst2_r, dst3_r, dst, 2 * dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hv_4t_4w_msa(uint8_t *src,
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

static void hevc_hv_4t_6w_msa(uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter_x,
                              const int8_t *filter_y,
                              int32_t height)
{
    uint32_t loop_cnt;
    uint64_t dst_val0, dst_val1, dst_val2, dst_val3;
    uint32_t dst_val_int0, dst_val_int1, dst_val_int2, dst_val_int3;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
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

        PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r,
                    dst2_l, dst2_r, dst3_l, dst3_r,
                    dst0_r, dst1_r, dst2_r, dst3_r);

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
}

static void hevc_hv_4t_8x2_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
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

    PCKEV_H2_SW(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r, dst1_r);
    ST_SW2(dst0_r, dst1_r, dst, dst_stride);
}

static void hevc_hv_4t_8x6_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
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

    PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r,
                dst2_l, dst2_r, dst3_l, dst3_r, dst0_r, dst1_r, dst2_r, dst3_r);
    PCKEV_H2_SW(dst4_l, dst4_r, dst5_l, dst5_r, dst4_r, dst5_r);

    ST_SW2(dst0_r, dst1_r, dst, dst_stride);
    dst += (2 * dst_stride);
    ST_SW2(dst2_r, dst3_r, dst, dst_stride);
    dst += (2 * dst_stride);
    ST_SW2(dst4_r, dst5_r, dst, dst_stride);
}

static void hevc_hv_4t_8multx4mult_msa(uint8_t *src,
                                       int32_t src_stride,
                                       int16_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t height,
                                       int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    int16_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
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

            PCKEV_H4_SW(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r,
                        dst0_r, dst1_r, dst2_r, dst3_r);

            ST_SW2(dst0_r, dst1_r, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);
            ST_SW2(dst2_r, dst3_r, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_4t_8w_msa(uint8_t *src,
                              int32_t src_stride,
                              int16_t *dst,
                              int32_t dst_stride,
                              const int8_t *filter_x,
                              const int8_t *filter_y,
                              int32_t height)
{

    if (2 == height) {
        hevc_hv_4t_8x2_msa(src, src_stride, dst, dst_stride,
                           filter_x, filter_y, height);
    } else if (6 == height) {
        hevc_hv_4t_8x6_msa(src, src_stride, dst, dst_stride,
                           filter_x, filter_y, height);
    } else if (0 == (height % 4)) {
        hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 8);
    }
}

static void hevc_hv_4t_12w_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 8);

    hevc_hv_4t_4w_msa(src + 8, src_stride, dst + 8, dst_stride,
                      filter_x, filter_y, height);

}

static void hevc_hv_4t_16w_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 16);
}

static void hevc_hv_4t_24w_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 24);
}

static void hevc_hv_4t_32w_msa(uint8_t *src,
                               int32_t src_stride,
                               int16_t *dst,
                               int32_t dst_stride,
                               const int8_t *filter_x,
                               const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 32);
}

#define MC_COPY(WIDTH)                                                    \
void ff_hevc_put_hevc_pel_pixels##WIDTH##_8_msa(int16_t *dst,             \
                                                uint8_t *src,             \
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

#define MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                            \
void ff_hevc_put_hevc_##PEL##_##DIR##WIDTH##_8_msa(int16_t *dst,            \
                                                     uint8_t *src,          \
                                                     ptrdiff_t src_stride,  \
                                                     int height,            \
                                                     intptr_t mx,           \
                                                     intptr_t my,           \
                                                     int width)             \
{                                                                           \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];           \
                                                                            \
    hevc_##DIR1##_##TAP##t_##WIDTH##w_msa(src, src_stride, dst,             \
                                          MAX_PB_SIZE, filter, height);     \
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

#define MC_HV(PEL, DIR, WIDTH, TAP, DIR1)                                     \
void ff_hevc_put_hevc_##PEL##_##DIR##WIDTH##_8_msa(int16_t *dst,              \
                                                     uint8_t *src,            \
                                                     ptrdiff_t src_stride,    \
                                                     int height,              \
                                                     intptr_t mx,             \
                                                     intptr_t my,             \
                                                     int width)               \
{                                                                             \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];                 \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];                 \
                                                                              \
    hevc_##DIR1##_##TAP##t_##WIDTH##w_msa(src, src_stride, dst, MAX_PB_SIZE,  \
                                          filter_x, filter_y, height);        \
}

MC_HV(qpel, hv, 4, 8, hv);
MC_HV(qpel, hv, 8, 8, hv);
MC_HV(qpel, hv, 12, 8, hv);
MC_HV(qpel, hv, 16, 8, hv);
MC_HV(qpel, hv, 24, 8, hv);
MC_HV(qpel, hv, 32, 8, hv);
MC_HV(qpel, hv, 48, 8, hv);
MC_HV(qpel, hv, 64, 8, hv);

MC_HV(epel, hv, 4, 4, hv);
MC_HV(epel, hv, 6, 4, hv);
MC_HV(epel, hv, 8, 4, hv);
MC_HV(epel, hv, 12, 4, hv);
MC_HV(epel, hv, 16, 4, hv);
MC_HV(epel, hv, 24, 4, hv);
MC_HV(epel, hv, 32, 4, hv);

#undef MC_HV
