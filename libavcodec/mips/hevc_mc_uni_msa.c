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

static const uint8_t ff_hevc_mask_arr[16 * 3] __attribute__((aligned(0x40))) = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
};

#define HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3,                  \
                                   mask0, mask1, mask2, mask3,              \
                                   filt0, filt1, filt2, filt3,              \
                                   out0, out1)                              \
{                                                                           \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m,  vec4_m, vec5_m, vec6_m, vec7_m;  \
                                                                            \
    VSHF_B2_SB(src0, src1, src2, src3, mask0, mask0, vec0_m, vec1_m);       \
    DOTP_SB2_SH(vec0_m, vec1_m, filt0, filt0, out0, out1);                  \
    VSHF_B2_SB(src0, src1, src2, src3, mask1, mask1, vec2_m, vec3_m);       \
    DPADD_SB2_SH(vec2_m, vec3_m, filt1, filt1, out0, out1);                 \
    VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec4_m, vec5_m);       \
    DPADD_SB2_SH(vec4_m, vec5_m, filt2, filt2, out0, out1);                 \
    VSHF_B2_SB(src0, src1, src2, src3, mask3, mask3, vec6_m, vec7_m);       \
    DPADD_SB2_SH(vec6_m, vec7_m, filt3, filt3, out0, out1);                 \
}

#define HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3,                    \
                                   mask0, mask1, mask2, mask3,                \
                                   filt0, filt1, filt2, filt3,                \
                                   out0, out1, out2, out3)                    \
{                                                                             \
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m, vec4_m, vec5_m, vec6_m, vec7_m;     \
                                                                              \
    VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);         \
    DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,   \
                out0, out1, out2, out3);                                      \
    VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0_m, vec1_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2_m, vec3_m);         \
    DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt2, filt2, filt2, filt2,  \
                 out0, out1, out2, out3);                                     \
    VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4_m, vec5_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6_m, vec7_m);         \
    DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt1, filt1, filt1, filt1,  \
                 out0, out1, out2, out3);                                     \
    VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec4_m, vec5_m);         \
    VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec6_m, vec7_m);         \
    DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt3, filt3, filt3, filt3,  \
                 out0, out1, out2, out3);                                     \
}

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

static void copy_width8_msa(const uint8_t *src, int32_t src_stride,
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

static void copy_width12_msa(const uint8_t *src, int32_t src_stride,
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

static void copy_width16_msa(const uint8_t *src, int32_t src_stride,
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

static void copy_width24_msa(const uint8_t *src, int32_t src_stride,
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

static void copy_width32_msa(const uint8_t *src, int32_t src_stride,
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

static void copy_width48_msa(const uint8_t *src, int32_t src_stride,
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

static void copy_width64_msa(const uint8_t *src, int32_t src_stride,
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

static void common_hz_8t_4x4_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1;

    mask0 = LD_UB(&ff_hevc_mask_arr[16]);
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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_8t_4x8_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 filt0, filt1, filt2, filt3;
    v16i8 src0, src1, src2, src3;
    v16u8 mask0, mask1, mask2, mask3, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&ff_hevc_mask_arr[16]);
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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
}

static void common_hz_8t_4x16_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  const int8_t *filter)
{
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&ff_hevc_mask_arr[16]);
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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
    dst += (8 * dst_stride);

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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
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

static void common_hz_8t_8w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, tmp0, tmp1;
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m, vec4_m, vec5_m, vec6_m, vec7_m;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&ff_hevc_mask_arr[0]);
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

        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);
        DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0_m, vec1_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2_m, vec3_m);
        DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt2, filt2, filt2, filt2,
                     out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4_m, vec5_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6_m, vec7_m);
        DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt1, filt1, filt1, filt1,
                     out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec4_m, vec5_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec6_m, vec7_m);
        DPADD_SB4_SH(vec4_m, vec5_m, vec6_m, vec7_m, filt3, filt3, filt3, filt3,
                     out0, out1, out2, out3);

        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_8t_12w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16u8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask00;
    v16u8 tmp0, tmp1, tmp2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3, out4, out5;

    mask00 = LD_UB(&ff_hevc_mask_arr[0]);
    mask0 = LD_UB(&ff_hevc_mask_arr[16]);

    src = src - 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask00 + 2;
    mask2 = mask00 + 4;
    mask3 = mask00 + 6;
    mask4 = mask0 + 2;
    mask5 = mask0 + 4;
    mask6 = mask0 + 6;

    for (loop_cnt = 4; loop_cnt--;) {
        /* 8 width */
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        /* 4 width */
        LD_SB4(src + 8, src_stride, src4, src5, src6, src7);

        XORI_B4_128_SB(src0, src1, src2, src3);
        XORI_B4_128_SB(src4, src5, src6, src7);
        src += (4 * src_stride);

        VSHF_B2_SB(src0, src0, src1, src1, mask00, mask00, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask00, mask00, vec2, vec3);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, out0,
                    out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, out0,
                     out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1, out0,
                     out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt3, filt3, filt3, filt3, out0,
                     out1, out2, out3);

        /* 4 width */
        VSHF_B2_SB(src4, src5, src6, src7, mask0, mask0, vec0, vec1);
        DOTP_SB2_SH(vec0, vec1, filt0, filt0, out4, out5);
        VSHF_B2_SB(src4, src5, src6, src7, mask4, mask4, vec2, vec3);
        DPADD_SB2_SH(vec2, vec3, filt1, filt1, out4, out5);
        VSHF_B2_SB(src4, src5, src6, src7, mask5, mask5, vec4, vec5);
        DPADD_SB2_SH(vec4, vec5, filt2, filt2, out4, out5);
        VSHF_B2_SB(src4, src5, src6, src7, mask6, mask6, vec6, vec7);
        DPADD_SB2_SH(vec6, vec7, filt3, filt3, out4, out5);

        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SRARI_H2_SH(out4, out5, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        SAT_SH2_SH(out4, out5, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        tmp1 = PCKEV_XORI128_UB(out2, out3);
        tmp2 = PCKEV_XORI128_UB(out4, out5);

        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        ST_W4(tmp2, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_8t_16w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&ff_hevc_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB2(src, src_stride, src0, src2);
        LD_SB2(src + 8, src_stride, src1, src3);
        src += (2 * src_stride);

        LD_SB2(src, src_stride, src4, src6);
        LD_SB2(src + 8, src_stride, src5, src7);
        src += (2 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
        XORI_B4_128_SB(src4, src5, src6, src7);
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

        HORIZ_8TAP_8WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1, mask2,
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

static void common_hz_8t_24w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3;
    v16u8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7, out;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9, vec10;
    v16i8 vec11;
    v8i16 out0, out1, out2, out3, out8, out9, filt;

    mask0 = LD_UB(&ff_hevc_mask_arr[0]);
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

    for (loop_cnt = 16; loop_cnt--;) {
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
        DPADD_SB4_SH(vec0, vec8, vec2, vec9, filt2, filt2, filt2, filt2,
                     out0, out8, out2, out9);
        DPADD_SB2_SH(vec1, vec3, filt2, filt2, out1, out3);
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
                     out0, out8, out2, out9);
        DPADD_SB2_SH(vec5, vec7, filt3, filt3, out1, out3);
        SRARI_H4_SH(out0, out8, out2, out9, 6);
        SRARI_H2_SH(out1, out3, 6);
        SAT_SH4_SH(out0, out8, out2, out9, 7);
        SAT_SH2_SH(out1, out3, 7);
        out = PCKEV_XORI128_UB(out8, out9);
        ST_D2(out, 0, 1, dst + 16, dst_stride);
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
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, filt2, filt3;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&ff_hevc_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        src0 = LD_SB(src);
        src1 = LD_SB(src + 8);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src += src_stride;
        XORI_B4_128_SB(src0, src1, src2, src3);

        src4 = LD_SB(src);
        src5 = LD_SB(src + 8);
        src6 = LD_SB(src + 16);
        src7 = LD_SB(src + 24);
        src += src_stride;
        XORI_B4_128_SB(src4, src5, src6, src7);

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

        HORIZ_8TAP_8WID_4VECS_FILT(src4, src5, src6, src7, mask0, mask1, mask2,
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

static void common_hz_8t_48w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, filt2, filt3, vec0, vec1, vec2;
    v16i8 src4;
    v16u8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7, out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_UB(&ff_hevc_mask_arr[0]);
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

    for (loop_cnt = 64; loop_cnt--;) {
        src0 = LD_SB(src);
        src1 = LD_SB(src + 8);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 32);
        src4 = LD_SB(src + 40);
        src += src_stride;

        XORI_B4_128_SB(src0, src1, src2, src3);
        src4 = (v16i8) __msa_xori_b((v16u8) src4, 128);

        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask0, mask0, mask0,
                   vec0, vec1, vec2);
        DOTP_SB3_SH(vec0, vec1, vec2, filt0, filt0, filt0, out0, out1, out2);
        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask1, mask1, mask1,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt1, filt1, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt1);
        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask2, mask2, mask2,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt2, filt2, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt2);

        VSHF_B3_SB(src0, src0, src1, src1, src2, src2, mask3, mask3, mask3,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt3, filt3, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt3);

        SRARI_H2_SH(out0, out1, 6);
        out3 = __msa_srari_h(out2, 6);
        SAT_SH3_SH(out0, out1, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);

        VSHF_B3_SB(src2, src3, src3, src3, src4, src4, mask4, mask0, mask0,
                   vec0, vec1, vec2);
        DOTP_SB3_SH(vec0, vec1, vec2, filt0, filt0, filt0, out0, out1, out2);
        VSHF_B3_SB(src2, src3, src3, src3, src4, src4, mask5, mask1, mask1,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt1, filt1, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt1);
        VSHF_B3_SB(src2, src3, src3, src3, src4, src4, mask6, mask2, mask2,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt2, filt2, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt2);
        VSHF_B3_SB(src2, src3, src3, src3, src4, src4, mask7, mask3, mask3,
                   vec0, vec1, vec2);
        DPADD_SB2_SH(vec0, vec1, filt3, filt3, out0, out1);
        out2 = __msa_dpadd_s_h(out2, vec2, filt3);

        SRARI_H2_SH(out0, out1, 6);
        out2 = __msa_srari_h(out2, 6);
        SAT_SH3_SH(out0, out1, out2, 7);
        out = PCKEV_XORI128_UB(out3, out0);
        ST_UB(out, dst + 16);
        out = PCKEV_XORI128_UB(out1, out2);
        ST_UB(out, dst + 32);
        dst += dst_stride;
    }
}

