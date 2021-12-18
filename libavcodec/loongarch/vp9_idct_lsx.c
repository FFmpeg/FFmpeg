/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Jin Bo <jinbo@loongson.cn>
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

#include "libavcodec/vp9dsp.h"
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "vp9dsp_loongarch.h"
#include "libavutil/attributes.h"

#define VP9_DCT_CONST_BITS   14
#define ALLOC_ALIGNED(align) __attribute__ ((aligned(align)))
#define ROUND_POWER_OF_TWO(value, n) (((value) + (1 << ((n) - 1))) >> (n))

const int32_t cospi_1_64 = 16364;
const int32_t cospi_2_64 = 16305;
const int32_t cospi_3_64 = 16207;
const int32_t cospi_4_64 = 16069;
const int32_t cospi_5_64 = 15893;
const int32_t cospi_6_64 = 15679;
const int32_t cospi_7_64 = 15426;
const int32_t cospi_8_64 = 15137;
const int32_t cospi_9_64 = 14811;
const int32_t cospi_10_64 = 14449;
const int32_t cospi_11_64 = 14053;
const int32_t cospi_12_64 = 13623;
const int32_t cospi_13_64 = 13160;
const int32_t cospi_14_64 = 12665;
const int32_t cospi_15_64 = 12140;
const int32_t cospi_16_64 = 11585;
const int32_t cospi_17_64 = 11003;
const int32_t cospi_18_64 = 10394;
const int32_t cospi_19_64 = 9760;
const int32_t cospi_20_64 = 9102;
const int32_t cospi_21_64 = 8423;
const int32_t cospi_22_64 = 7723;
const int32_t cospi_23_64 = 7005;
const int32_t cospi_24_64 = 6270;
const int32_t cospi_25_64 = 5520;
const int32_t cospi_26_64 = 4756;
const int32_t cospi_27_64 = 3981;
const int32_t cospi_28_64 = 3196;
const int32_t cospi_29_64 = 2404;
const int32_t cospi_30_64 = 1606;
const int32_t cospi_31_64 = 804;

const int32_t sinpi_1_9 = 5283;
const int32_t sinpi_2_9 = 9929;
const int32_t sinpi_3_9 = 13377;
const int32_t sinpi_4_9 = 15212;

#define VP9_DOTP_CONST_PAIR(reg0, reg1, cnst0, cnst1, out0, out1)  \
{                                                                  \
    __m128i k0_m = __lsx_vreplgr2vr_h(cnst0);                      \
    __m128i s0_m, s1_m, s2_m, s3_m;                                \
                                                                   \
    s0_m = __lsx_vreplgr2vr_h(cnst1);                              \
    k0_m = __lsx_vpackev_h(s0_m, k0_m);                            \
                                                                   \
    s1_m = __lsx_vilvl_h(__lsx_vneg_h(reg1), reg0);                \
    s0_m = __lsx_vilvh_h(__lsx_vneg_h(reg1), reg0);                \
    s3_m = __lsx_vilvl_h(reg0, reg1);                              \
    s2_m = __lsx_vilvh_h(reg0, reg1);                              \
    DUP2_ARG2(__lsx_vdp2_w_h, s1_m, k0_m, s0_m, k0_m, s1_m, s0_m); \
    DUP2_ARG2(__lsx_vsrari_w, s1_m, VP9_DCT_CONST_BITS,            \
              s0_m, VP9_DCT_CONST_BITS, s1_m, s0_m);               \
    out0 = __lsx_vpickev_h(s0_m, s1_m);                            \
    DUP2_ARG2(__lsx_vdp2_w_h, s3_m, k0_m, s2_m, k0_m, s1_m, s0_m); \
    DUP2_ARG2(__lsx_vsrari_w, s1_m, VP9_DCT_CONST_BITS,            \
              s0_m, VP9_DCT_CONST_BITS, s1_m, s0_m);               \
    out1 = __lsx_vpickev_h(s0_m, s1_m);                            \
}

#define VP9_SET_COSPI_PAIR(c0_h, c1_h)    \
( {                                       \
    __m128i out0_m, r0_m, r1_m;           \
                                          \
    r0_m = __lsx_vreplgr2vr_h(c0_h);      \
    r1_m = __lsx_vreplgr2vr_h(c1_h);      \
    out0_m = __lsx_vpackev_h(r1_m, r0_m); \
                                          \
    out0_m;                               \
} )

#define VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3)      \
{                                                                     \
    uint8_t *dst_m = (uint8_t *) (dst);                               \
    __m128i dst0_m, dst1_m, dst2_m, dst3_m;                           \
    __m128i tmp0_m, tmp1_m;                                           \
    __m128i res0_m, res1_m, res2_m, res3_m;                           \
    __m128i zero_m = __lsx_vldi(0);                                   \
    DUP4_ARG2(__lsx_vld, dst_m, 0, dst_m + dst_stride, 0,             \
              dst_m + 2 * dst_stride, 0, dst_m + 3 * dst_stride, 0,   \
              dst0_m, dst1_m, dst2_m, dst3_m);                        \
    DUP4_ARG2(__lsx_vilvl_b, zero_m, dst0_m, zero_m, dst1_m, zero_m,  \
              dst2_m, zero_m, dst3_m, res0_m, res1_m, res2_m, res3_m);\
    DUP4_ARG2(__lsx_vadd_h, res0_m, in0, res1_m, in1, res2_m, in2,    \
              res3_m, in3, res0_m, res1_m, res2_m, res3_m);           \
    DUP4_ARG1(__lsx_vclip255_h, res0_m, res1_m, res2_m, res3_m,       \
              res0_m, res1_m, res2_m, res3_m);                        \
    DUP2_ARG2(__lsx_vpickev_b, res1_m, res0_m, res3_m, res2_m,        \
              tmp0_m, tmp1_m);                                        \
    __lsx_vstelm_d(tmp0_m, dst_m, 0, 0);                              \
    __lsx_vstelm_d(tmp0_m, dst_m + dst_stride, 0, 1);                 \
    __lsx_vstelm_d(tmp1_m, dst_m + 2 * dst_stride, 0, 0);             \
    __lsx_vstelm_d(tmp1_m, dst_m + 3 * dst_stride, 0, 1);             \
}

#define VP9_UNPCK_UB_SH(in, out_h, out_l) \
{                                         \
    __m128i zero = __lsx_vldi(0);         \
    out_l = __lsx_vilvl_b(zero, in);      \
    out_h = __lsx_vilvh_b(zero, in);      \
}

#define VP9_ILVLTRANS4x8_H(in0, in1, in2, in3, in4, in5, in6, in7,          \
                           out0, out1, out2, out3, out4, out5, out6, out7)  \
{                                                                           \
    __m128i tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                 \
    __m128i tmp0_n, tmp1_n, tmp2_n, tmp3_n;                                 \
    __m128i zero_m = __lsx_vldi(0);                                         \
                                                                            \
    DUP4_ARG2(__lsx_vilvl_h, in1, in0, in3, in2, in5, in4, in7, in6,        \
              tmp0_n, tmp1_n, tmp2_n, tmp3_n);                              \
    tmp0_m = __lsx_vilvl_w(tmp1_n, tmp0_n);                                 \
    tmp2_m = __lsx_vilvh_w(tmp1_n, tmp0_n);                                 \
    tmp1_m = __lsx_vilvl_w(tmp3_n, tmp2_n);                                 \
    tmp3_m = __lsx_vilvh_w(tmp3_n, tmp2_n);                                 \
                                                                            \
    out0 = __lsx_vilvl_d(tmp1_m, tmp0_m);                                   \
    out1 = __lsx_vilvh_d(tmp1_m, tmp0_m);                                   \
    out2 = __lsx_vilvl_d(tmp3_m, tmp2_m);                                   \
    out3 = __lsx_vilvh_d(tmp3_m, tmp2_m);                                   \
                                                                            \
    out4 = zero_m;                                                          \
    out5 = zero_m;                                                          \
    out6 = zero_m;                                                          \
    out7 = zero_m;                                                          \
}

/* multiply and add macro */
#define VP9_MADD(inp0, inp1, inp2, inp3, cst0, cst1, cst2, cst3,            \
                 out0, out1, out2, out3)                                    \
{                                                                           \
    __m128i madd_s0_m, madd_s1_m, madd_s2_m, madd_s3_m;                     \
    __m128i tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                 \
                                                                            \
    madd_s1_m = __lsx_vilvl_h(inp1, inp0);                                  \
    madd_s0_m = __lsx_vilvh_h(inp1, inp0);                                  \
    madd_s3_m = __lsx_vilvl_h(inp3, inp2);                                  \
    madd_s2_m = __lsx_vilvh_h(inp3, inp2);                                  \
    DUP4_ARG2(__lsx_vdp2_w_h, madd_s1_m, cst0, madd_s0_m, cst0,             \
              madd_s1_m, cst1, madd_s0_m, cst1, tmp0_m, tmp1_m,             \
              tmp2_m, tmp3_m);                                              \
    DUP4_ARG2(__lsx_vsrari_w, tmp0_m, VP9_DCT_CONST_BITS, tmp1_m,           \
              VP9_DCT_CONST_BITS, tmp2_m, VP9_DCT_CONST_BITS, tmp3_m,       \
              VP9_DCT_CONST_BITS, tmp0_m, tmp1_m, tmp2_m, tmp3_m);          \
    DUP2_ARG2(__lsx_vpickev_h, tmp1_m, tmp0_m, tmp3_m, tmp2_m, out0, out1); \
    DUP4_ARG2(__lsx_vdp2_w_h, madd_s3_m, cst2, madd_s2_m, cst2, madd_s3_m,  \
              cst3, madd_s2_m, cst3, tmp0_m, tmp1_m, tmp2_m, tmp3_m);       \
    DUP4_ARG2(__lsx_vsrari_w, tmp0_m, VP9_DCT_CONST_BITS,                   \
              tmp1_m, VP9_DCT_CONST_BITS, tmp2_m, VP9_DCT_CONST_BITS,       \
              tmp3_m, VP9_DCT_CONST_BITS, tmp0_m, tmp1_m, tmp2_m, tmp3_m);  \
    DUP2_ARG2(__lsx_vpickev_h, tmp1_m, tmp0_m, tmp3_m, tmp2_m, out2, out3); \
}

