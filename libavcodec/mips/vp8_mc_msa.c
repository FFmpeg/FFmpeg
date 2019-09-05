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

#include "libavcodec/vp8dsp.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "vp8dsp_mips.h"

static const uint8_t mc_filt_mask_arr[16 * 3] = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
};

static const int8_t subpel_filters_msa[7][8] = {
    {-6, 123, 12, -1, 0, 0, 0, 0},
    {2, -11, 108, 36, -8, 1, 0, 0},     /* New 1/4 pel 6 tap filter */
    {-9, 93, 50, -6, 0, 0, 0, 0},
    {3, -16, 77, 77, -16, 3, 0, 0},     /* New 1/2 pel 6 tap filter */
    {-6, 50, 93, -9, 0, 0, 0, 0},
    {1, -8, 36, 108, -11, 2, 0, 0},     /* New 1/4 pel 6 tap filter */
    {-1, 12, 123, -6, 0, 0, 0, 0},
};

static const int8_t bilinear_filters_msa[7][2] = {
    {112, 16},
    {96, 32},
    {80, 48},
    {64, 64},
    {48, 80},
    {32, 96},
    {16, 112}
};

#define HORIZ_6TAP_FILT(src0, src1, mask0, mask1, mask2,                 \
                        filt_h0, filt_h1, filt_h2)                       \
( {                                                                      \
    v16i8 vec0_m, vec1_m, vec2_m;                                        \
    v8i16 hz_out_m;                                                      \
                                                                         \
    VSHF_B3_SB(src0, src1, src0, src1, src0, src1, mask0, mask1, mask2,  \
               vec0_m, vec1_m, vec2_m);                                  \
    hz_out_m = DPADD_SH3_SH(vec0_m, vec1_m, vec2_m,                      \
                            filt_h0, filt_h1, filt_h2);                  \
                                                                         \
    hz_out_m = __msa_srari_h(hz_out_m, 7);                               \
    hz_out_m = __msa_sat_s_h(hz_out_m, 7);                               \
                                                                         \
    hz_out_m;                                                            \
} )

#define HORIZ_6TAP_4WID_4VECS_FILT(src0, src1, src2, src3,             \
                                   mask0, mask1, mask2,                \
                                   filt0, filt1, filt2,                \
                                   out0, out1)                         \
{                                                                      \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m, vec4_m, vec5_m;              \
                                                                       \
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0_m, vec1_m);  \
    DOTP_SB2_SH(vec0_m, vec1_m, filt0, filt0, out0, out1);             \
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2_m, vec3_m);  \
    DPADD_SB2_SH(vec2_m, vec3_m, filt1, filt1, out0, out1);            \
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4_m, vec5_m);  \
    DPADD_SB2_SH(vec4_m, vec5_m, filt2, filt2, out0, out1);            \
}

#define HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3,                    \
                                   mask0, mask1, mask2,                       \
                                   filt0, filt1, filt2,                       \
                                   out0, out1, out2, out3)                    \
{                                                                             \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m, vec4_m, vec5_m, vec6_m, vec7_m;     \
                                                                              \
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);         \
    DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,   \
                out0, out1, out2, out3);                                      \
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec2_m, vec3_m);         \
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec4_m, vec5_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec6_m, vec7_m);         \
    DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt1, filt1, filt1, filt1,  \
                 out0, out1, out2, out3);                                     \
    DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt2, filt2, filt2, filt2,  \
                 out0, out1, out2, out3);                                     \
}

#define FILT_4TAP_DPADD_S_H(vec0, vec1, filt0, filt1)           \
( {                                                             \
    v8i16 tmp0;                                                 \
                                                                \
    tmp0 = __msa_dotp_s_h((v16i8) vec0, (v16i8) filt0);         \
    tmp0 = __msa_dpadd_s_h(tmp0, (v16i8) vec1, (v16i8) filt1);  \
                                                                \
    tmp0;                                                       \
} )

#define HORIZ_4TAP_FILT(src0, src1, mask0, mask1, filt_h0, filt_h1)    \
( {                                                                    \
    v16i8 vec0_m, vec1_m;                                              \
    v8i16 hz_out_m;                                                    \
                                                                       \
    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0_m, vec1_m);  \
    hz_out_m = FILT_4TAP_DPADD_S_H(vec0_m, vec1_m, filt_h0, filt_h1);  \
                                                                       \
    hz_out_m = __msa_srari_h(hz_out_m, 7);                             \
    hz_out_m = __msa_sat_s_h(hz_out_m, 7);                             \
                                                                       \
    hz_out_m;                                                          \
} )

#define HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3,             \
                                   mask0, mask1, filt0, filt1,         \
                                   out0, out1)                         \
{                                                                      \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m;                              \
                                                                       \
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0_m, vec1_m);  \
    DOTP_SB2_SH(vec0_m, vec1_m, filt0, filt0, out0, out1);             \
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2_m, vec3_m);  \
    DPADD_SB2_SH(vec2_m, vec3_m, filt1, filt1, out0, out1);            \
}

#define HORIZ_4TAP_8WID_4VECS_FILT(src0, src1, src2, src3,                    \
                                   mask0, mask1, filt0, filt1,                \
                                   out0, out1, out2, out3)                    \
{                                                                             \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m;                                     \
                                                                              \
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);         \
    DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,   \
                out0, out1, out2, out3);                                      \
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec2_m, vec3_m);         \
    DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt1, filt1, filt1, filt1,  \
                 out0, out1, out2, out3);                                     \
}

static void common_hz_6t_4x4_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2;
    v16u8 mask0, mask1, mask2, out;
    v8i16 filt, out0, out1;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 2;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_6TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               filt0, filt1, filt2, out0, out1);
    SRARI_H2_SH(out0, out1, 7);
    SAT_SH2_SH(out0, out1, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_6t_4x8_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2;
    v16u8 mask0, mask1, mask2, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 2;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_6TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               filt0, filt1, filt2, out0, out1);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_6TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               filt0, filt1, filt2, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
}

void ff_put_vp8_epel4_h6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    const int8_t *filter = subpel_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_6t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_hz_6t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    }
}

