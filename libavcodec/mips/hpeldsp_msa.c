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
#include "libavcodec/mips/hpeldsp_mips.h"

#define PCKEV_AVG_ST_UB(in0, in1, dst, pdst)                  \
{                                                             \
    v16u8 tmp_m;                                              \
                                                              \
    tmp_m = (v16u8) __msa_pckev_b((v16i8) in0, (v16i8) in1);  \
    tmp_m = __msa_aver_u_b(tmp_m, (v16u8) dst);               \
    ST_UB(tmp_m, (pdst));                                     \
}

#define PCKEV_ST_SB4(in0, in1, in2, in3, in4, in5, in6, in7, pdst, stride)  \
{                                                                           \
    v16i8 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                   \
    uint8_t *pdst_m = (uint8_t *) (pdst);                                   \
                                                                            \
    PCKEV_B4_SB(in0, in1, in2, in3, in4, in5, in6, in7,                     \
                tmp0_m, tmp1_m, tmp2_m, tmp3_m);                            \
    ST_SB4(tmp0_m, tmp1_m, tmp2_m, tmp3_m, pdst_m, stride);                 \
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

static void common_hz_bil_4w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint8_t height)
{
    uint8_t loop_cnt;
    uint32_t out0, out1;
    v16u8 src0, src1, src0_sld1, src1_sld1, res0, res1;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_UB2(src, src_stride, src0, src1);
        src += (2 * src_stride);

        SLDI_B2_0_UB(src0, src1, src0_sld1, src1_sld1, 1);
        AVER_UB2_UB(src0_sld1, src0, src1_sld1, src1, res0, res1);

        out0 = __msa_copy_u_w((v4i32) res0, 0);
        out1 = __msa_copy_u_w((v4i32) res1, 0);
        SW(out0, dst);
        dst += dst_stride;
        SW(out1, dst);
        dst += dst_stride;
    }
}

static void common_hz_bil_8w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint8_t height)
{
    uint8_t loop_cnt;
    v16i8 src0, src1, src2, src3, src0_sld1, src1_sld1, src2_sld1, src3_sld1;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        SLDI_B4_0_SB(src0, src1, src2, src3,
                     src0_sld1, src1_sld1, src2_sld1, src3_sld1, 1);
        AVER_ST8x4_UB(src0, src0_sld1, src1, src1_sld1,
                      src2, src2_sld1, src3, src3_sld1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_bil_16w_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        LD_UB8((src + 1), src_stride,
               src8, src9, src10, src11, src12, src13, src14, src15);
        src += (8 * src_stride);

        AVER_ST16x4_UB(src0, src8, src1, src9, src2, src10, src3, src11,
                       dst, dst_stride);
        dst += (4 * dst_stride);

        AVER_ST16x4_UB(src4, src12, src5, src13, src6, src14, src7, src15,
                       dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_bil_no_rnd_8x8_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16i8 src0_sld1, src1_sld1, src2_sld1, src3_sld1;
    v16i8 src4_sld1, src5_sld1, src6_sld1, src7_sld1;

    LD_SB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);

    SLDI_B4_0_SB(src0, src1, src2, src3,
                 src0_sld1, src1_sld1, src2_sld1, src3_sld1, 1);
    SLDI_B4_0_SB(src4, src5, src6, src7,
                 src4_sld1, src5_sld1, src6_sld1, src7_sld1, 1);

    AVE_ST8x4_UB(src0, src0_sld1, src1, src1_sld1,
                 src2, src2_sld1, src3, src3_sld1, dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST8x4_UB(src4, src4_sld1, src5, src5_sld1,
                 src6, src6_sld1, src7, src7_sld1, dst, dst_stride);
}

static void common_hz_bil_no_rnd_4x8_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    v16i8 src0, src1, src2, src3, src0_sld1, src1_sld1, src2_sld1, src3_sld1;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    SLDI_B4_0_SB(src0, src1, src2, src3,
                 src0_sld1, src1_sld1, src2_sld1, src3_sld1, 1);
    AVE_ST8x4_UB(src0, src0_sld1, src1, src1_sld1,
                 src2, src2_sld1, src3, src3_sld1, dst, dst_stride);
}

static void common_hz_bil_no_rnd_16x16_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 src9, src10, src11, src12, src13, src14, src15;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8((src + 1), src_stride,
           src8, src9, src10, src11, src12, src13, src14, src15);
    src += (8 * src_stride);

    AVE_ST16x4_UB(src0, src8, src1, src9, src2, src10, src3, src11,
                  dst, dst_stride);
    dst += (4 * dst_stride);

    LD_UB4(src, src_stride, src0, src1, src2, src3);
    LD_UB4((src + 1), src_stride, src8, src9, src10, src11);
    src += (4 * src_stride);

    AVE_ST16x4_UB(src4, src12, src5, src13, src6, src14, src7, src15,
                  dst, dst_stride);
    dst += (4 * dst_stride);

    LD_UB4(src, src_stride, src4, src5, src6, src7);
    LD_UB4((src + 1), src_stride, src12, src13, src14, src15);
    src += (4 * src_stride);

    AVE_ST16x4_UB(src0, src8, src1, src9, src2, src10, src3, src11,
                  dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST16x4_UB(src4, src12, src5, src13, src6, src14, src7, src15,
                  dst, dst_stride);
}

static void common_hz_bil_no_rnd_8x16_msa(const uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 src9, src10, src11, src12, src13, src14, src15;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8((src + 1), src_stride,
           src8, src9, src10, src11, src12, src13, src14, src15);

    AVE_ST16x4_UB(src0, src8, src1, src9, src2, src10, src3, src11,
                  dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST16x4_UB(src4, src12, src5, src13, src6, src14, src7, src15,
                  dst, dst_stride);
}

static void common_hz_bil_and_aver_dst_4w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint8_t height)
{
    uint8_t loop_cnt;
    uint32_t dst0, dst1, out0, out1;
    v16u8 src0, src1, src0_sld1, src1_sld1, res0, res1;
    v16u8 tmp0 = { 0 };
    v16u8 tmp1 = { 0 };

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_UB2(src, src_stride, src0, src1);
        src += (2 * src_stride);

        SLDI_B2_0_UB(src0, src1, src0_sld1, src1_sld1, 1);

        dst0 = LW(dst);
        dst1 = LW(dst + dst_stride);
        tmp0 = (v16u8) __msa_insert_w((v4i32) tmp0, 0, dst0);
        tmp1 = (v16u8) __msa_insert_w((v4i32) tmp1, 0, dst1);

        AVER_UB2_UB(src0_sld1, src0, src1_sld1, src1, res0, res1);
        AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);

        out0 = __msa_copy_u_w((v4i32) res0, 0);
        out1 = __msa_copy_u_w((v4i32) res1, 0);
        SW(out0, dst);
        dst += dst_stride;
        SW(out1, dst);
        dst += dst_stride;
    }
}

