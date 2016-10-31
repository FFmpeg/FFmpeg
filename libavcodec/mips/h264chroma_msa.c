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

#include "libavutil/mips/generic_macros_msa.h"
#include "h264chroma_mips.h"

static const uint8_t chroma_mask_arr[16 * 5] = {
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    0, 2, 2, 4, 4, 6, 6, 8, 16, 18, 18, 20, 20, 22, 22, 24,
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    0, 1, 1, 2, 16, 17, 17, 18, 4, 5, 5, 6, 6, 7, 7, 8,
    0, 1, 1, 2, 16, 17, 17, 18, 16, 17, 17, 18, 18, 19, 19, 20
};

static void avc_chroma_hz_2x2_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    uint16_t out0, out1;
    v16i8 src0, src1;
    v8u16 res_r;
    v8i16 res;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[0]);

    LD_SB2(src, src_stride, src0, src1);

    src0 = __msa_vshf_b(mask, src1, src0);
    res_r = __msa_dotp_u_h((v16u8) src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    out0 = __msa_copy_u_h(res, 0);
    out1 = __msa_copy_u_h(res, 2);

    SH(out0, dst);
    dst += dst_stride;
    SH(out1, dst);
}

static void avc_chroma_hz_2x4_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2, src3;
    v8u16 res_r;
    v8i16 res;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[64]);

    LD_UB4(src, src_stride, src0, src1, src2, src3);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, src0, src2);

    src0 = (v16u8) __msa_ilvr_d((v2i64) src2, (v2i64) src0);

    res_r = __msa_dotp_u_h(src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST2x4_UB(res, 0, dst, dst_stride);
}

static void avc_chroma_hz_2x8_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8u16 res_r;
    v8i16 res;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[64]);

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, src0, src2);
    VSHF_B2_UB(src4, src5, src6, src7, mask, mask, src4, src6);

    ILVR_D2_UB(src2, src0, src6, src4, src0, src4);

    res_r = __msa_dotp_u_h(src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST2x4_UB(res, 0, dst, dst_stride);
    dst += (4 * dst_stride);

    res_r = __msa_dotp_u_h(src4, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST2x4_UB(res, 0, dst, dst_stride);
}

static void avc_chroma_hz_2w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coeff0, uint32_t coeff1,
                                 int32_t height)
{
    if (2 == height) {
        avc_chroma_hz_2x2_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    } else if (4 == height) {
        avc_chroma_hz_2x4_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_hz_2x8_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    }
}

static void avc_chroma_hz_4x2_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    v16i8 src0, src1;
    v8u16 res_r;
    v4i32 res;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[0]);

    LD_SB2(src, src_stride, src0, src1);

    src0 = __msa_vshf_b(mask, src1, src0);
    res_r = __msa_dotp_u_h((v16u8) src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v4i32) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST4x2_UB(res, dst, dst_stride);
}

static void avc_chroma_hz_4x4multiple_msa(uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride,
                                          uint32_t coeff0, uint32_t coeff1,
                                          int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3;
    v8u16 res0_r, res1_r;
    v4i32 res0, res1;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[0]);

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        VSHF_B2_UB(src0, src1, src2, src3, mask, mask, src0, src2);
        DOTP_UB2_UH(src0, src2, coeff_vec, coeff_vec, res0_r, res1_r);

        res0_r <<= 3;
        res1_r <<= 3;

        SRARI_H2_UH(res0_r, res1_r, 6);
        SAT_UH2_UH(res0_r, res1_r, 7);
        PCKEV_B2_SW(res0_r, res0_r, res1_r, res1_r, res0, res1);

        ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_chroma_hz_4w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coeff0, uint32_t coeff1,
                                 int32_t height)
{
    if (2 == height) {
        avc_chroma_hz_4x2_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    } else {
        avc_chroma_hz_4x4multiple_msa(src, src_stride, dst, dst_stride, coeff0,
                                      coeff1, height);
    }
}

static void avc_chroma_hz_8w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coeff0, uint32_t coeff1,
                                 int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, out0, out1;
    v8u16 res0, res1, res2, res3;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[32]);

    for (row = height >> 2; row--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        VSHF_B2_UB(src0, src0, src1, src1, mask, mask, src0, src1);
        VSHF_B2_UB(src2, src2, src3, src3, mask, mask, src2, src3);
        DOTP_UB4_UH(src0, src1, src2, src3, coeff_vec, coeff_vec, coeff_vec,
                    coeff_vec, res0, res1, res2, res3);
        SLLI_4V(res0, res1, res2, res3, 3);
        SRARI_H4_UH(res0, res1, res2, res3, 6);
        SAT_UH4_UH(res0, res1, res2, res3, 7);
        PCKEV_B2_UB(res1, res0, res3, res2, out0, out1);
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
    }

    if (0 != (height % 4)) {
        for (row = (height % 4); row--;) {
            src0 = LD_UB(src);
            src += src_stride;

            src0 = (v16u8) __msa_vshf_b(mask, (v16i8) src0, (v16i8) src0);

            res0 = __msa_dotp_u_h(src0, coeff_vec);
            res0 <<= 3;
            res0 = (v8u16) __msa_srari_h((v8i16) res0, 6);
            res0 = __msa_sat_u_h(res0, 7);
            res0 = (v8u16) __msa_pckev_b((v16i8) res0, (v16i8) res0);

            ST8x1_UB(res0, dst);
            dst += dst_stride;
        }
    }
}

static void avc_chroma_vt_2x2_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    uint16_t out0, out1;
    v16i8 src0, src1, src2;
    v16u8 tmp0, tmp1;
    v8i16 res;
    v8u16 res_r;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    LD_SB3(src, src_stride, src0, src1, src2);

    ILVR_B2_UB(src1, src0, src2, src1, tmp0, tmp1);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp1, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    out0 = __msa_copy_u_h(res, 0);
    out1 = __msa_copy_u_h(res, 2);

    SH(out0, dst);
    dst += dst_stride;
    SH(out1, dst);
}

