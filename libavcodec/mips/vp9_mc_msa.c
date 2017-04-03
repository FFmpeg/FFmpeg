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

#include "libavcodec/vp9dsp.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "vp9dsp_mips.h"

static const uint8_t mc_filt_mask_arr[16 * 3] = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
};

static const int8_t vp9_bilinear_filters_msa[15][2] = {
    {120, 8},
    {112, 16},
    {104, 24},
    {96, 32},
    {88, 40},
    {80, 48},
    {72, 56},
    {64, 64},
    {56, 72},
    {48, 80},
    {40, 88},
    {32, 96},
    {24, 104},
    {16, 112},
    {8, 120}
};

#define FILT_8TAP_DPADD_S_H(vec0, vec1, vec2, vec3,             \
                            filt0, filt1, filt2, filt3)         \
( {                                                             \
    v8i16 tmp0, tmp1;                                           \
                                                                \
    tmp0 = __msa_dotp_s_h((v16i8) vec0, (v16i8) filt0);         \
    tmp0 = __msa_dpadd_s_h(tmp0, (v16i8) vec1, (v16i8) filt1);  \
    tmp1 = __msa_dotp_s_h((v16i8) vec2, (v16i8) filt2);         \
    tmp1 = __msa_dpadd_s_h(tmp1, (v16i8) vec3, (v16i8) filt3);  \
    tmp0 = __msa_adds_s_h(tmp0, tmp1);                          \
                                                                \
    tmp0;                                                       \
} )

#define HORIZ_8TAP_FILT(src0, src1, mask0, mask1, mask2, mask3,          \
                        filt_h0, filt_h1, filt_h2, filt_h3)              \
( {                                                                      \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m;                                \
    v8i16 hz_out_m;                                                      \
                                                                         \
    VSHF_B4_SB(src0, src1, mask0, mask1, mask2, mask3,                   \
               vec0_m, vec1_m, vec2_m, vec3_m);                          \
    hz_out_m = FILT_8TAP_DPADD_S_H(vec0_m, vec1_m, vec2_m, vec3_m,       \
                                   filt_h0, filt_h1, filt_h2, filt_h3);  \
                                                                         \
    hz_out_m = __msa_srari_h(hz_out_m, 7);                               \
    hz_out_m = __msa_sat_s_h(hz_out_m, 7);                               \
                                                                         \
    hz_out_m;                                                            \
} )

#define HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3,                  \
                                   mask0, mask1, mask2, mask3,              \
                                   filt0, filt1, filt2, filt3,              \
                                   out0, out1)                              \
{                                                                           \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m,  vec4_m, vec5_m, vec6_m, vec7_m;  \
    v8i16 res0_m, res1_m, res2_m, res3_m;                                   \
                                                                            \
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0_m, vec1_m);       \
    DOTP_SB2_SH(vec0_m, vec1_m, filt0, filt0, res0_m, res1_m);              \
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2_m, vec3_m);       \
    DPADD_SB2_SH(vec2_m, vec3_m, filt1, filt1, res0_m, res1_m);             \
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4_m, vec5_m);       \
    DOTP_SB2_SH(vec4_m, vec5_m, filt2, filt2, res2_m, res3_m);              \
    VSHF_B2_SB(src0, src1, src2, src3, mask3, mask3, vec6_m, vec7_m);       \
    DPADD_SB2_SH(vec6_m, vec7_m, filt3, filt3, res2_m, res3_m);             \
    ADDS_SH2_SH(res0_m, res2_m, res1_m, res3_m, out0, out1);                \
}

#define HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3,                    \
                                   mask0, mask1, mask2, mask3,                \
                                   filt0, filt1, filt2, filt3,                \
                                   out0, out1, out2, out3)                    \
{                                                                             \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m, vec4_m, vec5_m, vec6_m, vec7_m;     \
    v8i16 res0_m, res1_m, res2_m, res3_m, res4_m, res5_m, res6_m, res7_m;     \
                                                                              \
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);         \
    DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,   \
                res0_m, res1_m, res2_m, res3_m);                              \
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2_m, vec3_m);         \
    DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt2, filt2, filt2, filt2,   \
                res4_m, res5_m, res6_m, res7_m);                              \
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4_m, vec5_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6_m, vec7_m);         \
    DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt1, filt1, filt1, filt1,  \
                 res0_m, res1_m, res2_m, res3_m);                             \
    VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec4_m, vec5_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec6_m, vec7_m);         \
    DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt3, filt3, filt3, filt3,  \
                 res4_m, res5_m, res6_m, res7_m);                             \
    ADDS_SH4_SH(res0_m, res4_m, res1_m, res5_m, res2_m, res6_m, res3_m,       \
                res7_m, out0, out1, out2, out3);                              \
}

#define PCKEV_XORI128_AVG_ST_UB(in0, in1, dst, pdst)  \
{                                                     \
    v16u8 tmp_m;                                      \
                                                      \
    tmp_m = PCKEV_XORI128_UB(in1, in0);               \
    tmp_m = __msa_aver_u_b(tmp_m, (v16u8) dst);       \
    ST_UB(tmp_m, (pdst));                             \
}

#define PCKEV_AVG_ST_UB(in0, in1, dst, pdst)                  \
{                                                             \
    v16u8 tmp_m;                                              \
                                                              \
    tmp_m = (v16u8) __msa_pckev_b((v16i8) in0, (v16i8) in1);  \
    tmp_m = __msa_aver_u_b(tmp_m, (v16u8) dst);               \
    ST_UB(tmp_m, (pdst));                                     \
}

#define PCKEV_AVG_ST8x4_UB(in1, dst0, in2, dst1, in3, dst2, in4, dst3,  \
                           pdst, stride)                                \
{                                                                       \
    v16u8 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                               \
    uint8_t *pdst_m = (uint8_t *) (pdst);                               \
                                                                        \
    PCKEV_B2_UB(in2, in1, in4, in3, tmp0_m, tmp1_m);                    \
    PCKEV_D2_UB(dst1, dst0, dst3, dst2, tmp2_m, tmp3_m);                \
    AVER_UB2_UB(tmp0_m, tmp2_m, tmp1_m, tmp3_m, tmp0_m, tmp1_m);        \
    ST8x4_UB(tmp0_m, tmp1_m, pdst_m, stride);                           \
}

static void common_hz_8t_4x4_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out0, out1);
    SRARI_H2_SH(out0, out1, 7);
    SAT_SH2_SH(out0, out1, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4x8_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3;
    v16u8 mask0, mask1, mask2, mask3, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out0, out1);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4x16_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  const int8_t *filter)
{
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out0, out1);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out0, out1);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out2, out3);

    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (4 == height) {
        common_hz_8t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_hz_8t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    } else if (16 == height) {
        common_hz_8t_4x16_msa(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_hz_8t_8x4_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, out0, out1,
                               out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 7);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    tmp0 = PCKEV_XORI128_UB(out0, out1);
    tmp1 = PCKEV_XORI128_UB(out2, out3);
    ST8x4_UB(tmp0, tmp1, dst, dst_stride);
}

static void common_hz_8t_8x8mult_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   mask3, filt0, filt1, filt2, filt3, out0,
                                   out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_8t_8w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (4 == height) {
        common_hz_8t_8x4_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_hz_8t_8x8mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    }
}

static void common_hz_8t_16w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 8, src_stride, src1, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (2 * src_stride);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   mask3, filt0, filt1, filt2, filt3, out0,
                                   out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst);
        dst += dst_stride;
    }
}

static void common_hz_8t_32w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src1 = __msa_sldi_b(src2, src0, 8);
        src += src_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   mask3, filt0, filt1, filt2, filt3, out0,
                                   out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);

        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src1 = __msa_sldi_b(src2, src0, 8);
        src += src_stride;

        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 16);
        dst += dst_stride;

        XORI_B4_128_SB(src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   mask3, filt0, filt1, filt2, filt3, out0,
                                   out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 16);
        dst += dst_stride;
    }
}

static void common_hz_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = height; loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src1 = __msa_sldi_b(src2, src0, 8);

        XORI_B4_128_SB(src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                                   mask2, mask3, filt0, filt1, filt2, filt3,
                                   out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 16);

        src0 = LD_SB(src + 32);
        src2 = LD_SB(src + 48);
        src3 = LD_SB(src + 56);
        src1 = __msa_sldi_b(src2, src0, 8);
        src += src_stride;

        XORI_B4_128_SB(src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                                   mask2, mask3, filt0, filt1, filt2, filt3,
                                   out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst + 32);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 48);
        dst += dst_stride;
    }
}

static void common_vt_8t_4w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src2110, src4332, src6554, src8776;
    v16i8 src10998, filt0, filt1, filt2, filt3;
    v16u8 out;
    v8i16 filt, out10, out32;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r, src32_r,
               src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVR_D3_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r, src2110,
               src4332, src6554);
    XORI_B3_128_SB(src2110, src4332, src6554);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        ILVR_D2_SB(src87_r, src76_r, src109_r, src98_r, src8776, src10998);
        XORI_B2_128_SB(src8776, src10998);
        out10 = FILT_8TAP_DPADD_S_H(src2110, src4332, src6554, src8776, filt0,
                                    filt1, filt2, filt3);
        out32 = FILT_8TAP_DPADD_S_H(src4332, src6554, src8776, src10998, filt0,
                                    filt1, filt2, filt3);
        SRARI_H2_SH(out10, out32, 7);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        src2110 = src6554;
        src4332 = src8776;
        src6554 = src10998;
        src6 = src10;
    }
}

static void common_vt_8t_8w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, filt0, filt1, filt2, filt3;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r, src32_r,
               src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        out0_r = FILT_8TAP_DPADD_S_H(src10_r, src32_r, src54_r, src76_r, filt0,
                                     filt1, filt2, filt3);
        out1_r = FILT_8TAP_DPADD_S_H(src21_r, src43_r, src65_r, src87_r, filt0,
                                     filt1, filt2, filt3);
        out2_r = FILT_8TAP_DPADD_S_H(src32_r, src54_r, src76_r, src98_r, filt0,
                                     filt1, filt2, filt3);
        out3_r = FILT_8TAP_DPADD_S_H(src43_r, src65_r, src87_r, src109_r, filt0,
                                     filt1, filt2, filt3);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        tmp0 = PCKEV_XORI128_UB(out0_r, out1_r);
        tmp1 = PCKEV_XORI128_UB(out2_r, out3_r);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;
        src6 = src10;
    }
}

