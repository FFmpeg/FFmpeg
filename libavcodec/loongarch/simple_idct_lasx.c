/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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
#include "idctdsp_loongarch.h"

#define LASX_TRANSPOSE4x16(in_0, in_1, in_2, in_3, out_0, out_1, out_2, out_3) \
{                                                                              \
    __m256i temp_0, temp_1, temp_2, temp_3;                                    \
    __m256i temp_4, temp_5, temp_6, temp_7;                                    \
    DUP4_ARG3(__lasx_xvpermi_q, in_2, in_0, 0x20, in_2, in_0, 0x31, in_3, in_1,\
              0x20, in_3, in_1, 0x31, temp_0, temp_1, temp_2, temp_3);         \
    DUP2_ARG2(__lasx_xvilvl_h, temp_1, temp_0, temp_3, temp_2, temp_4, temp_6);\
    DUP2_ARG2(__lasx_xvilvh_h, temp_1, temp_0, temp_3, temp_2, temp_5, temp_7);\
    DUP2_ARG2(__lasx_xvilvl_w, temp_6, temp_4, temp_7, temp_5, out_0, out_2);  \
    DUP2_ARG2(__lasx_xvilvh_w, temp_6, temp_4, temp_7, temp_5, out_1, out_3);  \
}

#define LASX_IDCTROWCONDDC                                                     \
    const_val  = 16383 * ((1 << 19) / 16383);                                  \
    const_val1 = __lasx_xvreplgr2vr_w(const_val);                              \
    DUP4_ARG2(__lasx_xvld, block, 0, block, 32, block, 64, block, 96,          \
              in0, in1, in2, in3);                                             \
    LASX_TRANSPOSE4x16(in0, in1, in2, in3, in0, in1, in2, in3);                \
    a0 = __lasx_xvpermi_d(in0, 0xD8);                                          \
    a0 = __lasx_vext2xv_w_h(a0);                                               \
    temp  = __lasx_xvslli_w(a0, 3);                                            \
    a1 = __lasx_xvpermi_d(in0, 0x8D);                                          \
    a1 = __lasx_vext2xv_w_h(a1);                                               \
    a2 = __lasx_xvpermi_d(in1, 0xD8);                                          \
    a2 = __lasx_vext2xv_w_h(a2);                                               \
    a3 = __lasx_xvpermi_d(in1, 0x8D);                                          \
    a3 = __lasx_vext2xv_w_h(a3);                                               \
    b0 = __lasx_xvpermi_d(in2, 0xD8);                                          \
    b0 = __lasx_vext2xv_w_h(b0);                                               \
    b1 = __lasx_xvpermi_d(in2, 0x8D);                                          \
    b1 = __lasx_vext2xv_w_h(b1);                                               \
    b2 = __lasx_xvpermi_d(in3, 0xD8);                                          \
    b2 = __lasx_vext2xv_w_h(b2);                                               \
    b3 = __lasx_xvpermi_d(in3, 0x8D);                                          \
    b3 = __lasx_vext2xv_w_h(b3);                                               \
    select_vec = a0 | a1 | a2 | a3 | b0 | b1 | b2 | b3;                        \
    select_vec = __lasx_xvslti_wu(select_vec, 1);                              \
                                                                               \
    DUP4_ARG2(__lasx_xvrepl128vei_h, w1, 2, w1, 3, w1, 4, w1, 5,               \
              w2, w3, w4, w5);                                                 \
    DUP2_ARG2(__lasx_xvrepl128vei_h, w1, 6, w1, 7, w6, w7);                    \
    w1 = __lasx_xvrepl128vei_h(w1, 1);                                         \
                                                                               \
    /* part of FUNC6(idctRowCondDC) */                                         \
    temp0 = __lasx_xvmaddwl_w_h(const_val0, in0, w4);                          \
    DUP2_ARG2(__lasx_xvmulwl_w_h, in1, w2, in1, w6, temp1, temp2);             \
    a0    = __lasx_xvadd_w(temp0, temp1);                                      \
    a1    = __lasx_xvadd_w(temp0, temp2);                                      \
    a2    = __lasx_xvsub_w(temp0, temp2);                                      \
    a3    = __lasx_xvsub_w(temp0, temp1);                                      \
                                                                               \
    DUP2_ARG2(__lasx_xvilvh_h, in1, in0, w3, w1, temp0, temp1);                \
    b0 = __lasx_xvdp2_w_h(temp0, temp1);                                       \
    temp1 = __lasx_xvneg_h(w7);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w3);                                        \
    b1 = __lasx_xvdp2_w_h(temp0, temp2);                                       \
    temp1 = __lasx_xvneg_h(w1);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w5);                                        \
    b2 = __lasx_xvdp2_w_h(temp0, temp2);                                       \
    temp1 = __lasx_xvneg_h(w5);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w7);                                        \
    b3 = __lasx_xvdp2_w_h(temp0, temp2);                                       \
                                                                               \
    /* if (AV_RAN64A(row + 4)) */                                              \
    DUP2_ARG2(__lasx_xvilvl_h, in3, in2, w6, w4, temp0, temp1);                \
    a0 = __lasx_xvdp2add_w_h(a0, temp0, temp1);                                \
    temp1 = __lasx_xvilvl_h(w2, w4);                                           \
    a1 = __lasx_xvdp2sub_w_h(a1, temp0, temp1);                                \
    temp1 = __lasx_xvneg_h(w4);                                                \
    temp2 = __lasx_xvilvl_h(w2, temp1);                                        \
    a2 = __lasx_xvdp2add_w_h(a2, temp0, temp2);                                \
    temp1 = __lasx_xvneg_h(w6);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w4);                                        \
    a3 = __lasx_xvdp2add_w_h(a3, temp0, temp2);                                \
                                                                               \
    DUP2_ARG2(__lasx_xvilvh_h, in3, in2, w7, w5, temp0, temp1);                \
    b0 = __lasx_xvdp2add_w_h(b0, temp0, temp1);                                \
    DUP2_ARG2(__lasx_xvilvl_h, w5, w1, w3, w7, temp1, temp2);                  \
    b1 = __lasx_xvdp2sub_w_h(b1, temp0, temp1);                                \
    b2 = __lasx_xvdp2add_w_h(b2, temp0, temp2);                                \
    temp1 = __lasx_xvneg_h(w1);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w3);                                        \
    b3 = __lasx_xvdp2add_w_h(b3, temp0, temp2);                                \
                                                                               \
    DUP4_ARG2(__lasx_xvadd_w, a0, b0, a1, b1, a2, b2, a3, b3,                  \
              temp0, temp1, temp2, temp3);                                     \
    DUP4_ARG2(__lasx_xvsub_w, a0, b0, a1, b1, a2, b2, a3, b3,                  \
              a0, a1, a2, a3);                                                 \
    DUP4_ARG2(__lasx_xvsrai_w, temp0, 11, temp1, 11, temp2, 11, temp3, 11,     \
              temp0, temp1, temp2, temp3);                                     \
    DUP4_ARG2(__lasx_xvsrai_w, a0, 11, a1, 11, a2, 11, a3, 11, a0, a1, a2, a3);\
    DUP4_ARG3(__lasx_xvbitsel_v, temp0, temp, select_vec, temp1, temp,         \
              select_vec, temp2, temp, select_vec, temp3, temp, select_vec,    \
              in0, in1, in2, in3);                                             \
    DUP4_ARG3(__lasx_xvbitsel_v, a0, temp, select_vec, a1, temp,               \
              select_vec, a2, temp, select_vec, a3, temp, select_vec,          \
              a0, a1, a2, a3);                                                 \
    DUP4_ARG2(__lasx_xvpickev_h, in1, in0, in3, in2, a2, a3, a0, a1,           \
              in0, in1, in2, in3);                                             \
    DUP4_ARG2(__lasx_xvpermi_d, in0, 0xD8, in1, 0xD8, in2, 0xD8, in3, 0xD8,    \
              in0, in1, in2, in3);                                             \

