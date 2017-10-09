/*
 * Copyright (c) 2015 - 2017 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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
#include "libavcodec/mips/hevc_macros_msa.h"

static void copy_width8_msa(uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt;
    uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

    if (2 == height) {
        LD2(src, src_stride, out0, out1);
        SD(out0, dst);
        dst += dst_stride;
        SD(out1, dst);
    } else if (6 == height) {
        LD4(src, src_stride, out0, out1, out2, out3);
        src += (4 * src_stride);
        SD4(out0, out1, out2, out3, dst, dst_stride);
        dst += (4 * dst_stride);
        LD2(src, src_stride, out0, out1);
        SD(out0, dst);
        dst += dst_stride;
        SD(out1, dst);
    } else if (0 == (height % 8)) {
        for (cnt = (height >> 3); cnt--;) {
            LD4(src, src_stride, out0, out1, out2, out3);
            src += (4 * src_stride);
            LD4(src, src_stride, out4, out5, out6, out7);
            src += (4 * src_stride);
            SD4(out0, out1, out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
            SD4(out4, out5, out6, out7, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == (height % 4)) {
        for (cnt = (height >> 2); cnt--;) {
            LD4(src, src_stride, out0, out1, out2, out3);
            src += (4 * src_stride);
            SD4(out0, out1, out2, out3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

static void copy_width12_msa(uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    ST12x8_UB(src0, src1, src2, src3, src4, src5, src6, src7, dst, dst_stride);
    dst += (8 * dst_stride);
    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    ST12x8_UB(src0, src1, src2, src3, src4, src5, src6, src7, dst, dst_stride);
}

static void copy_width16_msa(uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    if (12 == height) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);
        ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7, dst, dst_stride);
        dst += (8 * dst_stride);
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        ST_UB4(src0, src1, src2, src3, dst, dst_stride);
        dst += (4 * dst_stride);
    } else if (0 == (height % 8)) {
        for (cnt = (height >> 3); cnt--;) {
            LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6,
                   src7);
            src += (8 * src_stride);
            ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7, dst,
                   dst_stride);
            dst += (8 * dst_stride);
        }
    } else if (0 == (height % 4)) {
        for (cnt = (height >> 2); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);

            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

static void copy_width24_msa(uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

    for (cnt = 4; cnt--;) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        LD4(src + 16, src_stride, out0, out1, out2, out3);
        src += (4 * src_stride);
        LD4(src + 16, src_stride, out4, out5, out6, out7);
        src += (4 * src_stride);

        ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7, dst, dst_stride);
        SD4(out0, out1, out2, out3, dst + 16, dst_stride);
        dst += (4 * dst_stride);
        SD4(out4, out5, out6, out7, dst + 16, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void copy_width32_msa(uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    for (cnt = (height >> 2); cnt--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        LD_UB4(src + 16, src_stride, src4, src5, src6, src7);
        src += (4 * src_stride);
        ST_UB4(src0, src1, src2, src3, dst, dst_stride);
        ST_UB4(src4, src5, src6, src7, dst + 16, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void copy_width48_msa(uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16u8 src11;

    for (cnt = (height >> 2); cnt--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        LD_UB4(src + 16, src_stride, src4, src5, src6, src7);
        LD_UB4(src + 32, src_stride, src8, src9, src10, src11);
        src += (4 * src_stride);

        ST_UB4(src0, src1, src2, src3, dst, dst_stride);
        ST_UB4(src4, src5, src6, src7, dst + 16, dst_stride);
        ST_UB4(src8, src9, src10, src11, dst + 32, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void copy_width64_msa(uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;

    for (cnt = (height >> 2); cnt--;) {
        LD_UB4(src, 16, src0, src1, src2, src3);
        src += src_stride;
        LD_UB4(src, 16, src4, src5, src6, src7);
        src += src_stride;
        LD_UB4(src, 16, src8, src9, src10, src11);
        src += src_stride;
        LD_UB4(src, 16, src12, src13, src14, src15);
        src += src_stride;

        ST_UB4(src0, src1, src2, src3, dst, 16);
        dst += dst_stride;
        ST_UB4(src4, src5, src6, src7, dst, 16);
        dst += dst_stride;
        ST_UB4(src8, src9, src10, src11, dst, 16);
        dst += dst_stride;
        ST_UB4(src12, src13, src14, src15, dst, 16);
        dst += dst_stride;
    }
}

static const uint8_t mc_filt_mask_arr[16 * 3] = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
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

#define FILT_4TAP_DPADD_S_H(vec0, vec1, filt0, filt1)           \
( {                                                             \
    v8i16 tmp0;                                                 \
                                                                \
    tmp0 = __msa_dotp_s_h((v16i8) vec0, (v16i8) filt0);         \
    tmp0 = __msa_dpadd_s_h(tmp0, (v16i8) vec1, (v16i8) filt1);  \
                                                                \
    tmp0;                                                       \
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

static void common_hz_8t_4x4_msa(uint8_t *src, int32_t src_stride,
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
    SRARI_H2_SH(out0, out1, 6);
    SAT_SH2_SH(out0, out1, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4x8_msa(uint8_t *src, int32_t src_stride,
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
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4x16_msa(uint8_t *src, int32_t src_stride,
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
    SRARI_H4_SH(out0, out1, out2, out3, 6);
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

    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4w_msa(uint8_t *src, int32_t src_stride,
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

static void common_hz_8t_8x4_msa(uint8_t *src, int32_t src_stride,
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
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    tmp0 = PCKEV_XORI128_UB(out0, out1);
    tmp1 = PCKEV_XORI128_UB(out2, out3);
    ST8x4_UB(tmp0, tmp1, dst, dst_stride);
}

static void common_hz_8t_8x8mult_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_8t_8w_msa(uint8_t *src, int32_t src_stride,
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

static void common_hz_8t_12w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint8_t *src1_ptr, *dst1;
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3;
    v16u8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask00, tmp0, tmp1;

    mask00 = LD_UB(&mc_filt_mask_arr[0]);
    mask0 = LD_UB(&mc_filt_mask_arr[16]);

    src1_ptr = src - 3;
    dst1 = dst;

    dst = dst1 + 8;
    src = src1_ptr + 8;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask00 + 2;
    mask2 = mask00 + 4;
    mask3 = mask00 + 6;
    mask4 = mask0 + 2;
    mask5 = mask0 + 4;
    mask6 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        /* 8 width */
        LD_SB4(src1_ptr, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src1_ptr += (4 * src_stride);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask00, mask1, mask2,
                                   mask3, filt0, filt1, filt2, filt3, out0,
                                   out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST8x4_UB(tmp0, tmp1, dst1, dst_stride);
        dst1 += (4 * dst_stride);

        /* 4 width */
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (4 * src_stride);
        HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask4, mask5,
                                   mask6, filt0, filt1, filt2, filt3, out0,
                                   out1);
        SRARI_H2_SH(out0, out1, 6);
        SAT_SH2_SH(out0, out1, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        ST4x4_UB(tmp0, tmp0, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_8t_16w_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst);
        dst += dst_stride;
    }
}

