/*
 * Copyright (c) 2015 -2017 Parag Salasakar (Parag.Salasakar@imgtec.com)
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
#include "h264dsp_mips.h"

static const uint8_t luma_mask_arr[16 * 6] __attribute__((aligned(0x40))) = {
    /* 8 width cases */
    0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11, 7, 12,
    1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 9, 7, 10, 8, 11,
    2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,

    /* 4 width cases */
    0, 5, 1, 6, 2, 7, 3, 8, 16, 21, 17, 22, 18, 23, 19, 24,
    1, 4, 2, 5, 3, 6, 4, 7, 17, 20, 18, 21, 19, 22, 20, 23,
    2, 3, 3, 4, 4, 5, 5, 6, 18, 19, 19, 20, 20, 21, 21, 22,
};

#define AVC_CALC_DPADD_B_6PIX_2COEFF_SH(vec0, vec1, vec2, vec3, vec4, vec5,  \
                                        out1, out2)                          \
{                                                                            \
    v16i8 tmp0_m, tmp1_m;                                                    \
    v16i8 minus5b_m = __msa_ldi_b(-5);                                       \
    v16i8 plus20b_m = __msa_ldi_b(20);                                       \
                                                                             \
    ILVRL_B2_SB(vec5, vec0, tmp0_m, tmp1_m);                                 \
    HADD_SB2_SH(tmp0_m, tmp1_m, out1, out2);                                 \
    ILVRL_B2_SB(vec4, vec1, tmp0_m, tmp1_m);                                 \
    DPADD_SB2_SH(tmp0_m, tmp1_m, minus5b_m, minus5b_m, out1, out2);          \
    ILVRL_B2_SB(vec3, vec2, tmp0_m, tmp1_m);                                 \
    DPADD_SB2_SH(tmp0_m, tmp1_m, plus20b_m, plus20b_m, out1, out2);          \
}

#define AVC_HORZ_FILTER_SH(in0, in1, mask0, mask1, mask2)  \
( {                                                        \
    v8i16 out0_m;                                          \
    v16i8 tmp0_m;                                          \
    v16i8 minus5b = __msa_ldi_b(-5);                       \
    v16i8 plus20b = __msa_ldi_b(20);                       \
                                                           \
    tmp0_m = __msa_vshf_b((v16i8) mask0, in1, in0);        \
    out0_m = __msa_hadd_s_h(tmp0_m, tmp0_m);               \
                                                           \
    tmp0_m = __msa_vshf_b((v16i8) mask1, in1, in0);        \
    out0_m = __msa_dpadd_s_h(out0_m, minus5b, tmp0_m);     \
                                                           \
    tmp0_m = __msa_vshf_b((v16i8) mask2, in1, in0);        \
    out0_m = __msa_dpadd_s_h(out0_m, plus20b, tmp0_m);     \
                                                           \
    out0_m;                                                \
} )

#define AVC_DOT_SH3_SH(in0, in1, in2, coeff0, coeff1, coeff2)       \
( {                                                                 \
    v8i16 out0_m;                                                   \
                                                                    \
    out0_m = __msa_dotp_s_h((v16i8) in0, (v16i8) coeff0);           \
    out0_m = __msa_dpadd_s_h(out0_m, (v16i8) in1, (v16i8) coeff1);  \
    out0_m = __msa_dpadd_s_h(out0_m, (v16i8) in2, (v16i8) coeff2);  \
                                                                    \
    out0_m;                                                         \
} )

#define AVC_DOT_SW3_SW(in0, in1, in2, coeff0, coeff1, coeff2)       \
( {                                                                 \
    v4i32 out0_m;                                                   \
                                                                    \
    out0_m = __msa_dotp_s_w((v8i16) in0, (v8i16) coeff0);           \
    out0_m = __msa_dpadd_s_w(out0_m, (v8i16) in1, (v8i16) coeff1);  \
    out0_m = __msa_dpadd_s_w(out0_m, (v8i16) in2, (v8i16) coeff2);  \
    out0_m = __msa_srari_w(out0_m, 10);                             \
    out0_m = __msa_sat_s_w(out0_m, 7);                              \
    out0_m;                                                         \
} )

static void avc_luma_hv_qrt_4x4_msa(const uint8_t *src_x, const uint8_t *src_y,
                                    uint8_t *dst, int32_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out;
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3, src_vt7, src_vt8;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    v16i8 src_vt10_r, src_vt32_r, src_vt54_r, src_vt76_r;
    v16i8 mask0, mask1, mask2, filt0, filt1, filt2;
    v8i16 hz_out0, hz_out1, vt_out0, vt_out1, out0, out1;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    LD_SB5(src_y, stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * stride);

    src_vt0 = (v16i8) __msa_insve_w((v4i32) src_vt0, 1, (v4i32) src_vt1);
    src_vt1 = (v16i8) __msa_insve_w((v4i32) src_vt1, 1, (v4i32) src_vt2);
    src_vt2 = (v16i8) __msa_insve_w((v4i32) src_vt2, 1, (v4i32) src_vt3);
    src_vt3 = (v16i8) __msa_insve_w((v4i32) src_vt3, 1, (v4i32) src_vt4);

    XORI_B4_128_SB(src_vt0, src_vt1, src_vt2, src_vt3);

    LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
    hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz1, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src_hz2, src_hz3, mask0, mask1, mask2);

    SRARI_H2_SH(hz_out0, hz_out1, 5);
    SAT_SH2_SH(hz_out0, hz_out1, 7);

    LD_SB4(src_y, stride, src_vt5, src_vt6, src_vt7, src_vt8);

    src_vt4 = (v16i8) __msa_insve_w((v4i32) src_vt4, 1, (v4i32) src_vt5);
    src_vt5 = (v16i8) __msa_insve_w((v4i32) src_vt5, 1, (v4i32) src_vt6);
    src_vt6 = (v16i8) __msa_insve_w((v4i32) src_vt6, 1, (v4i32) src_vt7);
    src_vt7 = (v16i8) __msa_insve_w((v4i32) src_vt7, 1, (v4i32) src_vt8);

    XORI_B4_128_SB(src_vt4, src_vt5, src_vt6, src_vt7);
    ILVR_B2_SB(src_vt1, src_vt0, src_vt3, src_vt2, src_vt10_r, src_vt32_r);
    ILVR_B2_SB(src_vt5, src_vt4, src_vt7, src_vt6, src_vt54_r, src_vt76_r);
    vt_out0 = AVC_DOT_SH3_SH(src_vt10_r, src_vt32_r, src_vt54_r, filt0, filt1,
                             filt2);
    vt_out1 = AVC_DOT_SH3_SH(src_vt32_r, src_vt54_r, src_vt76_r, filt0, filt1,
                             filt2);
    SRARI_H2_SH(vt_out0, vt_out1, 5);
    SAT_SH2_SH(vt_out0, vt_out1, 7);

    out0 = __msa_srari_h((hz_out0 + vt_out0), 1);
    out1 = __msa_srari_h((hz_out1 + vt_out1), 1);

    SAT_SH2_SH(out0, out1, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

static void avc_luma_hv_qrt_8x8_msa(const uint8_t *src_x, const uint8_t *src_y,
                                    uint8_t *dst, int32_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out0, out1;
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3, mask0, mask1, mask2;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    v16i8 src_vt7, src_vt8, src_vt9, src_vt10, src_vt11, src_vt12;
    v16i8 src_vt10_r, src_vt21_r, src_vt32_r, src_vt43_r, src_vt54_r;
    v16i8 src_vt65_r, src_vt76_r, src_vt87_r, src_vt98_r, src_vt109_r;
    v16i8 src_vt1110_r, src_vt1211_r, filt0, filt1, filt2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, vt_out0, vt_out1, vt_out2;
    v8i16 vt_out3, tmp0, tmp1, tmp2, tmp3;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB5(src_y, stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * stride);

    XORI_B5_128_SB(src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);

    LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
    src_x += (4 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);

    SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
    SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

    LD_SB4(src_y, stride, src_vt5, src_vt6, src_vt7, src_vt8);
    src_y += (4 * stride);
    XORI_B4_128_SB(src_vt5, src_vt6, src_vt7, src_vt8);

    ILVR_B4_SB(src_vt1, src_vt0, src_vt2, src_vt1, src_vt3, src_vt2, src_vt4,
               src_vt3, src_vt10_r, src_vt21_r, src_vt32_r, src_vt43_r);
    ILVR_B4_SB(src_vt5, src_vt4, src_vt6, src_vt5, src_vt7, src_vt6, src_vt8,
               src_vt7, src_vt54_r, src_vt65_r, src_vt76_r, src_vt87_r);
    vt_out0 = AVC_DOT_SH3_SH(src_vt10_r, src_vt32_r, src_vt54_r, filt0, filt1,
                             filt2);
    vt_out1 = AVC_DOT_SH3_SH(src_vt21_r, src_vt43_r, src_vt65_r, filt0, filt1,
                             filt2);
    vt_out2 = AVC_DOT_SH3_SH(src_vt32_r, src_vt54_r, src_vt76_r, filt0, filt1,
                             filt2);
    vt_out3 = AVC_DOT_SH3_SH(src_vt43_r, src_vt65_r, src_vt87_r, filt0, filt1,
                             filt2);
    SRARI_H4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 5);
    SAT_SH4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 7);

    tmp0 = __msa_srari_h((hz_out0 + vt_out0), 1);
    tmp1 = __msa_srari_h((hz_out1 + vt_out1), 1);
    tmp2 = __msa_srari_h((hz_out2 + vt_out2), 1);
    tmp3 = __msa_srari_h((hz_out3 + vt_out3), 1);

    LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);

    SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src_y, stride, src_vt9, src_vt10, src_vt11, src_vt12);
    XORI_B4_128_SB(src_vt9, src_vt10, src_vt11, src_vt12);

    hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);

    SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
    SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

    ILVR_B4_SB(src_vt9, src_vt8, src_vt10, src_vt9, src_vt11, src_vt10,
               src_vt12, src_vt11, src_vt98_r, src_vt109_r, src_vt1110_r,
               src_vt1211_r);
    vt_out0 = AVC_DOT_SH3_SH(src_vt54_r, src_vt76_r, src_vt98_r, filt0, filt1,
                             filt2);
    vt_out1 = AVC_DOT_SH3_SH(src_vt65_r, src_vt87_r, src_vt109_r, filt0, filt1,
                             filt2);
    vt_out2 = AVC_DOT_SH3_SH(src_vt76_r, src_vt98_r, src_vt1110_r, filt0, filt1,
                             filt2);
    vt_out3 = AVC_DOT_SH3_SH(src_vt87_r, src_vt109_r, src_vt1211_r, filt0,
                             filt1, filt2);
    SRARI_H4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 5);
    SAT_SH4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 7);

    tmp0 = __msa_srari_h((hz_out0 + vt_out0), 1);
    tmp1 = __msa_srari_h((hz_out1 + vt_out1), 1);
    tmp2 = __msa_srari_h((hz_out2 + vt_out2), 1);
    tmp3 = __msa_srari_h((hz_out3 + vt_out3), 1);

    SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
}

static void avc_luma_hv_qrt_16x16_msa(const uint8_t *src_x,
                                      const uint8_t *src_y, uint8_t *dst,
                                      int32_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    const uint8_t *src_x_tmp = src_x;
    const uint8_t *src_y_tmp = src_y;
    uint8_t *dst_tmp = dst;
    uint32_t multiple8_cnt, loop_cnt;
    v16u8 tmp0, tmp1;
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3, mask0, mask1, mask2;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    v16i8 src_vt7, src_vt8;
    v16i8 src_vt10_r, src_vt21_r, src_vt32_r, src_vt43_r, src_vt54_r;
    v16i8 src_vt65_r, src_vt76_r, src_vt87_r, filt0, filt1, filt2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, vt_out0, vt_out1, vt_out2;
    v8i16 vt_out3, out0, out1, out2, out3;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        src_x = src_x_tmp;
        src_y = src_y_tmp;
        dst = dst_tmp;

        LD_SB5(src_y, stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
        src_y += (5 * stride);

        XORI_B5_128_SB(src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
            XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
            src_x += (4 * stride);

            hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
            hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
            hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
            hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);
            SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
            SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

            LD_SB4(src_y, stride, src_vt5, src_vt6, src_vt7, src_vt8);
            src_y += (4 * stride);

            XORI_B4_128_SB(src_vt5, src_vt6, src_vt7, src_vt8);
            ILVR_B4_SB(src_vt1, src_vt0, src_vt2, src_vt1, src_vt3, src_vt2,
                       src_vt4, src_vt3, src_vt10_r, src_vt21_r, src_vt32_r,
                       src_vt43_r);
            ILVR_B4_SB(src_vt5, src_vt4, src_vt6, src_vt5, src_vt7, src_vt6,
                       src_vt8, src_vt7, src_vt54_r, src_vt65_r, src_vt76_r,
                       src_vt87_r);
            vt_out0 = AVC_DOT_SH3_SH(src_vt10_r, src_vt32_r, src_vt54_r, filt0,
                                     filt1, filt2);
            vt_out1 = AVC_DOT_SH3_SH(src_vt21_r, src_vt43_r, src_vt65_r, filt0,
                                     filt1, filt2);
            vt_out2 = AVC_DOT_SH3_SH(src_vt32_r, src_vt54_r, src_vt76_r, filt0,
                                     filt1, filt2);
            vt_out3 = AVC_DOT_SH3_SH(src_vt43_r, src_vt65_r, src_vt87_r, filt0,
                                     filt1, filt2);
            SRARI_H4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 5);
            SAT_SH4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 7);

            out0 = __msa_srari_h((hz_out0 + vt_out0), 1);
            out1 = __msa_srari_h((hz_out1 + vt_out1), 1);
            out2 = __msa_srari_h((hz_out2 + vt_out2), 1);
            out3 = __msa_srari_h((hz_out3 + vt_out3), 1);

            SAT_SH4_SH(out0, out1, out2, out3, 7);
            tmp0 = PCKEV_XORI128_UB(out0, out1);
            tmp1 = PCKEV_XORI128_UB(out2, out3);
            ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, stride);
            dst += (4 * stride);

            src_vt0 = src_vt4;
            src_vt1 = src_vt5;
            src_vt2 = src_vt6;
            src_vt3 = src_vt7;
            src_vt4 = src_vt8;
        }

        src_x_tmp += 8;
        src_y_tmp += 8;
        dst_tmp += 8;
    }
}

static void avc_luma_hv_qrt_and_aver_dst_4x4_msa(const uint8_t *src_x,
                                                 const uint8_t *src_y,
                                                 uint8_t *dst,
                                                 int32_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 res, dst0 = { 0 };
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3, src_vt7, src_vt8;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    v16i8 src_vt10_r, src_vt32_r, src_vt54_r, src_vt76_r;
    v16i8 mask0, mask1, mask2, filt0, filt1, filt2;
    v8i16 hz_out0, hz_out1, vt_out0, vt_out1, res0, res1;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    LD_SB5(src_y, stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * stride);

    src_vt0 = (v16i8) __msa_insve_w((v4i32) src_vt0, 1, (v4i32) src_vt1);
    src_vt1 = (v16i8) __msa_insve_w((v4i32) src_vt1, 1, (v4i32) src_vt2);
    src_vt2 = (v16i8) __msa_insve_w((v4i32) src_vt2, 1, (v4i32) src_vt3);
    src_vt3 = (v16i8) __msa_insve_w((v4i32) src_vt3, 1, (v4i32) src_vt4);

    XORI_B4_128_SB(src_vt0, src_vt1, src_vt2, src_vt3);

    LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
    hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz1, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src_hz2, src_hz3, mask0, mask1, mask2);

    SRARI_H2_SH(hz_out0, hz_out1, 5);
    SAT_SH2_SH(hz_out0, hz_out1, 7);

    LD_SB4(src_y, stride, src_vt5, src_vt6, src_vt7, src_vt8);

    src_vt4 = (v16i8) __msa_insve_w((v4i32) src_vt4, 1, (v4i32) src_vt5);
    src_vt5 = (v16i8) __msa_insve_w((v4i32) src_vt5, 1, (v4i32) src_vt6);
    src_vt6 = (v16i8) __msa_insve_w((v4i32) src_vt6, 1, (v4i32) src_vt7);
    src_vt7 = (v16i8) __msa_insve_w((v4i32) src_vt7, 1, (v4i32) src_vt8);

    XORI_B4_128_SB(src_vt4, src_vt5, src_vt6, src_vt7);
    ILVR_B2_SB(src_vt1, src_vt0, src_vt3, src_vt2, src_vt10_r, src_vt32_r);
    ILVR_B2_SB(src_vt5, src_vt4, src_vt7, src_vt6, src_vt54_r, src_vt76_r);
    vt_out0 = AVC_DOT_SH3_SH(src_vt10_r, src_vt32_r, src_vt54_r, filt0, filt1,
                             filt2);
    vt_out1 = AVC_DOT_SH3_SH(src_vt32_r, src_vt54_r, src_vt76_r, filt0, filt1,
                             filt2);
    SRARI_H2_SH(vt_out0, vt_out1, 5);
    SAT_SH2_SH(vt_out0, vt_out1, 7);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);

    res1 = __msa_srari_h((hz_out1 + vt_out1), 1);
    res0 = __msa_srari_h((hz_out0 + vt_out0), 1);

    SAT_SH2_SH(res0, res1, 7);
    res = PCKEV_XORI128_UB(res0, res1);
    dst0 = __msa_aver_u_b(res, dst0);

    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

