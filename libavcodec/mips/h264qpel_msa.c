/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
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

#define AVC_CALC_DPADD_H_6PIX_2COEFF_SH(in0, in1, in2, in3, in4, in5)    \
( {                                                                      \
    v4i32 tmp0_m, tmp1_m;                                                \
    v8i16 out0_m, out1_m, out2_m, out3_m;                                \
    v8i16 minus5h_m = __msa_ldi_h(-5);                                   \
    v8i16 plus20h_m = __msa_ldi_h(20);                                   \
                                                                         \
    ILVRL_H2_SW(in5, in0, tmp0_m, tmp1_m);                               \
                                                                         \
    tmp0_m = __msa_hadd_s_w((v8i16) tmp0_m, (v8i16) tmp0_m);             \
    tmp1_m = __msa_hadd_s_w((v8i16) tmp1_m, (v8i16) tmp1_m);             \
                                                                         \
    ILVRL_H2_SH(in1, in4, out0_m, out1_m);                               \
    DPADD_SH2_SW(out0_m, out1_m, minus5h_m, minus5h_m, tmp0_m, tmp1_m);  \
    ILVRL_H2_SH(in2, in3, out2_m, out3_m);                               \
    DPADD_SH2_SW(out2_m, out3_m, plus20h_m, plus20h_m, tmp0_m, tmp1_m);  \
                                                                         \
    SRARI_W2_SW(tmp0_m, tmp1_m, 10);                                     \
    SAT_SW2_SW(tmp0_m, tmp1_m, 7);                                       \
    out0_m = __msa_pckev_h((v8i16) tmp1_m, (v8i16) tmp0_m);              \
                                                                         \
    out0_m;                                                              \
} )

#define AVC_HORZ_FILTER_SH(in, mask0, mask1, mask2)     \
( {                                                     \
    v8i16 out0_m, out1_m;                               \
    v16i8 tmp0_m, tmp1_m;                               \
    v16i8 minus5b = __msa_ldi_b(-5);                    \
    v16i8 plus20b = __msa_ldi_b(20);                    \
                                                        \
    tmp0_m = __msa_vshf_b((v16i8) mask0, in, in);       \
    out0_m = __msa_hadd_s_h(tmp0_m, tmp0_m);            \
                                                        \
    tmp0_m = __msa_vshf_b((v16i8) mask1, in, in);       \
    out0_m = __msa_dpadd_s_h(out0_m, minus5b, tmp0_m);  \
                                                        \
    tmp1_m = __msa_vshf_b((v16i8) (mask2), in, in);     \
    out1_m = __msa_dpadd_s_h(out0_m, plus20b, tmp1_m);  \
                                                        \
    out1_m;                                             \
} )

static const uint8_t luma_mask_arr[16 * 8] = {
    /* 8 width cases */
    0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11, 7, 12,
    1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 9, 7, 10, 8, 11,
    2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,

    /* 4 width cases */
    0, 5, 1, 6, 2, 7, 3, 8, 16, 21, 17, 22, 18, 23, 19, 24,
    1, 4, 2, 5, 3, 6, 4, 7, 17, 20, 18, 21, 19, 22, 20, 23,
    2, 3, 3, 4, 4, 5, 5, 6, 18, 19, 19, 20, 20, 21, 21, 22,

    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25,
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26
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

#define AVC_CALC_DPADD_B_6PIX_2COEFF_R_SH(vec0, vec1, vec2, vec3, vec4, vec5)  \
( {                                                                            \
    v8i16 tmp1_m;                                                              \
    v16i8 tmp0_m, tmp2_m;                                                      \
    v16i8 minus5b_m = __msa_ldi_b(-5);                                         \
    v16i8 plus20b_m = __msa_ldi_b(20);                                         \
                                                                               \
    tmp1_m = (v8i16) __msa_ilvr_b((v16i8) vec5, (v16i8) vec0);                 \
    tmp1_m = __msa_hadd_s_h((v16i8) tmp1_m, (v16i8) tmp1_m);                   \
                                                                               \
    ILVR_B2_SB(vec4, vec1, vec3, vec2, tmp0_m, tmp2_m);                        \
    DPADD_SB2_SH(tmp0_m, tmp2_m, minus5b_m, plus20b_m, tmp1_m, tmp1_m);        \
                                                                               \
    tmp1_m;                                                                    \
} )

#define AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(vec0, vec1, vec2, vec3, vec4, vec5)  \
( {                                                                            \
    v4i32 tmp1_m;                                                              \
    v8i16 tmp2_m, tmp3_m;                                                      \
    v8i16 minus5h_m = __msa_ldi_h(-5);                                         \
    v8i16 plus20h_m = __msa_ldi_h(20);                                         \
                                                                               \
    tmp1_m = (v4i32) __msa_ilvr_h((v8i16) vec5, (v8i16) vec0);                 \
    tmp1_m = __msa_hadd_s_w((v8i16) tmp1_m, (v8i16) tmp1_m);                   \
                                                                               \
    ILVR_H2_SH(vec1, vec4, vec2, vec3, tmp2_m, tmp3_m);                        \
    DPADD_SH2_SW(tmp2_m, tmp3_m, minus5h_m, plus20h_m, tmp1_m, tmp1_m);        \
                                                                               \
    tmp1_m = __msa_srari_w(tmp1_m, 10);                                        \
    tmp1_m = __msa_sat_s_w(tmp1_m, 7);                                         \
                                                                               \
    tmp2_m = __msa_pckev_h((v8i16) tmp1_m, (v8i16) tmp1_m);                    \
                                                                               \
    tmp2_m;                                                                    \
} )

#define AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,              \
                                                    mask0, mask1, mask2)     \
( {                                                                          \
    v8i16 hz_out_m;                                                          \
    v16i8 vec0_m, vec1_m, vec2_m;                                            \
    v16i8 minus5b_m = __msa_ldi_b(-5);                                       \
    v16i8 plus20b_m = __msa_ldi_b(20);                                       \
                                                                             \
    vec0_m = __msa_vshf_b((v16i8) mask0, (v16i8) src1, (v16i8) src0);        \
    hz_out_m = __msa_hadd_s_h(vec0_m, vec0_m);                               \
                                                                             \
    VSHF_B2_SB(src0, src1, src0, src1, mask1, mask2, vec1_m, vec2_m);        \
    DPADD_SB2_SH(vec1_m, vec2_m, minus5b_m, plus20b_m, hz_out_m, hz_out_m);  \
                                                                             \
    hz_out_m;                                                                \
} )

static void avc_luma_hz_4w_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 res0, res1;
    v16u8 out;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

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
        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_8w_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 res0, res1, res2, res3;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);
    v16u8 out0, out1;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
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
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        out0 = PCKEV_XORI128_UB(res0, res1);
        out1 = PCKEV_XORI128_UB(res2, res3);
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_16w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, 8, src0, src1);
        src += src_stride;
        LD_SB2(src, 8, src2, src3);
        src += src_stride;

        XORI_B4_128_SB(src0, src1, src2, src3);
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

        LD_SB2(src, 8, src4, src5);
        src += src_stride;
        LD_SB2(src, 8, src6, src7);
        src += src_stride;

        XORI_B4_128_SB(src4, src5, src6, src7);
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
        PCKEV_B4_SB(res1, res0, res3, res2, res5, res4, res7, res6,
                    vec0, vec1, vec2, vec3);
        XORI_B4_128_SB(vec0, vec1, vec2, vec3);

        ST_SB4(vec0, vec1, vec2, vec3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_qrt_4w_msa(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   int32_t height, uint8_t hor_offset)
{
    uint8_t slide;
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 res0, res1;
    v16i8 res, mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    slide = 2 + hor_offset;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

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
        src0 = __msa_sld_b(src0, src0, slide);
        src1 = __msa_sld_b(src1, src1, slide);
        src2 = __msa_sld_b(src2, src2, slide);
        src3 = __msa_sld_b(src3, src3, slide);
        src0 = (v16i8) __msa_insve_w((v4i32) src0, 1, (v4i32) src1);
        src1 = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
        src0 = (v16i8) __msa_insve_d((v2i64) src0, 1, (v2i64) src1);
        res = __msa_aver_s_b(res, src0);
        res = (v16i8) __msa_xori_b((v16u8) res, 128);

        ST4x4_UB(res, res, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_qrt_8w_msa(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   int32_t height, uint8_t hor_offset)
{
    uint8_t slide;
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v16i8 tmp0, tmp1;
    v8i16 res0, res1, res2, res3;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    slide = 2 + hor_offset;

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
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

        src0 = __msa_sld_b(src0, src0, slide);
        src1 = __msa_sld_b(src1, src1, slide);
        src2 = __msa_sld_b(src2, src2, slide);
        src3 = __msa_sld_b(src3, src3, slide);

        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, tmp0, tmp1);
        PCKEV_D2_SB(src1, src0, src3, src2, src0, src1);

        tmp0 = __msa_aver_s_b(tmp0, src0);
        tmp1 = __msa_aver_s_b(tmp1, src1);

        XORI_B2_128_SB(tmp0, tmp1);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);

        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_qrt_16w_msa(const uint8_t *src, int32_t src_stride,
                                    uint8_t *dst, int32_t dst_stride,
                                    int32_t height, uint8_t hor_offset)
{
    uint32_t loop_cnt;
    v16i8 dst0, dst1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0, mask1, mask2, vshf;
    v8i16 res0, res1, res2, res3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    if (hor_offset) {
        vshf = LD_SB(&luma_mask_arr[16 + 96]);
    } else {
        vshf = LD_SB(&luma_mask_arr[96]);
    }

    for (loop_cnt = height >> 1; loop_cnt--;) {
        LD_SB2(src, 8, src0, src1);
        src += src_stride;
        LD_SB2(src, 8, src2, src3);
        src += src_stride;

        XORI_B4_128_SB(src0, src1, src2, src3);
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
        VSHF_B2_SB(src0, src1, src2, src3, vshf, vshf, src0, src2);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, dst0, dst1);

        dst0 = __msa_aver_s_b(dst0, src0);
        dst1 = __msa_aver_s_b(dst1, src2);

        XORI_B2_128_SB(dst0, dst1);

        ST_SB2(dst0, dst1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void avc_luma_vt_4w_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               int32_t height)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776;
    v16i8 filt0, filt1, filt2;
    v8i16 out10, out32;
    v16u8 out;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_r, src65_r, src76_r, src87_r);
        ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
        XORI_B2_128_SB(src6554, src8776);
        out10 = DPADD_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
        out32 = DPADD_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
        SRARI_H2_SH(out10, out32, 5);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);

        dst += (4 * dst_stride);
        src2110 = src6554;
        src4332 = src8776;
        src4 = src8;
    }
}

