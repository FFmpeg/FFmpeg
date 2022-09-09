/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hecai Yuan <yuanhecai@loongson.cn>
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
#include "libavcodec/vp8dsp.h"
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "vp8dsp_loongarch.h"

static const uint8_t mc_filt_mask_arr[16 * 3] = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
};

static const int8_t subpel_filters_lsx[7][8] = {
    {-6, 123, 12, -1, 0, 0, 0, 0},
    {2, -11, 108, 36, -8, 1, 0, 0},     /* New 1/4 pel 6 tap filter */
    {-9, 93, 50, -6, 0, 0, 0, 0},
    {3, -16, 77, 77, -16, 3, 0, 0},     /* New 1/2 pel 6 tap filter */
    {-6, 50, 93, -9, 0, 0, 0, 0},
    {1, -8, 36, 108, -11, 2, 0, 0},     /* New 1/4 pel 6 tap filter */
    {-1, 12, 123, -6, 0, 0, 0, 0},
};

#define DPADD_SH3_SH(in0, in1, in2, coeff0, coeff1, coeff2)         \
( {                                                                 \
    __m128i out0_m;                                                 \
                                                                    \
    out0_m = __lsx_vdp2_h_b(in0, coeff0);                           \
    out0_m = __lsx_vdp2add_h_b(out0_m, in1, coeff1);                \
    out0_m = __lsx_vdp2add_h_b(out0_m, in2, coeff2);                \
                                                                    \
    out0_m;                                                         \
} )

#define VSHF_B3_SB(in0, in1, in2, in3, in4, in5, mask0, mask1, mask2,  \
                out0, out1, out2)                                      \
{                                                                      \
    DUP2_ARG3(__lsx_vshuf_b, in1, in0, mask0, in3, in2, mask1,         \
              out0, out1);                                             \
    out2 = __lsx_vshuf_b(in5, in4, mask2);                             \
}

#define HORIZ_6TAP_FILT(src0, src1, mask0, mask1, mask2,                 \
                        filt_h0, filt_h1, filt_h2)                       \
( {                                                                      \
    __m128i vec0_m, vec1_m, vec2_m;                                      \
    __m128i hz_out_m;                                                    \
                                                                         \
    VSHF_B3_SB(src0, src1, src0, src1, src0, src1, mask0, mask1, mask2,  \
               vec0_m, vec1_m, vec2_m);                                  \
    hz_out_m = DPADD_SH3_SH(vec0_m, vec1_m, vec2_m,                      \
                            filt_h0, filt_h1, filt_h2);                  \
                                                                         \
    hz_out_m = __lsx_vsrari_h(hz_out_m, 7);                              \
    hz_out_m = __lsx_vsat_h(hz_out_m, 7);                                \
                                                                         \
    hz_out_m;                                                            \
} )

#define HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3,                            \
                                   mask0, mask1, mask2,                               \
                                   filt0, filt1, filt2,                               \
                                   out0, out1, out2, out3)                            \
{                                                                                     \
    __m128i vec0_m, vec1_m, vec2_m, vec3_m, vec4_m, vec5_m, vec6_m, vec7_m;           \
                                                                                      \
    DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src1, mask0, src2, src2,        \
              mask0, src3, src3, mask0, vec0_m, vec1_m, vec2_m, vec3_m);              \
    DUP4_ARG2(__lsx_vdp2_h_b, vec0_m, filt0, vec1_m, filt0, vec2_m, filt0,            \
              vec3_m, filt0, out0, out1, out2, out3);                                 \
    DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src1, mask1, src2, src2,        \
              mask1, src3, src3, mask1, vec0_m, vec1_m, vec2_m, vec3_m);              \
    DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask2, src1, src1, mask2, src2, src2,        \
              mask2, src3, src3, mask2, vec4_m, vec5_m, vec6_m, vec7_m);              \
    DUP4_ARG3(__lsx_vdp2add_h_b, out0, vec0_m, filt1, out1, vec1_m, filt1,            \
              out2, vec2_m, filt1, out3, vec3_m, filt1, out0, out1, out2, out3);      \
    DUP4_ARG3(__lsx_vdp2add_h_b, out0, vec4_m, filt2, out1, vec5_m, filt2,            \
              out2, vec6_m, filt2, out3, vec7_m, filt2, out0, out1, out2, out3);      \
}

