/*
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Lu Wang <wanglu@loongson.cn>
 *                Hao Chen <chenhao@loongson.cn>
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

#include "libavutil/loongarch/loongson_intrinsics.h"
#include "hevcdsp_lsx.h"

static const uint8_t ff_hevc_mask_arr[16 * 2] __attribute__((aligned(0x40))) = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20
};

static av_always_inline __m128i
hevc_bi_rnd_clip(__m128i in0, __m128i vec0, __m128i in1, __m128i vec1)
{
    __m128i out;

    vec0 = __lsx_vsadd_h(in0, vec0);
    vec1 = __lsx_vsadd_h(in1, vec1);
    out  = __lsx_vssrarni_bu_h(vec1, vec0, 7);
    return out;
}

/* hevc_bi_copy: dst = av_clip_uint8((src0 << 6 + src1) >> 7) */
static
void hevc_bi_copy_4w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                         const int16_t *src1_ptr, int32_t src2_stride,
                         uint8_t *dst, int32_t dst_stride, int32_t height)
{
    int32_t loop_cnt = height >> 3;
    int32_t res = (height & 0x07) >> 1;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_2x = (src2_stride << 1);
    int32_t src2_stride_4x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride;
    __m128i src0, src1;
    __m128i zero = __lsx_vldi(0);
    __m128i in0, in1, in2, in3;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i reg0, reg1, reg2, reg3;
    __m128i dst0, dst1, dst2, dst3;

    for (;loop_cnt--;) {
        reg0 = __lsx_vldrepl_w(src0_ptr, 0);
        reg1 = __lsx_vldrepl_w(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_w(src0_ptr + src_stride_2x, 0);
        reg3 = __lsx_vldrepl_w(src0_ptr + src_stride_3x, 0);
        src0_ptr += src_stride_4x;
        DUP2_ARG2(__lsx_vilvl_w, reg1, reg0, reg3, reg2, tmp0, tmp1);
        src0 = __lsx_vilvl_d(tmp1, tmp0);
        reg0 = __lsx_vldrepl_w(src0_ptr, 0);
        reg1 = __lsx_vldrepl_w(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_w(src0_ptr + src_stride_2x, 0);
        reg3 = __lsx_vldrepl_w(src0_ptr + src_stride_3x, 0);
        DUP2_ARG2(__lsx_vilvl_w, reg1, reg0, reg3, reg2, tmp0, tmp1);
        src1 = __lsx_vilvl_d(tmp1, tmp0);
        src0_ptr += src_stride_4x;

        tmp0 = __lsx_vldrepl_d(src1_ptr, 0);
        tmp1 = __lsx_vldrepl_d(src1_ptr + src2_stride, 0);
        tmp2 = __lsx_vldrepl_d(src1_ptr + src2_stride_2x, 0);
        tmp3 = __lsx_vldrepl_d(src1_ptr + src2_stride_3x, 0);
        src1_ptr += src2_stride_4x;
        DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, in0, in1);
        tmp0 = __lsx_vldrepl_d(src1_ptr, 0);
        tmp1 = __lsx_vldrepl_d(src1_ptr + src2_stride, 0);
        tmp2 = __lsx_vldrepl_d(src1_ptr + src2_stride_2x, 0);
        tmp3 = __lsx_vldrepl_d(src1_ptr + src2_stride_3x, 0);
        src1_ptr += src2_stride_4x;
        DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, in2, in3);
        DUP2_ARG2(__lsx_vsllwil_hu_bu, src0, 6, src1, 6, dst0, dst2);
        DUP2_ARG2(__lsx_vilvh_b, zero, src0, zero, src1, dst1, dst3);
        DUP2_ARG2(__lsx_vslli_h, dst1, 6, dst3, 6, dst1, dst3);
        dst0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        dst1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        __lsx_vstelm_w(dst0, dst, 0, 0);
        __lsx_vstelm_w(dst0, dst + dst_stride, 0, 1);
        __lsx_vstelm_w(dst0, dst + dst_stride_2x, 0, 2);
        __lsx_vstelm_w(dst0, dst + dst_stride_3x, 0, 3);
        dst += dst_stride_4x;
        __lsx_vstelm_w(dst1, dst, 0, 0);
        __lsx_vstelm_w(dst1, dst + dst_stride, 0, 1);
        __lsx_vstelm_w(dst1, dst + dst_stride_2x, 0, 2);
        __lsx_vstelm_w(dst1, dst + dst_stride_3x, 0, 3);
        dst += dst_stride_4x;
    }
    for(;res--;) {
        reg0 = __lsx_vldrepl_w(src0_ptr, 0);
        reg1 = __lsx_vldrepl_w(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_d(src1_ptr, 0);
        reg3 = __lsx_vldrepl_d(src1_ptr + src2_stride, 0);
        src0 = __lsx_vilvl_w(reg1, reg0);
        in0  = __lsx_vilvl_d(reg3, reg2);
        dst0 = __lsx_vsllwil_hu_bu(src0, 6);
        dst0 = __lsx_vsadd_h(dst0, in0);
        dst0 = __lsx_vssrarni_bu_h(dst0, dst0, 7);
        __lsx_vstelm_w(dst0, dst, 0, 0);
        __lsx_vstelm_w(dst0, dst + dst_stride, 0, 1);
        src0_ptr += src_stride_2x;
        src1_ptr += src2_stride_2x;
        dst += dst_stride_2x;
    }
}

static
void hevc_bi_copy_6w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                         const int16_t *src1_ptr, int32_t src2_stride,
                         uint8_t *dst, int32_t dst_stride, int32_t height)
{
    int32_t loop_cnt;
    int32_t res = (height & 0x07) >> 1;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    __m128i out0, out1, out2, out3;
    __m128i zero = __lsx_vldi(0);
    __m128i src0, src1, src2, src3;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    __m128i reg0, reg1, reg2, reg3;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        reg0 = __lsx_vldrepl_d(src0_ptr, 0);
        reg1 = __lsx_vldrepl_d(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_d(src0_ptr + src_stride_2x, 0);
        reg3 = __lsx_vldrepl_d(src0_ptr + src_stride_3x, 0);
        DUP2_ARG2(__lsx_vilvl_d, reg1, reg0, reg3, reg2, src0, src1);
        src0_ptr += src_stride_4x;
        reg0 = __lsx_vldrepl_d(src0_ptr, 0);
        reg1 = __lsx_vldrepl_d(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_d(src0_ptr + src_stride_2x, 0);
        reg3 = __lsx_vldrepl_d(src0_ptr + src_stride_3x, 0);
        DUP2_ARG2(__lsx_vilvl_d, reg1, reg0, reg3, reg2, src2, src3);
        src0_ptr += src_stride_4x;
        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        in4 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in5, in6);
        in7 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        DUP4_ARG2(__lsx_vsllwil_hu_bu, src0, 6, src1, 6, src2, 6, src3, 6,
                  dst0, dst2, dst4, dst6);
        DUP4_ARG2(__lsx_vilvh_b, zero, src0, zero, src1, zero, src2, zero, src3,
                  dst1, dst3, dst5, dst7);
        DUP4_ARG2(__lsx_vslli_h, dst1, 6, dst3, 6, dst5, 6, dst7, 6, dst1, dst3,
                  dst5, dst7);
        out0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        out1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        out2 = hevc_bi_rnd_clip(in4, dst4, in5, dst5);
        out3 = hevc_bi_rnd_clip(in6, dst6, in7, dst7);
        __lsx_vstelm_w(out0, dst, 0, 0);
        __lsx_vstelm_w(out0, dst + dst_stride, 0, 2);
        __lsx_vstelm_h(out0, dst, 4, 2);
        __lsx_vstelm_h(out0, dst + dst_stride, 4, 6);
        __lsx_vstelm_w(out1, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_w(out1, dst + dst_stride_3x, 0, 2);
        __lsx_vstelm_h(out1, dst + dst_stride_2x, 4, 2);
        __lsx_vstelm_h(out1, dst + dst_stride_3x, 4, 6);
        dst += dst_stride_4x;
        __lsx_vstelm_w(out2, dst, 0, 0);
        __lsx_vstelm_w(out2, dst + dst_stride, 0, 2);
        __lsx_vstelm_h(out2, dst, 4, 2);
        __lsx_vstelm_h(out2, dst + dst_stride, 4, 6);
        __lsx_vstelm_w(out3, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_w(out3, dst + dst_stride_3x, 0, 2);
        __lsx_vstelm_h(out3, dst + dst_stride_2x, 4, 2);
        __lsx_vstelm_h(out3, dst + dst_stride_3x, 4, 6);
        dst += dst_stride_4x;
    }
    for (;res--;) {
        reg0 = __lsx_vldrepl_d(src0_ptr, 0);
        reg1 = __lsx_vldrepl_d(src0_ptr + src_stride, 0);
        src0 = __lsx_vilvl_d(reg1, reg0);
        src0_ptr += src_stride_2x;
        in0 = __lsx_vld(src1_ptr, 0);
        in1 = __lsx_vldx(src1_ptr, src2_stride_x);
        src1_ptr += src2_stride_x;
        dst0 = __lsx_vsllwil_hu_bu(src0, 6);
        dst1 = __lsx_vilvh_b(zero, src0);
        dst1 = __lsx_vslli_h(dst1, 6);
        out0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        __lsx_vstelm_w(out0, dst, 0, 0);
        __lsx_vstelm_h(out0, dst, 4, 2);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 2);
        __lsx_vstelm_h(out0, dst, 4, 6);
        dst += dst_stride;
    }
}

static
void hevc_bi_copy_8w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                         const int16_t *src1_ptr, int32_t src2_stride,
                         uint8_t *dst, int32_t dst_stride, int32_t height)
{
    int32_t loop_cnt = height >> 3;
    int32_t res = (height & 7) >> 1;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    __m128i out0, out1, out2, out3;
    __m128i src0, src1, src2, src3;
    __m128i zero = __lsx_vldi(0);
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    __m128i reg0, reg1, reg2, reg3;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        reg0 = __lsx_vldrepl_d(src0_ptr, 0);
        reg1 = __lsx_vldrepl_d(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_d(src0_ptr + src_stride_2x, 0);
        reg3 = __lsx_vldrepl_d(src0_ptr + src_stride_3x, 0);
        DUP2_ARG2(__lsx_vilvl_d, reg1, reg0, reg3, reg2, src0, src1);
        src0_ptr += src_stride_4x;
        reg0 = __lsx_vldrepl_d(src0_ptr, 0);
        reg1 = __lsx_vldrepl_d(src0_ptr + src_stride, 0);
        reg2 = __lsx_vldrepl_d(src0_ptr + src_stride_2x, 0);
        reg3 = __lsx_vldrepl_d(src0_ptr + src_stride_3x, 0);
        DUP2_ARG2(__lsx_vilvl_d, reg1, reg0, reg3, reg2, src2, src3);
        src0_ptr += src_stride_4x;
        DUP4_ARG2(__lsx_vsllwil_hu_bu, src0, 6, src1, 6, src2, 6, src3, 6,
                  dst0, dst2, dst4, dst6);
        DUP4_ARG2(__lsx_vilvh_b, zero, src0, zero, src1, zero, src2, zero,
                  src3, dst1, dst3, dst5, dst7);
        DUP4_ARG2(__lsx_vslli_h, dst1, 6, dst3, 6, dst5, 6, dst7, 6, dst1,
                  dst3, dst5, dst7);
        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        in4 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in5, in6);
        in7 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        out0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        out1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        out2 = hevc_bi_rnd_clip(in4, dst4, in5, dst5);
        out3 = hevc_bi_rnd_clip(in6, dst6, in7, dst7);
        __lsx_vstelm_d(out0, dst, 0, 0);
        __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(out1, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(out1, dst + dst_stride_3x, 0, 1);
        dst += dst_stride_4x;
        __lsx_vstelm_d(out2, dst, 0, 0);
        __lsx_vstelm_d(out2, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(out3, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(out3, dst + dst_stride_3x, 0, 1);
        dst += dst_stride_4x;
    }
    for (;res--;) {
        reg0 = __lsx_vldrepl_d(src0_ptr, 0);
        reg1 = __lsx_vldrepl_d(src0_ptr + src_stride, 0);
        src0 = __lsx_vilvl_d(reg1, reg0);
        in0  = __lsx_vld(src1_ptr, 0);
        in1  = __lsx_vldx(src1_ptr, src2_stride_x);
        dst0 = __lsx_vsllwil_hu_bu(src0, 6);
        dst1 = __lsx_vilvh_b(zero, src0);
        dst1 = __lsx_vslli_h(dst1, 6);
        out0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
        src0_ptr += src_stride_2x;
        src1_ptr += src2_stride_x;
        dst += dst_stride_2x;
    }
}

static
void hevc_bi_copy_12w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                          const int16_t *src1_ptr, int32_t src2_stride,
                          uint8_t *dst, int32_t dst_stride, int32_t height)
{
    uint32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    const int16_t *_src1 = src1_ptr + 8;
    __m128i out0, out1, out2;
    __m128i src0, src1, src2, src3;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5;

    for (loop_cnt = 4; loop_cnt--;) {
        src0 = __lsx_vld(src0_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
                  src1, src2);
        src3 = __lsx_vldx(src0_ptr, src_stride_3x);
        src0_ptr += src_stride_4x;
        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        in4 = __lsx_vld(_src1, 0);
        DUP2_ARG2(__lsx_vldx, _src1, src2_stride_x, _src1, src2_stride_2x,
                  in5, in6);
        in7 = __lsx_vldx(_src1, src2_stride_3x);
        _src1 += src2_stride_2x;

        DUP2_ARG2(__lsx_vilvl_d, in5, in4, in7, in6, in4, in5);
        DUP4_ARG2(__lsx_vsllwil_hu_bu, src0, 6, src1, 6, src2, 6, src3, 6,
                  dst0, dst1, dst2, dst3)
        DUP2_ARG2(__lsx_vilvh_w, src1, src0, src3, src2, src0, src1);
        DUP2_ARG2(__lsx_vsllwil_hu_bu, src0, 6, src1, 6, dst4, dst5)
        out0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        out1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        out2 = hevc_bi_rnd_clip(in4, dst4, in5, dst5);
        __lsx_vstelm_d(out0, dst, 0, 0);
        __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(out1, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(out1, dst + dst_stride_3x, 0, 1);
        __lsx_vstelm_w(out2, dst, 8, 0);
        __lsx_vstelm_w(out2, dst + dst_stride, 8, 1);
        __lsx_vstelm_w(out2, dst + dst_stride_2x, 8, 2);
        __lsx_vstelm_w(out2, dst + dst_stride_3x, 8, 3);
        dst += dst_stride_4x;
    }
}

static
void hevc_bi_copy_16w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                          const int16_t *src1_ptr, int32_t src2_stride,
                          uint8_t *dst, int32_t dst_stride, int32_t height)
{
    uint32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    const int16_t *_src1 = src1_ptr + 8;
    __m128i out0, out1, out2, out3;
    __m128i src0, src1, src2, src3;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, dst2_l, dst3_l;
    __m128i zero = {0};

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        src0 = __lsx_vld(src0_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
                  src1, src2);
        src3 = __lsx_vldx(src0_ptr, src_stride_3x);
        src0_ptr += src_stride_4x;
        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        in4 = __lsx_vld(_src1, 0);
        DUP2_ARG2(__lsx_vldx, _src1, src2_stride_x, _src1, src2_stride_2x,
                  in5, in6);
        in7 = __lsx_vldx(_src1, src2_stride_3x);
        _src1 += src2_stride_2x;
        DUP4_ARG2(__lsx_vsllwil_hu_bu, src0, 6, src1, 6, src2, 6, src3, 6,
                  dst0_r, dst1_r, dst2_r, dst3_r)
        DUP4_ARG2(__lsx_vilvh_b, zero, src0, zero, src1, zero, src2, zero, src3,
                  dst0_l, dst1_l, dst2_l, dst3_l);
        DUP4_ARG2(__lsx_vslli_h, dst0_l, 6, dst1_l, 6, dst2_l, 6, dst3_l, 6,
                  dst0_l, dst1_l, dst2_l, dst3_l);

        out0 = hevc_bi_rnd_clip(in0, dst0_r, in4, dst0_l);
        out1 = hevc_bi_rnd_clip(in1, dst1_r, in5, dst1_l);
        out2 = hevc_bi_rnd_clip(in2, dst2_r, in6, dst2_l);
        out3 = hevc_bi_rnd_clip(in3, dst3_r, in7, dst3_l);
        __lsx_vst(out0, dst, 0);
        __lsx_vstx(out1, dst, dst_stride);
        __lsx_vstx(out2, dst, dst_stride_2x);
        __lsx_vstx(out3, dst, dst_stride_3x);
        dst += dst_stride_4x;
    }
}