static void common_vt_8t_16w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 filt0, filt1, filt2, filt3;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src10_l, src32_l, src54_l, src76_l;
    v16i8 src98_l, src21_l, src43_l, src65_l, src87_l, src109_l;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r, src32_r,
               src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_l, src32_l,
               src54_l, src21_l);
    ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_l,
                   src87_l, src98_l, src109_l);
        out0_r = FILT_8TAP_DPADD_S_H(src10_r, src32_r, src54_r, src76_r, filt0,
                                     filt1, filt2, filt3);
        out1_r = FILT_8TAP_DPADD_S_H(src21_r, src43_r, src65_r, src87_r, filt0,
                                     filt1, filt2, filt3);
        out2_r = FILT_8TAP_DPADD_S_H(src32_r, src54_r, src76_r, src98_r, filt0,
                                     filt1, filt2, filt3);
        out3_r = FILT_8TAP_DPADD_S_H(src43_r, src65_r, src87_r, src109_r, filt0,
                                     filt1, filt2, filt3);
        out0_l = FILT_8TAP_DPADD_S_H(src10_l, src32_l, src54_l, src76_l, filt0,
                                     filt1, filt2, filt3);
        out1_l = FILT_8TAP_DPADD_S_H(src21_l, src43_l, src65_l, src87_l, filt0,
                                     filt1, filt2, filt3);
        out2_l = FILT_8TAP_DPADD_S_H(src32_l, src54_l, src76_l, src98_l, filt0,
                                     filt1, filt2, filt3);
        out3_l = FILT_8TAP_DPADD_S_H(src43_l, src65_l, src87_l, src109_l, filt0,
                                     filt1, filt2, filt3);
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
        src54_r = src98_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;
        src10_l = src54_l;
        src32_l = src76_l;
        src54_l = src98_l;
        src21_l = src65_l;
        src43_l = src87_l;
        src65_l = src109_l;
        src6 = src10;
    }
}

static void common_vt_8t_16w_mult_msa(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter, int32_t height,
                                      int32_t width)
{
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt, cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 filt0, filt1, filt2, filt3;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src10_l, src32_l, src54_l, src76_l;
    v16i8 src98_l, src21_l, src43_l, src65_l, src87_l, src109_l;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);
        ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r,
                   src32_r, src54_r, src21_r);
        ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
        ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_l,
                   src32_l, src54_l, src21_l);
        ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src7, src8, src9, src10);
            XORI_B4_128_SB(src7, src8, src9, src10);
            src_tmp += (4 * src_stride);
            ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                       src87_r, src98_r, src109_r);
            ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_l,
                       src87_l, src98_l, src109_l);
            out0_r = FILT_8TAP_DPADD_S_H(src10_r, src32_r, src54_r, src76_r,
                                         filt0, filt1, filt2, filt3);
            out1_r = FILT_8TAP_DPADD_S_H(src21_r, src43_r, src65_r, src87_r,
                                         filt0, filt1, filt2, filt3);
            out2_r = FILT_8TAP_DPADD_S_H(src32_r, src54_r, src76_r, src98_r,
                                         filt0, filt1, filt2, filt3);
            out3_r = FILT_8TAP_DPADD_S_H(src43_r, src65_r, src87_r, src109_r,
                                         filt0, filt1, filt2, filt3);
            out0_l = FILT_8TAP_DPADD_S_H(src10_l, src32_l, src54_l, src76_l,
                                         filt0, filt1, filt2, filt3);
            out1_l = FILT_8TAP_DPADD_S_H(src21_l, src43_l, src65_l, src87_l,
                                         filt0, filt1, filt2, filt3);
            out2_l = FILT_8TAP_DPADD_S_H(src32_l, src54_l, src76_l, src98_l,
                                         filt0, filt1, filt2, filt3);
            out3_l = FILT_8TAP_DPADD_S_H(src43_l, src65_l, src87_l, src109_l,
                                         filt0, filt1, filt2, filt3);
            SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
            SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 7);
            SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
            SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
            PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                        out3_r, tmp0, tmp1, tmp2, tmp3);
            XORI_B4_128_UB(tmp0, tmp1, tmp2, tmp3);
            ST_UB4(tmp0, tmp1, tmp2, tmp3, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);

            src10_r = src54_r;
            src32_r = src76_r;
            src54_r = src98_r;
            src21_r = src65_r;
            src43_r = src87_r;
            src65_r = src109_r;
            src10_l = src54_l;
            src32_l = src76_l;
            src54_l = src98_l;
            src21_l = src65_l;
            src43_l = src87_l;
            src65_l = src109_l;
            src6 = src10;
        }

        src += 16;
        dst += 16;
    }
}

static void common_vt_8t_32w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              32);
}

static void common_vt_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              64);
}

