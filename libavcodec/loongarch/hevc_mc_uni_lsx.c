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

static const uint8_t ff_hevc_mask_arr[16 * 3] __attribute__((aligned(0x40))) = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
};

static av_always_inline
void common_hz_8t_64w_lsx(const uint8_t *src, int32_t src_stride,
                          uint8_t *dst, int32_t dst_stride,
                          const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    __m128i mask0, mask1, mask2, mask3, out1, out2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i filt0, filt1, filt2, filt3;
    __m128i res0, res1, res2, res3;

    mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    src -= 3;

    /* rearranging filter */
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filt0, filt1, filt2, filt3);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    for (loop_cnt = height; loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src, 8, src, 16, src, 24,
                  src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vld, src, 32, src, 40, src, 48, src, 56,
                  src4, src5, src6, src7);
        src += src_stride;

        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src1, mask0,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src3, src3, mask0,
                  vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, res0, res1, res2, res3);
        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask2, src1, src1, mask2,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask2, src3, src3, mask2,
                  vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, res0, vec0, filt2, res1, vec1, filt2,
                  res2, vec2, filt2, res3, vec3, filt2, res0, res1, res2, res3);
        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src1, mask1,
                  vec4, vec5);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask1, src3, src3, mask1,
                  vec6, vec7);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, res0, vec4, filt1, res1, vec5, filt1,
                  res2, vec6, filt1, res3, vec7, filt1, res0, res1, res2, res3);
        DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask3, src1, src1, mask3,
                  vec4, vec5);
        DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask3, src3, src3, mask3,
                  vec6, vec7);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, res0, vec4, filt3, res1, vec5, filt3,
                  res2, vec6, filt3, res3, vec7, filt3, res0, res1, res2, res3);

        DUP2_ARG3(__lsx_vssrarni_bu_h, res1, res0, 6, res3, res2, 6,
                  out1, out2);
        __lsx_vst(out1, dst, 0);
        __lsx_vst(out2, dst, 16);

        DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src5, src5, mask0,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask0, src7, src7, mask0,
                  vec2, vec3);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec1, filt0, vec2, filt0,
                  vec3, filt0, res0, res1, res2, res3);
        DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask2, src5, src5, mask2,
                  vec0, vec1);
        DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask2, src7, src7, mask2,
                  vec2, vec3);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, res0, vec0, filt2, res1, vec1, filt2,
                  res2, vec2, filt2, res3, vec3, filt2, res0, res1, res2, res3);
        DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask1, src5, src5, mask1,
                  vec4, vec5);
        DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask1, src7, src7, mask1,
                  vec6, vec7);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, res0, vec4, filt1, res1, vec5, filt1,
                  res2, vec6, filt1, res3, vec7, filt1, res0, res1, res2, res3);
        DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask3, src5, src5, mask3,
                  vec4, vec5);
        DUP2_ARG3(__lsx_vshuf_b, src6, src6, mask3, src7, src7, mask3,
                  vec6, vec7);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, res0, vec4, filt3, res1, vec5, filt3,
                  res2, vec6, filt3, res3, vec7, filt3, res0, res1, res2, res3);

        DUP2_ARG3(__lsx_vssrarni_bu_h, res1, res0, 6, res3, res2, 6,
                  out1, out2);
        __lsx_vst(out1, dst, 32);
        __lsx_vst(out2, dst, 48);
        dst += dst_stride;
    }
}