static void common_hz_8t_24w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7, out;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 vec11;
    v8i16 out0, out1, out2, out3, out4, out5, out6, out7, out8, out9, out10;
    v8i16 out11, filt;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 16, src_stride, src1, src3);
        XORI_B4_128_SB(src0, src1, src2, src3);
        src += (2 * src_stride);
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec8);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec9);
        VSHF_B2_SB(src0, src1, src2, src3, mask4, mask4, vec1, vec3);
        DOTP_SB4_SH(vec0, vec8, vec2, vec9, filt0, filt0, filt0, filt0, out0,
                    out8, out2, out9);
        DOTP_SB2_SH(vec1, vec3, filt0, filt0, out1, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0, vec8);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2, vec9);
        VSHF_B2_SB(src0, src1, src2, src3, mask6, mask6, vec1, vec3);
        DOTP_SB4_SH(vec0, vec8, vec2, vec9, filt2, filt2, filt2, filt2, out4,
                    out10, out6, out11);
        DOTP_SB2_SH(vec1, vec3, filt2, filt2, out5, out7);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec10);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec11);
        VSHF_B2_SB(src0, src1, src2, src3, mask5, mask5, vec5, vec7);
        DPADD_SB4_SH(vec4, vec10, vec6, vec11, filt1, filt1, filt1, filt1,
                     out0, out8, out2, out9);
        DPADD_SB2_SH(vec5, vec7, filt1, filt1, out1, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec4, vec10);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec6, vec11);
        VSHF_B2_SB(src0, src1, src2, src3, mask7, mask7, vec5, vec7);
        DPADD_SB4_SH(vec4, vec10, vec6, vec11, filt3, filt3, filt3, filt3,
                     out4, out10, out6, out11);
        DPADD_SB2_SH(vec5, vec7, filt3, filt3, out5, out7);
        ADDS_SH4_SH(out0, out4, out8, out10, out2, out6, out9, out11, out0,
                    out8, out2, out9);
        ADDS_SH2_SH(out1, out5, out3, out7, out1, out3);
        SRARI_H4_SH(out0, out8, out2, out9, 6);
        SRARI_H2_SH(out1, out3, 6);
        SAT_SH4_SH(out0, out8, out2, out9, 7);
        SAT_SH2_SH(out1, out3, 7);
        out = PCKEV_XORI128_UB(out8, out9);
        ST8x2_UB(out, dst + 16, dst_stride);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst);
        dst += dst_stride;
    }
}

static void common_hz_8t_32w_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 16);
        dst += dst_stride;
    }
}

static void common_hz_8t_48w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3, vec0, vec1, vec2;
    v16u8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7, out;
    v8i16 filt, out0, out1, out2, out3, out4, out5, out6;

    mask0 = LD_UB(&mc_filt_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB3(src, 16, src0, src2, src3);
        src1 = __msa_sldi_b(src2, src0, 8);

        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask0, mask0, mask0,
                   vec0, vec1, vec2);
        DOTP_SB3_SH(vec0, vec1, vec2, filt0, filt0, filt0, out0, out1, out2);
        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask1, mask1, mask1,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt1, filt1, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt1);
        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask2, mask2, mask2,
                   vec0, vec1, vec2);
        DOTP_SB3_SH(vec0, vec1, vec2, filt2, filt2, filt2, out3, out4, out5);
        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask3, mask3, mask3,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt3, filt3, out3, out4);
        out5 = __msa_dpadd_s_h(out5, vec2, filt3);
        ADDS_SH2_SH(out0, out3, out1, out4, out0, out1);
        out2 = __msa_adds_s_h(out2, out5);
        SRARI_H2_SH(out0, out1, 6);
        out6 = __msa_srari_h(out2, 6);
        SAT_SH3_SH(out0, out1, out6, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);

        src1 = LD_SB(src + 40);
        src += src_stride;
        src1 = (v16i8) __msa_xori_b((v16u8) src1, 128);

        VSHF_B3_SB(src2, src3, src3, src3, src1, src1, mask4, mask0, mask0,
                   vec0, vec1, vec2);
        DOTP_SB3_SH(vec0, vec1, vec2, filt0, filt0, filt0, out0, out1, out2);
        VSHF_B3_SB(src2, src3, src3, src3, src1, src1, mask5, mask1, mask1,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt1, filt1, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt1);
        VSHF_B3_SB(src2, src3, src3, src3, src1, src1, mask6, mask2, mask2,
                   vec0, vec1, vec2);
        DOTP_SB3_SH(vec0, vec1, vec2, filt2, filt2, filt2, out3, out4, out5);
        VSHF_B3_SB(src2, src3, src3, src3, src1, src1, mask7, mask3, mask3,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt3, filt3, out3, out4);
        out5 = __msa_dpadd_s_h(out5, vec2, filt3);
        ADDS_SH2_SH(out0, out3, out1, out4, out3, out4);
        out5 = __msa_adds_s_h(out2, out5);
        SRARI_H2_SH(out3, out4, 6);
        out5 = __msa_srari_h(out5, 6);
        SAT_SH3_SH(out3, out4, out5, 7);
        out = PCKEV_XORI128_UB(out6, out3);
        ST_UB(out, dst + 16);
        out = PCKEV_XORI128_UB(out4, out5);
        ST_UB(out, dst + 32);
        dst += dst_stride;
    }
}

static void common_hz_8t_64w_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst + 32);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 48);
        dst += dst_stride;
    }
}

static void common_vt_8t_4w_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H2_SH(out10, out32, 6);
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

static void common_vt_8t_8w_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
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

static void common_vt_8t_12w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    uint32_t out2, out3;
    uint64_t out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, res0, res1;
    v16i8 res2, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 vec01, vec23, vec45, vec67, tmp0, tmp1, tmp2;
    v8i16 filt, filt0, filt1, filt2, filt3;
    v4i32 mask = { 2, 6, 2, 6 };

    src -= (3 * src_stride);

    /* rearranging filter_y */
    filt = LD_SH(filter);
    SPLATI_H4_SH(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    /* 4 width */
    VSHF_W2_SB(src0, src1, src1, src2, mask, mask, vec0, vec1);
    VSHF_W2_SB(src2, src3, src3, src4, mask, mask, vec2, vec3);
    VSHF_W2_SB(src4, src5, src5, src6, mask, mask, vec4, vec5);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src7, src8);
        XORI_B2_128_SB(src7, src8);
        src += (2 * src_stride);

        ILVR_B4_SH(src1, src0, src3, src2, src5, src4, src7, src6,
                   vec01, vec23, vec45, vec67);
        tmp0 = FILT_8TAP_DPADD_S_H(vec01, vec23, vec45, vec67, filt0, filt1,
                                   filt2, filt3);
        ILVR_B4_SH(src2, src1, src4, src3, src6, src5, src8, src7, vec01, vec23,
                   vec45, vec67);
        tmp1 = FILT_8TAP_DPADD_S_H(vec01, vec23, vec45, vec67, filt0, filt1,
                                   filt2, filt3);

        /* 4 width */
        VSHF_W2_SB(src6, src7, src7, src8, mask, mask, vec6, vec7);
        ILVR_B4_SH(vec1, vec0, vec3, vec2, vec5, vec4, vec7, vec6, vec01, vec23,
                   vec45, vec67);
        tmp2 = FILT_8TAP_DPADD_S_H(vec01, vec23, vec45, vec67, filt0, filt1,
                                   filt2, filt3);
        SRARI_H2_SH(tmp0, tmp1, 6);
        tmp2 = __msa_srari_h(tmp2, 6);
        SAT_SH3_SH(tmp0, tmp1, tmp2, 7);
        PCKEV_B3_SB(tmp0, tmp0, tmp1, tmp1, tmp2, tmp2, res0, res1, res2);
        XORI_B3_128_SB(res0, res1, res2);

        out0 = __msa_copy_u_d((v2i64) res0, 0);
        out1 = __msa_copy_u_d((v2i64) res1, 0);
        out2 = __msa_copy_u_w((v4i32) res2, 0);
        out3 = __msa_copy_u_w((v4i32) res2, 1);
        SD(out0, dst);
        SW(out2, (dst + 8));
        dst += dst_stride;
        SD(out1, dst);
        SW(out3, (dst + 8));
        dst += dst_stride;

        src0 = src2;
        src1 = src3;
        src2 = src4;
        src3 = src5;
        src4 = src6;
        src5 = src7;
        src6 = src8;
        vec0 = vec2;
        vec1 = vec3;
        vec2 = vec4;
        vec3 = vec5;
        vec4 = vec6;
        vec5 = vec7;
    }
}

static void common_vt_8t_16w_msa(uint8_t *src, int32_t src_stride,
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
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 6);
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