static void common_hz_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    int32_t loop_cnt;
    v16u8 mask0, mask1, mask2, mask3, out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 filt0, filt1, filt2, filt3;
    v8i16 res0, res1, res2, res3, filt;

    mask0 = LD_UB(&ff_hevc_mask_arr[0]);
    src -= 3;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = height; loop_cnt--;) {
        LD_SB8(src, 8, src0, src1, src2, src3, src4, src5, src6, src7);
        src += src_stride;

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2, vec3);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, res0,
                    res1, res2, res3);
        VSHF_B2_SB(src0, src0, src1, src1, mask2, mask2, vec0, vec1);
        VSHF_B2_SB(src2, src2, src3, src3, mask2, mask2, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, res0,
                     res1, res2, res3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1, res0,
                     res1, res2, res3);
        VSHF_B2_SB(src0, src0, src1, src1, mask3, mask3, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask3, mask3, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt3, filt3, filt3, filt3, res0,
                     res1, res2, res3);

        SRARI_H4_SH(res0, res1, res2, res3, 6);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        out = PCKEV_XORI128_UB(res0, res1);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(res2, res3);
        ST_UB(out, dst + 16);

        VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0, vec1);
        VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2, vec3);
        DOTP_SB4_SH(vec0, vec1, vec2, vec3, filt0, filt0, filt0, filt0, res0,
                    res1, res2, res3);
        VSHF_B2_SB(src4, src4, src5, src5, mask2, mask2, vec0, vec1);
        VSHF_B2_SB(src6, src6, src7, src7, mask2, mask2, vec2, vec3);
        DPADD_SB4_SH(vec0, vec1, vec2, vec3, filt2, filt2, filt2, filt2, res0,
                     res1, res2, res3);
        VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt1, filt1, filt1, filt1, res0,
                     res1, res2, res3);
        VSHF_B2_SB(src4, src4, src5, src5, mask3, mask3, vec4, vec5);
        VSHF_B2_SB(src6, src6, src7, src7, mask3, mask3, vec6, vec7);
        DPADD_SB4_SH(vec4, vec5, vec6, vec7, filt3, filt3, filt3, filt3, res0,
                     res1, res2, res3);

        SRARI_H4_SH(res0, res1, res2, res3, 6);
        SAT_SH4_SH(res0, res1, res2, res3, 7);
        out = PCKEV_XORI128_UB(res0, res1);
        ST_UB(out, dst + 32);
        out = PCKEV_XORI128_UB(res2, res3);
        ST_UB(out, dst + 48);
        dst += dst_stride;
    }
}

static void common_vt_8t_4w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    uint32_t res = (height & 0x07) >> 1;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, src13, src14;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src2110, src4332, src6554, src8776;
    v16i8 src1110_r, src1211_r, src1312_r, src1413_r, src12111110, src14131312;
    v16i8 src10998, filt0, filt1, filt2, filt3;
    v8i16 filt, out10, out32, out54, out76;

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

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);
        LD_SB4(src, src_stride, src11, src12, src13, src14);
        src += (4 * src_stride);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        ILVR_B4_SB(src11, src10, src12, src11, src13, src12, src14, src13,
                   src1110_r, src1211_r, src1312_r, src1413_r);
        ILVR_D2_SB(src87_r, src76_r, src109_r, src98_r, src8776, src10998);
        ILVR_D2_SB(src1211_r, src1110_r, src1413_r, src1312_r,
                   src12111110, src14131312);
        XORI_B2_128_SB(src8776, src10998);
        XORI_B2_128_SB(src12111110, src14131312);

        DOTP_SB2_SH(src2110, src4332, filt0, filt0, out10, out32);
        DOTP_SB2_SH(src6554, src8776, filt0, filt0, out54, out76);
        DPADD_SB2_SH(src4332, src6554, filt1, filt1, out10, out32);
        DPADD_SB2_SH(src8776, src10998, filt1, filt1, out54, out76);
        DPADD_SB2_SH(src6554, src8776, filt2, filt2, out10, out32);
        DPADD_SB2_SH(src10998, src12111110, filt2, filt2, out54, out76);
        DPADD_SB2_SH(src8776, src10998, filt3, filt3, out10, out32);
        DPADD_SB2_SH(src12111110, src14131312, filt3, filt3, out54, out76);
        SRARI_H2_SH(out10, out32, 6);
        SRARI_H2_SH(out54, out76, 6);
        SAT_SH2_SH(out10, out32, 7);
        SAT_SH2_SH(out54, out76, 7);
        out0 = PCKEV_XORI128_UB(out10, out32);
        out1 = PCKEV_XORI128_UB(out54, out76);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        src2110 = src10998;
        src4332 = src12111110;
        src6554 = src14131312;
        src6 = src14;
    }
    for (; res--; ) {
        LD_SB2(src, src_stride, src7, src8);
        src += 2 * src_stride;
        ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
        src8776 = (v16i8)__msa_ilvr_d((v2i64) src87_r, (v2i64) src76_r);
        src8776 = (v16i8)__msa_xori_b(src8776, 128);
        out10 = (v8i16)__msa_dotp_s_h((v16i8) src2110, (v16i8) filt0);
        out10 = (v8i16)__msa_dpadd_s_h((v8i16) out10, src4332, filt1);
        out10 = (v8i16)__msa_dpadd_s_h((v8i16) out10, src6554, filt2);
        out10 = (v8i16)__msa_dpadd_s_h((v8i16) out10, src8776, filt3);
        out10 = (v8i16)__msa_srari_h((v8i16) out10, 6);
        out10 = (v8i16)__msa_sat_s_h((v8i16) out10, 7);
        out0  = (v16u8)__msa_pckev_b((v16i8) out10, (v16i8) out10);
        out0  = (v16u8)__msa_xori_b((v16u8) out0, 128);
        ST_W2(out0, 0, 1, dst, dst_stride);
        dst += 2 * dst_stride;
        src2110 = src4332;
        src4332 = src6554;
        src6554 = src8776;
        src6 = src8;
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
        DOTP_SB4_SH(src10_r, src21_r, src32_r, src43_r, filt0, filt0, filt0,
                    filt0, out0_r, out1_r, out2_r, out3_r);
        DPADD_SB4_SH(src32_r, src43_r, src54_r, src65_r, filt1, filt1, filt1,
                     filt1, out0_r, out1_r, out2_r, out3_r);
        DPADD_SB4_SH(src54_r, src65_r, src76_r, src87_r, filt2, filt2, filt2,
                     filt2, out0_r, out1_r, out2_r, out3_r);
        DPADD_SB4_SH(src76_r, src87_r, src98_r, src109_r, filt3, filt3, filt3,
                     filt3, out0_r, out1_r, out2_r, out3_r);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        tmp0 = PCKEV_XORI128_UB(out0_r, out1_r);
        tmp1 = PCKEV_XORI128_UB(out2_r, out3_r);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
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

static void common_vt_8t_12w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    uint32_t out2, out3;
    uint64_t out0, out1;
    v16u8 tmp0, tmp1, tmp2, tmp3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 filt0, filt1, filt2, filt3;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r, src21_r, src43_r;
    v16i8 src65_r, src87_r, src109_r, src10_l, src32_l, src54_l, src76_l;
    v16i8 src98_l, src21_l, src43_l, src65_l, src87_l, src109_l;
    v8i16 filt, out0_r, out1_r, out2_r, out3_r, out0_l, out1_l, out2_l, out3_l;

    src -= (3 * src_stride);

    filt = LD_SH(filter);
    SPLATI_H4_SB(filt, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    ILVR_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_r, src32_r,
               src54_r, src21_r);
    ILVR_B2_SB(src4, src3, src6, src5, src43_r, src65_r);
    ILVL_B4_SB(src1, src0, src3, src2, src5, src4, src2, src1, src10_l, src32_l,
               src54_l, src21_l);
    ILVL_B2_SB(src4, src3, src6, src5, src43_l, src65_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src7, src8, src9, src10);
        XORI_B4_128_SB(src7, src8, src9, src10);
        src += (4 * src_stride);

        ILVR_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_r,
                   src87_r, src98_r, src109_r);
        ILVL_B4_SB(src7, src6, src8, src7, src9, src8, src10, src9, src76_l,
                   src87_l, src98_l, src109_l);
        out0_r = HEVC_FILT_8TAP_SH(src10_r, src32_r, src54_r, src76_r, filt0,
                                   filt1, filt2, filt3);
        out1_r = HEVC_FILT_8TAP_SH(src21_r, src43_r, src65_r, src87_r, filt0,
                                   filt1, filt2, filt3);
        out2_r = HEVC_FILT_8TAP_SH(src32_r, src54_r, src76_r, src98_r, filt0,
                                   filt1, filt2, filt3);
        out3_r = HEVC_FILT_8TAP_SH(src43_r, src65_r, src87_r, src109_r, filt0,
                                   filt1, filt2, filt3);
        out0_l = HEVC_FILT_8TAP_SH(src10_l, src32_l, src54_l, src76_l, filt0,
                                   filt1, filt2, filt3);
        out1_l = HEVC_FILT_8TAP_SH(src21_l, src43_l, src65_l, src87_l, filt0,
                                   filt1, filt2, filt3);
        out2_l = HEVC_FILT_8TAP_SH(src32_l, src54_l, src76_l, src98_l, filt0,
                                   filt1, filt2, filt3);
        out3_l = HEVC_FILT_8TAP_SH(src43_l, src65_l, src87_l, src109_l, filt0,
                                   filt1, filt2, filt3);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SRARI_H4_SH(out0_l, out1_l, out2_l, out3_l, 6);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH4_SH(out0_l, out1_l, out2_l, out3_l, 7);
        PCKEV_B4_UB(out0_l, out0_r, out1_l, out1_r, out2_l, out2_r, out3_l,
                    out3_r, tmp0, tmp1, tmp2, tmp3);
        XORI_B4_128_UB(tmp0, tmp1, tmp2, tmp3);

        out0 = __msa_copy_u_d((v2i64) tmp0, 0);
        out1 = __msa_copy_u_d((v2i64) tmp1, 0);
        out2 = __msa_copy_u_w((v4i32) tmp0, 2);
        out3 = __msa_copy_u_w((v4i32) tmp1, 2);
        SD(out0, dst);
        SW(out2, (dst + 8));
        dst += dst_stride;
        SD(out1, dst);
        SW(out3, (dst + 8));
        dst += dst_stride;
        out0 = __msa_copy_u_d((v2i64) tmp2, 0);
        out1 = __msa_copy_u_d((v2i64) tmp3, 0);
        out2 = __msa_copy_u_w((v4i32) tmp2, 2);
        out3 = __msa_copy_u_w((v4i32) tmp3, 2);
        SD(out0, dst);
        SW(out2, (dst + 8));
        dst += dst_stride;
        SD(out1, dst);
        SW(out3, (dst + 8));
        dst += dst_stride;

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
        out0_r = HEVC_FILT_8TAP_SH(src10_r, src32_r, src54_r, src76_r, filt0,
                                   filt1, filt2, filt3);
        out1_r = HEVC_FILT_8TAP_SH(src21_r, src43_r, src65_r, src87_r, filt0,
                                   filt1, filt2, filt3);
        out2_r = HEVC_FILT_8TAP_SH(src32_r, src54_r, src76_r, src98_r, filt0,
                                   filt1, filt2, filt3);
        out3_r = HEVC_FILT_8TAP_SH(src43_r, src65_r, src87_r, src109_r, filt0,
                                   filt1, filt2, filt3);
        out0_l = HEVC_FILT_8TAP_SH(src10_l, src32_l, src54_l, src76_l, filt0,
                                   filt1, filt2, filt3);
        out1_l = HEVC_FILT_8TAP_SH(src21_l, src43_l, src65_l, src87_l, filt0,
                                   filt1, filt2, filt3);
        out2_l = HEVC_FILT_8TAP_SH(src32_l, src54_l, src76_l, src98_l, filt0,
                                   filt1, filt2, filt3);
        out3_l = HEVC_FILT_8TAP_SH(src43_l, src65_l, src87_l, src109_l, filt0,
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
            out0_r = HEVC_FILT_8TAP_SH(src10_r, src32_r, src54_r, src76_r,
                                       filt0, filt1, filt2, filt3);
            out1_r = HEVC_FILT_8TAP_SH(src21_r, src43_r, src65_r, src87_r,
                                       filt0, filt1, filt2, filt3);
            out2_r = HEVC_FILT_8TAP_SH(src32_r, src54_r, src76_r, src98_r,
                                       filt0, filt1, filt2, filt3);
            out3_r = HEVC_FILT_8TAP_SH(src43_r, src65_r, src87_r, src109_r,
                                       filt0, filt1, filt2, filt3);
            out0_l = HEVC_FILT_8TAP_SH(src10_l, src32_l, src54_l, src76_l,
                                       filt0, filt1, filt2, filt3);
            out1_l = HEVC_FILT_8TAP_SH(src21_l, src43_l, src65_l, src87_l,
                                       filt0, filt1, filt2, filt3);
            out2_l = HEVC_FILT_8TAP_SH(src32_l, src54_l, src76_l, src98_l,
                                       filt0, filt1, filt2, filt3);
            out3_l = HEVC_FILT_8TAP_SH(src43_l, src65_l, src87_l, src109_l,
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

static void common_vt_8t_24w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              16);

    common_vt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride, filter,
                        height);
}