#define FILT_4TAP_DPADD_S_H(vec0, vec1, filt0, filt1)           \
( {                                                             \
    __m128i tmp0;                                               \
                                                                \
    tmp0 = __lsx_vdp2_h_b(vec0, filt0);                         \
    tmp0 = __lsx_vdp2add_h_b(tmp0, vec1, filt1);                \
                                                                \
    tmp0;                                                       \
} )

#define HORIZ_4TAP_FILT(src0, src1, mask0, mask1, filt_h0, filt_h1)    \
( {                                                                    \
    __m128i vec0_m, vec1_m;                                            \
    __m128i hz_out_m;                                                  \
    DUP2_ARG3(__lsx_vshuf_b, src1, src0, mask0, src1, src0, mask1,     \
              vec0_m, vec1_m);                                         \
    hz_out_m = FILT_4TAP_DPADD_S_H(vec0_m, vec1_m, filt_h0, filt_h1);  \
                                                                       \
    hz_out_m = __lsx_vsrari_h(hz_out_m, 7);                            \
    hz_out_m = __lsx_vsat_h(hz_out_m, 7);                              \
                                                                       \
    hz_out_m;                                                          \
} )

void ff_put_vp8_epel8_h6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                             const uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_lsx[mx - 1];
    __m128i src0, src1, src2, src3, filt0, filt1, filt2;
    __m128i mask0, mask1, mask2;
    __m128i out0, out1, out2, out3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 2;

    /* rearranging filter */
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    filt2 = __lsx_vldrepl_h(filter, 4);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);

    DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
              src + src_stride3, 0, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    src += src_stride4;
    HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               filt0, filt1, filt2, out0, out1, out2, out3);

    DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
    DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
    __lsx_vstelm_d(out0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(out0, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_d(out1, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(out1, dst, 0, 1);
    dst += dst_stride;

    for (loop_cnt = (height >> 2) - 1; loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                  src + src_stride3, 0, src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        src += src_stride4;
        HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   filt0, filt1, filt2, out0, out1, out2, out3);

        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);

        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;
    }
}

void ff_put_vp8_epel16_h6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                              const uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_lsx[mx - 1];
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, filt0, filt1;
    __m128i filt2, mask0, mask1, mask2;
    __m128i out0, out1, out2, out3, out4, out5, out6, out7;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 2;
    /* rearranging filter */
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    filt2 = __lsx_vldrepl_h(filter, 4);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2,
                  0, src + src_stride3, 0, src0 ,src2, src4, src6);
        DUP4_ARG2(__lsx_vld, src, 8, src + src_stride, 8, src + src_stride2,
                  8, src + src_stride3, 8, src1, src3, src5, src7);

        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src6, 128, src7, 128,
                  src4, src5, src6, src7);
        src += src_stride4;

        HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   filt0, filt1, filt2, out0, out1, out2, out3);
        HORIZ_6TAP_8WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1, mask2,
                                   filt0, filt1, filt2, out4, out5, out6, out7);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vst(out0, dst, 0);
        dst += dst_stride;
        __lsx_vst(out1, dst, 0);
        dst += dst_stride;

        DUP2_ARG3(__lsx_vssrarni_b_h, out5, out4, 7, out7, out6, 7, out4, out5);
        DUP2_ARG2(__lsx_vxori_b, out4, 128, out5, 128, out4, out5);
        __lsx_vst(out4, dst, 0);
        dst += dst_stride;
        __lsx_vst(out5, dst, 0);
        dst += dst_stride;
    }
}

