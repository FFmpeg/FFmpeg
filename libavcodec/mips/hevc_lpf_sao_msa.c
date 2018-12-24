/*
 * Copyright (c) 2015 -2017 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

static void hevc_loopfilter_luma_hor_msa(uint8_t *src, int32_t stride,
                                         int32_t beta, int32_t *tc,
                                         uint8_t *p_is_pcm, uint8_t *q_is_pcm)
{
    uint8_t *p3 = src - (stride << 2);
    uint8_t *p2 = src - ((stride << 1) + stride);
    uint8_t *p1 = src - (stride << 1);
    uint8_t *p0 = src - stride;
    uint8_t *q0 = src;
    uint8_t *q1 = src + stride;
    uint8_t *q2 = src + (stride << 1);
    uint8_t *q3 = src + (stride << 1) + stride;
    uint8_t flag0, flag1;
    int32_t dp00, dq00, dp30, dq30, d00, d30;
    int32_t d0030, d0434;
    int32_t dp04, dq04, dp34, dq34, d04, d34;
    int32_t tc0, p_is_pcm0, q_is_pcm0, beta30, beta20, tc250;
    int32_t tc4, p_is_pcm4, q_is_pcm4, tc254, tmp;
    uint64_t dst_val0, dst_val1;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5;
    v2i64 cmp0, cmp1, cmp2, p_is_pcm_vec, q_is_pcm_vec;
    v2i64 cmp3;
    v8u16 temp0, temp1;
    v8i16 temp2;
    v8i16 tc_pos, tc_neg;
    v8i16 diff0, diff1, delta0, delta1, delta2, abs_delta0;
    v16i8 zero = { 0 };
    v8u16 p3_src, p2_src, p1_src, p0_src, q0_src, q1_src, q2_src, q3_src;

    dp00 = abs(p2[0] - (p1[0] << 1) + p0[0]);
    dq00 = abs(q2[0] - (q1[0] << 1) + q0[0]);
    dp30 = abs(p2[3] - (p1[3] << 1) + p0[3]);
    dq30 = abs(q2[3] - (q1[3] << 1) + q0[3]);
    d00 = dp00 + dq00;
    d30 = dp30 + dq30;
    dp04 = abs(p2[4] - (p1[4] << 1) + p0[4]);
    dq04 = abs(q2[4] - (q1[4] << 1) + q0[4]);
    dp34 = abs(p2[7] - (p1[7] << 1) + p0[7]);
    dq34 = abs(q2[7] - (q1[7] << 1) + q0[7]);
    d04 = dp04 + dq04;
    d34 = dp34 + dq34;

    p_is_pcm0 = p_is_pcm[0];
    p_is_pcm4 = p_is_pcm[1];
    q_is_pcm0 = q_is_pcm[0];
    q_is_pcm4 = q_is_pcm[1];

    cmp0 = __msa_fill_d(p_is_pcm0);
    cmp1 = __msa_fill_d(p_is_pcm4);
    p_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
    p_is_pcm_vec = __msa_ceqi_d(p_is_pcm_vec, 0);

    d0030 = (d00 + d30) >= beta;
    d0434 = (d04 + d34) >= beta;

    cmp0 = (v2i64) __msa_fill_w(d0030);
    cmp1 = (v2i64) __msa_fill_w(d0434);
    cmp3 = (v2i64) __msa_ilvev_w((v4i32) cmp1, (v4i32) cmp0);
    cmp3 = (v2i64) __msa_ceqi_w((v4i32) cmp3, 0);

    if ((!p_is_pcm0 || !p_is_pcm4 || !q_is_pcm0 || !q_is_pcm4) &&
        (!d0030 || !d0434)) {
        p3_src = LD_UH(p3);
        p2_src = LD_UH(p2);
        p1_src = LD_UH(p1);
        p0_src = LD_UH(p0);

        cmp0 = __msa_fill_d(q_is_pcm0);
        cmp1 = __msa_fill_d(q_is_pcm4);
        q_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
        q_is_pcm_vec = __msa_ceqi_d(q_is_pcm_vec, 0);

        tc0 = tc[0];
        beta30 = beta >> 3;
        beta20 = beta >> 2;
        tc250 = ((tc0 * 5 + 1) >> 1);
        tc4 = tc[1];
        tc254 = ((tc4 * 5 + 1) >> 1);

        cmp0 = (v2i64) __msa_fill_h(tc0);
        cmp1 = (v2i64) __msa_fill_h(tc4);

        ILVR_B4_UH(zero, p3_src, zero, p2_src, zero, p1_src, zero, p0_src,
                   p3_src, p2_src, p1_src, p0_src);
        q0_src = LD_UH(q0);
        q1_src = LD_UH(q1);
        q2_src = LD_UH(q2);
        q3_src = LD_UH(q3);

        flag0 = abs(p3[0] - p0[0]) + abs(q3[0] - q0[0]) < beta30 &&
                abs(p0[0] - q0[0]) < tc250;
        flag0 = flag0 && (abs(p3[3] - p0[3]) + abs(q3[3] - q0[3]) < beta30 &&
                abs(p0[3] - q0[3]) < tc250 && (d00 << 1) < beta20 &&
                (d30 << 1) < beta20);

        tc_pos = (v8i16) __msa_ilvev_d(cmp1, cmp0);
        ILVR_B4_UH(zero, q0_src, zero, q1_src, zero, q2_src, zero, q3_src,
                   q0_src, q1_src, q2_src, q3_src);
        flag1 = abs(p3[4] - p0[4]) + abs(q3[4] - q0[4]) < beta30 &&
                abs(p0[4] - q0[4]) < tc254;
        flag1 = flag1 && (abs(p3[7] - p0[7]) + abs(q3[7] - q0[7]) < beta30 &&
                abs(p0[7] - q0[7]) < tc254 && (d04 << 1) < beta20 &&
                (d34 << 1) < beta20);

        cmp0 = (v2i64) __msa_fill_w(flag0);
        cmp1 = (v2i64) __msa_fill_w(flag1);
        cmp2 = (v2i64) __msa_ilvev_w((v4i32) cmp1, (v4i32) cmp0);
        cmp2 = (v2i64) __msa_ceqi_w((v4i32) cmp2, 0);

        if (flag0 && flag1) { /* strong only */
            /* strong filter */
            tc_pos <<= 1;
            tc_neg = -tc_pos;

            /* p part */
            temp0 = (p1_src + p0_src + q0_src);
            temp1 = ((p3_src + p2_src) << 1) + p2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst0 = (v16u8) (temp2 + (v8i16) p2_src);

            temp1 = temp0 + p2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - p1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst1 = (v16u8) (temp2 + (v8i16) p1_src);

            temp1 = (temp0 << 1) + p2_src + q1_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst2 = (v16u8) (temp2 + (v8i16) p0_src);

            dst0 = __msa_bmz_v(dst0, (v16u8) p2_src, (v16u8) p_is_pcm_vec);
            dst1 = __msa_bmz_v(dst1, (v16u8) p1_src, (v16u8) p_is_pcm_vec);
            dst2 = __msa_bmz_v(dst2, (v16u8) p0_src, (v16u8) p_is_pcm_vec);

            /* q part */
            temp0 = (q1_src + p0_src + q0_src);

            temp1 = ((q3_src + q2_src) << 1) + q2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst5 = (v16u8) (temp2 + (v8i16) q2_src);

            temp1 = temp0 + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - q1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst4 = (v16u8) (temp2 + (v8i16) q1_src);

            temp1 = (temp0 << 1) + p1_src + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst3 = (v16u8) (temp2 + (v8i16) q0_src);

            dst3 = __msa_bmz_v(dst3, (v16u8) q0_src, (v16u8) q_is_pcm_vec);
            dst4 = __msa_bmz_v(dst4, (v16u8) q1_src, (v16u8) q_is_pcm_vec);
            dst5 = __msa_bmz_v(dst5, (v16u8) q2_src, (v16u8) q_is_pcm_vec);

            /* pack results to 8 bit */
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
            dst2 = (v16u8) __msa_pckev_b((v16i8) dst5, (v16i8) dst4);

            /* pack src to 8 bit */
            PCKEV_B2_UB(p1_src, p2_src, q0_src, p0_src, dst3, dst4);
            dst5 = (v16u8) __msa_pckev_b((v16i8) q2_src, (v16i8) q1_src);

            dst0 = __msa_bmz_v(dst0, dst3, (v16u8) cmp3);
            dst1 = __msa_bmz_v(dst1, dst4, (v16u8) cmp3);
            dst2 = __msa_bmz_v(dst2, dst5, (v16u8) cmp3);

            dst_val0 = __msa_copy_u_d((v2i64) dst2, 0);
            dst_val1 = __msa_copy_u_d((v2i64) dst2, 1);

            ST8x4_UB(dst0, dst1, p2, stride);
            p2 += (4 * stride);
            SD(dst_val0, p2);
            p2 += stride;
            SD(dst_val1, p2);
            /* strong filter ends */
        } else if (flag0 == flag1) { /* weak only */
            /* weak filter */
            tc_neg = -tc_pos;

            diff0 = (v8i16) (q0_src - p0_src);
            diff1 = (v8i16) (q1_src - p1_src);
            diff0 = (diff0 << 3) + diff0;
            diff1 = (diff1 << 1) + diff1;
            delta0 = diff0 - diff1;
            delta0 = __msa_srari_h(delta0, 4);

            temp1 = (v8u16) ((tc_pos << 3) + (tc_pos << 1));
            abs_delta0 = __msa_add_a_h(delta0, (v8i16) zero);
            abs_delta0 = (v8u16) abs_delta0 < temp1;

            delta0 = CLIP_SH(delta0, tc_neg, tc_pos);

            temp0 = (v8u16) (delta0 + p0_src);
            temp0 = (v8u16) CLIP_SH_0_255(temp0);
            temp0 = (v8u16) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                        (v16u8) p_is_pcm_vec);

            temp2 = (v8i16) (q0_src - delta0);
            temp2 = CLIP_SH_0_255(temp2);
            temp2 = (v8i16) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                        (v16u8) q_is_pcm_vec);

            p_is_pcm_vec = ~p_is_pcm_vec;
            q_is_pcm_vec = ~q_is_pcm_vec;
            tmp = (beta + (beta >> 1)) >> 3;
            cmp0 = __msa_fill_d(dp00 + dp30 < tmp);
            cmp1 = __msa_fill_d(dp04 + dp34 < tmp);
            cmp0 = __msa_ilvev_d(cmp1, cmp0);
            cmp0 = __msa_ceqi_d(cmp0, 0);
            p_is_pcm_vec = p_is_pcm_vec | cmp0;

            cmp0 = __msa_fill_d(dq00 + dq30 < tmp);
            cmp1 = __msa_fill_d(dq04 + dq34 < tmp);
            cmp0 = __msa_ilvev_d(cmp1, cmp0);
            cmp0 = __msa_ceqi_d(cmp0, 0);
            q_is_pcm_vec = q_is_pcm_vec | cmp0;

            tc_pos >>= 1;
            tc_neg = -tc_pos;

            delta1 = (v8i16) __msa_aver_u_h(p2_src, p0_src);
            delta1 -= (v8i16) p1_src;
            delta1 += delta0;
            delta1 >>= 1;
            delta1 = CLIP_SH(delta1, tc_neg, tc_pos);
            delta1 = (v8i16) p1_src + (v8i16) delta1;
            delta1 = CLIP_SH_0_255(delta1);
            delta1 = (v8i16) __msa_bmnz_v((v16u8) delta1, (v16u8) p1_src,
                                          (v16u8) p_is_pcm_vec);

            delta2 = (v8i16) __msa_aver_u_h(q0_src, q2_src);
            delta2 = delta2 - (v8i16) q1_src;
            delta2 = delta2 - delta0;
            delta2 = delta2 >> 1;
            delta2 = CLIP_SH(delta2, tc_neg, tc_pos);
            delta2 = (v8i16) q1_src + (v8i16) delta2;
            delta2 = CLIP_SH_0_255(delta2);
            delta2 = (v8i16) __msa_bmnz_v((v16u8) delta2, (v16u8) q1_src,
                                          (v16u8) q_is_pcm_vec);

            dst1 = (v16u8) __msa_bmz_v((v16u8) delta1, (v16u8) p1_src,
                                       (v16u8) abs_delta0);
            dst2 = (v16u8) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                       (v16u8) abs_delta0);
            dst3 = (v16u8) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                       (v16u8) abs_delta0);
            dst4 = (v16u8) __msa_bmz_v((v16u8) delta2, (v16u8) q1_src,
                                       (v16u8) abs_delta0);
            /* pack results to 8 bit */
            PCKEV_B2_UB(dst2, dst1, dst4, dst3, dst0, dst1);

            /* pack src to 8 bit */
            PCKEV_B2_UB(p0_src, p1_src, q1_src, q0_src, dst2, dst3);

            dst0 = __msa_bmz_v(dst0, dst2, (v16u8) cmp3);
            dst1 = __msa_bmz_v(dst1, dst3, (v16u8) cmp3);

            p2 += stride;
            ST8x4_UB(dst0, dst1, p2, stride);
            /* weak filter ends */
        } else { /* strong + weak */
            /* strong filter */
            tc_pos <<= 1;
            tc_neg = -tc_pos;

            /* p part */
            temp0 = (p1_src + p0_src + q0_src);
            temp1 = ((p3_src + p2_src) << 1) + p2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst0 = (v16u8) (temp2 + (v8i16) p2_src);

            temp1 = temp0 + p2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - p1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst1 = (v16u8) (temp2 + (v8i16) p1_src);

            temp1 = (temp0 << 1) + p2_src + q1_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst2 = (v16u8) (temp2 + (v8i16) p0_src);

            dst0 = __msa_bmz_v(dst0, (v16u8) p2_src, (v16u8) p_is_pcm_vec);
            dst1 = __msa_bmz_v(dst1, (v16u8) p1_src, (v16u8) p_is_pcm_vec);
            dst2 = __msa_bmz_v(dst2, (v16u8) p0_src, (v16u8) p_is_pcm_vec);

            /* q part */
            temp0 = (q1_src + p0_src + q0_src);

            temp1 = ((q3_src + q2_src) << 1) + q2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst5 = (v16u8) (temp2 + (v8i16) q2_src);

            temp1 = temp0 + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - q1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst4 = (v16u8) (temp2 + (v8i16) q1_src);

            temp1 = (temp0 << 1) + p1_src + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst3 = (v16u8) (temp2 + (v8i16) q0_src);

            dst3 = __msa_bmz_v(dst3, (v16u8) q0_src, (v16u8) q_is_pcm_vec);
            dst4 = __msa_bmz_v(dst4, (v16u8) q1_src, (v16u8) q_is_pcm_vec);
            dst5 = __msa_bmz_v(dst5, (v16u8) q2_src, (v16u8) q_is_pcm_vec);

            /* pack strong results to 8 bit */
            PCKEV_B2_UB(dst1, dst0, dst3, dst2, dst0, dst1);
            dst2 = (v16u8) __msa_pckev_b((v16i8) dst5, (v16i8) dst4);
            /* strong filter ends */

            /* weak filter */
            tc_pos >>= 1;
            tc_neg = -tc_pos;

            diff0 = (v8i16) (q0_src - p0_src);
            diff1 = (v8i16) (q1_src - p1_src);
            diff0 = (diff0 << 3) + diff0;
            diff1 = (diff1 << 1) + diff1;
            delta0 = diff0 - diff1;
            delta0 = __msa_srari_h(delta0, 4);

            temp1 = (v8u16) ((tc_pos << 3) + (tc_pos << 1));
            abs_delta0 = __msa_add_a_h(delta0, (v8i16) zero);
            abs_delta0 = (v8u16) abs_delta0 < temp1;

            delta0 = CLIP_SH(delta0, tc_neg, tc_pos);

            temp0 = (v8u16) (delta0 + p0_src);
            temp0 = (v8u16) CLIP_SH_0_255(temp0);
            temp0 = (v8u16) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                        (v16u8) p_is_pcm_vec);

            temp2 = (v8i16) (q0_src - delta0);
            temp2 = CLIP_SH_0_255(temp2);
            temp2 = (v8i16) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                        (v16u8) q_is_pcm_vec);

            p_is_pcm_vec = ~p_is_pcm_vec;
            q_is_pcm_vec = ~q_is_pcm_vec;
            tmp = (beta + (beta >> 1)) >> 3;
            cmp0 = __msa_fill_d(dp00 + dp30 < tmp);
            cmp1 = __msa_fill_d(dp04 + dp34 < tmp);
            cmp0 = __msa_ilvev_d(cmp1, cmp0);
            p_is_pcm_vec = p_is_pcm_vec | __msa_ceqi_d(cmp0, 0);

            cmp0 = __msa_fill_d(dq00 + dq30 < tmp);
            cmp1 = __msa_fill_d(dq04 + dq34 < tmp);
            cmp0 = __msa_ilvev_d(cmp1, cmp0);
            q_is_pcm_vec = q_is_pcm_vec | __msa_ceqi_d(cmp0, 0);

            tc_pos >>= 1;
            tc_neg = -tc_pos;

            delta1 = (v8i16) __msa_aver_u_h(p2_src, p0_src);
            delta1 -= (v8i16) p1_src;
            delta1 += delta0;
            delta1 >>= 1;
            delta1 = CLIP_SH(delta1, tc_neg, tc_pos);
            delta1 = (v8i16) p1_src + (v8i16) delta1;
            delta1 = CLIP_SH_0_255(delta1);
            delta1 = (v8i16) __msa_bmnz_v((v16u8) delta1, (v16u8) p1_src,
                                          (v16u8) p_is_pcm_vec);

            delta2 = (v8i16) __msa_aver_u_h(q0_src, q2_src);
            delta2 = delta2 - (v8i16) q1_src;
            delta2 = delta2 - delta0;
            delta2 = delta2 >> 1;
            delta2 = CLIP_SH(delta2, tc_neg, tc_pos);
            delta2 = (v8i16) q1_src + (v8i16) delta2;
            delta2 = CLIP_SH_0_255(delta2);
            delta2 = (v8i16) __msa_bmnz_v((v16u8) delta2, (v16u8) q1_src,
                                          (v16u8) q_is_pcm_vec);

            delta1 = (v8i16) __msa_bmz_v((v16u8) delta1, (v16u8) p1_src,
                                         (v16u8) abs_delta0);
            temp0 = (v8u16) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                        (v16u8) abs_delta0);
            temp2 = (v8i16) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                        (v16u8) abs_delta0);
            delta2 = (v8i16) __msa_bmz_v((v16u8) delta2, (v16u8) q1_src,
                                         (v16u8) abs_delta0);
            /* weak filter ends */

            /* pack weak results to 8 bit */
            PCKEV_B2_UB(delta1, p2_src, temp2, temp0, dst3, dst4);
            dst5 = (v16u8) __msa_pckev_b((v16i8) q2_src, (v16i8) delta2);

            /* select between weak or strong */
            dst0 = __msa_bmnz_v(dst0, dst3, (v16u8) cmp2);
            dst1 = __msa_bmnz_v(dst1, dst4, (v16u8) cmp2);
            dst2 = __msa_bmnz_v(dst2, dst5, (v16u8) cmp2);

            /* pack src to 8 bit */
            PCKEV_B2_UB(p1_src, p2_src, q0_src, p0_src, dst3, dst4);
            dst5 = (v16u8) __msa_pckev_b((v16i8) q2_src, (v16i8) q1_src);

            dst0 = __msa_bmz_v(dst0, dst3, (v16u8) cmp3);
            dst1 = __msa_bmz_v(dst1, dst4, (v16u8) cmp3);
            dst2 = __msa_bmz_v(dst2, dst5, (v16u8) cmp3);

            dst_val0 = __msa_copy_u_d((v2i64) dst2, 0);
            dst_val1 = __msa_copy_u_d((v2i64) dst2, 1);

            ST8x4_UB(dst0, dst1, p2, stride);
            p2 += (4 * stride);
            SD(dst_val0, p2);
            p2 += stride;
            SD(dst_val1, p2);
        }
    }
}