static void avc_luma_hv_qrt_and_aver_dst_8x8_msa(const uint8_t *src_x,
                                                 const uint8_t *src_y,
                                                 uint8_t *dst,
                                                 int32_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 out0, out1, dst0 = { 0 }, dst1 = { 0 };
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3, src_vt0, src_vt1, src_vt2;
    v16i8 src_vt3, src_vt4, src_vt5, src_vt6, src_vt7, src_vt8;
    v16i8 src_vt9, src_vt10, src_vt11, src_vt12, mask0, mask1, mask2;
    v16i8 src_vt10_r, src_vt21_r, src_vt32_r, src_vt43_r, src_vt54_r;
    v16i8 src_vt65_r, src_vt76_r, src_vt87_r, src_vt98_r, src_vt109_r;
    v16i8 src_vt1110_r, src_vt1211_r, filt0, filt1, filt2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, vt_out0, vt_out1, vt_out2;
    v8i16 vt_out3, tmp0, tmp1, tmp2, tmp3;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB5(src_y, stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * stride);

    XORI_B5_128_SB(src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);

    LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
    src_x += (4 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);

    SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
    SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

    LD_SB4(src_y, stride, src_vt5, src_vt6, src_vt7, src_vt8);
    src_y += (4 * stride);
    XORI_B4_128_SB(src_vt5, src_vt6, src_vt7, src_vt8);

    ILVR_B4_SB(src_vt1, src_vt0, src_vt2, src_vt1, src_vt3, src_vt2, src_vt4,
               src_vt3, src_vt10_r, src_vt21_r, src_vt32_r, src_vt43_r);
    ILVR_B4_SB(src_vt5, src_vt4, src_vt6, src_vt5, src_vt7, src_vt6, src_vt8,
               src_vt7, src_vt54_r, src_vt65_r, src_vt76_r, src_vt87_r);
    vt_out0 = AVC_DOT_SH3_SH(src_vt10_r, src_vt32_r, src_vt54_r, filt0, filt1,
                             filt2);
    vt_out1 = AVC_DOT_SH3_SH(src_vt21_r, src_vt43_r, src_vt65_r, filt0, filt1,
                             filt2);
    vt_out2 = AVC_DOT_SH3_SH(src_vt32_r, src_vt54_r, src_vt76_r, filt0, filt1,
                             filt2);
    vt_out3 = AVC_DOT_SH3_SH(src_vt43_r, src_vt65_r, src_vt87_r, filt0, filt1,
                             filt2);
    SRARI_H4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 5);
    SAT_SH4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 7);

    tmp0 = __msa_srari_h((hz_out0 + vt_out0), 1);
    tmp1 = __msa_srari_h((hz_out1 + vt_out1), 1);
    tmp2 = __msa_srari_h((hz_out2 + vt_out2), 1);
    tmp3 = __msa_srari_h((hz_out3 + vt_out3), 1);

    LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);

    SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src_y, stride, src_vt9, src_vt10, src_vt11, src_vt12);
    XORI_B4_128_SB(src_vt9, src_vt10, src_vt11, src_vt12);

    hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);

    SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
    SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

    ILVR_B4_SB(src_vt9, src_vt8, src_vt10, src_vt9, src_vt11, src_vt10,
               src_vt12, src_vt11, src_vt98_r, src_vt109_r, src_vt1110_r,
               src_vt1211_r);
    vt_out0 = AVC_DOT_SH3_SH(src_vt54_r, src_vt76_r, src_vt98_r, filt0, filt1,
                             filt2);
    vt_out1 = AVC_DOT_SH3_SH(src_vt65_r, src_vt87_r, src_vt109_r, filt0, filt1,
                             filt2);
    vt_out2 = AVC_DOT_SH3_SH(src_vt76_r, src_vt98_r, src_vt1110_r, filt0, filt1,
                             filt2);
    vt_out3 = AVC_DOT_SH3_SH(src_vt87_r, src_vt109_r, src_vt1211_r, filt0,
                             filt1, filt2);
    SRARI_H4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 5);
    SAT_SH4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 7);

    tmp0 = __msa_srari_h((hz_out0 + vt_out0), 1);
    tmp1 = __msa_srari_h((hz_out1 + vt_out1), 1);
    tmp2 = __msa_srari_h((hz_out2 + vt_out2), 1);
    tmp3 = __msa_srari_h((hz_out3 + vt_out3), 1);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);

    SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
}

static void avc_luma_hv_qrt_and_aver_dst_16x16_msa(const uint8_t *src_x,
                                                   const uint8_t *src_y,
                                                   uint8_t *dst,
                                                   int32_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    const uint8_t *src_x_tmp = src_x;
    const uint8_t *src_y_tmp = src_y;
    uint8_t *dst_tmp = dst;
    uint32_t multiple8_cnt, loop_cnt;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 tmp0, tmp1, dst0 = { 0 }, dst1 = { 0 };
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3, mask0, mask1, mask2;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4, src_vt5, src_vt6;
    v16i8 src_vt7, src_vt8;
    v16i8 src_vt10_r, src_vt21_r, src_vt32_r, src_vt43_r, src_vt54_r;
    v16i8 src_vt65_r, src_vt76_r, src_vt87_r, filt0, filt1, filt2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, vt_out0, vt_out1, vt_out2;
    v8i16 vt_out3, out0, out1, out2, out3;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        src_x = src_x_tmp;
        src_y = src_y_tmp;
        dst = dst_tmp;

        LD_SB5(src_y, stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
        src_y += (5 * stride);

        XORI_B5_128_SB(src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB4(src_x, stride, src_hz0, src_hz1, src_hz2, src_hz3);
            XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
            src_x += (4 * stride);

            hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, src_hz0, mask0, mask1, mask2);
            hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, src_hz1, mask0, mask1, mask2);
            hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, src_hz2, mask0, mask1, mask2);
            hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, src_hz3, mask0, mask1, mask2);
            SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
            SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

            LD_SB4(src_y, stride, src_vt5, src_vt6, src_vt7, src_vt8);
            src_y += (4 * stride);

            XORI_B4_128_SB(src_vt5, src_vt6, src_vt7, src_vt8);
            ILVR_B4_SB(src_vt1, src_vt0, src_vt2, src_vt1, src_vt3, src_vt2,
                       src_vt4, src_vt3, src_vt10_r, src_vt21_r, src_vt32_r,
                       src_vt43_r);
            ILVR_B4_SB(src_vt5, src_vt4, src_vt6, src_vt5, src_vt7, src_vt6,
                       src_vt8, src_vt7, src_vt54_r, src_vt65_r, src_vt76_r,
                       src_vt87_r);
            vt_out0 = AVC_DOT_SH3_SH(src_vt10_r, src_vt32_r, src_vt54_r, filt0,
                                     filt1, filt2);
            vt_out1 = AVC_DOT_SH3_SH(src_vt21_r, src_vt43_r, src_vt65_r, filt0,
                                     filt1, filt2);
            vt_out2 = AVC_DOT_SH3_SH(src_vt32_r, src_vt54_r, src_vt76_r, filt0,
                                     filt1, filt2);
            vt_out3 = AVC_DOT_SH3_SH(src_vt43_r, src_vt65_r, src_vt87_r, filt0,
                                     filt1, filt2);
            SRARI_H4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 5);
            SAT_SH4_SH(vt_out0, vt_out1, vt_out2, vt_out3, 7);

            out0 = __msa_srari_h((hz_out0 + vt_out0), 1);
            out1 = __msa_srari_h((hz_out1 + vt_out1), 1);
            out2 = __msa_srari_h((hz_out2 + vt_out2), 1);
            out3 = __msa_srari_h((hz_out3 + vt_out3), 1);

            LD4(dst, stride, tp0, tp1, tp2, tp3);
            INSERT_D2_UB(tp0, tp1, dst0);
            INSERT_D2_UB(tp2, tp3, dst1);

            SAT_SH4_SH(out0, out1, out2, out3, 7);
            tmp0 = PCKEV_XORI128_UB(out0, out1);
            tmp1 = PCKEV_XORI128_UB(out2, out3);
            AVER_UB2_UB(tmp0, dst0, tmp1, dst1, dst0, dst1);
            ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
            dst += (4 * stride);

            src_vt0 = src_vt4;
            src_vt1 = src_vt5;
            src_vt2 = src_vt6;
            src_vt3 = src_vt7;
            src_vt4 = src_vt8;
        }

        src_x_tmp += 8;
        src_y_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_put_h264_qpel16_mc00_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;

    LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * stride);
    LD_UB8(src, stride, src8, src9, src10, src11, src12, src13, src14, src15);

    ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7, dst, stride);
    dst += (8 * stride);
    ST_UB8(src8, src9, src10, src11, src12, src13, src14, src15, dst, stride);
}

void ff_put_h264_qpel8_mc00_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t src0, src1, src2, src3, src4, src5, src6, src7;

    LD4(src, stride, src0, src1, src2, src3);
    src += 4 * stride;
    LD4(src, stride, src4, src5, src6, src7);
    SD4(src0, src1, src2, src3, dst, stride);
    dst += 4 * stride;
    SD4(src4, src5, src6, src7, dst, stride);
}

void ff_avg_h264_qpel16_mc00_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * stride);
    LD_UB8(dst, stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);

    AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3, dst0, dst1,
                dst2, dst3);
    AVER_UB4_UB(src4, dst4, src5, dst5, src6, dst6, src7, dst7, dst4, dst5,
                dst6, dst7);
    ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst, stride);
    dst += (8 * stride);

    LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8(dst, stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);

    AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3, dst0, dst1,
                dst2, dst3);
    AVER_UB4_UB(src4, dst4, src5, dst5, src6, dst6, src7, dst7, dst4, dst5,
                dst6, dst7);
    ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst, stride);
}

void ff_avg_h264_qpel8_mc00_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3, tp4, tp5, tp6, tp7;
    v16u8 src0 = { 0 }, src1 = { 0 }, src2 = { 0 }, src3 = { 0 };
    v16u8 dst0 = { 0 }, dst1 = { 0 }, dst2 = { 0 }, dst3 = { 0 };

    LD4(src, stride, tp0, tp1, tp2, tp3);
    src += 4 * stride;
    LD4(src, stride, tp4, tp5, tp6, tp7);
    INSERT_D2_UB(tp0, tp1, src0);
    INSERT_D2_UB(tp2, tp3, src1);
    INSERT_D2_UB(tp4, tp5, src2);
    INSERT_D2_UB(tp6, tp7, src3);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    LD4(dst + 4 * stride, stride, tp4, tp5, tp6, tp7);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    INSERT_D2_UB(tp4, tp5, dst2);
    INSERT_D2_UB(tp6, tp7, dst3);

    AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3, dst0, dst1,
                dst2, dst3);

    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc00_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    v16u8 src0 = { 0 }, dst0 = { 0 };

    LW4(src, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, src0);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);

    dst0 = __msa_aver_u_b(src0, dst0);

    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc10_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t loop_cnt;
    v16i8 dst0, dst1, dst2, dst3, src0, src1, src2, src3, src4, src5, src6;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, src7, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    mask3 = mask0 + 8;
    mask4 = mask1 + 8;
    mask5 = mask2 + 8;
    src -= 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src += stride;
        LD_SB2(src, 16, src2, src3);
        src += stride;
        LD_SB2(src, 16, src4, src5);
        src += stride;
        LD_SB2(src, 16, src6, src7);
        src += stride;

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src2, src2, src2, src3, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src2, src2, src2, src3, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src0, src0, src0, src1, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src2, src2, src2, src3, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res0, res1, res2, res3);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res0, res1, res2, res3);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res0, res1, res2, res3);
        VSHF_B2_SB(src4, src4, src4, src5, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src6, src6, src6, src7, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src4, src4, src4, src5, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src6, src6, src6, src7, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src4, src4, src4, src5, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src6, src6, src6, src7, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res4, res5, res6, res7);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res4, res5, res6, res7);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res4, res5, res6, res7);
        SLDI_B4_SB(src1, src0, src3, src2, src5, src4, src7, src6, 2,
                   src0, src2, src4, src6);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SRARI_H4_SH(res4, res5, res6, res7, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        SAT_SH4_SH(res4, res5, res6, res7, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, dst0, dst1);
        PCKEV_B2_SB(res5, res4, res7, res6, dst2, dst3);
        dst0 = __msa_aver_s_b(dst0, src0);
        dst1 = __msa_aver_s_b(dst1, src2);
        dst2 = __msa_aver_s_b(dst2, src4);
        dst3 = __msa_aver_s_b(dst3, src6);
        XORI_B4_128_SB(dst0, dst1, dst2, dst3);
        ST_SB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }
}

void ff_put_h264_qpel16_mc30_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t loop_cnt;
    v16i8 dst0, dst1, dst2, dst3, src0, src1, src2, src3, src4, src5, src6;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, src7, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    mask3 = mask0 + 8;
    mask4 = mask1 + 8;
    mask5 = mask2 + 8;
    src -= 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src += stride;
        LD_SB2(src, 16, src2, src3);
        src += stride;
        LD_SB2(src, 16, src4, src5);
        src += stride;
        LD_SB2(src, 16, src6, src7);
        src += stride;

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src2, src2, src2, src3, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src2, src2, src2, src3, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src0, src0, src0, src1, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src2, src2, src2, src3, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res0, res1, res2, res3);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res0, res1, res2, res3);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res0, res1, res2, res3);
        VSHF_B2_SB(src4, src4, src4, src5, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src6, src6, src6, src7, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src4, src4, src4, src5, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src6, src6, src6, src7, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src4, src4, src4, src5, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src6, src6, src6, src7, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res4, res5, res6, res7);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res4, res5, res6, res7);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res4, res5, res6, res7);
        SLDI_B4_SB(src1, src0, src3, src2, src5, src4, src7, src6, 3,
                   src0, src2, src4, src6);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SRARI_H4_SH(res4, res5, res6, res7, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        SAT_SH4_SH(res4, res5, res6, res7, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, dst0, dst1);
        PCKEV_B2_SB(res5, res4, res7, res6, dst2, dst3);
        dst0 = __msa_aver_s_b(dst0, src0);
        dst1 = __msa_aver_s_b(dst1, src2);
        dst2 = __msa_aver_s_b(dst2, src4);
        dst3 = __msa_aver_s_b(dst3, src6);
        XORI_B4_128_SB(dst0, dst1, dst2, dst3);
        ST_SB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }
}