static void common_hv_8ht_8vt_4w_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter_horiz,
                                     const int8_t *filter_vert,
                                     int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    v16u8 mask0, mask1, mask2, mask3, out;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, tmp0, tmp1, out0, out1, out2, out3, out4;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2, filt_vt3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= (3 + 3 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt_hz0, filt_hz1, filt_hz2, filt_hz3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    hz_out0 = HORIZ_8TAP_FILT(src0, src1, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out2 = HORIZ_8TAP_FILT(src2, src3, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out4 = HORIZ_8TAP_FILT(src4, src5, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out5 = HORIZ_8TAP_FILT(src5, src6, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    SLDI_B2_SH(hz_out2, hz_out4, hz_out0, hz_out2, hz_out1, hz_out3, 8);

    filt = LD_SH(filter_vert);
    SPLATI_H4_SH(filt, 0, 1, 2, 3, filt_vt0, filt_vt1, filt_vt2, filt_vt3);

    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);
    out2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        hz_out7 = HORIZ_8TAP_FILT(src7, src8, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        hz_out6 = (v8i16) __msa_sldi_b((v16i8) hz_out7, (v16i8) hz_out5, 8);
        out3 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp0 = FILT_8TAP_DPADD_S_H(out0, out1, out2, out3, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out9 = HORIZ_8TAP_FILT(src9, src10, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        hz_out8 = (v8i16) __msa_sldi_b((v16i8) hz_out9, (v16i8) hz_out7, 8);
        out4 = (v8i16) __msa_ilvev_b((v16i8) hz_out9, (v16i8) hz_out8);
        tmp1 = FILT_8TAP_DPADD_S_H(out1, out2, out3, out4, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        SRARI_H2_SH(tmp0, tmp1, 7);
        SAT_SH2_SH(tmp0, tmp1, 7);
        out = PCKEV_XORI128_UB(tmp0, tmp1);
        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out5 = hz_out9;
        out0 = out2;
        out1 = out3;
        out2 = out4;
    }
}

static void common_hv_8ht_8vt_8w_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter_horiz,
                                     const int8_t *filter_vert,
                                     int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    v16u8 mask0, mask1, mask2, mask3, vec0, vec1;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2, filt_vt3;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, tmp0, tmp1, tmp2, tmp3;
    v8i16 out0, out1, out2, out3, out4, out5, out6, out7, out8, out9;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= (3 + 3 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt_hz0, filt_hz1, filt_hz2, filt_hz3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    hz_out0 = HORIZ_8TAP_FILT(src0, src0, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out1 = HORIZ_8TAP_FILT(src1, src1, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out2 = HORIZ_8TAP_FILT(src2, src2, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out3 = HORIZ_8TAP_FILT(src3, src3, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out4 = HORIZ_8TAP_FILT(src4, src4, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out5 = HORIZ_8TAP_FILT(src5, src5, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out6 = HORIZ_8TAP_FILT(src6, src6, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);

    filt = LD_SH(filter_vert);
    SPLATI_H4_SH(filt, 0, 1, 2, 3, filt_vt0, filt_vt1, filt_vt2, filt_vt3);

    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);
    ILVEV_B2_SH(hz_out4, hz_out5, hz_out1, hz_out2, out2, out4);
    ILVEV_B2_SH(hz_out3, hz_out4, hz_out5, hz_out6, out5, out6);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B4_128_SB(src7, src8, src9, src10);

        hz_out7 = HORIZ_8TAP_FILT(src7, src7, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out3 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp0 = FILT_8TAP_DPADD_S_H(out0, out1, out2, out3, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out8 = HORIZ_8TAP_FILT(src8, src8, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out7 = (v8i16) __msa_ilvev_b((v16i8) hz_out8, (v16i8) hz_out7);
        tmp1 = FILT_8TAP_DPADD_S_H(out4, out5, out6, out7, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out9 = HORIZ_8TAP_FILT(src9, src9, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out8 = (v8i16) __msa_ilvev_b((v16i8) hz_out9, (v16i8) hz_out8);
        tmp2 = FILT_8TAP_DPADD_S_H(out1, out2, out3, out8, filt_vt0,
                                   filt_vt1, filt_vt2, filt_vt3);

        hz_out10 = HORIZ_8TAP_FILT(src10, src10, mask0, mask1, mask2, mask3,
                                   filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out9 = (v8i16) __msa_ilvev_b((v16i8) hz_out10, (v16i8) hz_out9);
        tmp3 = FILT_8TAP_DPADD_S_H(out5, out6, out7, out9, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        vec0 = PCKEV_XORI128_UB(tmp0, tmp1);
        vec1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST8x4_UB(vec0, vec1, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out6 = hz_out10;
        out0 = out2;
        out1 = out3;
        out2 = out8;
        out4 = out6;
        out5 = out7;
        out6 = out9;
    }
}

static void common_hv_8ht_8vt_16w_msa(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert,
                                      int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        common_hv_8ht_8vt_8w_msa(src, src_stride, dst, dst_stride, filter_horiz,
                                 filter_vert, height);

        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_32w_msa(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert,
                                      int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        common_hv_8ht_8vt_8w_msa(src, src_stride, dst, dst_stride, filter_horiz,
                                 filter_vert, height);

        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_64w_msa(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert,
                                      int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 8; multiple8_cnt--;) {
        common_hv_8ht_8vt_8w_msa(src, src_stride, dst, dst_stride, filter_horiz,
                                 filter_vert, height);

        src += 8;
        dst += 8;
    }
}

static void common_hz_8t_and_aver_dst_4x4_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 dst0, dst1, dst2, dst3, res2, res3;
    v16u8 mask0, mask1, mask2, mask3;
    v8i16 filt, res0, res1;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, res0, res1);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    SRARI_H2_SH(res0, res1, 7);
    SAT_SH2_SH(res0, res1, 7);
    PCKEV_B2_UB(res0, res0, res1, res1, res2, res3);
    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst2);
    XORI_B2_128_UB(res2, res3);
    AVER_UB2_UB(res2, dst0, res3, dst2, res2, res3);
    ST4x4_UB(res2, res3, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hz_8t_and_aver_dst_4x8_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, res0, res1, res2, res3;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filt, vec0, vec1, vec2, vec3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    src += (4 * src_stride);
    LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, vec0, vec1);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                               mask3, filt0, filt1, filt2, filt3, vec2, vec3);
    SRARI_H4_SH(vec0, vec1, vec2, vec3, 7);
    SAT_SH4_SH(vec0, vec1, vec2, vec3, 7);
    PCKEV_B4_UB(vec0, vec0, vec1, vec1, vec2, vec2, vec3, vec3,
                res0, res1, res2, res3);
    ILVR_D2_UB(res1, res0, res3, res2, res0, res2);
    XORI_B2_128_UB(res0, res2);
    ILVR_W4_UB(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
               dst0, dst2, dst4, dst6);
    ILVR_D2_UB(dst2, dst0, dst6, dst4, dst0, dst4);
    AVER_UB2_UB(res0, dst0, res2, dst4, res0, res2);
    ST4x8_UB(res0, res2, dst, dst_stride);
}

static void common_hz_8t_and_aver_dst_4w_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    if (4 == height) {
        common_hz_8t_and_aver_dst_4x4_msa(src, src_stride, dst, dst_stride,
                                          filter);
    } else if (8 == height) {
        common_hz_8t_and_aver_dst_4x8_msa(src, src_stride, dst, dst_stride,
                                          filter);
    }
}

static void common_hz_8t_and_aver_dst_8w_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, dst0, dst1, dst2, dst3;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                                   mask3, filt0, filt1, filt2, filt3, out0,
                                   out1, out2, out3);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        CONVERT_UB_AVG_ST8x4_UB(out0, out1, out2, out3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_8t_and_aver_dst_16w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, dst0, dst1;
    v8i16 filt, out0, out1, out2, out3;
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = height >> 1; loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 8, src_stride, src1, src3);
        src += (2 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B4_SH(src0, src0, mask0, mask1, mask2, mask3, vec0, vec4, vec8,
                   vec12);
        VSHF_B4_SH(src1, src1, mask0, mask1, mask2, mask3, vec1, vec5, vec9,
                   vec13);
        VSHF_B4_SH(src2, src2, mask0, mask1, mask2, mask3, vec2, vec6, vec10,
                   vec14);
        VSHF_B4_SH(src3, src3, mask0, mask1, mask2, mask3, vec3, vec7, vec11,
                   vec15);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec0,
                    vec1, vec2, vec3);
        DOTP_SB4_SH(vec8, vec9, vec10, vec11, filt2, filt2, filt2, filt2, vec8,
                    vec9, vec10, vec11);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1, vec0,
                     vec1, vec2, vec3);
        DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt3, filt3, filt3, filt3,
                     vec8, vec9, vec10, vec11);
        ADDS_SH4_SH(vec0, vec8, vec1, vec9, vec2, vec10, vec3, vec11, out0,
                    out1, out2, out3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        PCKEV_XORI128_AVG_ST_UB(out1, out0, dst0, dst);
        dst += dst_stride;
        PCKEV_XORI128_AVG_ST_UB(out3, out2, dst1, dst);
        dst += dst_stride;
    }
}

static void common_hz_8t_and_aver_dst_32w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 dst1, dst2, mask0, mask1, mask2, mask3;
    v8i16 filt, out0, out1, out2, out3;
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = height; loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src1 = __msa_sldi_b(src2, src0, 8);
        src += src_stride;

        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B4_SH(src0, src0, mask0, mask1, mask2, mask3, vec0, vec4, vec8,
                   vec12);
        VSHF_B4_SH(src1, src1, mask0, mask1, mask2, mask3, vec1, vec5, vec9,
                   vec13);
        VSHF_B4_SH(src2, src2, mask0, mask1, mask2, mask3, vec2, vec6, vec10,
                   vec14);
        VSHF_B4_SH(src3, src3, mask0, mask1, mask2, mask3, vec3, vec7, vec11,
                   vec15);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec0,
                    vec1, vec2, vec3);
        DOTP_SB4_SH(vec8, vec9, vec10, vec11, filt2, filt2, filt2, filt2, vec8,
                    vec9, vec10, vec11);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1, vec0,
                     vec1, vec2, vec3);
        DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt3, filt3, filt3, filt3,
                     vec8, vec9, vec10, vec11);
        ADDS_SH4_SH(vec0, vec8, vec1, vec9, vec2, vec10, vec3, vec11, out0,
                    out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        LD_UB2(dst, 16, dst1, dst2);
        PCKEV_XORI128_AVG_ST_UB(out1, out0, dst1, dst);
        PCKEV_XORI128_AVG_ST_UB(out3, out2, dst2, dst + 16);
        dst += dst_stride;
    }
}

static void common_hz_8t_and_aver_dst_64w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    uint32_t loop_cnt, cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 dst1, dst2, mask0, mask1, mask2, mask3;
    v8i16 filt, out0, out1, out2, out3;
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = height; loop_cnt--;) {
        for (cnt = 0; cnt < 2; ++cnt) {
            src0 = LD_SB(&src[cnt << 5]);
            src2 = LD_SB(&src[16 + (cnt << 5)]);
            src3 = LD_SB(&src[24 + (cnt << 5)]);
            src1 = __msa_sldi_b(src2, src0, 8);

            XORI_B4_128_SB(src0, src1, src2, src3);
            VSHF_B4_SH(src0, src0, mask0, mask1, mask2, mask3, vec0, vec4, vec8,
                       vec12);
            VSHF_B4_SH(src1, src1, mask0, mask1, mask2, mask3, vec1, vec5, vec9,
                       vec13);
            VSHF_B4_SH(src2, src2, mask0, mask1, mask2, mask3, vec2, vec6,
                       vec10, vec14);
            VSHF_B4_SH(src3, src3, mask0, mask1, mask2, mask3, vec3, vec7,
                       vec11, vec15);
            DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                        vec0, vec1, vec2, vec3);
            DOTP_SB4_SH(vec8, vec9, vec10, vec11, filt2, filt2, filt2, filt2,
                        vec8, vec9, vec10, vec11);
            DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1,
                         vec0, vec1, vec2, vec3);
            DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt3, filt3, filt3, filt3,
                         vec8, vec9, vec10, vec11);
            ADDS_SH4_SH(vec0, vec8, vec1, vec9, vec2, vec10, vec3, vec11, out0,
                        out1, out2, out3);
            SRARI_H4_SH(out0, out1, out2, out3, 7);
            SAT_SH4_SH(out0, out1, out2, out3, 7);
            LD_UB2(&dst[cnt << 5], 16, dst1, dst2);
            PCKEV_XORI128_AVG_ST_UB(out1, out0, dst1, &dst[cnt << 5]);
            PCKEV_XORI128_AVG_ST_UB(out3, out2, dst2, &dst[16 + (cnt << 5)]);
        }

        src += src_stride;
        dst += dst_stride;
    }
}

static void common_vt_8t_and_aver_dst_4w_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16u8 dst0, dst1, dst2, dst3, out;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src2110, src4332, src6554, src8776;
    v16i8 src10998, filt0, filt1, filt2, filt3;
    v8i16 filt, out10, out32;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r, src32_r,
               src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVR_D3_SB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r, src2110,
               src4332, src6554);
    XORI_B3_128_SB(src2110, src4332, src6554);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        ILVR_D2_SB(src87_r, src76_r, src109_r, src98_r, src8776, src10998);
        XORI_B2_128_SB(src8776, src10998);
        out10 = FILT_8TAP_DPADD_S_H(src2110, src4332, src6554, src8776, filt0,
                                    filt1, filt2, filt3);
        out32 = FILT_8TAP_DPADD_S_H(src4332, src6554, src8776, src10998, filt0,
                                    filt1, filt2, filt3);
        SRARI_H2_SH(out10, out32, 7);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst2);

        dst0 = (v16u8) __msa_ilvr_d((v2i64) dst2, (v2i64) dst0);
        out = __msa_aver_u_b(out, dst0);

        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);

        src2110 = src6554;
        src4332 = src8776;
        src6554 = src10998;
        src6 = src10;
    }
}

static void common_vt_8t_and_aver_dst_8w_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r, src32_r,
               src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        XORI_B4_128_SB(src7, src8, src9, src10);
        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        out0 = FILT_8TAP_DPADD_S_H(src10_r, src32_r, src54_r, src76_r, filt0,
                                   filt1, filt2, filt3);
        out1 = FILT_8TAP_DPADD_S_H(src21_r, src43_r, src65_r, src87_r, filt0,
                                   filt1, filt2, filt3);
        out2 = FILT_8TAP_DPADD_S_H(src32_r, src54_r, src76_r, src98_r, filt0,
                                   filt1, filt2, filt3);
        out3 = FILT_8TAP_DPADD_S_H(src43_r, src65_r, src87_r, src109_r, filt0,
                                   filt1, filt2, filt3);
        SRARI_H4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        CONVERT_UB_AVG_ST8x4_UB(out0, out1, out2, out3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;
        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;
        src6 = src10;
    }
}

static void common_vt_8t_and_aver_dst_16w_mult_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter,
                                                   int32_t height,
                                                   int32_t width)
{
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    uint32_t loop_cnt, cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src10_l, src32_l, src54_l, src76_l;
    v16i8 src98_l, src21_l, src43_l, src65_l, src87_l, src109_l;
    v16i8 filt0, filt1, filt2, filt3;
    v16u8 dst0, dst1, dst2, dst3, tmp0, tmp1, tmp2, tmp3;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l, filt;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);

        ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r,
                   src32_r, src54_r, src21_r);
        ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
        ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_l,
                   src32_l, src54_l, src21_l);
        ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src7, src8, src9, src10);
            src_tmp += (4 * src_stride);

            LD_UB4(dst_tmp, dst_stride, dst0, dst1, dst2, dst3);
            XORI_B4_128_SB(src7, src8, src9, src10);
            ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                       src87_r, src98_r, src109_r);
            ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_l,
                       src87_l, src98_l, src109_l);
            out0_r = FILT_8TAP_DPADD_S_H(src10_r, src32_r, src54_r, src76_r,
                                         filt0, filt1, filt2, filt3);
            out1_r = FILT_8TAP_DPADD_S_H(src21_r, src43_r, src65_r, src87_r,
                                         filt0, filt1, filt2, filt3);
            out2_r = FILT_8TAP_DPADD_S_H(src32_r, src54_r, src76_r, src98_r,
                                         filt0, filt1, filt2, filt3);
            out3_r = FILT_8TAP_DPADD_S_H(src43_r, src65_r, src87_r, src109_r,
                                         filt0, filt1, filt2, filt3);
            out0_l = FILT_8TAP_DPADD_S_H(src10_l, src32_l, src54_l, src76_l,
                                         filt0, filt1, filt2, filt3);
            out1_l = FILT_8TAP_DPADD_S_H(src21_l, src43_l, src65_l, src87_l,
                                         filt0, filt1, filt2, filt3);
            out2_l = FILT_8TAP_DPADD_S_H(src32_l, src54_l, src76_l, src98_l,
                                         filt0, filt1, filt2, filt3);
            out3_l = FILT_8TAP_DPADD_S_H(src43_l, src65_l, src87_l, src109_l,
                                         filt0, filt1, filt2, filt3);
            SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 7);
            SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 7);
            SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
            SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
            PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                        out3_r, tmp0, tmp1, tmp2, tmp3);
            XORI_B4_128_UB(tmp0, tmp1, tmp2, tmp3);
            AVER_UB4_UB(tmp0, dst0, tmp1, dst1, tmp2, dst2, tmp3, dst3,
                        dst0, dst1, dst2, dst3);
            ST_UB4(dst0, dst1, dst2, dst3, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);

            src10_r = src54_r;
            src32_r = src76_r;
            src54_r = src98_r;
            src21_r = src65_r;
            src43_r = src87_r;
            src65_r = src109_r;
            src10_l = src54_l;
            src32_l = src76_l;
            src54_l = src98_l;
            src21_l = src65_l;
            src43_l = src87_l;
            src65_l = src109_l;
            src6 = src10;
        }

        src += 16;
        dst += 16;
    }
}

