/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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

#include <string.h>
#include "libavcodec/vp9dsp.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "vp9dsp_mips.h"

#define VP9_DCT_CONST_BITS   14
#define ROUND_POWER_OF_TWO(value, n)  (((value) + (1 << ((n) - 1))) >> (n))

static const int32_t cospi_1_64 = 16364;
static const int32_t cospi_2_64 = 16305;
static const int32_t cospi_3_64 = 16207;
static const int32_t cospi_4_64 = 16069;
static const int32_t cospi_5_64 = 15893;
static const int32_t cospi_6_64 = 15679;
static const int32_t cospi_7_64 = 15426;
static const int32_t cospi_8_64 = 15137;
static const int32_t cospi_9_64 = 14811;
static const int32_t cospi_10_64 = 14449;
static const int32_t cospi_11_64 = 14053;
static const int32_t cospi_12_64 = 13623;
static const int32_t cospi_13_64 = 13160;
static const int32_t cospi_14_64 = 12665;
static const int32_t cospi_15_64 = 12140;
static const int32_t cospi_16_64 = 11585;
static const int32_t cospi_17_64 = 11003;
static const int32_t cospi_18_64 = 10394;
static const int32_t cospi_19_64 = 9760;
static const int32_t cospi_20_64 = 9102;
static const int32_t cospi_21_64 = 8423;
static const int32_t cospi_22_64 = 7723;
static const int32_t cospi_23_64 = 7005;
static const int32_t cospi_24_64 = 6270;
static const int32_t cospi_25_64 = 5520;
static const int32_t cospi_26_64 = 4756;
static const int32_t cospi_27_64 = 3981;
static const int32_t cospi_28_64 = 3196;
static const int32_t cospi_29_64 = 2404;
static const int32_t cospi_30_64 = 1606;
static const int32_t cospi_31_64 = 804;

//  16384 * sqrt(2) * sin(kPi/9) * 2 / 3
static const int32_t sinpi_1_9 = 5283;
static const int32_t sinpi_2_9 = 9929;
static const int32_t sinpi_3_9 = 13377;
static const int32_t sinpi_4_9 = 15212;

#define VP9_DOTP_CONST_PAIR(reg0, reg1, cnst0, cnst1, out0, out1)  \
{                                                                  \
    v8i16 k0_m = __msa_fill_h(cnst0);                              \
    v4i32 s0_m, s1_m, s2_m, s3_m;                                  \
                                                                   \
    s0_m = (v4i32) __msa_fill_h(cnst1);                            \
    k0_m = __msa_ilvev_h((v8i16) s0_m, k0_m);                      \
                                                                   \
    ILVRL_H2_SW((-reg1), reg0, s1_m, s0_m);                        \
    ILVRL_H2_SW(reg0, reg1, s3_m, s2_m);                           \
    DOTP_SH2_SW(s1_m, s0_m, k0_m, k0_m, s1_m, s0_m);               \
    SRARI_W2_SW(s1_m, s0_m, VP9_DCT_CONST_BITS);                   \
    out0 = __msa_pckev_h((v8i16) s0_m, (v8i16) s1_m);              \
                                                                   \
    DOTP_SH2_SW(s3_m, s2_m, k0_m, k0_m, s1_m, s0_m);               \
    SRARI_W2_SW(s1_m, s0_m, VP9_DCT_CONST_BITS);                   \
    out1 = __msa_pckev_h((v8i16) s0_m, (v8i16) s1_m);              \
}

#define VP9_DOT_ADD_SUB_SRARI_PCK(in0, in1, in2, in3, in4, in5, in6, in7,  \
                                      dst0, dst1, dst2, dst3)              \
{                                                                          \
    v4i32 tp0_m, tp1_m, tp2_m, tp3_m, tp4_m;                               \
    v4i32 tp5_m, tp6_m, tp7_m, tp8_m, tp9_m;                               \
                                                                           \
    DOTP_SH4_SW(in0, in1, in0, in1, in4, in4, in5, in5,                    \
                tp0_m, tp2_m, tp3_m, tp4_m);                               \
    DOTP_SH4_SW(in2, in3, in2, in3, in6, in6, in7, in7,                    \
                tp5_m, tp6_m, tp7_m, tp8_m);                               \
    BUTTERFLY_4(tp0_m, tp3_m, tp7_m, tp5_m, tp1_m, tp9_m, tp7_m, tp5_m);   \
    BUTTERFLY_4(tp2_m, tp4_m, tp8_m, tp6_m, tp3_m, tp0_m, tp4_m, tp2_m);   \
    SRARI_W4_SW(tp1_m, tp9_m, tp7_m, tp5_m, VP9_DCT_CONST_BITS);           \
    SRARI_W4_SW(tp3_m, tp0_m, tp4_m, tp2_m, VP9_DCT_CONST_BITS);           \
    PCKEV_H4_SH(tp1_m, tp3_m, tp9_m, tp0_m, tp7_m, tp4_m, tp5_m, tp2_m,    \
                dst0, dst1, dst2, dst3);                                   \
}

#define VP9_DOT_SHIFT_RIGHT_PCK_H(in0, in1, in2)          \
( {                                                       \
    v8i16 dst_m;                                          \
    v4i32 tp0_m, tp1_m;                                   \
                                                          \
    DOTP_SH2_SW(in0, in1, in2, in2, tp1_m, tp0_m);        \
    SRARI_W2_SW(tp1_m, tp0_m, VP9_DCT_CONST_BITS);        \
    dst_m = __msa_pckev_h((v8i16) tp1_m, (v8i16) tp0_m);  \
                                                          \
    dst_m;                                                \
} )

#define VP9_ADST8(in0, in1, in2, in3, in4, in5, in6, in7,                 \
                  out0, out1, out2, out3, out4, out5, out6, out7)         \
{                                                                         \
    v8i16 cnst0_m, cnst1_m, cnst2_m, cnst3_m, cnst4_m;                    \
    v8i16 vec0_m, vec1_m, vec2_m, vec3_m, s0_m, s1_m;                     \
    v8i16 coeff0_m = { cospi_2_64, cospi_6_64, cospi_10_64, cospi_14_64,  \
        cospi_18_64, cospi_22_64, cospi_26_64, cospi_30_64 };             \
    v8i16 coeff1_m = { cospi_8_64, -cospi_8_64, cospi_16_64,              \
        -cospi_16_64, cospi_24_64, -cospi_24_64, 0, 0 };                  \
                                                                          \
    SPLATI_H2_SH(coeff0_m, 0, 7, cnst0_m, cnst1_m);                       \
    cnst2_m = -cnst0_m;                                                   \
    ILVEV_H2_SH(cnst0_m, cnst1_m, cnst1_m, cnst2_m, cnst0_m, cnst1_m);    \
    SPLATI_H2_SH(coeff0_m, 4, 3, cnst2_m, cnst3_m);                       \
    cnst4_m = -cnst2_m;                                                   \
    ILVEV_H2_SH(cnst2_m, cnst3_m, cnst3_m, cnst4_m, cnst2_m, cnst3_m);    \
                                                                          \
    ILVRL_H2_SH(in0, in7, vec1_m, vec0_m);                                \
    ILVRL_H2_SH(in4, in3, vec3_m, vec2_m);                                \
    VP9_DOT_ADD_SUB_SRARI_PCK(vec0_m, vec1_m, vec2_m, vec3_m, cnst0_m,    \
                              cnst1_m, cnst2_m, cnst3_m, in7, in0,        \
                              in4, in3);                                  \
                                                                          \
    SPLATI_H2_SH(coeff0_m, 2, 5, cnst0_m, cnst1_m);                       \
    cnst2_m = -cnst0_m;                                                   \
    ILVEV_H2_SH(cnst0_m, cnst1_m, cnst1_m, cnst2_m, cnst0_m, cnst1_m);    \
    SPLATI_H2_SH(coeff0_m, 6, 1, cnst2_m, cnst3_m);                       \
    cnst4_m = -cnst2_m;                                                   \
    ILVEV_H2_SH(cnst2_m, cnst3_m, cnst3_m, cnst4_m, cnst2_m, cnst3_m);    \
                                                                          \
    ILVRL_H2_SH(in2, in5, vec1_m, vec0_m);                                \
    ILVRL_H2_SH(in6, in1, vec3_m, vec2_m);                                \
                                                                          \
    VP9_DOT_ADD_SUB_SRARI_PCK(vec0_m, vec1_m, vec2_m, vec3_m, cnst0_m,    \
                              cnst1_m, cnst2_m, cnst3_m, in5, in2,        \
                              in6, in1);                                  \
    BUTTERFLY_4(in7, in0, in2, in5, s1_m, s0_m, in2, in5);                \
    out7 = -s0_m;                                                         \
    out0 = s1_m;                                                          \
                                                                          \
    SPLATI_H4_SH(coeff1_m, 0, 4, 1, 5,                                    \
                 cnst0_m, cnst1_m, cnst2_m, cnst3_m);                     \
                                                                          \
    ILVEV_H2_SH(cnst3_m, cnst0_m, cnst1_m, cnst2_m, cnst3_m, cnst2_m);    \
    cnst0_m = __msa_ilvev_h(cnst1_m, cnst0_m);                            \
    cnst1_m = cnst0_m;                                                    \
                                                                          \
    ILVRL_H2_SH(in4, in3, vec1_m, vec0_m);                                \
    ILVRL_H2_SH(in6, in1, vec3_m, vec2_m);                                \
    VP9_DOT_ADD_SUB_SRARI_PCK(vec0_m, vec1_m, vec2_m, vec3_m, cnst0_m,    \
                              cnst2_m, cnst3_m, cnst1_m, out1, out6,      \
                              s0_m, s1_m);                                \
                                                                          \
    SPLATI_H2_SH(coeff1_m, 2, 3, cnst0_m, cnst1_m);                       \
    cnst1_m = __msa_ilvev_h(cnst1_m, cnst0_m);                            \
                                                                          \
    ILVRL_H2_SH(in2, in5, vec1_m, vec0_m);                                \
    ILVRL_H2_SH(s0_m, s1_m, vec3_m, vec2_m);                              \
    out3 = VP9_DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);            \
    out4 = VP9_DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst1_m);            \
    out2 = VP9_DOT_SHIFT_RIGHT_PCK_H(vec2_m, vec3_m, cnst0_m);            \
    out5 = VP9_DOT_SHIFT_RIGHT_PCK_H(vec2_m, vec3_m, cnst1_m);            \
                                                                          \
    out1 = -out1;                                                         \
    out3 = -out3;                                                         \
    out5 = -out5;                                                         \
}

#define VP9_MADD_SHORT(m0, m1, c0, c1, res0, res1)                        \
{                                                                         \
    v4i32 madd0_m, madd1_m, madd2_m, madd3_m;                             \
    v8i16 madd_s0_m, madd_s1_m;                                           \
                                                                          \
    ILVRL_H2_SH(m1, m0, madd_s0_m, madd_s1_m);                            \
    DOTP_SH4_SW(madd_s0_m, madd_s1_m, madd_s0_m, madd_s1_m,               \
                c0, c0, c1, c1, madd0_m, madd1_m, madd2_m, madd3_m);      \
    SRARI_W4_SW(madd0_m, madd1_m, madd2_m, madd3_m, VP9_DCT_CONST_BITS);  \
    PCKEV_H2_SH(madd1_m, madd0_m, madd3_m, madd2_m, res0, res1);          \
}

#define VP9_MADD_BF(inp0, inp1, inp2, inp3, cst0, cst1, cst2, cst3,       \
                    out0, out1, out2, out3)                               \
{                                                                         \
    v8i16 madd_s0_m, madd_s1_m, madd_s2_m, madd_s3_m;                     \
    v4i32 tmp0_m, tmp1_m, tmp2_m, tmp3_m, m4_m, m5_m;                     \
                                                                          \
    ILVRL_H2_SH(inp1, inp0, madd_s0_m, madd_s1_m);                        \
    ILVRL_H2_SH(inp3, inp2, madd_s2_m, madd_s3_m);                        \
    DOTP_SH4_SW(madd_s0_m, madd_s1_m, madd_s2_m, madd_s3_m,               \
                cst0, cst0, cst2, cst2, tmp0_m, tmp1_m, tmp2_m, tmp3_m);  \
    BUTTERFLY_4(tmp0_m, tmp1_m, tmp3_m, tmp2_m,                           \
                m4_m, m5_m, tmp3_m, tmp2_m);                              \
    SRARI_W4_SW(m4_m, m5_m, tmp2_m, tmp3_m, VP9_DCT_CONST_BITS);          \
    PCKEV_H2_SH(m5_m, m4_m, tmp3_m, tmp2_m, out0, out1);                  \
    DOTP_SH4_SW(madd_s0_m, madd_s1_m, madd_s2_m, madd_s3_m,               \
                cst1, cst1, cst3, cst3, tmp0_m, tmp1_m, tmp2_m, tmp3_m);  \
    BUTTERFLY_4(tmp0_m, tmp1_m, tmp3_m, tmp2_m,                           \
                m4_m, m5_m, tmp3_m, tmp2_m);                              \
    SRARI_W4_SW(m4_m, m5_m, tmp2_m, tmp3_m, VP9_DCT_CONST_BITS);          \
    PCKEV_H2_SH(m5_m, m4_m, tmp3_m, tmp2_m, out2, out3);                  \
}

#define VP9_SET_COSPI_PAIR(c0_h, c1_h)   \
( {                                      \
    v8i16 out0_m, r0_m, r1_m;            \
                                         \
    r0_m = __msa_fill_h(c0_h);           \
    r1_m = __msa_fill_h(c1_h);           \
    out0_m = __msa_ilvev_h(r1_m, r0_m);  \
                                         \
    out0_m;                              \
} )