static
void hevc_bi_copy_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                          const int16_t *src1_ptr, int32_t src2_stride,
                          uint8_t *dst, int32_t dst_stride, int32_t height)
{
    hevc_bi_copy_16w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                         dst, dst_stride, height);
    hevc_bi_copy_8w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                         dst + 16, dst_stride, height);
}

static
void hevc_bi_copy_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                          const int16_t *src1_ptr, int32_t src2_stride,
                          uint8_t *dst, int32_t dst_stride, int32_t height)
{
    hevc_bi_copy_16w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                         dst, dst_stride, height);
    hevc_bi_copy_16w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                         dst + 16, dst_stride, height);
}

static
void hevc_bi_copy_48w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                          const int16_t *src1_ptr, int32_t src2_stride,
                          uint8_t *dst, int32_t dst_stride, int32_t height)
{
    hevc_bi_copy_16w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                         dst, dst_stride, height);
    hevc_bi_copy_32w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                         dst + 16, dst_stride, height);
}

static
void hevc_bi_copy_64w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                          const int16_t *src1_ptr, int32_t src2_stride,
                          uint8_t *dst, int32_t dst_stride, int32_t height)
{
    hevc_bi_copy_32w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                         dst, dst_stride, height);
    hevc_bi_copy_32w_lsx(src0_ptr + 32, src_stride, src1_ptr + 32, src2_stride,
                         dst + 32, dst_stride, height);
}

static void hevc_hz_8t_16w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr,  int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i src0, src1, src2, src3;
    __m128i filt0, filt1, filt2, filt3;
    __m128i mask1, mask2, mask3;
    __m128i vec0, vec1, vec2, vec3;
    __m128i dst0, dst1, dst2, dst3;
    __m128i in0, in1, in2, in3;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);

    src0_ptr -= 3;
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filt0, filt1, filt2, filt3);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 8, src0, src1);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 8, src2, src3);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in0, in1);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in2, in3);
        src1_ptr += src2_stride;

        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src1, mask0,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src3, src3, mask0,
                  vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, dst0, dst1, dst2, dst3);
        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src1, mask1,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask1, src3, src3, mask1,
                  vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec0, filt1, dst1, vec1, filt1,
                  dst2, vec2, filt1, dst3, vec3, filt1, dst0, dst1, dst2, dst3);
        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask2, src1, src1, mask2,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask2, src3, src3, mask2,
                  vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec0, filt2, dst1, vec1, filt2,
                  dst2, vec2, filt2, dst3, vec3, filt2, dst0, dst1, dst2, dst3);
        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask3, src1, src1, mask3,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask3, src3, src3, mask3,
                  vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec0, filt3, dst1, vec1, filt3,
                  dst2, vec2, filt3, dst3, vec3, filt3, dst0, dst1, dst2, dst3);

        dst0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        dst1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        __lsx_vst(dst0, dst, 0);
        __lsx_vstx(dst1, dst, dst_stride);
        dst += dst_stride_2x;
    }
}

static void hevc_hz_8t_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    __m128i src0, src1, tmp0, tmp1;
    __m128i filt0, filt1, filt2, filt3;
    __m128i mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    __m128i vec0, vec1, vec2, vec3;
    __m128i dst0, dst1, dst2;
    __m128i in0, in1, in2;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);

    src0_ptr -= 3;
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filt0, filt1, filt2, filt3);

    DUP4_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask0, 6, mask0, 8, mask1,
              mask2, mask3, mask4);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 10, mask0, 12, mask5, mask6);
    mask7 = __lsx_vaddi_bu(mask0, 14);

    for (loop_cnt = height; loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src0, src1);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in0, in1);
        in2 = __lsx_vld(src1_ptr, 32);
        src1_ptr += src2_stride;

        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src0, mask4, src1,
                  src1, mask0, src0, src0, mask1, vec0, vec1, vec2, vec3);
        DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, dst0, dst1);
        dst2 = __lsx_vdp2_h_bu_b(vec2, filt0);
        dst0 = __lsx_vdp2add_h_bu_b(dst0, vec3, filt1);
        DUP4_ARG3(__lsx_vshuf_b, src1, src0, mask5, src1, src1, mask1, src0,
                  src0, mask2, src1, src0, mask6, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst1, vec0, filt1, dst2, vec1, filt1,
                  dst0, vec2, filt2, dst1, vec3, filt2, dst1, dst2, dst0, dst1);
        DUP4_ARG3(__lsx_vshuf_b, src1, src1, mask2, src0, src0, mask3, src1, src0,
                  mask7, src1, src1, mask3, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst2, vec0, filt2, dst0, vec1, filt3,
                  dst1, vec2, filt3, dst2, vec3, filt3, dst2, dst0, dst1, dst2);

        tmp0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        dst2 = __lsx_vsadd_h(dst2, in2);
        tmp1 = __lsx_vssrarni_bu_h(dst2, dst2, 7);

        __lsx_vst(tmp0, dst, 0);
        __lsx_vstelm_d(tmp1, dst, 16, 0);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_hz_8t_16w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                       dst, dst_stride, filter, height);
    hevc_hz_8t_16w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                       dst + 16, dst_stride, filter, height);
}

static void hevc_hz_8t_48w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_hz_8t_16w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                       dst, dst_stride, filter, height);
    hevc_hz_8t_32w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                       dst + 16, dst_stride, filter, height);
}

static void hevc_hz_8t_64w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_hz_8t_32w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                       dst, dst_stride, filter, height);
    hevc_hz_8t_32w_lsx(src0_ptr + 32, src_stride, src1_ptr + 32, src2_stride,
                       dst + 32, dst_stride, filter, height);
}