static void avc_chroma_vt_2x4_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2, src3, src4;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 res;
    v8u16 res_r;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
               tmp0, tmp1, tmp2, tmp3);
    ILVR_W2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp2, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST2x4_UB(res, 0, dst, dst_stride);
}

static void avc_chroma_vt_2x8_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 res;
    v8u16 res_r;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    LD_UB4(src, src_stride, src5, src6, src7, src8);

    ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
               tmp0, tmp1, tmp2, tmp3);
    ILVR_W2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp2, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST2x4_UB(res, 0, dst, dst_stride);
    dst += (4 * dst_stride);

    ILVR_B4_UB(src5, src4, src6, src5, src7, src6, src8, src7,
               tmp0, tmp1, tmp2, tmp3);
    ILVR_W2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp2, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST2x4_UB(res, 0, dst, dst_stride);
    dst += (4 * dst_stride);
}

static void avc_chroma_vt_2w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coeff0, uint32_t coeff1,
                                 int32_t height)
{
    if (2 == height) {
        avc_chroma_vt_2x2_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    } else if (4 == height) {
        avc_chroma_vt_2x4_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_vt_2x8_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    }
}

static void avc_chroma_vt_4x2_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2;
    v16u8 tmp0, tmp1;
    v4i32 res;
    v8u16 res_r;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    LD_UB3(src, src_stride, src0, src1, src2);
    ILVR_B2_UB(src1, src0, src2, src1, tmp0, tmp1);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp1, (v2i64) tmp0);
    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v4i32) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);

    ST4x2_UB(res, dst, dst_stride);
}

static void avc_chroma_vt_4x4multiple_msa(uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride,
                                          uint32_t coeff0, uint32_t coeff1,
                                          int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8u16 res0_r, res1_r;
    v4i32 res0, res1;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    src0 = LD_UB(src);
    src += src_stride;

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
                   tmp0, tmp1, tmp2, tmp3);
        ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);
        DOTP_UB2_UH(tmp0, tmp2, coeff_vec, coeff_vec, res0_r, res1_r);

        res0_r <<= 3;
        res1_r <<= 3;

        SRARI_H2_UH(res0_r, res1_r, 6);
        SAT_UH2_UH(res0_r, res1_r, 7);
        PCKEV_B2_SW(res0_r, res0_r, res1_r, res1_r, res0, res1);

        ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void avc_chroma_vt_4w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coeff0, uint32_t coeff1,
                                 int32_t height)
{
    if (2 == height) {
        avc_chroma_vt_4x2_msa(src, src_stride, dst, dst_stride, coeff0, coeff1);
    } else {
        avc_chroma_vt_4x4multiple_msa(src, src_stride, dst, dst_stride, coeff0,
                                      coeff1, height);
    }
}

static void avc_chroma_vt_8w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coeff0, uint32_t coeff1,
                                 int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4, out0, out1;
    v8u16 res0, res1, res2, res3;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    src0 = LD_UB(src);
    src += src_stride;

    for (row = height >> 2; row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
                   src0, src1, src2, src3);
        DOTP_UB4_UH(src0, src1, src2, src3, coeff_vec, coeff_vec, coeff_vec,
                    coeff_vec, res0, res1, res2, res3);
        SLLI_4V(res0, res1, res2, res3, 3);
        SRARI_H4_UH(res0, res1, res2, res3, 6);
        SAT_UH4_UH(res0, res1, res2, res3, 7);
        PCKEV_B2_UB(res1, res0, res3, res2, out0, out1);

        ST8x4_UB(out0, out1, dst, dst_stride);

        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void avc_chroma_hv_2x2_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coef_hor0, uint32_t coef_hor1,
                                  uint32_t coef_ver0, uint32_t coef_ver1)
{
    uint16_t out0, out1;
    v16u8 src0, src1, src2;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v8i16 res_vert;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[48]);

    LD_UB3(src, src_stride, src0, src1, src2);
    VSHF_B2_UB(src0, src1, src1, src2, mask, mask, src0, src1);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res_vert = (v8i16) __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);

    out0 = __msa_copy_u_h(res_vert, 0);
    out1 = __msa_copy_u_h(res_vert, 1);

    SH(out0, dst);
    dst += dst_stride;
    SH(out1, dst);
}

static void avc_chroma_hv_2x4_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coef_hor0, uint32_t coef_hor1,
                                  uint32_t coef_ver0, uint32_t coef_ver1)
{
    v16u8 src0, src1, src2, src3, src4;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v8i16 res;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[48]);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, tmp0, tmp1);
    VSHF_B2_UB(src1, src2, src3, src4, mask, mask, tmp2, tmp3);
    ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, src0, src1);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = (v8i16) __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);

    ST2x4_UB(res, 0, dst, dst_stride);
}

static void avc_chroma_hv_2x8_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coef_hor0, uint32_t coef_hor1,
                                  uint32_t coef_ver0, uint32_t coef_ver1)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v8i16 res;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[48]);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    LD_UB4(src, src_stride, src5, src6, src7, src8);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, tmp0, tmp1);
    VSHF_B2_UB(src1, src2, src3, src4, mask, mask, tmp2, tmp3);
    ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, src0, src1);
    VSHF_B2_UB(src4, src5, src6, src7, mask, mask, tmp0, tmp1);
    VSHF_B2_UB(src5, src6, src7, src8, mask, mask, tmp2, tmp3);
    ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, src4, src5);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);

    ST2x4_UB(res, 0, dst, dst_stride);
    dst += (4 * dst_stride);

    DOTP_UB2_UH(src4, src5, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);

    ST2x4_UB(res, 0, dst, dst_stride);
}

static void avc_chroma_hv_2w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coef_hor0, uint32_t coef_hor1,
                                 uint32_t coef_ver0, uint32_t coef_ver1,
                                 int32_t height)
{
    if (2 == height) {
        avc_chroma_hv_2x2_msa(src, src_stride, dst, dst_stride, coef_hor0,
                              coef_hor1, coef_ver0, coef_ver1);
    } else if (4 == height) {
        avc_chroma_hv_2x4_msa(src, src_stride, dst, dst_stride, coef_hor0,
                              coef_hor1, coef_ver0, coef_ver1);
    } else if (8 == height) {
        avc_chroma_hv_2x8_msa(src, src_stride, dst, dst_stride, coef_hor0,
                              coef_hor1, coef_ver0, coef_ver1);
    }
}

