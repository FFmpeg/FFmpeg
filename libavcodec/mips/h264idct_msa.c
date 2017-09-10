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
#include "h264dsp_mips.h"
#include "libavcodec/bit_depth_template.c"

#define AVC_ITRANS_H(in0, in1, in2, in3, out0, out1, out2, out3)          \
{                                                                         \
    v8i16 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                 \
                                                                          \
    tmp0_m = in0 + in2;                                                   \
    tmp1_m = in0 - in2;                                                   \
    tmp2_m = in1 >> 1;                                                    \
    tmp2_m = tmp2_m - in3;                                                \
    tmp3_m = in3 >> 1;                                                    \
    tmp3_m = in1 + tmp3_m;                                                \
                                                                          \
    BUTTERFLY_4(tmp0_m, tmp1_m, tmp2_m, tmp3_m, out0, out1, out2, out3);  \
}

static void avc_deq_idct_luma_dc_msa(int16_t *dst, int16_t *src,
                                     int32_t de_q_val)
{
#define DC_DEST_STRIDE 16
    int16_t out0, out1, out2, out3, out4, out5, out6, out7;
    v8i16 src1, src3;
    v8i16 vec0, vec1, vec2, vec3;
    v8i16 tmp0, tmp1, tmp2, tmp3;
    v8i16 hres0, hres1, hres2, hres3;
    v8i16 vres0, vres1, vres2, vres3;
    v4i32 vres0_r, vres1_r, vres2_r, vres3_r;
    const v4i32 de_q_vec = __msa_fill_w(de_q_val);
    const v8i16 src0 = LD_SH(src);
    const v8i16 src2 = LD_SH(src + 8);

    ILVL_D2_SH(src0, src0, src2, src2, src1, src3);
    TRANSPOSE4x4_SH_SH(src0, src1, src2, src3, tmp0, tmp1, tmp2, tmp3);
    BUTTERFLY_4(tmp0, tmp2, tmp3, tmp1, vec0, vec3, vec2, vec1);
    BUTTERFLY_4(vec0, vec1, vec2, vec3, hres0, hres3, hres2, hres1);
    TRANSPOSE4x4_SH_SH(hres0, hres1, hres2, hres3, hres0, hres1, hres2, hres3);
    BUTTERFLY_4(hres0, hres1, hres3, hres2, vec0, vec3, vec2, vec1);
    BUTTERFLY_4(vec0, vec1, vec2, vec3, vres0, vres1, vres2, vres3);
    UNPCK_R_SH_SW(vres0, vres0_r);
    UNPCK_R_SH_SW(vres1, vres1_r);
    UNPCK_R_SH_SW(vres2, vres2_r);
    UNPCK_R_SH_SW(vres3, vres3_r);

    vres0_r *= de_q_vec;
    vres1_r *= de_q_vec;
    vres2_r *= de_q_vec;
    vres3_r *= de_q_vec;

    SRARI_W4_SW(vres0_r, vres1_r, vres2_r, vres3_r, 8);
    PCKEV_H2_SH(vres1_r, vres0_r, vres3_r, vres2_r, vec0, vec1);

    out0 = __msa_copy_s_h(vec0, 0);
    out1 = __msa_copy_s_h(vec0, 1);
    out2 = __msa_copy_s_h(vec0, 2);
    out3 = __msa_copy_s_h(vec0, 3);
    out4 = __msa_copy_s_h(vec0, 4);
    out5 = __msa_copy_s_h(vec0, 5);
    out6 = __msa_copy_s_h(vec0, 6);
    out7 = __msa_copy_s_h(vec0, 7);
    SH(out0, (dst + 0  * DC_DEST_STRIDE));
    SH(out1, (dst + 2  * DC_DEST_STRIDE));
    SH(out2, (dst + 8  * DC_DEST_STRIDE));
    SH(out3, (dst + 10 * DC_DEST_STRIDE));
    SH(out4, (dst + 1  * DC_DEST_STRIDE));
    SH(out5, (dst + 3  * DC_DEST_STRIDE));
    SH(out6, (dst + 9  * DC_DEST_STRIDE));
    SH(out7, (dst + 11 * DC_DEST_STRIDE));

    out0 = __msa_copy_s_h(vec1, 0);
    out1 = __msa_copy_s_h(vec1, 1);
    out2 = __msa_copy_s_h(vec1, 2);
    out3 = __msa_copy_s_h(vec1, 3);
    out4 = __msa_copy_s_h(vec1, 4);
    out5 = __msa_copy_s_h(vec1, 5);
    out6 = __msa_copy_s_h(vec1, 6);
    out7 = __msa_copy_s_h(vec1, 7);
    SH(out0, (dst + 4  * DC_DEST_STRIDE));
    SH(out1, (dst + 6  * DC_DEST_STRIDE));
    SH(out2, (dst + 12 * DC_DEST_STRIDE));
    SH(out3, (dst + 14 * DC_DEST_STRIDE));
    SH(out4, (dst + 5  * DC_DEST_STRIDE));
    SH(out5, (dst + 7  * DC_DEST_STRIDE));
    SH(out6, (dst + 13 * DC_DEST_STRIDE));
    SH(out7, (dst + 15 * DC_DEST_STRIDE));

#undef DC_DEST_STRIDE
}

