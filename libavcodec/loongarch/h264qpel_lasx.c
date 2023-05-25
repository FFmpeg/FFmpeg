/*
 * Loongson LASX optimized h264qpel
 *
 * Copyright (c) 2020 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#include "h264qpel_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "libavutil/attributes.h"

static const uint8_t luma_mask_arr[16 * 6] __attribute__((aligned(0x40))) = {
    /* 8 width cases */
    0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11, 7, 12,
    0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11, 7, 12,
    1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 9, 7, 10, 8, 11,
    1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 9, 7, 10, 8, 11,
    2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
    2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10
};

#define AVC_HORZ_FILTER_SH(in0, in1, mask0, mask1, mask2)  \
( {                                                        \
    __m256i out0_m;                                        \
    __m256i tmp0_m;                                        \
                                                           \
    tmp0_m = __lasx_xvshuf_b(in1, in0, mask0);             \
    out0_m = __lasx_xvhaddw_h_b(tmp0_m, tmp0_m);           \
    tmp0_m = __lasx_xvshuf_b(in1, in0, mask1);             \
    out0_m = __lasx_xvdp2add_h_b(out0_m, minus5b, tmp0_m); \
    tmp0_m = __lasx_xvshuf_b(in1, in0, mask2);             \
    out0_m = __lasx_xvdp2add_h_b(out0_m, plus20b, tmp0_m); \
                                                           \
    out0_m;                                                \
} )

#define AVC_DOT_SH3_SH(in0, in1, in2, coeff0, coeff1, coeff2)  \
( {                                                            \
    __m256i out0_m;                                            \
                                                               \
    out0_m = __lasx_xvdp2_h_b(in0, coeff0);                    \
    DUP2_ARG3(__lasx_xvdp2add_h_b, out0_m, in1, coeff1, out0_m,\
              in2, coeff2, out0_m, out0_m);                    \
                                                               \
    out0_m;                                                    \
} )