static av_always_inline
void hevc_vt_8t_8w_lsx(const uint8_t *src0_ptr, int32_t src_stride, const int16_t *src1_ptr,
                       int32_t src2_stride, uint8_t *dst, int32_t dst_stride,\
                       const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    __m128i src0, src1, src2, src3, src4, src5;
    __m128i src6, src7, src8, src9, src10;
    __m128i in0, in1, in2, in3;
    __m128i src10_r, src32_r, src54_r, src76_r, src98_r;
    __m128i src21_r, src43_r, src65_r, src87_r, src109_r;
    __m128i dst0_r, dst1_r, dst2_r, dst3_r;
    __m128i filt0, filt1, filt2, filt3;

    src0_ptr -= src_stride_3x;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filt0, filt1, filt2, filt3);

    src0 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src1, src2);
    src3 = __lsx_vldx(src0_ptr, src_stride_3x);
    src0_ptr += src_stride_4x;
    src4 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src5, src6);
    src0_ptr += src_stride_3x;
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
              src10_r, src32_r, src54_r, src21_r);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        src7 = __lsx_vld(src0_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
                  src8, src9);
        src10 = __lsx_vldx(src0_ptr, src_stride_3x);
        src0_ptr += src_stride_4x;
        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr, src2_stride_2x,
                  in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10, src9,
                  src76_r, src87_r, src98_r, src109_r);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src21_r, filt0, src32_r,
                  filt0, src43_r, filt0, dst0_r, dst1_r, dst2_r, dst3_r);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src32_r, filt1, dst1_r, src43_r,
                  filt1, dst2_r, src54_r, filt1, dst3_r, src65_r, filt1,
                  dst0_r, dst1_r, dst2_r, dst3_r);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src54_r, filt2, dst1_r, src65_r,
                  filt2, dst2_r, src76_r, filt2, dst3_r, src87_r, filt2,
                  dst0_r, dst1_r, dst2_r, dst3_r);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src76_r, filt3, dst1_r, src87_r,
                  filt3, dst2_r, src98_r, filt3, dst3_r, src109_r, filt3,
                  dst0_r, dst1_r, dst2_r, dst3_r);

        dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in1, dst1_r);
        dst1_r = hevc_bi_rnd_clip(in2, dst2_r, in3, dst3_r);
        __lsx_vstelm_d(dst0_r, dst, 0, 0);
        __lsx_vstelm_d(dst0_r, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(dst1_r, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(dst1_r, dst + dst_stride_3x, 0, 1);
        dst += dst_stride_4x;

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;

        src6 = src10;
    }
}

static av_always_inline
void hevc_vt_8t_16multx2mult_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                                 const int16_t *src1_ptr, int32_t src2_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height,
                                 int32_t width)
{
    const uint8_t *src0_ptr_tmp;
    const int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt;
    uint32_t cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i in0, in1, in2, in3;
    __m128i src10_r, src32_r, src54_r, src76_r;
    __m128i src21_r, src43_r, src65_r, src87_r;
    __m128i dst0_r, dst1_r;
    __m128i src10_l, src32_l, src54_l, src76_l;
    __m128i src21_l, src43_l, src65_l, src87_l;
    __m128i dst0_l, dst1_l;
    __m128i filt0, filt1, filt2, filt3;

    src0_ptr -= src_stride_3x;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filt0, filt1, filt2, filt3);

    for (cnt = (width >> 4); cnt--;) {
        src0_ptr_tmp = src0_ptr;
        src1_ptr_tmp = src1_ptr;
        dst_tmp = dst;

        src0 = __lsx_vld(src0_ptr_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr_tmp, src_stride, src0_ptr_tmp,
                  src_stride_2x, src1, src2);
        src3 = __lsx_vldx(src0_ptr_tmp, src_stride_3x);
        src0_ptr_tmp += src_stride_4x;
        src4 = __lsx_vld(src0_ptr_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr_tmp, src_stride, src0_ptr_tmp,
                  src_stride_2x, src5, src6);
        src0_ptr_tmp += src_stride_3x;

        DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  src10_r, src32_r, src54_r, src21_r);
        DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, src43_r, src65_r);
        DUP4_ARG2(__lsx_vilvh_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  src10_l, src32_l, src54_l, src21_l);
        DUP2_ARG2(__lsx_vilvh_b, src4, src3, src6, src5, src43_l, src65_l);

        for (loop_cnt = (height >> 1); loop_cnt--;) {
            src7 = __lsx_vld(src0_ptr_tmp, 0);
            src8 = __lsx_vldx(src0_ptr_tmp, src_stride);
            src0_ptr_tmp += src_stride_2x;
            DUP2_ARG2(__lsx_vld, src1_ptr_tmp, 0, src1_ptr_tmp, 16, in0, in2);
            src1_ptr_tmp += src2_stride;
            DUP2_ARG2(__lsx_vld, src1_ptr_tmp, 0, src1_ptr_tmp, 16, in1, in3);
            src1_ptr_tmp += src2_stride;

            DUP2_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src76_r, src87_r);
            DUP2_ARG2(__lsx_vilvh_b, src7, src6, src8, src7, src76_l, src87_l);

            DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src21_r, filt0, src10_l,
                      filt0, src21_l, filt0, dst0_r, dst1_r, dst0_l, dst1_l);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src32_r, filt1, dst1_r,
                      src43_r, filt1, dst0_l, src32_l, filt1, dst1_l, src43_l,
                      filt1, dst0_r, dst1_r, dst0_l, dst1_l);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src54_r, filt2, dst1_r,
                      src65_r, filt2, dst0_l, src54_l, filt2, dst1_l, src65_l,
                      filt2, dst0_r, dst1_r, dst0_l, dst1_l);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src76_r, filt3, dst1_r,
                      src87_r, filt3, dst0_l, src76_l, filt3, dst1_l, src87_l,
                      filt3, dst0_r, dst1_r, dst0_l, dst1_l);
            dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in2, dst0_l);
            dst1_r = hevc_bi_rnd_clip(in1, dst1_r, in3, dst1_l);

            __lsx_vst(dst0_r, dst_tmp, 0);
            __lsx_vstx(dst1_r, dst_tmp, dst_stride);
            dst_tmp += dst_stride_2x;

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

static void hevc_vt_8t_16w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx2mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter, height, 16);
}

static void hevc_vt_8t_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx2mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter, height, 16);
    hevc_vt_8t_8w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                      dst + 16, dst_stride, filter, height);
}

static void hevc_vt_8t_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx2mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter, height, 32);
}

static void hevc_vt_8t_48w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx2mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter, height, 48);
}

static void hevc_vt_8t_64w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_8t_16multx2mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter, height, 64);
}

static av_always_inline
void hevc_hv_8t_8multx1mult_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                                const int16_t *src1_ptr, int32_t src2_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter_x, const int8_t *filter_y,
                                int32_t height, int32_t width)
{
    uint32_t loop_cnt;
    uint32_t cnt;
    const uint8_t *src0_ptr_tmp;
    const int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    __m128i out;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i in0, tmp;
    __m128i filt0, filt1, filt2, filt3;
    __m128i filt_h0, filt_h1, filt_h2, filt_h3;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1, mask2, mask3;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    __m128i dst0_r, dst0_l;
    __m128i dst10_r, dst32_r, dst54_r, dst76_r;
    __m128i dst10_l, dst32_l, dst54_l, dst76_l;

    src0_ptr -= src_stride_3x + 3;

    DUP4_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filter_x, 4, filter_x,
              6, filt0, filt1, filt2, filt3);
    filt_h3 = __lsx_vld(filter_y, 0);
    filt_h3 = __lsx_vsllwil_h_b(filt_h3, 0);

    DUP4_ARG2(__lsx_vreplvei_w, filt_h3, 0, filt_h3, 1, filt_h3, 2, filt_h3, 3,
              filt_h0, filt_h1, filt_h2, filt_h3);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    for (cnt = width >> 3; cnt--;) {
        src0_ptr_tmp = src0_ptr;
        dst_tmp = dst;
        src1_ptr_tmp = src1_ptr;

        src0 = __lsx_vld(src0_ptr_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr_tmp, src_stride, src0_ptr_tmp,
                  src_stride_2x, src1, src2);
        src3 = __lsx_vldx(src0_ptr_tmp, src_stride_3x);
        src0_ptr_tmp += src_stride_4x;
        src4 = __lsx_vld(src0_ptr_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr_tmp, src_stride, src0_ptr_tmp,
                  src_stride_2x, src5, src6);
        src0_ptr_tmp += src_stride_3x;

        /* row 0 row 1 row 2 row 3 */
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, src0,
                  src0, mask2, src0, src0, mask3, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1, src1,
                  src1, mask2, src1, src1, mask3, vec4, vec5, vec6, vec7);
        DUP4_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, src2,
                  src2, mask2, src2, src2, mask3, vec8, vec9, vec10, vec11);
        DUP4_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, src3,
                  src3, mask2, src3, src3, mask3, vec12, vec13, vec14, vec15);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec4, filt0, vec8, filt0,
                  vec12, filt0, dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec5, filt1,
                  dst2, vec9, filt1, dst3, vec13, filt1, dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec2, filt2, dst1, vec6, filt2,
                  dst2, vec10, filt2, dst3, vec14, filt2, dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec3, filt3, dst1, vec7, filt3,
                  dst2, vec11, filt3, dst3, vec15, filt3, dst0, dst1, dst2, dst3);

        DUP4_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, src4,
                  src4, mask2, src4, src4, mask3, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, src5,
                  src5, mask2, src5, src5, mask3, vec4, vec5, vec6, vec7);
        DUP4_ARG3(__lsx_vshuf_b, src6, src6, mask0, src6, src6, mask1, src6,
                  src6, mask2, src6, src6, mask3, vec8, vec9, vec10, vec11);
        DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec4, filt0, dst4, dst5);
        dst6 = __lsx_vdp2_h_bu_b(vec8, filt0);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst4, vec1, filt1, dst5, vec5, filt1,
                  dst6, vec9, filt1, dst4, vec2, filt2, dst4, dst5, dst6, dst4);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst5, vec6, filt2, dst6, vec10, filt2,
                  dst4, vec3, filt3, dst5, vec7, filt3, dst5, dst6, dst4, dst5);
        dst6 = __lsx_vdp2add_h_bu_b(dst6, vec11, filt3);

        for (loop_cnt = height; loop_cnt--;) {
            src7 = __lsx_vld(src0_ptr_tmp, 0);
            src0_ptr_tmp += src_stride;

            in0 = __lsx_vld(src1_ptr_tmp, 0);
            src1_ptr_tmp += src2_stride;

            DUP4_ARG3(__lsx_vshuf_b, src7, src7, mask0, src7, src7, mask1, src7,
                      src7, mask2, src7, src7, mask3, vec0, vec1, vec2, vec3);
            dst7 = __lsx_vdp2_h_bu_b(vec0, filt0);
            DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst7, vec1, filt1, dst7, vec2,
                      filt2, dst7, dst7);
            dst7 = __lsx_vdp2add_h_bu_b(dst7, vec3, filt3);
            DUP4_ARG2(__lsx_vilvl_h, dst1, dst0, dst3, dst2, dst5, dst4, dst7,
                      dst6, dst10_r, dst32_r, dst54_r, dst76_r);
            DUP4_ARG2(__lsx_vilvh_h, dst1, dst0, dst3, dst2, dst5, dst4, dst7,
                      dst6, dst10_l, dst32_l, dst54_l, dst76_l);

            DUP2_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0,
                      dst0_r, dst0_l);
            DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l,
                      dst32_l, filt_h1, dst0_r, dst54_r, filt_h2, dst0_l,
                      dst54_l, filt_h2, dst0_r, dst0_l, dst0_r, dst0_l);
            DUP2_ARG3(__lsx_vdp2add_w_h, dst0_r, dst76_r, filt_h3, dst0_l,
                      dst76_l, filt_h3, dst0_r, dst0_l);
            dst0_r = __lsx_vsrli_w(dst0_r, 6);
            dst0_l = __lsx_vsrli_w(dst0_l, 6);

            tmp = __lsx_vpickev_h(dst0_l, dst0_r);
            tmp = __lsx_vsadd_h(tmp, in0);
            tmp = __lsx_vmaxi_h(tmp, 0);
            out = __lsx_vssrlrni_bu_h(tmp, tmp, 7);
            __lsx_vstelm_d(out, dst_tmp, 0, 0);
            dst_tmp += dst_stride;

            dst0 = dst1;
            dst1 = dst2;
            dst2 = dst3;
            dst3 = dst4;
            dst4 = dst5;
            dst5 = dst6;
            dst6 = dst7;
        }

        src0_ptr += 8;
        dst += 8;
        src1_ptr += 8;
    }
}