void ff_put_vp8_epel8_h6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2;
    v16u8 mask0, mask1, mask2, tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);

    src -= 2;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               filt0, filt1, filt2, out0, out1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    tmp0 = PCKEV_XORI128_UB(out0, out1);
    tmp1 = PCKEV_XORI128_UB(out2, out3);
    ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);

    for (loop_cnt = (height >> 2) - 1; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);
        HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   filt0, filt1, filt2, out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_put_vp8_epel16_h6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                              uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, filt0, filt1, filt2;
    v16u8 mask0, mask1, mask2, out;
    v8i16 filt, out0, out1, out2, out3, out4, out5, out6, out7;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 2;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        src += (4 * src_stride);

        HORIZ_6TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   filt0, filt1, filt2, out0, out1, out2, out3);
        HORIZ_6TAP_8WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1, mask2,
                                   filt0, filt1, filt2, out4, out5, out6, out7);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SRARI_H4_SH(out4, out5, out6, out7, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out4, out5, out6, out7, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out4, out5);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out6, out7);
        ST_UB(out, dst);
        dst += dst_stride;
    }
}

void ff_put_vp8_epel4_v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v16u8 out;
    v8i16 filt, out10, out32;

    src -= (2 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
        XORI_B2_128_SB(src6554, src8776);
        out10 = DPADD_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
        out32 = DPADD_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
        SRARI_H2_SH(out10, out32, 7);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        src2110 = src6554;
        src4332 = src8776;
        src4 = src8;
    }
}

void ff_put_vp8_epel8_v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src76_r, src98_r, src21_r, src43_r, src87_r;
    v16i8 src109_r, filt0, filt1, filt2;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r;

    src -= (2 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src3, src2, src2, src1, src4, src3,
               src10_r, src32_r, src21_r, src43_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        tmp0 = PCKEV_XORI128_UB(out0_r, out1_r);
        tmp1 = PCKEV_XORI128_UB(out2_r, out3_r);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src76_r;
        src32_r = src98_r;
        src21_r = src87_r;
        src43_r = src109_r;
        src4 = src10;
    }
}

void ff_put_vp8_epel16_v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                              uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l, filt;

    src -= (2 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H3_SB(filt, 0, 1, 2, filt0, filt1, filt2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src3, src2, src4, src3, src2, src1, src10_r,
               src32_r, src43_r, src21_r);
    ILVL_B4_SB(src1, src0, src3, src2, src4, src3, src2, src1, src10_l,
               src32_l, src43_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1,
                              filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1,
                              filt2);
        out2_r = DPADD_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1,
                              filt2);
        out3_r = DPADD_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1,
                              filt2);
        out0_l = DPADD_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1,
                              filt2);
        out1_l = DPADD_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1,
                              filt2);
        out2_l = DPADD_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1,
                              filt2);
        out3_l = DPADD_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1,
                              filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, tmp0, tmp1, tmp2, tmp3);
        XORI_B4_128_UB(tmp0, tmp1, tmp2, tmp3);
        ST_UB4(tmp0, tmp1, tmp2, tmp3, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src10_l = src54_l;
        src32_l = src76_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src4 = src8;
    }
}

void ff_put_vp8_epel4_h6v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 filt_hz0, filt_hz1, filt_hz2;
    v16u8 mask0, mask1, mask2, out;
    v8i16 tmp0, tmp1;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, filt, filt_vt0, filt_vt1, filt_vt2, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= (2 + 2 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H3_SB(filt, 0, 1, 2, filt_hz0, filt_hz1, filt_hz2);

    filt = LD_SH(filter_vert);
    SPLATI_H3_SH(filt, 0, 1, 2, filt_vt0, filt_vt1, filt_vt2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    hz_out0 = HORIZ_6TAP_FILT(src0, src1, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out2 = HORIZ_6TAP_FILT(src2, src3, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out1 = (v8i16) __msa_sldi_b((v16i8) hz_out2, (v16i8) hz_out0, 8);
    hz_out3 = HORIZ_6TAP_FILT(src3, src4, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src5, src6);
        src += (2 * src_stride);

        XORI_B2_128_SB(src5, src6);
        hz_out5 = HORIZ_6TAP_FILT(src5, src6, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        hz_out4 = (v8i16) __msa_sldi_b((v16i8) hz_out5, (v16i8) hz_out3, 8);

        LD_SB2(src, src_stride, src7, src8);
        src += (2 * src_stride);

        XORI_B2_128_SB(src7, src8);
        hz_out7 = HORIZ_6TAP_FILT(src7, src8, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        hz_out6 = (v8i16) __msa_sldi_b((v16i8) hz_out7, (v16i8) hz_out5, 8);

        out2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);
        tmp0 = DPADD_SH3_SH(out0, out1, out2, filt_vt0, filt_vt1, filt_vt2);

        out3 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp1 = DPADD_SH3_SH(out1, out2, out3, filt_vt0, filt_vt1, filt_vt2);

        SRARI_H2_SH(tmp0, tmp1, 7);
        SAT_SH2_SH(tmp0, tmp1, 7);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out3 = hz_out7;
        out0 = out2;
        out1 = out3;
    }
}

void ff_put_vp8_epel8_h6v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 filt_hz0, filt_hz1, filt_hz2;
    v16u8 mask0, mask1, mask2, vec0, vec1;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, out0, out1, out2, out3, out4, out5, out6, out7;
    v8i16 tmp0, tmp1, tmp2, tmp3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= (2 + 2 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H3_SB(filt, 0, 1, 2, filt_hz0, filt_hz1, filt_hz2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
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

    filt = LD_SH(filter_vert);
    SPLATI_H3_SH(filt, 0, 1, 2, filt_vt0, filt_vt1, filt_vt2);

    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);
    ILVEV_B2_SH(hz_out1, hz_out2, hz_out3, hz_out4, out3, out4);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        hz_out5 = HORIZ_6TAP_FILT(src5, src5, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);
        tmp0 = DPADD_SH3_SH(out0, out1, out2, filt_vt0, filt_vt1, filt_vt2);

        hz_out6 = HORIZ_6TAP_FILT(src6, src6, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out5 = (v8i16) __msa_ilvev_b((v16i8) hz_out6, (v16i8) hz_out5);
        tmp1 = DPADD_SH3_SH(out3, out4, out5, filt_vt0, filt_vt1, filt_vt2);

        hz_out7 = HORIZ_6TAP_FILT(src7, src7, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out7 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp2 = DPADD_SH3_SH(out1, out2, out7, filt_vt0, filt_vt1, filt_vt2);

        hz_out8 = HORIZ_6TAP_FILT(src8, src8, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        out6 = (v8i16) __msa_ilvev_b((v16i8) hz_out8, (v16i8) hz_out7);
        tmp3 = DPADD_SH3_SH(out4, out5, out6, filt_vt0, filt_vt1, filt_vt2);

        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        vec0 = PCKEV_XORI128_UB(tmp0, tmp1);
        vec1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_D4(vec0, vec1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out4 = hz_out8;
        out0 = out2;
        out1 = out7;
        out3 = out5;
        out4 = out6;
    }
}


void ff_put_vp8_epel16_h6v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h6v6_msa(dst, dst_stride, src, src_stride, height,
                                  mx, my);

        src += 8;
        dst += 8;
    }
}

static void common_hz_4t_4x4_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v8i16 filt, out0, out1;
    v16u8 out;

    mask0 = LD_SB(&mc_filt_mask_arr[16]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                               filt0, filt1, out0, out1);
    SRARI_H2_SH(out0, out1, 7);
    SAT_SH2_SH(out0, out1, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_4t_4x8_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&mc_filt_mask_arr[16]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);

    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                               filt0, filt1, out0, out1);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                               filt0, filt1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
}

static void common_hz_4t_4x16_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&mc_filt_mask_arr[16]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                               filt0, filt1, out0, out1);
    HORIZ_4TAP_4WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1,
                               filt0, filt1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                               filt0, filt1, out0, out1);
    HORIZ_4TAP_4WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1,
                               filt0, filt1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