void ff_put_h264_qpel8_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 tmp0, tmp1, tmp2, tmp3, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB8(src - 2, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res4, res5, res6, res7);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 2,
               src0, src1, src2, src3);
    SLDI_B4_SB(src4, src4, src5, src5, src6, src6, src7, src7, 2,
               src4, src5, src6, src7);
    PCKEV_D2_SB(src1, src0, src3, src2, src0, src1);
    PCKEV_D2_SB(src5, src4, src7, src6, src4, src5);
    SRARI_H4_SH(res0, res1, res2, res3, 5);
    SRARI_H4_SH(res4, res5, res6, res7, 5);
    SAT_SH4_SH(res0, res1, res2, res3, 7);
    SAT_SH4_SH(res4, res5, res6, res7, 7);
    PCKEV_B2_SB(res1, res0, res3, res2, tmp0, tmp1);
    PCKEV_B2_SB(res5, res4, res7, res6, tmp2, tmp3);
    tmp0 = __msa_aver_s_b(tmp0, src0);
    tmp1 = __msa_aver_s_b(tmp1, src1);
    tmp2 = __msa_aver_s_b(tmp2, src4);
    tmp3 = __msa_aver_s_b(tmp3, src5);
    XORI_B4_128_SB(tmp0, tmp1, tmp2, tmp3);
    ST_D8(tmp0, tmp1, tmp2, tmp3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel8_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 tmp0, tmp1, tmp2, tmp3, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB8(src - 2, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res4, res5, res6, res7);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 3,
               src0, src1, src2, src3);
    SLDI_B4_SB(src4, src4, src5, src5, src6, src6, src7, src7, 3,
               src4, src5, src6, src7);
    PCKEV_D2_SB(src1, src0, src3, src2, src0, src1);
    PCKEV_D2_SB(src5, src4, src7, src6, src4, src5);
    SRARI_H4_SH(res0, res1, res2, res3, 5);
    SRARI_H4_SH(res4, res5, res6, res7, 5);
    SAT_SH4_SH(res0, res1, res2, res3, 7);
    SAT_SH4_SH(res4, res5, res6, res7, 7);
    PCKEV_B2_SB(res1, res0, res3, res2, tmp0, tmp1);
    PCKEV_B2_SB(res5, res4, res7, res6, tmp2, tmp3);
    tmp0 = __msa_aver_s_b(tmp0, src0);
    tmp1 = __msa_aver_s_b(tmp1, src1);
    tmp2 = __msa_aver_s_b(tmp2, src4);
    tmp3 = __msa_aver_s_b(tmp3, src5);
    XORI_B4_128_SB(tmp0, tmp1, tmp2, tmp3);
    ST_D8(tmp0, tmp1, tmp2, tmp3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel4_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    v16i8 src0, src1, src2, src3, res, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 res0, res1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src - 2, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, res0, res1);
    SRARI_H2_SH(res0, res1, 5);
    SAT_SH2_SH(res0, res1, 7);
    res = __msa_pckev_b((v16i8) res1, (v16i8) res0);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 2,
               src0, src1, src2, src3);
    src0 = (v16i8) __msa_insve_w((v4i32) src0, 1, (v4i32) src1);
    src1 = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    src0 = (v16i8) __msa_insve_d((v2i64) src0, 1, (v2i64) src1);
    res = __msa_aver_s_b(res, src0);
    res = (v16i8) __msa_xori_b((v16u8) res, 128);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel4_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    v16i8 src0, src1, src2, src3, res, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 res0, res1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src - 2, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, res0, res1);
    SRARI_H2_SH(res0, res1, 5);
    SAT_SH2_SH(res0, res1, 7);
    res = __msa_pckev_b((v16i8) res1, (v16i8) res0);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 3,
               src0, src1, src2, src3);
    src0 = (v16i8) __msa_insve_w((v4i32) src0, 1, (v4i32) src1);
    src1 = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    src0 = (v16i8) __msa_insve_d((v2i64) src0, 1, (v2i64) src1);
    res = __msa_aver_s_b(res, src0);
    res = (v16i8) __msa_xori_b((v16u8) res, 128);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc20_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 vec11;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    src -= 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 8, src0, src1);
        src += stride;
        LD_SB2(src, 8, src2, src3);
        src += stride;
        LD_SB2(src, 8, src4, src5);
        src += stride;
        LD_SB2(src, 8, src6, src7);
        src += stride;

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec3);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec6, vec9);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec1, vec4);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec7, vec10);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec2, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res0, res1, res2, res3);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res0, res1, res2, res3);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res0, res1, res2, res3);
        VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec3);
        VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec6, vec9);
        VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec1, vec4);
        VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec7, vec10);
        VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec2, vec5);
        VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res4, res5, res6, res7);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res4, res5, res6, res7);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res4, res5, res6, res7);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SRARI_H4_SH(res4, res5, res6, res7, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        SAT_SH4_SH(res4, res5, res6, res7, 7);
        PCKEV_B4_SB(res1, res0, res3, res2, res5, res4, res7, res6, vec0, vec1,
                    vec2, vec3);
        XORI_B4_128_SB(vec0, vec1, vec2, vec3);
        ST_SB4(vec0, vec1, vec2, vec3, dst, stride);
        dst += (4 * stride);
    }
}

void ff_put_h264_qpel8_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 vec11;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB8(src - 2, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b,
                 plus20b, res0, res1, res2, res3);
    VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b,
                 plus20b, res4, res5, res6, res7);
    SRARI_H4_SH(res0, res1, res2, res3, 5);
    SRARI_H4_SH(res4, res5, res6, res7, 5);
    SAT_SH4_SH(res0, res1, res2, res3, 7);
    SAT_SH4_SH(res4, res5, res6, res7, 7);
    out0 = PCKEV_XORI128_UB(res0, res1);
    out1 = PCKEV_XORI128_UB(res2, res3);
    out2 = PCKEV_XORI128_UB(res4, res5);
    out3 = PCKEV_XORI128_UB(res6, res7);
    ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel4_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 res0, res1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src - 2, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, res0, res1);
    SRARI_H2_SH(res0, res1, 5);
    SAT_SH2_SH(res0, res1, 7);
    out = PCKEV_XORI128_UB(res0, res1);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc01_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res0, res1, res2, res3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, stride, src5, src6, src7, src8);
        src += (4 * stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src2);
        res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src3);
        res2 = (v16u8) __msa_aver_s_b((v16i8) res2, src4);
        res3 = (v16u8) __msa_aver_s_b((v16i8) res3, src5);
        XORI_B4_128_UB(res0, res1, res2, res3);
        ST_UB4(res0, res1, res2, res3, dst, stride);
        dst += (4 * stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src10_l = src54_l;
        src32_l = src76_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src2 = src6;
        src3 = src7;
        src4 = src8;
    }
}

void ff_put_h264_qpel16_mc03_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res0, res1, res2, res3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, stride, src5, src6, src7, src8);
        src += (4 * stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src3);
        res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src4);
        res2 = (v16u8) __msa_aver_s_b((v16i8) res2, src5);
        res3 = (v16u8) __msa_aver_s_b((v16i8) res3, src6);
        XORI_B4_128_UB(res0, res1, res2, res3);
        ST_UB4(res0, res1, res2, res3, dst, stride);
        dst += (4 * stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src10_l = src54_l;
        src32_l = src76_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src3 = src7;
        src4 = src8;
    }
}

void ff_put_h264_qpel8_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, src10_r, src32_r, src54_r, src65_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r, src1211_r, src1110_r;
    v16i8 tmp0, tmp1, tmp2, tmp3, filt0, filt1, filt2, out0, out1, out2, out3;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r, out6_r, out7_r;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB8(src, stride, src5, src6, src7, src8, src9, src10, src11, src12);
    XORI_B8_128_SB(src5, src6, src7, src8, src9, src10, src11, src12);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_B4_SB(src9, src8, src10, src9, src11, src10, src12, src11, src98_r,
               src109_r, src1110_r, src1211_r);
    out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
    out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
    out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
    out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
    out4_r = AVC_DOT_SH3_SH(src54_r, src76_r, src98_r, filt0, filt1, filt2);
    out5_r = AVC_DOT_SH3_SH(src65_r, src87_r, src109_r, filt0, filt1, filt2);
    out6_r = AVC_DOT_SH3_SH(src76_r, src98_r, src1110_r, filt0, filt1, filt2);
    out7_r = AVC_DOT_SH3_SH(src87_r, src109_r, src1211_r, filt0, filt1, filt2);
    PCKEV_D2_SB(src3, src2, src5, src4, tmp0, tmp1);
    PCKEV_D2_SB(src7, src6, src9, src8, tmp2, tmp3);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
    SRARI_H4_SH(out4_r, out5_r, out6_r, out7_r, 5);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH4_SH(out4_r, out5_r, out6_r, out7_r, 7);
    PCKEV_B2_SB(out1_r, out0_r, out3_r, out2_r, out0, out1);
    PCKEV_B2_SB(out5_r, out4_r, out7_r, out6_r, out2, out3);
    out0 = __msa_aver_s_b(out0, tmp0);
    out1 = __msa_aver_s_b(out1, tmp1);
    out2 = __msa_aver_s_b(out2, tmp2);
    out3 = __msa_aver_s_b(out3, tmp3);
    XORI_B4_128_SB(out0, out1, out2, out3);
    ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel8_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, src10_r, src32_r, src54_r, src65_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r, src1211_r, src1110_r;
    v16i8 filt0, filt1, filt2, out0, out1, out2, out3, tmp0, tmp1, tmp2, tmp3;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r, out6_r, out7_r;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB8(src, stride, src5, src6, src7, src8, src9, src10, src11, src12);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B8_128_SB(src5, src6, src7, src8, src9, src10, src11, src12);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_B4_SB(src9, src8, src10, src9, src11, src10, src12, src11, src98_r,
               src109_r, src1110_r, src1211_r);
    out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
    out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
    out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
    out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
    out4_r = AVC_DOT_SH3_SH(src54_r, src76_r, src98_r, filt0, filt1, filt2);
    out5_r = AVC_DOT_SH3_SH(src65_r, src87_r, src109_r, filt0, filt1, filt2);
    out6_r = AVC_DOT_SH3_SH(src76_r, src98_r, src1110_r, filt0, filt1, filt2);
    out7_r = AVC_DOT_SH3_SH(src87_r, src109_r, src1211_r, filt0, filt1, filt2);
    PCKEV_D2_SB(src4, src3, src6, src5, tmp0, tmp1);
    PCKEV_D2_SB(src8, src7, src10, src9, tmp2, tmp3);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
    SRARI_H4_SH(out4_r, out5_r, out6_r, out7_r, 5);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH4_SH(out4_r, out5_r, out6_r, out7_r, 7);
    PCKEV_B2_SB(out1_r, out0_r, out3_r, out2_r, out0, out1);
    PCKEV_B2_SB(out5_r, out4_r, out7_r, out6_r, out2, out3);
    out0 = __msa_aver_s_b(out0, tmp0);
    out1 = __msa_aver_s_b(out1, tmp1);
    out2 = __msa_aver_s_b(out2, tmp2);
    out3 = __msa_aver_s_b(out3, tmp3);
    XORI_B4_128_SB(out0, out1, out2, out3);
    ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel4_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v8i16 out10, out32;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    out10 = AVC_DOT_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = AVC_DOT_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    out = PCKEV_XORI128_UB(out10, out32);
    src32_r = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    src54_r = (v16i8) __msa_insve_w((v4i32) src4, 1, (v4i32) src5);
    src32_r = (v16i8) __msa_insve_d((v2i64) src32_r, 1, (v2i64) src54_r);
    out = __msa_aver_u_b(out, (v16u8) src32_r);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel4_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v8i16 out10, out32;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    out10 = AVC_DOT_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = AVC_DOT_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    out = PCKEV_XORI128_UB(out10, out32);
    src32_r = (v16i8) __msa_insve_w((v4i32) src3, 1, (v4i32) src4);
    src54_r = (v16i8) __msa_insve_w((v4i32) src5, 1, (v4i32) src6);
    src32_r = (v16i8) __msa_insve_d((v2i64) src32_r, 1, (v2i64) src54_r);
    out = __msa_aver_u_b(out, (v16u8) src32_r);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc11_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_msa(src - 2, src - (stride * 2), dst, stride);
}

void ff_put_h264_qpel16_mc31_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_msa(src - 2, src - (stride * 2) + 1, dst, stride);
}

void ff_put_h264_qpel16_mc13_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_msa(src + stride - 2, src - (stride * 2), dst,
                              stride);
}

void ff_put_h264_qpel16_mc33_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16x16_msa(src + stride - 2, src - (stride * 2) + 1, dst,
                              stride);
}

void ff_put_h264_qpel8_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8x8_msa(src - 2, src - (stride * 2), dst, stride);
}

void ff_put_h264_qpel8_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8x8_msa(src - 2, src - (stride * 2) + 1, dst, stride);
}

void ff_put_h264_qpel8_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8x8_msa(src + stride - 2, src - (stride * 2), dst, stride);
}

void ff_put_h264_qpel8_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8x8_msa(src + stride - 2, src - (stride * 2) + 1, dst,
                            stride);
}


void ff_put_h264_qpel4_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4x4_msa(src - 2, src - (stride * 2), dst, stride);
}

void ff_put_h264_qpel4_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4x4_msa(src - 2, src - (stride * 2) + 1, dst, stride);
}

void ff_put_h264_qpel4_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4x4_msa(src + stride - 2, src - (stride * 2), dst, stride);
}

void ff_put_h264_qpel4_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4x4_msa(src + stride - 2, src - (stride * 2) + 1, dst,
                            stride);
}