#define LASX_IDCTCOLS                                                          \
    /* part of FUNC6(idctSparaseCol) */                                        \
    LASX_TRANSPOSE4x16(in0, in1, in2, in3, in0, in1, in2, in3);                \
    temp0 = __lasx_xvmaddwl_w_h(const_val1, in0, w4);                          \
    DUP2_ARG2(__lasx_xvmulwl_w_h, in1, w2, in1, w6, temp1, temp2);             \
    a0    = __lasx_xvadd_w(temp0, temp1);                                      \
    a1    = __lasx_xvadd_w(temp0, temp2);                                      \
    a2    = __lasx_xvsub_w(temp0, temp2);                                      \
    a3    = __lasx_xvsub_w(temp0, temp1);                                      \
                                                                               \
    DUP2_ARG2(__lasx_xvilvh_h, in1, in0, w3, w1, temp0, temp1);                \
    b0 = __lasx_xvdp2_w_h(temp0, temp1);                                       \
    temp1 = __lasx_xvneg_h(w7);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w3);                                        \
    b1 = __lasx_xvdp2_w_h(temp0, temp2);                                       \
    temp1 = __lasx_xvneg_h(w1);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w5);                                        \
    b2 = __lasx_xvdp2_w_h(temp0, temp2);                                       \
    temp1 = __lasx_xvneg_h(w5);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w7);                                        \
    b3 = __lasx_xvdp2_w_h(temp0, temp2);                                       \
                                                                               \
    /* if (AV_RAN64A(row + 4)) */                                              \
    DUP2_ARG2(__lasx_xvilvl_h, in3, in2, w6, w4, temp0, temp1);                \
    a0 = __lasx_xvdp2add_w_h(a0, temp0, temp1);                                \
    temp1 = __lasx_xvilvl_h(w2, w4);                                           \
    a1 = __lasx_xvdp2sub_w_h(a1, temp0, temp1);                                \
    temp1 = __lasx_xvneg_h(w4);                                                \
    temp2 = __lasx_xvilvl_h(w2, temp1);                                        \
    a2 = __lasx_xvdp2add_w_h(a2, temp0, temp2);                                \
    temp1 = __lasx_xvneg_h(w6);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w4);                                        \
    a3 = __lasx_xvdp2add_w_h(a3, temp0, temp2);                                \
                                                                               \
    DUP2_ARG2(__lasx_xvilvh_h, in3, in2, w7, w5, temp0, temp1);                \
    b0 = __lasx_xvdp2add_w_h(b0, temp0, temp1);                                \
    DUP2_ARG2(__lasx_xvilvl_h, w5, w1, w3, w7, temp1, temp2);                  \
    b1 = __lasx_xvdp2sub_w_h(b1, temp0, temp1);                                \
    b2 = __lasx_xvdp2add_w_h(b2, temp0, temp2);                                \
    temp1 = __lasx_xvneg_h(w1);                                                \
    temp2 = __lasx_xvilvl_h(temp1, w3);                                        \
    b3 = __lasx_xvdp2add_w_h(b3, temp0, temp2);                                \
                                                                               \
    DUP4_ARG2(__lasx_xvadd_w, a0, b0, a1, b1, a2, b2, a3, b3,                  \
              temp0, temp1, temp2, temp3);                                     \
    DUP4_ARG2(__lasx_xvsub_w, a3, b3, a2, b2, a1, b1, a0, b0,                  \
              a3, a2, a1, a0);                                                 \
    DUP4_ARG3(__lasx_xvsrani_h_w, temp1, temp0, 20, temp3, temp2, 20, a2, a3,  \
              20, a0, a1, 20, in0, in1, in2, in3);                             \