static void hevc_loopfilter_luma_ver_msa(uint8_t *src, int32_t stride,
                                         int32_t beta, int32_t *tc,
                                         uint8_t *p_is_pcm, uint8_t *q_is_pcm)
{
    uint8_t *p3 = src;
    uint8_t *p2 = src + 3 * stride;
    uint8_t *p1 = src + (stride << 2);
    uint8_t *p0 = src + 7 * stride;
    uint8_t flag0, flag1;
    uint16_t tmp0, tmp1;
    uint32_t tmp2, tmp3;
    int32_t dp00, dq00, dp30, dq30, d00, d30;
    int32_t d0030, d0434;
    int32_t dp04, dq04, dp34, dq34, d04, d34;
    int32_t tc0, p_is_pcm0, q_is_pcm0, beta30, beta20, tc250;
    int32_t tc4, p_is_pcm4, q_is_pcm4, tc254, tmp;
    v16u8 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v2i64 cmp0, cmp1, cmp2, p_is_pcm_vec, q_is_pcm_vec;
    v2i64 cmp3;
    v8u16 temp0, temp1;
    v8i16 temp2;
    v8i16 tc_pos, tc_neg;
    v8i16 diff0, diff1, delta0, delta1, delta2, abs_delta0;
    v16i8 zero = { 0 };
    v8u16 p3_src, p2_src, p1_src, p0_src, q0_src, q1_src, q2_src, q3_src;

    dp00 = abs(p3[-3] - (p3[-2] << 1) + p3[-1]);
    dq00 = abs(p3[2] - (p3[1] << 1) + p3[0]);
    dp30 = abs(p2[-3] - (p2[-2] << 1) + p2[-1]);
    dq30 = abs(p2[2] - (p2[1] << 1) + p2[0]);
    d00 = dp00 + dq00;
    d30 = dp30 + dq30;
    p_is_pcm0 = p_is_pcm[0];
    q_is_pcm0 = q_is_pcm[0];

    dp04 = abs(p1[-3] - (p1[-2] << 1) + p1[-1]);
    dq04 = abs(p1[2] - (p1[1] << 1) + p1[0]);
    dp34 = abs(p0[-3] - (p0[-2] << 1) + p0[-1]);
    dq34 = abs(p0[2] - (p0[1] << 1) + p0[0]);
    d04 = dp04 + dq04;
    d34 = dp34 + dq34;
    p_is_pcm4 = p_is_pcm[1];
    q_is_pcm4 = q_is_pcm[1];

    cmp0 = __msa_fill_d(p_is_pcm0);
    cmp1 = __msa_fill_d(p_is_pcm4);
    p_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
    p_is_pcm_vec = __msa_ceqi_d(p_is_pcm_vec, 0);

    d0030 = (d00 + d30) >= beta;
    d0434 = (d04 + d34) >= beta;

    cmp0 = __msa_fill_d(d0030);
    cmp1 = __msa_fill_d(d0434);
    cmp3 = __msa_ilvev_d(cmp1, cmp0);
    cmp3 = (v2i64) __msa_ceqi_d(cmp3, 0);

    if ((!p_is_pcm0 || !p_is_pcm4 || !q_is_pcm0 || !q_is_pcm4) &&
        (!d0030 || !d0434)) {
        src -= 4;
        LD_UH8(src, stride, p3_src, p2_src, p1_src, p0_src, q0_src, q1_src,
               q2_src, q3_src);

        cmp0 = __msa_fill_d(q_is_pcm0);
        cmp1 = __msa_fill_d(q_is_pcm4);
        q_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
        q_is_pcm_vec = __msa_ceqi_d(q_is_pcm_vec, 0);

        tc0 = tc[0];
        beta30 = beta >> 3;
        beta20 = beta >> 2;
        tc250 = ((tc0 * 5 + 1) >> 1);

        tc4 = tc[1];
        tc254 = ((tc4 * 5 + 1) >> 1);
        cmp0 = (v2i64) __msa_fill_h(tc0 << 1);
        cmp1 = (v2i64) __msa_fill_h(tc4 << 1);
        tc_pos = (v8i16) __msa_ilvev_d(cmp1, cmp0);

        TRANSPOSE8x8_UB_UH(p3_src, p2_src, p1_src, p0_src, q0_src, q1_src,
                           q2_src, q3_src, p3_src, p2_src, p1_src, p0_src,
                           q0_src, q1_src, q2_src, q3_src);

        flag0 = abs(p3[-4] - p3[-1]) + abs(p3[3] - p3[0]) < beta30 &&
                abs(p3[-1] - p3[0]) < tc250;
        flag0 = flag0 && (abs(p2[-4] - p2[-1]) + abs(p2[3] - p2[0]) < beta30 &&
                abs(p2[-1] - p2[0]) < tc250 && (d00 << 1) < beta20 &&
                (d30 << 1) < beta20);
        cmp0 = __msa_fill_d(flag0);
        ILVR_B4_UH(zero, p3_src, zero, p2_src, zero, p1_src, zero, p0_src,
                   p3_src, p2_src, p1_src, p0_src);

        flag1 = abs(p1[-4] - p1[-1]) + abs(p1[3] - p1[0]) < beta30 &&
                abs(p1[-1] - p1[0]) < tc254;
        flag1 = flag1 && (abs(p0[-4] - p0[-1]) + abs(p0[3] - p0[0]) < beta30 &&
                abs(p0[-1] - p0[0]) < tc254 && (d04 << 1) < beta20 &&
                (d34 << 1) < beta20);
        ILVR_B4_UH(zero, q0_src, zero, q1_src, zero, q2_src, zero, q3_src,
                   q0_src, q1_src, q2_src, q3_src);

        cmp1 = __msa_fill_d(flag1);
        cmp2 = __msa_ilvev_d(cmp1, cmp0);
        cmp2 = __msa_ceqi_d(cmp2, 0);

        if (flag0 && flag1) { /* strong only */
            /* strong filter */
            tc_neg = -tc_pos;

            /* p part */
            temp0 = (p1_src + p0_src + q0_src);

            temp1 = ((p3_src + p2_src) << 1) + p2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst0 = (v16u8) (temp2 + (v8i16) p2_src);

            temp1 = temp0 + p2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - p1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst1 = (v16u8) (temp2 + (v8i16) p1_src);

            temp1 = (temp0 << 1) + p2_src + q1_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst2 = (v16u8) (temp2 + (v8i16) p0_src);

            dst0 = __msa_bmz_v(dst0, (v16u8) p2_src, (v16u8) p_is_pcm_vec);
            dst1 = __msa_bmz_v(dst1, (v16u8) p1_src, (v16u8) p_is_pcm_vec);
            dst2 = __msa_bmz_v(dst2, (v16u8) p0_src, (v16u8) p_is_pcm_vec);

            /* q part */
            temp0 = (q1_src + p0_src + q0_src);
            temp1 = ((q3_src + q2_src) << 1) + q2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst5 = (v16u8) (temp2 + (v8i16) q2_src);

            temp1 = temp0 + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - q1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst4 = (v16u8) (temp2 + (v8i16) q1_src);

            temp1 = (temp0 << 1) + p1_src + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst3 = (v16u8) (temp2 + (v8i16) q0_src);

            dst3 = __msa_bmz_v(dst3, (v16u8) q0_src, (v16u8) q_is_pcm_vec);
            dst4 = __msa_bmz_v(dst4, (v16u8) q1_src, (v16u8) q_is_pcm_vec);
            dst5 = __msa_bmz_v(dst5, (v16u8) q2_src, (v16u8) q_is_pcm_vec);
            /* strong filter ends */
        } else if (flag0 == flag1) { /* weak only */
            /* weak filter */
            tc_pos >>= 1;
            tc_neg = -tc_pos;

            diff0 = (v8i16) (q0_src - p0_src);
            diff1 = (v8i16) (q1_src - p1_src);
            diff0 = (diff0 << 3) + diff0;
            diff1 = (diff1 << 1) + diff1;
            delta0 = diff0 - diff1;
            delta0 = __msa_srari_h(delta0, 4);

            temp1 = (v8u16) ((tc_pos << 3) + (tc_pos << 1));
            abs_delta0 = __msa_add_a_h(delta0, (v8i16) zero);
            abs_delta0 = (v8u16) abs_delta0 < temp1;

            delta0 = CLIP_SH(delta0, tc_neg, tc_pos);
            temp0 = (v8u16) (delta0 + p0_src);
            temp0 = (v8u16) CLIP_SH_0_255(temp0);
            temp0 = (v8u16) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                        (v16u8) p_is_pcm_vec);

            temp2 = (v8i16) (q0_src - delta0);
            temp2 = CLIP_SH_0_255(temp2);
            temp2 = (v8i16) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                        (v16u8) q_is_pcm_vec);

            tmp = ((beta + (beta >> 1)) >> 3);
            cmp0 = __msa_fill_d(!p_is_pcm0 && ((dp00 + dp30) < tmp));
            cmp1 = __msa_fill_d(!p_is_pcm4 && ((dp04 + dp34) < tmp));
            p_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
            p_is_pcm_vec = __msa_ceqi_d(p_is_pcm_vec, 0);

            cmp0 = (v2i64) __msa_fill_h((!q_is_pcm0) && (dq00 + dq30 < tmp));
            cmp1 = (v2i64) __msa_fill_h((!q_is_pcm4) && (dq04 + dq34 < tmp));
            q_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
            q_is_pcm_vec = __msa_ceqi_d(q_is_pcm_vec, 0);

            tc_pos >>= 1;
            tc_neg = -tc_pos;

            delta1 = (v8i16) __msa_aver_u_h(p2_src, p0_src);
            delta1 -= (v8i16) p1_src;
            delta1 += delta0;
            delta1 >>= 1;
            delta1 = CLIP_SH(delta1, tc_neg, tc_pos);
            delta1 = (v8i16) p1_src + (v8i16) delta1;
            delta1 = CLIP_SH_0_255(delta1);
            delta1 = (v8i16) __msa_bmnz_v((v16u8) delta1, (v16u8) p1_src,
                                          (v16u8) p_is_pcm_vec);

            delta2 = (v8i16) __msa_aver_u_h(q0_src, q2_src);
            delta2 = delta2 - (v8i16) q1_src;
            delta2 = delta2 - delta0;
            delta2 = delta2 >> 1;
            delta2 = CLIP_SH(delta2, tc_neg, tc_pos);
            delta2 = (v8i16) q1_src + (v8i16) delta2;
            delta2 = CLIP_SH_0_255(delta2);
            delta2 = (v8i16) __msa_bmnz_v((v16u8) delta2, (v16u8) q1_src,
                                          (v16u8) q_is_pcm_vec);

            dst0 = __msa_bmz_v((v16u8) delta1, (v16u8) p1_src,
                               (v16u8) abs_delta0);
            dst1 = __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                               (v16u8) abs_delta0);
            dst2 = __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                               (v16u8) abs_delta0);
            dst3 = __msa_bmz_v((v16u8) delta2, (v16u8) q1_src,
                               (v16u8) abs_delta0);
            /* weak filter ends */

            dst0 = __msa_bmz_v(dst0, (v16u8) p1_src, (v16u8) cmp3);
            dst1 = __msa_bmz_v(dst1, (v16u8) p0_src, (v16u8) cmp3);
            dst2 = __msa_bmz_v(dst2, (v16u8) q0_src, (v16u8) cmp3);
            dst3 = __msa_bmz_v(dst3, (v16u8) q1_src, (v16u8) cmp3);

            PCKEV_B2_UB(dst2, dst0, dst3, dst1, dst0, dst1);

            /* transpose */
            ILVRL_B2_UB(dst1, dst0, dst4, dst5);
            ILVRL_H2_UB(dst5, dst4, dst0, dst1);

            src += 2;

            tmp2 = __msa_copy_u_w((v4i32) dst0, 0);
            tmp3 = __msa_copy_u_w((v4i32) dst0, 1);
            SW(tmp2, src);
            src += stride;
            SW(tmp3, src);
            src += stride;

            tmp2 = __msa_copy_u_w((v4i32) dst0, 2);
            tmp3 = __msa_copy_u_w((v4i32) dst0, 3);
            SW(tmp2, src);
            src += stride;
            SW(tmp3, src);
            src += stride;

            tmp2 = __msa_copy_u_w((v4i32) dst1, 0);
            tmp3 = __msa_copy_u_w((v4i32) dst1, 1);
            SW(tmp2, src);
            src += stride;
            SW(tmp3, src);
            src += stride;

            tmp2 = __msa_copy_u_w((v4i32) dst1, 2);
            tmp3 = __msa_copy_u_w((v4i32) dst1, 3);
            SW(tmp2, src);
            src += stride;
            SW(tmp3, src);

            return;
        } else { /* strong + weak */
            /* strong filter */
            tc_neg = -tc_pos;

            /* p part */
            temp0 = (p1_src + p0_src + q0_src);

            temp1 = ((p3_src + p2_src) << 1) + p2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst0 = (v16u8) (temp2 + (v8i16) p2_src);

            temp1 = temp0 + p2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - p1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst1 = (v16u8) (temp2 + (v8i16) p1_src);

            temp1 = (temp0 << 1) + p2_src + q1_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - p0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst2 = (v16u8) (temp2 + (v8i16) p0_src);

            dst0 = __msa_bmz_v(dst0, (v16u8) p2_src, (v16u8) p_is_pcm_vec);
            dst1 = __msa_bmz_v(dst1, (v16u8) p1_src, (v16u8) p_is_pcm_vec);
            dst2 = __msa_bmz_v(dst2, (v16u8) p0_src, (v16u8) p_is_pcm_vec);

            /* q part */
            temp0 = (q1_src + p0_src + q0_src);
            temp1 = ((q3_src + q2_src) << 1) + q2_src + temp0;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q2_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst5 = (v16u8) (temp2 + (v8i16) q2_src);

            temp1 = temp0 + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 2);
            temp2 = (v8i16) (temp1 - q1_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst4 = (v16u8) (temp2 + (v8i16) q1_src);

            temp1 = (temp0 << 1) + p1_src + q2_src;
            temp1 = (v8u16) __msa_srari_h((v8i16) temp1, 3);
            temp2 = (v8i16) (temp1 - q0_src);
            temp2 = CLIP_SH(temp2, tc_neg, tc_pos);
            dst3 = (v16u8) (temp2 + (v8i16) q0_src);

            dst3 = __msa_bmz_v(dst3, (v16u8) q0_src, (v16u8) q_is_pcm_vec);
            dst4 = __msa_bmz_v(dst4, (v16u8) q1_src, (v16u8) q_is_pcm_vec);
            dst5 = __msa_bmz_v(dst5, (v16u8) q2_src, (v16u8) q_is_pcm_vec);
            /* strong filter ends */

            /* weak filter */
            tc_pos >>= 1;
            tc_neg = -tc_pos;

            diff0 = (v8i16) (q0_src - p0_src);
            diff1 = (v8i16) (q1_src - p1_src);
            diff0 = (diff0 << 3) + diff0;
            diff1 = (diff1 << 1) + diff1;
            delta0 = diff0 - diff1;
            delta0 = __msa_srari_h(delta0, 4);

            temp1 = (v8u16) ((tc_pos << 3) + (tc_pos << 1));
            abs_delta0 = __msa_add_a_h(delta0, (v8i16) zero);
            abs_delta0 = (v8u16) abs_delta0 < temp1;

            delta0 = CLIP_SH(delta0, tc_neg, tc_pos);

            temp0 = (v8u16) (delta0 + p0_src);
            temp0 = (v8u16) CLIP_SH_0_255(temp0);
            temp0 = (v8u16) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                        (v16u8) p_is_pcm_vec);

            temp2 = (v8i16) (q0_src - delta0);
            temp2 = CLIP_SH_0_255(temp2);
            temp2 = (v8i16) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                        (v16u8) q_is_pcm_vec);

            tmp = (beta + (beta >> 1)) >> 3;
            cmp0 = __msa_fill_d(!p_is_pcm0 && ((dp00 + dp30) < tmp));
            cmp1 = __msa_fill_d(!p_is_pcm4 && ((dp04 + dp34) < tmp));
            p_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
            p_is_pcm_vec = __msa_ceqi_d(p_is_pcm_vec, 0);

            cmp0 = (v2i64) __msa_fill_h((!q_is_pcm0) && (dq00 + dq30 < tmp));
            cmp1 = (v2i64) __msa_fill_h((!q_is_pcm4) && (dq04 + dq34 < tmp));
            q_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
            q_is_pcm_vec = __msa_ceqi_d(q_is_pcm_vec, 0);

            tc_pos >>= 1;
            tc_neg = -tc_pos;

            delta1 = (v8i16) __msa_aver_u_h(p2_src, p0_src);
            delta1 -= (v8i16) p1_src;
            delta1 += delta0;
            delta1 >>= 1;
            delta1 = CLIP_SH(delta1, tc_neg, tc_pos);
            delta1 = (v8i16) p1_src + (v8i16) delta1;
            delta1 = CLIP_SH_0_255(delta1);
            delta1 = (v8i16) __msa_bmnz_v((v16u8) delta1, (v16u8) p1_src,
                                          (v16u8) p_is_pcm_vec);

            delta2 = (v8i16) __msa_aver_u_h(q0_src, q2_src);
            delta2 = delta2 - (v8i16) q1_src;
            delta2 = delta2 - delta0;
            delta2 = delta2 >> 1;
            delta2 = CLIP_SH(delta2, tc_neg, tc_pos);
            delta2 = (v8i16) q1_src + (v8i16) delta2;
            delta2 = CLIP_SH_0_255(delta2);
            delta2 = (v8i16) __msa_bmnz_v((v16u8) delta2, (v16u8) q1_src,
                                          (v16u8) q_is_pcm_vec);
            delta1 = (v8i16) __msa_bmz_v((v16u8) delta1, (v16u8) p1_src,
                                         (v16u8) abs_delta0);
            temp0 = (v8u16) __msa_bmz_v((v16u8) temp0, (v16u8) p0_src,
                                        (v16u8) abs_delta0);
            temp2 = (v8i16) __msa_bmz_v((v16u8) temp2, (v16u8) q0_src,
                                        (v16u8) abs_delta0);
            delta2 = (v8i16) __msa_bmz_v((v16u8) delta2, (v16u8) q1_src,
                                         (v16u8) abs_delta0);
            /* weak filter ends*/

            /* select between weak or strong */
            dst2 = __msa_bmnz_v(dst2, (v16u8) temp0, (v16u8) cmp2);
            dst3 = __msa_bmnz_v(dst3, (v16u8) temp2, (v16u8) cmp2);
            dst1 = __msa_bmnz_v(dst1, (v16u8) delta1, (v16u8) cmp2);
            dst4 = __msa_bmnz_v(dst4, (v16u8) delta2, (v16u8) cmp2);
            dst0 = __msa_bmnz_v(dst0, (v16u8) p2_src, (v16u8) cmp2);
            dst5 = __msa_bmnz_v(dst5, (v16u8) q2_src, (v16u8) cmp2);
        }

        dst0 = __msa_bmz_v(dst0, (v16u8) p2_src, (v16u8) cmp3);
        dst1 = __msa_bmz_v(dst1, (v16u8) p1_src, (v16u8) cmp3);
        dst2 = __msa_bmz_v(dst2, (v16u8) p0_src, (v16u8) cmp3);
        dst3 = __msa_bmz_v(dst3, (v16u8) q0_src, (v16u8) cmp3);
        dst4 = __msa_bmz_v(dst4, (v16u8) q1_src, (v16u8) cmp3);
        dst5 = __msa_bmz_v(dst5, (v16u8) q2_src, (v16u8) cmp3);

        /* pack results to 8 bit */
        PCKEV_B4_UB(dst2, dst0, dst3, dst1, dst4, dst4, dst5, dst5, dst0, dst1,
                    dst2, dst3);

        /* transpose */
        ILVRL_B2_UB(dst1, dst0, dst4, dst5);
        ILVRL_B2_UB(dst3, dst2, dst6, dst7);
        ILVRL_H2_UB(dst5, dst4, dst0, dst1);
        ILVRL_H2_UB(dst7, dst6, dst2, dst3);

        src += 1;

        tmp2 = __msa_copy_u_w((v4i32) dst0, 0);
        tmp3 = __msa_copy_u_w((v4i32) dst0, 1);
        tmp0 = __msa_copy_u_h((v8i16) dst2, 0);
        tmp1 = __msa_copy_u_h((v8i16) dst2, 2);
        SW(tmp2, src);
        SH(tmp0, src + 4);
        src += stride;
        SW(tmp3, src);
        SH(tmp1, src + 4);
        src += stride;

        tmp2 = __msa_copy_u_w((v4i32) dst0, 2);
        tmp3 = __msa_copy_u_w((v4i32) dst0, 3);
        tmp0 = __msa_copy_u_h((v8i16) dst2, 4);
        tmp1 = __msa_copy_u_h((v8i16) dst2, 6);
        SW(tmp2, src);
        SH(tmp0, src + 4);
        src += stride;
        SW(tmp3, src);
        SH(tmp1, src + 4);
        src += stride;

        tmp2 = __msa_copy_u_w((v4i32) dst1, 0);
        tmp3 = __msa_copy_u_w((v4i32) dst1, 1);
        tmp0 = __msa_copy_u_h((v8i16) dst3, 0);
        tmp1 = __msa_copy_u_h((v8i16) dst3, 2);
        SW(tmp2, src);
        SH(tmp0, src + 4);
        src += stride;
        SW(tmp3, src);
        SH(tmp1, src + 4);
        src += stride;

        tmp2 = __msa_copy_u_w((v4i32) dst1, 2);
        tmp3 = __msa_copy_u_w((v4i32) dst1, 3);
        tmp0 = __msa_copy_u_h((v8i16) dst3, 4);
        tmp1 = __msa_copy_u_h((v8i16) dst3, 6);
        SW(tmp2, src);
        SH(tmp0, src + 4);
        src += stride;
        SW(tmp3, src);
        SH(tmp1, src + 4);
    }
}