#define VP9_SET_CONST_PAIR(mask_h, idx1_h, idx2_h)                           \
( {                                                                          \
    __m128i c0_m, c1_m;                                                      \
                                                                             \
    DUP2_ARG2(__lsx_vreplvei_h, mask_h, idx1_h, mask_h, idx2_h, c0_m, c1_m); \
    c0_m = __lsx_vpackev_h(c1_m, c0_m);                                      \
                                                                             \
    c0_m;                                                                    \
} )

/* idct 8x8 macro */
#define VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,                 \
                       out0, out1, out2, out3, out4, out5, out6, out7)         \
{                                                                              \
    __m128i tp0_m, tp1_m, tp2_m, tp3_m, tp4_m, tp5_m, tp6_m, tp7_m;            \
    __m128i k0_m, k1_m, k2_m, k3_m, res0_m, res1_m, res2_m, res3_m;            \
    __m128i tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                    \
    v8i16 mask_m = { cospi_28_64, cospi_4_64, cospi_20_64, cospi_12_64,        \
          cospi_16_64, -cospi_4_64, -cospi_20_64, -cospi_16_64 };              \
                                                                               \
    k0_m = VP9_SET_CONST_PAIR(mask_m, 0, 5);                                   \
    k1_m = VP9_SET_CONST_PAIR(mask_m, 1, 0);                                   \
    k2_m = VP9_SET_CONST_PAIR(mask_m, 6, 3);                                   \
    k3_m = VP9_SET_CONST_PAIR(mask_m, 3, 2);                                   \
    VP9_MADD(in1, in7, in3, in5, k0_m, k1_m, k2_m, k3_m, in1, in7, in3, in5);  \
    DUP2_ARG2(__lsx_vsub_h, in1, in3, in7, in5, res0_m, res1_m);               \
    k0_m = VP9_SET_CONST_PAIR(mask_m, 4, 7);                                   \
    k1_m = __lsx_vreplvei_h(mask_m, 4);                                        \
                                                                               \
    res2_m = __lsx_vilvl_h(res0_m, res1_m);                                    \
    res3_m = __lsx_vilvh_h(res0_m, res1_m);                                    \
    DUP4_ARG2(__lsx_vdp2_w_h, res2_m, k0_m, res3_m, k0_m, res2_m, k1_m,        \
              res3_m, k1_m, tmp0_m, tmp1_m, tmp2_m, tmp3_m);                   \
    DUP4_ARG2(__lsx_vsrari_w, tmp0_m, VP9_DCT_CONST_BITS,                      \
              tmp1_m, VP9_DCT_CONST_BITS, tmp2_m, VP9_DCT_CONST_BITS,          \
              tmp3_m, VP9_DCT_CONST_BITS, tmp0_m, tmp1_m, tmp2_m, tmp3_m);     \
    tp4_m = __lsx_vadd_h(in1, in3);                                            \
    DUP2_ARG2(__lsx_vpickev_h, tmp1_m, tmp0_m, tmp3_m, tmp2_m, tp5_m, tp6_m);  \
    tp7_m = __lsx_vadd_h(in7, in5);                                            \
    k2_m = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);                       \
    k3_m = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);                        \
    VP9_MADD(in0, in4, in2, in6, k1_m, k0_m, k2_m, k3_m,                       \
             in0, in4, in2, in6);                                              \
    LSX_BUTTERFLY_4_H(in0, in4, in2, in6, tp0_m, tp1_m, tp2_m, tp3_m);         \
    LSX_BUTTERFLY_8_H(tp0_m, tp1_m, tp2_m, tp3_m, tp4_m, tp5_m, tp6_m, tp7_m,  \
                  out0, out1, out2, out3, out4, out5, out6, out7);             \
}

static av_always_inline
void vp9_idct8x8_1_add_lsx(int16_t *input, uint8_t *dst,
                                  int32_t dst_stride)
{
    int16_t out;
    int32_t val;
    __m128i vec;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    val = ROUND_POWER_OF_TWO(out, 5);
    vec = __lsx_vreplgr2vr_h(val);
    input[0] = 0;

    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, vec, vec, vec, vec);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, vec, vec, vec, vec);
}

static void vp9_idct8x8_12_colcol_addblk_lsx(int16_t *input, uint8_t *dst,
                                             int32_t dst_stride)
{
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i s0, s1, s2, s3, s4, s5, s6, s7, k0, k1, k2, k3, m0, m1, m2, m3;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements of 8x8 block */
    DUP4_ARG2(__lsx_vld, input, 0, input, 16, input, 32, input, 48,
              in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vld, input, 64, input, 80, input, 96, input, 112,
              in4, in5, in6, in7);
    __lsx_vst(zero, input, 0);
    __lsx_vst(zero, input, 16);
    __lsx_vst(zero, input, 32);
    __lsx_vst(zero, input, 48);
    __lsx_vst(zero, input, 64);
    __lsx_vst(zero, input, 80);
    __lsx_vst(zero, input, 96);
    __lsx_vst(zero, input, 112);
    DUP4_ARG2(__lsx_vilvl_d,in1, in0, in3, in2, in5, in4, in7,
              in6, in0, in1, in2, in3);

    /* stage1 */
    DUP2_ARG2(__lsx_vilvh_h, in3, in0, in2, in1, s0, s1);
    k0 = VP9_SET_COSPI_PAIR(cospi_28_64, -cospi_4_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_4_64, cospi_28_64);
    k2 = VP9_SET_COSPI_PAIR(-cospi_20_64, cospi_12_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_12_64, cospi_20_64);
    DUP4_ARG2(__lsx_vdp2_w_h, s0, k0, s0, k1, s1, k2, s1, k3,
              tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lsx_vsrari_w, tmp0, VP9_DCT_CONST_BITS, tmp1,
              VP9_DCT_CONST_BITS, tmp2, VP9_DCT_CONST_BITS, tmp3,
              VP9_DCT_CONST_BITS, tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lsx_vpickev_h, zero, tmp0, zero, tmp1, zero, tmp2, zero, tmp3,
              s0, s1, s2, s3);
    LSX_BUTTERFLY_4_H(s0, s1, s3, s2, s4, s7, s6, s5);

    /* stage2 */
    DUP2_ARG2(__lsx_vilvl_h, in3, in1, in2, in0, s1, s0);
    k0 = VP9_SET_COSPI_PAIR(cospi_16_64, cospi_16_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_16_64, -cospi_16_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);
    DUP4_ARG2(__lsx_vdp2_w_h, s0, k0, s0, k1, s1, k2, s1, k3,
                  tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lsx_vsrari_w, tmp0, VP9_DCT_CONST_BITS, tmp1,
              VP9_DCT_CONST_BITS, tmp2, VP9_DCT_CONST_BITS, tmp3,
              VP9_DCT_CONST_BITS, tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lsx_vpickev_h, zero, tmp0, zero, tmp1, zero, tmp2, zero, tmp3,
              s0, s1, s2, s3);
    LSX_BUTTERFLY_4_H(s0, s1, s2, s3, m0, m1, m2, m3);

    /* stage3 */
    s0 = __lsx_vilvl_h(s6, s5);

    k1 = VP9_SET_COSPI_PAIR(-cospi_16_64, cospi_16_64);
    DUP2_ARG2(__lsx_vdp2_w_h, s0, k1, s0, k0, tmp0, tmp1);
    DUP2_ARG2(__lsx_vsrari_w, tmp0, VP9_DCT_CONST_BITS, tmp1,
              VP9_DCT_CONST_BITS, tmp0, tmp1);
    DUP2_ARG2(__lsx_vpickev_h, zero, tmp0, zero, tmp1, s2, s3);

    /* stage4 */
    LSX_BUTTERFLY_8_H(m0, m1, m2, m3, s4, s2, s3, s7,
                      in0, in1, in2, in3, in4, in5, in6, in7);
    VP9_ILVLTRANS4x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);

    /* final rounding (add 2^4, divide by 2^5) and shift */
    DUP4_ARG2(__lsx_vsrari_h, in0 , 5, in1, 5, in2, 5, in3, 5,
              in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vsrari_h, in4 , 5, in5, 5, in6, 5, in7, 5,
              in4, in5, in6, in7);

    /* add block and store 8x8 */
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in4, in5, in6, in7);
}

