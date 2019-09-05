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
    v4i32 vec0, vec1, vec2, vec3, vec4, vec5;                 \
    v4i32 cnst64 = __msa_ldi_w(64);                           \
    v4i32 cnst83 = __msa_ldi_w(83);                           \
    v4i32 cnst36 = __msa_ldi_w(36);                           \
                                                              \
    DOTP_SH4_SW(in_r0, in_r1, in_l0, in_l1, cnst64, cnst64,   \
                cnst83, cnst36, vec0, vec2, vec1, vec3);      \
    DOTP_SH2_SW(in_l0, in_l1, cnst36, cnst83, vec4, vec5);    \
                                                              \
    sum0 = vec0 + vec2;                                       \
    sum1 = vec0 - vec2;                                       \
    sum3 = sum0;                                              \
    sum2 = sum1;                                              \
                                                              \
    vec1 += vec3;                                             \
    vec4 -= vec5;                                             \
                                                              \
    sum0 += vec1;                                             \
    sum1 += vec4;                                             \
    sum2 -= vec4;                                             \
    sum3 -= vec1;                                             \
                                                              \
    SRARI_W4_SW(sum0, sum1, sum2, sum3, shift);               \
    SAT_SW4_SW(sum0, sum1, sum2, sum3, 15);                   \
}

#define HEVC_IDCT8x8_COL(in0, in1, in2, in3, in4, in5, in6, in7, shift)  \
{                                                                        \
    v8i16 src0_r, src1_r, src2_r, src3_r;                                \
    v8i16 src0_l, src1_l, src2_l, src3_l;                                \
    v8i16 filt0, filter0, filter1, filter2, filter3;                     \
    v4i32 temp0_r, temp1_r, temp2_r, temp3_r, temp4_r, temp5_r;          \
    v4i32 temp0_l, temp1_l, temp2_l, temp3_l, temp4_l, temp5_l;          \
    v4i32 sum0_r, sum1_r, sum2_r, sum3_r;                                \
    v4i32 sum0_l, sum1_l, sum2_l, sum3_l;                                \
                                                                         \
    ILVR_H4_SH(in4, in0, in6, in2, in5, in1, in3, in7,                   \
               src0_r, src1_r, src2_r, src3_r);                          \
    ILVL_H4_SH(in4, in0, in6, in2, in5, in1, in3, in7,                   \
               src0_l, src1_l, src2_l, src3_l);                          \
                                                                         \
    filt0 = LD_SH(filter);                                               \
    SPLATI_W4_SH(filt0, filter0, filter1, filter2, filter3);             \
    DOTP_SH4_SW(src0_r, src0_l, src1_r, src1_l, filter0, filter0,        \
                filter1, filter1, temp0_r, temp0_l, temp1_r, temp1_l);   \
                                                                         \
    BUTTERFLY_4(temp0_r, temp0_l, temp1_l, temp1_r, sum0_r, sum0_l,      \
                sum1_l, sum1_r);                                         \
    sum2_r = sum1_r;                                                     \
    sum2_l = sum1_l;                                                     \
    sum3_r = sum0_r;                                                     \
    sum3_l = sum0_l;                                                     \
                                                                         \
    DOTP_SH4_SW(src2_r, src2_l, src3_r, src3_l,  filter2, filter2,       \
                filter3, filter3, temp2_r, temp2_l, temp3_r, temp3_l);   \
                                                                         \
    temp2_r += temp3_r;                                                  \
    temp2_l += temp3_l;                                                  \
    sum0_r += temp2_r;                                                   \
    sum0_l += temp2_l;                                                   \
    sum3_r -= temp2_r;                                                   \
    sum3_l -= temp2_l;                                                   \
                                                                         \
    SRARI_W4_SW(sum0_r, sum0_l, sum3_r, sum3_l, shift);                  \
    SAT_SW4_SW(sum0_r, sum0_l, sum3_r, sum3_l, 15);                      \
    PCKEV_H2_SH(sum0_l, sum0_r, sum3_l, sum3_r, in0, in7);               \
    DOTP_SH4_SW(src2_r, src2_l, src3_r, src3_l,  filter3, filter3,       \
                filter2, filter2, temp4_r, temp4_l, temp5_r, temp5_l);   \
                                                                         \
    temp4_r -= temp5_r;                                                  \
    temp4_l -= temp5_l;                                                  \
    sum1_r += temp4_r;                                                   \
    sum1_l += temp4_l;                                                   \
    sum2_r -= temp4_r;                                                   \
    sum2_l -= temp4_l;                                                   \
                                                                         \
    SRARI_W4_SW(sum1_r, sum1_l, sum2_r, sum2_l, shift);                  \
    SAT_SW4_SW(sum1_r, sum1_l, sum2_r, sum2_l, 15);                      \
    PCKEV_H2_SH(sum1_l, sum1_r, sum2_l, sum2_r, in3, in4);               \
                                                                         \
    filt0 = LD_SH(filter + 8);                                           \
    SPLATI_W4_SH(filt0, filter0, filter1, filter2, filter3);             \
    DOTP_SH4_SW(src0_r, src0_l, src1_r, src1_l,  filter0, filter0,       \
                filter1, filter1, temp0_r, temp0_l, temp1_r, temp1_l);   \
                                                                         \
    BUTTERFLY_4(temp0_r, temp0_l, temp1_l, temp1_r, sum0_r, sum0_l,      \
                sum1_l, sum1_r);                                         \
    sum2_r = sum1_r;                                                     \
    sum2_l = sum1_l;                                                     \
    sum3_r = sum0_r;                                                     \
    sum3_l = sum0_l;                                                     \
                                                                         \
    DOTP_SH4_SW(src2_r, src2_l, src3_r, src3_l, filter2, filter2,        \
                filter3, filter3, temp2_r, temp2_l, temp3_r, temp3_l);   \
                                                                         \
    temp2_r += temp3_r;                                                  \
    temp2_l += temp3_l;                                                  \
    sum0_r += temp2_r;                                                   \
    sum0_l += temp2_l;                                                   \
    sum3_r -= temp2_r;                                                   \
    sum3_l -= temp2_l;                                                   \
                                                                         \
    SRARI_W4_SW(sum0_r, sum0_l, sum3_r, sum3_l, shift);                  \
    SAT_SW4_SW(sum0_r, sum0_l, sum3_r, sum3_l, 15);                      \
    PCKEV_H2_SH(sum0_l, sum0_r, sum3_l, sum3_r, in1, in6);               \
    DOTP_SH4_SW(src2_r, src2_l, src3_r, src3_l, filter3, filter3,        \
                filter2, filter2, temp4_r, temp4_l, temp5_r, temp5_l);   \
                                                                         \
    temp4_r -= temp5_r;                                                  \
    temp4_l -= temp5_l;                                                  \
    sum1_r -= temp4_r;                                                   \
    sum1_l -= temp4_l;                                                   \
    sum2_r += temp4_r;                                                   \
    sum2_l += temp4_l;                                                   \
                                                                         \
    SRARI_W4_SW(sum1_r, sum1_l, sum2_r, sum2_l, shift);                  \
    SAT_SW4_SW(sum1_r, sum1_l, sum2_r, sum2_l, 15);                      \
    PCKEV_H2_SH(sum1_l, sum1_r, sum2_l, sum2_r, in2, in5);               \
}