static void avc_luma_vt_8w_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               int32_t height)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 out0_r, out1_r, out2_r, out3_r;
    v16i8 filt0, filt1, filt2;
    v16u8 out0, out1;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        out0 = PCKEV_XORI128_UB(out0_r, out1_r);
        out1 = PCKEV_XORI128_UB(out2_r, out3_r);
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src76_r;
        src32_r = src98_r;
        src21_r = src87_r;
        src43_r = src109_r;
        src4 = src10;
    }
}

static void avc_luma_vt_16w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                int32_t height)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;
    v16u8 res0, res1, res2, res3;
    v16i8 filt0, filt1, filt2;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_l, src21_l, src32_l, src43_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_r, src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_l, src65_l, src76_l, src87_l);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = DPADD_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = DPADD_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = DPADD_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = DPADD_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        XORI_B4_128_UB(res0, res1, res2, res3);

        ST_UB4(res0, res1, res2, res3, dst, dst_stride);
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

static void avc_luma_vt_qrt_4w_msa(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   int32_t height, uint8_t ver_offset)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776;
    v8i16 out10, out32;
    v16i8 filt0, filt1, filt2;
    v16u8 out;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_r, src65_r, src76_r, src87_r);
        ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
        XORI_B2_128_SB(src6554, src8776);
        out10 = DPADD_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
        out32 = DPADD_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
        SRARI_H2_SH(out10, out32, 5);
        SAT_SH2_SH(out10, out32, 7);

        out = PCKEV_XORI128_UB(out10, out32);

        if (ver_offset) {
            src32_r = (v16i8) __msa_insve_w((v4i32) src3, 1, (v4i32) src4);
            src54_r = (v16i8) __msa_insve_w((v4i32) src5, 1, (v4i32) src6);
        } else {
            src32_r = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
            src54_r = (v16i8) __msa_insve_w((v4i32) src4, 1, (v4i32) src5);
        }

        src32_r = (v16i8) __msa_insve_d((v2i64) src32_r, 1, (v2i64) src54_r);
        out = __msa_aver_u_b(out, (v16u8) src32_r);

        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
        src2110 = src6554;
        src4332 = src8776;
        src2 = src6;
        src3 = src7;
        src4 = src8;
    }
}

static void avc_luma_vt_qrt_8w_msa(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   int32_t height, uint8_t ver_offset)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 out0_r, out1_r, out2_r, out3_r;
    v16i8 res0, res1;
    v16i8 filt0, filt1, filt2;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        PCKEV_B2_SB(out1_r, out0_r, out3_r, out2_r, res0, res1);

        if (ver_offset) {
            PCKEV_D2_SB(src4, src3, src8, src7, src10_r, src32_r);
        } else {
            PCKEV_D2_SB(src3, src2, src7, src4, src10_r, src32_r);
        }

        res0 = __msa_aver_s_b(res0, (v16i8) src10_r);
        res1 = __msa_aver_s_b(res1, (v16i8) src32_r);

        XORI_B2_128_SB(res0, res1);
        ST8x4_UB(res0, res1, dst, dst_stride);

        dst += (4 * dst_stride);
        src10_r = src76_r;
        src32_r = src98_r;
        src21_r = src87_r;
        src43_r = src109_r;
        src2 = src8;
        src3 = src9;
        src4 = src10;
    }
}

static void avc_luma_vt_qrt_16w_msa(const uint8_t *src, int32_t src_stride,
                                    uint8_t *dst, int32_t dst_stride,
                                    int32_t height, uint8_t ver_offset)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;
    v16u8 res0, res1, res2, res3;
    v16i8 filt0, filt1, filt2;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_l, src21_l, src32_l, src43_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_r, src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_l, src65_l, src76_l, src87_l);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = DPADD_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = DPADD_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = DPADD_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = DPADD_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);

        if (ver_offset) {
            res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src3);
            res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src4);
            res2 = (v16u8) __msa_aver_s_b((v16i8) res2, src5);
            res3 = (v16u8) __msa_aver_s_b((v16i8) res3, src6);
        } else {
            res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src2);
            res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src3);
            res2 = (v16u8) __msa_aver_s_b((v16i8) res2, src4);
            res3 = (v16u8) __msa_aver_s_b((v16i8) res3, src5);
        }

        XORI_B4_128_UB(res0, res1, res2, res3);
        ST_UB4(res0, res1, res2, res3, dst, dst_stride);

        dst += (4 * dst_stride);

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

static void avc_luma_mid_4w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v8i16 dst0, dst1, dst2, dst3;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    hz_out0 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                          mask0, mask1, mask2);
    hz_out2 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                          mask0, mask1, mask2);

    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);

    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);

        hz_out5 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                              mask0, mask1,
                                                              mask2);
        hz_out7 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                              mask0, mask1,
                                                              mask2);

        PCKOD_D2_SH(hz_out5, hz_out5, hz_out7, hz_out7, hz_out6, hz_out8);

        dst0 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out0, hz_out1, hz_out2,
                                                 hz_out3, hz_out4, hz_out5);
        dst1 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out1, hz_out2, hz_out3,
                                                 hz_out4, hz_out5, hz_out6);
        dst2 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out2, hz_out3, hz_out4,
                                                 hz_out5, hz_out6, hz_out7);
        dst3 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out3, hz_out4, hz_out5,
                                                 hz_out6, hz_out7, hz_out8);

        PCKEV_B2_SB(dst1, dst0, dst3, dst2, src0, src1);
        XORI_B2_128_SB(src0, src1);

        ST4x4_UB(src0, src1, 0, 2, 0, 2, dst, dst_stride);

        dst += (4 * dst_stride);

        hz_out0 = hz_out4;
        hz_out1 = hz_out5;
        hz_out2 = hz_out6;
        hz_out3 = hz_out7;
        hz_out4 = hz_out8;
    }
}

static void avc_luma_mid_8w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v8i16 dst0, dst1, dst2, dst3;
    v16u8 out0, out1;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);

        hz_out5 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
        hz_out6 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
        hz_out7 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
        hz_out8 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);
        dst0 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out0, hz_out1, hz_out2,
                                               hz_out3, hz_out4, hz_out5);
        dst1 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out1, hz_out2, hz_out3,
                                               hz_out4, hz_out5, hz_out6);
        dst2 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out2, hz_out3, hz_out4,
                                               hz_out5, hz_out6, hz_out7);
        dst3 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out3, hz_out4, hz_out5,
                                               hz_out6, hz_out7, hz_out8);
        out0 = PCKEV_XORI128_UB(dst0, dst1);
        out1 = PCKEV_XORI128_UB(dst2, dst3);
        ST8x4_UB(out0, out1, dst, dst_stride);

        dst += (4 * dst_stride);
        hz_out3 = hz_out7;
        hz_out1 = hz_out5;
        hz_out5 = hz_out4;
        hz_out4 = hz_out8;
        hz_out2 = hz_out6;
        hz_out0 = hz_out5;
    }
}

static void avc_luma_mid_16w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 int32_t height)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_mid_8w_msa(src, src_stride, dst, dst_stride, height);
        src += 8;
        dst += 8;
    }
}

static void avc_luma_midh_qrt_4w_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     int32_t height, uint8_t horiz_offset)
{
    uint32_t row;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3;
    v4i32 hz_res0, hz_res1;
    v8i16 dst0, dst1;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);
    v8i16 zeros = { 0 };
    v16u8 out;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    for (row = (height >> 1); row--;) {
        LD_SB2(src, src_stride, src5, src6);
        src += (2 * src_stride);

        XORI_B2_128_SB(src5, src6);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src1, src2, src3, src4, src5, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1,
                   mask0, mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3,
                   mask0, mask1, mask2, shf_vec3, shf_vec4, shf_vec5);
        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);
        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);

        SRARI_W2_SW(hz_res0, hz_res1, 10);
        SAT_SW2_SW(hz_res0, hz_res1, 7);

        dst0 = __msa_srari_h(shf_vec2, 5);
        dst1 = __msa_srari_h(shf_vec5, 5);

        SAT_SH2_SH(dst0, dst1, 7);

        if (horiz_offset) {
            dst0 = __msa_ilvod_h(zeros, dst0);
            dst1 = __msa_ilvod_h(zeros, dst1);
        } else {
            ILVEV_H2_SH(dst0, zeros, dst1, zeros, dst0, dst1);
        }

        hz_res0 = __msa_aver_s_w(hz_res0, (v4i32) dst0);
        hz_res1 = __msa_aver_s_w(hz_res1, (v4i32) dst1);
        dst0 = __msa_pckev_h((v8i16) hz_res1, (v8i16) hz_res0);

        out = PCKEV_XORI128_UB(dst0, dst0);
        ST4x2_UB(out, dst, dst_stride);

        dst += (2 * dst_stride);

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
    }
}

static void avc_luma_midh_qrt_8w_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     int32_t height, uint8_t horiz_offset)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_midh_qrt_4w_msa(src, src_stride, dst, dst_stride, height,
                                 horiz_offset);

        src += 4;
        dst += 4;
    }
}

static void avc_luma_midh_qrt_16w_msa(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      int32_t height, uint8_t horiz_offset)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        avc_luma_midh_qrt_4w_msa(src, src_stride, dst, dst_stride, height,
                                 horiz_offset);

        src += 4;
        dst += 4;
    }
}