static void common_vt_8t_32w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              32);
}

static void common_vt_8t_48w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              48);
}

static void common_vt_8t_64w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_msa(src, src_stride, dst, dst_stride, filter, height,
                              64);
}

static void hevc_hv_uni_8t_4w_msa(const uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    uint32_t loop_cnt;
    uint32_t res = height & 0x07;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src9, src10, src11, src12, src13, src14;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst117, dst128, dst139, dst1410;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst98_r, dst1110_r, dst1312_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r, dst109_r, dst1211_r, dst1413_r;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);

    src -= ((3 * src_stride) + 3);
    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src3, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src4, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src5, mask0, mask1, mask2, mask3,
               vec8, vec9, vec10, vec11);
    VSHF_B4_SB(src3, src6, mask0, mask1, mask2, mask3,
               vec12, vec13, vec14, vec15);

    dst30 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                              filt3);
    dst41 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                              filt3);
    dst52 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                              filt3);
    dst63 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2,
                              filt3);

    ILVRL_H2_SH(dst41, dst30, dst10_r, dst43_r);
    ILVRL_H2_SH(dst52, dst41, dst21_r, dst54_r);
    ILVRL_H2_SH(dst63, dst52, dst32_r, dst65_r);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = height >> 3; loop_cnt--;) {
        LD_SB8(src, src_stride, src7, src8, src9, src10, src11, src12, src13,
               src14);
        src += (8 * src_stride);
        XORI_B8_128_SB(src7, src8, src9, src10, src11, src12, src13, src14);

        VSHF_B4_SB(src7, src11, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src8, src12, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src9, src13, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src10, src14, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);

        dst117 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                   filt3);
        dst128 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);
        dst139 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1,
                                   filt2, filt3);
        dst1410 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                   filt2, filt3);

        dst76_r = __msa_ilvr_h(dst117, dst66);
        ILVRL_H2_SH(dst128, dst117, dst87_r, dst1211_r);
        ILVRL_H2_SH(dst139, dst128, dst98_r, dst1312_r);
        ILVRL_H2_SH(dst1410, dst139, dst109_r, dst1413_r);
        dst117 = (v8i16) __msa_splati_d((v2i64) dst117, 1);
        dst1110_r = __msa_ilvr_h(dst117, dst1410);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst2_r = HEVC_FILT_8TAP(dst32_r, dst54_r, dst76_r, dst98_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst3_r = HEVC_FILT_8TAP(dst43_r, dst65_r, dst87_r, dst109_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst4_r = HEVC_FILT_8TAP(dst54_r, dst76_r, dst98_r, dst1110_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst5_r = HEVC_FILT_8TAP(dst65_r, dst87_r, dst109_r, dst1211_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst6_r = HEVC_FILT_8TAP(dst76_r, dst98_r, dst1110_r, dst1312_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst7_r = HEVC_FILT_8TAP(dst87_r, dst109_r, dst1211_r, dst1413_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);

        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRA_4V(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        SRARI_W4_SW(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRARI_W4_SW(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        SAT_SW4_SW(dst0_r, dst1_r, dst2_r, dst3_r, 7);
        SAT_SW4_SW(dst4_r, dst5_r, dst6_r, dst7_r, 7);
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst1_r);
        PCKEV_H2_SW(dst5_r, dst4_r, dst7_r, dst6_r, dst4_r, dst5_r);
        out0 = PCKEV_XORI128_UB(dst0_r, dst1_r);
        out1 = PCKEV_XORI128_UB(dst4_r, dst5_r);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst32_r = dst1110_r;
        dst54_r = dst1312_r;
        dst21_r = dst109_r;
        dst43_r = dst1211_r;
        dst65_r = dst1413_r;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst1410, 1);
    }
    if (res) {
        LD_SB8(src, src_stride, src7, src8, src9, src10, src11, src12, src13,
               src14);
        XORI_B8_128_SB(src7, src8, src9, src10, src11, src12, src13, src14);

        VSHF_B4_SB(src7, src11, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src8, src12, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src9, src13, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        VSHF_B4_SB(src10, src14, mask0, mask1, mask2, mask3,
                   vec12, vec13, vec14, vec15);

        dst117 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                   filt3);
        dst128 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);
        dst139 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1,
                                   filt2, filt3);
        dst1410 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                   filt2, filt3);

        dst76_r = __msa_ilvr_h(dst117, dst66);
        ILVRL_H2_SH(dst128, dst117, dst87_r, dst1211_r);
        ILVRL_H2_SH(dst139, dst128, dst98_r, dst1312_r);
        ILVRL_H2_SH(dst1410, dst139, dst109_r, dst1413_r);
        dst117 = (v8i16) __msa_splati_d((v2i64) dst117, 1);
        dst1110_r = __msa_ilvr_h(dst117, dst1410);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst2_r = HEVC_FILT_8TAP(dst32_r, dst54_r, dst76_r, dst98_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst3_r = HEVC_FILT_8TAP(dst43_r, dst65_r, dst87_r, dst109_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst4_r = HEVC_FILT_8TAP(dst54_r, dst76_r, dst98_r, dst1110_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst5_r = HEVC_FILT_8TAP(dst65_r, dst87_r, dst109_r, dst1211_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst6_r = HEVC_FILT_8TAP(dst76_r, dst98_r, dst1110_r, dst1312_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst7_r = HEVC_FILT_8TAP(dst87_r, dst109_r, dst1211_r, dst1413_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);

        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRA_4V(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        SRARI_W4_SW(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRARI_W4_SW(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        SAT_SW4_SW(dst0_r, dst1_r, dst2_r, dst3_r, 7);
        SAT_SW4_SW(dst4_r, dst5_r, dst6_r, dst7_r, 7);
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst1_r);
        PCKEV_H2_SW(dst5_r, dst4_r, dst7_r, dst6_r, dst4_r, dst5_r);
        out0 = PCKEV_XORI128_UB(dst0_r, dst1_r);
        out1 = PCKEV_XORI128_UB(dst4_r, dst5_r);
        if (res == 2) {
            ST_W2(out0, 0, 1, dst, dst_stride);
        } else if(res == 4) {
            ST_W4(out0, 0, 1, 2, 3, dst, dst_stride);
        } else {
            ST_W4(out0, 0, 1, 2, 3, dst, dst_stride);
            ST_W2(out1, 0, 1, dst + 4 * dst_stride, dst_stride);
        }
    }
}

static void hevc_hv_uni_8t_8multx2mult_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride,
                                           const int8_t *filter_x,
                                           const int8_t *filter_y,
                                           int32_t height, int32_t width)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1, filt2, filt3;
    v8i16 filt_h0, filt_h1, filt_h2, filt_h3;
    v16i8 mask1, mask2, mask3;
    v8i16 filter_vec;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst21_l, dst43_l, dst65_l, dst87_l;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);

    src -= ((3 * src_stride) + 3);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

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
        dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);
        dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                 filt2, filt3);

        VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3,
                   vec0, vec1, vec2, vec3);
        VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3,
                   vec4, vec5, vec6, vec7);
        VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3,
                   vec8, vec9, vec10, vec11);
        dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);
        dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                 filt3);
        dst6 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                                 filt3);

        for (loop_cnt = height >> 1; loop_cnt--;) {
            LD_SB2(src_tmp, src_stride, src7, src8);
            XORI_B2_128_SB(src7, src8);
            src_tmp += 2 * src_stride;

            ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                       dst10_r, dst32_r, dst54_r, dst21_r);
            ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1,
                       dst10_l, dst32_l, dst54_l, dst21_l);
            ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
            ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

            VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst7 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);

            ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
            dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_l = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst0_r >>= 6;
            dst0_l >>= 6;

            VSHF_B4_SB(src8, src8, mask0, mask1, mask2, mask3,
                       vec0, vec1, vec2, vec3);
            dst8 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1,
                                     filt2, filt3);

            ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
            dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst1_l = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l,
                                    filt_h0, filt_h1, filt_h2, filt_h3);
            dst1_r >>= 6;
            dst1_l >>= 6;
            SRARI_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, 6);
            SAT_SW4_SW(dst0_r, dst0_l, dst1_r, dst1_l, 7);

            PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst0, dst1);
            out = PCKEV_XORI128_UB(dst0, dst1);
            ST_D2(out, 0, 1, dst_tmp, dst_stride);
            dst_tmp += (2 * dst_stride);

            dst0 = dst2;
            dst1 = dst3;
            dst2 = dst4;
            dst3 = dst5;
            dst4 = dst6;
            dst5 = dst7;
            dst6 = dst8;
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_uni_8t_8w_msa(const uint8_t *src,
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

static void hevc_hv_uni_8t_12w_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    uint32_t loop_cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src11, src12, src13, src14;
    v16i8 mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 vec8, vec9, vec10, vec11, vec12, vec13, vec14, vec15;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8;
    v8i16 dst30, dst41, dst52, dst63, dst66, dst117, dst128, dst139, dst1410;
    v8i16 filt0, filt1, filt2, filt3, filt_h0, filt_h1, filt_h2, filt_h3;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst76_l, dst21_l, dst43_l, dst65_l;
    v8i16 dst87_r, dst98_r, dst1110_r, dst1312_r, dst109_r, dst1211_r;
    v8i16 dst1413_r, dst87_l, filter_vec;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v4i32 dst0_l, dst1_l;

    src -= ((3 * src_stride) + 3);

    filter_vec = LD_SH(filter_x);
    SPLATI_H4_SH(filter_vec, 0, 1, 2, 3, filt0, filt1, filt2, filt3);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W4_SH(filter_vec, filt_h0, filt_h1, filt_h2, filt_h3);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    src_tmp = src;
    dst_tmp = dst;

    LD_SB7(src_tmp, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src_tmp += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    /* row 0 row 1 row 2 row 3 */
    VSHF_B4_SB(src0, src0, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src1, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src2, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    VSHF_B4_SB(src3, src3, mask0, mask1, mask2, mask3, vec12, vec13, vec14,
               vec15);
    dst0 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    dst1 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                             filt3);
    dst2 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                             filt3);
    dst3 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                             filt2, filt3);

    VSHF_B4_SB(src4, src4, mask0, mask1, mask2, mask3, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src5, src5, mask0, mask1, mask2, mask3, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src6, src6, mask0, mask1, mask2, mask3, vec8, vec9, vec10,
               vec11);
    dst4 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                             filt3);
    dst5 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                             filt3);
    dst6 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                             filt3);

    for (loop_cnt = 8; loop_cnt--;) {
        LD_SB2(src_tmp, src_stride, src7, src8);
        XORI_B2_128_SB(src7, src8);
        src_tmp += 2 * src_stride;

        ILVR_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1, dst10_r,
                   dst32_r, dst54_r, dst21_r);
        ILVL_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst2, dst1, dst10_l,
                   dst32_l, dst54_l, dst21_l);
        ILVR_H2_SH(dst4, dst3, dst6, dst5, dst43_r, dst65_r);
        ILVL_H2_SH(dst4, dst3, dst6, dst5, dst43_l, dst65_l);

        VSHF_B4_SB(src7, src7, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst7 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst0_l = HEVC_FILT_8TAP(dst10_l, dst32_l, dst54_l, dst76_l,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst0_r >>= 6;
        dst0_l >>= 6;

        VSHF_B4_SB(src8, src8, mask0, mask1, mask2, mask3, vec0, vec1, vec2,
                   vec3);
        dst8 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                 filt3);

        ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst1_l = HEVC_FILT_8TAP(dst21_l, dst43_l, dst65_l, dst87_l,
                                filt_h0, filt_h1, filt_h2, filt_h3);
        dst1_r >>= 6;
        dst1_l >>= 6;
        SRARI_W4_SW(dst0_r, dst0_l, dst1_r, dst1_l, 6);
        SAT_SW4_SW(dst0_r, dst0_l, dst1_r, dst1_l, 7);

        PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst0, dst1);
        out0 = PCKEV_XORI128_UB(dst0, dst1);
        ST_D2(out0, 0, 1, dst_tmp, dst_stride);
        dst_tmp += (2 * dst_stride);

        dst0 = dst2;
        dst1 = dst3;
        dst2 = dst4;
        dst3 = dst5;
        dst4 = dst6;
        dst5 = dst7;
        dst6 = dst8;
    }

    src += 8;
    dst += 8;

    mask4 = LD_SB(ff_hevc_mask_arr + 16);
    mask5 = mask4 + 2;
    mask6 = mask4 + 4;
    mask7 = mask4 + 6;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B4_SB(src0, src3, mask4, mask5, mask6, mask7, vec0, vec1, vec2, vec3);
    VSHF_B4_SB(src1, src4, mask4, mask5, mask6, mask7, vec4, vec5, vec6, vec7);
    VSHF_B4_SB(src2, src5, mask4, mask5, mask6, mask7, vec8, vec9, vec10,
               vec11);
    VSHF_B4_SB(src3, src6, mask4, mask5, mask6, mask7, vec12, vec13, vec14,
               vec15);

    dst30 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                              filt3);
    dst41 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                              filt3);
    dst52 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1, filt2,
                              filt3);
    dst63 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1, filt2,
                              filt3);

    ILVRL_H2_SH(dst41, dst30, dst10_r, dst43_r);
    ILVRL_H2_SH(dst52, dst41, dst21_r, dst54_r);
    ILVRL_H2_SH(dst63, dst52, dst32_r, dst65_r);

    dst66 = (v8i16) __msa_splati_d((v2i64) dst63, 1);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB8(src, src_stride, src7, src8, src9, src10, src11, src12, src13,
               src14);
        src += (8 * src_stride);
        XORI_B8_128_SB(src7, src8, src9, src10, src11, src12, src13, src14);

        VSHF_B4_SB(src7, src11, mask4, mask5, mask6, mask7, vec0, vec1, vec2,
                   vec3);
        VSHF_B4_SB(src8, src12, mask4, mask5, mask6, mask7, vec4, vec5, vec6,
                   vec7);
        VSHF_B4_SB(src9, src13, mask4, mask5, mask6, mask7, vec8, vec9, vec10,
                   vec11);
        VSHF_B4_SB(src10, src14, mask4, mask5, mask6, mask7, vec12, vec13,
                   vec14, vec15);

        dst117 = HEVC_FILT_8TAP_SH(vec0, vec1, vec2, vec3, filt0, filt1, filt2,
                                   filt3);
        dst128 = HEVC_FILT_8TAP_SH(vec4, vec5, vec6, vec7, filt0, filt1, filt2,
                                   filt3);
        dst139 = HEVC_FILT_8TAP_SH(vec8, vec9, vec10, vec11, filt0, filt1,
                                   filt2, filt3);
        dst1410 = HEVC_FILT_8TAP_SH(vec12, vec13, vec14, vec15, filt0, filt1,
                                   filt2, filt3);

        dst76_r = __msa_ilvr_h(dst117, dst66);
        ILVRL_H2_SH(dst128, dst117, dst87_r, dst1211_r);
        ILVRL_H2_SH(dst139, dst128, dst98_r, dst1312_r);
        ILVRL_H2_SH(dst1410, dst139, dst109_r, dst1413_r);
        dst117 = (v8i16) __msa_splati_d((v2i64) dst117, 1);
        dst1110_r = __msa_ilvr_h(dst117, dst1410);

        dst0_r = HEVC_FILT_8TAP(dst10_r, dst32_r, dst54_r, dst76_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst1_r = HEVC_FILT_8TAP(dst21_r, dst43_r, dst65_r, dst87_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst2_r = HEVC_FILT_8TAP(dst32_r, dst54_r, dst76_r, dst98_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst3_r = HEVC_FILT_8TAP(dst43_r, dst65_r, dst87_r, dst109_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst4_r = HEVC_FILT_8TAP(dst54_r, dst76_r, dst98_r, dst1110_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst5_r = HEVC_FILT_8TAP(dst65_r, dst87_r, dst109_r, dst1211_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst6_r = HEVC_FILT_8TAP(dst76_r, dst98_r, dst1110_r, dst1312_r, filt_h0,
                                filt_h1, filt_h2, filt_h3);
        dst7_r = HEVC_FILT_8TAP(dst87_r, dst109_r, dst1211_r, dst1413_r,
                                filt_h0, filt_h1, filt_h2, filt_h3);

        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRA_4V(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        SRARI_W4_SW(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRARI_W4_SW(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        SAT_SW4_SW(dst0_r, dst1_r, dst2_r, dst3_r, 7);
        SAT_SW4_SW(dst4_r, dst5_r, dst6_r, dst7_r, 7);
        PCKEV_H2_SW(dst1_r, dst0_r, dst3_r, dst2_r, dst0_r, dst1_r);
        PCKEV_H2_SW(dst5_r, dst4_r, dst7_r, dst6_r, dst4_r, dst5_r);
        out0 = PCKEV_XORI128_UB(dst0_r, dst1_r);
        out1 = PCKEV_XORI128_UB(dst4_r, dst5_r);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst32_r = dst1110_r;
        dst54_r = dst1312_r;
        dst21_r = dst109_r;
        dst43_r = dst1211_r;
        dst65_r = dst1413_r;
        dst66 = (v8i16) __msa_splati_d((v2i64) dst1410, 1);
    }
}

static void hevc_hv_uni_8t_16w_msa(const uint8_t *src,
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

static void hevc_hv_uni_8t_24w_msa(const uint8_t *src,
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

static void hevc_hv_uni_8t_32w_msa(const uint8_t *src,
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

static void hevc_hv_uni_8t_48w_msa(const uint8_t *src,
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

static void hevc_hv_uni_8t_64w_msa(const uint8_t *src,
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

static void common_hz_4t_4x2_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 filt0, filt1, src0, src1, mask0, mask1, vec0, vec1;
    v16u8 out;
    v8i16 filt, res0;

    mask0 = LD_SB(&ff_hevc_mask_arr[16]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    LD_SB2(src, src_stride, src0, src1);
    XORI_B2_128_SB(src0, src1);
    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    res0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    res0 = __msa_srari_h(res0, 6);
    res0 = __msa_sat_s_h(res0, 7);
    out = PCKEV_XORI128_UB(res0, res0);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void common_hz_4t_4x4_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v8i16 filt, out0, out1;
    v16u8 out;

    mask0 = LD_SB(&ff_hevc_mask_arr[16]);
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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void common_hz_4t_4x8_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&ff_hevc_mask_arr[16]);
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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
}

static void common_hz_4t_4x16_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  const int8_t *filter)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&ff_hevc_mask_arr[16]);
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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
    dst += (8 * dst_stride);

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
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
    out = PCKEV_XORI128_UB(out2, out3);
    ST_W4(out, 0, 1, 2, 3, dst + 4 * dst_stride, dst_stride);
}

static void common_hz_4t_4w_msa(const uint8_t *src, int32_t src_stride,
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

static void common_hz_4t_6w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v16u8 out4, out5;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);

    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_4TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, filt0,
                               filt1, out0, out1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out4 = PCKEV_XORI128_UB(out0, out1);
    out5 = PCKEV_XORI128_UB(out2, out3);
    ST_W2(out4, 0, 2, dst, dst_stride);
    ST_H2(out4, 2, 6, dst + 4, dst_stride);
    ST_W2(out5, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out5, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
    dst += (4 * dst_stride);

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);

    XORI_B4_128_SB(src0, src1, src2, src3);
    HORIZ_4TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, filt0,
                               filt1, out0, out1, out2, out3);
    SRARI_H4_SH(out0, out1, out2, out3, 6);
    SAT_SH4_SH(out0, out1, out2, out3, 7);
    out4 = PCKEV_XORI128_UB(out0, out1);
    out5 = PCKEV_XORI128_UB(out2, out3);
    ST_W2(out4, 0, 2, dst, dst_stride);
    ST_H2(out4, 2, 6, dst + 4, dst_stride);
    ST_W2(out5, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out5, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
}

static void common_hz_4t_8x2mult_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, filt0, filt1, mask0, mask1;
    v16u8 out;
    v8i16 filt, vec0, vec1, vec2, vec3;

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
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
        ST_D2(out, 0, 1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void common_hz_4t_8x4mult_msa(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3;

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
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
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_4t_8w_msa(const uint8_t *src, int32_t src_stride,
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

static void common_hz_4t_12w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, filt0, filt1, mask0, mask1, mask2, mask3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v16i8 vec10, vec11;
    v16u8 tmp0, tmp1;
    v8i16 filt, out0, out1, out2, out3, out4, out5;

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    mask2 = LD_SB(&ff_hevc_mask_arr[32]);

    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;
    mask3 = mask2 + 2;

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B4_128_SB(src0, src1, src2, src3);
        VSHF_B2_SB(src0, src1, src2, src3, mask2, mask2, vec0, vec1);
        DOTP_SB2_SH(vec0, vec1, filt0, filt0, out0, out1);
        VSHF_B2_SB(src0, src1, src2, src3, mask3, mask3, vec2, vec3);
        DPADD_SB2_SH(vec2, vec3, filt1, filt1, out0, out1);
        SRARI_H2_SH(out0, out1, 6);
        SAT_SH2_SH(out0, out1, 7);
        tmp0 = PCKEV_XORI128_UB(out0, out1);
        ST_W4(tmp0, 0, 1, 2, 3, dst + 8, dst_stride);

        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec4, vec5);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec6, vec7);
        DOTP_SB4_SH(vec4, vec5, vec6, vec7, filt0, filt0, filt0, filt0,
                    out2, out3, out4, out5);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec8, vec9);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec10, vec11);
        DPADD_SB4_SH(vec8, vec9, vec10, vec11, filt1, filt1, filt1, filt1,
                     out2, out3, out4, out5);
        SRARI_H4_SH(out2, out3, out4, out5, 6);
        SAT_SH4_SH(out2, out3, out4, out5, 7);
        tmp0 = PCKEV_XORI128_UB(out2, out3);
        tmp1 = PCKEV_XORI128_UB(out4, out5);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_4t_16w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, mask0, mask1;
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m;
    v8i16 filt, out0, out1, out2, out3, out4, out5, out6, out7;
    v16u8 out;

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
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

        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);
        DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec0_m, vec1_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec2_m, vec3_m);
        DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt1, filt1, filt1, filt1,
                     out0, out1, out2, out3);
        SRARI_H4_SH(out0, out1, out2, out3, 6);
        SAT_SH4_SH(out0, out1, out2, out3, 7);
        out = PCKEV_XORI128_UB(out0, out1);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out2, out3);
        ST_UB(out, dst);
        dst += dst_stride;

        VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0_m, vec1_m);
        VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2_m, vec3_m);
        DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,
                    out4, out5, out6, out7);
        VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec0_m, vec1_m);
        VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec2_m, vec3_m);
        DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt1, filt1, filt1, filt1,
                     out4, out5, out6, out7);
        SRARI_H4_SH(out4, out5, out6, out7, 6);
        SAT_SH4_SH(out4, out5, out6, out7, 7);
        out = PCKEV_XORI128_UB(out4, out5);
        ST_UB(out, dst);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out6, out7);
        ST_UB(out, dst);
        dst += dst_stride;
    }
}