static void common_hz_bil_and_aver_dst_8w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint8_t height)
{
    uint8_t loop_cnt;
    v16i8 src0, src1, src2, src3, src0_sld1, src1_sld1, src2_sld1, src3_sld1;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        SLDI_B4_0_SB(src0, src1, src2, src3,
                     src0_sld1, src1_sld1, src2_sld1, src3_sld1, 1);

        AVER_DST_ST8x4_UB(src0, src0_sld1, src1, src1_sld1, src2, src2_sld1,
                          src3, src3_sld1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hz_bil_and_aver_dst_16w_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 src9, src10, src11, src12, src13, src14, src15;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        LD_UB8((src + 1), src_stride,
               src8, src9, src10, src11, src12, src13, src14, src15);
        src += (8 * src_stride);

        AVER_DST_ST16x4_UB(src0, src8, src1, src9, src2, src10, src3, src11,
                           dst, dst_stride);
        dst += (4 * dst_stride);
        AVER_DST_ST16x4_UB(src4, src12, src5, src13, src6, src14, src7, src15,
                           dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_vt_bil_4w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint8_t height)
{
    uint8_t loop_cnt;
    uint32_t out0, out1;
    v16u8 src0, src1, src2, res0, res1;

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_UB2(src, src_stride, src1, src2);
        src += (2 * src_stride);

        AVER_UB2_UB(src0, src1, src1, src2, res0, res1);

        out0 = __msa_copy_u_w((v4i32) res0, 0);
        out1 = __msa_copy_u_w((v4i32) res1, 0);
        SW(out0, dst);
        dst += dst_stride;
        SW(out1, dst);
        dst += dst_stride;

        src0 = src2;
    }
}

static void common_vt_bil_8w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4;

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        AVER_ST8x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                      dst, dst_stride);
        dst += (4 * dst_stride);

        src0 = src4;
    }
}

static void common_vt_bil_16w_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src1, src2, src3, src4, src5, src6, src7, src8);
        src += (8 * src_stride);

        AVER_ST16x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                       dst, dst_stride);
        dst += (4 * dst_stride);
        AVER_ST16x4_UB(src4, src5, src5, src6, src6, src7, src7, src8,
                       dst, dst_stride);
        dst += (4 * dst_stride);

        src0 = src8;
    }
}

static void common_vt_bil_no_rnd_8x8_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    src8 = LD_UB(src);

    AVE_ST8x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                 dst, dst_stride);
    dst += (4 * dst_stride);

    AVE_ST8x4_UB(src4, src5, src5, src6, src6, src7, src7, src8,
                 dst, dst_stride);
}

static void common_vt_bil_no_rnd_4x8_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4;

    LD_UB5(src, src_stride, src0, src1, src2, src3, src4);
    AVE_ST8x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                 dst, dst_stride);
}

static void common_vt_bil_no_rnd_16x16_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 src9, src10, src11, src12, src13, src14, src15, src16;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    LD_UB8(src, src_stride,
           src8, src9, src10, src11, src12, src13, src14, src15);
    src += (8 * src_stride);
    src16 = LD_UB(src);

    AVE_ST16x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                  dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST16x4_UB(src4, src5, src5, src6, src6, src7, src7, src8,
                  dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST16x4_UB(src8, src9, src9, src10, src10, src11, src11, src12,
                  dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST16x4_UB(src12, src13, src13, src14,
                  src14, src15, src15, src16, dst, dst_stride);
}

static void common_vt_bil_no_rnd_8x16_msa(const uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    src8 = LD_UB(src);

    AVE_ST16x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                  dst, dst_stride);
    dst += (4 * dst_stride);
    AVE_ST16x4_UB(src4, src5, src5, src6, src6, src7, src7, src8,
                  dst, dst_stride);
}