static av_always_inline
void common_vt_8t_8w_lsx(const uint8_t *src, int32_t src_stride,
                         uint8_t *dst, int32_t dst_stride,
                         const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_4x = (src_stride << 2);
    int32_t dst_stride_4x = (dst_stride << 2);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    __m128i src65_r, src87_r, src109_r, filt0, filt1, filt2, filt3;
    __m128i tmp0, tmp1;
    __m128i out0_r, out1_r, out2_r, out3_r;

    src -= src_stride_3x;
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filt0, filt1, filt2, filt3);

    src0 = __lsx_vld(src, 0);
    DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src1, src2);
    src3 = __lsx_vldx(src, src_stride_3x);
    src += src_stride_4x;
    src4 = __lsx_vld(src, 0);
    DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src5, src6);
    src += src_stride_3x;
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
              src10_r, src32_r, src54_r, src21_r);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        src7 = __lsx_vld(src, 0);
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src8, src9);
        src10 = __lsx_vldx(src, src_stride_3x);
        src += src_stride_4x;

        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10,
                  src9, src76_r, src87_r, src98_r, src109_r);
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src21_r, filt0, src32_r,
                  filt0, src43_r, filt0, out0_r, out1_r, out2_r, out3_r);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src32_r, filt1, out1_r,
                  src43_r, filt1, out2_r, src54_r, filt1, out3_r, src65_r,
                  filt1, out0_r, out1_r, out2_r, out3_r);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src54_r, filt2, out1_r,
                  src65_r, filt2, out2_r, src76_r, filt2, out3_r, src87_r,
                  filt2, out0_r, out1_r, out2_r, out3_r);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src76_r, filt3, out1_r,
                  src87_r, filt3, out2_r, src98_r, filt3, out3_r, src109_r,
                  filt3, out0_r, out1_r, out2_r, out3_r);

        DUP2_ARG3(__lsx_vssrarni_bu_h, out1_r, out0_r, 6, out3_r, out2_r, 6,
                  tmp0, tmp1)
        __lsx_vstelm_d(tmp0, dst, 0, 0);
        __lsx_vstelm_d(tmp0, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(tmp1, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(tmp1, dst + dst_stride_3x, 0, 1);
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
void common_vt_8t_16w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                          int32_t dst_stride, const int8_t *filter,
                          int32_t height, int32_t width)
{
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt, cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filt0, filt1, filt2, filt3;
    __m128i src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    __m128i src65_r, src87_r, src109_r, src10_l, src32_l, src54_l, src76_l;
    __m128i src98_l, src21_l, src43_l, src65_l, src87_l, src109_l;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    src -= src_stride_3x;
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6, filt0,
              filt1, filt2, filt3);

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        src0 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                  src1, src2);
        src3 = __lsx_vldx(src_tmp, src_stride_3x);
        src_tmp += src_stride_4x;
        src4 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                  src5, src6);
        src_tmp += src_stride_3x;
        DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  src10_r, src32_r, src54_r, src21_r);
        DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, src43_r, src65_r);
        DUP4_ARG2(__lsx_vilvh_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  src10_l, src32_l, src54_l, src21_l);
        DUP2_ARG2(__lsx_vilvh_b, src4, src3, src6, src5, src43_l, src65_l);

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            src7 = __lsx_vld(src_tmp, 0);
            DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                      src8, src9);
            src10 = __lsx_vldx(src_tmp, src_stride_3x);
            src_tmp += src_stride_4x;
            DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10,
                      src9, src76_r, src87_r, src98_r, src109_r);
            DUP4_ARG2(__lsx_vilvh_b, src7, src6, src8, src7, src9, src8, src10,
                      src9, src76_l, src87_l, src98_l, src109_l);
            DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src21_r, filt0, src32_r,
                      filt0, src43_r, filt0, out0_r, out1_r, out2_r, out3_r);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src32_r, filt1, out1_r,
                      src43_r, filt1, out2_r, src54_r, filt1, out3_r, src65_r,
                      filt1, out0_r, out1_r, out2_r, out3_r);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src54_r, filt2, out1_r,
                      src65_r, filt2, out2_r, src76_r, filt2, out3_r, src87_r,
                      filt2, out0_r, out1_r, out2_r, out3_r);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src76_r, filt3, out1_r,
                      src87_r, filt3, out2_r, src98_r, filt3, out3_r, src109_r,
                      filt3, out0_r, out1_r, out2_r, out3_r);
            DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_l, filt0, src21_l, filt0, src32_l,
                      filt0, src43_l, filt0, out0_l, out1_l, out2_l, out3_l);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_l, src32_l, filt1, out1_l,
                      src43_l, filt1, out2_l, src54_l, filt1, out3_l, src65_l,
                      filt1, out0_l, out1_l, out2_l, out3_l);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_l, src54_l, filt2, out1_l,
                      src65_l, filt2, out2_l, src76_l, filt2, out3_l, src87_l,
                      filt2, out0_l, out1_l, out2_l, out3_l);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_l, src76_l, filt3, out1_l,
                      src87_l, filt3, out2_l, src98_l, filt3, out3_l, src109_l,
                      filt3, out0_l, out1_l, out2_l, out3_l);
            DUP4_ARG3(__lsx_vssrarni_bu_h, out0_l, out0_r, 6, out1_l, out1_r,
                      6, out2_l, out2_r, 6, out3_l, out3_r, 6,
                      tmp0, tmp1, tmp2, tmp3);
            __lsx_vst(tmp0, dst_tmp, 0);
            __lsx_vstx(tmp1, dst_tmp, dst_stride);
            __lsx_vstx(tmp2, dst_tmp, dst_stride_2x);
            __lsx_vstx(tmp3, dst_tmp, dst_stride_3x);
            dst_tmp += dst_stride_4x;

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

static void common_vt_8t_24w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_lsx(src, src_stride, dst, dst_stride, filter, height, 16);
    common_vt_8t_8w_lsx(src + 16, src_stride, dst + 16, dst_stride, filter,
                        height);
}

static void common_vt_8t_32w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_lsx(src, src_stride, dst, dst_stride, filter, height, 32);
}

static void common_vt_8t_48w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_lsx(src, src_stride, dst, dst_stride, filter, height, 48);
}

static void common_vt_8t_64w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_lsx(src, src_stride, dst, dst_stride, filter, height, 64);
}