static av_always_inline
void avc_luma_hv_qrt_and_aver_dst_16x16_lasx(uint8_t *src_x,
                                             uint8_t *src_y,
                                             uint8_t *dst, ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    uint32_t loop_cnt;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tmp0, tmp1;
    __m256i src_hz0, src_hz1, src_hz2, src_hz3, mask0, mask1, mask2;
    __m256i src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    __m256i src_vt7, src_vt8;
    __m256i src_vt10_h, src_vt21_h, src_vt32_h, src_vt43_h, src_vt54_h;
    __m256i src_vt65_h, src_vt76_h, src_vt87_h, filt0, filt1, filt2;
    __m256i hz_out0, hz_out1, hz_out2, hz_out3, vt_out0, vt_out1, vt_out2;
    __m256i vt_out3, out0, out1, out2, out3;
    __m256i minus5b = __lasx_xvldi(0xFB);
    __m256i plus20b = __lasx_xvldi(20);

    filt0 = __lasx_xvreplgr2vr_h(filt_const0);
    filt1 = __lasx_xvreplgr2vr_h(filt_const1);
    filt2 = __lasx_xvreplgr2vr_h(filt_const2);

    mask0 = __lasx_xvld(luma_mask_arr, 0);
    DUP2_ARG2(__lasx_xvld, luma_mask_arr, 32, luma_mask_arr, 64, mask1, mask2);
    src_vt0 = __lasx_xvld(src_y, 0);
    DUP4_ARG2(__lasx_xvldx, src_y, stride, src_y, stride_2x, src_y, stride_3x,
              src_y, stride_4x, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += stride_4x;

    src_vt0 = __lasx_xvxori_b(src_vt0, 128);
    DUP4_ARG2(__lasx_xvxori_b, src_vt1, 128, src_vt2, 128, src_vt3, 128,
              src_vt4, 128, src_vt1, src_vt2, src_vt3, src_vt4);

    for (loop_cnt = 4; loop_cnt--;) {
        src_hz0 = __lasx_xvld(src_x, 0);
        DUP2_ARG2(__lasx_xvldx, src_x, stride, src_x, stride_2x,
                  src_hz1, src_hz2);
        src_hz3 = __lasx_xvldx(src_x, stride_3x);
        src_x  += stride_4x;
        src_hz0 = __lasx_xvpermi_d(src_hz0, 0x94);
        src_hz1 = __lasx_xvpermi_d(src_hz1, 0x94);
        src_hz2 = __lasx_xvpermi_d(src_hz2, 0x94);
        src_hz3 = __lasx_xvpermi_d(src_hz3, 0x94);
        DUP4_ARG2(__lasx_xvxori_b, src_hz0, 128, src_hz1, 128, src_hz2, 128,
                  src_hz3, 128, src_hz0, src_hz1, src_hz2, src_hz3);

        hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);
        hz_out0 = __lasx_xvssrarni_b_h(hz_out1, hz_out0, 5);
        hz_out2 = __lasx_xvssrarni_b_h(hz_out3, hz_out2, 5);

        DUP4_ARG2(__lasx_xvldx, src_y, stride, src_y, stride_2x,
                  src_y, stride_3x, src_y, stride_4x,
                  src_vt5, src_vt6, src_vt7, src_vt8);
        src_y += stride_4x;

        DUP4_ARG2(__lasx_xvxori_b, src_vt5, 128, src_vt6, 128, src_vt7, 128,
                  src_vt8, 128, src_vt5, src_vt6, src_vt7, src_vt8);

        DUP4_ARG3(__lasx_xvpermi_q, src_vt0, src_vt4, 0x02, src_vt1, src_vt5,
                  0x02, src_vt2, src_vt6, 0x02, src_vt3, src_vt7, 0x02,
                  src_vt0, src_vt1, src_vt2, src_vt3);
        src_vt87_h = __lasx_xvpermi_q(src_vt4, src_vt8, 0x02);
        DUP4_ARG2(__lasx_xvilvh_b, src_vt1, src_vt0, src_vt2, src_vt1,
                  src_vt3, src_vt2, src_vt87_h, src_vt3,
                  src_hz0, src_hz1, src_hz2, src_hz3);
        DUP4_ARG2(__lasx_xvilvl_b, src_vt1, src_vt0, src_vt2, src_vt1,
                  src_vt3, src_vt2, src_vt87_h, src_vt3,
                  src_vt0, src_vt1, src_vt2, src_vt3);
        DUP4_ARG3(__lasx_xvpermi_q, src_vt0, src_hz0, 0x02, src_vt1, src_hz1,
                  0x02, src_vt2, src_hz2, 0x02, src_vt3, src_hz3, 0x02,
                  src_vt10_h, src_vt21_h, src_vt32_h, src_vt43_h);
        DUP4_ARG3(__lasx_xvpermi_q, src_vt0, src_hz0, 0x13, src_vt1, src_hz1,
                  0x13, src_vt2, src_hz2, 0x13, src_vt3, src_hz3, 0x13,
                  src_vt54_h, src_vt65_h, src_vt76_h, src_vt87_h);
        vt_out0 = AVC_DOT_SH3_SH(src_vt10_h, src_vt32_h, src_vt54_h, filt0,
                                 filt1, filt2);
        vt_out1 = AVC_DOT_SH3_SH(src_vt21_h, src_vt43_h, src_vt65_h, filt0,
                                 filt1, filt2);
        vt_out2 = AVC_DOT_SH3_SH(src_vt32_h, src_vt54_h, src_vt76_h, filt0,
                                 filt1, filt2);
        vt_out3 = AVC_DOT_SH3_SH(src_vt43_h, src_vt65_h, src_vt87_h, filt0,
                                 filt1, filt2);
        vt_out0 = __lasx_xvssrarni_b_h(vt_out1, vt_out0, 5);
        vt_out2 = __lasx_xvssrarni_b_h(vt_out3, vt_out2, 5);

        DUP2_ARG2(__lasx_xvaddwl_h_b, hz_out0, vt_out0, hz_out2, vt_out2,
                  out0, out2);
        DUP2_ARG2(__lasx_xvaddwh_h_b, hz_out0, vt_out0, hz_out2, vt_out2,
                  out1, out3);
        tmp0 = __lasx_xvssrarni_b_h(out1, out0, 1);
        tmp1 = __lasx_xvssrarni_b_h(out3, out2, 1);

        DUP2_ARG2(__lasx_xvxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        out0 = __lasx_xvld(dst, 0);
        DUP2_ARG2(__lasx_xvldx, dst, stride, dst, stride_2x, out1, out2);
        out3 = __lasx_xvldx(dst, stride_3x);
        out0 = __lasx_xvpermi_q(out0, out2, 0x02);
        out1 = __lasx_xvpermi_q(out1, out3, 0x02);
        out2 = __lasx_xvilvl_d(out1, out0);
        out3 = __lasx_xvilvh_d(out1, out0);
        out0 = __lasx_xvpermi_q(out2, out3, 0x02);
        out1 = __lasx_xvpermi_q(out2, out3, 0x13);
        tmp0 = __lasx_xvavgr_bu(out0, tmp0);
        tmp1 = __lasx_xvavgr_bu(out1, tmp1);

        __lasx_xvstelm_d(tmp0, dst, 0, 0);
        __lasx_xvstelm_d(tmp0, dst + stride, 0, 1);
        __lasx_xvstelm_d(tmp1, dst + stride_2x, 0, 0);
        __lasx_xvstelm_d(tmp1, dst + stride_3x, 0, 1);

        __lasx_xvstelm_d(tmp0, dst, 8, 2);
        __lasx_xvstelm_d(tmp0, dst + stride, 8, 3);
        __lasx_xvstelm_d(tmp1, dst + stride_2x, 8, 2);
        __lasx_xvstelm_d(tmp1, dst + stride_3x, 8, 3);

        dst    += stride_4x;
        src_vt0 = src_vt4;
        src_vt1 = src_vt5;
        src_vt2 = src_vt6;
        src_vt3 = src_vt7;
        src_vt4 = src_vt8;
    }
}

static av_always_inline void
avc_luma_hv_qrt_16x16_lasx(uint8_t *src_x, uint8_t *src_y,
                           uint8_t *dst, ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    uint32_t loop_cnt;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tmp0, tmp1;
    __m256i src_hz0, src_hz1, src_hz2, src_hz3, mask0, mask1, mask2;
    __m256i src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    __m256i src_vt7, src_vt8;
    __m256i src_vt10_h, src_vt21_h, src_vt32_h, src_vt43_h, src_vt54_h;
    __m256i src_vt65_h, src_vt76_h, src_vt87_h, filt0, filt1, filt2;
    __m256i hz_out0, hz_out1, hz_out2, hz_out3, vt_out0, vt_out1, vt_out2;
    __m256i vt_out3, out0, out1, out2, out3;
    __m256i minus5b = __lasx_xvldi(0xFB);
    __m256i plus20b = __lasx_xvldi(20);

    filt0 = __lasx_xvreplgr2vr_h(filt_const0);
    filt1 = __lasx_xvreplgr2vr_h(filt_const1);
    filt2 = __lasx_xvreplgr2vr_h(filt_const2);

    mask0 = __lasx_xvld(luma_mask_arr, 0);
    DUP2_ARG2(__lasx_xvld, luma_mask_arr, 32, luma_mask_arr, 64, mask1, mask2);
    src_vt0 = __lasx_xvld(src_y, 0);
    DUP4_ARG2(__lasx_xvldx, src_y, stride, src_y, stride_2x, src_y, stride_3x,
              src_y, stride_4x, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += stride_4x;

    src_vt0 = __lasx_xvxori_b(src_vt0, 128);
    DUP4_ARG2(__lasx_xvxori_b, src_vt1, 128, src_vt2, 128, src_vt3, 128,
              src_vt4, 128, src_vt1, src_vt2, src_vt3, src_vt4);

    for (loop_cnt = 4; loop_cnt--;) {
        src_hz0 = __lasx_xvld(src_x, 0);
        DUP2_ARG2(__lasx_xvldx, src_x, stride, src_x, stride_2x,
                  src_hz1, src_hz2);
        src_hz3 = __lasx_xvldx(src_x, stride_3x);
        src_x  += stride_4x;
        src_hz0 = __lasx_xvpermi_d(src_hz0, 0x94);
        src_hz1 = __lasx_xvpermi_d(src_hz1, 0x94);
        src_hz2 = __lasx_xvpermi_d(src_hz2, 0x94);
        src_hz3 = __lasx_xvpermi_d(src_hz3, 0x94);
        DUP4_ARG2(__lasx_xvxori_b, src_hz0, 128, src_hz1, 128, src_hz2, 128,
                  src_hz3, 128, src_hz0, src_hz1, src_hz2, src_hz3);

        hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);
        hz_out0 = __lasx_xvssrarni_b_h(hz_out1, hz_out0, 5);
        hz_out2 = __lasx_xvssrarni_b_h(hz_out3, hz_out2, 5);

        DUP4_ARG2(__lasx_xvldx, src_y, stride, src_y, stride_2x,
                  src_y, stride_3x, src_y, stride_4x,
                  src_vt5, src_vt6, src_vt7, src_vt8);
        src_y += stride_4x;

        DUP4_ARG2(__lasx_xvxori_b, src_vt5, 128, src_vt6, 128, src_vt7, 128,
                  src_vt8, 128, src_vt5, src_vt6, src_vt7, src_vt8);
        DUP4_ARG3(__lasx_xvpermi_q, src_vt0, src_vt4, 0x02, src_vt1, src_vt5,
                  0x02, src_vt2, src_vt6, 0x02, src_vt3, src_vt7, 0x02,
                  src_vt0, src_vt1, src_vt2, src_vt3);
        src_vt87_h = __lasx_xvpermi_q(src_vt4, src_vt8, 0x02);
        DUP4_ARG2(__lasx_xvilvh_b, src_vt1, src_vt0, src_vt2, src_vt1,
                  src_vt3, src_vt2, src_vt87_h, src_vt3,
                  src_hz0, src_hz1, src_hz2, src_hz3);
        DUP4_ARG2(__lasx_xvilvl_b, src_vt1, src_vt0, src_vt2, src_vt1,
                  src_vt3, src_vt2, src_vt87_h, src_vt3,
                  src_vt0, src_vt1, src_vt2, src_vt3);
        DUP4_ARG3(__lasx_xvpermi_q, src_vt0, src_hz0, 0x02, src_vt1,
                  src_hz1, 0x02, src_vt2, src_hz2, 0x02, src_vt3, src_hz3,
                  0x02, src_vt10_h, src_vt21_h, src_vt32_h, src_vt43_h);
        DUP4_ARG3(__lasx_xvpermi_q, src_vt0, src_hz0, 0x13, src_vt1,
                  src_hz1, 0x13, src_vt2, src_hz2, 0x13, src_vt3, src_hz3,
                  0x13, src_vt54_h, src_vt65_h, src_vt76_h, src_vt87_h);

        vt_out0 = AVC_DOT_SH3_SH(src_vt10_h, src_vt32_h, src_vt54_h,
                                 filt0, filt1, filt2);
        vt_out1 = AVC_DOT_SH3_SH(src_vt21_h, src_vt43_h, src_vt65_h,
                                 filt0, filt1, filt2);
        vt_out2 = AVC_DOT_SH3_SH(src_vt32_h, src_vt54_h, src_vt76_h,
                                 filt0, filt1, filt2);
        vt_out3 = AVC_DOT_SH3_SH(src_vt43_h, src_vt65_h, src_vt87_h,
                                 filt0, filt1, filt2);
        vt_out0 = __lasx_xvssrarni_b_h(vt_out1, vt_out0, 5);
        vt_out2 = __lasx_xvssrarni_b_h(vt_out3, vt_out2, 5);

        DUP2_ARG2(__lasx_xvaddwl_h_b, hz_out0, vt_out0, hz_out2, vt_out2,
                  out0, out2);
        DUP2_ARG2(__lasx_xvaddwh_h_b, hz_out0, vt_out0, hz_out2, vt_out2,
                  out1, out3);
        tmp0 = __lasx_xvssrarni_b_h(out1, out0, 1);
        tmp1 = __lasx_xvssrarni_b_h(out3, out2, 1);

        DUP2_ARG2(__lasx_xvxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        __lasx_xvstelm_d(tmp0, dst, 0, 0);
        __lasx_xvstelm_d(tmp0, dst + stride, 0, 1);
        __lasx_xvstelm_d(tmp1, dst + stride_2x, 0, 0);
        __lasx_xvstelm_d(tmp1, dst + stride_3x, 0, 1);

        __lasx_xvstelm_d(tmp0, dst, 8, 2);
        __lasx_xvstelm_d(tmp0, dst + stride, 8, 3);
        __lasx_xvstelm_d(tmp1, dst + stride_2x, 8, 2);
        __lasx_xvstelm_d(tmp1, dst + stride_3x, 8, 3);

        dst    += stride_4x;
        src_vt0 = src_vt4;
        src_vt1 = src_vt5;
        src_vt2 = src_vt6;
        src_vt3 = src_vt7;
        src_vt4 = src_vt8;
    }
}

/* put_pixels8_8_inline_asm: dst = src */
static av_always_inline void
put_pixels8_8_inline_asm(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint64_t tmp[8];
    ptrdiff_t stride_2, stride_3, stride_4;
    __asm__ volatile (
    "slli.d     %[stride_2],     %[stride],   1           \n\t"
    "add.d      %[stride_3],     %[stride_2], %[stride]   \n\t"
    "slli.d     %[stride_4],     %[stride_2], 1           \n\t"
    "ld.d       %[tmp0],         %[src],      0x0         \n\t"
    "ldx.d      %[tmp1],         %[src],      %[stride]   \n\t"
    "ldx.d      %[tmp2],         %[src],      %[stride_2] \n\t"
    "ldx.d      %[tmp3],         %[src],      %[stride_3] \n\t"
    "add.d      %[src],          %[src],      %[stride_4] \n\t"
    "ld.d       %[tmp4],         %[src],      0x0         \n\t"
    "ldx.d      %[tmp5],         %[src],      %[stride]   \n\t"
    "ldx.d      %[tmp6],         %[src],      %[stride_2] \n\t"
    "ldx.d      %[tmp7],         %[src],      %[stride_3] \n\t"

    "st.d       %[tmp0],         %[dst],      0x0         \n\t"
    "stx.d      %[tmp1],         %[dst],      %[stride]   \n\t"
    "stx.d      %[tmp2],         %[dst],      %[stride_2] \n\t"
    "stx.d      %[tmp3],         %[dst],      %[stride_3] \n\t"
    "add.d      %[dst],          %[dst],      %[stride_4] \n\t"
    "st.d       %[tmp4],         %[dst],      0x0         \n\t"
    "stx.d      %[tmp5],         %[dst],      %[stride]   \n\t"
    "stx.d      %[tmp6],         %[dst],      %[stride_2] \n\t"
    "stx.d      %[tmp7],         %[dst],      %[stride_3] \n\t"
    : [tmp0]"=&r"(tmp[0]),        [tmp1]"=&r"(tmp[1]),
      [tmp2]"=&r"(tmp[2]),        [tmp3]"=&r"(tmp[3]),
      [tmp4]"=&r"(tmp[4]),        [tmp5]"=&r"(tmp[5]),
      [tmp6]"=&r"(tmp[6]),        [tmp7]"=&r"(tmp[7]),
      [stride_2]"=&r"(stride_2),  [stride_3]"=&r"(stride_3),
      [stride_4]"=&r"(stride_4),
      [dst]"+&r"(dst),            [src]"+&r"(src)
    : [stride]"r"(stride)
    : "memory"
    );
}

/* avg_pixels8_8_lsx   : dst = avg(src, dst)
 * put_pixels8_l2_8_lsx: dst = avg(src, half) , half stride is 8.
 * avg_pixels8_l2_8_lsx: dst = avg(avg(src, half), dst) , half stride is 8.*/
static av_always_inline void
avg_pixels8_8_lsx(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t *tmp = dst;
    ptrdiff_t stride_2, stride_3, stride_4;
    __asm__ volatile (
    /* h0~h7 */
    "slli.d     %[stride_2],     %[stride],   1           \n\t"
    "add.d      %[stride_3],     %[stride_2], %[stride]   \n\t"
    "slli.d     %[stride_4],     %[stride_2], 1           \n\t"
    "vld        $vr0,            %[src],      0           \n\t"
    "vldx       $vr1,            %[src],      %[stride]   \n\t"
    "vldx       $vr2,            %[src],      %[stride_2] \n\t"
    "vldx       $vr3,            %[src],      %[stride_3] \n\t"
    "add.d      %[src],          %[src],      %[stride_4] \n\t"
    "vld        $vr4,            %[src],      0           \n\t"
    "vldx       $vr5,            %[src],      %[stride]   \n\t"
    "vldx       $vr6,            %[src],      %[stride_2] \n\t"
    "vldx       $vr7,            %[src],      %[stride_3] \n\t"

    "vld        $vr8,            %[tmp],      0           \n\t"
    "vldx       $vr9,            %[tmp],      %[stride]   \n\t"
    "vldx       $vr10,           %[tmp],      %[stride_2] \n\t"
    "vldx       $vr11,           %[tmp],      %[stride_3] \n\t"
    "add.d      %[tmp],          %[tmp],      %[stride_4] \n\t"
    "vld        $vr12,           %[tmp],      0           \n\t"
    "vldx       $vr13,           %[tmp],      %[stride]   \n\t"
    "vldx       $vr14,           %[tmp],      %[stride_2] \n\t"
    "vldx       $vr15,           %[tmp],      %[stride_3] \n\t"

    "vavgr.bu    $vr0,           $vr8,        $vr0        \n\t"
    "vavgr.bu    $vr1,           $vr9,        $vr1        \n\t"
    "vavgr.bu    $vr2,           $vr10,       $vr2        \n\t"
    "vavgr.bu    $vr3,           $vr11,       $vr3        \n\t"
    "vavgr.bu    $vr4,           $vr12,       $vr4        \n\t"
    "vavgr.bu    $vr5,           $vr13,       $vr5        \n\t"
    "vavgr.bu    $vr6,           $vr14,       $vr6        \n\t"
    "vavgr.bu    $vr7,           $vr15,       $vr7        \n\t"

    "vstelm.d    $vr0,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr1,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr2,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr3,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr4,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr5,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr6,           %[dst],      0,  0       \n\t"
    "add.d       %[dst],         %[dst],      %[stride]   \n\t"
    "vstelm.d    $vr7,           %[dst],      0,  0       \n\t"
    : [dst]"+&r"(dst), [tmp]"+&r"(tmp), [src]"+&r"(src),
      [stride_2]"=&r"(stride_2),  [stride_3]"=&r"(stride_3),
      [stride_4]"=&r"(stride_4)
    : [stride]"r"(stride)
    : "memory"
    );
}

/* put_pixels16_8_lsx: dst = src */
static av_always_inline void
put_pixels16_8_lsx(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    ptrdiff_t stride_2, stride_3, stride_4;
    __asm__ volatile (
    "slli.d     %[stride_2],     %[stride],      1            \n\t"
    "add.d      %[stride_3],     %[stride_2],    %[stride]    \n\t"
    "slli.d     %[stride_4],     %[stride_2],    1            \n\t"
    "vld        $vr0,            %[src],         0            \n\t"
    "vldx       $vr1,            %[src],         %[stride]    \n\t"
    "vldx       $vr2,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr3,            %[src],         %[stride_3]  \n\t"
    "add.d      %[src],          %[src],         %[stride_4]  \n\t"
    "vld        $vr4,            %[src],         0            \n\t"
    "vldx       $vr5,            %[src],         %[stride]    \n\t"
    "vldx       $vr6,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr7,            %[src],         %[stride_3]  \n\t"
    "add.d      %[src],          %[src],         %[stride_4]  \n\t"

    "vst        $vr0,            %[dst],         0            \n\t"
    "vstx       $vr1,            %[dst],         %[stride]    \n\t"
    "vstx       $vr2,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr3,            %[dst],         %[stride_3]  \n\t"
    "add.d      %[dst],          %[dst],         %[stride_4]  \n\t"
    "vst        $vr4,            %[dst],         0            \n\t"
    "vstx       $vr5,            %[dst],         %[stride]    \n\t"
    "vstx       $vr6,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr7,            %[dst],         %[stride_3]  \n\t"
    "add.d      %[dst],          %[dst],         %[stride_4]  \n\t"

    "vld        $vr0,            %[src],         0            \n\t"
    "vldx       $vr1,            %[src],         %[stride]    \n\t"
    "vldx       $vr2,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr3,            %[src],         %[stride_3]  \n\t"
    "add.d      %[src],          %[src],         %[stride_4]  \n\t"
    "vld        $vr4,            %[src],         0            \n\t"
    "vldx       $vr5,            %[src],         %[stride]    \n\t"
    "vldx       $vr6,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr7,            %[src],         %[stride_3]  \n\t"

    "vst        $vr0,            %[dst],         0            \n\t"
    "vstx       $vr1,            %[dst],         %[stride]    \n\t"
    "vstx       $vr2,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr3,            %[dst],         %[stride_3]  \n\t"
    "add.d      %[dst],          %[dst],         %[stride_4]  \n\t"
    "vst        $vr4,            %[dst],         0            \n\t"
    "vstx       $vr5,            %[dst],         %[stride]    \n\t"
    "vstx       $vr6,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr7,            %[dst],         %[stride_3]  \n\t"
    : [dst]"+&r"(dst),            [src]"+&r"(src),
      [stride_2]"=&r"(stride_2),  [stride_3]"=&r"(stride_3),
      [stride_4]"=&r"(stride_4)
    : [stride]"r"(stride)
    : "memory"
    );
}

/* avg_pixels16_8_lsx    : dst = avg(src, dst)
 * put_pixels16_l2_8_lsx: dst = avg(src, half) , half stride is 8.
 * avg_pixels16_l2_8_lsx: dst = avg(avg(src, half), dst) , half stride is 8.*/
static av_always_inline void
avg_pixels16_8_lsx(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t *tmp = dst;
    ptrdiff_t stride_2, stride_3, stride_4;
    __asm__ volatile (
    /* h0~h7 */
    "slli.d     %[stride_2],     %[stride],      1            \n\t"
    "add.d      %[stride_3],     %[stride_2],    %[stride]    \n\t"
    "slli.d     %[stride_4],     %[stride_2],    1            \n\t"
    "vld        $vr0,            %[src],         0            \n\t"
    "vldx       $vr1,            %[src],         %[stride]    \n\t"
    "vldx       $vr2,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr3,            %[src],         %[stride_3]  \n\t"
    "add.d      %[src],          %[src],         %[stride_4]  \n\t"
    "vld        $vr4,            %[src],         0            \n\t"
    "vldx       $vr5,            %[src],         %[stride]    \n\t"
    "vldx       $vr6,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr7,            %[src],         %[stride_3]  \n\t"
    "add.d      %[src],          %[src],         %[stride_4]  \n\t"

    "vld        $vr8,            %[tmp],         0            \n\t"
    "vldx       $vr9,            %[tmp],         %[stride]    \n\t"
    "vldx       $vr10,           %[tmp],         %[stride_2]  \n\t"
    "vldx       $vr11,           %[tmp],         %[stride_3]  \n\t"
    "add.d      %[tmp],          %[tmp],         %[stride_4]  \n\t"
    "vld        $vr12,           %[tmp],         0            \n\t"
    "vldx       $vr13,           %[tmp],         %[stride]    \n\t"
    "vldx       $vr14,           %[tmp],         %[stride_2]  \n\t"
    "vldx       $vr15,           %[tmp],         %[stride_3]  \n\t"
    "add.d      %[tmp],          %[tmp],         %[stride_4]  \n\t"

    "vavgr.bu   $vr0,            $vr8,           $vr0         \n\t"
    "vavgr.bu   $vr1,            $vr9,           $vr1         \n\t"
    "vavgr.bu   $vr2,            $vr10,          $vr2         \n\t"
    "vavgr.bu   $vr3,            $vr11,          $vr3         \n\t"
    "vavgr.bu   $vr4,            $vr12,          $vr4         \n\t"
    "vavgr.bu   $vr5,            $vr13,          $vr5         \n\t"
    "vavgr.bu   $vr6,            $vr14,          $vr6         \n\t"
    "vavgr.bu   $vr7,            $vr15,          $vr7         \n\t"

    "vst        $vr0,            %[dst],         0            \n\t"
    "vstx       $vr1,            %[dst],         %[stride]    \n\t"
    "vstx       $vr2,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr3,            %[dst],         %[stride_3]  \n\t"
    "add.d      %[dst],          %[dst],         %[stride_4]  \n\t"
    "vst        $vr4,            %[dst],         0            \n\t"
    "vstx       $vr5,            %[dst],         %[stride]    \n\t"
    "vstx       $vr6,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr7,            %[dst],         %[stride_3]  \n\t"
    "add.d      %[dst],          %[dst],         %[stride_4]  \n\t"

    /* h8~h15 */
    "vld        $vr0,            %[src],         0            \n\t"
    "vldx       $vr1,            %[src],         %[stride]    \n\t"
    "vldx       $vr2,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr3,            %[src],         %[stride_3]  \n\t"
    "add.d      %[src],          %[src],         %[stride_4]  \n\t"
    "vld        $vr4,            %[src],         0            \n\t"
    "vldx       $vr5,            %[src],         %[stride]    \n\t"
    "vldx       $vr6,            %[src],         %[stride_2]  \n\t"
    "vldx       $vr7,            %[src],         %[stride_3]  \n\t"

    "vld        $vr8,            %[tmp],         0            \n\t"
    "vldx       $vr9,            %[tmp],         %[stride]    \n\t"
    "vldx       $vr10,           %[tmp],         %[stride_2]  \n\t"
    "vldx       $vr11,           %[tmp],         %[stride_3]  \n\t"
    "add.d      %[tmp],          %[tmp],         %[stride_4]  \n\t"
    "vld        $vr12,           %[tmp],         0            \n\t"
    "vldx       $vr13,           %[tmp],         %[stride]    \n\t"
    "vldx       $vr14,           %[tmp],         %[stride_2]  \n\t"
    "vldx       $vr15,           %[tmp],         %[stride_3]  \n\t"

    "vavgr.bu    $vr0,           $vr8,           $vr0         \n\t"
    "vavgr.bu    $vr1,           $vr9,           $vr1         \n\t"
    "vavgr.bu    $vr2,           $vr10,          $vr2         \n\t"
    "vavgr.bu    $vr3,           $vr11,          $vr3         \n\t"
    "vavgr.bu    $vr4,           $vr12,          $vr4         \n\t"
    "vavgr.bu    $vr5,           $vr13,          $vr5         \n\t"
    "vavgr.bu    $vr6,           $vr14,          $vr6         \n\t"
    "vavgr.bu    $vr7,           $vr15,          $vr7         \n\t"

    "vst        $vr0,            %[dst],         0            \n\t"
    "vstx       $vr1,            %[dst],         %[stride]    \n\t"
    "vstx       $vr2,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr3,            %[dst],         %[stride_3]  \n\t"
    "add.d      %[dst],          %[dst],         %[stride_4]  \n\t"
    "vst        $vr4,            %[dst],         0            \n\t"
    "vstx       $vr5,            %[dst],         %[stride]    \n\t"
    "vstx       $vr6,            %[dst],         %[stride_2]  \n\t"
    "vstx       $vr7,            %[dst],         %[stride_3]  \n\t"
    : [dst]"+&r"(dst), [tmp]"+&r"(tmp), [src]"+&r"(src),
      [stride_2]"=&r"(stride_2),  [stride_3]"=&r"(stride_3),
      [stride_4]"=&r"(stride_4)
    : [stride]"r"(stride)
    : "memory"
    );
}

#define QPEL8_H_LOWPASS(out_v)                                               \
    src00 = __lasx_xvld(src, - 2);                                           \
    src += srcStride;                                                        \
    src10 = __lasx_xvld(src, - 2);                                           \
    src += srcStride;                                                        \
    src00 = __lasx_xvpermi_q(src00, src10, 0x02);                            \
    src01 = __lasx_xvshuf_b(src00, src00, (__m256i)mask1);                   \
    src02 = __lasx_xvshuf_b(src00, src00, (__m256i)mask2);                   \
    src03 = __lasx_xvshuf_b(src00, src00, (__m256i)mask3);                   \
    src04 = __lasx_xvshuf_b(src00, src00, (__m256i)mask4);                   \
    src05 = __lasx_xvshuf_b(src00, src00, (__m256i)mask5);                   \
    DUP2_ARG2(__lasx_xvaddwl_h_bu, src02, src03, src01, src04, src02, src01);\
    src00 = __lasx_xvaddwl_h_bu(src00, src05);                               \
    src02 = __lasx_xvmul_h(src02, h_20);                                     \
    src01 = __lasx_xvmul_h(src01, h_5);                                      \
    src02 = __lasx_xvssub_h(src02, src01);                                   \
    src02 = __lasx_xvsadd_h(src02, src00);                                   \
    src02 = __lasx_xvsadd_h(src02, h_16);                                    \
    out_v = __lasx_xvssrani_bu_h(src02, src02, 5);                           \

static av_always_inline void
put_h264_qpel8_h_lowpass_lasx(uint8_t *dst, const uint8_t *src, int dstStride,
                              int srcStride)
{
    int dstStride_2x = dstStride << 1;
    __m256i src00, src01, src02, src03, src04, src05, src10;
    __m256i out0, out1, out2, out3;
    __m256i h_20 = __lasx_xvldi(0x414);
    __m256i h_5  = __lasx_xvldi(0x405);
    __m256i h_16 = __lasx_xvldi(0x410);
    __m256i mask1 = {0x0807060504030201, 0x0, 0x0807060504030201, 0x0};
    __m256i mask2 = {0x0908070605040302, 0x0, 0x0908070605040302, 0x0};
    __m256i mask3 = {0x0a09080706050403, 0x0, 0x0a09080706050403, 0x0};
    __m256i mask4 = {0x0b0a090807060504, 0x0, 0x0b0a090807060504, 0x0};
    __m256i mask5 = {0x0c0b0a0908070605, 0x0, 0x0c0b0a0908070605, 0x0};

    QPEL8_H_LOWPASS(out0)
    QPEL8_H_LOWPASS(out1)
    QPEL8_H_LOWPASS(out2)
    QPEL8_H_LOWPASS(out3)
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    __lasx_xvstelm_d(out2, dst, 0, 0);
    __lasx_xvstelm_d(out2, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    __lasx_xvstelm_d(out3, dst, 0, 0);
    __lasx_xvstelm_d(out3, dst + dstStride, 0, 2);
}

#define QPEL8_V_LOWPASS(src0, src1, src2, src3, src4, src5, src6,       \
                        tmp0, tmp1, tmp2, tmp3, tmp4, tmp5)             \
{                                                                       \
    tmp0 = __lasx_xvpermi_q(src0, src1, 0x02);                          \
    tmp1 = __lasx_xvpermi_q(src1, src2, 0x02);                          \
    tmp2 = __lasx_xvpermi_q(src2, src3, 0x02);                          \
    tmp3 = __lasx_xvpermi_q(src3, src4, 0x02);                          \
    tmp4 = __lasx_xvpermi_q(src4, src5, 0x02);                          \
    tmp5 = __lasx_xvpermi_q(src5, src6, 0x02);                          \
    DUP2_ARG2(__lasx_xvaddwl_h_bu, tmp2, tmp3, tmp1, tmp4, tmp2, tmp1); \
    tmp0 = __lasx_xvaddwl_h_bu(tmp0, tmp5);                             \
    tmp2 = __lasx_xvmul_h(tmp2, h_20);                                  \
    tmp1 = __lasx_xvmul_h(tmp1, h_5);                                   \
    tmp2 = __lasx_xvssub_h(tmp2, tmp1);                                 \
    tmp2 = __lasx_xvsadd_h(tmp2, tmp0);                                 \
    tmp2 = __lasx_xvsadd_h(tmp2, h_16);                                 \
    tmp2 = __lasx_xvssrani_bu_h(tmp2, tmp2, 5);                         \
}

static av_always_inline void
put_h264_qpel8_v_lowpass_lasx(uint8_t *dst, uint8_t *src, int dstStride,
                              int srcStride)
{
    int srcStride_2x = srcStride << 1;
    int dstStride_2x = dstStride << 1;
    int srcStride_4x = srcStride << 2;
    int srcStride_3x = srcStride_2x + srcStride;
    __m256i src00, src01, src02, src03, src04, src05, src06;
    __m256i src07, src08, src09, src10, src11, src12;
    __m256i tmp00, tmp01, tmp02, tmp03, tmp04, tmp05;
    __m256i h_20 = __lasx_xvldi(0x414);
    __m256i h_5  = __lasx_xvldi(0x405);
    __m256i h_16 = __lasx_xvldi(0x410);

    DUP2_ARG2(__lasx_xvld, src - srcStride_2x, 0, src - srcStride, 0,
              src00, src01);
    src02 = __lasx_xvld(src, 0);
    DUP4_ARG2(__lasx_xvldx, src, srcStride, src, srcStride_2x, src,
              srcStride_3x, src, srcStride_4x, src03, src04, src05, src06);
    src += srcStride_4x;
    DUP4_ARG2(__lasx_xvldx, src, srcStride, src, srcStride_2x, src,
              srcStride_3x, src, srcStride_4x, src07, src08, src09, src10);
    src += srcStride_4x;
    DUP2_ARG2(__lasx_xvldx, src, srcStride, src, srcStride_2x, src11, src12);

    QPEL8_V_LOWPASS(src00, src01, src02, src03, src04, src05, src06,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    __lasx_xvstelm_d(tmp02, dst, 0, 0);
    __lasx_xvstelm_d(tmp02, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    QPEL8_V_LOWPASS(src02, src03, src04, src05, src06, src07, src08,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    __lasx_xvstelm_d(tmp02, dst, 0, 0);
    __lasx_xvstelm_d(tmp02, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    QPEL8_V_LOWPASS(src04, src05, src06, src07, src08, src09, src10,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    __lasx_xvstelm_d(tmp02, dst, 0, 0);
    __lasx_xvstelm_d(tmp02, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    QPEL8_V_LOWPASS(src06, src07, src08, src09, src10, src11, src12,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    __lasx_xvstelm_d(tmp02, dst, 0, 0);
    __lasx_xvstelm_d(tmp02, dst + dstStride, 0, 2);
}

static av_always_inline void
avg_h264_qpel8_v_lowpass_lasx(uint8_t *dst, uint8_t *src, int dstStride,
                              int srcStride)
{
    int srcStride_2x = srcStride << 1;
    int srcStride_4x = srcStride << 2;
    int dstStride_2x = dstStride << 1;
    int dstStride_4x = dstStride << 2;
    int srcStride_3x = srcStride_2x + srcStride;
    int dstStride_3x = dstStride_2x + dstStride;
    __m256i src00, src01, src02, src03, src04, src05, src06;
    __m256i src07, src08, src09, src10, src11, src12, tmp00;
    __m256i tmp01, tmp02, tmp03, tmp04, tmp05, tmp06, tmp07, tmp08, tmp09;
    __m256i h_20 = __lasx_xvldi(0x414);
    __m256i h_5  = __lasx_xvldi(0x405);
    __m256i h_16 = __lasx_xvldi(0x410);


    DUP2_ARG2(__lasx_xvld, src - srcStride_2x, 0, src - srcStride, 0,
              src00, src01);
    src02 = __lasx_xvld(src, 0);
    DUP4_ARG2(__lasx_xvldx, src, srcStride, src, srcStride_2x, src,
              srcStride_3x, src, srcStride_4x, src03, src04, src05, src06);
    src += srcStride_4x;
    DUP4_ARG2(__lasx_xvldx, src, srcStride, src, srcStride_2x, src,
              srcStride_3x, src, srcStride_4x, src07, src08, src09, src10);
    src += srcStride_4x;
    DUP2_ARG2(__lasx_xvldx, src, srcStride, src, srcStride_2x, src11, src12);

    tmp06 = __lasx_xvld(dst, 0);
    DUP4_ARG2(__lasx_xvldx, dst, dstStride, dst, dstStride_2x,
              dst, dstStride_3x, dst, dstStride_4x,
              tmp07, tmp02, tmp03, tmp04);
    dst += dstStride_4x;
    DUP2_ARG2(__lasx_xvldx, dst, dstStride, dst, dstStride_2x,
              tmp05, tmp00);
    tmp01 = __lasx_xvldx(dst, dstStride_3x);
    dst -= dstStride_4x;

    tmp06 = __lasx_xvpermi_q(tmp06, tmp07, 0x02);
    tmp07 = __lasx_xvpermi_q(tmp02, tmp03, 0x02);
    tmp08 = __lasx_xvpermi_q(tmp04, tmp05, 0x02);
    tmp09 = __lasx_xvpermi_q(tmp00, tmp01, 0x02);

    QPEL8_V_LOWPASS(src00, src01, src02, src03, src04, src05, src06,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    tmp06 = __lasx_xvavgr_bu(tmp06, tmp02);
    __lasx_xvstelm_d(tmp06, dst, 0, 0);
    __lasx_xvstelm_d(tmp06, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    QPEL8_V_LOWPASS(src02, src03, src04, src05, src06, src07, src08,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    tmp07 = __lasx_xvavgr_bu(tmp07, tmp02);
    __lasx_xvstelm_d(tmp07, dst, 0, 0);
    __lasx_xvstelm_d(tmp07, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    QPEL8_V_LOWPASS(src04, src05, src06, src07, src08, src09, src10,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    tmp08 = __lasx_xvavgr_bu(tmp08, tmp02);
    __lasx_xvstelm_d(tmp08, dst, 0, 0);
    __lasx_xvstelm_d(tmp08, dst + dstStride, 0, 2);
    dst += dstStride_2x;
    QPEL8_V_LOWPASS(src06, src07, src08, src09, src10, src11, src12,
                    tmp00, tmp01, tmp02, tmp03, tmp04, tmp05);
    tmp09 = __lasx_xvavgr_bu(tmp09, tmp02);
    __lasx_xvstelm_d(tmp09, dst, 0, 0);
    __lasx_xvstelm_d(tmp09, dst + dstStride, 0, 2);
}

#define QPEL8_HV_LOWPASS_H(tmp)                                              \
{                                                                            \
    src00 = __lasx_xvld(src, -2);                                            \
    src += srcStride;                                                        \
    src10 = __lasx_xvld(src, -2);                                            \
    src += srcStride;                                                        \
    src00 = __lasx_xvpermi_q(src00, src10, 0x02);                            \
    src01 = __lasx_xvshuf_b(src00, src00, (__m256i)mask1);                   \
    src02 = __lasx_xvshuf_b(src00, src00, (__m256i)mask2);                   \
    src03 = __lasx_xvshuf_b(src00, src00, (__m256i)mask3);                   \
    src04 = __lasx_xvshuf_b(src00, src00, (__m256i)mask4);                   \
    src05 = __lasx_xvshuf_b(src00, src00, (__m256i)mask5);                   \
    DUP2_ARG2(__lasx_xvaddwl_h_bu, src02, src03, src01, src04, src02, src01);\
    src00 = __lasx_xvaddwl_h_bu(src00, src05);                               \
    src02 = __lasx_xvmul_h(src02, h_20);                                     \
    src01 = __lasx_xvmul_h(src01, h_5);                                      \
    src02 = __lasx_xvssub_h(src02, src01);                                   \
    tmp  = __lasx_xvsadd_h(src02, src00);                                    \
}

#define QPEL8_HV_LOWPASS_V(src0, src1, src2, src3,                       \
                           src4, src5, temp0, temp1,                     \
                           temp2, temp3, temp4, temp5,                   \
                           out)                                          \
{                                                                        \
    DUP2_ARG2(__lasx_xvaddwl_w_h, src2, src3, src1, src4, temp0, temp2); \
    DUP2_ARG2(__lasx_xvaddwh_w_h, src2, src3, src1, src4, temp1, temp3); \
    temp4 = __lasx_xvaddwl_w_h(src0, src5);                              \
    temp5 = __lasx_xvaddwh_w_h(src0, src5);                              \
    temp0 = __lasx_xvmul_w(temp0, w_20);                                 \
    temp1 = __lasx_xvmul_w(temp1, w_20);                                 \
    temp2 = __lasx_xvmul_w(temp2, w_5);                                  \
    temp3 = __lasx_xvmul_w(temp3, w_5);                                  \
    temp0 = __lasx_xvssub_w(temp0, temp2);                               \
    temp1 = __lasx_xvssub_w(temp1, temp3);                               \
    temp0 = __lasx_xvsadd_w(temp0, temp4);                               \
    temp1 = __lasx_xvsadd_w(temp1, temp5);                               \
    temp0 = __lasx_xvsadd_w(temp0, w_512);                               \
    temp1 = __lasx_xvsadd_w(temp1, w_512);                               \
    temp0 = __lasx_xvssrani_hu_w(temp0, temp0, 10);                      \
    temp1 = __lasx_xvssrani_hu_w(temp1, temp1, 10);                      \
    temp0 = __lasx_xvpackev_d(temp1, temp0);                             \
    out   = __lasx_xvssrani_bu_h(temp0, temp0, 0);                       \
}

static av_always_inline void
put_h264_qpel8_hv_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                               ptrdiff_t dstStride, ptrdiff_t srcStride)
{
    __m256i src00, src01, src02, src03, src04, src05, src10;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    __m256i tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
    __m256i h_20 = __lasx_xvldi(0x414);
    __m256i h_5  = __lasx_xvldi(0x405);
    __m256i w_20 = __lasx_xvldi(0x814);
    __m256i w_5  = __lasx_xvldi(0x805);
    __m256i w_512 = {512};
    __m256i mask1 = {0x0807060504030201, 0x0, 0x0807060504030201, 0x0};
    __m256i mask2 = {0x0908070605040302, 0x0, 0x0908070605040302, 0x0};
    __m256i mask3 = {0x0a09080706050403, 0x0, 0x0a09080706050403, 0x0};
    __m256i mask4 = {0x0b0a090807060504, 0x0, 0x0b0a090807060504, 0x0};
    __m256i mask5 = {0x0c0b0a0908070605, 0x0, 0x0c0b0a0908070605, 0x0};

    w_512 = __lasx_xvreplve0_w(w_512);

    src -= srcStride << 1;
    QPEL8_HV_LOWPASS_H(tmp0)
    QPEL8_HV_LOWPASS_H(tmp2)
    QPEL8_HV_LOWPASS_H(tmp4)
    QPEL8_HV_LOWPASS_H(tmp6)
    QPEL8_HV_LOWPASS_H(tmp8)
    QPEL8_HV_LOWPASS_H(tmp10)
    QPEL8_HV_LOWPASS_H(tmp12)
    tmp11 = __lasx_xvpermi_q(tmp12, tmp10, 0x21);
    tmp9  = __lasx_xvpermi_q(tmp10, tmp8,  0x21);
    tmp7  = __lasx_xvpermi_q(tmp8,  tmp6,  0x21);
    tmp5  = __lasx_xvpermi_q(tmp6,  tmp4,  0x21);
    tmp3  = __lasx_xvpermi_q(tmp4,  tmp2,  0x21);
    tmp1  = __lasx_xvpermi_q(tmp2,  tmp0,  0x21);

    QPEL8_HV_LOWPASS_V(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, src00, src01,
                       src02, src03, src04, src05, tmp0)
    QPEL8_HV_LOWPASS_V(tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, src00, src01,
                       src02, src03, src04, src05, tmp2)
    QPEL8_HV_LOWPASS_V(tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, src00, src01,
                       src02, src03, src04, src05, tmp4)
    QPEL8_HV_LOWPASS_V(tmp6, tmp7, tmp8, tmp9, tmp10, tmp11, src00, src01,
                       src02, src03, src04, src05, tmp6)
    __lasx_xvstelm_d(tmp0, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp0, dst, 0, 2);
    dst += dstStride;
    __lasx_xvstelm_d(tmp2, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp2, dst, 0, 2);
    dst += dstStride;
    __lasx_xvstelm_d(tmp4, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp4, dst, 0, 2);
    dst += dstStride;
    __lasx_xvstelm_d(tmp6, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp6, dst, 0, 2);
}

static av_always_inline void
avg_h264_qpel8_h_lowpass_lasx(uint8_t *dst, const uint8_t *src, int dstStride,
                              int srcStride)
{
    int dstStride_2x = dstStride << 1;
    int dstStride_4x = dstStride << 2;
    int dstStride_3x = dstStride_2x + dstStride;
    __m256i src00, src01, src02, src03, src04, src05, src10;
    __m256i dst00, dst01, dst0, dst1, dst2, dst3;
    __m256i out0, out1, out2, out3;
    __m256i h_20 = __lasx_xvldi(0x414);
    __m256i h_5  = __lasx_xvldi(0x405);
    __m256i h_16 = __lasx_xvldi(0x410);
    __m256i mask1 = {0x0807060504030201, 0x0, 0x0807060504030201, 0x0};
    __m256i mask2 = {0x0908070605040302, 0x0, 0x0908070605040302, 0x0};
    __m256i mask3 = {0x0a09080706050403, 0x0, 0x0a09080706050403, 0x0};
    __m256i mask4 = {0x0b0a090807060504, 0x0, 0x0b0a090807060504, 0x0};
    __m256i mask5 = {0x0c0b0a0908070605, 0x0, 0x0c0b0a0908070605, 0x0};

    QPEL8_H_LOWPASS(out0)
    QPEL8_H_LOWPASS(out1)
    QPEL8_H_LOWPASS(out2)
    QPEL8_H_LOWPASS(out3)
    src00 = __lasx_xvld(dst, 0);
    DUP4_ARG2(__lasx_xvldx, dst, dstStride, dst, dstStride_2x, dst,
              dstStride_3x, dst, dstStride_4x, src01, src02, src03, src04);
    dst += dstStride_4x;
    DUP2_ARG2(__lasx_xvldx, dst, dstStride, dst, dstStride_2x, src05, dst00);
    dst01 = __lasx_xvldx(dst, dstStride_3x);
    dst -= dstStride_4x;
    dst0 = __lasx_xvpermi_q(src00, src01, 0x02);
    dst1 = __lasx_xvpermi_q(src02, src03, 0x02);
    dst2 = __lasx_xvpermi_q(src04, src05, 0x02);
    dst3 = __lasx_xvpermi_q(dst00, dst01, 0x02);
    dst0 = __lasx_xvavgr_bu(dst0, out0);
    dst1 = __lasx_xvavgr_bu(dst1, out1);
    dst2 = __lasx_xvavgr_bu(dst2, out2);
    dst3 = __lasx_xvavgr_bu(dst3, out3);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + dstStride, 0, 2);
    __lasx_xvstelm_d(dst1, dst + dstStride_2x, 0, 0);
    __lasx_xvstelm_d(dst1, dst + dstStride_3x, 0, 2);
    dst += dstStride_4x;
    __lasx_xvstelm_d(dst2, dst, 0, 0);
    __lasx_xvstelm_d(dst2, dst + dstStride, 0, 2);
    __lasx_xvstelm_d(dst3, dst + dstStride_2x, 0, 0);
    __lasx_xvstelm_d(dst3, dst + dstStride_3x, 0, 2);
}

static av_always_inline void
avg_h264_qpel8_hv_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                               ptrdiff_t dstStride, ptrdiff_t srcStride)
{
    __m256i src00, src01, src02, src03, src04, src05, src10;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    __m256i tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
    __m256i h_20 = __lasx_xvldi(0x414);
    __m256i h_5  = __lasx_xvldi(0x405);
    __m256i w_20 = __lasx_xvldi(0x814);
    __m256i w_5  = __lasx_xvldi(0x805);
    __m256i w_512 = {512};
    __m256i mask1 = {0x0807060504030201, 0x0, 0x0807060504030201, 0x0};
    __m256i mask2 = {0x0908070605040302, 0x0, 0x0908070605040302, 0x0};
    __m256i mask3 = {0x0a09080706050403, 0x0, 0x0a09080706050403, 0x0};
    __m256i mask4 = {0x0b0a090807060504, 0x0, 0x0b0a090807060504, 0x0};
    __m256i mask5 = {0x0c0b0a0908070605, 0x0, 0x0c0b0a0908070605, 0x0};
    ptrdiff_t dstStride_2x = dstStride << 1;
    ptrdiff_t dstStride_4x = dstStride << 2;
    ptrdiff_t dstStride_3x = dstStride_2x + dstStride;

    w_512 = __lasx_xvreplve0_w(w_512);

    src -= srcStride << 1;
    QPEL8_HV_LOWPASS_H(tmp0)
    QPEL8_HV_LOWPASS_H(tmp2)
    QPEL8_HV_LOWPASS_H(tmp4)
    QPEL8_HV_LOWPASS_H(tmp6)
    QPEL8_HV_LOWPASS_H(tmp8)
    QPEL8_HV_LOWPASS_H(tmp10)
    QPEL8_HV_LOWPASS_H(tmp12)
    tmp11 = __lasx_xvpermi_q(tmp12, tmp10, 0x21);
    tmp9  = __lasx_xvpermi_q(tmp10, tmp8,  0x21);
    tmp7  = __lasx_xvpermi_q(tmp8,  tmp6,  0x21);
    tmp5  = __lasx_xvpermi_q(tmp6,  tmp4,  0x21);
    tmp3  = __lasx_xvpermi_q(tmp4,  tmp2,  0x21);
    tmp1  = __lasx_xvpermi_q(tmp2,  tmp0,  0x21);

    QPEL8_HV_LOWPASS_V(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, src00, src01,
                       src02, src03, src04, src05, tmp0)
    QPEL8_HV_LOWPASS_V(tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, src00, src01,
                       src02, src03, src04, src05, tmp2)
    QPEL8_HV_LOWPASS_V(tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, src00, src01,
                       src02, src03, src04, src05, tmp4)
    QPEL8_HV_LOWPASS_V(tmp6, tmp7, tmp8, tmp9, tmp10, tmp11, src00, src01,
                       src02, src03, src04, src05, tmp6)

    src00 = __lasx_xvld(dst, 0);
    DUP4_ARG2(__lasx_xvldx, dst, dstStride, dst, dstStride_2x, dst,
              dstStride_3x, dst, dstStride_4x, src01, src02, src03, src04);
    dst += dstStride_4x;
    DUP2_ARG2(__lasx_xvldx, dst, dstStride, dst, dstStride_2x, src05, tmp8);
    tmp9 = __lasx_xvldx(dst, dstStride_3x);
    dst -= dstStride_4x;
    tmp1 = __lasx_xvpermi_q(src00, src01, 0x02);
    tmp3 = __lasx_xvpermi_q(src02, src03, 0x02);
    tmp5 = __lasx_xvpermi_q(src04, src05, 0x02);
    tmp7 = __lasx_xvpermi_q(tmp8,  tmp9,  0x02);
    tmp0 = __lasx_xvavgr_bu(tmp0, tmp1);
    tmp2 = __lasx_xvavgr_bu(tmp2, tmp3);
    tmp4 = __lasx_xvavgr_bu(tmp4, tmp5);
    tmp6 = __lasx_xvavgr_bu(tmp6, tmp7);
    __lasx_xvstelm_d(tmp0, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp0, dst, 0, 2);
    dst += dstStride;
    __lasx_xvstelm_d(tmp2, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp2, dst, 0, 2);
    dst += dstStride;
    __lasx_xvstelm_d(tmp4, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp4, dst, 0, 2);
    dst += dstStride;
    __lasx_xvstelm_d(tmp6, dst, 0, 0);
    dst += dstStride;
    __lasx_xvstelm_d(tmp6, dst, 0, 2);
}

static av_always_inline void
put_h264_qpel16_h_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                               int dstStride, int srcStride)
{
    put_h264_qpel8_h_lowpass_lasx(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_lasx(dst+8, src+8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    put_h264_qpel8_h_lowpass_lasx(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_lasx(dst+8, src+8, dstStride, srcStride);
}

static av_always_inline void
avg_h264_qpel16_h_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                               int dstStride, int srcStride)
{
    avg_h264_qpel8_h_lowpass_lasx(dst, src, dstStride, srcStride);
    avg_h264_qpel8_h_lowpass_lasx(dst+8, src+8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    avg_h264_qpel8_h_lowpass_lasx(dst, src, dstStride, srcStride);
    avg_h264_qpel8_h_lowpass_lasx(dst+8, src+8, dstStride, srcStride);
}

static void put_h264_qpel16_v_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                                           int dstStride, int srcStride)
{
    put_h264_qpel8_v_lowpass_lasx(dst, (uint8_t*)src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_lasx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_v_lowpass_lasx(dst, (uint8_t*)src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_lasx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
}

static void avg_h264_qpel16_v_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                                           int dstStride, int srcStride)
{
    avg_h264_qpel8_v_lowpass_lasx(dst, (uint8_t*)src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_lasx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_v_lowpass_lasx(dst, (uint8_t*)src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_lasx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
}

static void put_h264_qpel16_hv_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                                     ptrdiff_t dstStride, ptrdiff_t srcStride)
{
    put_h264_qpel8_hv_lowpass_lasx(dst, src, dstStride, srcStride);
    put_h264_qpel8_hv_lowpass_lasx(dst + 8, src + 8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    put_h264_qpel8_hv_lowpass_lasx(dst, src, dstStride, srcStride);
    put_h264_qpel8_hv_lowpass_lasx(dst + 8, src + 8, dstStride, srcStride);
}

static void avg_h264_qpel16_hv_lowpass_lasx(uint8_t *dst, const uint8_t *src,
                                     ptrdiff_t dstStride, ptrdiff_t srcStride)
{
    avg_h264_qpel8_hv_lowpass_lasx(dst, src, dstStride, srcStride);
    avg_h264_qpel8_hv_lowpass_lasx(dst + 8, src + 8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    avg_h264_qpel8_hv_lowpass_lasx(dst, src, dstStride, srcStride);
    avg_h264_qpel8_hv_lowpass_lasx(dst + 8, src + 8, dstStride, srcStride);
}

void ff_put_h264_qpel8_mc00_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    /* In mmi optimization, it used function ff_put_pixels8_8_mmi
     * which implemented in hpeldsp_mmi.c */
    put_pixels8_8_inline_asm(dst, src, stride);
}

void ff_put_h264_qpel8_mc10_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lasx(half, src, 8, stride);
    /* in qpel8, the stride of half and height of block is 8 */
    put_pixels8_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_put_h264_qpel8_mc20_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    put_h264_qpel8_h_lowpass_lasx(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc30_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lasx(half, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src+1, half, stride, stride);
}

void ff_put_h264_qpel8_mc01_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_v_lowpass_lasx(half, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_put_h264_qpel8_mc11_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc21_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lasx(halfH, src, 8, stride);
    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc31_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src + 1, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc02_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    put_h264_qpel8_v_lowpass_lasx(dst, (uint8_t*)src, stride, stride);
}

void ff_put_h264_qpel8_mc12_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfH, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc22_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    put_h264_qpel8_hv_lowpass_lasx(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc32_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfH, (uint8_t*)src + 1, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc03_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_v_lowpass_lasx(half, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src + stride, half, stride, stride);
}

void ff_put_h264_qpel8_mc13_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc23_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lasx(halfH, src + stride, 8, stride);
    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc33_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src + 1, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc00_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    /* In mmi optimization, it used function ff_avg_pixels8_8_mmi
     * which implemented in hpeldsp_mmi.c */
    avg_pixels8_8_lsx(dst, src, stride);
}

void ff_avg_h264_qpel8_mc10_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lasx(half, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_avg_h264_qpel8_mc20_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avg_h264_qpel8_h_lowpass_lasx(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc30_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lasx(half, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, src+1, half, stride, stride);
}

void ff_avg_h264_qpel8_mc11_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc21_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lasx(halfH, src, 8, stride);
    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc31_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src + 1, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc02_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avg_h264_qpel8_v_lowpass_lasx(dst, (uint8_t*)src, stride, stride);
}

void ff_avg_h264_qpel8_mc12_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfH, (uint8_t*)src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc22_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avg_h264_qpel8_hv_lowpass_lasx(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc32_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfH, (uint8_t*)src + 1, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc13_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc23_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lasx(halfH, src + stride, 8, stride);
    put_h264_qpel8_hv_lowpass_lasx(halfHV, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc33_lasx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lasx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lasx(halfV, (uint8_t*)src + 1, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel16_mc00_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    /* In mmi optimization, it used function ff_put_pixels16_8_mmi
     * which implemented in hpeldsp_mmi.c */
    put_pixels16_8_lsx(dst, src, stride);
}

void ff_put_h264_qpel16_mc10_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_h_lowpass_lasx(half, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_put_h264_qpel16_mc20_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    put_h264_qpel16_h_lowpass_lasx(dst, src, stride, stride);
}

void ff_put_h264_qpel16_mc30_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_h_lowpass_lasx(half, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, src+1, half, stride, stride);
}

void ff_put_h264_qpel16_mc01_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_v_lowpass_lasx(half, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_put_h264_qpel16_mc11_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_lasx((uint8_t*)src - 2, (uint8_t*)src - (stride * 2),
                               dst, stride);
}

void ff_put_h264_qpel16_mc21_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lasx(halfH, src, 16, stride);
    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc31_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_lasx((uint8_t*)src - 2, (uint8_t*)src - (stride * 2) + 1,
                               dst, stride);
}

void ff_put_h264_qpel16_mc02_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    put_h264_qpel16_v_lowpass_lasx(dst, src, stride, stride);
}

void ff_put_h264_qpel16_mc12_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lasx(halfH, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc22_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    put_h264_qpel16_hv_lowpass_lasx(dst, src, stride, stride);
}

void ff_put_h264_qpel16_mc32_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lasx(halfH, src + 1, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc03_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_v_lowpass_lasx(half, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, src+stride, half, stride, stride);
}

void ff_put_h264_qpel16_mc13_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_lasx((uint8_t*)src + stride - 2, (uint8_t*)src - (stride * 2),
                               dst, stride);
}

void ff_put_h264_qpel16_mc23_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lasx(halfH, src + stride, 16, stride);
    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc33_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_lasx((uint8_t*)src + stride - 2,
                               (uint8_t*)src - (stride * 2) + 1, dst, stride);
}

void ff_avg_h264_qpel16_mc00_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    /* In mmi optimization, it used function ff_avg_pixels16_8_mmi
     * which implemented in hpeldsp_mmi.c */
    avg_pixels16_8_lsx(dst, src, stride);
}

void ff_avg_h264_qpel16_mc10_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_h_lowpass_lasx(half, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_avg_h264_qpel16_mc20_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avg_h264_qpel16_h_lowpass_lasx(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc30_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_h_lowpass_lasx(half, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, src+1, half, stride, stride);
}

void ff_avg_h264_qpel16_mc01_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_v_lowpass_lasx(half, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_avg_h264_qpel16_mc11_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_lasx((uint8_t*)src - 2,
                                           (uint8_t*)src - (stride * 2),
                                           dst, stride);
}

void ff_avg_h264_qpel16_mc21_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lasx(halfH, src, 16, stride);
    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc31_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_lasx((uint8_t*)src - 2,
                                            (uint8_t*)src - (stride * 2) + 1,
                                            dst, stride);
}

void ff_avg_h264_qpel16_mc02_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avg_h264_qpel16_v_lowpass_lasx(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc12_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lasx(halfH, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc22_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avg_h264_qpel16_hv_lowpass_lasx(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc32_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lasx(halfH, src + 1, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc03_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_v_lowpass_lasx(half, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, src + stride, half, stride, stride);
}

void ff_avg_h264_qpel16_mc13_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_lasx((uint8_t*)src + stride - 2,
                                            (uint8_t*)src - (stride * 2),
                                            dst, stride);
}

void ff_avg_h264_qpel16_mc23_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lasx(halfH, src + stride, 16, stride);
    put_h264_qpel16_hv_lowpass_lasx(halfHV, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc33_lasx(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_lasx((uint8_t*)src + stride - 2,
                                            (uint8_t*)src - (stride * 2) + 1,
                                            dst, stride);
}