static void common_vt_8t_16w_mult_msa(uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter, int32_t height,
                                      int32_t width)
{
    uint8_t *src_tmp;
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
            SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
            SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 6);
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

static void common_vt_8t_24w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              16);

    common_vt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride, filter,
                        height);
}

static void common_vt_8t_32w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              32);
}

static void common_vt_8t_48w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              48);
}

static void common_vt_8t_64w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              64);
}

static void hevc_hv_uni_8t_4w_msa(uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst87;
    v4i32 dst0_r, dst1_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };
    v8i16 mask4 = { 0, 4, 1, 5, 2, 6, 3, 7 };

    src -= ((3 * src_stride) + 3);
    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W4_SW(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src3, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src4, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src5, mask0, mask1, mask2, mask3,
               vec8, vec9, vec10, vec11);
    VSHF_B4_SB(src3, src6, mask0, mask1, mask2, mask3,
               vec12, vec13, vec14, vec15);

    dst30 = const_vec;
    DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                 dst30, dst30, dst30, dst30);
    dst41 = const_vec;
    DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                 dst41, dst41, dst41, dst41);
    dst52 = const_vec;
    DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3,
                 dst52, dst52, dst52, dst52);
    dst63 = const_vec;
    DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2, filt3,
                 dst63, dst63, dst63, dst63);

    ILVR_H3_SH(dst41, dst30, dst52, dst41, dst63, dst52,
               dst10_r, dst21_r, dst32_r);
    dst43_r = __msa_ilvl_h(dst41, dst30);
    dst54_r = __msa_ilvl_h(dst52, dst41);
    dst65_r = __msa_ilvl_h(dst63, dst52);
    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = height >> 1; loop_cnt--;) {
        LD_SB2(src, src_stride, src7, src8);
        src += 2 * src_stride;
        XORI_B2_128_SB(src7, src8);

        VSHF_B4_SB(src7, src8, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        dst87 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst87, dst87, dst87, dst87);

        dst76_r = __msa_ilvr_h(dst87, dst66);
        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst87_r = __msa_vshf_h(mask4, dst87, dst87);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);

        dst0_r >>= 6;
        dst1_r >>= 6;
        SRARI_W2_SW(dst0_r, dst1_r, 6);
        dst0_r = CLIP_SW_0_255(dst0_r);
        dst1_r = CLIP_SW_0_255(dst1_r);

        HEVC_PCK_SW_SB2(dst1_r, dst0_r, dst0_r);
        ST4x2_UB(dst0_r, dst, dst_stride);
        dst += (2 * dst_stride);

        dst10_r = dst32_r;
        dst32_r = dst54_r;
        dst54_r = dst76_r;
        dst21_r = dst43_r;
        dst43_r = dst65_r;
        dst65_r = dst87_r;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst87, 1);
    }
}

static void hevc_hv_uni_8t_8multx2mult_msa(uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride,
                                           const int8_t *filter_x,
                                           const int8_t *filter_y,
                                           int32_t height, int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1, filt2, filt3;
    v4i32 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= ((3 * src_stride) + 3);
    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W4_SW(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (cnt = width >> 3; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

        /* row 0 row 1 row 2 row 3 */
        VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);
        dst0 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst0, dst0, dst0, dst0);
        dst1 = const_vec;
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                     dst1, dst1, dst1, dst1);
        dst2 = const_vec;
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3,
                     dst2, dst2, dst2, dst2);
        dst3 = const_vec;
        DPADD_SB4_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2, filt3,
                     dst3, dst3, dst3, dst3);

        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        dst4 = const_vec;
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                     dst4, dst4, dst4, dst4);
        dst5 = const_vec;
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2, filt3,
                     dst5, dst5, dst5, dst5);
        dst6 = const_vec;
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2, filt3,
                     dst6, dst6, dst6, dst6);

        ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_r, dst32_r, dst54_r, dst21_r);
        ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
        ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                   dst10_l, dst32_l, dst54_l, dst21_l);
        ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            LD_SB2(src_tmp, src_stride, src7, src8);
            XORI_B2_128_SB(src7, src8);
            src_tmp += 2 * src_stride;

            VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst7 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst7, dst7, dst7, dst7);

            ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
            dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_l = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_r >>= 6;
            dst0_l >>= 6;

            VSHF_B4_SB(src8, src8, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst8 = const_vec;
            DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2, filt3,
                         dst8, dst8, dst8, dst8);

            ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
            dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst1_l = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst1_r >>= 6;
            dst1_l >>= 6;
            SRARI_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, 6);
            dst0_r = CLIP_SW_0_255(dst0_r);
            dst0_l = CLIP_SW_0_255(dst0_l);
            dst1_r = CLIP_SW_0_255(dst1_r);
            dst1_l = CLIP_SW_0_255(dst1_l);

            HEVC_PCK_SW_SB4(dst0_l, dst0_r, dst1_l, dst1_r, dst0_r);
            ST8x2_UB(dst0_r, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);

            dst10_r = dst32_r;
            dst32_r = dst54_r;
            dst54_r = dst76_r;
            dst10_l = dst32_l;
            dst32_l = dst54_l;
            dst54_l = dst76_l;
            dst21_r = dst43_r;
            dst43_r = dst65_r;
            dst65_r = dst87_r;
            dst21_l = dst43_l;
            dst43_l = dst65_l;
            dst65_l = dst87_l;
            dst6 = dst8;
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_uni_8t_8w_msa(uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 8);
}

static void hevc_hv_uni_8t_12w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 8);

    hevc_hv_uni_8t_4w_msa(src + 8, src_stride, dst + 8, dst_stride,
                          filter_x, filter_y, height);
}

static void hevc_hv_uni_8t_16w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 16);
}

static void hevc_hv_uni_8t_24w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 24);
}

static void hevc_hv_uni_8t_32w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 32);
}

static void hevc_hv_uni_8t_48w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 48);
}

static void hevc_hv_uni_8t_64w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_8t_8multx2mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 64);
}