static void avc_chroma_hv_4x2_msa(uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint32_t coef_hor0, uint32_t coef_hor1,
                                  uint32_t coef_ver0, uint32_t coef_ver1)
{
    v16u8 src0, src1, src2;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v16i8 mask;
    v4i32 res;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[0]);
    LD_UB3(src, src_stride, src0, src1, src2);
    VSHF_B2_UB(src0, src1, src1, src2, mask, mask, src0, src1);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = (v4i32) __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);

    ST4x2_UB(res, dst, dst_stride);
}

static void avc_chroma_hv_4x4multiple_msa(uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride,
                                          uint32_t coef_hor0,
                                          uint32_t coef_hor1,
                                          uint32_t coef_ver0,
                                          uint32_t coef_ver1,
                                          int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4;
    v8u16 res_hz0, res_hz1, res_hz2, res_hz3;
    v8u16 res_vt0, res_vt1, res_vt2, res_vt3;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);
    v4i32 res0, res1;

    mask = LD_SB(&chroma_mask_arr[0]);

    src0 = LD_UB(src);
    src += src_stride;

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        VSHF_B2_UB(src0, src1, src1, src2, mask, mask, src0, src1);
        VSHF_B2_UB(src2, src3, src3, src4, mask, mask, src2, src3);
        DOTP_UB4_UH(src0, src1, src2, src3, coeff_hz_vec, coeff_hz_vec,
                    coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1, res_hz2,
                    res_hz3);
        MUL4(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_hz2,
             coeff_vt_vec1, res_hz3, coeff_vt_vec0, res_vt0, res_vt1, res_vt2,
             res_vt3);
        ADD2(res_vt0, res_vt1, res_vt2, res_vt3, res_vt0, res_vt1);
        SRARI_H2_UH(res_vt0, res_vt1, 6);
        SAT_UH2_UH(res_vt0, res_vt1, 7);
        PCKEV_B2_SW(res_vt0, res_vt0, res_vt1, res_vt1, res0, res1);

        ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void avc_chroma_hv_4w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coef_hor0, uint32_t coef_hor1,
                                 uint32_t coef_ver0, uint32_t coef_ver1,
                                 int32_t height)
{
    if (2 == height) {
        avc_chroma_hv_4x2_msa(src, src_stride, dst, dst_stride, coef_hor0,
                              coef_hor1, coef_ver0, coef_ver1);
    } else {
        avc_chroma_hv_4x4multiple_msa(src, src_stride, dst, dst_stride,
                                      coef_hor0, coef_hor1, coef_ver0,
                                      coef_ver1, height);
    }
}

static void avc_chroma_hv_8w_msa(uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint32_t coef_hor0, uint32_t coef_hor1,
                                 uint32_t coef_ver0, uint32_t coef_ver1,
                                 int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4, out0, out1;
    v8u16 res_hz0, res_hz1, res_hz2, res_hz3, res_hz4;
    v8u16 res_vt0, res_vt1, res_vt2, res_vt3;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[32]);

    src0 = LD_UB(src);
    src += src_stride;

    src0 = (v16u8) __msa_vshf_b(mask, (v16i8) src0, (v16i8) src0);
    res_hz0 = __msa_dotp_u_h(src0, coeff_hz_vec);

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        VSHF_B2_UB(src1, src1, src2, src2, mask, mask, src1, src2);
        VSHF_B2_UB(src3, src3, src4, src4, mask, mask, src3, src4);
        DOTP_UB4_UH(src1, src2, src3, src4, coeff_hz_vec, coeff_hz_vec,
                    coeff_hz_vec, coeff_hz_vec, res_hz1, res_hz2, res_hz3,
                    res_hz4);
        MUL4(res_hz1, coeff_vt_vec0, res_hz2, coeff_vt_vec0, res_hz3,
             coeff_vt_vec0, res_hz4, coeff_vt_vec0, res_vt0, res_vt1, res_vt2,
             res_vt3);

        res_vt0 += (res_hz0 * coeff_vt_vec1);
        res_vt1 += (res_hz1 * coeff_vt_vec1);
        res_vt2 += (res_hz2 * coeff_vt_vec1);
        res_vt3 += (res_hz3 * coeff_vt_vec1);

        SRARI_H4_UH(res_vt0, res_vt1, res_vt2, res_vt3, 6);
        SAT_UH4_UH(res_vt0, res_vt1, res_vt2, res_vt3, 7);
        PCKEV_B2_UB(res_vt1, res_vt0, res_vt3, res_vt2, out0, out1);
        ST8x4_UB(out0, out1, dst, dst_stride);

        dst += (4 * dst_stride);

        res_hz0 = res_hz4;
    }
}

static void avc_chroma_hz_and_aver_dst_2x2_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    uint16_t out0, out1;
    uint32_t load0, load1;
    v16i8 src0, src1;
    v16u8 dst_data = { 0 };
    v8u16 res_r;
    v16u8 res;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[0]);

    LD_SB2(src, src_stride, src0, src1);

    load0 = LW(dst);
    load1 = LW(dst + dst_stride);

    INSERT_W2_UB(load0, load1, dst_data);

    src0 = __msa_vshf_b(mask, src1, src0);

    res_r = __msa_dotp_u_h((v16u8) src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v16u8) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    dst_data = __msa_aver_u_b(res, dst_data);

    out0 = __msa_copy_u_h((v8i16) dst_data, 0);
    out1 = __msa_copy_u_h((v8i16) dst_data, 2);

    SH(out0, dst);
    dst += dst_stride;
    SH(out1, dst);
}