#define HEVC_IDCT16x16_COL(src0_r, src1_r, src2_r, src3_r,                \
                           src4_r, src5_r, src6_r, src7_r,                \
                           src0_l, src1_l, src2_l, src3_l,                \
                           src4_l, src5_l, src6_l, src7_l, shift)         \
{                                                                         \
    int16_t *ptr0, *ptr1;                                                 \
    v8i16 filt0, filt1, dst0, dst1;                                       \
    v8i16 filter0, filter1, filter2, filter3;                             \
    v4i32 temp0_r, temp1_r, temp0_l, temp1_l;                             \
    v4i32 sum0_r, sum1_r, sum2_r, sum3_r, sum0_l, sum1_l, sum2_l;         \
    v4i32 sum3_l, res0_r, res1_r, res0_l, res1_l;                         \
                                                                          \
    ptr0 = (buf_ptr + 112);                                               \
    ptr1 = (buf_ptr + 128);                                               \
    k = -1;                                                               \
                                                                          \
    for (j = 0; j < 4; j++)                                               \
    {                                                                     \
        LD_SH2(filter, 8, filt0, filt1)                                   \
        filter += 16;                                                     \
        SPLATI_W2_SH(filt0, 0, filter0, filter1);                         \
        SPLATI_W2_SH(filt1, 0, filter2, filter3);                         \
        DOTP_SH4_SW(src0_r, src0_l, src4_r, src4_l,  filter0, filter0,    \
                    filter2, filter2, sum0_r, sum0_l, sum2_r, sum2_l);    \
        DOTP_SH2_SW(src7_r, src7_l, filter2, filter2, sum3_r, sum3_l);    \
        DPADD_SH4_SW(src1_r, src1_l, src5_r, src5_l,  filter1, filter1,   \
                     filter3, filter3, sum0_r, sum0_l, sum2_r, sum2_l);   \
        DPADD_SH2_SW(src6_r, src6_l, filter3, filter3, sum3_r, sum3_l);   \
                                                                          \
        sum1_r = sum0_r;                                                  \
        sum1_l = sum0_l;                                                  \
                                                                          \
        SPLATI_W2_SH(filt0, 2, filter0, filter1);                         \
        SPLATI_W2_SH(filt1, 2, filter2, filter3);                         \
        DOTP_SH2_SW(src2_r, src2_l, filter0, filter0, temp0_r, temp0_l);  \
        DPADD_SH2_SW(src6_r, src6_l, filter2, filter2, sum2_r, sum2_l);   \
        DOTP_SH2_SW(src5_r, src5_l, filter2, filter2, temp1_r, temp1_l);  \
                                                                          \
        sum0_r += temp0_r;                                                \
        sum0_l += temp0_l;                                                \
        sum1_r -= temp0_r;                                                \
        sum1_l -= temp0_l;                                                \
                                                                          \
        sum3_r = temp1_r - sum3_r;                                        \
        sum3_l = temp1_l - sum3_l;                                        \
                                                                          \
        DOTP_SH2_SW(src3_r, src3_l, filter1, filter1, temp0_r, temp0_l);  \
        DPADD_SH4_SW(src7_r, src7_l, src4_r, src4_l, filter3, filter3,    \
                     filter3, filter3, sum2_r, sum2_l, sum3_r, sum3_l);   \
                                                                          \
        sum0_r += temp0_r;                                                \
        sum0_l += temp0_l;                                                \
        sum1_r -= temp0_r;                                                \
        sum1_l -= temp0_l;                                                \
                                                                          \
        BUTTERFLY_4(sum0_r, sum0_l, sum2_l, sum2_r, res0_r, res0_l,       \
                    res1_l, res1_r);                                      \
        SRARI_W4_SW(res0_r, res0_l, res1_r, res1_l, shift);               \
        SAT_SW4_SW(res0_r, res0_l, res1_r, res1_l, 15);                   \
        PCKEV_H2_SH(res0_l, res0_r, res1_l, res1_r, dst0, dst1);          \
        ST_SH(dst0, buf_ptr);                                             \
        ST_SH(dst1, (buf_ptr + ((15 - (j * 2)) * 16)));                   \
                                                                          \
        BUTTERFLY_4(sum1_r, sum1_l, sum3_l, sum3_r, res0_r, res0_l,       \
                    res1_l, res1_r);                                      \
        SRARI_W4_SW(res0_r, res0_l, res1_r, res1_l, shift);               \
        SAT_SW4_SW(res0_r, res0_l, res1_r, res1_l, 15);                   \
        PCKEV_H2_SH(res0_l, res0_r, res1_l, res1_r, dst0, dst1);          \
        ST_SH(dst0, (ptr0 + (((j / 2 + j % 2) * 2 * k) * 16)));           \
        ST_SH(dst1, (ptr1 - (((j / 2 + j % 2) * 2 * k) * 16)));           \
                                                                          \
        k *= -1;                                                          \
        buf_ptr += 16;                                                    \
    }                                                                     \
}