#define VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3)  \
{                                                                 \
    uint8_t *dst_m = (uint8_t *) (dst);                           \
    v16u8 dst0_m, dst1_m, dst2_m, dst3_m;                         \
    v16i8 tmp0_m, tmp1_m;                                         \
    v16i8 zero_m = { 0 };                                         \
    v8i16 res0_m, res1_m, res2_m, res3_m;                         \
                                                                  \
    LD_UB4(dst_m, dst_stride, dst0_m, dst1_m, dst2_m, dst3_m);    \
    ILVR_B4_SH(zero_m, dst0_m, zero_m, dst1_m, zero_m, dst2_m,    \
               zero_m, dst3_m, res0_m, res1_m, res2_m, res3_m);   \
    ADD4(res0_m, in0, res1_m, in1, res2_m, in2, res3_m, in3,      \
         res0_m, res1_m, res2_m, res3_m);                         \
    CLIP_SH4_0_255(res0_m, res1_m, res2_m, res3_m);               \
    PCKEV_B2_SB(res1_m, res0_m, res3_m, res2_m, tmp0_m, tmp1_m);  \
    ST8x4_UB(tmp0_m, tmp1_m, dst_m, dst_stride);                  \
}

#define VP9_IDCT4x4(in0, in1, in2, in3, out0, out1, out2, out3)       \
{                                                                     \
    v8i16 c0_m, c1_m, c2_m, c3_m;                                     \
    v8i16 step0_m, step1_m;                                           \
    v4i32 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                             \
                                                                      \
    c0_m = VP9_SET_COSPI_PAIR(cospi_16_64, cospi_16_64);              \
    c1_m = VP9_SET_COSPI_PAIR(cospi_16_64, -cospi_16_64);             \
    step0_m = __msa_ilvr_h(in2, in0);                                 \
    DOTP_SH2_SW(step0_m, step0_m, c0_m, c1_m, tmp0_m, tmp1_m);        \
                                                                      \
    c2_m = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);              \
    c3_m = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);               \
    step1_m = __msa_ilvr_h(in3, in1);                                 \
    DOTP_SH2_SW(step1_m, step1_m, c2_m, c3_m, tmp2_m, tmp3_m);        \
    SRARI_W4_SW(tmp0_m, tmp1_m, tmp2_m, tmp3_m, VP9_DCT_CONST_BITS);  \
                                                                      \
    PCKEV_H2_SW(tmp1_m, tmp0_m, tmp3_m, tmp2_m, tmp0_m, tmp2_m);      \
    SLDI_B2_0_SW(tmp0_m, tmp2_m, tmp1_m, tmp3_m, 8);                  \
    BUTTERFLY_4((v8i16) tmp0_m, (v8i16) tmp1_m,                       \
                (v8i16) tmp2_m, (v8i16) tmp3_m,                       \
                out0, out1, out2, out3);                              \
}

#define VP9_IADST4x4(in0, in1, in2, in3, out0, out1, out2, out3)      \
{                                                                     \
    v8i16 res0_m, res1_m, c0_m, c1_m;                                 \
    v8i16 k1_m, k2_m, k3_m, k4_m;                                     \
    v8i16 zero_m = { 0 };                                             \
    v4i32 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                             \
    v4i32 int0_m, int1_m, int2_m, int3_m;                             \
    v8i16 mask_m = { sinpi_1_9, sinpi_2_9, sinpi_3_9,                 \
        sinpi_4_9, -sinpi_1_9, -sinpi_2_9, -sinpi_3_9,                \
        -sinpi_4_9 };                                                 \
                                                                      \
    SPLATI_H4_SH(mask_m, 3, 0, 1, 2, c0_m, c1_m, k1_m, k2_m);         \
    ILVEV_H2_SH(c0_m, c1_m, k1_m, k2_m, c0_m, c1_m);                  \
    ILVR_H2_SH(in0, in2, in1, in3, res0_m, res1_m);                   \
    DOTP_SH2_SW(res0_m, res1_m, c0_m, c1_m, tmp2_m, tmp1_m);          \
    int0_m = tmp2_m + tmp1_m;                                         \
                                                                      \
    SPLATI_H2_SH(mask_m, 4, 7, k4_m, k3_m);                           \
    ILVEV_H2_SH(k4_m, k1_m, k3_m, k2_m, c0_m, c1_m);                  \
    DOTP_SH2_SW(res0_m, res1_m, c0_m, c1_m, tmp0_m, tmp1_m);          \
    int1_m = tmp0_m + tmp1_m;                                         \
                                                                      \
    c0_m = __msa_splati_h(mask_m, 6);                                 \
    ILVL_H2_SH(k2_m, c0_m, zero_m, k2_m, c0_m, c1_m);                 \
    ILVR_H2_SH(in0, in2, in1, in3, res0_m, res1_m);                   \
    DOTP_SH2_SW(res0_m, res1_m, c0_m, c1_m, tmp0_m, tmp1_m);          \
    int2_m = tmp0_m + tmp1_m;                                         \
                                                                      \
    c0_m = __msa_splati_h(mask_m, 6);                                 \
    c0_m = __msa_ilvev_h(c0_m, k1_m);                                 \
                                                                      \
    res0_m = __msa_ilvr_h((in1), (in3));                              \
    tmp0_m = __msa_dotp_s_w(res0_m, c0_m);                            \
    int3_m = tmp2_m + tmp0_m;                                         \
                                                                      \
    res0_m = __msa_ilvr_h((in2), (in3));                              \
    c1_m = __msa_ilvev_h(k4_m, k3_m);                                 \
                                                                      \
    tmp2_m = __msa_dotp_s_w(res0_m, c1_m);                            \
    res1_m = __msa_ilvr_h((in0), (in2));                              \
    c1_m = __msa_ilvev_h(k1_m, zero_m);                               \
                                                                      \
    tmp3_m = __msa_dotp_s_w(res1_m, c1_m);                            \
    int3_m += tmp2_m;                                                 \
    int3_m += tmp3_m;                                                 \
                                                                      \
    SRARI_W4_SW(int0_m, int1_m, int2_m, int3_m, VP9_DCT_CONST_BITS);  \
    PCKEV_H2_SH(int0_m, int0_m, int1_m, int1_m, out0, out1);          \
    PCKEV_H2_SH(int2_m, int2_m, int3_m, int3_m, out2, out3);          \
}

#define TRANSPOSE4X8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,          \
                           out0, out1, out2, out3, out4, out5, out6, out7)  \
{                                                                           \
    v8i16 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                   \
    v8i16 tmp0_n, tmp1_n, tmp2_n, tmp3_n;                                   \
    v8i16 zero_m = { 0 };                                                   \
                                                                            \
    ILVR_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,                      \
               tmp0_n, tmp1_n, tmp2_n, tmp3_n);                             \
    ILVRL_W2_SH(tmp1_n, tmp0_n, tmp0_m, tmp2_m);                            \
    ILVRL_W2_SH(tmp3_n, tmp2_n, tmp1_m, tmp3_m);                            \
                                                                            \
    out0 = (v8i16) __msa_ilvr_d((v2i64) tmp1_m, (v2i64) tmp0_m);            \
    out1 = (v8i16) __msa_ilvl_d((v2i64) tmp1_m, (v2i64) tmp0_m);            \
    out2 = (v8i16) __msa_ilvr_d((v2i64) tmp3_m, (v2i64) tmp2_m);            \
    out3 = (v8i16) __msa_ilvl_d((v2i64) tmp3_m, (v2i64) tmp2_m);            \
                                                                            \
    out4 = zero_m;                                                          \
    out5 = zero_m;                                                          \
    out6 = zero_m;                                                          \
    out7 = zero_m;                                                          \
}

static void vp9_idct4x4_1_add_msa(int16_t *input, uint8_t *dst,
                                  int32_t dst_stride)
{
    int16_t out;
    v8i16 vec;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO(out, 4);
    vec = __msa_fill_h(out);

    ADDBLK_ST4x4_UB(vec, vec, vec, vec, dst, dst_stride);
}

static void vp9_idct4x4_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                          int32_t dst_stride)
{
    v8i16 in0, in1, in2, in3;

    /* load vector elements of 4x4 block */
    LD4x4_SH(input, in0, in1, in2, in3);
    /* rows */
    VP9_IDCT4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* columns */
    TRANSPOSE4x4_SH_SH(in0, in1, in2, in3, in0, in1, in2, in3);
    VP9_IDCT4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* rounding (add 2^3, divide by 2^4) */
    SRARI_H4_SH(in0, in1, in2, in3, 4);
    ADDBLK_ST4x4_UB(in0, in1, in2, in3, dst, dst_stride);
}

static void vp9_iadst4x4_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                           int32_t dst_stride)
{
    v8i16 in0, in1, in2, in3;

    /* load vector elements of 4x4 block */
    LD4x4_SH(input, in0, in1, in2, in3);
    /* rows */
    VP9_IADST4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* columns */
    TRANSPOSE4x4_SH_SH(in0, in1, in2, in3, in0, in1, in2, in3);
    VP9_IADST4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* rounding (add 2^3, divide by 2^4) */
    SRARI_H4_SH(in0, in1, in2, in3, 4);
    ADDBLK_ST4x4_UB(in0, in1, in2, in3, dst, dst_stride);
}

static void vp9_iadst_idct_4x4_add_msa(int16_t *input, uint8_t *dst,
                                       int32_t dst_stride, int32_t eob)
{
    v8i16 in0, in1, in2, in3;

    /* load vector elements of 4x4 block */
    LD4x4_SH(input, in0, in1, in2, in3);
    /* cols */
    VP9_IADST4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* columns */
    TRANSPOSE4x4_SH_SH(in0, in1, in2, in3, in0, in1, in2, in3);
    VP9_IDCT4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* rounding (add 2^3, divide by 2^4) */
    SRARI_H4_SH(in0, in1, in2, in3, 4);
    ADDBLK_ST4x4_UB(in0, in1, in2, in3, dst, dst_stride);
}

static void vp9_idct_iadst_4x4_add_msa(int16_t *input, uint8_t *dst,
                                       int32_t dst_stride, int32_t eob)
{
    v8i16 in0, in1, in2, in3;

    /* load vector elements of 4x4 block */
    LD4x4_SH(input, in0, in1, in2, in3);
    /* cols */
    VP9_IDCT4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* columns */
    TRANSPOSE4x4_SH_SH(in0, in1, in2, in3, in0, in1, in2, in3);
    VP9_IADST4x4(in0, in1, in2, in3, in0, in1, in2, in3);
    /* rounding (add 2^3, divide by 2^4) */
    SRARI_H4_SH(in0, in1, in2, in3, 4);
    ADDBLK_ST4x4_UB(in0, in1, in2, in3, dst, dst_stride);
}

#define VP9_SET_CONST_PAIR(mask_h, idx1_h, idx2_h)     \
( {                                                    \
    v8i16 c0_m, c1_m;                                  \
                                                       \
    SPLATI_H2_SH(mask_h, idx1_h, idx2_h, c0_m, c1_m);  \
    c0_m = __msa_ilvev_h(c1_m, c0_m);                  \
                                                       \
    c0_m;                                              \
} )

/* multiply and add macro */
#define VP9_MADD(inp0, inp1, inp2, inp3, cst0, cst1, cst2, cst3,          \
                 out0, out1, out2, out3)                                  \
{                                                                         \
    v8i16 madd_s0_m, madd_s1_m, madd_s2_m, madd_s3_m;                     \
    v4i32 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                 \
                                                                          \
    ILVRL_H2_SH(inp1, inp0, madd_s1_m, madd_s0_m);                        \
    ILVRL_H2_SH(inp3, inp2, madd_s3_m, madd_s2_m);                        \
    DOTP_SH4_SW(madd_s1_m, madd_s0_m, madd_s1_m, madd_s0_m,               \
                cst0, cst0, cst1, cst1, tmp0_m, tmp1_m, tmp2_m, tmp3_m);  \
    SRARI_W4_SW(tmp0_m, tmp1_m, tmp2_m, tmp3_m, VP9_DCT_CONST_BITS);      \
    PCKEV_H2_SH(tmp1_m, tmp0_m, tmp3_m, tmp2_m, out0, out1);              \
    DOTP_SH4_SW(madd_s3_m, madd_s2_m, madd_s3_m, madd_s2_m,               \
                cst2, cst2, cst3, cst3, tmp0_m, tmp1_m, tmp2_m, tmp3_m);  \
    SRARI_W4_SW(tmp0_m, tmp1_m, tmp2_m, tmp3_m, VP9_DCT_CONST_BITS);      \
    PCKEV_H2_SH(tmp1_m, tmp0_m, tmp3_m, tmp2_m, out2, out3);              \
}

/* idct 8x8 macro */
#define VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,                 \
                       out0, out1, out2, out3, out4, out5, out6, out7)         \
{                                                                              \
    v8i16 tp0_m, tp1_m, tp2_m, tp3_m, tp4_m, tp5_m, tp6_m, tp7_m;              \
    v8i16 k0_m, k1_m, k2_m, k3_m, res0_m, res1_m, res2_m, res3_m;              \
    v4i32 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                      \
    v8i16 mask_m = { cospi_28_64, cospi_4_64, cospi_20_64, cospi_12_64,        \
       cospi_16_64, -cospi_4_64, -cospi_20_64, -cospi_16_64 };                 \
                                                                               \
    k0_m = VP9_SET_CONST_PAIR(mask_m, 0, 5);                                   \
    k1_m = VP9_SET_CONST_PAIR(mask_m, 1, 0);                                   \
    k2_m = VP9_SET_CONST_PAIR(mask_m, 6, 3);                                   \
    k3_m = VP9_SET_CONST_PAIR(mask_m, 3, 2);                                   \
    VP9_MADD(in1, in7, in3, in5, k0_m, k1_m, k2_m, k3_m, in1, in7, in3, in5);  \
    SUB2(in1, in3, in7, in5, res0_m, res1_m);                                  \
    k0_m = VP9_SET_CONST_PAIR(mask_m, 4, 7);                                   \
    k1_m = __msa_splati_h(mask_m, 4);                                          \
                                                                               \
    ILVRL_H2_SH(res0_m, res1_m, res2_m, res3_m);                               \
    DOTP_SH4_SW(res2_m, res3_m, res2_m, res3_m, k0_m, k0_m, k1_m, k1_m,        \
                tmp0_m, tmp1_m, tmp2_m, tmp3_m);                               \
    SRARI_W4_SW(tmp0_m, tmp1_m, tmp2_m, tmp3_m, VP9_DCT_CONST_BITS);           \
    tp4_m = in1 + in3;                                                         \
    PCKEV_H2_SH(tmp1_m, tmp0_m, tmp3_m, tmp2_m, tp5_m, tp6_m);                 \
    tp7_m = in7 + in5;                                                         \
    k2_m = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);                       \
    k3_m = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);                        \
    VP9_MADD(in0, in4, in2, in6, k1_m, k0_m, k2_m, k3_m,                       \
             in0, in4, in2, in6);                                              \
    BUTTERFLY_4(in0, in4, in2, in6, tp0_m, tp1_m, tp2_m, tp3_m);               \
    BUTTERFLY_8(tp0_m, tp1_m, tp2_m, tp3_m, tp4_m, tp5_m, tp6_m, tp7_m,        \
                out0, out1, out2, out3, out4, out5, out6, out7);               \
}