static av_always_inline
void hevc_hv_8t_8x2_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                        int32_t dst_stride, const int8_t *filter_x,
                        const int8_t *filter_y, int32_t height, int32_t width)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;

    __m128i out;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i filt0, filt1, filt2, filt3;
    __m128i filt_h0, filt_h1, filt_h2, filt_h3;
    __m128i mask1, mask2, mask3;
    __m128i filter_vec;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l;
    __m128i dst10_r, dst32_r, dst54_r, dst76_r;
    __m128i dst10_l, dst32_l, dst54_l, dst76_l;
    __m128i dst21_r, dst43_r, dst65_r, dst87_r;
    __m128i dst21_l, dst43_l, dst65_l, dst87_l;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);

    src -= (src_stride_3x + 3);
    DUP4_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filter_x, 4,
              filter_x, 6, filt0, filt1, filt2, filt3);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP4_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filter_vec, 2,
              filter_vec, 3, filt_h0, filt_h1, filt_h2, filt_h3);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    for (cnt = width >> 3; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        src0 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                  src1, src2);
        src3 = __lsx_vldx(src_tmp, src_stride_3x);
        src_tmp += src_stride_4x;
        src4 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                  src5, src6);
        src_tmp += src_stride_3x;

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
        DUP4_ARG2(__lsx_vilvl_h, dst1, dst0, dst3, dst2, dst5, dst4, dst2,
                  dst1, dst10_r, dst32_r, dst54_r, dst21_r);
        DUP4_ARG2(__lsx_vilvh_h, dst1, dst0, dst3, dst2, dst5, dst4, dst2,
                  dst1, dst10_l, dst32_l, dst54_l, dst21_l);
        DUP2_ARG2(__lsx_vilvl_h, dst4, dst3, dst6, dst5, dst43_r, dst65_r);
        DUP2_ARG2(__lsx_vilvh_h, dst4, dst3, dst6, dst5, dst43_l, dst65_l);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            src7 = __lsx_vld(src_tmp, 0);
            src8 = __lsx_vldx(src_tmp, src_stride);
            src_tmp += src_stride_2x;

            DUP4_ARG3(__lsx_vshuf_b, src7, src7, mask0, src7, src7, mask1, src7,
                      src7, mask2, src7, src7, mask3, vec0, vec1, vec2, vec3);
            dst7 = __lsx_vdp2_h_bu_b(vec0, filt0);
            DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst7, vec1, filt1, dst7, vec2,
                      filt2, dst7, dst7);
            dst7 = __lsx_vdp2add_h_bu_b(dst7, vec3, filt3);
            dst76_r = __lsx_vilvl_h(dst7, dst6);
            dst76_l = __lsx_vilvh_h(dst7, dst6);
            DUP2_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0,
                      dst0_r, dst0_l);
            DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l,
                      dst32_l, filt_h1, dst0_r, dst54_r, filt_h2, dst0_l,
                      dst54_l, filt_h2, dst0_r, dst0_l, dst0_r, dst0_l);
            DUP2_ARG3(__lsx_vdp2add_w_h, dst0_r, dst76_r, filt_h3, dst0_l,
                      dst76_l, filt_h3, dst0_r, dst0_l);
            DUP2_ARG2(__lsx_vsrai_w, dst0_r, 6, dst0_l, 6, dst0_r, dst0_l);

            DUP4_ARG3(__lsx_vshuf_b, src8, src8, mask0, src8, src8, mask1, src8,
                      src8, mask2, src8, src8, mask3, vec0, vec1, vec2, vec3);
            dst8 = __lsx_vdp2_h_bu_b(vec0, filt0);
            DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst8, vec1, filt1, dst8, vec2,
                      filt2, dst8, dst8);
            dst8 = __lsx_vdp2add_h_bu_b(dst8, vec3, filt3);

            dst87_r = __lsx_vilvl_h(dst8, dst7);
            dst87_l = __lsx_vilvh_h(dst8, dst7);
            DUP2_ARG2(__lsx_vdp2_w_h, dst21_r, filt_h0, dst21_l, filt_h0,
                      dst1_r, dst1_l);
            DUP4_ARG3(__lsx_vdp2add_w_h, dst1_r, dst43_r, filt_h1, dst1_l,
                      dst43_l, filt_h1, dst1_r, dst65_r, filt_h2, dst1_l,
                      dst65_l, filt_h2, dst1_r, dst1_l, dst1_r, dst1_l);
            DUP2_ARG3(__lsx_vdp2add_w_h, dst1_r, dst87_r, filt_h3, dst1_l,
                      dst87_l, filt_h3, dst1_r, dst1_l);
            DUP2_ARG2(__lsx_vsrai_w, dst1_r, 6, dst1_l, 6, dst1_r, dst1_l);
            DUP4_ARG2(__lsx_vsrari_w, dst0_r, 6, dst0_l, 6,dst1_r, 6, dst1_l,
                      6, dst0_r, dst0_l, dst1_r, dst1_l);
            DUP4_ARG1(__lsx_vclip255_w, dst0_l, dst0_r, dst1_l, dst1_r,
                      dst0_l, dst0_r, dst1_l, dst1_r);
            DUP2_ARG2(__lsx_vpickev_h, dst0_l, dst0_r, dst1_l, dst1_r,
                      dst0, dst1);
            out = __lsx_vpickev_b(dst1, dst0);
            __lsx_vstelm_d(out, dst_tmp, 0, 0);
            __lsx_vstelm_d(out, dst_tmp + dst_stride, 0, 1);
            dst_tmp += dst_stride_2x;

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

static void hevc_hv_8t_8w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                              int32_t dst_stride, const int8_t *filter_x,
                              const int8_t *filter_y, int32_t height)
{
    hevc_hv_8t_8x2_lsx(src, src_stride, dst, dst_stride,
                       filter_x, filter_y, height, 8);
}

static void hevc_hv_8t_16w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_8t_8x2_lsx(src, src_stride, dst, dst_stride,
                      filter_x, filter_y, height, 16);
}

static void hevc_hv_8t_24w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_8t_8x2_lsx(src, src_stride, dst, dst_stride,
                       filter_x, filter_y, height, 24);
}

static void hevc_hv_8t_32w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_8t_8x2_lsx(src, src_stride, dst, dst_stride,
                       filter_x, filter_y, height, 32);
}

static void hevc_hv_8t_48w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_8t_8x2_lsx(src, src_stride, dst, dst_stride,
                       filter_x, filter_y, height, 48);
}

static void hevc_hv_8t_64w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_8t_8x2_lsx(src, src_stride, dst, dst_stride,
                       filter_x, filter_y, height, 64);
}