void ff_put_vp8_epel8_v6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                             const uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src3, src4, src7, src8, src9, src10;
    __m128i src10_l, src32_l, src76_l, src98_l, src21_l, src43_l, src87_l;
    __m128i src109_l, filt0, filt1, filt2;
    __m128i out0_l, out1_l, out2_l, out3_l;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    src -= src_stride2;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    filt2 = __lsx_vldrepl_h(filter, 4);

    DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
              src + src_stride3, 0, src0, src1, src2, src3);
    src += src_stride4;
    src4 = __lsx_vld(src, 0);
    src += src_stride;

    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    src4 = __lsx_vxori_b(src4, 128);

    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src2, src1, src4,
              src3, src10_l, src32_l, src21_l, src43_l);
    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2,
                  0, src + src_stride3, 0, src7, src8, src9, src10);
        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10,
                  128, src7, src8, src9, src10);
        src += src_stride4;

        DUP4_ARG2(__lsx_vilvl_b, src7, src4, src8, src7, src9, src8, src10,
                  src9, src76_l, src87_l, src98_l, src109_l);

        out0_l = DPADD_SH3_SH(src10_l, src32_l, src76_l, filt0, filt1, filt2);
        out1_l = DPADD_SH3_SH(src21_l, src43_l, src87_l, filt0, filt1, filt2);
        out2_l = DPADD_SH3_SH(src32_l, src76_l, src98_l, filt0, filt1, filt2);
        out3_l = DPADD_SH3_SH(src43_l, src87_l, src109_l, filt0, filt1, filt2);

        DUP2_ARG3(__lsx_vssrarni_b_h, out1_l, out0_l, 7, out3_l, out2_l, 7,
                  out0_l, out1_l);
        DUP2_ARG2(__lsx_vxori_b, out0_l, 128, out1_l, 128, out0_l, out1_l);

        __lsx_vstelm_d(out0_l, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0_l, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1_l, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1_l, dst, 0, 1);
        dst += dst_stride;

        src10_l = src76_l;
        src32_l = src98_l;
        src21_l = src87_l;
        src43_l = src109_l;
        src4 = src10;
    }
}

void ff_put_vp8_epel16_v6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                              const uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i src10_l, src32_l, src54_l, src76_l, src21_l, src43_l, src65_l, src87_l;
    __m128i src10_h, src32_h, src54_h, src76_h, src21_h, src43_h, src65_h, src87_h;
    __m128i filt0, filt1, filt2;
    __m128i tmp0, tmp1, tmp2, tmp3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    filt2 = __lsx_vldrepl_h(filter, 4);

    DUP4_ARG2(__lsx_vld, src - src_stride2, 0, src - src_stride, 0, src, 0,
              src + src_stride, 0, src0, src1, src2, src3);
    src4 = __lsx_vld(src + src_stride2, 0);
    src += src_stride3;

    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128, src0,
              src1, src2, src3);
    src4 = __lsx_vxori_b(src4, 128);

    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src4, src3, src2, src1,
              src10_l, src32_l, src43_l, src21_l);
    DUP4_ARG2(__lsx_vilvh_b, src1, src0, src3, src2, src4, src3, src2, src1,
              src10_h, src32_h, src43_h, src21_h);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                  src + src_stride3, 0, src5, src6, src7, src8);
        src += src_stride4;
        DUP4_ARG2(__lsx_vxori_b, src5, 128, src6, 128, src7, 128, src8, 128,
                  src5, src6, src7, src8);

        DUP4_ARG2(__lsx_vilvl_b, src5, src4, src6, src5, src7, src6, src8, src7,
                  src54_l, src65_l, src76_l, src87_l);
        DUP4_ARG2(__lsx_vilvh_b, src5, src4, src6, src5, src7, src6, src8, src7,
                  src54_h, src65_h, src76_h, src87_h);

        tmp0 = DPADD_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        tmp1 = DPADD_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        tmp2 = DPADD_SH3_SH(src10_h, src32_h, src54_h, filt0, filt1, filt2);
        tmp3 = DPADD_SH3_SH(src21_h, src43_h, src65_h, filt0, filt1, filt2);

        DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        __lsx_vst(tmp0, dst, 0);
        dst += dst_stride;
        __lsx_vst(tmp1, dst, 0);
        dst += dst_stride;

        tmp0 = DPADD_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        tmp1 = DPADD_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        tmp2 = DPADD_SH3_SH(src32_h, src54_h, src76_h, filt0, filt1, filt2);
        tmp3 = DPADD_SH3_SH(src43_h, src65_h, src87_h, filt0, filt1, filt2);

        DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        __lsx_vst(tmp0, dst, 0);
        dst += dst_stride;
        __lsx_vst(tmp1, dst, 0);
        dst += dst_stride;

        src10_l = src54_l;
        src32_l = src76_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src10_h = src54_h;
        src32_h = src76_h;
        src21_h = src65_h;
        src43_h = src87_h;
        src4 = src8;
    }
}