void ff_put_vp8_epel4_h4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    const int8_t *filter = subpel_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_4t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_hz_4t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    } else if (16 == height) {
        common_hz_4t_4x16_msa(src, src_stride, dst, dst_stride, filter);
    }
}

void ff_put_vp8_epel8_h4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
        HORIZ_4TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, filt0,
                                   filt1, out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_put_vp8_epel16_h4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                              uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, mask0, mask1;
    v8i16 filt, out0, out1, out2, out3, out4, out5, out6, out7;
    v16u8 out;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        HORIZ_4TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, filt0,
                                   filt1, out0, out1, out2, out3);
        HORIZ_4TAP_8WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1, filt0,
                                   filt1, out4, out5, out6, out7);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SRARI_H4_SH(out4, out5, out6, out7, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out4, out5, out6, out7, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out4, out5);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out6, out7);
        ST_UB(out, dst);
        dst += dst_stride;
    }
}

void ff_put_vp8_epel4_v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r;
    v16i8 src2110, src4332, filt0, filt1;
    v8i16 filt, out10, out32;
    v16u8 out;

    src -= src_stride;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB3(src, src_stride, src3, src4, src5);
        src += (3 * src_stride);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_r, (v2i64) src32_r);
        src4332 = (v16i8) __msa_xori_b((v16u8) src4332, 128);
        out10 = FILT_4TAP_DPADD_S_H(src2110, src4332, filt0, filt1);

        src2 = LD_SB(src);
        src += (src_stride);
        ILVR_B2_SB(src5, src4, src2, src5, src54_r, src65_r);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src65_r, (v2i64) src54_r);
        src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);
        out32 = FILT_4TAP_DPADD_S_H(src4332, src2110, filt0, filt1);
        SRARI_H2_SH(out10, out32, 7);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_put_vp8_epel8_v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                             uint8_t *src, ptrdiff_t src_stride,
                             int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src7, src8, src9, src10;
    v16i8 src10_r, src72_r, src98_r, src21_r, src87_r, src109_r, filt0, filt1;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r;

    src -= src_stride;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src2, src8, src7, src9, src8, src10, src9,
                   src72_r, src87_r, src98_r, src109_r);
        out0_r = FILT_4TAP_DPADD_S_H(src10_r, src72_r, filt0, filt1);
        out1_r = FILT_4TAP_DPADD_S_H(src21_r, src87_r, filt0, filt1);
        out2_r = FILT_4TAP_DPADD_S_H(src72_r, src98_r, filt0, filt1);
        out3_r = FILT_4TAP_DPADD_S_H(src87_r, src109_r, filt0, filt1);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        tmp0 = PCKEV_XORI128_UB(out0_r, out1_r);
        tmp1 = PCKEV_XORI128_UB(out2_r, out3_r);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src98_r;
        src21_r = src109_r;
        src2 = src10;
    }
}

void ff_put_vp8_epel16_v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                              uint8_t *src, ptrdiff_t src_stride,
                              int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src54_r, src21_r, src43_r, src65_r, src10_l;
    v16i8 src32_l, src54_l, src21_l, src43_l, src65_l, filt0, filt1;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    src -= src_stride;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);
        ILVR_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
                   src32_r, src43_r, src54_r, src65_r);
        ILVL_B4_SB(src3, src2, src4, src3, src5, src4, src6, src5,
                   src32_l, src43_l, src54_l, src65_l);
        out0_r = FILT_4TAP_DPADD_S_H(src10_r, src32_r, filt0, filt1);
        out1_r = FILT_4TAP_DPADD_S_H(src21_r, src43_r, filt0, filt1);
        out2_r = FILT_4TAP_DPADD_S_H(src32_r, src54_r, filt0, filt1);
        out3_r = FILT_4TAP_DPADD_S_H(src43_r, src65_r, filt0, filt1);
        out0_l = FILT_4TAP_DPADD_S_H(src10_l, src32_l, filt0, filt1);
        out1_l = FILT_4TAP_DPADD_S_H(src21_l, src43_l, filt0, filt1);
        out2_l = FILT_4TAP_DPADD_S_H(src32_l, src54_l, filt0, filt1);
        out3_l = FILT_4TAP_DPADD_S_H(src43_l, src65_l, filt0, filt1);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, tmp0, tmp1, tmp2, tmp3);
        XORI_B4_128_UB(tmp0, tmp1, tmp2, tmp3);
        ST_UB4(tmp0, tmp1, tmp2, tmp3, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src54_r;
        src21_r = src65_r;
        src10_l = src54_l;
        src21_l = src65_l;
        src2 = src6;
    }
}

void ff_put_vp8_epel4_h4v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, filt_hz0, filt_hz1;
    v16u8 mask0, mask1, out;
    v8i16 filt, filt_vt0, filt_vt1, tmp0, tmp1, vec0, vec1, vec2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= (1 + 1 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H2_SB(filt, 0, 1, filt_hz0, filt_hz1);

    mask1 = mask0 + 2;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    hz_out0 = HORIZ_4TAP_FILT(src0, src1, mask0, mask1, filt_hz0, filt_hz1);
    hz_out1 = HORIZ_4TAP_FILT(src1, src2, mask0, mask1, filt_hz0, filt_hz1);
    vec0 = (v8i16) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);

    filt = LD_SH(filter_vert);
    SPLATI_H2_SH(filt, 0, 1, filt_vt0, filt_vt1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B2_128_SB(src3, src4);
        hz_out3 = HORIZ_4TAP_FILT(src3, src4, mask0, mask1, filt_hz0, filt_hz1);
        hz_out2 = (v8i16) __msa_sldi_b((v16i8) hz_out3, (v16i8) hz_out1, 8);
        vec1 = (v8i16) __msa_ilvev_b((v16i8) hz_out3, (v16i8) hz_out2);
        tmp0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt_vt0, filt_vt1);

        XORI_B2_128_SB(src5, src6);
        hz_out5 = HORIZ_4TAP_FILT(src5, src6, mask0, mask1, filt_hz0, filt_hz1);
        hz_out4 = (v8i16) __msa_sldi_b((v16i8) hz_out5, (v16i8) hz_out3, 8);
        vec2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);
        tmp1 = FILT_4TAP_DPADD_S_H(vec1, vec2, filt_vt0, filt_vt1);

        SRARI_H2_SH(tmp0, tmp1, 7);
        SAT_SH2_SH(tmp0, tmp1, 7);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out1 = hz_out5;
        vec0 = vec2;
    }
}