static void common_hz_4t_4x2_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 filt0, filt1, src0, src1, mask0, mask1, vec0, vec1;
    v16u8 out;
    v8i16 filt, res0;

    mask0 = LD_SB(&mc_filt_mask_arr[16]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);
    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    res0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt0, filt1);
    res0 = __msa_srari_h(res0, 6);
    res0 = __msa_sat_s_h(res0, 7);
    out = PCKEV_XORI128_UB(res0, res0);
    ST4x2_UB(out, dst, dst_stride);
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
    SRARI_H2_SH(out0, out1, 6);
    SAT_SH2_SH(out0, out1, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
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
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
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
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
    HORIZ_4TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                               filt0, filt1, out0, out1);
    HORIZ_4TAP_4WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1,
                               filt0, filt1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out = PCKEV_XORI128_UB(out0, out1);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
    dst += (4 * dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_4t_4w_msa(uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (2 == height) {
        common_hz_4t_4x2_msa(src, src_stride, dst, dst_stride, filter);
    } else if (4 == height) {
        common_hz_4t_4x4_msa(src, src_stride, dst, dst_stride, filter);
    } else if (8 == height) {
        common_hz_4t_4x8_msa(src, src_stride, dst, dst_stride, filter);
    } else if (16 == height) {
        common_hz_4t_4x16_msa(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_hz_4t_6w_msa(uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v16u8 out4, out5;
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);

        out4 = PCKEV_XORI128_UB(out0, out1);
        out5 = PCKEV_XORI128_UB(out2, out3);
        ST6x4_UB(out4, out5, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_4t_8x2mult_msa(uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, vec0, vec1, vec2, vec3;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= 1;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src1);
        src += (2 * src_stride);

        XORI_B2_128_SB(src0, src1);
        VSHF_B2_SH(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
        DOTP_SB2_SH(vec0, vec1, filt0, filt0, vec0, vec1);
        VSHF_B2_SH(src0, src0, src1, src1, mask1, mask1, vec2, vec3);
        DPADD_SB2_SH(vec2, vec3, filt1, filt1, vec0, vec1);
        SRARI_H2_SH(vec0, vec1, 6);
        SAT_SH2_SH(vec0, vec1, 7);
        out = PCKEV_XORI128_UB(vec0, vec1);
        ST8x2_UB(out, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void common_hz_4t_8x4mult_msa(uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_4t_8w_msa(uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if ((2 == height) || (6 == height)) {
        common_hz_4t_8x2mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    } else {
        common_hz_4t_8x4mult_msa(src, src_stride, dst, dst_stride, filter,
                                 height);
    }
}

static void common_hz_4t_12w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v16i8 vec10, vec11;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3, out4, out5;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    mask2 = LD_SB(&mc_filt_mask_arr[32]);

    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec6, vec7);
        VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec0, vec1);
        DOTP_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    out2, out3, out4, out5);
        DOTP_SB2_SH(vec0, vec1, filt0, filt0, out0, out1);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec8, vec9);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec10, vec11);
        VSHF_B2_SB(src0, src1, src2, src3, mask3, mask3, vec2, vec3);
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt1, filt1, filt1, filt1,
                     out2, out3, out4, out5);
        DPADD_SB2_SH(vec2, vec3, filt1, filt1, out0, out1);
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SRARI_H2_SH(out4, out5, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        SAT_SH2_SH(out4, out5, 7);
        tmp0 = PCKEV_XORI128_UB(out2, out3);
        tmp1 = PCKEV_XORI128_UB(out4, out5);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        ST4x4_UB(tmp0, tmp0, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_4t_16w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
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
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SRARI_H4_SH(out4, out5, out6, out7, 6);
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

static void common_hz_4t_24w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint8_t *dst1 = dst + 16;
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 filt0, filt1, mask0, mask1, mask00, mask11;
    v8i16 filt, out0, out1, out2, out3;
    v16u8 tmp0, tmp1;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;
    mask00 = mask0 + 8;
    mask11 = mask0 + 10;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src2, src4, src6);
        LD_SB4(src + 16, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        VSHF_B2_SB(src0, src0, src0, src1, mask0, mask00, vec0, vec1);
        VSHF_B2_SB(src2, src2, src2, src3, mask0, mask00, vec2, vec3);
        VSHF_B2_SB(src0, src0, src0, src1, mask1, mask11, vec4, vec5);
        VSHF_B2_SB(src2, src2, src2, src3, mask1, mask11, vec6, vec7);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1,
                     out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        ST_UB(tmp0, dst);
        dst += dst_stride;
        tmp0 = PCKEV_XORI128_UB(out2, out3);
        ST_UB(tmp0, dst);
        dst += dst_stride;

        VSHF_B2_SB(src4, src4, src4, src5, mask0, mask00, vec0, vec1);
        VSHF_B2_SB(src6, src6, src6, src7, mask0, mask00, vec2, vec3);
        VSHF_B2_SB(src4, src4, src4, src5, mask1, mask11, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src7, mask1, mask11, vec6, vec7);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1,
                     out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        ST_UB(tmp0, dst);
        dst += dst_stride;
        tmp0 = PCKEV_XORI128_UB(out2, out3);
        ST_UB(tmp0, dst);
        dst += dst_stride;

        /* 8 width */
        VSHF_B2_SB(src1, src1, src3, src3, mask0, mask0, vec0, vec1);
        VSHF_B2_SB(src5, src5, src7, src7, mask0, mask0, vec2, vec3);
        VSHF_B2_SB(src1, src1, src3, src3, mask1, mask1, vec4, vec5);
        VSHF_B2_SB(src5, src5, src7, src7, mask1, mask1, vec6, vec7);

        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1,
                     out0, out1, out2, out3);

        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST8x4_UB(tmp0, tmp1, dst1, dst_stride);
        dst1 += (4 * dst_stride);
    }
}

static void common_hz_4t_32w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, out0, out1, out2, out3, out4, out5, out6, out7;

    mask0 = LD_SB(&mc_filt_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        src0 = LD_SB(src);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src += src_stride;
        src4 = LD_SB(src);
        src6 = LD_SB(src + 16);
        src7 = LD_SB(src + 24);
        SLDI_B2_SB(src2, src6, src0, src4, src1, src5, 8);
        src += src_stride;

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);
        HORIZ_4TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1,
                                   filt0, filt1, out0, out1, out2, out3);
        HORIZ_4TAP_8WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1,
                                   filt0, filt1, out4, out5, out6, out7);
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SRARI_H4_SH(out4, out5, out6, out7, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        SAT_SH4_SH(out4, out5, out6, out7, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst + 16);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out4, out5);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out6, out7);
        ST_UB(out, dst + 16);
        dst += dst_stride;
    }
}

static void common_vt_4t_4x2_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4, src10_r, src32_r, src21_r, src43_r;
    v16i8 src2110, src4332, filt0, filt1;
    v16u8 out;
    v8i16 filt, out10;

    src -= src_stride;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_r, (v2i64) src10_r);
    src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);
    LD_SB2(src, src_stride, src3, src4);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
    src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_r, (v2i64) src32_r);
    src4332 = (v16i8) __msa_xori_b((v16u8) src4332, 128);
    out10 = FILT_4TAP_DPADD_S_H(src2110, src4332, filt0, filt1);
    out10 = __msa_srari_h(out10, 6);
    out10 = __msa_sat_s_h(out10, 7);
    out = PCKEV_XORI128_UB(out10, out10);
    ST4x2_UB(out, dst, dst_stride);
}

static void common_vt_4t_4x4multiple_msa(uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride,
                                         const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
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
        SRARI_H2_SH(out10, out32, 6);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_vt_4t_4w_msa(uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (2 == height) {
        common_vt_4t_4x2_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_vt_4t_4x4multiple_msa(src, src_stride, dst, dst_stride, filter,
                                     height);
    }
}

static void common_vt_4t_6w_msa(uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16u8 src0, src1, src2, src3, vec0, vec1, vec2, vec3, out0, out1;
    v8i16 vec01, vec12, vec23, vec30, tmp0, tmp1, tmp2, tmp3;
    v8i16 filt, filt0, filt1;

    src -= src_stride;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    SPLATI_H2_SH(filt, 0, 1, filt0, filt1);

    LD_UB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    vec0 = (v16u8) __msa_xori_b((v16u8) src0, 128);
    vec1 = (v16u8) __msa_xori_b((v16u8) src1, 128);
    vec2 = (v16u8) __msa_xori_b((v16u8) src2, 128);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src3, src0, src1, src2);
        src += (4 * src_stride);

        vec3 = (v16u8) __msa_xori_b((v16u8) src3, 128);
        ILVR_B2_SH(vec1, vec0, vec3, vec2, vec01, vec23);
        tmp0 = FILT_4TAP_DPADD_S_H(vec01, vec23, filt0, filt1);

        vec0 = __msa_xori_b((v16u8) src0, 128);
        ILVR_B2_SH(vec2, vec1, vec0, vec3, vec12, vec30);
        tmp1 = FILT_4TAP_DPADD_S_H(vec12, vec30, filt0, filt1);

        vec1 = __msa_xori_b((v16u8) src1, 128);
        vec01 = (v8i16) __msa_ilvr_b((v16i8) vec1, (v16i8) vec0);
        tmp2 = FILT_4TAP_DPADD_S_H(vec23, vec01, filt0, filt1);

        vec2 = __msa_xori_b((v16u8) src2, 128);
        vec12 = (v8i16) __msa_ilvr_b((v16i8) vec2, (v16i8) vec1);
        tmp3 = FILT_4TAP_DPADD_S_H(vec30, vec12, filt0, filt1);

        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST6x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_vt_4t_8x2_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4;
    v8i16 src01, src12, src23, src34, tmp0, tmp1, filt, filt0, filt1;
    v16u8 out;

    src -= src_stride;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    SPLATI_H2_SH(filt, 0, 1, filt0, filt1);

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    ILVR_B2_SH(src1, src0, src3, src2, src01, src23);
    tmp0 = FILT_4TAP_DPADD_S_H(src01, src23, filt0, filt1);
    ILVR_B2_SH(src2, src1, src4, src3, src12, src34);
    tmp1 = FILT_4TAP_DPADD_S_H(src12, src34, filt0, filt1);
    SRARI_H2_SH(tmp0, tmp1, 6);
    SAT_SH2_SH(tmp0, tmp1, 7);
    out = PCKEV_XORI128_UB(tmp0, tmp1);
    ST8x2_UB(out, dst, dst_stride);
}

