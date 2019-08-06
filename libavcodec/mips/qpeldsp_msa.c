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
#include "qpeldsp_mips.h"

#define APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask, coef0, coef1, coef2)  \
( {                                                                     \
    v16u8 out, tmp0, tmp1;                                              \
    v16u8 data0, data1, data2, data3, data4, data5;                     \
    v8i16 res_r, res_l;                                                 \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                               \
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l;                               \
                                                                        \
    VSHF_B2_UB(inp0, inp0, inp1, inp1, mask, mask, tmp0, tmp1);         \
    ILVRL_B2_UH(inp1, inp0, sum0_r, sum0_l);                            \
    data0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) tmp0, 15);       \
    data3 = (v16u8) __msa_sldi_b((v16i8) tmp1, (v16i8) inp1, 1);        \
    HADD_UB2_UH(sum0_r, sum0_l, sum0_r, sum0_l);                        \
    ILVRL_B2_UH(data3, data0, sum1_r, sum1_l);                          \
    data1 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) tmp0, 14);       \
    data4 = (v16u8) __msa_sldi_b((v16i8) tmp1, (v16i8) inp1, 2);        \
    sum0_r *= (v8u16) (coef0);                                          \
    sum0_l *= (v8u16) (coef0);                                          \
    ILVRL_B2_UH(data4, data1, sum2_r, sum2_l);                          \
    data2 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) tmp0, 13);       \
    data5 = (v16u8) __msa_sldi_b((v16i8) tmp1, (v16i8) inp1, 3);        \
    DPADD_UB2_UH(sum2_r, sum2_l, coef2, coef2, sum0_r, sum0_l);         \
    ILVRL_B2_UH(data5, data2, sum3_r, sum3_l);                          \
    HADD_UB2_UH(sum3_r, sum3_l, sum3_r, sum3_l);                        \
    DPADD_UB2_UH(sum1_r, sum1_l, coef1, coef1, sum3_r, sum3_l);         \
    res_r = (v8i16) (sum0_r - sum3_r);                                  \
    res_l = (v8i16) (sum0_l - sum3_l);                                  \
    SRARI_H2_SH(res_r, res_l, 5);                                       \
    CLIP_SH2_0_255(res_r, res_l);                                       \
    out = (v16u8) __msa_pckev_b((v16i8) res_l, (v16i8) res_r);          \
                                                                        \
    out;                                                                \
} )

#define APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,                       \
                                      mask0, mask1, mask2, mask3,       \
                                      coef0, coef1, coef2)              \
( {                                                                     \
    v16u8 out;                                                          \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                               \
    v8u16 sum4_r, sum5_r, sum6_r, sum7_r;                               \
    v8i16 res0_r, res1_r;                                               \
                                                                        \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask0, mask0, sum0_r, sum4_r);   \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask3, mask3, sum3_r, sum7_r);   \
    HADD_UB2_UH(sum3_r, sum7_r, sum3_r, sum7_r);                        \
    DOTP_UB2_UH(sum0_r, sum4_r, coef0, coef0, sum0_r, sum4_r);          \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask2, mask2, sum2_r, sum6_r);   \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask1, mask1, sum1_r, sum5_r);   \
    DPADD_UB2_UH(sum2_r, sum6_r, coef2, coef2, sum0_r, sum4_r);         \
    DPADD_UB2_UH(sum1_r, sum5_r, coef1, coef1, sum3_r, sum7_r);         \
    res0_r = (v8i16) (sum0_r - sum3_r);                                 \
    res1_r = (v8i16) (sum4_r - sum7_r);                                 \
    SRARI_H2_SH(res0_r, res1_r, 5);                                     \
    CLIP_SH2_0_255(res0_r, res1_r);                                     \
    out = (v16u8) __msa_pckev_b((v16i8) res1_r, (v16i8) res0_r);        \
                                                                        \
    out;                                                                \
} )

#define APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,                        \
                                           mask0, mask1, mask2, mask3,  \
                                           coef0, coef1, coef2)         \
( {                                                                     \
    v16u8 out;                                                          \
    v8i16 res0_r;                                                       \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                               \
                                                                        \
    VSHF_B2_UH(inp0, inp0, inp0, inp0, mask0, mask3, sum0_r, sum3_r);   \
    sum3_r = __msa_hadd_u_h((v16u8) sum3_r, (v16u8) sum3_r);            \
    sum0_r = __msa_dotp_u_h((v16u8) sum0_r, (v16u8) coef0);             \
    VSHF_B2_UH(inp0, inp0, inp0, inp0, mask2, mask1, sum2_r, sum1_r);   \
    DPADD_UB2_UH(sum2_r, sum1_r, coef2, coef1, sum0_r, sum3_r);         \
    res0_r = (v8i16) (sum0_r - sum3_r);                                 \
    res0_r = __msa_srari_h(res0_r, 5);                                  \
    CLIP_SH_0_255(res0_r);                                              \
    out = (v16u8) __msa_pckev_b((v16i8) res0_r, (v16i8) res0_r);        \
                                                                        \
    out;                                                                \
} )

#define APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,   \
                                                    mask2, mask3, coef0,  \
                                                    coef1, coef2)         \
( {                                                                       \
    v16u8 out;                                                            \
    v8i16 res0_r;                                                         \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                                 \
                                                                          \
    VSHF_B2_UH(inp0, inp0, inp0, inp0, mask0, mask3, sum0_r, sum3_r);     \
    sum3_r = __msa_hadd_u_h((v16u8) sum3_r, (v16u8) sum3_r);              \
    sum0_r = __msa_dotp_u_h((v16u8) sum0_r, (v16u8) coef0);               \
    VSHF_B2_UH(inp0, inp0, inp0, inp0, mask2, mask1, sum2_r, sum1_r);     \
    DPADD_UB2_UH(sum2_r, sum1_r, coef2, coef1, sum0_r, sum3_r);           \
    res0_r = (v8i16) (sum0_r - sum3_r);                                   \
    res0_r += 15;                                                         \
    res0_r >>= 5;                                                         \
    CLIP_SH_0_255(res0_r);                                                \
    out = (v16u8) __msa_pckev_b((v16i8) res0_r, (v16i8) res0_r);          \
                                                                          \
    out;                                                                  \
} )

#define APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,              \
                                         coef0, coef1, coef2)           \
( {                                                                     \
    v16u8 out, tmp0, tmp1;                                              \
    v16u8 data0, data1, data2, data3, data4, data5;                     \
    v8i16 res_r, res_l;                                                 \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                               \
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l;                               \
                                                                        \
    VSHF_B2_UB(inp0, inp0, inp1, inp1, mask, mask, tmp0, tmp1);         \
    ILVRL_B2_UH(inp1, inp0, sum0_r, sum0_l);                            \
    data0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) tmp0, 15);       \
    data3 = (v16u8) __msa_sldi_b((v16i8) tmp1, (v16i8) inp1, 1);        \
    HADD_UB2_UH(sum0_r, sum0_l, sum0_r, sum0_l);                        \
    ILVRL_B2_UH(data3, data0, sum1_r, sum1_l);                          \
    data1 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) tmp0, 14);       \
    data4 = (v16u8) __msa_sldi_b((v16i8) tmp1, (v16i8) inp1, 2);        \
    sum0_r *= (v8u16) (coef0);                                          \
    sum0_l *= (v8u16) (coef0);                                          \
    ILVRL_B2_UH(data4, data1, sum2_r, sum2_l);                          \
    data2 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) tmp0, 13);       \
    data5 = (v16u8) __msa_sldi_b((v16i8) tmp1, (v16i8) inp1, 3);        \
    DPADD_UB2_UH(sum2_r, sum2_l, coef2, coef2, sum0_r, sum0_l);         \
    ILVRL_B2_UH(data5, data2, sum3_r, sum3_l);                          \
    HADD_UB2_UH(sum3_r, sum3_l, sum3_r, sum3_l);                        \
    DPADD_UB2_UH(sum1_r, sum1_l, coef1, coef1, sum3_r, sum3_l);         \
    res_r = (v8i16) (sum0_r - sum3_r);                                  \
    res_l = (v8i16) (sum0_l - sum3_l);                                  \
    res_r += 15;                                                        \
    res_l += 15;                                                        \
    res_r >>= 5;                                                        \
    res_l >>= 5;                                                        \
    CLIP_SH2_0_255(res_r, res_l);                                       \
    out = (v16u8) __msa_pckev_b((v16i8) res_l, (v16i8) res_r);          \
                                                                        \
    out;                                                                \
} )

#define APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1,                  \
                                               mask0, mask1, mask2, mask3,  \
                                               coef0, coef1, coef2)         \
( {                                                                         \
    v16u8 out;                                                              \
    v8i16 res0_r, res1_r;                                                   \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                                   \
    v8u16 sum4_r, sum5_r, sum6_r, sum7_r;                                   \
                                                                            \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask0, mask0, sum0_r, sum4_r);       \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask3, mask3, sum3_r, sum7_r);       \
    HADD_UB2_UH(sum3_r, sum7_r, sum3_r, sum7_r);                            \
    DOTP_UB2_UH(sum0_r, sum4_r, coef0, coef0, sum0_r, sum4_r);              \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask2, mask2, sum2_r, sum6_r);       \
    VSHF_B2_UH(inp0, inp0, inp1, inp1, mask1, mask1, sum1_r, sum5_r);       \
    DPADD_UB2_UH(sum2_r, sum6_r, coef2, coef2, sum0_r, sum4_r);             \
    DPADD_UB2_UH(sum1_r, sum5_r, coef1, coef1, sum3_r, sum7_r);             \
    res0_r = (v8i16) (sum0_r - sum3_r);                                     \
    res1_r = (v8i16) (sum4_r - sum7_r);                                     \
    res0_r += 15;                                                           \
    res1_r += 15;                                                           \
    res0_r >>= 5;                                                           \
    res1_r >>= 5;                                                           \
    CLIP_SH2_0_255(res0_r, res1_r);                                         \
    out = (v16u8) __msa_pckev_b((v16i8) res1_r, (v16i8) res0_r);            \
                                                                            \
    out;                                                                    \
} )