static void hevc_hv_8t_8w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                              const int16_t *src1_ptr, int32_t src2_stride,
                              uint8_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
{
    hevc_hv_8t_8multx1mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, height, 8);
}

static void hevc_hv_8t_16w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, height, 16);
}

static void hevc_hv_8t_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, height, 24);
}

static void hevc_hv_8t_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, height, 32);
}

static void hevc_hv_8t_48w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, height, 48);
}

static void hevc_hv_8t_64w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_8t_8multx1mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, height, 64);
}

static void hevc_hz_4t_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    const int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt;
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_x = src2_stride << 1;
    int32_t src2_stride_2x = src2_stride << 2;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;

    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i filt0, filt1;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1, mask2, mask3;
    __m128i vec0, vec1, vec2, vec3;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    src0_ptr -= 1;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 8, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 10);

    dst_tmp = dst + 16;
    src1_ptr_tmp = src1_ptr + 16;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src0, src1);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src2, src3);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src4, src5);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src6, src7);
        src0_ptr += src_stride;

        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in0, in1);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in2, in3);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in4, in5);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in6, in7);
        src1_ptr += src2_stride;

        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src0, mask2, src2,
                  src2, mask0, src3, src2, mask2, vec0, vec1, vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src0, mask3, src2,
                  src2, mask1, src3, src2, mask3, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec0, filt1, dst1, vec1, filt1,
                  dst2, vec2, filt1, dst3, vec3, filt1, dst0, dst1, dst2, dst3);

        DUP4_ARG3(__lsx_vshuf_b, src4, src4, mask0, src5, src4, mask2, src6,
                  src6, mask0, src7, src6, mask2, vec0, vec1, vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, dst4, dst5, dst6, dst7);
        DUP4_ARG3(__lsx_vshuf_b, src4, src4, mask1, src5, src4, mask3, src6,
                  src6, mask1, src7, src6, mask3, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst4, vec0, filt1, dst5, vec1, filt1,
                  dst6, vec2, filt1, dst7, vec3, filt1, dst4, dst5, dst6, dst7);

        dst0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        dst1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        dst2 = hevc_bi_rnd_clip(in4, dst4, in5, dst5);
        dst3 = hevc_bi_rnd_clip(in6, dst6, in7, dst7);
        __lsx_vst(dst0, dst, 0);
        __lsx_vstx(dst1, dst, dst_stride);
        __lsx_vstx(dst2, dst, dst_stride_2x);
        __lsx_vstx(dst3, dst, dst_stride_3x);
        dst += dst_stride_4x;

        in0 = __lsx_vld(src1_ptr_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr_tmp, src2_stride_x, src1_ptr_tmp,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr_tmp, src2_stride_3x);
        src1_ptr_tmp += src2_stride_2x;

        DUP4_ARG3(__lsx_vshuf_b, src1, src1, mask0, src3, src3, mask0, src5,
                  src5, mask0, src7, src7, mask0, vec0, vec1, vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vshuf_b, src1, src1, mask1, src3, src3, mask1, src5,
                  src5, mask1, src7, src7, mask1, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec0, filt1, dst1, vec1, filt1,
                  dst2, vec2, filt1, dst3, vec3, filt1, dst0, dst1, dst2, dst3);
        dst0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        dst1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        __lsx_vstelm_d(dst0, dst_tmp, 0, 0);
        __lsx_vstelm_d(dst0, dst_tmp + dst_stride, 0, 1);
        __lsx_vstelm_d(dst1, dst_tmp + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(dst1, dst_tmp + dst_stride_3x, 0, 1);
        dst_tmp += dst_stride_4x;
    }
}

static void hevc_hz_4t_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    __m128i src0, src1, src2;
    __m128i in0, in1, in2, in3;
    __m128i filt0, filt1;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1, mask2, mask3;
    __m128i dst0, dst1, dst2, dst3;
    __m128i vec0, vec1, vec2, vec3;

    src0_ptr -= 1;

    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 8, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 10);

    for (loop_cnt = height; loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src0, src1);
        src2 = __lsx_vld(src0_ptr, 24);
        src0_ptr += src_stride;
        DUP4_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, src1_ptr, 32,
                  src1_ptr, 48, in0, in1, in2, in3);
        src1_ptr += src2_stride;
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src0, mask2, src1,
                  src1, mask0, src2, src2, mask0, vec0, vec1, vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src0, mask3, src1,
                  src1, mask1, src2, src2, mask1, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec0, filt1, dst1, vec1, filt1,
                  dst2, vec2, filt1, dst3, vec3, filt1, dst0, dst1, dst2, dst3);
        dst0 = hevc_bi_rnd_clip(in0, dst0, in1, dst1);
        dst1 = hevc_bi_rnd_clip(in2, dst2, in3, dst3);
        __lsx_vst(dst0, dst, 0);
        __lsx_vst(dst1, dst, 16);
        dst += dst_stride;
    }
}

static void hevc_vt_4t_12w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    const int16_t *_src1 = src1_ptr + 8;
    __m128i src0, src1, src2, src3, src4, src5, src6;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i src10_r, src32_r, src21_r, src43_r, src54_r, src65_r;
    __m128i dst0_r, dst1_r, dst2_r, dst3_r;
    __m128i src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    __m128i src2110, src4332, src6554;
    __m128i dst0_l, dst1_l, filt0, filt1;

    src0_ptr -= src_stride;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);

    src0 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src1, src2);
    src0_ptr += src_stride_3x;
    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_r, src21_r);
    DUP2_ARG2(__lsx_vilvh_b, src1, src0, src2, src1, src10_l, src21_l);
    src2110 = __lsx_vilvl_d(src21_l, src10_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        src3 = __lsx_vld(src0_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
                  src4, src5);
        src6 = __lsx_vldx(src0_ptr, src_stride_3x);
        src0_ptr += src_stride_4x;
        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += src2_stride_2x;
        in4 = __lsx_vld(_src1, 0);
        DUP2_ARG2(__lsx_vldx, _src1, src2_stride_x, _src1, src2_stride_2x,
                  in5, in6);
        in7 = __lsx_vldx(_src1, src2_stride_3x);
        _src1 += src2_stride_2x;
        DUP2_ARG2(__lsx_vilvl_d, in5, in4, in7, in6, in4, in5);

        DUP2_ARG2(__lsx_vilvl_b, src3, src2, src4, src3, src32_r, src43_r);
        DUP2_ARG2(__lsx_vilvh_b, src3, src2, src4, src3, src32_l, src43_l);
        src4332 = __lsx_vilvl_d(src43_l, src32_l);
        DUP2_ARG2(__lsx_vilvl_b, src5, src4, src6, src5, src54_r, src65_r);
        DUP2_ARG2(__lsx_vilvh_b, src5, src4, src6, src5, src54_l, src65_l);
        src6554 = __lsx_vilvl_d(src65_l, src54_l);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src21_r, filt0, src2110,
                  filt0, src32_r, filt0, dst0_r, dst1_r, dst0_l, dst2_r);
        DUP2_ARG2(__lsx_vdp2_h_bu_b, src43_r, filt0, src4332, filt0,
                  dst3_r, dst1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src32_r, filt1, dst1_r,
                  src43_r, filt1, dst0_l, src4332, filt1, dst2_r, src54_r,
                  filt1, dst0_r, dst1_r, dst0_l, dst2_r);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst3_r, src65_r, filt1, dst1_l,
                  src6554, filt1, dst3_r, dst1_l);
        dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in1, dst1_r);
        dst1_r = hevc_bi_rnd_clip(in2, dst2_r, in3, dst3_r);
        dst0_l = hevc_bi_rnd_clip(in4, dst0_l, in5, dst1_l);
        __lsx_vstelm_d(dst0_r, dst, 0, 0);
        __lsx_vstelm_d(dst0_r, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(dst1_r, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(dst1_r, dst + dst_stride_3x, 0, 1);
        __lsx_vstelm_w(dst0_l, dst, 8, 0);
        __lsx_vstelm_w(dst0_l, dst + dst_stride, 8, 1);
        __lsx_vstelm_w(dst0_l, dst + dst_stride_2x, 8, 2);
        __lsx_vstelm_w(dst0_l, dst + dst_stride_3x, 8, 3);
        dst += dst_stride_4x;

        src2 = src6;
        src10_r = src54_r;
        src21_r = src65_r;
        src2110 = src6554;
    }
}

static void hevc_vt_4t_16w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    __m128i src0, src1, src2, src3, src4, src5;
    __m128i in0, in1, in2, in3;
    __m128i src10_r, src32_r, src21_r, src43_r;
    __m128i src10_l, src32_l, src21_l, src43_l;
    __m128i dst0_r, dst1_r, dst0_l, dst1_l;
    __m128i filt0, filt1;

    src0_ptr -= src_stride;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);

    src0 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src1, src2);
    src0_ptr += src_stride_3x;
    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_r, src21_r);
    DUP2_ARG2(__lsx_vilvh_b, src1, src0, src2, src1, src10_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        src3 = __lsx_vld(src0_ptr, 0);
        src4 = __lsx_vldx(src0_ptr, src_stride);
        src0_ptr += src_stride_2x;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in0, in2);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in1, in3);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vilvl_b, src3, src2, src4, src3, src32_r, src43_r);
        DUP2_ARG2(__lsx_vilvh_b, src3, src2, src4, src3, src32_l, src43_l);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src21_r, filt0, src10_l,
                  filt0, src21_l, filt0, dst0_r, dst1_r, dst0_l, dst1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src32_r, filt1, dst1_r, src43_r,
                  filt1, dst0_l, src32_l, filt1, dst1_l, src43_l, filt1,
                  dst0_r, dst1_r, dst0_l, dst1_l);

        dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in2, dst0_l);
        dst1_r = hevc_bi_rnd_clip(in1, dst1_r, in3, dst1_l);
        __lsx_vst(dst0_r, dst, 0);
        __lsx_vstx(dst1_r, dst, dst_stride);
        dst += dst_stride_2x;

        src5 = __lsx_vld(src0_ptr, 0);
        src2 = __lsx_vldx(src0_ptr, src_stride);
        src0_ptr += src_stride_2x;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in0, in2);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in1, in3);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vilvl_b, src5, src4, src2, src5, src10_r, src21_r);
        DUP2_ARG2(__lsx_vilvh_b, src5, src4, src2, src5, src10_l, src21_l);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, src32_r, filt0, src32_l, filt0, src43_r,
                  filt0, src43_l, filt0, dst0_r, dst0_l, dst1_r, dst1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src10_r, filt1, dst0_l,
                  src10_l, filt1, dst1_r, src21_r, filt1, dst1_l, src21_l,
                  filt1, dst0_r, dst0_l, dst1_r, dst1_l);
        dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in2, dst0_l);
        dst1_r = hevc_bi_rnd_clip(in1, dst1_r, in3, dst1_l);
        __lsx_vst(dst0_r, dst, 0);
        __lsx_vstx(dst1_r, dst, dst_stride);
        dst += dst_stride_2x;
    }
}

