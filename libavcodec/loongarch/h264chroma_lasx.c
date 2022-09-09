/*
 * Loongson LASX optimized h264chroma
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

#include "h264chroma_lasx.h"
#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

static const uint8_t chroma_mask_arr[64] = {
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20
};

static av_always_inline void avc_chroma_hv_8x4_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride, uint32_t coef_hor0,
                             uint32_t coef_hor1, uint32_t coef_ver0,
                             uint32_t coef_ver1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride_2x << 1;
    __m256i src0, src1, src2, src3, src4, out;
    __m256i res_hz0, res_hz1, res_hz2, res_vt0, res_vt1;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_hz_vec = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 0, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src, stride_3x, src, stride_4x,
              src1, src2, src3, src4);
    DUP2_ARG3(__lasx_xvpermi_q, src2, src1, 0x20, src4, src3, 0x20, src1, src3);
    src0 = __lasx_xvshuf_b(src0, src0, mask);
    DUP2_ARG3(__lasx_xvshuf_b, src1, src1, mask, src3, src3, mask, src1, src3);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_hz_vec, src1, coeff_hz_vec, res_hz0, res_hz1);
    res_hz2 = __lasx_xvdp2_h_bu(src3, coeff_hz_vec);
    res_vt0 = __lasx_xvmul_h(res_hz1, coeff_vt_vec0);
    res_vt1 = __lasx_xvmul_h(res_hz2, coeff_vt_vec0);
    res_hz0 = __lasx_xvpermi_q(res_hz1, res_hz0, 0x20);
    res_hz1 = __lasx_xvpermi_q(res_hz1, res_hz2, 0x3);
    res_vt0 = __lasx_xvmadd_h(res_vt0, res_hz0, coeff_vt_vec1);
    res_vt1 = __lasx_xvmadd_h(res_vt1, res_hz1, coeff_vt_vec1);
    out = __lasx_xvssrarni_bu_h(res_vt1, res_vt0, 6);
    __lasx_xvstelm_d(out, dst, 0, 0);
    __lasx_xvstelm_d(out, dst + stride, 0, 2);
    __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_hv_8x8_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride, uint32_t coef_hor0,
                             uint32_t coef_hor1, uint32_t coef_ver0,
                             uint32_t coef_ver1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i out0, out1;
    __m256i res_hz0, res_hz1, res_hz2, res_hz3, res_hz4;
    __m256i res_vt0, res_vt1, res_vt2, res_vt3;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_hz_vec = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 0, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src, stride_3x, src, stride_4x,
              src1, src2, src3, src4);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src, stride_3x, src, stride_4x,
              src5, src6, src7, src8);
    DUP4_ARG3(__lasx_xvpermi_q, src2, src1, 0x20, src4, src3, 0x20, src6, src5, 0x20,
              src8, src7, 0x20, src1, src3, src5, src7);
    src0 = __lasx_xvshuf_b(src0, src0, mask);
    DUP4_ARG3(__lasx_xvshuf_b, src1, src1, mask, src3, src3, mask, src5, src5, mask, src7,
              src7, mask, src1, src3, src5, src7);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_hz_vec, src1, coeff_hz_vec, src3,
              coeff_hz_vec, src5, coeff_hz_vec, res_hz0, res_hz1, res_hz2, res_hz3);
    res_hz4 = __lasx_xvdp2_h_bu(src7, coeff_hz_vec);
    res_vt0 = __lasx_xvmul_h(res_hz1, coeff_vt_vec0);
    res_vt1 = __lasx_xvmul_h(res_hz2, coeff_vt_vec0);
    res_vt2 = __lasx_xvmul_h(res_hz3, coeff_vt_vec0);
    res_vt3 = __lasx_xvmul_h(res_hz4, coeff_vt_vec0);
    res_hz0 = __lasx_xvpermi_q(res_hz1, res_hz0, 0x20);
    res_hz1 = __lasx_xvpermi_q(res_hz1, res_hz2, 0x3);
    res_hz2 = __lasx_xvpermi_q(res_hz2, res_hz3, 0x3);
    res_hz3 = __lasx_xvpermi_q(res_hz3, res_hz4, 0x3);
    DUP4_ARG3(__lasx_xvmadd_h, res_vt0, res_hz0, coeff_vt_vec1, res_vt1, res_hz1, coeff_vt_vec1,
              res_vt2, res_hz2, coeff_vt_vec1, res_vt3, res_hz3, coeff_vt_vec1,
              res_vt0, res_vt1, res_vt2, res_vt3);
    DUP2_ARG3(__lasx_xvssrarni_bu_h, res_vt1, res_vt0, 6, res_vt3, res_vt2, 6, out0, out1);
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + stride, 0, 2);
    __lasx_xvstelm_d(out0, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + stride, 0, 2);
    __lasx_xvstelm_d(out1, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out1, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_hz_8x4_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride, uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    __m256i src0, src1, src2, src3, out;
    __m256i res0, res1;
    __m256i mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 0, src, 0, mask, src0);
    DUP2_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src1, src2);
    src3 = __lasx_xvldx(src, stride_3x);
    DUP2_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src0, src2);
    DUP2_ARG3(__lasx_xvshuf_b, src0, src0, mask, src2, src2, mask, src0, src2);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, res0, res1);
    out = __lasx_xvssrarni_bu_h(res1, res0, 6);
    __lasx_xvstelm_d(out, dst, 0, 0);
    __lasx_xvstelm_d(out, dst + stride, 0, 2);
    __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);

}

static av_always_inline void avc_chroma_hz_8x8_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride, uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i out0, out1;
    __m256i res0, res1, res2, res3;
    __m256i mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 0, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src, stride_3x, src, stride_4x,
              src1, src2, src3, src4);
    src += stride_4x;
    DUP2_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src5, src6);
    src7 = __lasx_xvldx(src, stride_3x);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4, 0x20,
              src7, src6, 0x20, src0, src2, src4, src6);
    DUP4_ARG3(__lasx_xvshuf_b, src0, src0, mask, src2, src2, mask, src4, src4, mask,
              src6, src6, mask, src0, src2, src4, src6);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, src4, coeff_vec, src6,
              coeff_vec, res0, res1, res2, res3);
    DUP2_ARG3(__lasx_xvssrarni_bu_h, res1, res0, 6, res3, res2, 6, out0, out1);
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + stride, 0, 2);
    __lasx_xvstelm_d(out0, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + stride, 0, 2);
    __lasx_xvstelm_d(out1, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out1, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_hz_nonmult_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coeff0,
                             uint32_t coeff1, int32_t height)
{
    uint32_t row;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i src0, src1, src2, src3, out;
    __m256i res0, res1;
    __m256i mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    mask = __lasx_xvld(chroma_mask_arr, 0);
    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);

    for (row = height >> 2; row--;) {
        DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
                  src0, src1, src2, src3);
        src += stride_4x;
        DUP2_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src0, src2);
        DUP2_ARG3(__lasx_xvshuf_b, src0, src0, mask, src2, src2, mask, src0, src2);
        DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, res0, res1);
        out = __lasx_xvssrarni_bu_h(res1, res0, 6);
        __lasx_xvstelm_d(out, dst, 0, 0);
        __lasx_xvstelm_d(out, dst + stride, 0, 2);
        __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
        __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);
        dst += stride_4x;
    }

    if ((height & 3)) {
        src0 = __lasx_xvld(src, 0);
        src1 = __lasx_xvldx(src, stride);
        src1 = __lasx_xvpermi_q(src1, src0, 0x20);
        src0 = __lasx_xvshuf_b(src1, src1, mask);
        res0 = __lasx_xvdp2_h_bu(src0, coeff_vec);
        out  = __lasx_xvssrarni_bu_h(res0, res0, 6);
        __lasx_xvstelm_d(out, dst, 0, 0);
        dst += stride;
        __lasx_xvstelm_d(out, dst, 0, 2);
    }
}

static av_always_inline void avc_chroma_vt_8x4_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride, uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    __m256i src0, src1, src2, src3, src4, out;
    __m256i res0, res1;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    src0 = __lasx_xvld(src, 0);
    src += stride;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src1, src2, src3, src4);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2, 0x20,
              src4, src3, 0x20, src0, src1, src2, src3);
    DUP2_ARG2(__lasx_xvilvl_b, src1, src0, src3, src2, src0, src2);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, res0, res1);
    out  = __lasx_xvssrarni_bu_h(res1, res0, 6);
    __lasx_xvstelm_d(out, dst, 0, 0);
    __lasx_xvstelm_d(out, dst + stride, 0, 2);
    __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_vt_8x8_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride, uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i out0, out1;
    __m256i res0, res1, res2, res3;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    src0 = __lasx_xvld(src, 0);
    src += stride;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src1, src2, src3, src4);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src5, src6, src7, src8);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2, 0x20,
              src4, src3, 0x20, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src5, src4, 0x20, src6, src5, 0x20, src7, src6, 0x20,
              src8, src7, 0x20, src4, src5, src6, src7);
    DUP4_ARG2(__lasx_xvilvl_b, src1, src0, src3, src2, src5, src4, src7, src6,
              src0, src2, src4, src6);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, src4, coeff_vec,
              src6, coeff_vec, res0, res1, res2, res3);
    DUP2_ARG3(__lasx_xvssrarni_bu_h, res1, res0, 6, res3, res2, 6, out0, out1);
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + stride, 0, 2);
    __lasx_xvstelm_d(out0, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + stride, 0, 2);
    __lasx_xvstelm_d(out1, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out1, dst + stride_3x, 0, 3);
}

static av_always_inline void copy_width8x8_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride)
{
    uint64_t tmp[8];
    ptrdiff_t stride_2, stride_3, stride_4;
    __asm__ volatile (
        "slli.d   %[stride_2],     %[stride],     1             \n\t"
        "add.d    %[stride_3],     %[stride_2],   %[stride]     \n\t"
        "slli.d   %[stride_4],     %[stride_2],   1             \n\t"
        "ld.d     %[tmp0],         %[src],        0x0           \n\t"
        "ldx.d    %[tmp1],         %[src],        %[stride]     \n\t"
        "ldx.d    %[tmp2],         %[src],        %[stride_2]   \n\t"
        "ldx.d    %[tmp3],         %[src],        %[stride_3]   \n\t"
        "add.d    %[src],          %[src],        %[stride_4]   \n\t"
        "ld.d     %[tmp4],         %[src],        0x0           \n\t"
        "ldx.d    %[tmp5],         %[src],        %[stride]     \n\t"
        "ldx.d    %[tmp6],         %[src],        %[stride_2]   \n\t"
        "ldx.d    %[tmp7],         %[src],        %[stride_3]   \n\t"

        "st.d     %[tmp0],         %[dst],        0x0           \n\t"
        "stx.d    %[tmp1],         %[dst],        %[stride]     \n\t"
        "stx.d    %[tmp2],         %[dst],        %[stride_2]   \n\t"
        "stx.d    %[tmp3],         %[dst],        %[stride_3]   \n\t"
        "add.d    %[dst],          %[dst],        %[stride_4]   \n\t"
        "st.d     %[tmp4],         %[dst],        0x0           \n\t"
        "stx.d    %[tmp5],         %[dst],        %[stride]     \n\t"
        "stx.d    %[tmp6],         %[dst],        %[stride_2]   \n\t"
        "stx.d    %[tmp7],         %[dst],        %[stride_3]   \n\t"
        : [tmp0]"=&r"(tmp[0]),        [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),        [tmp3]"=&r"(tmp[3]),
          [tmp4]"=&r"(tmp[4]),        [tmp5]"=&r"(tmp[5]),
          [tmp6]"=&r"(tmp[6]),        [tmp7]"=&r"(tmp[7]),
          [dst]"+&r"(dst),            [src]"+&r"(src),
          [stride_2]"=&r"(stride_2),  [stride_3]"=&r"(stride_3),
          [stride_4]"=&r"(stride_4)
        : [stride]"r"(stride)
        : "memory"
    );
}

static av_always_inline void copy_width8x4_lasx(const uint8_t *src, uint8_t *dst,
                             ptrdiff_t stride)
{
    uint64_t tmp[4];
    ptrdiff_t stride_2, stride_3;
    __asm__ volatile (
        "slli.d   %[stride_2],     %[stride],     1             \n\t"
        "add.d    %[stride_3],     %[stride_2],   %[stride]     \n\t"
        "ld.d     %[tmp0],         %[src],        0x0           \n\t"
        "ldx.d    %[tmp1],         %[src],        %[stride]     \n\t"
        "ldx.d    %[tmp2],         %[src],        %[stride_2]   \n\t"
        "ldx.d    %[tmp3],         %[src],        %[stride_3]   \n\t"

        "st.d     %[tmp0],         %[dst],        0x0           \n\t"
        "stx.d    %[tmp1],         %[dst],        %[stride]     \n\t"
        "stx.d    %[tmp2],         %[dst],        %[stride_2]   \n\t"
        "stx.d    %[tmp3],         %[dst],        %[stride_3]   \n\t"
        : [tmp0]"=&r"(tmp[0]),        [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),        [tmp3]"=&r"(tmp[3]),
          [stride_2]"=&r"(stride_2),  [stride_3]"=&r"(stride_3)
        : [stride]"r"(stride), [dst]"r"(dst), [src]"r"(src)
        : "memory"
    );
}

static void avc_chroma_hv_8w_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                  uint32_t coef_hor0, uint32_t coef_hor1,
                                  uint32_t coef_ver0, uint32_t coef_ver1,
                                  int32_t height)
{
    if (4 == height) {
        avc_chroma_hv_8x4_lasx(src, dst, stride, coef_hor0, coef_hor1, coef_ver0,
                               coef_ver1);
    } else if (8 == height) {
        avc_chroma_hv_8x8_lasx(src, dst, stride, coef_hor0, coef_hor1, coef_ver0,
                               coef_ver1);
    }
}

static void avc_chroma_hv_4x2_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coef_hor0, uint32_t coef_hor1,
                                   uint32_t coef_ver0, uint32_t coef_ver1)
{
    ptrdiff_t stride_2 = stride << 1;
    __m256i src0, src1, src2;
    __m256i res_hz, res_vt;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_hz_vec  = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);
    __m256i coeff_vt_vec  = __lasx_xvpermi_q(coeff_vt_vec1, coeff_vt_vec0, 0x02);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 32, src, 0, mask, src0);
    DUP2_ARG2(__lasx_xvldx, src, stride, src, stride_2, src1, src2);
    DUP2_ARG3(__lasx_xvshuf_b, src1, src0, mask, src2, src1, mask, src0, src1);
    src0 = __lasx_xvpermi_q(src0, src1, 0x02);
    res_hz = __lasx_xvdp2_h_bu(src0, coeff_hz_vec);
    res_vt = __lasx_xvmul_h(res_hz, coeff_vt_vec);
    res_hz = __lasx_xvpermi_q(res_hz, res_vt, 0x01);
    res_vt = __lasx_xvadd_h(res_hz, res_vt);
    res_vt = __lasx_xvssrarni_bu_h(res_vt, res_vt, 6);
    __lasx_xvstelm_w(res_vt, dst, 0, 0);
    __lasx_xvstelm_w(res_vt, dst + stride, 0, 1);
}

static void avc_chroma_hv_4x4_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coef_hor0, uint32_t coef_hor1,
                                   uint32_t coef_ver0, uint32_t coef_ver1)
{
    ptrdiff_t stride_2 = stride << 1;
    ptrdiff_t stride_3 = stride_2 + stride;
    ptrdiff_t stride_4 = stride_2 << 1;
    __m256i src0, src1, src2, src3, src4;
    __m256i res_hz0, res_hz1, res_vt0, res_vt1;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_hz_vec  = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 32, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src1, src2, src3, src4);
    DUP4_ARG3(__lasx_xvshuf_b, src1, src0, mask, src2, src1, mask, src3, src2, mask,
              src4, src3, mask, src0, src1, src2, src3);
    DUP2_ARG3(__lasx_xvpermi_q, src0, src2, 0x02, src1, src3, 0x02, src0, src1);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_hz_vec, src1, coeff_hz_vec, res_hz0, res_hz1);
    DUP2_ARG2(__lasx_xvmul_h, res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);
    res_hz0 = __lasx_xvadd_h(res_vt0, res_vt1);
    res_hz0 = __lasx_xvssrarni_bu_h(res_hz0, res_hz0, 6);
    __lasx_xvstelm_w(res_hz0, dst, 0, 0);
    __lasx_xvstelm_w(res_hz0, dst + stride, 0, 1);
    __lasx_xvstelm_w(res_hz0, dst + stride_2, 0, 4);
    __lasx_xvstelm_w(res_hz0, dst + stride_3, 0, 5);
}

static void avc_chroma_hv_4x8_lasx(const uint8_t *src, uint8_t * dst, ptrdiff_t stride,
                                   uint32_t coef_hor0, uint32_t coef_hor1,
                                   uint32_t coef_ver0, uint32_t coef_ver1)
{
    ptrdiff_t stride_2 = stride << 1;
    ptrdiff_t stride_3 = stride_2 + stride;
    ptrdiff_t stride_4 = stride_2 << 1;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i res_hz0, res_hz1, res_hz2, res_hz3;
    __m256i res_vt0, res_vt1, res_vt2, res_vt3;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_hz_vec  = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 32, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src1, src2, src3, src4);
    src += stride_4;
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src5, src6, src7, src8);
    DUP4_ARG3(__lasx_xvshuf_b, src1, src0, mask, src2, src1, mask, src3, src2, mask,
              src4, src3, mask, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvshuf_b, src5, src4, mask, src6, src5, mask, src7, src6, mask,
              src8, src7, mask, src4, src5, src6, src7);
    DUP4_ARG3(__lasx_xvpermi_q, src0, src2, 0x02, src1, src3, 0x02, src4, src6, 0x02,
              src5, src7, 0x02, src0, src1, src4, src5);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_hz_vec, src1, coeff_hz_vec, src4, coeff_hz_vec,
              src5, coeff_hz_vec, res_hz0, res_hz1, res_hz2, res_hz3);
    DUP4_ARG2(__lasx_xvmul_h, res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_hz2,
              coeff_vt_vec1, res_hz3, coeff_vt_vec0, res_vt0, res_vt1, res_vt2, res_vt3);
    DUP2_ARG2(__lasx_xvadd_h, res_vt0, res_vt1, res_vt2, res_vt3, res_vt0, res_vt2);
    res_hz0 = __lasx_xvssrarni_bu_h(res_vt2, res_vt0, 6);
    __lasx_xvstelm_w(res_hz0, dst, 0, 0);
    __lasx_xvstelm_w(res_hz0, dst + stride, 0, 1);
    __lasx_xvstelm_w(res_hz0, dst + stride_2, 0, 4);
    __lasx_xvstelm_w(res_hz0, dst + stride_3, 0, 5);
    dst += stride_4;
    __lasx_xvstelm_w(res_hz0, dst, 0, 2);
    __lasx_xvstelm_w(res_hz0, dst + stride, 0, 3);
    __lasx_xvstelm_w(res_hz0, dst + stride_2, 0, 6);
    __lasx_xvstelm_w(res_hz0, dst + stride_3, 0, 7);
}

static void avc_chroma_hv_4w_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                  uint32_t coef_hor0, uint32_t coef_hor1,
                                  uint32_t coef_ver0, uint32_t coef_ver1,
                                  int32_t height)
{
    if (8 == height) {
        avc_chroma_hv_4x8_lasx(src, dst, stride, coef_hor0, coef_hor1, coef_ver0,
                               coef_ver1);
    } else if (4 == height) {
        avc_chroma_hv_4x4_lasx(src, dst, stride, coef_hor0, coef_hor1, coef_ver0,
                               coef_ver1);
    } else if (2 == height) {
        avc_chroma_hv_4x2_lasx(src, dst, stride, coef_hor0, coef_hor1, coef_ver0,
                               coef_ver1);
    }
}

static void avc_chroma_hz_4x2_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coeff0, uint32_t coeff1)
{
    __m256i src0, src1;
    __m256i res, mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec  = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 32, src, 0, mask, src0);
    src1 = __lasx_xvldx(src, stride);
    src0 = __lasx_xvshuf_b(src1, src0, mask);
    res = __lasx_xvdp2_h_bu(src0, coeff_vec);
    res = __lasx_xvslli_h(res, 3);
    res = __lasx_xvssrarni_bu_h(res, res, 6);
    __lasx_xvstelm_w(res, dst, 0, 0);
    __lasx_xvstelm_w(res, dst + stride, 0, 1);
}

static void avc_chroma_hz_4x4_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2 = stride << 1;
    ptrdiff_t stride_3 = stride_2 + stride;
    __m256i src0, src1, src2, src3;
    __m256i res, mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec  = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 32, src, 0, mask, src0);
    DUP2_ARG2(__lasx_xvldx, src, stride, src, stride_2, src1, src2);
    src3 = __lasx_xvldx(src, stride_3);
    DUP2_ARG3(__lasx_xvshuf_b, src1, src0, mask, src3, src2, mask, src0, src2);
    src0 = __lasx_xvpermi_q(src0, src2, 0x02);
    res = __lasx_xvdp2_h_bu(src0, coeff_vec);
    res = __lasx_xvslli_h(res, 3);
    res = __lasx_xvssrarni_bu_h(res, res, 6);
    __lasx_xvstelm_w(res, dst, 0, 0);
    __lasx_xvstelm_w(res, dst + stride, 0, 1);
    __lasx_xvstelm_w(res, dst + stride_2, 0, 4);
    __lasx_xvstelm_w(res, dst + stride_3, 0, 5);
}

static void avc_chroma_hz_4x8_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2 = stride << 1;
    ptrdiff_t stride_3 = stride_2 + stride;
    ptrdiff_t stride_4 = stride_2 << 1;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i res0, res1, mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec  = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 32, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src1, src2, src3, src4);
    src += stride_4;
    DUP2_ARG2(__lasx_xvldx, src, stride, src, stride_2, src5, src6);
    src7 = __lasx_xvldx(src, stride_3);
    DUP4_ARG3(__lasx_xvshuf_b, src1, src0, mask, src3, src2, mask, src5, src4, mask,
              src7, src6, mask, src0, src2, src4, src6);
    DUP2_ARG3(__lasx_xvpermi_q, src0, src2, 0x02, src4, src6, 0x02, src0, src4);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src4, coeff_vec, res0, res1);
    res0 = __lasx_xvssrarni_bu_h(res1, res0, 6);
    __lasx_xvstelm_w(res0, dst, 0, 0);
    __lasx_xvstelm_w(res0, dst + stride, 0, 1);
    __lasx_xvstelm_w(res0, dst + stride_2, 0, 4);
    __lasx_xvstelm_w(res0, dst + stride_3, 0, 5);
    dst += stride_4;
    __lasx_xvstelm_w(res0, dst, 0, 2);
    __lasx_xvstelm_w(res0, dst + stride, 0, 3);
    __lasx_xvstelm_w(res0, dst + stride_2, 0, 6);
    __lasx_xvstelm_w(res0, dst + stride_3, 0, 7);
}

static void avc_chroma_hz_4w_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                  uint32_t coeff0, uint32_t coeff1,
                                  int32_t height)
{
    if (8 == height) {
        avc_chroma_hz_4x8_lasx(src, dst, stride, coeff0, coeff1);
    } else if (4 == height) {
        avc_chroma_hz_4x4_lasx(src, dst, stride, coeff0, coeff1);
    } else if (2 == height) {
        avc_chroma_hz_4x2_lasx(src, dst, stride, coeff0, coeff1);
    }
}

static void avc_chroma_hz_8w_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                  uint32_t coeff0, uint32_t coeff1,
                                  int32_t height)
{
    if (4 == height) {
        avc_chroma_hz_8x4_lasx(src, dst, stride, coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_hz_8x8_lasx(src, dst, stride, coeff0, coeff1);
    } else {
        avc_chroma_hz_nonmult_lasx(src, dst, stride, coeff0, coeff1, height);
    }
}

static void avc_chroma_vt_4x2_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coeff0, uint32_t coeff1)
{
    __m256i src0, src1, src2;
    __m256i tmp0, tmp1;
    __m256i res;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec  = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    src0 = __lasx_xvld(src, 0);
    DUP2_ARG2(__lasx_xvldx, src, stride, src, stride << 1, src1, src2);
    DUP2_ARG2(__lasx_xvilvl_b, src1, src0, src2, src1, tmp0, tmp1);
    tmp0 = __lasx_xvilvl_d(tmp1, tmp0);
    res  = __lasx_xvdp2_h_bu(tmp0, coeff_vec);
    res  = __lasx_xvslli_h(res, 3);
    res  = __lasx_xvssrarni_bu_h(res, res, 6);
    __lasx_xvstelm_w(res, dst, 0, 0);
    __lasx_xvstelm_w(res, dst + stride, 0, 1);
}

static void avc_chroma_vt_4x4_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2 = stride << 1;
    ptrdiff_t stride_3 = stride_2 + stride;
    ptrdiff_t stride_4 = stride_2 << 1;
    __m256i src0, src1, src2, src3, src4;
    __m256i tmp0, tmp1, tmp2, tmp3;
    __m256i res;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec  = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    src0 = __lasx_xvld(src, 0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src1, src2, src3, src4);
    DUP4_ARG2(__lasx_xvilvl_b, src1, src0, src2, src1, src3, src2, src4, src3,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);
    tmp0 = __lasx_xvpermi_q(tmp0, tmp2, 0x02);
    res = __lasx_xvdp2_h_bu(tmp0, coeff_vec);
    res = __lasx_xvslli_h(res, 3);
    res = __lasx_xvssrarni_bu_h(res, res, 6);
    __lasx_xvstelm_w(res, dst, 0, 0);
    __lasx_xvstelm_w(res, dst + stride, 0, 1);
    __lasx_xvstelm_w(res, dst + stride_2, 0, 4);
    __lasx_xvstelm_w(res, dst + stride_3, 0, 5);
}

static void avc_chroma_vt_4x8_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                   uint32_t coeff0, uint32_t coeff1)
{
    ptrdiff_t stride_2 = stride << 1;
    ptrdiff_t stride_3 = stride_2 + stride;
    ptrdiff_t stride_4 = stride_2 << 1;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m256i res0, res1;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec  = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    src0 = __lasx_xvld(src, 0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src1, src2, src3, src4);
    src += stride_4;
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2, src, stride_3,
              src, stride_4, src5, src6, src7, src8);
    DUP4_ARG2(__lasx_xvilvl_b, src1, src0, src2, src1, src3, src2, src4, src3,
              tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lasx_xvilvl_b, src5, src4, src6, src5, src7, src6, src8, src7,
              tmp4, tmp5, tmp6, tmp7);
    DUP4_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6,
              tmp0, tmp2, tmp4, tmp6);
    tmp0 = __lasx_xvpermi_q(tmp0, tmp2, 0x02);
    tmp4 = __lasx_xvpermi_q(tmp4, tmp6, 0x02);
    DUP2_ARG2(__lasx_xvdp2_h_bu, tmp0, coeff_vec, tmp4, coeff_vec, res0, res1);
    res0 = __lasx_xvssrarni_bu_h(res1, res0, 6);
    __lasx_xvstelm_w(res0, dst, 0, 0);
    __lasx_xvstelm_w(res0, dst + stride, 0, 1);
    __lasx_xvstelm_w(res0, dst + stride_2, 0, 4);
    __lasx_xvstelm_w(res0, dst + stride_3, 0, 5);
    dst += stride_4;
    __lasx_xvstelm_w(res0, dst, 0, 2);
    __lasx_xvstelm_w(res0, dst + stride, 0, 3);
    __lasx_xvstelm_w(res0, dst + stride_2, 0, 6);
    __lasx_xvstelm_w(res0, dst + stride_3, 0, 7);
}

static void avc_chroma_vt_4w_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                  uint32_t coeff0, uint32_t coeff1,
                                  int32_t height)
{
    if (8 == height) {
        avc_chroma_vt_4x8_lasx(src, dst, stride, coeff0, coeff1);
    } else if (4 == height) {
        avc_chroma_vt_4x4_lasx(src, dst, stride, coeff0, coeff1);
    } else if (2 == height) {
        avc_chroma_vt_4x2_lasx(src, dst, stride, coeff0, coeff1);
    }
}

static void avc_chroma_vt_8w_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                  uint32_t coeff0, uint32_t coeff1,
                                  int32_t height)
{
    if (4 == height) {
        avc_chroma_vt_8x4_lasx(src, dst, stride, coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_vt_8x8_lasx(src, dst, stride, coeff0, coeff1);
    }
}

static void copy_width4_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                             int32_t height)
{
    uint32_t tp0, tp1, tp2, tp3, tp4, tp5, tp6, tp7;

    if (8 == height) {
        ptrdiff_t stride_2, stride_3, stride_4;

        __asm__ volatile (
        "slli.d   %[stride_2],     %[stride],     1             \n\t"
        "add.d    %[stride_3],     %[stride_2],   %[stride]     \n\t"
        "slli.d   %[stride_4],     %[stride_2],   1             \n\t"
        "ld.wu    %[tp0],          %[src],        0             \n\t"
        "ldx.wu   %[tp1],          %[src],        %[stride]     \n\t"
        "ldx.wu   %[tp2],          %[src],        %[stride_2]   \n\t"
        "ldx.wu   %[tp3],          %[src],        %[stride_3]   \n\t"
        "add.d    %[src],          %[src],        %[stride_4]   \n\t"
        "ld.wu    %[tp4],          %[src],        0             \n\t"
        "ldx.wu   %[tp5],          %[src],        %[stride]     \n\t"
        "ldx.wu   %[tp6],          %[src],        %[stride_2]   \n\t"
        "ldx.wu   %[tp7],          %[src],        %[stride_3]   \n\t"
        "st.w     %[tp0],          %[dst],        0             \n\t"
        "stx.w    %[tp1],          %[dst],        %[stride]     \n\t"
        "stx.w    %[tp2],          %[dst],        %[stride_2]   \n\t"
        "stx.w    %[tp3],          %[dst],        %[stride_3]   \n\t"
        "add.d    %[dst],          %[dst],        %[stride_4]   \n\t"
        "st.w     %[tp4],          %[dst],        0             \n\t"
        "stx.w    %[tp5],          %[dst],        %[stride]     \n\t"
        "stx.w    %[tp6],          %[dst],        %[stride_2]   \n\t"
        "stx.w    %[tp7],          %[dst],        %[stride_3]   \n\t"
        : [stride_2]"+&r"(stride_2), [stride_3]"+&r"(stride_3), [stride_4]"+&r"(stride_4),
          [src]"+&r"(src), [dst]"+&r"(dst), [tp0]"+&r"(tp0), [tp1]"+&r"(tp1),
          [tp2]"+&r"(tp2), [tp3]"+&r"(tp3), [tp4]"+&r"(tp4), [tp5]"+&r"(tp5),
          [tp6]"+&r"(tp6), [tp7]"+&r"(tp7)
        : [stride]"r"(stride)
        : "memory"
        );
    } else if (4 == height) {
        ptrdiff_t stride_2, stride_3;

        __asm__ volatile (
        "slli.d   %[stride_2],     %[stride],     1             \n\t"
        "add.d    %[stride_3],     %[stride_2],   %[stride]     \n\t"
        "ld.wu    %[tp0],          %[src],        0             \n\t"
        "ldx.wu   %[tp1],          %[src],        %[stride]     \n\t"
        "ldx.wu   %[tp2],          %[src],        %[stride_2]   \n\t"
        "ldx.wu   %[tp3],          %[src],        %[stride_3]   \n\t"
        "st.w     %[tp0],          %[dst],        0             \n\t"
        "stx.w    %[tp1],          %[dst],        %[stride]     \n\t"
        "stx.w    %[tp2],          %[dst],        %[stride_2]   \n\t"
        "stx.w    %[tp3],          %[dst],        %[stride_3]   \n\t"
        : [stride_2]"+&r"(stride_2), [stride_3]"+&r"(stride_3),
          [src]"+&r"(src), [dst]"+&r"(dst), [tp0]"+&r"(tp0), [tp1]"+&r"(tp1),
          [tp2]"+&r"(tp2), [tp3]"+&r"(tp3)
        : [stride]"r"(stride)
        : "memory"
        );
    } else if (2 == height) {
        __asm__ volatile (
        "ld.wu    %[tp0],          %[src],        0             \n\t"
        "ldx.wu   %[tp1],          %[src],        %[stride]     \n\t"
        "st.w     %[tp0],          %[dst],        0             \n\t"
        "stx.w    %[tp1],          %[dst],        %[stride]     \n\t"
        : [tp0]"+&r"(tp0), [tp1]"+&r"(tp1)
        : [src]"r"(src), [dst]"r"(dst), [stride]"r"(stride)
        : "memory"
        );
    }
}

static void copy_width8_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                             int32_t height)
{
    if (8 == height) {
        copy_width8x8_lasx(src, dst, stride);
    } else if (4 == height) {
        copy_width8x4_lasx(src, dst, stride);
    }
}

void ff_put_h264_chroma_mc4_lasx(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int height, int x, int y)
{
    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if(x && y) {
        avc_chroma_hv_4w_lasx(src, dst, stride, x, (8 - x), y, (8 - y), height);
    } else if (x) {
        avc_chroma_hz_4w_lasx(src, dst, stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_4w_lasx(src, dst, stride, y, (8 - y), height);
    } else {
        copy_width4_lasx(src, dst, stride, height);
    }
}

void ff_put_h264_chroma_mc8_lasx(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int height, int x, int y)
{
    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (!(x || y)) {
        copy_width8_lasx(src, dst, stride, height);
    } else if (x && y) {
        avc_chroma_hv_8w_lasx(src, dst, stride, x, (8 - x), y, (8 - y), height);
    } else if (x) {
        avc_chroma_hz_8w_lasx(src, dst, stride, x, (8 - x), height);
    } else {
        avc_chroma_vt_8w_lasx(src, dst, stride, y, (8 - y), height);
    }
}

static av_always_inline void avc_chroma_hv_and_aver_dst_8x4_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coef_hor0,
                             uint32_t coef_hor1, uint32_t coef_ver0,
                             uint32_t coef_ver1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tp0, tp1, tp2, tp3;
    __m256i src0, src1, src2, src3, src4, out;
    __m256i res_hz0, res_hz1, res_hz2, res_vt0, res_vt1;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_hz_vec = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 0, src, 0, mask, src0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src, stride_3x, src, stride_4x,
              src1, src2, src3, src4);
    DUP2_ARG3(__lasx_xvpermi_q, src2, src1, 0x20, src4, src3, 0x20, src1, src3);
    src0 = __lasx_xvshuf_b(src0, src0, mask);
    DUP2_ARG3(__lasx_xvshuf_b, src1, src1, mask, src3, src3, mask, src1, src3);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_hz_vec, src1, coeff_hz_vec, res_hz0, res_hz1);
    res_hz2 = __lasx_xvdp2_h_bu(src3, coeff_hz_vec);
    res_vt0 = __lasx_xvmul_h(res_hz1, coeff_vt_vec0);
    res_vt1 = __lasx_xvmul_h(res_hz2, coeff_vt_vec0);
    res_hz0 = __lasx_xvpermi_q(res_hz1, res_hz0, 0x20);
    res_hz1 = __lasx_xvpermi_q(res_hz1, res_hz2, 0x3);
    res_vt0 = __lasx_xvmadd_h(res_vt0, res_hz0, coeff_vt_vec1);
    res_vt1 = __lasx_xvmadd_h(res_vt1, res_hz1, coeff_vt_vec1);
    out = __lasx_xvssrarni_bu_h(res_vt1, res_vt0, 6);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    tp0 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    out = __lasx_xvavgr_bu(out, tp0);
    __lasx_xvstelm_d(out, dst, 0, 0);
    __lasx_xvstelm_d(out, dst + stride, 0, 2);
    __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_hv_and_aver_dst_8x8_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coef_hor0,
                             uint32_t coef_hor1, uint32_t coef_ver0,
                             uint32_t coef_ver1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tp0, tp1, tp2, tp3, dst0, dst1;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i out0, out1;
    __m256i res_hz0, res_hz1, res_hz2, res_hz3, res_hz4;
    __m256i res_vt0, res_vt1, res_vt2, res_vt3;
    __m256i mask;
    __m256i coeff_hz_vec0 = __lasx_xvreplgr2vr_b(coef_hor0);
    __m256i coeff_hz_vec1 = __lasx_xvreplgr2vr_b(coef_hor1);
    __m256i coeff_vt_vec0 = __lasx_xvreplgr2vr_h(coef_ver0);
    __m256i coeff_vt_vec1 = __lasx_xvreplgr2vr_h(coef_ver1);
    __m256i coeff_hz_vec = __lasx_xvilvl_b(coeff_hz_vec0, coeff_hz_vec1);

    DUP2_ARG2(__lasx_xvld, chroma_mask_arr, 0, src, 0, mask, src0);
    src += stride;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src1, src2, src3, src4);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src5, src6, src7, src8);
    DUP4_ARG3(__lasx_xvpermi_q, src2, src1, 0x20, src4, src3, 0x20, src6, src5, 0x20,
              src8, src7, 0x20, src1, src3, src5, src7);
    src0 = __lasx_xvshuf_b(src0, src0, mask);
    DUP4_ARG3(__lasx_xvshuf_b, src1, src1, mask, src3, src3, mask, src5, src5, mask, src7,
              src7, mask, src1, src3, src5, src7);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_hz_vec, src1, coeff_hz_vec, src3,
              coeff_hz_vec, src5, coeff_hz_vec, res_hz0, res_hz1, res_hz2, res_hz3);
    res_hz4 = __lasx_xvdp2_h_bu(src7, coeff_hz_vec);
    res_vt0 = __lasx_xvmul_h(res_hz1, coeff_vt_vec0);
    res_vt1 = __lasx_xvmul_h(res_hz2, coeff_vt_vec0);
    res_vt2 = __lasx_xvmul_h(res_hz3, coeff_vt_vec0);
    res_vt3 = __lasx_xvmul_h(res_hz4, coeff_vt_vec0);
    res_hz0 = __lasx_xvpermi_q(res_hz1, res_hz0, 0x20);
    res_hz1 = __lasx_xvpermi_q(res_hz1, res_hz2, 0x3);
    res_hz2 = __lasx_xvpermi_q(res_hz2, res_hz3, 0x3);
    res_hz3 = __lasx_xvpermi_q(res_hz3, res_hz4, 0x3);
    res_vt0 = __lasx_xvmadd_h(res_vt0, res_hz0, coeff_vt_vec1);
    res_vt1 = __lasx_xvmadd_h(res_vt1, res_hz1, coeff_vt_vec1);
    res_vt2 = __lasx_xvmadd_h(res_vt2, res_hz2, coeff_vt_vec1);
    res_vt3 = __lasx_xvmadd_h(res_vt3, res_hz3, coeff_vt_vec1);
    DUP2_ARG3(__lasx_xvssrarni_bu_h, res_vt1, res_vt0, 6, res_vt3, res_vt2, 6,
              out0, out1);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    dst0 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    dst += stride_4x;
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    dst -= stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    dst1 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    out0 = __lasx_xvavgr_bu(out0, dst0);
    out1 = __lasx_xvavgr_bu(out1, dst1);
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + stride, 0, 2);
    __lasx_xvstelm_d(out0, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + stride, 0, 2);
    __lasx_xvstelm_d(out1, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out1, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_hz_and_aver_dst_8x4_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coeff0,
                             uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    __m256i tp0, tp1, tp2, tp3;
    __m256i src0, src1, src2, src3, out;
    __m256i res0, res1;
    __m256i mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    mask = __lasx_xvld(chroma_mask_arr, 0);
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src0, src1, src2, src3);
    DUP2_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src0, src2);
    DUP2_ARG3(__lasx_xvshuf_b, src0, src0, mask, src2, src2, mask, src0, src2);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, res0, res1);
    out = __lasx_xvssrarni_bu_h(res1, res0, 6);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    tp0 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    out = __lasx_xvavgr_bu(out, tp0);
    __lasx_xvstelm_d(out, dst, 0, 0);
    __lasx_xvstelm_d(out, dst + stride, 0, 2);
    __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_hz_and_aver_dst_8x8_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coeff0,
                             uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tp0, tp1, tp2, tp3, dst0, dst1;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i out0, out1;
    __m256i res0, res1, res2, res3;
    __m256i mask;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    mask = __lasx_xvld(chroma_mask_arr, 0);
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src0, src1, src2, src3);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src4, src5, src6, src7);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4, 0x20,
              src7, src6, 0x20, src0, src2, src4, src6);
    DUP4_ARG3(__lasx_xvshuf_b, src0, src0, mask, src2, src2, mask, src4, src4,
              mask, src6, src6, mask, src0, src2, src4, src6);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, src4, coeff_vec, src6,
              coeff_vec, res0, res1, res2, res3);
    DUP2_ARG3(__lasx_xvssrarni_bu_h, res1, res0, 6, res3, res2, 6, out0, out1);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    dst0 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    dst += stride_4x;
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    dst -= stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    dst1 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    out0 = __lasx_xvavgr_bu(out0, dst0);
    out1 = __lasx_xvavgr_bu(out1, dst1);
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + stride, 0, 2);
    __lasx_xvstelm_d(out0, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + stride, 0, 2);
    __lasx_xvstelm_d(out1, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out1, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_vt_and_aver_dst_8x4_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coeff0,
                             uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tp0, tp1, tp2, tp3;
    __m256i src0, src1, src2, src3, src4, out;
    __m256i res0, res1;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    src0 = __lasx_xvld(src, 0);
    DUP4_ARG2(__lasx_xvldx, src, stride, src, stride_2x, src, stride_3x, src, stride_4x,
              src1, src2, src3, src4);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2, 0x20,
              src4, src3, 0x20, src0, src1, src2, src3);
    DUP2_ARG2(__lasx_xvilvl_b, src1, src0, src3, src2, src0, src2);
    DUP2_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, res0, res1);
    out = __lasx_xvssrarni_bu_h(res1, res0, 6);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    tp0 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    out = __lasx_xvavgr_bu(out, tp0);
    __lasx_xvstelm_d(out, dst, 0, 0);
    __lasx_xvstelm_d(out, dst + stride, 0, 2);
    __lasx_xvstelm_d(out, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out, dst + stride_3x, 0, 3);
}

static av_always_inline void avc_chroma_vt_and_aver_dst_8x8_lasx(const uint8_t *src,
                             uint8_t *dst, ptrdiff_t stride, uint32_t coeff0,
                             uint32_t coeff1)
{
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;
    __m256i tp0, tp1, tp2, tp3, dst0, dst1;
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i out0, out1;
    __m256i res0, res1, res2, res3;
    __m256i coeff_vec0 = __lasx_xvreplgr2vr_b(coeff0);
    __m256i coeff_vec1 = __lasx_xvreplgr2vr_b(coeff1);
    __m256i coeff_vec = __lasx_xvilvl_b(coeff_vec0, coeff_vec1);

    coeff_vec = __lasx_xvslli_b(coeff_vec, 3);
    src0 = __lasx_xvld(src, 0);
    src += stride;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src1, src2, src3, src4);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x, src, stride_3x,
              src5, src6, src7, src8);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2, 0x20,
              src4, src3, 0x20, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src5, src4, 0x20, src6, src5, 0x20, src7, src6, 0x20,
              src8, src7, 0x20, src4, src5, src6, src7);
    DUP4_ARG2(__lasx_xvilvl_b, src1, src0, src3, src2, src5, src4, src7, src6,
              src0, src2, src4, src6);
    DUP4_ARG2(__lasx_xvdp2_h_bu, src0, coeff_vec, src2, coeff_vec, src4, coeff_vec, src6,
              coeff_vec, res0, res1, res2, res3);
    DUP2_ARG3(__lasx_xvssrarni_bu_h, res1, res0, 6, res3, res2, 6, out0, out1);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    dst0 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    dst += stride_4x;
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x, dst, stride_3x,
              tp0, tp1, tp2, tp3);
    dst -= stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tp2, tp0, tp3, tp1, tp0, tp2);
    dst1 = __lasx_xvpermi_q(tp2, tp0, 0x20);
    out0 = __lasx_xvavgr_bu(out0, dst0);
    out1 = __lasx_xvavgr_bu(out1, dst1);
    __lasx_xvstelm_d(out0, dst, 0, 0);
    __lasx_xvstelm_d(out0, dst + stride, 0, 2);
    __lasx_xvstelm_d(out0, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(out1, dst, 0, 0);
    __lasx_xvstelm_d(out1, dst + stride, 0, 2);
    __lasx_xvstelm_d(out1, dst + stride_2x, 0, 1);
    __lasx_xvstelm_d(out1, dst + stride_3x, 0, 3);
}

static av_always_inline void avg_width8x8_lasx(const uint8_t *src, uint8_t *dst,
                                               ptrdiff_t stride)
{
    __m256i src0, src1, src2, src3;
    __m256i dst0, dst1, dst2, dst3;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    ptrdiff_t stride_4x = stride << 2;

    src0 = __lasx_xvldrepl_d(src, 0);
    src1 = __lasx_xvldrepl_d(src + stride, 0);
    src2 = __lasx_xvldrepl_d(src + stride_2x, 0);
    src3 = __lasx_xvldrepl_d(src + stride_3x, 0);
    dst0 = __lasx_xvldrepl_d(dst, 0);
    dst1 = __lasx_xvldrepl_d(dst + stride, 0);
    dst2 = __lasx_xvldrepl_d(dst + stride_2x, 0);
    dst3 = __lasx_xvldrepl_d(dst + stride_3x, 0);
    src0 = __lasx_xvpackev_d(src1,src0);
    src2 = __lasx_xvpackev_d(src3,src2);
    src0 = __lasx_xvpermi_q(src0, src2, 0x02);
    dst0 = __lasx_xvpackev_d(dst1,dst0);
    dst2 = __lasx_xvpackev_d(dst3,dst2);
    dst0 = __lasx_xvpermi_q(dst0, dst2, 0x02);
    dst0 = __lasx_xvavgr_bu(src0, dst0);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst0, dst + stride_3x, 0, 3);

    src += stride_4x;
    dst += stride_4x;
    src0 = __lasx_xvldrepl_d(src, 0);
    src1 = __lasx_xvldrepl_d(src + stride, 0);
    src2 = __lasx_xvldrepl_d(src + stride_2x, 0);
    src3 = __lasx_xvldrepl_d(src + stride_3x, 0);
    dst0 = __lasx_xvldrepl_d(dst, 0);
    dst1 = __lasx_xvldrepl_d(dst + stride, 0);
    dst2 = __lasx_xvldrepl_d(dst + stride_2x, 0);
    dst3 = __lasx_xvldrepl_d(dst + stride_3x, 0);
    src0 = __lasx_xvpackev_d(src1,src0);
    src2 = __lasx_xvpackev_d(src3,src2);
    src0 = __lasx_xvpermi_q(src0, src2, 0x02);
    dst0 = __lasx_xvpackev_d(dst1,dst0);
    dst2 = __lasx_xvpackev_d(dst3,dst2);
    dst0 = __lasx_xvpermi_q(dst0, dst2, 0x02);
    dst0 = __lasx_xvavgr_bu(src0, dst0);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst0, dst + stride_3x, 0, 3);
}

static av_always_inline void avg_width8x4_lasx(const uint8_t *src, uint8_t *dst,
                                               ptrdiff_t stride)
{
    __m256i src0, src1, src2, src3;
    __m256i dst0, dst1, dst2, dst3;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;

    src0 = __lasx_xvldrepl_d(src, 0);
    src1 = __lasx_xvldrepl_d(src + stride, 0);
    src2 = __lasx_xvldrepl_d(src + stride_2x, 0);
    src3 = __lasx_xvldrepl_d(src + stride_3x, 0);
    dst0 = __lasx_xvldrepl_d(dst, 0);
    dst1 = __lasx_xvldrepl_d(dst + stride, 0);
    dst2 = __lasx_xvldrepl_d(dst + stride_2x, 0);
    dst3 = __lasx_xvldrepl_d(dst + stride_3x, 0);
    src0 = __lasx_xvpackev_d(src1,src0);
    src2 = __lasx_xvpackev_d(src3,src2);
    src0 = __lasx_xvpermi_q(src0, src2, 0x02);
    dst0 = __lasx_xvpackev_d(dst1,dst0);
    dst2 = __lasx_xvpackev_d(dst3,dst2);
    dst0 = __lasx_xvpermi_q(dst0, dst2, 0x02);
    dst0 = __lasx_xvavgr_bu(src0, dst0);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst0, dst + stride_3x, 0, 3);
}

static void avc_chroma_hv_and_aver_dst_8w_lasx(const uint8_t *src, uint8_t *dst,
                                               ptrdiff_t stride,
                                               uint32_t coef_hor0,
                                               uint32_t coef_hor1,
                                               uint32_t coef_ver0,
                                               uint32_t coef_ver1,
                                               int32_t height)
{
    if (4 == height) {
        avc_chroma_hv_and_aver_dst_8x4_lasx(src, dst, stride, coef_hor0,
                                            coef_hor1, coef_ver0, coef_ver1);
    } else if (8 == height) {
        avc_chroma_hv_and_aver_dst_8x8_lasx(src, dst, stride, coef_hor0,
                                            coef_hor1, coef_ver0, coef_ver1);
    }
}

static void avc_chroma_hz_and_aver_dst_8w_lasx(const uint8_t *src, uint8_t *dst,
                                               ptrdiff_t stride, uint32_t coeff0,
                                               uint32_t coeff1, int32_t height)
{
    if (4 == height) {
        avc_chroma_hz_and_aver_dst_8x4_lasx(src, dst, stride, coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_hz_and_aver_dst_8x8_lasx(src, dst, stride, coeff0, coeff1);
    }
}

static void avc_chroma_vt_and_aver_dst_8w_lasx(const uint8_t *src, uint8_t *dst,
                                               ptrdiff_t stride, uint32_t coeff0,
                                               uint32_t coeff1, int32_t height)
{
    if (4 == height) {
        avc_chroma_vt_and_aver_dst_8x4_lasx(src, dst, stride, coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_vt_and_aver_dst_8x8_lasx(src, dst, stride, coeff0, coeff1);
    }
}

static void avg_width8_lasx(const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                            int32_t height)
{
    if (8 == height) {
        avg_width8x8_lasx(src, dst, stride);
    } else if (4 == height) {
        avg_width8x4_lasx(src, dst, stride);
    }
}

void ff_avg_h264_chroma_mc8_lasx(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int height, int x, int y)
{
    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (!(x || y)) {
        avg_width8_lasx(src, dst, stride, height);
    } else if (x && y) {
        avc_chroma_hv_and_aver_dst_8w_lasx(src, dst, stride, x, (8 - x), y,
                                           (8 - y), height);
    } else if (x) {
        avc_chroma_hz_and_aver_dst_8w_lasx(src, dst, stride, x, (8 - x), height);
    } else {
        avc_chroma_vt_and_aver_dst_8w_lasx(src, dst, stride, y, (8 - y), height);
    }
}