#define APPLY_VERT_QPEL_FILTER(inp0, inp1, inp2, inp3,                  \
                               inp4, inp5, inp6, inp7,                  \
                               coef0, coef1, coef2)                     \
( {                                                                     \
    v16u8 res;                                                          \
    v8i16 res_r, res_l;                                                 \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                               \
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l;                               \
                                                                        \
    ILVRL_B2_UH(inp4, inp0, sum0_r, sum0_l);                            \
    ILVRL_B2_UH(inp7, inp3, sum3_r, sum3_l);                            \
    DOTP_UB2_UH(sum0_r, sum0_l, coef0, coef0, sum0_r, sum0_l);          \
    HADD_UB2_UH(sum3_r, sum3_l, sum3_r, sum3_l);                        \
    ILVRL_B2_UH(inp6, inp2, sum2_r, sum2_l);                            \
    ILVRL_B2_UH(inp5, inp1, sum1_r, sum1_l);                            \
    DPADD_UB2_UH(sum2_r, sum2_l, coef2, coef2, sum0_r, sum0_l);         \
    DPADD_UB2_UH(sum1_r, sum1_l, coef1, coef1, sum3_r, sum3_l);         \
    res_r = (v8i16) (sum0_r - sum3_r);                                  \
    res_l = (v8i16) (sum0_l - sum3_l);                                  \
    SRARI_H2_SH(res_r, res_l, 5);                                       \
    CLIP_SH2_0_255(res_r, res_l);                                       \
    res = (v16u8) __msa_pckev_b((v16i8) res_l, (v16i8) res_r);          \
                                                                        \
    res;                                                                \
} )

#define APPLY_VERT_QPEL_FILTER_8BYTE(inp00, inp01, inp02, inp03,        \
                                     inp04, inp05, inp06, inp07,        \
                                     inp10, inp11, inp12, inp13,        \
                                     inp14, inp15, inp16, inp17,        \
                                     coef0, coef1, coef2)               \
( {                                                                     \
    v16u8 res;                                                          \
    v8i16 val0, val1;                                                   \
    v8u16 sum00, sum01, sum02, sum03;                                   \
    v8u16 sum10, sum11, sum12, sum13;                                   \
                                                                        \
    ILVR_B4_UH(inp04, inp00, inp14, inp10, inp07, inp03, inp17, inp13,  \
               sum00, sum10, sum03, sum13);                             \
    DOTP_UB2_UH(sum00, sum10, coef0, coef0, sum00, sum10);              \
    HADD_UB2_UH(sum03, sum13, sum03, sum13);                            \
    ILVR_B4_UH(inp06, inp02, inp16, inp12, inp05, inp01, inp15, inp11,  \
               sum02, sum12, sum01, sum11);                             \
    DPADD_UB2_UH(sum02, sum12, coef2, coef2, sum00, sum10);             \
    DPADD_UB2_UH(sum01, sum11, coef1, coef1, sum03, sum13);             \
    val0 = (v8i16) (sum00 - sum03);                                     \
    val1 = (v8i16) (sum10 - sum13);                                     \
    SRARI_H2_SH(val0, val1, 5);                                         \
    CLIP_SH2_0_255(val0, val1);                                         \
    res = (v16u8) __msa_pckev_b((v16i8) val1, (v16i8) val0);            \
                                                                        \
    res;                                                                \
} )

#define APPLY_VERT_QPEL_NO_ROUND_FILTER(inp0, inp1, inp2, inp3,         \
                                        inp4, inp5, inp6, inp7,         \
                                        coef0, coef1, coef2)            \
( {                                                                     \
    v16u8 res;                                                          \
    v8i16 res_r, res_l;                                                 \
    v8u16 sum0_r, sum1_r, sum2_r, sum3_r;                               \
    v8u16 sum0_l, sum1_l, sum2_l, sum3_l;                               \
                                                                        \
    ILVRL_B2_UH(inp4, inp0, sum0_r, sum0_l);                            \
    ILVRL_B2_UH(inp7, inp3, sum3_r, sum3_l);                            \
    DOTP_UB2_UH(sum0_r, sum0_l, coef0, coef0, sum0_r, sum0_l);          \
    HADD_UB2_UH(sum3_r, sum3_l, sum3_r, sum3_l);                        \
    ILVRL_B2_UH(inp6, inp2, sum2_r, sum2_l);                            \
    ILVRL_B2_UH(inp5, inp1, sum1_r, sum1_l);                            \
    DPADD_UB2_UH(sum2_r, sum2_l, coef2, coef2, sum0_r, sum0_l);         \
    DPADD_UB2_UH(sum1_r, sum1_l, coef1, coef1, sum3_r, sum3_l);         \
    res_r = (v8i16) (sum0_r - sum3_r);                                  \
    res_l = (v8i16) (sum0_l - sum3_l);                                  \
    res_r += 15;                                                        \
    res_l += 15;                                                        \
    res_r >>= 5;                                                        \
    res_l >>= 5;                                                        \
    CLIP_SH2_0_255(res_r, res_l);                                       \
    res = (v16u8) __msa_pckev_b((v16i8) res_l, (v16i8) res_r);          \
                                                                        \
    res;                                                                \
} )

#define APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp00, inp01, inp02, inp03,  \
                                              inp04, inp05, inp06, inp07,  \
                                              inp10, inp11, inp12, inp13,  \
                                              inp14, inp15, inp16, inp17,  \
                                              coef0, coef1, coef2)         \
( {                                                                        \
    v16u8 res;                                                             \
    v8i16 val0, val1;                                                      \
    v8u16 sum00, sum01, sum02, sum03;                                      \
    v8u16 sum10, sum11, sum12, sum13;                                      \
                                                                           \
    ILVR_B4_UH(inp04, inp00, inp14, inp10, inp07, inp03, inp17, inp13,     \
               sum00, sum10, sum03, sum13);                                \
    DOTP_UB2_UH(sum00, sum10, coef0, coef0, sum00, sum10);                 \
    HADD_UB2_UH(sum03, sum13, sum03, sum13);                               \
    ILVR_B4_UH(inp06, inp02, inp16, inp12, inp05, inp01, inp15, inp11,     \
               sum02, sum12, sum01, sum11);                                \
    DPADD_UB2_UH(sum02, sum12, coef2, coef2, sum00, sum10);                \
    DPADD_UB2_UH(sum01, sum11, coef1, coef1, sum03, sum13);                \
    val0 = (v8i16) (sum00 - sum03);                                        \
    val1 = (v8i16) (sum10 - sum13);                                        \
    val0 += 15;                                                            \
    val1 += 15;                                                            \
    val0 >>= 5;                                                            \
    val1 >>= 5;                                                            \
    CLIP_SH2_0_255(val0, val1);                                            \
    res = (v16u8) __msa_pckev_b((v16i8) val1, (v16i8) val0);               \
                                                                           \
    res;                                                                   \
} )

static void horiz_mc_qpel_aver_src0_8width_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride,
                                               int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
        inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
        AVER_UB2_UB(inp0, res0, inp2, res1, res0, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_aver_src0_16width_msa(const uint8_t *src,
                                                int32_t src_stride,
                                                uint8_t *dst,
                                                int32_t dst_stride,
                                                int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp0, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp2, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp4, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp6, res);
        ST_UB(res, dst);
        dst += dst_stride;
    }
}

static void horiz_mc_qpel_8width_msa(const uint8_t *src,
                                     int32_t src_stride,
                                     uint8_t *dst,
                                     int32_t dst_stride,
                                     int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_16width_msa(const uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride,
                                      int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v8u16 const20 = (v8u16) __msa_ldi_h(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;
    }
}

static void horiz_mc_qpel_aver_src1_8width_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride,
                                               int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        SLDI_B4_UB(inp0, inp0, inp1, inp1, inp2, inp2, inp3, inp3, 1,
                   inp0, inp1, inp2, inp3);
        inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
        inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
        AVER_UB2_UB(inp0, res0, inp2, res1, res0, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_aver_src1_16width_msa(const uint8_t *src,
                                                int32_t src_stride,
                                                uint8_t *dst,
                                                int32_t dst_stride,
                                                int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v8u16 const20 = (v8u16) __msa_ldi_h(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp1);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp5);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp7);
        ST_UB(res, dst);
        dst += dst_stride;
    }
}

static void horiz_mc_qpel_no_rnd_aver_src0_8width_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      uint8_t *dst,
                                                      int32_t dst_stride,
                                                      int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                      mask2, mask3, const20,
                                                      const6, const3);
        res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                      mask2, mask3, const20,
                                                      const6, const3);
        inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
        inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
        res0 = __msa_ave_u_b(inp0, res0);
        res1 = __msa_ave_u_b(inp2, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_no_rnd_aver_src0_16width_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride,
                                                       int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v8u16 const20 = (v8u16) __msa_ldi_h(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp0, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp2, inp3, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp2, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp4, inp5, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp4, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp6, inp7, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp6, res);
        ST_UB(res, dst);
        dst += dst_stride;
    }
}

static void horiz_mc_qpel_no_rnd_8width_msa(const uint8_t *src,
                                            int32_t src_stride,
                                            uint8_t *dst,
                                            int32_t dst_stride,
                                            int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                      mask2, mask3, const20,
                                                      const6, const3);
        res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                      mask2, mask3, const20,
                                                      const6, const3);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_no_rnd_16width_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp2, inp3, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp4, inp5, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp6, inp7, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;
    }
}

static void horiz_mc_qpel_no_rnd_aver_src1_8width_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      uint8_t *dst,
                                                      int32_t dst_stride,
                                                      int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                      mask2, mask3, const20,
                                                      const6, const3);
        res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                      mask2, mask3, const20,
                                                      const6, const3);
        SLDI_B4_UB(inp0, inp0, inp1, inp1, inp2, inp2, inp3, inp3, 1,
                   inp0, inp1, inp2, inp3);
        inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
        inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
        res0 = __msa_ave_u_b(inp0, res0);
        res1 = __msa_ave_u_b(inp2, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_no_rnd_aver_src1_16width_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride,
                                                       int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp1);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp2, inp3, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp4, inp5, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp5);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp6, inp7, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp7);
        ST_UB(res, dst);
        dst += dst_stride;
    }
}