static void hevc_vt_4t_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    int32_t dst_stride_2x = dst_stride << 1;
    __m128i src0, src1, src2, src3, src4, src5;
    __m128i src6, src7, src8, src9, src10, src11;
    __m128i in0, in1, in2, in3, in4, in5;
    __m128i src10_r, src32_r, src76_r, src98_r;
    __m128i src21_r, src43_r, src87_r, src109_r;
    __m128i src10_l, src32_l, src21_l, src43_l;
    __m128i dst0_r, dst1_r, dst2_r, dst3_r;
    __m128i dst0_l, dst1_l;
    __m128i filt0, filt1;

    src0_ptr -= src_stride;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);

    /* 16width */
    DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src0, src6);
    src0_ptr += src_stride;
    DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src1, src7);
    src0_ptr += src_stride;
    DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src2, src8);
    src0_ptr += src_stride;
    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_r, src21_r);
    DUP2_ARG2(__lsx_vilvh_b, src1, src0, src2, src1, src10_l, src21_l);
    /* 8width */
    DUP2_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src76_r, src87_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        /* 16width */
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src3, src9);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src0_ptr, 0, src0_ptr, 16, src4, src10);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in0, in2);
        in4 = __lsx_vld(src1_ptr, 32);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr, 16, in1, in3);
        in5 = __lsx_vld(src1_ptr, 32);
        src1_ptr += src2_stride;
        DUP2_ARG2(__lsx_vilvl_b, src3, src2, src4, src3, src32_r, src43_r);
        DUP2_ARG2(__lsx_vilvh_b, src3, src2, src4, src3, src32_l, src43_l);
        /* 8width */
        DUP2_ARG2(__lsx_vilvl_b, src9, src8, src10, src9, src98_r, src109_r);
        /* 16width */
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src10_l, filt0, src21_r,
                  filt0, src21_l, filt0, dst0_r, dst0_l, dst1_r, dst1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src32_r, filt1,  dst0_l,
                  src32_l, filt1, dst1_r, src43_r, filt1, dst1_l, src43_l, filt1,
                  dst0_r, dst0_l, dst1_r, dst1_l);
        /* 8width */
        DUP2_ARG2(__lsx_vdp2_h_bu_b, src76_r, filt0, src87_r, filt0,
                  dst2_r, dst3_r);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst2_r, src98_r, filt1, dst3_r,
                  src109_r, filt1, dst2_r, dst3_r);
        /* 16width */
        dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in2, dst0_l);
        dst1_r = hevc_bi_rnd_clip(in1, dst1_r, in3, dst1_l);
        dst2_r = hevc_bi_rnd_clip(in4, dst2_r, in5, dst3_r);
        __lsx_vst(dst0_r, dst, 0);
        __lsx_vstx(dst1_r, dst, dst_stride);
        __lsx_vstelm_d(dst2_r, dst, 16, 0);
        __lsx_vstelm_d(dst2_r, dst + dst_stride, 16, 1);
        dst += dst_stride_2x;

        /* 16width */
        DUP4_ARG2(__lsx_vld, src0_ptr, 0, src1_ptr, 0, src1_ptr, 16, src1_ptr,
                  32, src5, in0, in2, in4);
        src1_ptr += src2_stride;
        DUP4_ARG2(__lsx_vld, src0_ptr, 16,  src1_ptr, 0, src1_ptr, 16, src1_ptr,
                  32, src11, in1, in3, in5);
        src1_ptr += src2_stride;
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vld, src0_ptr, 0,  src0_ptr, 16, src2, src8);
        src0_ptr += src_stride;
        DUP2_ARG2(__lsx_vilvl_b, src5, src4, src2, src5, src10_r, src21_r);
        DUP2_ARG2(__lsx_vilvh_b, src5, src4, src2, src5, src10_l, src21_l);
        /* 8width */
        DUP2_ARG2(__lsx_vilvl_b, src11, src10, src8, src11, src76_r, src87_r);
        /* 16width */
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src32_r, filt0, src32_l, filt0, src43_r,
                  filt0, src43_l, filt0, dst0_r, dst0_l, dst1_r, dst1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0_r, src10_r, filt1, dst0_l,
                  src10_l, filt1, dst1_r, src21_r, filt1, dst1_l, src21_l,
                  filt1, dst0_r, dst0_l, dst1_r, dst1_l);

        /* 8width */
        DUP2_ARG2(__lsx_vdp2_h_bu_b, src98_r, filt0, src109_r, filt0,
                  dst2_r, dst3_r);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b,  dst2_r, src76_r, filt1, dst3_r,
                  src87_r, filt1, dst2_r, dst3_r);

        dst0_r = hevc_bi_rnd_clip(in0, dst0_r, in2, dst0_l);
        dst1_r = hevc_bi_rnd_clip(in1, dst1_r, in3, dst1_l);
        dst2_r = hevc_bi_rnd_clip(in4, dst2_r, in5, dst3_r);
        __lsx_vst(dst0_r, dst, 0);
        __lsx_vstx(dst1_r, dst, dst_stride);
        __lsx_vstelm_d(dst2_r, dst, 16, 0);
        __lsx_vstelm_d(dst2_r, dst + dst_stride, 16, 1);
        dst += dst_stride_2x;
    }
}

static void hevc_vt_4t_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter, int32_t height)
{
    hevc_vt_4t_16w_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                       dst, dst_stride, filter, height);
    hevc_vt_4t_16w_lsx(src0_ptr + 16, src_stride, src1_ptr + 16, src2_stride,
                       dst + 16, dst_stride, filter, height);
}