#define VP9_IADST8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,              \
                        out0, out1, out2, out3, out4, out5, out6, out7)      \
{                                                                            \
    v4i32 r0_m, r1_m, r2_m, r3_m, r4_m, r5_m, r6_m, r7_m;                    \
    v4i32 m0_m, m1_m, m2_m, m3_m, t0_m, t1_m;                                \
    v8i16 res0_m, res1_m, res2_m, res3_m, k0_m, k1_m, in_s0, in_s1;          \
    v8i16 mask1_m = { cospi_2_64, cospi_30_64, -cospi_2_64,                  \
        cospi_10_64, cospi_22_64, -cospi_10_64, cospi_18_64, cospi_14_64 };  \
    v8i16 mask2_m = { cospi_14_64, -cospi_18_64, cospi_26_64,                \
        cospi_6_64, -cospi_26_64, cospi_8_64, cospi_24_64, -cospi_8_64 };    \
    v8i16 mask3_m = { -cospi_24_64, cospi_8_64, cospi_16_64,                 \
        -cospi_16_64, 0, 0, 0, 0 };                                          \
                                                                             \
    k0_m = VP9_SET_CONST_PAIR(mask1_m, 0, 1);                                \
    k1_m = VP9_SET_CONST_PAIR(mask1_m, 1, 2);                                \
    ILVRL_H2_SH(in1, in0, in_s1, in_s0);                                     \
    DOTP_SH4_SW(in_s1, in_s0, in_s1, in_s0, k0_m, k0_m, k1_m, k1_m,          \
                r0_m, r1_m, r2_m, r3_m);                                     \
    k0_m = VP9_SET_CONST_PAIR(mask1_m, 6, 7);                                \
    k1_m = VP9_SET_CONST_PAIR(mask2_m, 0, 1);                                \
    ILVRL_H2_SH(in5, in4, in_s1, in_s0);                                     \
    DOTP_SH4_SW(in_s1, in_s0, in_s1, in_s0, k0_m, k0_m, k1_m, k1_m,          \
                r4_m, r5_m, r6_m, r7_m);                                     \
    ADD4(r0_m, r4_m, r1_m, r5_m, r2_m, r6_m, r3_m, r7_m,                     \
         m0_m, m1_m, m2_m, m3_m);                                            \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SH(m1_m, m0_m, m3_m, m2_m, res0_m, res1_m);                     \
    SUB4(r0_m, r4_m, r1_m, r5_m, r2_m, r6_m, r3_m, r7_m,                     \
         m0_m, m1_m, m2_m, m3_m);                                            \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SW(m1_m, m0_m, m3_m, m2_m, t0_m, t1_m);                         \
    k0_m = VP9_SET_CONST_PAIR(mask1_m, 3, 4);                                \
    k1_m = VP9_SET_CONST_PAIR(mask1_m, 4, 5);                                \
    ILVRL_H2_SH(in3, in2, in_s1, in_s0);                                     \
    DOTP_SH4_SW(in_s1, in_s0, in_s1, in_s0, k0_m, k0_m, k1_m, k1_m,          \
                r0_m, r1_m, r2_m, r3_m);                                     \
    k0_m = VP9_SET_CONST_PAIR(mask2_m, 2, 3);                                \
    k1_m = VP9_SET_CONST_PAIR(mask2_m, 3, 4);                                \
    ILVRL_H2_SH(in7, in6, in_s1, in_s0);                                     \
    DOTP_SH4_SW(in_s1, in_s0, in_s1, in_s0, k0_m, k0_m, k1_m, k1_m,          \
                r4_m, r5_m, r6_m, r7_m);                                     \
    ADD4(r0_m, r4_m, r1_m, r5_m, r2_m, r6_m, r3_m, r7_m,                     \
         m0_m, m1_m, m2_m, m3_m);                                            \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SH(m1_m, m0_m, m3_m, m2_m, res2_m, res3_m);                     \
    SUB4(r0_m, r4_m, r1_m, r5_m, r2_m, r6_m, r3_m, r7_m,                     \
         m0_m, m1_m, m2_m, m3_m);                                            \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SW(m1_m, m0_m, m3_m, m2_m, r2_m, r3_m);                         \
    ILVRL_H2_SW(r3_m, r2_m, m2_m, m3_m);                                     \
    BUTTERFLY_4(res0_m, res1_m, res3_m, res2_m, out0, in7, in4, in3);        \
    k0_m = VP9_SET_CONST_PAIR(mask2_m, 5, 6);                                \
    k1_m = VP9_SET_CONST_PAIR(mask2_m, 6, 7);                                \
    ILVRL_H2_SH(t1_m, t0_m, in_s1, in_s0);                                   \
    DOTP_SH4_SW(in_s1, in_s0, in_s1, in_s0, k0_m, k0_m, k1_m, k1_m,          \
                r0_m, r1_m, r2_m, r3_m);                                     \
    k1_m = VP9_SET_CONST_PAIR(mask3_m, 0, 1);                                \
    DOTP_SH4_SW(m2_m, m3_m, m2_m, m3_m, k0_m, k0_m, k1_m, k1_m,              \
                r4_m, r5_m, r6_m, r7_m);                                     \
    ADD4(r0_m, r6_m, r1_m, r7_m, r2_m, r4_m, r3_m, r5_m,                     \
         m0_m, m1_m, m2_m, m3_m);                                            \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SH(m1_m, m0_m, m3_m, m2_m, in1, out6);                          \
    SUB4(r0_m, r6_m, r1_m, r7_m, r2_m, r4_m, r3_m, r5_m,                     \
         m0_m, m1_m, m2_m, m3_m);                                            \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SH(m1_m, m0_m, m3_m, m2_m, in2, in5);                           \
    k0_m = VP9_SET_CONST_PAIR(mask3_m, 2, 2);                                \
    k1_m = VP9_SET_CONST_PAIR(mask3_m, 2, 3);                                \
    ILVRL_H2_SH(in4, in3, in_s1, in_s0);                                     \
    DOTP_SH4_SW(in_s1, in_s0, in_s1, in_s0, k0_m, k0_m, k1_m, k1_m,          \
                m0_m, m1_m, m2_m, m3_m);                                     \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SH(m1_m, m0_m, m3_m, m2_m, in3, out4);                          \
    ILVRL_H2_SW(in5, in2, m2_m, m3_m);                                       \
    DOTP_SH4_SW(m2_m, m3_m, m2_m, m3_m, k0_m, k0_m, k1_m, k1_m,              \
                m0_m, m1_m, m2_m, m3_m);                                     \
    SRARI_W4_SW(m0_m, m1_m, m2_m, m3_m, VP9_DCT_CONST_BITS);                 \
    PCKEV_H2_SH(m1_m, m0_m, m3_m, m2_m, out2, in5);                          \
                                                                             \
    out1 = -in1;                                                             \
    out3 = -in3;                                                             \
    out5 = -in5;                                                             \
    out7 = -in7;                                                             \
}

static void vp9_idct8x8_1_add_msa(int16_t *input, uint8_t *dst,
                                  int32_t dst_stride)
{
    int16_t out;
    int32_t val;
    v8i16 vec;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    val = ROUND_POWER_OF_TWO(out, 5);
    vec = __msa_fill_h(val);

    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, vec, vec, vec, vec);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, vec, vec, vec, vec);
}

static void vp9_idct8x8_12_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                             int32_t dst_stride)
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 s0, s1, s2, s3, s4, s5, s6, s7, k0, k1, k2, k3, m0, m1, m2, m3;
    v4i32 tmp0, tmp1, tmp2, tmp3;
    v8i16 zero = { 0 };

    /* load vector elements of 8x8 block */
    LD_SH8(input, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    ILVR_D2_SH(in1, in0, in3, in2, in0, in1);
    ILVR_D2_SH(in5, in4, in7, in6, in2, in3);
    //TRANSPOSE8X4_SH_SH(in0, in1, in2, in3, in0, in1, in2, in3);

    /* stage1 */
    ILVL_H2_SH(in3, in0, in2, in1, s0, s1);
    k0 = VP9_SET_COSPI_PAIR(cospi_28_64, -cospi_4_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_4_64, cospi_28_64);
    k2 = VP9_SET_COSPI_PAIR(-cospi_20_64, cospi_12_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_12_64, cospi_20_64);
    DOTP_SH4_SW(s0, s0, s1, s1, k0, k1, k2, k3, tmp0, tmp1, tmp2, tmp3);
    SRARI_W4_SW(tmp0, tmp1, tmp2, tmp3, VP9_DCT_CONST_BITS);
    PCKEV_H2_SH(zero, tmp0, zero, tmp1, s0, s1);
    PCKEV_H2_SH(zero, tmp2, zero, tmp3, s2, s3);
    BUTTERFLY_4(s0, s1, s3, s2, s4, s7, s6, s5);

    /* stage2 */
    ILVR_H2_SH(in3, in1, in2, in0, s1, s0);
    k0 = VP9_SET_COSPI_PAIR(cospi_16_64, cospi_16_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_16_64, -cospi_16_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);
    DOTP_SH4_SW(s0, s0, s1, s1, k0, k1, k2, k3, tmp0, tmp1, tmp2, tmp3);
    SRARI_W4_SW(tmp0, tmp1, tmp2, tmp3, VP9_DCT_CONST_BITS);
    PCKEV_H2_SH(zero, tmp0, zero, tmp1, s0, s1);
    PCKEV_H2_SH(zero, tmp2, zero, tmp3, s2, s3);
    BUTTERFLY_4(s0, s1, s2, s3, m0, m1, m2, m3);

    /* stage3 */
    s0 = __msa_ilvr_h(s6, s5);

    k1 = VP9_SET_COSPI_PAIR(-cospi_16_64, cospi_16_64);
    DOTP_SH2_SW(s0, s0, k1, k0, tmp0, tmp1);
    SRARI_W2_SW(tmp0, tmp1, VP9_DCT_CONST_BITS);
    PCKEV_H2_SH(zero, tmp0, zero, tmp1, s2, s3);

    /* stage4 */
    BUTTERFLY_8(m0, m1, m2, m3, s4, s2, s3, s7,
                in0, in1, in2, in3, in4, in5, in6, in7);
    TRANSPOSE4X8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);

    /* final rounding (add 2^4, divide by 2^5) and shift */
    SRARI_H4_SH(in0, in1, in2, in3, 5);
    SRARI_H4_SH(in4, in5, in6, in7, 5);

    /* add block and store 8x8 */
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in4, in5, in6, in7);
}

static void vp9_idct8x8_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                          int32_t dst_stride)
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    /* load vector elements of 8x8 block */
    LD_SH8(input, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    /* 1D idct8x8 */
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);
    /* columns transform */
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    /* 1D idct8x8 */
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);
    /* final rounding (add 2^4, divide by 2^5) and shift */
    SRARI_H4_SH(in0, in1, in2, in3, 5);
    SRARI_H4_SH(in4, in5, in6, in7, 5);
    /* add block and store 8x8 */
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in4, in5, in6, in7);
}