void ff_put_vp8_epel8_h4v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, filt_hz0, filt_hz1;
    v16u8 mask0, mask1, out0, out1;
    v8i16 filt, filt_vt0, filt_vt1, tmp0, tmp1, tmp2, tmp3;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 vec0, vec1, vec2, vec3, vec4;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= (1 + 1 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H2_SB(filt, 0, 1, filt_hz0, filt_hz1);

    mask1 = mask0 + 2;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    hz_out0 = HORIZ_4TAP_FILT(src0, src0, mask0, mask1, filt_hz0, filt_hz1);
    hz_out1 = HORIZ_4TAP_FILT(src1, src1, mask0, mask1, filt_hz0, filt_hz1);
    hz_out2 = HORIZ_4TAP_FILT(src2, src2, mask0, mask1, filt_hz0, filt_hz1);
    ILVEV_B2_SH(hz_out0, hz_out1, hz_out1, hz_out2, vec0, vec2);

    filt = LD_SH(filter_vert);
    SPLATI_H2_SH(filt, 0, 1, filt_vt0, filt_vt1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);
        hz_out3 = HORIZ_4TAP_FILT(src3, src3, mask0, mask1, filt_hz0, filt_hz1);
        vec1 = (v8i16) __msa_ilvev_b((v16i8) hz_out3, (v16i8) hz_out2);
        tmp0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt_vt0, filt_vt1);

        hz_out0 = HORIZ_4TAP_FILT(src4, src4, mask0, mask1, filt_hz0, filt_hz1);
        vec3 = (v8i16) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out3);
        tmp1 = FILT_4TAP_DPADD_S_H(vec2, vec3, filt_vt0, filt_vt1);

        hz_out1 = HORIZ_4TAP_FILT(src5, src5, mask0, mask1, filt_hz0, filt_hz1);
        vec4 = (v8i16) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp2 = FILT_4TAP_DPADD_S_H(vec1, vec4, filt_vt0, filt_vt1);

        hz_out2 = HORIZ_4TAP_FILT(src6, src6, mask0, mask1, filt_hz0, filt_hz1);
        ILVEV_B2_SH(hz_out3, hz_out0, hz_out1, hz_out2, vec0, vec1);
        tmp3 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt_vt0, filt_vt1);

        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        vec0 = vec4;
        vec2 = vec1;
    }
}

void ff_put_vp8_epel16_h4v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h4v4_msa(dst, dst_stride, src, src_stride, height,
                                  mx, my);

        src += 8;
        dst += 8;
    }
}

