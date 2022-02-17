/*
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

static const int16_t gt8x8_cnst[16] __attribute__ ((aligned (64))) = {
    64, 64, 83, 36, 89, 50, 18, 75, 64, -64, 36, -83, 75, -89, -50, -18
};

static const int16_t gt16x16_cnst[64] __attribute__ ((aligned (64))) = {
    64, 83, 64, 36, 89, 75, 50, 18, 90, 80, 57, 25, 70, 87, 9, 43,
    64, 36, -64, -83, 75, -18, -89, -50, 87, 9, -80, -70, -43, 57, -25, -90,
    64, -36, -64, 83, 50, -89, 18, 75, 80, -70, -25, 90, -87, 9, 43, 57,
    64, -83, 64, -36, 18, -50, 75, -89, 70, -87, 90, -80, 9, -43, -57, 25
};

static const int16_t gt32x32_cnst0[256] __attribute__ ((aligned (64))) = {
    90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46, 38, 31, 22, 13, 4,
    90, 82, 67, 46, 22, -4, -31, -54, -73, -85, -90, -88, -78, -61, -38, -13,
    88, 67, 31, -13, -54, -82, -90, -78, -46, -4, 38, 73, 90, 85, 61, 22,
    85, 46, -13, -67, -90, -73, -22, 38, 82, 88, 54, -4, -61, -90, -78, -31,
    82, 22, -54, -90, -61, 13, 78, 85, 31, -46, -90, -67, 4, 73, 88, 38,
    78, -4, -82, -73, 13, 85, 67, -22, -88, -61, 31, 90, 54, -38, -90, -46,
    73, -31, -90, -22, 78, 67, -38, -90, -13, 82, 61, -46, -88, -4, 85, 54,
    67, -54, -78, 38, 85, -22, -90, 4, 90, 13, -88, -31, 82, 46, -73, -61,
    61, -73, -46, 82, 31, -88, -13, 90, -4, -90, 22, 85, -38, -78, 54, 67,
    54, -85, -4, 88, -46, -61, 82, 13, -90, 38, 67, -78, -22, 90, -31, -73,
    46, -90, 38, 54, -90, 31, 61, -88, 22, 67, -85, 13, 73, -82, 4, 78,
    38, -88, 73, -4, -67, 90, -46, -31, 85, -78, 13, 61, -90, 54, 22, -82,
    31, -78, 90, -61, 4, 54, -88, 82, -38, -22, 73, -90, 67, -13, -46, 85,
    22, -61, 85, -90, 73, -38, -4, 46, -78, 90, -82, 54, -13, -31, 67, -88,
    13, -38, 61, -78, 88, -90, 85, -73, 54, -31, 4, 22, -46, 67, -82, 90,
    4, -13, 22, -31, 38, -46, 54, -61, 67, -73, 78, -82, 85, -88, 90, -90
};

static const int16_t gt32x32_cnst1[64] __attribute__ ((aligned (64))) = {
    90, 87, 80, 70, 57, 43, 25, 9, 87, 57, 9, -43, -80, -90, -70, -25,
    80, 9, -70, -87, -25, 57, 90, 43, 70, -43, -87, 9, 90, 25, -80, -57,
    57, -80, -25, 90, -9, -87, 43, 70, 43, -90, 57, 25, -87, 70, 9, -80,
    25, -70, 90, -80, 43, 9, -57, 87, 9, -25, 43, -57, 70, -80, 87, -90
};

static const int16_t gt32x32_cnst2[16] __attribute__ ((aligned (64))) = {
    89, 75, 50, 18, 75, -18, -89, -50, 50, -89, 18, 75, 18, -50, 75, -89
};

#define HEVC_IDCT4x4_COL(in_r0, in_l0, in_r1, in_l1,          \
                         sum0, sum1, sum2, sum3, shift)       \
{                                                             \
    __m128i vec0, vec1, vec2, vec3, vec4, vec5;               \
    __m128i cnst64 = __lsx_vldi(0x0840);                      \
    __m128i cnst83 = __lsx_vldi(0x0853);                      \
    __m128i cnst36 = __lsx_vldi(0x0824);                      \
                                                              \
    vec0 = __lsx_vdp2_w_h(in_r0, cnst64);                     \
    vec1 = __lsx_vdp2_w_h(in_l0, cnst83);                     \
    vec2 = __lsx_vdp2_w_h(in_r1, cnst64);                     \
    vec3 = __lsx_vdp2_w_h(in_l1, cnst36);                     \
    vec4 = __lsx_vdp2_w_h(in_l0, cnst36);                     \
    vec5 = __lsx_vdp2_w_h(in_l1, cnst83);                     \
                                                              \
    sum0 = __lsx_vadd_w(vec0, vec2);                          \
    sum1 = __lsx_vsub_w(vec0, vec2);                          \
    vec1 = __lsx_vadd_w(vec1, vec3);                          \
    vec4 = __lsx_vsub_w(vec4, vec5);                          \
    sum2 = __lsx_vsub_w(sum1, vec4);                          \
    sum3 = __lsx_vsub_w(sum0, vec1);                          \
    sum0 = __lsx_vadd_w(sum0, vec1);                          \
    sum1 = __lsx_vadd_w(sum1, vec4);                          \
                                                              \
    sum0 = __lsx_vsrari_w(sum0, shift);                       \
    sum1 = __lsx_vsrari_w(sum1, shift);                       \
    sum2 = __lsx_vsrari_w(sum2, shift);                       \
    sum3 = __lsx_vsrari_w(sum3, shift);                       \
    sum0 = __lsx_vsat_w(sum0, 15);                            \
    sum1 = __lsx_vsat_w(sum1, 15);                            \
    sum2 = __lsx_vsat_w(sum2, 15);                            \
    sum3 = __lsx_vsat_w(sum3, 15);                            \
}

#define HEVC_IDCT8x8_COL(in0, in1, in2, in3, in4, in5, in6, in7, shift)  \
{                                                                        \
    __m128i src0_r, src1_r, src2_r, src3_r;                              \
    __m128i src0_l, src1_l, src2_l, src3_l;                              \
    __m128i filter0, filter1, filter2, filter3;                          \
    __m128i temp0_r, temp1_r, temp2_r, temp3_r, temp4_r, temp5_r;        \
    __m128i temp0_l, temp1_l, temp2_l, temp3_l, temp4_l, temp5_l;        \
    __m128i sum0_r, sum1_r, sum2_r, sum3_r;                              \
    __m128i sum0_l, sum1_l, sum2_l, sum3_l;                              \
                                                                         \
    DUP4_ARG2(__lsx_vilvl_h, in4, in0, in6, in2, in5, in1, in3, in7,     \
              src0_r, src1_r, src2_r, src3_r);                           \
    DUP4_ARG2(__lsx_vilvh_h, in4, in0, in6, in2, in5, in1, in3, in7,     \
              src0_l, src1_l, src2_l, src3_l);                           \
                                                                         \
    DUP4_ARG2(__lsx_vldrepl_w, filter, 0, filter, 4, filter, 8,          \
              filter, 12, filter0, filter1, filter2, filter3);           \
    DUP4_ARG2(__lsx_vdp2_w_h, src0_r, filter0, src0_l, filter0,          \
              src1_r, filter1, src1_l, filter1,  temp0_r, temp0_l,       \
              temp1_r, temp1_l);                                         \
                                                                         \
    LSX_BUTTERFLY_4_W(temp0_r, temp0_l, temp1_l, temp1_r, sum0_r, sum0_l,\
                      sum1_l, sum1_r);                                   \
    sum2_r = sum1_r;                                                     \
    sum2_l = sum1_l;                                                     \
    sum3_r = sum0_r;                                                     \
    sum3_l = sum0_l;                                                     \
                                                                         \
    DUP4_ARG2(__lsx_vdp2_w_h, src2_r, filter2, src2_l, filter2,          \
              src3_r, filter3, src3_l, filter3,  temp2_r, temp2_l,       \
              temp3_r, temp3_l);                                         \
    temp2_r = __lsx_vadd_w(temp2_r, temp3_r);                            \
    temp2_l = __lsx_vadd_w(temp2_l, temp3_l);                            \
    sum0_r  = __lsx_vadd_w(sum0_r, temp2_r);                             \
    sum0_l  = __lsx_vadd_w(sum0_l, temp2_l);                             \
    sum3_r  = __lsx_vsub_w(sum3_r, temp2_r);                             \
    sum3_l  = __lsx_vsub_w(sum3_l, temp2_l);                             \
                                                                         \
    in0 = __lsx_vssrarni_h_w(sum0_l, sum0_r, shift);                     \
    in7 = __lsx_vssrarni_h_w(sum3_l, sum3_r, shift);                     \
                                                                         \
    DUP4_ARG2(__lsx_vdp2_w_h, src2_r, filter3, src2_l, filter3,          \
              src3_r, filter2, src3_l, filter2,  temp4_r, temp4_l,       \
              temp5_r, temp5_l);                                         \
    temp4_r = __lsx_vsub_w(temp4_r, temp5_r);                            \
    temp4_l = __lsx_vsub_w(temp4_l, temp5_l);                            \
    sum1_r  = __lsx_vadd_w(sum1_r, temp4_r);                             \
    sum1_l  = __lsx_vadd_w(sum1_l, temp4_l);                             \
    sum2_r  = __lsx_vsub_w(sum2_r, temp4_r);                             \
    sum2_l  = __lsx_vsub_w(sum2_l, temp4_l);                             \
                                                                         \
    in3 = __lsx_vssrarni_h_w(sum1_l, sum1_r, shift);                     \
    in4 = __lsx_vssrarni_h_w(sum2_l, sum2_r, shift);                     \
                                                                         \
    DUP4_ARG2(__lsx_vldrepl_w, filter, 16, filter, 20, filter, 24,       \
              filter, 28, filter0, filter1, filter2, filter3);           \
    DUP4_ARG2(__lsx_vdp2_w_h, src0_r, filter0, src0_l, filter0,          \
              src1_r, filter1, src1_l, filter1,  temp0_r, temp0_l,       \
              temp1_r, temp1_l);                                         \
                                                                         \
    LSX_BUTTERFLY_4_W(temp0_r, temp0_l, temp1_l, temp1_r, sum0_r, sum0_l,\
                      sum1_l, sum1_r);                                   \
    sum2_r = sum1_r;                                                     \
    sum2_l = sum1_l;                                                     \
    sum3_r = sum0_r;                                                     \
    sum3_l = sum0_l;                                                     \
                                                                         \
    DUP4_ARG2(__lsx_vdp2_w_h, src2_r, filter2, src2_l, filter2,          \
              src3_r, filter3, src3_l, filter3,  temp2_r, temp2_l,       \
              temp3_r, temp3_l);                                         \
    temp2_r = __lsx_vadd_w(temp2_r, temp3_r);                            \
    temp2_l = __lsx_vadd_w(temp2_l, temp3_l);                            \
    sum0_r  = __lsx_vadd_w(sum0_r, temp2_r);                             \
    sum0_l  = __lsx_vadd_w(sum0_l, temp2_l);                             \
    sum3_r  = __lsx_vsub_w(sum3_r, temp2_r);                             \
    sum3_l  = __lsx_vsub_w(sum3_l, temp2_l);                             \
                                                                         \
    in1 = __lsx_vssrarni_h_w(sum0_l, sum0_r, shift);                     \
    in6 = __lsx_vssrarni_h_w(sum3_l, sum3_r, shift);                     \
                                                                         \
    DUP4_ARG2(__lsx_vdp2_w_h, src2_r, filter3, src2_l, filter3,          \
              src3_r, filter2, src3_l, filter2,  temp4_r, temp4_l,       \
              temp5_r, temp5_l);                                         \
    temp4_r = __lsx_vsub_w(temp4_r, temp5_r);                            \
    temp4_l = __lsx_vsub_w(temp4_l, temp5_l);                            \
    sum1_r  = __lsx_vsub_w(sum1_r, temp4_r);                             \
    sum1_l  = __lsx_vsub_w(sum1_l, temp4_l);                             \
    sum2_r  = __lsx_vadd_w(sum2_r, temp4_r);                             \
    sum2_l  = __lsx_vadd_w(sum2_l, temp4_l);                             \
                                                                         \
    in2 = __lsx_vssrarni_h_w(sum1_l, sum1_r, shift);                     \
    in5 = __lsx_vssrarni_h_w(sum2_l, sum2_r, shift);                     \
}

#define HEVC_IDCT16x16_COL(src0_r, src1_r, src2_r, src3_r,                   \
                           src4_r, src5_r, src6_r, src7_r,                   \
                           src0_l, src1_l, src2_l, src3_l,                   \
                           src4_l, src5_l, src6_l, src7_l, shift)            \
{                                                                            \
    int16_t *ptr0, *ptr1;                                                    \
    __m128i dst0, dst1;                                                      \
    __m128i filter0, filter1, filter2, filter3;                              \
    __m128i temp0_r, temp1_r, temp0_l, temp1_l;                              \
    __m128i sum0_r, sum1_r, sum2_r, sum3_r, sum0_l, sum1_l, sum2_l;          \
    __m128i sum3_l, res0_r, res1_r, res0_l, res1_l;                          \
                                                                             \
    ptr0 = (buf_ptr + 112);                                                  \
    ptr1 = (buf_ptr + 128);                                                  \
    k = -1;                                                                  \
                                                                             \
    for (j = 0; j < 4; j++)                                                  \
    {                                                                        \
        DUP4_ARG2(__lsx_vldrepl_w, filter, 0, filter, 4, filter, 16,         \
                  filter, 20, filter0, filter1, filter2, filter3);           \
        DUP4_ARG2(__lsx_vdp2_w_h, src0_r, filter0, src0_l, filter0,          \
                  src4_r, filter2, src4_l, filter2,  sum0_r, sum0_l,         \
                  sum2_r, sum2_l);                                           \
        DUP2_ARG2(__lsx_vdp2_w_h, src7_r, filter2, src7_l, filter2,          \
                  sum3_r, sum3_l);                                           \
        DUP4_ARG3(__lsx_vdp2add_w_h, sum0_r, src1_r, filter1, sum0_l,        \
                  src1_l, filter1, sum2_r, src5_r, filter3, sum2_l,          \
                  src5_l, filter3, sum0_r, sum0_l, sum2_r, sum2_l);          \
        DUP2_ARG3(__lsx_vdp2add_w_h, sum3_r, src6_r, filter3, sum3_l,        \
                  src6_l, filter3, sum3_r, sum3_l);                          \
                                                                             \
        sum1_r = sum0_r;                                                     \
        sum1_l = sum0_l;                                                     \
                                                                             \
        DUP4_ARG2(__lsx_vldrepl_w, filter, 8, filter, 12, filter, 24,        \
                  filter, 28, filter0, filter1, filter2, filter3);           \
        filter += 16;                                                        \
        DUP2_ARG2(__lsx_vdp2_w_h, src2_r, filter0, src2_l, filter0,          \
                  temp0_r, temp0_l);                                         \
        DUP2_ARG3(__lsx_vdp2add_w_h, sum2_r, src6_r, filter2, sum2_l,        \
                  src6_l, filter2, sum2_r, sum2_l);                          \
        DUP2_ARG2(__lsx_vdp2_w_h, src5_r, filter2, src5_l, filter2,          \
                  temp1_r, temp1_l);                                         \
                                                                             \
        sum0_r = __lsx_vadd_w(sum0_r, temp0_r);                              \
        sum0_l = __lsx_vadd_w(sum0_l, temp0_l);                              \
        sum1_r = __lsx_vsub_w(sum1_r, temp0_r);                              \
        sum1_l = __lsx_vsub_w(sum1_l, temp0_l);                              \
        sum3_r = __lsx_vsub_w(temp1_r, sum3_r);                              \
        sum3_l = __lsx_vsub_w(temp1_l, sum3_l);                              \
                                                                             \
        DUP2_ARG2(__lsx_vdp2_w_h, src3_r, filter1, src3_l, filter1,          \
                  temp0_r, temp0_l);                                         \
        DUP4_ARG3(__lsx_vdp2add_w_h, sum2_r, src7_r, filter3, sum2_l,        \
                  src7_l, filter3, sum3_r, src4_r, filter3, sum3_l,          \
                  src4_l, filter3, sum2_r, sum2_l, sum3_r, sum3_l);          \
                                                                             \
        sum0_r = __lsx_vadd_w(sum0_r, temp0_r);                              \
        sum0_l = __lsx_vadd_w(sum0_l, temp0_l);                              \
        sum1_r = __lsx_vsub_w(sum1_r, temp0_r);                              \
        sum1_l = __lsx_vsub_w(sum1_l, temp0_l);                              \
                                                                             \
        LSX_BUTTERFLY_4_W(sum0_r, sum0_l, sum2_l, sum2_r, res0_r, res0_l,    \
                          res1_l, res1_r);                                   \
        dst0 = __lsx_vssrarni_h_w(res0_l, res0_r, shift);                    \
        dst1 = __lsx_vssrarni_h_w(res1_l, res1_r, shift);                    \
        __lsx_vst(dst0, buf_ptr, 0);                                         \
        __lsx_vst(dst1, (buf_ptr + ((15 - (j * 2)) << 4)), 0);               \
                                                                             \
        LSX_BUTTERFLY_4_W(sum1_r, sum1_l, sum3_l, sum3_r, res0_r, res0_l,    \
                          res1_l, res1_r);                                   \
                                                                             \
        dst0 = __lsx_vssrarni_h_w(res0_l, res0_r, shift);                    \
        dst1 = __lsx_vssrarni_h_w(res1_l, res1_r, shift);                    \
        __lsx_vst(dst0, (ptr0 + ((((j + 1) >> 1) * 2 * k) << 4)), 0);        \
        __lsx_vst(dst1, (ptr1 - ((((j + 1) >> 1) * 2 * k) << 4)), 0);        \
                                                                             \
        k *= -1;                                                             \
        buf_ptr += 16;                                                       \
    }                                                                        \
}

#define HEVC_EVEN16_CALC(input, sum0_r, sum0_l, load_idx, store_idx)  \
{                                                                     \
    tmp0_r = __lsx_vld(input + load_idx * 8, 0);                      \
    tmp0_l = __lsx_vld(input + load_idx * 8, 16);                     \
    tmp1_r = sum0_r;                                                  \
    tmp1_l = sum0_l;                                                  \
    sum0_r = __lsx_vadd_w(sum0_r, tmp0_r);                            \
    sum0_l = __lsx_vadd_w(sum0_l, tmp0_l);                            \
    __lsx_vst(sum0_r, (input + load_idx * 8), 0);                     \
    __lsx_vst(sum0_l, (input + load_idx * 8), 16);                    \
    tmp1_r = __lsx_vsub_w(tmp1_r, tmp0_r);                            \
    tmp1_l = __lsx_vsub_w(tmp1_l, tmp0_l);                            \
    __lsx_vst(tmp1_r, (input + store_idx * 8), 0);                    \
    __lsx_vst(tmp1_l, (input + store_idx * 8), 16);                   \
}

#define HEVC_IDCT_LUMA4x4_COL(in_r0, in_l0, in_r1, in_l1,     \
                              res0, res1, res2, res3, shift)  \
{                                                             \
    __m128i vec0, vec1, vec2, vec3;                           \
    __m128i cnst74 = __lsx_vldi(0x84a);                       \
    __m128i cnst55 = __lsx_vldi(0x837);                       \
    __m128i cnst29 = __lsx_vldi(0x81d);                       \
                                                              \
    vec0 = __lsx_vadd_w(in_r0, in_r1);                        \
    vec2 = __lsx_vsub_w(in_r0, in_l1);                        \
    res0 = __lsx_vmul_w(vec0, cnst29);                        \
    res1 = __lsx_vmul_w(vec2, cnst55);                        \
    res2 = __lsx_vsub_w(in_r0, in_r1);                        \
    vec1 = __lsx_vadd_w(in_r1, in_l1);                        \
    res2 = __lsx_vadd_w(res2, in_l1);                         \
    vec3 = __lsx_vmul_w(in_l0, cnst74);                       \
    res3 = __lsx_vmul_w(vec0, cnst55);                        \
                                                              \
    res0 = __lsx_vadd_w(res0, __lsx_vmul_w(vec1, cnst55));    \
    res1 = __lsx_vsub_w(res1, __lsx_vmul_w(vec1, cnst29));    \
    res2 = __lsx_vmul_w(res2, cnst74);                        \
    res3 = __lsx_vadd_w(res3, __lsx_vmul_w(vec2, cnst29));    \
                                                              \
    res0 = __lsx_vadd_w(res0, vec3);                          \
    res1 = __lsx_vadd_w(res1, vec3);                          \
    res3 = __lsx_vsub_w(res3, vec3);                          \
                                                              \
    res0 = __lsx_vsrari_w(res0, shift);                       \
    res1 = __lsx_vsrari_w(res1, shift);                       \
    res2 = __lsx_vsrari_w(res2, shift);                       \
    res3 = __lsx_vsrari_w(res3, shift);                       \
    res0 = __lsx_vsat_w(res0, 15);                            \
    res1 = __lsx_vsat_w(res1, 15);                            \
    res2 = __lsx_vsat_w(res2, 15);                            \
    res3 = __lsx_vsat_w(res3, 15);                            \
}

void ff_hevc_idct_4x4_lsx(int16_t *coeffs, int col_limit)
{
    __m128i in0, in1;
    __m128i in_r0, in_l0, in_r1, in_l1;
    __m128i sum0, sum1, sum2, sum3;
    __m128i zero = __lsx_vldi(0x00);

    in0   = __lsx_vld(coeffs, 0);
    in1   = __lsx_vld(coeffs, 16);
    in_r0 = __lsx_vilvl_h(zero, in0);
    in_l0 = __lsx_vilvh_h(zero, in0);
    in_r1 = __lsx_vilvl_h(zero, in1);
    in_l1 = __lsx_vilvh_h(zero, in1);

    HEVC_IDCT4x4_COL(in_r0, in_l0, in_r1, in_l1, sum0, sum1, sum2, sum3, 7);
    LSX_TRANSPOSE4x4_W(sum0, sum1, sum2, sum3, in_r0, in_l0, in_r1, in_l1);
    HEVC_IDCT4x4_COL(in_r0, in_l0, in_r1, in_l1, sum0, sum1, sum2, sum3, 12);

    /* Pack and transpose */
    in0  = __lsx_vpickev_h(sum2, sum0);
    in1  = __lsx_vpickev_h(sum3, sum1);
    sum0 = __lsx_vilvl_h(in1, in0);
    sum1 = __lsx_vilvh_h(in1, in0);
    in0  = __lsx_vilvl_w(sum1, sum0);
    in1  = __lsx_vilvh_w(sum1, sum0);

    __lsx_vst(in0, coeffs, 0);
    __lsx_vst(in1, coeffs, 16);
}