static av_always_inline
void common_vt_4t_24w_lsx(const uint8_t *src, int32_t src_stride,
                          uint8_t *dst, int32_t dst_stride,
                          const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    const uint8_t *_src;

    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i src11, filt0, filt1;
    __m128i src10_r, src32_r, src76_r, src98_r, src21_r, src43_r, src87_r;
    __m128i src109_r, src10_l, src32_l, src21_l, src43_l;
    __m128i out0_r, out1_r, out2_r, out3_r, out0_l, out1_l;
    __m128i out1, out2, out3, out4;

    src -= src_stride;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    _src = src + 16;

    /* 16 width */
    src0 = __lsx_vld(src, 0);
    DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src1, src2);
    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_r, src21_r);
    DUP2_ARG2(__lsx_vilvh_b, src1, src0, src2, src1, src10_l, src21_l);

    /* 8 width */
    src6 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride_2x, src7, src8);
    src += src_stride_3x;
    _src += src_stride_3x;
    DUP2_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src76_r, src87_r);

    for (loop_cnt = 8; loop_cnt--;) {
        /* 16 width */
        DUP2_ARG2(__lsx_vld, src, 0, _src, 0, src3, src9);
        DUP2_ARG2(__lsx_vldx, src, src_stride, _src, src_stride, src4, src10);
        DUP2_ARG2(__lsx_vilvl_b, src3, src2, src4, src3, src32_r, src43_r);
        DUP2_ARG2(__lsx_vilvh_b, src3, src2, src4, src3, src32_l, src43_l);

        /* 8 width */
        src += src_stride_2x;
        _src += src_stride_2x;
        DUP2_ARG2(__lsx_vilvl_b, src9, src8, src10, src9, src98_r, src109_r);

        /* 16 width */
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src10_l, filt0, src21_r,
                  filt0, src21_l, filt0, out0_r, out0_l, out1_r, out1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src32_r, filt1, out0_l, src32_l,
                  filt1, out1_r, src43_r, filt1, out1_l, src43_l, filt1,
                  out0_r, out0_l, out1_r, out1_l);

        /* 8 width */
        DUP2_ARG2(__lsx_vdp2_h_bu_b, src76_r, filt0, src87_r, filt0,
                  out2_r, out3_r);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b, out2_r, src98_r, filt1, out3_r,
                  src109_r, filt1, out2_r, out3_r);

        /* 16 + 8 width */
        DUP4_ARG3(__lsx_vssrarni_bu_h, out0_l, out0_r, 6, out2_r, out2_r, 6,
                out3_r, out3_r, 6, out1_l, out1_r, 6, out1, out2, out3, out4);
        __lsx_vst(out1, dst, 0);
        __lsx_vstelm_d(out2, dst, 16, 0);
        dst += dst_stride;
        __lsx_vst(out4, dst, 0);
        __lsx_vstelm_d(out3, dst, 16, 0);
        dst += dst_stride;

        /* 16 width */
        DUP2_ARG2(__lsx_vld, src, 0, _src, 0, src5, src11);
        DUP2_ARG2(__lsx_vldx, src, src_stride, _src, src_stride, src2, src8);
        DUP2_ARG2(__lsx_vilvl_b, src5, src4, src2, src5, src10_r, src21_r);
        DUP2_ARG2(__lsx_vilvh_b, src5, src4, src2, src5, src10_l, src21_l);

        /* 8 width */
        src += src_stride_2x;
        _src += src_stride_2x;
        DUP2_ARG2(__lsx_vilvl_b, src11, src10, src8, src11, src76_r, src87_r);

        /* 16 width */
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src32_r, filt0, src32_l, filt0, src43_r,
                  filt0, src43_l, filt0, out0_r, out0_l, out1_r, out1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src10_r, filt1, out0_l, src10_l,
                  filt1, out1_r, src21_r, filt1, out1_l, src21_l, filt1,
                  out0_r, out0_l, out1_r, out1_l);

        /* 8 width */
        DUP2_ARG2(__lsx_vdp2_h_bu_b, src98_r, filt0, src109_r, filt0,
                  out2_r, out3_r);
        DUP2_ARG3(__lsx_vdp2add_h_bu_b, out2_r, src76_r, filt1, out3_r,
                  src87_r, filt1, out2_r, out3_r);

        /* 16 + 8 width */
        DUP4_ARG3(__lsx_vssrarni_bu_h, out0_l, out0_r, 6, out2_r, out2_r, 6,
                  out1_l, out1_r, 6, out3_r, out3_r, 6, out1, out2, out3, out4);

        __lsx_vst(out1, dst, 0);
        __lsx_vstelm_d(out2, dst, 16, 0);
        dst += dst_stride;
        __lsx_vst(out3, dst, 0);
        __lsx_vstelm_d(out4, dst, 16, 0);
        dst += dst_stride;
    }
}

static av_always_inline
void common_vt_4t_32w_lsx(const uint8_t *src, int32_t src_stride,
                          uint8_t *dst, int32_t dst_stride,
                          const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    int32_t src_stride_2x = (src_stride << 1);
    int32_t dst_stride_2x = (dst_stride << 1);
    int32_t src_stride_3x = src_stride_2x + src_stride;
    const uint8_t *_src;

    __m128i src0, src1, src2, src3, src4, src6, src7, src8, src9, src10;
    __m128i src10_r, src32_r, src76_r, src98_r;
    __m128i src21_r, src43_r, src87_r, src109_r;
    __m128i out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;
    __m128i src10_l, src32_l, src76_l, src98_l;
    __m128i src21_l, src43_l, src87_l, src109_l;
    __m128i filt0, filt1;
    __m128i out1, out2;

    src -= src_stride;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    _src = src + 16;

    /* 16 width */
    src0 = __lsx_vld(src, 0);
    DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src1, src2);

    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_r, src21_r);
    DUP2_ARG2(__lsx_vilvh_b, src1, src0, src2, src1, src10_l, src21_l);

    /* next 16 width */
    src6 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride_2x, src7, src8);
    src += src_stride_3x;
    _src += src_stride_3x;

    DUP2_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src76_r, src87_r);
    DUP2_ARG2(__lsx_vilvh_b, src7, src6, src8, src7, src76_l, src87_l);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        /* 16 width */
        DUP2_ARG2(__lsx_vld, src, 0, _src, 0, src3, src9);
        DUP2_ARG2(__lsx_vldx, src, src_stride, _src, src_stride, src4, src10);
        DUP2_ARG2(__lsx_vilvl_b, src3, src2, src4, src3, src32_r, src43_r);
        DUP2_ARG2(__lsx_vilvh_b, src3, src2, src4, src3, src32_l, src43_l);

        /* 16 width */
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src10_r, filt0, src10_l, filt0, src21_r,
                  filt0, src21_l, filt0, out0_r, out0_l, out1_r, out1_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out0_r, src32_r, filt1, out0_l, src32_l,
                  filt1, out1_r, src43_r, filt1, out1_l, src43_l, filt1,
                  out0_r, out0_l, out1_r, out1_l);

        DUP2_ARG3(__lsx_vssrarni_bu_h, out0_l, out0_r, 6, out1_l, out1_r, 6,
                  out1, out2);
        __lsx_vst(out1, dst, 0);
        __lsx_vstx(out2, dst, dst_stride);

        src10_r = src32_r;
        src21_r = src43_r;
        src10_l = src32_l;
        src21_l = src43_l;
        src2 = src4;

        /* next 16 width */
        src += src_stride_2x;
        _src += src_stride_2x;
        DUP2_ARG2(__lsx_vilvl_b, src9, src8, src10, src9, src98_r, src109_r);
        DUP2_ARG2(__lsx_vilvh_b, src9, src8, src10, src9, src98_l, src109_l);

        /* next 16 width */
        DUP4_ARG2(__lsx_vdp2_h_bu_b, src76_r, filt0, src76_l, filt0, src87_r,
                  filt0, src87_l, filt0, out2_r, out2_l, out3_r, out3_l);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, out2_r, src98_r, filt1, out2_l, src98_l,
                  filt1, out3_r, src109_r, filt1, out3_l, src109_l, filt1,
                  out2_r, out2_l, out3_r, out3_l);

        /* next 16 width */
        DUP2_ARG3(__lsx_vssrarni_bu_h, out2_l, out2_r, 6, out3_l, out3_r, 6,
                  out1, out2);
        __lsx_vst(out1, dst, 16);
        __lsx_vst(out2, dst + dst_stride, 16);

        dst += dst_stride_2x;

        src76_r = src98_r;
        src87_r = src109_r;
        src76_l = src98_l;
        src87_l = src109_l;
        src8 = src10;
    }
}