static void vp9_iadst8x8_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                           int32_t dst_stride)
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 out0, out1, out2, out3, out4, out5, out6, out7;
    v8i16 cnst0, cnst1, cnst2, cnst3, cnst4;
    v8i16 temp0, temp1, temp2, temp3, s0, s1;
    v16i8 zero = { 0 };

    /* load vector elements of 8x8 block */
    LD_SH8(input, 8, in0, in1, in2, in3, in4, in5, in6, in7);

    /* 1D adst8x8 */
    VP9_ADST8(in0, in1, in2, in3, in4, in5, in6, in7,
              in0, in1, in2, in3, in4, in5, in6, in7);

    /* columns transform */
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);

    cnst0 = __msa_fill_h(cospi_2_64);
    cnst1 = __msa_fill_h(cospi_30_64);
    cnst2 = -cnst0;
    ILVEV_H2_SH(cnst0, cnst1, cnst1, cnst2, cnst0, cnst1);
    cnst2 = __msa_fill_h(cospi_18_64);
    cnst3 = __msa_fill_h(cospi_14_64);
    cnst4 = -cnst2;
    ILVEV_H2_SH(cnst2, cnst3, cnst3, cnst4, cnst2, cnst3);

    ILVRL_H2_SH(in0, in7, temp1, temp0);
    ILVRL_H2_SH(in4, in3, temp3, temp2);
    VP9_DOT_ADD_SUB_SRARI_PCK(temp0, temp1, temp2, temp3, cnst0, cnst1, cnst2,
                              cnst3, in7, in0, in4, in3);

    cnst0 = __msa_fill_h(cospi_10_64);
    cnst1 = __msa_fill_h(cospi_22_64);
    cnst2 = -cnst0;
    ILVEV_H2_SH(cnst0, cnst1, cnst1, cnst2, cnst0, cnst1);
    cnst2 = __msa_fill_h(cospi_26_64);
    cnst3 = __msa_fill_h(cospi_6_64);
    cnst4 = -cnst2;
    ILVEV_H2_SH(cnst2, cnst3, cnst3, cnst4, cnst2, cnst3);

    ILVRL_H2_SH(in2, in5, temp1, temp0);
    ILVRL_H2_SH(in6, in1, temp3, temp2);
    VP9_DOT_ADD_SUB_SRARI_PCK(temp0, temp1, temp2, temp3, cnst0, cnst1, cnst2,
                              cnst3, in5, in2, in6, in1);
    BUTTERFLY_4(in7, in0, in2, in5, s1, s0, in2, in5);
    out7 = -s0;
    out0 = s1;
    SRARI_H2_SH(out0, out7, 5);
    dst0 = LD_UB(dst + 0 * dst_stride);
    dst7 = LD_UB(dst + 7 * dst_stride);

    res0 = (v8i16) __msa_ilvr_b(zero, (v16i8) dst0);
    res0 += out0;
    res0 = CLIP_SH_0_255(res0);
    res0 = (v8i16) __msa_pckev_b((v16i8) res0, (v16i8) res0);
    ST8x1_UB(res0, dst);

    res7 = (v8i16) __msa_ilvr_b(zero, (v16i8) dst7);
    res7 += out7;
    res7 = CLIP_SH_0_255(res7);
    res7 = (v8i16) __msa_pckev_b((v16i8) res7, (v16i8) res7);
    ST8x1_UB(res7, dst + 7 * dst_stride);

    cnst1 = __msa_fill_h(cospi_24_64);
    cnst0 = __msa_fill_h(cospi_8_64);
    cnst3 = -cnst1;
    cnst2 = -cnst0;

    ILVEV_H2_SH(cnst3, cnst0, cnst1, cnst2, cnst3, cnst2);
    cnst0 = __msa_ilvev_h(cnst1, cnst0);
    cnst1 = cnst0;

    ILVRL_H2_SH(in4, in3, temp1, temp0);
    ILVRL_H2_SH(in6, in1, temp3, temp2);
    VP9_DOT_ADD_SUB_SRARI_PCK(temp0, temp1, temp2, temp3, cnst0, cnst2, cnst3,
                              cnst1, out1, out6, s0, s1);
    out1 = -out1;
    SRARI_H2_SH(out1, out6, 5);
    dst1 = LD_UB(dst + 1 * dst_stride);
    dst6 = LD_UB(dst + 6 * dst_stride);
    ILVR_B2_SH(zero, dst1, zero, dst6, res1, res6);
    ADD2(res1, out1, res6, out6, res1, res6);
    CLIP_SH2_0_255(res1, res6);
    PCKEV_B2_SH(res1, res1, res6, res6, res1, res6);
    ST8x1_UB(res1, dst + dst_stride);
    ST8x1_UB(res6, dst + 6 * dst_stride);

    cnst0 = __msa_fill_h(cospi_16_64);
    cnst1 = -cnst0;
    cnst1 = __msa_ilvev_h(cnst1, cnst0);

    ILVRL_H2_SH(in2, in5, temp1, temp0);
    ILVRL_H2_SH(s0, s1, temp3, temp2);
    out3 = VP9_DOT_SHIFT_RIGHT_PCK_H(temp0, temp1, cnst0);
    out4 = VP9_DOT_SHIFT_RIGHT_PCK_H(temp0, temp1, cnst1);
    out3 = -out3;
    SRARI_H2_SH(out3, out4, 5);
    dst3 = LD_UB(dst + 3 * dst_stride);
    dst4 = LD_UB(dst + 4 * dst_stride);
    ILVR_B2_SH(zero, dst3, zero, dst4, res3, res4);
    ADD2(res3, out3, res4, out4, res3, res4);
    CLIP_SH2_0_255(res3, res4);
    PCKEV_B2_SH(res3, res3, res4, res4, res3, res4);
    ST8x1_UB(res3, dst + 3 * dst_stride);
    ST8x1_UB(res4, dst + 4 * dst_stride);

    out2 = VP9_DOT_SHIFT_RIGHT_PCK_H(temp2, temp3, cnst0);
    out5 = VP9_DOT_SHIFT_RIGHT_PCK_H(temp2, temp3, cnst1);
    out5 = -out5;
    SRARI_H2_SH(out2, out5, 5);
    dst2 = LD_UB(dst + 2 * dst_stride);
    dst5 = LD_UB(dst + 5 * dst_stride);
    ILVR_B2_SH(zero, dst2, zero, dst5, res2, res5);
    ADD2(res2, out2, res5, out5, res2, res5);
    CLIP_SH2_0_255(res2, res5);
    PCKEV_B2_SH(res2, res2, res5, res5, res2, res5);
    ST8x1_UB(res2, dst + 2 * dst_stride);
    ST8x1_UB(res5, dst + 5 * dst_stride);
}

static void vp9_iadst_idct_8x8_add_msa(int16_t *input, uint8_t *dst,
                                       int32_t dst_stride, int32_t eob)
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    /* load vector elements of 8x8 block */
    LD_SH8(input, 8, in1, in6, in3, in4, in5, in2, in7, in0);
    /* 1D idct8x8 */
    VP9_IADST8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                    in0, in1, in2, in3, in4, in5, in6, in7);
    /* columns transform */
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    /* 1D idct8x8 */
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);
    /* final rounding (add 2^4, divide by 2^5) and shift */
    SRARI_H4_SH(in0, in1, in2, in3, 5);
    SRARI_H4_SH(in4, in5, in6, in7, 5);
    /* add block and store 8x8 */
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in4, in5, in6, in7);
}

static void vp9_idct_iadst_8x8_add_msa(int16_t *input, uint8_t *dst,
                                       int32_t dst_stride, int32_t eob)
{
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    /* load vector elements of 8x8 block */
    LD_SH8(input, 8, in0, in1, in2, in3, in4, in5, in6, in7);

    /* 1D idct8x8 */
    VP9_IDCT8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                   in0, in1, in2, in3, in4, in5, in6, in7);
    /* columns transform */
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in1, in6, in3, in4, in5, in2, in7, in0);
    /* 1D idct8x8 */
    VP9_IADST8x8_1D(in0, in1, in2, in3, in4, in5, in6, in7,
                    in0, in1, in2, in3, in4, in5, in6, in7);
    /* final rounding (add 2^4, divide by 2^5) and shift */
    SRARI_H4_SH(in0, in1, in2, in3, 5);
    SRARI_H4_SH(in4, in5, in6, in7, 5);
    /* add block and store 8x8 */
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in0, in1, in2, in3);
    dst += (4 * dst_stride);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, in4, in5, in6, in7);
}

#define VP9_IADST8x16_1D(r0, r1, r2, r3, r4, r5, r6, r7, r8,          \
                         r9, r10, r11, r12, r13, r14, r15,            \
                         out0, out1, out2, out3, out4, out5,          \
                         out6, out7, out8, out9, out10, out11,        \
                         out12, out13, out14, out15)                  \
{                                                                     \
    v8i16 g0_m, g1_m, g2_m, g3_m, g4_m, g5_m, g6_m, g7_m;             \
    v8i16 g8_m, g9_m, g10_m, g11_m, g12_m, g13_m, g14_m, g15_m;       \
    v8i16 h0_m, h1_m, h2_m, h3_m, h4_m, h5_m, h6_m, h7_m;             \
    v8i16 h8_m, h9_m, h10_m, h11_m;                                   \
    v8i16 k0_m, k1_m, k2_m, k3_m;                                     \
                                                                      \
    /* stage 1 */                                                     \
    k0_m = VP9_SET_COSPI_PAIR(cospi_1_64, cospi_31_64);               \
    k1_m = VP9_SET_COSPI_PAIR(cospi_31_64, -cospi_1_64);              \
    k2_m = VP9_SET_COSPI_PAIR(cospi_17_64, cospi_15_64);              \
    k3_m = VP9_SET_COSPI_PAIR(cospi_15_64, -cospi_17_64);             \
    VP9_MADD_BF(r15, r0, r7, r8, k0_m, k1_m, k2_m, k3_m,              \
                g0_m, g1_m, g2_m, g3_m);                              \
    k0_m = VP9_SET_COSPI_PAIR(cospi_5_64, cospi_27_64);               \
    k1_m = VP9_SET_COSPI_PAIR(cospi_27_64, -cospi_5_64);              \
    k2_m = VP9_SET_COSPI_PAIR(cospi_21_64, cospi_11_64);              \
    k3_m = VP9_SET_COSPI_PAIR(cospi_11_64, -cospi_21_64);             \
    VP9_MADD_BF(r13, r2, r5, r10, k0_m, k1_m, k2_m, k3_m,             \
                g4_m, g5_m, g6_m, g7_m);                              \
    k0_m = VP9_SET_COSPI_PAIR(cospi_9_64, cospi_23_64);               \
    k1_m = VP9_SET_COSPI_PAIR(cospi_23_64, -cospi_9_64);              \
    k2_m = VP9_SET_COSPI_PAIR(cospi_25_64, cospi_7_64);               \
    k3_m = VP9_SET_COSPI_PAIR(cospi_7_64, -cospi_25_64);              \
    VP9_MADD_BF(r11, r4, r3, r12, k0_m, k1_m, k2_m, k3_m,             \
                g8_m, g9_m, g10_m, g11_m);                            \
    k0_m = VP9_SET_COSPI_PAIR(cospi_13_64, cospi_19_64);              \
    k1_m = VP9_SET_COSPI_PAIR(cospi_19_64, -cospi_13_64);             \
    k2_m = VP9_SET_COSPI_PAIR(cospi_29_64, cospi_3_64);               \
    k3_m = VP9_SET_COSPI_PAIR(cospi_3_64, -cospi_29_64);              \
    VP9_MADD_BF(r9, r6, r1, r14, k0_m, k1_m, k2_m, k3_m,              \
                g12_m, g13_m, g14_m, g15_m);                          \
                                                                      \
    /* stage 2 */                                                     \
    k0_m = VP9_SET_COSPI_PAIR(cospi_4_64, cospi_28_64);               \
    k1_m = VP9_SET_COSPI_PAIR(cospi_28_64, -cospi_4_64);              \
    k2_m = VP9_SET_COSPI_PAIR(-cospi_28_64, cospi_4_64);              \
    VP9_MADD_BF(g1_m, g3_m, g9_m, g11_m, k0_m, k1_m, k2_m, k0_m,      \
                h0_m, h1_m, h2_m, h3_m);                              \
    k0_m = VP9_SET_COSPI_PAIR(cospi_12_64, cospi_20_64);              \
    k1_m = VP9_SET_COSPI_PAIR(-cospi_20_64, cospi_12_64);             \
    k2_m = VP9_SET_COSPI_PAIR(cospi_20_64, -cospi_12_64);             \
    VP9_MADD_BF(g7_m, g5_m, g15_m, g13_m, k0_m, k1_m, k2_m, k0_m,     \
                h4_m, h5_m, h6_m, h7_m);                              \
    BUTTERFLY_4(h0_m, h2_m, h6_m, h4_m, out8, out9, out11, out10);    \
    BUTTERFLY_8(g0_m, g2_m, g4_m, g6_m, g14_m, g12_m, g10_m, g8_m,    \
                h8_m, h9_m, h10_m, h11_m, h6_m, h4_m, h2_m, h0_m);    \
                                                                      \
    /* stage 3 */                                                     \
    BUTTERFLY_4(h8_m, h9_m, h11_m, h10_m, out0, out1, h11_m, h10_m);  \
    k0_m = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);               \
    k1_m = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);              \
    k2_m = VP9_SET_COSPI_PAIR(-cospi_24_64, cospi_8_64);              \
    VP9_MADD_BF(h0_m, h2_m, h4_m, h6_m, k0_m, k1_m, k2_m, k0_m,       \
                out4, out6, out5, out7);                              \
    VP9_MADD_BF(h1_m, h3_m, h5_m, h7_m, k0_m, k1_m, k2_m, k0_m,       \
                out12, out14, out13, out15);                          \
                                                                      \
    /* stage 4 */                                                     \
    k0_m = VP9_SET_COSPI_PAIR(cospi_16_64, cospi_16_64);              \
    k1_m = VP9_SET_COSPI_PAIR(-cospi_16_64, -cospi_16_64);            \
    k2_m = VP9_SET_COSPI_PAIR(cospi_16_64, -cospi_16_64);             \
    k3_m = VP9_SET_COSPI_PAIR(-cospi_16_64, cospi_16_64);             \
    VP9_MADD_SHORT(h10_m, h11_m, k1_m, k2_m, out2, out3);             \
    VP9_MADD_SHORT(out6, out7, k0_m, k3_m, out6, out7);               \
    VP9_MADD_SHORT(out10, out11, k0_m, k3_m, out10, out11);           \
    VP9_MADD_SHORT(out14, out15, k1_m, k2_m, out14, out15);           \
}