static void common_vt_8t_and_aver_dst_16w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    common_vt_8t_and_aver_dst_16w_mult_msa(src, src_stride, dst, dst_stride,
                                           filter, height, 16);
}

static void common_vt_8t_and_aver_dst_32w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    common_vt_8t_and_aver_dst_16w_mult_msa(src, src_stride, dst, dst_stride,
                                           filter, height, 32);
}

static void common_vt_8t_and_aver_dst_64w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    common_vt_8t_and_aver_dst_16w_mult_msa(src, src_stride, dst, dst_stride,
                                           filter, height, 64);
}

static void common_hv_8ht_8vt_and_aver_dst_4w_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  const int8_t *filter_horiz,
                                                  const int8_t *filter_vert,
                                                  int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16u8 dst0, dst1, dst2, dst3, mask0, mask1, mask2, mask3, tmp0, tmp1;
    v16i8 filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, res0, res1, vec0, vec1, vec2, vec3, vec4;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2, filt_vt3;

    mask0 = LD_UB(&mc_filt_mask_arr[16]);
    src -= (3 + 3 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt_hz0, filt_hz1, filt_hz2, filt_hz3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    hz_out0 = HORIZ_8TAP_FILT(src0, src1, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out2 = HORIZ_8TAP_FILT(src2, src3, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out4 = HORIZ_8TAP_FILT(src4, src5, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out5 = HORIZ_8TAP_FILT(src5, src6, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    SLDI_B2_SH(hz_out2, hz_out4, hz_out0, hz_out2, hz_out1, hz_out3, 8);

    filt = LD_SH(filter_vert);
    SPLATI_H4_SH(filt, 0, 1, 2, 3, filt_vt0, filt_vt1, filt_vt2, filt_vt3);

    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
    vec2 = (v8i16) __msa_ilvev_b((v16i8) hz_out5, (v16i8) hz_out4);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        hz_out7 = HORIZ_8TAP_FILT(src7, src8, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        hz_out6 = (v8i16) __msa_sldi_b((v16i8) hz_out7, (v16i8) hz_out5, 8);
        vec3 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        res0 = FILT_8TAP_DPADD_S_H(vec0, vec1, vec2, vec3, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out9 = HORIZ_8TAP_FILT(src9, src10, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        hz_out8 = (v8i16) __msa_sldi_b((v16i8) hz_out9, (v16i8) hz_out7, 8);
        vec4 = (v8i16) __msa_ilvev_b((v16i8) hz_out9, (v16i8) hz_out8);
        res1 = FILT_8TAP_DPADD_S_H(vec1, vec2, vec3, vec4, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst2);

        SRARI_H2_SH(res0, res1, 7);
        SAT_SH2_SH(res0, res1, 7);
        PCKEV_B2_UB(res0, res0, res1, res1, tmp0, tmp1);
        XORI_B2_128_UB(tmp0, tmp1);
        AVER_UB2_UB(tmp0, dst0, tmp1, dst2, tmp0, tmp1);
        ST4x4_UB(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out5 = hz_out9;
        vec0 = vec2;
        vec1 = vec3;
        vec2 = vec4;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_8w_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  const int8_t *filter_horiz,
                                                  const int8_t *filter_vert,
                                                  int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    v8i16 filt, filt_vt0, filt_vt1, filt_vt2, filt_vt3;
    v16u8 dst0, dst1, dst2, dst3, mask0, mask1, mask2, mask3;
    v8i16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8i16 hz_out7, hz_out8, hz_out9, hz_out10, tmp0, tmp1, tmp2, tmp3;
    v8i16 out0, out1, out2, out3, out4, out5, out6, out7, out8, out9;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= (3 + 3 * src_stride);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt_hz0, filt_hz1, filt_hz2, filt_hz3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);
    hz_out0 = HORIZ_8TAP_FILT(src0, src0, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out1 = HORIZ_8TAP_FILT(src1, src1, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out2 = HORIZ_8TAP_FILT(src2, src2, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out3 = HORIZ_8TAP_FILT(src3, src3, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out4 = HORIZ_8TAP_FILT(src4, src4, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out5 = HORIZ_8TAP_FILT(src5, src5, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);
    hz_out6 = HORIZ_8TAP_FILT(src6, src6, mask0, mask1, mask2, mask3, filt_hz0,
                              filt_hz1, filt_hz2, filt_hz3);

    filt = LD_SH(filter_vert);
    SPLATI_H4_SH(filt, 0, 1, 2, 3, filt_vt0, filt_vt1, filt_vt2, filt_vt3);

    ILVEV_B2_SH(hz_out0, hz_out1, hz_out2, hz_out3, out0, out1);
    ILVEV_B2_SH(hz_out4, hz_out5, hz_out1, hz_out2, out2, out4);
    ILVEV_B2_SH(hz_out3, hz_out4, hz_out5, hz_out6, out5, out6);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        hz_out7 = HORIZ_8TAP_FILT(src7, src7, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out3 = (v8i16) __msa_ilvev_b((v16i8) hz_out7, (v16i8) hz_out6);
        tmp0 = FILT_8TAP_DPADD_S_H(out0, out1, out2, out3, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out8 = HORIZ_8TAP_FILT(src8, src8, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out7 = (v8i16) __msa_ilvev_b((v16i8) hz_out8, (v16i8) hz_out7);
        tmp1 = FILT_8TAP_DPADD_S_H(out4, out5, out6, out7, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out9 = HORIZ_8TAP_FILT(src9, src9, mask0, mask1, mask2, mask3,
                                  filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out8 = (v8i16) __msa_ilvev_b((v16i8) hz_out9, (v16i8) hz_out8);
        tmp2 = FILT_8TAP_DPADD_S_H(out1, out2, out3, out8, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        hz_out10 = HORIZ_8TAP_FILT(src10, src10, mask0, mask1, mask2, mask3,
                                   filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        out9 = (v8i16) __msa_ilvev_b((v16i8) hz_out10, (v16i8) hz_out9);
        tmp3 = FILT_8TAP_DPADD_S_H(out5, out6, out7, out9, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);

        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        CONVERT_UB_AVG_ST8x4_UB(tmp0, tmp1, tmp2, tmp3, dst0, dst1, dst2, dst3,
                                dst, dst_stride);
        dst += (4 * dst_stride);

        hz_out6 = hz_out10;
        out0 = out2;
        out1 = out3;
        out2 = out8;
        out4 = out6;
        out5 = out7;
        out6 = out9;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_16w_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert,
                                                   int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        common_hv_8ht_8vt_and_aver_dst_8w_msa(src, src_stride, dst, dst_stride,
                                              filter_horiz, filter_vert,
                                              height);

        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_32w_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert,
                                                   int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        common_hv_8ht_8vt_and_aver_dst_8w_msa(src, src_stride, dst, dst_stride,
                                              filter_horiz, filter_vert,
                                              height);

        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_64w_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert,
                                                   int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 8; multiple8_cnt--;) {
        common_hv_8ht_8vt_and_aver_dst_8w_msa(src, src_stride, dst, dst_stride,
                                              filter_horiz, filter_vert,
                                              height);

        src += 8;
        dst += 8;
    }
}

static void common_hz_2t_4x4_msa(const uint8_t *src, int32_t src_stride,
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
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hz_2t_4x8_msa(const uint8_t *src, int32_t src_stride,
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
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);
    ST4x4_UB(res2, res3, 0, 1, 0, 1, dst, dst_stride);
}

void ff_put_bilin_4h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_2t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_hz_2t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_hz_2t_8x4_msa(const uint8_t *src, int32_t src_stride,
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
    ST8x4_UB(src0, src1, dst, dst_stride);
}

static void common_hz_2t_8x8mult_msa(const uint8_t *src, int32_t src_stride,
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
    ST8x4_UB(out0, out1, dst, dst_stride);
    dst += (4 * dst_stride);

    VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                vec0, vec1, vec2, vec3);
    SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
    PCKEV_B2_SB(vec1, vec0, vec3, vec2, out0, out1);
    ST8x4_UB(out0, out1, dst, dst_stride);
    dst += (4 * dst_stride);

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
        ST8x4_UB(out0, out1, dst, dst_stride);

        VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    vec0, vec1, vec2, vec3);
        SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
        PCKEV_B2_SB(vec1, vec0, vec3, vec2, out0, out1);
        ST8x4_UB(out0, out1, dst + 4 * dst_stride, dst_stride);
    }
}

void ff_put_bilin_8h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_2t_8x4_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_hz_2t_8x8mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    }
}

void ff_put_bilin_16h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];
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

void ff_put_bilin_32h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 out0, out1, out2, out3, out4, out5, out6, out7, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    for (loop_cnt = height >> 1; loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src1 = __msa_sldi_b(src2, src0, 8);
        src += src_stride;
        src4 = LD_SB(src);
        src6 = LD_SB(src + 16);
        src7 = LD_SB(src + 24);
        src5 = __msa_sldi_b(src6, src4, 8);
        src += src_stride;

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
        PCKEV_ST_SB(out2, out3, dst + 16);
        dst += dst_stride;
        PCKEV_ST_SB(out4, out5, dst);
        PCKEV_ST_SB(out6, out7, dst + 16);
        dst += dst_stride;
    }
}

void ff_put_bilin_64h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 out0, out1, out2, out3, out4, out5, out6, out7, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    for (loop_cnt = height; loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src4 = LD_SB(src + 32);
        src6 = LD_SB(src + 48);
        src7 = LD_SB(src + 56);
        SLDI_B3_SB(src2, src4, src6, src0, src2, src4, src1, src3, src5, 8);
        src += src_stride;

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
        PCKEV_ST_SB(out2, out3, dst + 16);
        PCKEV_ST_SB(out4, out5, dst + 32);
        PCKEV_ST_SB(out6, out7, dst + 48);
        dst += dst_stride;
    }
}

static void common_vt_2t_4x4_msa(const uint8_t *src, int32_t src_stride,
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
    ST4x4_UB(src2110, src2110, 0, 1, 2, 3, dst, dst_stride);
}

static void common_vt_2t_4x8_msa(const uint8_t *src, int32_t src_stride,
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
    ST4x4_UB(src2110, src2110, 0, 1, 2, 3, dst, dst_stride);
    ST4x4_UB(src4332, src4332, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
}

void ff_put_bilin_4v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_vt_2t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_vt_2t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_vt_2t_8x4_msa(const uint8_t *src, int32_t src_stride,
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
    ST8x4_UB(out0, out1, dst, dst_stride);
}

static void common_vt_2t_8x8mult_msa(const uint8_t *src, int32_t src_stride,
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
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);

        DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_B2_SB(tmp1, tmp0, tmp3, tmp2, out0, out1);
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);

        src0 = src8;
    }
}

void ff_put_bilin_8v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_vt_2t_8x4_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_vt_2t_8x8mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    }
}

void ff_put_bilin_16v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];
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

void ff_put_bilin_32v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, filt0;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    src0 = LD_UB(src);
    src5 = LD_UB(src + 16);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        ILVR_B2_UB(src1, src0, src2, src1, vec0, vec2);
        ILVL_B2_UB(src1, src0, src2, src1, vec1, vec3);

        LD_UB4(src + 16, src_stride, src6, src7, src8, src9);
        src += (4 * src_stride);

        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst);
        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst + dst_stride);

        ILVR_B2_UB(src3, src2, src4, src3, vec4, vec6);
        ILVL_B2_UB(src3, src2, src4, src3, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst + 2 * dst_stride);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst + 3 * dst_stride);

        ILVR_B2_UB(src6, src5, src7, src6, vec0, vec2);
        ILVL_B2_UB(src6, src5, src7, src6, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst + 16);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst + 16 + dst_stride);

        ILVR_B2_UB(src8, src7, src9, src8, vec4, vec6);
        ILVL_B2_UB(src8, src7, src9, src8, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst + 16 + 2 * dst_stride);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst + 16 + 3 * dst_stride);
        dst += (4 * dst_stride);

        src0 = src4;
        src5 = src9;
    }
}