static void avc_luma_midv_qrt_4w_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     int32_t height, uint8_t ver_offset)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    hz_out0 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                          mask0, mask1, mask2);
    hz_out2 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                          mask0, mask1, mask2);

    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);

    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        XORI_B4_128_SB(src0, src1, src2, src3);

        hz_out5 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                              mask0, mask1,
                                                              mask2);
        hz_out7 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                              mask0, mask1,
                                                              mask2);

        PCKOD_D2_SH(hz_out5, hz_out5, hz_out7, hz_out7, hz_out6, hz_out8);

        dst0 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out0, hz_out1, hz_out2,
                                               hz_out3, hz_out4, hz_out5);
        dst2 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out1, hz_out2, hz_out3,
                                               hz_out4, hz_out5, hz_out6);
        dst4 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out2, hz_out3, hz_out4,
                                               hz_out5, hz_out6, hz_out7);
        dst6 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out3, hz_out4, hz_out5,
                                               hz_out6, hz_out7, hz_out8);

        if (ver_offset) {
            dst1 = __msa_srari_h(hz_out3, 5);
            dst3 = __msa_srari_h(hz_out4, 5);
            dst5 = __msa_srari_h(hz_out5, 5);
            dst7 = __msa_srari_h(hz_out6, 5);
        } else {
            dst1 = __msa_srari_h(hz_out2, 5);
            dst3 = __msa_srari_h(hz_out3, 5);
            dst5 = __msa_srari_h(hz_out4, 5);
            dst7 = __msa_srari_h(hz_out5, 5);
        }

        SAT_SH4_SH(dst1, dst3, dst5, dst7, 7);

        dst0 = __msa_aver_s_h(dst0, dst1);
        dst1 = __msa_aver_s_h(dst2, dst3);
        dst2 = __msa_aver_s_h(dst4, dst5);
        dst3 = __msa_aver_s_h(dst6, dst7);

        PCKEV_B2_SB(dst1, dst0, dst3, dst2, src0, src1);
        XORI_B2_128_SB(src0, src1);

        ST4x4_UB(src0, src1, 0, 2, 0, 2, dst, dst_stride);

        dst += (4 * dst_stride);
        hz_out0 = hz_out4;
        hz_out1 = hz_out5;
        hz_out2 = hz_out6;
        hz_out3 = hz_out7;
        hz_out4 = hz_out8;
    }
}

static void avc_luma_midv_qrt_8w_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     int32_t height, uint8_t ver_offset)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 out;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);

        hz_out5 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
        hz_out6 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
        hz_out7 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
        hz_out8 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);

        dst0 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out0, hz_out1, hz_out2,
                                               hz_out3, hz_out4, hz_out5);
        dst2 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out1, hz_out2, hz_out3,
                                               hz_out4, hz_out5, hz_out6);
        dst4 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out2, hz_out3, hz_out4,
                                               hz_out5, hz_out6, hz_out7);
        dst6 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out3, hz_out4, hz_out5,
                                               hz_out6, hz_out7, hz_out8);

        if (ver_offset) {
            dst1 = __msa_srari_h(hz_out3, 5);
            dst3 = __msa_srari_h(hz_out4, 5);
            dst5 = __msa_srari_h(hz_out5, 5);
            dst7 = __msa_srari_h(hz_out6, 5);
        } else {
            dst1 = __msa_srari_h(hz_out2, 5);
            dst3 = __msa_srari_h(hz_out3, 5);
            dst5 = __msa_srari_h(hz_out4, 5);
            dst7 = __msa_srari_h(hz_out5, 5);
        }

        SAT_SH4_SH(dst1, dst3, dst5, dst7, 7);

        dst0 = __msa_aver_s_h(dst0, dst1);
        dst1 = __msa_aver_s_h(dst2, dst3);
        dst2 = __msa_aver_s_h(dst4, dst5);
        dst3 = __msa_aver_s_h(dst6, dst7);

        out = PCKEV_XORI128_UB(dst0, dst0);
        ST8x1_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(dst1, dst1);
        ST8x1_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(dst2, dst2);
        ST8x1_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(dst3, dst3);
        ST8x1_UB(out, dst);
        dst += dst_stride;

        hz_out0 = hz_out4;
        hz_out1 = hz_out5;
        hz_out2 = hz_out6;
        hz_out3 = hz_out7;
        hz_out4 = hz_out8;
    }
}

static void avc_luma_midv_qrt_16w_msa(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      int32_t height, uint8_t vert_offset)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_midv_qrt_8w_msa(src, src_stride, dst, dst_stride, height,
                                 vert_offset);

        src += 8;
        dst += 8;
    }
}

static void avc_luma_hv_qrt_4w_msa(const uint8_t *src_x, const uint8_t *src_y,
                                   int32_t src_stride, uint8_t *dst,
                                   int32_t dst_stride, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4;
    v16i8 src_vt5, src_vt6, src_vt7, src_vt8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, vert_out0, vert_out1;
    v8i16 out0, out1;
    v16u8 out;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    LD_SB5(src_y, src_stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * src_stride);

    src_vt0 = (v16i8) __msa_insve_w((v4i32) src_vt0, 1, (v4i32) src_vt1);
    src_vt1 = (v16i8) __msa_insve_w((v4i32) src_vt1, 1, (v4i32) src_vt2);
    src_vt2 = (v16i8) __msa_insve_w((v4i32) src_vt2, 1, (v4i32) src_vt3);
    src_vt3 = (v16i8) __msa_insve_w((v4i32) src_vt3, 1, (v4i32) src_vt4);

    XORI_B4_128_SB(src_vt0, src_vt1, src_vt2, src_vt3);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src_x, src_stride, src_hz0, src_hz1, src_hz2, src_hz3);
        src_x += (4 * src_stride);

        XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);

        hz_out0 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src_hz0,
                                                              src_hz1, mask0,
                                                              mask1, mask2);
        hz_out1 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src_hz2,
                                                              src_hz3, mask0,
                                                              mask1, mask2);

        SRARI_H2_SH(hz_out0, hz_out1, 5);
        SAT_SH2_SH(hz_out0, hz_out1, 7);

        LD_SB4(src_y, src_stride, src_vt5, src_vt6, src_vt7, src_vt8);
        src_y += (4 * src_stride);

        src_vt4 = (v16i8) __msa_insve_w((v4i32) src_vt4, 1, (v4i32) src_vt5);
        src_vt5 = (v16i8) __msa_insve_w((v4i32) src_vt5, 1, (v4i32) src_vt6);
        src_vt6 = (v16i8) __msa_insve_w((v4i32) src_vt6, 1, (v4i32) src_vt7);
        src_vt7 = (v16i8) __msa_insve_w((v4i32) src_vt7, 1, (v4i32) src_vt8);

        XORI_B4_128_SB(src_vt4, src_vt5, src_vt6, src_vt7);

        /* filter calc */
        vert_out0 = AVC_CALC_DPADD_B_6PIX_2COEFF_R_SH(src_vt0, src_vt1,
                                                      src_vt2, src_vt3,
                                                      src_vt4, src_vt5);
        vert_out1 = AVC_CALC_DPADD_B_6PIX_2COEFF_R_SH(src_vt2, src_vt3,
                                                      src_vt4, src_vt5,
                                                      src_vt6, src_vt7);

        SRARI_H2_SH(vert_out0, vert_out1, 5);
        SAT_SH2_SH(vert_out0, vert_out1, 7);

        out0 = __msa_srari_h((hz_out0 + vert_out0), 1);
        out1 = __msa_srari_h((hz_out1 + vert_out1), 1);

        SAT_SH2_SH(out0, out1, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        src_vt3 = src_vt7;
        src_vt1 = src_vt5;
        src_vt0 = src_vt4;
        src_vt4 = src_vt8;
        src_vt2 = src_vt6;
    }
}

static void avc_luma_hv_qrt_8w_msa(const uint8_t *src_x, const uint8_t *src_y,
                                   int32_t src_stride, uint8_t *dst,
                                   int32_t dst_stride, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4;
    v16i8 src_vt5, src_vt6, src_vt7, src_vt8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 vert_out0, vert_out1, vert_out2, vert_out3;
    v8i16 out0, out1, out2, out3;
    v16u8 tmp0, tmp1;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);
    LD_SB5(src_y, src_stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * src_stride);

    src_vt0 = (v16i8) __msa_insve_d((v2i64) src_vt0, 1, (v2i64) src_vt1);
    src_vt1 = (v16i8) __msa_insve_d((v2i64) src_vt1, 1, (v2i64) src_vt2);
    src_vt2 = (v16i8) __msa_insve_d((v2i64) src_vt2, 1, (v2i64) src_vt3);
    src_vt3 = (v16i8) __msa_insve_d((v2i64) src_vt3, 1, (v2i64) src_vt4);

    XORI_B4_128_SB(src_vt0, src_vt1, src_vt2, src_vt3);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src_x, src_stride, src_hz0, src_hz1, src_hz2, src_hz3);
        XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
        src_x += (4 * src_stride);

        hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, mask0, mask1, mask2);

        SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
        SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);

        LD_SB4(src_y, src_stride, src_vt5, src_vt6, src_vt7, src_vt8);
        src_y += (4 * src_stride);

        src_vt4 = (v16i8) __msa_insve_d((v2i64) src_vt4, 1, (v2i64) src_vt5);
        src_vt5 = (v16i8) __msa_insve_d((v2i64) src_vt5, 1, (v2i64) src_vt6);
        src_vt6 = (v16i8) __msa_insve_d((v2i64) src_vt6, 1, (v2i64) src_vt7);
        src_vt7 = (v16i8) __msa_insve_d((v2i64) src_vt7, 1, (v2i64) src_vt8);

        XORI_B4_128_SB(src_vt4, src_vt5, src_vt6, src_vt7);

        /* filter calc */
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src_vt0, src_vt1, src_vt2, src_vt3,
                                        src_vt4, src_vt5, vert_out0, vert_out1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src_vt2, src_vt3, src_vt4, src_vt5,
                                        src_vt6, src_vt7, vert_out2, vert_out3);

        SRARI_H4_SH(vert_out0, vert_out1, vert_out2, vert_out3, 5);
        SAT_SH4_SH(vert_out0, vert_out1, vert_out2, vert_out3, 7);

        out0 = __msa_srari_h((hz_out0 + vert_out0), 1);
        out1 = __msa_srari_h((hz_out1 + vert_out1), 1);
        out2 = __msa_srari_h((hz_out2 + vert_out2), 1);
        out3 = __msa_srari_h((hz_out3 + vert_out3), 1);

        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);

        dst += (4 * dst_stride);
        src_vt3 = src_vt7;
        src_vt1 = src_vt5;
        src_vt5 = src_vt4;
        src_vt4 = src_vt8;
        src_vt2 = src_vt6;
        src_vt0 = src_vt5;
    }
}

static void avc_luma_hv_qrt_16w_msa(const uint8_t *src_x, const uint8_t *src_y,
                                    int32_t src_stride, uint8_t *dst,
                                    int32_t dst_stride, int32_t height)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_hv_qrt_8w_msa(src_x, src_y, src_stride, dst, dst_stride,
                               height);

        src_x += 8;
        src_y += 8;
        dst += 8;
    }
}