#define HEVC_EVEN16_CALC(input, sum0_r, sum0_l, load_idx, store_idx)  \
{                                                                     \
    LD_SW2(input + load_idx * 8, 4, tmp0_r, tmp0_l);                  \
    tmp1_r = sum0_r;                                                  \
    tmp1_l = sum0_l;                                                  \
    sum0_r += tmp0_r;                                                 \
    sum0_l += tmp0_l;                                                 \
    ST_SW2(sum0_r, sum0_l, (input + load_idx * 8), 4);                \
    tmp1_r -= tmp0_r;                                                 \
    tmp1_l -= tmp0_l;                                                 \
    ST_SW2(tmp1_r, tmp1_l, (input + store_idx * 8), 4);               \
}

#define HEVC_IDCT_LUMA4x4_COL(in_r0, in_l0, in_r1, in_l1,     \
                              res0, res1, res2, res3, shift)  \
{                                                             \
    v4i32 vec0, vec1, vec2, vec3;                             \
    v4i32 cnst74 = __msa_ldi_w(74);                           \
    v4i32 cnst55 = __msa_ldi_w(55);                           \
    v4i32 cnst29 = __msa_ldi_w(29);                           \
                                                              \
    vec0 = in_r0 + in_r1;                                     \
    vec2 = in_r0 - in_l1;                                     \
    res0 = vec0 * cnst29;                                     \
    res1 = vec2 * cnst55;                                     \
    res2 = in_r0 - in_r1;                                     \
    vec1 = in_r1 + in_l1;                                     \
    res2 += in_l1;                                            \
    vec3 = in_l0 * cnst74;                                    \
    res3 = vec0 * cnst55;                                     \
                                                              \
    res0 += vec1 * cnst55;                                    \
    res1 -= vec1 * cnst29;                                    \
    res2 *= cnst74;                                           \
    res3 += vec2 * cnst29;                                    \
                                                              \
    res0 += vec3;                                             \
    res1 += vec3;                                             \
    res3 -= vec3;                                             \
                                                              \
    SRARI_W4_SW(res0, res1, res2, res3, shift);               \
    SAT_SW4_SW(res0, res1, res2, res3, 15);                   \
}

static void hevc_idct_4x4_msa(int16_t *coeffs)
{
    v8i16 in0, in1;
    v4i32 in_r0, in_l0, in_r1, in_l1;
    v4i32 sum0, sum1, sum2, sum3;
    v8i16 zeros = { 0 };

    LD_SH2(coeffs, 8, in0, in1);
    ILVRL_H2_SW(zeros, in0, in_r0, in_l0);
    ILVRL_H2_SW(zeros, in1, in_r1, in_l1);

    HEVC_IDCT4x4_COL(in_r0, in_l0, in_r1, in_l1, sum0, sum1, sum2, sum3, 7);
    TRANSPOSE4x4_SW_SW(sum0, sum1, sum2, sum3, in_r0, in_l0, in_r1, in_l1);
    HEVC_IDCT4x4_COL(in_r0, in_l0, in_r1, in_l1, sum0, sum1, sum2, sum3, 12);

    /* Pack and transpose */
    PCKEV_H2_SH(sum2, sum0, sum3, sum1, in0, in1);
    ILVRL_H2_SW(in1, in0, sum0, sum1);
    ILVRL_W2_SH(sum1, sum0, in0, in1);

    ST_SH2(in0, in1, coeffs, 8);
}

static void hevc_idct_8x8_msa(int16_t *coeffs)
{
    const int16_t *filter = &gt8x8_cnst[0];
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    LD_SH8(coeffs, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    HEVC_IDCT8x8_COL(in0, in1, in2, in3, in4, in5, in6, in7, 7);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    HEVC_IDCT8x8_COL(in0, in1, in2, in3, in4, in5, in6, in7, 12);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       in0, in1, in2, in3, in4, in5, in6, in7);
    ST_SH8(in0, in1, in2, in3, in4, in5, in6, in7, coeffs, 8);
}