static void common_hz_4t_24w_msa(const uint8_t *src, int32_t src_stride,
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

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;
    mask00 = mask0 + 8;
    mask11 = mask0 + 10;

    for (loop_cnt = 8; loop_cnt--;) {
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
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst1, dst_stride);
        dst1 += (4 * dst_stride);
    }
}

static void common_hz_4t_32w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 filt0, filt1, mask0, mask1;
    v16u8 out;
    v16i8 vec0_m, vec1_m, vec2_m, vec3_m;
    v8i16 filt, out0, out1, out2, out3, out4, out5, out6, out7;

    mask0 = LD_SB(&ff_hevc_mask_arr[0]);
    src -= 1;

    /* rearranging filter */
    filt = LD_SH(filter);
    SPLATI_H2_SB(filt, 0, 1, filt0, filt1);

    mask1 = mask0 + 2;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        src0 = LD_SB(src);
        src1 = LD_SB(src + 8);
        src2 = LD_SB(src + 16);
        src3 = LD_SB(src + 24);
        src += src_stride;
        src4 = LD_SB(src);
        src5 = LD_SB(src + 8);
        src6 = LD_SB(src + 16);
        src7 = LD_SB(src + 24);
        src += src_stride;

        XORI_B8_128_SB(src0, src1, src2, src3, src4, src5, src6, src7);

        VSHF_B2_SB(src0, src0, src1, src1, mask0, mask0, vec0_m, vec1_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask0, mask0, vec2_m, vec3_m);
        DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,
                    out0, out1, out2, out3);
        VSHF_B2_SB(src0, src0, src1, src1, mask1, mask1, vec0_m, vec1_m);
        VSHF_B2_SB(src2, src2, src3, src3, mask1, mask1, vec2_m, vec3_m);
        DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt1, filt1, filt1, filt1,
                     out0, out1, out2, out3);

        VSHF_B2_SB(src4, src4, src5, src5, mask0, mask0, vec0_m, vec1_m);
        VSHF_B2_SB(src6, src6, src7, src7, mask0, mask0, vec2_m, vec3_m);
        DOTP_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt0, filt0, filt0, filt0,
                    out4, out5, out6, out7);
        VSHF_B2_SB(src4, src4, src5, src5, mask1, mask1, vec0_m, vec1_m);
        VSHF_B2_SB(src6, src6, src7, src7, mask1, mask1, vec2_m, vec3_m);
        DPADD_SB4_SH(vec0_m, vec1_m, vec2_m, vec3_m, filt1, filt1, filt1, filt1,
                     out4, out5, out6, out7);
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