static void hevc_loopfilter_chroma_hor_msa(uint8_t *src, int32_t stride,
                                           int32_t *tc, uint8_t *p_is_pcm,
                                           uint8_t *q_is_pcm)
{
    uint8_t *p1_ptr = src - (stride << 1);
    uint8_t *p0_ptr = src - stride;
    uint8_t *q0_ptr = src;
    uint8_t *q1_ptr = src + stride;
    v2i64 cmp0, cmp1, p_is_pcm_vec, q_is_pcm_vec;
    v8u16 p1, p0, q0, q1;
    v8i16 tc_pos, tc_neg;
    v16i8 zero = { 0 };
    v8i16 temp0, temp1, delta;

    if (!(tc[0] <= 0) || !(tc[1] <= 0)) {
        cmp0 = (v2i64) __msa_fill_h(tc[0]);
        cmp1 = (v2i64) __msa_fill_h(tc[1]);
        tc_pos = (v8i16) __msa_ilvev_d(cmp1, cmp0);
        tc_neg = -tc_pos;

        cmp0 = __msa_fill_d(p_is_pcm[0]);
        cmp1 = __msa_fill_d(p_is_pcm[1]);
        p_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
        p_is_pcm_vec = __msa_ceqi_d(p_is_pcm_vec, 0);

        cmp0 = __msa_fill_d(q_is_pcm[0]);
        cmp1 = __msa_fill_d(q_is_pcm[1]);
        q_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
        q_is_pcm_vec = __msa_ceqi_d(q_is_pcm_vec, 0);

        p1 = LD_UH(p1_ptr);
        p0 = LD_UH(p0_ptr);
        q0 = LD_UH(q0_ptr);
        q1 = LD_UH(q1_ptr);

        ILVR_B4_UH(zero, p1, zero, p0, zero, q0, zero, q1, p1, p0, q0, q1);

        temp0 = (v8i16) (q0 - p0);
        temp1 = (v8i16) (p1 - q1);
        temp0 <<= 2;
        temp0 += temp1;
        delta = __msa_srari_h((v8i16) temp0, 3);
        delta = CLIP_SH(delta, tc_neg, tc_pos);

        temp0 = (v8i16) ((v8i16) p0 + delta);
        temp0 = CLIP_SH_0_255(temp0);
        temp0 = (v8i16) __msa_bmz_v((v16u8) temp0, (v16u8) p0,
                                    (v16u8) p_is_pcm_vec);

        temp1 = (v8i16) ((v8i16) q0 - delta);
        temp1 = CLIP_SH_0_255(temp1);
        temp1 = (v8i16) __msa_bmz_v((v16u8) temp1, (v16u8) q0,
                                    (v16u8) q_is_pcm_vec);

        tc_pos = (v8i16) __msa_clei_s_d((v2i64) tc_pos, 0);
        temp0 = (v8i16) __msa_bmnz_v((v16u8) temp0, (v16u8) p0, (v16u8) tc_pos);
        temp1 = (v8i16) __msa_bmnz_v((v16u8) temp1, (v16u8) q0, (v16u8) tc_pos);

        temp0 = (v8i16) __msa_pckev_b((v16i8) temp1, (v16i8) temp0);
        ST8x2_UB(temp0, p0_ptr, stride);
    }
}