void ff_put_vp8_epel4_h6v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 filt_hz0, filt_hz1, filt_hz2;
    v16u8 res0, res1, mask0, mask1, mask2;
    v8i16 filt, filt_vt0, filt_vt1, tmp0, tmp1, vec0, vec1, vec2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= (2 + 1 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H3_SB(filt, 0, 1, 2, filt_hz0, filt_hz1, filt_hz2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    hz_out0 = HORIZ_6TAP_FILT(src0, src1, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out1 = HORIZ_6TAP_FILT(src1, src2, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    vec0 = (v8i16) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);

    filt = LD_SH(filter_vert);
    SPLATI_H2_SH(filt, 0, 1, filt_vt0, filt_vt1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);
        hz_out3 = HORIZ_6TAP_FILT(src3, src4, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        hz_out2 = (v8i16) __msa_sldi_b((v16i8) hz_out3, (v16i8) hz_out1, 8);
        vec1 = (v8i16) __msa_ilvev_b((v16i8) hz_out3, (v16i8) hz_out2);
        tmp0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt_vt0, filt_vt1);

        hz_out5 = HORIZ_6TAP_FILT(src5, src6, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        hz_out4 = (v8i16) __msa_sldi_b((v16i8) hz_out5, (v16i8) hz_out3, 8);
        vec2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);
        tmp1 = FILT_4TAP_DPADD_S_H(vec1, vec2, filt_vt0, filt_vt1);

        SRARI_H2_SH(tmp0, tmp1, 7);
        SAT_SH2_SH(tmp0, tmp1, 7);
        PCKEV_B2_UB(tmp0, tmp0, tmp1, tmp1, res0, res1);
        XORI_B2_128_UB(res0, res1);
        ST_W2(res0, 0, 1, dst, dst_stride);
        ST_W2(res1, 0, 1, dst + 2 * dst_stride, dst_stride);
        dst += (4 * dst_stride);

        hz_out1 = hz_out5;
        vec0 = vec2;
    }
}

void ff_put_vp8_epel8_h6v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 filt_hz0, filt_hz1, filt_hz2, mask0, mask1, mask2;
    v8i16 filt, filt_vt0, filt_vt1, hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 tmp0, tmp1, tmp2, tmp3, vec0, vec1, vec2, vec3;
    v16u8 out0, out1;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= (2 + src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H3_SB(filt, 0, 1, 2, filt_hz0, filt_hz1, filt_hz2);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    hz_out0 = HORIZ_6TAP_FILT(src0, src0, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out1 = HORIZ_6TAP_FILT(src1, src1, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    hz_out2 = HORIZ_6TAP_FILT(src2, src2, mask0, mask1, mask2, filt_hz0,
                              filt_hz1, filt_hz2);
    ILVEV_B2_SH(hz_out0, hz_out1, hz_out1, hz_out2, vec0, vec2);

    filt = LD_SH(filter_vert);
    SPLATI_H2_SH(filt, 0, 1, filt_vt0, filt_vt1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);

        hz_out3 = HORIZ_6TAP_FILT(src3, src3, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        vec1 = (v8i16) __msa_ilvev_b((v16i8) hz_out3, (v16i8) hz_out2);
        tmp0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt_vt0, filt_vt1);

        hz_out0 = HORIZ_6TAP_FILT(src4, src4, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        vec3 = (v8i16) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out3);
        tmp1 = FILT_4TAP_DPADD_S_H(vec2, vec3, filt_vt0, filt_vt1);

        hz_out1 = HORIZ_6TAP_FILT(src5, src5, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        vec0 = (v8i16) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp2 = FILT_4TAP_DPADD_S_H(vec1, vec0, filt_vt0, filt_vt1);

        hz_out2 = HORIZ_6TAP_FILT(src6, src6, mask0, mask1, mask2, filt_hz0,
                                  filt_hz1, filt_hz2);
        ILVEV_B2_SH(hz_out3, hz_out0, hz_out1, hz_out2, vec1, vec2);
        tmp3 = FILT_4TAP_DPADD_S_H(vec1, vec2, filt_vt0, filt_vt1);

        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_put_vp8_epel16_h6v4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h6v4_msa(dst, dst_stride, src, src_stride, height,
                                  mx, my);

        src += 8;
        dst += 8;
    }
}

void ff_put_vp8_epel4_h4v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 filt_hz0, filt_hz1, mask0, mask1;
    v16u8 out;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, tmp0, tmp1, out0, out1, out2, out3;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2;

    mask0 = LD_SB(&mc_filt_mask_arr[16]);

    src -= (1 + 2 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H2_SB(filt, 0, 1, filt_hz0, filt_hz1);

    mask1 = mask0 + 2;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    hz_out0 = HORIZ_4TAP_FILT(src0, src1, mask0, mask1, filt_hz0, filt_hz1);
    hz_out2 = HORIZ_4TAP_FILT(src2, src3, mask0, mask1, filt_hz0, filt_hz1);
    hz_out3 = HORIZ_4TAP_FILT(src3, src4, mask0, mask1, filt_hz0, filt_hz1);
    hz_out1 = (v8i16) __msa_sldi_b((v16i8) hz_out2, (v16i8) hz_out0, 8);
    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);

    filt = LD_SH(filter_vert);
    SPLATI_H3_SH(filt, 0, 1, 2, filt_vt0, filt_vt1, filt_vt2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        XORI_B4_128_SB(src5, src6, src7, src8);
        src += (4 * src_stride);

        hz_out5 = HORIZ_4TAP_FILT(src5, src6, mask0, mask1, filt_hz0, filt_hz1);
        hz_out4 = (v8i16) __msa_sldi_b((v16i8) hz_out5, (v16i8) hz_out3, 8);
        out2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);
        tmp0 = DPADD_SH3_SH(out0, out1, out2, filt_vt0, filt_vt1, filt_vt2);

        hz_out7 = HORIZ_4TAP_FILT(src7, src8, mask0, mask1, filt_hz0, filt_hz1);
        hz_out6 = (v8i16) __msa_sldi_b((v16i8) hz_out7, (v16i8) hz_out5, 8);
        out3 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp1 = DPADD_SH3_SH(out1, out2, out3, filt_vt0, filt_vt1, filt_vt2);

        SRARI_H2_SH(tmp0, tmp1, 7);
        SAT_SH2_SH(tmp0, tmp1, 7);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out3 = hz_out7;
        out0 = out2;
        out1 = out3;
    }
}

void ff_put_vp8_epel8_h4v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                               uint8_t *src, ptrdiff_t src_stride,
                               int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = subpel_filters_msa[mx - 1];
    const int8_t *filter_vert = subpel_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 filt_hz0, filt_hz1, mask0, mask1;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2, tmp0, tmp1, tmp2, tmp3;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, out0, out1, out2, out3, out4, out5, out6, out7;
    v16u8 vec0, vec1;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= (1 + 2 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H2_SB(filt, 0, 1, filt_hz0, filt_hz1);

    mask1 = mask0 + 2;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    hz_out0 = HORIZ_4TAP_FILT(src0, src0, mask0, mask1, filt_hz0, filt_hz1);
    hz_out1 = HORIZ_4TAP_FILT(src1, src1, mask0, mask1, filt_hz0, filt_hz1);
    hz_out2 = HORIZ_4TAP_FILT(src2, src2, mask0, mask1, filt_hz0, filt_hz1);
    hz_out3 = HORIZ_4TAP_FILT(src3, src3, mask0, mask1, filt_hz0, filt_hz1);
    hz_out4 = HORIZ_4TAP_FILT(src4, src4, mask0, mask1, filt_hz0, filt_hz1);
    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);
    ILVEV_B2_SH(hz_out1, hz_out2, hz_out3, hz_out4, out3, out4);

    filt = LD_SH(filter_vert);
    SPLATI_H3_SH(filt, 0, 1, 2, filt_vt0, filt_vt1, filt_vt2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);

        hz_out5 = HORIZ_4TAP_FILT(src5, src5, mask0, mask1, filt_hz0, filt_hz1);
        out2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);
        tmp0 = DPADD_SH3_SH(out0, out1, out2, filt_vt0, filt_vt1, filt_vt2);

        hz_out6 = HORIZ_4TAP_FILT(src6, src6, mask0, mask1, filt_hz0, filt_hz1);
        out5 = (v8i16) __msa_ilvev_b((v16i8) hz_out6, (v16i8) hz_out5);
        tmp1 = DPADD_SH3_SH(out3, out4, out5, filt_vt0, filt_vt1, filt_vt2);

        hz_out7 = HORIZ_4TAP_FILT(src7, src7, mask0, mask1, filt_hz0, filt_hz1);
        out6 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp2 = DPADD_SH3_SH(out1, out2, out6, filt_vt0, filt_vt1, filt_vt2);

        hz_out8 = HORIZ_4TAP_FILT(src8, src8, mask0, mask1, filt_hz0, filt_hz1);
        out7 = (v8i16) __msa_ilvev_b((v16i8) hz_out8, (v16i8) hz_out7);
        tmp3 = DPADD_SH3_SH(out4, out5, out7, filt_vt0, filt_vt1, filt_vt2);

        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        vec0 = PCKEV_XORI128_UB(tmp0, tmp1);
        vec1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_D4(vec0, vec1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out4 = hz_out8;
        out0 = out2;
        out1 = out6;
        out3 = out5;
        out4 = out7;
    }
}

void ff_put_vp8_epel16_h4v6_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_vp8_epel8_h4v6_msa(dst, dst_stride, src, src_stride, height,
                                  mx, my);

        src += 8;
        dst += 8;
    }
}