static void vp9_idct16_1d_columns_addblk_msa(int16_t *input, uint8_t *dst,
                                             int32_t dst_stride)
{
    v8i16 loc0, loc1, loc2, loc3;
    v8i16 reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14;
    v8i16 reg3, reg13, reg11, reg5, reg7, reg9, reg1, reg15;
    v8i16 tmp5, tmp6, tmp7;

    /* load up 8x8 */
    LD_SH8(input, 16, reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);
    input += 8 * 16;
    /* load bottom 8x8 */
    LD_SH8(input, 16, reg8, reg9, reg10, reg11, reg12, reg13, reg14, reg15);

    VP9_DOTP_CONST_PAIR(reg2, reg14, cospi_28_64, cospi_4_64, reg2, reg14);
    VP9_DOTP_CONST_PAIR(reg10, reg6, cospi_12_64, cospi_20_64, reg10, reg6);
    BUTTERFLY_4(reg2, reg14, reg6, reg10, loc0, loc1, reg14, reg2);
    VP9_DOTP_CONST_PAIR(reg14, reg2, cospi_16_64, cospi_16_64, loc2, loc3);
    VP9_DOTP_CONST_PAIR(reg0, reg8, cospi_16_64, cospi_16_64, reg0, reg8);
    VP9_DOTP_CONST_PAIR(reg4, reg12, cospi_24_64, cospi_8_64, reg4, reg12);
    BUTTERFLY_4(reg8, reg0, reg4, reg12, reg2, reg6, reg10, reg14);

    reg0 = reg2 - loc1;
    reg2 = reg2 + loc1;
    reg12 = reg14 - loc0;
    reg14 = reg14 + loc0;
    reg4 = reg6 - loc3;
    reg6 = reg6 + loc3;
    reg8 = reg10 - loc2;
    reg10 = reg10 + loc2;

    /* stage 2 */
    VP9_DOTP_CONST_PAIR(reg1, reg15, cospi_30_64, cospi_2_64, reg1, reg15);
    VP9_DOTP_CONST_PAIR(reg9, reg7, cospi_14_64, cospi_18_64, loc2, loc3);

    reg9 = reg1 - loc2;
    reg1 = reg1 + loc2;
    reg7 = reg15 - loc3;
    reg15 = reg15 + loc3;

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_22_64, cospi_10_64, reg5, reg11);
    VP9_DOTP_CONST_PAIR(reg13, reg3, cospi_6_64, cospi_26_64, loc0, loc1);
    BUTTERFLY_4(loc0, loc1, reg11, reg5, reg13, reg3, reg11, reg5);

    loc1 = reg15 + reg3;
    reg3 = reg15 - reg3;
    loc2 = reg2 + loc1;
    reg15 = reg2 - loc1;

    loc1 = reg1 + reg13;
    reg13 = reg1 - reg13;
    loc0 = reg0 + loc1;
    loc1 = reg0 - loc1;
    tmp6 = loc0;
    tmp7 = loc1;
    reg0 = loc2;

    VP9_DOTP_CONST_PAIR(reg7, reg9, cospi_24_64, cospi_8_64, reg7, reg9);
    VP9_DOTP_CONST_PAIR((-reg5), (-reg11), cospi_8_64, cospi_24_64, reg5,
                        reg11);

    loc0 = reg9 + reg5;
    reg5 = reg9 - reg5;
    reg2 = reg6 + loc0;
    reg1 = reg6 - loc0;

    loc0 = reg7 + reg11;
    reg11 = reg7 - reg11;
    loc1 = reg4 + loc0;
    loc2 = reg4 - loc0;
    tmp5 = loc1;

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_16_64, cospi_16_64, reg5, reg11);
    BUTTERFLY_4(reg8, reg10, reg11, reg5, loc0, reg4, reg9, loc1);

    reg10 = loc0;
    reg11 = loc1;

    VP9_DOTP_CONST_PAIR(reg3, reg13, cospi_16_64, cospi_16_64, reg3, reg13);
    BUTTERFLY_4(reg12, reg14, reg13, reg3, reg8, reg6, reg7, reg5);
    reg13 = loc2;

    /* Transpose and store the output */
    reg12 = tmp5;
    reg14 = tmp6;
    reg3 = tmp7;

    SRARI_H4_SH(reg0, reg2, reg4, reg6, 6);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg0, reg2, reg4, reg6);
    dst += (4 * dst_stride);
    SRARI_H4_SH(reg8, reg10, reg12, reg14, 6);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg8, reg10, reg12, reg14);
    dst += (4 * dst_stride);
    SRARI_H4_SH(reg3, reg13, reg11, reg5, 6);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg3, reg13, reg11, reg5);
    dst += (4 * dst_stride);
    SRARI_H4_SH(reg7, reg9, reg1, reg15, 6);
    VP9_ADDBLK_ST8x4_UB(dst, dst_stride, reg7, reg9, reg1, reg15);
}

static void vp9_idct16_1d_columns_msa(int16_t *input, int16_t *output)
{
    v8i16 loc0, loc1, loc2, loc3;
    v8i16 reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14;
    v8i16 reg3, reg13, reg11, reg5, reg7, reg9, reg1, reg15;
    v8i16 tmp5, tmp6, tmp7;

    /* load up 8x8 */
    LD_SH8(input, 16, reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);
    input += 8 * 16;
    /* load bottom 8x8 */
    LD_SH8(input, 16, reg8, reg9, reg10, reg11, reg12, reg13, reg14, reg15);

    VP9_DOTP_CONST_PAIR(reg2, reg14, cospi_28_64, cospi_4_64, reg2, reg14);
    VP9_DOTP_CONST_PAIR(reg10, reg6, cospi_12_64, cospi_20_64, reg10, reg6);
    BUTTERFLY_4(reg2, reg14, reg6, reg10, loc0, loc1, reg14, reg2);
    VP9_DOTP_CONST_PAIR(reg14, reg2, cospi_16_64, cospi_16_64, loc2, loc3);
    VP9_DOTP_CONST_PAIR(reg0, reg8, cospi_16_64, cospi_16_64, reg0, reg8);
    VP9_DOTP_CONST_PAIR(reg4, reg12, cospi_24_64, cospi_8_64, reg4, reg12);
    BUTTERFLY_4(reg8, reg0, reg4, reg12, reg2, reg6, reg10, reg14);

    reg0 = reg2 - loc1;
    reg2 = reg2 + loc1;
    reg12 = reg14 - loc0;
    reg14 = reg14 + loc0;
    reg4 = reg6 - loc3;
    reg6 = reg6 + loc3;
    reg8 = reg10 - loc2;
    reg10 = reg10 + loc2;

    /* stage 2 */
    VP9_DOTP_CONST_PAIR(reg1, reg15, cospi_30_64, cospi_2_64, reg1, reg15);
    VP9_DOTP_CONST_PAIR(reg9, reg7, cospi_14_64, cospi_18_64, loc2, loc3);

    reg9 = reg1 - loc2;
    reg1 = reg1 + loc2;
    reg7 = reg15 - loc3;
    reg15 = reg15 + loc3;

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_22_64, cospi_10_64, reg5, reg11);
    VP9_DOTP_CONST_PAIR(reg13, reg3, cospi_6_64, cospi_26_64, loc0, loc1);
    BUTTERFLY_4(loc0, loc1, reg11, reg5, reg13, reg3, reg11, reg5);

    loc1 = reg15 + reg3;
    reg3 = reg15 - reg3;
    loc2 = reg2 + loc1;
    reg15 = reg2 - loc1;

    loc1 = reg1 + reg13;
    reg13 = reg1 - reg13;
    loc0 = reg0 + loc1;
    loc1 = reg0 - loc1;
    tmp6 = loc0;
    tmp7 = loc1;
    reg0 = loc2;

    VP9_DOTP_CONST_PAIR(reg7, reg9, cospi_24_64, cospi_8_64, reg7, reg9);
    VP9_DOTP_CONST_PAIR((-reg5), (-reg11), cospi_8_64, cospi_24_64, reg5,
                        reg11);

    loc0 = reg9 + reg5;
    reg5 = reg9 - reg5;
    reg2 = reg6 + loc0;
    reg1 = reg6 - loc0;

    loc0 = reg7 + reg11;
    reg11 = reg7 - reg11;
    loc1 = reg4 + loc0;
    loc2 = reg4 - loc0;

    tmp5 = loc1;

    VP9_DOTP_CONST_PAIR(reg5, reg11, cospi_16_64, cospi_16_64, reg5, reg11);
    BUTTERFLY_4(reg8, reg10, reg11, reg5, loc0, reg4, reg9, loc1);

    reg10 = loc0;
    reg11 = loc1;

    VP9_DOTP_CONST_PAIR(reg3, reg13, cospi_16_64, cospi_16_64, reg3, reg13);
    BUTTERFLY_4(reg12, reg14, reg13, reg3, reg8, reg6, reg7, reg5);
    reg13 = loc2;

    /* Transpose and store the output */
    reg12 = tmp5;
    reg14 = tmp6;
    reg3 = tmp7;

    /* transpose block */
    TRANSPOSE8x8_SH_SH(reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14,
                       reg0, reg2, reg4, reg6, reg8, reg10, reg12, reg14);
    ST_SH4(reg0, reg2, reg4, reg6, output, 16);
    ST_SH4(reg8, reg10, reg12, reg14, (output + 4 * 16), 16);

    /* transpose block */
    TRANSPOSE8x8_SH_SH(reg3, reg13, reg11, reg5, reg7, reg9, reg1, reg15,
                       reg3, reg13, reg11, reg5, reg7, reg9, reg1, reg15);
    ST_SH4(reg3, reg13, reg11, reg5, (output + 8), 16);
    ST_SH4(reg7, reg9, reg1, reg15, (output + 8 + 4 * 16), 16);
}

static void vp9_idct16x16_1_add_msa(int16_t *input, uint8_t *dst,
                                    int32_t dst_stride)
{
    uint8_t i;
    int16_t out;
    v8i16 vec, res0, res1, res2, res3, res4, res5, res6, res7;
    v16u8 dst0, dst1, dst2, dst3, tmp0, tmp1, tmp2, tmp3;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO(out, 6);

    vec = __msa_fill_h(out);

    for (i = 4; i--;)
    {
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        UNPCK_UB_SH(dst0, res0, res4);
        UNPCK_UB_SH(dst1, res1, res5);
        UNPCK_UB_SH(dst2, res2, res6);
        UNPCK_UB_SH(dst3, res3, res7);
        ADD4(res0, vec, res1, vec, res2, vec, res3, vec, res0, res1, res2,
             res3);
        ADD4(res4, vec, res5, vec, res6, vec, res7, vec, res4, res5, res6,
             res7);
        CLIP_SH4_0_255(res0, res1, res2, res3);
        CLIP_SH4_0_255(res4, res5, res6, res7);
        PCKEV_B4_UB(res4, res0, res5, res1, res6, res2, res7, res3,
                    tmp0, tmp1, tmp2, tmp3);
        ST_UB4(tmp0, tmp1, tmp2, tmp3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void vp9_idct16x16_10_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                               int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[16 * 16] ALLOC_ALIGNED(ALIGNMENT);
    int16_t *out = out_arr;

    /* transform rows */
    vp9_idct16_1d_columns_msa(input, out);

    /* short case just considers top 4 rows as valid output */
    out += 4 * 16;
    for (i = 12; i--;) {
        __asm__ volatile (
            "sw     $zero,   0(%[out])     \n\t"
            "sw     $zero,   4(%[out])     \n\t"
            "sw     $zero,   8(%[out])     \n\t"
            "sw     $zero,  12(%[out])     \n\t"
            "sw     $zero,  16(%[out])     \n\t"
            "sw     $zero,  20(%[out])     \n\t"
            "sw     $zero,  24(%[out])     \n\t"
            "sw     $zero,  28(%[out])     \n\t"

            :
            : [out] "r" (out)
        );

        out += 16;
    }

    out = out_arr;

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_addblk_msa((out + (i << 3)), (dst + (i << 3)),
                                         dst_stride);
    }
}

static void vp9_idct16x16_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                            int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[16 * 16] ALLOC_ALIGNED(ALIGNMENT);
    int16_t *out = out_arr;

    /* transform rows */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_msa((input + (i << 3)), (out + (i << 7)));
    }

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_addblk_msa((out + (i << 3)), (dst + (i << 3)),
                                         dst_stride);
    }
}

static void vp9_iadst16_1d_columns_msa(int16_t *input, int16_t *output)
{
    v8i16 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15;
    v8i16 l0, l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15;

    /* load input data */
    LD_SH16(input, 16,
            l0, l1, l2, l3, l4, l5, l6, l7,
            l8, l9, l10, l11, l12, l13, l14, l15);

    /* ADST in horizontal */
    VP9_IADST8x16_1D(l0, l1, l2, l3, l4, l5, l6, l7,
                     l8, l9, l10, l11, l12, l13, l14, l15,
                     r0, r1, r2, r3, r4, r5, r6, r7,
                     r8, r9, r10, r11, r12, r13, r14, r15);

    l1 = -r8;
    l3 = -r4;
    l13 = -r13;
    l15 = -r1;

    TRANSPOSE8x8_SH_SH(r0, l1, r12, l3, r6, r14, r10, r2,
                       l0, l1, l2, l3, l4, l5, l6, l7);
    ST_SH8(l0, l1, l2, l3, l4, l5, l6, l7, output, 16);
    TRANSPOSE8x8_SH_SH(r3, r11, r15, r7, r5, l13, r9, l15,
                       l8, l9, l10, l11, l12, l13, l14, l15);
    ST_SH8(l8, l9, l10, l11, l12, l13, l14, l15, (output + 8), 16);
}