void ff_put_h264_qpel16_mc21_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t *dst_tmp = dst;
    const uint8_t *src_tmp = src - (2 * stride) - 2;
    uint32_t multiple8_cnt, loop_cnt;
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, mask0, mask1;
    v16i8 mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out10_l, hz_out21_l;
    v8i16 hz_out32_l, hz_out43_l, hz_out54_l, hz_out65_l, hz_out76_l;
    v8i16 hz_out87_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        dst = dst_tmp;
        src = src_tmp;

        LD_SB5(src, stride, src0, src1, src2, src3, src4);
        XORI_B5_128_SB(src0, src1, src2, src3, src4);
        src += (5 * stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
        hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB4(src, stride, src5, src6, src7, src8);
            src += (4 * stride);

            XORI_B4_128_SB(src5, src6, src7, src8);

            hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
            hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
            hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
            hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);

            ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_r, hz_out21_r, hz_out32_r,
                       hz_out43_r);
            ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_l, hz_out21_l, hz_out32_l,
                       hz_out43_l);
            ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_r, hz_out65_r, hz_out76_r,
                       hz_out87_r);
            ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_l, hz_out65_l, hz_out76_l,
                       hz_out87_l);

            tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0,
                                  filt1, filt2);
            dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0,
                                  filt1, filt2);
            dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0,
                                  filt1, filt2);
            dst4 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0,
                                  filt1, filt2);
            dst6 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

            dst1 = __msa_srari_h(hz_out2, 5);
            dst3 = __msa_srari_h(hz_out3, 5);
            dst5 = __msa_srari_h(hz_out4, 5);
            dst7 = __msa_srari_h(hz_out5, 5);
            SAT_SH4_SH(dst1, dst3, dst5, dst7, 7);

            dst0 = __msa_aver_s_h(dst0, dst1);
            dst1 = __msa_aver_s_h(dst2, dst3);
            dst2 = __msa_aver_s_h(dst4, dst5);
            dst3 = __msa_aver_s_h(dst6, dst7);

            out0 = PCKEV_XORI128_UB(dst0, dst1);
            out1 = PCKEV_XORI128_UB(dst2, dst3);
            ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
            dst += (4 * stride);

            hz_out0 = hz_out4;
            hz_out1 = hz_out5;
            hz_out2 = hz_out6;
            hz_out3 = hz_out7;
            hz_out4 = hz_out8;
        }

        src_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_put_h264_qpel16_mc23_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t *dst_tmp = dst;
    const uint8_t *src_tmp = src - (2 * stride) - 2;
    uint32_t multiple8_cnt, loop_cnt;
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, mask0, mask1;
    v16i8 mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out10_l, hz_out21_l;
    v8i16 hz_out32_l, hz_out43_l, hz_out54_l, hz_out65_l, hz_out76_l;
    v8i16 hz_out87_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        dst = dst_tmp;
        src = src_tmp;

        LD_SB5(src, stride, src0, src1, src2, src3, src4);
        XORI_B5_128_SB(src0, src1, src2, src3, src4);
        src += (5 * stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
        hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB4(src, stride, src5, src6, src7, src8);
            src += (4 * stride);

            XORI_B4_128_SB(src5, src6, src7, src8);

            hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
            hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
            hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
            hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);

            ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_r, hz_out21_r, hz_out32_r,
                       hz_out43_r);
            ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_l, hz_out21_l, hz_out32_l,
                       hz_out43_l);
            ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_r, hz_out65_r, hz_out76_r,
                       hz_out87_r);
            ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_l, hz_out65_l, hz_out76_l,
                       hz_out87_l);

            tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0,
                                  filt1, filt2);
            dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0,
                                  filt1, filt2);
            dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0,
                                  filt1, filt2);
            dst4 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0,
                                  filt1, filt2);
            dst6 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

            dst1 = __msa_srari_h(hz_out3, 5);
            dst3 = __msa_srari_h(hz_out4, 5);
            dst5 = __msa_srari_h(hz_out5, 5);
            dst7 = __msa_srari_h(hz_out6, 5);
            SAT_SH4_SH(dst1, dst3, dst5, dst7, 7);

            dst0 = __msa_aver_s_h(dst0, dst1);
            dst1 = __msa_aver_s_h(dst2, dst3);
            dst2 = __msa_aver_s_h(dst4, dst5);
            dst3 = __msa_aver_s_h(dst6, dst7);

            out0 = PCKEV_XORI128_UB(dst0, dst1);
            out1 = PCKEV_XORI128_UB(dst2, dst3);
            ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
            dst += (4 * stride);

            hz_out0 = hz_out4;
            hz_out1 = hz_out5;
            hz_out2 = hz_out6;
            hz_out3 = hz_out7;
            hz_out4 = hz_out8;
        }

        src_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_put_h264_qpel8_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, hz_out11, hz_out12;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out89_r, hz_out910_r;
    v8i16 hz_out1110_r, hz_out1211_r, dst0, dst1, dst2, dst3;
    v8i16 hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l, hz_out54_l;
    v8i16 hz_out65_l, hz_out76_l, hz_out87_l, hz_out89_l, hz_out910_l;
    v8i16 hz_out1110_l, hz_out1211_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

    LD_SB4(src, stride, src5, src6, src7, src8);
    src += (4 * stride);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
    hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);
    ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_l, hz_out65_l, hz_out76_l, hz_out87_l);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0, filt1,
                          filt2);
    dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0, filt1,
                          filt2);
    dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    SRARI_H4_SH(hz_out2, hz_out3, hz_out4, hz_out5, 5);
    SAT_SH4_SH(hz_out2, hz_out3, hz_out4, hz_out5, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out2);
    dst1 = __msa_aver_s_h(dst1, hz_out3);
    dst2 = __msa_aver_s_h(dst2, hz_out4);
    dst3 = __msa_aver_s_h(dst3, hz_out5);

    out0 = PCKEV_XORI128_UB(dst0, dst1);
    out1 = PCKEV_XORI128_UB(dst2, dst3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src, stride, src9, src10, src11, src12);
    XORI_B4_128_SB(src9, src10, src11, src12);
    hz_out9 = AVC_HORZ_FILTER_SH(src9, src9, mask0, mask1, mask2);
    hz_out10 = AVC_HORZ_FILTER_SH(src10, src10, mask0, mask1, mask2);
    hz_out11 = AVC_HORZ_FILTER_SH(src11, src11, mask0, mask1, mask2);
    hz_out12 = AVC_HORZ_FILTER_SH(src12, src12, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_r, hz_out910_r, hz_out1110_r,
               hz_out1211_r);
    ILVL_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_l, hz_out910_l, hz_out1110_l,
               hz_out1211_l);
    tmp0 = AVC_DOT_SW3_SW(hz_out54_r, hz_out76_r, hz_out89_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out54_l, hz_out76_l, hz_out89_l, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out65_r, hz_out87_r, hz_out910_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out65_l, hz_out87_l, hz_out910_l, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out76_r, hz_out89_r, hz_out1110_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out76_l, hz_out89_l, hz_out1110_l, filt0, filt1,
                          filt2);
    dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out87_r, hz_out910_r, hz_out1211_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out87_l, hz_out910_l, hz_out1211_l, filt0, filt1,
                          filt2);
    dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    SRARI_H4_SH(hz_out6, hz_out7, hz_out8, hz_out9, 5);
    SAT_SH4_SH(hz_out6, hz_out7, hz_out8, hz_out9, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out6);
    dst1 = __msa_aver_s_h(dst1, hz_out7);
    dst2 = __msa_aver_s_h(dst2, hz_out8);
    dst3 = __msa_aver_s_h(dst3, hz_out9);

    out0 = PCKEV_XORI128_UB(dst0, dst1);
    out1 = PCKEV_XORI128_UB(dst2, dst3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel8_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, hz_out11, hz_out12;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out89_r, hz_out910_r;
    v8i16 hz_out1110_r, hz_out1211_r, dst0, dst1, dst2, dst3;
    v8i16 hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l, hz_out54_l;
    v8i16 hz_out65_l, hz_out76_l, hz_out87_l, hz_out89_l, hz_out910_l;
    v8i16 hz_out1110_l, hz_out1211_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

    LD_SB4(src, stride, src5, src6, src7, src8);
    src += (4 * stride);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
    hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);
    ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_l, hz_out65_l, hz_out76_l, hz_out87_l);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0, filt1,
                          filt2);
    dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0, filt1,
                          filt2);
    dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    SRARI_H4_SH(hz_out3, hz_out4, hz_out5, hz_out6, 5);
    SAT_SH4_SH(hz_out3, hz_out4, hz_out5, hz_out6, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out3);
    dst1 = __msa_aver_s_h(dst1, hz_out4);
    dst2 = __msa_aver_s_h(dst2, hz_out5);
    dst3 = __msa_aver_s_h(dst3, hz_out6);

    out0 = PCKEV_XORI128_UB(dst0, dst1);
    out1 = PCKEV_XORI128_UB(dst2, dst3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src, stride, src9, src10, src11, src12);
    XORI_B4_128_SB(src9, src10, src11, src12);
    hz_out9 = AVC_HORZ_FILTER_SH(src9, src9, mask0, mask1, mask2);
    hz_out10 = AVC_HORZ_FILTER_SH(src10, src10, mask0, mask1, mask2);
    hz_out11 = AVC_HORZ_FILTER_SH(src11, src11, mask0, mask1, mask2);
    hz_out12 = AVC_HORZ_FILTER_SH(src12, src12, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_r, hz_out910_r, hz_out1110_r,
               hz_out1211_r);
    ILVL_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_l, hz_out910_l, hz_out1110_l,
               hz_out1211_l);
    tmp0 = AVC_DOT_SW3_SW(hz_out54_r, hz_out76_r, hz_out89_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out54_l, hz_out76_l, hz_out89_l, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out65_r, hz_out87_r, hz_out910_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out65_l, hz_out87_l, hz_out910_l, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out76_r, hz_out89_r, hz_out1110_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out76_l, hz_out89_l, hz_out1110_l, filt0, filt1,
                          filt2);
    dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out87_r, hz_out910_r, hz_out1211_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out87_l, hz_out910_l, hz_out1211_l, filt0, filt1,
                          filt2);
    dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    SRARI_H4_SH(hz_out7, hz_out8, hz_out9, hz_out10, 5);
    SAT_SH4_SH(hz_out7, hz_out8, hz_out9, hz_out10, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out7);
    dst1 = __msa_aver_s_h(dst1, hz_out8);
    dst2 = __msa_aver_s_h(dst2, hz_out9);
    dst3 = __msa_aver_s_h(dst3, hz_out10);

    out0 = PCKEV_XORI128_UB(dst0, dst1);
    out1 = PCKEV_XORI128_UB(dst2, dst3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel4_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 res;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, filt0, filt1, filt2;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);
    PCKOD_D2_SH(hz_out4, hz_out4, hz_out6, hz_out6, hz_out5, hz_out7);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    SRARI_H2_SH(hz_out2, hz_out4, 5);
    SAT_SH2_SH(hz_out2, hz_out4, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out2);
    dst1 = __msa_aver_s_h(dst1, hz_out4);

    res = PCKEV_XORI128_UB(dst0, dst1);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel4_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 res;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, filt0, filt1, filt2;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);
    PCKOD_D2_SH(hz_out4, hz_out4, hz_out6, hz_out6, hz_out5, hz_out7);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    PCKEV_D2_SH(hz_out4, hz_out3, hz_out6, hz_out5, hz_out0, hz_out1);
    SRARI_H2_SH(hz_out0, hz_out1, 5);
    SAT_SH2_SH(hz_out0, hz_out1, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out0);
    dst1 = __msa_aver_s_h(dst1, hz_out1);

    res = PCKEV_XORI128_UB(dst0, dst1);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc02_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res0, res1, res2, res3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);
    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, stride, src5, src6, src7, src8);
        src += (4 * stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        XORI_B4_128_UB(res0, res1, res2, res3);
        ST_UB4(res0, res1, res2, res3, dst, stride);
        dst += (4 * stride);

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

void ff_put_h264_qpel8_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, src10_r, src21_r, src32_r, src43_r, src76_r, src87_r;
    v16i8 src98_r, src109_r, src89_r, src910_r, src1110_r, src1211_r;
    v16i8 filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r, out6_r, out7_r;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * stride);
    LD_SB5(src, stride, src8, src9, src10, src11, src12);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src76_r, src87_r,
               src98_r, src109_r);
    ILVR_B4_SB(src9, src8, src10, src9, src11, src10, src12, src11, src89_r,
               src910_r, src1110_r, src1211_r);
    XORI_B4_128_SB(src10_r, src21_r, src32_r, src43_r);
    XORI_B4_128_SB(src76_r, src87_r, src98_r, src109_r);
    XORI_B4_128_SB(src89_r, src910_r, src1110_r, src1211_r);
    out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
    out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
    out2_r = AVC_DOT_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
    out3_r = AVC_DOT_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
    out4_r = AVC_DOT_SH3_SH(src76_r, src98_r, src89_r, filt0, filt1, filt2);
    out5_r = AVC_DOT_SH3_SH(src87_r, src109_r, src910_r, filt0, filt1, filt2);
    out6_r = AVC_DOT_SH3_SH(src98_r, src89_r, src1110_r, filt0, filt1, filt2);
    out7_r = AVC_DOT_SH3_SH(src109_r, src910_r, src1211_r, filt0, filt1, filt2);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
    SRARI_H4_SH(out4_r, out5_r, out6_r, out7_r, 5);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH4_SH(out4_r, out5_r, out6_r, out7_r, 7);
    out0 = PCKEV_XORI128_UB(out0_r, out1_r);
    out1 = PCKEV_XORI128_UB(out2_r, out3_r);
    out2 = PCKEV_XORI128_UB(out4_r, out5_r);
    out3 = PCKEV_XORI128_UB(out6_r, out7_r);
    ST_D8(out0, out1, out2, out3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel4_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v8i16 out10, out32;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D4_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r, src87_r,
               src76_r, src2110, src4332, src6554, src8776);
    XORI_B4_128_SB(src2110, src4332, src6554, src8776);
    out10 = AVC_DOT_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = AVC_DOT_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    out = PCKEV_XORI128_UB(out10, out32);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc12_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t row;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3, mask3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    LD_SB5(src + 8, stride, src7, src8, src9, src10, src11);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src7, src8, src9, src10, src11);

    for (row = 16; row--;) {
        LD_SB2(src, 8, src5, src6);
        src += stride;
        XORI_B2_128_SB(src5, src6);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src7, src8, src9, src10, src11, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        dst0 = __msa_srari_h(shf_vec2, 5);
        dst1 = __msa_srari_h(shf_vec5, 5);
        dst2 = __msa_srari_h(shf_vec8, 5);
        dst3 = __msa_srari_h(shf_vec11, 5);
        SAT_SH4_SH(dst0, dst1, dst2, dst3, 7);
        PCKEV_H2_SH(dst2, dst0, dst3, dst1, dst0, dst1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, dst2, dst3);
        dst0 = __msa_aver_s_h(dst2, dst0);
        dst1 = __msa_aver_s_h(dst3, dst1);
        out = PCKEV_XORI128_UB(dst0, dst1);
        ST_UB(out, dst);
        dst += stride;

        src0 = src1;
        src1 = src2;
        src2 = src3;
        src3 = src4;
        src4 = src5;
        src7 = src8;
        src8 = src9;
        src9 = src10;
        src10 = src11;
        src11 = src6;
    }
}

void ff_put_h264_qpel16_mc32_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t row;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3, mask3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    LD_SB5(src + 8, stride, src7, src8, src9, src10, src11);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src7, src8, src9, src10, src11);

    for (row = 16; row--;) {
        LD_SB2(src, 8, src5, src6);
        src += stride;
        XORI_B2_128_SB(src5, src6);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src7, src8, src9, src10, src11, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        dst0 = __msa_srari_h(shf_vec2, 5);
        dst1 = __msa_srari_h(shf_vec5, 5);
        dst2 = __msa_srari_h(shf_vec8, 5);
        dst3 = __msa_srari_h(shf_vec11, 5);
        SAT_SH4_SH(dst0, dst1, dst2, dst3, 7);
        dst0 = __msa_pckod_h(dst2, dst0);
        dst1 = __msa_pckod_h(dst3, dst1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, dst2, dst3);
        dst0 = __msa_aver_s_h(dst2, dst0);
        dst1 = __msa_aver_s_h(dst3, dst1);
        out = PCKEV_XORI128_UB(dst0, dst1);
        ST_UB(out, dst);
        dst += stride;

        src0 = src1;
        src1 = src2;
        src2 = src3;
        src3 = src4;
        src4 = src5;
        src7 = src8;
        src8 = src9;
        src9 = src10;
        src10 = src11;
        src11 = src6;
    }
}

void ff_put_h264_qpel8_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t row;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11;
    v8i16 mask3, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    for (row = 4; row--;) {
        LD_SB2(src, stride, src5, src6);
        src += (2 * stride);
        XORI_B2_128_SB(src5, src6);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src1, src2, src3, src4, src5, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        dst0 = __msa_srari_h(shf_vec2, 5);
        dst1 = __msa_srari_h(shf_vec5, 5);
        dst2 = __msa_srari_h(shf_vec8, 5);
        dst3 = __msa_srari_h(shf_vec11, 5);
        SAT_SH4_SH(dst0, dst1, dst2, dst3, 7);
        PCKEV_H2_SH(dst2, dst0, dst3, dst1, dst0, dst1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, dst2, dst3);
        dst0 = __msa_aver_s_h(dst2, dst0);
        dst1 = __msa_aver_s_h(dst3, dst1);
        out = PCKEV_XORI128_UB(dst0, dst1);
        ST_D2(out, 0, 1, dst, stride);
        dst += (2 * stride);

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
    }
}

void ff_put_h264_qpel8_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t row;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11;
    v8i16 mask3, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    for (row = 4; row--;) {
        LD_SB2(src, stride, src5, src6);
        src += (2 * stride);
        XORI_B2_128_SB(src5, src6);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src1, src2, src3, src4, src5, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        dst0 = __msa_srari_h(shf_vec2, 5);
        dst1 = __msa_srari_h(shf_vec5, 5);
        dst2 = __msa_srari_h(shf_vec8, 5);
        dst3 = __msa_srari_h(shf_vec11, 5);
        SAT_SH4_SH(dst0, dst1, dst2, dst3, 7);
        dst0 = __msa_pckod_h(dst2, dst0);
        dst1 = __msa_pckod_h(dst3, dst1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, dst2, dst3);
        dst0 = __msa_aver_s_h(dst2, dst0);
        dst1 = __msa_aver_s_h(dst3, dst1);
        out = PCKEV_XORI128_UB(dst0, dst1);
        ST_D2(out, 0, 1, dst, stride);
        dst += (2 * stride);

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
    }
}