static void common_vt_bil_and_aver_dst_4w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint8_t height)
{
    uint8_t loop_cnt;
    uint32_t out0, out1, dst0, dst1;
    v16u8 src0, src1, src2;
    v16u8 tmp0 = { 0 };
    v16u8 tmp1 = { 0 };
    v16u8 res0, res1;

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_UB2(src, src_stride, src1, src2);
        src += (2 * src_stride);
        dst0 = LW(dst);
        dst1 = LW(dst + dst_stride);
        tmp0 = (v16u8) __msa_insert_w((v4i32) tmp0, 0, dst0);
        tmp1 = (v16u8) __msa_insert_w((v4i32) tmp1, 0, dst1);
        AVER_UB2_UB(src0, src1, src1, src2, res0, res1);
        AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
        out0 = __msa_copy_u_w((v4i32) res0, 0);
        out1 = __msa_copy_u_w((v4i32) res1, 0);
        SW(out0, dst);
        dst += dst_stride;
        SW(out1, dst);
        dst += dst_stride;
        src0 = src2;
    }
}

static void common_vt_bil_and_aver_dst_8w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4;

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_UB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        AVER_DST_ST8x4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                          dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void common_vt_bil_and_aver_dst_16w_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 res0, res1, res2, res3, res4, res5, res6, res7;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    src0 = LD_UB(src);
    src += src_stride;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src1, src2, src3, src4, src5, src6, src7, src8);
        src += (8 * src_stride);
        AVER_UB4_UB(src0, src1, src1, src2, src2, src3, src3, src4,
                    res0, res1, res2, res3);
        AVER_UB4_UB(src4, src5, src5, src6, src6, src7, src7, src8,
                    res4, res5, res6, res7);

        LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
        AVER_UB4_UB(dst0, res0, dst1, res1, dst2, res2, dst3, res3,
                    res0, res1, res2, res3);
        AVER_UB4_UB(dst4, res4, dst5, res5, dst6, res6, dst7, res7,
                    res4, res5, res6, res7);
        ST_UB8(res0, res1, res2, res3, res4, res5, res6, res7, dst, dst_stride);
        dst += (8 * dst_stride);

        src0 = src8;
    }
}

static void common_hv_bil_4w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint8_t height)
{
    uint8_t loop_cnt;
    uint32_t res0, res1;
    v16i8 src0, src1, src2, src0_sld1, src1_sld1, src2_sld1;
    v16u8 src0_r, src1_r, src2_r, res;
    v8u16 add0, add1, add2, sum0, sum1;

    src0 = LD_SB(src);
    src += src_stride;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src1, src2);
        src += (2 * src_stride);

        SLDI_B3_0_SB(src0, src1, src2, src0_sld1, src1_sld1, src2_sld1, 1);
        ILVR_B3_UB(src0_sld1, src0, src1_sld1, src1, src2_sld1, src2,
                   src0_r, src1_r, src2_r);
        HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
        ADD2(add0, add1, add1, add2, sum0, sum1);
        SRARI_H2_UH(sum0, sum1, 2);
        res = (v16u8) __msa_pckev_b((v16i8) sum1, (v16i8) sum0);
        res0 = __msa_copy_u_w((v4i32) res, 0);
        res1 = __msa_copy_u_w((v4i32) res, 2);
        SW(res0, dst);
        dst += dst_stride;
        SW(res1, dst);
        dst += dst_stride;

        src0 = src2;
    }
}