static void hevc_hv_4t_6w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                              const int16_t *src1_ptr, int32_t src2_stride,
                              uint8_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
{
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_2x = (src2_stride << 1);
    int32_t src2_stride_4x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride;
    __m128i out0, out1;
    __m128i src0, src1, src2, src3, src4, src5, src6;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, mask1;
    __m128i filt0, filt1, filt_h0, filt_h1;
    __m128i dsth0, dsth1, dsth2, dsth3, dsth4, dsth5;
    __m128i dsth6, dsth7, dsth8, dsth9, dsth10;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst4_r, dst5_r, dst6_r, dst7_r;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    __m128i reg0, reg1, reg2, reg3;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);

    src0_ptr -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filt_h1 = __lsx_vld(filter_y, 0);
    filt_h1 = __lsx_vsllwil_h_b(filt_h1, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filt_h1, 0, filt_h1, 1, filt_h0, filt_h1);

    mask1 = __lsx_vaddi_bu(mask0, 2);

    src0 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src1, src2);
    src0_ptr += src_stride_3x;

    DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1, vec2, vec3);
    DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, vec4, vec5);

    DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, dsth0, dsth1);
    dsth2 = __lsx_vdp2_h_bu_b(vec4, filt0);
    DUP2_ARG3(__lsx_vdp2add_h_bu_b, dsth0, vec1, filt1, dsth1, vec3, filt1,
              dsth0, dsth1);
    dsth2 = __lsx_vdp2add_h_bu_b(dsth2, vec5, filt1);

    DUP2_ARG2(__lsx_vilvl_h, dsth1, dsth0, dsth2, dsth1, tmp0, tmp2);
    DUP2_ARG2(__lsx_vilvh_h, dsth1, dsth0, dsth2, dsth1, tmp1, tmp3);

    src3 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src4, src5);
    src6 = __lsx_vldx(src0_ptr, src_stride_3x);
    src0_ptr += src_stride_4x;
    DUP2_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, vec2, vec3);
    DUP2_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, vec4, vec5);
    DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask0, src6, src6, mask1, vec6, vec7);

    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0, vec6,
              filt0, dsth3, dsth4, dsth5, dsth6);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dsth3, vec1, filt1, dsth4, vec3, filt1, dsth5,
              vec5, filt1, dsth6, vec7, filt1, dsth3, dsth4, dsth5, dsth6);

    src3 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src4, src5);
    src6 = __lsx_vldx(src0_ptr, src_stride_3x);

    DUP2_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, vec2, vec3);
    DUP2_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, vec4, vec5);
    DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask0, src6, src6, mask1, vec6, vec7);

    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0, vec6,
              filt0, dsth7, dsth8, dsth9, dsth10);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dsth7, vec1, filt1, dsth8, vec3, filt1, dsth9,
              vec5, filt1, dsth10, vec7, filt1, dsth7, dsth8, dsth9, dsth10);

    DUP2_ARG2(__lsx_vilvl_h, dsth3, dsth2, dsth4, dsth3, tmp4, tmp6);
    DUP2_ARG2(__lsx_vilvh_h, dsth3, dsth2, dsth4, dsth3, tmp5, tmp7);
    DUP2_ARG2(__lsx_vilvl_h, dsth5, dsth4, dsth6, dsth5, dsth0, dsth2);
    DUP2_ARG2(__lsx_vilvh_h, dsth5, dsth4, dsth6, dsth5, dsth1, dsth3);
    DUP4_ARG2(__lsx_vdp2_w_h, tmp0, filt_h0, tmp2, filt_h0, tmp4, filt_h0,
              tmp6, filt_h0, dst0_r, dst1_r, dst2_r, dst3_r);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, tmp4, filt_h1, dst1_r, tmp6,
              filt_h1, dst2_r, dsth0, filt_h1, dst3_r, dsth2, filt_h1,
              dst0_r, dst1_r, dst2_r, dst3_r);
    DUP2_ARG2(__lsx_vpickev_d, tmp3, tmp1, tmp7, tmp5, tmp0, tmp8);
    dst0_l = __lsx_vdp2_w_h(tmp0, filt_h0);
    dst0_l = __lsx_vdp2add_w_h(dst0_l, tmp8, filt_h1);

    DUP2_ARG2(__lsx_vilvl_h, dsth7, dsth6, dsth8, dsth7, tmp0, tmp2);
    DUP2_ARG2(__lsx_vilvh_h, dsth7, dsth6, dsth8, dsth7, tmp1, tmp3);
    DUP2_ARG2(__lsx_vilvl_h, dsth9, dsth8, dsth10, dsth9, tmp4, tmp6);
    DUP2_ARG2(__lsx_vilvh_h, dsth9, dsth8, dsth10, dsth9, tmp5, tmp7);
    DUP4_ARG2(__lsx_vdp2_w_h, dsth0, filt_h0, dsth2, filt_h0, tmp0, filt_h0,
              tmp2, filt_h0, dst4_r, dst5_r, dst6_r, dst7_r);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst4_r, tmp0, filt_h1, dst5_r, tmp2,
              filt_h1, dst6_r, tmp4, filt_h1, dst7_r, tmp6, filt_h1,
              dst4_r, dst5_r, dst6_r, dst7_r);
    DUP2_ARG2(__lsx_vpickev_d, dsth3, dsth1, tmp3, tmp1, tmp0, tmp1);
    tmp2 = __lsx_vpickev_d(tmp7, tmp5);

    DUP2_ARG2(__lsx_vdp2_w_h, tmp8, filt_h0, tmp0, filt_h0, dst1_l, dst2_l);
    dst3_l = __lsx_vdp2_w_h(tmp1, filt_h0);
    DUP2_ARG3(__lsx_vdp2add_w_h, dst1_l, tmp0, filt_h1, dst2_l, tmp1, filt_h1,
              dst1_l, dst2_l);
    dst3_l = __lsx_vdp2add_w_h(dst3_l, tmp2, filt_h1);

    DUP4_ARG2(__lsx_vsrai_d, dst0_r, 6, dst1_r, 6, dst2_r, 6, dst3_r, 6,
              dst0_r, dst1_r, dst2_r, dst3_r);
    DUP4_ARG2(__lsx_vsrai_d, dst4_r, 6, dst5_r, 6, dst6_r, 6, dst7_r, 6,
              dst4_r, dst5_r, dst6_r, dst7_r);
    DUP4_ARG2(__lsx_vsrai_d, dst0_l, 6, dst1_l, 6, dst2_l, 6, dst3_l, 6,
              dst0_l, dst1_l, dst2_l, dst3_l);
    DUP2_ARG2(__lsx_vpickev_h, dst1_r, dst0_r, dst3_r, dst2_r, tmp0, tmp1);
    DUP2_ARG2(__lsx_vpickev_h, dst5_r, dst4_r, dst7_r, dst6_r, tmp2, tmp3);
    DUP2_ARG2(__lsx_vpickev_h, dst1_l, dst0_l, dst3_l, dst2_l, tmp4, tmp5);

    reg0 = __lsx_vldrepl_d(src1_ptr, 0);
    reg1 = __lsx_vldrepl_d(src1_ptr + src2_stride, 0);
    dsth0 = __lsx_vilvl_d(reg1, reg0);
    reg0 = __lsx_vldrepl_d(src1_ptr + src2_stride_2x, 0);
    reg1 = __lsx_vldrepl_d(src1_ptr + src2_stride_3x, 0);
    dsth1 = __lsx_vilvl_d(reg1, reg0);
    src1_ptr += src2_stride_4x;
    reg0 = __lsx_vldrepl_d(src1_ptr, 0);
    reg1 = __lsx_vldrepl_d(src1_ptr + src2_stride, 0);
    dsth2 = __lsx_vilvl_d(reg1, reg0);
    reg0 = __lsx_vldrepl_d(src1_ptr + src2_stride_2x, 0);
    reg1 = __lsx_vldrepl_d(src1_ptr + src2_stride_3x, 0);
    dsth3 = __lsx_vilvl_d(reg1, reg0);

    DUP4_ARG2(__lsx_vsadd_h, dsth0, tmp0, dsth1, tmp1, dsth2, tmp2, dsth3,
              tmp3, tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lsx_vmaxi_h, tmp0, 0, tmp1, 0, tmp2, 0, tmp3, 0,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG3(__lsx_vssrlrni_bu_h, tmp1, tmp0, 7, tmp3, tmp2, 7, out0, out1);

    __lsx_vstelm_w(out0, dst, 0, 0);
    __lsx_vstelm_w(out0, dst + dst_stride, 0, 1);
    __lsx_vstelm_w(out0, dst + dst_stride_2x, 0, 2);
    __lsx_vstelm_w(out0, dst + dst_stride_3x, 0, 3);
    dst += dst_stride_4x;
    __lsx_vstelm_w(out1, dst, 0, 0);
    __lsx_vstelm_w(out1, dst + dst_stride, 0, 1);
    __lsx_vstelm_w(out1, dst + dst_stride_2x, 0, 2);
    __lsx_vstelm_w(out1, dst + dst_stride_3x, 0, 3);
    dst -= dst_stride_4x;

    src1_ptr -= src2_stride_4x;

    reg0 = __lsx_vldrepl_w(src1_ptr, 8);
    reg1 = __lsx_vldrepl_w(src1_ptr + src2_stride, 8);
    reg2 = __lsx_vldrepl_w(src1_ptr + src2_stride_2x, 8);
    reg3 = __lsx_vldrepl_w(src1_ptr + src2_stride_3x, 8);
    DUP2_ARG2(__lsx_vilvl_w, reg1, reg0, reg3, reg2, tmp0, tmp1);
    dsth4 = __lsx_vilvl_d(tmp1, tmp0);
    src1_ptr += src2_stride_4x;

    reg0 = __lsx_vldrepl_w(src1_ptr, 8);
    reg1 = __lsx_vldrepl_w(src1_ptr + src2_stride, 8);
    reg2 = __lsx_vldrepl_w(src1_ptr + src2_stride_2x, 8);
    reg3 = __lsx_vldrepl_w(src1_ptr + src2_stride_3x, 8);
    DUP2_ARG2(__lsx_vilvl_w, reg1, reg0, reg3, reg2, tmp0, tmp1);
    dsth5 = __lsx_vilvl_d(tmp1, tmp0);
    DUP2_ARG2(__lsx_vsadd_h, dsth4, tmp4, dsth5, tmp5, tmp4, tmp5);
    DUP2_ARG2(__lsx_vmaxi_h, tmp4, 0, tmp5, 7, tmp4, tmp5);
    out0 = __lsx_vssrlrni_bu_h(tmp5, tmp4, 7);

    __lsx_vstelm_h(out0, dst, 4, 0);
    __lsx_vstelm_h(out0, dst + dst_stride, 4, 1);
    __lsx_vstelm_h(out0, dst + dst_stride_2x, 4, 2);
    __lsx_vstelm_h(out0, dst + dst_stride_3x, 4, 3);
    dst += dst_stride_4x;
    __lsx_vstelm_h(out0, dst, 4, 4);
    __lsx_vstelm_h(out0, dst + dst_stride, 4, 5);
    __lsx_vstelm_h(out0, dst + dst_stride_2x, 4, 6);
    __lsx_vstelm_h(out0, dst + dst_stride_3x, 4, 7);
}

static av_always_inline
void hevc_hv_4t_8x2_lsx(const uint8_t *src0_ptr, int32_t src_stride, const int16_t *src1_ptr,
                        int32_t src2_stride, uint8_t *dst, int32_t dst_stride,
                        const int8_t *filter_x, const int8_t *filter_y)
{
    int32_t src_stride_2x = (src_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;

    __m128i out;
    __m128i src0, src1, src2, src3, src4;
    __m128i filt0, filt1;
    __m128i filt_h0, filt_h1;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1, filter_vec;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    __m128i dst0, dst1, dst2, dst3, dst4;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l;
    __m128i dst10_r, dst32_r, dst21_r, dst43_r;
    __m128i dst10_l, dst32_l, dst21_l, dst43_l;
    __m128i tmp0, tmp1;
    __m128i in0, in1;

    src0_ptr -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask1 = __lsx_vaddi_bu(mask0, 2);

    src0 = __lsx_vld(src0_ptr, 0);
    DUP4_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src0_ptr, src_stride_3x, src0_ptr, src_stride_4x,
              src1, src2, src3, src4);

    DUP2_ARG2(__lsx_vld, src1_ptr, 0, src1_ptr + src2_stride, 0, in0, in1);

    DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1, vec2, vec3);
    DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, vec4, vec5);
    DUP2_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, vec6, vec7);
    DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, vec8, vec9);

    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0, vec6,
              filt0, dst0, dst1, dst2, dst3);
    dst4 = __lsx_vdp2_h_bu_b(vec8, filt0);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec3, filt1, dst2,
              vec5, filt1, dst3, vec7, filt1, dst0, dst1, dst2, dst3);
    dst4 = __lsx_vdp2add_h_bu_b(dst4, vec9, filt1);

    DUP2_ARG2(__lsx_vilvl_h, dst1, dst0, dst2, dst1, dst10_r, dst21_r);
    DUP2_ARG2(__lsx_vilvh_h, dst1, dst0, dst2, dst1, dst10_l, dst21_l);
    DUP2_ARG2(__lsx_vilvl_h, dst3, dst2, dst4, dst3, dst32_r, dst43_r);
    DUP2_ARG2(__lsx_vilvh_h, dst3, dst2, dst4, dst3, dst32_l, dst43_l);
    DUP4_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0, dst21_r,
              filt_h0, dst21_l, filt_h0, dst0_r, dst0_l, dst1_r, dst1_l);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l, dst32_l,
              filt_h1, dst1_r, dst43_r, filt_h1, dst1_l, dst43_l, filt_h1,
              dst0_r, dst0_l, dst1_r, dst1_l);
    DUP4_ARG2(__lsx_vsrai_w, dst0_r, 6, dst0_l, 6, dst1_r, 6, dst1_l, 6,
              dst0_r, dst0_l, dst1_r, dst1_l);
    DUP2_ARG2(__lsx_vpickev_h, dst0_l, dst0_r, dst1_l, dst1_r, tmp0, tmp1);
    DUP2_ARG2(__lsx_vsadd_h, in0, tmp0, in1, tmp1, tmp0, tmp1);
    DUP2_ARG2(__lsx_vmaxi_h, tmp0, 0, tmp1, 0, tmp0, tmp1);
    out = __lsx_vssrlrni_bu_h(tmp1, tmp0, 7);
    __lsx_vstelm_d(out, dst, 0, 0);
    __lsx_vstelm_d(out, dst + dst_stride, 0, 1);
}