static void horiz_mc_qpel_avg_dst_aver_src0_8width_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride,
                                                       int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
        inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
        dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
        dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
        AVER_UB2_UB(inp0, res0, inp2, res1, res0, res1);
        AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_avg_dst_aver_src0_16width_msa(const uint8_t *src,
                                                        int32_t src_stride,
                                                        uint8_t *dst,
                                                        int32_t dst_stride,
                                                        int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res0, res1;
    v16u8 dst0, dst1;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                       const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                       const20, const6, const3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        AVER_UB2_UB(inp0, res0, inp2, res1, res0, res1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);
        ST_UB2(res0, res1, dst, dst_stride);
        dst += (2 * dst_stride);

        res0 = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                       const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                       const20, const6, const3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        AVER_UB2_UB(inp4, res0, inp6, res1, res0, res1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);
        ST_UB2(res0, res1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void horiz_mc_qpel_avg_dst_8width_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride,
                                             int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
        dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
        AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_avg_dst_16width_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res0, res1;
    v16u8 dst0, dst1;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                       const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                       const20, const6, const3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);
        ST_UB2(res0, res1, dst, dst_stride);
        dst += (2 * dst_stride);

        res0 = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                       const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                       const20, const6, const3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);
        ST_UB2(res0, res1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}

static void horiz_mc_qpel_avg_dst_aver_src1_8width_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride,
                                                       int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 res0, res1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                             mask0, mask1, mask2, mask3,
                                             const20, const6, const3);
        LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
        SLDI_B4_UB(inp0, inp0, inp1, inp1, inp2, inp2, inp3, inp3, 1,
                   inp0, inp1, inp2, inp3);
        inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
        inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
        dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
        dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
        AVER_UB2_UB(inp0, res0, inp2, res1, res0, res1);
        AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
        ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

static void horiz_mc_qpel_avg_dst_aver_src1_16width_msa(const uint8_t *src,
                                                        int32_t src_stride,
                                                        uint8_t *dst,
                                                        int32_t dst_stride,
                                                        int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res0, res1, dst0, dst1;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                       const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                       const20, const6, const3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        AVER_UB2_UB(res0, inp1, res1, inp3, res0, res1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);
        ST_UB2(res0, res1, dst, dst_stride);
        dst += (2 * dst_stride);
        res0 = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                       const20, const6, const3);
        res1 = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                       const20, const6, const3);
        LD_UB2(dst, dst_stride, dst0, dst1);
        AVER_UB2_UB(res0, inp5, res1, inp7, res0, res1);
        AVER_UB2_UB(dst0, res0, dst1, res1, res0, res1);
        ST_UB2(res0, res1, dst, dst_stride);
        dst += (2 * dst_stride);
    }
}


static void vert_mc_qpel_aver_src0_8x8_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 tmp0, tmp1, res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                        inp1, inp2, inp3, inp4,
                                        inp1, inp0, inp0, inp1,
                                        inp2, inp3, inp4, inp5,
                                        const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                        inp3, inp4, inp5, inp6,
                                        inp3, inp2, inp1, inp0,
                                        inp4, inp5, inp6, inp7,
                                        const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                        inp5, inp6, inp7, inp8,
                                        inp5, inp4, inp3, inp2,
                                        inp6, inp7, inp8, inp8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                        inp7, inp8, inp8, inp7,
                                        inp7, inp6, inp5, inp4,
                                        inp8, inp8, inp7, inp6,
                                        const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp4, 1, (v2i64) inp5);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp6, 1, (v2i64) inp7);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void vert_mc_qpel_aver_src0_16x16_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp0, inp0, inp1, inp2,
                                  inp1, inp2, inp3, inp4,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp0);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp5 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp1, inp0, inp0, inp1,
                                  inp2, inp3, inp4, inp5,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp1);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp2, inp1, inp0, inp0,
                                  inp3, inp4, inp5, inp6,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp2);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp7 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp3, inp2, inp1, inp0,
                                  inp4, inp5, inp6, inp7,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp3);
    ST_UB(res0, dst);
    dst += dst_stride;

    LD_UB2(src, src_stride, inp8, inp9);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp4, inp3, inp2, inp1,
                                  inp5, inp6, inp7, inp8,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp4);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp5, inp4, inp3, inp2,
                                  inp6, inp7, inp8, inp9,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp5);
    ST_UB(res0, dst);
    dst += dst_stride;

    LD_UB2(src, src_stride, inp10, inp11);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp6, inp5, inp4, inp3,
                                  inp7, inp8, inp9, inp10,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp6);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp7, inp6, inp5, inp4,
                                  inp8, inp9, inp10, inp11,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp7);
    ST_UB(res0, dst);
    dst += dst_stride;

    LD_UB2(src, src_stride, inp12, inp13);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp8, inp7, inp6, inp5,
                                  inp9, inp10, inp11, inp12,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp8);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp9, inp8, inp7, inp6,
                                  inp10, inp11, inp12, inp13,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp9);
    ST_UB(res0, dst);
    dst += dst_stride;

    LD_UB2(src, src_stride, inp14, inp15);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp10, inp9, inp8, inp7,
                                  inp11, inp12, inp13, inp14,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp10);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp11, inp10, inp9, inp8,
                                  inp12, inp13, inp14, inp15,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp11);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER(inp12, inp11, inp10, inp9,
                                  inp13, inp14, inp15, inp16,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp12);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp13, inp12, inp11, inp10,
                                  inp14, inp15, inp16, inp16,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp13);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp14, inp13, inp12, inp11,
                                  inp15, inp16, inp16, inp15,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp14);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp15, inp14, inp13, inp12,
                                  inp16, inp16, inp15, inp14,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp15);
    ST_UB(res0, dst);
}

static void vert_mc_qpel_8x8_msa(const uint8_t *src,
                                 int32_t src_stride,
                                 uint8_t *dst,
                                 int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                        inp1, inp2, inp3, inp4,
                                        inp1, inp0, inp0, inp1,
                                        inp2, inp3, inp4, inp5,
                                        const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                        inp3, inp4, inp5, inp6,
                                        inp3, inp2, inp1, inp0,
                                        inp4, inp5, inp6, inp7,
                                        const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                        inp5, inp6, inp7, inp8,
                                        inp5, inp4, inp3, inp2,
                                        inp6, inp7, inp8, inp8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                        inp7, inp8, inp8, inp7,
                                        inp7, inp6, inp5, inp4,
                                        inp8, inp8, inp7, inp6,
                                        const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void vert_mc_qpel_16x16_msa(const uint8_t *src,
                                   int32_t src_stride,
                                   uint8_t *dst,
                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    inp4 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp0, inp0, inp1, inp2,
                                  inp1, inp2, inp3, inp4,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp5 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp1, inp0, inp0, inp1,
                                  inp2, inp3, inp4, inp5,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp2, inp1, inp0, inp0,
                                  inp3, inp4, inp5, inp6,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp7 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp3, inp2, inp1, inp0,
                                  inp4, inp5, inp6, inp7,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp4, inp3, inp2, inp1,
                                  inp5, inp6, inp7, inp8,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp9 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp5, inp4, inp3, inp2,
                                  inp6, inp7, inp8, inp9,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp6, inp5, inp4, inp3,
                                  inp7, inp8, inp9, inp10,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp11 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp7, inp6, inp5, inp4,
                                  inp8, inp9, inp10, inp11,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp8, inp7, inp6, inp5,
                                  inp9, inp10, inp11, inp12,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp13 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp9, inp8, inp7, inp6,
                                  inp10, inp11, inp12, inp13,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp10, inp9, inp8, inp7,
                                  inp11, inp12, inp13, inp14,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp15 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp11, inp10, inp9, inp8,
                                  inp12, inp13, inp14, inp15,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER(inp12, inp11, inp10, inp9,
                                  inp13, inp14, inp15, inp16,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp13, inp12, inp11, inp10,
                                  inp14, inp15, inp16, inp16,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp14, inp13, inp12, inp11,
                                  inp15, inp16, inp16, inp15,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp15, inp14, inp13, inp12,
                                  inp16, inp16, inp15, inp14,
                                  const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;
}

static void vert_mc_qpel_aver_src1_8x8_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 tmp0, tmp1, res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                        inp1, inp2, inp3, inp4,
                                        inp1, inp0, inp0, inp1,
                                        inp2, inp3, inp4, inp5,
                                        const20, const6, const3);

    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                        inp3, inp4, inp5, inp6,
                                        inp3, inp2, inp1, inp0,
                                        inp4, inp5, inp6, inp7,
                                        const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp1, 1, (v2i64) inp2);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp3, 1, (v2i64) inp4);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                        inp5, inp6, inp7, inp8,
                                        inp5, inp4, inp3, inp2,
                                        inp6, inp7, inp8, inp8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                        inp7, inp8, inp8, inp7,
                                        inp7, inp6, inp5, inp4,
                                        inp8, inp8, inp7, inp6,
                                        const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp5, 1, (v2i64) inp6);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp7, 1, (v2i64) inp8);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void vert_mc_qpel_aver_src1_16x16_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    inp4 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp0, inp0, inp1, inp2,
                                  inp1, inp2, inp3, inp4,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp1);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp5 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp1, inp0, inp0, inp1,
                                  inp2, inp3, inp4, inp5,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp2);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp2, inp1, inp0, inp0,
                                  inp3, inp4, inp5, inp6,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp7 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp3, inp2, inp1, inp0,
                                  inp4, inp5, inp6, inp7,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp4);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp4, inp3, inp2, inp1,
                                  inp5, inp6, inp7, inp8,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp5);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp9 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp5, inp4, inp3, inp2,
                                  inp6, inp7, inp8, inp9,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp6);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp6, inp5, inp4, inp3,
                                  inp7, inp8, inp9, inp10,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp7);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp11 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp7, inp6, inp5, inp4,
                                  inp8, inp9, inp10, inp11,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp8);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp8, inp7, inp6, inp5,
                                  inp9, inp10, inp11, inp12,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp9);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp13 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp9, inp8, inp7, inp6,
                                  inp10, inp11, inp12, inp13,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp10);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp10, inp9, inp8, inp7,
                                  inp11, inp12, inp13, inp14,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp11);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp15 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp11, inp10, inp9, inp8,
                                  inp12, inp13, inp14, inp15,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp12);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER(inp12, inp11, inp10, inp9,
                                  inp13, inp14, inp15, inp16,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp13);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp13, inp12, inp11, inp10,
                                  inp14, inp15, inp16, inp16,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp14);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp14, inp13, inp12, inp11,
                                  inp15, inp16, inp16, inp15,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp15);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER(inp15, inp14, inp13, inp12,
                                  inp16, inp16, inp15, inp14,
                                  const20, const6, const3);
    res0 = __msa_aver_u_b(res0, inp16);
    ST_UB(res0, dst);
}