static void vp9_iadst16_1d_columns_addblk_msa(int16_t *input, uint8_t *dst,
                                              int32_t dst_stride)
{
    v8i16 v0, v2, v4, v6, k0, k1, k2, k3;
    v8i16 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15;
    v8i16 out0, out1, out2, out3, out4, out5, out6, out7;
    v8i16 out8, out9, out10, out11, out12, out13, out14, out15;
    v8i16 g0, g1, g2, g3, g4, g5, g6, g7, g8, g9, g10, g11, g12, g13, g14, g15;
    v8i16 h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v8i16 res8, res9, res10, res11, res12, res13, res14, res15;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 dst8, dst9, dst10, dst11, dst12, dst13, dst14, dst15;
    v16i8 zero = { 0 };

    r0 = LD_SH(input + 0 * 16);
    r3 = LD_SH(input + 3 * 16);
    r4 = LD_SH(input + 4 * 16);
    r7 = LD_SH(input + 7 * 16);
    r8 = LD_SH(input + 8 * 16);
    r11 = LD_SH(input + 11 * 16);
    r12 = LD_SH(input + 12 * 16);
    r15 = LD_SH(input + 15 * 16);

    /* stage 1 */
    k0 = VP9_SET_COSPI_PAIR(cospi_1_64, cospi_31_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_31_64, -cospi_1_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_17_64, cospi_15_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_15_64, -cospi_17_64);
    VP9_MADD_BF(r15, r0, r7, r8, k0, k1, k2, k3, g0, g1, g2, g3);
    k0 = VP9_SET_COSPI_PAIR(cospi_9_64, cospi_23_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_23_64, -cospi_9_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_25_64, cospi_7_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_7_64, -cospi_25_64);
    VP9_MADD_BF(r11, r4, r3, r12, k0, k1, k2, k3, g8, g9, g10, g11);
    BUTTERFLY_4(g0, g2, g10, g8, h8, h9, v2, v0);
    k0 = VP9_SET_COSPI_PAIR(cospi_4_64, cospi_28_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_28_64, -cospi_4_64);
    k2 = VP9_SET_COSPI_PAIR(-cospi_28_64, cospi_4_64);
    VP9_MADD_BF(g1, g3, g9, g11, k0, k1, k2, k0, h0, h1, h2, h3);

    r1 = LD_SH(input + 1 * 16);
    r2 = LD_SH(input + 2 * 16);
    r5 = LD_SH(input + 5 * 16);
    r6 = LD_SH(input + 6 * 16);
    r9 = LD_SH(input + 9 * 16);
    r10 = LD_SH(input + 10 * 16);
    r13 = LD_SH(input + 13 * 16);
    r14 = LD_SH(input + 14 * 16);

    k0 = VP9_SET_COSPI_PAIR(cospi_5_64, cospi_27_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_27_64, -cospi_5_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_21_64, cospi_11_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_11_64, -cospi_21_64);
    VP9_MADD_BF(r13, r2, r5, r10, k0, k1, k2, k3, g4, g5, g6, g7);
    k0 = VP9_SET_COSPI_PAIR(cospi_13_64, cospi_19_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_19_64, -cospi_13_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_29_64, cospi_3_64);
    k3 = VP9_SET_COSPI_PAIR(cospi_3_64, -cospi_29_64);
    VP9_MADD_BF(r9, r6, r1, r14, k0, k1, k2, k3, g12, g13, g14, g15);
    BUTTERFLY_4(g4, g6, g14, g12, h10, h11, v6, v4);
    BUTTERFLY_4(h8, h9, h11, h10, out0, out1, h11, h10);
    out1 = -out1;
    SRARI_H2_SH(out0, out1, 6);
    dst0 = LD_UB(dst + 0 * dst_stride);
    dst1 = LD_UB(dst + 15 * dst_stride);
    ILVR_B2_SH(zero, dst0, zero, dst1, res0, res1);
    ADD2(res0, out0, res1, out1, res0, res1);
    CLIP_SH2_0_255(res0, res1);
    PCKEV_B2_SH(res0, res0, res1, res1, res0, res1);
    ST8x1_UB(res0, dst);
    ST8x1_UB(res1, dst + 15 * dst_stride);

    k0 = VP9_SET_COSPI_PAIR(cospi_12_64, cospi_20_64);
    k1 = VP9_SET_COSPI_PAIR(-cospi_20_64, cospi_12_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_20_64, -cospi_12_64);
    VP9_MADD_BF(g7, g5, g15, g13, k0, k1, k2, k0, h4, h5, h6, h7);
    BUTTERFLY_4(h0, h2, h6, h4, out8, out9, out11, out10);
    out8 = -out8;

    SRARI_H2_SH(out8, out9, 6);
    dst8 = LD_UB(dst + 1 * dst_stride);
    dst9 = LD_UB(dst + 14 * dst_stride);
    ILVR_B2_SH(zero, dst8, zero, dst9, res8, res9);
    ADD2(res8, out8, res9, out9, res8, res9);
    CLIP_SH2_0_255(res8, res9);
    PCKEV_B2_SH(res8, res8, res9, res9, res8, res9);
    ST8x1_UB(res8, dst + dst_stride);
    ST8x1_UB(res9, dst + 14 * dst_stride);

    k0 = VP9_SET_COSPI_PAIR(cospi_8_64, cospi_24_64);
    k1 = VP9_SET_COSPI_PAIR(cospi_24_64, -cospi_8_64);
    k2 = VP9_SET_COSPI_PAIR(-cospi_24_64, cospi_8_64);
    VP9_MADD_BF(v0, v2, v4, v6, k0, k1, k2, k0, out4, out6, out5, out7);
    out4 = -out4;
    SRARI_H2_SH(out4, out5, 6);
    dst4 = LD_UB(dst + 3 * dst_stride);
    dst5 = LD_UB(dst + 12 * dst_stride);
    ILVR_B2_SH(zero, dst4, zero, dst5, res4, res5);
    ADD2(res4, out4, res5, out5, res4, res5);
    CLIP_SH2_0_255(res4, res5);
    PCKEV_B2_SH(res4, res4, res5, res5, res4, res5);
    ST8x1_UB(res4, dst + 3 * dst_stride);
    ST8x1_UB(res5, dst + 12 * dst_stride);

    VP9_MADD_BF(h1, h3, h5, h7, k0, k1, k2, k0, out12, out14, out13, out15);
    out13 = -out13;
    SRARI_H2_SH(out12, out13, 6);
    dst12 = LD_UB(dst + 2 * dst_stride);
    dst13 = LD_UB(dst + 13 * dst_stride);
    ILVR_B2_SH(zero, dst12, zero, dst13, res12, res13);
    ADD2(res12, out12, res13, out13, res12, res13);
    CLIP_SH2_0_255(res12, res13);
    PCKEV_B2_SH(res12, res12, res13, res13, res12, res13);
    ST8x1_UB(res12, dst + 2 * dst_stride);
    ST8x1_UB(res13, dst + 13 * dst_stride);

    k0 = VP9_SET_COSPI_PAIR(cospi_16_64, cospi_16_64);
    k3 = VP9_SET_COSPI_PAIR(-cospi_16_64, cospi_16_64);
    VP9_MADD_SHORT(out6, out7, k0, k3, out6, out7);
    SRARI_H2_SH(out6, out7, 6);
    dst6 = LD_UB(dst + 4 * dst_stride);
    dst7 = LD_UB(dst + 11 * dst_stride);
    ILVR_B2_SH(zero, dst6, zero, dst7, res6, res7);
    ADD2(res6, out6, res7, out7, res6, res7);
    CLIP_SH2_0_255(res6, res7);
    PCKEV_B2_SH(res6, res6, res7, res7, res6, res7);
    ST8x1_UB(res6, dst + 4 * dst_stride);
    ST8x1_UB(res7, dst + 11 * dst_stride);

    VP9_MADD_SHORT(out10, out11, k0, k3, out10, out11);
    SRARI_H2_SH(out10, out11, 6);
    dst10 = LD_UB(dst + 6 * dst_stride);
    dst11 = LD_UB(dst + 9 * dst_stride);
    ILVR_B2_SH(zero, dst10, zero, dst11, res10, res11);
    ADD2(res10, out10, res11, out11, res10, res11);
    CLIP_SH2_0_255(res10, res11);
    PCKEV_B2_SH(res10, res10, res11, res11, res10, res11);
    ST8x1_UB(res10, dst + 6 * dst_stride);
    ST8x1_UB(res11, dst + 9 * dst_stride);

    k1 = VP9_SET_COSPI_PAIR(-cospi_16_64, -cospi_16_64);
    k2 = VP9_SET_COSPI_PAIR(cospi_16_64, -cospi_16_64);
    VP9_MADD_SHORT(h10, h11, k1, k2, out2, out3);
    SRARI_H2_SH(out2, out3, 6);
    dst2 = LD_UB(dst + 7 * dst_stride);
    dst3 = LD_UB(dst + 8 * dst_stride);
    ILVR_B2_SH(zero, dst2, zero, dst3, res2, res3);
    ADD2(res2, out2, res3, out3, res2, res3);
    CLIP_SH2_0_255(res2, res3);
    PCKEV_B2_SH(res2, res2, res3, res3, res2, res3);
    ST8x1_UB(res2, dst + 7 * dst_stride);
    ST8x1_UB(res3, dst + 8 * dst_stride);

    VP9_MADD_SHORT(out14, out15, k1, k2, out14, out15);
    SRARI_H2_SH(out14, out15, 6);
    dst14 = LD_UB(dst + 5 * dst_stride);
    dst15 = LD_UB(dst + 10 * dst_stride);
    ILVR_B2_SH(zero, dst14, zero, dst15, res14, res15);
    ADD2(res14, out14, res15, out15, res14, res15);
    CLIP_SH2_0_255(res14, res15);
    PCKEV_B2_SH(res14, res14, res15, res15, res14, res15);
    ST8x1_UB(res14, dst + 5 * dst_stride);
    ST8x1_UB(res15, dst + 10 * dst_stride);
}

static void vp9_iadst16x16_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                             int32_t dst_stride)
{
    int16_t out_arr[16 * 16] ALLOC_ALIGNED(ALIGNMENT);
    int16_t *out = out_arr;
    int32_t i;

    /* transform rows */
    for (i = 0; i < 2; i++) {
        /* process 16 * 8 block */
        vp9_iadst16_1d_columns_msa((input + (i << 3)), (out + (i << 7)));
    }

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_iadst16_1d_columns_addblk_msa((out + (i << 3)), (dst + (i << 3)),
                                          dst_stride);
    }
}

static void vp9_iadst_idct_16x16_add_msa(int16_t *input, uint8_t *dst,
                                         int32_t dst_stride, int32_t eob)
{
    int32_t i;
    int16_t out[16 * 16];
    int16_t *out_ptr = &out[0];

    /* transform rows */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_iadst16_1d_columns_msa((input + (i << 3)), (out_ptr + (i << 7)));
    }

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_addblk_msa((out_ptr + (i << 3)),
                                         (dst + (i << 3)), dst_stride);
    }
}

static void vp9_idct_iadst_16x16_add_msa(int16_t *input, uint8_t *dst,
                                         int32_t dst_stride, int32_t eob)
{
    int32_t i;
    int16_t out[16 * 16];
    int16_t *out_ptr = &out[0];

    /* transform rows */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_idct16_1d_columns_msa((input + (i << 3)), (out_ptr + (i << 7)));
    }

    /* transform columns */
    for (i = 0; i < 2; i++) {
        /* process 8 * 16 block */
        vp9_iadst16_1d_columns_addblk_msa((out_ptr + (i << 3)),
                                          (dst + (i << 3)), dst_stride);
    }
}

static void vp9_idct_butterfly_transpose_store(int16_t *tmp_buf,
                                               int16_t *tmp_eve_buf,
                                               int16_t *tmp_odd_buf,
                                               int16_t *dst)
{
    v8i16 vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    v8i16 m0, m1, m2, m3, m4, m5, m6, m7, n0, n1, n2, n3, n4, n5, n6, n7;

    /* FINAL BUTTERFLY : Dependency on Even & Odd */
    vec0 = LD_SH(tmp_odd_buf);
    vec1 = LD_SH(tmp_odd_buf + 9 * 8);
    vec2 = LD_SH(tmp_odd_buf + 14 * 8);
    vec3 = LD_SH(tmp_odd_buf + 6 * 8);
    loc0 = LD_SH(tmp_eve_buf);
    loc1 = LD_SH(tmp_eve_buf + 8 * 8);
    loc2 = LD_SH(tmp_eve_buf + 4 * 8);
    loc3 = LD_SH(tmp_eve_buf + 12 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, m0, m4, m2, m6);

    ST_SH((loc0 - vec3), (tmp_buf + 31 * 8));
    ST_SH((loc1 - vec2), (tmp_buf + 23 * 8));
    ST_SH((loc2 - vec1), (tmp_buf + 27 * 8));
    ST_SH((loc3 - vec0), (tmp_buf + 19 * 8));

    /* Load 8 & Store 8 */
    vec0 = LD_SH(tmp_odd_buf + 4 * 8);
    vec1 = LD_SH(tmp_odd_buf + 13 * 8);
    vec2 = LD_SH(tmp_odd_buf + 10 * 8);
    vec3 = LD_SH(tmp_odd_buf + 3 * 8);
    loc0 = LD_SH(tmp_eve_buf + 2 * 8);
    loc1 = LD_SH(tmp_eve_buf + 10 * 8);
    loc2 = LD_SH(tmp_eve_buf + 6 * 8);
    loc3 = LD_SH(tmp_eve_buf + 14 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, m1, m5, m3, m7);

    ST_SH((loc0 - vec3), (tmp_buf + 29 * 8));
    ST_SH((loc1 - vec2), (tmp_buf + 21 * 8));
    ST_SH((loc2 - vec1), (tmp_buf + 25 * 8));
    ST_SH((loc3 - vec0), (tmp_buf + 17 * 8));

    /* Load 8 & Store 8 */
    vec0 = LD_SH(tmp_odd_buf + 2 * 8);
    vec1 = LD_SH(tmp_odd_buf + 11 * 8);
    vec2 = LD_SH(tmp_odd_buf + 12 * 8);
    vec3 = LD_SH(tmp_odd_buf + 7 * 8);
    loc0 = LD_SH(tmp_eve_buf + 1 * 8);
    loc1 = LD_SH(tmp_eve_buf + 9 * 8);
    loc2 = LD_SH(tmp_eve_buf + 5 * 8);
    loc3 = LD_SH(tmp_eve_buf + 13 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, n0, n4, n2, n6);

    ST_SH((loc0 - vec3), (tmp_buf + 30 * 8));
    ST_SH((loc1 - vec2), (tmp_buf + 22 * 8));
    ST_SH((loc2 - vec1), (tmp_buf + 26 * 8));
    ST_SH((loc3 - vec0), (tmp_buf + 18 * 8));

    /* Load 8 & Store 8 */
    vec0 = LD_SH(tmp_odd_buf + 5 * 8);
    vec1 = LD_SH(tmp_odd_buf + 15 * 8);
    vec2 = LD_SH(tmp_odd_buf + 8 * 8);
    vec3 = LD_SH(tmp_odd_buf + 1 * 8);
    loc0 = LD_SH(tmp_eve_buf + 3 * 8);
    loc1 = LD_SH(tmp_eve_buf + 11 * 8);
    loc2 = LD_SH(tmp_eve_buf + 7 * 8);
    loc3 = LD_SH(tmp_eve_buf + 15 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, n1, n5, n3, n7);

    ST_SH((loc0 - vec3), (tmp_buf + 28 * 8));
    ST_SH((loc1 - vec2), (tmp_buf + 20 * 8));
    ST_SH((loc2 - vec1), (tmp_buf + 24 * 8));
    ST_SH((loc3 - vec0), (tmp_buf + 16 * 8));

    /* Transpose : 16 vectors */
    /* 1st & 2nd 8x8 */
    TRANSPOSE8x8_SH_SH(m0, n0, m1, n1, m2, n2, m3, n3,
                       m0, n0, m1, n1, m2, n2, m3, n3);
    ST_SH4(m0, n0, m1, n1, (dst + 0), 32);
    ST_SH4(m2, n2, m3, n3, (dst + 4 * 32), 32);

    TRANSPOSE8x8_SH_SH(m4, n4, m5, n5, m6, n6, m7, n7,
                       m4, n4, m5, n5, m6, n6, m7, n7);
    ST_SH4(m4, n4, m5, n5, (dst + 8), 32);
    ST_SH4(m6, n6, m7, n7, (dst + 8 + 4 * 32), 32);

    /* 3rd & 4th 8x8 */
    LD_SH8((tmp_buf + 8 * 16), 8, m0, n0, m1, n1, m2, n2, m3, n3);
    LD_SH8((tmp_buf + 12 * 16), 8, m4, n4, m5, n5, m6, n6, m7, n7);
    TRANSPOSE8x8_SH_SH(m0, n0, m1, n1, m2, n2, m3, n3,
                       m0, n0, m1, n1, m2, n2, m3, n3);
    ST_SH4(m0, n0, m1, n1, (dst + 16), 32);
    ST_SH4(m2, n2, m3, n3, (dst + 16 + 4 * 32), 32);

    TRANSPOSE8x8_SH_SH(m4, n4, m5, n5, m6, n6, m7, n7,
                       m4, n4, m5, n5, m6, n6, m7, n7);
    ST_SH4(m4, n4, m5, n5, (dst + 24), 32);
    ST_SH4(m6, n6, m7, n7, (dst + 24 + 4 * 32), 32);
}