void ff_put_bilin_64v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16u8 src11, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, filt0;
    v8u16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 filt;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_UB4(src, 16, src0, src3, src6, src9);
    src += src_stride;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_UB2(src, src_stride, src1, src2);
        LD_UB2(src + 16, src_stride, src4, src5);
        LD_UB2(src + 32, src_stride, src7, src8);
        LD_UB2(src + 48, src_stride, src10, src11);
        src += (2 * src_stride);

        ILVR_B2_UB(src1, src0, src2, src1, vec0, vec2);
        ILVL_B2_UB(src1, src0, src2, src1, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst + dst_stride);

        ILVR_B2_UB(src4, src3, src5, src4, vec4, vec6);
        ILVL_B2_UB(src4, src3, src5, src4, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp4, tmp5);
        SRARI_H2_UH(tmp4, tmp5, 7);
        SAT_UH2_UH(tmp4, tmp5, 7);
        PCKEV_ST_SB(tmp4, tmp5, dst + 16);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp6, tmp7);
        SRARI_H2_UH(tmp6, tmp7, 7);
        SAT_UH2_UH(tmp6, tmp7, 7);
        PCKEV_ST_SB(tmp6, tmp7, dst + 16 + dst_stride);

        ILVR_B2_UB(src7, src6, src8, src7, vec0, vec2);
        ILVL_B2_UB(src7, src6, src8, src7, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_ST_SB(tmp0, tmp1, dst + 32);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_ST_SB(tmp2, tmp3, dst + 32 + dst_stride);

        ILVR_B2_UB(src10, src9, src11, src10, vec4, vec6);
        ILVL_B2_UB(src10, src9, src11, src10, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp4, tmp5);
        SRARI_H2_UH(tmp4, tmp5, 7);
        SAT_UH2_UH(tmp4, tmp5, 7);
        PCKEV_ST_SB(tmp4, tmp5, dst + 48);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp6, tmp7);
        SRARI_H2_UH(tmp6, tmp7, 7);
        SAT_UH2_UH(tmp6, tmp7, 7);
        PCKEV_ST_SB(tmp6, tmp7, dst + 48 + dst_stride);
        dst += (2 * dst_stride);

        src0 = src2;
        src3 = src5;
        src6 = src8;
        src9 = src11;
    }
}

static void common_hv_2ht_2vt_4x4_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_horiz, const int8_t *filter_vert)
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
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hv_2ht_2vt_4x8_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_horiz, const int8_t *filter_vert)
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
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);
    ST4x4_UB(res2, res3, 0, 1, 0, 1, dst, dst_stride);
}

void ff_put_bilin_4hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    const int8_t *filter_horiz = vp9_bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_hv_2ht_2vt_4x4_msa(src, src_stride, dst, dst_stride,
                                  filter_horiz, filter_vert);
    } else if (8 == height) {
        common_hv_2ht_2vt_4x8_msa(src, src_stride, dst, dst_stride,
                                  filter_horiz, filter_vert);
    }
}

static void common_hv_2ht_2vt_8x4_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const int8_t *filter_horiz, const int8_t *filter_vert)
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
    ST8x4_UB(out0, out1, dst, dst_stride);
}

static void common_hv_2ht_2vt_8x8mult_msa(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   const int8_t *filter_horiz, const int8_t *filter_vert,
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
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);

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
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_put_bilin_8hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    const int8_t *filter_horiz = vp9_bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_hv_2ht_2vt_8x4_msa(src, src_stride, dst, dst_stride,
                                  filter_horiz, filter_vert);
    } else {
        common_hv_2ht_2vt_8x8mult_msa(src, src_stride, dst, dst_stride,
                                      filter_horiz, filter_vert, height);
    }
}

void ff_put_bilin_16hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src, ptrdiff_t src_stride,
                           int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = vp9_bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = vp9_bilinear_filters_msa[my - 1];
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

void ff_put_bilin_32hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src, ptrdiff_t src_stride,
                           int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_put_bilin_16hv_msa(dst, dst_stride, src, src_stride, height, mx, my);

        src += 16;
        dst += 16;
    }
}

void ff_put_bilin_64hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src, ptrdiff_t src_stride,
                           int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        ff_put_bilin_16hv_msa(dst, dst_stride, src, src_stride, height, mx, my);

        src += 16;
        dst += 16;
    }
}

static void common_hz_2t_and_aver_dst_4x4_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, mask;
    v16u8 filt0, dst0, dst1, dst2, dst3, vec0, vec1, res0, res1;
    v8u16 vec2, vec3, filt;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, vec0, vec1);
    DOTP_UB2_UH(vec0, vec1, filt0, filt0, vec2, vec3);
    SRARI_H2_UH(vec2, vec3, 7);
    PCKEV_B2_UB(vec2, vec2, vec3, vec3, res0, res1);
    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst2);
    AVER_UB2_UB(res0, dst0, res1, dst2, res0, res1);
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hz_2t_and_aver_dst_4x8_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, vec0, vec1, vec2, vec3, res0, res1, res2, res3;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8u16 vec4, vec5, vec6, vec7, filt;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, vec0, vec1);
    VSHF_B2_UB(src4, src5, src6, src7, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec4, vec5,
                vec6, vec7);
    SRARI_H4_UH(vec4, vec5, vec6, vec7, 7);
    PCKEV_B4_UB(vec4, vec4, vec5, vec5, vec6, vec6, vec7, vec7, res0, res1,
                res2, res3);
    ILVR_W4_UB(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, dst0, dst2,
               dst4, dst6);
    AVER_UB4_UB(res0, dst0, res1, dst2, res2, dst4, res3, dst6, res0, res1,
                res2, res3);
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);
    ST4x4_UB(res2, res3, 0, 1, 0, 1, dst, dst_stride);
}

void ff_avg_bilin_4h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_2t_and_aver_dst_4x4_msa(src, src_stride, dst, dst_stride,
                                          filter);
    } else if (8 == height) {
        common_hz_2t_and_aver_dst_4x8_msa(src, src_stride, dst, dst_stride,
                                          filter);
    }
}

static void common_hz_2t_and_aver_dst_8x4_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, mask;
    v16u8 filt0, dst0, dst1, dst2, dst3;
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
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    PCKEV_AVG_ST8x4_UB(vec0, dst0, vec1, dst1, vec2, dst2, vec3, dst3,
                       dst, dst_stride);
}

static void common_hz_2t_and_aver_dst_8x8mult_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  const int8_t *filter,
                                                  int32_t height)
{
    v16i8 src0, src1, src2, src3, mask;
    v16u8 filt0, dst0, dst1, dst2, dst3;
    v8u16 vec0, vec1, vec2, vec3, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);
    VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec0, vec1,
                vec2, vec3);
    SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);
    PCKEV_AVG_ST8x4_UB(vec0, dst0, vec1, dst1, vec2, dst2, vec3, dst3,
                       dst, dst_stride);
    dst += (4 * dst_stride);

    VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
    VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec0, vec1,
                vec2, vec3);
    SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    PCKEV_AVG_ST8x4_UB(vec0, dst0, vec1, dst1, vec2, dst2, vec3, dst3,
                       dst, dst_stride);
    dst += (4 * dst_stride);

    if (16 == height) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec0,
                    vec1, vec2, vec3);
        SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        PCKEV_AVG_ST8x4_UB(vec0, dst0, vec1, dst1, vec2, dst2, vec3, dst3,
                           dst, dst_stride);
        dst += (4 * dst_stride);

        VSHF_B2_UH(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UH(src2, src2, src3, src3, mask, mask, vec2, vec3);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, vec0,
                    vec1, vec2, vec3);
        SRARI_H4_UH(vec0, vec1, vec2, vec3, 7);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        PCKEV_AVG_ST8x4_UB(vec0, dst0, vec1, dst1, vec2, dst2, vec3, dst3,
                           dst, dst_stride);
    }
}

void ff_avg_bilin_8h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];

    if (4 == height) {
        common_hz_2t_and_aver_dst_8x4_msa(src, src_stride, dst, dst_stride,
                                          filter);
    } else {
        common_hz_2t_and_aver_dst_8x8mult_msa(src, src_stride, dst, dst_stride,
                                              filter, height);
    }
}

void ff_avg_bilin_16h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, dst0, dst1, dst2, dst3;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 res0, res1, res2, res3, res4, res5, res6, res7, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

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
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, res0, res1,
                res2, res3);
    DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0, res4, res5,
                res6, res7);
    SRARI_H4_UH(res0, res1, res2, res3, 7);
    SRARI_H4_UH(res4, res5, res6, res7, 7);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    PCKEV_AVG_ST_UB(res1, res0, dst0, dst);
    dst += dst_stride;
    PCKEV_AVG_ST_UB(res3, res2, dst1, dst);
    dst += dst_stride;
    PCKEV_AVG_ST_UB(res5, res4, dst2, dst);
    dst += dst_stride;
    PCKEV_AVG_ST_UB(res7, res6, dst3, dst);
    dst += dst_stride;

    for (loop_cnt = (height >> 2) - 1; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        VSHF_B2_UB(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UB(src2, src2, src3, src3, mask, mask, vec2, vec3);
        VSHF_B2_UB(src4, src4, src5, src5, mask, mask, vec4, vec5);
        VSHF_B2_UB(src6, src6, src7, src7, mask, mask, vec6, vec7);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, res0,
                    res1, res2, res3);
        DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0, res4,
                    res5, res6, res7);
        SRARI_H4_UH(res0, res1, res2, res3, 7);
        SRARI_H4_UH(res4, res5, res6, res7, 7);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        PCKEV_AVG_ST_UB(res1, res0, dst0, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(res3, res2, dst1, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(res5, res4, dst2, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(res7, res6, dst3, dst);
        dst += dst_stride;
    }
}

void ff_avg_bilin_32h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, dst0, dst1, dst2, dst3;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 res0, res1, res2, res3, res4, res5, res6, res7, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src1 = __msa_sldi_b(src2, src0, 8);
        src += src_stride;
        src4 = LD_SB(src);
        src6 = LD_SB(src + 16);
        src7 = LD_SB(src + 24);
        src5 = __msa_sldi_b(src6, src4, 8);
        src += src_stride;

        VSHF_B2_UB(src0, src0, src1, src1, mask, mask, vec0, vec1);
        VSHF_B2_UB(src2, src2, src3, src3, mask, mask, vec2, vec3);
        VSHF_B2_UB(src4, src4, src5, src5, mask, mask, vec4, vec5);
        VSHF_B2_UB(src6, src6, src7, src7, mask, mask, vec6, vec7);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    res0, res1, res2, res3);
        DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    res4, res5, res6, res7);
        SRARI_H4_UH(res0, res1, res2, res3, 7);
        SRARI_H4_UH(res4, res5, res6, res7, 7);
        LD_UB2(dst, 16, dst0, dst1);
        PCKEV_AVG_ST_UB(res1, res0, dst0, dst);
        PCKEV_AVG_ST_UB(res3, res2, dst1, (dst + 16));
        dst += dst_stride;
        LD_UB2(dst, 16, dst2, dst3);
        PCKEV_AVG_ST_UB(res5, res4, dst2, dst);
        PCKEV_AVG_ST_UB(res7, res6, dst3, (dst + 16));
        dst += dst_stride;
    }
}