static void common_hz_2t_4x4_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, mask;
    v16u8 filt0, vec0, vec1, res0, res1;
    v8u16 vec2, vec3, filt;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, vec0, vec1);
    DOTP_UB2_UH(vec0, vec1, filt0, filt0, vec2, vec3);
    SRARI_H2_UH(vec2, vec3, 7);
    PCKEV_B2_UB(vec2, vec2, vec3, vec3, res0, res1);
    ST_W2(res0, 0, 1, dst, dst_stride);
    ST_W2(res1, 0, 1, dst + 2 * dst_stride, dst_stride);
}

static void common_hz_2t_4x8_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16u8 vec0, vec1, vec2, vec3, filt0;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16i8 res0, res1, res2, res3;
    v8u16 vec4, vec5, vec6, vec7, filt;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, vec0, vec1);
    VSHF_B2_UB(src4, src5, src6, src7, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                vec4, vec5, vec6, vec7);
    SRARI_H4_UH(vec4, vec5, vec6, vec7, 7);
    PCKEV_B4_SB(vec4, vec4, vec5, vec5, vec6, vec6, vec7, vec7,
                res0, res1, res2, res3);
    ST_W2(res0, 0, 1, dst, dst_stride);
    ST_W2(res1, 0, 1, dst + 2 * dst_stride, dst_stride);
    ST_W2(res2, 0, 1, dst + 4 * dst_stride, dst_stride);
    ST_W2(res3, 0, 1, dst + 6 * dst_stride, dst_stride);
}

void ff_put_vp8_bilinear4_h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    const int8_t *filter = bilinear_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_2t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_hz_2t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_hz_2t_8x4_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16u8 filt0;
    v16i8 src0, src1, src2, src3, mask;
    v8u16 vec0, vec1, vec2, vec3, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                vec0, vec1, vec2, vec3);
    SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
    PCKEV_B2_SB(vec1, vec0, vec3, vec2, src0, src1);
    ST_D4(src0, src1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hz_2t_8x8mult_msa(uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    v16u8 filt0;
    v16i8 src0, src1, src2, src3, mask, out0, out1;
    v8u16 vec0, vec1, vec2, vec3, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);

    VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                vec0, vec1, vec2, vec3);
    SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);

    PCKEV_B2_SB(vec1, vec0, vec3, vec2, out0, out1);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);

    VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                vec0, vec1, vec2, vec3);
    SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
    PCKEV_B2_SB(vec1, vec0, vec3, vec2, out0, out1);
    ST_D4(out0, out1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
    dst += (8 * dst_stride);

    if (16 == height) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    vec0, vec1, vec2, vec3);
        SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        PCKEV_B2_SB(vec1, vec0, vec3, vec2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);

        VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    vec0, vec1, vec2, vec3);
        SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
        PCKEV_B2_SB(vec1, vec0, vec3, vec2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
    }
}

void ff_put_vp8_bilinear8_h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    const int8_t *filter = bilinear_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_2t_8x4_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_hz_2t_8x8mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    }
}

void ff_put_vp8_bilinear16_h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                 uint8_t *src, ptrdiff_t src_stride,
                                 int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = bilinear_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 out0, out1, out2, out3, out4, out5, out6, out7, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    loop_cnt = (height >> 2) - 1;

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB4(src, src_stride, src0, src2, src4, src6);
    LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
    src += (4 * src_stride);

    VSHF_B2_UB(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UB(src2, src2, src3, src3, mask, mask, vec2, vec3);
    VSHF_B2_UB(src4, src4, src5, src5, mask, mask, vec4, vec5);
    VSHF_B2_UB(src6, src6, src7, src7, mask, mask, vec6, vec7);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                out0, out1, out2, out3);
    DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                out4, out5, out6, out7);
    SRARI_H4_UH(out0, out1, out2, out3, 7);
    SRARI_H4_UH(out4, out5, out6, out7, 7);
    PCKEV_ST_SB(out0, out1, dst);
    dst += dst_stride;
    PCKEV_ST_SB(out2, out3, dst);
    dst += dst_stride;
    PCKEV_ST_SB(out4, out5, dst);
    dst += dst_stride;
    PCKEV_ST_SB(out6, out7, dst);
    dst += dst_stride;

    for (; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        VSHF_B2_UB(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UB(src2, src2, src3, src3, mask, mask, vec2, vec3);
        VSHF_B2_UB(src4, src4, src5, src5, mask, mask, vec4, vec5);
        VSHF_B2_UB(src6, src6, src7, src7, mask, mask, vec6, vec7);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    out4, out5, out6, out7);
        SRARI_H4_UH(out0, out1, out2, out3, 7);
        SRARI_H4_UH(out4, out5, out6, out7, 7);
        PCKEV_ST_SB(out0, out1, dst);
        dst += dst_stride;
        PCKEV_ST_SB(out2, out3, dst);
        dst += dst_stride;
        PCKEV_ST_SB(out4, out5, dst);
        dst += dst_stride;
        PCKEV_ST_SB(out6, out7, dst);
        dst += dst_stride;
    }
}

static void common_vt_2t_4x4_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src10_r, src32_r, src21_r, src43_r, src2110, src4332;
    v16u8 filt0;
    v8i16 filt;
    v8u16 tmp0, tmp1;

    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    DOTP_UB2_UH(src2110, src4332, filt0, filt0, tmp0, tmp1);
    SRARI_H2_UH(tmp0, tmp1, 7);
    SAT_UH2_UH(tmp0, tmp1, 7);
    src2110 = __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    ST_W4(src2110, 0, 1, 2, 3, dst, dst_stride);
}

static void common_vt_2t_4x8_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src2110, src4332, src6554, src8776;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v16u8 filt0;
    v8i16 filt;

    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);

    src8 = LD_SB(src);
    src += src_stride;

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D4_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r,
               src87_r, src76_r, src2110, src4332, src6554, src8776);
    DOTP_UB4_UH(src2110, src4332, src6554, src8776, filt0, filt0, filt0, filt0,
                tmp0, tmp1, tmp2, tmp3);
    SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B2_SB(tmp1, tmp0, tmp3, tmp2, src2110, src4332);
    ST_W8(src2110, src4332, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
}

void ff_put_vp8_bilinear4_v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    const int8_t *filter = bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_vt_2t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_vt_2t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_vt_2t_8x4_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16u8 src0, src1, src2, src3, src4, vec0, vec1, vec2, vec3, filt0;
    v16i8 out0, out1;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    ILVR_B2_UB(src1, src0, src2, src1, vec0, vec1);
    ILVR_B2_UB(src3, src2, src4, src3, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                tmp0, tmp1, tmp2, tmp3);
    SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B2_SB(tmp1, tmp0, tmp3, tmp2, out0, out1);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_vt_2t_8x8mult_msa(uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, filt0;
    v16i8 out0, out1;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src1, src2, src3, src4, src5, src6, src7, src8);
        src += (8 * src_stride);

        ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
                   vec0, vec1, vec2, vec3);
        ILVR_B4_UB(src5, src4, src6, src5, src7, src6, src8, src7,
                   vec4, vec5, vec6, vec7);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_B2_SB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);

        DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_B2_SB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
        dst += (8 * dst_stride);

        src0 = src8;
    }
}