static void hevc_idct_16x16_msa(int16_t *coeffs)
{
    int16_t i, j, k;
    int16_t buf[256];
    int16_t *buf_ptr = &buf[0];
    int16_t *src = coeffs;
    const int16_t *filter = &gt16x16_cnst[0];
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 in8, in9, in10, in11, in12, in13, in14, in15;
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8i16 src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l, src7_l;

    for (i = 2; i--;) {
        LD_SH16(src, 16, in0, in1, in2, in3, in4, in5, in6, in7,
                in8, in9, in10, in11, in12, in13, in14, in15);

        ILVR_H4_SH(in4, in0, in12, in8, in6, in2, in14, in10,
                   src0_r, src1_r, src2_r, src3_r);
        ILVR_H4_SH(in5, in1, in13, in9, in3, in7, in11, in15,
                   src4_r, src5_r, src6_r, src7_r);
        ILVL_H4_SH(in4, in0, in12, in8, in6, in2, in14, in10,
                   src0_l, src1_l, src2_l, src3_l);
        ILVL_H4_SH(in5, in1, in13, in9, in3, in7, in11, in15,
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
        LD_SH16(src, 8, in0, in8, in1, in9, in2, in10, in3, in11,
                in4, in12, in5, in13, in6, in14, in7, in15);
        TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                           in0, in1, in2, in3, in4, in5, in6, in7);
        TRANSPOSE8x8_SH_SH(in8, in9, in10, in11, in12, in13, in14, in15,
                           in8, in9, in10, in11, in12, in13, in14, in15);
        ILVR_H4_SH(in4, in0, in12, in8, in6, in2, in14, in10,
                   src0_r, src1_r, src2_r, src3_r);
        ILVR_H4_SH(in5, in1, in13, in9, in3, in7, in11, in15,
                   src4_r, src5_r, src6_r, src7_r);
        ILVL_H4_SH(in4, in0, in12, in8, in6, in2, in14, in10,
                   src0_l, src1_l, src2_l, src3_l);
        ILVL_H4_SH(in5, in1, in13, in9, in3, in7, in11, in15,
                   src4_l, src5_l, src6_l, src7_l);
        HEVC_IDCT16x16_COL(src0_r, src1_r, src2_r, src3_r, src4_r, src5_r,
                           src6_r, src7_r, src0_l, src1_l, src2_l, src3_l,
                           src4_l, src5_l, src6_l, src7_l, 12);

        src += 128;
        buf_ptr = coeffs + 8;
        filter = &gt16x16_cnst[0];
    }

    LD_SH8(coeffs, 16, in0, in1, in2, in3, in4, in5, in6, in7);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    ST_SH8(vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, coeffs, 16);

    LD_SH8((coeffs + 8), 16, in0, in1, in2, in3, in4, in5, in6, in7);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    LD_SH8((coeffs + 128), 16, in8, in9, in10, in11, in12, in13, in14, in15);
    ST_SH8(vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, (coeffs + 128), 16);
    TRANSPOSE8x8_SH_SH(in8, in9, in10, in11, in12, in13, in14, in15,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    ST_SH8(vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, (coeffs + 8), 16);

    LD_SH8((coeffs + 136), 16, in0, in1, in2, in3, in4, in5, in6, in7);
    TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                       vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7);
    ST_SH8(vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, (coeffs + 136), 16);
}