static void hevc_loopfilter_chroma_ver_msa(uint8_t *src, int32_t stride,
                                           int32_t *tc, uint8_t *p_is_pcm,
                                           uint8_t *q_is_pcm)
{
    v2i64 cmp0, cmp1, p_is_pcm_vec, q_is_pcm_vec;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8u16 p1, p0, q0, q1;
    v8i16 tc_pos, tc_neg;
    v16i8 zero = { 0 };
    v8i16 temp0, temp1, delta;

    if (!(tc[0] <= 0) || !(tc[1] <= 0)) {
        cmp0 = (v2i64) __msa_fill_h(tc[0]);
        cmp1 = (v2i64) __msa_fill_h(tc[1]);
        tc_pos = (v8i16) __msa_ilvev_d(cmp1, cmp0);
        tc_neg = -tc_pos;

        cmp0 = __msa_fill_d(p_is_pcm[0]);
        cmp1 = __msa_fill_d(p_is_pcm[1]);
        p_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
        p_is_pcm_vec = __msa_ceqi_d(p_is_pcm_vec, 0);

        cmp0 = __msa_fill_d(q_is_pcm[0]);
        cmp1 = __msa_fill_d(q_is_pcm[1]);
        q_is_pcm_vec = __msa_ilvev_d(cmp1, cmp0);
        q_is_pcm_vec = __msa_ceqi_d(q_is_pcm_vec, 0);

        src -= 2;
        LD_UB8(src, stride, src0, src1, src2, src3, src4, src5, src6, src7);
        TRANSPOSE8x4_UB_UH(src0, src1, src2, src3, src4, src5, src6, src7,
                           p1, p0, q0, q1);
        ILVR_B4_UH(zero, p1, zero, p0, zero, q0, zero, q1, p1, p0, q0, q1);

        temp0 = (v8i16) (q0 - p0);
        temp1 = (v8i16) (p1 - q1);
        temp0 <<= 2;
        temp0 += temp1;
        delta = __msa_srari_h((v8i16) temp0, 3);
        delta = CLIP_SH(delta, tc_neg, tc_pos);

        temp0 = (v8i16) ((v8i16) p0 + delta);
        temp0 = CLIP_SH_0_255(temp0);
        temp0 = (v8i16) __msa_bmz_v((v16u8) temp0, (v16u8) p0,
                                    (v16u8) p_is_pcm_vec);

        temp1 = (v8i16) ((v8i16) q0 - delta);
        temp1 = CLIP_SH_0_255(temp1);
        temp1 = (v8i16) __msa_bmz_v((v16u8) temp1, (v16u8) q0,
                                    (v16u8) q_is_pcm_vec);

        tc_pos = (v8i16) __msa_clei_s_d((v2i64) tc_pos, 0);
        temp0 = (v8i16) __msa_bmnz_v((v16u8) temp0, (v16u8) p0, (v16u8) tc_pos);
        temp1 = (v8i16) __msa_bmnz_v((v16u8) temp1, (v16u8) q0, (v16u8) tc_pos);

        temp0 = (v8i16) __msa_ilvev_b((v16i8) temp1, (v16i8) temp0);

        src += 1;
        ST2x4_UB(temp0, 0, src, stride);
        src += (4 * stride);
        ST2x4_UB(temp0, 4, src, stride);
    }
}