static av_always_inline
void hevc_hv_4t_8x2_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                        int32_t dst_stride, const int8_t *filter_x,
                        const int8_t *filter_y)
{
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    __m128i out;
    __m128i src0, src1, src2, src3, src4;
    __m128i filt0, filt1;
    __m128i filt_h0, filt_h1, filter_vec;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    __m128i dst0, dst1, dst2, dst3, dst4;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l;
    __m128i dst10_r, dst32_r, dst21_r, dst43_r;
    __m128i dst10_l, dst32_l, dst21_l, dst43_l;
    __m128i out0_r, out1_r;

    src -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask1 = __lsx_vaddi_bu(mask0, 2);
    src0 = __lsx_vld(src, 0);
    DUP4_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src,
              src_stride_3x, src, src_stride_4x, src1, src2, src3, src4);

    DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, src1, src1,
              mask0, src1, src1, mask1, vec0, vec1, vec2, vec3);
    DUP4_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, src3, src3,
              mask0, src3, src3, mask1, vec4, vec5, vec6, vec7);
    DUP2_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, vec8, vec9);

    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0, vec6,
              filt0, dst0, dst1, dst2, dst3);
    dst4 = __lsx_vdp2_h_bu_b(vec8, filt0);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec3, filt1, dst2,
              vec5, filt1, dst3, vec7, filt1, dst0, dst1, dst2, dst3);
    dst4 = __lsx_vdp2add_h_bu_b(dst4, vec9, filt1);
    DUP4_ARG2(__lsx_vilvl_h, dst1, dst0, dst2, dst1, dst3, dst2, dst4, dst3,
              dst10_r, dst21_r, dst32_r, dst43_r);
    DUP4_ARG2(__lsx_vilvh_h, dst1, dst0, dst2, dst1, dst3, dst2, dst4, dst3,
              dst10_l, dst21_l, dst32_l, dst43_l);
    DUP4_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst10_l, filt_h0, dst21_r,
              filt_h0, dst21_l, filt_h0, dst0_r, dst0_l, dst1_r, dst1_l);
    DUP4_ARG3(__lsx_vdp2add_w_h, dst0_r, dst32_r, filt_h1, dst0_l, dst32_l,
              filt_h1, dst1_r, dst43_r, filt_h1, dst1_l, dst43_l, filt_h1,
              dst0_r, dst0_l, dst1_r, dst1_l);
    DUP2_ARG3(__lsx_vsrani_h_w, dst0_l, dst0_r, 6, dst1_l, dst1_r, 6,
              out0_r, out1_r);
    out = __lsx_vssrarni_bu_h(out1_r, out0_r, 6);
    __lsx_vstelm_d(out, dst, 0, 0);
    __lsx_vstelm_d(out, dst + dst_stride, 0, 1);
}

static av_always_inline
void hevc_hv_4t_8multx4_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                            int32_t dst_stride, const int8_t *filter_x,
                            const int8_t *filter_y, int32_t width8mult)
{
    uint32_t cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i out0, out1;
    __m128i src0, src1, src2, src3, src4, src5, src6, mask0, mask1;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i filt0, filt1, filt_h0, filt_h1, filter_vec;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, tmp0, tmp1, tmp2, tmp3;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    __m128i dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;

    src -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    mask1 = __lsx_vaddi_bu(mask0, 2);

    for (cnt = width8mult; cnt--;) {
        src0 = __lsx_vld(src, 0);
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src1, src2);
        src3 = __lsx_vldx(src, src_stride_3x);
        src += src_stride_4x;
        src4 = __lsx_vld(src, 0);
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src5, src6);
        src += (8 - src_stride_4x);
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

        DUP4_ARG2(__lsx_vilvl_h, dst3, dst2, dst4, dst3, dst5, dst4, dst6,
                  dst5, dst32_r, dst43_r, dst54_r, dst65_r);
        DUP4_ARG2(__lsx_vilvh_h, dst3, dst2, dst4, dst3, dst5, dst4, dst6,
                  dst5, dst32_l, dst43_l, dst54_l, dst65_l);

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

        DUP4_ARG3(__lsx_vsrani_h_w, dst0_l, dst0_r, 6, dst1_l, dst1_r, 6,
                  dst2_l, dst2_r, 6, dst3_l, dst3_r, 6, tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG3(__lsx_vssrarni_bu_h, tmp1, tmp0, 6, tmp3, tmp2, 6, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
        __lsx_vstelm_d(out1, dst + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(out1, dst + dst_stride_3x, 0, 1);
        dst += 8;
    }
}

static av_always_inline
void hevc_hv_4t_8x6_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                        int32_t dst_stride, const int8_t *filter_x,
                        const int8_t *filter_y)
{
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    __m128i out0, out1, out2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i filt0, filt1;
    __m128i filt_h0, filt_h1, filter_vec;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    __m128i vec10, vec11, vec12, vec13, vec14, vec15, vec16, vec17;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst4_r, dst4_l, dst5_r, dst5_l;
    __m128i dst10_r, dst32_r, dst10_l, dst32_l;
    __m128i dst21_r, dst43_r, dst21_l, dst43_l;
    __m128i dst54_r, dst54_l, dst65_r, dst65_l;
    __m128i dst76_r, dst76_l, dst87_r, dst87_l;
    __m128i out0_r, out1_r, out2_r, out3_r, out4_r, out5_r;

    src -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask1 = __lsx_vaddi_bu(mask0, 2);

    src0 = __lsx_vld(src, 0);
    DUP4_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x,src,
              src_stride_3x, src, src_stride_4x, src1, src2, src3, src4);
    src += src_stride_4x;
    DUP4_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x,src,
              src_stride_3x, src, src_stride_4x, src5, src6, src7, src8);

    DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, src1, src1,
              mask0, src1, src1, mask1, vec0, vec1, vec2, vec3);
    DUP4_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, src3, src3,
              mask0, src3, src3, mask1, vec4, vec5, vec6, vec7);
    DUP4_ARG3(__lsx_vshuf_b, src4, src4, mask0, src4, src4, mask1, src5, src5,
              mask0, src5, src5, mask1, vec8, vec9, vec10, vec11);
    DUP4_ARG3(__lsx_vshuf_b, src6, src6, mask0, src6, src6, mask1, src7, src7,
              mask0, src7, src7, mask1, vec12, vec13, vec14, vec15);
    DUP2_ARG3(__lsx_vshuf_b, src8, src8, mask0, src8, src8, mask1, vec16, vec17);

    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0, vec6,
              filt0, dst0, dst1, dst2, dst3);
    DUP4_ARG2(__lsx_vdp2_h_bu_b, vec8, filt0, vec10, filt0, vec12, filt0, vec14,
              filt0, dst4, dst5, dst6, dst7);
    dst8 = __lsx_vdp2_h_bu_b(vec16, filt0);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst0, vec1, filt1, dst1, vec3, filt1, dst2,
              vec5, filt1, dst3, vec7, filt1, dst0, dst1, dst2, dst3);
    DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst4, vec9, filt1, dst5, vec11, filt1, dst6,
              vec13, filt1, dst7, vec15, filt1, dst4, dst5, dst6, dst7);
    dst8 = __lsx_vdp2add_h_bu_b(dst8, vec17, filt1);

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

    DUP4_ARG3(__lsx_vsrani_h_w, dst0_l, dst0_r, 6, dst1_l, dst1_r, 6, dst2_l,
              dst2_r, 6, dst3_l, dst3_r, 6, out0_r, out1_r, out2_r, out3_r);
    DUP2_ARG3(__lsx_vsrani_h_w, dst4_l, dst4_r, 6, dst5_l, dst5_r, 6,
              out4_r, out5_r);
    DUP2_ARG3(__lsx_vssrarni_bu_h, out1_r, out0_r, 6, out3_r, out2_r, 6,
              out0, out1);
    out2 = __lsx_vssrarni_bu_h(out5_r, out4_r, 6);

    __lsx_vstelm_d(out0, dst, 0, 0);
    __lsx_vstelm_d(out0, dst + dst_stride, 0, 1);
    __lsx_vstelm_d(out1, dst + dst_stride_2x, 0, 0);
    __lsx_vstelm_d(out1, dst + dst_stride_3x, 0, 1);
    dst += dst_stride_4x;
    __lsx_vstelm_d(out2, dst, 0, 0);
    __lsx_vstelm_d(out2, dst + dst_stride, 0, 1);
}