static void common_hv_bil_8w_msa(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 uint8_t height)
{
    uint8_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src0_sld1, src1_sld1, src2_sld1, src3_sld1, src4_sld1;
    v16u8 src0_r, src1_r, src2_r, src3_r, src4_r;
    v8u16 add0, add1, add2, add3, add4;
    v8u16 sum0, sum1, sum2, sum3;

    src0 = LD_SB(src);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        SLDI_B3_0_SB(src0, src1, src2, src0_sld1, src1_sld1, src2_sld1, 1);
        SLDI_B2_0_SB(src3, src4, src3_sld1, src4_sld1, 1);
        ILVR_B3_UB(src0_sld1, src0, src1_sld1, src1, src2_sld1, src2, src0_r,
                   src1_r, src2_r);
        ILVR_B2_UB(src3_sld1, src3, src4_sld1, src4, src3_r, src4_r);
        HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
        HADD_UB2_UH(src3_r, src4_r, add3, add4);
        ADD4(add0, add1, add1, add2, add2, add3, add3, add4,
             sum0, sum1, sum2, sum3);
        SRARI_H4_UH(sum0, sum1, sum2, sum3, 2);
        PCKEV_B2_SB(sum1, sum0, sum3, sum2, src0, src1);
        ST8x4_UB(src0, src1, dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void common_hv_bil_16w_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16u8 src10, src11, src12, src13, src14, src15, src16, src17;
    v8u16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8u16 src8_r, src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l;
    v8u16 src7_l, src8_l;
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r, sum4_r, sum5_r, sum6_r, sum7_r;
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l, sum4_l, sum5_l, sum6_l, sum7_l;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        LD_UB8((src + 1), src_stride,
               src9, src10, src11, src12, src13, src14, src15, src16);
        src += (8 * src_stride);

        src8 = LD_UB(src);
        src17 = LD_UB(src + 1);

        ILVRL_B2_UH(src9, src0, src0_r, src0_l);
        ILVRL_B2_UH(src10, src1, src1_r, src1_l);
        ILVRL_B2_UH(src11, src2, src2_r, src2_l);
        ILVRL_B2_UH(src12, src3, src3_r, src3_l);
        ILVRL_B2_UH(src13, src4, src4_r, src4_l);
        ILVRL_B2_UH(src14, src5, src5_r, src5_l);
        ILVRL_B2_UH(src15, src6, src6_r, src6_l);
        ILVRL_B2_UH(src16, src7, src7_r, src7_l);
        ILVRL_B2_UH(src17, src8, src8_r, src8_l);
        HADD_UB3_UH(src0_r, src1_r, src2_r, src0_r, src1_r, src2_r);
        HADD_UB3_UH(src3_r, src4_r, src5_r, src3_r, src4_r, src5_r);
        HADD_UB3_UH(src6_r, src7_r, src8_r, src6_r, src7_r, src8_r);
        HADD_UB3_UH(src0_l, src1_l, src2_l, src0_l, src1_l, src2_l);
        HADD_UB3_UH(src3_l, src4_l, src5_l, src3_l, src4_l, src5_l);
        HADD_UB3_UH(src6_l, src7_l, src8_l, src6_l, src7_l, src8_l);
        ADD4(src0_r, src1_r, src1_r, src2_r, src2_r, src3_r, src3_r, src4_r,
             sum0_r, sum1_r, sum2_r, sum3_r);
        ADD4(src4_r, src5_r, src5_r, src6_r, src6_r, src7_r, src7_r, src8_r,
             sum4_r, sum5_r, sum6_r, sum7_r);
        ADD4(src0_l, src1_l, src1_l, src2_l, src2_l, src3_l, src3_l, src4_l,
             sum0_l, sum1_l, sum2_l, sum3_l);
        ADD4(src4_l, src5_l, src5_l, src6_l, src6_l, src7_l, src7_l, src8_l,
             sum4_l, sum5_l, sum6_l, sum7_l);
        SRARI_H4_UH(sum0_r, sum1_r, sum2_r, sum3_r, 2);
        SRARI_H4_UH(sum4_r, sum5_r, sum6_r, sum7_r, 2);
        SRARI_H4_UH(sum0_l, sum1_l, sum2_l, sum3_l, 2);
        SRARI_H4_UH(sum4_l, sum5_l, sum6_l, sum7_l, 2);
        PCKEV_ST_SB4(sum0_l, sum0_r, sum1_l, sum1_r, sum2_l, sum2_r,
                     sum3_l, sum3_r, dst, dst_stride);
        dst += (4 * dst_stride);
        PCKEV_ST_SB4(sum4_l, sum4_r, sum5_l, sum5_r, sum6_l, sum6_r,
                     sum7_l, sum7_r, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void common_hv_bil_no_rnd_8x8_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16u8 src0_sld1, src1_sld1, src2_sld1, src3_sld1;
    v16u8 src4_sld1, src5_sld1, src6_sld1, src7_sld1, src8_sld1;
    v8u16 src0_r, src1_r, src2_r, src3_r;
    v8u16 src4_r, src5_r, src6_r, src7_r, src8_r;
    v8u16 add0, add1, add2, add3, add4, add5, add6, add7, add8;
    v8u16 sum0, sum1, sum2, sum3, sum4, sum5, sum6, sum7;
    v16i8 out0, out1;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    src8 = LD_UB(src);

    SLDI_B4_0_UB(src0, src1, src2, src3, src0_sld1, src1_sld1, src2_sld1,
                 src3_sld1, 1);
    SLDI_B3_0_UB(src4, src5, src6, src4_sld1, src5_sld1, src6_sld1, 1);
    SLDI_B2_0_UB(src7, src8, src7_sld1, src8_sld1, 1);
    ILVR_B4_UH(src0_sld1, src0, src1_sld1, src1, src2_sld1, src2, src3_sld1,
               src3, src0_r, src1_r, src2_r, src3_r);
    ILVR_B3_UH(src4_sld1, src4, src5_sld1, src5, src6_sld1, src6, src4_r,
               src5_r, src6_r);
    ILVR_B2_UH(src7_sld1, src7, src8_sld1, src8, src7_r, src8_r);
    HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
    HADD_UB3_UH(src3_r, src4_r, src5_r, add3, add4, add5);
    HADD_UB3_UH(src6_r, src7_r, src8_r, add6, add7, add8);

    sum0 = add0 + add1 + 1;
    sum1 = add1 + add2 + 1;
    sum2 = add2 + add3 + 1;
    sum3 = add3 + add4 + 1;
    sum4 = add4 + add5 + 1;
    sum5 = add5 + add6 + 1;
    sum6 = add6 + add7 + 1;
    sum7 = add7 + add8 + 1;

    SRA_4V(sum0, sum1, sum2, sum3, 2);
    SRA_4V(sum4, sum5, sum6, sum7, 2);
    PCKEV_B2_SB(sum1, sum0, sum3, sum2, out0, out1);
    ST8x4_UB(out0, out1, dst, dst_stride);
    PCKEV_B2_SB(sum5, sum4, sum7, sum6, out0, out1);
    ST8x4_UB(out0, out1, dst + 4 * dst_stride, dst_stride);
}

static void common_hv_bil_no_rnd_4x8_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src0_sld1, src1_sld1, src2_sld1, src3_sld1, src4_sld1;
    v8u16 src0_r, src1_r, src2_r, src3_r, src4_r;
    v8u16 add0, add1, add2, add3, add4;
    v8u16 sum0, sum1, sum2, sum3;
    v16i8 out0, out1;

    LD_SB4(src, src_stride, src0, src1, src2, src3);
    src += (4 * src_stride);
    src4 = LD_SB(src);

    SLDI_B3_0_SB(src0, src1, src2, src0_sld1, src1_sld1, src2_sld1, 1);
    SLDI_B2_0_SB(src3, src4, src3_sld1, src4_sld1, 1);
    ILVR_B3_UH(src0_sld1, src0, src1_sld1, src1, src2_sld1, src2, src0_r,
               src1_r, src2_r);
    ILVR_B2_UH(src3_sld1, src3, src4_sld1, src4, src3_r, src4_r);
    HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
    HADD_UB2_UH(src3_r, src4_r, add3, add4);

    sum0 = add0 + add1 + 1;
    sum1 = add1 + add2 + 1;
    sum2 = add2 + add3 + 1;
    sum3 = add3 + add4 + 1;

    SRA_4V(sum0, sum1, sum2, sum3, 2);
    PCKEV_B2_SB(sum1, sum0, sum3, sum2, out0, out1);
    ST8x4_UB(out0, out1, dst, dst_stride);
}

static void common_hv_bil_no_rnd_16x16_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16u8 src10, src11, src12, src13, src14, src15, src16, src17;
    v8u16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8u16 src8_r, src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l;
    v8u16 src7_l, src8_l;
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r, sum4_r, sum5_r, sum6_r, sum7_r;
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l, sum4_l, sum5_l, sum6_l, sum7_l;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8((src + 1), src_stride,
           src9, src10, src11, src12, src13, src14, src15, src16);
    src += (8 * src_stride);
    src8 = LD_UB(src);
    src17 = LD_UB(src + 1);

    ILVRL_B2_UH(src9, src0, src0_r, src0_l);
    ILVRL_B2_UH(src10, src1, src1_r, src1_l);
    ILVRL_B2_UH(src11, src2, src2_r, src2_l);
    ILVRL_B2_UH(src12, src3, src3_r, src3_l);
    ILVRL_B2_UH(src13, src4, src4_r, src4_l);
    ILVRL_B2_UH(src14, src5, src5_r, src5_l);
    ILVRL_B2_UH(src15, src6, src6_r, src6_l);
    ILVRL_B2_UH(src16, src7, src7_r, src7_l);
    ILVRL_B2_UH(src17, src8, src8_r, src8_l);

    HADD_UB3_UH(src0_r, src1_r, src2_r, src0_r, src1_r, src2_r);
    HADD_UB3_UH(src3_r, src4_r, src5_r, src3_r, src4_r, src5_r);
    HADD_UB3_UH(src6_r, src7_r, src8_r, src6_r, src7_r, src8_r);
    HADD_UB3_UH(src0_l, src1_l, src2_l, src0_l, src1_l, src2_l);
    HADD_UB3_UH(src3_l, src4_l, src5_l, src3_l, src4_l, src5_l);
    HADD_UB3_UH(src6_l, src7_l, src8_l, src6_l, src7_l, src8_l);

    sum0_r = src0_r + src1_r + 1;
    sum1_r = src1_r + src2_r + 1;
    sum2_r = src2_r + src3_r + 1;
    sum3_r = src3_r + src4_r + 1;
    sum4_r = src4_r + src5_r + 1;
    sum5_r = src5_r + src6_r + 1;
    sum6_r = src6_r + src7_r + 1;
    sum7_r = src7_r + src8_r + 1;
    sum0_l = src0_l + src1_l + 1;
    sum1_l = src1_l + src2_l + 1;
    sum2_l = src2_l + src3_l + 1;
    sum3_l = src3_l + src4_l + 1;
    sum4_l = src4_l + src5_l + 1;
    sum5_l = src5_l + src6_l + 1;
    sum6_l = src6_l + src7_l + 1;
    sum7_l = src7_l + src8_l + 1;

    SRA_4V(sum0_r, sum1_r, sum2_r, sum3_r, 2);
    SRA_4V(sum4_r, sum5_r, sum6_r, sum7_r, 2);
    SRA_4V(sum0_l, sum1_l, sum2_l, sum3_l, 2);
    SRA_4V(sum4_l, sum5_l, sum6_l, sum7_l, 2);
    PCKEV_ST_SB4(sum0_l, sum0_r, sum1_l, sum1_r,
                 sum2_l, sum2_r, sum3_l, sum3_r, dst, dst_stride);
    dst += (4 * dst_stride);

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8((src + 1), src_stride,
           src9, src10, src11, src12, src13, src14, src15, src16);
    src += (8 * src_stride);
    src8 = LD_UB(src);
    src17 = LD_UB(src + 1);

    PCKEV_ST_SB4(sum4_l, sum4_r, sum5_l, sum5_r,
                 sum6_l, sum6_r, sum7_l, sum7_r, dst, dst_stride);
    dst += (4 * dst_stride);

    ILVRL_B2_UH(src9, src0, src0_r, src0_l);
    ILVRL_B2_UH(src10, src1, src1_r, src1_l);
    ILVRL_B2_UH(src11, src2, src2_r, src2_l);
    ILVRL_B2_UH(src12, src3, src3_r, src3_l);
    ILVRL_B2_UH(src13, src4, src4_r, src4_l);
    ILVRL_B2_UH(src14, src5, src5_r, src5_l);
    ILVRL_B2_UH(src15, src6, src6_r, src6_l);
    ILVRL_B2_UH(src16, src7, src7_r, src7_l);
    ILVRL_B2_UH(src17, src8, src8_r, src8_l);

    HADD_UB3_UH(src0_r, src1_r, src2_r, src0_r, src1_r, src2_r);
    HADD_UB3_UH(src3_r, src4_r, src5_r, src3_r, src4_r, src5_r);
    HADD_UB3_UH(src6_r, src7_r, src8_r, src6_r, src7_r, src8_r);
    HADD_UB3_UH(src0_l, src1_l, src2_l, src0_l, src1_l, src2_l);
    HADD_UB3_UH(src3_l, src4_l, src5_l, src3_l, src4_l, src5_l);
    HADD_UB3_UH(src6_l, src7_l, src8_l, src6_l, src7_l, src8_l);

    sum0_r = src0_r + src1_r + 1;
    sum1_r = src1_r + src2_r + 1;
    sum2_r = src2_r + src3_r + 1;
    sum3_r = src3_r + src4_r + 1;
    sum4_r = src4_r + src5_r + 1;
    sum5_r = src5_r + src6_r + 1;
    sum6_r = src6_r + src7_r + 1;
    sum7_r = src7_r + src8_r + 1;
    sum0_l = src0_l + src1_l + 1;
    sum1_l = src1_l + src2_l + 1;
    sum2_l = src2_l + src3_l + 1;
    sum3_l = src3_l + src4_l + 1;
    sum4_l = src4_l + src5_l + 1;
    sum5_l = src5_l + src6_l + 1;
    sum6_l = src6_l + src7_l + 1;
    sum7_l = src7_l + src8_l + 1;

    SRA_4V(sum0_r, sum1_r, sum2_r, sum3_r, 2);
    SRA_4V(sum4_r, sum5_r, sum6_r, sum7_r, 2);
    SRA_4V(sum0_l, sum1_l, sum2_l, sum3_l, 2);
    SRA_4V(sum4_l, sum5_l, sum6_l, sum7_l, 2);
    PCKEV_ST_SB4(sum0_l, sum0_r, sum1_l, sum1_r,
                 sum2_l, sum2_r, sum3_l, sum3_r, dst, dst_stride);
    dst += (4 * dst_stride);
    PCKEV_ST_SB4(sum4_l, sum4_r, sum5_l, sum5_r,
                 sum6_l, sum6_r, sum7_l, sum7_r, dst, dst_stride);
}