static void hevc_idct_8x32_column_msa(int16_t *coeffs, uint8_t buf_pitch,
                                      uint8_t round)
{
    uint8_t i;
    const int16_t *filter_ptr0 = &gt32x32_cnst0[0];
    const int16_t *filter_ptr1 = &gt32x32_cnst1[0];
    const int16_t *filter_ptr2 = &gt32x32_cnst2[0];
    const int16_t *filter_ptr3 = &gt8x8_cnst[0];
    int16_t *src0 = (coeffs + buf_pitch);
    int16_t *src1 = (coeffs + 2 * buf_pitch);
    int16_t *src2 = (coeffs + 4 * buf_pitch);
    int16_t *src3 = (coeffs);
    int32_t cnst0, cnst1;
    int32_t tmp_buf[8 * 32 + 15];
    int32_t *tmp_buf_ptr = tmp_buf + 15;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v8i16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8i16 src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l, src7_l;
    v8i16 filt0, filter0, filter1, filter2, filter3;
    v4i32 sum0_r, sum0_l, sum1_r, sum1_l, tmp0_r, tmp0_l, tmp1_r, tmp1_l;

    /* Align pointer to 64 byte boundary */
    tmp_buf_ptr = (int32_t *)(((uintptr_t) tmp_buf_ptr) & ~(uintptr_t) 63);

    /* process coeff 4, 12, 20, 28 */
    LD_SH4(src2, 8 * buf_pitch, in0, in1, in2, in3);
    ILVR_H2_SH(in1, in0, in3, in2, src0_r, src1_r);
    ILVL_H2_SH(in1, in0, in3, in2, src0_l, src1_l);

    LD_SH2(src3, 16 * buf_pitch, in4, in6);
    LD_SH2((src3 + 8 * buf_pitch), 16 * buf_pitch, in5, in7);
    ILVR_H2_SH(in6, in4, in7, in5, src2_r, src3_r);
    ILVL_H2_SH(in6, in4, in7, in5, src2_l, src3_l);

    /* loop for all columns of constants */
    for (i = 0; i < 2; i++) {
        /* processing single column of constants */
        cnst0 = LW(filter_ptr2);
        cnst1 = LW(filter_ptr2 + 2);

        filter0 = (v8i16) __msa_fill_w(cnst0);
        filter1 = (v8i16) __msa_fill_w(cnst1);

        DOTP_SH2_SW(src0_r, src0_l, filter0, filter0, sum0_r, sum0_l);
        DPADD_SH2_SW(src1_r, src1_l, filter1, filter1, sum0_r, sum0_l);
        ST_SW2(sum0_r, sum0_l, (tmp_buf_ptr + 2 * i * 8), 4);

        /* processing single column of constants */
        cnst0 = LW(filter_ptr2 + 4);
        cnst1 = LW(filter_ptr2 + 6);

        filter0 = (v8i16) __msa_fill_w(cnst0);
        filter1 = (v8i16) __msa_fill_w(cnst1);

        DOTP_SH2_SW(src0_r, src0_l, filter0, filter0, sum0_r, sum0_l);
        DPADD_SH2_SW(src1_r, src1_l, filter1, filter1, sum0_r, sum0_l);
        ST_SW2(sum0_r, sum0_l, (tmp_buf_ptr + (2 * i + 1) * 8), 4);

        filter_ptr2 += 8;
    }

    /* process coeff 0, 8, 16, 24 */
    /* loop for all columns of constants */
    for (i = 0; i < 2; i++) {
        /* processing first column of filter constants */
        cnst0 = LW(filter_ptr3);
        cnst1 = LW(filter_ptr3 + 2);

        filter0 = (v8i16) __msa_fill_w(cnst0);
        filter1 = (v8i16) __msa_fill_w(cnst1);

        DOTP_SH4_SW(src2_r, src2_l, src3_r, src3_l, filter0, filter0, filter1,
                    filter1, sum0_r, sum0_l, tmp1_r, tmp1_l);

        sum1_r = sum0_r - tmp1_r;
        sum1_l = sum0_l - tmp1_l;
        sum0_r = sum0_r + tmp1_r;
        sum0_l = sum0_l + tmp1_l;

        HEVC_EVEN16_CALC(tmp_buf_ptr, sum0_r, sum0_l, i, (7 - i));
        HEVC_EVEN16_CALC(tmp_buf_ptr, sum1_r, sum1_l, (3 - i), (4 + i));

        filter_ptr3 += 8;
    }

    /* process coeff 2 6 10 14 18 22 26 30 */
    LD_SH8(src1, 4 * buf_pitch, in0, in1, in2, in3, in4, in5, in6, in7);
    ILVR_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,
               src0_r, src1_r, src2_r, src3_r);
    ILVL_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,
               src0_l, src1_l, src2_l, src3_l);

    /* loop for all columns of constants */
    for (i = 0; i < 8; i++) {
        /* processing single column of constants */
        filt0 = LD_SH(filter_ptr1);
        SPLATI_W4_SH(filt0, filter0, filter1, filter2, filter3);
        DOTP_SH2_SW(src0_r, src0_l, filter0, filter0, sum0_r, sum0_l);
        DPADD_SH4_SW(src1_r, src1_l, src2_r, src2_l, filter1, filter1, filter2,
                     filter2, sum0_r, sum0_l, sum0_r, sum0_l);
        DPADD_SH2_SW(src3_r, src3_l, filter3, filter3, sum0_r, sum0_l);

        LD_SW2(tmp_buf_ptr + i * 8, 4, tmp0_r, tmp0_l);
        tmp1_r = tmp0_r;
        tmp1_l = tmp0_l;
        tmp0_r += sum0_r;
        tmp0_l += sum0_l;
        ST_SW2(tmp0_r, tmp0_l, (tmp_buf_ptr + i * 8), 4);
        tmp1_r -= sum0_r;
        tmp1_l -= sum0_l;
        ST_SW2(tmp1_r, tmp1_l, (tmp_buf_ptr + (15 - i) * 8), 4);

        filter_ptr1 += 8;
    }

    /* process coeff 1 3 5 7 9 11 13 15 17 19 21 23 25 27 29 31 */
    LD_SH8(src0, 2 * buf_pitch, in0, in1, in2, in3, in4, in5, in6, in7);
    src0 += 16 * buf_pitch;
    ILVR_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,
               src0_r, src1_r, src2_r, src3_r);
    ILVL_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,
               src0_l, src1_l, src2_l, src3_l);

    LD_SH8(src0, 2 * buf_pitch, in0, in1, in2, in3, in4, in5, in6, in7);
    ILVR_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,
               src4_r, src5_r, src6_r, src7_r);
    ILVL_H4_SH(in1, in0, in3, in2, in5, in4, in7, in6,
               src4_l, src5_l, src6_l, src7_l);

    /* loop for all columns of filter constants */
    for (i = 0; i < 16; i++) {
        /* processing single column of constants */
        filt0 = LD_SH(filter_ptr0);
        SPLATI_W4_SH(filt0, filter0, filter1, filter2, filter3);
        DOTP_SH2_SW(src0_r, src0_l, filter0, filter0, sum0_r, sum0_l);
        DPADD_SH4_SW(src1_r, src1_l, src2_r, src2_l, filter1, filter1, filter2,
                     filter2, sum0_r, sum0_l, sum0_r, sum0_l);
        DPADD_SH2_SW(src3_r, src3_l, filter3, filter3, sum0_r, sum0_l);

        tmp1_r = sum0_r;
        tmp1_l = sum0_l;

        filt0 = LD_SH(filter_ptr0 + 8);
        SPLATI_W4_SH(filt0, filter0, filter1, filter2, filter3);
        DOTP_SH2_SW(src4_r, src4_l, filter0, filter0, sum0_r, sum0_l);
        DPADD_SH4_SW(src5_r, src5_l, src6_r, src6_l, filter1, filter1, filter2,
                     filter2, sum0_r, sum0_l, sum0_r, sum0_l);
        DPADD_SH2_SW(src7_r, src7_l, filter3, filter3, sum0_r, sum0_l);

        sum0_r += tmp1_r;
        sum0_l += tmp1_l;

        LD_SW2(tmp_buf_ptr + i * 8, 4, tmp0_r, tmp0_l);
        tmp1_r = tmp0_r;
        tmp1_l = tmp0_l;
        tmp0_r += sum0_r;
        tmp0_l += sum0_l;
        sum1_r = __msa_fill_w(round);
        SRAR_W2_SW(tmp0_r, tmp0_l, sum1_r);
        SAT_SW2_SW(tmp0_r, tmp0_l, 15);
        in0 = __msa_pckev_h((v8i16) tmp0_l, (v8i16) tmp0_r);
        ST_SH(in0, (coeffs + i * buf_pitch));
        tmp1_r -= sum0_r;
        tmp1_l -= sum0_l;
        SRAR_W2_SW(tmp1_r, tmp1_l, sum1_r);
        SAT_SW2_SW(tmp1_r, tmp1_l, 15);
        in0 = __msa_pckev_h((v8i16) tmp1_l, (v8i16) tmp1_r);
        ST_SH(in0, (coeffs + (31 - i) * buf_pitch));

        filter_ptr0 += 16;
    }
}

static void hevc_idct_transpose_32x8_to_8x32(int16_t *coeffs, int16_t *tmp_buf)
{
    uint8_t i;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    for (i = 0; i < 4; i++) {
        LD_SH8(coeffs + i * 8, 32, in0, in1, in2, in3, in4, in5, in6, in7);
        TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                           in0, in1, in2, in3, in4, in5, in6, in7);
        ST_SH8(in0, in1, in2, in3, in4, in5, in6, in7, tmp_buf + i * 8 * 8, 8);
    }
}