static void avc_chroma_hz_and_aver_dst_2x4_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2, src3;
    v16u8 dst0, dst1, dst2, dst3;
    v8u16 res_r;
    v16i8 res, mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[64]);

    LD_UB4(src, src_stride, src0, src1, src2, src3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 1, (v8i16) dst1);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 2, (v8i16) dst2);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 3, (v8i16) dst3);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, src0, src2);

    src0 = (v16u8) __msa_ilvr_d((v2i64) src2, (v2i64) src0);

    res_r = __msa_dotp_u_h(src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);

    ST2x4_UB(dst0, 0, dst, dst_stride);
}

static void avc_chroma_hz_and_aver_dst_2x8_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8u16 res0_r, res1_r;
    v16u8 res0, res1, mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_UB(&chroma_mask_arr[64]);

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);

    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 1, (v8i16) dst1);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 2, (v8i16) dst2);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 3, (v8i16) dst3);

    dst4 = (v16u8) __msa_insve_h((v8i16) dst4, 1, (v8i16) dst5);
    dst4 = (v16u8) __msa_insve_h((v8i16) dst4, 2, (v8i16) dst6);
    dst4 = (v16u8) __msa_insve_h((v8i16) dst4, 3, (v8i16) dst7);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, src0, src2);
    VSHF_B2_UB(src4, src5, src6, src7, mask, mask, src4, src6);
    ILVR_D2_UB(src2, src0, src6, src4, src0, src4);
    DOTP_UB2_UH(src0, src4, coeff_vec, coeff_vec, res0_r, res1_r);

    res0_r <<= 3;
    res1_r <<= 3;

    SRARI_H2_UH(res0_r, res1_r, 6);
    SAT_UH2_UH(res0_r, res1_r, 7);
    PCKEV_B2_UB(res0_r, res0_r, res1_r, res1_r, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst4, dst0, dst4);

    ST2x4_UB(dst0, 0, dst, dst_stride);
    dst += (4 * dst_stride);
    ST2x4_UB(dst4, 0, dst, dst_stride);
}

static void avc_chroma_hz_and_aver_dst_2w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coeff0, uint32_t coeff1,
                                              int32_t height)
{
    if (2 == height) {
        avc_chroma_hz_and_aver_dst_2x2_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    } else if (4 == height) {
        avc_chroma_hz_and_aver_dst_2x4_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_hz_and_aver_dst_2x8_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    }
}

static void avc_chroma_hz_and_aver_dst_4x2_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    uint32_t load0, load1;
    v16i8 src0, src1;
    v16u8 dst_data = { 0 };
    v8u16 res_r;
    v16i8 res, mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[0]);

    LD_SB2(src, src_stride, src0, src1);

    load0 = LW(dst);
    load1 = LW(dst + dst_stride);

    INSERT_W2_UB(load0, load1, dst_data);

    src0 = __msa_vshf_b(mask, src1, src0);

    res_r = __msa_dotp_u_h((v16u8) src0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    dst_data = __msa_aver_u_b((v16u8) res, dst_data);

    ST4x2_UB(dst_data, dst, dst_stride);
}

static void avc_chroma_hz_and_aver_dst_4x4multiple_msa(uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride,
                                                       uint32_t coeff0,
                                                       uint32_t coeff1,
                                                       int32_t height)
{
    uint32_t load0, load1;
    uint32_t row;
    v16u8 src0, src1, src2, src3;
    v16u8 dst0 = { 0 };
    v16u8 dst1 = { 0 };
    v8u16 res0_r, res1_r;
    v16u8 res0, res1, mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_UB(&chroma_mask_arr[0]);

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        load0 = LW(dst);
        load1 = LW(dst + dst_stride);

        INSERT_W2_UB(load0, load1, dst0);

        load0 = LW(dst + 2 * dst_stride);
        load1 = LW(dst + 3 * dst_stride);

        INSERT_W2_UB(load0, load1, dst1);

        VSHF_B2_UB(src0, src1, src2, src3, mask, mask, src0, src2);
        DOTP_UB2_UH(src0, src2, coeff_vec, coeff_vec, res0_r, res1_r);

        res0_r <<= 3;
        res1_r <<= 3;

        SRARI_H2_UH(res0_r, res1_r, 6);
        SAT_UH2_UH(res0_r, res1_r, 7);
        PCKEV_B2_UB(res0_r, res0_r, res1_r, res1_r, res0, res1);
        AVER_UB2_UB(res0, dst0, res1, dst1, dst0, dst1);

        ST4x4_UB(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_chroma_hz_and_aver_dst_4w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coeff0, uint32_t coeff1,
                                              int32_t height)
{
    if (2 == height) {
        avc_chroma_hz_and_aver_dst_4x2_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    } else {
        avc_chroma_hz_and_aver_dst_4x4multiple_msa(src, src_stride,
                                                   dst, dst_stride,
                                                   coeff0, coeff1, height);
    }
}

static void avc_chroma_hz_and_aver_dst_8w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coeff0, uint32_t coeff1,
                                              int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, out0, out1;
    v8u16 res0, res1, res2, res3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 mask;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    mask = LD_SB(&chroma_mask_arr[32]);

    for (row = height >> 2; row--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        VSHF_B2_UB(src0, src0, src1, src1, mask, mask, src0, src1);
        VSHF_B2_UB(src2, src2, src3, src3, mask, mask, src2, src3);
        DOTP_UB4_UH(src0, src1, src2, src3, coeff_vec, coeff_vec, coeff_vec,
                    coeff_vec, res0, res1, res2, res3);
        SLLI_4V(res0, res1, res2, res3, 3);
        SRARI_H4_UH(res0, res1, res2, res3, 6);
        SAT_UH4_UH(res0, res1, res2, res3, 7);
        PCKEV_B2_UB(res1, res0, res3, res2, out0, out1);
        PCKEV_D2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
        AVER_UB2_UB(out0, dst0, out1, dst1, out0, out1);
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void avc_chroma_vt_and_aver_dst_2x2_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    uint16_t out0, out1;
    uint32_t load0, load1;
    v16i8 src0, src1, src2, tmp0, tmp1, res;
    v16u8 dst_data = { 0 };
    v8u16 res_r;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    LD_SB3(src, src_stride, src0, src1, src2);
    load0 = LW(dst);
    load1 = LW(dst + dst_stride);

    INSERT_W2_UB(load0, load1, dst_data);

    ILVR_B2_SB(src1, src0, src2, src1, tmp0, tmp1);

    tmp0 = (v16i8) __msa_ilvr_d((v2i64) tmp1, (v2i64) tmp0);
    res_r = __msa_dotp_u_h((v16u8) tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    dst_data = __msa_aver_u_b((v16u8) res, dst_data);
    out0 = __msa_copy_u_h((v8i16) dst_data, 0);
    out1 = __msa_copy_u_h((v8i16) dst_data, 2);

    SH(out0, dst);
    dst += dst_stride;
    SH(out1, dst);
}

static void avc_chroma_vt_and_aver_dst_2x4_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    uint32_t load0, load1;
    v16i8 src0, src1, src2, src3, src4;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8u16 res_r;
    v8i16 res;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);
    v16u8 dst_data = { 0 };

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);

    load0 = LW(dst);
    load1 = LW(dst + dst_stride);

    dst_data = (v16u8) __msa_insert_h((v8i16) dst_data, 0, load0);
    dst_data = (v16u8) __msa_insert_h((v8i16) dst_data, 1, load1);

    load0 = LW(dst + 2 * dst_stride);
    load1 = LW(dst + 3 * dst_stride);

    dst_data = (v16u8) __msa_insert_h((v8i16) dst_data, 2, load0);
    dst_data = (v16u8) __msa_insert_h((v8i16) dst_data, 3, load1);

    ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
               tmp0, tmp1, tmp2, tmp3);
    ILVR_W2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp2, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    res = (v8i16) __msa_aver_u_b((v16u8) res, dst_data);

    ST2x4_UB(res, 0, dst, dst_stride);
    dst += (4 * dst_stride);
}