void ff_hevc_idct_8x8_lsx(int16_t *coeffs, int col_limit)
{
    const int16_t *filter = &gt8x8_cnst[0];
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;

    DUP4_ARG2(__lsx_vld, coeffs, 0, coeffs, 16, coeffs, 32,
              coeffs, 48, in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vld, coeffs, 64, coeffs, 80, coeffs, 96,
              coeffs, 112, in4, in5, in6, in7);
    HEVC_IDCT8x8_COL(in0, in1, in2, in3, in4, in5, in6, in7, 7);
    LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    HEVC_IDCT8x8_COL(in0, in1, in2, in3, in4, in5, in6, in7, 12);
    LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);

    __lsx_vst(in0, coeffs, 0);
    __lsx_vst(in1, coeffs, 16);
    __lsx_vst(in2, coeffs, 32);
    __lsx_vst(in3, coeffs, 48);
    __lsx_vst(in4, coeffs, 64);
    __lsx_vst(in5, coeffs, 80);
    __lsx_vst(in6, coeffs, 96);
    __lsx_vst(in7, coeffs, 112);
}

void ff_hevc_idct_16x16_lsx(int16_t *coeffs, int col_limit)
{
    int16_t i, j, k;
    int16_t buf[256];
    int16_t *buf_ptr = &buf[0];
    int16_t *src = coeffs;
    const int16_t *filter = &gt16x16_cnst[0];
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i in8, in9, in10, in11, in12, in13, in14, in15;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    __m128i src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l, src7_l;

    for (i = 2; i--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src, 32, src, 64, src, 96,
                  in0, in1, in2, in3);
        DUP4_ARG2(__lsx_vld, src, 128, src, 160, src, 192, src, 224,
                  in4, in5, in6, in7);
        DUP4_ARG2(__lsx_vld, src, 256, src, 288, src, 320, src, 352,
                  in8, in9, in10, in11);
        DUP4_ARG2(__lsx_vld, src, 384, src, 416, src, 448, src, 480,
                  in12, in13, in14, in15);

        DUP4_ARG2(__lsx_vilvl_h, in4, in0, in12, in8, in6, in2, in14, in10,
                  src0_r, src1_r, src2_r, src3_r);
        DUP4_ARG2(__lsx_vilvl_h, in5, in1, in13, in9, in3, in7, in11, in15,
                  src4_r, src5_r, src6_r, src7_r);
        DUP4_ARG2(__lsx_vilvh_h, in4, in0, in12, in8, in6, in2, in14, in10,
                  src0_l, src1_l, src2_l, src3_l);
        DUP4_ARG2(__lsx_vilvh_h, in5, in1, in13, in9, in3, in7, in11, in15,
                  src4_l, src5_l, src6_l, src7_l);

        HEVC_IDCT16x16_COL(src0_r, src1_r, src2_r, src3_r, src4_r, src5_r,
                           src6_r, src7_r, src0_l, src1_l, src2_l, src3_l,
                           src4_l, src5_l, src6_l, src7_l, 7);

        src += 8;
        buf_ptr = (&buf[0] + 8);
        filter = &gt16x16_cnst[0];
    }

    src = &buf[0];
    buf_ptr = coeffs;
    filter = &gt16x16_cnst[0];

    for (i = 2; i--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  in0, in8, in1, in9);
        DUP4_ARG2(__lsx_vld, src, 64, src, 80, src, 96, src, 112,
                  in2, in10, in3, in11);
        DUP4_ARG2(__lsx_vld, src, 128, src, 144, src, 160, src, 176,
                  in4, in12, in5, in13);
        DUP4_ARG2(__lsx_vld, src, 192, src, 208, src, 224, src, 240,
                  in6, in14, in7, in15);
        LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                           in0, in1, in2, in3, in4, in5, in6, in7);
        LSX_TRANSPOSE8x8_H(in8, in9, in10, in11, in12, in13, in14, in15,
                           in8, in9, in10, in11, in12, in13, in14, in15);
        DUP4_ARG2(__lsx_vilvl_h, in4, in0, in12, in8, in6, in2, in14, in10,
                  src0_r, src1_r, src2_r, src3_r);
        DUP4_ARG2(__lsx_vilvl_h, in5, in1, in13, in9, in3, in7, in11, in15,
                  src4_r, src5_r, src6_r, src7_r);
        DUP4_ARG2(__lsx_vilvh_h, in4, in0, in12, in8, in6, in2, in14, in10,
                  src0_l, src1_l, src2_l, src3_l);
        DUP4_ARG2(__lsx_vilvh_h, in5, in1, in13, in9, in3, in7, in11, in15,
                  src4_l, src5_l, src6_l, src7_l);
        HEVC_IDCT16x16_COL(src0_r, src1_r, src2_r, src3_r, src4_r, src5_r,
                           src6_r, src7_r, src0_l, src1_l, src2_l, src3_l,
                           src4_l, src5_l, src6_l, src7_l, 12);

        src += 128;
        buf_ptr = coeffs + 8;
        filter = &gt16x16_cnst[0];
    }

    DUP4_ARG2(__lsx_vld, coeffs, 0, coeffs, 32, coeffs, 64, coeffs, 96,
              in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vld, coeffs, 128, coeffs, 160, coeffs, 192, coeffs, 224,
              in4, in5, in6, in7);
    LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    __lsx_vst(vec0, coeffs, 0);
    __lsx_vst(vec1, coeffs, 32);
    __lsx_vst(vec2, coeffs, 64);
    __lsx_vst(vec3, coeffs, 96);
    __lsx_vst(vec4, coeffs, 128);
    __lsx_vst(vec5, coeffs, 160);
    __lsx_vst(vec6, coeffs, 192);
    __lsx_vst(vec7, coeffs, 224);

    src = coeffs + 8;
    DUP4_ARG2(__lsx_vld, src, 0, src, 32, src, 64, src, 96, in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vld, src, 128, src, 160, src, 192, src, 224,
              in4, in5, in6, in7);
    LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    src = coeffs + 128;
    DUP4_ARG2(__lsx_vld, src, 0, src, 32, src, 64, src, 96,
              in8, in9, in10, in11);
    DUP4_ARG2(__lsx_vld, src, 128, src, 160, src, 192, src, 224,
              in12, in13, in14, in15);

    __lsx_vst(vec0, src, 0);
    __lsx_vst(vec1, src, 32);
    __lsx_vst(vec2, src, 64);
    __lsx_vst(vec3, src, 96);
    __lsx_vst(vec4, src, 128);
    __lsx_vst(vec5, src, 160);
    __lsx_vst(vec6, src, 192);
    __lsx_vst(vec7, src, 224);
    LSX_TRANSPOSE8x8_H(in8, in9, in10, in11, in12, in13, in14, in15,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    src = coeffs + 8;
    __lsx_vst(vec0, src, 0);
    __lsx_vst(vec1, src, 32);
    __lsx_vst(vec2, src, 64);
    __lsx_vst(vec3, src, 96);
    __lsx_vst(vec4, src, 128);
    __lsx_vst(vec5, src, 160);
    __lsx_vst(vec6, src, 192);
    __lsx_vst(vec7, src, 224);

    src = coeffs + 136;
    DUP4_ARG2(__lsx_vld, src, 0, src, 32, src, 64, src, 96,
              in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vld, src, 128, src, 160, src, 192, src, 224,
              in4, in5, in6, in7);
    LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    __lsx_vst(vec0, src, 0);
    __lsx_vst(vec1, src, 32);
    __lsx_vst(vec2, src, 64);
    __lsx_vst(vec3, src, 96);
    __lsx_vst(vec4, src, 128);
    __lsx_vst(vec5, src, 160);
    __lsx_vst(vec6, src, 192);
    __lsx_vst(vec7, src, 224);
}