static void vp9_idct8x8_colcol_addblk_lsx(int16_t *input, uint8_t *dst,
                                          int32_t dst_stride)
{
    __m128i in0, in1, in2, in3, in4, in5, in6, in7;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements of 8x8 block */
    DUP4_ARG2(__lsx_vld, input, 0, input, 16, input, 32, input, 48,
              in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vld, input, 64, input, 80, input, 96, input, 112,
              in4, in5, in6, in7);
    __lsx_vst(zero, input, 0);
    __lsx_vst(zero, input, 16);
    __lsx_vst(zero, input, 32);
    __lsx_vst(zero, input, 48);
    __lsx_vst(zero, input, 64);
    __lsx_vst(zero, input, 80);
    __lsx_vst(zero, input, 96);
    __lsx_vst(zero, input, 112);
    /* 1D idct8x8 */
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);
    /* columns transform */
    LSX_TRANSPOSE8x8_H(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    /* 1D idct8x8 */
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);
    /* final rounding (add 2^4, divide by 2^5) and shift */
    DUP4_ARG2(__lsx_vsrari_h, in0, 5, in1, 5, in2, 5, in3, 5,
              in0, in1, in2, in3);
    DUP4_ARG2(__lsx_vsrari_h, in4, 5, in5, 5, in6, 5, in7, 5,
              in4, in5, in6, in7);
    /* add block and store 8x8 */
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in4, in5, in6, in7);
}

static void vp9_idct16_1d_columns_addblk_lsx(int16_t *input, uint8_t *dst,
                                             int32_t dst_stride)
{
    __m128i loc0, loc1, loc2, loc3;
    __m128i reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14;
    __m128i reg1, reg3, reg5, reg7, reg9, reg11, reg13, reg15;
    __m128i tmp5, tmp6, tmp7;
    __m128i zero = __lsx_vldi(0);
    int32_t offset = dst_stride << 2;

    DUP4_ARG2(__lsx_vld, input, 32*0, input, 32*1, input, 32*2, input, 32*3,
              reg0, reg1, reg2, reg3);
    DUP4_ARG2(__lsx_vld, input, 32*4, input, 32*5, input, 32*6, input, 32*7,
              reg4, reg5, reg6, reg7);
    DUP4_ARG2(__lsx_vld, input, 32*8, input, 32*9, input, 32*10, input, 32*11,
              reg8, reg9, reg10, reg11);
    DUP4_ARG2(__lsx_vld, input, 32*12, input, 32*13, input, 32*14, input,
              32*15, reg12, reg13, reg14, reg15);

    __lsx_vst(zero, input, 32*0);
    __lsx_vst(zero, input, 32*1);
    __lsx_vst(zero, input, 32*2);
    __lsx_vst(zero, input, 32*3);
    __lsx_vst(zero, input, 32*4);
    __lsx_vst(zero, input, 32*5);
    __lsx_vst(zero, input, 32*6);
    __lsx_vst(zero, input, 32*7);
    __lsx_vst(zero, input, 32*8);
    __lsx_vst(zero, input, 32*9);
    __lsx_vst(zero, input, 32*10);
    __lsx_vst(zero, input, 32*11);
    __lsx_vst(zero, input, 32*12);
    __lsx_vst(zero, input, 32*13);
    __lsx_vst(zero, input, 32*14);
    __lsx_vst(zero, input, 32*15);

    VP9_DOTP_CONST_PAIR(reg2, reg14, cospi_28_64, cospi_4_64, reg2, reg14);
    VP9_DOTP_CONST_PAIR(reg10, reg6, cospi_12_64, cospi_20_64, reg10, reg6);
    LSX_BUTTERFLY_4_H(reg2, reg14, reg6, reg10, loc0, loc1, reg14, reg2);
    VP9_DOTP_CONST_PAIR(reg14, reg2, cospi_16_64, cospi_16_64, loc2, loc3);
    VP9_DOTP_CONST_PAIR(reg0, reg8, cospi_16_64, cospi_16_64, reg0, reg8);
    VP9_DOTP_CONST_PAIR(reg4, reg12, cospi_24_64, cospi_8_64, reg4, reg12);
    LSX_BUTTERFLY_4_H(reg8, reg0, reg4, reg12, reg2, reg6, reg10, reg14);

    reg0 = __lsx_vsub_h(reg2, loc1);
    reg2 = __lsx_vadd_h(reg2, loc1);
    reg12 = __lsx_vsub_h(reg14, loc0);
    reg14 = __lsx_vadd_h(reg14, loc0);
    reg4 = __lsx_vsub_h(reg6, loc3);
    reg6 = __lsx_vadd_h(reg6, loc3);
    reg8 = __lsx_vsub_h(reg10, loc2);
    reg10 = __lsx_vadd_h(reg10, loc2);

    /* stage2 */
    VP9_DOTP_CONST_PAIR(reg1, reg15, cospi_30_64, cospi_2_64, reg1, reg15);
    VP9_DOTP_CONST_PAIR(reg9, reg7, cospi_14_64, cospi_18_64, loc2, loc3);

    reg9 = __lsx_vsub_h(reg1, loc2);
    reg1 = __lsx_vadd_h(reg1, loc2);
    reg7 = __lsx_vsub_h(reg15, loc3);
    reg15 = __lsx_vadd_h(reg15, loc3);

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_22_64, cospi_10_64, reg5, reg11);
    VP9_DOTP_CONST_PAIR(reg13, reg3, cospi_6_64, cospi_26_64, loc0, loc1);
    LSX_BUTTERFLY_4_H(loc0, loc1, reg11, reg5, reg13, reg3, reg11, reg5);

    loc1 = __lsx_vadd_h(reg15, reg3);
    reg3 = __lsx_vsub_h(reg15, reg3);
    loc2 = __lsx_vadd_h(reg2, loc1);
    reg15 = __lsx_vsub_h(reg2, loc1);

    loc1 = __lsx_vadd_h(reg1, reg13);
    reg13 = __lsx_vsub_h(reg1, reg13);
    loc0 = __lsx_vadd_h(reg0, loc1);
    loc1 = __lsx_vsub_h(reg0, loc1);
    tmp6 = loc0;
    tmp7 = loc1;
    reg0 = loc2;

    VP9_DOTP_CONST_PAIR(reg7, reg9, cospi_24_64, cospi_8_64, reg7, reg9);
    VP9_DOTP_CONST_PAIR(__lsx_vneg_h(reg5), __lsx_vneg_h(reg11), cospi_8_64,
                        cospi_24_64, reg5, reg11);

    loc0 = __lsx_vadd_h(reg9, reg5);
    reg5 = __lsx_vsub_h(reg9, reg5);
    reg2 = __lsx_vadd_h(reg6, loc0);
    reg1 = __lsx_vsub_h(reg6, loc0);

    loc0 = __lsx_vadd_h(reg7, reg11);
    reg11 = __lsx_vsub_h(reg7, reg11);
    loc1 = __lsx_vadd_h(reg4, loc0);
    loc2 = __lsx_vsub_h(reg4, loc0);
    tmp5 = loc1;

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_16_64, cospi_16_64, reg5, reg11);
    LSX_BUTTERFLY_4_H(reg8, reg10, reg11, reg5, loc0, reg4, reg9, loc1);

    reg10 = loc0;
    reg11 = loc1;

    VP9_DOTP_CONST_PAIR(reg3, reg13, cospi_16_64, cospi_16_64, reg3, reg13);
    LSX_BUTTERFLY_4_H(reg12, reg14, reg13, reg3, reg8, reg6, reg7, reg5);
    reg13 = loc2;

    /* Transpose and store the output */
    reg12 = tmp5;
    reg14 = tmp6;
    reg3 = tmp7;

    DUP4_ARG2(__lsx_vsrari_h, reg0, 6, reg2, 6, reg4, 6, reg6, 6,
              reg0, reg2, reg4, reg6);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg0, reg2, reg4, reg6);
    dst += offset;
    DUP4_ARG2(__lsx_vsrari_h, reg8, 6, reg10, 6, reg12, 6, reg14, 6,
              reg8, reg10, reg12, reg14);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg8, reg10, reg12, reg14);
    dst += offset;
    DUP4_ARG2(__lsx_vsrari_h, reg3, 6, reg5, 6, reg11, 6, reg13, 6,
              reg3, reg5, reg11, reg13);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg3, reg13, reg11, reg5);
    dst += offset;
    DUP4_ARG2(__lsx_vsrari_h, reg1, 6, reg7, 6, reg9, 6, reg15, 6,
              reg1, reg7, reg9, reg15);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg7, reg9, reg1, reg15);
}