static void vert_mc_qpel_no_rnd_aver_src0_8x8_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 tmp0, tmp1, res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                                 inp1, inp2, inp3, inp4,
                                                 inp1, inp0, inp0, inp1,
                                                 inp2, inp3, inp4, inp5,
                                                 const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                                 inp3, inp4, inp5, inp6,
                                                 inp3, inp2, inp1, inp0,
                                                 inp4, inp5, inp6, inp7,
                                                 const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    res0 = __msa_ave_u_b(res0, tmp0);
    res1 = __msa_ave_u_b(res1, tmp1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                                 inp5, inp6, inp7, inp8,
                                                 inp5, inp4, inp3, inp2,
                                                 inp6, inp7, inp8, inp8,
                                                 const20, const6, const3);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                                 inp7, inp8, inp8, inp7,
                                                 inp7, inp6, inp5, inp4,
                                                 inp8, inp8, inp7, inp6,
                                                 const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp4, 1, (v2i64) inp5);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp6, 1, (v2i64) inp7);
    res0 = __msa_ave_u_b(res0, tmp0);
    res1 = __msa_ave_u_b(res1, tmp1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void vert_mc_qpel_no_rnd_aver_src0_16x16_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp0, inp0, inp1, inp2,
                                           inp1, inp2, inp3, inp4,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp0);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp5 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp1, inp0, inp0, inp1,
                                           inp2, inp3, inp4, inp5,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp1);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp2, inp1, inp0, inp0,
                                           inp3, inp4, inp5, inp6,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp2);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp7 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp3, inp2, inp1, inp0,
                                           inp4, inp5, inp6, inp7,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp4, inp3, inp2, inp1,
                                           inp5, inp6, inp7, inp8,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp4);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp9 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp5, inp4, inp3, inp2,
                                           inp6, inp7, inp8, inp9,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp5);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp6, inp5, inp4, inp3,
                                           inp7, inp8, inp9, inp10,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp6);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp11 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp7, inp6, inp5, inp4,
                                           inp8, inp9, inp10, inp11,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp7);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp8, inp7, inp6, inp5,
                                           inp9, inp10, inp11, inp12,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp8);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp13 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp9, inp8, inp7, inp6,
                                           inp10, inp11, inp12, inp13,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp9);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp10, inp9, inp8, inp7,
                                           inp11, inp12, inp13, inp14,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp10);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp15 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp11, inp10, inp9, inp8,
                                           inp12, inp13, inp14, inp15,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp11);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp12, inp11, inp10, inp9,
                                           inp13, inp14, inp15, inp16,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp12);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp13, inp12, inp11, inp10,
                                           inp14, inp15, inp16, inp16,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp13);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp14, inp13, inp12, inp11,
                                           inp15, inp16, inp16, inp15,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp14);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp15, inp14, inp13, inp12,
                                           inp16, inp16, inp15, inp14,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp15);
    ST_UB(res0, dst);
    dst += dst_stride;
}

static void vert_mc_qpel_no_rnd_8x8_msa(const uint8_t *src,
                                        int32_t src_stride,
                                        uint8_t *dst,
                                        int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                                 inp1, inp2, inp3, inp4,
                                                 inp1, inp0, inp0, inp1,
                                                 inp2, inp3, inp4, inp5,
                                                 const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                                 inp3, inp4, inp5, inp6,
                                                 inp3, inp2, inp1, inp0,
                                                 inp4, inp5, inp6, inp7,
                                                 const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                                 inp5, inp6, inp7, inp8,
                                                 inp5, inp4, inp3, inp2,
                                                 inp6, inp7, inp8, inp8,
                                                 const20, const6, const3);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                                 inp7, inp8, inp8, inp7,
                                                 inp7, inp6, inp5, inp4,
                                                 inp8, inp8, inp7, inp6,
                                                 const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void vert_mc_qpel_no_rnd_16x16_msa(const uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst,
                                          int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp0, inp0, inp1, inp2,
                                           inp1, inp2, inp3, inp4,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp5 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp1, inp0, inp0, inp1,
                                           inp2, inp3, inp4, inp5,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp2, inp1, inp0, inp0,
                                           inp3, inp4, inp5, inp6,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp7 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp3, inp2, inp1, inp0,
                                           inp4, inp5, inp6, inp7,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp4, inp3, inp2, inp1,
                                           inp5, inp6, inp7, inp8,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp9 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp5, inp4, inp3, inp2,
                                           inp6, inp7, inp8, inp9,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp6, inp5, inp4, inp3,
                                           inp7, inp8, inp9, inp10,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp11 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp7, inp6, inp5, inp4,
                                           inp8, inp9, inp10, inp11,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp8, inp7, inp6, inp5,
                                           inp9, inp10, inp11, inp12,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp13 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp9, inp8, inp7, inp6,
                                           inp10, inp11, inp12, inp13,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp10, inp9, inp8, inp7,
                                           inp11, inp12, inp13, inp14,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp15 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp11, inp10, inp9, inp8,
                                           inp12, inp13, inp14, inp15,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp12, inp11, inp10, inp9,
                                           inp13, inp14, inp15, inp16,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp13, inp12, inp11, inp10,
                                           inp14, inp15, inp16, inp16,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp14, inp13, inp12, inp11,
                                           inp15, inp16, inp16, inp15,
                                           const20, const6, const3);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp15, inp14, inp13, inp12,
                                           inp16, inp16, inp15, inp14,
                                           const20, const6, const3);
    ST_UB(res0, dst);
}

static void vert_mc_qpel_no_rnd_aver_src1_8x8_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 tmp0, tmp1, res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                                 inp1, inp2, inp3, inp4,
                                                 inp1, inp0, inp0, inp1,
                                                 inp2, inp3, inp4, inp5,
                                                 const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                                 inp3, inp4, inp5, inp6,
                                                 inp3, inp2, inp1, inp0,
                                                 inp4, inp5, inp6, inp7,
                                                 const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp1, 1, (v2i64) inp2);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp3, 1, (v2i64) inp4);
    res0 = __msa_ave_u_b(res0, tmp0);
    res1 = __msa_ave_u_b(res1, tmp1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                                 inp5, inp6, inp7, inp8,
                                                 inp5, inp4, inp3, inp2,
                                                 inp6, inp7, inp8, inp8,
                                                 const20, const6, const3);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                                 inp7, inp8, inp8, inp7,
                                                 inp7, inp6, inp5, inp4,
                                                 inp8, inp8, inp7, inp6,
                                                 const20, const6, const3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp5, 1, (v2i64) inp6);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp7, 1, (v2i64) inp8);
    res0 = __msa_ave_u_b(res0, tmp0);
    res1 = __msa_ave_u_b(res1, tmp1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst + 4 * dst_stride, dst_stride);
}

static void vert_mc_qpel_no_rnd_aver_src1_16x16_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp0, inp0, inp1, inp2,
                                           inp1, inp2, inp3, inp4,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp1);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp5 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp1, inp0, inp0, inp1,
                                           inp2, inp3, inp4, inp5,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp2);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp2, inp1, inp0, inp0,
                                           inp3, inp4, inp5, inp6,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp3);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp7 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp3, inp2, inp1, inp0,
                                           inp4, inp5, inp6, inp7,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp4);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp4, inp3, inp2, inp1,
                                           inp5, inp6, inp7, inp8,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp5);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp9 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp5, inp4, inp3, inp2,
                                           inp6, inp7, inp8, inp9,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp6);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp6, inp5, inp4, inp3,
                                           inp7, inp8, inp9, inp10,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp7);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp11 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp7, inp6, inp5, inp4,
                                           inp8, inp9, inp10, inp11,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp8);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp8, inp7, inp6, inp5,
                                           inp9, inp10, inp11, inp12,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp9);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp13 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp9, inp8, inp7, inp6,
                                           inp10, inp11, inp12, inp13,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp10);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp10, inp9, inp8, inp7,
                                           inp11, inp12, inp13, inp14,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp11);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp15 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp11, inp10, inp9, inp8,
                                           inp12, inp13, inp14, inp15,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp12);
    ST_UB(res0, dst);
    dst += dst_stride;

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp12, inp11, inp10, inp9,
                                           inp13, inp14, inp15, inp16,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp13);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp13, inp12, inp11, inp10,
                                           inp14, inp15, inp16, inp16,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp14);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp14, inp13, inp12, inp11,
                                           inp15, inp16, inp16, inp15,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp15);
    ST_UB(res0, dst);
    dst += dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER(inp15, inp14, inp13, inp12,
                                           inp16, inp16, inp15, inp14,
                                           const20, const6, const3);
    res0 = __msa_ave_u_b(res0, inp16);
    ST_UB(res0, dst);
}