void ff_put_h264_qpel4_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src21_r, src32_r, src43_r, src54_r, src65_r, src76_r;
    v16i8 src87_r, src10_l, src21_l, src32_l, src43_l, src54_l, src65_l;
    v16i8 src76_l, src87_l, filt0, filt1, filt2;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3, shf_vec7;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);
    v8i16 zeros = { 0 };

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    LD_SB4(src, stride, src5, src6, src7, src8);
    XORI_B4_128_SB(src5, src6, src7, src8);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);
    ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l, src65_l,
               src76_l, src87_l);
    vt_res0 = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
    hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
    hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);

    vt_res0 = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec6);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec7);
    hz_res2 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec6, minus5h, plus20h, hz_res2, hz_res2);
    hz_res3 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec7, minus5h, plus20h, hz_res3, hz_res3);

    SRARI_W2_SW(hz_res0, hz_res1, 10);
    SAT_SW2_SW(hz_res0, hz_res1, 7);
    SRARI_W2_SW(hz_res2, hz_res3, 10);
    SAT_SW2_SW(hz_res2, hz_res3, 7);

    dst0 = __msa_srari_h(shf_vec2, 5);
    dst1 = __msa_srari_h(shf_vec5, 5);
    dst2 = __msa_srari_h(shf_vec6, 5);
    dst3 = __msa_srari_h(shf_vec7, 5);

    SAT_SH2_SH(dst0, dst1, 7);
    SAT_SH2_SH(dst2, dst3, 7);
    ILVEV_H2_SH(dst0, zeros, dst1, zeros, dst0, dst1);
    ILVEV_H2_SH(dst2, zeros, dst3, zeros, dst2, dst3);

    hz_res0 = __msa_aver_s_w(hz_res0, (v4i32) dst0);
    hz_res1 = __msa_aver_s_w(hz_res1, (v4i32) dst1);
    hz_res2 = __msa_aver_s_w(hz_res2, (v4i32) dst2);
    hz_res3 = __msa_aver_s_w(hz_res3, (v4i32) dst3);

    PCKEV_H2_SH(hz_res1, hz_res0, hz_res3, hz_res2, dst0, dst2);
    out = PCKEV_XORI128_UB(dst0, dst2);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel4_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src21_r, src32_r, src43_r, src54_r, src65_r, src76_r;
    v16i8 src87_r, src10_l, src21_l, src32_l, src43_l, src54_l, src65_l;
    v16i8 src76_l, src87_l, filt0, filt1, filt2;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3, shf_vec7;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);
    v8i16 zeros = { 0 };

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    LD_SB4(src, stride, src5, src6, src7, src8);
    XORI_B4_128_SB(src5, src6, src7, src8);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);
    ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l, src65_l,
               src76_l, src87_l);

    vt_res0 = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
    hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
    hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);

    vt_res0 = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec6);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec7);
    hz_res2 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec6, minus5h, plus20h, hz_res2, hz_res2);
    hz_res3 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec7, minus5h, plus20h, hz_res3, hz_res3);

    SRARI_W2_SW(hz_res0, hz_res1, 10);
    SAT_SW2_SW(hz_res0, hz_res1, 7);
    SRARI_W2_SW(hz_res2, hz_res3, 10);
    SAT_SW2_SW(hz_res2, hz_res3, 7);

    dst0 = __msa_srari_h(shf_vec2, 5);
    dst1 = __msa_srari_h(shf_vec5, 5);
    dst2 = __msa_srari_h(shf_vec6, 5);
    dst3 = __msa_srari_h(shf_vec7, 5);

    SAT_SH2_SH(dst0, dst1, 7);
    SAT_SH2_SH(dst2, dst3, 7);

    dst0 = __msa_ilvod_h(zeros, dst0);
    dst1 = __msa_ilvod_h(zeros, dst1);
    dst2 = __msa_ilvod_h(zeros, dst2);
    dst3 = __msa_ilvod_h(zeros, dst3);

    hz_res0 = __msa_aver_s_w(hz_res0, (v4i32) dst0);
    hz_res1 = __msa_aver_s_w(hz_res1, (v4i32) dst1);
    hz_res2 = __msa_aver_s_w(hz_res2, (v4i32) dst2);
    hz_res3 = __msa_aver_s_w(hz_res3, (v4i32) dst3);

    PCKEV_H2_SH(hz_res1, hz_res0, hz_res3, hz_res2, dst0, dst2);
    out = PCKEV_XORI128_UB(dst0, dst2);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_put_h264_qpel16_mc22_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    const uint8_t *src_tmp = src - (2 * stride) - 2;
    uint8_t *dst_tmp = dst;
    uint32_t multiple8_cnt, loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, dst2, dst3;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out10_l, hz_out21_l;
    v8i16 hz_out32_l, hz_out43_l, hz_out54_l, hz_out65_l, hz_out76_l;
    v8i16 hz_out87_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        src = src_tmp;
        dst = dst_tmp;

        LD_SB5(src, stride, src0, src1, src2, src3, src4);
        XORI_B5_128_SB(src0, src1, src2, src3, src4);
        src += (5 * stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
        hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB4(src, stride, src0, src1, src2, src3);
            XORI_B4_128_SB(src0, src1, src2, src3);
            src += (4 * stride);

            hz_out5 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
            hz_out6 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
            hz_out7 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
            hz_out8 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);

            ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_r, hz_out21_r, hz_out32_r,
                       hz_out43_r);
            ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_l, hz_out21_l, hz_out32_l,
                       hz_out43_l);
            ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_r, hz_out65_r, hz_out76_r,
                       hz_out87_r);
            ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_l, hz_out65_l, hz_out76_l,
                       hz_out87_l);

            tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0,
                                  filt1, filt2);
            dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0,
                                  filt1, filt2);
            dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0,
                                  filt1, filt2);
            dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0,
                                  filt1, filt2);
            dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

            out0 = PCKEV_XORI128_UB(dst0, dst1);
            out1 = PCKEV_XORI128_UB(dst2, dst3);
            ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
            dst += (4 * stride);

            hz_out0 = hz_out4;
            hz_out1 = hz_out5;
            hz_out2 = hz_out6;
            hz_out3 = hz_out7;
            hz_out4 = hz_out8;
        }

        src_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_put_h264_qpel8_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, hz_out11, hz_out12;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out89_r, hz_out910_r;
    v8i16 hz_out1110_r, hz_out1211_r, dst0, dst1, dst2, dst3;
    v8i16 hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l, hz_out54_l;
    v8i16 hz_out65_l, hz_out76_l, hz_out87_l, hz_out89_l, hz_out910_l;
    v8i16 hz_out1110_l, hz_out1211_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    src -= ((2 * stride) + 2);
    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

    LD_SB4(src, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * stride);
    hz_out5 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out7 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);
    ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_l, hz_out65_l, hz_out76_l, hz_out87_l);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0, filt1,
                          filt2);
    dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0, filt1,
                          filt2);
    dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    out0 = PCKEV_XORI128_UB(dst0, dst1);
    out1 = PCKEV_XORI128_UB(dst2, dst3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    hz_out9 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out10 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out11 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out12 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_r, hz_out910_r, hz_out1110_r,
               hz_out1211_r);
    ILVL_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_l, hz_out910_l, hz_out1110_l,
               hz_out1211_l);
    tmp0 = AVC_DOT_SW3_SW(hz_out54_r, hz_out76_r, hz_out89_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out54_l, hz_out76_l, hz_out89_l, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out65_r, hz_out87_r, hz_out910_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out65_l, hz_out87_l, hz_out910_l, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out76_r, hz_out89_r, hz_out1110_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out76_l, hz_out89_l, hz_out1110_l, filt0, filt1,
                          filt2);
    dst2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out87_r, hz_out910_r, hz_out1211_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out87_l, hz_out910_l, hz_out1211_l, filt0, filt1,
                          filt2);
    dst3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    out0 = PCKEV_XORI128_UB(dst0, dst1);
    out1 = PCKEV_XORI128_UB(dst2, dst3);
    ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
}

void ff_put_h264_qpel4_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 res;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, filt0, filt1, filt2;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);
    hz_out0 = AVC_HORZ_FILTER_SH(src0, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);
    PCKOD_D2_SH(hz_out4, hz_out4, hz_out6, hz_out6, hz_out5, hz_out7);
    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    res = PCKEV_XORI128_UB(dst0, dst1);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc10_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t loop_cnt;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 out0, out1, out2, out3, src0, src1, src2, src3, src4, src5, src6;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, src7, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    mask3 = mask0 + 8;
    mask4 = mask1 + 8;
    mask5 = mask2 + 8;
    src -= 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src += stride;
        LD_SB2(src, 16, src2, src3);
        src += stride;
        LD_SB2(src, 16, src4, src5);
        src += stride;
        LD_SB2(src, 16, src6, src7);
        src += stride;

        LD_UB4(dst, stride, dst0, dst1, dst2, dst3);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src2, src2, src2, src3, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src2, src2, src2, src3, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src0, src0, src0, src1, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src2, src2, src2, src3, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res0, res1, res2, res3);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res0, res1, res2, res3);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res0, res1, res2, res3);
        VSHF_B2_SB(src4, src4, src4, src5, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src6, src6, src6, src7, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src4, src4, src4, src5, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src6, src6, src6, src7, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src4, src4, src4, src5, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src6, src6, src6, src7, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res4, res5, res6, res7);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res4, res5, res6, res7);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res4, res5, res6, res7);
        SLDI_B4_SB(src1, src0, src3, src2, src5, src4, src7, src6, 2,
                   src0, src2, src4, src6);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SRARI_H4_SH(res4, res5, res6, res7, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        SAT_SH4_SH(res4, res5, res6, res7, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, out0, out1);
        PCKEV_B2_SB(res5, res4, res7, res6, out2, out3);
        out0 = __msa_aver_s_b(out0, src0);
        out1 = __msa_aver_s_b(out1, src2);
        out2 = __msa_aver_s_b(out2, src4);
        out3 = __msa_aver_s_b(out3, src6);
        XORI_B4_128_SB(out0, out1, out2, out3);
        AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
        AVER_UB2_UB(out2, dst2, out3, dst3, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }
}

void ff_avg_h264_qpel16_mc30_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t loop_cnt;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 out0, out1, out2, out3, src0, src1, src2, src3, src4, src5, src6;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, src7, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    mask3 = mask0 + 8;
    mask4 = mask1 + 8;
    mask5 = mask2 + 8;
    src -= 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 16, src0, src1);
        src += stride;
        LD_SB2(src, 16, src2, src3);
        src += stride;
        LD_SB2(src, 16, src4, src5);
        src += stride;
        LD_SB2(src, 16, src6, src7);
        src += stride;

        LD_UB4(dst, stride, dst0, dst1, dst2, dst3);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src2, src2, src2, src3, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src2, src2, src2, src3, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src0, src0, src0, src1, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src2, src2, src2, src3, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res0, res1, res2, res3);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res0, res1, res2, res3);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res0, res1, res2, res3);
        VSHF_B2_SB(src4, src4, src4, src5, mask0, mask3, vec0, vec3);
        VSHF_B2_SB(src6, src6, src6, src7, mask0, mask3, vec6, vec9);
        VSHF_B2_SB(src4, src4, src4, src5, mask1, mask4, vec1, vec4);
        VSHF_B2_SB(src6, src6, src6, src7, mask1, mask4, vec7, vec10);
        VSHF_B2_SB(src4, src4, src4, src5, mask2, mask5, vec2, vec5);
        VSHF_B2_SB(src6, src6, src6, src7, mask2, mask5, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res4, res5, res6, res7);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res4, res5, res6, res7);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res4, res5, res6, res7);
        SLDI_B4_SB(src1, src0, src3, src2, src5, src4, src7, src6, 3,
                   src0, src2, src4, src6);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SRARI_H4_SH(res4, res5, res6, res7, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        SAT_SH4_SH(res4, res5, res6, res7, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, out0, out1);
        PCKEV_B2_SB(res5, res4, res7, res6, out2, out3);
        out0 = __msa_aver_s_b(out0, src0);
        out1 = __msa_aver_s_b(out1, src2);
        out2 = __msa_aver_s_b(out2, src4);
        out3 = __msa_aver_s_b(out3, src6);
        XORI_B4_128_SB(out0, out1, out2, out3);
        AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
        AVER_UB2_UB(out2, dst2, out3, dst3, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }
}

void ff_avg_h264_qpel8_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, dst2 = { 0 }, dst3 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 tmp0, tmp1, tmp2, tmp3, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB8(src - 2, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res4, res5, res6, res7);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 2,
               src0, src1, src2, src3);
    SLDI_B4_SB(src4, src4, src5, src5, src6, src6, src7, src7, 2,
               src4, src5, src6, src7);
    PCKEV_D2_SB(src1, src0, src3, src2, src0, src1);
    PCKEV_D2_SB(src5, src4, src7, src6, src4, src5);
    SRARI_H4_SH(res0, res1, res2, res3, 5);
    SRARI_H4_SH(res4, res5, res6, res7, 5);
    SAT_SH4_SH(res0, res1, res2, res3, 7);
    SAT_SH4_SH(res4, res5, res6, res7, 7);
    PCKEV_B2_SB(res1, res0, res3, res2, tmp0, tmp1);
    PCKEV_B2_SB(res5, res4, res7, res6, tmp2, tmp3);
    tmp0 = __msa_aver_s_b(tmp0, src0);
    tmp1 = __msa_aver_s_b(tmp1, src1);
    tmp2 = __msa_aver_s_b(tmp2, src4);
    tmp3 = __msa_aver_s_b(tmp3, src5);
    XORI_B4_128_SB(tmp0, tmp1, tmp2, tmp3);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst2);
    INSERT_D2_UB(tp2, tp3, dst3);
    AVER_UB2_UB(tmp0, dst0, tmp1, dst1, dst0, dst1);
    AVER_UB2_UB(tmp2, dst2, tmp3, dst3, dst2, dst3);
    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel8_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, dst2 = { 0 }, dst3 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 tmp0, tmp1, tmp2, tmp3, vec11;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB8(src - 2, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res4, res5, res6, res7);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 3,
               src0, src1, src2, src3);
    SLDI_B4_SB(src4, src4, src5, src5, src6, src6, src7, src7, 3,
               src4, src5, src6, src7);
    PCKEV_D2_SB(src1, src0, src3, src2, src0, src1);
    PCKEV_D2_SB(src5, src4, src7, src6, src4, src5);
    SRARI_H4_SH(res0, res1, res2, res3, 5);
    SRARI_H4_SH(res4, res5, res6, res7, 5);
    SAT_SH4_SH(res0, res1, res2, res3, 7);
    SAT_SH4_SH(res4, res5, res6, res7, 7);
    PCKEV_B2_SB(res1, res0, res3, res2, tmp0, tmp1);
    PCKEV_B2_SB(res5, res4, res7, res6, tmp2, tmp3);
    tmp0 = __msa_aver_s_b(tmp0, src0);
    tmp1 = __msa_aver_s_b(tmp1, src1);
    tmp2 = __msa_aver_s_b(tmp2, src4);
    tmp3 = __msa_aver_s_b(tmp3, src5);
    XORI_B4_128_SB(tmp0, tmp1, tmp2, tmp3);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst2);
    INSERT_D2_UB(tp2, tp3, dst3);
    AVER_UB2_UB(tmp0, dst0, tmp1, dst1, dst0, dst1);
    AVER_UB2_UB(tmp2, dst2, tmp3, dst3, dst2, dst3);
    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    v16u8 dst0 = { 0 };
    v16i8 src0, src1, src2, src3, res, vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 mask0, mask1, mask2;
    v8i16 out0, out1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src - 2, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, out0, out1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, out0, out1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, out0, out1);
    SRARI_H2_SH(out0, out1, 5);
    SAT_SH2_SH(out0, out1, 7);
    res = __msa_pckev_b((v16i8) out1, (v16i8) out0);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 2,
               src0, src1, src2, src3);
    src0 = (v16i8) __msa_insve_w((v4i32) src0, 1, (v4i32) src1);
    src1 = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    src0 = (v16i8) __msa_insve_d((v2i64) src0, 1, (v2i64) src1);
    res = __msa_aver_s_b(res, src0);
    res = (v16i8) __msa_xori_b((v16u8) res, 128);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);
    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel4_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    v16u8 dst0 = { 0 };
    v16i8 src0, src1, src2, src3, res, vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 mask0, mask1, mask2;
    v8i16 out0, out1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src - 2, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, out0, out1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, out0, out1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, out0, out1);
    SRARI_H2_SH(out0, out1, 5);
    SAT_SH2_SH(out0, out1, 7);
    res = __msa_pckev_b((v16i8) out1, (v16i8) out0);
    SLDI_B4_SB(src0, src0, src1, src1, src2, src2, src3, src3, 3,
               src0, src1, src2, src3);
    src0 = (v16i8) __msa_insve_w((v4i32) src0, 1, (v4i32) src1);
    src1 = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    src0 = (v16i8) __msa_insve_d((v2i64) src0, 1, (v2i64) src1);
    res = __msa_aver_s_b(res, src0);
    res = (v16i8) __msa_xori_b((v16u8) res, 128);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);
    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc20_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t loop_cnt;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 vec11;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    src -= 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 8, src0, src1);
        src += stride;
        LD_SB2(src, 8, src2, src3);
        src += stride;
        LD_SB2(src, 8, src4, src5);
        src += stride;
        LD_SB2(src, 8, src6, src7);
        src += stride;

        LD_UB4(dst, stride, dst0, dst1, dst2, dst3);
        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec3);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec6, vec9);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec1, vec4);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec7, vec10);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec2, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res0, res1, res2, res3);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res0, res1, res2, res3);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res0, res1, res2, res3);
        VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec3);
        VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec6, vec9);
        VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec1, vec4);
        VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec7, vec10);
        VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec2, vec5);
        VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec8, vec11);
        HADD_SB4_SH(vec0, vec3, vec6, vec9, res4, res5, res6, res7);
        DPADD_SB4_SH(vec1, vec4, vec7, vec10, minus5b, minus5b, minus5b,
                     minus5b, res4, res5, res6, res7);
        DPADD_SB4_SH(vec2, vec5, vec8, vec11, plus20b, plus20b, plus20b,
                     plus20b, res4, res5, res6, res7);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SRARI_H4_SH(res4, res5, res6, res7, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        SAT_SH4_SH(res4, res5, res6, res7, 7);
        PCKEV_B4_SB(res1, res0, res3, res2, res5, res4, res7, res6, vec0, vec1,
                    vec2, vec3);
        XORI_B4_128_SB(vec0, vec1, vec2, vec3);
        AVER_UB2_UB(vec0, dst0, vec1, dst1, dst0, dst1);
        AVER_UB2_UB(vec2, dst2, vec3, dst3, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);
    }
}