static void common_vt_4t_8x6_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    uint32_t loop_cnt;
    uint64_t out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5;
    v8i16 vec0, vec1, vec2, vec3, vec4, tmp0, tmp1, tmp2;
    v8i16 filt, filt0, filt1;

    src -= src_stride;

    /* rearranging filter_y */
    filt = LD_SH(filter);
    SPLATI_H2_SH(filt, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SH(src1, src0, src2, src1, vec0, vec2);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB3(src, src_stride, src3, src4, src5);
        src += (3 * src_stride);

        XORI_B3_128_SB(src3, src4, src5);
        ILVR_B3_SH(src3, src2, src4, src3, src5, src4, vec1, vec3, vec4);
        tmp0 = FILT_4TAP_DPADD_S_H(vec0, vec1, filt0, filt1);
        tmp1 = FILT_4TAP_DPADD_S_H(vec2, vec3, filt0, filt1);
        tmp2 = FILT_4TAP_DPADD_S_H(vec1, vec4, filt0, filt1);
        SRARI_H2_SH(tmp0, tmp1, 6);
        tmp2 = __msa_srari_h(tmp2, 6);
        SAT_SH3_SH(tmp0, tmp1, tmp2, 7);
        PCKEV_B2_SH(tmp1, tmp0, tmp2, tmp2, tmp0, tmp2);
        XORI_B2_128_SH(tmp0, tmp2);

        out0 = __msa_copy_u_d((v2i64) tmp0, 0);
        out1 = __msa_copy_u_d((v2i64) tmp0, 1);
        out2 = __msa_copy_u_d((v2i64) tmp2, 0);
        SD(out0, dst);
        dst += dst_stride;
        SD(out1, dst);
        dst += dst_stride;
        SD(out2, dst);
        dst += dst_stride;

        src2 = src5;
        vec0 = vec3;
        vec2 = vec4;
    }
}

static void common_vt_4t_8x4mult_msa(uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
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
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        tmp0 = PCKEV_XORI128_UB(out0_r, out1_r);
        tmp1 = PCKEV_XORI128_UB(out2_r, out3_r);
        ST8x4_UB(tmp0, tmp1, dst, dst_stride);
        dst += (4 * dst_stride);

        src10_r = src98_r;
        src21_r = src109_r;
        src2 = src10;
    }
}

static void common_vt_4t_8w_msa(uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (2 == height) {
        common_vt_4t_8x2_msa(src, src_stride, dst, dst_stride, filter);
    } else if (6 == height) {
        common_vt_4t_8x6_msa(src, src_stride, dst, dst_stride, filter);
    } else {
        common_vt_4t_8x4mult_msa(src, src_stride, dst, dst_stride,
                                 filter, height);
    }
}

static void common_vt_4t_12w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v16u8 out0, out1;
    v8i16 src10, src21, src32, src43, src54, src65, src87, src109, src1211;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, filt, filt0, filt1;
    v4u32 mask = { 2, 6, 2, 6 };

    /* rearranging filter_y */
    filt = LD_SH(filter);
    SPLATI_H2_SH(filt, 0, 1, filt0, filt1);

    src -= src_stride;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    VSHF_W2_SB(src0, src1, src1, src2, mask, mask, vec0, vec1);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);
        ILVR_B2_SH(src1, src0, src3, src2, src10, src32);
        VSHF_W2_SB(src2, src3, src3, src4, mask, mask, vec2, vec3);
        VSHF_W2_SB(src4, src5, src5, src6, mask, mask, vec4, vec5);
        tmp0 = FILT_4TAP_DPADD_S_H(src10, src32, filt0, filt1);
        ILVR_B4_SH(src2, src1, src4, src3, src5, src4, src6, src5,
                   src21, src43, src54, src65);
        tmp1 = FILT_4TAP_DPADD_S_H(src21, src43, filt0, filt1);
        tmp2 = FILT_4TAP_DPADD_S_H(src32, src54, filt0, filt1);
        tmp3 = FILT_4TAP_DPADD_S_H(src43, src65, filt0, filt1);
        ILVR_B3_SH(vec1, vec0, vec3, vec2, vec5, vec4, src87, src109, src1211);
        tmp4 = FILT_4TAP_DPADD_S_H(src87, src109, filt0, filt1);
        tmp5 = FILT_4TAP_DPADD_S_H(src109, src1211, filt0, filt1);
        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
        SRARI_H2_SH(tmp4, tmp5, 6);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        SAT_SH2_SH(tmp4, tmp5, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST8x4_UB(out0, out1, dst, dst_stride);
        out0 = PCKEV_XORI128_UB(tmp4, tmp5);
        ST4x4_UB(out0, out0, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        src0 = src4;
        src1 = src5;
        src2 = src6;
        vec0 = vec4;
        vec1 = vec5;
        src2 = src6;
    }
}

static void common_vt_4t_16w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
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
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 6);
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

static void common_vt_4t_24w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    uint64_t out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, filt0, filt1;
    v16i8 src10_r, src32_r, src76_r, src98_r, src21_r, src43_r, src87_r;
    v16i8 src109_r, src10_l, src32_l, src21_l, src43_l;
    v16u8 out;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r, out0_l, out1_l;

    src -= src_stride;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    /* 16 width */
    LD_SB3(src, src_stride, src0, src1, src2);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    /* 8 width */
    LD_SB3(src + 16, src_stride, src6, src7, src8);
    src += (3 * src_stride);
    XORI_B3_128_SB(src6, src7, src8);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        /* 16 width */
        LD_SB2(src, src_stride, src3, src4);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        /* 8 width */
        LD_SB2(src + 16, src_stride, src9, src10);
        src += (2 * src_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);

        /* 16 width */
        out0_r = FILT_4TAP_DPADD_S_H(src10_r, src32_r, filt0, filt1);
        out0_l = FILT_4TAP_DPADD_S_H(src10_l, src32_l, filt0, filt1);
        out1_r = FILT_4TAP_DPADD_S_H(src21_r, src43_r, filt0, filt1);
        out1_l = FILT_4TAP_DPADD_S_H(src21_l, src43_l, filt0, filt1);

        /* 8 width */
        out2_r = FILT_4TAP_DPADD_S_H(src76_r, src98_r, filt0, filt1);
        out3_r = FILT_4TAP_DPADD_S_H(src87_r, src109_r, filt0, filt1);

        /* 16 + 8 width */
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SRARI_H2_SH(out0_l, out1_l, 6);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH2_SH(out0_l, out1_l, 7);
        out = PCKEV_XORI128_UB(out0_r, out0_l);
        ST_UB(out, dst);
        PCKEV_B2_SH(out2_r, out2_r, out3_r, out3_r, out2_r, out3_r);
        XORI_B2_128_SH(out2_r, out3_r);
        out0 = __msa_copy_u_d((v2i64) out2_r, 0);
        out1 = __msa_copy_u_d((v2i64) out3_r, 0);
        SD(out0, dst + 16);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out1_r, out1_l);
        ST_UB(out, dst);
        SD(out1, dst + 16);
        dst += dst_stride;

        /* 16 width */
        LD_SB2(src, src_stride, src5, src2);
        XORI_B2_128_SB(src5, src2);
        ILVR_B2_SB(src5, src4, src2, src5, src10_r, src21_r);
        ILVL_B2_SB(src5, src4, src2, src5, src10_l, src21_l);

        /* 8 width */
        LD_SB2(src + 16, src_stride, src11, src8);
        src += (2 * src_stride);
        XORI_B2_128_SB(src11, src8);
        ILVR_B2_SB(src11, src10, src8, src11, src76_r, src87_r);

        /* 16 width */
        out0_r = FILT_4TAP_DPADD_S_H(src32_r, src10_r, filt0, filt1);
        out0_l = FILT_4TAP_DPADD_S_H(src32_l, src10_l, filt0, filt1);
        out1_r = FILT_4TAP_DPADD_S_H(src43_r, src21_r, filt0, filt1);
        out1_l = FILT_4TAP_DPADD_S_H(src43_l, src21_l, filt0, filt1);

        /* 8 width */
        out2_r = FILT_4TAP_DPADD_S_H(src98_r, src76_r, filt0, filt1);
        out3_r = FILT_4TAP_DPADD_S_H(src109_r, src87_r, filt0, filt1);

        /* 16 + 8 width */
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SRARI_H2_SH(out0_l, out1_l, 6);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH2_SH(out0_l, out1_l, 7);
        out = PCKEV_XORI128_UB(out0_r, out0_l);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2_r, out2_r);
        ST8x1_UB(out, dst + 16);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out1_r, out1_l);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out3_r, out3_r);
        ST8x1_UB(out, dst + 16);
        dst += dst_stride;
    }
}