static void avc_chroma_vt_and_aver_dst_2x8_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    uint32_t load0, load1, load2, load3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8i16 res;
    v8u16 res_r;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);
    v16u8 dst_data0 = { 0 };
    v16u8 dst_data1 = { 0 };

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    LD_SB4(src, src_stride, src5, src6, src7, src8);

    LW4(dst, dst_stride, load0, load1, load2, load3);

    dst_data0 = (v16u8) __msa_insert_h((v8i16) dst_data0, 0, load0);
    dst_data0 = (v16u8) __msa_insert_h((v8i16) dst_data0, 1, load1);
    dst_data0 = (v16u8) __msa_insert_h((v8i16) dst_data0, 2, load2);
    dst_data0 = (v16u8) __msa_insert_h((v8i16) dst_data0, 3, load3);

    LW4(dst + 4 * dst_stride, dst_stride, load0, load1, load2, load3);

    dst_data1 = (v16u8) __msa_insert_h((v8i16) dst_data1, 0, load0);
    dst_data1 = (v16u8) __msa_insert_h((v8i16) dst_data1, 1, load1);
    dst_data1 = (v16u8) __msa_insert_h((v8i16) dst_data1, 2, load2);
    dst_data1 = (v16u8) __msa_insert_h((v8i16) dst_data1, 3, load3);

    ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
               tmp0, tmp1, tmp2, tmp3);

    ILVR_W2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp2, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    res = (v8i16) __msa_aver_u_b((v16u8) res, dst_data0);

    ST2x4_UB(res, 0, dst, dst_stride);
    dst += (4 * dst_stride);

    ILVR_B4_UB(src5, src4, src6, src5, src7, src6, src8, src7,
               tmp0, tmp1, tmp2, tmp3);

    ILVR_W2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);

    tmp0 = (v16u8) __msa_ilvr_d((v2i64) tmp2, (v2i64) tmp0);

    res_r = __msa_dotp_u_h(tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);

    res = (v8i16) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    res = (v8i16) __msa_aver_u_b((v16u8) res, dst_data1);

    ST2x4_UB(res, 0, dst, dst_stride);
}

static void avc_chroma_vt_and_aver_dst_2w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coeff0, uint32_t coeff1,
                                              int32_t height)
{
    if (2 == height) {
        avc_chroma_vt_and_aver_dst_2x2_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    } else if (4 == height) {
        avc_chroma_vt_and_aver_dst_2x4_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    } else if (8 == height) {
        avc_chroma_vt_and_aver_dst_2x8_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    }
}

static void avc_chroma_vt_and_aver_dst_4x2_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coeff0, uint32_t coeff1)
{
    uint32_t load0, load1;
    v16i8 src0, src1, src2, tmp0, tmp1;
    v16u8 dst_data = { 0 };
    v8u16 res_r;
    v16u8 res;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    LD_SB3(src, src_stride, src0, src1, src2);

    load0 = LW(dst);
    load1 = LW(dst + dst_stride);

    INSERT_W2_UB(load0, load1, dst_data);
    ILVR_B2_SB(src1, src0, src2, src1, tmp0, tmp1);

    tmp0 = (v16i8) __msa_ilvr_d((v2i64) tmp1, (v2i64) tmp0);

    res_r = __msa_dotp_u_h((v16u8) tmp0, coeff_vec);
    res_r <<= 3;
    res_r = (v8u16) __msa_srari_h((v8i16) res_r, 6);
    res_r = __msa_sat_u_h(res_r, 7);
    res = (v16u8) __msa_pckev_b((v16i8) res_r, (v16i8) res_r);
    res = __msa_aver_u_b(res, dst_data);

    ST4x2_UB(res, dst, dst_stride);
}