static void hevc_idct_8x32_column_lsx(int16_t *coeffs, int32_t buf_pitch,
                                      uint8_t round)
{
    uint8_t i;
    int32_t buf_pitch_2  = buf_pitch << 1;
    int32_t buf_pitch_4  = buf_pitch << 2;
    int32_t buf_pitch_8  = buf_pitch << 3;
    int32_t buf_pitch_16 = buf_pitch << 4;

    const int16_t *filter_ptr0 = &gt32x32_cnst0[0];
    const int16_t *filter_ptr1 = &gt32x32_cnst1[0];
    const int16_t *filter_ptr2 = &gt32x32_cnst2[0];
    const int16_t *filter_ptr3 = &gt8x8_cnst[0];
    int16_t *src0 = (coeffs + buf_pitch);
    int16_t *src1 = (coeffs + buf_pitch_2);
    int16_t *src2 = (coeffs + buf_pitch_4);
    int16_t *src3 = (coeffs);
    int32_t tmp_buf[8 * 32 + 15];
    int32_t *tmp_buf_ptr = tmp_buf + 15;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    __m128i src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l, src7_l;
    __m128i filter0, filter1, filter2, filter3;
    __m128i sum0_r, sum0_l, sum1_r, sum1_l, tmp0_r, tmp0_l, tmp1_r, tmp1_l;

    /* Align pointer to 64 byte boundary */
    tmp_buf_ptr = (int32_t *)(((uintptr_t) tmp_buf_ptr) & ~(uintptr_t) 63);

    /* process coeff 4, 12, 20, 28 */
    in0 = __lsx_vld(src2, 0);
    in1 = __lsx_vld(src2 + buf_pitch_8, 0);
    in2 = __lsx_vld(src2 + buf_pitch_16, 0);
    in3 = __lsx_vld(src2 + buf_pitch_16 + buf_pitch_8, 0);
    in4 = __lsx_vld(src3, 0);
    in5 = __lsx_vld(src3 + buf_pitch_8, 0);
    in6 = __lsx_vld(src3 + buf_pitch_16, 0);
    in7 = __lsx_vld(src3 + buf_pitch_16 + buf_pitch_8, 0);
    DUP4_ARG2(__lsx_vilvl_h, in1, in0, in3, in2, in6, in4, in7, in5,
              src0_r, src1_r, src2_r, src3_r);
    DUP4_ARG2(__lsx_vilvh_h, in1, in0, in3, in2, in6, in4, in7, in5,
              src0_l, src1_l, src2_l, src3_l);

    filter0 = __lsx_vldrepl_w(filter_ptr2, 0);
    filter1 = __lsx_vldrepl_w(filter_ptr2, 4);
    sum0_r = __lsx_vdp2_w_h(src0_r, filter0);
    sum0_l = __lsx_vdp2_w_h(src0_l, filter0);
    sum0_r = __lsx_vdp2add_w_h(sum0_r, src1_r, filter1);
    sum0_l = __lsx_vdp2add_w_h(sum0_l, src1_l, filter1);
    __lsx_vst(sum0_r, tmp_buf_ptr, 0);
    __lsx_vst(sum0_l, tmp_buf_ptr, 16);

    filter0 = __lsx_vldrepl_w(filter_ptr2, 8);
    filter1 = __lsx_vldrepl_w(filter_ptr2, 12);
    sum0_r = __lsx_vdp2_w_h(src0_r, filter0);
    sum0_l = __lsx_vdp2_w_h(src0_l, filter0);
    sum0_r = __lsx_vdp2add_w_h(sum0_r, src1_r, filter1);
    sum0_l = __lsx_vdp2add_w_h(sum0_l, src1_l, filter1);
    __lsx_vst(sum0_r, tmp_buf_ptr, 32);
    __lsx_vst(sum0_l, tmp_buf_ptr, 48);

    filter0 = __lsx_vldrepl_w(filter_ptr2, 16);
    filter1 = __lsx_vldrepl_w(filter_ptr2, 20);
    sum0_r = __lsx_vdp2_w_h(src0_r, filter0);
    sum0_l = __lsx_vdp2_w_h(src0_l, filter0);
    sum0_r = __lsx_vdp2add_w_h(sum0_r, src1_r, filter1);
    sum0_l = __lsx_vdp2add_w_h(sum0_l, src1_l, filter1);
    __lsx_vst(sum0_r, tmp_buf_ptr, 64);
    __lsx_vst(sum0_l, tmp_buf_ptr, 80);

    filter0 = __lsx_vldrepl_w(filter_ptr2, 24);
    filter1 = __lsx_vldrepl_w(filter_ptr2, 28);
    sum0_r = __lsx_vdp2_w_h(src0_r, filter0);
    sum0_l = __lsx_vdp2_w_h(src0_l, filter0);
    sum0_r = __lsx_vdp2add_w_h(sum0_r, src1_r, filter1);
    sum0_l = __lsx_vdp2add_w_h(sum0_l, src1_l, filter1);
    __lsx_vst(sum0_r, tmp_buf_ptr, 96);
    __lsx_vst(sum0_l, tmp_buf_ptr, 112);

    /* process coeff 0, 8, 16, 24 */
    filter0 = __lsx_vldrepl_w(filter_ptr3, 0);
    filter1 = __lsx_vldrepl_w(filter_ptr3, 4);

    DUP4_ARG2(__lsx_vdp2_w_h, src2_r, filter0, src2_l, filter0,
              src3_r, filter1, src3_l, filter1, sum0_r, sum0_l, tmp1_r, tmp1_l);
    sum1_r = __lsx_vsub_w(sum0_r, tmp1_r);
    sum1_l = __lsx_vsub_w(sum0_l, tmp1_l);
    sum0_r = __lsx_vadd_w(sum0_r, tmp1_r);
    sum0_l = __lsx_vadd_w(sum0_l, tmp1_l);

    HEVC_EVEN16_CALC(tmp_buf_ptr, sum0_r, sum0_l, 0, 7);
    HEVC_EVEN16_CALC(tmp_buf_ptr, sum1_r, sum1_l, 3, 4);

    filter0 = __lsx_vldrepl_w(filter_ptr3, 16);
    filter1 = __lsx_vldrepl_w(filter_ptr3, 20);

    DUP4_ARG2(__lsx_vdp2_w_h, src2_r, filter0, src2_l, filter0,
              src3_r, filter1, src3_l, filter1, sum0_r, sum0_l, tmp1_r, tmp1_l);
    sum1_r = __lsx_vsub_w(sum0_r, tmp1_r);
    sum1_l = __lsx_vsub_w(sum0_l, tmp1_l);
    sum0_r = __lsx_vadd_w(sum0_r, tmp1_r);
    sum0_l = __lsx_vadd_w(sum0_l, tmp1_l);

    HEVC_EVEN16_CALC(tmp_buf_ptr, sum0_r, sum0_l, 1, 6);
    HEVC_EVEN16_CALC(tmp_buf_ptr, sum1_r, sum1_l, 2, 5);

    /* process coeff 2 6 10 14 18 22 26 30 */
    in0 = __lsx_vld(src1, 0);
    in1 = __lsx_vld(src1 + buf_pitch_4, 0);
    in2 = __lsx_vld(src1 + buf_pitch_8, 0);
    in3 = __lsx_vld(src1 + buf_pitch_8 + buf_pitch_4, 0);
    in4 = __lsx_vld(src1 + buf_pitch_16, 0);
    in5 = __lsx_vld(src1 + buf_pitch_16 + buf_pitch_4, 0);
    in6 = __lsx_vld(src1 + buf_pitch_16 + buf_pitch_8, 0);
    in7 = __lsx_vld(src1 + buf_pitch_16 + buf_pitch_8 + buf_pitch_4, 0);

    DUP4_ARG2(__lsx_vilvl_h, in1, in0, in3, in2, in5, in4, in7, in6,
              src0_r, src1_r, src2_r, src3_r);
    DUP4_ARG2(__lsx_vilvh_h, in1, in0, in3, in2, in5, in4, in7, in6,
              src0_l, src1_l, src2_l, src3_l);

    /* loop for all columns of constants */
    for (i = 0; i < 8; i++) {
        /* processing single column of constants */
        filter0 = __lsx_vldrepl_w(filter_ptr1, 0);
        filter1 = __lsx_vldrepl_w(filter_ptr1, 4);
        filter2 = __lsx_vldrepl_w(filter_ptr1, 8);
        filter3 = __lsx_vldrepl_w(filter_ptr1, 12);
        sum0_r = __lsx_vdp2_w_h(src0_r, filter0);
        sum0_l = __lsx_vdp2_w_h(src0_l, filter0);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src1_r, filter1);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src1_l, filter1);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src2_r, filter2);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src2_l, filter2);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src3_r, filter3);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src3_l, filter3);

        tmp0_r = __lsx_vld(tmp_buf_ptr + (i << 3), 0);
        tmp0_l = __lsx_vld(tmp_buf_ptr + (i << 3), 16);
        tmp1_r = tmp0_r;
        tmp1_l = tmp0_l;
        tmp0_r = __lsx_vadd_w(tmp0_r, sum0_r);
        tmp0_l = __lsx_vadd_w(tmp0_l, sum0_l);
        tmp1_r = __lsx_vsub_w(tmp1_r, sum0_r);
        tmp1_l = __lsx_vsub_w(tmp1_l, sum0_l);
        __lsx_vst(tmp0_r, tmp_buf_ptr + (i << 3), 0);
        __lsx_vst(tmp0_l, tmp_buf_ptr + (i << 3), 16);
        __lsx_vst(tmp1_r, tmp_buf_ptr + ((15 - i) * 8), 0);
        __lsx_vst(tmp1_l, tmp_buf_ptr + ((15 - i) * 8), 16);

        filter_ptr1 += 8;
    }

    /* process coeff 1 3 5 7 9 11 13 15 17 19 21 23 25 27 29 31 */
    in0 = __lsx_vld(src0, 0);
    in1 = __lsx_vld(src0 + buf_pitch_2, 0);
    in2 = __lsx_vld(src0 + buf_pitch_4, 0);
    in3 = __lsx_vld(src0 + buf_pitch_4 + buf_pitch_2, 0);
    in4 = __lsx_vld(src0 + buf_pitch_8, 0);
    in5 = __lsx_vld(src0 + buf_pitch_8 + buf_pitch_2, 0);
    in6 = __lsx_vld(src0 + buf_pitch_8 + buf_pitch_4, 0);
    in7 = __lsx_vld(src0 + buf_pitch_8 + buf_pitch_4 + buf_pitch_2, 0);

    src0 += 16 * buf_pitch;
    DUP4_ARG2(__lsx_vilvl_h, in1, in0, in3, in2, in5, in4, in7, in6,
              src0_r, src1_r, src2_r, src3_r);
    DUP4_ARG2(__lsx_vilvh_h, in1, in0, in3, in2, in5, in4, in7, in6,
              src0_l, src1_l, src2_l, src3_l);
    in0 = __lsx_vld(src0, 0);
    in1 = __lsx_vld(src0 + buf_pitch_2, 0);
    in2 = __lsx_vld(src0 + buf_pitch_4, 0);
    in3 = __lsx_vld(src0 + buf_pitch_4 + buf_pitch_2, 0);
    in4 = __lsx_vld(src0 + buf_pitch_8, 0);
    in5 = __lsx_vld(src0 + buf_pitch_8 + buf_pitch_2, 0);
    in6 = __lsx_vld(src0 + buf_pitch_8 + buf_pitch_4, 0);
    in7 = __lsx_vld(src0 + buf_pitch_8 + buf_pitch_4 + buf_pitch_2, 0);

    DUP4_ARG2(__lsx_vilvl_h, in1, in0, in3, in2, in5, in4, in7, in6,
              src4_r, src5_r, src6_r, src7_r);
    DUP4_ARG2(__lsx_vilvh_h, in1, in0, in3, in2, in5, in4, in7, in6,
              src4_l, src5_l, src6_l, src7_l);

    /* loop for all columns of filter constants */
    for (i = 0; i < 16; i++) {
        /* processing single column of constants */
        filter0 = __lsx_vldrepl_w(filter_ptr0, 0);
        filter1 = __lsx_vldrepl_w(filter_ptr0, 4);
        filter2 = __lsx_vldrepl_w(filter_ptr0, 8);
        filter3 = __lsx_vldrepl_w(filter_ptr0, 12);
        sum0_r = __lsx_vdp2_w_h(src0_r, filter0);
        sum0_l = __lsx_vdp2_w_h(src0_l, filter0);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src1_r, filter1);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src1_l, filter1);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src2_r, filter2);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src2_l, filter2);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src3_r, filter3);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src3_l, filter3);
        tmp1_r = sum0_r;
        tmp1_l = sum0_l;

        filter0 = __lsx_vldrepl_w(filter_ptr0, 16);
        filter1 = __lsx_vldrepl_w(filter_ptr0, 20);
        filter2 = __lsx_vldrepl_w(filter_ptr0, 24);
        filter3 = __lsx_vldrepl_w(filter_ptr0, 28);
        sum0_r = __lsx_vdp2_w_h(src4_r, filter0);
        sum0_l = __lsx_vdp2_w_h(src4_l, filter0);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src5_r, filter1);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src5_l, filter1);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src6_r, filter2);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src6_l, filter2);
        sum0_r = __lsx_vdp2add_w_h(sum0_r, src7_r, filter3);
        sum0_l = __lsx_vdp2add_w_h(sum0_l, src7_l, filter3);
        sum0_r = __lsx_vadd_w(sum0_r, tmp1_r);
        sum0_l = __lsx_vadd_w(sum0_l, tmp1_l);

        tmp0_r = __lsx_vld(tmp_buf_ptr + i * 8, 0);
        tmp0_l = __lsx_vld(tmp_buf_ptr + i * 8, 16);
        tmp1_r = tmp0_r;
        tmp1_l = tmp0_l;
        tmp0_r = __lsx_vadd_w(tmp0_r, sum0_r);
        tmp0_l = __lsx_vadd_w(tmp0_l, sum0_l);
        sum1_r = __lsx_vreplgr2vr_w(round);
        tmp0_r = __lsx_vssrarn_h_w(tmp0_r, sum1_r);
        tmp0_l = __lsx_vssrarn_h_w(tmp0_l, sum1_r);
        in0    = __lsx_vpackev_d(tmp0_l, tmp0_r);
        __lsx_vst(in0, (coeffs + i * buf_pitch), 0);
        tmp1_r = __lsx_vsub_w(tmp1_r, sum0_r);
        tmp1_l = __lsx_vsub_w(tmp1_l, sum0_l);
        tmp1_r = __lsx_vssrarn_h_w(tmp1_r, sum1_r);
        tmp1_l = __lsx_vssrarn_h_w(tmp1_l, sum1_r);
        in0    = __lsx_vpackev_d(tmp1_l, tmp1_r);
        __lsx_vst(in0, (coeffs + (31 - i) * buf_pitch), 0);

        filter_ptr0 += 16;
    }
}