static void vp9_idct8x32_column_even_process_store(int16_t *tmp_buf,
                                                   int16_t *tmp_eve_buf)
{
    v8i16 vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    v8i16 reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7;
    v8i16 stp0, stp1, stp2, stp3, stp4, stp5, stp6, stp7;

    /* Even stage 1 */
    LD_SH8(tmp_buf, (4 * 32), reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);
    tmp_buf += (2 * 32);

    VP9_DOTP_CONST_PAIR(reg1, reg7, cospi_28_64, cospi_4_64, reg1, reg7);
    VP9_DOTP_CONST_PAIR(reg5, reg3, cospi_12_64, cospi_20_64, reg5, reg3);
    BUTTERFLY_4(reg1, reg7, reg3, reg5, vec1, vec3, vec2, vec0);
    VP9_DOTP_CONST_PAIR(vec2, vec0, cospi_16_64, cospi_16_64, loc2, loc3);

    loc1 = vec3;
    loc0 = vec1;

    VP9_DOTP_CONST_PAIR(reg0, reg4, cospi_16_64, cospi_16_64, reg0, reg4);
    VP9_DOTP_CONST_PAIR(reg2, reg6, cospi_24_64, cospi_8_64, reg2, reg6);
    BUTTERFLY_4(reg4, reg0, reg2, reg6, vec1, vec3, vec2, vec0);
    BUTTERFLY_4(vec0, vec1, loc1, loc0, stp3, stp0, stp7, stp4);
    BUTTERFLY_4(vec2, vec3, loc3, loc2, stp2, stp1, stp6, stp5);

    /* Even stage 2 */
    /* Load 8 */
    LD_SH8(tmp_buf, (4 * 32), reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);

    VP9_DOTP_CONST_PAIR(reg0, reg7, cospi_30_64, cospi_2_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(reg4, reg3, cospi_14_64, cospi_18_64, reg4, reg3);
    VP9_DOTP_CONST_PAIR(reg2, reg5, cospi_22_64, cospi_10_64, reg2, reg5);
    VP9_DOTP_CONST_PAIR(reg6, reg1, cospi_6_64, cospi_26_64, reg6, reg1);

    vec0 = reg0 + reg4;
    reg0 = reg0 - reg4;
    reg4 = reg6 + reg2;
    reg6 = reg6 - reg2;
    reg2 = reg1 + reg5;
    reg1 = reg1 - reg5;
    reg5 = reg7 + reg3;
    reg7 = reg7 - reg3;
    reg3 = vec0;

    vec1 = reg2;
    reg2 = reg3 + reg4;
    reg3 = reg3 - reg4;
    reg4 = reg5 - vec1;
    reg5 = reg5 + vec1;

    VP9_DOTP_CONST_PAIR(reg7, reg0, cospi_24_64, cospi_8_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR((-reg6), reg1, cospi_24_64, cospi_8_64, reg6, reg1);

    vec0 = reg0 - reg6;
    reg0 = reg0 + reg6;
    vec1 = reg7 - reg1;
    reg7 = reg7 + reg1;

    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, reg6, reg1);
    VP9_DOTP_CONST_PAIR(reg4, reg3, cospi_16_64, cospi_16_64, reg3, reg4);

    /* Even stage 3 : Dependency on Even stage 1 & Even stage 2 */
    /* Store 8 */
    BUTTERFLY_4(stp0, stp1, reg7, reg5, loc1, loc3, loc2, loc0);
    ST_SH2(loc1, loc3, tmp_eve_buf, 8);
    ST_SH2(loc2, loc0, (tmp_eve_buf + 14 * 8), 8);

    BUTTERFLY_4(stp2, stp3, reg4, reg1, loc1, loc3, loc2, loc0);
    ST_SH2(loc1, loc3, (tmp_eve_buf + 2 * 8), 8);
    ST_SH2(loc2, loc0, (tmp_eve_buf + 12 * 8), 8);

    /* Store 8 */
    BUTTERFLY_4(stp4, stp5, reg6, reg3, loc1, loc3, loc2, loc0);
    ST_SH2(loc1, loc3, (tmp_eve_buf + 4 * 8), 8);
    ST_SH2(loc2, loc0, (tmp_eve_buf + 10 * 8), 8);

    BUTTERFLY_4(stp6, stp7, reg2, reg0, loc1, loc3, loc2, loc0);
    ST_SH2(loc1, loc3, (tmp_eve_buf + 6 * 8), 8);
    ST_SH2(loc2, loc0, (tmp_eve_buf + 8 * 8), 8);
}

static void vp9_idct8x32_column_odd_process_store(int16_t *tmp_buf,
                                                  int16_t *tmp_odd_buf)
{
    v8i16 vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    v8i16 reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7;

    /* Odd stage 1 */
    reg0 = LD_SH(tmp_buf + 32);
    reg1 = LD_SH(tmp_buf + 7 * 32);
    reg2 = LD_SH(tmp_buf + 9 * 32);
    reg3 = LD_SH(tmp_buf + 15 * 32);
    reg4 = LD_SH(tmp_buf + 17 * 32);
    reg5 = LD_SH(tmp_buf + 23 * 32);
    reg6 = LD_SH(tmp_buf + 25 * 32);
    reg7 = LD_SH(tmp_buf + 31 * 32);

    VP9_DOTP_CONST_PAIR(reg0, reg7, cospi_31_64, cospi_1_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(reg4, reg3, cospi_15_64, cospi_17_64, reg3, reg4);
    VP9_DOTP_CONST_PAIR(reg2, reg5, cospi_23_64, cospi_9_64, reg2, reg5);
    VP9_DOTP_CONST_PAIR(reg6, reg1, cospi_7_64, cospi_25_64, reg1, reg6);

    vec0 = reg0 + reg3;
    reg0 = reg0 - reg3;
    reg3 = reg7 + reg4;
    reg7 = reg7 - reg4;
    reg4 = reg1 + reg2;
    reg1 = reg1 - reg2;
    reg2 = reg6 + reg5;
    reg6 = reg6 - reg5;
    reg5 = vec0;

    /* 4 Stores */
    ADD2(reg5, reg4, reg3, reg2, vec0, vec1);
    ST_SH2(vec0, vec1, (tmp_odd_buf + 4 * 8), 8);
    SUB2(reg5, reg4, reg3, reg2, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_24_64, cospi_8_64, vec0, vec1);
    ST_SH2(vec0, vec1, tmp_odd_buf, 8);

    /* 4 Stores */
    VP9_DOTP_CONST_PAIR(reg7, reg0, cospi_28_64, cospi_4_64, reg0, reg7);
    VP9_DOTP_CONST_PAIR(reg6, reg1, -cospi_4_64, cospi_28_64, reg1, reg6);
    BUTTERFLY_4(reg0, reg7, reg6, reg1, vec0, vec1, vec2, vec3);
    ST_SH2(vec0, vec1, (tmp_odd_buf + 6 * 8), 8);
    VP9_DOTP_CONST_PAIR(vec2, vec3, cospi_24_64, cospi_8_64, vec2, vec3);
    ST_SH2(vec2, vec3, (tmp_odd_buf + 2 * 8), 8);

    /* Odd stage 2 */
    /* 8 loads */
    reg0 = LD_SH(tmp_buf + 3 * 32);
    reg1 = LD_SH(tmp_buf + 5 * 32);
    reg2 = LD_SH(tmp_buf + 11 * 32);
    reg3 = LD_SH(tmp_buf + 13 * 32);
    reg4 = LD_SH(tmp_buf + 19 * 32);
    reg5 = LD_SH(tmp_buf + 21 * 32);
    reg6 = LD_SH(tmp_buf + 27 * 32);
    reg7 = LD_SH(tmp_buf + 29 * 32);

    VP9_DOTP_CONST_PAIR(reg1, reg6, cospi_27_64, cospi_5_64, reg1, reg6);
    VP9_DOTP_CONST_PAIR(reg5, reg2, cospi_11_64, cospi_21_64, reg2, reg5);
    VP9_DOTP_CONST_PAIR(reg3, reg4, cospi_19_64, cospi_13_64, reg3, reg4);
    VP9_DOTP_CONST_PAIR(reg7, reg0, cospi_3_64, cospi_29_64, reg0, reg7);

    /* 4 Stores */
    SUB4(reg1, reg2, reg6, reg5, reg0, reg3, reg7, reg4,
         vec0, vec1, vec2, vec3);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_12_64, cospi_20_64, loc0, loc1);
    VP9_DOTP_CONST_PAIR(vec3, vec2, -cospi_20_64, cospi_12_64, loc2, loc3);
    BUTTERFLY_4(loc2, loc3, loc1, loc0, vec0, vec1, vec3, vec2);
    ST_SH2(vec0, vec1, (tmp_odd_buf + 12 * 8), 3 * 8);
    VP9_DOTP_CONST_PAIR(vec3, vec2, -cospi_8_64, cospi_24_64, vec0, vec1);
    ST_SH2(vec0, vec1, (tmp_odd_buf + 10 * 8), 8);

    /* 4 Stores */
    ADD4(reg0, reg3, reg1, reg2, reg5, reg6, reg4, reg7,
         vec0, vec1, vec2, vec3);
    BUTTERFLY_4(vec0, vec3, vec2, vec1, reg0, reg1, reg3, reg2);
    ST_SH2(reg0, reg1, (tmp_odd_buf + 13 * 8), 8);
    VP9_DOTP_CONST_PAIR(reg3, reg2, -cospi_8_64, cospi_24_64, reg0, reg1);
    ST_SH2(reg0, reg1, (tmp_odd_buf + 8 * 8), 8);

    /* Odd stage 3 : Dependency on Odd stage 1 & Odd stage 2 */
    /* Load 8 & Store 8 */
    LD_SH4(tmp_odd_buf, 8, reg0, reg1, reg2, reg3);
    LD_SH4((tmp_odd_buf + 8 * 8), 8, reg4, reg5, reg6, reg7);

    ADD4(reg0, reg4, reg1, reg5, reg2, reg6, reg3, reg7,
         loc0, loc1, loc2, loc3);
    ST_SH4(loc0, loc1, loc2, loc3, tmp_odd_buf, 8);

    SUB2(reg0, reg4, reg1, reg5, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc0, loc1);

    SUB2(reg2, reg6, reg3, reg7, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc2, loc3);
    ST_SH4(loc0, loc1, loc2, loc3, (tmp_odd_buf + 8 * 8), 8);

    /* Load 8 & Store 8 */
    LD_SH4((tmp_odd_buf + 4 * 8), 8, reg1, reg2, reg0, reg3);
    LD_SH4((tmp_odd_buf + 12 * 8), 8, reg4, reg5, reg6, reg7);

    ADD4(reg0, reg4, reg1, reg5, reg2, reg6, reg3, reg7,
         loc0, loc1, loc2, loc3);
    ST_SH4(loc0, loc1, loc2, loc3, (tmp_odd_buf + 4 * 8), 8);

    SUB2(reg0, reg4, reg3, reg7, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc0, loc1);

    SUB2(reg1, reg5, reg2, reg6, vec0, vec1);
    VP9_DOTP_CONST_PAIR(vec1, vec0, cospi_16_64, cospi_16_64, loc2, loc3);
    ST_SH4(loc0, loc1, loc2, loc3, (tmp_odd_buf + 12 * 8), 8);
}

static void vp9_idct8x32_column_butterfly_addblk(int16_t *tmp_eve_buf,
                                                 int16_t *tmp_odd_buf,
                                                 uint8_t *dst,
                                                 int32_t dst_stride)
{
    v8i16 vec0, vec1, vec2, vec3, loc0, loc1, loc2, loc3;
    v8i16 m0, m1, m2, m3, m4, m5, m6, m7, n0, n1, n2, n3, n4, n5, n6, n7;

    /* FINAL BUTTERFLY : Dependency on Even & Odd */
    vec0 = LD_SH(tmp_odd_buf);
    vec1 = LD_SH(tmp_odd_buf + 9 * 8);
    vec2 = LD_SH(tmp_odd_buf + 14 * 8);
    vec3 = LD_SH(tmp_odd_buf + 6 * 8);
    loc0 = LD_SH(tmp_eve_buf);
    loc1 = LD_SH(tmp_eve_buf + 8 * 8);
    loc2 = LD_SH(tmp_eve_buf + 4 * 8);
    loc3 = LD_SH(tmp_eve_buf + 12 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, m0, m4, m2, m6);
    SRARI_H4_SH(m0, m2, m4, m6, 6);
    VP9_ADDBLK_ST8x4_UB(dst, (4 * dst_stride), m0, m2, m4, m6);

    SUB4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, m6, m2, m4, m0);
    SRARI_H4_SH(m0, m2, m4, m6, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 19 * dst_stride), (4 * dst_stride),
                        m0, m2, m4, m6);

    /* Load 8 & Store 8 */
    vec0 = LD_SH(tmp_odd_buf + 4 * 8);
    vec1 = LD_SH(tmp_odd_buf + 13 * 8);
    vec2 = LD_SH(tmp_odd_buf + 10 * 8);
    vec3 = LD_SH(tmp_odd_buf + 3 * 8);
    loc0 = LD_SH(tmp_eve_buf + 2 * 8);
    loc1 = LD_SH(tmp_eve_buf + 10 * 8);
    loc2 = LD_SH(tmp_eve_buf + 6 * 8);
    loc3 = LD_SH(tmp_eve_buf + 14 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, m1, m5, m3, m7);
    SRARI_H4_SH(m1, m3, m5, m7, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 2 * dst_stride), (4 * dst_stride),
                        m1, m3, m5, m7);

    SUB4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, m7, m3, m5, m1);
    SRARI_H4_SH(m1, m3, m5, m7, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 17 * dst_stride), (4 * dst_stride),
                        m1, m3, m5, m7);

    /* Load 8 & Store 8 */
    vec0 = LD_SH(tmp_odd_buf + 2 * 8);
    vec1 = LD_SH(tmp_odd_buf + 11 * 8);
    vec2 = LD_SH(tmp_odd_buf + 12 * 8);
    vec3 = LD_SH(tmp_odd_buf + 7 * 8);
    loc0 = LD_SH(tmp_eve_buf + 1 * 8);
    loc1 = LD_SH(tmp_eve_buf + 9 * 8);
    loc2 = LD_SH(tmp_eve_buf + 5 * 8);
    loc3 = LD_SH(tmp_eve_buf + 13 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, n0, n4, n2, n6);
    SRARI_H4_SH(n0, n2, n4, n6, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 1 * dst_stride), (4 * dst_stride),
                        n0, n2, n4, n6);

    SUB4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, n6, n2, n4, n0);
    SRARI_H4_SH(n0, n2, n4, n6, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 18 * dst_stride), (4 * dst_stride),
                        n0, n2, n4, n6);

    /* Load 8 & Store 8 */
    vec0 = LD_SH(tmp_odd_buf + 5 * 8);
    vec1 = LD_SH(tmp_odd_buf + 15 * 8);
    vec2 = LD_SH(tmp_odd_buf + 8 * 8);
    vec3 = LD_SH(tmp_odd_buf + 1 * 8);
    loc0 = LD_SH(tmp_eve_buf + 3 * 8);
    loc1 = LD_SH(tmp_eve_buf + 11 * 8);
    loc2 = LD_SH(tmp_eve_buf + 7 * 8);
    loc3 = LD_SH(tmp_eve_buf + 15 * 8);

    ADD4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, n1, n5, n3, n7);
    SRARI_H4_SH(n1, n3, n5, n7, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 3 * dst_stride), (4 * dst_stride),
                        n1, n3, n5, n7);

    SUB4(loc0, vec3, loc1, vec2, loc2, vec1, loc3, vec0, n7, n3, n5, n1);
    SRARI_H4_SH(n1, n3, n5, n7, 6);
    VP9_ADDBLK_ST8x4_UB((dst + 16 * dst_stride), (4 * dst_stride),
                        n1, n3, n5, n7);
}