static void common_vt_4t_32w_mult_msa(uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter, int32_t height,
                                      int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *dst_tmp, *src_tmp;
    v16i8 src0, src1, src2, src3, src4, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src87_r, src109_r;
    v8i16 out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;
    v16i8 src10_l, src32_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src87_l, src109_l;
    v8i16 filt;
    v16i8 filt0, filt1;
    v16u8 out;

    src -= src_stride;

    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    for (cnt = (width >> 5); cnt--;) {
        dst_tmp = dst;
        src_tmp = src;

        /* 16 width */
        LD_SB3(src_tmp, src_stride, src0, src1, src2);
        XORI_B3_128_SB(src0, src1, src2);

        ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
        ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

        /* next 16 width */
        LD_SB3(src_tmp + 16, src_stride, src6, src7, src8);
        src_tmp += (3 * src_stride);

        XORI_B3_128_SB(src6, src7, src8);
        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        ILVL_B2_SB(src7, src6, src8, src7, src76_l, src87_l);

        for (loop_cnt = (height >> 1); loop_cnt--;) {
            /* 16 width */
            LD_SB2(src_tmp, src_stride, src3, src4);
            XORI_B2_128_SB(src3, src4);
            ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
            ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

            /* 16 width */
            out0_r = FILT_4TAP_DPADD_S_H(src10_r, src32_r, filt0, filt1);
            out0_l = FILT_4TAP_DPADD_S_H(src10_l, src32_l, filt0, filt1);
            out1_r = FILT_4TAP_DPADD_S_H(src21_r, src43_r, filt0, filt1);
            out1_l = FILT_4TAP_DPADD_S_H(src21_l, src43_l, filt0, filt1);

            /* 16 width */
            SRARI_H4_SH(out0_r, out1_r, out0_l, out1_l, 6);
            SAT_SH4_SH(out0_r, out1_r, out0_l, out1_l, 7);
            out = PCKEV_XORI128_UB(out0_r, out0_l);
            ST_UB(out, dst_tmp);
            out = PCKEV_XORI128_UB(out1_r, out1_l);
            ST_UB(out, dst_tmp + dst_stride);

            src10_r = src32_r;
            src21_r = src43_r;
            src10_l = src32_l;
            src21_l = src43_l;
            src2 = src4;

            /* next 16 width */
            LD_SB2(src_tmp + 16, src_stride, src9, src10);
            src_tmp += (2 * src_stride);
            XORI_B2_128_SB(src9, src10);
            ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
            ILVL_B2_SB(src9, src8, src10, src9, src98_l, src109_l);

            /* next 16 width */
            out2_r = FILT_4TAP_DPADD_S_H(src76_r, src98_r, filt0, filt1);
            out2_l = FILT_4TAP_DPADD_S_H(src76_l, src98_l, filt0, filt1);
            out3_r = FILT_4TAP_DPADD_S_H(src87_r, src109_r, filt0, filt1);
            out3_l = FILT_4TAP_DPADD_S_H(src87_l, src109_l, filt0, filt1);

            /* next 16 width */
            SRARI_H4_SH(out2_r, out3_r, out2_l, out3_l, 6);
            SAT_SH4_SH(out2_r, out3_r, out2_l, out3_l, 7);
            out = PCKEV_XORI128_UB(out2_r, out2_l);
            ST_UB(out, dst_tmp + 16);
            out = PCKEV_XORI128_UB(out3_r, out3_l);
            ST_UB(out, dst_tmp + 16 + dst_stride);

            dst_tmp += 2 * dst_stride;

            src76_r = src98_r;
            src87_r = src109_r;
            src76_l = src98_l;
            src87_l = src109_l;
            src8 = src10;
        }

        src += 32;
        dst += 32;
    }
}

static void common_vt_4t_32w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_4t_32w_mult_msa(src, src_stride, dst, dst_stride,
                              filter, height, 32);
}

static void hevc_hv_uni_4t_4x2_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst1_r;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVR_H2_SH(dst1, dst0, dst2, dst1, dst10_r, dst21_r);
    LD_SB2(src, src_stride, src3, src4);
    XORI_B2_128_SB(src3, src4);

    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    dst32_r = __msa_ilvr_h(dst3, dst2);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_r >>= 6;

    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    dst43_r = __msa_ilvr_h(dst4, dst3);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_r >>= 6;

    dst0_r = (v4i32) __msa_pckev_h((v8i16) dst1_r, (v8i16) dst0_r);
    dst0_r = (v4i32) __msa_srari_h((v8i16) dst0_r, 6);
    dst0_r = (v4i32) CLIP_SH_0_255(dst0_r);
    dst0_r = (v4i32) __msa_pckev_b((v16i8) dst0_r, (v16i8) dst0_r);

    ST4x2_UB(dst0_r, dst, dst_stride);
}