void ff_simple_idct_lasx(int16_t *block)
{
    int32_t const_val = 1 << 10;
    __m256i w1 = {0x4B42539F58C50000, 0x11A822A332493FFF,
                  0x4B42539F58C50000, 0x11A822A332493FFF};
    __m256i in0, in1, in2, in3;
    __m256i w2, w3, w4, w5, w6, w7;
    __m256i a0, a1, a2, a3;
    __m256i b0, b1, b2, b3;
    __m256i temp0, temp1, temp2, temp3;
    __m256i const_val0 = __lasx_xvreplgr2vr_w(const_val);
    __m256i const_val1, select_vec, temp;

    LASX_IDCTROWCONDDC
    LASX_IDCTCOLS
    DUP4_ARG2(__lasx_xvpermi_d, in0, 0xD8, in1, 0xD8, in2, 0xD8, in3, 0xD8,
              in0, in1, in2, in3);
    __lasx_xvst(in0, block, 0);
    __lasx_xvst(in1, block, 32);
    __lasx_xvst(in2, block, 64);
    __lasx_xvst(in3, block, 96);
}

void ff_simple_idct_put_lasx(uint8_t *dst, ptrdiff_t dst_stride,
                             int16_t *block)
{
    int32_t const_val = 1 << 10;
    ptrdiff_t dst_stride_2x = dst_stride << 1;
    ptrdiff_t dst_stride_4x = dst_stride << 2;
    ptrdiff_t dst_stride_3x = dst_stride_2x + dst_stride;
    __m256i w1 = {0x4B42539F58C50000, 0x11A822A332493FFF,
                  0x4B42539F58C50000, 0x11A822A332493FFF};
    __m256i in0, in1, in2, in3;
    __m256i w2, w3, w4, w5, w6, w7;
    __m256i a0, a1, a2, a3;
    __m256i b0, b1, b2, b3;
    __m256i temp0, temp1, temp2, temp3;
    __m256i const_val0 = __lasx_xvreplgr2vr_w(const_val);
    __m256i const_val1, select_vec, temp;

    LASX_IDCTROWCONDDC
    LASX_IDCTCOLS
    DUP4_ARG2(__lasx_xvpermi_d, in0, 0xD8, in1, 0xD8, in2, 0xD8, in3, 0xD8,
              in0, in1, in2, in3);
    DUP4_ARG1(__lasx_xvclip255_h, in0, in1, in2, in3, in0, in1, in2, in3);
    DUP2_ARG2(__lasx_xvpickev_b, in1, in0, in3, in2, in0, in1);
    __lasx_xvstelm_d(in0, dst, 0, 0);
    __lasx_xvstelm_d(in0, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(in0, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(in0, dst + dst_stride_3x, 0, 3);
    dst += dst_stride_4x;
    __lasx_xvstelm_d(in1, dst, 0, 0);
    __lasx_xvstelm_d(in1, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(in1, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(in1, dst + dst_stride_3x, 0, 3);
}

void ff_simple_idct_add_lasx(uint8_t *dst, ptrdiff_t dst_stride,
                             int16_t *block)
{
    int32_t const_val = 1 << 10;
    uint8_t *dst1 = dst;
    ptrdiff_t dst_stride_2x = dst_stride << 1;
    ptrdiff_t dst_stride_4x = dst_stride << 2;
    ptrdiff_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m256i w1 = {0x4B42539F58C50000, 0x11A822A332493FFF,
                  0x4B42539F58C50000, 0x11A822A332493FFF};
    __m256i sh = {0x0003000200010000, 0x000B000A00090008,
                  0x0007000600050004, 0x000F000E000D000C};
    __m256i in0, in1, in2, in3;
    __m256i w2, w3, w4, w5, w6, w7;
    __m256i a0, a1, a2, a3;
    __m256i b0, b1, b2, b3;
    __m256i temp0, temp1, temp2, temp3;
    __m256i const_val0 = __lasx_xvreplgr2vr_w(const_val);
    __m256i const_val1, select_vec, temp;

    LASX_IDCTROWCONDDC
    LASX_IDCTCOLS
    a0    = __lasx_xvldrepl_d(dst1, 0);
    a0    = __lasx_vext2xv_hu_bu(a0);
    dst1 += dst_stride;
    a1    = __lasx_xvldrepl_d(dst1, 0);
    a1    = __lasx_vext2xv_hu_bu(a1);
    dst1 += dst_stride;
    a2    = __lasx_xvldrepl_d(dst1, 0);
    a2    = __lasx_vext2xv_hu_bu(a2);
    dst1 += dst_stride;
    a3    = __lasx_xvldrepl_d(dst1, 0);
    a3    = __lasx_vext2xv_hu_bu(a3);
    dst1 += dst_stride;
    b0    = __lasx_xvldrepl_d(dst1, 0);
    b0    = __lasx_vext2xv_hu_bu(b0);
    dst1 += dst_stride;
    b1    = __lasx_xvldrepl_d(dst1, 0);
    b1    = __lasx_vext2xv_hu_bu(b1);
    dst1 += dst_stride;
    b2    = __lasx_xvldrepl_d(dst1, 0);
    b2    = __lasx_vext2xv_hu_bu(b2);
    dst1 += dst_stride;
    b3    = __lasx_xvldrepl_d(dst1, 0);
    b3    = __lasx_vext2xv_hu_bu(b3);
    DUP4_ARG3(__lasx_xvshuf_h, sh, a1, a0, sh, a3, a2, sh, b1, b0, sh, b3, b2,
              temp0, temp1, temp2, temp3);
    DUP4_ARG2(__lasx_xvadd_h, temp0, in0, temp1, in1, temp2, in2, temp3, in3,
              in0, in1, in2, in3);
    DUP4_ARG2(__lasx_xvpermi_d, in0, 0xD8, in1, 0xD8, in2, 0xD8, in3, 0xD8,
              in0, in1, in2, in3);
    DUP4_ARG1(__lasx_xvclip255_h, in0, in1, in2, in3, in0, in1, in2, in3);
    DUP2_ARG2(__lasx_xvpickev_b, in1, in0, in3, in2, in0, in1);
    __lasx_xvstelm_d(in0, dst, 0, 0);
    __lasx_xvstelm_d(in0, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(in0, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(in0, dst + dst_stride_3x, 0, 3);
    dst += dst_stride_4x;
    __lasx_xvstelm_d(in1, dst, 0, 0);
    __lasx_xvstelm_d(in1, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(in1, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(in1, dst + dst_stride_3x, 0, 3);
}