static void avc_chroma_vt_and_aver_dst_4x4mul_msa(uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  uint32_t coeff0,
                                                  uint32_t coeff1,
                                                  int32_t height)
{
    uint32_t load0, load1, row;
    v16i8 src0, src1, src2, src3, src4;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v16u8 dst0 = { 0 };
    v16u8 dst1 = { 0 };
    v8u16 res0_r, res1_r;
    v16u8 res0, res1;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    src0 = LD_SB(src);
    src += src_stride;

    for (row = (height >> 2); row--;) {
        LD_SB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        load0 = LW(dst);
        load1 = LW(dst + dst_stride);

        INSERT_W2_UB(load0, load1, dst0);
        load0 = LW(dst + 2 * dst_stride);
        load1 = LW(dst + 3 * dst_stride);
        INSERT_W2_UB(load0, load1, dst1);

        ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
                   tmp0, tmp1, tmp2, tmp3);
        ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, tmp0, tmp2);
        DOTP_UB2_UH(tmp0, tmp2, coeff_vec, coeff_vec, res0_r, res1_r);

        res0_r <<= 3;
        res1_r <<= 3;

        SRARI_H2_UH(res0_r, res1_r, 6);
        SAT_UH2_UH(res0_r, res1_r, 7);
        PCKEV_B2_UB(res0_r, res0_r, res1_r, res1_r, res0, res1);
        AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);

        ST4x4_UB(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void avc_chroma_vt_and_aver_dst_4w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coeff0, uint32_t coeff1,
                                              int32_t height)
{
    if (2 == height) {
        avc_chroma_vt_and_aver_dst_4x2_msa(src, src_stride, dst, dst_stride,
                                           coeff0, coeff1);
    } else {
        avc_chroma_vt_and_aver_dst_4x4mul_msa(src, src_stride, dst, dst_stride,
                                              coeff0, coeff1, height);
    }
}

static void avc_chroma_vt_and_aver_dst_8w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coeff0, uint32_t coeff1,
                                              int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4;
    v16u8 out0, out1;
    v8u16 res0, res1, res2, res3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 coeff_vec0 = __msa_fill_b(coeff0);
    v16i8 coeff_vec1 = __msa_fill_b(coeff1);
    v16u8 coeff_vec = (v16u8) __msa_ilvr_b(coeff_vec0, coeff_vec1);

    src0 = LD_UB(src);
    src += src_stride;

    for (row = height >> 2; row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        ILVR_B4_UB(src1, src0, src2, src1, src3, src2, src4, src3,
                   src0, src1, src2, src3);
        DOTP_UB4_UH(src0, src1, src2, src3, coeff_vec, coeff_vec, coeff_vec,
                    coeff_vec, res0, res1, res2, res3);
        SLLI_4V(res0, res1, res2, res3, 3);
        SRARI_H4_UH(res0, res1, res2, res3, 6);
        SAT_UH4_UH(res0, res1, res2, res3, 7);
        PCKEV_B2_UB(res1, res0, res3, res2, out0, out1);
        PCKEV_D2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
        AVER_UB2_UB(out0, dst0, out1, dst1, out0, out1);
        ST8x4_UB(out0, out1, dst, dst_stride);

        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void avc_chroma_hv_and_aver_dst_2x2_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coef_hor0,
                                               uint32_t coef_hor1,
                                               uint32_t coef_ver0,
                                               uint32_t coef_ver1)
{
    uint16_t out0, out1;
    v16u8 dst0, dst1;
    v16u8 src0, src1, src2;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v16i8 res, mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[48]);

    LD_UB3(src, src_stride, src0, src1, src2);
    LD_UB2(dst, dst_stride, dst0, dst1);
    VSHF_B2_UB(src0, src1, src1, src2, mask, mask, src0, src1);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 1, (v8i16) dst1);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);
    out0 = __msa_copy_u_h((v8i16) dst0, 0);
    out1 = __msa_copy_u_h((v8i16) dst0, 1);

    SH(out0, dst);
    dst += dst_stride;
    SH(out1, dst);
}

static void avc_chroma_hv_and_aver_dst_2x4_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coef_hor0,
                                               uint32_t coef_hor1,
                                               uint32_t coef_ver0,
                                               uint32_t coef_ver1)
{
    v16u8 src0, src1, src2, src3, src4;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v16u8 dst0, dst1, dst2, dst3;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v16i8 res, mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[48]);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, tmp0, tmp1);
    VSHF_B2_UB(src1, src2, src3, src4, mask, mask, tmp2, tmp3);
    ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, src0, src1);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);

    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 1, (v8i16) dst1);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 2, (v8i16) dst2);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 3, (v8i16) dst3);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);

    ST2x4_UB(dst0, 0, dst, dst_stride);
}

static void avc_chroma_hv_and_aver_dst_2x8_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coef_hor0,
                                               uint32_t coef_hor1,
                                               uint32_t coef_ver0,
                                               uint32_t coef_ver1)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v16i8 res, mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[48]);

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    LD_UB4(src, src_stride, src5, src6, src7, src8);

    LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);

    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 1, (v8i16) dst1);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 2, (v8i16) dst2);
    dst0 = (v16u8) __msa_insve_h((v8i16) dst0, 3, (v8i16) dst3);

    dst4 = (v16u8) __msa_insve_h((v8i16) dst4, 1, (v8i16) dst5);
    dst4 = (v16u8) __msa_insve_h((v8i16) dst4, 2, (v8i16) dst6);
    dst4 = (v16u8) __msa_insve_h((v8i16) dst4, 3, (v8i16) dst7);

    VSHF_B2_UB(src0, src1, src2, src3, mask, mask, tmp0, tmp1);
    VSHF_B2_UB(src1, src2, src3, src4, mask, mask, tmp2, tmp3);
    ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, src0, src1);
    VSHF_B2_UB(src4, src5, src6, src7, mask, mask, tmp0, tmp1);
    VSHF_B2_UB(src5, src6, src7, src8, mask, mask, tmp2, tmp3);
    ILVR_D2_UB(tmp1, tmp0, tmp3, tmp2, src4, src5);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);

    ST2x4_UB(dst0, 0, dst, dst_stride);
    dst += (4 * dst_stride);

    DOTP_UB2_UH(src4, src5, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);
    dst4 = __msa_aver_u_b((v16u8) res, dst4);

    ST2x4_UB(dst4, 0, dst, dst_stride);
}