static void common_vt_4t_4x2_msa(const uint8_t *src, int32_t src_stride,
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
    out10 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
    out10 = __msa_srari_h(out10, 6);
    out10 = __msa_sat_s_h(out10, 7);
    out = PCKEV_XORI128_UB(out10, out10);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void common_vt_4t_4x4multiple_msa(const uint8_t *src, int32_t src_stride,
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
        out10 = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);

        src2 = LD_SB(src);
        src += (src_stride);
        ILVR_B2_SB(src5, src4, src2, src5, src54_r, src65_r);
        src2110 = (v16i8) __msa_ilvr_d((v2i64) src65_r, (v2i64) src54_r);
        src2110 = (v16i8) __msa_xori_b((v16u8) src2110, 128);
        out32 = HEVC_FILT_4TAP_SH(src4332, src2110, filt0, filt1);
        SRARI_H2_SH(out10, out32, 6);
        SAT_SH2_SH(out10, out32, 7);
        out = PCKEV_XORI128_UB(out10, out32);
        ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_vt_4t_4w_msa(const uint8_t *src, int32_t src_stride,
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

static void common_vt_4t_6w_msa(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src10_r, src32_r, src21_r, src43_r, src54_r, src65_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r, filt0, filt1, filter_vec;

    src -= src_stride;

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);

    LD_SB2(src, src_stride, src3, src4);
    src += (2 * src_stride);
    XORI_B2_128_SB(src3, src4);
    ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);

    dst0_r = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
    dst1_r = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);

    LD_SB2(src, src_stride, src5, src6);
    src += (2 * src_stride);
    XORI_B2_128_SB(src5, src6);
    ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);

    dst2_r = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
    dst3_r = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);

    SRARI_H4_SH(dst0_r, dst1_r, dst2_r, dst3_r, 6);
    SAT_SH4_SH(dst0_r, dst1_r, dst2_r, dst3_r, 7);
    out0 = PCKEV_XORI128_UB(dst0_r, dst1_r);
    out1 = PCKEV_XORI128_UB(dst2_r, dst3_r);
    ST_W2(out0, 0, 2, dst, dst_stride);
    ST_H2(out0, 2, 6, dst + 4, dst_stride);
    ST_W2(out1, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
    dst += (4 * dst_stride);

    LD_SB2(src, src_stride, src3, src4);
    src += (2 * src_stride);
    XORI_B2_128_SB(src3, src4);
    ILVR_B2_SB(src3, src6, src4, src3, src32_r, src43_r);

    dst0_r = HEVC_FILT_4TAP_SH(src54_r, src32_r, filt0, filt1);
    dst1_r = HEVC_FILT_4TAP_SH(src65_r, src43_r, filt0, filt1);

    LD_SB2(src, src_stride, src5, src6);
    src += (2 * src_stride);
    XORI_B2_128_SB(src5, src6);
    ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);

    dst2_r = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
    dst3_r = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);

    SRARI_H4_SH(dst0_r, dst1_r, dst2_r, dst3_r, 6);
    SAT_SH4_SH(dst0_r, dst1_r, dst2_r, dst3_r, 7);
    out0 = PCKEV_XORI128_UB(dst0_r, dst1_r);
    out1 = PCKEV_XORI128_UB(dst2_r, dst3_r);
    ST_W2(out0, 0, 2, dst, dst_stride);
    ST_H2(out0, 2, 6, dst + 4, dst_stride);
    ST_W2(out1, 0, 2, dst + 2 * dst_stride, dst_stride);
    ST_H2(out1, 2, 6, dst + 2 * dst_stride + 4, dst_stride);
}

static void common_vt_4t_8x2_msa(const uint8_t *src, int32_t src_stride,
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
    tmp0 = HEVC_FILT_4TAP_SH(src01, src23, filt0, filt1);
    ILVR_B2_SH(src2, src1, src4, src3, src12, src34);
    tmp1 = HEVC_FILT_4TAP_SH(src12, src34, filt0, filt1);
    SRARI_H2_SH(tmp0, tmp1, 6);
    SAT_SH2_SH(tmp0, tmp1, 7);
    out = PCKEV_XORI128_UB(tmp0, tmp1);
    ST_D2(out, 0, 1, dst, dst_stride);
}

static void common_vt_4t_8x6_msa(const uint8_t *src, int32_t src_stride,
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
        tmp0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        tmp1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        tmp2 = HEVC_FILT_4TAP_SH(vec1, vec4, filt0, filt1);
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

static void common_vt_4t_8x4mult_msa(const uint8_t *src, int32_t src_stride,
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
        out0_r = HEVC_FILT_4TAP_SH(src10_r, src72_r, filt0, filt1);
        out1_r = HEVC_FILT_4TAP_SH(src21_r, src87_r, filt0, filt1);
        out2_r = HEVC_FILT_4TAP_SH(src72_r, src98_r, filt0, filt1);
        out3_r = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
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

static void common_vt_4t_8w_msa(const uint8_t *src, int32_t src_stride,
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

static void common_vt_4t_12w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16u8 out0, out1;
    v16i8 src10_r, src32_r, src21_r, src43_r, src54_r, src65_r;
    v16i8 src10_l, src32_l, src54_l, src21_l, src43_l, src65_l;
    v16i8 src2110, src4332, src6554;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r, dst0_l, dst1_l, filt0, filt1;
    v8i16 filter_vec;

    src -= (1 * src_stride);

    filter_vec = LD_SH(filter);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);
    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);
    src2110 = (v16i8) __msa_ilvr_d((v2i64) src21_l, (v2i64) src10_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src, src_stride, src3, src4, src5, src6);
        src += (4 * src_stride);

        XORI_B4_128_SB(src3, src4, src5, src6);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);
        src4332 = (v16i8) __msa_ilvr_d((v2i64) src43_l, (v2i64) src32_l);
        ILVR_B2_SB(src5, src4, src6, src5, src54_r, src65_r);
        ILVL_B2_SB(src5, src4, src6, src5, src54_l, src65_l);
        src6554 = (v16i8) __msa_ilvr_d((v2i64) src65_l, (v2i64) src54_l);

        dst0_r = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        dst1_r = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        dst0_l = HEVC_FILT_4TAP_SH(src2110, src4332, filt0, filt1);
        dst2_r = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
        dst3_r = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
        dst1_l = HEVC_FILT_4TAP_SH(src4332, src6554, filt0, filt1);

        SRARI_H4_SH(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRARI_H2_SH(dst0_l, dst1_l, 6);
        SAT_SH4_SH(dst0_r, dst1_r, dst2_r, dst3_r, 7);
        SAT_SH2_SH(dst0_l, dst1_l, 7);
        out0 = PCKEV_XORI128_UB(dst0_r, dst1_r);
        out1 = PCKEV_XORI128_UB(dst2_r, dst3_r);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        out0 = PCKEV_XORI128_UB(dst0_l, dst1_l);
        ST_W4(out0, 0, 1, 2, 3, dst + 8, dst_stride);
        dst += (4 * dst_stride);

        src2 = src6;
        src10_r = src54_r;
        src21_r = src65_r;
        src2110 = src6554;
    }
}