static void hevc_idct_transpose_8x32_to_32x8(int16_t *tmp_buf, int16_t *coeffs)
{
    uint8_t i;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    for (i = 0; i < 4; i++) {
        LD_SH8(tmp_buf + i * 8 * 8, 8, in0, in1, in2, in3, in4, in5, in6, in7);
        TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                           in0, in1, in2, in3, in4, in5, in6, in7);
        ST_SH8(in0, in1, in2, in3, in4, in5, in6, in7, coeffs + i * 8, 32);
    }
}

static void hevc_idct_32x32_msa(int16_t *coeffs)
{
    uint8_t row_cnt, col_cnt;
    int16_t *src = coeffs;
    int16_t tmp_buf[8 * 32 + 31];
    int16_t *tmp_buf_ptr = tmp_buf + 31;
    uint8_t round;
    uint8_t buf_pitch;

    /* Align pointer to 64 byte boundary */
    tmp_buf_ptr = (int16_t *)(((uintptr_t) tmp_buf_ptr) & ~(uintptr_t) 63);

    /* column transform */
    round = 7;
    buf_pitch = 32;
    for (col_cnt = 0; col_cnt < 4; col_cnt++) {
        /* process 8x32 blocks */
        hevc_idct_8x32_column_msa((coeffs + col_cnt * 8), buf_pitch, round);
    }

    /* row transform */
    round = 12;
    buf_pitch = 8;
    for (row_cnt = 0; row_cnt < 4; row_cnt++) {
        /* process 32x8 blocks */
        src = (coeffs + 32 * 8 * row_cnt);

        hevc_idct_transpose_32x8_to_8x32(src, tmp_buf_ptr);
        hevc_idct_8x32_column_msa(tmp_buf_ptr, buf_pitch, round);
        hevc_idct_transpose_8x32_to_32x8(tmp_buf_ptr, src);
    }
}

static void hevc_idct_dc_4x4_msa(int16_t *coeffs)
{
    int32_t val;
    v8i16 dst;

    val = (coeffs[0] + 1) >> 1;
    val = (val + 32) >> 6;
    dst = __msa_fill_h(val);

    ST_SH2(dst, dst, coeffs, 8);
}

static void hevc_idct_dc_8x8_msa(int16_t *coeffs)
{
    int32_t val;
    v8i16 dst;

    val = (coeffs[0] + 1) >> 1;
    val = (val + 32) >> 6;
    dst = __msa_fill_h(val);

    ST_SH8(dst, dst, dst, dst, dst, dst, dst, dst, coeffs, 8);
}

static void hevc_idct_dc_16x16_msa(int16_t *coeffs)
{
    uint8_t loop;
    int32_t val;
    v8i16 dst;

    val = (coeffs[0] + 1) >> 1;
    val = (val + 32) >> 6;
    dst = __msa_fill_h(val);

    for (loop = 4; loop--;) {
        ST_SH8(dst, dst, dst, dst, dst, dst, dst, dst, coeffs, 8);
        coeffs += 8 * 8;
    }
}

static void hevc_idct_dc_32x32_msa(int16_t *coeffs)
{
    uint8_t loop;
    int32_t val;
    v8i16 dst;

    val = (coeffs[0] + 1) >> 1;
    val = (val + 32) >> 6;
    dst = __msa_fill_h(val);

    for (loop = 16; loop--;) {
        ST_SH8(dst, dst, dst, dst, dst, dst, dst, dst, coeffs, 8);
        coeffs += 8 * 8;
    }
}

static void hevc_addblk_4x4_msa(int16_t *coeffs, uint8_t *dst, int32_t stride)
{
    uint32_t dst0, dst1, dst2, dst3;
    v8i16 dst_r0, dst_l0, in0, in1;
    v4i32 dst_vec = { 0 };
    v16u8 zeros = { 0 };

    LD_SH2(coeffs, 8, in0, in1);
    LW4(dst, stride, dst0, dst1, dst2, dst3);
    INSERT_W4_SW(dst0, dst1, dst2, dst3, dst_vec);
    ILVRL_B2_SH(zeros, dst_vec, dst_r0, dst_l0);
    ADD2(dst_r0, in0, dst_l0, in1, dst_r0, dst_l0);
    CLIP_SH2_0_255(dst_r0, dst_l0);
    dst_vec = (v4i32) __msa_pckev_b((v16i8) dst_l0, (v16i8) dst_r0);
    ST_W4(dst_vec, 0, 1, 2, 3, dst, stride);
}