static void avc_luma_hz_and_aver_dst_4x4_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride)
{
    v16i8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3, res;
    v8i16 res0, res1;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB4(src, src_stride, src0, src1, src2, src3);

    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
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
    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst1);

    dst0 = (v16u8) __msa_pckev_d((v2i64) dst1, (v2i64) dst0);
    res = __msa_aver_u_b(res, dst0);

    ST4x4_UB(res, res, 0, 1, 2, 3, dst, dst_stride);
}

static void avc_luma_hz_and_aver_dst_8x8_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3;
    v8i16 res0, res1, res2, res3;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        XORI_B4_128_SB(src0, src1, src2, src3);
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
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        CONVERT_UB_AVG_ST8x4_UB(res0, res1, res2, res3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);

        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_and_aver_dst_16x16_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 mask0, mask1, mask2;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB2(src, 8, src0, src1);
        src += src_stride;
        LD_SB2(src, 8, src2, src3);
        src += src_stride;

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        XORI_B4_128_SB(src0, src1, src2, src3);
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
        LD_SB2(src, 8, src4, src5);
        src += src_stride;
        LD_SB2(src, 8, src6, src7);
        src += src_stride;
        XORI_B4_128_SB(src4, src5, src6, src7);
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
        PCKEV_B4_SB(res1, res0, res3, res2, res5, res4, res7, res6,
                    vec0, vec1, vec2, vec3);
        XORI_B4_128_SB(vec0, vec1, vec2, vec3);
        AVER_UB4_UB(vec0, dst0, vec1, dst1, vec2, dst2, vec3, dst3,
                    dst0, dst1, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_qrt_and_aver_dst_4x4_msa(const uint8_t *src,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride,
                                                 uint8_t hor_offset)
{
    uint8_t slide;
    v16i8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 mask0, mask1, mask2;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 out0, out1;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);
    v16u8 res0, res1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);

    if (hor_offset) {
        slide = 3;
    } else {
        slide = 2;
    }

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

    XORI_B4_128_SB(src0, src1, src2, src3);
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0, vec1);
    HADD_SB2_SH(vec0, vec1, out0, out1);
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2, vec3);
    DPADD_SB2_SH(vec2, vec3, minus5b, minus5b, out0, out1);
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4, vec5);
    DPADD_SB2_SH(vec4, vec5, plus20b, plus20b, out0, out1);
    SRARI_H2_SH(out0, out1, 5);
    SAT_SH2_SH(out0, out1, 7);

    PCKEV_B2_UB(out0, out0, out1, out1, res0, res1);

    src0 = __msa_sld_b(src0, src0, slide);
    src1 = __msa_sld_b(src1, src1, slide);
    src2 = __msa_sld_b(src2, src2, slide);
    src3 = __msa_sld_b(src3, src3, slide);
    src0 = (v16i8) __msa_insve_w((v4i32) src0, 1, (v4i32) src1);
    src1 = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
    res0 = (v16u8) __msa_aver_s_b((v16i8) res0, src0);
    res1 = (v16u8) __msa_aver_s_b((v16i8) res1, src1);

    XORI_B2_128_UB(res0, res1);

    dst0 = (v16u8) __msa_insve_w((v4i32) dst0, 1, (v4i32) dst1);
    dst1 = (v16u8) __msa_insve_w((v4i32) dst2, 1, (v4i32) dst3);

    AVER_UB2_UB(res0, dst0, res1, dst1, dst0, dst1);

    ST4x4_UB(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
}

static void avc_luma_hz_qrt_and_aver_dst_8x8_msa(const uint8_t *src,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride,
                                                 uint8_t hor_offset)
{
    uint8_t slide;
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0, mask1, mask2;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v8i16 out0, out1, out2, out3;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);
    v16i8 res0, res1, res2, res3;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    if (hor_offset) {
        slide = 3;
    } else {
        slide = 2;
    }

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
        HADD_SB4_SH(vec0, vec1, vec2, vec3, out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, minus5b, minus5b, minus5b, minus5b,
                     out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec8, vec9);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec10, vec11);
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, plus20b, plus20b, plus20b,
                     plus20b, out0, out1, out2, out3);

        src0 = __msa_sld_b(src0, src0, slide);
        src1 = __msa_sld_b(src1, src1, slide);
        src2 = __msa_sld_b(src2, src2, slide);
        src3 = __msa_sld_b(src3, src3, slide);

        SRARI_H4_SH(out0, out1, out2, out3, 5);
        SAT_SH4_SH(out0, out1, out2, out3, 7);

        PCKEV_B4_SB(out0, out0, out1, out1, out2, out2, out3, out3,
                    res0, res1, res2, res3);

        res0 = __msa_aver_s_b(res0, src0);
        res1 = __msa_aver_s_b(res1, src1);
        res2 = __msa_aver_s_b(res2, src2);
        res3 = __msa_aver_s_b(res3, src3);

        XORI_B4_128_SB(res0, res1, res2, res3);
        AVER_ST8x4_UB(res0, dst0, res1, dst1, res2, dst2, res3, dst3,
                      dst, dst_stride);

        dst += (4 * dst_stride);
    }
}

static void avc_luma_hz_qrt_and_aver_dst_16x16_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   uint8_t hor_offset)
{
    uint32_t loop_cnt;
    v16i8 out0, out1;
    v16i8 src0, src1, src2, src3;
    v16i8 mask0, mask1, mask2, vshf;
    v16u8 dst0, dst1;
    v8i16 res0, res1, res2, res3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16i8 vec6, vec7, vec8, vec9, vec10, vec11;
    v16i8 minus5b = __msa_ldi_b(-5);
    v16i8 plus20b = __msa_ldi_b(20);

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    if (hor_offset) {
        vshf = LD_SB(&luma_mask_arr[16 + 96]);
    } else {
        vshf = LD_SB(&luma_mask_arr[96]);
    }

    for (loop_cnt = 8; loop_cnt--;) {
        LD_SB2(src, 8, src0, src1);
        src += src_stride;
        LD_SB2(src, 8, src2, src3);
        src += src_stride;

        LD_UB2(dst, dst_stride, dst0, dst1);

        XORI_B4_128_SB(src0, src1, src2, src3);
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
        VSHF_B2_SB(src0, src1, src2, src3, vshf, vshf, src0, src2);
        SRARI_H4_SH(res0, res1, res2, res3, 5);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        PCKEV_B2_SB(res1, res0, res3, res2, out0, out1);

        out0 = __msa_aver_s_b(out0, src0);
        out1 = __msa_aver_s_b(out1, src2);

        XORI_B2_128_SB(out0, out1);
        AVER_UB2_UB(out0, dst0, out1, dst1, dst0, dst1);
        ST_UB2(dst0, dst1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void avc_luma_vt_and_aver_dst_4x4_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride)
{
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776;
    v8i16 out10, out32;
    v16i8 filt0, filt1, filt2;
    v16u8 res;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, src_stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
               src54_r, src65_r, src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    out10 = DPADD_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = DPADD_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    res = PCKEV_XORI128_UB(out10, out32);

    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst1);

    dst0 = (v16u8) __msa_pckev_d((v2i64) dst1, (v2i64) dst0);
    dst0 = __msa_aver_u_b(res, dst0);

    ST4x4_UB(dst0, dst0, 0, 1, 2, 3, dst, dst_stride);
}

static void avc_luma_vt_and_aver_dst_8x8_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 out0, out1, out2, out3;
    v16i8 filt0, filt1, filt2;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        out0 = DPADD_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
        out1 = DPADD_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
        out2 = DPADD_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
        out3 = DPADD_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
        SRARI_H4_SH(out0, out1, out2, out3, 5);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        CONVERT_UB_AVG_ST8x4_UB(out0, out1, out2, out3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src76_r;
        src32_r = src98_r;
        src21_r = src87_r;
        src43_r = src109_r;
        src4 = src10;
    }
}

static void avc_luma_vt_and_aver_dst_16x16_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;
    v16i8 filt0, filt1, filt2;
    v16u8 res0, res1, res2, res3;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_l, src21_l, src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_r, src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_l, src65_l, src76_l, src87_l);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = DPADD_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = DPADD_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = DPADD_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = DPADD_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, res0, res1, res2, res3);
        XORI_B4_128_UB(res0, res1, res2, res3);
        AVER_UB4_UB(res0, dst0, res1, dst1, res2, dst2, res3, dst3,
                    res0, res1, res2, res3);
        ST_UB4(res0, res1, res2, res3, dst, dst_stride);
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

static void avc_luma_vt_qrt_and_aver_dst_4x4_msa(const uint8_t *src,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride,
                                                 uint8_t ver_offset)
{
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src2110, src4332, src6554, src8776;
    v8i16 out10, out32;
    v16i8 filt0, filt1, filt2;
    v16u8 res;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_D2_SB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    XORI_B2_128_SB(src2110, src4332);
    LD_SB4(src, src_stride, src5, src6, src7, src8);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
               src54_r, src65_r, src76_r, src87_r);
    ILVR_D2_SB(src65_r, src54_r, src87_r, src76_r, src6554, src8776);
    XORI_B2_128_SB(src6554, src8776);
    out10 = DPADD_SH3_SH(src2110, src4332, src6554, filt0, filt1, filt2);
    out32 = DPADD_SH3_SH(src4332, src6554, src8776, filt0, filt1, filt2);
    SRARI_H2_SH(out10, out32, 5);
    SAT_SH2_SH(out10, out32, 7);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    res = PCKEV_XORI128_UB(out10, out32);

    if (ver_offset) {
        src32_r = (v16i8) __msa_insve_w((v4i32) src3, 1, (v4i32) src4);
        src54_r = (v16i8) __msa_insve_w((v4i32) src5, 1, (v4i32) src6);
    } else {
        src32_r = (v16i8) __msa_insve_w((v4i32) src2, 1, (v4i32) src3);
        src54_r = (v16i8) __msa_insve_w((v4i32) src4, 1, (v4i32) src5);
    }

    src32_r = (v16i8) __msa_insve_d((v2i64) src32_r, 1, (v2i64) src54_r);
    res = __msa_aver_u_b(res, (v16u8) src32_r);

    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst1);

    dst0 = (v16u8) __msa_pckev_d((v2i64) dst1, (v2i64) dst0);
    dst0 = __msa_aver_u_b(res, dst0);

    ST4x4_UB(dst0, dst0, 0, 1, 2, 3, dst, dst_stride);
}