static void vp9_idct16_1d_columns_lsx(int16_t *input, int16_t *output)
{
    __m128i loc0, loc1, loc2, loc3;
    __m128i reg1, reg3, reg5, reg7, reg9, reg11, reg13, reg15;
    __m128i reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14;
    __m128i tmp5, tmp6, tmp7;
    __m128i zero = __lsx_vldi(0);
    int16_t *offset;

    DUP4_ARG2(__lsx_vld, input, 32*0, input, 32*1, input, 32*2, input, 32*3,
              reg0, reg1, reg2, reg3);
    DUP4_ARG2(__lsx_vld, input, 32*4, input, 32*5, input, 32*6, input, 32*7,
              reg4, reg5, reg6, reg7);
    DUP4_ARG2(__lsx_vld, input, 32*8, input, 32*9, input, 32*10, input, 32*11,
              reg8, reg9, reg10, reg11);
    DUP4_ARG2(__lsx_vld, input, 32*12, input, 32*13, input, 32*14, input,
              32*15, reg12, reg13, reg14, reg15);

    __lsx_vst(zero, input, 32*0);
    __lsx_vst(zero, input, 32*1);
    __lsx_vst(zero, input, 32*2);
    __lsx_vst(zero, input, 32*3);
    __lsx_vst(zero, input, 32*4);
    __lsx_vst(zero, input, 32*5);
    __lsx_vst(zero, input, 32*6);
    __lsx_vst(zero, input, 32*7);
    __lsx_vst(zero, input, 32*8);
    __lsx_vst(zero, input, 32*9);
    __lsx_vst(zero, input, 32*10);
    __lsx_vst(zero, input, 32*11);
    __lsx_vst(zero, input, 32*12);
    __lsx_vst(zero, input, 32*13);
    __lsx_vst(zero, input, 32*14);
    __lsx_vst(zero, input, 32*15);

    VP9_DOTP_CONST_PAIR(reg2, reg14, cospi_28_64, cospi_4_64, reg2, reg14);
    VP9_DOTP_CONST_PAIR(reg10, reg6, cospi_12_64, cospi_20_64, reg10, reg6);
    LSX_BUTTERFLY_4_H(reg2, reg14, reg6, reg10, loc0, loc1, reg14, reg2);
    VP9_DOTP_CONST_PAIR(reg14, reg2, cospi_16_64, cospi_16_64, loc2, loc3);
    VP9_DOTP_CONST_PAIR(reg0, reg8, cospi_16_64, cospi_16_64, reg0, reg8);
    VP9_DOTP_CONST_PAIR(reg4, reg12, cospi_24_64, cospi_8_64, reg4, reg12);
    LSX_BUTTERFLY_4_H(reg8, reg0, reg4, reg12, reg2, reg6, reg10, reg14);

    reg0 = __lsx_vsub_h(reg2, loc1);
    reg2 = __lsx_vadd_h(reg2, loc1);
    reg12 = __lsx_vsub_h(reg14, loc0);
    reg14 = __lsx_vadd_h(reg14, loc0);
    reg4 = __lsx_vsub_h(reg6, loc3);
    reg6 = __lsx_vadd_h(reg6, loc3);
    reg8 = __lsx_vsub_h(reg10, loc2);
    reg10 = __lsx_vadd_h(reg10, loc2);

    /* stage2 */
    VP9_DOTP_CONST_PAIR(reg1, reg15, cospi_30_64, cospi_2_64, reg1, reg15);
    VP9_DOTP_CONST_PAIR(reg9, reg7, cospi_14_64, cospi_18_64, loc2, loc3);

    reg9 = __lsx_vsub_h(reg1, loc2);
    reg1 = __lsx_vadd_h(reg1, loc2);
    reg7 = __lsx_vsub_h(reg15, loc3);
    reg15 = __lsx_vadd_h(reg15, loc3);

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_22_64, cospi_10_64, reg5, reg11);
    VP9_DOTP_CONST_PAIR(reg13, reg3, cospi_6_64, cospi_26_64, loc0, loc1);
    LSX_BUTTERFLY_4_H(loc0, loc1, reg11, reg5, reg13, reg3, reg11, reg5);

    loc1 = __lsx_vadd_h(reg15, reg3);
    reg3 = __lsx_vsub_h(reg15, reg3);
    loc2 = __lsx_vadd_h(reg2, loc1);
    reg15 = __lsx_vsub_h(reg2, loc1);

    loc1 = __lsx_vadd_h(reg1, reg13);
    reg13 = __lsx_vsub_h(reg1, reg13);
    loc0 = __lsx_vadd_h(reg0, loc1);
    loc1 = __lsx_vsub_h(reg0, loc1);
    tmp6 = loc0;
    tmp7 = loc1;
    reg0 = loc2;

    VP9_DOTP_CONST_PAIR(reg7, reg9, cospi_24_64, cospi_8_64, reg7, reg9);
    VP9_DOTP_CONST_PAIR(__lsx_vneg_h(reg5), __lsx_vneg_h(reg11), cospi_8_64,
                        cospi_24_64, reg5, reg11);

    loc0 = __lsx_vadd_h(reg9, reg5);
    reg5 = __lsx_vsub_h(reg9, reg5);
    reg2 = __lsx_vadd_h(reg6, loc0);
    reg1 = __lsx_vsub_h(reg6, loc0);

    loc0 = __lsx_vadd_h(reg7, reg11);
    reg11 = __lsx_vsub_h(reg7, reg11);
    loc1 = __lsx_vadd_h(reg4, loc0);
    loc2 = __lsx_vsub_h(reg4, loc0);

    tmp5 = loc1;

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_16_64, cospi_16_64, reg5, reg11);
    LSX_BUTTERFLY_4_H(reg8, reg10, reg11, reg5, loc0, reg4, reg9, loc1);

    reg10 = loc0;
    reg11 = loc1;

    VP9_DOTP_CONST_PAIR(reg3, reg13, cospi_16_64, cospi_16_64, reg3, reg13);
    LSX_BUTTERFLY_4_H(reg12, reg14, reg13, reg3, reg8, reg6, reg7, reg5);
    reg13 = loc2;

    /* Transpose and store the output */
    reg12 = tmp5;
    reg14 = tmp6;
    reg3 = tmp7;

    /* transpose block */
    LSX_TRANSPOSE8x8_H(reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14,
                       reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14);

    __lsx_vst(reg0, output, 32*0);
    __lsx_vst(reg2, output, 32*1);
    __lsx_vst(reg4, output, 32*2);
    __lsx_vst(reg6, output, 32*3);
    __lsx_vst(reg8, output, 32*4);
    __lsx_vst(reg10, output, 32*5);
    __lsx_vst(reg12, output, 32*6);
    __lsx_vst(reg14, output, 32*7);

    /* transpose block */
    LSX_TRANSPOSE8x8_H(reg3, reg13, reg11, reg5, reg7, reg9, reg1, reg15,
                       reg3, reg13, reg11, reg5, reg7, reg9, reg1, reg15);

    offset = output + 8;
    __lsx_vst(reg3, offset, 32*0);
    __lsx_vst(reg13, offset, 32*1);
    __lsx_vst(reg11, offset, 32*2);
    __lsx_vst(reg5, offset, 32*3);

    offset = output + 8 + 4 * 16;
    __lsx_vst(reg7, offset, 32*0);
    __lsx_vst(reg9, offset, 32*1);
    __lsx_vst(reg1, offset, 32*2);
    __lsx_vst(reg15, offset, 32*3);
}

static void vp9_idct16x16_1_add_lsx(int16_t *input, uint8_t *dst,
                                    int32_t dst_stride)
{
    uint8_t i;
    int16_t out;
    __m128i vec, res0, res1, res2, res3, res4, res5, res6, res7;
    __m128i dst0, dst1, dst2, dst3, tmp0, tmp1, tmp2, tmp3;
    int32_t stride2 = dst_stride << 1;
    int32_t stride3 = stride2 + dst_stride;
    int32_t stride4 = stride2 << 1;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO(out, 6);
    input[0] = 0;
    vec = __lsx_vreplgr2vr_h(out);

    for (i = 4; i--;) {
        dst0 = __lsx_vld(dst, 0);
        DUP2_ARG2(__lsx_vldx, dst, dst_stride, dst, stride2, dst1, dst2);
        dst3 = __lsx_vldx(dst, stride3);
        VP9_UNPCK_UB_SH(dst0, res4, res0);
        VP9_UNPCK_UB_SH(dst1, res5, res1);
        VP9_UNPCK_UB_SH(dst2, res6, res2);
        VP9_UNPCK_UB_SH(dst3, res7, res3);
        DUP4_ARG2(__lsx_vadd_h, res0, vec, res1, vec, res2, vec, res3, vec,
                  res0, res1, res2, res3);
        DUP4_ARG2(__lsx_vadd_h, res4, vec, res5, vec, res6, vec, res7, vec,
                  res4, res5, res6, res7);
        DUP4_ARG1(__lsx_vclip255_h, res0, res1, res2, res3,
                  res0, res1, res2, res3);
        DUP4_ARG1(__lsx_vclip255_h, res4, res5, res6, res7,
                  res4, res5, res6, res7);
        DUP4_ARG2(__lsx_vpickev_b, res4, res0, res5, res1, res6,
                  res2, res7, res3, tmp0, tmp1, tmp2, tmp3);
        __lsx_vst(tmp0, dst, 0);
        __lsx_vstx(tmp1, dst, dst_stride);
        __lsx_vstx(tmp2, dst, stride2);
        __lsx_vstx(tmp3, dst, stride3);
        dst += stride4;
    }
}

static void vp9_idct16x16_10_colcol_addblk_lsx(int16_t *input, uint8_t *dst,
                                               int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[16 * 16] ALLOC_ALIGNED(16);
    int16_t *out = out_arr;
    __m128i zero = __lsx_vldi(0);

    /* transform rows */
    vp9_idct16_1d_columns_lsx(input, out);

    /* short case just considers top 4 rows as valid output */
    out += 4 * 16;
    for (i = 3; i--;) {
        __lsx_vst(zero, out, 0);
        __lsx_vst(zero, out, 16);
        __lsx_vst(zero, out, 32);
        __lsx_vst(zero, out, 48);
        __lsx_vst(zero, out, 64);
        __lsx_vst(zero, out, 80);
        __lsx_vst(zero, out, 96);
        __lsx_vst(zero, out, 112);
        out += 64;
    }

    out = out_arr;

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_addblk_lsx((out + (i << 3)), (dst + (i << 3)),
                                         dst_stride);
    }
}