static void common_hv_bil_no_rnd_8x16_msa(const uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    v16u8 src10, src11, src12, src13, src14, src15, src16, src17;
    v8u16 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v8u16 src8_r, src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l;
    v8u16 src7_l, src8_l;
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r, sum4_r, sum5_r, sum6_r, sum7_r;
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l, sum4_l, sum5_l, sum6_l, sum7_l;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8((src + 1), src_stride,
           src9, src10, src11, src12, src13, src14, src15, src16);
    src += (8 * src_stride);
    src8 = LD_UB(src);
    src17 = LD_UB(src + 1);

    ILVRL_B2_UH(src9, src0, src0_r, src0_l);
    ILVRL_B2_UH(src10, src1, src1_r, src1_l);
    ILVRL_B2_UH(src11, src2, src2_r, src2_l);
    ILVRL_B2_UH(src12, src3, src3_r, src3_l);
    ILVRL_B2_UH(src13, src4, src4_r, src4_l);
    ILVRL_B2_UH(src14, src5, src5_r, src5_l);
    ILVRL_B2_UH(src15, src6, src6_r, src6_l);
    ILVRL_B2_UH(src16, src7, src7_r, src7_l);
    ILVRL_B2_UH(src17, src8, src8_r, src8_l);

    HADD_UB3_UH(src0_r, src1_r, src2_r, src0_r, src1_r, src2_r);
    HADD_UB3_UH(src3_r, src4_r, src5_r, src3_r, src4_r, src5_r);
    HADD_UB3_UH(src6_r, src7_r, src8_r, src6_r, src7_r, src8_r);
    HADD_UB3_UH(src0_l, src1_l, src2_l, src0_l, src1_l, src2_l);
    HADD_UB3_UH(src3_l, src4_l, src5_l, src3_l, src4_l, src5_l);
    HADD_UB3_UH(src6_l, src7_l, src8_l, src6_l, src7_l, src8_l);

    sum0_r = src0_r + src1_r + 1;
    sum1_r = src1_r + src2_r + 1;
    sum2_r = src2_r + src3_r + 1;
    sum3_r = src3_r + src4_r + 1;
    sum4_r = src4_r + src5_r + 1;
    sum5_r = src5_r + src6_r + 1;
    sum6_r = src6_r + src7_r + 1;
    sum7_r = src7_r + src8_r + 1;
    sum0_l = src0_l + src1_l + 1;
    sum1_l = src1_l + src2_l + 1;
    sum2_l = src2_l + src3_l + 1;
    sum3_l = src3_l + src4_l + 1;
    sum4_l = src4_l + src5_l + 1;
    sum5_l = src5_l + src6_l + 1;
    sum6_l = src6_l + src7_l + 1;
    sum7_l = src7_l + src8_l + 1;

    SRA_4V(sum0_r, sum1_r, sum2_r, sum3_r, 2);
    SRA_4V(sum4_r, sum5_r, sum6_r, sum7_r, 2);
    SRA_4V(sum0_l, sum1_l, sum2_l, sum3_l, 2);
    SRA_4V(sum4_l, sum5_l, sum6_l, sum7_l, 2);
    PCKEV_ST_SB4(sum0_l, sum0_r, sum1_l, sum1_r,
                 sum2_l, sum2_r, sum3_l, sum3_r, dst, dst_stride);
    dst += (4 * dst_stride);
    PCKEV_ST_SB4(sum4_l, sum4_r, sum5_l, sum5_r,
                 sum6_l, sum6_r, sum7_l, sum7_r, dst, dst_stride);
}