static void hevc_addblk_8x8_msa(int16_t *coeffs, uint8_t *dst, int32_t stride)
{
    uint8_t *temp_dst = dst;
    uint64_t dst0, dst1, dst2, dst3;
    v2i64 dst_vec0 = { 0 };
    v2i64 dst_vec1 = { 0 };
    v8i16 dst_r0, dst_l0, dst_r1, dst_l1;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16u8 zeros = { 0 };

    LD_SH8(coeffs, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    LD4(temp_dst, stride, dst0, dst1, dst2, dst3);
    temp_dst += (4 * stride);

    INSERT_D2_SD(dst0, dst1, dst_vec0);
    INSERT_D2_SD(dst2, dst3, dst_vec1);
    ILVRL_B2_SH(zeros, dst_vec0, dst_r0, dst_l0);
    ILVRL_B2_SH(zeros, dst_vec1, dst_r1, dst_l1);
    ADD4(dst_r0, in0, dst_l0, in1, dst_r1, in2, dst_l1, in3,
         dst_r0, dst_l0, dst_r1, dst_l1);
    CLIP_SH4_0_255(dst_r0, dst_l0, dst_r1, dst_l1);
    PCKEV_B2_SH(dst_l0, dst_r0, dst_l1, dst_r1, dst_r0, dst_r1);
    ST_D4(dst_r0, dst_r1, 0, 1, 0, 1, dst, stride);

    LD4(temp_dst, stride, dst0, dst1, dst2, dst3);
    INSERT_D2_SD(dst0, dst1, dst_vec0);
    INSERT_D2_SD(dst2, dst3, dst_vec1);
    UNPCK_UB_SH(dst_vec0, dst_r0, dst_l0);
    UNPCK_UB_SH(dst_vec1, dst_r1, dst_l1);
    ADD4(dst_r0, in4, dst_l0, in5, dst_r1, in6, dst_l1, in7,
         dst_r0, dst_l0, dst_r1, dst_l1);
    CLIP_SH4_0_255(dst_r0, dst_l0, dst_r1, dst_l1);
    PCKEV_B2_SH(dst_l0, dst_r0, dst_l1, dst_r1, dst_r0, dst_r1);
    ST_D4(dst_r0, dst_r1, 0, 1, 0, 1, dst + 4 * stride, stride);
}

static void hevc_addblk_16x16_msa(int16_t *coeffs, uint8_t *dst, int32_t stride)
{
    uint8_t loop_cnt;
    uint8_t *temp_dst = dst;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 dst_r0, dst_l0, dst_r1, dst_l1, dst_r2, dst_l2, dst_r3, dst_l3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    /* Pre-load for next iteration */
    LD_UB4(temp_dst, stride, dst4, dst5, dst6, dst7);
    temp_dst += (4 * stride);
    LD_SH4(coeffs, 16, in0, in2, in4, in6);
    LD_SH4((coeffs + 8), 16, in1, in3, in5, in7);
    coeffs += 64;

    for (loop_cnt = 3; loop_cnt--;) {
        UNPCK_UB_SH(dst4, dst_r0, dst_l0);
        UNPCK_UB_SH(dst5, dst_r1, dst_l1);
        UNPCK_UB_SH(dst6, dst_r2, dst_l2);
        UNPCK_UB_SH(dst7, dst_r3, dst_l3);

        dst_r0 += in0;
        dst_l0 += in1;
        dst_r1 += in2;
        dst_l1 += in3;
        dst_r2 += in4;
        dst_l2 += in5;
        dst_r3 += in6;
        dst_l3 += in7;

        /* Pre-load for next iteration */
        LD_UB4(temp_dst, stride, dst4, dst5, dst6, dst7);
        temp_dst += (4 * stride);
        LD_SH4(coeffs, 16, in0, in2, in4, in6);
        LD_SH4((coeffs + 8), 16, in1, in3, in5, in7);
        coeffs += 64;

        CLIP_SH8_0_255(dst_r0, dst_l0, dst_r1, dst_l1,
                       dst_r2, dst_l2, dst_r3, dst_l3);

        PCKEV_B4_UB(dst_l0, dst_r0, dst_l1, dst_r1, dst_l2, dst_r2, dst_l3,
                    dst_r3, dst0, dst1, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }

    UNPCK_UB_SH(dst4, dst_r0, dst_l0);
    UNPCK_UB_SH(dst5, dst_r1, dst_l1);
    UNPCK_UB_SH(dst6, dst_r2, dst_l2);
    UNPCK_UB_SH(dst7, dst_r3, dst_l3);

    dst_r0 += in0;
    dst_l0 += in1;
    dst_r1 += in2;
    dst_l1 += in3;
    dst_r2 += in4;
    dst_l2 += in5;
    dst_r3 += in6;
    dst_l3 += in7;

    CLIP_SH8_0_255(dst_r0, dst_l0, dst_r1, dst_l1,
                   dst_r2, dst_l2, dst_r3, dst_l3);
    PCKEV_B4_UB(dst_l0, dst_r0, dst_l1, dst_r1, dst_l2, dst_r2, dst_l3,
                dst_r3, dst0, dst1, dst2, dst3);
    ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
}

static void hevc_addblk_32x32_msa(int16_t *coeffs, uint8_t *dst, int32_t stride)
{
    uint8_t loop_cnt;
    uint8_t *temp_dst = dst;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 dst_r0, dst_l0, dst_r1, dst_l1, dst_r2, dst_l2, dst_r3, dst_l3;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    /* Pre-load for next iteration */
    LD_UB2(temp_dst, 16, dst4, dst5);
    temp_dst += stride;
    LD_UB2(temp_dst, 16, dst6, dst7);
    temp_dst += stride;
    LD_SH4(coeffs, 16, in0, in2, in4, in6);
    LD_SH4((coeffs + 8), 16, in1, in3, in5, in7);
    coeffs += 64;

    for (loop_cnt = 14; loop_cnt--;) {
        UNPCK_UB_SH(dst4, dst_r0, dst_l0);
        UNPCK_UB_SH(dst5, dst_r1, dst_l1);
        UNPCK_UB_SH(dst6, dst_r2, dst_l2);
        UNPCK_UB_SH(dst7, dst_r3, dst_l3);

        dst_r0 += in0;
        dst_l0 += in1;
        dst_r1 += in2;
        dst_l1 += in3;
        dst_r2 += in4;
        dst_l2 += in5;
        dst_r3 += in6;
        dst_l3 += in7;

        /* Pre-load for next iteration */
        LD_UB2(temp_dst, 16, dst4, dst5);
        temp_dst += stride;
        LD_UB2(temp_dst, 16, dst6, dst7);
        temp_dst += stride;
        LD_SH4(coeffs, 16, in0, in2, in4, in6);
        LD_SH4((coeffs + 8), 16, in1, in3, in5, in7);
        coeffs += 64;

        CLIP_SH8_0_255(dst_r0, dst_l0, dst_r1, dst_l1,
                       dst_r2, dst_l2, dst_r3, dst_l3);
        PCKEV_B4_UB(dst_l0, dst_r0, dst_l1, dst_r1, dst_l2, dst_r2, dst_l3,
                    dst_r3, dst0, dst1, dst2, dst3);
        ST_UB2(dst0, dst1, dst, 16);
        dst += stride;
        ST_UB2(dst2, dst3, dst, 16);
        dst += stride;
    }

    UNPCK_UB_SH(dst4, dst_r0, dst_l0);
    UNPCK_UB_SH(dst5, dst_r1, dst_l1);
    UNPCK_UB_SH(dst6, dst_r2, dst_l2);
    UNPCK_UB_SH(dst7, dst_r3, dst_l3);

    dst_r0 += in0;
    dst_l0 += in1;
    dst_r1 += in2;
    dst_l1 += in3;
    dst_r2 += in4;
    dst_l2 += in5;
    dst_r3 += in6;
    dst_l3 += in7;

    /* Pre-load for next iteration */
    LD_UB2(temp_dst, 16, dst4, dst5);
    temp_dst += stride;
    LD_UB2(temp_dst, 16, dst6, dst7);
    temp_dst += stride;
    LD_SH4(coeffs, 16, in0, in2, in4, in6);
    LD_SH4((coeffs + 8), 16, in1, in3, in5, in7);

    CLIP_SH8_0_255(dst_r0, dst_l0, dst_r1, dst_l1,
                   dst_r2, dst_l2, dst_r3, dst_l3);
    PCKEV_B4_UB(dst_l0, dst_r0, dst_l1, dst_r1, dst_l2, dst_r2, dst_l3,
                dst_r3, dst0, dst1, dst2, dst3);
    ST_UB2(dst0, dst1, dst, 16);
    dst += stride;
    ST_UB2(dst2, dst3, dst, 16);
    dst += stride;

    UNPCK_UB_SH(dst4, dst_r0, dst_l0);
    UNPCK_UB_SH(dst5, dst_r1, dst_l1);
    UNPCK_UB_SH(dst6, dst_r2, dst_l2);
    UNPCK_UB_SH(dst7, dst_r3, dst_l3);

    dst_r0 += in0;
    dst_l0 += in1;
    dst_r1 += in2;
    dst_l1 += in3;
    dst_r2 += in4;
    dst_l2 += in5;
    dst_r3 += in6;
    dst_l3 += in7;

    CLIP_SH8_0_255(dst_r0, dst_l0, dst_r1, dst_l1,
                   dst_r2, dst_l2, dst_r3, dst_l3);
    PCKEV_B4_UB(dst_l0, dst_r0, dst_l1, dst_r1, dst_l2, dst_r2, dst_l3,
                dst_r3, dst0, dst1, dst2, dst3);
    ST_UB2(dst0, dst1, dst, 16);
    dst += stride;
    ST_UB2(dst2, dst3, dst, 16);
}

static void hevc_idct_luma_4x4_msa(int16_t *coeffs)
{
    v8i16 in0, in1, dst0, dst1;
    v4i32 in_r0, in_l0, in_r1, in_l1, res0, res1, res2, res3;

    LD_SH2(coeffs, 8, in0, in1);
    UNPCK_SH_SW(in0, in_r0, in_l0);
    UNPCK_SH_SW(in1, in_r1, in_l1);
    HEVC_IDCT_LUMA4x4_COL(in_r0, in_l0, in_r1, in_l1, res0, res1, res2, res3,
                          7);
    TRANSPOSE4x4_SW_SW(res0, res1, res2, res3, in_r0, in_l0, in_r1, in_l1);
    HEVC_IDCT_LUMA4x4_COL(in_r0, in_l0, in_r1, in_l1, res0, res1, res2, res3,
                          12);

    /* Pack and transpose */
    PCKEV_H2_SH(res2, res0, res3, res1, dst0, dst1);
    ILVRL_H2_SW(dst1, dst0, res0, res1);
    ILVRL_W2_SH(res1, res0, dst0, dst1);

    ST_SH2(dst0, dst1, coeffs, 8);
}

void ff_hevc_idct_4x4_msa(int16_t *coeffs, int col_limit)
{
    hevc_idct_4x4_msa(coeffs);
}

void ff_hevc_idct_8x8_msa(int16_t *coeffs, int col_limit)
{
    hevc_idct_8x8_msa(coeffs);
}

void ff_hevc_idct_16x16_msa(int16_t *coeffs, int col_limit)
{
    hevc_idct_16x16_msa(coeffs);
}

void ff_hevc_idct_32x32_msa(int16_t *coeffs, int col_limit)
{
    hevc_idct_32x32_msa(coeffs);
}

void ff_hevc_addblk_4x4_msa(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride)
{
    hevc_addblk_4x4_msa(coeffs, dst, stride);
}

void ff_hevc_addblk_8x8_msa(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride)
{
    hevc_addblk_8x8_msa(coeffs, dst, stride);
}

void ff_hevc_addblk_16x16_msa(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride)
{
    hevc_addblk_16x16_msa(coeffs, dst, stride);
}

void ff_hevc_addblk_32x32_msa(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride)
{
    hevc_addblk_32x32_msa(coeffs, dst, stride);
}

void ff_hevc_idct_dc_4x4_msa(int16_t *coeffs)
{
    hevc_idct_dc_4x4_msa(coeffs);
}

void ff_hevc_idct_dc_8x8_msa(int16_t *coeffs)
{
    hevc_idct_dc_8x8_msa(coeffs);
}

void ff_hevc_idct_dc_16x16_msa(int16_t *coeffs)
{
    hevc_idct_dc_16x16_msa(coeffs);
}

void ff_hevc_idct_dc_32x32_msa(int16_t *coeffs)
{
    hevc_idct_dc_32x32_msa(coeffs);
}

void ff_hevc_idct_luma_4x4_msa(int16_t *coeffs)
{
    hevc_idct_luma_4x4_msa(coeffs);
}