void ff_put_vp8_epel8_h6v6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                               const uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_lsx[mx - 1];
    const int8_t *filter_vert = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i filt_hz0, filt_hz1, filt_hz2;
    __m128i mask0, mask1, mask2, filt_vt0, filt_vt1, filt_vt2;
    __m128i hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    __m128i hz_out7, hz_out8, out0, out1, out2, out3, out4, out5, out6, out7;
    __m128i tmp0, tmp1, tmp2, tmp3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= (2 + src_stride2);

    /* rearranging filter */
    DUP2_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filt_hz0, filt_hz1);
    filt_hz2 = __lsx_vldrepl_h(filter_horiz, 4);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);

    DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
              src + src_stride3, 0, src0, src1, src2, src3);
    src += src_stride4;
    src4 = __lsx_vld(src, 0);
    src +=  src_stride;

    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0 ,src1, src2, src3);
    src4 = __lsx_vxori_b(src4, 128);

    hz_out0 = HORIZ_6TAP_FILT(src0, src0, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out1 = HORIZ_6TAP_FILT(src1, src1, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out2 = HORIZ_6TAP_FILT(src2, src2, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out3 = HORIZ_6TAP_FILT(src3, src3, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out4 = HORIZ_6TAP_FILT(src4, src4, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);

    DUP2_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filt_vt0, filt_vt1);
    filt_vt2 = __lsx_vldrepl_h(filter_vert, 4);

    DUP2_ARG2(__lsx_vpackev_b, hz_out1, hz_out0, hz_out3, hz_out2, out0, out1);
    DUP2_ARG2(__lsx_vpackev_b, hz_out2, hz_out1, hz_out4, hz_out3, out3, out4);
    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                  src + src_stride3, 0, src5, src6, src7, src8);
        src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src5, 128, src6, 128, src7, 128, src8, 128,
                  src5, src6, src7, src8);

        hz_out5 = HORIZ_6TAP_FILT(src5, src5, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out2 = __lsx_vpackev_b(hz_out5, hz_out4);
        tmp0 = DPADD_SH3_SH(out0, out1, out2,filt_vt0, filt_vt1, filt_vt2);

        hz_out6 = HORIZ_6TAP_FILT(src6, src6, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out5 = __lsx_vpackev_b(hz_out6, hz_out5);
        tmp1 = DPADD_SH3_SH(out3, out4, out5, filt_vt0, filt_vt1, filt_vt2);

        hz_out7 = HORIZ_6TAP_FILT(src7, src7, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);

        out7 = __lsx_vpackev_b(hz_out7, hz_out6);
        tmp2 = DPADD_SH3_SH(out1, out2, out7, filt_vt0, filt_vt1, filt_vt2);

        hz_out8 = HORIZ_6TAP_FILT(src8, src8, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out6 = __lsx_vpackev_b(hz_out8, hz_out7);
        tmp3 = DPADD_SH3_SH(out4, out5, out6, filt_vt0, filt_vt1, filt_vt2);

        DUP2_ARG3(__lsx_vssrarni_b_h, tmp1, tmp0, 7, tmp3, tmp2, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        __lsx_vstelm_d(tmp0, dst, 0, 0);

        dst += dst_stride;
        __lsx_vstelm_d(tmp0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(tmp1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(tmp1, dst, 0, 1);
        dst += dst_stride;

        hz_out4 = hz_out8;
        out0 = out2;
        out1 = out7;
        out3 = out5;
        out4 = out6;
    }
}

void ff_put_vp8_epel16_h6v6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                                const uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h6v6_lsx(dst, dst_stride, src, src_stride, height, mx, my);
        src += 8;
        dst += 8;
    }
}

void ff_put_vp8_epel8_v4_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                             const uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src7, src8, src9, src10;
    __m128i src10_l, src72_l, src98_l, src21_l, src87_l, src109_l, filt0, filt1;
    __m128i out0, out1, out2, out3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    src -= src_stride;

    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src0, src1);
    src2 = __lsx_vld(src + src_stride2, 0);
    src += src_stride3;

    DUP2_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src0, src1);
    src2 = __lsx_vxori_b(src2, 128);
    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                  src + src_stride3, 0, src7, src8, src9, src10);
        src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        DUP4_ARG2(__lsx_vilvl_b, src7, src2, src8, src7, src9, src8, src10, src9,
                  src72_l, src87_l, src98_l, src109_l);

        out0 = FILT_4TAP_DPADD_S_H(src10_l, src72_l, filt0, filt1);
        out1 = FILT_4TAP_DPADD_S_H(src21_l, src87_l, filt0, filt1);
        out2 = FILT_4TAP_DPADD_S_H(src72_l, src98_l, filt0, filt1);
        out3 = FILT_4TAP_DPADD_S_H(src87_l, src109_l, filt0, filt1);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);

        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;

        src10_l = src98_l;
        src21_l = src109_l;
        src2 = src10;
    }
}