static av_always_inline
void hevc_hv_4t_8multx4_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                            const int16_t *src1_ptr, int32_t src2_stride,
                            uint8_t *dst, int32_t dst_stride,
                            const int8_t *filter_x, const int8_t *filter_y,
                            int32_t width8mult)
{
    uint32_t cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;

    __m128i out0, out1;
    __m128i src0, src1, src2, src3, src4, src5, src6, mask0, mask1;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i filt0, filt1, filt_h0, filt_h1, filter_vec;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, tmp0, tmp1, tmp2, tmp3;
    __m128i in0, in1, in2, in3;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    __m128i dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;

    src0_ptr -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    mask1 = __lsx_vaddi_bu(mask0, 2);

    for (cnt = width8mult; cnt--;) {
        src0 = __lsx_vld(src0_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
                  src1, src2);
        src3 = __lsx_vldx(src0_ptr, src_stride_3x);
        src0_ptr += src_stride_4x;
        src4 = __lsx_vld(src0_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
                  src5, src6);
        src0_ptr += (8 - src_stride_4x);

        in0 = __lsx_vld(src1_ptr, 0);
        DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr,
                  src2_stride_2x, in1, in2);
        in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
        src1_ptr += 8;

        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1,
                  vec2, vec3);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1,
                  vec4, vec5);

        DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, dst0, dst1);
        dst2 = __lsx_vdp2_h_bu_b(vec4, filt0);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec3, filt1,
                  dst0, dst1);
        dst2 = __lsx_vdp2add_h_bu_b(dst2, vec5, filt1);

        DUP2_ARG2(__lsx_vilvl_h, dst1, dst0, dst2, dst1, dst10_r, dst21_r);
        DUP2_ARG2(__lsx_vilvh_h, dst1, dst0, dst2, dst1, dst10_l, dst21_l);

        DUP2_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1,
                  vec2, vec3);
        DUP2_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1,
                  vec4, vec5);
        DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask0, src6, src6, mask1,
                  vec6, vec7);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0,
                  vec6, filt0, dst3, dst4, dst5, dst6);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst3, vec1, filt1, dst4, vec3, filt1,
                  dst5, vec5, filt1, dst6, vec7, filt1, dst3, dst4, dst5, dst6);

        DUP2_ARG2(__lsx_vilvl_h, dst3, dst2, dst4, dst3, dst32_r, dst43_r);
        DUP2_ARG2(__lsx_vilvh_h, dst3, dst2, dst4, dst3, dst32_l, dst43_l);
        DUP2_ARG2(__lsx_vilvl_h, dst5, dst4, dst6, dst5, dst54_r, dst65_r);
        DUP2_ARG2(__lsx_vilvh_h, dst5, dst4, dst6, dst5, dst54_l, dst65_l);

        DUP4_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0, dst21_r,
                  filt_h0, dst21_l, filt_h0, dst0_r, dst0_l, dst1_r, dst1_l);
        DUP4_ARG2(__lsx_vdp2_w_h, dst32_r, filt_h0, dst32_l, filt_h0, dst43_r,
                  filt_h0, dst43_l, filt_h0, dst2_r, dst2_l, dst3_r, dst3_l);
        DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l, dst32_l,
                  filt_h1, dst1_r, dst43_r, filt_h1, dst1_l, dst43_l, filt_h1,
                  dst0_r, dst0_l, dst1_r, dst1_l);
        DUP4_ARG3(__lsx_vdp2add_w_h, dst2_r, dst54_r, filt_h1, dst2_l, dst54_l,
                  filt_h1, dst3_r, dst65_r, filt_h1, dst3_l, dst65_l, filt_h1,
                  dst2_r, dst2_l, dst3_r, dst3_l);

        DUP4_ARG2(__lsx_vsrai_w, dst0_r, 6, dst0_l, 6, dst1_r, 6, dst1_l, 6,
                  dst0_r, dst0_l, dst1_r, dst1_l);
        DUP4_ARG2(__lsx_vsrai_w, dst2_r, 6, dst2_l, 6, dst3_r, 6, dst3_l, 6,
                  dst2_r, dst2_l, dst3_r, dst3_l);
        DUP4_ARG2(__lsx_vpickev_h, dst0_l, dst0_r, dst1_l, dst1_r, dst2_l,
                  dst2_r, dst3_l, dst3_r, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG2(__lsx_vsadd_h, in0, tmp0, in1, tmp1, in2, tmp2, in3, tmp3,
                  tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG2(__lsx_vmaxi_h, tmp0, 0, tmp1, 0, tmp2, 0, tmp3, 0,
                  tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG3(__lsx_vssrlrni_bu_h, tmp1, tmp0, 7, tmp3, tmp2, 7, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(out1, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(out1, dst + dst_stride_3x, 0, 1);
        dst += 8;
    }
}

static av_always_inline
void hevc_hv_4t_8x6_lsx(const uint8_t *src0_ptr, int32_t src_stride, const int16_t *src1_ptr,
                        int32_t src2_stride, uint8_t *dst, int32_t dst_stride,
                        const int8_t *filter_x, const int8_t *filter_y)
{
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src2_stride_x = (src2_stride << 1);
    int32_t src2_stride_2x = (src2_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;

    __m128i out0, out1, out2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i in0, in1, in2, in3, in4, in5;
    __m128i filt0, filt1;
    __m128i filt_h0, filt_h1;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1, filter_vec;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    __m128i vec10, vec11, vec12, vec13, vec14, vec15, vec16, vec17;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst4_r, dst4_l, dst5_r, dst5_l;
    __m128i dst10_r, dst32_r, dst10_l, dst32_l;
    __m128i dst21_r, dst43_r, dst21_l, dst43_l;
    __m128i dst54_r, dst54_l, dst65_r, dst65_l;
    __m128i dst76_r, dst76_l, dst87_r, dst87_l;

    src0_ptr -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask1 = __lsx_vaddi_bu(mask0, 2);

    src0 = __lsx_vld(src0_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src1, src2);
    src3 = __lsx_vldx(src0_ptr, src_stride_3x);
    src0_ptr += src_stride_4x;
    src4 = __lsx_vld(src0_ptr, 0);
    DUP4_ARG2(__lsx_vldx, src0_ptr, src_stride, src0_ptr, src_stride_2x,
              src0_ptr, src_stride_3x, src0_ptr, src_stride_4x,
              src5, src6, src7, src8);

    in0 = __lsx_vld(src1_ptr, 0);
    DUP2_ARG2(__lsx_vldx, src1_ptr, src2_stride_x, src1_ptr, src2_stride_2x,
              in1, in2);
    in3 = __lsx_vldx(src1_ptr, src2_stride_3x);
    src1_ptr += src2_stride_2x;
    in4 = __lsx_vld(src1_ptr, 0);
    in5 = __lsx_vldx(src1_ptr, src2_stride_x);

    DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1, vec2, vec3);
    DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, vec4, vec5);
    DUP2_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, vec6, vec7);
    DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, vec8, vec9);
    DUP2_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, vec10, vec11);
    DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask0, src6, src6, mask1, vec12, vec13);
    DUP2_ARG3(__lsx_vshuf_b, src7, src7, mask0, src7, src7, mask1, vec14, vec15);
    DUP2_ARG3(__lsx_vshuf_b, src8, src8, mask0, src8, src8, mask1, vec16, vec17);

    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0, vec6,
              filt0, dst0, dst1, dst2, dst3);
    dst4 = __lsx_vdp2_h_bu_b(vec8, filt0);
    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec10, filt0, vec12, filt0, vec14, filt0,
              vec16, filt0, dst5, dst6, dst7, dst8);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec3, filt1, dst2,
              vec5, filt1, dst3, vec7, filt1, dst0, dst1, dst2, dst3);
    dst4 = __lsx_vdp2add_h_bu_b(dst4, vec9, filt1);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst5, vec11, filt1, dst6, vec13, filt1,
              dst7, vec15, filt1, dst8, vec17, filt1, dst5, dst6, dst7, dst8);

    DUP4_ARG2(__lsx_vilvl_h, dst1, dst0, dst2, dst1, dst3, dst2, dst4, dst3,
              dst10_r, dst21_r, dst32_r, dst43_r);
    DUP4_ARG2(__lsx_vilvh_h, dst1, dst0, dst2, dst1, dst3, dst2, dst4, dst3,
              dst10_l, dst21_l, dst32_l, dst43_l);
    DUP4_ARG2(__lsx_vilvl_h, dst5, dst4, dst6, dst5, dst7, dst6, dst8, dst7,
              dst54_r, dst65_r, dst76_r, dst87_r);
    DUP4_ARG2(__lsx_vilvh_h, dst5, dst4, dst6, dst5, dst7, dst6, dst8, dst7,
              dst54_l, dst65_l, dst76_l, dst87_l);

    DUP4_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0, dst21_r,
              filt_h0, dst21_l, filt_h0, dst0_r, dst0_l, dst1_r, dst1_l);
    DUP4_ARG2(__lsx_vdp2_w_h, dst32_r, filt_h0, dst32_l, filt_h0, dst43_r,
              filt_h0, dst43_l, filt_h0, dst2_r, dst2_l, dst3_r, dst3_l);
    DUP4_ARG2(__lsx_vdp2_w_h, dst54_r, filt_h0, dst54_l, filt_h0, dst65_r,
              filt_h0, dst65_l, filt_h0, dst4_r, dst4_l, dst5_r, dst5_l);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l, dst32_l,
              filt_h1, dst1_r, dst43_r, filt_h1, dst1_l, dst43_l, filt_h1,
              dst0_r, dst0_l, dst1_r, dst1_l);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst2_r, dst54_r, filt_h1, dst2_l, dst54_l,
              filt_h1, dst3_r, dst65_r, filt_h1, dst3_l, dst65_l, filt_h1,
              dst2_r, dst2_l, dst3_r, dst3_l);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst4_r, dst76_r, filt_h1, dst4_l, dst76_l,
              filt_h1, dst5_r, dst87_r, filt_h1, dst5_l, dst87_l, filt_h1,
              dst4_r, dst4_l, dst5_r, dst5_l);

    DUP4_ARG2(__lsx_vsrai_w, dst0_r, 6, dst0_l, 6, dst1_r, 6, dst1_l, 6,
              dst0_r, dst0_l, dst1_r, dst1_l);
    DUP4_ARG2(__lsx_vsrai_w, dst2_r, 6, dst2_l, 6, dst3_r, 6, dst3_l, 6,
              dst2_r, dst2_l, dst3_r, dst3_l);
    DUP4_ARG2(__lsx_vsrai_w, dst4_r, 6, dst4_l, 6, dst5_r, 6, dst5_l, 6,
              dst4_r, dst4_l, dst5_r, dst5_l);
    DUP4_ARG2(__lsx_vpickev_h, dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r,
              dst3_l, dst3_r, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vpickev_h, dst4_l, dst4_r, dst5_l, dst5_r, tmp4, tmp5);
    DUP4_ARG2(__lsx_vsadd_h, in0, tmp0, in1, tmp1, in2, tmp2, in3, tmp3,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vsadd_h, in4, tmp4, in5, tmp5, tmp4, tmp5);
    DUP4_ARG2(__lsx_vmaxi_h, tmp0, 0, tmp1, 0, tmp2, 0, tmp3, 0,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vmaxi_h, tmp4, 0, tmp5, 0, tmp4, tmp5);
    DUP2_ARG3(__lsx_vssrlrni_bu_h, tmp1, tmp0, 7, tmp3, tmp2, 7, out0, out1);
    out2 = __lsx_vssrlrni_bu_h(tmp5, tmp4, 7);
    __lsx_vstelm_d(out0, dst, 0, 0);
    __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
    __lsx_vstelm_d(out1, dst + dst_stride_2x, 0, 0);
    __lsx_vstelm_d(out1, dst + dst_stride_3x, 0, 1);
    dst += dst_stride_4x;
    __lsx_vstelm_d(out2, dst, 0, 0);
    __lsx_vstelm_d(out2, dst + dst_stride, 0, 1);
}