void ff_put_vp8_bilinear8_v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                uint8_t *src, ptrdiff_t src_stride,
                                int height, int mx, int my)
{
    const int8_t *filter = bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_vt_2t_8x4_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_vt_2t_8x8mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    }
}

void ff_put_vp8_bilinear16_v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                 uint8_t *src, ptrdiff_t src_stride,
                                 int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = bilinear_filters_msa[my - 1];
    v16u8 src0, src1, src2, src3, src4;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, filt0;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        ILVR_B2_UB(src1, src0, src2, src1, vec0, vec2);
        ILVL_B2_UB(src1, src0, src2, src1, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst);
        dst += dst_stride;

        ILVR_B2_UB(src3, src2, src4, src3, vec4, vec6);
        ILVL_B2_UB(src3, src2, src4, src3, vec5, vec7);
        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst);
        dst += dst_stride;

        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst);
        dst += dst_stride;

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst);
        dst += dst_stride;

        src0 = src4;
    }
}

static void common_hv_2ht_2vt_4x4_msa(uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert)
{
    v16i8 src0, src1, src2, src3, src4, mask;
    v16u8 filt_vt, filt_hz, vec0, vec1, res0, res1;
    v8u16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, filt, tmp0, tmp1;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_UH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h((v8i16) filt, 0);

    filt = LD_UH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    hz_out0 = HORIZ_2TAP_FILT_UH(src0, src1, mask, filt_hz, 7);
    hz_out2 = HORIZ_2TAP_FILT_UH(src2, src3, mask, filt_hz, 7);
    hz_out4 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
    hz_out1 = (v8u16) __msa_sldi_b((v16i8) hz_out2, (v16i8) hz_out0, 8);
    hz_out3 = (v8u16) __msa_pckod_d((v2i64) hz_out4, (v2i64) hz_out2);

    ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
    DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp0, tmp1);
    SRARI_H2_UH(tmp0, tmp1, 7);
    SAT_UH2_UH(tmp0, tmp1, 7);
    PCKEV_B2_UB(tmp0, tmp0, tmp1, tmp1, res0, res1);
    ST_W2(res0, 0, 1, dst, dst_stride);
    ST_W2(res1, 0, 1, dst + 2 * dst_stride, dst_stride);
}

static void common_hv_2ht_2vt_4x8_msa(uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, mask;
    v16i8 res0, res1, res2, res3;
    v16u8 filt_hz, filt_vt, vec0, vec1, vec2, vec3;
    v8u16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8u16 hz_out7, hz_out8, vec4, vec5, vec6, vec7, filt;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_UH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h((v8i16) filt, 0);

    filt = LD_UH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    src8 = LD_SB(src);

    hz_out0 = HORIZ_2TAP_FILT_UH(src0, src1, mask, filt_hz, 7);
    hz_out2 = HORIZ_2TAP_FILT_UH(src2, src3, mask, filt_hz, 7);
    hz_out4 = HORIZ_2TAP_FILT_UH(src4, src5, mask, filt_hz, 7);
    hz_out6 = HORIZ_2TAP_FILT_UH(src6, src7, mask, filt_hz, 7);
    hz_out8 = HORIZ_2TAP_FILT_UH(src8, src8, mask, filt_hz, 7);
    SLDI_B3_UH(hz_out2, hz_out4, hz_out6, hz_out0, hz_out2, hz_out4, hz_out1,
               hz_out3, hz_out5, 8);
    hz_out7 = (v8u16) __msa_pckod_d((v2i64) hz_out8, (v2i64) hz_out6);

    ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
    ILVEV_B2_UB(hz_out4, hz_out5, hz_out6, hz_out7, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt_vt, filt_vt, filt_vt, filt_vt,
                vec4, vec5, vec6, vec7);
    SRARI_H4_UH(vec4, vec5, vec6, vec7, 7);
    SAT_UH4_UH(vec4, vec5, vec6, vec7, 7);
    PCKEV_B4_SB(vec4, vec4, vec5, vec5, vec6, vec6, vec7, vec7,
                res0, res1, res2, res3);
    ST_W2(res0, 0, 1, dst, dst_stride);
    ST_W2(res1, 0, 1, dst + 2 * dst_stride, dst_stride);
    ST_W2(res2, 0, 1, dst + 4 * dst_stride, dst_stride);
    ST_W2(res3, 0, 1, dst + 6 * dst_stride, dst_stride);
}

void ff_put_vp8_bilinear4_hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                 uint8_t *src, ptrdiff_t src_stride,
                                 int height, int mx, int my)
{
    const int8_t *filter_horiz = bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_hv_2ht_2vt_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter_horiz, filter_vert);
    } else if (8 == height) {
        common_hv_2ht_2vt_4x8_msa(src, src_stride, dst, dst_stride,
                                  filter_horiz, filter_vert);
    }
}