void ff_put_vp8_epel16_v4_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                              const uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src3, src4, src5, src6;
    __m128i src10_l, src32_l, src54_l, src21_l, src43_l, src65_l, src10_h;
    __m128i src32_h, src54_h, src21_h, src43_h, src65_h, filt0, filt1;
    __m128i tmp0, tmp1, tmp2, tmp3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    src -= src_stride;
    DUP2_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filt0, filt1);
    DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src0, src1);
    src2 = __lsx_vld(src + src_stride2, 0);
    src += src_stride3;

    DUP2_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src0, src1);
    src2 = __lsx_vxori_b(src2, 128);
    DUP2_ARG2(__lsx_vilvl_b, src1, src0, src2, src1, src10_l, src21_l);
    DUP2_ARG2(__lsx_vilvh_b, src1, src0, src2, src1, src10_h, src21_h);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2,
                  0, src + src_stride3, 0, src3, src4, src5, src6);
        src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src3, 128, src4, 128, src5, 128, src6, 128,
                  src3, src4, src5, src6);
        DUP4_ARG2(__lsx_vilvl_b, src3, src2, src4, src3, src5, src4, src6,
                  src5, src32_l, src43_l, src54_l, src65_l);
        DUP4_ARG2(__lsx_vilvh_b, src3, src2, src4, src3, src5, src4, src6,
                  src5, src32_h, src43_h, src54_h, src65_h);

        tmp0 = FILT_4TAP_DPADD_S_H(src10_l, src32_l, filt0, filt1);
        tmp1 = FILT_4TAP_DPADD_S_H(src21_l, src43_l, filt0, filt1);
        tmp2 = FILT_4TAP_DPADD_S_H(src10_h, src32_h, filt0, filt1);
        tmp3 = FILT_4TAP_DPADD_S_H(src21_h, src43_h, filt0, filt1);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);

        __lsx_vst(tmp0, dst, 0);
        dst += dst_stride;
        __lsx_vst(tmp1, dst, 0);
        dst += dst_stride;

        tmp0 = FILT_4TAP_DPADD_S_H(src32_l, src54_l, filt0, filt1);
        tmp1 = FILT_4TAP_DPADD_S_H(src43_l, src65_l, filt0, filt1);
        tmp2 = FILT_4TAP_DPADD_S_H(src32_h, src54_h, filt0, filt1);
        tmp3 = FILT_4TAP_DPADD_S_H(src43_h, src65_h, filt0, filt1);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);

        __lsx_vst(tmp0, dst, 0);
        dst += dst_stride;
        __lsx_vst(tmp1, dst, 0);
        dst += dst_stride;

        src10_l = src54_l;
        src21_l = src65_l;
        src10_h = src54_h;
        src21_h = src65_h;
        src2 = src6;
    }
}