static void common_vt_4t_16w_msa(const uint8_t *src, int32_t src_stride,
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
        out0_r = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        out1_r = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        out2_r = HEVC_FILT_4TAP_SH(src32_r, src54_r, filt0, filt1);
        out3_r = HEVC_FILT_4TAP_SH(src43_r, src65_r, filt0, filt1);
        out0_l = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        out1_l = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);
        out2_l = HEVC_FILT_4TAP_SH(src32_l, src54_l, filt0, filt1);
        out3_l = HEVC_FILT_4TAP_SH(src43_l, src65_l, filt0, filt1);
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

static void common_vt_4t_24w_msa(const uint8_t *src, int32_t src_stride,
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

    for (loop_cnt = 8; loop_cnt--;) {
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
        out0_r = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        out0_l = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        out1_r = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        out1_l = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);

        /* 8 width */
        out2_r = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        out3_r = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);

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
        out0_r = HEVC_FILT_4TAP_SH(src32_r, src10_r, filt0, filt1);
        out0_l = HEVC_FILT_4TAP_SH(src32_l, src10_l, filt0, filt1);
        out1_r = HEVC_FILT_4TAP_SH(src43_r, src21_r, filt0, filt1);
        out1_l = HEVC_FILT_4TAP_SH(src43_l, src21_l, filt0, filt1);

        /* 8 width */
        out2_r = HEVC_FILT_4TAP_SH(src98_r, src76_r, filt0, filt1);
        out3_r = HEVC_FILT_4TAP_SH(src109_r, src87_r, filt0, filt1);

        /* 16 + 8 width */
        SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
        SRARI_H2_SH(out0_l, out1_l, 6);
        SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
        SAT_SH2_SH(out0_l, out1_l, 7);
        out = PCKEV_XORI128_UB(out0_r, out0_l);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out2_r, out2_r);
        ST_D1(out, 0, dst + 16);
        dst += dst_stride;
        out = PCKEV_XORI128_UB(out1_r, out1_l);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out3_r, out3_r);
        ST_D1(out, 0, dst + 16);
        dst += dst_stride;
    }
}

static void common_vt_4t_32w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt;
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

    /* 16 width */
    LD_SB3(src, src_stride, src0, src1, src2);
    XORI_B3_128_SB(src0, src1, src2);

    ILVR_B2_SB(src1, src0, src2, src1, src10_r, src21_r);
    ILVL_B2_SB(src1, src0, src2, src1, src10_l, src21_l);

    /* next 16 width */
    LD_SB3(src + 16, src_stride, src6, src7, src8);
    src += (3 * src_stride);

    XORI_B3_128_SB(src6, src7, src8);
    ILVR_B2_SB(src7, src6, src8, src7, src76_r, src87_r);
    ILVL_B2_SB(src7, src6, src8, src7, src76_l, src87_l);

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        /* 16 width */
        LD_SB2(src, src_stride, src3, src4);
        XORI_B2_128_SB(src3, src4);
        ILVR_B2_SB(src3, src2, src4, src3, src32_r, src43_r);
        ILVL_B2_SB(src3, src2, src4, src3, src32_l, src43_l);

        /* 16 width */
        out0_r = HEVC_FILT_4TAP_SH(src10_r, src32_r, filt0, filt1);
        out0_l = HEVC_FILT_4TAP_SH(src10_l, src32_l, filt0, filt1);
        out1_r = HEVC_FILT_4TAP_SH(src21_r, src43_r, filt0, filt1);
        out1_l = HEVC_FILT_4TAP_SH(src21_l, src43_l, filt0, filt1);

        /* 16 width */
        SRARI_H4_SH(out0_r, out1_r, out0_l, out1_l, 6);
        SAT_SH4_SH(out0_r, out1_r, out0_l, out1_l, 7);
        out = PCKEV_XORI128_UB(out0_r, out0_l);
        ST_UB(out, dst);
        out = PCKEV_XORI128_UB(out1_r, out1_l);
        ST_UB(out, dst + dst_stride);

        src10_r = src32_r;
        src21_r = src43_r;
        src10_l = src32_l;
        src21_l = src43_l;
        src2 = src4;

        /* next 16 width */
        LD_SB2(src + 16, src_stride, src9, src10);
        src += (2 * src_stride);
        XORI_B2_128_SB(src9, src10);
        ILVR_B2_SB(src9, src8, src10, src9, src98_r, src109_r);
        ILVL_B2_SB(src9, src8, src10, src9, src98_l, src109_l);

        /* next 16 width */
        out2_r = HEVC_FILT_4TAP_SH(src76_r, src98_r, filt0, filt1);
        out2_l = HEVC_FILT_4TAP_SH(src76_l, src98_l, filt0, filt1);
        out3_r = HEVC_FILT_4TAP_SH(src87_r, src109_r, filt0, filt1);
        out3_l = HEVC_FILT_4TAP_SH(src87_l, src109_l, filt0, filt1);

        /* next 16 width */
        SRARI_H4_SH(out2_r, out3_r, out2_l, out3_l, 6);
        SAT_SH4_SH(out2_r, out3_r, out2_l, out3_l, 7);
        out = PCKEV_XORI128_UB(out2_r, out2_l);
        ST_UB(out, dst + 16);
        out = PCKEV_XORI128_UB(out3_r, out3_l);
        ST_UB(out, dst + 16 + dst_stride);

        dst += 2 * dst_stride;

        src76_r = src98_r;
        src87_r = src109_r;
        src76_l = src98_l;
        src87_l = src109_l;
        src8 = src10;
    }
}

static void hevc_hv_uni_4t_4x2_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, tmp;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5;
    v8i16 dst20, dst31, dst42, dst10, dst32, dst21, dst43;
    v4i32 dst0, dst1;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    VSHF_B2_SB(src0, src2, src0, src2, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src3, src1, src3, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src4, src2, src4, mask0, mask1, vec4, vec5);

    dst20 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst31 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst42 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

    ILVRL_H2_SH(dst31, dst20, dst10, dst32);
    ILVRL_H2_SH(dst42, dst31, dst21, dst43);

    dst0 = HEVC_FILT_4TAP(dst10, dst32, filt_h0, filt_h1);
    dst1 = HEVC_FILT_4TAP(dst21, dst43, filt_h0, filt_h1);
    dst0 >>= 6;
    dst1 >>= 6;
    tmp = __msa_pckev_h((v8i16) dst1, (v8i16) dst0);
    tmp = __msa_srari_h(tmp, 6);
    tmp = __msa_sat_s_h(tmp, 7);
    out = PCKEV_XORI128_UB(tmp, tmp);
    ST_W2(out, 0, 1, dst, dst_stride);
}

static void hevc_hv_uni_4t_4x4_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filter_vec, tmp0, tmp1;
    v8i16 dst30, dst41, dst52, dst63;
    v8i16 dst10, dst32, dst54, dst21, dst43, dst65;
    v4i32 dst0, dst1, dst2, dst3;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
    XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

    VSHF_B2_SB(src0, src3, src0, src3, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src4, src1, src4, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src5, src2, src5, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src6, src3, src6, mask0, mask1, vec6, vec7);

    dst30 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst41 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst52 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst63 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    ILVRL_H2_SH(dst41, dst30, dst10, dst43);
    ILVRL_H2_SH(dst52, dst41, dst21, dst54);
    ILVRL_H2_SH(dst63, dst52, dst32, dst65);
    dst0 = HEVC_FILT_4TAP(dst10, dst32, filt_h0, filt_h1);
    dst1 = HEVC_FILT_4TAP(dst21, dst43, filt_h0, filt_h1);
    dst2 = HEVC_FILT_4TAP(dst32, dst54, filt_h0, filt_h1);
    dst3 = HEVC_FILT_4TAP(dst43, dst65, filt_h0, filt_h1);
    SRA_4V(dst0, dst1, dst2, dst3, 6);
    PCKEV_H2_SH(dst1, dst0, dst3, dst2, tmp0, tmp1);
    SRARI_H2_SH(tmp0, tmp1, 6);
    SAT_SH2_SH(tmp0, tmp1, 7);
    out = PCKEV_XORI128_UB(tmp0, tmp1);
    ST_W4(out, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_hv_uni_4t_4multx8mult_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride,
                                           const int8_t *filter_x,
                                           const int8_t *filter_y,
                                           int32_t height)
{
    uint32_t loop_cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5;
    v16i8 src6, src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr + 16);
    v16i8 mask1;
    v8i16 filter_vec, tmp0, tmp1, tmp2, tmp3;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v4i32 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst76_r;
    v8i16 dst21_r, dst43_r, dst65_r, dst87_r;
    v8i16 dst98_r, dst109_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src1, src0, src1, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask0, mask1, vec2, vec3);
    dst10 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst21 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    ILVRL_H2_SH(dst21, dst10, dst10_r, dst21_r);
    dst22 = (v8i16) __msa_splati_d((v2i64) dst21, 1);

    for (loop_cnt = height >> 3; loop_cnt--;) {
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);

        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);

        VSHF_B2_SB(src3, src7, src3, src7, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src8, src4, src8, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src9, src5, src9, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src10, src6, src10, mask0, mask1, vec6, vec7);

        dst73 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst84 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst95 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst106 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        dst32_r = __msa_ilvr_h(dst73, dst22);
        ILVRL_H2_SH(dst84, dst73, dst43_r, dst87_r);
        ILVRL_H2_SH(dst95, dst84, dst54_r, dst98_r);
        ILVRL_H2_SH(dst106, dst95, dst65_r, dst109_r);
        dst22 = (v8i16) __msa_splati_d((v2i64) dst73, 1);
        dst76_r = __msa_ilvr_h(dst22, dst106);

        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        dst6_r = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
        dst7_r = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);
        SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
        SRA_4V(dst4_r, dst5_r, dst6_r, dst7_r, 6);
        PCKEV_H4_SH(dst1_r, dst0_r, dst3_r, dst2_r,
                    dst5_r, dst4_r, dst7_r, dst6_r,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_uni_4t_4w_msa(const uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    if (2 == height) {
        hevc_hv_uni_4t_4x2_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y);
    } else if (4 == height) {
        hevc_hv_uni_4t_4x4_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y);
    } else if (0 == (height % 8)) {
        hevc_hv_uni_4t_4multx8mult_msa(src, src_stride, dst, dst_stride,
                                       filter_x, filter_y, height);
    }
}