static void hevc_idct_transpose_32x8_to_8x32(int16_t *coeffs, int16_t *tmp_buf)
{
    uint8_t i;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;

    for (i = 0; i < 4; i++) {
        DUP4_ARG2(__lsx_vld, coeffs, 0, coeffs, 64, coeffs, 128,
                  coeffs, 192, in0, in1, in2, in3);
        DUP4_ARG2(__lsx_vld, coeffs, 256, coeffs, 320, coeffs, 384,
                  coeffs, 448, in4, in5, in6, in7);
        coeffs += 8;
        LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                           in0, in1, in2, in3, in4, in5, in6, in7);
        __lsx_vst(in0, tmp_buf, 0);
        __lsx_vst(in1, tmp_buf, 16);
        __lsx_vst(in2, tmp_buf, 32);
        __lsx_vst(in3, tmp_buf, 48);
        __lsx_vst(in4, tmp_buf, 64);
        __lsx_vst(in5, tmp_buf, 80);
        __lsx_vst(in6, tmp_buf, 96);
        __lsx_vst(in7, tmp_buf, 112);
        tmp_buf += 64;
    }
}

static void hevc_idct_transpose_8x32_to_32x8(int16_t *tmp_buf, int16_t *coeffs)
{
    uint8_t i;
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;

    for (i = 0; i < 4; i++) {
        DUP4_ARG2(__lsx_vld, tmp_buf, 0, tmp_buf, 16, tmp_buf, 32,
                  tmp_buf, 48, in0, in1, in2, in3);
        DUP4_ARG2(__lsx_vld, tmp_buf, 64, tmp_buf, 80, tmp_buf, 96,
                  tmp_buf, 112, in4, in5, in6, in7);
        tmp_buf += 64;
        LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                           in0, in1, in2, in3, in4, in5, in6, in7);
        __lsx_vst(in0, coeffs, 0);
        __lsx_vst(in1, coeffs, 64);
        __lsx_vst(in2, coeffs, 128);
        __lsx_vst(in3, coeffs, 192);
        __lsx_vst(in4, coeffs, 256);
        __lsx_vst(in5, coeffs, 320);
        __lsx_vst(in6, coeffs, 384);
        __lsx_vst(in7, coeffs, 448);
        coeffs += 8;
    }
}