void ff_put_vp8_epel8_h6v4_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                               const uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_lsx[mx - 1];
    const int8_t *filter_vert = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src3, src4, src5, src6;
    __m128i filt_hz0, filt_hz1, filt_hz2, mask0, mask1, mask2;
    __m128i filt_vt0, filt_vt1, hz_out0, hz_out1, hz_out2, hz_out3;
    __m128i tmp0, tmp1, tmp2, tmp3, vec0, vec1, vec2, vec3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= (2 + src_stride);

    /* rearranging filter */
    DUP2_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filt_hz0, filt_hz1);
    filt_hz2 = __lsx_vldrepl_h(filter_horiz, 4);

    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);

    DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src0, src1);
    src2 = __lsx_vld(src + src_stride2, 0);
    src += src_stride3;

    DUP2_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src0, src1);
    src2 = __lsx_vxori_b(src2, 128);
    hz_out0 = HORIZ_6TAP_FILT(src0, src0, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out1 = HORIZ_6TAP_FILT(src1, src1, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out2 = HORIZ_6TAP_FILT(src2, src2, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    DUP2_ARG2(__lsx_vpackev_b, hz_out1, hz_out0, hz_out2, hz_out1, vec0, vec2);

    DUP2_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filt_vt0, filt_vt1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                  src + src_stride3, 0, src3, src4, src5, src6);
        src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src3, 128, src4, 128, src5, 128, src6, 128,
                  src3, src4, src5, src6);

        hz_out3 = HORIZ_6TAP_FILT(src3, src3, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        vec1 = __lsx_vpackev_b(hz_out3, hz_out2);
        tmp0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt_vt0, filt_vt1);

        hz_out0 = HORIZ_6TAP_FILT(src4, src4, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        vec3 = __lsx_vpackev_b(hz_out0, hz_out3);
        tmp1 = FILT_4TAP_DPADD_S_H(vec2, vec3, filt_vt0, filt_vt1);

        hz_out1 = HORIZ_6TAP_FILT(src5, src5, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        vec0 = __lsx_vpackev_b(hz_out1, hz_out0);
        tmp2 = FILT_4TAP_DPADD_S_H(vec1, vec0, filt_vt0, filt_vt1);

        hz_out2 = HORIZ_6TAP_FILT(src6, src6, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        DUP2_ARG2(__lsx_vpackev_b, hz_out0, hz_out3, hz_out2, hz_out1, vec1, vec2);
        tmp3 = FILT_4TAP_DPADD_S_H(vec1, vec2, filt_vt0, filt_vt1);

        DUP2_ARG3(__lsx_vssrarni_b_h, tmp1, tmp0, 7, tmp3, tmp2, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);

        __lsx_vstelm_d(tmp0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(tmp0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(tmp1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(tmp1, dst, 0, 1);
        dst += dst_stride;
    }
}

void ff_put_vp8_epel16_h6v4_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                                const uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h6v4_lsx(dst, dst_stride, src, src_stride, height,
                                  mx, my);
        src += 8;
        dst += 8;
    }
}

void ff_put_vp8_epel8_h4v6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                               const uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_lsx[mx - 1];
    const int8_t *filter_vert = subpel_filters_lsx[my - 1];
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m128i filt_hz0, filt_hz1, mask0, mask1;
    __m128i filt_vt0, filt_vt1, filt_vt2;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    __m128i out0, out1, out2, out3, out4, out5, out6, out7;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= (1 + src_stride2);

    /* rearranging filter */
    DUP2_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filt_hz0, filt_hz1);
    mask1 = __lsx_vaddi_bu(mask0, 2);

    DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
              src + src_stride3, 0, src0, src1, src2, src3);
    src += src_stride4;
    src4 = __lsx_vld(src, 0);
    src += src_stride;

    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    src4 = __lsx_vxori_b(src4, 128);

    tmp0 = HORIZ_4TAP_FILT(src0, src0, mask0, mask1, filt_hz0, filt_hz1);
    tmp1 = HORIZ_4TAP_FILT(src1, src1, mask0, mask1, filt_hz0, filt_hz1);
    tmp2 = HORIZ_4TAP_FILT(src2, src2, mask0, mask1, filt_hz0, filt_hz1);
    tmp3 = HORIZ_4TAP_FILT(src3, src3, mask0, mask1, filt_hz0, filt_hz1);
    tmp4 = HORIZ_4TAP_FILT(src4, src4, mask0, mask1, filt_hz0, filt_hz1);

    DUP4_ARG2(__lsx_vpackev_b, tmp1, tmp0, tmp3, tmp2, tmp2, tmp1,
              tmp4, tmp3, out0, out1, out3, out4);

    DUP2_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filt_vt0, filt_vt1);
    filt_vt2 = __lsx_vldrepl_h(filter_vert, 4);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                  src + src_stride3, 0, src5, src6, src7, src8);
        src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src5, 128, src6, 128, src7, 128, src8, 128,
                  src5, src6, src7, src8);

        tmp5 = HORIZ_4TAP_FILT(src5, src5, mask0, mask1, filt_hz0, filt_hz1);
        out2 = __lsx_vpackev_b(tmp5, tmp4);
        tmp0 = DPADD_SH3_SH(out0, out1, out2, filt_vt0, filt_vt1, filt_vt2);

        tmp6 = HORIZ_4TAP_FILT(src6, src6, mask0, mask1, filt_hz0, filt_hz1);
        out5 = __lsx_vpackev_b(tmp6, tmp5);
        tmp1 = DPADD_SH3_SH(out3, out4, out5, filt_vt0, filt_vt1, filt_vt2);

        tmp7 = HORIZ_4TAP_FILT(src7, src7, mask0, mask1, filt_hz0, filt_hz1);
        out6 = __lsx_vpackev_b(tmp7, tmp6);
        tmp2 = DPADD_SH3_SH(out1, out2, out6, filt_vt0, filt_vt1, filt_vt2);

        tmp8 = HORIZ_4TAP_FILT(src8, src8, mask0, mask1, filt_hz0, filt_hz1);
        out7 = __lsx_vpackev_b(tmp8, tmp7);
        tmp3 = DPADD_SH3_SH(out4, out5, out7, filt_vt0, filt_vt1, filt_vt2);

        DUP2_ARG3(__lsx_vssrarni_b_h, tmp1, tmp0, 7, tmp3, tmp2, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);

        __lsx_vstelm_d(tmp0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(tmp0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(tmp1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(tmp1, dst, 0, 1);
        dst += dst_stride;

        tmp4 = tmp8;
        out0 = out2;
        out1 = out6;
        out3 = out5;
        out4 = out7;
    }
}

void ff_put_vp8_epel16_h4v6_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                                const uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h4v6_lsx(dst, dst_stride, src, src_stride, height,
                                  mx, my);
        src += 8;
        dst += 8;
    }
}