static void avc_chroma_hv_and_aver_dst_2w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coef_hor0,
                                              uint32_t coef_hor1,
                                              uint32_t coef_ver0,
                                              uint32_t coef_ver1,
                                              int32_t height)
{
    if (2 == height) {
        avc_chroma_hv_and_aver_dst_2x2_msa(src, src_stride, dst, dst_stride,
                                           coef_hor0, coef_hor1,
                                           coef_ver0, coef_ver1);
    } else if (4 == height) {
        avc_chroma_hv_and_aver_dst_2x4_msa(src, src_stride, dst, dst_stride,
                                           coef_hor0, coef_hor1,
                                           coef_ver0, coef_ver1);
    } else if (8 == height) {
        avc_chroma_hv_and_aver_dst_2x8_msa(src, src_stride, dst, dst_stride,
                                           coef_hor0, coef_hor1,
                                           coef_ver0, coef_ver1);
    }
}

static void avc_chroma_hv_and_aver_dst_4x2_msa(uint8_t *src, int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint32_t coef_hor0,
                                               uint32_t coef_hor1,
                                               uint32_t coef_ver0,
                                               uint32_t coef_ver1)
{
    v16u8 src0, src1, src2;
    v16u8 dst0, dst1;
    v8u16 res_hz0, res_hz1, res_vt0, res_vt1;
    v16i8 res, mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[0]);

    LD_UB3(src, src_stride, src0, src1, src2);
    LD_UB2(dst, dst_stride, dst0, dst1);
    VSHF_B2_UB(src0, src1, src1, src2, mask, mask, src0, src1);
    DOTP_UB2_UH(src0, src1, coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1);
    MUL2(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_vt0, res_vt1);

    res_vt0 += res_vt1;
    res_vt0 = (v8u16) __msa_srari_h((v8i16) res_vt0, 6);
    res_vt0 = __msa_sat_u_h(res_vt0, 7);
    res = __msa_pckev_b((v16i8) res_vt0, (v16i8) res_vt0);
    dst0 = (v16u8) __msa_insve_w((v4i32) dst0, 1, (v4i32) dst1);
    dst0 = __msa_aver_u_b((v16u8) res, dst0);

    ST4x2_UB(dst0, dst, dst_stride);
}

static void avc_chroma_hv_and_aver_dst_4x4mul_msa(uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  uint32_t coef_hor0,
                                                  uint32_t coef_hor1,
                                                  uint32_t coef_ver0,
                                                  uint32_t coef_ver1,
                                                  int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4;
    v16u8 dst0, dst1, dst2, dst3;
    v8u16 res_hz0, res_hz1, res_hz2, res_hz3;
    v8u16 res_vt0, res_vt1, res_vt2, res_vt3;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);
    v16u8 res0, res1;

    mask = LD_SB(&chroma_mask_arr[0]);

    src0 = LD_UB(src);
    src += src_stride;

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);

        VSHF_B2_UB(src0, src1, src1, src2, mask, mask, src0, src1);
        VSHF_B2_UB(src2, src3, src3, src4, mask, mask, src2, src3);
        DOTP_UB4_UH(src0, src1, src2, src3, coeff_hz_vec, coeff_hz_vec,
                    coeff_hz_vec, coeff_hz_vec, res_hz0, res_hz1, res_hz2,
                    res_hz3);
        MUL4(res_hz0, coeff_vt_vec1, res_hz1, coeff_vt_vec0, res_hz2,
             coeff_vt_vec1, res_hz3, coeff_vt_vec0, res_vt0, res_vt1, res_vt2,
             res_vt3);
        ADD2(res_vt0, res_vt1, res_vt2, res_vt3, res_vt0, res_vt1);
        SRARI_H2_UH(res_vt0, res_vt1, 6);
        SAT_UH2_UH(res_vt0, res_vt1, 7);
        PCKEV_B2_UB(res_vt0, res_vt0, res_vt1, res_vt1, res0, res1);

        dst0 = (v16u8) __msa_insve_w((v4i32) dst0, 1, (v4i32) dst1);
        dst1 = (v16u8) __msa_insve_w((v4i32) dst2, 1, (v4i32) dst3);

        AVER_UB2_UB(res0, dst0, res1, dst1, dst0, dst1);

        ST4x4_UB(dst0, dst1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void avc_chroma_hv_and_aver_dst_4w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coef_hor0,
                                              uint32_t coef_hor1,
                                              uint32_t coef_ver0,
                                              uint32_t coef_ver1,
                                              int32_t height)
{
    if (2 == height) {
        avc_chroma_hv_and_aver_dst_4x2_msa(src, src_stride, dst, dst_stride,
                                           coef_hor0, coef_hor1,
                                           coef_ver0, coef_ver1);
    } else {
        avc_chroma_hv_and_aver_dst_4x4mul_msa(src, src_stride, dst, dst_stride,
                                              coef_hor0, coef_hor1,
                                              coef_ver0, coef_ver1, height);
    }
}