void ff_hevc_idct_32x32_lsx(int16_t *coeffs, int col_limit)
{
    uint8_t row_cnt, col_cnt;
    int16_t *src = coeffs;
    int16_t tmp_buf[8 * 32 + 31];
    int16_t *tmp_buf_ptr = tmp_buf + 31;
    uint8_t round;
    int32_t buf_pitch;

    /* Align pointer to 64 byte boundary */
    tmp_buf_ptr = (int16_t *)(((uintptr_t) tmp_buf_ptr) & ~(uintptr_t) 63);

    /* column transform */
    round = 7;
    buf_pitch = 32;
    for (col_cnt = 0; col_cnt < 4; col_cnt++) {
        /* process 8x32 blocks */
        hevc_idct_8x32_column_lsx((coeffs + col_cnt * 8), buf_pitch, round);
    }

    /* row transform */
    round = 12;
    buf_pitch = 8;
    for (row_cnt = 0; row_cnt < 4; row_cnt++) {
        /* process 32x8 blocks */
        src = (coeffs + 32 * 8 * row_cnt);

        hevc_idct_transpose_32x8_to_8x32(src, tmp_buf_ptr);
        hevc_idct_8x32_column_lsx(tmp_buf_ptr, buf_pitch, round);
        hevc_idct_transpose_8x32_to_32x8(tmp_buf_ptr, src);
    }
}