void ff_avg_bilin_64h_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[mx - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt0, dst0, dst1, dst2, dst3;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 out0, out1, out2, out3, out4, out5, out6, out7, filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB4(src, 16, src0, src2, src4, src6);
        src7 = LD_SB(src + 56);
        SLDI_B3_SB(src2, src4, src6, src0, src2, src4, src1, src3, src5, 8);
        src += src_stride;

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
        LD_UB4(dst, 16, dst0, dst1, dst2, dst3);
        PCKEV_AVG_ST_UB(out1, out0, dst0, dst);
        PCKEV_AVG_ST_UB(out3, out2, dst1, dst + 16);
        PCKEV_AVG_ST_UB(out5, out4, dst2, dst + 32);
        PCKEV_AVG_ST_UB(out7, out6, dst3, dst + 48);
        dst += dst_stride;
    }
}

static void common_vt_2t_and_aver_dst_4x4_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4;
    v16u8 dst0, dst1, dst2, dst3, out, filt0, src2110, src4332;
    v16i8 src10_r, src32_r, src21_r, src43_r;
    v8i16 filt;
    v8u16 tmp0, tmp1;

    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);

    src4 = LD_SB(src);
    src += src_stride;

    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
    dst0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3,
               src10_r, src21_r, src32_r, src43_r);
    ILVR_D2_UB(src21_r, src10_r, src43_r, src32_r, src2110, src4332);
    DOTP_UB2_UH(src2110, src4332, filt0, filt0, tmp0, tmp1);
    SRARI_H2_UH(tmp0, tmp1, 7);
    SAT_UH2_UH(tmp0, tmp1, 7);

    out = (v16u8) __msa_pckev_b((v16i8) tmp1, (v16i8) tmp0);
    out = __msa_aver_u_b(out, dst0);

    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_vt_2t_and_aver_dst_4x8_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src87_r;
    v16i8 src10_r, src32_r, src54_r, src76_r, src21_r, src43_r, src65_r;
    v16u8 src2110, src4332, src6554, src8776, filt0;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    src8 = LD_SB(src);

    LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_W4_UB(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, dst0, dst1,
               dst2, dst3);
    ILVR_D2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
    ILVR_B4_SB(src1, src0, src2, src1, src3, src2, src4, src3, src10_r, src21_r,
               src32_r, src43_r);
    ILVR_B4_SB(src5, src4, src6, src5, src7, src6, src8, src7, src54_r, src65_r,
               src76_r, src87_r);
    ILVR_D4_UB(src21_r, src10_r, src43_r, src32_r, src65_r, src54_r,
               src87_r, src76_r, src2110, src4332, src6554, src8776);
    DOTP_UB4_UH(src2110, src4332, src6554, src8776, filt0, filt0, filt0, filt0,
                tmp0, tmp1, tmp2, tmp3);
    SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B2_UB(tmp1, tmp0, tmp3, tmp2, src2110, src4332);
    AVER_UB2_UB(src2110, dst0, src4332, dst1, src2110, src4332);
    ST4x4_UB(src2110, src2110, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    ST4x4_UB(src4332, src4332, 0, 1, 2, 3, dst, dst_stride);
}

void ff_avg_bilin_4v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_vt_2t_and_aver_dst_4x4_msa(src, src_stride, dst, dst_stride,
                                          filter);
    } else if (8 == height) {
        common_vt_2t_and_aver_dst_4x8_msa(src, src_stride, dst, dst_stride,
                                          filter);
    }
}

static void common_vt_2t_and_aver_dst_8x4_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              const int8_t *filter)
{
    v16u8 src0, src1, src2, src3, src4;
    v16u8 dst0, dst1, dst2, dst3, vec0, vec1, vec2, vec3, filt0;
    v8u16 tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    filt0 = (v16u8) __msa_splati_h(filt, 0);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    ILVR_B2_UB(src1, src0, src2, src1, vec0, vec1);
    ILVR_B2_UB(src3, src2, src4, src3, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                tmp0, tmp1, tmp2, tmp3);
    SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_AVG_ST8x4_UB(tmp0, dst0, tmp1, dst1, tmp2, dst2, tmp3, dst3,
                       dst, dst_stride);
}

static void common_vt_2t_and_aver_dst_8x8mult_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  const int8_t *filter,
                                                  int32_t height)
{
    uint32_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, filt0;
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
        LD_UB8(dst, dst_stride, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8);

        ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
                   vec0, vec1, vec2, vec3);
        ILVR_B4_UB(src5, src4, src6, src5, src7, src6, src8, src7,
                   vec4, vec5, vec6, vec7);
        DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_AVG_ST8x4_UB(tmp0, dst1, tmp1, dst2, tmp2, dst3, tmp3,
                           dst4, dst, dst_stride);
        dst += (4 * dst_stride);

        DOTP_UB4_UH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
        PCKEV_AVG_ST8x4_UB(tmp0, dst5, tmp1, dst6, tmp2, dst7, tmp3,
                           dst8, dst, dst_stride);
        dst += (4 * dst_stride);

        src0 = src8;
    }
}

void ff_avg_bilin_8v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                         const uint8_t *src, ptrdiff_t src_stride,
                         int height, int mx, int my)
{
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_vt_2t_and_aver_dst_8x4_msa(src, src_stride, dst, dst_stride,
                                          filter);
    } else {
        common_vt_2t_and_aver_dst_8x8mult_msa(src, src_stride, dst, dst_stride,
                                              filter, height);
    }
}

void ff_avg_bilin_16v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];
    v16u8 src0, src1, src2, src3, src4, dst0, dst1, dst2, dst3, filt0;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 tmp0, tmp1, tmp2, tmp3, filt;

    /* rearranging filter_y */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        ILVR_B2_UB(src1, src0, src2, src1, vec0, vec2);
        ILVL_B2_UB(src1, src0, src2, src1, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst0, dst);
        dst += dst_stride;

        ILVR_B2_UB(src3, src2, src4, src3, vec4, vec6);
        ILVL_B2_UB(src3, src2, src4, src3, vec5, vec7);
        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst1, dst);
        dst += dst_stride;

        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst2, dst);
        dst += dst_stride;

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst3, dst);
        dst += dst_stride;

        src0 = src4;
    }
}

void ff_avg_bilin_32v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, filt0;
    v8u16 tmp0, tmp1, tmp2, tmp3, filt;

    /* rearranging filter_y */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_UB2(src, 16, src0, src5);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        ILVR_B2_UB(src1, src0, src2, src1, vec0, vec2);
        ILVL_B2_UB(src1, src0, src2, src1, vec1, vec3);

        LD_UB4(src + 16, src_stride, src6, src7, src8, src9);
        LD_UB4(dst + 16, dst_stride, dst4, dst5, dst6, dst7);
        src += (4 * src_stride);

        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst0, dst);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst1, dst + dst_stride);

        ILVR_B2_UB(src3, src2, src4, src3, vec4, vec6);
        ILVL_B2_UB(src3, src2, src4, src3, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst2, dst + 2 * dst_stride);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst3, dst + 3 * dst_stride);

        ILVR_B2_UB(src6, src5, src7, src6, vec0, vec2);
        ILVL_B2_UB(src6, src5, src7, src6, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst4, dst + 16);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst5, dst + 16 + dst_stride);

        ILVR_B2_UB(src8, src7, src9, src8, vec4, vec6);
        ILVL_B2_UB(src8, src7, src9, src8, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst6, dst + 16 + 2 * dst_stride);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst7, dst + 16 + 3 * dst_stride);
        dst += (4 * dst_stride);

        src0 = src4;
        src5 = src9;
    }
}

void ff_avg_bilin_64v_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter = vp9_bilinear_filters_msa[my - 1];
    v16u8 src0, src1, src2, src3, src4, src5;
    v16u8 src6, src7, src8, src9, src10, src11, filt0;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8u16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8u16 filt;

    /* rearranging filter_y */
    filt = LD_UH(filter);
    filt0 = (v16u8) __msa_splati_h((v8i16) filt, 0);

    LD_UB4(src, 16, src0, src3, src6, src9);
    src += src_stride;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_UB2(src, src_stride, src1, src2);
        LD_UB2(dst, dst_stride, dst0, dst1);
        LD_UB2(src + 16, src_stride, src4, src5);
        LD_UB2(dst + 16, dst_stride, dst2, dst3);
        LD_UB2(src + 32, src_stride, src7, src8);
        LD_UB2(dst + 32, dst_stride, dst4, dst5);
        LD_UB2(src + 48, src_stride, src10, src11);
        LD_UB2(dst + 48, dst_stride, dst6, dst7);
        src += (2 * src_stride);

        ILVR_B2_UB(src1, src0, src2, src1, vec0, vec2);
        ILVL_B2_UB(src1, src0, src2, src1, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst0, dst);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst1, dst + dst_stride);

        ILVR_B2_UB(src4, src3, src5, src4, vec4, vec6);
        ILVL_B2_UB(src4, src3, src5, src4, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp4, tmp5);
        SRARI_H2_UH(tmp4, tmp5, 7);
        SAT_UH2_UH(tmp4, tmp5, 7);
        PCKEV_AVG_ST_UB(tmp5, tmp4, dst2, dst + 16);

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp6, tmp7);
        SRARI_H2_UH(tmp6, tmp7, 7);
        SAT_UH2_UH(tmp6, tmp7, 7);
        PCKEV_AVG_ST_UB(tmp7, tmp6, dst3, dst + 16 + dst_stride);

        ILVR_B2_UB(src7, src6, src8, src7, vec0, vec2);
        ILVL_B2_UB(src7, src6, src8, src7, vec1, vec3);
        DOTP_UB2_UH(vec0, vec1, filt0, filt0, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst4, dst + 32);

        DOTP_UB2_UH(vec2, vec3, filt0, filt0, tmp2, tmp3);
        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        PCKEV_AVG_ST_UB(tmp3, tmp2, dst5, dst + 32 + dst_stride);

        ILVR_B2_UB(src10, src9, src11, src10, vec4, vec6);
        ILVL_B2_UB(src10, src9, src11, src10, vec5, vec7);
        DOTP_UB2_UH(vec4, vec5, filt0, filt0, tmp4, tmp5);
        SRARI_H2_UH(tmp4, tmp5, 7);
        SAT_UH2_UH(tmp4, tmp5, 7);
        PCKEV_AVG_ST_UB(tmp5, tmp4, dst6, (dst + 48));

        DOTP_UB2_UH(vec6, vec7, filt0, filt0, tmp6, tmp7);
        SRARI_H2_UH(tmp6, tmp7, 7);
        SAT_UH2_UH(tmp6, tmp7, 7);
        PCKEV_AVG_ST_UB(tmp7, tmp6, dst7, dst + 48 + dst_stride);
        dst += (2 * dst_stride);

        src0 = src2;
        src3 = src5;
        src6 = src8;
        src9 = src11;
    }
}