static void hevc_sao_band_filter_4width_msa(uint8_t *dst, int32_t dst_stride,
                                            uint8_t *src, int32_t src_stride,
                                            int32_t sao_left_class,
                                            int16_t *sao_offset_val,
                                            int32_t height)
{
    v16u8 src0, src1, src2, src3;
    v16i8 src0_r, src1_r;
    v16i8 offset, offset_val, mask;
    v16i8 dst0, offset0, offset1;
    v16i8 zero = { 0 };

    offset_val = LD_SB(sao_offset_val + 1);
    offset_val = (v16i8) __msa_pckev_d((v2i64) offset_val, (v2i64) offset_val);

    offset_val = __msa_pckev_b(offset_val, offset_val);
    offset1 = (v16i8) __msa_insve_w((v4i32) zero, 3, (v4i32) offset_val);
    offset0 = __msa_sld_b(offset1, zero, 28 - ((sao_left_class) & 31));
    offset1 = __msa_sld_b(zero, offset1, 28 - ((sao_left_class) & 31));

    /* load in advance. */
    LD_UB4(src, src_stride, src0, src1, src2, src3);

    if (!((sao_left_class > 12) & (sao_left_class < 29))) {
        SWAP(offset0, offset1);
    }

    for (height -= 4; height; height -= 4) {
        src += (4 * src_stride);

        ILVEV_D2_SB(src0, src1, src2, src3, src0_r, src1_r);

        src0_r = (v16i8) __msa_pckev_w((v4i32) src1_r, (v4i32) src0_r);
        mask = __msa_srli_b(src0_r, 3);
        offset = __msa_vshf_b(mask, offset1, offset0);

        src0_r = (v16i8) __msa_xori_b((v16u8) src0_r, 128);
        dst0 = __msa_adds_s_b(src0_r, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        /* load in advance. */
        LD_UB4(src, src_stride, src0, src1, src2, src3);

        /* store results */
        ST4x4_UB(dst0, dst0, 0, 1, 2, 3, dst, dst_stride);
        dst += (4 * dst_stride);
    }

    ILVEV_D2_SB(src0, src1, src2, src3, src0_r, src1_r);

    src0_r = (v16i8) __msa_pckev_w((v4i32) src1_r, (v4i32) src0_r);
    mask = __msa_srli_b(src0_r, 3);
    offset = __msa_vshf_b(mask, offset1, offset0);

    src0_r = (v16i8) __msa_xori_b((v16u8) src0_r, 128);
    dst0 = __msa_adds_s_b(src0_r, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    /* store results */
    ST4x4_UB(dst0, dst0, 0, 1, 2, 3, dst, dst_stride);
}

static void hevc_sao_band_filter_8width_msa(uint8_t *dst, int32_t dst_stride,
                                            uint8_t *src, int32_t src_stride,
                                            int32_t sao_left_class,
                                            int16_t *sao_offset_val,
                                            int32_t height)
{
    v16u8 src0, src1, src2, src3;
    v16i8 src0_r, src1_r, mask0, mask1;
    v16i8 offset_mask0, offset_mask1, offset_val;
    v16i8 offset0, offset1, dst0, dst1;
    v16i8 zero = { 0 };

    offset_val = LD_SB(sao_offset_val + 1);
    offset_val = (v16i8) __msa_pckev_d((v2i64) offset_val, (v2i64) offset_val);
    offset_val = __msa_pckev_b(offset_val, offset_val);
    offset1 = (v16i8) __msa_insve_w((v4i32) zero, 3, (v4i32) offset_val);
    offset0 = __msa_sld_b(offset1, zero, 28 - ((sao_left_class) & 31));
    offset1 = __msa_sld_b(zero, offset1, 28 - ((sao_left_class) & 31));

    /* load in advance. */
    LD_UB4(src, src_stride, src0, src1, src2, src3);

    if (!((sao_left_class > 12) & (sao_left_class < 29))) {
        SWAP(offset0, offset1);
    }

    for (height -= 4; height; height -= 4) {
        src += src_stride << 2;

        ILVR_D2_SB(src1, src0, src3, src2, src0_r, src1_r);

        mask0 = __msa_srli_b(src0_r, 3);
        mask1 = __msa_srli_b(src1_r, 3);

        offset_mask0 = __msa_vshf_b(mask0, offset1, offset0);
        offset_mask1 = __msa_vshf_b(mask1, offset1, offset0);

        /* load in advance. */
        LD_UB4(src, src_stride, src0, src1, src2, src3);

        XORI_B2_128_SB(src0_r, src1_r);

        dst0 = __msa_adds_s_b(src0_r, offset_mask0);
        dst1 = __msa_adds_s_b(src1_r, offset_mask1);

        XORI_B2_128_SB(dst0, dst1);

        /* store results */
        ST8x4_UB(dst0, dst1, dst, dst_stride);
        dst += dst_stride << 2;
    }

    ILVR_D2_SB(src1, src0, src3, src2, src0_r, src1_r);

    mask0 = __msa_srli_b(src0_r, 3);
    mask1 = __msa_srli_b(src1_r, 3);

    offset_mask0 = __msa_vshf_b(mask0, offset1, offset0);
    offset_mask1 = __msa_vshf_b(mask1, offset1, offset0);

    XORI_B2_128_SB(src0_r, src1_r);

    dst0 = __msa_adds_s_b(src0_r, offset_mask0);
    dst1 = __msa_adds_s_b(src1_r, offset_mask1);

    XORI_B2_128_SB(dst0, dst1);

    /* store results */
    ST8x4_UB(dst0, dst1, dst, dst_stride);
}

static void hevc_sao_band_filter_16multiple_msa(uint8_t *dst,
                                                int32_t dst_stride,
                                                uint8_t *src,
                                                int32_t src_stride,
                                                int32_t sao_left_class,
                                                int16_t *sao_offset_val,
                                                int32_t width, int32_t height)
{
    int32_t w_cnt;
    v16u8 src0, src1, src2, src3;
    v16i8 out0, out1, out2, out3;
    v16i8 mask0, mask1, mask2, mask3;
    v16i8 tmp0, tmp1, tmp2, tmp3, offset_val;
    v16i8 offset0, offset1;
    v16i8 zero = { 0 };

    offset_val = LD_SB(sao_offset_val + 1);
    offset_val = (v16i8) __msa_pckev_d((v2i64) offset_val, (v2i64) offset_val);
    offset_val = __msa_pckev_b(offset_val, offset_val);
    offset1 = (v16i8) __msa_insve_w((v4i32) zero, 3, (v4i32) offset_val);
    offset0 = __msa_sld_b(offset1, zero, 28 - ((sao_left_class) & 31));
    offset1 = __msa_sld_b(zero, offset1, 28 - ((sao_left_class) & 31));

    if (!((sao_left_class > 12) & (sao_left_class < 29))) {
        SWAP(offset0, offset1);
    }

    while (height > 0) {
        /* load in advance */
        LD_UB4(src, src_stride, src0, src1, src2, src3);

        for (w_cnt = 16; w_cnt < width; w_cnt += 16) {
            mask0 = __msa_srli_b((v16i8) src0, 3);
            mask1 = __msa_srli_b((v16i8) src1, 3);
            mask2 = __msa_srli_b((v16i8) src2, 3);
            mask3 = __msa_srli_b((v16i8) src3, 3);

            VSHF_B2_SB(offset0, offset1, offset0, offset1, mask0, mask1,
                       tmp0, tmp1);
            VSHF_B2_SB(offset0, offset1, offset0, offset1, mask2, mask3,
                       tmp2, tmp3);
            XORI_B4_128_UB(src0, src1, src2, src3);

            out0 = __msa_adds_s_b((v16i8) src0, tmp0);
            out1 = __msa_adds_s_b((v16i8) src1, tmp1);
            out2 = __msa_adds_s_b((v16i8) src2, tmp2);
            out3 = __msa_adds_s_b((v16i8) src3, tmp3);

            /* load for next iteration */
            LD_UB4(src + w_cnt, src_stride, src0, src1, src2, src3);

            XORI_B4_128_SB(out0, out1, out2, out3);

            ST_SB4(out0, out1, out2, out3, dst + w_cnt - 16, dst_stride);
        }

        mask0 = __msa_srli_b((v16i8) src0, 3);
        mask1 = __msa_srli_b((v16i8) src1, 3);
        mask2 = __msa_srli_b((v16i8) src2, 3);
        mask3 = __msa_srli_b((v16i8) src3, 3);

        VSHF_B2_SB(offset0, offset1, offset0, offset1, mask0, mask1, tmp0,
                   tmp1);
        VSHF_B2_SB(offset0, offset1, offset0, offset1, mask2, mask3, tmp2,
                   tmp3);
        XORI_B4_128_UB(src0, src1, src2, src3);

        out0 = __msa_adds_s_b((v16i8) src0, tmp0);
        out1 = __msa_adds_s_b((v16i8) src1, tmp1);
        out2 = __msa_adds_s_b((v16i8) src2, tmp2);
        out3 = __msa_adds_s_b((v16i8) src3, tmp3);

        XORI_B4_128_SB(out0, out1, out2, out3);

        ST_SB4(out0, out1, out2, out3, dst + w_cnt - 16, dst_stride);

        src += src_stride << 2;
        dst += dst_stride << 2;
        height -= 4;
    }
}

static void hevc_sao_edge_filter_0degree_4width_msa(uint8_t *dst,
                                                    int32_t dst_stride,
                                                    uint8_t *src,
                                                    int32_t src_stride,
                                                    int16_t *sao_offset_val,
                                                    int32_t height)
{
    uint32_t dst_val0, dst_val1;
    v16u8 cmp_minus10, diff_minus10, diff_minus11, src_minus10, src_minus11;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16i8 sao_offset = LD_SB(sao_offset_val);
    v16i8 src_plus10, offset, src0, dst0;
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 zero = { 0 };

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);
    src -= 1;

    /* load in advance */
    LD_UB2(src, src_stride, src_minus10, src_minus11);

    for (height -= 2; height; height -= 2) {
        src += (2 * src_stride);

        src_minus10 = (v16u8) __msa_pckev_d((v2i64) src_minus11,
                                            (v2i64) src_minus10);

        src0 = (v16i8) __msa_sldi_b(zero, (v16i8) src_minus10, 1);
        src_plus10 = (v16i8) __msa_sldi_b(zero, (v16i8) src_minus10, 2);

        cmp_minus10 = ((v16u8) src0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus10 = ((v16u8) src0 == (v16u8) src_plus10);
        diff_minus11 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = ((v16u8) src_plus10 < (v16u8) src0);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus10);

        offset = (v16i8) diff_minus10 + (v16i8) diff_minus11 + 2;

        /* load in advance */
        LD_UB2(src, src_stride, src_minus10, src_minus11);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
                   offset, offset);

        src0 = (v16i8) __msa_xori_b((v16u8) src0, 128);
        dst0 = __msa_adds_s_b(src0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
        dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);
        SW(dst_val0, dst);
        dst += dst_stride;
        SW(dst_val1, dst);
        dst += dst_stride;
    }

    src_minus10 = (v16u8) __msa_pckev_d((v2i64) src_minus11,
                                        (v2i64) src_minus10);

    src0 = (v16i8) __msa_sldi_b(zero, (v16i8) src_minus10, 1);
    src_plus10 = (v16i8) __msa_sldi_b(zero, (v16i8) src_minus10, 2);

    cmp_minus10 = ((v16u8) src0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus10 = ((v16u8) src0 == (v16u8) src_plus10);
    diff_minus11 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = ((v16u8) src_plus10 < (v16u8) src0);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus10);

    offset = (v16i8) diff_minus10 + (v16i8) diff_minus11 + 2;
    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    src0 = (v16i8) __msa_xori_b((v16u8) src0, 128);
    dst0 = __msa_adds_s_b(src0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
    dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);

    SW(dst_val0, dst);
    dst += dst_stride;
    SW(dst_val1, dst);
}

static void hevc_sao_edge_filter_0degree_8width_msa(uint8_t *dst,
                                                    int32_t dst_stride,
                                                    uint8_t *src,
                                                    int32_t src_stride,
                                                    int16_t *sao_offset_val,
                                                    int32_t height)
{
    uint64_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16u8 cmp_minus10, diff_minus10, diff_minus11;
    v16u8 src0, src1, dst0, src_minus10, src_minus11, src_plus10, src_plus11;
    v16i8 offset, sao_offset = LD_SB(sao_offset_val);

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);
    src -= 1;

    /* load in advance */
    LD_UB2(src, src_stride, src_minus10, src_minus11);

    for (height -= 2; height; height -= 2) {
        src += (src_stride << 1);

        SLDI_B2_0_UB(src_minus10, src_minus11, src0, src1, 1);
        SLDI_B2_0_UB(src_minus10, src_minus11, src_plus10, src_plus11, 2);

        PCKEV_D2_UB(src_minus11, src_minus10, src_plus11, src_plus10,
                    src_minus10, src_plus10);
        src0 = (v16u8) __msa_pckev_d((v2i64) src1, (v2i64) src0);

        cmp_minus10 = (src0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < src0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus10 = (src0 == src_plus10);
        diff_minus11 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_plus10 < src0);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus10);

        offset = (v16i8) diff_minus10 + (v16i8) diff_minus11 + 2;

        /* load in advance */
        LD_UB2(src, src_stride, src_minus10, src_minus11);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
                   offset, offset);

        src0 = __msa_xori_b(src0, 128);
        dst0 = (v16u8) __msa_adds_s_b((v16i8) src0, offset);
        dst0 = __msa_xori_b(dst0, 128);

        dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);
        SD(dst_val0, dst);
        dst += dst_stride;
        SD(dst_val1, dst);
        dst += dst_stride;
    }

    SLDI_B2_0_UB(src_minus10, src_minus11, src0, src1, 1);
    SLDI_B2_0_UB(src_minus10, src_minus11, src_plus10, src_plus11, 2);

    PCKEV_D2_UB(src_minus11, src_minus10, src_plus11, src_plus10, src_minus10,
                src_plus10);
    src0 = (v16u8) __msa_pckev_d((v2i64) src1, (v2i64) src0);

    cmp_minus10 = ((v16u8) src0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus10 = (src0 ==  src_plus10);
    diff_minus11 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_plus10 < src0);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus10);

    offset = (v16i8) diff_minus10 + (v16i8) diff_minus11 + 2;

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    src0 = __msa_xori_b(src0, 128);
    dst0 = (v16u8) __msa_adds_s_b((v16i8) src0, offset);
    dst0 = __msa_xori_b(dst0, 128);

    dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
    dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);
    SD(dst_val0, dst);
    dst += dst_stride;
    SD(dst_val1, dst);
}