static void avc_luma_vt_qrt_and_aver_dst_8x8_msa(const uint8_t *src,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride,
                                                 uint8_t ver_offset)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 out0_r, out1_r, out2_r, out3_r;
    v16i8 res0, res1;
    v16u8 vec0, vec1;
    v16i8 filt0, filt1, filt2;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src4, src8, src7, src9, src8, src10, src9,
                   src76_r, src87_r, src98_r, src109_r);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src76_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src87_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src76_r, src98_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src87_r, src109_r, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        PCKEV_B2_SB(out1_r, out0_r, out3_r, out2_r, res0, res1);

        if (ver_offset) {
            PCKEV_D2_SB(src4, src3, src8, src7, src10_r, src32_r);
        } else {
            PCKEV_D2_SB(src3, src2, src7, src4, src10_r, src32_r);
        }

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        ILVR_D2_UB(dst1, dst0, dst3, dst2, dst0, dst1);

        vec0 = (v16u8) __msa_aver_s_b(res0, src10_r);
        vec1 = (v16u8) __msa_aver_s_b(res1, src32_r);

        XORI_B2_128_UB(vec0, vec1);
        AVER_UB2_UB(vec0, dst0, vec1, dst1, vec0, vec1);
        ST8x4_UB(vec0, vec1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src76_r;
        src32_r = src98_r;
        src21_r = src87_r;
        src43_r = src109_r;
        src2 = src8;
        src3 = src9;
        src4 = src10;
    }
}

static void avc_luma_vt_qrt_and_aver_dst_16x16_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   uint8_t ver_offset)
{
    int32_t loop_cnt;
    int16_t filt_const0 = 0xfb01;
    int16_t filt_const1 = 0x1414;
    int16_t filt_const2 = 0x1fb;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16i8 src87_r, src10_l, src32_l, src54_l, src76_l, src21_l, src43_l;
    v16i8 src65_l, src87_l;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;
    v16i8 out0, out1, out2, out3;
    v16i8 filt0, filt1, filt2;
    v16u8 res0, res1, res2, res3;

    filt0 = (v16i8) __msa_fill_h(filt_const0);
    filt1 = (v16i8) __msa_fill_h(filt_const1);
    filt2 = (v16i8) __msa_fill_h(filt_const2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVL_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_l, src21_l, src32_l, src43_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src5, src6, src7, src8);
        src += (4 * src_stride);

        XORI_B4_128_SB(src5, src6, src7, src8);
        ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_r, src65_r, src76_r, src87_r);
        ILVL_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7,
                   src54_l, src65_l, src76_l, src87_l);
        out0_r = DPADD_SH3_SH(src10_r, src32_r, src54_r, filt0, filt1, filt2);
        out1_r = DPADD_SH3_SH(src21_r, src43_r, src65_r, filt0, filt1, filt2);
        out2_r = DPADD_SH3_SH(src32_r, src54_r, src76_r, filt0, filt1, filt2);
        out3_r = DPADD_SH3_SH(src43_r, src65_r, src87_r, filt0, filt1, filt2);
        out0_l = DPADD_SH3_SH(src10_l, src32_l, src54_l, filt0, filt1, filt2);
        out1_l = DPADD_SH3_SH(src21_l, src43_l, src65_l, filt0, filt1, filt2);
        out2_l = DPADD_SH3_SH(src32_l, src54_l, src76_l, filt0, filt1, filt2);
        out3_l = DPADD_SH3_SH(src43_l, src65_l, src87_l, filt0, filt1, filt2);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 5);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 5);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_SB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, out0, out1, out2, out3);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        if (ver_offset) {
            res0 = (v16u8) __msa_aver_s_b(out0, src3);
            res1 = (v16u8) __msa_aver_s_b(out1, src4);
            res2 = (v16u8) __msa_aver_s_b(out2, src5);
            res3 = (v16u8) __msa_aver_s_b(out3, src6);
        } else {
            res0 = (v16u8) __msa_aver_s_b(out0, src2);
            res1 = (v16u8) __msa_aver_s_b(out1, src3);
            res2 = (v16u8) __msa_aver_s_b(out2, src4);
            res3 = (v16u8) __msa_aver_s_b(out3, src5);
        }

        XORI_B4_128_UB(res0, res1, res2, res3);
        AVER_UB4_UB(res0, dst0, res1, dst1, res2, dst2, res3, dst3,
                    dst0, dst1, dst2, dst3);
        ST_UB4(dst0, dst1, dst2, dst3, dst, dst_stride);
        dst += (4 * dst_stride);

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

static void avc_luma_mid_and_aver_dst_4x4_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v8i16 res0, res1, res2, res3;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 tmp0, tmp1, tmp2, tmp3;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    hz_out0 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                          mask0, mask1, mask2);
    hz_out2 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                          mask0, mask1, mask2);

    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);

    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);

    hz_out5 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                          mask0, mask1, mask2);
    hz_out7 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                          mask0, mask1, mask2);

    PCKOD_D2_SH(hz_out5, hz_out5, hz_out7, hz_out7, hz_out6, hz_out8);

    res0 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out0, hz_out1, hz_out2,
                                             hz_out3, hz_out4, hz_out5);
    res1 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out1, hz_out2, hz_out3,
                                             hz_out4, hz_out5, hz_out6);
    res2 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out2, hz_out3, hz_out4,
                                             hz_out5, hz_out6, hz_out7);
    res3 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out3, hz_out4, hz_out5,
                                             hz_out6, hz_out7, hz_out8);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    tmp0 = PCKEV_XORI128_UB(res0, res1);
    tmp1 = PCKEV_XORI128_UB(res2, res3);
    PCKEV_D2_UB(dst1, dst0, dst3, dst2, tmp2, tmp3);
    AVER_UB2_UB(tmp0, tmp2, tmp1, tmp3, tmp0, tmp1);

    ST4x4_UB(tmp0, tmp1, 0, 2, 0, 2, dst, dst_stride);
}

static void avc_luma_mid_and_aver_dst_8w_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v16u8 dst0, dst1, dst2, dst3;
    v8i16 res0, res1, res2, res3;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);

        hz_out5 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
        hz_out6 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
        hz_out7 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
        hz_out8 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);

        res0 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out0, hz_out1, hz_out2,
                                               hz_out3, hz_out4, hz_out5);
        res1 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out1, hz_out2, hz_out3,
                                               hz_out4, hz_out5, hz_out6);
        res2 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out2, hz_out3, hz_out4,
                                               hz_out5, hz_out6, hz_out7);
        res3 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out3, hz_out4, hz_out5,
                                               hz_out6, hz_out7, hz_out8);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        CONVERT_UB_AVG_ST8x4_UB(res0, res1, res2, res3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);

        dst += (4 * dst_stride);
        hz_out3 = hz_out7;
        hz_out1 = hz_out5;
        hz_out5 = hz_out4;
        hz_out4 = hz_out8;
        hz_out2 = hz_out6;
        hz_out0 = hz_out5;
    }
}

static void avc_luma_mid_and_aver_dst_16x16_msa(const uint8_t *src,
                                                int32_t src_stride,
                                                uint8_t *dst,
                                                int32_t dst_stride)
{
    avc_luma_mid_and_aver_dst_8w_msa(src, src_stride, dst, dst_stride, 16);
    avc_luma_mid_and_aver_dst_8w_msa(src + 8, src_stride, dst + 8, dst_stride,
                                     16);
}

static void avc_luma_midh_qrt_and_aver_dst_4w_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  int32_t height,
                                                  uint8_t horiz_offset)
{
    uint32_t row;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16u8 dst0, dst1, res;
    v8i16 vt_res0, vt_res1, vt_res2, vt_res3;
    v4i32 hz_res0, hz_res1;
    v8i16 res0, res1;
    v8i16 shf_vec0, shf_vec1, shf_vec2, shf_vec3, shf_vec4, shf_vec5;
    v8i16 mask0 = { 0, 5, 1, 6, 2, 7, 3, 8 };
    v8i16 mask1 = { 1, 4, 2, 5, 3, 6, 4, 7 };
    v8i16 mask2 = { 2, 3, 3, 4, 4, 5, 5, 6 };
    v8i16 minus5h = __msa_ldi_h(-5);
    v8i16 plus20h = __msa_ldi_h(20);
    v8i16 zeros = { 0 };

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    for (row = (height >> 1); row--;) {
        LD_SB2(src, src_stride, src5, src6);
        src += (2 * src_stride);

        XORI_B2_128_SB(src5, src6);
        LD_UB2(dst, dst_stride, dst0, dst1);

        dst0 = (v16u8) __msa_ilvr_w((v4i32) dst1, (v4i32) dst0);

        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src0, src1, src2, src3, src4, src5,
                                        vt_res0, vt_res1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src1, src2, src3, src4, src5, src6,
                                        vt_res2, vt_res3);
        VSHF_H3_SH(vt_res0, vt_res1, vt_res0, vt_res1, vt_res0, vt_res1,
                   mask0, mask1, mask2, shf_vec0, shf_vec1, shf_vec2);
        VSHF_H3_SH(vt_res2, vt_res3, vt_res2, vt_res3, vt_res2, vt_res3,
                   mask0, mask1, mask2, shf_vec3, shf_vec4, shf_vec5);

        hz_res0 = __msa_hadd_s_w(shf_vec0, shf_vec0);
        DPADD_SH2_SW(shf_vec1, shf_vec2, minus5h, plus20h, hz_res0, hz_res0);

        hz_res1 = __msa_hadd_s_w(shf_vec3, shf_vec3);
        DPADD_SH2_SW(shf_vec4, shf_vec5, minus5h, plus20h, hz_res1, hz_res1);

        SRARI_W2_SW(hz_res0, hz_res1, 10);
        SAT_SW2_SW(hz_res0, hz_res1, 7);

        res0 = __msa_srari_h(shf_vec2, 5);
        res1 = __msa_srari_h(shf_vec5, 5);

        SAT_SH2_SH(res0, res1, 7);

        if (horiz_offset) {
            res0 = __msa_ilvod_h(zeros, res0);
            res1 = __msa_ilvod_h(zeros, res1);
        } else {
            ILVEV_H2_SH(res0, zeros, res1, zeros, res0, res1);
        }
        hz_res0 = __msa_aver_s_w(hz_res0, (v4i32) res0);
        hz_res1 = __msa_aver_s_w(hz_res1, (v4i32) res1);
        res0 = __msa_pckev_h((v8i16) hz_res1, (v8i16) hz_res0);

        res = PCKEV_XORI128_UB(res0, res0);

        dst0 = __msa_aver_u_b(res, dst0);

        ST4x2_UB(dst0, dst, dst_stride);
        dst += (2 * dst_stride);

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
    }
}