static void common_hv_2ht_2vt_and_aver_dst_4x4_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert)
{
    v16i8 src0, src1, src2, src3, src4, mask;
    v16u8 filt_hz, filt_vt, vec0, vec1;
    v16u8 dst0, dst1, dst2, dst3, res0, res1;
    v8u16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, tmp0, tmp1, filt;

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

    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    ILVR_W2_UB(dst1, dst0, dst3, dst2, dst0, dst2);
    DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp0, tmp1);
    SRARI_H2_UH(tmp0, tmp1, 7);
    SAT_UH2_UH(tmp0, tmp1, 7);
    PCKEV_B2_UB(tmp0, tmp0, tmp1, tmp1, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst2, res0, res1);
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void common_hv_2ht_2vt_and_aver_dst_4x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, mask;
    v16u8 filt_hz, filt_vt, vec0, vec1, vec2, vec3, res0, res1, res2, res3;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8u16 hz_out0, hz_out1, hz_out2, hz_out3, hz_out4, hz_out5, hz_out6;
    v8u16 hz_out7, hz_out8, tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    mask = LD_SB(&mc_filt_mask_arr[16]);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h(filt, 0);

    filt = LD_SH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h(filt, 0);

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

    LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_W4_UB(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6, dst0, dst2,
               dst4, dst6);
    ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
    ILVEV_B2_UB(hz_out4, hz_out5, hz_out6, hz_out7, vec2, vec3);
    DOTP_UB4_UH(vec0, vec1, vec2, vec3, filt_vt, filt_vt, filt_vt, filt_vt,
                tmp0, tmp1, tmp2, tmp3);
    SRARI_H4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    SAT_UH4_UH(tmp0, tmp1, tmp2, tmp3, 7);
    PCKEV_B4_UB(tmp0, tmp0, tmp1, tmp1, tmp2, tmp2, tmp3, tmp3, res0, res1,
                res2, res3);
    AVER_UB4_UB(res0, dst0, res1, dst2, res2, dst4, res3, dst6, res0, res1,
                res2, res3);
    ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);
    ST4x4_UB(res2, res3, 0, 1, 0, 1, dst, dst_stride);
}

void ff_avg_bilin_4hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    const int8_t *filter_horiz = vp9_bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_hv_2ht_2vt_and_aver_dst_4x4_msa(src, src_stride, dst, dst_stride,
                                               filter_horiz, filter_vert);
    } else if (8 == height) {
        common_hv_2ht_2vt_and_aver_dst_4x8_msa(src, src_stride, dst, dst_stride,
                                               filter_horiz, filter_vert);
    }
}

static void common_hv_2ht_2vt_and_aver_dst_8x4_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert)
{
    v16i8 src0, src1, src2, src3, src4, mask;
    v16u8 filt_hz, filt_vt, dst0, dst1, dst2, dst3, vec0, vec1, vec2, vec3;
    v8u16 hz_out0, hz_out1, tmp0, tmp1, tmp2, tmp3;
    v8i16 filt;

    mask = LD_SB(&mc_filt_mask_arr[0]);

    /* rearranging filter */
    filt = LD_SH(filter_horiz);
    filt_hz = (v16u8) __msa_splati_h(filt, 0);

    filt = LD_SH(filter_vert);
    filt_vt = (v16u8) __msa_splati_h(filt, 0);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);

    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
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
    PCKEV_AVG_ST8x4_UB(tmp0, dst0, tmp1, dst1, tmp2, dst2, tmp3, dst3,
                       dst, dst_stride);
}

static void common_hv_2ht_2vt_and_aver_dst_8x8mult_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride,
                                                       const int8_t *filter_horiz,
                                                       const int8_t *filter_vert,
                                                       int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, mask;
    v16u8 filt_hz, filt_vt, vec0, dst0, dst1, dst2, dst3;
    v8u16 hz_out0, hz_out1, tmp0, tmp1, tmp2, tmp3;
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

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        hz_out1 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp0 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out0 = HORIZ_2TAP_FILT_UH(src2, src2, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
        tmp1 = __msa_dotp_u_h(vec0, filt_vt);

        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);

        hz_out1 = HORIZ_2TAP_FILT_UH(src3, src3, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out1, (v16i8) hz_out0);
        tmp2 = __msa_dotp_u_h(vec0, filt_vt);

        hz_out0 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
        vec0 = (v16u8) __msa_ilvev_b((v16i8) hz_out0, (v16i8) hz_out1);
        tmp3 = __msa_dotp_u_h(vec0, filt_vt);

        SRARI_H2_UH(tmp2, tmp3, 7);
        SAT_UH2_UH(tmp2, tmp3, 7);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        PCKEV_AVG_ST8x4_UB(tmp0, dst0, tmp1, dst1, tmp2, dst2, tmp3,
                           dst3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_avg_bilin_8hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                          const uint8_t *src, ptrdiff_t src_stride,
                          int height, int mx, int my)
{
    const int8_t *filter_horiz = vp9_bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = vp9_bilinear_filters_msa[my - 1];

    if (4 == height) {
        common_hv_2ht_2vt_and_aver_dst_8x4_msa(src, src_stride, dst, dst_stride,
                                               filter_horiz, filter_vert);
    } else {
        common_hv_2ht_2vt_and_aver_dst_8x8mult_msa(src, src_stride,
                                                   dst, dst_stride,
                                                   filter_horiz, filter_vert,
                                                   height);
    }
}

void ff_avg_bilin_16hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src, ptrdiff_t src_stride,
                           int height, int mx, int my)
{
    uint32_t loop_cnt;
    const int8_t *filter_horiz = vp9_bilinear_filters_msa[mx - 1];
    const int8_t *filter_vert = vp9_bilinear_filters_msa[my - 1];
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, mask;
    v16u8 filt_hz, filt_vt, vec0, vec1, dst0, dst1, dst2, dst3;
    v8u16 hz_out0, hz_out1, hz_out2, hz_out3, tmp0, tmp1;
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
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        hz_out1 = HORIZ_2TAP_FILT_UH(src0, src0, mask, filt_hz, 7);
        hz_out3 = HORIZ_2TAP_FILT_UH(src1, src1, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst0, dst);
        dst += dst_stride;

        hz_out0 = HORIZ_2TAP_FILT_UH(src2, src2, mask, filt_hz, 7);
        hz_out2 = HORIZ_2TAP_FILT_UH(src3, src3, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out1, hz_out0, hz_out3, hz_out2, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst1, dst);
        dst += dst_stride;

        hz_out1 = HORIZ_2TAP_FILT_UH(src4, src4, mask, filt_hz, 7);
        hz_out3 = HORIZ_2TAP_FILT_UH(src5, src5, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out0, hz_out1, hz_out2, hz_out3, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst2, dst);
        dst += dst_stride;

        hz_out0 = HORIZ_2TAP_FILT_UH(src6, src6, mask, filt_hz, 7);
        hz_out2 = HORIZ_2TAP_FILT_UH(src7, src7, mask, filt_hz, 7);
        ILVEV_B2_UB(hz_out1, hz_out0, hz_out3, hz_out2, vec0, vec1);
        DOTP_UB2_UH(vec0, vec1, filt_vt, filt_vt, tmp0, tmp1);
        SRARI_H2_UH(tmp0, tmp1, 7);
        SAT_UH2_UH(tmp0, tmp1, 7);
        PCKEV_AVG_ST_UB(tmp1, tmp0, dst3, dst);
        dst += dst_stride;
    }
}

void ff_avg_bilin_32hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src, ptrdiff_t src_stride,
                           int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        ff_avg_bilin_16hv_msa(dst, dst_stride, src, src_stride, height, mx, my);

        src += 16;
        dst += 16;
    }
}

void ff_avg_bilin_64hv_msa(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src, ptrdiff_t src_stride,
                           int height, int mx, int my)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        ff_avg_bilin_16hv_msa(dst, dst_stride, src, src_stride, height, mx, my);

        src += 16;
        dst += 16;
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