static av_always_inline
void hevc_hv_4t_8multx4mult_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                                int32_t dst_stride, const int8_t *filter_x,
                                const int8_t *filter_y, int32_t height,
                                int32_t width8mult)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i out0, out1;
    __m128i src0, src1, src2, src3, src4, src5, src6;
    __m128i filt0, filt1;
    __m128i filt_h0, filt_h1, filter_vec;
    __m128i mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    __m128i mask1;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst10_r, dst32_r, dst21_r, dst43_r;
    __m128i dst10_l, dst32_l, dst21_l, dst43_l;
    __m128i dst54_r, dst54_l, dst65_r, dst65_l, dst6;
    __m128i out0_r, out1_r, out2_r, out3_r;

    src -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);
    mask1 = __lsx_vaddi_bu(mask0, 2);

    for (cnt = width8mult; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        src0 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                  src1, src2);
        src_tmp += src_stride_3x;

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

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            src3 = __lsx_vld(src_tmp, 0);
            DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                      src4, src5);
            src6 = __lsx_vldx(src_tmp, src_stride_3x);
            src_tmp += src_stride_4x;

            DUP4_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, src4,
                      src4, mask0, src4, src4, mask1, vec0, vec1, vec2, vec3);
            DUP4_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, src6,
                      src6, mask0, src6, src6, mask1, vec4, vec5, vec6, vec7);

            DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0,
                      vec6, filt0, dst3, dst4, dst5, dst6);
            DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst3, vec1, filt1, dst4, vec3,
                      filt1, dst5, vec5, filt1, dst6, vec7, filt1,
                      dst3, dst4, dst5, dst6);

            DUP4_ARG2(__lsx_vilvl_h, dst3, dst2, dst4, dst3, dst5, dst4,
                      dst6, dst5, dst32_r, dst43_r, dst54_r, dst65_r);
            DUP4_ARG2(__lsx_vilvh_h, dst3, dst2, dst4, dst3, dst5, dst4,
                      dst6, dst5, dst32_l, dst43_l, dst54_l, dst65_l);

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

            DUP4_ARG3(__lsx_vsrani_h_w, dst0_l, dst0_r, 6, dst1_l, dst1_r, 6,
                      dst2_l, dst2_r, 6, dst3_l, dst3_r, 6, out0_r, out1_r,
                      out2_r, out3_r);
            DUP2_ARG3(__lsx_vssrarni_bu_h, out1_r, out0_r, 6, out3_r, out2_r,
                      6, out0, out1);
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
        src += 8;
        dst += 8;
    }
}

static
void hevc_hv_4t_8w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                       int32_t dst_stride, const int8_t *filter_x,
                       const int8_t *filter_y, int32_t height)
{
    if (2 == height) {
        hevc_hv_4t_8x2_lsx(src, src_stride, dst, dst_stride, filter_x, filter_y);
    } else if (4 == height) {
        hevc_hv_4t_8multx4_lsx(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, 1);
    } else if (6 == height) {
        hevc_hv_4t_8x6_lsx(src, src_stride, dst, dst_stride, filter_x, filter_y);
    } else if (0 == (height & 0x03)) {
        hevc_hv_4t_8multx4mult_lsx(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 1);
    }
}