static void hevc_hv_uni_4t_6w_msa(const uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v16i8 src7, src8, src9, src10;
    v8i16 filt0, filt1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6, dsth7, dsth8, dsth9;
    v8i16 dsth10, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst4_r, dst5_r, dst6_r, dst7_r;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 dst54_r, dst76_r, dst98_r, dst65_r, dst87_r, dst109_r;
    v8i16 dst98_l, dst65_l, dst54_l, dst76_l, dst87_l, dst109_l;
    v8i16 dst1021_l, dst3243_l, dst5465_l, dst7687_l, dst98109_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

    LD_SB8(src, src_stride, src3, src4, src5, src6, src7, src8, src9, src10);
    XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);

    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

    dsth3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dsth6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src9, src9, src9, src9, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src10, src10, src10, src10, mask0, mask1, vec6, vec7);

    dsth7 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth8 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth9 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dsth10 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

    ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
    ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
    ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
    ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);
    ILVRL_H2_SH(dsth7, dsth6, dst76_r, dst76_l);
    ILVRL_H2_SH(dsth8, dsth7, dst87_r, dst87_l);
    ILVRL_H2_SH(dsth9, dsth8, dst98_r, dst98_l);
    ILVRL_H2_SH(dsth10, dsth9, dst109_r, dst109_l);

    PCKEV_D2_SH(dst21_l, dst10_l, dst43_l, dst32_l, dst1021_l, dst3243_l);
    PCKEV_D2_SH(dst65_l, dst54_l, dst87_l, dst76_l, dst5465_l, dst7687_l);
    dst98109_l = (v8i16) __msa_pckev_d((v2i64) dst109_l, (v2i64) dst98_l);

    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst6_r = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
    dst7_r = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst1021_l, dst3243_l, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst3243_l, dst5465_l, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst5465_l, dst7687_l, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst7687_l, dst98109_l, filt_h0, filt_h1);
    SRA_4V(dst0_r, dst1_r, dst2_r, dst3_r, 6);
    SRA_4V(dst4_r, dst5_r, dst6_r, dst7_r, 6);
    SRA_4V(dst0_l, dst1_l, dst2_l, dst3_l, 6);
    PCKEV_H2_SH(dst1_r, dst0_r, dst3_r, dst2_r, tmp0, tmp1);
    PCKEV_H2_SH(dst5_r, dst4_r, dst7_r, dst6_r, tmp2, tmp3);
    PCKEV_H2_SH(dst1_l, dst0_l, dst3_l, dst2_l, tmp4, tmp5);
    SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
    SRARI_H2_SH(tmp4, tmp5, 6);
    SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3,7);
    SAT_SH2_SH(tmp4, tmp5,7);
    out0 = PCKEV_XORI128_UB(tmp0, tmp1);
    out1 = PCKEV_XORI128_UB(tmp2, tmp3);
    out2 = PCKEV_XORI128_UB(tmp4, tmp5);
    ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
    ST_H8(out2, 0, 1, 2, 3, 4, 5, 6, 7, dst + 4, dst_stride);
}

static void hevc_hv_uni_4t_8x2_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y)
{
    v16u8 out;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v8i16 dst0, dst1, dst2, dst3, dst4;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 out0_r, out1_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    XORI_B5_128_SB(src0, src1, src2, src3, src4);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);

    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);
    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);
    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
    PCKEV_H2_SH(dst0_l, dst0_r, dst1_l, dst1_r, out0_r, out1_r);
    SRARI_H2_SH(out0_r, out1_r, 6);
    SAT_SH2_SH(out0_r, out1_r, 7);
    out = PCKEV_XORI128_UB(out0_r, out1_r);
    ST_D2(out, 0, 1, dst, dst_stride);
}

static void hevc_hv_uni_4t_8multx4_msa(const uint8_t *src,
                                       int32_t src_stride,
                                       uint8_t *dst,
                                       int32_t dst_stride,
                                       const int8_t *filter_x,
                                       const int8_t *filter_y,
                                       int32_t width8mult)
{
    uint32_t cnt;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, mask0, mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, tmp0, tmp1, tmp2, tmp3;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    for (cnt = width8mult; cnt--;) {
        LD_SB7(src, src_stride, src0, src1, src2, src3, src4, src5, src6);
        src += 8;
        XORI_B7_128_SB(src0, src1, src2, src3, src4, src5, src6);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

        dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
        ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
        ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
        ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);

        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);

        SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
        SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);

        PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
        dst += 8;
    }
}

static void hevc_hv_uni_4t_8x6_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y)
{
    v16u8 out0, out1, out2;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7, vec8, vec9;
    v16i8 vec10, vec11, vec12, vec13, vec14, vec15, vec16, vec17;
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
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    LD_SB5(src, src_stride, src0, src1, src2, src3, src4);
    src += (5 * src_stride);
    LD_SB4(src, src_stride, src5, src6, src7, src8);

    XORI_B5_128_SB(src0, src1, src2, src3, src4);
    XORI_B4_128_SB(src5, src6, src7, src8);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);
    VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec6, vec7);
    VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec8, vec9);
    VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec10, vec11);
    VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec12, vec13);
    VSHF_B2_SB(src7, src7, src7, src7, mask0, mask1, vec14, vec15);
    VSHF_B2_SB(src8, src8, src8, src8, mask0, mask1, vec16, vec17);

    dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
    dst3 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);
    dst4 = HEVC_FILT_4TAP_SH(vec8, vec9, filt0, filt1);
    dst5 = HEVC_FILT_4TAP_SH(vec10, vec11, filt0, filt1);
    dst6 = HEVC_FILT_4TAP_SH(vec12, vec13, filt0, filt1);
    dst7 = HEVC_FILT_4TAP_SH(vec14, vec15, filt0, filt1);
    dst8 = HEVC_FILT_4TAP_SH(vec16, vec17, filt0, filt1);

    ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
    ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);
    ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
    ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
    ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
    ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);
    ILVRL_H2_SH(dst7, dst6, dst76_r, dst76_l);
    ILVRL_H2_SH(dst8, dst7, dst87_r, dst87_l);

    dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
    dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
    dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
    dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
    dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
    dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
    dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
    dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);
    dst4_r = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
    dst4_l = HEVC_FILT_4TAP(dst54_l, dst76_l, filt_h0, filt_h1);
    dst5_r = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
    dst5_l = HEVC_FILT_4TAP(dst65_l, dst87_l, filt_h0, filt_h1);

    SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
    SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);
    SRA_4V(dst4_r, dst4_l, dst5_r, dst5_l, 6);
    PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                dst2_l, dst2_r, dst3_l, dst3_r, out0_r, out1_r, out2_r, out3_r);
    PCKEV_H2_SH(dst4_l, dst4_r, dst5_l, dst5_r, out4_r, out5_r);
    SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
    SRARI_H2_SH(out4_r, out5_r, 6);
    SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
    SAT_SH2_SH(out4_r, out5_r, 7);
    out0 = PCKEV_XORI128_UB(out0_r, out1_r);
    out1 = PCKEV_XORI128_UB(out2_r, out3_r);
    out2 = PCKEV_XORI128_UB(out4_r, out5_r);

    ST_D4(out0, out1, 0, 1, 0, 1, dst, dst_stride);
    ST_D2(out2, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void hevc_hv_uni_4t_8multx4mult_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride,
                                           const int8_t *filter_x,
                                           const int8_t *filter_y,
                                           int32_t height,
                                           int32_t width8mult)
{
    uint32_t loop_cnt, cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6;
    v8i16 filt0, filt1;
    v8i16 filt_h0, filt_h1, filter_vec;
    v16i8 mask0 = LD_SB(ff_hevc_mask_arr);
    v16i8 mask1;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v8i16 dst10_r, dst32_r, dst21_r, dst43_r;
    v8i16 dst10_l, dst32_l, dst21_l, dst43_l;
    v8i16 dst54_r, dst54_l, dst65_r, dst65_l, dst6;
    v8i16 out0_r, out1_r, out2_r, out3_r;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask1 = mask0 + 2;

    for (cnt = width8mult; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LD_SB3(src_tmp, src_stride, src0, src1, src2);
        src_tmp += (3 * src_stride);

        XORI_B3_128_SB(src0, src1, src2);

        VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

        dst0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

        ILVRL_H2_SH(dst1, dst0, dst10_r, dst10_l);
        ILVRL_H2_SH(dst2, dst1, dst21_r, dst21_l);

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
            src_tmp += (4 * src_stride);

            XORI_B4_128_SB(src3, src4, src5, src6);

            VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
            VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
            VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
            VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

            dst3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
            dst4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
            dst5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
            dst6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

            ILVRL_H2_SH(dst3, dst2, dst32_r, dst32_l);
            ILVRL_H2_SH(dst4, dst3, dst43_r, dst43_l);
            ILVRL_H2_SH(dst5, dst4, dst54_r, dst54_l);
            ILVRL_H2_SH(dst6, dst5, dst65_r, dst65_l);

            dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
            dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
            dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
            dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
            dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
            dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
            dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
            dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);

            SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
            SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);

            PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r,
                        dst2_l, dst2_r, dst3_l, dst3_r,
                        out0_r, out1_r, out2_r, out3_r);

            SRARI_H4_SH(out0_r, out1_r, out2_r, out3_r, 6);
            SAT_SH4_SH(out0_r, out1_r, out2_r, out3_r, 7);
            out0 = PCKEV_XORI128_UB(out0_r, out1_r);
            out1 = PCKEV_XORI128_UB(out2_r, out3_r);
            ST_D4(out0, out1, 0, 1, 0, 1, dst_tmp, dst_stride);
            dst_tmp += (4 * dst_stride);

            dst10_r = dst54_r;
            dst10_l = dst54_l;
            dst21_r = dst65_r;
            dst21_l = dst65_l;
            dst2 = dst6;
        }

        src += 8;
        dst += 8;
    }
}

static void hevc_hv_uni_4t_8w_msa(const uint8_t *src,
                                  int32_t src_stride,
                                  uint8_t *dst,
                                  int32_t dst_stride,
                                  const int8_t *filter_x,
                                  const int8_t *filter_y,
                                  int32_t height)
{
    if (2 == height) {
        hevc_hv_uni_4t_8x2_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y);
    } else if (4 == height) {
        hevc_hv_uni_4t_8multx4_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, 1);
    } else if (6 == height) {
        hevc_hv_uni_4t_8x6_msa(src, src_stride, dst, dst_stride,
                               filter_x, filter_y);
    } else if (0 == (height % 4)) {
        hevc_hv_uni_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter_x, filter_y, height, 1);
    }
}