static void copy_width32_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    if (0 == height % 12) {
        for (cnt = (height / 12); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            LD_UB4(src + 16, src_stride, src4, src5, src6, src7);
            src += (4 * src_stride);
            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            ST_UB4(src4, src5, src6, src7, dst + 16, dst_stride);
            dst += (4 * dst_stride);

            LD_UB4(src, src_stride, src0, src1, src2, src3);
            LD_UB4(src + 16, src_stride, src4, src5, src6, src7);
            src += (4 * src_stride);
            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            ST_UB4(src4, src5, src6, src7, dst + 16, dst_stride);
            dst += (4 * dst_stride);

            LD_UB4(src, src_stride, src0, src1, src2, src3);
            LD_UB4(src + 16, src_stride, src4, src5, src6, src7);
            src += (4 * src_stride);
            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            ST_UB4(src4, src5, src6, src7, dst + 16, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == height % 8) {
        copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 32);
    } else if (0 == height % 4) {
        for (cnt = (height >> 2); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            LD_UB4(src + 16, src_stride, src4, src5, src6, src7);
            src += (4 * src_stride);
            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            ST_UB4(src4, src5, src6, src7, dst + 16, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

static void copy_width64_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 64);
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

static void avg_width32_msa(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt;
    uint8_t *dst_dup = dst;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 dst8, dst9, dst10, dst11, dst12, dst13, dst14, dst15;

    for (cnt = (height / 8); cnt--;) {
        LD_UB4(src, src_stride, src0, src2, src4, src6);
        LD_UB4(src + 16, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);
        LD_UB4(dst_dup, dst_stride, dst0, dst2, dst4, dst6);
        LD_UB4(dst_dup + 16, dst_stride, dst1, dst3, dst5, dst7);
        dst_dup += (4 * dst_stride);
        LD_UB4(src, src_stride, src8, src10, src12, src14);
        LD_UB4(src + 16, src_stride, src9, src11, src13, src15);
        src += (4 * src_stride);
        LD_UB4(dst_dup, dst_stride, dst8, dst10, dst12, dst14);
        LD_UB4(dst_dup + 16, dst_stride, dst9, dst11, dst13, dst15);
        dst_dup += (4 * dst_stride);

        AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3,
                    dst0, dst1, dst2, dst3);
        AVER_UB4_UB(src4, dst4, src5, dst5, src6, dst6, src7, dst7,
                    dst4, dst5, dst6, dst7);
        AVER_UB4_UB(src8, dst8, src9, dst9, src10, dst10, src11, dst11,
                    dst8, dst9, dst10, dst11);
        AVER_UB4_UB(src12, dst12, src13, dst13, src14, dst14, src15, dst15,
                    dst12, dst13, dst14, dst15);

        ST_UB4(dst0, dst2, dst4, dst6, dst, dst_stride);
        ST_UB4(dst1, dst3, dst5, dst7, dst + 16, dst_stride);
        dst += (4 * dst_stride);
        ST_UB4(dst8, dst10, dst12, dst14, dst, dst_stride);
        ST_UB4(dst9, dst11, dst13, dst15, dst + 16, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avg_width64_msa(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt;
    uint8_t *dst_dup = dst;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 dst8, dst9, dst10, dst11, dst12, dst13, dst14, dst15;

    for (cnt = (height / 4); cnt--;) {
        LD_UB4(src, 16, src0, src1, src2, src3);
        src += src_stride;
        LD_UB4(src, 16, src4, src5, src6, src7);
        src += src_stride;
        LD_UB4(src, 16, src8, src9, src10, src11);
        src += src_stride;
        LD_UB4(src, 16, src12, src13, src14, src15);
        src += src_stride;

        LD_UB4(dst_dup, 16, dst0, dst1, dst2, dst3);
        dst_dup += dst_stride;
        LD_UB4(dst_dup, 16, dst4, dst5, dst6, dst7);
        dst_dup += dst_stride;
        LD_UB4(dst_dup, 16, dst8, dst9, dst10, dst11);
        dst_dup += dst_stride;
        LD_UB4(dst_dup, 16, dst12, dst13, dst14, dst15);
        dst_dup += dst_stride;

        AVER_UB4_UB(src0, dst0, src1, dst1, src2, dst2, src3, dst3,
                    dst0, dst1, dst2, dst3);
        AVER_UB4_UB(src4, dst4, src5, dst5, src6, dst6, src7, dst7,
                    dst4, dst5, dst6, dst7);
        AVER_UB4_UB(src8, dst8, src9, dst9, src10, dst10, src11, dst11,
                    dst8, dst9, dst10, dst11);
        AVER_UB4_UB(src12, dst12, src13, dst13, src14, dst14, src15, dst15,
                    dst12, dst13, dst14, dst15);

        ST_UB4(dst0, dst1, dst2, dst3, dst, 16);
        dst += dst_stride;
        ST_UB4(dst4, dst5, dst6, dst7, dst, 16);
        dst += dst_stride;
        ST_UB4(dst8, dst9, dst10, dst11, dst, 16);
        dst += dst_stride;
        ST_UB4(dst12, dst13, dst14, dst15, dst, 16);
        dst += dst_stride;
    }
}

static const int8_t vp9_subpel_filters_msa[3][15][8] = {
    [FILTER_8TAP_REGULAR] = {
         {0, 1, -5, 126, 8, -3, 1, 0},
         {-1, 3, -10, 122, 18, -6, 2, 0},
         {-1, 4, -13, 118, 27, -9, 3, -1},
         {-1, 4, -16, 112, 37, -11, 4, -1},
         {-1, 5, -18, 105, 48, -14, 4, -1},
         {-1, 5, -19, 97, 58, -16, 5, -1},
         {-1, 6, -19, 88, 68, -18, 5, -1},
         {-1, 6, -19, 78, 78, -19, 6, -1},
         {-1, 5, -18, 68, 88, -19, 6, -1},
         {-1, 5, -16, 58, 97, -19, 5, -1},
         {-1, 4, -14, 48, 105, -18, 5, -1},
         {-1, 4, -11, 37, 112, -16, 4, -1},
         {-1, 3, -9, 27, 118, -13, 4, -1},
         {0, 2, -6, 18, 122, -10, 3, -1},
         {0, 1, -3, 8, 126, -5, 1, 0},
    }, [FILTER_8TAP_SHARP] = {
        {-1, 3, -7, 127, 8, -3, 1, 0},
        {-2, 5, -13, 125, 17, -6, 3, -1},
        {-3, 7, -17, 121, 27, -10, 5, -2},
        {-4, 9, -20, 115, 37, -13, 6, -2},
        {-4, 10, -23, 108, 48, -16, 8, -3},
        {-4, 10, -24, 100, 59, -19, 9, -3},
        {-4, 11, -24, 90, 70, -21, 10, -4},
        {-4, 11, -23, 80, 80, -23, 11, -4},
        {-4, 10, -21, 70, 90, -24, 11, -4},
        {-3, 9, -19, 59, 100, -24, 10, -4},
        {-3, 8, -16, 48, 108, -23, 10, -4},
        {-2, 6, -13, 37, 115, -20, 9, -4},
        {-2, 5, -10, 27, 121, -17, 7, -3},
        {-1, 3, -6, 17, 125, -13, 5, -2},
        {0, 1, -3, 8, 127, -7, 3, -1},
    }, [FILTER_8TAP_SMOOTH] = {
        {-3, -1, 32, 64, 38, 1, -3, 0},
        {-2, -2, 29, 63, 41, 2, -3, 0},
        {-2, -2, 26, 63, 43, 4, -4, 0},
        {-2, -3, 24, 62, 46, 5, -4, 0},
        {-2, -3, 21, 60, 49, 7, -4, 0},
        {-1, -4, 18, 59, 51, 9, -4, 0},
        {-1, -4, 16, 57, 53, 12, -4, -1},
        {-1, -4, 14, 55, 55, 14, -4, -1},
        {-1, -4, 12, 53, 57, 16, -4, -1},
        {0, -4, 9, 51, 59, 18, -4, -1},
        {0, -4, 7, 49, 60, 21, -3, -2},
        {0, -4, 5, 46, 62, 24, -3, -2},
        {0, -4, 4, 43, 63, 26, -2, -2},
        {0, -3, 2, 41, 63, 29, -2, -2},
        {0, -3, 1, 38, 64, 32, -1, -3},
    }
};

#define VP9_8TAP_MIPS_MSA_FUNC(SIZE, type, type_idx)                           \
void ff_put_8tap_##type##_##SIZE##h_msa(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_msa[type_idx][mx-1];             \
                                                                               \
    common_hz_8t_##SIZE##w_msa(src, srcstride, dst, dststride, filter, h);     \
}                                                                              \
                                                                               \
void ff_put_8tap_##type##_##SIZE##v_msa(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_msa[type_idx][my-1];             \
                                                                               \
    common_vt_8t_##SIZE##w_msa(src, srcstride, dst, dststride, filter, h);     \
}                                                                              \
                                                                               \
void ff_put_8tap_##type##_##SIZE##hv_msa(uint8_t *dst, ptrdiff_t dststride,    \
                                         const uint8_t *src,                   \
                                         ptrdiff_t srcstride,                  \
                                         int h, int mx, int my)                \
{                                                                              \
    const uint8_t *hfilter = vp9_subpel_filters_msa[type_idx][mx-1];           \
    const uint8_t *vfilter = vp9_subpel_filters_msa[type_idx][my-1];           \
                                                                               \
    common_hv_8ht_8vt_##SIZE##w_msa(src, srcstride, dst, dststride, hfilter,   \
                                    vfilter, h);                               \
}                                                                              \
                                                                               \
void ff_avg_8tap_##type##_##SIZE##h_msa(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_msa[type_idx][mx-1];             \
                                                                               \
    common_hz_8t_and_aver_dst_##SIZE##w_msa(src, srcstride, dst,               \
                                            dststride, filter, h);             \
}                                                                              \
                                                                               \
void ff_avg_8tap_##type##_##SIZE##v_msa(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_msa[type_idx][my-1];             \
                                                                               \
    common_vt_8t_and_aver_dst_##SIZE##w_msa(src, srcstride, dst, dststride,    \
                                            filter, h);                        \
}                                                                              \
                                                                               \
void ff_avg_8tap_##type##_##SIZE##hv_msa(uint8_t *dst, ptrdiff_t dststride,    \
                                         const uint8_t *src,                   \
                                         ptrdiff_t srcstride,                  \
                                         int h, int mx, int my)                \
{                                                                              \
    const uint8_t *hfilter = vp9_subpel_filters_msa[type_idx][mx-1];           \
    const uint8_t *vfilter = vp9_subpel_filters_msa[type_idx][my-1];           \
                                                                               \
    common_hv_8ht_8vt_and_aver_dst_##SIZE##w_msa(src, srcstride, dst,          \
                                                 dststride, hfilter,           \
                                                 vfilter, h);                  \
}

#define VP9_COPY_AVG_MIPS_MSA_FUNC(SIZE)                           \
void ff_copy##SIZE##_msa(uint8_t *dst, ptrdiff_t dststride,        \
                         const uint8_t *src, ptrdiff_t srcstride,  \
                         int h, int mx, int my)                    \
{                                                                  \
                                                                   \
    copy_width##SIZE##_msa(src, srcstride, dst, dststride, h);     \
}                                                                  \
                                                                   \
void ff_avg##SIZE##_msa(uint8_t *dst, ptrdiff_t dststride,         \
                        const uint8_t *src, ptrdiff_t srcstride,   \
                        int h, int mx, int my)                     \
{                                                                  \
                                                                   \
    avg_width##SIZE##_msa(src, srcstride, dst, dststride, h);      \
}

#define VP9_AVG_MIPS_MSA_FUNC(SIZE)                               \
void ff_avg##SIZE##_msa(uint8_t *dst, ptrdiff_t dststride,        \
                        const uint8_t *src, ptrdiff_t srcstride,  \
                        int h, int mx, int my)                    \
{                                                                 \
                                                                  \
    avg_width##SIZE##_msa(src, srcstride, dst, dststride, h);     \
}

VP9_8TAP_MIPS_MSA_FUNC(64, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(32, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(16, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(8, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MSA_FUNC(4, regular, FILTER_8TAP_REGULAR);

VP9_8TAP_MIPS_MSA_FUNC(64, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(32, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(16, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(8, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MSA_FUNC(4, sharp, FILTER_8TAP_SHARP);

VP9_8TAP_MIPS_MSA_FUNC(64, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(32, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(16, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(8, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MSA_FUNC(4, smooth, FILTER_8TAP_SMOOTH);

VP9_COPY_AVG_MIPS_MSA_FUNC(64);
VP9_COPY_AVG_MIPS_MSA_FUNC(32);
VP9_COPY_AVG_MIPS_MSA_FUNC(16);
VP9_COPY_AVG_MIPS_MSA_FUNC(8);
VP9_AVG_MIPS_MSA_FUNC(4);

#undef VP9_8TAP_MIPS_MSA_FUNC
#undef VP9_COPY_AVG_MIPS_MSA_FUNC
#undef VP9_AVG_MIPS_MSA_FUNC