static void avc_idct8_addblk_msa(uint8_t *dst, int16_t *src, int32_t dst_stride)
{
    v8i16 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 vec0, vec1, vec2, vec3;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v8i16 res0, res1, res2, res3, res4, res5, res6, res7;
    v4i32 tmp0_r, tmp1_r, tmp2_r, tmp3_r, tmp4_r, tmp5_r, tmp6_r, tmp7_r;
    v4i32 tmp0_l, tmp1_l, tmp2_l, tmp3_l, tmp4_l, tmp5_l, tmp6_l, tmp7_l;
    v4i32 vec0_r, vec1_r, vec2_r, vec3_r, vec0_l, vec1_l, vec2_l, vec3_l;
    v4i32 res0_r, res1_r, res2_r, res3_r, res4_r, res5_r, res6_r, res7_r;
    v4i32 res0_l, res1_l, res2_l, res3_l, res4_l, res5_l, res6_l, res7_l;
    v16i8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 zeros = { 0 };

    src[0] += 32;

    LD_SH8(src, 8, src0, src1, src2, src3, src4, src5, src6, src7);
    ST_SH8(zeros, zeros, zeros, zeros, zeros, zeros, zeros, zeros, src, 8);

    vec0 = src0 + src4;
    vec1 = src0 - src4;
    vec2 = src2 >> 1;
    vec2 = vec2 - src6;
    vec3 = src6 >> 1;
    vec3 = src2 + vec3;

    BUTTERFLY_4(vec0, vec1, vec2, vec3, tmp0, tmp1, tmp2, tmp3);

    vec0 = src7 >> 1;
    vec0 = src5 - vec0 - src3 - src7;
    vec1 = src3 >> 1;
    vec1 = src1 - vec1 + src7 - src3;
    vec2 = src5 >> 1;
    vec2 = vec2 - src1 + src7 + src5;
    vec3 = src1 >> 1;
    vec3 = vec3 + src3 + src5 + src1;
    tmp4 = vec3 >> 2;
    tmp4 += vec0;
    tmp5 = vec2 >> 2;
    tmp5 += vec1;
    tmp6 = vec1 >> 2;
    tmp6 -= vec2;
    tmp7 = vec0 >> 2;
    tmp7 = vec3 - tmp7;

    BUTTERFLY_8(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7,
                res0, res1, res2, res3, res4, res5, res6, res7);
    TRANSPOSE8x8_SH_SH(res0, res1, res2, res3, res4, res5, res6, res7,
                       res0, res1, res2, res3, res4, res5, res6, res7);
    UNPCK_SH_SW(res0, tmp0_r, tmp0_l);
    UNPCK_SH_SW(res1, tmp1_r, tmp1_l);
    UNPCK_SH_SW(res2, tmp2_r, tmp2_l);
    UNPCK_SH_SW(res3, tmp3_r, tmp3_l);
    UNPCK_SH_SW(res4, tmp4_r, tmp4_l);
    UNPCK_SH_SW(res5, tmp5_r, tmp5_l);
    UNPCK_SH_SW(res6, tmp6_r, tmp6_l);
    UNPCK_SH_SW(res7, tmp7_r, tmp7_l);
    BUTTERFLY_4(tmp0_r, tmp0_l, tmp4_l, tmp4_r, vec0_r, vec0_l, vec1_l, vec1_r);

    vec2_r = tmp2_r >> 1;
    vec2_l = tmp2_l >> 1;
    vec2_r -= tmp6_r;
    vec2_l -= tmp6_l;
    vec3_r = tmp6_r >> 1;
    vec3_l = tmp6_l >> 1;
    vec3_r += tmp2_r;
    vec3_l += tmp2_l;

    BUTTERFLY_4(vec0_r, vec1_r, vec2_r, vec3_r, tmp0_r, tmp2_r, tmp4_r, tmp6_r);
    BUTTERFLY_4(vec0_l, vec1_l, vec2_l, vec3_l, tmp0_l, tmp2_l, tmp4_l, tmp6_l);

    vec0_r = tmp7_r >> 1;
    vec0_l = tmp7_l >> 1;
    vec0_r = tmp5_r - vec0_r - tmp3_r - tmp7_r;
    vec0_l = tmp5_l - vec0_l - tmp3_l - tmp7_l;
    vec1_r = tmp3_r >> 1;
    vec1_l = tmp3_l >> 1;
    vec1_r = tmp1_r - vec1_r + tmp7_r - tmp3_r;
    vec1_l = tmp1_l - vec1_l + tmp7_l - tmp3_l;
    vec2_r = tmp5_r >> 1;
    vec2_l = tmp5_l >> 1;
    vec2_r = vec2_r - tmp1_r + tmp7_r + tmp5_r;
    vec2_l = vec2_l - tmp1_l + tmp7_l + tmp5_l;
    vec3_r = tmp1_r >> 1;
    vec3_l = tmp1_l >> 1;
    vec3_r = vec3_r + tmp3_r + tmp5_r + tmp1_r;
    vec3_l = vec3_l + tmp3_l + tmp5_l + tmp1_l;
    tmp1_r = vec3_r >> 2;
    tmp1_l = vec3_l >> 2;
    tmp1_r += vec0_r;
    tmp1_l += vec0_l;
    tmp3_r = vec2_r >> 2;
    tmp3_l = vec2_l >> 2;
    tmp3_r += vec1_r;
    tmp3_l += vec1_l;
    tmp5_r = vec1_r >> 2;
    tmp5_l = vec1_l >> 2;
    tmp5_r -= vec2_r;
    tmp5_l -= vec2_l;
    tmp7_r = vec0_r >> 2;
    tmp7_l = vec0_l >> 2;
    tmp7_r = vec3_r - tmp7_r;
    tmp7_l = vec3_l - tmp7_l;

    BUTTERFLY_4(tmp0_r, tmp0_l, tmp7_l, tmp7_r, res0_r, res0_l, res7_l, res7_r);
    BUTTERFLY_4(tmp2_r, tmp2_l, tmp5_l, tmp5_r, res1_r, res1_l, res6_l, res6_r);
    BUTTERFLY_4(tmp4_r, tmp4_l, tmp3_l, tmp3_r, res2_r, res2_l, res5_l, res5_r);
    BUTTERFLY_4(tmp6_r, tmp6_l, tmp1_l, tmp1_r, res3_r, res3_l, res4_l, res4_r);
    SRA_4V(res0_r, res0_l, res1_r, res1_l, 6);
    SRA_4V(res2_r, res2_l, res3_r, res3_l, 6);
    SRA_4V(res4_r, res4_l, res5_r, res5_l, 6);
    SRA_4V(res6_r, res6_l, res7_r, res7_l, 6);
    PCKEV_H4_SH(res0_l, res0_r, res1_l, res1_r, res2_l, res2_r, res3_l, res3_r,
                res0, res1, res2, res3);
    PCKEV_H4_SH(res4_l, res4_r, res5_l, res5_r, res6_l, res6_r, res7_l, res7_r,
                res4, res5, res6, res7);
    LD_SB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_B4_SH(zeros, dst0, zeros, dst1, zeros, dst2, zeros, dst3,
               tmp0, tmp1, tmp2, tmp3);
    ILVR_B4_SH(zeros, dst4, zeros, dst5, zeros, dst6, zeros, dst7,
               tmp4, tmp5, tmp6, tmp7);
    ADD4(res0, tmp0, res1, tmp1, res2, tmp2, res3, tmp3,
         res0, res1, res2, res3);
    ADD4(res4, tmp4, res5, tmp5, res6, tmp6, res7, tmp7,
         res4, res5, res6, res7);
    CLIP_SH4_0_255(res0, res1, res2, res3);
    CLIP_SH4_0_255(res4, res5, res6, res7);
    PCKEV_B4_SB(res1, res0, res3, res2, res5, res4, res7, res6,
                dst0, dst1, dst2, dst3);
    ST8x4_UB(dst0, dst1, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x4_UB(dst2, dst3, dst, dst_stride);
}

static void avc_idct8_dc_addblk_msa(uint8_t *dst, int16_t *src,
                                    int32_t dst_stride)
{
    int32_t dc_val;
    v16i8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r, dst4_r, dst5_r, dst6_r, dst7_r;
    v8i16 dc;
    v16i8 zeros = { 0 };

    dc_val = (src[0] + 32) >> 6;
    dc = __msa_fill_h(dc_val);

    src[0] = 0;

    LD_SB8(dst, dst_stride, dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7);
    ILVR_B4_SH(zeros, dst0, zeros, dst1, zeros, dst2, zeros, dst3,
               dst0_r, dst1_r, dst2_r, dst3_r);
    ILVR_B4_SH(zeros, dst4, zeros, dst5, zeros, dst6, zeros, dst7,
               dst4_r, dst5_r, dst6_r, dst7_r);
    ADD4(dst0_r, dc, dst1_r, dc, dst2_r, dc, dst3_r, dc,
         dst0_r, dst1_r, dst2_r, dst3_r);
    ADD4(dst4_r, dc, dst5_r, dc, dst6_r, dc, dst7_r, dc,
         dst4_r, dst5_r, dst6_r, dst7_r);
    CLIP_SH4_0_255(dst0_r, dst1_r, dst2_r, dst3_r);
    CLIP_SH4_0_255(dst4_r, dst5_r, dst6_r, dst7_r);
    PCKEV_B4_SB(dst1_r, dst0_r, dst3_r, dst2_r, dst5_r, dst4_r, dst7_r, dst6_r,
                dst0, dst1, dst2, dst3);
    ST8x4_UB(dst0, dst1, dst, dst_stride);
    dst += (4 * dst_stride);
    ST8x4_UB(dst2, dst3, dst, dst_stride);
}

void ff_h264_idct_add_msa(uint8_t *dst, int16_t *src, int32_t dst_stride)
{
    uint32_t src0_m, src1_m, src2_m, src3_m, out0_m, out1_m, out2_m, out3_m;
    v16i8 dst0_m = { 0 };
    v16i8 dst1_m = { 0 };
    v8i16 hres0, hres1, hres2, hres3, vres0, vres1, vres2, vres3;
    v8i16 inp0_m, inp1_m, res0_m, res1_m, src1, src3;
    const v8i16 src0 = LD_SH(src);
    const v8i16 src2 = LD_SH(src + 8);
    const v8i16 zero = { 0 };
    const uint8_t *dst1 = dst + dst_stride;
    const uint8_t *dst2 = dst + 2 * dst_stride;
    const uint8_t *dst3 = dst + 3 * dst_stride;

    ILVL_D2_SH(src0, src0, src2, src2, src1, src3);
    ST_SH2(zero, zero, src, 8);
    AVC_ITRANS_H(src0, src1, src2, src3, hres0, hres1, hres2, hres3);
    TRANSPOSE4x4_SH_SH(hres0, hres1, hres2, hres3, hres0, hres1, hres2, hres3);
    AVC_ITRANS_H(hres0, hres1, hres2, hres3, vres0, vres1, vres2, vres3);
    src0_m = LW(dst);
    src1_m = LW(dst1);
    SRARI_H4_SH(vres0, vres1, vres2, vres3, 6);
    src2_m = LW(dst2);
    src3_m = LW(dst3);
    ILVR_D2_SH(vres1, vres0, vres3, vres2, inp0_m, inp1_m);
    INSERT_W2_SB(src0_m, src1_m, dst0_m);
    INSERT_W2_SB(src2_m, src3_m, dst1_m);
    ILVR_B2_SH(zero, dst0_m, zero, dst1_m, res0_m, res1_m);
    ADD2(res0_m, inp0_m, res1_m, inp1_m, res0_m, res1_m);
    CLIP_SH2_0_255(res0_m, res1_m);
    PCKEV_B2_SB(res0_m, res0_m, res1_m, res1_m, dst0_m, dst1_m);
    out0_m = __msa_copy_u_w((v4i32) dst0_m, 0);
    out1_m = __msa_copy_u_w((v4i32) dst0_m, 1);
    out2_m = __msa_copy_u_w((v4i32) dst1_m, 0);
    out3_m = __msa_copy_u_w((v4i32) dst1_m, 1);
    SW(out0_m, dst);
    SW(out1_m, dst1);
    SW(out2_m, dst2);
    SW(out3_m, dst3);
}

void ff_h264_idct8_addblk_msa(uint8_t *dst, int16_t *src,
                              int32_t dst_stride)
{
    avc_idct8_addblk_msa(dst, src, dst_stride);
}

void ff_h264_idct4x4_addblk_dc_msa(uint8_t *dst, int16_t *src,
                                   int32_t dst_stride)
{
    v16u8 pred = { 0 };
    v16i8 out;
    v8i16 pred_r, pred_l;
    const uint32_t src0 = LW(dst);
    const uint32_t src1 = LW(dst + dst_stride);
    const uint32_t src2 = LW(dst + 2 * dst_stride);
    const uint32_t src3 = LW(dst + 3 * dst_stride);
    const int16_t dc = (src[0] + 32) >> 6;
    const v8i16 input_dc = __msa_fill_h(dc);

    src[0] = 0;
    INSERT_W4_UB(src0, src1, src2, src3, pred);
    UNPCK_UB_SH(pred, pred_r, pred_l);
    ADD2(pred_r, input_dc, pred_l, input_dc, pred_r, pred_l);
    CLIP_SH2_0_255(pred_r, pred_l);
    out = __msa_pckev_b((v16i8) pred_l, (v16i8) pred_r);
    ST4x4_UB(out, out, 0, 1, 2, 3, dst, dst_stride);
}

void ff_h264_idct8_dc_addblk_msa(uint8_t *dst, int16_t *src,
                                 int32_t dst_stride)
{
    avc_idct8_dc_addblk_msa(dst, src, dst_stride);
}

void ff_h264_idct_add16_msa(uint8_t *dst,
                            const int32_t *blk_offset,
                            int16_t *block, int32_t dst_stride,
                            const uint8_t nzc[15 * 8])
{
    int32_t i;

    for (i = 0; i < 16; i++) {
        int32_t nnz = nzc[scan8[i]];

        if (nnz) {
            if (nnz == 1 && ((dctcoef *) block)[i * 16])
                ff_h264_idct4x4_addblk_dc_msa(dst + blk_offset[i],
                                              block + i * 16 * sizeof(pixel),
                                              dst_stride);
            else
                ff_h264_idct_add_msa(dst + blk_offset[i],
                                     block + i * 16 * sizeof(pixel),
                                     dst_stride);
        }
    }
}

void ff_h264_idct8_add4_msa(uint8_t *dst, const int32_t *blk_offset,
                            int16_t *block, int32_t dst_stride,
                            const uint8_t nzc[15 * 8])
{
    int32_t cnt;

    for (cnt = 0; cnt < 16; cnt += 4) {
        int32_t nnz = nzc[scan8[cnt]];

        if (nnz) {
            if (nnz == 1 && ((dctcoef *) block)[cnt * 16])
                ff_h264_idct8_dc_addblk_msa(dst + blk_offset[cnt],
                                            block + cnt * 16 * sizeof(pixel),
                                            dst_stride);
            else
                ff_h264_idct8_addblk_msa(dst + blk_offset[cnt],
                                         block + cnt * 16 * sizeof(pixel),
                                         dst_stride);
        }
    }
}

void ff_h264_idct_add8_msa(uint8_t **dst,
                           const int32_t *blk_offset,
                           int16_t *block, int32_t dst_stride,
                           const uint8_t nzc[15 * 8])
{
    int32_t i, j;

    for (j = 1; j < 3; j++) {
        for (i = (j * 16); i < (j * 16 + 4); i++) {
            if (nzc[scan8[i]])
                ff_h264_idct_add_msa(dst[j - 1] + blk_offset[i],
                                     block + i * 16 * sizeof(pixel),
                                     dst_stride);
            else if (((dctcoef *) block)[i * 16])
                ff_h264_idct4x4_addblk_dc_msa(dst[j - 1] + blk_offset[i],
                                              block + i * 16 * sizeof(pixel),
                                              dst_stride);
        }
    }
}

void ff_h264_idct_add8_422_msa(uint8_t **dst,
                               const int32_t *blk_offset,
                               int16_t *block, int32_t dst_stride,
                               const uint8_t nzc[15 * 8])
{
    int32_t i, j;

    for (j = 1; j < 3; j++) {
        for (i = (j * 16); i < (j * 16 + 4); i++) {
            if (nzc[scan8[i]])
                ff_h264_idct_add_msa(dst[j - 1] + blk_offset[i],
                                     block + i * 16 * sizeof(pixel),
                                     dst_stride);
            else if (((dctcoef *) block)[i * 16])
                ff_h264_idct4x4_addblk_dc_msa(dst[j - 1] + blk_offset[i],
                                              block + i * 16 * sizeof(pixel),
                                              dst_stride);
        }
    }

    for (j = 1; j < 3; j++) {
        for (i = (j * 16 + 4); i < (j * 16 + 8); i++) {
            if (nzc[scan8[i + 4]])
                ff_h264_idct_add_msa(dst[j - 1] + blk_offset[i + 4],
                                     block + i * 16 * sizeof(pixel),
                                     dst_stride);
            else if (((dctcoef *) block)[i * 16])
                ff_h264_idct4x4_addblk_dc_msa(dst[j - 1] + blk_offset[i + 4],
                                              block + i * 16 * sizeof(pixel),
                                              dst_stride);
        }
    }
}

void ff_h264_idct_add16_intra_msa(uint8_t *dst,
                                  const int32_t *blk_offset,
                                  int16_t *block,
                                  int32_t dst_stride,
                                  const uint8_t nzc[15 * 8])
{
    int32_t i;

    for (i = 0; i < 16; i++) {
        if (nzc[scan8[i]])
            ff_h264_idct_add_msa(dst + blk_offset[i],
                                 block + i * 16 * sizeof(pixel), dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct4x4_addblk_dc_msa(dst + blk_offset[i],
                                          block + i * 16 * sizeof(pixel),
                                          dst_stride);
    }
}

void ff_h264_deq_idct_luma_dc_msa(int16_t *dst, int16_t *src,
                                  int32_t de_qval)
{
    avc_deq_idct_luma_dc_msa(dst, src, de_qval);
}