void ff_put_vp8_pixels8_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                            const uint8_t *src, ptrdiff_t src_stride,
                            int height, int mx, int my)
{
    int32_t cnt;
    __m128i src0, src1, src2, src3;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    if (0 == height % 8) {
        for (cnt = height >> 3; cnt--;) {
            DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                      src + src_stride3, 0, src0, src1, src2, src3);
            src += src_stride4;

            __lsx_vstelm_d(src0, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src1, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src2, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src3, dst, 0, 0);
            dst += dst_stride;

            DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                      src + src_stride3, 0, src0, src1, src2, src3);
            src += src_stride4;

            __lsx_vstelm_d(src0, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src1, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src2, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src3, dst, 0, 0);
            dst += dst_stride;
        }
    } else if( 0 == height % 4) {
        for (cnt = (height >> 2); cnt--;) {
            DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                      src + src_stride3, 0, src0, src1, src2, src3);
            src += src_stride4;

            __lsx_vstelm_d(src0, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src1, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src2, dst, 0, 0);
            dst += dst_stride;
            __lsx_vstelm_d(src3, dst, 0, 0);
            dst += dst_stride;
        }
    }
}

void ff_put_vp8_pixels16_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                             const uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    int32_t width = 16;
    int32_t cnt, loop_cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;

    ptrdiff_t src_stride2 = src_stride << 1;
    ptrdiff_t src_stride3 = src_stride2 + src_stride;
    ptrdiff_t src_stride4 = src_stride2 << 1;

    ptrdiff_t dst_stride2 = dst_stride << 1;
    ptrdiff_t dst_stride3 = dst_stride2 + dst_stride;
    ptrdiff_t dst_stride4 = dst_stride2 << 1;

    if (0 == height % 8) {
        for (cnt = (width >> 4); cnt--;) {
            src_tmp = src;
            dst_tmp = dst;
            for (loop_cnt = (height >> 3); loop_cnt--;) {
                DUP4_ARG2(__lsx_vld, src_tmp, 0, src_tmp + src_stride, 0,
                          src_tmp + src_stride2, 0, src_tmp + src_stride3, 0,
                          src4, src5, src6, src7);
                src_tmp += src_stride4;

                __lsx_vst(src4, dst_tmp,               0);
                __lsx_vst(src5, dst_tmp + dst_stride,  0);
                __lsx_vst(src6, dst_tmp + dst_stride2, 0);
                __lsx_vst(src7, dst_tmp + dst_stride3, 0);
                dst_tmp += dst_stride4;

                DUP4_ARG2(__lsx_vld, src_tmp, 0, src_tmp + src_stride, 0,
                          src_tmp + src_stride2, 0, src_tmp + src_stride3, 0,
                          src4, src5, src6, src7);
                src_tmp += src_stride4;

                __lsx_vst(src4, dst_tmp,               0);
                __lsx_vst(src5, dst_tmp + dst_stride,  0);
                __lsx_vst(src6, dst_tmp + dst_stride2, 0);
                __lsx_vst(src7, dst_tmp + dst_stride3, 0);
                dst_tmp += dst_stride4;
            }
            src += 16;
            dst += 16;
        }
    } else if (0 == height % 4) {
        for (cnt = (height >> 2); cnt--;) {
            DUP4_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src + src_stride2, 0,
                      src + src_stride3, 0, src0, src1, src2, src3);
            src += 4 * src_stride4;

            __lsx_vst(src0, dst,               0);
            __lsx_vst(src1, dst + dst_stride,  0);
            __lsx_vst(src2, dst + dst_stride2, 0);
            __lsx_vst(src3, dst + dst_stride3, 0);
            dst += dst_stride4;
       }
    }
}