static void hevc_hv_uni_4t_4x4_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 out0_r, out1_r;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVR_H2_SH(dst1, dst0, dst2, dst1, dst10_r, dst21_r);
    LD_SB4(src, src_stride, src3, src4, src5, src6);
    XORI_B4_128_SB(src3, src4, src5, src6);

    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    dst32_r = __msa_ilvr_h(dst3, dst2);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_r >>= 6;

    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    dst43_r = __msa_ilvr_h(dst4, dst3);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_r >>= 6;

    /* row 5 */
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

    dst10_r = __msa_ilvr_h(dst5, dst4);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
    dst2_r >>= 6;

    /* row 6 */
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

    dst21_r = __msa_ilvr_h(dst2, dst5);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
    dst3_r >>= 6;

    PCKEV_H2_SH(dst1_r, dst0_r, dst3_r, dst2_r, out0_r, out1_r);
    SRARI_H2_SH(out0_r, out1_r, 6);
    CLIP_SH2_0_255(out0_r, out1_r);
    out0_r = (v8i16) __msa_pckev_b((v16i8) out1_r, (v16i8) out0_r);

    ST4x4_UB(out0_r, out0_r, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_hv_uni_4t_4multx8mult_msa(uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride,
                                           const int8_t *filter_x,
                                           const int8_t *filter_y,
                                           int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, dst9;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 out0_r, out1_r, out2_r, out3_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVR_H2_SH(dst1, dst0, dst2, dst1, dst10_r, dst21_r);

    for (loop_cnt = height >> 3; loop_cnt--;) {
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);

        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);

        /* row 3 */
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        dst32_r = __msa_ilvr_h(dst3, dst2);
        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_r >>= 6;

        /* row 4 */
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

        dst43_r = __msa_ilvr_h(dst4, dst3);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_r >>= 6;

        /* row 5 */
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

        dst54_r = __msa_ilvr_h(dst5, dst4);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_r >>= 6;

        /* row 6 */
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst6 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst6, dst6);

        dst65_r = __msa_ilvr_h(dst6, dst5);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst3_r >>= 6;

        /* row 7 */
        VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
        dst7 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst7, dst7);

        dst76_r = __msa_ilvr_h(dst7, dst6);
        dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        dst4_r >>= 6;

        /* row 8 */
        VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec0, vec1);
        dst8 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst8, dst8);

        dst87_r = __msa_ilvr_h(dst8, dst7);
        dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        dst5_r >>= 6;

        /* row 9 */
        VSHF_B2_SB(src9, src9, src9, src9, mask0, mask1, vec0, vec1);
        dst9 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst9, dst9);

        dst10_r = __msa_ilvr_h(dst9, dst8);
        dst6_r = HEVC_FILT_4TAP(dst76_r, dst10_r, filt_h0, filt_h1);
        dst6_r >>= 6;

        /* row 10 */
        VSHF_B2_SB(src10, src10, src10, src10, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

        dst21_r = __msa_ilvr_h(dst2, dst9);
        dst7_r = HEVC_FILT_4TAP(dst87_r, dst21_r, filt_h0, filt_h1);
        dst7_r >>= 6;

        PCKEV_H4_SH(dst1_r, dst0_r, dst3_r, dst2_r,
                    dst5_r, dst4_r, dst7_r, dst6_r,
                    out0_r, out1_r, out2_r, out3_r);

        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        CLIP_SH4_0_255(out0_r, out1_r, out2_r, out3_r);

        PCKEV_B2_SH(out1_r, out0_r, out3_r, out2_r, out0_r, out1_r);
        ST4x8_UB(out0_r, out1_r, dst, dst_stride);
        dst += (8 * dst_stride);
    }
}

static void hevc_hv_uni_4t_4w_msa(uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    if (2 == height) {
        hevc_hv_uni_4t_4x2_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height);
    } else if (4 == height) {
        hevc_hv_uni_4t_4x4_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height);
    } else if (0 == (height % 8)) {
        hevc_hv_uni_4t_4multx8mult_msa(src, src_stride, dst, dst_stride,
                                       filter_x, filter_y, height);
    }
}

static void hevc_hv_uni_4t_6w_msa(uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 out0_r, out1_r, out2_r, out3_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    for (loop_cnt = height >> 2; loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);

        /* row 3 */
        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        dst3 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst0_r >>= 6;
        dst0_l >>= 6;

        /* row 4 */
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
        dst4 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

        ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst1_r >>= 6;
        dst1_l >>= 6;

        /* row 5 */
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
        dst5 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

        ILVRL_H2_SH(dst5, dst4, dst10_r, dst10_l);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst10_l, filt_h0, filt_h1);

        dst2_r >>= 6;
        dst2_l >>= 6;

        /* row 6 */
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

        ILVRL_H2_SH(dst2, dst5, dst21_r, dst21_l);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst21_l, filt_h0, filt_h1);

        dst3_r >>= 6;
        dst3_l >>= 6;

        PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                    dst2_l, dst2_r, dst3_l, dst3_r,
                    out0_r, out1_r, out2_r, out3_r);

        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        CLIP_SH4_0_255(out0_r, out1_r, out2_r, out3_r);

        PCKEV_B2_SH(out1_r, out0_r, out3_r, out2_r, out0_r, out1_r);
        ST6x4_UB(out0_r, out1_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void hevc_hv_uni_4t_8x2_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 out0_r, out1_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    LD_SB2(src, src_stride, src3, src4);
    XORI_B2_128_SB(src3, src4);

    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst0_r >>= 6;
    dst0_l >>= 6;

    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst1_r >>= 6;
    dst1_l >>= 6;

    PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, out0_r, out1_r);
    SRARI_H2_SH(out0_r, out1_r, 6);
    CLIP_SH2_0_255(out0_r, out1_r);
    out0_r = (v8i16) __msa_pckev_b((v16i8) out1_r, (v16i8) out0_r);

    ST8x2_UB(out0_r, dst, dst_stride);
}

static void hevc_hv_uni_4t_8x6_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst4_r, dst4_l, dst5_r, dst5_l;
    v8i16 dst10_r, dst32_r, dst10_l, dst32_l;
    v8i16 dst21_r, dst43_r, dst21_l, dst43_l;
    v8i16 dst54_r, dst54_l, dst65_r, dst65_l;
    v8i16 dst76_r, dst76_l, dst87_r, dst87_l;
    v8i16 out0_r, out1_r, out2_r, out3_r, out4_r, out5_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dst0 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
    dst1 = const_vec;
    DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
    dst2 = const_vec;
    DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

    LD_SB2(src, src_stride, src3, src4);
    src += (2 * src_stride);

    XORI_B2_128_SB(src3, src4);

    /* row 3 */
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    dst3 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);

    dst0_r >>= 6;
    dst0_l >>= 6;

    /* row 4 */
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
    dst4 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst1_r >>= 6;
    dst1_l >>= 6;

    LD_SB2(src, src_stride, src5, src6);
    src += (2 * src_stride);

    XORI_B2_128_SB(src5, src6);

    /* row 5 */
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
    dst5 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

    ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
    dst2_r >>= 6;
    dst2_l >>= 6;

    /* row 6 */
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
    dst6 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst6, dst6);

    ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);
    dst3_r >>= 6;
    dst3_l >>= 6;

    LD_SB2(src, src_stride, src7, src8);
    src += (2 * src_stride);

    XORI_B2_128_SB(src7, src8);

    /* row 7 */
    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
    dst7 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst7, dst7);

    ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst4_l = HEVC_FILT_4TAP(dst54_l, dst76_l, filt_h0, filt_h1);

    dst4_r >>= 6;
    dst4_l >>= 6;

    /* row 8 */
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec0, vec1);
    dst8 = const_vec;
    DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst8, dst8);

    ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst5_l = HEVC_FILT_4TAP(dst65_l, dst87_l, filt_h0, filt_h1);
    dst5_r >>= 6;
    dst5_l >>= 6;

    PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                dst2_l, dst2_r, dst3_l, dst3_r, out0_r, out1_r, out2_r, out3_r);
    PCKEV_H2_SH(dst4_l, dst4_r, dst5_l, dst5_r, out4_r, out5_r);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
    SRARI_H2_SH(out4_r, out5_r, 6);
    CLIP_SH4_0_255(out0_r, out1_r, out2_r, out3_r);
    CLIP_SH2_0_255(out4_r, out5_r);

    PCKEV_B2_SH(out1_r, out0_r, out3_r, out2_r, out0_r, out1_r);
    out2_r = (v8i16) __msa_pckev_b((v16i8) out5_r, (v16i8) out4_r);

    ST8x4_UB(out0_r, out1_r, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x2_UB(out2_r, dst, dst_stride);
}