static void vp9_idct16x16_colcol_addblk_lsx(int16_t *input, uint8_t *dst,
                                            int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[16 * 16] ALLOC_ALIGNED(16);
    int16_t *out = out_arr;

    /* transform rows */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_lsx((input + (i << 3)), (out + (i << 7)));
    }

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_addblk_lsx((out + (i << 3)), (dst + (i << 3)),
                                         dst_stride);
    }
}

static void vp9_idct_butterfly_transpose_store(int16_t *tmp_buf,
                                               int16_t *tmp_eve_buf,
                                               int16_t *tmp_odd_buf,
                                               int16_t *dst)
{
    __m128i vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    __m128i m0, m1, m2, m3, m4, m5, m6, m7, n0, n1, n2, n3, n4, n5, n6, n7;

    /* FINAL BUTTERFLY : Dependency on Even & Odd */
    vec0 = __lsx_vld(tmp_odd_buf, 0);
    vec1 = __lsx_vld(tmp_odd_buf, 9 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 14 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 6 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 0);
    loc1 = __lsx_vld(tmp_eve_buf, 8 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 4 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 12 * 16);

    DUP4_ARG2(__lsx_vadd_h,loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              m0, m4, m2, m6);

    #define SUB(a, b) __lsx_vsub_h(a, b)

    __lsx_vst(SUB(loc0, vec3), tmp_buf, 31 * 16);
    __lsx_vst(SUB(loc1, vec2), tmp_buf, 23 * 16);
    __lsx_vst(SUB(loc2, vec1), tmp_buf, 27 * 16);
    __lsx_vst(SUB(loc3, vec0), tmp_buf, 19 * 16);

    /* Load 8 & Store 8 */
    vec0 = __lsx_vld(tmp_odd_buf, 4 * 16);
    vec1 = __lsx_vld(tmp_odd_buf, 13 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 10 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 3 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 2 * 16);
    loc1 = __lsx_vld(tmp_eve_buf, 10 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 6 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 14 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              m1, m5, m3, m7);

    __lsx_vst(SUB(loc0, vec3), tmp_buf, 29 * 16);
    __lsx_vst(SUB(loc1, vec2), tmp_buf, 21 * 16);
    __lsx_vst(SUB(loc2, vec1), tmp_buf, 25 * 16);
    __lsx_vst(SUB(loc3, vec0), tmp_buf, 17 * 16);

    /* Load 8 & Store 8 */
    vec0 = __lsx_vld(tmp_odd_buf, 2 * 16);
    vec1 = __lsx_vld(tmp_odd_buf, 11 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 12 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 7 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 1 * 16);
    loc1 = __lsx_vld(tmp_eve_buf, 9 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 5 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 13 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              n0, n4, n2, n6);

    __lsx_vst(SUB(loc0, vec3), tmp_buf, 30 * 16);
    __lsx_vst(SUB(loc1, vec2), tmp_buf, 22 * 16);
    __lsx_vst(SUB(loc2, vec1), tmp_buf, 26 * 16);
    __lsx_vst(SUB(loc3, vec0), tmp_buf, 18 * 16);

    /* Load 8 & Store 8 */
    vec0 = __lsx_vld(tmp_odd_buf, 5 * 16);
    vec1 = __lsx_vld(tmp_odd_buf, 15 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 8 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 1 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 3 * 16);
    loc1 = __lsx_vld(tmp_eve_buf, 11 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 7 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 15 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              n1, n5, n3, n7);

    __lsx_vst(SUB(loc0, vec3), tmp_buf, 28 * 16);
    __lsx_vst(SUB(loc1, vec2), tmp_buf, 20 * 16);
    __lsx_vst(SUB(loc2, vec1), tmp_buf, 24 * 16);
    __lsx_vst(SUB(loc3, vec0), tmp_buf, 16 * 16);

    /* Transpose : 16 vectors */
    /* 1st & 2nd 8x8 */
    LSX_TRANSPOSE8x8_H(m0, n0, m1, n1, m2, n2, m3, n3,
                       m0, n0, m1, n1, m2, n2, m3, n3);
    __lsx_vst(m0, dst, 0);
    __lsx_vst(n0, dst, 32 * 2);
    __lsx_vst(m1, dst, 32 * 4);
    __lsx_vst(n1, dst, 32 * 6);
    __lsx_vst(m2, dst, 32 * 8);
    __lsx_vst(n2, dst, 32 * 10);
    __lsx_vst(m3, dst, 32 * 12);
    __lsx_vst(n3, dst, 32 * 14);

    LSX_TRANSPOSE8x8_H(m4, n4, m5, n5, m6, n6, m7, n7,
                       m4, n4, m5, n5, m6, n6, m7, n7);

    __lsx_vst(m4, dst, 16);
    __lsx_vst(n4, dst, 16 + 32 * 2);
    __lsx_vst(m5, dst, 16 + 32 * 4);
    __lsx_vst(n5, dst, 16 + 32 * 6);
    __lsx_vst(m6, dst, 16 + 32 * 8);
    __lsx_vst(n6, dst, 16 + 32 * 10);
    __lsx_vst(m7, dst, 16 + 32 * 12);
    __lsx_vst(n7, dst, 16 + 32 * 14);

    /* 3rd & 4th 8x8 */
    DUP4_ARG2(__lsx_vld, tmp_buf, 16 * 16, tmp_buf, 16 * 17,
              tmp_buf, 16 * 18, tmp_buf, 16 * 19, m0, n0, m1, n1);
    DUP4_ARG2(__lsx_vld, tmp_buf, 16 * 20, tmp_buf, 16 * 21,
              tmp_buf, 16 * 22, tmp_buf, 16 * 23, m2, n2, m3, n3);

    DUP4_ARG2(__lsx_vld, tmp_buf, 16 * 24, tmp_buf, 16 * 25,
              tmp_buf, 16 * 26, tmp_buf, 16 * 27, m4, n4, m5, n5);
    DUP4_ARG2(__lsx_vld, tmp_buf, 16 * 28, tmp_buf, 16 * 29,
              tmp_buf, 16 * 30, tmp_buf, 16 * 31, m6, n6, m7, n7);

    LSX_TRANSPOSE8x8_H(m0, n0, m1, n1, m2, n2, m3, n3,
                       m0, n0, m1, n1, m2, n2, m3, n3);

    __lsx_vst(m0, dst, 32);
    __lsx_vst(n0, dst, 32 + 32 * 2);
    __lsx_vst(m1, dst, 32 + 32 * 4);
    __lsx_vst(n1, dst, 32 + 32 * 6);
    __lsx_vst(m2, dst, 32 + 32 * 8);
    __lsx_vst(n2, dst, 32 + 32 * 10);
    __lsx_vst(m3, dst, 32 + 32 * 12);
    __lsx_vst(n3, dst, 32 + 32 * 14);

    LSX_TRANSPOSE8x8_H(m4, n4, m5, n5, m6, n6, m7, n7,
                       m4, n4, m5, n5, m6, n6, m7, n7);

    __lsx_vst(m4, dst, 48);
    __lsx_vst(n4, dst, 48 + 32 * 2);
    __lsx_vst(m5, dst, 48 + 32 * 4);
    __lsx_vst(n5, dst, 48 + 32 * 6);
    __lsx_vst(m6, dst, 48 + 32 * 8);
    __lsx_vst(n6, dst, 48 + 32 * 10);
    __lsx_vst(m7, dst, 48 + 32 * 12);
    __lsx_vst(n7, dst, 48 + 32 * 14);
}