static av_always_inline
void hevc_hv_4t_8multx4mult_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                                const int16_t *src1_ptr, int32_t src2_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter_x, const int8_t *filter_y,
                                int32_t height, int32_t width)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src0_ptr_tmp;
    const int16_t *src1_ptr_tmp;
    uint8_t *dst_tmp;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src2_stride_x = (src2_stride << 1);
    const int32_t src2_stride_2x = (src2_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    const int32_t src2_stride_3x = src2_stride_2x + src2_stride_x;
    __m128i out0, out1;
    __m128i src0, src1, src2, src3, src4, src5, src6;
    __m128i in0, in1, in2, in3;
    __m128i filt0, filt1;
    __m128i filt_h0, filt_h1;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1, filter_vec;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i dst10_r, dst32_r, dst21_r, dst43_r;
    __m128i dst10_l, dst32_l, dst21_l, dst43_l;
    __m128i dst54_r, dst54_l, dst65_r, dst65_l, dst6;

    src0_ptr -= (src_stride + 1);

    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);

    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask1 = __lsx_vaddi_bu(mask0, 2);

    for (cnt = width >> 3; cnt--;) {
        src0_ptr_tmp = src0_ptr;
        dst_tmp = dst;
        src1_ptr_tmp = src1_ptr;

        src0 = __lsx_vld(src0_ptr_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src0_ptr_tmp, src_stride, src0_ptr_tmp,
                  src_stride_2x, src1, src2);
        src0_ptr_tmp += src_stride_3x;

        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1,
                  vec2, vec3);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1,
                  vec4, vec5);

        DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, dst0, dst1);
        dst2 = __lsx_vdp2_h_bu_b(vec4, filt0);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec3, filt1,
                  dst0, dst1);
        dst2 = __lsx_vdp2add_h_bu_b(dst2, vec5, filt1);

        DUP2_ARG2(__lsx_vilvl_h, dst1, dst0, dst2, dst1, dst10_r, dst21_r);
        DUP2_ARG2(__lsx_vilvh_h, dst1, dst0, dst2, dst1, dst10_l, dst21_l);

        for (loop_cnt = height >> 2; loop_cnt--;) {
            src3 = __lsx_vld(src0_ptr_tmp, 0);
            DUP2_ARG2(__lsx_vldx, src0_ptr_tmp, src_stride, src0_ptr_tmp,
                      src_stride_2x, src4, src5);
            src6 = __lsx_vldx(src0_ptr_tmp, src_stride_3x);
            src0_ptr_tmp += src_stride_4x;
            in0 = __lsx_vld(src1_ptr_tmp, 0);
            DUP2_ARG2(__lsx_vldx, src1_ptr_tmp, src2_stride_x, src1_ptr_tmp,
                      src2_stride_2x, in1, in2);
            in3 = __lsx_vldx(src1_ptr_tmp, src2_stride_3x);
            src1_ptr_tmp += src2_stride_2x;

            DUP4_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, src4,
                      src4, mask0, src4, src4, mask1, vec0, vec1, vec2, vec3);
            DUP4_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, src6,
                      src6, mask0, src6, src6, mask1, vec4, vec5, vec6, vec7);

            DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0,
                      vec6, filt0, dst3, dst4, dst5, dst6);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst3, vec1, filt1, dst4, vec3,
                      filt1, dst5, vec5, filt1, dst6, vec7, filt1,
                      dst3, dst4, dst5, dst6);

            DUP2_ARG2(__lsx_vilvl_h, dst3, dst2, dst4, dst3, dst32_r, dst43_r);
            DUP2_ARG2(__lsx_vilvh_h, dst3, dst2, dst4, dst3, dst32_l, dst43_l);
            DUP2_ARG2(__lsx_vilvl_h, dst5, dst4, dst6, dst5, dst54_r, dst65_r);
            DUP2_ARG2(__lsx_vilvh_h, dst5, dst4, dst6, dst5, dst54_l, dst65_l);

            DUP4_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0, dst21_r,
                      filt_h0, dst21_l, filt_h0, dst0_r, dst0_l, dst1_r, dst1_l);
            DUP4_ARG2(__lsx_vdp2_w_h, dst32_r, filt_h0, dst32_l, filt_h0, dst43_r,
                      filt_h0, dst43_l, filt_h0, dst2_r, dst2_l, dst3_r, dst3_l);
            DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l,
                      dst32_l, filt_h1, dst1_r, dst43_r, filt_h1, dst1_l,
                      dst43_l, filt_h1, dst0_r, dst0_l, dst1_r, dst1_l);
            DUP4_ARG3(__lsx_vdp2add_w_h, dst2_r, dst54_r, filt_h1, dst2_l,
                      dst54_l, filt_h1, dst3_r, dst65_r, filt_h1, dst3_l,
                      dst65_l, filt_h1, dst2_r, dst2_l, dst3_r, dst3_l);

            DUP4_ARG2(__lsx_vsrai_w, dst0_r, 6, dst0_l, 6, dst1_r, 6, dst1_l, 6,
                      dst0_r, dst0_l, dst1_r, dst1_l);
            DUP4_ARG2(__lsx_vsrai_w, dst2_r, 6, dst2_l, 6, dst3_r, 6, dst3_l, 6,
                      dst2_r, dst2_l, dst3_r, dst3_l);
            DUP4_ARG2(__lsx_vpickev_h, dst0_l, dst0_r, dst1_l, dst1_r, dst2_l,
                      dst2_r, dst3_l, dst3_r, tmp0, tmp1, tmp2, tmp3);
            DUP4_ARG2(__lsx_vsadd_h, in0, tmp0, in1, tmp1, in2, tmp2, in3, tmp3,
                      tmp0, tmp1, tmp2, tmp3);
            DUP4_ARG2(__lsx_vmaxi_h, tmp0, 0, tmp1, 0, tmp2, 0, tmp3, 0, tmp0,
                      tmp1, tmp2, tmp3);
            DUP2_ARG3(__lsx_vssrlrni_bu_h, tmp1, tmp0, 7, tmp3, tmp2, 7, out0, out1);
            __lsx_vstelm_d(out0, dst_tmp, 0, 0);
            __lsx_vstelm_d(out0, dst_tmp + dst_stride, 0, 1);
            __lsx_vstelm_d(out1, dst_tmp + dst_stride_2x, 0, 0);
            __lsx_vstelm_d(out1, dst_tmp + dst_stride_3x, 0, 1);
            dst_tmp += dst_stride_4x;

            dst10_r = dst54_r;
            dst10_l = dst54_l;
            dst21_r = dst65_r;
            dst21_l = dst65_l;
            dst2 = dst6;
        }

        src0_ptr += 8;
        dst += 8;
        src1_ptr += 8;
    }
}

static void hevc_hv_4t_8w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                              const int16_t *src1_ptr, int32_t src2_stride,
                              uint8_t *dst, int32_t dst_stride,
                              const int8_t *filter_x, const int8_t *filter_y,
                              int32_t height)
{
    if (2 == height) {
        hevc_hv_4t_8x2_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                           dst, dst_stride, filter_x, filter_y);
    } else if (4 == height) {
        hevc_hv_4t_8multx4_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, 1);
    } else if (6 == height) {
        hevc_hv_4t_8x6_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                           dst, dst_stride, filter_x, filter_y);
    } else {
        hevc_hv_4t_8multx4mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter_x, filter_y, height, 8);
    }
}

static void hevc_hv_4t_16w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    if (4 == height) {
        hevc_hv_4t_8multx4_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                               dst, dst_stride, filter_x, filter_y, 2);
    } else {
        hevc_hv_4t_8multx4mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                                dst, dst_stride, filter_x, filter_y, height, 16);
    }
}

static void hevc_hv_4t_24w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                            dst, dst_stride, filter_x, filter_y, height, 24);
}

static void hevc_hv_4t_32w_lsx(const uint8_t *src0_ptr, int32_t src_stride,
                               const int16_t *src1_ptr, int32_t src2_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_x, const int8_t *filter_y,
                               int32_t height)
{
    hevc_hv_4t_8multx4mult_lsx(src0_ptr, src_stride, src1_ptr, src2_stride,
                            dst, dst_stride, filter_x, filter_y, height, 32);
}

#define BI_MC_COPY(WIDTH)                                                 \
void ff_hevc_put_hevc_bi_pel_pixels##WIDTH##_8_lsx(uint8_t *dst,          \
                                                   ptrdiff_t dst_stride,  \
                                                   const uint8_t *src,    \
                                                   ptrdiff_t src_stride,  \
                                                   const int16_t *src_16bit, \
                                                   int height,            \
                                                   intptr_t mx,           \
                                                   intptr_t my,           \
                                                   int width)             \
{                                                                         \
    hevc_bi_copy_##WIDTH##w_lsx(src, src_stride, src_16bit, MAX_PB_SIZE,  \
                                dst, dst_stride, height);                 \
}

BI_MC_COPY(4);
BI_MC_COPY(6);
BI_MC_COPY(8);
BI_MC_COPY(12);
BI_MC_COPY(16);
BI_MC_COPY(24);
BI_MC_COPY(32);
BI_MC_COPY(48);
BI_MC_COPY(64);

#undef BI_MC_COPY

#define BI_MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                          \
void ff_hevc_put_hevc_bi_##PEL##_##DIR##WIDTH##_8_lsx(uint8_t *dst,          \
                                                      ptrdiff_t dst_stride,  \
                                                      const uint8_t *src,    \
                                                      ptrdiff_t src_stride,  \
                                                      const int16_t *src_16bit, \
                                                      int height,            \
                                                      intptr_t mx,           \
                                                      intptr_t my,           \
                                                      int width)             \
{                                                                            \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];            \
                                                                             \
    hevc_##DIR1##_##TAP##t_##WIDTH##w_lsx(src, src_stride, src_16bit,        \
                                          MAX_PB_SIZE, dst, dst_stride,      \
                                          filter, height);                   \
}

BI_MC(qpel, h, 16, 8, hz, mx);
BI_MC(qpel, h, 24, 8, hz, mx);
BI_MC(qpel, h, 32, 8, hz, mx);
BI_MC(qpel, h, 48, 8, hz, mx);
BI_MC(qpel, h, 64, 8, hz, mx);

BI_MC(qpel, v, 8, 8, vt, my);
BI_MC(qpel, v, 16, 8, vt, my);
BI_MC(qpel, v, 24, 8, vt, my);
BI_MC(qpel, v, 32, 8, vt, my);
BI_MC(qpel, v, 48, 8, vt, my);
BI_MC(qpel, v, 64, 8, vt, my);

BI_MC(epel, h, 24, 4, hz, mx);
BI_MC(epel, h, 32, 4, hz, mx);

BI_MC(epel, v, 12, 4, vt, my);
BI_MC(epel, v, 16, 4, vt, my);
BI_MC(epel, v, 24, 4, vt, my);
BI_MC(epel, v, 32, 4, vt, my);

#undef BI_MC

#define BI_MC_HV(PEL, WIDTH, TAP)                                         \
void ff_hevc_put_hevc_bi_##PEL##_hv##WIDTH##_8_lsx(uint8_t *dst,          \
                                                   ptrdiff_t dst_stride,  \
                                                   const uint8_t *src,    \
                                                   ptrdiff_t src_stride,  \
                                                   const int16_t *src_16bit, \
                                                   int height,            \
                                                   intptr_t mx,           \
                                                   intptr_t my,           \
                                                   int width)             \
{                                                                         \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];             \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];             \
                                                                          \
    hevc_hv_##TAP##t_##WIDTH##w_lsx(src, src_stride, src_16bit,           \
                                    MAX_PB_SIZE, dst, dst_stride,         \
                                    filter_x, filter_y, height);          \
}

BI_MC_HV(qpel, 8, 8);
BI_MC_HV(qpel, 16, 8);
BI_MC_HV(qpel, 24, 8);
BI_MC_HV(qpel, 32, 8);
BI_MC_HV(qpel, 48, 8);
BI_MC_HV(qpel, 64, 8);

BI_MC_HV(epel, 8, 4);
BI_MC_HV(epel, 6, 4);
BI_MC_HV(epel, 16, 4);
BI_MC_HV(epel, 24, 4);
BI_MC_HV(epel, 32, 4);

#undef BI_MC_HV