void ff_avg_h264_qpel8_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 out0, out1, out2 = { 0 }, out3 = { 0 };
    v16u8 out4, out5, out6 = { 0 }, out7 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 vec11;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    LD_SB8(src - 2, stride, src0, src1, src2, src3, src4, src5, src6, src7);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res0, res1, res2, res3);
    VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
    VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
    HADD_SB4_SH(vec0, vec1, vec2, vec3, res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                 res4, res5, res6, res7);
    VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec8, vec9);
    VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec10, vec11);
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b, plus20b,
                 res4, res5, res6, res7);
    SRARI_H4_SH(res0, res1, res2, res3, 5);
    SRARI_H4_SH(res4, res5, res6, res7, 5);
    SAT_SH4_SH(res0, res1, res2, res3, 7);
    SAT_SH4_SH(res4, res5, res6, res7, 7);
    out0 = PCKEV_XORI128_UB(res0, res1);
    out1 = PCKEV_XORI128_UB(res2, res3);
    out4 = PCKEV_XORI128_UB(res4, res5);
    out5 = PCKEV_XORI128_UB(res6, res7);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, out2);
    INSERT_D2_UB(tp2, tp3, out3);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, out6);
    INSERT_D2_UB(tp2, tp3, out7);
    AVER_UB2_UB(out0, out2, out1, out3, out0, out1);
    AVER_UB2_UB(out4, out6, out5, out7, out4, out5);
    ST_D8(out0, out1, out4, out5, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    v16u8 res, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 mask0, mask1, mask2;
    v8i16 res0, res1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src - 2, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, res0, res1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, res0, res1);
    SRARI_H2_SH(res0, res1, 5);
    SAT_SH2_SH(res0, res1, 7);
    res = PCKEV_XORI128_UB(res0, res1);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    res = __msa_aver_u_b(res, dst0);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc01_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res0, res1, res2, res3, dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, stride, src5, src6, src7, src8);
        src += (4 * stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src2);
        res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src3);
        res2 = (v16u8) __msa_aver_s_b((v16i8) res2, src4);
        res3 = (v16u8) __msa_aver_s_b((v16i8) res3, src5);
        LD_UB4(dst, stride, dst0, dst1, dst2, dst3);
        XORI_B4_128_UB(res0, res1, res2, res3);
        AVER_UB2_UB(res0, dst0, res1, dst1, dst0, dst1);
        AVER_UB2_UB(res2, dst2, res3, dst3, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src10_l = src54_l;
        src32_l = src76_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src2 = src6;
        src3 = src7;
        src4 = src8;
    }
}

void ff_avg_h264_qpel16_mc03_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res0, res1, res2, res3, dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, stride, src5, src6, src7, src8);
        src += (4 * stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src3);
        res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src4);
        res2 = (v16u8) __msa_aver_s_b((v16i8) res2, src5);
        res3 = (v16u8) __msa_aver_s_b((v16i8) res3, src6);
        LD_UB4(dst, stride, dst0, dst1, dst2, dst3);
        XORI_B4_128_UB(res0, res1, res2, res3);
        AVER_UB2_UB(res0, dst0, res1, dst1, dst0, dst1);
        AVER_UB2_UB(res2, dst2, res3, dst3, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, stride);
        dst += (4 * stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src10_l = src54_l;
        src32_l = src76_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src3 = src7;
        src4 = src8;
    }
}

void ff_avg_h264_qpel8_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, dst2 = { 0 }, dst3 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10, src11, src12;
    v16i8 src13, src14, tmp0, tmp1, tmp2, tmp3, src109_r;
    v16i8 src10_r, src32_r, src76_r, src98_r, src21_r, src43_r, src87_r;
    v16i8 filt0, filt1, filt2, out0, out1, out2, out3;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r, out6_r, out7_r;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    LD_SB8(src, stride, src7, src8, src9, src10, src11, src12, src13, src14);
    XORI_B8_128_SB(src7, src8, src9, src10, src11, src12, src13, src14);
    ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9, src76_r,
               src87_r, src98_r, src109_r);
    out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
    out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
    out2_r = AVC_DOT_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
    out3_r = AVC_DOT_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
    PCKEV_D2_SB(src3, src2, src7, src4, tmp0, tmp1);
    ILVR_B4_SB(src11, src10, src12, src11, src13, src12, src14, src13, src10_r,
               src21_r, src32_r, src43_r);
    out4_r = AVC_DOT_SH3_SH(src76_r, src98_r, src10_r, filt0, filt1, filt2);
    out5_r = AVC_DOT_SH3_SH(src87_r, src109_r, src21_r, filt0, filt1, filt2);
    out6_r = AVC_DOT_SH3_SH(src98_r, src10_r, src32_r, filt0, filt1, filt2);
    out7_r = AVC_DOT_SH3_SH(src109_r, src21_r, src43_r, filt0, filt1, filt2);
    PCKEV_D2_SB(src9, src8, src11, src10, tmp2, tmp3);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
    SRARI_H4_SH(out4_r, out5_r, out6_r, out7_r, 5);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH4_SH(out4_r, out5_r, out6_r, out7_r, 7);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst2);
    INSERT_D2_UB(tp2, tp3, dst3);

    PCKEV_B2_SB(out1_r, out0_r, out3_r, out2_r, out0, out1);
    PCKEV_B2_SB(out5_r, out4_r, out7_r, out6_r, out2, out3);
    out0 = __msa_aver_s_b(out0, tmp0);
    out1 = __msa_aver_s_b(out1, tmp1);
    out2 = __msa_aver_s_b(out2, tmp2);
    out3 = __msa_aver_s_b(out3, tmp3);
    XORI_B4_128_SB(out0, out1, out2, out3);
    AVER_UB4_UB(out0, dst0, out1, dst1, out2, dst2, out3, dst3, dst0, dst1,
                dst2, dst3);
    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel8_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, dst2 = { 0 }, dst3 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10, src11, src12;
    v16i8 src13, src14, tmp0, tmp1, tmp2, tmp3, src109_r;
    v16i8 src10_r, src32_r, src76_r, src98_r, src21_r, src43_r, src87_r;
    v16i8 filt0, filt1, filt2, out0, out1, out2, out3;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r, out6_r, out7_r;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    LD_SB8(src, stride, src7, src8, src9, src10, src11, src12, src13, src14);
    XORI_B8_128_SB(src7, src8, src9, src10, src11, src12, src13, src14);
    ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9, src76_r,
               src87_r, src98_r, src109_r);
    out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
    out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
    out2_r = AVC_DOT_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
    out3_r = AVC_DOT_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
    PCKEV_D2_SB(src4, src3, src8, src7, tmp0, tmp1);
    ILVR_B4_SB(src11, src10, src12, src11, src13, src12, src14, src13, src10_r,
               src21_r, src32_r, src43_r);
    out4_r = AVC_DOT_SH3_SH(src76_r, src98_r, src10_r, filt0, filt1, filt2);
    out5_r = AVC_DOT_SH3_SH(src87_r, src109_r, src21_r, filt0, filt1, filt2);
    out6_r = AVC_DOT_SH3_SH(src98_r, src10_r, src32_r, filt0, filt1, filt2);
    out7_r = AVC_DOT_SH3_SH(src109_r, src21_r, src43_r, filt0, filt1, filt2);
    PCKEV_D2_SB(src10, src9, src12, src11, tmp2, tmp3);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
    SRARI_H4_SH(out4_r, out5_r, out6_r, out7_r, 5);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH4_SH(out4_r, out5_r, out6_r, out7_r, 7);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst2);
    INSERT_D2_UB(tp2, tp3, dst3);

    PCKEV_B2_SB(out1_r, out0_r, out3_r, out2_r, out0, out1);
    PCKEV_B2_SB(out5_r, out4_r, out7_r, out6_r, out2, out3);
    out0 = __msa_aver_s_b(out0, tmp0);
    out1 = __msa_aver_s_b(out1, tmp1);
    out2 = __msa_aver_s_b(out2, tmp2);
    out3 = __msa_aver_s_b(out3, tmp3);
    XORI_B4_128_SB(out0, out1, out2, out3);
    AVER_UB4_UB(out0, dst0, out1, dst1, out2, dst2, out3, dst3, dst0, dst1,
                dst2, dst3);
    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v8i16 out10, out32;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);
    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    src32_r = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    src54_r = (v16i8) __msa_insve_w((v4i32) src4, 1, (v4i32) src5);
    src32_r = (v16i8) __msa_insve_d((v2i64) src32_r, 1, (v2i64) src54_r);
    out10 = AVC_DOT_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = AVC_DOT_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    res = PCKEV_XORI128_UB(out10, out32);
    res = __msa_aver_u_b(res, (v16u8) src32_r);
    dst0 = __msa_aver_u_b(res, dst0);
    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel4_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v8i16 out10, out32;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    out10 = AVC_DOT_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = AVC_DOT_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    res = PCKEV_XORI128_UB(out10, out32);
    src32_r = (v16i8) __msa_insve_w((v4i32) src3, 1, (v4i32) src4);
    src54_r = (v16i8) __msa_insve_w((v4i32) src5, 1, (v4i32) src6);
    src32_r = (v16i8) __msa_insve_d((v2i64) src32_r, 1, (v2i64) src54_r);
    res = __msa_aver_u_b(res, (v16u8) src32_r);
    dst0 = __msa_aver_u_b(res, dst0);
    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc11_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src - 2,
                                           src - (stride * 2),
                                           dst, stride);
}

void ff_avg_h264_qpel16_mc31_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src - 2,
                                           src - (stride * 2) +
                                           sizeof(uint8_t),
                                           dst, stride);
}

void ff_avg_h264_qpel16_mc13_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src + stride - 2,
                                           src - (stride * 2),
                                           dst, stride);
}

void ff_avg_h264_qpel16_mc33_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src + stride - 2,
                                           src - (stride * 2) +
                                           sizeof(uint8_t),
                                           dst, stride);
}

void ff_avg_h264_qpel8_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src - 2,
                                         src - (stride * 2),
                                         dst, stride);
}

void ff_avg_h264_qpel8_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), dst, stride);
}

void ff_avg_h264_qpel8_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src + stride - 2,
                                         src - (stride * 2),
                                         dst, stride);
}

void ff_avg_h264_qpel8_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src + stride - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), dst, stride);
}


void ff_avg_h264_qpel4_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src - 2,
                                         src - (stride * 2),
                                         dst, stride);
}

void ff_avg_h264_qpel4_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), dst, stride);
}

void ff_avg_h264_qpel4_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src + stride - 2,
                                         src - (stride * 2),
                                         dst, stride);
}

void ff_avg_h264_qpel4_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src + stride - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), dst, stride);
}