static void vert_mc_qpel_avg_dst_aver_src0_8x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 tmp0, tmp1, res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                        inp1, inp2, inp3, inp4,
                                        inp1, inp0, inp0, inp1,
                                        inp2, inp3, inp4, inp5,
                                        const20, const6, const3);

    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                        inp3, inp4, inp5, inp6,
                                        inp3, inp2, inp1, inp0,
                                        inp4, inp5, inp6, inp7,
                                        const20, const6, const3);

    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                        inp5, inp6, inp7, inp8,
                                        inp5, inp4, inp3, inp2,
                                        inp6, inp7, inp8, inp8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                        inp7, inp8, inp8, inp7,
                                        inp7, inp6, inp5, inp4,
                                        inp8, inp8, inp7, inp6,
                                        const20, const6, const3);

    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp4, 1, (v2i64) inp5);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp6, 1, (v2i64) inp7);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void vert_mc_qpel_avg_dst_aver_src0_16x16_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0, res1, dst0, dst1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp0, inp0, inp1, inp2,
                                  inp1, inp2, inp3, inp4,
                                  const20, const6, const3);

    inp5 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp1, inp0, inp0, inp1,
                                  inp2, inp3, inp4, inp5,
                                  const20, const6, const3);

    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp0, res1, inp1, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp2, inp1, inp0, inp0,
                                  inp3, inp4, inp5, inp6,
                                  const20, const6, const3);

    inp7 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp3, inp2, inp1, inp0,
                                  inp4, inp5, inp6, inp7,
                                  const20, const6, const3);

    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp2, res1, inp3, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp8, inp9);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp4, inp3, inp2, inp1,
                                  inp5, inp6, inp7, inp8,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp5, inp4, inp3, inp2,
                                  inp6, inp7, inp8, inp9,
                                  const20, const6, const3);

    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp4, res1, inp5, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp10, inp11);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp6, inp5, inp4, inp3,
                                  inp7, inp8, inp9, inp10,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp7, inp6, inp5, inp4,
                                  inp8, inp9, inp10, inp11,
                                  const20, const6, const3);

    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp6, res1, inp7, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp12, inp13);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp8, inp7, inp6, inp5,
                                  inp9, inp10, inp11, inp12,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp9, inp8, inp7, inp6,
                                  inp10, inp11, inp12, inp13,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp8, res1, inp9, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp14, inp15);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp10, inp9, inp8, inp7,
                                  inp11, inp12, inp13, inp14,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp11, inp10, inp9, inp8,
                                  inp12, inp13, inp14, inp15,
                                  const20, const6, const3);

    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp10, res1, inp11, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER(inp12, inp11, inp10, inp9,
                                  inp13, inp14, inp15, inp16,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp13, inp12, inp11, inp10,
                                  inp14, inp15, inp16, inp16,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp12, res1, inp13, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    res0 = APPLY_VERT_QPEL_FILTER(inp14, inp13, inp12, inp11,
                                  inp15, inp16, inp16, inp15,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp15, inp14, inp13, inp12,
                                  inp16, inp16, inp15, inp14,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp14, res1, inp15, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
}

static void vert_mc_qpel_avg_dst_8x8_msa(const uint8_t *src,
                                         int32_t src_stride,
                                         uint8_t *dst,
                                         int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                        inp1, inp2, inp3, inp4,
                                        inp1, inp0, inp0, inp1,
                                        inp2, inp3, inp4, inp5,
                                        const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                        inp3, inp4, inp5, inp6,
                                        inp3, inp2, inp1, inp0,
                                        inp4, inp5, inp6, inp7,
                                        const20, const6, const3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
    AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                        inp5, inp6, inp7, inp8,
                                        inp5, inp4, inp3, inp2,
                                        inp6, inp7, inp8, inp8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                        inp7, inp8, inp8, inp7,
                                        inp7, inp6, inp5, inp4,
                                        inp8, inp8, inp7, inp6,
                                        const20, const6, const3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
    AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void vert_mc_qpel_avg_dst_16x16_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0, res1, dst0, dst1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp0, inp0, inp1, inp2,
                                  inp1, inp2, inp3, inp4,
                                  const20, const6, const3);
    inp5 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp1, inp0, inp0, inp1,
                                  inp2, inp3, inp4, inp5,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp2, inp1, inp0, inp0,
                                  inp3, inp4, inp5, inp6,
                                  const20, const6, const3);
    inp7 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp3, inp2, inp1, inp0,
                                  inp4, inp5, inp6, inp7,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp4, inp3, inp2, inp1,
                                  inp5, inp6, inp7, inp8,
                                  const20, const6, const3);
    inp9 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp5, inp4, inp3, inp2,
                                  inp6, inp7, inp8, inp9,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp6, inp5, inp4, inp3,
                                  inp7, inp8, inp9, inp10,
                                  const20, const6, const3);
    inp11 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp7, inp6, inp5, inp4,
                                  inp8, inp9, inp10, inp11,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp8, inp7, inp6, inp5,
                                  inp9, inp10, inp11, inp12,
                                  const20, const6, const3);
    inp13 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp9, inp8, inp7, inp6,
                                  inp10, inp11, inp12, inp13,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp10, inp9, inp8, inp7,
                                  inp11, inp12, inp13, inp14,
                                  const20, const6, const3);
    inp15 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp11, inp10, inp9, inp8,
                                  inp12, inp13, inp14, inp15,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER(inp12, inp11, inp10, inp9,
                                  inp13, inp14, inp15, inp16,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp13, inp12, inp11, inp10,
                                  inp14, inp15, inp16, inp16,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    res0 = APPLY_VERT_QPEL_FILTER(inp14, inp13, inp12, inp11,
                                  inp15, inp16, inp16, inp15,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp15, inp14, inp13, inp12,
                                  inp16, inp16, inp15, inp14,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
}

static void vert_mc_qpel_avg_dst_aver_src1_8x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 tmp0, tmp1, res0, res1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    LD_UB2(src, src_stride, inp4, inp5);
    src += (2 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp0, inp0, inp1, inp2,
                                        inp1, inp2, inp3, inp4,
                                        inp1, inp0, inp0, inp1,
                                        inp2, inp3, inp4, inp5,
                                        const20, const6, const3);
    LD_UB2(src, src_stride, inp6, inp7);
    src += (2 * src_stride);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp2, inp1, inp0, inp0,
                                        inp3, inp4, inp5, inp6,
                                        inp3, inp2, inp1, inp0,
                                        inp4, inp5, inp6, inp7,
                                        const20, const6, const3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp1, 1, (v2i64) inp2);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp3, 1, (v2i64) inp4);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);

    inp8 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(inp4, inp3, inp2, inp1,
                                        inp5, inp6, inp7, inp8,
                                        inp5, inp4, inp3, inp2,
                                        inp6, inp7, inp8, inp8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(inp6, inp5, inp4, inp3,
                                        inp7, inp8, inp8, inp7,
                                        inp7, inp6, inp5, inp4,
                                        inp8, inp8, inp7, inp6,
                                        const20, const6, const3);
    LD_UB4(dst, dst_stride, dst0, dst1, dst2, dst3);
    tmp0 = (v16u8) __msa_insve_d((v2i64) inp5, 1, (v2i64) inp6);
    tmp1 = (v16u8) __msa_insve_d((v2i64) inp7, 1, (v2i64) inp8);
    dst0 = (v16u8) __msa_insve_d((v2i64) dst0, 1, (v2i64) dst1);
    dst2 = (v16u8) __msa_insve_d((v2i64) dst2, 1, (v2i64) dst3);
    AVER_UB2_UB(res0, tmp0, res1, tmp1, res0, res1);
    AVER_UB2_UB(dst0, res0, dst2, res1, res0, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void vert_mc_qpel_avg_dst_aver_src1_16x16_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7, inp8;
    v16u8 inp9, inp10, inp11, inp12, inp13, inp14, inp15, inp16;
    v16u8 res0, res1, dst0, dst1;
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB5(src, src_stride, inp0, inp1, inp2, inp3, inp4);
    src += (5 * src_stride);
    res0 = APPLY_VERT_QPEL_FILTER(inp0, inp0, inp1, inp2,
                                  inp1, inp2, inp3, inp4,
                                  const20, const6, const3);
    inp5 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp1, inp0, inp0, inp1,
                                  inp2, inp3, inp4, inp5,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp1, res1, inp2, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp6 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp2, inp1, inp0, inp0,
                                  inp3, inp4, inp5, inp6,
                                  const20, const6, const3);
    inp7 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp3, inp2, inp1, inp0,
                                  inp4, inp5, inp6, inp7,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp3, res1, inp4, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp8 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp4, inp3, inp2, inp1,
                                  inp5, inp6, inp7, inp8,
                                  const20, const6, const3);
    inp9 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp5, inp4, inp3, inp2,
                                  inp6, inp7, inp8, inp9,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp5, res1, inp6, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp10 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp6, inp5, inp4, inp3,
                                  inp7, inp8, inp9, inp10,
                                  const20, const6, const3);
    inp11 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp7, inp6, inp5, inp4,
                                  inp8, inp9, inp10, inp11,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp7, res1, inp8, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp12 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp8, inp7, inp6, inp5,
                                  inp9, inp10, inp11, inp12,
                                  const20, const6, const3);
    inp13 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp9, inp8, inp7, inp6,
                                  inp10, inp11, inp12, inp13,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp9, res1, inp10, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp14 = LD_UB(src);
    src += src_stride;
    res0 = APPLY_VERT_QPEL_FILTER(inp10, inp9, inp8, inp7,
                                  inp11, inp12, inp13, inp14,
                                  const20, const6, const3);
    inp15 = LD_UB(src);
    src += src_stride;
    res1 = APPLY_VERT_QPEL_FILTER(inp11, inp10, inp9, inp8,
                                  inp12, inp13, inp14, inp15,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp11, res1, inp12, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp16 = LD_UB(src);
    res0 = APPLY_VERT_QPEL_FILTER(inp12, inp11, inp10, inp9,
                                  inp13, inp14, inp15, inp16,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp13, inp12, inp11, inp10,
                                  inp14, inp15, inp16, inp16,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp13, res1, inp14, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
    dst += (2 * dst_stride);

    res0 = APPLY_VERT_QPEL_FILTER(inp14, inp13, inp12, inp11,
                                  inp15, inp16, inp16, inp15,
                                  const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER(inp15, inp14, inp13, inp12,
                                  inp16, inp16, inp15, inp14,
                                  const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    AVER_UB2_UB(res0, inp15, res1, inp16, res0, res1);
    AVER_UB2_UB(res0, dst0, res1, dst1, res0, res1);
    ST_UB2(res0, res1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_horiz_src0_16x16_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp0, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp2, inp3, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp2, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp4, inp5, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp4, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp6, inp7, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(inp6, res);
        ST_UB(res, dst);
        dst += dst_stride;
    }

    LD_UB2(src, 1, inp0, inp1);
    res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                           const20, const6, const3);
    res = __msa_ave_u_b(inp0, res);
    ST_UB(res, dst);
}

static void hv_mc_qpel_no_rnd_aver_hv_src00_16x16_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      uint8_t *dst,
                                                      int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_hv_src00_8x8_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_ave_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_ave_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_ave_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_ave_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                       mask2, mask3, const20,
                                                       const6, const3);
    horiz8 = __msa_ave_u_b(inp0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = __msa_ave_u_b(avg1, res1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = __msa_ave_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_horiz_16x16_msa(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst,
                                              int32_t dst_stride,
                                              int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp2, inp3, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp4, inp5, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp6, inp7, mask,
                                               const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;
    }

    LD_UB2(src, 1, inp0, inp1);
    res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                           const20, const6, const3);
    ST_UB(res, dst);
}

static void hv_mc_qpel_no_rnd_aver_v_src0_16x16_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_v_src0_8x8_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                         mask2, mask3, const20,
                                                         const6, const3);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = __msa_ave_u_b(avg1, res1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_horiz_src1_16x16_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp1);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp2, inp3, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp4, inp5, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp5);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp6, inp7, mask,
                                               const20, const6, const3);
        res = __msa_ave_u_b(res, inp7);
        ST_UB(res, dst);
        dst += dst_stride;
    }

    LD_UB2(src, 1, inp0, inp1);
    res = APPLY_HORIZ_QPEL_NO_ROUND_FILTER(inp0, inp1, mask,
                                           const20, const6, const3);
    res = __msa_ave_u_b(inp1, res);
    ST_UB(res, dst);
}