static void hevc_hv_uni_4t_12w_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    uint32_t loop_cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16u8 out0, out1;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    v16i8 mask0, mask1, mask2, mask3;
    v8i16 filt0, filt1, filt_h0, filt_h1, filter_vec, tmp0, tmp1, tmp2, tmp3;
    v8i16 dsth0, dsth1, dsth2, dsth3, dsth4, dsth5, dsth6;
    v8i16 dst10, dst21, dst22, dst73, dst84, dst95, dst106;
    v8i16 dst76_r, dst98_r, dst87_r, dst109_r;
    v8i16 dst10_r, dst32_r, dst54_r, dst21_r, dst43_r, dst65_r;
    v8i16 dst10_l, dst32_l, dst54_l, dst21_l, dst43_l, dst65_l;
    v4i32 dst0_r, dst0_l, dst1_r, dst1_l, dst2_r, dst2_l, dst3_r, dst3_l;
    v4i32 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    src -= (src_stride + 1);

    filter_vec = LD_SH(filter_x);
    SPLATI_H2_SH(filter_vec, 0, 1, filt0, filt1);

    filter_vec = LD_SH(filter_y);
    UNPCK_R_SB_SH(filter_vec, filter_vec);

    SPLATI_W2_SH(filter_vec, 0, filt_h0, filt_h1);

    mask0 = LD_SB(ff_hevc_mask_arr);
    mask1 = mask0 + 2;

    src_tmp = src;
    dst_tmp = dst;

    LD_SB3(src_tmp, src_stride, src0, src1, src2);
    src_tmp += (3 * src_stride);

    XORI_B3_128_SB(src0, src1, src2);

    VSHF_B2_SB(src0, src0, src0, src0, mask0, mask1, vec0, vec1);
    VSHF_B2_SB(src1, src1, src1, src1, mask0, mask1, vec2, vec3);
    VSHF_B2_SB(src2, src2, src2, src2, mask0, mask1, vec4, vec5);

    dsth0 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dsth1 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
    dsth2 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);

    ILVRL_H2_SH(dsth1, dsth0, dst10_r, dst10_l);
    ILVRL_H2_SH(dsth2, dsth1, dst21_r, dst21_l);

    for (loop_cnt = 4; loop_cnt--;) {
        LD_SB4(src_tmp, src_stride, src3, src4, src5, src6);
        src_tmp += (4 * src_stride);
        XORI_B4_128_SB(src3, src4, src5, src6);

        VSHF_B2_SB(src3, src3, src3, src3, mask0, mask1, vec0, vec1);
        VSHF_B2_SB(src4, src4, src4, src4, mask0, mask1, vec2, vec3);
        VSHF_B2_SB(src5, src5, src5, src5, mask0, mask1, vec4, vec5);
        VSHF_B2_SB(src6, src6, src6, src6, mask0, mask1, vec6, vec7);

        dsth3 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dsth4 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dsth5 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dsth6 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        ILVRL_H2_SH(dsth3, dsth2, dst32_r, dst32_l);
        ILVRL_H2_SH(dsth4, dsth3, dst43_r, dst43_l);
        ILVRL_H2_SH(dsth5, dsth4, dst54_r, dst54_l);
        ILVRL_H2_SH(dsth6, dsth5, dst65_r, dst65_l);

        dst0_r = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst0_l = HEVC_FILT_4TAP(dst10_l, dst32_l, filt_h0, filt_h1);
        dst1_r = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst1_l = HEVC_FILT_4TAP(dst21_l, dst43_l, filt_h0, filt_h1);
        dst2_r = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst2_l = HEVC_FILT_4TAP(dst32_l, dst54_l, filt_h0, filt_h1);
        dst3_r = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst3_l = HEVC_FILT_4TAP(dst43_l, dst65_l, filt_h0, filt_h1);

        SRA_4V(dst0_r, dst0_l, dst1_r, dst1_l, 6);
        SRA_4V(dst2_r, dst2_l, dst3_r, dst3_l, 6);

        PCKEV_H4_SH(dst0_l, dst0_r, dst1_l, dst1_r, dst2_l, dst2_r, dst3_l,
                    dst3_r, tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_D4(out0, out1, 0, 1, 0, 1, dst_tmp, dst_stride);
        dst_tmp += (4 * dst_stride);

        dst10_r = dst54_r;
        dst10_l = dst54_l;
        dst21_r = dst65_r;
        dst21_l = dst65_l;
        dsth2 = dsth6;
    }

    src += 8;
    dst += 8;

    mask2 = LD_SB(ff_hevc_mask_arr + 16);
    mask3 = mask2 + 2;

    LD_SB3(src, src_stride, src0, src1, src2);
    src += (3 * src_stride);
    XORI_B3_128_SB(src0, src1, src2);
    VSHF_B2_SB(src0, src1, src0, src1, mask2, mask3, vec0, vec1);
    VSHF_B2_SB(src1, src2, src1, src2, mask2, mask3, vec2, vec3);

    dst10 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
    dst21 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);

    ILVRL_H2_SH(dst21, dst10, dst10_r, dst21_r);
    dst22 = (v8i16) __msa_splati_d((v2i64) dst21, 1);

    for (loop_cnt = 2; loop_cnt--;) {
        LD_SB8(src, src_stride,
               src3, src4, src5, src6, src7, src8, src9, src10);
        src += (8 * src_stride);
        XORI_B8_128_SB(src3, src4, src5, src6, src7, src8, src9, src10);
        VSHF_B2_SB(src3, src7, src3, src7, mask2, mask3, vec0, vec1);
        VSHF_B2_SB(src4, src8, src4, src8, mask2, mask3, vec2, vec3);
        VSHF_B2_SB(src5, src9, src5, src9, mask2, mask3, vec4, vec5);
        VSHF_B2_SB(src6, src10, src6, src10, mask2, mask3, vec6, vec7);

        dst73 = HEVC_FILT_4TAP_SH(vec0, vec1, filt0, filt1);
        dst84 = HEVC_FILT_4TAP_SH(vec2, vec3, filt0, filt1);
        dst95 = HEVC_FILT_4TAP_SH(vec4, vec5, filt0, filt1);
        dst106 = HEVC_FILT_4TAP_SH(vec6, vec7, filt0, filt1);

        dst32_r = __msa_ilvr_h(dst73, dst22);
        ILVRL_H2_SH(dst84, dst73, dst43_r, dst87_r);
        ILVRL_H2_SH(dst95, dst84, dst54_r, dst98_r);
        ILVRL_H2_SH(dst106, dst95, dst65_r, dst109_r);
        dst22 = (v8i16) __msa_splati_d((v2i64) dst73, 1);
        dst76_r = __msa_ilvr_h(dst22, dst106);

        dst0 = HEVC_FILT_4TAP(dst10_r, dst32_r, filt_h0, filt_h1);
        dst1 = HEVC_FILT_4TAP(dst21_r, dst43_r, filt_h0, filt_h1);
        dst2 = HEVC_FILT_4TAP(dst32_r, dst54_r, filt_h0, filt_h1);
        dst3 = HEVC_FILT_4TAP(dst43_r, dst65_r, filt_h0, filt_h1);
        dst4 = HEVC_FILT_4TAP(dst54_r, dst76_r, filt_h0, filt_h1);
        dst5 = HEVC_FILT_4TAP(dst65_r, dst87_r, filt_h0, filt_h1);
        dst6 = HEVC_FILT_4TAP(dst76_r, dst98_r, filt_h0, filt_h1);
        dst7 = HEVC_FILT_4TAP(dst87_r, dst109_r, filt_h0, filt_h1);
        SRA_4V(dst0, dst1, dst2, dst3, 6);
        SRA_4V(dst4, dst5, dst6, dst7, 6);
        PCKEV_H4_SH(dst1, dst0, dst3, dst2, dst5, dst4, dst7, dst6,
                    tmp0, tmp1, tmp2, tmp3);
        SRARI_H4_SH(tmp0, tmp1, tmp2, tmp3, 6);
        SAT_SH4_SH(tmp0, tmp1, tmp2, tmp3, 7);
        out0 = PCKEV_XORI128_UB(tmp0, tmp1);
        out1 = PCKEV_XORI128_UB(tmp2, tmp3);
        ST_W8(out0, out1, 0, 1, 2, 3, 0, 1, 2, 3, dst, dst_stride);
        dst += (8 * dst_stride);

        dst10_r = dst98_r;
        dst21_r = dst109_r;
        dst22 = (v8i16) __msa_splati_d((v2i64) dst106, 1);
    }
}

static void hevc_hv_uni_4t_16w_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    if (4 == height) {
        hevc_hv_uni_4t_8multx4_msa(src, src_stride, dst, dst_stride, filter_x,
                                   filter_y, 2);
    } else {
        hevc_hv_uni_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                       filter_x, filter_y, height, 2);
    }
}

static void hevc_hv_uni_4t_24w_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 3);
}

static void hevc_hv_uni_4t_32w_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride,
                                   const int8_t *filter_x,
                                   const int8_t *filter_y,
                                   int32_t height)
{
    hevc_hv_uni_4t_8multx4mult_msa(src, src_stride, dst, dst_stride,
                                   filter_x, filter_y, height, 4);
}

#define UNI_MC_COPY(WIDTH)                                                 \
void ff_hevc_put_hevc_uni_pel_pixels##WIDTH##_8_msa(uint8_t *dst,          \
                                                    ptrdiff_t dst_stride,  \
                                                    const uint8_t *src,    \
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
                                                       ptrdiff_t dst_stride,   \
                                                       const uint8_t *src,     \
                                                       ptrdiff_t src_stride,   \
                                                       int height,             \
                                                       intptr_t mx,            \
                                                       intptr_t my,            \
                                                       int width)              \
{                                                                              \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR];                  \
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

#define UNI_MC_HV(PEL, WIDTH, TAP)                                         \
void ff_hevc_put_hevc_uni_##PEL##_hv##WIDTH##_8_msa(uint8_t *dst,          \
                                                    ptrdiff_t dst_stride,  \
                                                    const uint8_t *src,    \
                                                    ptrdiff_t src_stride,  \
                                                    int height,            \
                                                    intptr_t mx,           \
                                                    intptr_t my,           \
                                                    int width)             \
{                                                                          \
    const int8_t *filter_x = ff_hevc_##PEL##_filters[mx];                  \
    const int8_t *filter_y = ff_hevc_##PEL##_filters[my];                  \
                                                                           \
    hevc_hv_uni_##TAP##t_##WIDTH##w_msa(src, src_stride, dst, dst_stride,  \
                                        filter_x, filter_y, height);       \
}

UNI_MC_HV(qpel, 4, 8);
UNI_MC_HV(qpel, 8, 8);
UNI_MC_HV(qpel, 12, 8);
UNI_MC_HV(qpel, 16, 8);
UNI_MC_HV(qpel, 24, 8);
UNI_MC_HV(qpel, 32, 8);
UNI_MC_HV(qpel, 48, 8);
UNI_MC_HV(qpel, 64, 8);

UNI_MC_HV(epel, 4, 4);
UNI_MC_HV(epel, 6, 4);
UNI_MC_HV(epel, 8, 4);
UNI_MC_HV(epel, 12, 4);
UNI_MC_HV(epel, 16, 4);
UNI_MC_HV(epel, 24, 4);
UNI_MC_HV(epel, 32, 4);

#undef UNI_MC_HV