static void vp9_idct8x32_1d_columns_addblk_msa(int16_t *input, uint8_t *dst,
                                               int32_t dst_stride)
{
    int16_t tmp_odd_buf[16 * 8] ALLOC_ALIGNED(ALIGNMENT);
    int16_t tmp_eve_buf[16 * 8] ALLOC_ALIGNED(ALIGNMENT);

    vp9_idct8x32_column_even_process_store(input, &tmp_eve_buf[0]);
    vp9_idct8x32_column_odd_process_store(input, &tmp_odd_buf[0]);
    vp9_idct8x32_column_butterfly_addblk(&tmp_eve_buf[0], &tmp_odd_buf[0],
                                         dst, dst_stride);
}

static void vp9_idct8x32_1d_columns_msa(int16_t *input, int16_t *output,
                                        int16_t *tmp_buf)
{
    int16_t tmp_odd_buf[16 * 8] ALLOC_ALIGNED(ALIGNMENT);
    int16_t tmp_eve_buf[16 * 8] ALLOC_ALIGNED(ALIGNMENT);

    vp9_idct8x32_column_even_process_store(input, &tmp_eve_buf[0]);
    vp9_idct8x32_column_odd_process_store(input, &tmp_odd_buf[0]);
    vp9_idct_butterfly_transpose_store(tmp_buf, &tmp_eve_buf[0],
                                       &tmp_odd_buf[0], output);
}

static void vp9_idct32x32_1_add_msa(int16_t *input, uint8_t *dst,
                                    int32_t dst_stride)
{
    int32_t i;
    int16_t out;
    v16u8 dst0, dst1, dst2, dst3, tmp0, tmp1, tmp2, tmp3;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7, vec;

    out = ROUND_POWER_OF_TWO((input[0] * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO((out * cospi_16_64), VP9_DCT_CONST_BITS);
    out = ROUND_POWER_OF_TWO(out, 6);

    vec = __msa_fill_h(out);

    for (i = 16; i--;)
    {
        LD_UB2(dst, 16, dst0, dst1);
        LD_UB2(dst + dst_stride, 16, dst2, dst3);

        UNPCK_UB_SH(dst0, res0, res4);
        UNPCK_UB_SH(dst1, res1, res5);
        UNPCK_UB_SH(dst2, res2, res6);
        UNPCK_UB_SH(dst3, res3, res7);
        ADD4(res0, vec, res1, vec, res2, vec, res3, vec, res0, res1, res2,
             res3);
        ADD4(res4, vec, res5, vec, res6, vec, res7, vec, res4, res5, res6,
             res7);
        CLIP_SH4_0_255(res0, res1, res2, res3);
        CLIP_SH4_0_255(res4, res5, res6, res7);
        PCKEV_B4_UB(res4, res0, res5, res1, res6, res2, res7, res3,
                    tmp0, tmp1, tmp2, tmp3);

        ST_UB2(tmp0, tmp1, dst, 16);
        dst += dst_stride;
        ST_UB2(tmp2, tmp3, dst, 16);
        dst += dst_stride;
    }
}

static void vp9_idct32x32_34_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                               int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[32 * 32] ALLOC_ALIGNED(ALIGNMENT);
    int16_t *out_ptr = out_arr;
    int16_t tmp_buf[8 * 32] ALLOC_ALIGNED(ALIGNMENT);

    for (i = 32; i--;) {
        __asm__ volatile (
            "sw     $zero,       (%[out_ptr])     \n\t"
            "sw     $zero,      4(%[out_ptr])     \n\t"
            "sw     $zero,      8(%[out_ptr])     \n\t"
            "sw     $zero,     12(%[out_ptr])     \n\t"
            "sw     $zero,     16(%[out_ptr])     \n\t"
            "sw     $zero,     20(%[out_ptr])     \n\t"
            "sw     $zero,     24(%[out_ptr])     \n\t"
            "sw     $zero,     28(%[out_ptr])     \n\t"
            "sw     $zero,     32(%[out_ptr])     \n\t"
            "sw     $zero,     36(%[out_ptr])     \n\t"
            "sw     $zero,     40(%[out_ptr])     \n\t"
            "sw     $zero,     44(%[out_ptr])     \n\t"
            "sw     $zero,     48(%[out_ptr])     \n\t"
            "sw     $zero,     52(%[out_ptr])     \n\t"
            "sw     $zero,     56(%[out_ptr])     \n\t"
            "sw     $zero,     60(%[out_ptr])     \n\t"

            :
            : [out_ptr] "r" (out_ptr)
        );

        out_ptr += 32;
    }

    out_ptr = out_arr;

    /* process 8*32 block */
    vp9_idct8x32_1d_columns_msa(input, out_ptr, &tmp_buf[0]);

    /* transform columns */
    for (i = 0; i < 4; i++) {
        /* process 8*32 block */
        vp9_idct8x32_1d_columns_addblk_msa((out_ptr + (i << 3)),
                                           (dst + (i << 3)), dst_stride);
    }
}

static void vp9_idct32x32_colcol_addblk_msa(int16_t *input, uint8_t *dst,
                                            int32_t dst_stride)
{
    int32_t i;
    int16_t out_arr[32 * 32] ALLOC_ALIGNED(ALIGNMENT);
    int16_t *out_ptr = out_arr;
    int16_t tmp_buf[8 * 32] ALLOC_ALIGNED(ALIGNMENT);

    /* transform rows */
    for (i = 0; i < 4; i++) {
        /* process 8*32 block */
        vp9_idct8x32_1d_columns_msa((input + (i << 3)), (out_ptr + (i << 8)),
                                    &tmp_buf[0]);
    }

    /* transform columns */
    for (i = 0; i < 4; i++) {
        /* process 8*32 block */
        vp9_idct8x32_1d_columns_addblk_msa((out_ptr + (i << 3)),
                                           (dst + (i << 3)), dst_stride);
    }
}

void ff_idct_idct_4x4_add_msa(uint8_t *dst, ptrdiff_t stride,
                              int16_t *block, int eob)
{
    if (eob > 1) {
        vp9_idct4x4_colcol_addblk_msa(block, dst, stride);
        memset(block, 0, 4 * 4 * sizeof(*block));
    }
    else {
        vp9_idct4x4_1_add_msa(block, dst, stride);
        block[0] = 0;
    }
}

void ff_idct_idct_8x8_add_msa(uint8_t *dst, ptrdiff_t stride,
                              int16_t *block, int eob)
{
    if (eob == 1) {
        vp9_idct8x8_1_add_msa(block, dst, stride);
        block[0] = 0;
    }
    else if (eob <= 12) {
        vp9_idct8x8_12_colcol_addblk_msa(block, dst, stride);
        memset(block, 0, 4 * 8 * sizeof(*block));
    }
    else {
        vp9_idct8x8_colcol_addblk_msa(block, dst, stride);
        memset(block, 0, 8 * 8 * sizeof(*block));
    }
}

void ff_idct_idct_16x16_add_msa(uint8_t *dst, ptrdiff_t stride,
                                int16_t *block, int eob)
{
    int i;

    if (eob == 1) {
        /* DC only DCT coefficient. */
        vp9_idct16x16_1_add_msa(block, dst, stride);
        block[0] = 0;
    }
    else if (eob <= 10) {
        vp9_idct16x16_10_colcol_addblk_msa(block, dst, stride);
        for (i = 0; i < 4; ++i) {
            memset(block, 0, 4 * sizeof(*block));
            block += 16;
        }
    }
    else {
        vp9_idct16x16_colcol_addblk_msa(block, dst, stride);
        memset(block, 0, 16 * 16 * sizeof(*block));
    }
}

void ff_idct_idct_32x32_add_msa(uint8_t *dst, ptrdiff_t stride,
                                int16_t *block, int eob)
{
    int i;

    if (eob == 1) {
        vp9_idct32x32_1_add_msa(block, dst, stride);
        block[0] = 0;
    }
    else if (eob <= 34) {
        vp9_idct32x32_34_colcol_addblk_msa(block, dst, stride);
        for (i = 0; i < 8; ++i) {
            memset(block, 0, 8 * sizeof(*block));
            block += 32;
        }
    }
    else {
        vp9_idct32x32_colcol_addblk_msa(block, dst, stride);
        memset(block, 0, 32 * 32 * sizeof(*block));
    }
}

void ff_iadst_iadst_4x4_add_msa(uint8_t *dst, ptrdiff_t stride,
                                int16_t *block, int eob)
{
    vp9_iadst4x4_colcol_addblk_msa(block, dst, stride);
    memset(block, 0, 4 * 4 * sizeof(*block));
}

void ff_iadst_iadst_8x8_add_msa(uint8_t *dst, ptrdiff_t stride,
                                int16_t *block, int eob)
{
    vp9_iadst8x8_colcol_addblk_msa(block, dst, stride);
    memset(block, 0, 8 * 8 * sizeof(*block));
}

void ff_iadst_iadst_16x16_add_msa(uint8_t *dst, ptrdiff_t stride,
                                  int16_t *block, int eob)
{
    vp9_iadst16x16_colcol_addblk_msa(block, dst, stride);
    memset(block, 0, 16 * 16 * sizeof(*block));
}

void ff_idct_iadst_4x4_add_msa(uint8_t *dst, ptrdiff_t stride,
                               int16_t *block, int eob)
{
    vp9_idct_iadst_4x4_add_msa(block, dst, stride, eob);
    memset(block, 0, 4 * 4 * sizeof(*block));
}

void ff_idct_iadst_8x8_add_msa(uint8_t *dst, ptrdiff_t stride,
                               int16_t *block, int eob)
{
    vp9_idct_iadst_8x8_add_msa(block, dst, stride, eob);
    memset(block, 0, 8 * 8 * sizeof(*block));
}

void ff_idct_iadst_16x16_add_msa(uint8_t *dst, ptrdiff_t stride,
                                 int16_t *block, int eob)
{
    vp9_idct_iadst_16x16_add_msa(block, dst, stride, eob);
    memset(block, 0, 16 * 16 * sizeof(*block));
}

void ff_iadst_idct_4x4_add_msa(uint8_t *dst, ptrdiff_t stride,
                               int16_t *block, int eob)
{
    vp9_iadst_idct_4x4_add_msa(block, dst, stride, eob);
    memset(block, 0, 4 * 4 * sizeof(*block));
}

void ff_iadst_idct_8x8_add_msa(uint8_t *dst, ptrdiff_t stride,
                               int16_t *block, int eob)
{
    vp9_iadst_idct_8x8_add_msa(block, dst, stride, eob);
    memset(block, 0, 8 * 8 * sizeof(*block));
}

void ff_iadst_idct_16x16_add_msa(uint8_t *dst, ptrdiff_t stride,
                                 int16_t *block, int eob)
{
    vp9_iadst_idct_16x16_add_msa(block, dst, stride, eob);
    memset(block, 0, 16 * 16 * sizeof(*block));
}