void ff_avg_h264_qpel16_mc21_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    uint8_t *dst_tmp = dst;
    const uint8_t *src_tmp = src - (2 * stride) - 2;
    uint32_t multiple8_cnt, loop_cnt;
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1, dst0 = { 0 }, dst1 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, mask0, mask1;
    v16i8 mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out10_l, hz_out21_l;
    v8i16 hz_out32_l, hz_out43_l, hz_out54_l, hz_out65_l, hz_out76_l;
    v8i16 hz_out87_l, filt0, filt1, filt2;
    v4i32 tmp0_w, tmp1_w;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        dst = dst_tmp;
        src = src_tmp;

        LD_SB5(src, stride, src0, src1, src2, src3, src4);
        XORI_B5_128_SB(src0, src1, src2, src3, src4);
        src += (5 * stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
        hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB2(src, stride, src5, src6);
            src += (2 * stride);

            XORI_B2_128_SB(src5, src6);
            hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
            hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
            ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_r, hz_out21_r, hz_out32_r,
                       hz_out43_r);
            ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_l, hz_out21_l, hz_out32_l,
                       hz_out43_l);
            ILVR_H2_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out54_r,
                       hz_out65_r);
            ILVL_H2_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out54_l,
                       hz_out65_l);
            tmp0_w = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0,
                                    filt1, filt2);
            tmp0 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
            tmp0_w = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0,
                                    filt1, filt2);
            tmp2 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

            tmp1 = __msa_srari_h(hz_out2, 5);
            tmp3 = __msa_srari_h(hz_out3, 5);
            SAT_SH2_SH(tmp1, tmp3, 7);

            tmp0 = __msa_aver_s_h(tmp0, tmp1);
            tmp1 = __msa_aver_s_h(tmp2, tmp3);

            LD2(dst, stride, tp0, tp1);
            INSERT_D2_UB(tp0, tp1, dst0);

            out0 = PCKEV_XORI128_UB(tmp0, tmp1);
            dst0 = __msa_aver_u_b(out0, dst0);
            ST_D2(dst0, 0, 1, dst, stride);
            dst += (2 * stride);

            LD_SB2(src, stride, src7, src8);
            src += (2 * stride);

            XORI_B2_128_SB(src7, src8);
            hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
            hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
            ILVR_H2_SH(hz_out7, hz_out6, hz_out8, hz_out7, hz_out76_r,
                       hz_out87_r);
            ILVL_H2_SH(hz_out7, hz_out6, hz_out8, hz_out7, hz_out76_l,
                       hz_out87_l);
            tmp0_w = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0,
                                    filt1, filt2);
            tmp4 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
            tmp0_w = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0,
                                    filt1, filt2);
            tmp6 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

            tmp5 = __msa_srari_h(hz_out4, 5);
            tmp7 = __msa_srari_h(hz_out5, 5);
            SAT_SH2_SH(tmp5, tmp7, 7);

            tmp2 = __msa_aver_s_h(tmp4, tmp5);
            tmp3 = __msa_aver_s_h(tmp6, tmp7);

            LD2(dst, stride, tp2, tp3);
            INSERT_D2_UB(tp2, tp3, dst1);

            out1 = PCKEV_XORI128_UB(tmp2, tmp3);
            dst1 = __msa_aver_u_b(out1, dst1);
            ST_D2(dst1, 0, 1, dst, stride);
            dst += (2 * stride);

            hz_out0 = hz_out4;
            hz_out1 = hz_out5;
            hz_out2 = hz_out6;
            hz_out3 = hz_out7;
            hz_out4 = hz_out8;
        }

        src_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_avg_h264_qpel16_mc23_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    uint8_t *dst_tmp = dst;
    const uint8_t *src_tmp = src - (2 * stride) - 2;
    uint32_t multiple8_cnt, loop_cnt;
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 out0, out1, dst0 = { 0 }, dst1 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, mask0, mask1;
    v16i8 mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out10_l, hz_out21_l;
    v8i16 hz_out32_l, hz_out43_l, hz_out54_l, hz_out65_l, hz_out76_l;
    v8i16 hz_out87_l, filt0, filt1, filt2;
    v4i32 tmp0_w, tmp1_w;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        dst = dst_tmp;
        src = src_tmp;

        LD_SB5(src, stride, src0, src1, src2, src3, src4);
        XORI_B5_128_SB(src0, src1, src2, src3, src4);
        src += (5 * stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
        hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB2(src, stride, src5, src6);
            src += (2 * stride);

            XORI_B2_128_SB(src5, src6);
            hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
            hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
            ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_r, hz_out21_r, hz_out32_r,
                       hz_out43_r);
            ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_l, hz_out21_l, hz_out32_l,
                       hz_out43_l);
            ILVR_H2_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out54_r, hz_out65_r);
            ILVL_H2_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out54_l, hz_out65_l);

            tmp0_w = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0,
                                    filt1, filt2);
            tmp0 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
            tmp0_w = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0,
                                    filt1, filt2);
            tmp2 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

            tmp1 = __msa_srari_h(hz_out3, 5);
            tmp3 = __msa_srari_h(hz_out4, 5);
            SAT_SH2_SH(tmp1, tmp3, 7);

            tmp0 = __msa_aver_s_h(tmp0, tmp1);
            tmp1 = __msa_aver_s_h(tmp2, tmp3);

            LD2(dst, stride, tp0, tp1);
            INSERT_D2_UB(tp0, tp1, dst0);
            out0 = PCKEV_XORI128_UB(tmp0, tmp1);
            dst0 = __msa_aver_u_b(out0, dst0);
            ST_D2(dst0, 0, 1, dst, stride);
            dst += (2 * stride);

            LD_SB2(src, stride, src7, src8);
            src += (2 * stride);

            XORI_B2_128_SB(src7, src8);
            hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
            hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
            ILVR_H2_SH(hz_out7, hz_out6, hz_out8, hz_out7, hz_out76_r,
                       hz_out87_r);
            ILVL_H2_SH(hz_out7, hz_out6, hz_out8, hz_out7, hz_out76_l,
                       hz_out87_l);
            tmp0_w = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0,
                                    filt1, filt2);
            tmp4 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
            tmp0_w = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0,
                                    filt1, filt2);
            tmp1_w = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0,
                                    filt1, filt2);
            tmp6 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

            tmp5 = __msa_srari_h(hz_out5, 5);
            tmp7 = __msa_srari_h(hz_out6, 5);
            SAT_SH2_SH(tmp5, tmp7, 7);

            tmp2 = __msa_aver_s_h(tmp4, tmp5);
            tmp3 = __msa_aver_s_h(tmp6, tmp7);

            LD2(dst, stride, tp2, tp3);
            INSERT_D2_UB(tp2, tp3, dst1);
            out1 = PCKEV_XORI128_UB(tmp2, tmp3);
            dst1 = __msa_aver_u_b(out1, dst1);
            ST_D2(dst1, 0, 1, dst, stride);
            dst += (2 * stride);

            hz_out0 = hz_out4;
            hz_out1 = hz_out5;
            hz_out2 = hz_out6;
            hz_out3 = hz_out7;
            hz_out4 = hz_out8;
        }

        src_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_avg_h264_qpel8_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, hz_out11, hz_out12;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out89_r, hz_out910_r;
    v8i16 hz_out1110_r, hz_out1211_r, tmp0, tmp1, tmp2, tmp3;
    v8i16 hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l, hz_out54_l;
    v8i16 hz_out65_l, hz_out76_l, hz_out87_l, hz_out89_l, hz_out910_l;
    v8i16 hz_out1110_l, hz_out1211_l, filt0, filt1, filt2;
    v4i32 tmp0_w, tmp1_w;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

    LD_SB4(src, stride, src5, src6, src7, src8);
    src += (4 * stride);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
    hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);
    ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_l, hz_out65_l, hz_out76_l, hz_out87_l);

    tmp0_w = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0, filt1,
                            filt2);
    tmp0 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0, filt1,
                            filt2);
    tmp1 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0, filt1,
                            filt2);
    tmp2 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0, filt1,
                            filt2);
    tmp3 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

    SRARI_H4_SH(hz_out2, hz_out3, hz_out4, hz_out5, 5);
    SAT_SH4_SH(hz_out2, hz_out3, hz_out4, hz_out5, 7);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);

    tmp0 = __msa_aver_s_h(tmp0, hz_out2);
    tmp1 = __msa_aver_s_h(tmp1, hz_out3);
    tmp2 = __msa_aver_s_h(tmp2, hz_out4);
    tmp3 = __msa_aver_s_h(tmp3, hz_out5);

    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src, stride, src9, src10, src11, src12);
    XORI_B4_128_SB(src9, src10, src11, src12);
    hz_out9 = AVC_HORZ_FILTER_SH(src9, src9, mask0, mask1, mask2);
    hz_out10 = AVC_HORZ_FILTER_SH(src10, src10, mask0, mask1, mask2);
    hz_out11 = AVC_HORZ_FILTER_SH(src11, src11, mask0, mask1, mask2);
    hz_out12 = AVC_HORZ_FILTER_SH(src12, src12, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_r, hz_out910_r, hz_out1110_r,
               hz_out1211_r);
    ILVL_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_l, hz_out910_l, hz_out1110_l,
               hz_out1211_l);
    tmp0_w = AVC_DOT_SW3_SW(hz_out54_r, hz_out76_r, hz_out89_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out54_l, hz_out76_l, hz_out89_l, filt0, filt1,
                            filt2);
    tmp0 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out65_r, hz_out87_r, hz_out910_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out65_l, hz_out87_l, hz_out910_l, filt0, filt1,
                            filt2);
    tmp1 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out76_r, hz_out89_r, hz_out1110_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out76_l, hz_out89_l, hz_out1110_l, filt0, filt1,
                            filt2);
    tmp2 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out87_r, hz_out910_r, hz_out1211_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out87_l, hz_out910_l, hz_out1211_l, filt0, filt1,
                            filt2);
    tmp3 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

    SRARI_H4_SH(hz_out6, hz_out7, hz_out8, hz_out9, 5);
    SAT_SH4_SH(hz_out6, hz_out7, hz_out8, hz_out9, 7);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);

    tmp0 = __msa_aver_s_h(tmp0, hz_out6);
    tmp1 = __msa_aver_s_h(tmp1, hz_out7);
    tmp2 = __msa_aver_s_h(tmp2, hz_out8);
    tmp3 = __msa_aver_s_h(tmp3, hz_out9);

    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel8_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, hz_out11, hz_out12;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out89_r, hz_out910_r;
    v8i16 hz_out1110_r, hz_out1211_r, tmp0, tmp1, tmp2, tmp3;
    v8i16 hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l, hz_out54_l;
    v8i16 hz_out65_l, hz_out76_l, hz_out87_l, hz_out89_l, hz_out910_l;
    v8i16 hz_out1110_l, hz_out1211_l, filt0, filt1, filt2;
    v4i32 tmp0_w, tmp1_w;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

    LD_SB4(src, stride, src5, src6, src7, src8);
    src += (4 * stride);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out5 = AVC_HORZ_FILTER_SH(src5, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src6, mask0, mask1, mask2);
    hz_out7 = AVC_HORZ_FILTER_SH(src7, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);
    ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_l, hz_out65_l, hz_out76_l, hz_out87_l);

    tmp0_w = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0, filt1,
                            filt2);
    tmp0 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0, filt1,
                            filt2);
    tmp1 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0, filt1,
                            filt2);
    tmp2 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0, filt1,
                            filt2);
    tmp3 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

    SRARI_H4_SH(hz_out3, hz_out4, hz_out5, hz_out6, 5);
    SAT_SH4_SH(hz_out3, hz_out4, hz_out5, hz_out6, 7);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);

    tmp0 = __msa_aver_s_h(tmp0, hz_out3);
    tmp1 = __msa_aver_s_h(tmp1, hz_out4);
    tmp2 = __msa_aver_s_h(tmp2, hz_out5);
    tmp3 = __msa_aver_s_h(tmp3, hz_out6);

    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src, stride, src9, src10, src11, src12);
    XORI_B4_128_SB(src9, src10, src11, src12);
    hz_out9 = AVC_HORZ_FILTER_SH(src9, src9, mask0, mask1, mask2);
    hz_out10 = AVC_HORZ_FILTER_SH(src10, src10, mask0, mask1, mask2);
    hz_out11 = AVC_HORZ_FILTER_SH(src11, src11, mask0, mask1, mask2);
    hz_out12 = AVC_HORZ_FILTER_SH(src12, src12, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_r, hz_out910_r, hz_out1110_r,
               hz_out1211_r);
    ILVL_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_l, hz_out910_l, hz_out1110_l,
               hz_out1211_l);
    tmp0_w = AVC_DOT_SW3_SW(hz_out54_r, hz_out76_r, hz_out89_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out54_l, hz_out76_l, hz_out89_l, filt0, filt1,
                            filt2);
    tmp0 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out65_r, hz_out87_r, hz_out910_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out65_l, hz_out87_l, hz_out910_l, filt0, filt1,
                            filt2);
    tmp1 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out76_r, hz_out89_r, hz_out1110_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out76_l, hz_out89_l, hz_out1110_l, filt0, filt1,
                            filt2);
    tmp2 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);
    tmp0_w = AVC_DOT_SW3_SW(hz_out87_r, hz_out910_r, hz_out1211_r, filt0, filt1,
                            filt2);
    tmp1_w = AVC_DOT_SW3_SW(hz_out87_l, hz_out910_l, hz_out1211_l, filt0, filt1,
                            filt2);
    tmp3 = __msa_pckev_h((v8i16) tmp1_w, (v8i16) tmp0_w);

    SRARI_H4_SH(hz_out7, hz_out8, hz_out9, hz_out10, 5);
    SAT_SH4_SH(hz_out7, hz_out8, hz_out9, hz_out10, 7);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);

    tmp0 = __msa_aver_s_h(tmp0, hz_out7);
    tmp1 = __msa_aver_s_h(tmp1, hz_out8);
    tmp2 = __msa_aver_s_h(tmp2, hz_out9);
    tmp3 = __msa_aver_s_h(tmp3, hz_out10);

    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    v16u8 res, out = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, filt0, filt1, filt2;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);
    PCKOD_D2_SH(hz_out4, hz_out4, hz_out6, hz_out6, hz_out5, hz_out7);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    SRARI_H2_SH(hz_out2, hz_out4, 5);
    SAT_SH2_SH(hz_out2, hz_out4, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out2);
    dst1 = __msa_aver_s_h(dst1, hz_out4);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, out);
    res = PCKEV_XORI128_UB(dst0, dst1);
    res = __msa_aver_u_b(res, out);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel4_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    uint32_t tp0, tp1, tp2, tp3;
    v16u8 res, out = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, dst0, dst1, filt0, filt1, filt2;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);
    PCKOD_D2_SH(hz_out4, hz_out4, hz_out6, hz_out6, hz_out5, hz_out7);

    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    dst0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    dst1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

    PCKEV_D2_SH(hz_out4, hz_out3, hz_out6, hz_out5, hz_out0, hz_out1);
    SRARI_H2_SH(hz_out0, hz_out1, 5);
    SAT_SH2_SH(hz_out0, hz_out1, 7);

    dst0 = __msa_aver_s_h(dst0, hz_out0);
    dst1 = __msa_aver_s_h(dst1, hz_out1);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, out);
    res = PCKEV_XORI128_UB(dst0, dst1);
    res = __msa_aver_u_b(res, out);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc02_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res0, res1, res2, res3, dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l, filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);
    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, stride, src5, src6, src7, src8);
        src += (4 * stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r,
                   src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l,
                   src65_l, src76_l, src87_l);
        out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        LD_UB4(dst, stride, dst0, dst1, dst2, dst3);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        XORI_B4_128_UB(res0, res1, res2, res3);
        AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
        AVER_UB2_UB(res2, dst2, res3, dst3, res2, res3);
        ST_UB4(res0, res1, res2, res3, dst, stride);
        dst += (4 * stride);

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

void ff_avg_h264_qpel8_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint64_t tp0, tp1, tp2, tp3;
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 dst0 = { 0 }, dst1 = { 0 }, dst2 = { 0 }, dst3 = { 0 };
    v16u8 out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10, src109_r;
    v16i8 src10_r, src32_r, src76_r, src98_r, src21_r, src43_r, src87_r;
    v16i8 filt0, filt1, filt2;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r, out6_r, out7_r;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);

    LD_SB4(src, stride, src7, src8, src9, src10);
    src += (4 * stride);
    XORI_B4_128_SB(src7, src8, src9, src10);
    ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9, src76_r,
               src87_r, src98_r, src109_r);
    out0_r = AVC_DOT_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
    out1_r = AVC_DOT_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
    out2_r = AVC_DOT_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
    out3_r = AVC_DOT_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);

    LD_SB4(src, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    ILVR_B4_SB(src0, src10, src1, src0, src2, src1, src3, src2, src10_r,
               src21_r, src32_r, src43_r);
    out4_r = AVC_DOT_SH3_SH(src76_r, src98_r, src10_r, filt0, filt1, filt2);
    out5_r = AVC_DOT_SH3_SH(src87_r, src109_r, src21_r, filt0, filt1, filt2);
    out6_r = AVC_DOT_SH3_SH(src98_r, src10_r, src32_r, filt0, filt1, filt2);
    out7_r = AVC_DOT_SH3_SH(src109_r, src21_r, src43_r, filt0, filt1, filt2);

    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    LD4(dst + 4 * stride, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst2);
    INSERT_D2_UB(tp2, tp3, dst3);

    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
    SRARI_H4_SH(out4_r, out5_r, out6_r, out7_r, 5);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH4_SH(out4_r, out5_r, out6_r, out7_r, 7);
    out0 = PCKEV_XORI128_UB(out0_r, out1_r);
    out1 = PCKEV_XORI128_UB(out2_r, out3_r);
    out2 = PCKEV_XORI128_UB(out4_r, out5_r);
    out3 = PCKEV_XORI128_UB(out6_r, out7_r);
    AVER_UB4_UB(out0, dst0, out1, dst1, out2, dst2, out3, dst3, dst0, dst1,
                dst2, dst3);
    ST_D8(dst0, dst1, dst2, dst3, 0, 1, 0, 1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 res, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776, filt0, filt1, filt2;
    v8i16 out10, out32;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= (stride * 2);
    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    out10 = AVC_DOT_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = AVC_DOT_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    res = PCKEV_XORI128_UB(out10, out32);
    dst0 = __msa_aver_u_b(res, dst0);
    ST_W4(dst0, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc12_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t row;
    v16u8 out, dst0;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, tmp0, tmp1, tmp2, tmp3, mask3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    LD_SB5(src + 8, stride, src7, src8, src9, src10, src11);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src7, src8, src9, src10, src11);

    for (row = 16; row--;) {
        LD_SB2(src, 8, src5, src6);
        src += stride;
        XORI_B2_128_SB(src5, src6);
        dst0 = LD_UB(dst);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src7, src8, src9, src10, src11, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        tmp0 = __msa_srari_h(shf_vec2, 5);
        tmp1 = __msa_srari_h(shf_vec5, 5);
        tmp2 = __msa_srari_h(shf_vec8, 5);
        tmp3 = __msa_srari_h(shf_vec11, 5);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_H2_SH(tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, tmp2, tmp3);
        tmp0 = __msa_aver_s_h(tmp2, tmp0);
        tmp1 = __msa_aver_s_h(tmp3, tmp1);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        out = __msa_aver_u_b(out, dst0);
        ST_UB(out, dst);
        dst += stride;

        src0 = src1;
        src1 = src2;
        src2 = src3;
        src3 = src4;
        src4 = src5;
        src7 = src8;
        src8 = src9;
        src9 = src10;
        src10 = src11;
        src11 = src6;
    }
}

void ff_avg_h264_qpel16_mc32_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint32_t row;
    v16u8 out, dst0;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, tmp0, tmp1, tmp2, tmp3, mask3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    LD_SB5(src + 8, stride, src7, src8, src9, src10, src11);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src7, src8, src9, src10, src11);

    for (row = 16; row--;) {
        LD_SB2(src, 8, src5, src6);
        src += stride;
        XORI_B2_128_SB(src5, src6);
        dst0 = LD_UB(dst);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src7, src8, src9, src10, src11, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        tmp0 = __msa_srari_h(shf_vec2, 5);
        tmp1 = __msa_srari_h(shf_vec5, 5);
        tmp2 = __msa_srari_h(shf_vec8, 5);
        tmp3 = __msa_srari_h(shf_vec11, 5);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        tmp0 = __msa_pckod_h(tmp2, tmp0);
        tmp1 = __msa_pckod_h(tmp3, tmp1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, tmp2, tmp3);
        tmp0 = __msa_aver_s_h(tmp2, tmp0);
        tmp1 = __msa_aver_s_h(tmp3, tmp1);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        out = __msa_aver_u_b(out, dst0);
        ST_UB(out, dst);
        dst += stride;

        src0 = src1;
        src1 = src2;
        src2 = src3;
        src3 = src4;
        src4 = src5;
        src7 = src8;
        src8 = src9;
        src9 = src10;
        src10 = src11;
        src11 = src6;
    }
}

void ff_avg_h264_qpel8_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t row;
    uint64_t tp0, tp1;
    v16u8 out, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, tmp0, tmp1, tmp2, tmp3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11;
    v8i16 mask3, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    for (row = 4; row--;) {
        LD_SB2(src, stride, src5, src6);
        src += (2 * stride);
        XORI_B2_128_SB(src5, src6);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src1, src2, src3, src4, src5, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        tmp0 = __msa_srari_h(shf_vec2, 5);
        tmp1 = __msa_srari_h(shf_vec5, 5);
        tmp2 = __msa_srari_h(shf_vec8, 5);
        tmp3 = __msa_srari_h(shf_vec11, 5);
        LD2(dst, stride, tp0, tp1);
        INSERT_D2_UB(tp0, tp1, dst0);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_H2_SH(tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, tmp2, tmp3);
        tmp0 = __msa_aver_s_h(tmp2, tmp0);
        tmp1 = __msa_aver_s_h(tmp3, tmp1);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        out = __msa_aver_u_b(out, dst0);
        ST_D2(out, 0, 1, dst, stride);
        dst += (2 * stride);

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
    }
}