static void hevc_sao_edge_filter_0degree_16multiple_msa(uint8_t *dst,
                                                        int32_t dst_stride,
                                                        uint8_t *src,
                                                        int32_t src_stride,
                                                        int16_t *sao_offset_val,
                                                        int32_t width,
                                                        int32_t height)
{
    uint8_t *dst_ptr, *src_minus1;
    int32_t v_cnt;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 sao_offset;
    v16u8 cmp_minus10, cmp_plus10, diff_minus10, diff_plus10, cmp_minus11;
    v16u8 cmp_plus11, diff_minus11, diff_plus11, cmp_minus12, cmp_plus12;
    v16u8 diff_minus12, diff_plus12, cmp_minus13, cmp_plus13, diff_minus13;
    v16u8 diff_plus13;
    v16u8 src10, src11, src12, src13, dst0, dst1, dst2, dst3;
    v16u8 src_minus10, src_minus11, src_minus12, src_minus13;
    v16i8 offset_mask0, offset_mask1, offset_mask2, offset_mask3;
    v16i8 src_zero0, src_zero1, src_zero2, src_zero3;
    v16i8 src_plus10, src_plus11, src_plus12, src_plus13;

    sao_offset = LD_SB(sao_offset_val);
    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    for (; height; height -= 4) {
        src_minus1 = src - 1;
        LD_UB4(src_minus1, src_stride,
               src_minus10, src_minus11, src_minus12, src_minus13);

        for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
            src_minus1 += 16;
            dst_ptr = dst + v_cnt;
            LD_UB4(src_minus1, src_stride, src10, src11, src12, src13);

            SLDI_B2_SB(src10, src11, src_minus10, src_minus11, src_zero0,
                       src_zero1, 1);
            SLDI_B2_SB(src12, src13, src_minus12, src_minus13, src_zero2,
                       src_zero3, 1);
            SLDI_B2_SB(src10, src11, src_minus10, src_minus11, src_plus10,
                       src_plus11, 2);
            SLDI_B2_SB(src12, src13, src_minus12, src_minus13, src_plus12,
                       src_plus13, 2);

            cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
            cmp_plus10 = ((v16u8) src_zero0 == (v16u8) src_plus10);
            cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
            cmp_plus11 = ((v16u8) src_zero1 == (v16u8) src_plus11);
            cmp_minus12 = ((v16u8) src_zero2 == src_minus12);
            cmp_plus12 = ((v16u8) src_zero2 == (v16u8) src_plus12);
            cmp_minus13 = ((v16u8) src_zero3 == src_minus13);
            cmp_plus13 = ((v16u8) src_zero3 == (v16u8) src_plus13);

            diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
            diff_plus10 = __msa_nor_v(cmp_plus10, cmp_plus10);
            diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
            diff_plus11 = __msa_nor_v(cmp_plus11, cmp_plus11);
            diff_minus12 = __msa_nor_v(cmp_minus12, cmp_minus12);
            diff_plus12 = __msa_nor_v(cmp_plus12, cmp_plus12);
            diff_minus13 = __msa_nor_v(cmp_minus13, cmp_minus13);
            diff_plus13 = __msa_nor_v(cmp_plus13, cmp_plus13);

            cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
            cmp_plus10 = ((v16u8) src_plus10 < (v16u8) src_zero0);
            cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
            cmp_plus11 = ((v16u8) src_plus11 < (v16u8) src_zero1);
            cmp_minus12 = (src_minus12 < (v16u8) src_zero2);
            cmp_plus12 = ((v16u8) src_plus12 < (v16u8) src_zero2);
            cmp_minus13 = (src_minus13 < (v16u8) src_zero3);
            cmp_plus13 = ((v16u8) src_plus13 < (v16u8) src_zero3);

            diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);
            diff_plus10 = __msa_bmnz_v(diff_plus10, const1, cmp_plus10);
            diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);
            diff_plus11 = __msa_bmnz_v(diff_plus11, const1, cmp_plus11);
            diff_minus12 = __msa_bmnz_v(diff_minus12, const1, cmp_minus12);
            diff_plus12 = __msa_bmnz_v(diff_plus12, const1, cmp_plus12);
            diff_minus13 = __msa_bmnz_v(diff_minus13, const1, cmp_minus13);
            diff_plus13 = __msa_bmnz_v(diff_plus13, const1, cmp_plus13);

            offset_mask0 = 2 + (v16i8) diff_minus10 + (v16i8) diff_plus10;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset_mask0,
                       offset_mask0, offset_mask0, offset_mask0);
            offset_mask1 = 2 + (v16i8) diff_minus11 + (v16i8) diff_plus11;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset_mask1,
                       offset_mask1, offset_mask1, offset_mask1);
            offset_mask2 = 2 + (v16i8) diff_minus12 + (v16i8) diff_plus12;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset_mask2,
                       offset_mask2, offset_mask2, offset_mask2);
            offset_mask3 = 2 + (v16i8) diff_minus13 + (v16i8) diff_plus13;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset_mask3,
                       offset_mask3, offset_mask3, offset_mask3);

            XORI_B4_128_SB(src_zero0, src_zero1, src_zero2, src_zero3);

            dst0 = (v16u8) __msa_adds_s_b((v16i8) src_zero0, offset_mask0);
            dst1 = (v16u8) __msa_adds_s_b((v16i8) src_zero1, offset_mask1);
            dst2 = (v16u8) __msa_adds_s_b((v16i8) src_zero2, offset_mask2);
            dst3 = (v16u8) __msa_adds_s_b((v16i8) src_zero3, offset_mask3);

            XORI_B4_128_UB(dst0, dst1, dst2, dst3);

            src_minus10 = src10;
            ST_UB(dst0, dst_ptr);
            src_minus11 = src11;
            ST_UB(dst1, dst_ptr + dst_stride);
            src_minus12 = src12;
            ST_UB(dst2, dst_ptr + (dst_stride << 1));
            src_minus13 = src13;
            ST_UB(dst3, dst_ptr + (dst_stride * 3));
        }

        src += (src_stride << 2);
        dst += (dst_stride << 2);
    }
}

static void hevc_sao_edge_filter_90degree_4width_msa(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     uint8_t *src,
                                                     int32_t src_stride,
                                                     int16_t *sao_offset_val,
                                                     int32_t height)
{
    uint32_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 dst0;
    v16i8 sao_offset = LD_SB(sao_offset_val);
    v16u8 cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    v16u8 src_minus10, src_minus11, src10, src11;
    v16i8 src_zero0, src_zero1;
    v16i8 offset;
    v8i16 offset_mask0, offset_mask1;

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    /* load in advance */
    LD_UB2(src - src_stride, src_stride, src_minus10, src_minus11);
    LD_UB2(src + src_stride, src_stride, src10, src11);

    for (height -= 2; height; height -= 2) {
        src += (src_stride << 1);

        src_minus10 = (v16u8) __msa_ilvr_b((v16i8) src10, (v16i8) src_minus10);
        src_zero0 = __msa_ilvr_b((v16i8) src_minus11, (v16i8) src_minus11);
        src_minus11 = (v16u8) __msa_ilvr_b((v16i8) src11, (v16i8) src_minus11);
        src_zero1 = __msa_ilvr_b((v16i8) src10, (v16i8) src10);

        cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
        diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
        cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

        offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
        offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

        offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
        dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
                   offset, offset);

        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
        dst0 = __msa_adds_s_b(dst0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        LD_UB2(src + src_stride, src_stride, src10, src11);

        dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
        dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);
        SW(dst_val0, dst);
        dst += dst_stride;
        SW(dst_val1, dst);

        dst += dst_stride;
    }

    src_minus10 = (v16u8) __msa_ilvr_b((v16i8) src10, (v16i8) src_minus10);
    src_zero0 = __msa_ilvr_b((v16i8) src_minus11, (v16i8) src_minus11);
    src_minus11 = (v16u8) __msa_ilvr_b((v16i8) src11, (v16i8) src_minus11);
    src_zero1 = __msa_ilvr_b((v16i8) src10, (v16i8) src10);

    cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
    diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
    cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

    offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
    offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

    offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
    dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset,
               offset, offset, offset);

    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
    dst0 = __msa_adds_s_b(dst0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
    dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);
    SW(dst_val0, dst);
    dst += dst_stride;
    SW(dst_val1, dst);
}

static void hevc_sao_edge_filter_90degree_8width_msa(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     uint8_t *src,
                                                     int32_t src_stride,
                                                     int16_t *sao_offset_val,
                                                     int32_t height)
{
    uint64_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 offset, sao_offset = LD_SB(sao_offset_val);
    v16i8 src_zero0, src_zero1, dst0;
    v16u8 cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    v16u8 src_minus10, src_minus11, src10, src11;
    v8i16 offset_mask0, offset_mask1;

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    /* load in advance */
    LD_UB2(src - src_stride, src_stride, src_minus10, src_minus11);
    LD_UB2(src + src_stride, src_stride, src10, src11);

    for (height -= 2; height; height -= 2) {
        src += (src_stride << 1);

        src_minus10 = (v16u8) __msa_ilvr_b((v16i8) src10, (v16i8) src_minus10);
        src_zero0 = __msa_ilvr_b((v16i8) src_minus11, (v16i8) src_minus11);
        src_minus11 = (v16u8) __msa_ilvr_b((v16i8) src11, (v16i8) src_minus11);
        src_zero1 = __msa_ilvr_b((v16i8) src10, (v16i8) src10);

        cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
        diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
        cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

        offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
        offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

        offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
        dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset,
                   offset, offset, offset);

        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
        dst0 = __msa_adds_s_b(dst0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        LD_UB2(src + src_stride, src_stride, src10, src11);

        dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);
        SD(dst_val0, dst);
        dst += dst_stride;
        SD(dst_val1, dst);
        dst += dst_stride;
    }

    src_minus10 = (v16u8) __msa_ilvr_b((v16i8) src10, (v16i8) src_minus10);
    src_zero0 = __msa_ilvr_b((v16i8) src_minus11, (v16i8) src_minus11);
    src_minus11 = (v16u8) __msa_ilvr_b((v16i8) src11, (v16i8) src_minus11);
    src_zero1 = __msa_ilvr_b((v16i8) src10, (v16i8) src10);

    cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
    diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
    cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

    offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
    offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

    offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
    dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
    dst0 = __msa_adds_s_b(dst0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
    dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);
    SD(dst_val0, dst);
    dst += dst_stride;
    SD(dst_val1, dst);
}

static void hevc_sao_edge_filter_90degree_16multiple_msa(uint8_t *dst,
                                                         int32_t dst_stride,
                                                         uint8_t *src,
                                                         int32_t src_stride,
                                                         int16_t *
                                                         sao_offset_val,
                                                         int32_t width,
                                                         int32_t height)
{
    uint8_t *src_orig = src;
    uint8_t *dst_orig = dst;
    int32_t h_cnt, v_cnt;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16u8 cmp_minus10, cmp_plus10, diff_minus10, diff_plus10, cmp_minus11;
    v16u8 cmp_plus11, diff_minus11, diff_plus11, cmp_minus12, cmp_plus12;
    v16u8 diff_minus12, diff_plus12, cmp_minus13, cmp_plus13, diff_minus13;
    v16u8 diff_plus13;
    v16u8 src10, src_minus10, dst0, src11, src_minus11, dst1;
    v16u8 src12, dst2, src13, dst3;
    v16i8 offset_mask0, offset_mask1, offset_mask2, offset_mask3, sao_offset;

    sao_offset = LD_SB(sao_offset_val);
    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
        src = src_orig + v_cnt;
        dst = dst_orig + v_cnt;

        LD_UB2(src - src_stride, src_stride, src_minus10, src_minus11);

        for (h_cnt = (height >> 2); h_cnt--;) {
            LD_UB4(src + src_stride, src_stride, src10, src11, src12, src13);

            cmp_minus10 = (src_minus11 == src_minus10);
            cmp_plus10 = (src_minus11 == src10);
            cmp_minus11 = (src10 == src_minus11);
            cmp_plus11 = (src10 == src11);
            cmp_minus12 = (src11 == src10);
            cmp_plus12 = (src11 == src12);
            cmp_minus13 = (src12 == src11);
            cmp_plus13 = (src12 == src13);

            diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
            diff_plus10 = __msa_nor_v(cmp_plus10, cmp_plus10);
            diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
            diff_plus11 = __msa_nor_v(cmp_plus11, cmp_plus11);
            diff_minus12 = __msa_nor_v(cmp_minus12, cmp_minus12);
            diff_plus12 = __msa_nor_v(cmp_plus12, cmp_plus12);
            diff_minus13 = __msa_nor_v(cmp_minus13, cmp_minus13);
            diff_plus13 = __msa_nor_v(cmp_plus13, cmp_plus13);

            cmp_minus10 = (src_minus10 < src_minus11);
            cmp_plus10 = (src10 < src_minus11);
            cmp_minus11 = (src_minus11 < src10);
            cmp_plus11 = (src11 < src10);
            cmp_minus12 = (src10 < src11);
            cmp_plus12 = (src12 < src11);
            cmp_minus13 = (src11 < src12);
            cmp_plus13 = (src13 < src12);

            diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);
            diff_plus10 = __msa_bmnz_v(diff_plus10, const1, cmp_plus10);
            diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);
            diff_plus11 = __msa_bmnz_v(diff_plus11, const1, cmp_plus11);
            diff_minus12 = __msa_bmnz_v(diff_minus12, const1, cmp_minus12);
            diff_plus12 = __msa_bmnz_v(diff_plus12, const1, cmp_plus12);
            diff_minus13 = __msa_bmnz_v(diff_minus13, const1, cmp_minus13);
            diff_plus13 = __msa_bmnz_v(diff_plus13, const1, cmp_plus13);

            offset_mask0 = 2 + (v16i8) diff_minus10 + (v16i8) diff_plus10;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask0, offset_mask0, offset_mask0, offset_mask0);
            offset_mask1 = 2 + (v16i8) diff_minus11 + (v16i8) diff_plus11;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask1, offset_mask1, offset_mask1, offset_mask1);
            offset_mask2 = 2 + (v16i8) diff_minus12 + (v16i8) diff_plus12;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask2, offset_mask2, offset_mask2, offset_mask2);
            offset_mask3 = 2 + (v16i8) diff_minus13 + (v16i8) diff_plus13;
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask3, offset_mask3, offset_mask3, offset_mask3);

            src_minus10 = src12;
            XORI_B4_128_UB(src_minus11, src10, src11, src12);

            dst0 = (v16u8) __msa_adds_s_b((v16i8) src_minus11, offset_mask0);
            dst1 = (v16u8) __msa_adds_s_b((v16i8) src10, offset_mask1);
            dst2 = (v16u8) __msa_adds_s_b((v16i8) src11, offset_mask2);
            dst3 = (v16u8) __msa_adds_s_b((v16i8) src12, offset_mask3);

            XORI_B4_128_UB(dst0, dst1, dst2, dst3);
            src_minus11 = src13;

            ST_UB4(dst0, dst1, dst2, dst3, dst, dst_stride);

            src += (src_stride << 2);
            dst += (dst_stride << 2);
        }
    }
}