static av_always_inline
void hevc_hv_4t_12w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                        int32_t dst_stride, const int8_t *filter_x,
                        const int8_t *filter_y, int32_t height)
{
    uint32_t loop_cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    __m128i out0, out1;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i mask0, mask1, mask2, mask3;
    __m128i filt0, filt1, filt_h0, filt_h1, filter_vec, tmp0, tmp1, tmp2, tmp3;
    __m128i dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6;
    __m128i dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    __m128i dst76_r, dst98_r, dst87_r, dst109_r;
    __m128i dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    __m128i dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    __m128i dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    src -= (src_stride + 1);
    DUP2_ARG2(__lsx_vldrepl_h, filter_x, 0, filter_x, 2, filt0, filt1);

    filter_vec = __lsx_vld(filter_y, 0);
    filter_vec = __lsx_vsllwil_h_b(filter_vec, 0);
    DUP2_ARG2(__lsx_vreplvei_w, filter_vec, 0, filter_vec, 1, filt_h0, filt_h1);

    mask0 = __lsx_vld(ff_hevc_mask_arr, 0);
    mask1 = __lsx_vaddi_bu(mask0, 2);

    src_tmp = src;
    dst_tmp = dst;

    src0 = __lsx_vld(src_tmp, 0);
    DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
              src1, src2);
    src_tmp += src_stride_3x;

    DUP2_ARG3(__lsx_vshuf_b, src0, src0, mask0, src0, src0, mask1, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src1, src1, mask0, src1, src1, mask1, vec2, vec3);
    DUP2_ARG3(__lsx_vshuf_b, src2, src2, mask0, src2, src2, mask1, vec4, vec5);

    DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, dsth0, dsth1);
    dsth2 = __lsx_vdp2_h_bu_b(vec4, filt0);
    DUP2_ARG3(__lsx_vdp2add_h_bu_b, dsth0, vec1, filt1, dsth1, vec3, filt1,
              dsth0, dsth1);
    dsth2 = __lsx_vdp2add_h_bu_b(dsth2, vec5, filt1);

    DUP2_ARG2(__lsx_vilvl_h, dsth1, dsth0, dsth2, dsth1, dst10_r, dst21_r);
    DUP2_ARG2(__lsx_vilvh_h, dsth1, dsth0, dsth2, dsth1, dst10_l, dst21_l);

    for (loop_cnt = 4; loop_cnt--;) {
        src3 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride_2x,
                  src4, src5);
        src6 = __lsx_vldx(src_tmp, src_stride_3x);
        src_tmp += src_stride_4x;

        DUP4_ARG3(__lsx_vshuf_b, src3, src3, mask0, src3, src3, mask1, src4,
                  src4, mask0, src4, src4, mask1, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vshuf_b, src5, src5, mask0, src5, src5, mask1, src6,
                  src6, mask0, src6, src6, mask1, vec4, vec5, vec6, vec7);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0,
                  vec6, filt0, dsth3, dsth4, dsth5, dsth6);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dsth3, vec1, filt1, dsth4,
                  vec3, filt1, dsth5, vec5, filt1, dsth6, vec7, filt1,
                  dsth3, dsth4, dsth5, dsth6);

        DUP4_ARG2(__lsx_vilvl_h, dsth3, dsth2, dsth4, dsth3, dsth5, dsth4,
                  dsth6, dsth5, dst32_r, dst43_r, dst54_r, dst65_r);
        DUP4_ARG2(__lsx_vilvh_h, dsth3, dsth2, dsth4, dsth3, dsth5, dsth4,
                  dsth6, dsth5, dst32_l, dst43_l, dst54_l, dst65_l);

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

        DUP4_ARG3(__lsx_vsrani_h_w, dst0_l, dst0_r, 6, dst1_l, dst1_r, 6, dst2_l,
                  dst2_r, 6, dst3_l, dst3_r, 6, tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG3(__lsx_vssrarni_bu_h, tmp1, tmp0, 6, tmp3, tmp2, 6, out0, out1);

        __lsx_vstelm_d(out0, dst_tmp, 0, 0);
        __lsx_vstelm_d(out0, dst_tmp + dst_stride, 0, 1);
        __lsx_vstelm_d(out1, dst_tmp + dst_stride_2x, 0, 0);
        __lsx_vstelm_d(out1, dst_tmp + dst_stride_3x, 0, 1);
        dst_tmp += dst_stride_4x;

        dst10_r = dst54_r;
        dst10_l = dst54_l;
        dst21_r = dst65_r;
        dst21_l = dst65_l;
        dsth2 = dsth6;
    }

    src += 8;
    dst += 8;

    mask2 = __lsx_vld(ff_hevc_mask_arr, 16);
    mask3 = __lsx_vaddi_bu(mask2, 2);

    src0 = __lsx_vld(src, 0);
    DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src1, src2);
    src += src_stride_3x;
    DUP2_ARG3(__lsx_vshuf_b, src1, src0, mask2, src1, src0, mask3, vec0, vec1);
    DUP2_ARG3(__lsx_vshuf_b, src2, src1, mask2, src2, src1, mask3, vec2, vec3);

    DUP2_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, dst10, dst21);
    DUP2_ARG3(__lsx_vdp2add_h_bu_b, dst10, vec1, filt1, dst21, vec3, filt1,
              dst10, dst21);

    dst10_r = __lsx_vilvl_h(dst21, dst10);
    dst21_r = __lsx_vilvh_h(dst21, dst10);
    dst22 = __lsx_vreplvei_d(dst21, 1);

    for (loop_cnt = 2; loop_cnt--;) {
        src3 = __lsx_vld(src, 0);
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src4, src5);
        src6 = __lsx_vldx(src, src_stride_3x);
        src += src_stride_4x;
        src7 = __lsx_vld(src, 0);
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src8, src9);
        src10 = __lsx_vldx(src, src_stride_3x);
        src += src_stride_4x;
        DUP4_ARG3(__lsx_vshuf_b, src7, src3, mask2, src7, src3, mask3, src8,
                  src4, mask2, src8, src4, mask3, vec0, vec1, vec2, vec3);
        DUP4_ARG3(__lsx_vshuf_b, src9, src5, mask2, src9, src5, mask3, src10,
                  src6, mask2, src10, src6, mask3, vec4, vec5, vec6, vec7);

        DUP4_ARG2(__lsx_vdp2_h_bu_b, vec0, filt0, vec2, filt0, vec4, filt0,
                  vec6, filt0, dst73, dst84, dst95, dst106);
        DUP4_ARG3(__lsx_vdp2add_h_bu_b, dst73, vec1, filt1, dst84, vec3,
                  filt1, dst95, vec5, filt1, dst106, vec7, filt1,
                  dst73, dst84, dst95, dst106);

        dst32_r = __lsx_vilvl_h(dst73, dst22);
        DUP2_ARG2(__lsx_vilvl_h, dst84, dst73, dst95, dst84, dst43_r, dst54_r);
        DUP2_ARG2(__lsx_vilvh_h, dst84, dst73, dst95, dst84, dst87_r, dst98_r);
        dst65_r = __lsx_vilvl_h(dst106, dst95);
        dst109_r = __lsx_vilvh_h(dst106, dst95);
        dst22 = __lsx_vreplvei_d(dst73, 1);
        dst76_r = __lsx_vilvl_h(dst22, dst106);

        DUP4_ARG2(__lsx_vdp2_w_h, dst10_r, filt_h0, dst21_r, filt_h0, dst32_r,
                  filt_h0, dst43_r, filt_h0, dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lsx_vdp2_w_h, dst54_r, filt_h0, dst65_r, filt_h0, dst76_r,
                  filt_h0, dst87_r, filt_h0, dst4, dst5, dst6, dst7);
        DUP4_ARG3(__lsx_vdp2add_w_h, dst0, dst32_r, filt_h1, dst1, dst43_r,
                  filt_h1, dst2, dst54_r, filt_h1, dst3, dst65_r, filt_h1,
                  dst0, dst1, dst2, dst3);
        DUP4_ARG3(__lsx_vdp2add_w_h, dst4, dst76_r, filt_h1, dst5, dst87_r,
                  filt_h1, dst6, dst98_r, filt_h1, dst7, dst109_r, filt_h1,
                  dst4, dst5, dst6, dst7);

        DUP4_ARG3(__lsx_vsrani_h_w, dst1, dst0, 6, dst3, dst2, 6, dst5, dst4,
                  6, dst7, dst6, 6, tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG3(__lsx_vssrarni_bu_h, tmp1, tmp0, 6, tmp3, tmp2, 6, out0, out1);

        __lsx_vstelm_w(out0, dst, 0, 0);
        __lsx_vstelm_w(out0, dst + dst_stride, 0, 1);
        __lsx_vstelm_w(out0, dst + dst_stride_2x, 0, 2);
        __lsx_vstelm_w(out0, dst + dst_stride_3x, 0, 3);
        dst += dst_stride_4x;
        __lsx_vstelm_w(out1, dst, 0, 0);
        __lsx_vstelm_w(out1, dst + dst_stride, 0, 1);
        __lsx_vstelm_w(out1, dst + dst_stride_2x, 0, 2);
        __lsx_vstelm_w(out1, dst + dst_stride_3x, 0, 3);
        dst += dst_stride_4x;

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = __lsx_vreplvei_d(dst106, 1);
    }
}