static void common_hv_2ht_2vt_8x4_msa(uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert)
{
    v16i8 src0, src1, src2, src3, src4, mask, out0, out1;
    v16u8 filt_hz, filt_vt, vec0, vec1, vec2, vec3;
    v8u16 hz_out0, hz_out1, tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h(filt, 0);

    filt = LD_SH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h(filt, 0);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);

    hz_out0 = HORIZ_2TAP_FILT_UH(src0, src0, mask, filt_hz, 7);
    hz_out1 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);
    vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
    tmp0 = __msa_dotp_u_h(vec0, filt_vt);

    hz_out0 = HORIZ_2TAP_FILT_UH(src2, src2, mask, filt_hz, 7);
    vec1 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
    tmp1 = __msa_dotp_u_h(vec1, filt_vt);

    hz_out1 = HORIZ_2TAP_FILT_UH(src3, src3, mask, filt_hz, 7);
    vec2 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
    tmp2 = __msa_dotp_u_h(vec2, filt_vt);

    hz_out0 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
    vec3 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
    tmp3 = __msa_dotp_u_h(vec3, filt_vt);

    SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B2_SB(tmp1, tmp0, tmp3, tmp2, out0, out1);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hv_2ht_2vt_8x8mult_msa(uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride,
                                          const int8_t *filter_horiz,
                                          const int8_t *filter_vert,
                                          int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, mask, out0, out1;
    v16u8 filt_hz, filt_vt, vec0;
    v8u16 hz_out0, hz_out1, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    v8i16 filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h(filt, 0);

    filt = LD_SH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h(filt, 0);

    src0 = LD_SB(src);
    src += src_stride;

    hz_out0 = HORIZ_2TAP_FILT_UH(src0, src0, mask, filt_hz, 7);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        hz_out1 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp1 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out0 = HORIZ_2TAP_FILT_UH(src2, src2, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
        tmp2 = __msa_dotp_u_h(vec0, filt_vt);

        SRARI_H2_UH(tmp1, tmp2, 7);
        SAT_UH2_UH(tmp1, tmp2, 7);

        hz_out1 = HORIZ_2TAP_FILT_UH(src3, src3, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp3 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out0 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
        LD_SB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
        tmp4 = __msa_dotp_u_h(vec0, filt_vt);

        SRARI_H2_UH(tmp3, tmp4, 7);
        SAT_UH2_UH(tmp3, tmp4, 7);
        PCKEV_B2_SB(tmp2, tmp1, tmp4, tmp3, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);

        hz_out1 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp5 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out0 = HORIZ_2TAP_FILT_UH(src2, src2, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
        tmp6 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out1 = HORIZ_2TAP_FILT_UH(src3, src3, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp7 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out0 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
        tmp8 = __msa_dotp_u_h(vec0, filt_vt);

        SRARI_H4_UH(tmp5, tmp6, tmp7, tmp8, 7);
        SAT_UH4_UH(tmp5, tmp6, tmp7, tmp8, 7);
        PCKEV_B2_SB(tmp6, tmp5, tmp8, tmp7, out0, out1);
        ST_D4(out0, out1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
        dst += (8 * dst_stride);
    }
}

void ff_put_vp8_bilinear8_hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                 uint8_t *src, ptrdiff_t src_stride,
                                 int height, int mx, int my)
{
    const int8_t *filter_horiz = bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_hv_2ht_2vt_8x4_msa(src, src_stride, dst, dst_stride,
                                  filter_horiz, filter_vert);
    } else {
        common_hv_2ht_2vt_8x8mult_msa(src, src_stride, dst, dst_stride,
                                      filter_horiz, filter_vert, height);
    }
}

void ff_put_vp8_bilinear16_hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                                  uint8_t *src, ptrdiff_t src_stride,
                                  int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = bilinear_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt_hz, filt_vt, vec0, vec1;
    v8u16 tmp1, tmp2, hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h(filt, 0);

    filt = LD_SH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h(filt, 0);

    LD_SB2(src, 8, src0, src1);
    src += src_stride;

    hz_out0 = HORIZ_2TAP_FILT_UH(src0, src0, mask, filt_hz, 7);
    hz_out2 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);


    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        hz_out1 = HORIZ_2TAP_FILT_UH(src0, src0, mask, filt_hz, 7);
        hz_out3 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp1, tmp2);
        SRARI_H2_UH(tmp1, tmp2, 7);
        SAT_UH2_UH(tmp1, tmp2, 7);
        PCKEV_ST_SB(tmp1, tmp2, dst);
        dst += dst_stride;

        hz_out0 = HORIZ_2TAP_FILT_UH(src2, src2, mask, filt_hz, 7);
        hz_out2 = HORIZ_2TAP_FILT_UH(src3, src3, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out1, hz_out0, hz_out3, hz_out2, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp1, tmp2);
        SRARI_H2_UH(tmp1, tmp2, 7);
        SAT_UH2_UH(tmp1, tmp2, 7);
        PCKEV_ST_SB(tmp1, tmp2, dst);
        dst += dst_stride;

        hz_out1 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
        hz_out3 = HORIZ_2TAP_FILT_UH(src5, src5, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp1, tmp2);
        SRARI_H2_UH(tmp1, tmp2, 7);
        SAT_UH2_UH(tmp1, tmp2, 7);
        PCKEV_ST_SB(tmp1, tmp2, dst);
        dst += dst_stride;

        hz_out0 = HORIZ_2TAP_FILT_UH(src6, src6, mask, filt_hz, 7);
        hz_out2 = HORIZ_2TAP_FILT_UH(src7, src7, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out1, hz_out0, hz_out3, hz_out2, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp1, tmp2);
        SRARI_H2_UH(tmp1, tmp2, 7);
        SAT_UH2_UH(tmp1, tmp2, 7);
        PCKEV_ST_SB(tmp1, tmp2, dst);
        dst += dst_stride;
    }
}

void ff_put_vp8_pixels8_msa(uint8_t *dst, ptrdiff_t dst_stride,
                            uint8_t *src, ptrdiff_t src_stride,
                            int height, int mx, int my)
{
    int32_t cnt;
    uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    if (0 == height % 8) {
        for (cnt = height >> 3; cnt--;) {
            LD_UB8(src, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src += (8 * src_stride);

            out0 = __msa_copy_u_d((v2i64) src0, 0);
            out1 = __msa_copy_u_d((v2i64) src1, 0);
            out2 = __msa_copy_u_d((v2i64) src2, 0);
            out3 = __msa_copy_u_d((v2i64) src3, 0);
            out4 = __msa_copy_u_d((v2i64) src4, 0);
            out5 = __msa_copy_u_d((v2i64) src5, 0);
            out6 = __msa_copy_u_d((v2i64) src6, 0);
            out7 = __msa_copy_u_d((v2i64) src7, 0);

            SD4(out0, out1, out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
            SD4(out4, out5, out6, out7, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == height % 4) {
        for (cnt = (height / 4); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);
            out0 = __msa_copy_u_d((v2i64) src0, 0);
            out1 = __msa_copy_u_d((v2i64) src1, 0);
            out2 = __msa_copy_u_d((v2i64) src2, 0);
            out3 = __msa_copy_u_d((v2i64) src3, 0);

            SD4(out0, out1, out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

static void copy_16multx8mult_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  int32_t height, int32_t width)
{
    int32_t cnt, loop_cnt;
    uint8_t *src_tmp, *dst_tmp;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_UB8(src_tmp, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src_tmp += (8 * src_stride);

            ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7,
                   dst_tmp, dst_stride);
            dst_tmp += (8 * dst_stride);
        }

        src += 16;
        dst += 16;
    }
}

void ff_put_vp8_pixels16_msa(uint8_t *dst, ptrdiff_t dst_stride,
                            uint8_t *src, ptrdiff_t src_stride,
                            int height, int mx, int my)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3;

    if (0 == height % 8) {
        copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 16);
    } else if (0 == height % 4) {
        for (cnt = (height >> 2); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);

            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}