static void avc_luma_midh_qrt_and_aver_dst_8w_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  int32_t height,
                                                  uint8_t horiz_offset)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_midh_qrt_and_aver_dst_4w_msa(src, src_stride, dst, dst_stride,
                                              height, horiz_offset);

        src += 4;
        dst += 4;
    }
}

static void avc_luma_midh_qrt_and_aver_dst_16w_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   int32_t height,
                                                   uint8_t horiz_offset)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        avc_luma_midh_qrt_and_aver_dst_4w_msa(src, src_stride, dst, dst_stride,
                                              height, horiz_offset);

        src += 4;
        dst += 4;
    }
}

static void avc_luma_midv_qrt_and_aver_dst_4w_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  int32_t height,
                                                  uint8_t ver_offset)
{
    int32_t loop_cnt;
    int32_t out0, out1;
    v16i8 src0, src1, src2, src3, src4;
    v16u8 dst0, dst1;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6;
    v8i16 res0, res1, res2, res3;
    v16u8 vec0, vec1;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    hz_out0 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                          mask0, mask1, mask2);
    hz_out2 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src2, src3,
                                                          mask0, mask1, mask2);

    PCKOD_D2_SH(hz_out0, hz_out0, hz_out2, hz_out2, hz_out1, hz_out3);

    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src1);
        src += (2 * src_stride);

        XORI_B2_128_SB(src0, src1);
        LD_UB2(dst, dst_stride, dst0, dst1);
        hz_out5 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src0, src1,
                                                              mask0, mask1,
                                                              mask2);
        hz_out6 = (v8i16) __msa_pckod_d((v2i64) hz_out5, (v2i64) hz_out5);
        res0 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out0, hz_out1, hz_out2,
                                                 hz_out3, hz_out4, hz_out5);
        res2 = AVC_CALC_DPADD_H_6PIX_2COEFF_R_SH(hz_out1, hz_out2, hz_out3,
                                                 hz_out4, hz_out5, hz_out6);

        if (ver_offset) {
            res1 = __msa_srari_h(hz_out3, 5);
            res3 = __msa_srari_h(hz_out4, 5);
        } else {
            res1 = __msa_srari_h(hz_out2, 5);
            res3 = __msa_srari_h(hz_out3, 5);
        }

        SAT_SH2_SH(res1, res3, 7);

        res0 = __msa_aver_s_h(res0, res1);
        res1 = __msa_aver_s_h(res2, res3);

        vec0 = PCKEV_XORI128_UB(res0, res0);
        vec1 = PCKEV_XORI128_UB(res1, res1);

        AVER_UB2_UB(vec0, dst0, vec1, dst1, dst0, dst1);

        out0 = __msa_copy_u_w((v4i32) dst0, 0);
        out1 = __msa_copy_u_w((v4i32) dst1, 0);
        SW(out0, dst);
        dst += dst_stride;
        SW(out1, dst);
        dst += dst_stride;

        hz_out0 = hz_out2;
        hz_out1 = hz_out3;
        hz_out2 = hz_out4;
        hz_out3 = hz_out5;
        hz_out4 = hz_out6;
    }
}

static void avc_luma_midv_qrt_and_aver_dst_8w_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  int32_t height,
                                                  uint8_t vert_offset)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 hz_out4, hz_out5, hz_out6, hz_out7, hz_out8;
    v8i16 res0, res1, res2, res3;
    v8i16 res4, res5, res6, res7;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    hz_out0 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
    hz_out1 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
    hz_out2 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
    hz_out3 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);
    hz_out4 = AVC_HORZ_FILTER_SH(src4, mask0, mask1, mask2);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        hz_out5 = AVC_HORZ_FILTER_SH(src0, mask0, mask1, mask2);
        hz_out6 = AVC_HORZ_FILTER_SH(src1, mask0, mask1, mask2);
        hz_out7 = AVC_HORZ_FILTER_SH(src2, mask0, mask1, mask2);
        hz_out8 = AVC_HORZ_FILTER_SH(src3, mask0, mask1, mask2);

        res0 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out0, hz_out1, hz_out2,
                                               hz_out3, hz_out4, hz_out5);
        res2 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out1, hz_out2, hz_out3,
                                               hz_out4, hz_out5, hz_out6);
        res4 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out2, hz_out3, hz_out4,
                                               hz_out5, hz_out6, hz_out7);
        res6 = AVC_CALC_DPADD_H_6PIX_2COEFF_SH(hz_out3, hz_out4, hz_out5,
                                               hz_out6, hz_out7, hz_out8);

        if (vert_offset) {
            res1 = __msa_srari_h(hz_out3, 5);
            res3 = __msa_srari_h(hz_out4, 5);
            res5 = __msa_srari_h(hz_out5, 5);
            res7 = __msa_srari_h(hz_out6, 5);
        } else {
            res1 = __msa_srari_h(hz_out2, 5);
            res3 = __msa_srari_h(hz_out3, 5);
            res5 = __msa_srari_h(hz_out4, 5);
            res7 = __msa_srari_h(hz_out5, 5);
        }

        SAT_SH4_SH(res1, res3, res5, res7, 7);

        res0 = __msa_aver_s_h(res0, res1);
        res1 = __msa_aver_s_h(res2, res3);
        res2 = __msa_aver_s_h(res4, res5);
        res3 = __msa_aver_s_h(res6, res7);

        CONVERT_UB_AVG_ST8x4_UB(res0, res1, res2, res3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out0 = hz_out4;
        hz_out1 = hz_out5;
        hz_out2 = hz_out6;
        hz_out3 = hz_out7;
        hz_out4 = hz_out8;
    }
}

static void avc_luma_midv_qrt_and_aver_dst_16w_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   int32_t height,
                                                   uint8_t vert_offset)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_midv_qrt_and_aver_dst_8w_msa(src, src_stride, dst, dst_stride,
                                              height, vert_offset);

        src += 8;
        dst += 8;
    }
}

static void avc_luma_hv_qrt_and_aver_dst_4x4_msa(const uint8_t *src_x,
                                                 const uint8_t *src_y,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride)
{
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3, src_vt4;
    v16i8 src_vt5, src_vt6, src_vt7, src_vt8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, vert_out0, vert_out1;
    v8i16 res0, res1;
    v16u8 res;

    LD_SB3(&luma_mask_arr[48], 16, mask0, mask1, mask2);
    LD_SB5(src_y, src_stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * src_stride);

    src_vt0 = (v16i8) __msa_insve_w((v4i32) src_vt0, 1, (v4i32) src_vt1);
    src_vt1 = (v16i8) __msa_insve_w((v4i32) src_vt1, 1, (v4i32) src_vt2);
    src_vt2 = (v16i8) __msa_insve_w((v4i32) src_vt2, 1, (v4i32) src_vt3);
    src_vt3 = (v16i8) __msa_insve_w((v4i32) src_vt3, 1, (v4i32) src_vt4);

    XORI_B4_128_SB(src_vt0, src_vt1, src_vt2, src_vt3);
    LD_SB4(src_x, src_stride, src_hz0, src_hz1, src_hz2, src_hz3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
    hz_out0 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src_hz0, src_hz1,
                                                          mask0, mask1, mask2);
    hz_out1 = AVC_XOR_VSHF_B_AND_APPLY_6TAP_HORIZ_FILT_SH(src_hz2, src_hz3,
                                                          mask0, mask1, mask2);
    SRARI_H2_SH(hz_out0, hz_out1, 5);
    SAT_SH2_SH(hz_out0, hz_out1, 7);
    LD_SB4(src_y, src_stride, src_vt5, src_vt6, src_vt7, src_vt8);

    src_vt4 = (v16i8) __msa_insve_w((v4i32) src_vt4, 1, (v4i32) src_vt5);
    src_vt5 = (v16i8) __msa_insve_w((v4i32) src_vt5, 1, (v4i32) src_vt6);
    src_vt6 = (v16i8) __msa_insve_w((v4i32) src_vt6, 1, (v4i32) src_vt7);
    src_vt7 = (v16i8) __msa_insve_w((v4i32) src_vt7, 1, (v4i32) src_vt8);

    XORI_B4_128_SB(src_vt4, src_vt5, src_vt6, src_vt7);

    /* filter calc */
    vert_out0 = AVC_CALC_DPADD_B_6PIX_2COEFF_R_SH(src_vt0, src_vt1, src_vt2,
                                                  src_vt3, src_vt4, src_vt5);
    vert_out1 = AVC_CALC_DPADD_B_6PIX_2COEFF_R_SH(src_vt2, src_vt3, src_vt4,
                                                  src_vt5, src_vt6, src_vt7);
    SRARI_H2_SH(vert_out0, vert_out1, 5);
    SAT_SH2_SH(vert_out0, vert_out1, 7);

    res1 = __msa_srari_h((hz_out1 + vert_out1), 1);
    res0 = __msa_srari_h((hz_out0 + vert_out0), 1);

    SAT_SH2_SH(res0, res1, 7);
    res = PCKEV_XORI128_UB(res0, res1);

    dst0 = (v16u8) __msa_insve_w((v4i32) dst0, 1, (v4i32) dst1);
    dst1 = (v16u8) __msa_insve_w((v4i32) dst2, 1, (v4i32) dst3);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst0 = __msa_aver_u_b(res, dst0);

    ST4x4_UB(dst0, dst0, 0, 1, 2, 3, dst, dst_stride);
}