static void hevc_hv_4t_16w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    if (4 == height) {
        hevc_hv_4t_8multx4_lsx(src, src_stride, dst, dst_stride, filter_x,
                               filter_y, 2);
    } else {
        hevc_hv_4t_8multx4mult_lsx(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 2);
    }
}

static void hevc_hv_4t_24w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_4t_8multx4mult_lsx(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 3);
}

static void hevc_hv_4t_32w_lsx(const uint8_t *src, int32_t src_stride, uint8_t *dst,
                               int32_t dst_stride, const int8_t *filter_x,
                               const int8_t *filter_y, int32_t height)
{
    hevc_hv_4t_8multx4mult_lsx(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 4);
}

#define UNI_MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                           \
void ff_hevc_put_hevc_uni_##PEL##_##DIR##WIDTH##_8_lsx(uint8_t *dst,           \
                                                       ptrdiff_t dst_stride,   \
                                                       const uint8_t *src,     \
                                                       ptrdiff_t src_stride,   \
                                                       int height,             \
                                                       intptr_t mx,            \
                                                       intptr_t my,            \
                                                       int width)              \
{                                                                              \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR];                  \
                                                                               \
    common_##DIR1##_##TAP##t_##WIDTH##w_lsx(src, src_stride, dst, dst_stride,  \
                                            filter, height);                   \
}

UNI_MC(qpel, h, 64, 8, hz, mx);

UNI_MC(qpel, v, 24, 8, vt, my);
UNI_MC(qpel, v, 32, 8, vt, my);
UNI_MC(qpel, v, 48, 8, vt, my);
UNI_MC(qpel, v, 64, 8, vt, my);

UNI_MC(epel, v, 24, 4, vt, my);
UNI_MC(epel, v, 32, 4, vt, my);

#undef UNI_MC

#define UNI_MC_HV(PEL, WIDTH, TAP)                                         \
void ff_hevc_put_hevc_uni_##PEL##_hv##WIDTH##_8_lsx(uint8_t *dst,          \
                                                    ptrdiff_t dst_stride,  \
                                                    const uint8_t *src,    \
                                                    ptrdiff_t src_stride,  \
                                                    int height,            \
                                                    intptr_t mx,           \
                                                    intptr_t my,           \
                                                    int width)             \
{                                                                          \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx];                  \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my];                  \
                                                                           \
    hevc_hv_##TAP##t_##WIDTH##w_lsx(src, src_stride, dst, dst_stride,  \
                                    filter_x, filter_y, height);       \
}

UNI_MC_HV(qpel, 8, 8);
UNI_MC_HV(qpel, 16, 8);
UNI_MC_HV(qpel, 24, 8);
UNI_MC_HV(qpel, 32, 8);
UNI_MC_HV(qpel, 48, 8);
UNI_MC_HV(qpel, 64, 8);

UNI_MC_HV(epel, 8, 4);
UNI_MC_HV(epel, 12, 4);
UNI_MC_HV(epel, 16, 4);
UNI_MC_HV(epel, 24, 4);
UNI_MC_HV(epel, 32, 4);

#undef UNI_MC_HV