static void common_hv_bil_and_aver_dst_4w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint8_t height)
{
    uint8_t loop_cnt;
    uint32_t out0, out1;
    v16i8 src0, src1, src2, src0_sld1, src1_sld1, src2_sld1;
    v16u8 src0_r, src1_r, src2_r;
    v8u16 add0, add1, add2, sum0, sum1;
    v16u8 dst0, dst1, res0, res1;

    src0 = LD_SB(src);
    src += src_stride;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        LD_SB2(src, src_stride, src1, src2);
        src += (2 * src_stride);

        LD_UB2(dst, dst_stride, dst0, dst1);
        SLDI_B3_0_SB(src0, src1, src2, src0_sld1, src1_sld1, src2_sld1, 1);
        ILVR_B3_UB(src0_sld1, src0, src1_sld1, src1, src2_sld1, src2, src0_r,
                   src1_r, src2_r);
        HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
        ADD2(add0, add1, add1, add2, sum0, sum1);
        SRARI_H2_UH(sum0, sum1, 2);
        PCKEV_B2_UB(sum0, sum0, sum1, sum1, res0, res1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);

        out0 = __msa_copy_u_w((v4i32) res0, 0);
        out1 = __msa_copy_u_w((v4i32) res1, 0);
        SW(out0, dst);
        dst += dst_stride;
        SW(out1, dst);
        dst += dst_stride;

        src0 = src2;
    }
}