static void hv_mc_qpel_no_rnd_aver_hv_src10_16x16_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      uint8_t *dst,
                                                      int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_hv_src10_8x8_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz0 = __msa_ave_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz2 = __msa_ave_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz4 = __msa_ave_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz6 = __msa_ave_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                       mask2, mask3, const20,
                                                       const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_ave_u_b(inp0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_h_src0_16x16_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_h_src0_8x8_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_ave_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_ave_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_ave_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_ave_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                       mask2, mask3, const20,
                                                       const6, const3);
    horiz8 = __msa_ave_u_b(inp0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    ST_D4(res1, res0, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);

    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_16x16_msa(const uint8_t *src,
                                        int32_t src_stride,
                                        uint8_t *dst,
                                        int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_8x8_msa(const uint8_t *src,
                                      int32_t src_stride,
                                      uint8_t *dst,
                                      int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    horiz6 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                         mask2, mask3, const20,
                                                         const6, const3);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;


    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_h_src1_16x16_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_h_src1_8x8_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz0 = __msa_ave_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz2 = __msa_ave_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz4 = __msa_ave_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz6 = __msa_ave_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                       mask2, mask3, const20,
                                                       const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_ave_u_b(inp0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_hv_src01_16x16_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      uint8_t *dst,
                                                      int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_hv_src01_8x8_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_ave_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_ave_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_ave_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_ave_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                       mask2, mask3, const20,
                                                       const6, const3);
    horiz8 = __msa_ave_u_b(inp0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = __msa_ave_u_b(avg1, res1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = __msa_ave_u_b(avg0, res0);

    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_v_src1_16x16_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_v_src1_8x8_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = __msa_ave_u_b(avg0, res0);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    horiz6 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                    mask2, mask3, const20,
                                                    const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = __msa_ave_u_b(avg1, res1);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                         mask2, mask3, const20,
                                                         const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = __msa_ave_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_hv_src11_16x16_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      uint8_t *dst,
                                                      int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_no_rnd_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_no_rnd_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_no_rnd_aver_hv_src11_8x8_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    uint8_t *dst,
                                                    int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz0 = __msa_ave_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz2 = __msa_ave_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp0, inp1, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);

    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);
    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz4 = __msa_ave_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                                 horiz1, horiz2, horiz3, horiz4,
                                                 horiz1, horiz0, horiz0, horiz1,
                                                 horiz2, horiz3, horiz4, horiz5,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = __msa_ave_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE(inp2, inp3, mask0, mask1,
                                                  mask2, mask3, const20,
                                                  const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz6 = __msa_ave_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                                 horiz3, horiz4, horiz5, horiz6,
                                                 horiz3, horiz2, horiz1, horiz0,
                                                 horiz4, horiz5, horiz6, horiz7,
                                                 const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_NO_ROUND_FILTER_8BYTE_1ROW(inp0, mask0, mask1,
                                                       mask2, mask3, const20,
                                                       const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_ave_u_b(inp0, res0);
    res0 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                                 horiz5, horiz6, horiz7, horiz8,
                                                 horiz5, horiz4, horiz3, horiz2,
                                                 horiz6, horiz7, horiz8, horiz8,
                                                 const20, const6, const3);
    res1 = APPLY_VERT_QPEL_NO_ROUND_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                                 horiz7, horiz8, horiz8, horiz7,
                                                 horiz7, horiz6, horiz5, horiz4,
                                                 horiz8, horiz8, horiz7, horiz6,
                                                 const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = __msa_ave_u_b(avg0, res0);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = __msa_ave_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_horiz_src0_16x16_msa(const uint8_t *src,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride,
                                                 int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp0, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp2, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp4, res);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(inp6, res);
        ST_UB(res, dst);
        dst += dst_stride;
    }

    LD_UB2(src, 1, inp0, inp1);
    res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask, const20, const6, const3);
    res = __msa_aver_u_b(inp0, res);
    ST_UB(res, dst);
}

static void hv_mc_qpel_aver_hv_src00_16x16_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_hv_src00_8x8_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = __msa_aver_u_b(avg1, res1);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    horiz8 = __msa_aver_u_b(inp0, res0);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = __msa_aver_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_horiz_16x16_msa(const uint8_t *src,
                                            int32_t src_stride,
                                            uint8_t *dst,
                                            int32_t dst_stride,
                                            int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                      const20, const6, const3);
        ST_UB(res, dst);
        dst += dst_stride;
    }

    LD_UB2(src, 1, inp0, inp1);
    res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask, const20, const6, const3);
    ST_UB(res, dst);
}

static void hv_mc_qpel_aver_v_src0_16x16_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_v_src0_8x8_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,
                                                mask0, mask1, mask2, mask3,
                                                const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = __msa_aver_u_b(avg1, res1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = __msa_aver_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_horiz_src1_16x16_msa(const uint8_t *src,
                                                 int32_t src_stride,
                                                 uint8_t *dst,
                                                 int32_t dst_stride,
                                                 int32_t height)
{
    uint8_t loop_count;
    v16u8 inp0, inp1, inp2, inp3, inp4, inp5, inp6, inp7;
    v16u8 res;
    v16u8 mask = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);
    v8u16 const20 = (v8u16) __msa_ldi_h(20);

    for (loop_count = (height >> 2); loop_count--;) {
        LD_UB4(src, src_stride, inp0, inp2, inp4, inp6);
        LD_UB4((src + 1), src_stride, inp1, inp3, inp5, inp7);
        src += (4 * src_stride);
        res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp1);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp2, inp3, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp3);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp4, inp5, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp5);
        ST_UB(res, dst);
        dst += dst_stride;

        res = APPLY_HORIZ_QPEL_FILTER(inp6, inp7, mask,
                                      const20, const6, const3);
        res = __msa_aver_u_b(res, inp7);
        ST_UB(res, dst);
        dst += dst_stride;
    }

    LD_UB2(src, 1, inp0, inp1);
    res = APPLY_HORIZ_QPEL_FILTER(inp0, inp1, mask, const20, const6, const3);
    res = __msa_aver_u_b(inp1, res);
    ST_UB(res, dst);
}