static void avc_chroma_hv_and_aver_dst_8w_msa(uint8_t *src, int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint32_t coef_hor0,
                                              uint32_t coef_hor1,
                                              uint32_t coef_ver0,
                                              uint32_t coef_ver1,
                                              int32_t height)
{
    uint32_t row;
    v16u8 src0, src1, src2, src3, src4, out0, out1;
    v8u16 res_hz0, res_hz1, res_hz2;
    v8u16 res_hz3, res_hz4;
    v8u16 res_vt0, res_vt1, res_vt2, res_vt3;
    v16u8 dst0, dst1, dst2, dst3;
    v16i8 mask;
    v16i8 coeff_hz_vec0 = __msa_fill_b(coef_hor0);
    v16i8 coeff_hz_vec1 = __msa_fill_b(coef_hor1);
    v16u8 coeff_hz_vec = (v16u8) __msa_ilvr_b(coeff_hz_vec0, coeff_hz_vec1);
    v8u16 coeff_vt_vec0 = (v8u16) __msa_fill_h(coef_ver0);
    v8u16 coeff_vt_vec1 = (v8u16) __msa_fill_h(coef_ver1);

    mask = LD_SB(&chroma_mask_arr[32]);

    src0 = LD_UB(src);
    src += src_stride;

    src0 = (v16u8) __msa_vshf_b(mask, (v16i8) src0, (v16i8) src0);
    res_hz0 = __msa_dotp_u_h(src0, coeff_hz_vec);

    for (row = (height >> 2); row--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        VSHF_B2_UB(src1, src1, src2, src2, mask, mask, src1, src2);
        VSHF_B2_UB(src3, src3, src4, src4, mask, mask, src3, src4);
        DOTP_UB4_UH(src1, src2, src3, src4, coeff_hz_vec, coeff_hz_vec,
                    coeff_hz_vec, coeff_hz_vec, res_hz1, res_hz2, res_hz3,
                    res_hz4);
        MUL4(res_hz1, coeff_vt_vec0, res_hz2, coeff_vt_vec0, res_hz3,
             coeff_vt_vec0, res_hz4, coeff_vt_vec0, res_vt0, res_vt1, res_vt2,
             res_vt3);

        res_vt0 += (res_hz0 * coeff_vt_vec1);
        res_vt1 += (res_hz1 * coeff_vt_vec1);
        res_vt2 += (res_hz2 * coeff_vt_vec1);
        res_vt3 += (res_hz3 * coeff_vt_vec1);

        SRARI_H4_UH(res_vt0, res_vt1, res_vt2, res_vt3, 6);
        SAT_UH4_UH(res_vt0, res_vt1, res_vt2, res_vt3, 7);

        PCKEV_B2_UB(res_vt1, res_vt0, res_vt3, res_vt2, out0, out1);
        PCKEV_D2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
        AVER_UB2_UB(out0, dst0, out1, dst1, out0, out1);
        ST8x4_UB(out0, out1, dst, dst_stride);
        dst += (4 * dst_stride);

        res_hz0 = res_hz4;
    }
}

static void copy_width8_msa(uint8_t *src, int32_t src_stride,
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

static void avg_width4_msa(uint8_t *src, int32_t src_stride,
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

static void avg_width8_msa(uint8_t *src, int32_t src_stride,
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

void ff_put_h264_chroma_mc8_msa(uint8_t *dst, uint8_t *src,
                                int stride, int height, int x, int y)
{
    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (x && y) {
        avc_chroma_hv_8w_msa(src, stride, dst,
                             stride, x, (8 - x), y, (8 - y), height);
    } else if (x) {
        avc_chroma_hz_8w_msa(src, stride, dst, stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_8w_msa(src, stride, dst, stride, y, (8 - y), height);
    } else {
        copy_width8_msa(src, stride, dst, stride, height);
    }
}

void ff_put_h264_chroma_mc4_msa(uint8_t *dst, uint8_t *src,
                                int stride, int height, int x, int y)
{
    int32_t cnt;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (x && y) {
        avc_chroma_hv_4w_msa(src, stride, dst,
                             stride, x, (8 - x), y, (8 - y), height);
    } else if (x) {
        avc_chroma_hz_4w_msa(src, stride, dst, stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_4w_msa(src, stride, dst, stride, y, (8 - y), height);
    } else {
        for (cnt = height; cnt--;) {
            *((uint32_t *) dst) = *((uint32_t *) src);

            src += stride;
            dst += stride;
        }
    }
}

void ff_put_h264_chroma_mc2_msa(uint8_t *dst, uint8_t *src,
                                int stride, int height, int x, int y)
{
    int32_t cnt;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (x && y) {
        avc_chroma_hv_2w_msa(src, stride, dst,
                             stride, x, (8 - x), y, (8 - y), height);
    } else if (x) {
        avc_chroma_hz_2w_msa(src, stride, dst, stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_2w_msa(src, stride, dst, stride, y, (8 - y), height);
    } else {
        for (cnt = height; cnt--;) {
            *((uint16_t *) dst) = *((uint16_t *) src);

            src += stride;
            dst += stride;
        }
    }
}

void ff_avg_h264_chroma_mc8_msa(uint8_t *dst, uint8_t *src,
                                int stride, int height, int x, int y)
{
    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);


    if (x && y) {
        avc_chroma_hv_and_aver_dst_8w_msa(src, stride, dst,
                                          stride, x, (8 - x), y,
                                          (8 - y), height);
    } else if (x) {
        avc_chroma_hz_and_aver_dst_8w_msa(src, stride, dst,
                                          stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_and_aver_dst_8w_msa(src, stride, dst,
                                          stride, y, (8 - y), height);
    } else {
        avg_width8_msa(src, stride, dst, stride, height);
    }
}

void ff_avg_h264_chroma_mc4_msa(uint8_t *dst, uint8_t *src,
                                int stride, int height, int x, int y)
{
    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (x && y) {
        avc_chroma_hv_and_aver_dst_4w_msa(src, stride, dst,
                                          stride, x, (8 - x), y,
                                          (8 - y), height);
    } else if (x) {
        avc_chroma_hz_and_aver_dst_4w_msa(src, stride, dst,
                                          stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_and_aver_dst_4w_msa(src, stride, dst,
                                          stride, y, (8 - y), height);
    } else {
        avg_width4_msa(src, stride, dst, stride, height);
    }
}

void ff_avg_h264_chroma_mc2_msa(uint8_t *dst, uint8_t *src,
                                int stride, int height, int x, int y)
{
    int32_t cnt;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    if (x && y) {
        avc_chroma_hv_and_aver_dst_2w_msa(src, stride, dst,
                                          stride, x, (8 - x), y,
                                          (8 - y), height);
    } else if (x) {
        avc_chroma_hz_and_aver_dst_2w_msa(src, stride, dst,
                                          stride, x, (8 - x), height);
    } else if (y) {
        avc_chroma_vt_and_aver_dst_2w_msa(src, stride, dst,
                                          stride, y, (8 - y), height);
    } else {
        for (cnt = height; cnt--;) {
            dst[0] = (dst[0] + src[0] + 1) >> 1;
            dst[1] = (dst[1] + src[1] + 1) >> 1;

            src += stride;
            dst += stride;
        }
    }
}