static void common_hv_bil_and_aver_dst_8w_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              uint8_t height)
{
    uint8_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v16i8 src0_sld1, src1_sld1, src2_sld1, src3_sld1, src4_sld1;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 src0_r, src1_r, src2_r, src3_r, src4_r;
    v8u16 add0, add1, add2, add3, add4;
    v8u16 sum0, sum1, sum2, sum3;

    src0 = LD_SB(src);
    src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LD_SB4(src, src_stride, src1, src2, src3, src4);
        src += (4 * src_stride);

        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        SLDI_B3_0_SB(src0, src1, src2, src0_sld1, src1_sld1, src2_sld1, 1);
        SLDI_B2_0_SB(src3, src4, src3_sld1, src4_sld1, 1);
        ILVR_B3_UB(src0_sld1, src0, src1_sld1, src1, src2_sld1, src2, src0_r,
                   src1_r, src2_r);
        ILVR_B2_UB(src3_sld1, src3, src4_sld1, src4, src3_r, src4_r);
        HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
        HADD_UB2_UH(src3_r, src4_r, add3, add4);
        ADD4(add0, add1, add1, add2, add2, add3, add3, add4,
             sum0, sum1, sum2, sum3);
        SRARI_H4_UH(sum0, sum1, sum2, sum3, 2);
        PCKEV_AVG_ST8x4_UB(sum0, dst0, sum1, dst1,
                           sum2, dst2, sum3, dst3, dst, dst_stride);
        dst += (4 * dst_stride);
        src0 = src4;
    }
}