static void hv_mc_qpel_aver_hv_src10_16x16_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_hv_src10_8x8_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = __msa_aver_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = __msa_aver_u_b(avg1, res1);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_aver_u_b(inp0, res0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = __msa_aver_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_h_src0_16x16_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_h_src0_8x8_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    horiz8 = __msa_aver_u_b(inp0, res0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_16x16_msa(const uint8_t *src,
                                 int32_t src_stride,
                                 uint8_t *dst,
                                 int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_8x8_msa(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,
                                                mask0, mask1, mask2, mask3,
                                                const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_h_src1_16x16_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_h_src1_8x8_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);

    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_aver_u_b(inp0, res0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
    dst += (4 * dst_stride);

    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_hv_src01_16x16_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_hv_src01_8x8_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);

    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);

    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_insve_d((v2i64) horiz1, 1, (v2i64) horiz2);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    horiz8 = __msa_aver_u_b(inp0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_insve_d((v2i64) horiz3, 1, (v2i64) horiz4);
    res1 = __msa_aver_u_b(avg1, res1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg0 = (v16u8) __msa_insve_d((v2i64) horiz5, 1, (v2i64) horiz6);
    res0 = __msa_aver_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_insve_d((v2i64) horiz7, 1, (v2i64) horiz8);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_v_src1_16x16_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst,
                                             int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_v_src1_8x8_msa(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst,
                                           int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_insve_d((v2i64) horiz1, 1, (v2i64) horiz2);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,
                                                mask0, mask1, mask2, mask3,
                                                const20, const6, const3);
    avg1 = (v16u8) __msa_insve_d((v2i64) horiz3, 1, (v2i64) horiz4);
    res1 = __msa_aver_u_b(avg1, res1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;
    avg0 = (v16u8) __msa_insve_d((v2i64) horiz5, 1, (v2i64) horiz6);
    res0 = __msa_aver_u_b(avg0, res0);

    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_insve_d((v2i64) horiz7, 1, (v2i64) horiz8);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_aver_hv_src11_16x16_msa(const uint8_t *src,
                                               int32_t src_stride,
                                               uint8_t *dst,
                                               int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_aver_hv_src11_8x8_msa(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB4(src, src_stride, inp0, inp1, inp2, inp3);
    src += (4 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                         mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_insve_d((v2i64) inp0, 1, (v2i64) inp1);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = __msa_aver_u_b(avg0, res0);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_insve_d((v2i64) inp2, 1, (v2i64) inp3);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = __msa_aver_u_b(avg1, res1);
    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_aver_u_b(inp0, res0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += 2 * dst_stride;

    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = __msa_aver_u_b(avg0, res0);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D4(res0, res1, 0, 1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src00_16x16_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src00_8x8_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    horiz8 = __msa_aver_u_b(inp0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_v_src0_16x16_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_v_src0_8x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    horiz2 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    horiz4 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,
                                                mask0, mask1, mask2, mask3,
                                                const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src10_16x16_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_aver_src0_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src10_8x8_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);

    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz1, (v2i64) horiz0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);

    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz3, (v2i64) horiz2);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_aver_u_b(inp0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz5, (v2i64) horiz4);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz7, (v2i64) horiz6);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_h_src0_16x16_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_h_src0_8x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    horiz8 = __msa_aver_u_b(inp0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_16x16_msa(const uint8_t *src, int32_t src_stride,
                                         uint8_t *dst, int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_16x16_msa(buff, 16, dst, dst_stride);

}

static void hv_mc_qpel_avg_dst_8x8_msa(const uint8_t *src, int32_t src_stride,
                                       uint8_t *dst, int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    horiz2 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    horiz4 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,
                                                mask0, mask1, mask2, mask3,
                                                const20, const6, const3);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_h_src1_16x16_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_h_src1_8x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);

    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);

    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_aver_u_b(inp0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src01_16x16_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src0_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src01_8x8_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);

    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    horiz8 = __msa_aver_u_b(inp0, res0);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1,
                                        horiz5, horiz6, horiz7, horiz8,
                                        horiz5, horiz4, horiz3, horiz2,
                                        horiz6, horiz7, horiz8, horiz8,
                                        const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3,
                                        horiz7, horiz8, horiz8, horiz7,
                                        horiz7, horiz6, horiz5, horiz4,
                                        horiz8, horiz8, horiz7, horiz6,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = __msa_aver_u_b(avg0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = __msa_aver_u_b(avg1, res1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_v_src1_16x16_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_v_src1_8x8_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz2 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    horiz4 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2,
                                        horiz1, horiz2, horiz3, horiz4,
                                        horiz1, horiz0, horiz0, horiz1,
                                        horiz2, horiz3, horiz4, horiz5,
                                        const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    horiz6 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3,
                                           mask0, mask1, mask2, mask3,
                                           const20, const6, const3);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0,
                                        horiz3, horiz4, horiz5, horiz6,
                                        horiz3, horiz2, horiz1, horiz0,
                                        horiz4, horiz5, horiz6, horiz7,
                                        const20, const6, const3);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    horiz8 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0,
                                                mask0, mask1, mask2, mask3,
                                                const20, const6, const3);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1, horiz5,
                                        horiz6, horiz7, horiz8, horiz5, horiz4,
                                        horiz3, horiz2, horiz6, horiz7, horiz8,
                                        horiz8, const20, const6, const3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3, horiz7,
                                        horiz8, horiz8, horiz7, horiz7, horiz6,
                                        horiz5, horiz4, horiz8, horiz8, horiz7,
                                        horiz6, const20, const6, const3);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = __msa_aver_u_b(avg0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = __msa_aver_u_b(avg1, res1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src11_16x16_msa(const uint8_t *src,
                                                       int32_t src_stride,
                                                       uint8_t *dst,
                                                       int32_t dst_stride)
{
    uint8_t buff[272];

    hv_mc_qpel_aver_horiz_src1_16x16_msa(src, src_stride, buff, 16, 16);
    vert_mc_qpel_avg_dst_aver_src1_16x16_msa(buff, 16, dst, dst_stride);
}

static void hv_mc_qpel_avg_dst_aver_hv_src11_8x8_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     uint8_t *dst,
                                                     int32_t dst_stride)
{
    v16u8 inp0, inp1, inp2, inp3;
    v16u8 res0, res1, avg0, avg1;
    v16u8 horiz0, horiz1, horiz2, horiz3;
    v16u8 horiz4, horiz5, horiz6, horiz7, horiz8;
    v16u8 dst0, dst1;
    v16u8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v16u8 mask1 = { 0, 2, 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5, 8, 6, 8 };
    v16u8 mask2 = { 1, 3, 0, 4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 8, 5, 7 };
    v16u8 mask3 = { 2, 4, 1, 5, 0, 6, 0, 7, 1, 8, 2, 8, 3, 7, 4, 6 };
    v16u8 const20 = (v16u8) __msa_ldi_b(20);
    v16u8 const6 = (v16u8) __msa_ldi_b(6);
    v16u8 const3 = (v16u8) __msa_ldi_b(3);

    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz0 = __msa_aver_u_b(inp0, res0);
    horiz1 = (v16u8) __msa_splati_d((v2i64) horiz0, 1);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    LD_UB2(src, src_stride, inp0, inp1);
    src += (2 * src_stride);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz2 = __msa_aver_u_b(inp2, res1);
    horiz3 = (v16u8) __msa_splati_d((v2i64) horiz2, 1);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp0, inp1, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp0, inp0, inp1, inp1, 1, inp0, inp1);

    inp0 = (v16u8) __msa_ilvr_d((v2i64) inp1, (v2i64) inp0);
    horiz4 = __msa_aver_u_b(inp0, res0);
    horiz5 = (v16u8) __msa_splati_d((v2i64) horiz4, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz2, (v2i64) horiz1);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz0, horiz0, horiz1, horiz2, horiz1,
                                        horiz2, horiz3, horiz4, horiz1, horiz0,
                                        horiz0, horiz1, horiz2, horiz3, horiz4,
                                        horiz5, const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(src, src_stride, inp2, inp3);
    src += (2 * src_stride);
    res1 = APPLY_HORIZ_QPEL_FILTER_8BYTE(inp2, inp3, mask0, mask1, mask2, mask3,
                                         const20, const6, const3);
    SLDI_B2_UB(inp2, inp2, inp3, inp3, 1, inp2, inp3);

    inp2 = (v16u8) __msa_ilvr_d((v2i64) inp3, (v2i64) inp2);
    horiz6 = __msa_aver_u_b(inp2, res1);
    horiz7 = (v16u8) __msa_splati_d((v2i64) horiz6, 1);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz4, (v2i64) horiz3);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz2, horiz1, horiz0, horiz0, horiz3,
                                        horiz4, horiz5, horiz6, horiz3, horiz2,
                                        horiz1, horiz0, horiz4, horiz5, horiz6,
                                        horiz7, const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    inp0 = LD_UB(src);
    res0 = APPLY_HORIZ_QPEL_FILTER_8BYTE_1ROW(inp0, mask0, mask1, mask2, mask3,
                                              const20, const6, const3);
    inp0 = (v16u8) __msa_sldi_b((v16i8) inp0, (v16i8) inp0, 1);
    horiz8 = __msa_aver_u_b(inp0, res0);
    LD_UB2(dst, dst_stride, dst0, dst1);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) horiz6, (v2i64) horiz5);
    res0 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz4, horiz3, horiz2, horiz1, horiz5,
                                        horiz6, horiz7, horiz8, horiz5, horiz4,
                                        horiz3, horiz2, horiz6, horiz7, horiz8,
                                        horiz8, const20, const6, const3);
    res0 = __msa_aver_u_b(avg0, res0);
    avg0 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res0 = __msa_aver_u_b(avg0, res0);
    ST_D2(res0, 0, 1, dst, dst_stride);
    dst += (2 * dst_stride);

    LD_UB2(dst, dst_stride, dst0, dst1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) horiz8, (v2i64) horiz7);
    res1 = APPLY_VERT_QPEL_FILTER_8BYTE(horiz6, horiz5, horiz4, horiz3, horiz7,
                                        horiz8, horiz8, horiz7, horiz7, horiz6,
                                        horiz5, horiz4, horiz8, horiz8, horiz7,
                                        horiz6, const20, const6, const3);
    res1 = __msa_aver_u_b(avg1, res1);
    avg1 = (v16u8) __msa_ilvr_d((v2i64) dst1, (v2i64) dst0);
    res1 = __msa_aver_u_b(avg1, res1);
    ST_D2(res1, 0, 1, dst, dst_stride);
}

static void copy_8x8_msa(const uint8_t *src, int32_t src_stride,
                         uint8_t *dst, int32_t dst_stride)
{
    uint64_t src0, src1;
    int32_t loop_cnt;

    for (loop_cnt = 4; loop_cnt--;) {
        src0 = LD(src);
        src += src_stride;
        src1 = LD(src);
        src += src_stride;

        SD(src0, dst);
        dst += dst_stride;
        SD(src1, dst);
        dst += dst_stride;
    }
}

static void copy_16x16_msa(const uint8_t *src, int32_t src_stride,
                           uint8_t *dst, int32_t dst_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 src8, src9, src10, src11, src12, src13, src14, src15;

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    src += (8 * src_stride);
    LD_UB8(src, src_stride,
           src8, src9, src10, src11, src12, src13, src14, src15);

    ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7, dst, dst_stride);
    dst += (8 * dst_stride);
    ST_UB8(src8, src9, src10, src11, src12, src13, src14, src15,
           dst, dst_stride);
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

void ff_copy_16x16_msa(uint8_t *dest, const uint8_t *src, ptrdiff_t stride)
{
    copy_16x16_msa(src, stride, dest, stride);
}