void ff_avg_h264_qpel8_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t row;
    uint64_t tp0, tp1;
    v16u8 out, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, tmp0, tmp1, tmp2, tmp3;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v8i16 shf_vec7, shf_vec8, shf_vec9, shf_vec10, shf_vec11;
    v8i16 mask3, mask4, mask5;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);

    mask3 = mask0 + 4;
    mask4 = mask1 + 4;
    mask5 = mask2 + 4;

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    for (row = 4; row--;) {
        LD_SB2(src, stride, src5, src6);
        src += (2 * stride);
        XORI_B2_128_SB(src5, src6);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src1, src2, src3, src4, src5, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
                   mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
                   mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask3,
                   mask4, mask5, shf_vec6, shf_vec7, shf_vec8);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask3,
                   mask4, mask5, shf_vec9, shf_vec10, shf_vec11);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        hz_res2 = __msa_hadd_s_w(shf_vec6, shf_vec6);
        hz_res3 = __msa_hadd_s_w(shf_vec9, shf_vec9);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);
        DPADD_SH2_SW(shf_vec7, shf_vec8, minus5h, plus20h, hz_res2, hz_res2);
        DPADD_SH2_SW(shf_vec10, shf_vec11, minus5h, plus20h, hz_res3, hz_res3);
        SRARI_W4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 10);
        SAT_SW4_SW(hz_res0, hz_res1, hz_res2, hz_res3, 7);
        tmp0 = __msa_srari_h(shf_vec2, 5);
        tmp1 = __msa_srari_h(shf_vec5, 5);
        tmp2 = __msa_srari_h(shf_vec8, 5);
        tmp3 = __msa_srari_h(shf_vec11, 5);
        LD2(dst, stride, tp0, tp1);
        INSERT_D2_UB(tp0, tp1, dst0);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        tmp0 = __msa_pckod_h(tmp2, tmp0);
        tmp1 = __msa_pckod_h(tmp3, tmp1);
        PCKEV_H2_SH(hz_res2, hz_res0, hz_res3, hz_res1, tmp2, tmp3);
        tmp0 = __msa_aver_s_h(tmp2, tmp0);
        tmp1 = __msa_aver_s_h(tmp3, tmp1);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        out = __msa_aver_u_b(out, dst0);
        ST_D2(out, 0, 1, dst, stride);
        dst += (2 * stride);

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
    }
}

void ff_avg_h264_qpel4_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out, dstv = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src21_r, src32_r, src43_r, src54_r, src65_r, src76_r;
    v16i8 src87_r, src10_l, src21_l, src32_l, src43_l, src54_l, src65_l;
    v16i8 src76_l, src87_l, filt0, filt1, filt2;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3, shf_vec7;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);
    v8i16 zeros = { 0 };

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    LD_SB4(src, stride, src5, src6, src7, src8);
    XORI_B4_128_SB(src5, src6, src7, src8);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);
    ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l, src65_l,
               src76_l, src87_l);
    vt_res0 = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
    hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
    hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);

    vt_res0 = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec6);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec7);
    hz_res2 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec6, minus5h, plus20h, hz_res2, hz_res2);
    hz_res3 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec7, minus5h, plus20h, hz_res3, hz_res3);

    SRARI_W2_SW(hz_res0, hz_res1, 10);
    SAT_SW2_SW(hz_res0, hz_res1, 7);
    SRARI_W2_SW(hz_res2, hz_res3, 10);
    SAT_SW2_SW(hz_res2, hz_res3, 7);

    dst0 = __msa_srari_h(shf_vec2, 5);
    dst1 = __msa_srari_h(shf_vec5, 5);
    dst2 = __msa_srari_h(shf_vec6, 5);
    dst3 = __msa_srari_h(shf_vec7, 5);

    SAT_SH2_SH(dst0, dst1, 7);
    SAT_SH2_SH(dst2, dst3, 7);
    ILVEV_H2_SH(dst0, zeros, dst1, zeros, dst0, dst1);
    ILVEV_H2_SH(dst2, zeros, dst3, zeros, dst2, dst3);

    hz_res0 = __msa_aver_s_w(hz_res0, (v4i32) dst0);
    hz_res1 = __msa_aver_s_w(hz_res1, (v4i32) dst1);
    hz_res2 = __msa_aver_s_w(hz_res2, (v4i32) dst2);
    hz_res3 = __msa_aver_s_w(hz_res3, (v4i32) dst3);

    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dstv);
    PCKEV_H2_SH(hz_res1, hz_res0, hz_res3, hz_res2, dst0, dst2);
    out = PCKEV_XORI128_UB(dst0, dst2);
    out = __msa_aver_u_b(out, dstv);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel4_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint32_t tp0, tp1, tp2, tp3;
    const int16_t filt_const0 = 0xfb01;
    const int16_t filt_const1 = 0x1414;
    const int16_t filt_const2 = 0x1fb;
    v16u8 out, dstv = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src21_r, src32_r, src43_r, src54_r, src65_r, src76_r;
    v16i8 src87_r, src10_l, src21_l, src32_l, src43_l, src54_l, src65_l;
    v16i8 src76_l, src87_l, filt0, filt1, filt2;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3, dst0, dst1, dst2, dst3, shf_vec7;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5, shf_vec6;
    v4i32 hz_res0, hz_res1, hz_res2, hz_res3;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);
    v8i16 zeros = { 0 };

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    LD_SB4(src, stride, src5, src6, src7, src8);
    XORI_B4_128_SB(src5, src6, src7, src8);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_l, src21_l,
               src32_l, src43_l);
    ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_l, src65_l,
               src76_l, src87_l);
    vt_res0 = AVC_DOT_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
    hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
    hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);

    vt_res0 = AVC_DOT_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
    vt_res1 = AVC_DOT_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
    vt_res2 = AVC_DOT_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
    vt_res3 = AVC_DOT_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
    VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1, mask0,
               mask1, mask2, shf_vec0, shf_vec1, shf_vec6);
    VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3, mask0,
               mask1, mask2, shf_vec3, shf_vec4, shf_vec7);
    hz_res2 = __msa_hadd_s_w(shf_vec0, shf_vec0);
    DPADD_SH2_SW(shf_vec1, shf_vec6, minus5h, plus20h, hz_res2, hz_res2);
    hz_res3 = __msa_hadd_s_w(shf_vec3, shf_vec3);
    DPADD_SH2_SW(shf_vec4, shf_vec7, minus5h, plus20h, hz_res3, hz_res3);

    SRARI_W2_SW(hz_res0, hz_res1, 10);
    SAT_SW2_SW(hz_res0, hz_res1, 7);
    SRARI_W2_SW(hz_res2, hz_res3, 10);
    SAT_SW2_SW(hz_res2, hz_res3, 7);

    dst0 = __msa_srari_h(shf_vec2, 5);
    dst1 = __msa_srari_h(shf_vec5, 5);
    dst2 = __msa_srari_h(shf_vec6, 5);
    dst3 = __msa_srari_h(shf_vec7, 5);

    SAT_SH2_SH(dst0, dst1, 7);
    SAT_SH2_SH(dst2, dst3, 7);

    dst0 = __msa_ilvod_h(zeros, dst0);
    dst1 = __msa_ilvod_h(zeros, dst1);
    dst2 = __msa_ilvod_h(zeros, dst2);
    dst3 = __msa_ilvod_h(zeros, dst3);

    hz_res0 = __msa_aver_s_w(hz_res0, (v4i32) dst0);
    hz_res1 = __msa_aver_s_w(hz_res1, (v4i32) dst1);
    hz_res2 = __msa_aver_s_w(hz_res2, (v4i32) dst2);
    hz_res3 = __msa_aver_s_w(hz_res3, (v4i32) dst3);

    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dstv);
    PCKEV_H2_SH(hz_res1, hz_res0, hz_res3, hz_res2, dst0, dst2);
    out = PCKEV_XORI128_UB(dst0, dst2);
    out = __msa_aver_u_b(out, dstv);
    ST_W4(out, 0, 1, 2, 3, dst, stride);
}

void ff_avg_h264_qpel16_mc22_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    const uint8_t *src_tmp = src - (2 * stride) - 2;
    uint8_t *dst_tmp = dst;
    uint64_t tp0, tp1, tp2, tp3;
    uint32_t multiple8_cnt, loop_cnt;
    v16u8 dst0, dst1, out0, out1;
    v16i8 src0, src1, src2, src3, src4, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, res0, res1, res2, res3;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out10_l, hz_out21_l;
    v8i16 hz_out32_l, hz_out43_l, hz_out54_l, hz_out65_l, hz_out76_l;
    v8i16 hz_out87_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        src = src_tmp;
        dst = dst_tmp;

        LD_SB5(src, stride, src0, src1, src2, src3, src4);
        XORI_B5_128_SB(src0, src1, src2, src3, src4);
        src += (5 * stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
        hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

        for (loop_cnt = 4; loop_cnt--;) {
            LD_SB4(src, stride, src0, src1, src2, src3);
            XORI_B4_128_SB(src0, src1, src2, src3);
            src += (4 * stride);

            hz_out5 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
            hz_out6 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
            hz_out7 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
            hz_out8 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
            ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_r, hz_out21_r, hz_out32_r,
                       hz_out43_r);
            ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2,
                       hz_out4, hz_out3, hz_out10_l, hz_out21_l, hz_out32_l,
                       hz_out43_l);
            ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_r, hz_out65_r, hz_out76_r,
                       hz_out87_r);
            ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6,
                       hz_out8, hz_out7, hz_out54_l, hz_out65_l, hz_out76_l,
                       hz_out87_l);

            tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0,
                                  filt1, filt2);
            res0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0,
                                  filt1, filt2);
            res1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0,
                                  filt1, filt2);
            res2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
            tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0,
                                  filt1, filt2);
            tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0,
                                  filt1, filt2);
            res3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);

            LD4(dst, stride, tp0, tp1, tp2, tp3);
            INSERT_D2_UB(tp0, tp1, dst0);
            INSERT_D2_UB(tp2, tp3, dst1);
            out0 = PCKEV_XORI128_UB(res0, res1);
            out1 = PCKEV_XORI128_UB(res2, res3);
            AVER_UB2_UB(out0, dst0, out1, dst1, out0, out1);
            ST_D4(out0, out1, 0, 1, 0, 1, dst, stride);
            dst += (4 * stride);

            hz_out0 = hz_out4;
            hz_out1 = hz_out5;
            hz_out2 = hz_out6;
            hz_out3 = hz_out7;
            hz_out4 = hz_out8;
        }

        src_tmp += 8;
        dst_tmp += 8;
    }
}

void ff_avg_h264_qpel8_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    uint64_t tp0, tp1, tp2, tp3;
    v16u8 out0, out1, dst0 = { 0 }, dst1 = { 0 };
    v16i8 src0, src1, src2, src3, src4, mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, hz_out11, hz_out12;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r, hz_out89_r, hz_out910_r;
    v8i16 hz_out1110_r, hz_out1211_r, res0, res1, res2, res3;
    v8i16 hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l, hz_out54_l;
    v8i16 hz_out65_l, hz_out76_l, hz_out87_l, hz_out89_l, hz_out910_l;
    v8i16 hz_out1110_l, hz_out1211_l, filt0, filt1, filt2;
    v4i32 tmp0, tmp1;

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    src -= ((2 * stride) + 2);
    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src4, mask0, mask1, mask2);

    LD_SB4(src, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * stride);
    hz_out5 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out7 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVL_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_l, hz_out21_l, hz_out32_l, hz_out43_l);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);
    ILVL_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_l, hz_out65_l, hz_out76_l, hz_out87_l);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out10_l, hz_out32_l, hz_out54_l, filt0, filt1,
                          filt2);
    res0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_l, hz_out43_l, hz_out65_l, filt0, filt1,
                          filt2);
    res1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out32_l, hz_out54_l, hz_out76_l, filt0, filt1,
                          filt2);
    res2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_l, hz_out65_l, hz_out87_l, filt0, filt1,
                          filt2);
    res3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    out0 = PCKEV_XORI128_UB(res0, res1);
    out1 = PCKEV_XORI128_UB(res2, res3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
    dst += (4 * stride);

    LD_SB4(src, stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    hz_out9 = AVC_HORZ_FILTER_SH(src0, src0, mask0, mask1, mask2);
    hz_out10 = AVC_HORZ_FILTER_SH(src1, src1, mask0, mask1, mask2);
    hz_out11 = AVC_HORZ_FILTER_SH(src2, src2, mask0, mask1, mask2);
    hz_out12 = AVC_HORZ_FILTER_SH(src3, src3, mask0, mask1, mask2);
    ILVR_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_r, hz_out910_r, hz_out1110_r,
               hz_out1211_r);
    ILVL_H4_SH(hz_out9, hz_out8, hz_out10, hz_out9, hz_out11, hz_out10,
               hz_out12, hz_out11, hz_out89_l, hz_out910_l, hz_out1110_l,
               hz_out1211_l);
    tmp0 = AVC_DOT_SW3_SW(hz_out54_r, hz_out76_r, hz_out89_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out54_l, hz_out76_l, hz_out89_l, filt0, filt1,
                          filt2);
    res0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out65_r, hz_out87_r, hz_out910_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out65_l, hz_out87_l, hz_out910_l, filt0, filt1,
                          filt2);
    res1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out76_r, hz_out89_r, hz_out1110_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out76_l, hz_out89_l, hz_out1110_l, filt0, filt1,
                          filt2);
    res2 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out87_r, hz_out910_r, hz_out1211_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out87_l, hz_out910_l, hz_out1211_l, filt0, filt1,
                          filt2);
    res3 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    LD4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_D2_UB(tp0, tp1, dst0);
    INSERT_D2_UB(tp2, tp3, dst1);
    out0 = PCKEV_XORI128_UB(res0, res1);
    out1 = PCKEV_XORI128_UB(res2, res3);
    AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
    ST_D4(dst0, dst1, 0, 1, 0, 1, dst, stride);
}

void ff_avg_h264_qpel4_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    const int32_t filt_const0 = 0xfffb0001;
    const int32_t filt_const1 = 0x140014;
    const int32_t filt_const2 = 0x1fffb;
    uint32_t tp0, tp1, tp2, tp3;
    v16u8 res, dst0 = { 0 };
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, res0, res1, filt0, filt1, filt2;
    v8i16 hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r, hz_out54_r;
    v8i16 hz_out65_r, hz_out76_r, hz_out87_r;
    v4i32 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    filt0 = (v8i16) __msa_fill_w(filt_const0);
    filt1 = (v8i16) __msa_fill_w(filt_const1);
    filt2 = (v8i16) __msa_fill_w(filt_const2);

    src -= ((2 * stride) + 2);

    LD_SB5(src, stride, src0, src1, src2, src3, src4);
    src += (5 * stride);
    LD_SB4(src, stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);
    hz_out0 = AVC_HORZ_FILTER_SH(src0, src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, src5, mask0, mask1, mask2);
    hz_out6 = AVC_HORZ_FILTER_SH(src6, src7, mask0, mask1, mask2);
    hz_out8 = AVC_HORZ_FILTER_SH(src8, src8, mask0, mask1, mask2);
    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);
    PCKOD_D2_SH(hz_out4, hz_out4, hz_out6, hz_out6, hz_out5, hz_out7);
    ILVR_H4_SH(hz_out1, hz_out0, hz_out2, hz_out1, hz_out3, hz_out2, hz_out4,
               hz_out3, hz_out10_r, hz_out21_r, hz_out32_r, hz_out43_r);
    ILVR_H4_SH(hz_out5, hz_out4, hz_out6, hz_out5, hz_out7, hz_out6, hz_out8,
               hz_out7, hz_out54_r, hz_out65_r, hz_out76_r, hz_out87_r);

    tmp0 = AVC_DOT_SW3_SW(hz_out10_r, hz_out32_r, hz_out54_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out21_r, hz_out43_r, hz_out65_r, filt0, filt1,
                          filt2);
    res0 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    tmp0 = AVC_DOT_SW3_SW(hz_out32_r, hz_out54_r, hz_out76_r, filt0, filt1,
                          filt2);
    tmp1 = AVC_DOT_SW3_SW(hz_out43_r, hz_out65_r, hz_out87_r, filt0, filt1,
                          filt2);
    res1 = __msa_pckev_h((v8i16) tmp1, (v8i16) tmp0);
    LW4(dst, stride, tp0, tp1, tp2, tp3);
    INSERT_W4_UB(tp0, tp1, tp2, tp3, dst0);
    res = PCKEV_XORI128_UB(res0, res1);
    res = __msa_aver_u_b(res, dst0);
    ST_W4(res, 0, 1, 2, 3, dst, stride);
}