static void common_hv_bil_and_aver_dst_16w_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst, int32_t dst_stride,
                                               uint8_t height)
{
    uint8_t loop_cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16u8 src11, src12, src13, src14, src15, src16, src17;
    v16u8 src0_r, src1_r, src2_r, src3_r, src4_r, src5_r, src6_r, src7_r;
    v16u8 src8_r, src0_l, src1_l, src2_l, src3_l, src4_l, src5_l, src6_l;
    v16u8 src7_l, src8_l;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r, sum4_r, sum5_r, sum6_r, sum7_r;
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l, sum4_l, sum5_l, sum6_l, sum7_l;
    v8u16 add0, add1, add2, add3, add4, add5, add6, add7, add8;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
        LD_UB8((src + 1), src_stride,
               src9, src10, src11, src12, src13, src14, src15, src16);
        src += (8 * src_stride);

        src8 = LD_UB(src);
        src17 = LD_UB(src + 1);

        ILVRL_B2_UB(src9, src0, src0_r, src0_l);
        ILVRL_B2_UB(src10, src1, src1_r, src1_l);
        ILVRL_B2_UB(src11, src2, src2_r, src2_l);
        ILVRL_B2_UB(src12, src3, src3_r, src3_l);
        ILVRL_B2_UB(src13, src4, src4_r, src4_l);
        ILVRL_B2_UB(src14, src5, src5_r, src5_l);
        ILVRL_B2_UB(src15, src6, src6_r, src6_l);
        ILVRL_B2_UB(src16, src7, src7_r, src7_l);
        ILVRL_B2_UB(src17, src8, src8_r, src8_l);
        HADD_UB3_UH(src0_r, src1_r, src2_r, add0, add1, add2);
        HADD_UB3_UH(src3_r, src4_r, src5_r, add3, add4, add5);
        HADD_UB3_UH(src6_r, src7_r, src8_r, add6, add7, add8);
        ADD4(add0, add1, add1, add2, add2, add3, add3, add4, sum0_r, sum1_r,
             sum2_r, sum3_r);
        ADD4(add4, add5, add5, add6, add6, add7, add7, add8, sum4_r, sum5_r,
             sum6_r, sum7_r);
        HADD_UB3_UH(src0_l, src1_l, src2_l, add0, add1, add2);
        HADD_UB3_UH(src3_l, src4_l, src5_l, add3, add4, add5);
        HADD_UB3_UH(src6_l, src7_l, src8_l, add6, add7, add8);
        ADD4(add0, add1, add1, add2, add2, add3, add3, add4, sum0_l, sum1_l,
             sum2_l, sum3_l);
        ADD4(add4, add5, add5, add6, add6, add7, add7, add8, sum4_l, sum5_l,
             sum6_l, sum7_l);
        SRARI_H4_UH(sum0_r, sum1_r, sum2_r, sum3_r, 2);
        SRARI_H4_UH(sum4_r, sum5_r, sum6_r, sum7_r, 2);
        SRARI_H4_UH(sum0_l, sum1_l, sum2_l, sum3_l, 2);
        SRARI_H4_UH(sum4_l, sum5_l, sum6_l, sum7_l, 2);
        LD_UB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
        PCKEV_AVG_ST_UB(sum0_l, sum0_r, dst0, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum1_l, sum1_r, dst1, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum2_l, sum2_r, dst2, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum3_l, sum3_r, dst3, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum4_l, sum4_r, dst4, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum5_l, sum5_r, dst5, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum6_l, sum6_r, dst6, dst);
        dst += dst_stride;
        PCKEV_AVG_ST_UB(sum7_l, sum7_r, dst7, dst);
        dst += dst_stride;
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

void ff_put_pixels16_msa(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h)
{
    copy_width16_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels16_x2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_hz_bil_16w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels16_y2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_vt_bil_16w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels16_xy2_msa(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h)
{
    common_hv_bil_16w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels8_msa(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h)
{
    copy_width8_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels8_x2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_hz_bil_8w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels8_y2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_vt_bil_8w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels8_xy2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_hv_bil_8w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels4_x2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_hz_bil_4w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels4_y2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_vt_bil_4w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_pixels4_xy2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_hv_bil_4w_msa(pixels, line_size, block, line_size, h);
}

void ff_put_no_rnd_pixels16_x2_msa(uint8_t *block, const uint8_t *pixels,
                                   ptrdiff_t line_size, int h)
{
    if (h == 16) {
        common_hz_bil_no_rnd_16x16_msa(pixels, line_size, block, line_size);
    } else if (h == 8) {
        common_hz_bil_no_rnd_8x16_msa(pixels, line_size, block, line_size);
    }
}

void ff_put_no_rnd_pixels16_y2_msa(uint8_t *block, const uint8_t *pixels,
                                   ptrdiff_t line_size, int h)
{
    if (h == 16) {
        common_vt_bil_no_rnd_16x16_msa(pixels, line_size, block, line_size);
    } else if (h == 8) {
        common_vt_bil_no_rnd_8x16_msa(pixels, line_size, block, line_size);
    }
}

void ff_put_no_rnd_pixels16_xy2_msa(uint8_t *block,
                                    const uint8_t *pixels,
                                    ptrdiff_t line_size, int h)
{
    if (h == 16) {
        common_hv_bil_no_rnd_16x16_msa(pixels, line_size, block, line_size);
    } else if (h == 8) {
        common_hv_bil_no_rnd_8x16_msa(pixels, line_size, block, line_size);
    }
}

void ff_put_no_rnd_pixels8_x2_msa(uint8_t *block, const uint8_t *pixels,
                                  ptrdiff_t line_size, int h)
{
    if (h == 8) {
        common_hz_bil_no_rnd_8x8_msa(pixels, line_size, block, line_size);
    } else if (h == 4) {
        common_hz_bil_no_rnd_4x8_msa(pixels, line_size, block, line_size);
    }
}

void ff_put_no_rnd_pixels8_y2_msa(uint8_t *block, const uint8_t *pixels,
                                  ptrdiff_t line_size, int h)
{
    if (h == 8) {
        common_vt_bil_no_rnd_8x8_msa(pixels, line_size, block, line_size);
    } else if (h == 4) {
        common_vt_bil_no_rnd_4x8_msa(pixels, line_size, block, line_size);
    }
}

void ff_put_no_rnd_pixels8_xy2_msa(uint8_t *block, const uint8_t *pixels,
                                   ptrdiff_t line_size, int h)
{
    if (h == 8) {
        common_hv_bil_no_rnd_8x8_msa(pixels, line_size, block, line_size);
    } else if (h == 4) {
        common_hv_bil_no_rnd_4x8_msa(pixels, line_size, block, line_size);
    }
}

void ff_avg_pixels16_msa(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h)
{
    avg_width16_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels16_x2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_hz_bil_and_aver_dst_16w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels16_y2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_vt_bil_and_aver_dst_16w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels16_xy2_msa(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h)
{
    common_hv_bil_and_aver_dst_16w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels8_msa(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h)
{
    avg_width8_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels8_x2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_hz_bil_and_aver_dst_8w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels8_y2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_vt_bil_and_aver_dst_8w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels8_xy2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_hv_bil_and_aver_dst_8w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels4_msa(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h)
{
    avg_width4_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels4_x2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_hz_bil_and_aver_dst_4w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels4_y2_msa(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    common_vt_bil_and_aver_dst_4w_msa(pixels, line_size, block, line_size, h);
}

void ff_avg_pixels4_xy2_msa(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    common_hv_bil_and_aver_dst_4w_msa(pixels, line_size, block, line_size, h);
}