void ff_copy_8x8_msa(uint8_t *dest, const uint8_t *src, ptrdiff_t stride)
{
    copy_8x8_msa(src, stride, dest, stride);
}

void ff_horiz_mc_qpel_aver_src0_8width_msa(uint8_t *dest,
                                           const uint8_t *src,
                                           ptrdiff_t stride)
{
    horiz_mc_qpel_aver_src0_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_aver_src0_16width_msa(uint8_t *dest,
                                            const uint8_t *src,
                                            ptrdiff_t stride)
{
    horiz_mc_qpel_aver_src0_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_8width_msa(uint8_t *dest, const uint8_t *src,
                                 ptrdiff_t stride)
{
    horiz_mc_qpel_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_16width_msa(uint8_t *dest,
                                  const uint8_t *src, ptrdiff_t stride)
{
    horiz_mc_qpel_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_aver_src1_8width_msa(uint8_t *dest,
                                           const uint8_t *src,
                                           ptrdiff_t stride)
{
    horiz_mc_qpel_aver_src1_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_aver_src1_16width_msa(uint8_t *dest,
                                            const uint8_t *src,
                                            ptrdiff_t stride)
{
    horiz_mc_qpel_aver_src1_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_no_rnd_aver_src0_8width_msa(uint8_t *dest,
                                                  const uint8_t *src,
                                                  ptrdiff_t stride)
{
    horiz_mc_qpel_no_rnd_aver_src0_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_no_rnd_aver_src0_16width_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    horiz_mc_qpel_no_rnd_aver_src0_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_no_rnd_8width_msa(uint8_t *dest,
                                        const uint8_t *src, ptrdiff_t stride)
{
    horiz_mc_qpel_no_rnd_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_no_rnd_16width_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    horiz_mc_qpel_no_rnd_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_no_rnd_aver_src1_8width_msa(uint8_t *dest,
                                                  const uint8_t *src,
                                                  ptrdiff_t stride)
{
    horiz_mc_qpel_no_rnd_aver_src1_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_no_rnd_aver_src1_16width_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    horiz_mc_qpel_no_rnd_aver_src1_16width_msa(src, stride, dest, stride, 16);
}

void ff_avg_width8_msa(uint8_t *dest, const uint8_t *src, ptrdiff_t stride)
{
    avg_width8_msa(src, stride, dest, stride, 8);
}

void ff_avg_width16_msa(uint8_t *dest, const uint8_t *src, ptrdiff_t stride)
{
    avg_width16_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_avg_dst_aver_src0_8width_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    horiz_mc_qpel_avg_dst_aver_src0_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_avg_dst_aver_src0_16width_msa(uint8_t *dest,
                                                    const uint8_t *src,
                                                    ptrdiff_t stride)
{
    horiz_mc_qpel_avg_dst_aver_src0_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_avg_dst_8width_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    horiz_mc_qpel_avg_dst_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_avg_dst_16width_msa(uint8_t *dest,
                                          const uint8_t *src, ptrdiff_t stride)
{
    horiz_mc_qpel_avg_dst_16width_msa(src, stride, dest, stride, 16);
}

void ff_horiz_mc_qpel_avg_dst_aver_src1_8width_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    horiz_mc_qpel_avg_dst_aver_src1_8width_msa(src, stride, dest, stride, 8);
}

void ff_horiz_mc_qpel_avg_dst_aver_src1_16width_msa(uint8_t *dest,
                                                    const uint8_t *src,
                                                    ptrdiff_t stride)
{
    horiz_mc_qpel_avg_dst_aver_src1_16width_msa(src, stride, dest, stride, 16);
}


void ff_vert_mc_qpel_aver_src0_8x8_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_aver_src0_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_aver_src0_16x16_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_aver_src0_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_8x8_msa(uint8_t *dest, const uint8_t *src,
                             ptrdiff_t stride)
{
    vert_mc_qpel_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_16x16_msa(uint8_t *dest, const uint8_t *src,
                               ptrdiff_t stride)
{
    vert_mc_qpel_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_aver_src1_8x8_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_aver_src1_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_aver_src1_16x16_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_aver_src1_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_no_rnd_aver_src0_8x8_msa(uint8_t *dest,
                                              const uint8_t *src,
                                              ptrdiff_t stride)
{
    vert_mc_qpel_no_rnd_aver_src0_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_no_rnd_aver_src0_16x16_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    vert_mc_qpel_no_rnd_aver_src0_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_no_rnd_8x8_msa(uint8_t *dest,
                                    const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_no_rnd_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_no_rnd_16x16_msa(uint8_t *dest,
                                      const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_no_rnd_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_no_rnd_aver_src1_8x8_msa(uint8_t *dest,
                                              const uint8_t *src,
                                              ptrdiff_t stride)
{
    vert_mc_qpel_no_rnd_aver_src1_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_no_rnd_aver_src1_16x16_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    vert_mc_qpel_no_rnd_aver_src1_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_avg_dst_aver_src0_8x8_msa(uint8_t *dest,
                                               const uint8_t *src,
                                               ptrdiff_t stride)
{
    vert_mc_qpel_avg_dst_aver_src0_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_avg_dst_aver_src0_16x16_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    vert_mc_qpel_avg_dst_aver_src0_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_avg_dst_8x8_msa(uint8_t *dest,
                                     const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_avg_dst_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_avg_dst_16x16_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    vert_mc_qpel_avg_dst_16x16_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_avg_dst_aver_src1_8x8_msa(uint8_t *dest,
                                               const uint8_t *src,
                                               ptrdiff_t stride)
{
    vert_mc_qpel_avg_dst_aver_src1_8x8_msa(src, stride, dest, stride);
}

void ff_vert_mc_qpel_avg_dst_aver_src1_16x16_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    vert_mc_qpel_avg_dst_aver_src1_16x16_msa(src, stride, dest, stride);
}

/* HV cases */
void ff_hv_mc_qpel_aver_hv_src00_16x16_msa(uint8_t *dest,
                                           const uint8_t *src,
                                           ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src00_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src00_8x8_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src00_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_v_src0_16x16_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_v_src0_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_v_src0_8x8_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_v_src0_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src10_16x16_msa(uint8_t *dest,
                                           const uint8_t *src,
                                           ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src10_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src10_8x8_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src10_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_h_src0_16x16_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_h_src0_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_h_src0_8x8_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_h_src0_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_16x16_msa(uint8_t *dest, const uint8_t *src,
                             ptrdiff_t stride)
{
    hv_mc_qpel_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_8x8_msa(uint8_t *dest, const uint8_t *src,
                           ptrdiff_t stride)
{
    hv_mc_qpel_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_h_src1_16x16_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_h_src1_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_h_src1_8x8_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_h_src1_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src01_16x16_msa(uint8_t *dest,
                                           const uint8_t *src,
                                           ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src01_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src01_8x8_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src01_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_v_src1_16x16_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_v_src1_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_v_src1_8x8_msa(uint8_t *dest,
                                       const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_v_src1_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src11_16x16_msa(uint8_t *dest,
                                           const uint8_t *src,
                                           ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src11_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_aver_hv_src11_8x8_msa(uint8_t *dest,
                                         const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_aver_hv_src11_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src00_16x16_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src00_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src00_8x8_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src00_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_v_src0_16x16_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_v_src0_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_v_src0_8x8_msa(uint8_t *dest,
                                               const uint8_t *src,
                                               ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_v_src0_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src10_16x16_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src10_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src10_8x8_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src10_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_h_src0_16x16_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_h_src0_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_h_src0_8x8_msa(uint8_t *dest,
                                               const uint8_t *src,
                                               ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_h_src0_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_16x16_msa(uint8_t *dest,
                                     const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_8x8_msa(uint8_t *dest,
                                   const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_h_src1_16x16_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_h_src1_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_h_src1_8x8_msa(uint8_t *dest,
                                               const uint8_t *src,
                                               ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_h_src1_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src01_16x16_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src01_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src01_8x8_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src01_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_v_src1_16x16_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_v_src1_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_v_src1_8x8_msa(uint8_t *dest,
                                               const uint8_t *src,
                                               ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_v_src1_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src11_16x16_msa(uint8_t *dest,
                                                   const uint8_t *src,
                                                   ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src11_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_avg_dst_aver_hv_src11_8x8_msa(uint8_t *dest,
                                                 const uint8_t *src,
                                                 ptrdiff_t stride)
{
    hv_mc_qpel_avg_dst_aver_hv_src11_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src00_16x16_msa(uint8_t *dest,
                                                  const uint8_t *src,
                                                  ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src00_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src00_8x8_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src00_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_v_src0_16x16_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_v_src0_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_v_src0_8x8_msa(uint8_t *dest,
                                              const uint8_t *src,
                                              ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_v_src0_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src10_16x16_msa(uint8_t *dest,
                                                  const uint8_t *src,
                                                  ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src10_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src10_8x8_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src10_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_h_src0_16x16_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_h_src0_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_h_src0_8x8_msa(uint8_t *dest,
                                              const uint8_t *src,
                                              ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_h_src0_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_16x16_msa(uint8_t *dest,
                                    const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_8x8_msa(uint8_t *dest,
                                  const uint8_t *src, ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_h_src1_16x16_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_h_src1_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_h_src1_8x8_msa(uint8_t *dest,
                                              const uint8_t *src,
                                              ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_h_src1_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src01_16x16_msa(uint8_t *dest,
                                                  const uint8_t *src,
                                                  ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src01_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src01_8x8_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src01_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_v_src1_16x16_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_v_src1_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_v_src1_8x8_msa(uint8_t *dest,
                                              const uint8_t *src,
                                              ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_v_src1_8x8_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src11_16x16_msa(uint8_t *dest,
                                                  const uint8_t *src,
                                                  ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src11_16x16_msa(src, stride, dest, stride);
}

void ff_hv_mc_qpel_no_rnd_aver_hv_src11_8x8_msa(uint8_t *dest,
                                                const uint8_t *src,
                                                ptrdiff_t stride)
{
    hv_mc_qpel_no_rnd_aver_hv_src11_8x8_msa(src, stride, dest, stride);
}