static void hevc_sao_edge_filter_45degree_4width_msa(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     uint8_t *src,
                                                     int32_t src_stride,
                                                     int16_t *sao_offset_val,
                                                     int32_t height)
{
    uint8_t *src_orig;
    uint32_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 offset, sao_offset = LD_SB(sao_offset_val);
    v16u8 cmp_minus10, diff_minus10, src_minus10, cmp_minus11, diff_minus11;
    v16u8 src_minus11, src10, src11;
    v16i8 src_plus0, src_zero0, src_plus1, src_zero1, dst0;
    v8i16 offset_mask0, offset_mask1;

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    src_orig = src - 1;

    /* load in advance */
    LD_UB2(src_orig - src_stride, src_stride, src_minus10, src_minus11);
    LD_UB2(src_orig + src_stride, src_stride, src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += (src_stride << 1);

        SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
        SLDI_B2_0_SB(src10, src11, src_plus0, src_plus1, 2);

        ILVR_B2_UB(src_plus0, src_minus10, src_plus1, src_minus11, src_minus10,
                   src_minus11);
        ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
                   src_zero1);

        cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
        diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
        cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

        offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
        offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

        offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
        dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset,
                   offset, offset, offset);

        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
        dst0 = __msa_adds_s_b(dst0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        LD_UB2(src_orig + src_stride, src_stride, src10, src11);

        dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
        dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);
        SW(dst_val0, dst);
        dst += dst_stride;
        SW(dst_val1, dst);

        dst += dst_stride;
    }

    SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
    SLDI_B2_0_SB(src10, src11, src_plus0, src_plus1, 2);

    ILVR_B2_UB(src_plus0, src_minus10, src_plus1, src_minus11, src_minus10,
               src_minus11);
    ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
               src_zero1);

    cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
    diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
    cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

    offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
    offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

    offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
    dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
    dst0 = __msa_adds_s_b(dst0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
    dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);
    SW(dst_val0, dst);
    dst += dst_stride;
    SW(dst_val1, dst);
}

static void hevc_sao_edge_filter_45degree_8width_msa(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     uint8_t *src,
                                                     int32_t src_stride,
                                                     int16_t *sao_offset_val,
                                                     int32_t height)
{
    uint8_t *src_orig;
    uint64_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 offset, sao_offset = LD_SB(sao_offset_val);
    v16u8 cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    v16u8 src_minus10, src10, src_minus11, src11;
    v16i8 src_zero0, src_plus10, src_zero1, src_plus11, dst0;
    v8i16 offset_mask0, offset_mask1;

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    LD_UB2(src_orig - src_stride, src_stride, src_minus10, src_minus11);
    LD_UB2(src_orig + src_stride, src_stride, src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += (src_stride << 1);

        SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
        SLDI_B2_0_SB(src10, src11, src_plus10, src_plus11, 2);

        ILVR_B2_UB(src_plus10, src_minus10, src_plus11, src_minus11,
                   src_minus10, src_minus11);
        ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1,
                   src_zero0, src_zero1);

        cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
        diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
        cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

        offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
        offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

        offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
        dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
                   offset, offset);

        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
        dst0 = __msa_adds_s_b(dst0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        LD_UB2(src_orig + src_stride, src_stride, src10, src11);

        dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);
        SD(dst_val0, dst);
        dst += dst_stride;
        SD(dst_val1, dst);
        dst += dst_stride;
    }

    SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
    SLDI_B2_0_SB(src10, src11, src_plus10, src_plus11, 2);
    ILVR_B2_UB(src_plus10, src_minus10, src_plus11, src_minus11, src_minus10,
               src_minus11);
    ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
               src_zero1);

    cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
    diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
    cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

    offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
    offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

    offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
    dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
    dst0 = __msa_adds_s_b(dst0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    src_minus10 = src10;
    src_minus11 = src11;

    /* load in advance */
    LD_UB2(src_orig + src_stride, src_stride, src10, src11);

    dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
    dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);
    SD(dst_val0, dst);
    dst += dst_stride;
    SD(dst_val1, dst);
}

static void hevc_sao_edge_filter_45degree_16multiple_msa(uint8_t *dst,
                                                         int32_t dst_stride,
                                                         uint8_t *src,
                                                         int32_t src_stride,
                                                         int16_t *
                                                         sao_offset_val,
                                                         int32_t width,
                                                         int32_t height)
{
    uint8_t *src_orig = src;
    uint8_t *dst_orig = dst;
    int32_t v_cnt;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16u8 cmp_minus10, cmp_plus10, diff_minus10, diff_plus10, cmp_minus11;
    v16u8 cmp_plus11, diff_minus11, diff_plus11, cmp_minus12, cmp_plus12;
    v16u8 diff_minus12, diff_plus12, cmp_minus13, cmp_plus13, diff_minus13;
    v16u8 diff_plus13, src_minus14, src_plus13;
    v16i8 offset_mask0, offset_mask1, offset_mask2, offset_mask3;
    v16u8 src10, src_minus10, dst0, src11, src_minus11, dst1;
    v16u8 src12, src_minus12, dst2, src13, src_minus13, dst3;
    v16i8 src_zero0, src_plus10, src_zero1, src_plus11, src_zero2, src_plus12;
    v16i8 src_zero3, sao_offset;

    sao_offset = LD_SB(sao_offset_val);
    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    for (; height; height -= 4) {
        src_orig = src - 1;
        dst_orig = dst;
        LD_UB4(src_orig, src_stride, src_minus11, src_minus12, src_minus13,
               src_minus14);

        for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
            src_minus10 = LD_UB(src_orig - src_stride);
            LD_UB4(src_orig + 16, src_stride, src10, src11, src12, src13);
            src_plus13 = LD_UB(src + 1 + v_cnt + (src_stride << 2));
            src_orig += 16;

            SLDI_B2_SB(src10, src11, src_minus11, src_minus12, src_zero0,
                       src_zero1, 1);
            SLDI_B2_SB(src12, src13, src_minus13, src_minus14, src_zero2,
                       src_zero3, 1);
            SLDI_B2_SB(src11, src12, src_minus12, src_minus13, src_plus10,
                       src_plus11, 2);

            src_plus12 = __msa_sldi_b((v16i8) src13, (v16i8) src_minus14, 2);

            cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
            cmp_plus10 = ((v16u8) src_zero0 == (v16u8) src_plus10);
            cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
            cmp_plus11 = ((v16u8) src_zero1 == (v16u8) src_plus11);
            cmp_minus12 = ((v16u8) src_zero2 == src_minus12);
            cmp_plus12 = ((v16u8) src_zero2 == (v16u8) src_plus12);
            cmp_minus13 = ((v16u8) src_zero3 == src_minus13);
            cmp_plus13 = ((v16u8) src_zero3 == src_plus13);

            diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
            diff_plus10 = __msa_nor_v(cmp_plus10, cmp_plus10);
            diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
            diff_plus11 = __msa_nor_v(cmp_plus11, cmp_plus11);
            diff_minus12 = __msa_nor_v(cmp_minus12, cmp_minus12);
            diff_plus12 = __msa_nor_v(cmp_plus12, cmp_plus12);
            diff_minus13 = __msa_nor_v(cmp_minus13, cmp_minus13);
            diff_plus13 = __msa_nor_v(cmp_plus13, cmp_plus13);

            cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
            cmp_plus10 = ((v16u8) src_plus10 < (v16u8) src_zero0);
            cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
            cmp_plus11 = ((v16u8) src_plus11 < (v16u8) src_zero1);
            cmp_minus12 = (src_minus12 < (v16u8) src_zero2);
            cmp_plus12 = ((v16u8) src_plus12 < (v16u8) src_zero2);
            cmp_minus13 = (src_minus13 < (v16u8) src_zero3);
            cmp_plus13 = (src_plus13 < (v16u8) src_zero3);

            diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);
            diff_plus10 = __msa_bmnz_v(diff_plus10, const1, cmp_plus10);
            diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);
            diff_plus11 = __msa_bmnz_v(diff_plus11, const1, cmp_plus11);
            diff_minus12 = __msa_bmnz_v(diff_minus12, const1, cmp_minus12);
            diff_plus12 = __msa_bmnz_v(diff_plus12, const1, cmp_plus12);
            diff_minus13 = __msa_bmnz_v(diff_minus13, const1, cmp_minus13);
            diff_plus13 = __msa_bmnz_v(diff_plus13, const1, cmp_plus13);

            offset_mask0 = 2 + (v16i8) diff_minus10 + (v16i8) diff_plus10;
            offset_mask1 = 2 + (v16i8) diff_minus11 + (v16i8) diff_plus11;
            offset_mask2 = 2 + (v16i8) diff_minus12 + (v16i8) diff_plus12;
            offset_mask3 = 2 + (v16i8) diff_minus13 + (v16i8) diff_plus13;

            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask0, offset_mask0, offset_mask0, offset_mask0);
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask1, offset_mask1, offset_mask1, offset_mask1);
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask2, offset_mask2, offset_mask2, offset_mask2);
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask3, offset_mask3, offset_mask3, offset_mask3);

            XORI_B4_128_SB(src_zero0, src_zero1, src_zero2, src_zero3);

            dst0 = (v16u8) __msa_adds_s_b((v16i8) src_zero0, offset_mask0);
            dst1 = (v16u8) __msa_adds_s_b((v16i8) src_zero1, offset_mask1);
            dst2 = (v16u8) __msa_adds_s_b((v16i8) src_zero2, offset_mask2);
            dst3 = (v16u8) __msa_adds_s_b((v16i8) src_zero3, offset_mask3);

            XORI_B4_128_UB(dst0, dst1, dst2, dst3);

            src_minus11 = src10;
            src_minus12 = src11;
            src_minus13 = src12;
            src_minus14 = src13;

            ST_UB4(dst0, dst1, dst2, dst3, dst_orig, dst_stride);
            dst_orig += 16;
        }

        src += (src_stride << 2);
        dst += (dst_stride << 2);
    }
}

static void hevc_sao_edge_filter_135degree_4width_msa(uint8_t *dst,
                                                      int32_t dst_stride,
                                                      uint8_t *src,
                                                      int32_t src_stride,
                                                      int16_t *sao_offset_val,
                                                      int32_t height)
{
    uint8_t *src_orig;
    uint32_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 offset, sao_offset = LD_SB(sao_offset_val);
    v16i8 src_zero0, src_zero1, dst0;
    v16u8 cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    v16u8 src_minus10, src10, src_minus11, src11;
    v8i16 offset_mask0, offset_mask1;

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    LD_UB2(src_orig - src_stride, src_stride, src_minus10, src_minus11);
    LD_UB2(src_orig + src_stride, src_stride, src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += (src_stride << 1);

        SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
        SLDI_B2_0_UB(src_minus10, src_minus11, src_minus10, src_minus11, 2);

        ILVR_B2_UB(src10, src_minus10, src11, src_minus11, src_minus10,
                   src_minus11);
        ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
                   src_zero1);

        cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
        diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
        cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

        offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
        offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

        offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
        dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
                   offset, offset);

        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
        dst0 = __msa_adds_s_b(dst0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        LD_UB2(src_orig + src_stride, src_stride, src10, src11);

        dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
        dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);

        SW(dst_val0, dst);
        dst += dst_stride;
        SW(dst_val1, dst);

        dst += dst_stride;
    }

    SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
    SLDI_B2_0_UB(src_minus10, src_minus11, src_minus10, src_minus11, 2);

    ILVR_B2_UB(src10, src_minus10, src11, src_minus11, src_minus10,
               src_minus11);
    ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
               src_zero1);

    cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
    diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
    cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

    offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
    offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

    offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
    dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
    dst0 = __msa_adds_s_b(dst0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    dst_val0 = __msa_copy_u_w((v4i32) dst0, 0);
    dst_val1 = __msa_copy_u_w((v4i32) dst0, 2);

    SW(dst_val0, dst);
    dst += dst_stride;
    SW(dst_val1, dst);
    dst += dst_stride;
}

static void hevc_sao_edge_filter_135degree_8width_msa(uint8_t *dst,
                                                      int32_t dst_stride,
                                                      uint8_t *src,
                                                      int32_t src_stride,
                                                      int16_t *sao_offset_val,
                                                      int32_t height)
{
    uint8_t *src_orig;
    uint64_t dst_val0, dst_val1;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16i8 offset, sao_offset = LD_SB(sao_offset_val);
    v16u8 cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    v16u8 src_minus10, src10, src_minus11, src11;
    v16i8 src_zero0, src_zero1, dst0;
    v8i16 offset_mask0, offset_mask1;

    sao_offset = __msa_pckev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    LD_UB2(src_orig - src_stride, src_stride, src_minus10, src_minus11);
    LD_UB2(src_orig + src_stride, src_stride, src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += (src_stride << 1);

        SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
        SLDI_B2_0_UB(src_minus10, src_minus11, src_minus10, src_minus11, 2);
        ILVR_B2_UB(src10, src_minus10, src11, src_minus11, src_minus10,
                   src_minus11);
        ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
                   src_zero1);

        cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
        diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
        cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
        diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

        cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
        diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
        cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
        diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

        offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
        offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

        offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
        dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

        VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
                   offset, offset);

        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
        dst0 = __msa_adds_s_b(dst0, offset);
        dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        LD_UB2(src_orig + src_stride, src_stride, src10, src11);

        dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
        dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);

        SD(dst_val0, dst);
        dst += dst_stride;
        SD(dst_val1, dst);
        dst += dst_stride;
    }

    SLDI_B2_0_SB(src_minus11, src10, src_zero0, src_zero1, 1);
    SLDI_B2_0_UB(src_minus10, src_minus11, src_minus10, src_minus11, 2);
    ILVR_B2_UB(src10, src_minus10, src11, src_minus11, src_minus10,
               src_minus11);
    ILVR_B2_SB(src_zero0, src_zero0, src_zero1, src_zero1, src_zero0,
               src_zero1);

    cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
    diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
    cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
    diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);

    cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
    diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
    cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
    diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);

    offset_mask0 = (v8i16) (__msa_hadd_u_h(diff_minus10, diff_minus10) + 2);
    offset_mask1 = (v8i16) (__msa_hadd_u_h(diff_minus11, diff_minus11) + 2);

    offset = __msa_pckev_b((v16i8) offset_mask1, (v16i8) offset_mask0);
    dst0 = __msa_pckev_b((v16i8) src_zero1, (v16i8) src_zero0);

    VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset, offset, offset,
               offset, offset);

    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);
    dst0 = __msa_adds_s_b(dst0, offset);
    dst0 = (v16i8) __msa_xori_b((v16u8) dst0, 128);

    dst_val0 = __msa_copy_u_d((v2i64) dst0, 0);
    dst_val1 = __msa_copy_u_d((v2i64) dst0, 1);

    SD(dst_val0, dst);
    dst += dst_stride;
    SD(dst_val1, dst);
    dst += dst_stride;
}