static void avc_luma_hv_qrt_and_aver_dst_8x8_msa(const uint8_t *src_x,
                                                 const uint8_t *src_y,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride)
{
    uint32_t loop_cnt;
    v16i8 src_hz0, src_hz1, src_hz2, src_hz3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src_vt0, src_vt1, src_vt2, src_vt3;
    v16i8 src_vt4, src_vt5, src_vt6, src_vt7, src_vt8;
    v16i8 mask0, mask1, mask2;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3;
    v8i16 vert_out0, vert_out1, vert_out2, vert_out3;
    v8i16 out0, out1, out2, out3;

    LD_SB3(&luma_mask_arr[0], 16, mask0, mask1, mask2);

    LD_SB5(src_y, src_stride, src_vt0, src_vt1, src_vt2, src_vt3, src_vt4);
    src_y += (5 * src_stride);

    src_vt0 = (v16i8) __msa_insve_d((v2i64) src_vt0, 1, (v2i64) src_vt1);
    src_vt1 = (v16i8) __msa_insve_d((v2i64) src_vt1, 1, (v2i64) src_vt2);
    src_vt2 = (v16i8) __msa_insve_d((v2i64) src_vt2, 1, (v2i64) src_vt3);
    src_vt3 = (v16i8) __msa_insve_d((v2i64) src_vt3, 1, (v2i64) src_vt4);

    XORI_B4_128_SB(src_vt0, src_vt1, src_vt2, src_vt3);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB4(src_x, src_stride, src_hz0, src_hz1, src_hz2, src_hz3);
        XORI_B4_128_SB(src_hz0, src_hz1, src_hz2, src_hz3);
        src_x += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        hz_out0 = AVC_HORZ_FILTER_SH(src_hz0, mask0, mask1, mask2);
        hz_out1 = AVC_HORZ_FILTER_SH(src_hz1, mask0, mask1, mask2);
        hz_out2 = AVC_HORZ_FILTER_SH(src_hz2, mask0, mask1, mask2);
        hz_out3 = AVC_HORZ_FILTER_SH(src_hz3, mask0, mask1, mask2);
        SRARI_H4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 5);
        SAT_SH4_SH(hz_out0, hz_out1, hz_out2, hz_out3, 7);
        LD_SB4(src_y, src_stride, src_vt5, src_vt6, src_vt7, src_vt8);
        src_y += (4 * src_stride);

        src_vt4 = (v16i8) __msa_insve_d((v2i64) src_vt4, 1, (v2i64) src_vt5);
        src_vt5 = (v16i8) __msa_insve_d((v2i64) src_vt5, 1, (v2i64) src_vt6);
        src_vt6 = (v16i8) __msa_insve_d((v2i64) src_vt6, 1, (v2i64) src_vt7);
        src_vt7 = (v16i8) __msa_insve_d((v2i64) src_vt7, 1, (v2i64) src_vt8);

        XORI_B4_128_SB(src_vt4, src_vt5, src_vt6, src_vt7);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src_vt0, src_vt1, src_vt2, src_vt3,
                                        src_vt4, src_vt5, vert_out0, vert_out1);
        AVC_CALC_DPADD_B_6PIX_2COEFF_SH(src_vt2, src_vt3, src_vt4, src_vt5,
                                        src_vt6, src_vt7, vert_out2, vert_out3);
        SRARI_H4_SH(vert_out0, vert_out1, vert_out2, vert_out3, 5);
        SAT_SH4_SH(vert_out0, vert_out1, vert_out2, vert_out3, 7);

        out0 = __msa_srari_h((hz_out0 + vert_out0), 1);
        out1 = __msa_srari_h((hz_out1 + vert_out1), 1);
        out2 = __msa_srari_h((hz_out2 + vert_out2), 1);
        out3 = __msa_srari_h((hz_out3 + vert_out3), 1);

        SAT_SH4_SH(out0, out1, out2, out3, 7);
        CONVERT_UB_AVG_ST8x4_UB(out0, out1, out2, out3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);
        dst += (4 * dst_stride);

        src_vt0 = src_vt4;
        src_vt1 = src_vt5;
        src_vt2 = src_vt6;
        src_vt3 = src_vt7;
        src_vt4 = src_vt8;
    }
}

static void avc_luma_hv_qrt_and_aver_dst_16x16_msa(const uint8_t *src_x,
                                                   const uint8_t *src_y,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    uint32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_hv_qrt_and_aver_dst_8x8_msa(src_x, src_y, src_stride,
                                             dst, dst_stride);

        src_x += 8;
        src_y += 8;
        dst += 8;
    }

    src_x += (8 * src_stride) - 16;
    src_y += (8 * src_stride) - 16;
    dst += (8 * dst_stride) - 16;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        avc_luma_hv_qrt_and_aver_dst_8x8_msa(src_x, src_y, src_stride,
                                             dst, dst_stride);

        src_x += 8;
        src_y += 8;
        dst += 8;
    }
}

static void copy_width8_msa(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt;
    uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    if (0 == height % 12) {
        for (cnt = (height / 12); cnt--;) {
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

            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);

            out0 = __msa_copy_u_d((v2i64) src0, 0);
            out1 = __msa_copy_u_d((v2i64) src1, 0);
            out2 = __msa_copy_u_d((v2i64) src2, 0);
            out3 = __msa_copy_u_d((v2i64) src3, 0);

            SD4(out0, out1, out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == height % 8) {
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
    } else if (0 == height % 2) {
        for (cnt = (height / 2); cnt--;) {
            LD_UB2(src, src_stride, src0, src1);
            src += (2 * src_stride);
            out0 = __msa_copy_u_d((v2i64) src0, 0);
            out1 = __msa_copy_u_d((v2i64) src1, 0);

            SD(out0, dst);
            dst += dst_stride;
            SD(out1, dst);
            dst += dst_stride;
        }
    }
}

static void copy_16multx8mult_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  int32_t height, int32_t width)
{
    int32_t cnt, loop_cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
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

static void copy_width16_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    if (0 == height % 12) {
        for (cnt = (height / 12); cnt--;) {
            LD_UB8(src, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src += (8 * src_stride);
            ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7,
                   dst, dst_stride);
            dst += (8 * dst_stride);

            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);
            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == height % 8) {
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

static void avg_width4_msa(const uint8_t *src, int32_t src_stride,
                           uint8_t *dst, int32_t dst_stride,
                           int32_t height)
{
    int32_t cnt;
    uint32_t out0, out1, out2, out3;
    v16u8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3;

    if (0 == (height % 4)) {
        for (cnt = (height / 4); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);

            LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

            AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3,
                        dst0, dst1, dst2, dst3);

            out0 = __msa_copy_u_w((v4i32) dst0, 0);
            out1 = __msa_copy_u_w((v4i32) dst1, 0);
            out2 = __msa_copy_u_w((v4i32) dst2, 0);
            out3 = __msa_copy_u_w((v4i32) dst3, 0);
            SW4(out0, out1, out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == (height % 2)) {
        for (cnt = (height / 2); cnt--;) {
            LD_UB2(src, src_stride, src0, src1);
            src += (2 * src_stride);

            LD_UB2(dst, dst_stride, dst0, dst1);

            AVER_UB2_UB(src0, dst0, src1, dst1, dst0, dst1);

            out0 = __msa_copy_u_w((v4i32) dst0, 0);
            out1 = __msa_copy_u_w((v4i32) dst1, 0);
            SW(out0, dst);
            dst += dst_stride;
            SW(out1, dst);
            dst += dst_stride;
        }
    }
}

static void avg_width8_msa(const uint8_t *src, int32_t src_stride,
                           uint8_t *dst, int32_t dst_stride,
                           int32_t height)
{
    int32_t cnt;
    uint64_t out0, out1, out2, out3;
    v16u8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3;

    for (cnt = (height / 4); cnt--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3,
                    dst0, dst1, dst2, dst3);

        out0 = __msa_copy_u_d((v2i64) dst0, 0);
        out1 = __msa_copy_u_d((v2i64) dst1, 0);
        out2 = __msa_copy_u_d((v2i64) dst2, 0);
        out3 = __msa_copy_u_d((v2i64) dst3, 0);
        SD4(out0, out1, out2, out3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avg_width16_msa(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    for (cnt = (height / 8); cnt--;) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);
        LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);

        AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3,
                    dst0, dst1, dst2, dst3);
        AVER_UB4_UB(src4, dst4, src5, dst5, src6, dst6, src7, dst7,
                    dst4, dst5, dst6, dst7);
        ST_UB8(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

void ff_put_h264_qpel16_mc00_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    copy_width16_msa(src, stride, dst, stride, 16);
}

void ff_put_h264_qpel8_mc00_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    copy_width8_msa(src, stride, dst, stride, 8);
}

void ff_avg_h264_qpel16_mc00_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avg_width16_msa(src, stride, dst, stride, 16);
}

void ff_avg_h264_qpel8_mc00_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avg_width8_msa(src, stride, dst, stride, 8);
}

void ff_avg_h264_qpel4_mc00_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avg_width4_msa(src, stride, dst, stride, 4);
}

void ff_put_h264_qpel16_mc10_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hz_qrt_16w_msa(src - 2, stride, dst, stride, 16, 0);
}

void ff_put_h264_qpel16_mc30_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hz_qrt_16w_msa(src - 2, stride, dst, stride, 16, 1);
}

void ff_put_h264_qpel8_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_8w_msa(src - 2, stride, dst, stride, 8, 0);
}

void ff_put_h264_qpel8_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_8w_msa(src - 2, stride, dst, stride, 8, 1);
}

void ff_put_h264_qpel4_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_4w_msa(src - 2, stride, dst, stride, 4, 0);
}

void ff_put_h264_qpel4_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_4w_msa(src - 2, stride, dst, stride, 4, 1);
}

void ff_put_h264_qpel16_mc20_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hz_16w_msa(src - 2, stride, dst, stride, 16);
}

void ff_put_h264_qpel8_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_8w_msa(src - 2, stride, dst, stride, 8);
}

void ff_put_h264_qpel4_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_4w_msa(src - 2, stride, dst, stride, 4);
}

void ff_put_h264_qpel16_mc01_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_vt_qrt_16w_msa(src - (stride * 2), stride, dst, stride, 16, 0);
}

void ff_put_h264_qpel16_mc03_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_vt_qrt_16w_msa(src - (stride * 2), stride, dst, stride, 16, 1);
}

void ff_put_h264_qpel8_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_8w_msa(src - (stride * 2), stride, dst, stride, 8, 0);
}

void ff_put_h264_qpel8_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_8w_msa(src - (stride * 2), stride, dst, stride, 8, 1);
}

void ff_put_h264_qpel4_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_4w_msa(src - (stride * 2), stride, dst, stride, 4, 0);
}

void ff_put_h264_qpel4_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_4w_msa(src - (stride * 2), stride, dst, stride, 4, 1);
}

void ff_put_h264_qpel16_mc11_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16w_msa(src - 2,
                            src - (stride * 2), stride, dst, stride, 16);
}

void ff_put_h264_qpel16_mc31_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16w_msa(src - 2,
                            src - (stride * 2) +
                            sizeof(uint8_t), stride, dst, stride, 16);
}