static void hevc_hv_uni_4t_8w_mult_msa(uint8_t *src,
                                       int32_t src_stride,
                                       uint8_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t height,
                                       int32_t width)
{
    uint32_t loop_cnt, cnt;
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v4i32 filt_h0, filt_h1;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16i8 mask1;
    v8i16 filter_vec, const_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 out0_r, out1_r, out2_r, out3_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    vec0 = __msa_clti_s_b((v16i8) filter_vec, 0);
    filter_vec = (v8i16) __msa_ilvr_b(vec0, (v16i8) filter_vec);

    SPLATI_W2_SW(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    const_vec = __msa_ldi_h(128);
    const_vec <<= 6;

    for (cnt = width >> 3; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB3(src_tmp, src_stride, src0, src1, src2);
        src_tmp += (3 * src_stride);

        XORI_B3_128_SB(src0, src1, src2);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

        dst0 = const_vec;
        DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst0, dst0);
        dst1 = const_vec;
        DPADD_SB2_SH(vec2, vec3, filt0, filt1, dst1, dst1);
        dst2 = const_vec;
        DPADD_SB2_SH(vec4, vec5, filt0, filt1, dst2, dst2);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

        for (loop_cnt = height >> 2; loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
            src_tmp += (4 * src_stride);

            XORI_B4_128_SB(src3, src4, src5, src6);

            /* row 3 */
            VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
            dst3 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst3, dst3);

            ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
            dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
            dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);

            dst0_r >>= 6;
            dst0_l >>= 6;

            /* row 4 */
            VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec0, vec1);
            dst4 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst4, dst4);

            ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
            dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
            dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
            dst1_r >>= 6;
            dst1_l >>= 6;

            /* row 5 */
            VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec0, vec1);
            dst5 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst5, dst5);

            ILVRL_H2_SH(dst5, dst4, dst10_r, dst10_l);
            dst2_r = HEVC_FILT_4TAP(dst32_r, dst10_r, filt_h0, filt_h1);
            dst2_l = HEVC_FILT_4TAP(dst32_l, dst10_l, filt_h0, filt_h1);

            dst2_r >>= 6;
            dst2_l >>= 6;

            /* row 6 */
            VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec0, vec1);
            dst2 = const_vec;
            DPADD_SB2_SH(vec0, vec1, filt0, filt1, dst2, dst2);

            ILVRL_H2_SH(dst2, dst5, dst21_r, dst21_l);
            dst3_r = HEVC_FILT_4TAP(dst43_r, dst21_r, filt_h0, filt_h1);
            dst3_l = HEVC_FILT_4TAP(dst43_l, dst21_l, filt_h0, filt_h1);

            dst3_r >>= 6;
            dst3_l >>= 6;

            PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r,
                        out0_r, out1_r, out2_r, out3_r);

            SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
            CLIP_SH4_0_255(out0_r, out1_r, out2_r, out3_r);

            PCKEV_B2_SH(out1_r, out0_r, out3_r, out2_r, out0_r, out1_r);
            ST8x4_UB(out0_r, out1_r, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_uni_4t_8w_msa(uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    if (2 == height) {
        hevc_hv_uni_4t_8x2_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height);
    } else if (6 == height) {
        hevc_hv_uni_4t_8x6_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height);
    } else if (0 == (height % 4)) {
        hevc_hv_uni_4t_8w_mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 8);
    }
}

static void hevc_hv_uni_4t_12w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_4t_8w_mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 8);

    hevc_hv_uni_4t_4w_msa(src + 8, src_stride, dst + 8, dst_stride,
                          filter_x, filter_y, height);
}

static void hevc_hv_uni_4t_16w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_4t_8w_mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 16);
}

static void hevc_hv_uni_4t_24w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_4t_8w_mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 24);
}

static void hevc_hv_uni_4t_32w_msa(uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_4t_8w_mult_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y, height, 32);
}

#define UNI_MC_COPY(WIDTH)                                                 \
void ff_hevc_put_hevc_uni_pel_pixels##WIDTH##_8_msa(uint8_t *dst,          \
                                                    ptrdiff_t dst_stride,  \
                                                    uint8_t *src,          \
                                                    ptrdiff_t src_stride,  \
                                                    int height,            \
                                                    intptr_t mx,           \
                                                    intptr_t my,           \
                                                    int width)             \
{                                                                          \
    copy_width##WIDTH##_msa(src, src_stride, dst, dst_stride, height);     \
}

UNI_MC_COPY(8);
UNI_MC_COPY(12);
UNI_MC_COPY(16);
UNI_MC_COPY(24);
UNI_MC_COPY(32);
UNI_MC_COPY(48);
UNI_MC_COPY(64);

#undef UNI_MC_COPY

#define UNI_MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                           \
void ff_hevc_put_hevc_uni_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,           \
                                                         ptrdiff_t             \
                                                         dst_stride,           \
                                                         uint8_t *src,         \
                                                         ptrdiff_t             \
                                                         src_stride,           \
                                                         int height,           \
                                                         intptr_t mx,          \
                                                         intptr_t my,          \
                                                         int width)            \
{                                                                              \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];              \
                                                                               \
    common_##DIR1##_##TAP##t_##WIDTH##w_msa(src, src_stride, dst, dst_stride,  \
                                            filter, height);                   \
}

UNI_MC(qpel, h, 4, 8, hz, mx);
UNI_MC(qpel, h, 8, 8, hz, mx);
UNI_MC(qpel, h, 12, 8, hz, mx);
UNI_MC(qpel, h, 16, 8, hz, mx);
UNI_MC(qpel, h, 24, 8, hz, mx);
UNI_MC(qpel, h, 32, 8, hz, mx);
UNI_MC(qpel, h, 48, 8, hz, mx);
UNI_MC(qpel, h, 64, 8, hz, mx);

UNI_MC(qpel, v, 4, 8, vt, my);
UNI_MC(qpel, v, 8, 8, vt, my);
UNI_MC(qpel, v, 12, 8, vt, my);
UNI_MC(qpel, v, 16, 8, vt, my);
UNI_MC(qpel, v, 24, 8, vt, my);
UNI_MC(qpel, v, 32, 8, vt, my);
UNI_MC(qpel, v, 48, 8, vt, my);
UNI_MC(qpel, v, 64, 8, vt, my);

UNI_MC(epel, h, 4, 4, hz, mx);
UNI_MC(epel, h, 6, 4, hz, mx);
UNI_MC(epel, h, 8, 4, hz, mx);
UNI_MC(epel, h, 12, 4, hz, mx);
UNI_MC(epel, h, 16, 4, hz, mx);
UNI_MC(epel, h, 24, 4, hz, mx);
UNI_MC(epel, h, 32, 4, hz, mx);

UNI_MC(epel, v, 4, 4, vt, my);
UNI_MC(epel, v, 6, 4, vt, my);
UNI_MC(epel, v, 8, 4, vt, my);
UNI_MC(epel, v, 12, 4, vt, my);
UNI_MC(epel, v, 16, 4, vt, my);
UNI_MC(epel, v, 24, 4, vt, my);
UNI_MC(epel, v, 32, 4, vt, my);

#undef UNI_MC

#define UNI_MC_HV(PEL, DIR, WIDTH, TAP, DIR1)                           \
void ff_hevc_put_hevc_uni_##PEL##_##DIR##WIDTH##_8_msa(uint8_t *dst,    \
                                                         ptrdiff_t      \
                                                         dst_stride,    \
                                                         uint8_t *src,  \
                                                         ptrdiff_t      \
                                                         src_stride,    \
                                                         int height,    \
                                                         intptr_t mx,   \
                                                         intptr_t my,   \
                                                         int width)     \
{                                                                       \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx - 1];           \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my - 1];           \
                                                                        \
    hevc_##DIR1##_uni_##TAP##t_##WIDTH##w_msa(src, src_stride, dst,     \
                                              dst_stride, filter_x,     \
                                              filter_y, height);        \
}

UNI_MC_HV(qpel, hv, 4, 8, hv);
UNI_MC_HV(qpel, hv, 8, 8, hv);
UNI_MC_HV(qpel, hv, 12, 8, hv);
UNI_MC_HV(qpel, hv, 16, 8, hv);
UNI_MC_HV(qpel, hv, 24, 8, hv);
UNI_MC_HV(qpel, hv, 32, 8, hv);
UNI_MC_HV(qpel, hv, 48, 8, hv);
UNI_MC_HV(qpel, hv, 64, 8, hv);

UNI_MC_HV(epel, hv, 4, 4, hv);
UNI_MC_HV(epel, hv, 6, 4, hv);
UNI_MC_HV(epel, hv, 8, 4, hv);
UNI_MC_HV(epel, hv, 12, 4, hv);
UNI_MC_HV(epel, hv, 16, 4, hv);
UNI_MC_HV(epel, hv, 24, 4, hv);
UNI_MC_HV(epel, hv, 32, 4, hv);

#undef UNI_MC_HV