static void hevc_sao_edge_filter_135degree_16multiple_msa(uint8_t *dst,
                                                          int32_t dst_stride,
                                                          uint8_t *src,
                                                          int32_t src_stride,
                                                          int16_t *
                                                          sao_offset_val,
                                                          int32_t width,
                                                          int32_t height)
{
    uint8_t *src_orig, *dst_orig;
    int32_t v_cnt;
    v16i8 edge_idx = { 1, 2, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v16u8 const1 = (v16u8) __msa_ldi_b(1);
    v16u8 dst0, dst1, dst2, dst3;
    v16u8 cmp_minus10, cmp_minus11, cmp_minus12, cmp_minus13, cmp_plus10;
    v16u8 cmp_plus11, cmp_plus12, cmp_plus13, diff_minus10, diff_minus11;
    v16u8 diff_minus12, diff_minus13, diff_plus10, diff_plus11, diff_plus12;
    v16u8 diff_plus13, src10, src11, src12, src13, src_minus10, src_minus11;
    v16u8 src_plus10, src_plus11, src_plus12, src_plus13;
    v16i8 src_minus12, src_minus13, src_zero0, src_zero1, src_zero2, src_zero3;
    v16i8 offset_mask0, offset_mask1, offset_mask2, offset_mask3, sao_offset;

    sao_offset = LD_SB(sao_offset_val);
    sao_offset = __msa_pckev_b(sao_offset, sao_offset);

    for (; height; height -= 4) {
        src_orig = src - 1;
        dst_orig = dst;

        LD_UB4(src_orig, src_stride, src_minus11, src_plus10, src_plus11,
               src_plus12);

        for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
            src_minus10 = LD_UB(src_orig + 2 - src_stride);
            LD_UB4(src_orig + 16, src_stride, src10, src11, src12, src13);
            src_plus13 = LD_UB(src_orig + (src_stride << 2));
            src_orig += 16;

            src_zero0 = __msa_sldi_b((v16i8) src10, (v16i8) src_minus11, 1);
            cmp_minus10 = ((v16u8) src_zero0 == src_minus10);
            cmp_plus10 = ((v16u8) src_zero0 == src_plus10);

            src_zero1 = __msa_sldi_b((v16i8) src11, (v16i8) src_plus10, 1);
            src_minus11 = (v16u8) __msa_sldi_b((v16i8) src10,
                                               (v16i8) src_minus11, 2);
            cmp_minus11 = ((v16u8) src_zero1 == src_minus11);
            cmp_plus11 = ((v16u8) src_zero1 == src_plus11);

            src_zero2 = __msa_sldi_b((v16i8) src12, (v16i8) src_plus11, 1);
            src_minus12 = __msa_sldi_b((v16i8) src11, (v16i8) src_plus10, 2);
            cmp_minus12 = ((v16u8) src_zero2 == (v16u8) src_minus12);
            cmp_plus12 = ((v16u8) src_zero2 == src_plus12);

            src_zero3 = __msa_sldi_b((v16i8) src13, (v16i8) src_plus12, 1);
            src_minus13 = __msa_sldi_b((v16i8) src12, (v16i8) src_plus11, 2);
            cmp_minus13 = ((v16u8) src_zero3 == (v16u8) src_minus13);
            cmp_plus13 = ((v16u8) src_zero3 == src_plus13);

            diff_minus10 = __msa_nor_v(cmp_minus10, cmp_minus10);
            diff_plus10 = __msa_nor_v(cmp_plus10, cmp_plus10);
            diff_minus11 = __msa_nor_v(cmp_minus11, cmp_minus11);
            diff_plus11 = __msa_nor_v(cmp_plus11, cmp_plus11);
            diff_minus12 = __msa_nor_v(cmp_minus12, cmp_minus12);
            diff_plus12 = __msa_nor_v(cmp_plus12, cmp_plus12);
            diff_minus13 = __msa_nor_v(cmp_minus13, cmp_minus13);
            diff_plus13 = __msa_nor_v(cmp_plus13, cmp_plus13);

            cmp_minus10 = (src_minus10 < (v16u8) src_zero0);
            cmp_plus10 = (src_plus10 < (v16u8) src_zero0);
            cmp_minus11 = (src_minus11 < (v16u8) src_zero1);
            cmp_plus11 = (src_plus11 < (v16u8) src_zero1);
            cmp_minus12 = ((v16u8) src_minus12 < (v16u8) src_zero2);
            cmp_plus12 = (src_plus12 < (v16u8) src_zero2);
            cmp_minus13 = ((v16u8) src_minus13 < (v16u8) src_zero3);
            cmp_plus13 = (src_plus13 < (v16u8) src_zero3);

            diff_minus10 = __msa_bmnz_v(diff_minus10, const1, cmp_minus10);
            diff_plus10 = __msa_bmnz_v(diff_plus10, const1, cmp_plus10);
            diff_minus11 = __msa_bmnz_v(diff_minus11, const1, cmp_minus11);
            diff_plus11 = __msa_bmnz_v(diff_plus11, const1, cmp_plus11);
            diff_minus12 = __msa_bmnz_v(diff_minus12, const1, cmp_minus12);
            diff_plus12 = __msa_bmnz_v(diff_plus12, const1, cmp_plus12);
            diff_minus13 = __msa_bmnz_v(diff_minus13, const1, cmp_minus13);
            diff_plus13 = __msa_bmnz_v(diff_plus13, const1, cmp_plus13);

            offset_mask0 = 2 + (v16i8) diff_minus10 + (v16i8) diff_plus10;
            offset_mask1 = 2 + (v16i8) diff_minus11 + (v16i8) diff_plus11;
            offset_mask2 = 2 + (v16i8) diff_minus12 + (v16i8) diff_plus12;
            offset_mask3 = 2 + (v16i8) diff_minus13 + (v16i8) diff_plus13;

            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask0, offset_mask0, offset_mask0, offset_mask0);
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask1, offset_mask1, offset_mask1, offset_mask1);
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask2, offset_mask2, offset_mask2, offset_mask2);
            VSHF_B2_SB(edge_idx, edge_idx, sao_offset, sao_offset,
                       offset_mask3, offset_mask3, offset_mask3, offset_mask3);

            XORI_B4_128_SB(src_zero0, src_zero1, src_zero2, src_zero3);

            dst0 = (v16u8) __msa_adds_s_b((v16i8) src_zero0, offset_mask0);
            dst1 = (v16u8) __msa_adds_s_b((v16i8) src_zero1, offset_mask1);
            dst2 = (v16u8) __msa_adds_s_b((v16i8) src_zero2, offset_mask2);
            dst3 = (v16u8) __msa_adds_s_b((v16i8) src_zero3, offset_mask3);

            XORI_B4_128_UB(dst0, dst1, dst2, dst3);

            src_minus11 = src10;
            src_plus10 = src11;
            src_plus11 = src12;
            src_plus12 = src13;

            ST_UB4(dst0, dst1, dst2, dst3, dst_orig, dst_stride);
            dst_orig += 16;
        }

        src += (src_stride << 2);
        dst += (dst_stride << 2);
    }
}

void ff_hevc_loop_filter_luma_h_8_msa(uint8_t *src,
                                      ptrdiff_t src_stride,
                                      int32_t beta, int32_t *tc,
                                      uint8_t *no_p, uint8_t *no_q)
{
    hevc_loopfilter_luma_hor_msa(src, src_stride, beta, tc, no_p, no_q);
}

void ff_hevc_loop_filter_luma_v_8_msa(uint8_t *src,
                                      ptrdiff_t src_stride,
                                      int32_t beta, int32_t *tc,
                                      uint8_t *no_p, uint8_t *no_q)
{
    hevc_loopfilter_luma_ver_msa(src, src_stride, beta, tc, no_p, no_q);
}

void ff_hevc_loop_filter_chroma_h_8_msa(uint8_t *src,
                                        ptrdiff_t src_stride,
                                        int32_t *tc, uint8_t *no_p,
                                        uint8_t *no_q)
{
    hevc_loopfilter_chroma_hor_msa(src, src_stride, tc, no_p, no_q);
}

void ff_hevc_loop_filter_chroma_v_8_msa(uint8_t *src,
                                        ptrdiff_t src_stride,
                                        int32_t *tc, uint8_t *no_p,
                                        uint8_t *no_q)
{
    hevc_loopfilter_chroma_ver_msa(src, src_stride, tc, no_p, no_q);
}

void ff_hevc_sao_band_filter_0_8_msa(uint8_t *dst, uint8_t *src,
                                     ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                     int16_t *sao_offset_val, int sao_left_class,
                                     int width, int height)
{
    if (width >> 4) {
        hevc_sao_band_filter_16multiple_msa(dst, stride_dst, src, stride_src,
                                            sao_left_class, sao_offset_val,
                                            width - (width % 16), height);
        dst += width - (width % 16);
        src += width - (width % 16);
        width %= 16;
    }

    if (width >> 3) {
        hevc_sao_band_filter_8width_msa(dst, stride_dst, src, stride_src,
                                        sao_left_class, sao_offset_val, height);
        dst += 8;
        src += 8;
        width %= 8;
    }

    if (width) {
        hevc_sao_band_filter_4width_msa(dst, stride_dst, src, stride_src,
                                        sao_left_class, sao_offset_val, height);
    }
}

void ff_hevc_sao_edge_filter_8_msa(uint8_t *dst, uint8_t *src,
                                   ptrdiff_t stride_dst,
                                   int16_t *sao_offset_val,
                                   int eo, int width, int height)
{
    ptrdiff_t stride_src = (2 * MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE) / sizeof(uint8_t);

    switch (eo) {
    case 0:
        if (width >> 4) {
            hevc_sao_edge_filter_0degree_16multiple_msa(dst, stride_dst,
                                                        src, stride_src,
                                                        sao_offset_val,
                                                        width - (width % 16),
                                                        height);
            dst += width - (width % 16);
            src += width - (width % 16);
            width %= 16;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_0degree_8width_msa(dst, stride_dst,
                                                    src, stride_src,
                                                    sao_offset_val, height);
            dst += 8;
            src += 8;
            width %= 8;
        }

        if (width) {
            hevc_sao_edge_filter_0degree_4width_msa(dst, stride_dst,
                                                    src, stride_src,
                                                    sao_offset_val, height);
        }
        break;

    case 1:
        if (width >> 4) {
            hevc_sao_edge_filter_90degree_16multiple_msa(dst, stride_dst,
                                                         src, stride_src,
                                                         sao_offset_val,
                                                         width - (width % 16),
                                                         height);
            dst += width - (width % 16);
            src += width - (width % 16);
            width %= 16;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_90degree_8width_msa(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
            dst += 8;
            src += 8;
            width %= 8;
        }

        if (width) {
            hevc_sao_edge_filter_90degree_4width_msa(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
        }
        break;

    case 2:
        if (width >> 4) {
            hevc_sao_edge_filter_45degree_16multiple_msa(dst, stride_dst,
                                                         src, stride_src,
                                                         sao_offset_val,
                                                         width - (width % 16),
                                                         height);
            dst += width - (width % 16);
            src += width - (width % 16);
            width %= 16;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_45degree_8width_msa(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
            dst += 8;
            src += 8;
            width %= 8;
        }

        if (width) {
            hevc_sao_edge_filter_45degree_4width_msa(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
        }
        break;

    case 3:
        if (width >> 4) {
            hevc_sao_edge_filter_135degree_16multiple_msa(dst, stride_dst,
                                                          src, stride_src,
                                                          sao_offset_val,
                                                          width - (width % 16),
                                                          height);
            dst += width - (width % 16);
            src += width - (width % 16);
            width %= 16;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_135degree_8width_msa(dst, stride_dst,
                                                      src, stride_src,
                                                      sao_offset_val, height);
            dst += 8;
            src += 8;
            width %= 8;
        }

        if (width) {
            hevc_sao_edge_filter_135degree_4width_msa(dst, stride_dst,
                                                      src, stride_src,
                                                      sao_offset_val, height);
        }
        break;
    }
}