void ff_put_h264_qpel16_mc13_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16w_msa(src + stride - 2,
                            src - (stride * 2), stride, dst, stride, 16);
}

void ff_put_h264_qpel16_mc33_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_16w_msa(src + stride - 2,
                            src - (stride * 2) +
                            sizeof(uint8_t), stride, dst, stride, 16);
}

void ff_put_h264_qpel8_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8w_msa(src - 2, src - (stride * 2), stride, dst, stride, 8);
}

void ff_put_h264_qpel8_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8w_msa(src - 2,
                           src - (stride * 2) +
                           sizeof(uint8_t), stride, dst, stride, 8);
}

void ff_put_h264_qpel8_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8w_msa(src + stride - 2,
                           src - (stride * 2), stride, dst, stride, 8);
}

void ff_put_h264_qpel8_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_8w_msa(src + stride - 2,
                           src - (stride * 2) +
                           sizeof(uint8_t), stride, dst, stride, 8);
}


void ff_put_h264_qpel4_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4w_msa(src - 2, src - (stride * 2), stride, dst, stride, 4);
}

void ff_put_h264_qpel4_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4w_msa(src - 2,
                           src - (stride * 2) +
                           sizeof(uint8_t), stride, dst, stride, 4);
}

void ff_put_h264_qpel4_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4w_msa(src + stride - 2,
                           src - (stride * 2), stride, dst, stride, 4);
}

void ff_put_h264_qpel4_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_4w_msa(src + stride - 2,
                           src - (stride * 2) +
                           sizeof(uint8_t), stride, dst, stride, 4);
}

void ff_put_h264_qpel16_mc21_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midv_qrt_16w_msa(src - (2 * stride) - 2,
                              stride, dst, stride, 16, 0);
}

void ff_put_h264_qpel16_mc23_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midv_qrt_16w_msa(src - (2 * stride) - 2,
                              stride, dst, stride, 16, 1);
}

void ff_put_h264_qpel8_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_8w_msa(src - (2 * stride) - 2, stride, dst, stride, 8, 0);
}

void ff_put_h264_qpel8_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_8w_msa(src - (2 * stride) - 2, stride, dst, stride, 8, 1);
}

void ff_put_h264_qpel4_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_4w_msa(src - (2 * stride) - 2, stride, dst, stride, 4, 0);
}

void ff_put_h264_qpel4_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_4w_msa(src - (2 * stride) - 2, stride, dst, stride, 4, 1);
}

void ff_put_h264_qpel16_mc02_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_vt_16w_msa(src - (stride * 2), stride, dst, stride, 16);
}

void ff_put_h264_qpel8_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_8w_msa(src - (stride * 2), stride, dst, stride, 8);
}

void ff_put_h264_qpel4_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_4w_msa(src - (stride * 2), stride, dst, stride, 4);
}

void ff_put_h264_qpel16_mc12_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midh_qrt_16w_msa(src - (2 * stride) - 2,
                              stride, dst, stride, 16, 0);
}

void ff_put_h264_qpel16_mc32_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midh_qrt_16w_msa(src - (2 * stride) - 2,
                              stride, dst, stride, 16, 1);
}

void ff_put_h264_qpel8_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_8w_msa(src - (2 * stride) - 2, stride, dst, stride, 8, 0);
}

void ff_put_h264_qpel8_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_8w_msa(src - (2 * stride) - 2, stride, dst, stride, 8, 1);
}

void ff_put_h264_qpel4_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_4w_msa(src - (2 * stride) - 2, stride, dst, stride, 4, 0);
}

void ff_put_h264_qpel4_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_4w_msa(src - (2 * stride) - 2, stride, dst, stride, 4, 1);
}

void ff_put_h264_qpel16_mc22_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_mid_16w_msa(src - (2 * stride) - 2, stride, dst, stride, 16);
}

void ff_put_h264_qpel8_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_mid_8w_msa(src - (2 * stride) - 2, stride, dst, stride, 8);
}

void ff_put_h264_qpel4_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_mid_4w_msa(src - (2 * stride) - 2, stride, dst, stride, 4);
}

void ff_avg_h264_qpel16_mc10_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hz_qrt_and_aver_dst_16x16_msa(src - 2, stride, dst, stride, 0);
}

void ff_avg_h264_qpel16_mc30_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hz_qrt_and_aver_dst_16x16_msa(src - 2, stride, dst, stride, 1);
}

void ff_avg_h264_qpel8_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_and_aver_dst_8x8_msa(src - 2, stride, dst, stride, 0);
}

void ff_avg_h264_qpel8_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_and_aver_dst_8x8_msa(src - 2, stride, dst, stride, 1);
}

void ff_avg_h264_qpel4_mc10_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_and_aver_dst_4x4_msa(src - 2, stride, dst, stride, 0);
}

void ff_avg_h264_qpel4_mc30_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_qrt_and_aver_dst_4x4_msa(src - 2, stride, dst, stride, 1);
}

void ff_avg_h264_qpel16_mc20_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hz_and_aver_dst_16x16_msa(src - 2, stride, dst, stride);
}

void ff_avg_h264_qpel8_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_and_aver_dst_8x8_msa(src - 2, stride, dst, stride);
}

void ff_avg_h264_qpel4_mc20_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hz_and_aver_dst_4x4_msa(src - 2, stride, dst, stride);
}

void ff_avg_h264_qpel16_mc01_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_vt_qrt_and_aver_dst_16x16_msa(src - (stride * 2),
                                           stride, dst, stride, 0);
}

void ff_avg_h264_qpel16_mc03_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_vt_qrt_and_aver_dst_16x16_msa(src - (stride * 2),
                                           stride, dst, stride, 1);
}

void ff_avg_h264_qpel8_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_and_aver_dst_8x8_msa(src - (stride * 2),
                                         stride, dst, stride, 0);
}

void ff_avg_h264_qpel8_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_and_aver_dst_8x8_msa(src - (stride * 2),
                                         stride, dst, stride, 1);
}

void ff_avg_h264_qpel4_mc01_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_and_aver_dst_4x4_msa(src - (stride * 2),
                                         stride, dst, stride, 0);
}

void ff_avg_h264_qpel4_mc03_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_qrt_and_aver_dst_4x4_msa(src - (stride * 2),
                                         stride, dst, stride, 1);
}

void ff_avg_h264_qpel16_mc11_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src - 2,
                                           src - (stride * 2),
                                           stride, dst, stride);
}

void ff_avg_h264_qpel16_mc31_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src - 2,
                                           src - (stride * 2) +
                                           sizeof(uint8_t), stride,
                                           dst, stride);
}

void ff_avg_h264_qpel16_mc13_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src + stride - 2,
                                           src - (stride * 2),
                                           stride, dst, stride);
}

void ff_avg_h264_qpel16_mc33_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_16x16_msa(src + stride - 2,
                                           src - (stride * 2) +
                                           sizeof(uint8_t), stride,
                                           dst, stride);
}

void ff_avg_h264_qpel8_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src - 2,
                                         src - (stride * 2),
                                         stride, dst, stride);
}

void ff_avg_h264_qpel8_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), stride, dst, stride);
}

void ff_avg_h264_qpel8_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src + stride - 2,
                                         src - (stride * 2),
                                         stride, dst, stride);
}

void ff_avg_h264_qpel8_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_8x8_msa(src + stride - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), stride, dst, stride);
}


void ff_avg_h264_qpel4_mc11_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src - 2,
                                         src - (stride * 2),
                                         stride, dst, stride);
}

void ff_avg_h264_qpel4_mc31_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), stride, dst, stride);
}

void ff_avg_h264_qpel4_mc13_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src + stride - 2,
                                         src - (stride * 2),
                                         stride, dst, stride);
}

void ff_avg_h264_qpel4_mc33_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_hv_qrt_and_aver_dst_4x4_msa(src + stride - 2,
                                         src - (stride * 2) +
                                         sizeof(uint8_t), stride, dst, stride);
}

void ff_avg_h264_qpel16_mc21_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midv_qrt_and_aver_dst_16w_msa(src - (2 * stride) - 2,
                                           stride, dst, stride, 16, 0);
}

void ff_avg_h264_qpel16_mc23_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midv_qrt_and_aver_dst_16w_msa(src - (2 * stride) - 2,
                                           stride, dst, stride, 16, 1);
}

void ff_avg_h264_qpel8_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_and_aver_dst_8w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 8, 0);
}

void ff_avg_h264_qpel8_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_and_aver_dst_8w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 8, 1);
}

void ff_avg_h264_qpel4_mc21_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_and_aver_dst_4w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 4, 0);
}

void ff_avg_h264_qpel4_mc23_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midv_qrt_and_aver_dst_4w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 4, 1);
}

void ff_avg_h264_qpel16_mc02_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_vt_and_aver_dst_16x16_msa(src - (stride * 2), stride, dst, stride);
}

void ff_avg_h264_qpel8_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_and_aver_dst_8x8_msa(src - (stride * 2), stride, dst, stride);
}

void ff_avg_h264_qpel4_mc02_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_vt_and_aver_dst_4x4_msa(src - (stride * 2), stride, dst, stride);
}

void ff_avg_h264_qpel16_mc12_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midh_qrt_and_aver_dst_16w_msa(src - (2 * stride) - 2,
                                           stride, dst, stride, 16, 0);
}

void ff_avg_h264_qpel16_mc32_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_midh_qrt_and_aver_dst_16w_msa(src - (2 * stride) - 2,
                                           stride, dst, stride, 16, 1);
}

void ff_avg_h264_qpel8_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_and_aver_dst_8w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 8, 0);
}

void ff_avg_h264_qpel8_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_and_aver_dst_8w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 8, 1);
}

void ff_avg_h264_qpel4_mc12_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_and_aver_dst_4w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 4, 0);
}

void ff_avg_h264_qpel4_mc32_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_midh_qrt_and_aver_dst_4w_msa(src - (2 * stride) - 2,
                                          stride, dst, stride, 4, 1);
}

void ff_avg_h264_qpel16_mc22_msa(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avc_luma_mid_and_aver_dst_16x16_msa(src - (2 * stride) - 2,
                                        stride, dst, stride);
}

void ff_avg_h264_qpel8_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_mid_and_aver_dst_8w_msa(src - (2 * stride) - 2,
                                     stride, dst, stride, 8);
}

void ff_avg_h264_qpel4_mc22_msa(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avc_luma_mid_and_aver_dst_4x4_msa(src - (2 * stride) - 2,
                                      stride, dst, stride);
}