static void vp9_idct8x32_column_even_process_store(int16_t *tmp_buf,
                                                   int16_t *tmp_eve_buf)
{
    __m128i vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7;
    __m128i stp0, stp1, stp2, stp3, stp4, stp5, stp6, stp7;
    __m128i zero = __lsx_vldi(0);

    /* Even stage 1 */
    DUP4_ARG2(__lsx_vld, tmp_buf, 0, tmp_buf, 32 * 8,
              tmp_buf, 32 * 16, tmp_buf, 32 * 24, reg0, reg1, reg2, reg3);
    DUP4_ARG2(__lsx_vld, tmp_buf, 32 * 32, tmp_buf, 32 * 40,
              tmp_buf, 32 * 48, tmp_buf, 32 * 56, reg4, reg5, reg6, reg7);

    __lsx_vst(zero, tmp_buf, 0);
    __lsx_vst(zero, tmp_buf, 32 * 8);
    __lsx_vst(zero, tmp_buf, 32 * 16);
    __lsx_vst(zero, tmp_buf, 32 * 24);
    __lsx_vst(zero, tmp_buf, 32 * 32);
    __lsx_vst(zero, tmp_buf, 32 * 40);
    __lsx_vst(zero, tmp_buf, 32 * 48);
    __lsx_vst(zero, tmp_buf, 32 * 56);

    tmp_buf += (2 * 32);

    VP9_DOTP_CONST_PAIR(reg1, reg7, cospi_28_64, cospi_4_64, reg1, reg7);
    VP9_DOTP_CONST_PAIR(reg5, reg3, cospi_12_64, cospi_20_64, reg5, reg3);
    LSX_BUTTERFLY_4_H(reg1, reg7, reg3, reg5, vec1, vec3, vec2, vec0);
    VP9_DOTP_CONST_PAIR(vec2, vec0, cospi_16_64, cospi_16_64, loc2, loc3);

    loc1 = vec3;
    loc0 = vec1;

    VP9_DOTP_CONST_PAIR(reg0, reg4, cospi_16_64, cospi_16_64, reg0, reg4);
    VP9_DOTP_CONST_PAIR(reg2, reg6, cospi_24_64, cospi_8_64, reg2, reg6);
    LSX_BUTTERFLY_4_H(reg4, reg0, reg2, reg6, vec1, vec3, vec2, vec0);
    LSX_BUTTERFLY_4_H(vec0, vec1, loc1, loc0, stp3, stp0, stp7, stp4);
    LSX_BUTTERFLY_4_H(vec2, vec3, loc3, loc2, stp2, stp1, stp6, stp5);

    /* Even stage 2 */
    /* Load 8 */
    DUP4_ARG2(__lsx_vld, tmp_buf, 0, tmp_buf, 32 * 8,
              tmp_buf, 32 * 16, tmp_buf, 32 * 24, reg0, reg1, reg2, reg3);
    DUP4_ARG2(__lsx_vld, tmp_buf, 32 * 32, tmp_buf, 32 * 40,
              tmp_buf, 32 * 48, tmp_buf, 32 * 56, reg4, reg5, reg6, reg7);

    __lsx_vst(zero, tmp_buf, 0);
    __lsx_vst(zero, tmp_buf, 32 * 8);
    __lsx_vst(zero, tmp_buf, 32 * 16);
    __lsx_vst(zero, tmp_buf, 32 * 24);
    __lsx_vst(zero, tmp_buf, 32 * 32);
    __lsx_vst(zero, tmp_buf, 32 * 40);
    __lsx_vst(zero, tmp_buf, 32 * 48);
    __lsx_vst(zero, tmp_buf, 32 * 56);

    VP9_DOTP_CONST_PAIR(reg0, reg7, cospi_30_64, cospi_2_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(reg4, reg3, cospi_14_64, cospi_18_64, reg4, reg3);
    VP9_DOTP_CONST_PAIR(reg2, reg5, cospi_22_64, cospi_10_64, reg2, reg5);
    VP9_DOTP_CONST_PAIR(reg6, reg1, cospi_6_64, cospi_26_64, reg6, reg1);

    vec0 = __lsx_vadd_h(reg0, reg4);
    reg0 = __lsx_vsub_h(reg0, reg4);
    reg4 = __lsx_vadd_h(reg6, reg2);
    reg6 = __lsx_vsub_h(reg6, reg2);
    reg2 = __lsx_vadd_h(reg1, reg5);
    reg1 = __lsx_vsub_h(reg1, reg5);
    reg5 = __lsx_vadd_h(reg7, reg3);
    reg7 = __lsx_vsub_h(reg7, reg3);
    reg3 = vec0;

    vec1 = reg2;
    reg2 = __lsx_vadd_h(reg3, reg4);
    reg3 = __lsx_vsub_h(reg3, reg4);
    reg4 = __lsx_vsub_h(reg5, vec1);
    reg5 = __lsx_vadd_h(reg5, vec1);

    VP9_DOTP_CONST_PAIR(reg7, reg0, cospi_24_64, cospi_8_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(__lsx_vneg_h(reg6), reg1, cospi_24_64, cospi_8_64,
                        reg6, reg1);

    vec0 = __lsx_vsub_h(reg0, reg6);
    reg0 = __lsx_vadd_h(reg0, reg6);
    vec1 = __lsx_vsub_h(reg7, reg1);
    reg7 = __lsx_vadd_h(reg7, reg1);

    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, reg6, reg1);
    VP9_DOTP_CONST_PAIR(reg4, reg3, cospi_16_64, cospi_16_64, reg3, reg4);

    /* Even stage 3 : Dependency on Even stage 1 & Even stage 2 */
    /* Store 8 */
    LSX_BUTTERFLY_4_H(stp0, stp1, reg7, reg5, loc1, loc3, loc2, loc0);
    __lsx_vst(loc1, tmp_eve_buf, 0);
    __lsx_vst(loc3, tmp_eve_buf, 16);
    __lsx_vst(loc2, tmp_eve_buf, 14 * 16);
    __lsx_vst(loc0, tmp_eve_buf, 14 * 16 + 16);
    LSX_BUTTERFLY_4_H(stp2, stp3, reg4, reg1, loc1, loc3, loc2, loc0);
    __lsx_vst(loc1, tmp_eve_buf, 2 * 16);
    __lsx_vst(loc3, tmp_eve_buf, 2 * 16 + 16);
    __lsx_vst(loc2, tmp_eve_buf, 12 * 16);
    __lsx_vst(loc0, tmp_eve_buf, 12 * 16 + 16);

    /* Store 8 */
    LSX_BUTTERFLY_4_H(stp4, stp5, reg6, reg3, loc1, loc3, loc2, loc0);
    __lsx_vst(loc1, tmp_eve_buf, 4 * 16);
    __lsx_vst(loc3, tmp_eve_buf, 4 * 16 + 16);
    __lsx_vst(loc2, tmp_eve_buf, 10 * 16);
    __lsx_vst(loc0, tmp_eve_buf, 10 * 16 + 16);

    LSX_BUTTERFLY_4_H(stp6, stp7, reg2, reg0, loc1, loc3, loc2, loc0);
    __lsx_vst(loc1, tmp_eve_buf, 6 * 16);
    __lsx_vst(loc3, tmp_eve_buf, 6 * 16 + 16);
    __lsx_vst(loc2, tmp_eve_buf, 8 * 16);
    __lsx_vst(loc0, tmp_eve_buf, 8 * 16 + 16);
}

static void vp9_idct8x32_column_odd_process_store(int16_t *tmp_buf,
                                                  int16_t *tmp_odd_buf)
{
    __m128i vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7;
    __m128i zero = __lsx_vldi(0);

    /* Odd stage 1 */
    reg0 = __lsx_vld(tmp_buf, 64);
    reg1 = __lsx_vld(tmp_buf, 7 * 64);
    reg2 = __lsx_vld(tmp_buf, 9 * 64);
    reg3 = __lsx_vld(tmp_buf, 15 * 64);
    reg4 = __lsx_vld(tmp_buf, 17 * 64);
    reg5 = __lsx_vld(tmp_buf, 23 * 64);
    reg6 = __lsx_vld(tmp_buf, 25 * 64);
    reg7 = __lsx_vld(tmp_buf, 31 * 64);

    __lsx_vst(zero, tmp_buf, 64);
    __lsx_vst(zero, tmp_buf, 7 * 64);
    __lsx_vst(zero, tmp_buf, 9 * 64);
    __lsx_vst(zero, tmp_buf, 15 * 64);
    __lsx_vst(zero, tmp_buf, 17 * 64);
    __lsx_vst(zero, tmp_buf, 23 * 64);
    __lsx_vst(zero, tmp_buf, 25 * 64);
    __lsx_vst(zero, tmp_buf, 31 * 64);

    VP9_DOTP_CONST_PAIR(reg0, reg7, cospi_31_64, cospi_1_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(reg4, reg3, cospi_15_64, cospi_17_64, reg3, reg4);
    VP9_DOTP_CONST_PAIR(reg2, reg5, cospi_23_64, cospi_9_64, reg2, reg5);
    VP9_DOTP_CONST_PAIR(reg6, reg1, cospi_7_64, cospi_25_64, reg1, reg6);

    vec0 = __lsx_vadd_h(reg0, reg3);
    reg0 = __lsx_vsub_h(reg0, reg3);
    reg3 = __lsx_vadd_h(reg7, reg4);
    reg7 = __lsx_vsub_h(reg7, reg4);
    reg4 = __lsx_vadd_h(reg1, reg2);
    reg1 = __lsx_vsub_h(reg1, reg2);
    reg2 = __lsx_vadd_h(reg6, reg5);
    reg6 = __lsx_vsub_h(reg6, reg5);
    reg5 = vec0;

    /* 4 Stores */
    DUP2_ARG2(__lsx_vadd_h, reg5, reg4, reg3, reg2, vec0, vec1);
    __lsx_vst(vec0, tmp_odd_buf, 4 * 16);
    __lsx_vst(vec1, tmp_odd_buf, 4 * 16 + 16);
    DUP2_ARG2(__lsx_vsub_h, reg5, reg4, reg3, reg2, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_24_64, cospi_8_64, vec0, vec1);
    __lsx_vst(vec0, tmp_odd_buf, 0);
    __lsx_vst(vec1, tmp_odd_buf, 16);

    /* 4 Stores */
    VP9_DOTP_CONST_PAIR(reg7, reg0, cospi_28_64, cospi_4_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(reg6, reg1, -cospi_4_64, cospi_28_64, reg1, reg6);
    LSX_BUTTERFLY_4_H(reg0, reg7, reg6, reg1, vec0, vec1, vec2, vec3);
    __lsx_vst(vec0, tmp_odd_buf, 6 * 16);
    __lsx_vst(vec1, tmp_odd_buf, 6 * 16 + 16);
    VP9_DOTP_CONST_PAIR(vec2, vec3, cospi_24_64, cospi_8_64, vec2, vec3);
    __lsx_vst(vec2, tmp_odd_buf, 2 * 16);
    __lsx_vst(vec3, tmp_odd_buf, 2 * 16 + 16);

    /* Odd stage 2 */
    /* 8 loads */
    reg0 = __lsx_vld(tmp_buf, 3 * 64);
    reg1 = __lsx_vld(tmp_buf, 5 * 64);
    reg2 = __lsx_vld(tmp_buf, 11 * 64);
    reg3 = __lsx_vld(tmp_buf, 13 * 64);
    reg4 = __lsx_vld(tmp_buf, 19 * 64);
    reg5 = __lsx_vld(tmp_buf, 21 * 64);
    reg6 = __lsx_vld(tmp_buf, 27 * 64);
    reg7 = __lsx_vld(tmp_buf, 29 * 64);

    __lsx_vst(zero, tmp_buf, 3 * 64);
    __lsx_vst(zero, tmp_buf, 5 * 64);
    __lsx_vst(zero, tmp_buf, 11 * 64);
    __lsx_vst(zero, tmp_buf, 13 * 64);
    __lsx_vst(zero, tmp_buf, 19 * 64);
    __lsx_vst(zero, tmp_buf, 21 * 64);
    __lsx_vst(zero, tmp_buf, 27 * 64);
    __lsx_vst(zero, tmp_buf, 29 * 64);

    VP9_DOTP_CONST_PAIR(reg1, reg6, cospi_27_64, cospi_5_64, reg1, reg6);
    VP9_DOTP_CONST_PAIR(reg5, reg2, cospi_11_64, cospi_21_64, reg2, reg5);
    VP9_DOTP_CONST_PAIR(reg3, reg4, cospi_19_64, cospi_13_64, reg3, reg4);
    VP9_DOTP_CONST_PAIR(reg7, reg0, cospi_3_64, cospi_29_64, reg0, reg7);

    /* 4 Stores */
    DUP4_ARG2(__lsx_vsub_h,reg1, reg2, reg6, reg5, reg0, reg3, reg7, reg4,
              vec0, vec1, vec2, vec3);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_12_64, cospi_20_64, loc0, loc1);
    VP9_DOTP_CONST_PAIR(vec3, vec2, -cospi_20_64, cospi_12_64, loc2, loc3);
    LSX_BUTTERFLY_4_H(loc2, loc3, loc1, loc0, vec0, vec1, vec3, vec2);
    __lsx_vst(vec0, tmp_odd_buf, 12 * 16);
    __lsx_vst(vec1, tmp_odd_buf, 12 * 16 + 3 * 16);
    VP9_DOTP_CONST_PAIR(vec3, vec2, -cospi_8_64, cospi_24_64, vec0, vec1);
    __lsx_vst(vec0, tmp_odd_buf, 10 * 16);
    __lsx_vst(vec1, tmp_odd_buf, 10 * 16 + 16);

    /* 4 Stores */
    DUP4_ARG2(__lsx_vadd_h, reg0, reg3, reg1, reg2, reg5, reg6, reg4, reg7,
              vec0, vec1, vec2, vec3);
    LSX_BUTTERFLY_4_H(vec0, vec3, vec2, vec1, reg0, reg1, reg3, reg2);
    __lsx_vst(reg0, tmp_odd_buf, 13 * 16);
    __lsx_vst(reg1, tmp_odd_buf, 13 * 16 + 16);
    VP9_DOTP_CONST_PAIR(reg3, reg2, -cospi_8_64, cospi_24_64,
                        reg0, reg1);
    __lsx_vst(reg0, tmp_odd_buf, 8 * 16);
    __lsx_vst(reg1, tmp_odd_buf, 8 * 16 + 16);

    /* Odd stage 3 : Dependency on Odd stage 1 & Odd stage 2 */
    /* Load 8 & Store 8 */
    DUP4_ARG2(__lsx_vld, tmp_odd_buf, 0, tmp_odd_buf, 16,
              tmp_odd_buf, 32, tmp_odd_buf, 48, reg0, reg1, reg2, reg3);
    DUP4_ARG2(__lsx_vld, tmp_odd_buf, 8 * 16, tmp_odd_buf, 8 * 16 + 16,
              tmp_odd_buf, 8 * 16 + 32, tmp_odd_buf, 8 * 16 + 48,
              reg4, reg5, reg6, reg7);

    DUP4_ARG2(__lsx_vadd_h, reg0, reg4, reg1, reg5, reg2, reg6, reg3, reg7,
                  loc0, loc1, loc2, loc3);
    __lsx_vst(loc0, tmp_odd_buf, 0);
    __lsx_vst(loc1, tmp_odd_buf, 16);
    __lsx_vst(loc2, tmp_odd_buf, 32);
    __lsx_vst(loc3, tmp_odd_buf, 48);
    DUP2_ARG2(__lsx_vsub_h, reg0, reg4, reg1, reg5, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc0, loc1);

    DUP2_ARG2(__lsx_vsub_h, reg2, reg6, reg3, reg7, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc2, loc3);
    __lsx_vst(loc0, tmp_odd_buf, 8 * 16);
    __lsx_vst(loc1, tmp_odd_buf, 8 * 16 + 16);
    __lsx_vst(loc2, tmp_odd_buf, 8 * 16 + 32);
    __lsx_vst(loc3, tmp_odd_buf, 8 * 16 + 48);

    /* Load 8 & Store 8 */
    DUP4_ARG2(__lsx_vld, tmp_odd_buf, 4 * 16, tmp_odd_buf, 4 * 16 + 16,
              tmp_odd_buf, 4 * 16 + 32, tmp_odd_buf, 4 * 16 + 48,
              reg1, reg2, reg0, reg3);
    DUP4_ARG2(__lsx_vld, tmp_odd_buf, 12 * 16, tmp_odd_buf, 12 * 16 + 16,
              tmp_odd_buf, 12 * 16 + 32, tmp_odd_buf, 12 * 16 + 48,
              reg4, reg5, reg6, reg7);

    DUP4_ARG2(__lsx_vadd_h, reg0, reg4, reg1, reg5, reg2, reg6, reg3, reg7,
              loc0, loc1, loc2, loc3);
    __lsx_vst(loc0, tmp_odd_buf, 4 * 16);
    __lsx_vst(loc1, tmp_odd_buf, 4 * 16 + 16);
    __lsx_vst(loc2, tmp_odd_buf, 4 * 16 + 32);
    __lsx_vst(loc3, tmp_odd_buf, 4 * 16 + 48);

    DUP2_ARG2(__lsx_vsub_h, reg0, reg4, reg3, reg7, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc0, loc1);

    DUP2_ARG2(__lsx_vsub_h, reg1, reg5, reg2, reg6, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc2, loc3);
    __lsx_vst(loc0, tmp_odd_buf, 12 * 16);
    __lsx_vst(loc1, tmp_odd_buf, 12 * 16 + 16);
    __lsx_vst(loc2, tmp_odd_buf, 12 * 16 + 32);
    __lsx_vst(loc3, tmp_odd_buf, 12 * 16 + 48);
}

static void vp9_idct8x32_column_butterfly_addblk(int16_t *tmp_eve_buf,
                                                 int16_t *tmp_odd_buf,
                                                 uint8_t *dst,
                                                 int32_t dst_stride)
{
    __m128i vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    __m128i m0, m1, m2, m3, m4, m5, m6, m7, n0, n1, n2, n3, n4, n5, n6, n7;

    /* FINAL BUTTERFLY : Dependency on Even & Odd */
    vec0 = __lsx_vld(tmp_odd_buf, 0);
    vec1 = __lsx_vld(tmp_odd_buf, 9 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 14 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 6 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 0);
    loc1 = __lsx_vld(tmp_eve_buf, 8 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 4 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 12 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              m0, m4, m2, m6);
    DUP4_ARG2(__lsx_vsrari_h, m0, 6, m2, 6, m4, 6, m6, 6, m0, m2, m4, m6);
    VP9_ADDBLK_ST8x4_UB(dst, (4 * dst_stride), m0, m2, m4, m6);

    DUP4_ARG2(__lsx_vsub_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              m6, m2, m4, m0);
    DUP4_ARG2(__lsx_vsrari_h, m0, 6, m2, 6, m4, 6, m6, 6, m0, m2, m4, m6);
    VP9_ADDBLK_ST8x4_UB((dst + 19 * dst_stride), (4 * dst_stride),
                        m0, m2, m4, m6);

    /* Load 8 & Store 8 */
    vec0 = __lsx_vld(tmp_odd_buf, 4 * 16);
    vec1 = __lsx_vld(tmp_odd_buf, 13 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 10 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 3 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 2 * 16);
    loc1 = __lsx_vld(tmp_eve_buf, 10 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 6 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 14 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
               m1, m5, m3, m7);
    DUP4_ARG2(__lsx_vsrari_h, m1, 6, m3, 6, m5, 6, m7, 6, m1, m3, m5, m7);
    VP9_ADDBLK_ST8x4_UB((dst + 2 * dst_stride), (4 * dst_stride),
                        m1, m3, m5, m7);

    DUP4_ARG2(__lsx_vsub_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              m7, m3, m5, m1);
    DUP4_ARG2(__lsx_vsrari_h, m1, 6, m3, 6, m5, 6, m7, 6, m1, m3, m5, m7);
    VP9_ADDBLK_ST8x4_UB((dst + 17 * dst_stride), (4 * dst_stride),
                        m1, m3, m5, m7);

    /* Load 8 & Store 8 */
    vec0 = __lsx_vld(tmp_odd_buf, 2 * 16);
    vec1 = __lsx_vld(tmp_odd_buf, 11 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 12 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 7 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 1 * 16);
    loc1 = __lsx_vld(tmp_eve_buf, 9 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 5 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 13 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              n0, n4, n2, n6);
    DUP4_ARG2(__lsx_vsrari_h, n0, 6, n2, 6, n4, 6, n6, 6, n0, n2, n4, n6);
    VP9_ADDBLK_ST8x4_UB((dst + 1 * dst_stride), (4 * dst_stride),
                        n0, n2, n4, n6);
    DUP4_ARG2(__lsx_vsub_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              n6, n2, n4, n0);
    DUP4_ARG2(__lsx_vsrari_h, n0, 6, n2, 6, n4, 6, n6, 6, n0, n2, n4, n6);
    VP9_ADDBLK_ST8x4_UB((dst + 18 * dst_stride), (4 * dst_stride),
                        n0, n2, n4, n6);

    /* Load 8 & Store 8 */
    vec0 = __lsx_vld(tmp_odd_buf, 5 * 16);
    vec1 = __lsx_vld(tmp_odd_buf, 15 * 16);
    vec2 = __lsx_vld(tmp_odd_buf, 8 * 16);
    vec3 = __lsx_vld(tmp_odd_buf, 1 * 16);
    loc0 = __lsx_vld(tmp_eve_buf, 3 * 16);
    loc1 = __lsx_vld(tmp_eve_buf, 11 * 16);
    loc2 = __lsx_vld(tmp_eve_buf, 7 * 16);
    loc3 = __lsx_vld(tmp_eve_buf, 15 * 16);

    DUP4_ARG2(__lsx_vadd_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              n1, n5, n3, n7);
    DUP4_ARG2(__lsx_vsrari_h, n1, 6, n3, 6, n5, 6, n7, 6, n1, n3, n5, n7);
    VP9_ADDBLK_ST8x4_UB((dst + 3 * dst_stride), (4 * dst_stride),
                        n1, n3, n5, n7);
    DUP4_ARG2(__lsx_vsub_h, loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0,
              n7, n3, n5, n1);
    DUP4_ARG2(__lsx_vsrari_h, n1, 6, n3, 6, n5, 6, n7, 6, n1, n3, n5, n7);
    VP9_ADDBLK_ST8x4_UB((dst + 16 * dst_stride), (4 * dst_stride),
                        n1, n3, n5, n7);
}

static void vp9_idct8x32_1d_columns_addblk_lsx(int16_t *input, uint8_t *dst,
                                               int32_t dst_stride)
{
    int16_t tmp_odd_buf[16 * 8] ALLOC_ALIGNED(16);
    int16_t tmp_eve_buf[16 * 8] ALLOC_ALIGNED(16);

    vp9_idct8x32_column_even_process_store(input, &tmp_eve_buf[0]);
    vp9_idct8x32_column_odd_process_store(input, &tmp_odd_buf[0]);
    vp9_idct8x32_column_butterfly_addblk(&tmp_eve_buf[0], &tmp_odd_buf[0],
                                         dst, dst_stride);
}

static void vp9_idct8x32_1d_columns_lsx(int16_t *input, int16_t *output,
                                        int16_t *tmp_buf)
{
    int16_t tmp_odd_buf[16 * 8] ALLOC_ALIGNED(16);
    int16_t tmp_eve_buf[16 * 8] ALLOC_ALIGNED(16);

    vp9_idct8x32_column_even_process_store(input, &tmp_eve_buf[0]);
    vp9_idct8x32_column_odd_process_store(input, &tmp_odd_buf[0]);
    vp9_idct_butterfly_transpose_store(tmp_buf, &tmp_eve_buf[0],
                                       &tmp_odd_buf[0], output);
}

static void vp9_idct32x32_1_add_lsx(int16_t *input, uint8_t *dst,
                                    int32_t dst_stride)
{
    int32_t i;
    int16_t out;
    uint8_t *dst_tmp = dst + dst_stride;
    __m128i zero = __lsx_vldi(0);
    __m128i dst0, dst1, dst2, dst3, tmp0, tmp1, tmp2, tmp3;
    __m128i res0, res1, res2, res3, res4, res5, res6, res7, vec;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO(out, 6);
    input[0] = 0;

    vec = __lsx_vreplgr2vr_h(out);

    for (i = 16; i--;) {
        DUP2_ARG2(__lsx_vld, dst, 0, dst, 16, dst0, dst1);
        DUP2_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst2, dst3);

        DUP4_ARG2(__lsx_vilvl_b, zero, dst0, zero, dst1, zero, dst2, zero, dst3,
                  res0, res1, res2, res3);
        DUP4_ARG2(__lsx_vilvh_b, zero, dst0, zero, dst1, zero, dst2, zero, dst3,
                  res4, res5, res6, res7);
        DUP4_ARG2(__lsx_vadd_h, res0, vec, res1, vec, res2, vec, res3, vec,
                  res0, res1, res2, res3);
        DUP4_ARG2(__lsx_vadd_h, res4, vec, res5, vec, res6, vec, res7, vec,
                  res4, res5, res6, res7);
        DUP4_ARG1(__lsx_vclip255_h, res0, res1, res2, res3, res0, res1, res2, res3);
        DUP4_ARG1(__lsx_vclip255_h, res4, res5, res6, res7, res4, res5, res6, res7);
        DUP4_ARG2(__lsx_vpickev_b, res4, res0, res5, res1, res6, res2, res7, res3,
                  tmp0, tmp1, tmp2, tmp3);

        __lsx_vst(tmp0, dst, 0);
        __lsx_vst(tmp1, dst, 16);
        __lsx_vst(tmp2, dst_tmp, 0);
        __lsx_vst(tmp3, dst_tmp, 16);
        dst = dst_tmp + dst_stride;
        dst_tmp = dst + dst_stride;
    }
}

static void vp9_idct32x32_34_colcol_addblk_lsx(int16_t *input, uint8_t *dst,
                                               int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[32 * 32] ALLOC_ALIGNED(16);
    int16_t *out_ptr = out_arr;
    int16_t tmp_buf[8 * 32] ALLOC_ALIGNED(16);
    __m128i zero = __lsx_vldi(0);

    for (i = 16; i--;) {
        __lsx_vst(zero, out_ptr, 0);
        __lsx_vst(zero, out_ptr, 16);
        __lsx_vst(zero, out_ptr, 32);
        __lsx_vst(zero, out_ptr, 48);
        __lsx_vst(zero, out_ptr, 64);
        __lsx_vst(zero, out_ptr, 80);
        __lsx_vst(zero, out_ptr, 96);
        __lsx_vst(zero, out_ptr, 112);
        out_ptr += 64;
    }

    out_ptr = out_arr;

    /* process 8*32 block */
    vp9_idct8x32_1d_columns_lsx(input, out_ptr, &tmp_buf[0]);

    /* transform columns */
    for (i = 0; i < 4; i++) {
        /* process 8*32 block */
        vp9_idct8x32_1d_columns_addblk_lsx((out_ptr + (i << 3)),
                                           (dst + (i << 3)), dst_stride);
    }
}

static void vp9_idct32x32_colcol_addblk_lsx(int16_t *input, uint8_t *dst,
                                            int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[32 * 32] ALLOC_ALIGNED(16);
    int16_t *out_ptr = out_arr;
    int16_t tmp_buf[8 * 32] ALLOC_ALIGNED(16);

    /* transform rows */
    for (i = 0; i < 4; i++) {
        /* process 8*32 block */
        vp9_idct8x32_1d_columns_lsx((input + (i << 3)), (out_ptr + (i << 8)),
                                    &tmp_buf[0]);
    }

    /* transform columns */
    for (i = 0; i < 4; i++) {
        /* process 8*32 block */
        vp9_idct8x32_1d_columns_addblk_lsx((out_ptr + (i << 3)),
                                           (dst + (i << 3)), dst_stride);
    }
}

void ff_idct_idct_8x8_add_lsx(uint8_t *dst, ptrdiff_t stride,
                              int16_t *block, int eob)
{
    if (eob == 1) {
        vp9_idct8x8_1_add_lsx(block, dst, stride);
    }
    else if (eob <= 12) {
        vp9_idct8x8_12_colcol_addblk_lsx(block, dst, stride);
    }
    else {
        vp9_idct8x8_colcol_addblk_lsx(block, dst, stride);
    }
}

void ff_idct_idct_16x16_add_lsx(uint8_t *dst, ptrdiff_t stride,
                                int16_t *block, int eob)
{
    if (eob == 1) {
        /* DC only DCT coefficient. */
        vp9_idct16x16_1_add_lsx(block, dst, stride);
    }
    else if (eob <= 10) {
        vp9_idct16x16_10_colcol_addblk_lsx(block, dst, stride);
    }
    else {
        vp9_idct16x16_colcol_addblk_lsx(block, dst, stride);
    }
}

void ff_idct_idct_32x32_add_lsx(uint8_t *dst, ptrdiff_t stride,
                                int16_t *block, int eob)
{
    if (eob == 1) {
        vp9_idct32x32_1_add_lsx(block, dst, stride);
    }
    else if (eob <= 34) {
        vp9_idct32x32_34_colcol_addblk_lsx(block, dst, stride);
    }
    else {
        vp9_idct32x32_colcol_addblk_lsx(block, dst, stride);
    }
}
