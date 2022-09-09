/*
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Lu Wang <wanglu@loongson.cn>
 *                Hao Chen <chenhao@loongson.cn>
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

#include "libavutil/loongarch/loongson_intrinsics.h"
#include "hevcdsp_lsx.h"

void ff_hevc_loop_filter_luma_h_8_lsx(uint8_t *src, ptrdiff_t stride,
                                      int32_t beta, const int32_t *tc,
                                      const uint8_t *p_is_pcm, const uint8_t *q_is_pcm)
{
    ptrdiff_t stride_2x = (stride << 1);
    ptrdiff_t stride_4x = (stride << 2);
    ptrdiff_t stride_3x = stride_2x + stride;
    uint8_t *p3 = src - stride_4x;
    uint8_t *p2 = src - stride_3x;
    uint8_t *p1 = src - stride_2x;
    uint8_t *p0 = src - stride;
    uint8_t *q0 = src;
    uint8_t *q1 = src + stride;
    uint8_t *q2 = src + stride_2x;
    uint8_t *q3 = src + stride_3x;
    uint8_t flag0, flag1;
    int32_t dp00, dq00, dp30, dq30, d00, d30, d0030, d0434;
    int32_t dp04, dq04, dp34, dq34, d04, d34;
    int32_t tc0, p_is_pcm0, q_is_pcm0, beta30, beta20, tc250;
    int32_t tc4, p_is_pcm4, q_is_pcm4, tc254, tmp;

    __m128i dst0, dst1, dst2, dst3, dst4, dst5;
    __m128i cmp0, cmp1, cmp2, cmp3, p_is_pcm_vec, q_is_pcm_vec;
    __m128i temp0, temp1;
    __m128i temp2, tc_pos, tc_neg;
    __m128i diff0, diff1, delta0, delta1, delta2, abs_delta0;
    __m128i zero = {0};
    __m128i p3_src, p2_src, p1_src, p0_src, q0_src, q1_src, q2_src, q3_src;

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

    DUP2_ARG1(__lsx_vreplgr2vr_d, p_is_pcm0, p_is_pcm4, cmp0, cmp1);
    p_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
    p_is_pcm_vec = __lsx_vseqi_d(p_is_pcm_vec, 0);
    d0030 = (d00 + d30) >= beta;
    d0434 = (d04 + d34) >= beta;
    DUP2_ARG1(__lsx_vreplgr2vr_w, d0030, d0434, cmp0, cmp1);
    cmp3 = __lsx_vpackev_w(cmp1, cmp0);
    cmp3 = __lsx_vseqi_w(cmp3, 0);

    if ((!p_is_pcm0 || !p_is_pcm4 || !q_is_pcm0 || !q_is_pcm4) &&
        (!d0030 || !d0434)) {
        DUP4_ARG2(__lsx_vld, p3, 0, p2, 0, p1, 0, p0, 0,
                  p3_src, p2_src, p1_src, p0_src);
        DUP2_ARG1(__lsx_vreplgr2vr_d, q_is_pcm0, q_is_pcm4, cmp0, cmp1);
        q_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
        q_is_pcm_vec = __lsx_vseqi_d(q_is_pcm_vec, 0);

        tc0 = tc[0];
        beta30 = beta >> 3;
        beta20 = beta >> 2;
        tc250 = (((tc0 << 2) + tc0 + 1) >> 1);
        tc4 = tc[1];
        tc254 = (((tc4 << 2) + tc4 + 1) >> 1);

        DUP2_ARG1(__lsx_vreplgr2vr_h, tc0, tc4, cmp0, cmp1);
        DUP4_ARG2(__lsx_vilvl_b, zero, p3_src, zero, p2_src, zero, p1_src, zero,
                  p0_src, p3_src, p2_src, p1_src, p0_src);
        DUP4_ARG2(__lsx_vld, q0, 0, q1, 0, q2, 0, q3, 0,
                  q0_src, q1_src, q2_src, q3_src);
        flag0 = abs(p3[0] - p0[0]) + abs(q3[0] - q0[0]) < beta30 &&
                abs(p0[0] - q0[0]) < tc250;
        flag0 = flag0 && (abs(p3[3] - p0[3]) + abs(q3[3] - q0[3]) < beta30 &&
                abs(p0[3] - q0[3]) < tc250 && (d00 << 1) < beta20 &&
                (d30 << 1) < beta20);
        tc_pos = __lsx_vpackev_d(cmp1, cmp0);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0_src, zero, q1_src, zero, q2_src,
                  zero, q3_src, q0_src, q1_src, q2_src, q3_src);

        flag1 = abs(p3[4] - p0[4]) + abs(q3[4] - q0[4]) < beta30 &&
                abs(p0[4] - q0[4]) < tc254;
        flag1 = flag1 && (abs(p3[7] - p0[7]) + abs(q3[7] - q0[7]) < beta30 &&
                abs(p0[7] - q0[7]) < tc254 && (d04 << 1) < beta20 &&
                (d34 << 1) < beta20);
        DUP2_ARG1(__lsx_vreplgr2vr_w, flag0, flag1, cmp0, cmp1);
        cmp2 = __lsx_vpackev_w(cmp1, cmp0);
        cmp2 = __lsx_vseqi_w(cmp2, 0);

        if (flag0 && flag1) { /* strong only */
            /* strong filter */
            tc_pos = __lsx_vslli_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            /* p part */
            DUP2_ARG2(__lsx_vadd_h, p1_src, p0_src, temp0, q0_src,
                      temp0, temp0);
            temp1 = __lsx_vadd_h(p3_src, p2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst0 = __lsx_vadd_h(temp2, p2_src);

            temp1 = __lsx_vadd_h(temp0, p2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, p1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst1 = __lsx_vadd_h(temp2, p1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, q1_src,
                      temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst2 = __lsx_vadd_h(temp2, p0_src);

            p_is_pcm_vec = __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, p2_src, p_is_pcm_vec, dst1,
                      p1_src, p_is_pcm_vec, dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, p0_src, p_is_pcm_vec);

            /* q part */
            DUP2_ARG2(__lsx_vadd_h, q1_src, p0_src, temp0, q0_src,
                      temp0, temp0);
            temp1 = __lsx_vadd_h(q3_src, q2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, q2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst5 = __lsx_vadd_h(temp2, q2_src);

            temp1 = __lsx_vadd_h(temp0, q2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, q1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst4 = __lsx_vadd_h(temp2, q1_src);

            temp0 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp0, p1_src, temp1, q2_src,
                      temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst3 = __lsx_vadd_h(temp2, q0_src);

            q_is_pcm_vec = __lsx_vnor_v(q_is_pcm_vec, q_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst3, q0_src, q_is_pcm_vec, dst4,
                      q1_src, q_is_pcm_vec, dst3, dst4);
            dst5 = __lsx_vbitsel_v(dst5, q2_src, q_is_pcm_vec);

            /* pack results to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, dst1, dst0, dst3, dst2, dst0, dst1);
            dst2 = __lsx_vpickev_b(dst5, dst4);

            /* pack src to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, p1_src, p2_src, q0_src, p0_src,
                      dst3, dst4);
            dst5 = __lsx_vpickev_b(q2_src, q1_src);

            cmp3 = __lsx_vnor_v(cmp3, cmp3);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, dst3, cmp3, dst1, dst4, cmp3,
                      dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, dst5, cmp3);

            __lsx_vstelm_d(dst0, p2, 0, 0);
            __lsx_vstelm_d(dst0, p2 + stride, 0, 1);
            __lsx_vstelm_d(dst1, p2 + stride_2x, 0, 0);
            __lsx_vstelm_d(dst1, p2 + stride_3x, 0, 1);
            __lsx_vstelm_d(dst2, p2 + stride_4x, 0, 0);
            __lsx_vstelm_d(dst2, p2 + stride_4x + stride, 0, 1);
            /* strong filter ends */
        } else if (flag0 == flag1) { /* weak only */
            /* weak filter */
            tc_neg = __lsx_vneg_h(tc_pos);
            DUP2_ARG2(__lsx_vsub_h, q0_src, p0_src, q1_src, p1_src,
                      diff0, diff1);
            DUP2_ARG2(__lsx_vadd_h, __lsx_vslli_h(diff0, 3), diff0,
                      __lsx_vslli_h(diff1, 1), diff1, diff0, diff1);
            delta0 = __lsx_vsub_h(diff0, diff1);
            delta0 = __lsx_vsrari_h(delta0, 4);
            temp1 = __lsx_vadd_h(__lsx_vslli_h(tc_pos, 3),
                                 __lsx_vslli_h(tc_pos, 1));
            abs_delta0 = __lsx_vadda_h(delta0, zero);
            abs_delta0 = __lsx_vsle_hu(temp1, abs_delta0);
            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);

            delta0 = __lsx_vclip_h(delta0, tc_neg, tc_pos);
            temp2 = __lsx_vadd_h(delta0, p0_src);
            temp2 = __lsx_vclip255_h(temp2);
            temp0 = __lsx_vbitsel_v(temp2, p0_src,
                                    __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec));
            temp2 = __lsx_vsub_h(q0_src, delta0);
            temp2 = __lsx_vclip255_h(temp2);
            temp2 = __lsx_vbitsel_v(temp2, q0_src, __lsx_vnor_v(q_is_pcm_vec,
                                    q_is_pcm_vec));
            DUP2_ARG2(__lsx_vnor_v, p_is_pcm_vec, p_is_pcm_vec, q_is_pcm_vec,
                      q_is_pcm_vec, p_is_pcm_vec, q_is_pcm_vec);

            tmp = (beta + (beta >> 1)) >> 3;
            DUP2_ARG1(__lsx_vreplgr2vr_d, dp00 + dp30 < tmp, dp04 + dp34 < tmp,
                      cmp0, cmp1);
            cmp0 = __lsx_vpackev_d(cmp1, cmp0);
            cmp0 = __lsx_vseqi_d(cmp0, 0);
            p_is_pcm_vec = __lsx_vor_v(p_is_pcm_vec, cmp0);

            DUP2_ARG1(__lsx_vreplgr2vr_d, dq00 + dq30 < tmp, dq04 + dq34 < tmp,
                      cmp0, cmp1);
            cmp0 = __lsx_vpackev_d(cmp1, cmp0);
            cmp0 = __lsx_vseqi_d(cmp0, 0);
            q_is_pcm_vec = __lsx_vor_v(q_is_pcm_vec, cmp0);
            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vavgr_hu, p2_src, p0_src, q0_src, q2_src,
                      delta1, delta2);
            DUP2_ARG2(__lsx_vsub_h, delta1, p1_src, delta2, q1_src,
                      delta1, delta2);
            delta1 = __lsx_vadd_h(delta1, delta0);
            delta2 = __lsx_vsub_h(delta2, delta0);
            DUP2_ARG2(__lsx_vsrai_h, delta1, 1, delta2, 1, delta1, delta2);
            DUP2_ARG3(__lsx_vclip_h, delta1, tc_neg, tc_pos, delta2,
                      tc_neg, tc_pos, delta1, delta2);
            DUP2_ARG2(__lsx_vadd_h, p1_src, delta1, q1_src, delta2,
                      delta1, delta2);
            DUP2_ARG1(__lsx_vclip255_h, delta1, delta2, delta1, delta2);
            DUP2_ARG3(__lsx_vbitsel_v, delta1, p1_src, p_is_pcm_vec, delta2,
                      q1_src, q_is_pcm_vec, delta1, delta2);

            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);
            DUP4_ARG3(__lsx_vbitsel_v, delta1, p1_src, abs_delta0, temp0,
                      p0_src,  abs_delta0, temp2, q0_src, abs_delta0, delta2,
                      q1_src, abs_delta0, dst1, dst2, dst3, dst4);
            /* pack results to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, dst2, dst1, dst4, dst3, dst0, dst1);
            /* pack src to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, p0_src, p1_src, q1_src, q0_src,
                      dst2, dst3);
            cmp3 = __lsx_vnor_v(cmp3, cmp3);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, dst2, cmp3, dst1, dst3, cmp3,
                      dst0, dst1);

            p2 += stride;
            __lsx_vstelm_d(dst0, p2, 0, 0);
            __lsx_vstelm_d(dst0, p2 + stride, 0, 1);
            __lsx_vstelm_d(dst1, p2 + stride_2x, 0, 0);
            __lsx_vstelm_d(dst1, p2 + stride_3x, 0, 1);
            /* weak filter ends */
        } else { /* strong + weak */
            /* strong filter */
            tc_pos = __lsx_vslli_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            /* p part */
            DUP2_ARG2(__lsx_vadd_h, p1_src, p0_src, temp0, q0_src,
                      temp0, temp0);
            temp1 = __lsx_vadd_h(p3_src, p2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst0 = __lsx_vadd_h(temp2, p2_src);

            temp1 = __lsx_vadd_h(temp0, p2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, p1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst1 = __lsx_vadd_h(temp2, p1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, q1_src, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst2 = __lsx_vadd_h(temp2, p0_src);

            p_is_pcm_vec = __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, p2_src, p_is_pcm_vec, dst1,
                      p1_src, p_is_pcm_vec, dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, p0_src, p_is_pcm_vec);

            /* q part */
            DUP2_ARG2(__lsx_vadd_h, q1_src, p0_src, temp0, q0_src,
                      temp0, temp0);
            temp1 = __lsx_vadd_h(q3_src, q2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1,  q2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst5 = __lsx_vadd_h(temp2, q2_src);

            temp1 = __lsx_vadd_h(temp0, q2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, q1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst4 = __lsx_vadd_h(temp2, q1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p1_src, temp1, q2_src, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst3 = __lsx_vadd_h(temp2, q0_src);

            q_is_pcm_vec = __lsx_vnor_v(q_is_pcm_vec, q_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst3, q0_src, q_is_pcm_vec, dst4,
                      q1_src, q_is_pcm_vec, dst3, dst4);
            dst5 = __lsx_vbitsel_v(dst5, q2_src, q_is_pcm_vec);

            /* pack strong results to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, dst1, dst0, dst3, dst2, dst0, dst1);
            dst2 = __lsx_vpickev_b(dst5, dst4);
            /* strong filter ends */

            /* weak filter */
            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vsub_h, q0_src, p0_src, q1_src, p1_src,
                      diff0, diff1);
            DUP2_ARG2(__lsx_vadd_h, __lsx_vslli_h(diff0, 3), diff0,
                      __lsx_vslli_h(diff1, 1), diff1, diff0, diff1);
            delta0 = __lsx_vsub_h(diff0, diff1);
            delta0 = __lsx_vsrari_h(delta0, 4);
            temp1 = __lsx_vadd_h(__lsx_vslli_h(tc_pos, 3),
                                 __lsx_vslli_h(tc_pos, 1));
            abs_delta0 = __lsx_vadda_h(delta0, zero);
            abs_delta0 = __lsx_vsle_hu(temp1, abs_delta0);
            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);

            delta0 = __lsx_vclip_h(delta0, tc_neg, tc_pos);
            temp2 = __lsx_vadd_h(delta0, p0_src);
            temp2 = __lsx_vclip255_h(temp2);
            temp0 = __lsx_vbitsel_v(temp2, p0_src, p_is_pcm_vec);

            temp2 = __lsx_vsub_h(q0_src, delta0);
            temp2 = __lsx_vclip255_h(temp2);
            temp2 = __lsx_vbitsel_v(temp2, q0_src, q_is_pcm_vec);

            tmp = (beta + (beta >> 1)) >> 3;
            DUP2_ARG1(__lsx_vreplgr2vr_d, dp00 + dp30 < tmp, dp04 + dp34 < tmp,
                      cmp0, cmp1);
            cmp0 = __lsx_vpackev_d(cmp1, cmp0);
            p_is_pcm_vec = __lsx_vor_v(p_is_pcm_vec, __lsx_vseqi_d(cmp0, 0));
            DUP2_ARG1(__lsx_vreplgr2vr_d, dq00 + dq30 < tmp, dq04 + dq34 < tmp,
                      cmp0, cmp1);
            cmp0 = __lsx_vpackev_d(cmp1, cmp0);
            q_is_pcm_vec = __lsx_vor_v(q_is_pcm_vec, __lsx_vseqi_d(cmp0, 0));

            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vavgr_hu, p2_src, p0_src, q0_src, q2_src,
                      delta1, delta2);
            DUP2_ARG2(__lsx_vsub_h, delta1, p1_src, delta2, q1_src,
                      delta1, delta2);
            delta1 = __lsx_vadd_h(delta1, delta0);
            delta2 = __lsx_vsub_h(delta2, delta0);
            DUP2_ARG2(__lsx_vsrai_h, delta1, 1, delta2, 1, delta1, delta2);
            DUP2_ARG3(__lsx_vclip_h, delta1, tc_neg, tc_pos, delta2, tc_neg,
                      tc_pos, delta1, delta2);
            DUP2_ARG2(__lsx_vadd_h, p1_src, delta1, q1_src, delta2,
                      delta1, delta2);
            DUP2_ARG1(__lsx_vclip255_h, delta1, delta2, delta1, delta2);
            DUP2_ARG3(__lsx_vbitsel_v, delta1, p1_src, p_is_pcm_vec, delta2,
                      q1_src, q_is_pcm_vec, delta1, delta2);
            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);
            DUP4_ARG3(__lsx_vbitsel_v, delta1, p1_src, abs_delta0, delta2,
                      q1_src, abs_delta0, temp0, p0_src, abs_delta0, temp2,
                      q0_src, abs_delta0, delta1, delta2, temp0, temp2);
            /* weak filter ends */

            /* pack weak results to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, delta1, p2_src, temp2, temp0,
                      dst3, dst4);
            dst5 = __lsx_vpickev_b(q2_src, delta2);

            /* select between weak or strong */
            DUP2_ARG3(__lsx_vbitsel_v, dst0, dst3, cmp2, dst1, dst4, cmp2,
                      dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, dst5, cmp2);

            /* pack src to 8 bit */
            DUP2_ARG2(__lsx_vpickev_b, p1_src, p2_src, q0_src, p0_src,
                      dst3, dst4);
            dst5 = __lsx_vpickev_b(q2_src, q1_src);

            cmp3 = __lsx_vnor_v(cmp3, cmp3);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, dst3, cmp3, dst1, dst4, cmp3,
                      dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, dst5, cmp3);

            __lsx_vstelm_d(dst0, p2, 0, 0);
            __lsx_vstelm_d(dst0, p2 + stride, 0, 1);
            __lsx_vstelm_d(dst1, p2 + stride_2x, 0, 0);
            __lsx_vstelm_d(dst1, p2 + stride_3x, 0, 1);
            __lsx_vstelm_d(dst2, p2 + stride_4x, 0, 0);
            __lsx_vstelm_d(dst2, p2 + stride_4x + stride, 0, 1);
        }
    }
}

void ff_hevc_loop_filter_luma_v_8_lsx(uint8_t *src, ptrdiff_t stride,
                                      int32_t beta, const int32_t *tc,
                                      const uint8_t *p_is_pcm, const uint8_t *q_is_pcm)
{
    ptrdiff_t stride_2x = (stride << 1);
    ptrdiff_t stride_4x = (stride << 2);
    ptrdiff_t stride_3x = stride_2x + stride;
    uint8_t *p3 = src;
    uint8_t *p2 = src + stride_3x;
    uint8_t *p1 = src + stride_4x;
    uint8_t *p0 = src + stride_4x + stride_3x;
    uint8_t flag0, flag1;
    int32_t dp00, dq00, dp30, dq30, d00, d30;
    int32_t d0030, d0434;
    int32_t dp04, dq04, dp34, dq34, d04, d34;
    int32_t tc0, p_is_pcm0, q_is_pcm0, beta30, beta20, tc250;
    int32_t tc4, p_is_pcm4, q_is_pcm4, tc254, tmp;

    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    __m128i cmp0, cmp1, cmp2, p_is_pcm_vec, q_is_pcm_vec;
    __m128i cmp3;
    __m128i temp0, temp1;
    __m128i temp2;
    __m128i tc_pos, tc_neg;
    __m128i diff0, diff1, delta0, delta1, delta2, abs_delta0;
    __m128i zero = {0};
    __m128i p3_src, p2_src, p1_src, p0_src, q0_src, q1_src, q2_src, q3_src;

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

    DUP2_ARG1(__lsx_vreplgr2vr_d, p_is_pcm0, p_is_pcm4, cmp0, cmp1);
    p_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
    p_is_pcm_vec = __lsx_vseqi_d(p_is_pcm_vec, 0);

    d0030 = (d00 + d30) >= beta;
    d0434 = (d04 + d34) >= beta;

    DUP2_ARG1(__lsx_vreplgr2vr_d, d0030, d0434, cmp0, cmp1);
    cmp3 = __lsx_vpackev_d(cmp1, cmp0);
    cmp3 = __lsx_vseqi_d(cmp3, 0);

    if ((!p_is_pcm0 || !p_is_pcm4 || !q_is_pcm0 || !q_is_pcm4) &&
        (!d0030 || !d0434)) {
        src -= 4;
        DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride_2x, 0,
                  src + stride_3x, 0, p3_src, p2_src, p1_src, p0_src);
        src += stride_4x;
        DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride_2x, 0,
                  src + stride_3x, 0, q0_src, q1_src, q2_src, q3_src);
        src -= stride_4x;

        DUP2_ARG1(__lsx_vreplgr2vr_d, q_is_pcm0, q_is_pcm4, cmp0, cmp1);
        q_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
        q_is_pcm_vec = __lsx_vseqi_d(q_is_pcm_vec, 0);

        tc0 = tc[0];
        beta30 = beta >> 3;
        beta20 = beta >> 2;
        tc250 = (((tc0 << 2) + tc0 + 1) >> 1);
        tc4 = tc[1];
        tc254 = (((tc4 << 2) + tc4 + 1) >> 1);
        DUP2_ARG1( __lsx_vreplgr2vr_h, tc0 << 1, tc4 << 1, cmp0, cmp1);
        tc_pos = __lsx_vpackev_d(cmp1, cmp0);
        LSX_TRANSPOSE8x8_B(p3_src, p2_src, p1_src, p0_src, q0_src, q1_src,
                           q2_src, q3_src, p3_src, p2_src, p1_src, p0_src,
                           q0_src, q1_src, q2_src, q3_src);

        flag0 = abs(p3[-4] - p3[-1]) + abs(p3[3] - p3[0]) < beta30 &&
                abs(p3[-1] - p3[0]) < tc250;
        flag0 = flag0 && (abs(p2[-4] - p2[-1]) + abs(p2[3] - p2[0]) < beta30 &&
                abs(p2[-1] - p2[0]) < tc250 && (d00 << 1) < beta20 &&
                (d30 << 1) < beta20);
        cmp0 = __lsx_vreplgr2vr_d(flag0);
        DUP4_ARG2(__lsx_vilvl_b, zero, p3_src, zero, p2_src, zero, p1_src, zero,
                  p0_src, p3_src, p2_src, p1_src, p0_src);

        flag1 = abs(p1[-4] - p1[-1]) + abs(p1[3] - p1[0]) < beta30 &&
                abs(p1[-1] - p1[0]) < tc254;
        flag1 = flag1 && (abs(p0[-4] - p0[-1]) + abs(p0[3] - p0[0]) < beta30 &&
                abs(p0[-1] - p0[0]) < tc254 && (d04 << 1) < beta20 &&
                (d34 << 1) < beta20);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0_src, zero, q1_src, zero, q2_src, zero,
                  q3_src, q0_src, q1_src, q2_src, q3_src);

        cmp1 = __lsx_vreplgr2vr_d(flag1);
        cmp2 = __lsx_vpackev_d(cmp1, cmp0);
        cmp2 = __lsx_vseqi_d(cmp2, 0);

        if (flag0 && flag1) { /* strong only */
            /* strong filter */
            tc_neg = __lsx_vneg_h(tc_pos);
            /* p part */
            DUP2_ARG2(__lsx_vadd_h, p1_src, p0_src, temp0, q0_src,
                      temp0, temp0);
            temp1 = __lsx_vadd_h(p3_src, p2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst0 = __lsx_vadd_h(temp2, p2_src);

            temp1 = __lsx_vadd_h(temp0, p2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, p1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst1 = __lsx_vadd_h(temp2, p1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, q1_src, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst2 = __lsx_vadd_h(temp2, p0_src);

            p_is_pcm_vec = __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, p2_src, p_is_pcm_vec, dst1, p1_src,
                      p_is_pcm_vec, dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, p0_src, p_is_pcm_vec);

            /* q part */
            DUP2_ARG2(__lsx_vadd_h, q1_src, p0_src, temp0, q0_src,
                      temp0, temp0);
            temp1 = __lsx_vadd_h(q3_src, q2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, q2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst5 = __lsx_vadd_h(temp2, q2_src);

            temp1 = __lsx_vadd_h(temp0, q2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, q1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst4 = __lsx_vadd_h(temp2, q1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p1_src, temp1, q2_src, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst3 = __lsx_vadd_h(temp2, q0_src);

            q_is_pcm_vec = __lsx_vnor_v(q_is_pcm_vec, q_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst3, q0_src, q_is_pcm_vec, dst4, q1_src,
                      q_is_pcm_vec, dst3, dst4);
            dst5 = __lsx_vbitsel_v(dst5, q2_src, q_is_pcm_vec);
            /* strong filter ends */
        } else if (flag0 == flag1) { /* weak only */
            /* weak filter */
            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vsub_h, q0_src, p0_src, q1_src, p1_src,
                      diff0, diff1);
            DUP2_ARG2(__lsx_vadd_h, __lsx_vslli_h(diff0, 3), diff0,
                      __lsx_vslli_h(diff1, 1), diff1, diff0, diff1);
            delta0 = __lsx_vsub_h(diff0, diff1);
            delta0 = __lsx_vsrari_h(delta0, 4);
            temp1 = __lsx_vadd_h(__lsx_vslli_h(tc_pos, 3),
                                 __lsx_vslli_h(tc_pos, 1));
            abs_delta0 = __lsx_vadda_h(delta0, zero);
            abs_delta0 = __lsx_vsle_hu(temp1, abs_delta0);
            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);

            delta0 = __lsx_vclip_h(delta0, tc_neg, tc_pos);
            temp2 = __lsx_vadd_h(delta0, p0_src);
            temp2 = __lsx_vclip255_h(temp2);
            p_is_pcm_vec = __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec);
            temp0 = __lsx_vbitsel_v(temp2, p0_src, p_is_pcm_vec);

            temp2 = __lsx_vsub_h(q0_src, delta0);
            temp2 = __lsx_vclip255_h(temp2);
            q_is_pcm_vec = __lsx_vnor_v(q_is_pcm_vec, q_is_pcm_vec);
            temp2 = __lsx_vbitsel_v(temp2, q0_src, q_is_pcm_vec);

            tmp = ((beta + (beta >> 1)) >> 3);
            DUP2_ARG1(__lsx_vreplgr2vr_d, !p_is_pcm0 && ((dp00 + dp30) < tmp),
                      !p_is_pcm4 && ((dp04 + dp34) < tmp), cmp0, cmp1);
            p_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
            p_is_pcm_vec = __lsx_vseqi_d(p_is_pcm_vec, 0);

            DUP2_ARG1(__lsx_vreplgr2vr_h, (!q_is_pcm0) && (dq00 + dq30 < tmp),
                      (!q_is_pcm4) && (dq04 + dq34 < tmp), cmp0, cmp1);
            q_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
            q_is_pcm_vec = __lsx_vseqi_d(q_is_pcm_vec, 0);
            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vavgr_hu, p2_src, p0_src, q0_src, q2_src,
                      delta1, delta2);
            DUP2_ARG2(__lsx_vsub_h, delta1, p1_src, delta2, q1_src,
                      delta1, delta2);
            delta1 = __lsx_vadd_h(delta1, delta0);
            delta2 = __lsx_vsub_h(delta2, delta0);
            DUP2_ARG2(__lsx_vsrai_h, delta1, 1, delta2, 1, delta1, delta2);
            DUP2_ARG3(__lsx_vclip_h, delta1, tc_neg, tc_pos, delta2, tc_neg,
                      tc_pos, delta1, delta2);
            DUP2_ARG2(__lsx_vadd_h, p1_src, delta1, q1_src, delta2,
                      delta1, delta2);
            DUP2_ARG1(__lsx_vclip255_h, delta1, delta2, delta1, delta2);
            DUP2_ARG3(__lsx_vbitsel_v, delta1, p1_src, p_is_pcm_vec, delta2,
                      q1_src, q_is_pcm_vec, delta1, delta2);

            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);
            DUP4_ARG3(__lsx_vbitsel_v, delta1, p1_src, abs_delta0, temp0,
                      p0_src, abs_delta0, temp2, q0_src, abs_delta0, delta2,
                      q1_src, abs_delta0, dst0, dst1, dst2, dst3);
            /* weak filter ends */

            cmp3 = __lsx_vnor_v(cmp3, cmp3);
            DUP4_ARG3(__lsx_vbitsel_v, dst0, p1_src, cmp3, dst1, p0_src,
                      cmp3, dst2, q0_src, cmp3, dst3, q1_src, cmp3,
                      dst0, dst1, dst2, dst3);
            DUP2_ARG2(__lsx_vpickev_b, dst2, dst0, dst3, dst1, dst0, dst1);

            /* transpose */
            dst4 = __lsx_vilvl_b(dst1, dst0);
            dst5 = __lsx_vilvh_b(dst1, dst0);
            dst0 = __lsx_vilvl_h(dst5, dst4);
            dst1 = __lsx_vilvh_h(dst5, dst4);

            src += 2;
            __lsx_vstelm_w(dst0, src, 0, 0);
            __lsx_vstelm_w(dst0, src + stride, 0, 1);
            __lsx_vstelm_w(dst0, src + stride_2x, 0, 2);
            __lsx_vstelm_w(dst0, src + stride_3x, 0, 3);
            src += stride_4x;
            __lsx_vstelm_w(dst1, src, 0, 0);
            __lsx_vstelm_w(dst1, src + stride, 0, 1);
            __lsx_vstelm_w(dst1, src + stride_2x, 0, 2);
            __lsx_vstelm_w(dst1, src + stride_3x, 0, 3);
            return;
        } else { /* strong + weak */
            /* strong filter */
            tc_neg = __lsx_vneg_h(tc_pos);

            /* p part */
            DUP2_ARG2(__lsx_vadd_h, p1_src, p0_src, temp0, q0_src,
                      temp0, temp0);

            temp1 = __lsx_vadd_h(p3_src, p2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst0 = __lsx_vadd_h(temp2, p2_src);

            temp1 = __lsx_vadd_h(temp0, p2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, p1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst1 = __lsx_vadd_h(temp2, p1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p2_src, temp1, q1_src, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, p0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst2 = __lsx_vadd_h(temp2, p0_src);

            p_is_pcm_vec = __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst0, p2_src, p_is_pcm_vec, dst1, p1_src,
                      p_is_pcm_vec, dst0, dst1);
            dst2 = __lsx_vbitsel_v(dst2, p0_src, p_is_pcm_vec);

            /* q part */
            DUP2_ARG2(__lsx_vadd_h, q1_src, p0_src, temp0, q0_src, temp0, temp0);
            temp1 = __lsx_vadd_h(q3_src, q2_src);
            temp1 = __lsx_vslli_h(temp1, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, q2_src, temp1, temp0, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q2_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst5 = __lsx_vadd_h(temp2, q2_src);

            temp1 = __lsx_vadd_h(temp0, q2_src);
            temp1 = __lsx_vsrari_h(temp1, 2);
            temp2 = __lsx_vsub_h(temp1, q1_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst4 = __lsx_vadd_h(temp2, q1_src);

            temp1 = __lsx_vslli_h(temp0, 1);
            DUP2_ARG2(__lsx_vadd_h, temp1, p1_src, temp1, q2_src, temp1, temp1);
            temp1 = __lsx_vsrari_h(temp1, 3);
            temp2 = __lsx_vsub_h(temp1, q0_src);
            temp2 = __lsx_vclip_h(temp2, tc_neg, tc_pos);
            dst3 = __lsx_vadd_h(temp2, q0_src);

            q_is_pcm_vec = __lsx_vnor_v(q_is_pcm_vec, q_is_pcm_vec);
            DUP2_ARG3(__lsx_vbitsel_v, dst3, q0_src, q_is_pcm_vec, dst4, q1_src,
                      q_is_pcm_vec, dst3, dst4);
            dst5 = __lsx_vbitsel_v(dst5, q2_src, q_is_pcm_vec);
            /* strong filter ends */

            /* weak filter */
            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vsub_h, q0_src, p0_src, q1_src, p1_src,
                      diff0, diff1);
            DUP2_ARG2(__lsx_vadd_h, __lsx_vslli_h(diff0, 3), diff0,
                      __lsx_vslli_h(diff1, 1), diff1, diff0, diff1);
            delta0 = __lsx_vsub_h(diff0, diff1);
            delta0 = __lsx_vsrari_h(delta0, 4);

            temp1 = __lsx_vadd_h(__lsx_vslli_h(tc_pos, 3),
                    __lsx_vslli_h(tc_pos, 1));
            abs_delta0 = __lsx_vadda_h(delta0, zero);
            abs_delta0 = __lsx_vsle_hu(temp1, abs_delta0);
            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);
            delta0 = __lsx_vclip_h(delta0, tc_neg, tc_pos);
            temp2 = __lsx_vadd_h(delta0, p0_src);
            temp2 = __lsx_vclip255_h(temp2);
            temp0 = __lsx_vbitsel_v(temp2, p0_src, p_is_pcm_vec);
            temp2 = __lsx_vsub_h(q0_src, delta0);
            temp2 = __lsx_vclip255_h(temp2);
            temp2 = __lsx_vbitsel_v(temp2, q0_src, q_is_pcm_vec);

            tmp = (beta + (beta >> 1)) >> 3;
            DUP2_ARG1(__lsx_vreplgr2vr_d, !p_is_pcm0 && ((dp00 + dp30) < tmp),
                      !p_is_pcm4 && ((dp04 + dp34) < tmp), cmp0, cmp1);
            p_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
            p_is_pcm_vec = __lsx_vseqi_d(p_is_pcm_vec, 0);

            DUP2_ARG1(__lsx_vreplgr2vr_h, (!q_is_pcm0) && (dq00 + dq30 < tmp),
                      (!q_is_pcm4) && (dq04 + dq34 < tmp), cmp0, cmp1);
            q_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
            q_is_pcm_vec = __lsx_vseqi_d(q_is_pcm_vec, 0);
            tc_pos = __lsx_vsrai_h(tc_pos, 1);
            tc_neg = __lsx_vneg_h(tc_pos);

            DUP2_ARG2(__lsx_vavgr_hu, p2_src, p0_src, q0_src, q2_src,
                      delta1, delta2);
            DUP2_ARG2(__lsx_vsub_h, delta1, p1_src, delta2, q1_src,
                      delta1, delta2);
            delta1 = __lsx_vadd_h(delta1, delta0);
            delta2 = __lsx_vsub_h(delta2, delta0);
            DUP2_ARG2(__lsx_vsrai_h, delta1, 1, delta2, 1, delta1, delta2);
            DUP2_ARG3(__lsx_vclip_h, delta1, tc_neg, tc_pos, delta2, tc_neg,
                      tc_pos, delta1, delta2);
            DUP2_ARG2(__lsx_vadd_h, p1_src, delta1, q1_src, delta2,
                      delta1, delta2);
            DUP2_ARG1(__lsx_vclip255_h, delta1, delta2, delta1, delta2);
            DUP2_ARG3(__lsx_vbitsel_v, delta1, p1_src, p_is_pcm_vec, delta2,
                      q1_src, q_is_pcm_vec, delta1, delta2);

            abs_delta0 = __lsx_vnor_v(abs_delta0, abs_delta0);
            DUP4_ARG3(__lsx_vbitsel_v, delta1, p1_src, abs_delta0, delta2,
                      q1_src, abs_delta0, temp0, p0_src, abs_delta0, temp2,
                      q0_src, abs_delta0, delta1, delta2, temp0, temp2);
            /* weak filter ends*/

            /* select between weak or strong */
            DUP4_ARG3(__lsx_vbitsel_v, dst0, p2_src, cmp2, dst1, delta1,
                      cmp2, dst2, temp0, cmp2, dst3, temp2, cmp2,
                      dst0, dst1, dst2, dst3);
            DUP2_ARG3(__lsx_vbitsel_v, dst4, delta2, cmp2, dst5, q2_src, cmp2,
                      dst4, dst5);
        }

        cmp3 = __lsx_vnor_v(cmp3, cmp3);
        DUP4_ARG3(__lsx_vbitsel_v, dst0, p2_src, cmp3, dst1, p1_src, cmp3, dst2,
                  p0_src, cmp3, dst3, q0_src, cmp3, dst0, dst1, dst2, dst3);
        DUP2_ARG3(__lsx_vbitsel_v, dst4, q1_src, cmp3, dst5, q2_src, cmp3,
                  dst4, dst5);

        /* pack results to 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, dst2, dst0, dst3, dst1, dst4, dst4, dst5,
                  dst5, dst0, dst1, dst2, dst3);

        /* transpose */
        DUP2_ARG2(__lsx_vilvl_b, dst1, dst0, dst3, dst2, dst4, dst6);
        DUP2_ARG2(__lsx_vilvh_b, dst1, dst0, dst3, dst2, dst5, dst7);
        DUP2_ARG2(__lsx_vilvl_h, dst5, dst4, dst7, dst6, dst0, dst2);
        DUP2_ARG2(__lsx_vilvh_h, dst5, dst4, dst7, dst6, dst1, dst3);

        src += 1;
        __lsx_vstelm_w(dst0, src, 0, 0);
        __lsx_vstelm_h(dst2, src, 4, 0);
        src += stride;
        __lsx_vstelm_w(dst0, src, 0, 1);
        __lsx_vstelm_h(dst2, src, 4, 2);
        src += stride;

        __lsx_vstelm_w(dst0, src, 0, 2);
        __lsx_vstelm_h(dst2, src, 4, 4);
        src += stride;
        __lsx_vstelm_w(dst0, src, 0, 3);
        __lsx_vstelm_h(dst2, src, 4, 6);
        src += stride;

        __lsx_vstelm_w(dst1, src, 0, 0);
        __lsx_vstelm_h(dst3, src, 4, 0);
        src += stride;
        __lsx_vstelm_w(dst1, src, 0, 1);
        __lsx_vstelm_h(dst3, src, 4, 2);
        src += stride;

        __lsx_vstelm_w(dst1, src, 0, 2);
        __lsx_vstelm_h(dst3, src, 4, 4);
        src += stride;
        __lsx_vstelm_w(dst1, src, 0, 3);
        __lsx_vstelm_h(dst3, src, 4, 6);
    }
}

void ff_hevc_loop_filter_chroma_h_8_lsx(uint8_t *src, ptrdiff_t stride,
                                        const int32_t *tc, const uint8_t *p_is_pcm,
                                        const uint8_t *q_is_pcm)
{
    uint8_t *p1_ptr = src - (stride << 1);
    uint8_t *p0_ptr = src - stride;
    uint8_t *q0_ptr = src;
    uint8_t *q1_ptr = src + stride;
    __m128i cmp0, cmp1, p_is_pcm_vec, q_is_pcm_vec;
    __m128i p1, p0, q0, q1;
    __m128i tc_pos, tc_neg;
    __m128i zero = {0};
    __m128i temp0, temp1, delta;

    if (!(tc[0] <= 0) || !(tc[1] <= 0)) {
        DUP2_ARG1(__lsx_vreplgr2vr_h, tc[0], tc[1], cmp0, cmp1);
        tc_pos = __lsx_vpackev_d(cmp1, cmp0);
        tc_neg = __lsx_vneg_h(tc_pos);
        DUP2_ARG1(__lsx_vreplgr2vr_d, p_is_pcm[0], p_is_pcm[1], cmp0, cmp1);
        p_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
        p_is_pcm_vec = __lsx_vseqi_d(p_is_pcm_vec, 0);

        DUP2_ARG1(__lsx_vreplgr2vr_d, q_is_pcm[0], q_is_pcm[1], cmp0, cmp1);
        q_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
        q_is_pcm_vec = __lsx_vseqi_d(q_is_pcm_vec, 0);

        DUP4_ARG2(__lsx_vld, p1_ptr, 0, p0_ptr, 0, q0_ptr, 0, q1_ptr, 0,
                  p1, p0, q0, q1);
        DUP4_ARG2(__lsx_vilvl_b, zero, p1, zero, p0, zero, q0, zero, q1,
                  p1, p0, q0, q1);
        DUP2_ARG2(__lsx_vsub_h, q0, p0, p1, q1, temp0, temp1);
        temp0 = __lsx_vslli_h(temp0, 2);
        temp0 = __lsx_vadd_h(temp0, temp1);
        delta = __lsx_vsrari_h(temp0, 3);
        delta = __lsx_vclip_h(delta, tc_neg, tc_pos);
        temp0 = __lsx_vadd_h(p0, delta);
        temp0 = __lsx_vclip255_h(temp0);
        p_is_pcm_vec = __lsx_vnor_v(p_is_pcm_vec, p_is_pcm_vec);
        temp0 = __lsx_vbitsel_v(temp0, p0, p_is_pcm_vec);

        temp1 = __lsx_vsub_h(q0, delta);
        temp1 = __lsx_vclip255_h(temp1);
        q_is_pcm_vec = __lsx_vnor_v(q_is_pcm_vec, q_is_pcm_vec);
        temp1 = __lsx_vbitsel_v(temp1, q0, q_is_pcm_vec);

        tc_pos = __lsx_vslei_d(tc_pos, 0);
        DUP2_ARG3(__lsx_vbitsel_v, temp0, p0, tc_pos, temp1, q0, tc_pos,
                  temp0, temp1);
        temp0 = __lsx_vpickev_b(temp1, temp0);
        __lsx_vstelm_d(temp0, p0_ptr, 0, 0);
        __lsx_vstelm_d(temp0, p0_ptr + stride, 0, 1);
    }
}

void ff_hevc_loop_filter_chroma_v_8_lsx(uint8_t *src, ptrdiff_t stride,
                                        const int32_t *tc, const uint8_t *p_is_pcm,
                                        const uint8_t *q_is_pcm)
{
    ptrdiff_t stride_2x = (stride << 1);
    ptrdiff_t stride_4x = (stride << 2);
    ptrdiff_t stride_3x = stride_2x + stride;
    __m128i cmp0, cmp1, p_is_pcm_vec, q_is_pcm_vec;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i p1, p0, q0, q1;
    __m128i tc_pos, tc_neg;
    __m128i zero = {0};
    __m128i temp0, temp1, delta;

    if (!(tc[0] <= 0) || !(tc[1] <= 0)) {
        DUP2_ARG1(__lsx_vreplgr2vr_h, tc[0], tc[1], cmp0, cmp1);
        tc_pos = __lsx_vpackev_d(cmp1, cmp0);
        tc_neg = __lsx_vneg_h(tc_pos);

        DUP2_ARG1(__lsx_vreplgr2vr_d, p_is_pcm[0], p_is_pcm[1], cmp0, cmp1);
        p_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
        p_is_pcm_vec = __lsx_vseqi_d(p_is_pcm_vec, 0);
        DUP2_ARG1(__lsx_vreplgr2vr_d, q_is_pcm[0], q_is_pcm[1], cmp0, cmp1);
        q_is_pcm_vec = __lsx_vpackev_d(cmp1, cmp0);
        q_is_pcm_vec = __lsx_vseqi_d(q_is_pcm_vec, 0);

        src -= 2;
        DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride_2x, 0,
                  src + stride_3x, 0, src0, src1, src2, src3);
        src += stride_4x;
        DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride_2x, 0,
                  src + stride_3x, 0, src4, src5, src6, src7);
        src -= stride_4x;
        LSX_TRANSPOSE8x4_B(src0, src1, src2, src3, src4, src5, src6, src7,
                           p1, p0, q0, q1);
        DUP4_ARG2(__lsx_vilvl_b, zero, p1, zero, p0, zero, q0, zero, q1,
                  p1, p0, q0, q1);

        DUP2_ARG2(__lsx_vsub_h, q0, p0, p1, q1, temp0, temp1);
        temp0 = __lsx_vslli_h(temp0, 2);
        temp0 = __lsx_vadd_h(temp0, temp1);
        delta = __lsx_vsrari_h(temp0, 3);
        delta = __lsx_vclip_h(delta, tc_neg, tc_pos);

        temp0 = __lsx_vadd_h(p0, delta);
        temp1 = __lsx_vsub_h(q0, delta);
        DUP2_ARG1(__lsx_vclip255_h, temp0, temp1, temp0, temp1);
        DUP2_ARG2(__lsx_vnor_v, p_is_pcm_vec, p_is_pcm_vec, q_is_pcm_vec,
                  q_is_pcm_vec, p_is_pcm_vec, q_is_pcm_vec);
        DUP2_ARG3(__lsx_vbitsel_v, temp0, p0, p_is_pcm_vec, temp1, q0,
                  q_is_pcm_vec, temp0, temp1);

        tc_pos = __lsx_vslei_d(tc_pos, 0);
        DUP2_ARG3(__lsx_vbitsel_v, temp0, p0, tc_pos, temp1, q0, tc_pos,
                  temp0, temp1);
        temp0 = __lsx_vpackev_b(temp1, temp0);

        src += 1;
        __lsx_vstelm_h(temp0, src, 0, 0);
        __lsx_vstelm_h(temp0, src + stride, 0, 1);
        __lsx_vstelm_h(temp0, src + stride_2x, 0, 2);
        __lsx_vstelm_h(temp0, src + stride_3x, 0, 3);
        src += stride_4x;
        __lsx_vstelm_h(temp0, src, 0, 4);
        __lsx_vstelm_h(temp0, src + stride, 0, 5);
        __lsx_vstelm_h(temp0, src + stride_2x, 0, 6);
        __lsx_vstelm_h(temp0, src + stride_3x, 0, 7);
        src -= stride_4x;
    }
}

static void hevc_sao_edge_filter_0degree_4width_lsx(uint8_t *dst,
                                                    int32_t dst_stride,
                                                    const uint8_t *src,
                                                    int32_t src_stride,
                                                    const int16_t *sao_offset_val,
                                                    int32_t height)
{
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i cmp_minus10, cmp_minus11, diff_minus10, diff_minus11;
    __m128i sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i src_minus10, src_minus11, src_plus10, offset, src0, dst0;
    __m128i const1 = __lsx_vldi(1);
    __m128i zero = {0};

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);
    src -= 1;

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src_minus10, src_minus11);

    for (height -= 2; height; height -= 2) {
        src += src_stride_2x;
        src_minus10 = __lsx_vpickev_d(src_minus11, src_minus10);
        src0 = __lsx_vshuf_b(zero, src_minus10, shuf1);
        src_plus10 = __lsx_vshuf_b(zero, src_minus10, shuf2);

        DUP2_ARG2(__lsx_vseq_b, src0, src_minus10, src0, src_plus10,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src0, src_minus10, src0, src_plus10,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
        diff_minus11, const1, cmp_minus11, diff_minus10, diff_minus11);

        offset = __lsx_vadd_b(diff_minus10, diff_minus11);
        offset = __lsx_vaddi_bu(offset, 2);

        /* load in advance */
        DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0,
                  src_minus10, src_minus11);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset,
                  sao_offset, sao_offset, offset, offset, offset);
        src0 = __lsx_vxori_b(src0, 128);
        dst0 = __lsx_vsadd_b(src0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);

        __lsx_vstelm_w(dst0, dst, 0, 0);
        __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
        dst += dst_stride_2x;
    }

    src_minus10 = __lsx_vpickev_d(src_minus11, src_minus10);
    src0 = __lsx_vshuf_b(zero, src_minus10, shuf1);
    src_plus10 = __lsx_vshuf_b(zero, src_minus10, shuf2);

    DUP2_ARG2(__lsx_vseq_b, src0, src_minus10, src0, src_plus10, cmp_minus10,
              cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src0, src_minus10, src0, src_plus10, cmp_minus10,
              cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    offset = __lsx_vadd_b(diff_minus10, diff_minus11);
    offset = __lsx_vaddi_bu(offset, 2);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset, sao_offset,
              offset, offset, offset);
    src0 = __lsx_vxori_b(src0, 128);
    dst0 = __lsx_vsadd_b(src0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_w(dst0, dst, 0, 0);
    __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
}

static void hevc_sao_edge_filter_0degree_8width_lsx(uint8_t *dst,
                                                    int32_t dst_stride,
                                                    const uint8_t *src,
                                                    int32_t src_stride,
                                                    const int16_t *sao_offset_val,
                                                    int32_t height)
{
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i cmp_minus10, cmp_minus11, diff_minus10, diff_minus11;
    __m128i src0, src1, dst0, src_minus10, src_minus11, src_plus10, src_plus11;
    __m128i offset, sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i zeros = {0};

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);
    src -= 1;

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0, src_minus10, src_minus11);

    for (height -= 2; height; height -= 2) {
        src += src_stride_2x;
        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf1, zeros,
                  src_minus11, shuf1, src0, src1);
        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf2, zeros,
                  src_minus11, shuf2, src_plus10, src_plus11);
        DUP2_ARG2(__lsx_vpickev_d, src_minus11, src_minus10, src_plus11,
                  src_plus10, src_minus10, src_plus10);
        src0 = __lsx_vpickev_d(src1, src0);

        DUP2_ARG2(__lsx_vseq_b, src0, src_minus10, src0, src_plus10,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src0, src_minus10, src0, src_plus10,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
        diff_minus11, const1, cmp_minus11, diff_minus10, diff_minus11);

        offset = __lsx_vadd_b(diff_minus10, diff_minus11);
        offset = __lsx_vaddi_bu(offset, 2);

        /* load in advance */
        DUP2_ARG2(__lsx_vld, src, 0, src + src_stride, 0,
                  src_minus10, src_minus11);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);
        src0 = __lsx_vxori_b(src0, 128);
        dst0 = __lsx_vsadd_b(src0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);

        __lsx_vstelm_d(dst0, dst, 0, 0);
        __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
        dst += dst_stride_2x;
    }

    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf1, zeros, src_minus11,
              shuf1, src0, src1);
    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf2, zeros, src_minus11,
              shuf2, src_plus10, src_plus11);
    DUP2_ARG2(__lsx_vpickev_d, src_minus11, src_minus10, src_plus11,
              src_plus10, src_minus10, src_plus10);
    src0 =  __lsx_vpickev_d(src1, src0);

    DUP2_ARG2(__lsx_vseq_b, src0, src_minus10, src0, src_plus10, cmp_minus10,
              cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src0, src_minus10, src0, src_plus10, cmp_minus10,
              cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    offset = __lsx_vadd_b(diff_minus10, diff_minus11);
    offset = __lsx_vaddi_bu(offset, 2);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    src0 = __lsx_vxori_b(src0, 128);
    dst0 = __lsx_vsadd_b(src0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_d(dst0, dst, 0, 0);
    __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
}

static void hevc_sao_edge_filter_0degree_16multiple_lsx(uint8_t *dst,
                                                        int32_t dst_stride,
                                                        const uint8_t *src,
                                                        int32_t src_stride,
                                                        const int16_t *sao_offset_val,
                                                        int32_t width,
                                                        int32_t height)
{
    uint8_t *dst_ptr;
    const uint8_t *src_minus1;
    int32_t v_cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i sao_offset;
    __m128i cmp_minus10, cmp_plus10, diff_minus10, diff_plus10, cmp_minus11;
    __m128i cmp_plus11, diff_minus11, diff_plus11, cmp_minus12, cmp_plus12;
    __m128i diff_minus12, diff_plus12, cmp_minus13, cmp_plus13, diff_minus13;
    __m128i diff_plus13;
    __m128i src10, src11, src12, src13, dst0, dst1, dst2, dst3;
    __m128i src_minus10, src_minus11, src_minus12, src_minus13;
    __m128i offset_mask0, offset_mask1, offset_mask2, offset_mask3;
    __m128i src_zero0, src_zero1, src_zero2, src_zero3;
    __m128i src_plus10, src_plus11, src_plus12, src_plus13;

    sao_offset = __lsx_vld(sao_offset_val, 0);
    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);

    for (; height; height -= 4) {
        src_minus1 = src - 1;
        src_minus10 = __lsx_vld(src_minus1, 0);
        DUP2_ARG2(__lsx_vldx, src_minus1, src_stride, src_minus1,
                  src_stride_2x, src_minus11, src_minus12);
        src_minus13 = __lsx_vldx(src_minus1, src_stride_3x);

        for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
            src_minus1 += 16;
            dst_ptr = dst + v_cnt;
            src10 = __lsx_vld(src_minus1, 0);
            DUP2_ARG2(__lsx_vldx, src_minus1, src_stride, src_minus1,
                      src_stride_2x, src11, src12);
            src13 = __lsx_vldx(src_minus1, src_stride_3x);
            DUP4_ARG3(__lsx_vshuf_b, src10, src_minus10, shuf1, src11,
                      src_minus11, shuf1, src12, src_minus12, shuf1, src13,
                      src_minus13, shuf1, src_zero0, src_zero1,
                      src_zero2, src_zero3);
            DUP4_ARG3(__lsx_vshuf_b, src10, src_minus10, shuf2, src11,
                      src_minus11, shuf2, src12, src_minus12, shuf2, src13,
                      src_minus13, shuf2, src_plus10, src_plus11,
                      src_plus12, src_plus13);
            DUP4_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero0,
                      src_plus10, src_zero1, src_minus11, src_zero1, src_plus11,
                      cmp_minus10, cmp_plus10, cmp_minus11, cmp_plus11);
            DUP4_ARG2(__lsx_vseq_b, src_zero2, src_minus12, src_zero2,
                      src_plus12, src_zero3, src_minus13, src_zero3, src_plus13,
                      cmp_minus12, cmp_plus12, cmp_minus13, cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13);
            DUP4_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero0,
                      src_plus10, src_zero1, src_minus11, src_zero1, src_plus11,
                      cmp_minus10, cmp_plus10, cmp_minus11, cmp_plus11);
            DUP4_ARG2(__lsx_vsle_bu, src_zero2, src_minus12, src_zero2,
                      src_plus12, src_zero3, src_minus13, src_zero3, src_plus13,
                      cmp_minus12, cmp_plus12, cmp_minus13, cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, cmp_minus10, cmp_plus10, cmp_minus11,
                      cmp_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
                      diff_plus10, const1, cmp_plus10, diff_minus11, const1,
                      cmp_minus11, diff_plus11, const1, cmp_plus11,
                      diff_minus10, diff_plus10, diff_minus11, diff_plus11);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus12, const1, cmp_minus12,
                      diff_plus12, const1, cmp_plus12, diff_minus13, const1,
                      cmp_minus13, diff_plus13, const1, cmp_plus13,
                      diff_minus12, diff_plus12, diff_minus13, diff_plus13);

            DUP4_ARG2(__lsx_vadd_b, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13, offset_mask0, offset_mask1, offset_mask2,
                      offset_mask3);
            DUP4_ARG2(__lsx_vaddi_bu, offset_mask0, 2, offset_mask1, 2,
                      offset_mask2, 2, offset_mask3, 2, offset_mask0,
                      offset_mask1, offset_mask2, offset_mask3);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask0,
                      sao_offset, sao_offset, offset_mask0, offset_mask0,
                      offset_mask0);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask1,
                      sao_offset, sao_offset, offset_mask1, offset_mask1,
                      offset_mask1);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask2,
                      sao_offset, sao_offset, offset_mask2, offset_mask2,
                      offset_mask2);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask3,
                      sao_offset, sao_offset, offset_mask3, offset_mask3,
                      offset_mask3);

            DUP4_ARG2(__lsx_vxori_b, src_zero0, 128, src_zero1, 128,
                      src_zero2, 128, src_zero3, 128, src_zero0, src_zero1,
                      src_zero2, src_zero3);
            DUP4_ARG2(__lsx_vsadd_b, src_zero0, offset_mask0, src_zero1,
                      offset_mask1, src_zero2, offset_mask2, src_zero3,
                      offset_mask3, dst0, dst1, dst2, dst3);
            DUP4_ARG2(__lsx_vxori_b, dst0, 128, dst1, 128, dst2, 128, dst3,
                      128, dst0, dst1, dst2, dst3);

            src_minus10 = src10;
            src_minus11 = src11;
            src_minus12 = src12;
            src_minus13 = src13;

            __lsx_vst(dst0, dst_ptr, 0);
            __lsx_vst(dst1, dst_ptr + dst_stride, 0);
            __lsx_vst(dst2, dst_ptr + dst_stride_2x, 0);
            __lsx_vst(dst3, dst_ptr + dst_stride_3x, 0);
        }
        src += src_stride_4x;
        dst += dst_stride_4x;
    }
}

static void hevc_sao_edge_filter_90degree_4width_lsx(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     const uint8_t *src,
                                                     int32_t src_stride,
                                                     const int16_t *sao_offset_val,
                                                     int32_t height)
{
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i dst0;
    __m128i sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    __m128i src_minus10, src_minus11, src10, src11;
    __m128i src_zero0, src_zero1;
    __m128i offset;
    __m128i offset_mask0, offset_mask1;

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);

    /* load in advance */
    DUP4_ARG2(__lsx_vld, src - src_stride, 0, src, 0, src + src_stride, 0,
              src + src_stride_2x, 0, src_minus10, src_minus11, src10, src11);

    for (height -= 2; height; height -= 2) {
        src += src_stride_2x;
        DUP4_ARG2(__lsx_vilvl_b, src10, src_minus10, src_minus11, src_minus11,
                  src11, src_minus11, src10, src10, src_minus10, src_zero0,
                  src_minus11, src_zero1);
        DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
                 diff_minus11, const1, cmp_minus11, diff_minus10, diff_minus11);

        DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
                  diff_minus11, offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
                  offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
                  src_zero0, offset, dst0);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);

        dst0 = __lsx_vxori_b(dst0, 128);
        dst0 = __lsx_vsadd_b(dst0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);
        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x,
                  src10, src11);

        __lsx_vstelm_w(dst0, dst, 0, 0);
        __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
        dst += dst_stride_2x;
    }

    DUP4_ARG2(__lsx_vilvl_b, src10, src_minus10, src_minus11, src_minus11,
              src11,  src_minus11, src10, src10, src_minus10, src_zero0,
              src_minus11, src_zero1);
    DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
              diff_minus11, offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vaddi_bu, offset_mask0, 2, offset_mask1, 2,
              offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
              src_zero0, offset, dst0);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    dst0 = __lsx_vxori_b(dst0, 128);
    dst0 = __lsx_vsadd_b(dst0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_w(dst0, dst, 0, 0);
    __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
}

static void hevc_sao_edge_filter_90degree_8width_lsx(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     const uint8_t *src,
                                                     int32_t src_stride,
                                                     const int16_t *sao_offset_val,
                                                     int32_t height)
{
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i offset, sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i src_zero0, src_zero1, dst0;
    __m128i cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    __m128i src_minus10, src_minus11, src10, src11;
    __m128i offset_mask0, offset_mask1;

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src - src_stride, 0, src, 0, src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x, src10, src11);

    for (height -= 2; height; height -= 2) {
        src += src_stride_2x;
        DUP4_ARG2(__lsx_vilvl_b, src10, src_minus10, src_minus11, src_minus11,
                  src11, src_minus11, src10, src10, src_minus10, src_zero0,
                  src_minus11, src_zero1);
        DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
                diff_minus11, const1, cmp_minus11, diff_minus10, diff_minus11);

        DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
                  diff_minus11, offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
                  offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
                  src_zero0, offset, dst0);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);

        dst0 = __lsx_vxori_b(dst0, 128);
        dst0 = __lsx_vsadd_b(dst0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);
        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        DUP2_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x,
                  src10, src11);

        __lsx_vstelm_d(dst0, dst, 0, 0);
        __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
        dst += dst_stride_2x;
    }

    DUP4_ARG2(__lsx_vilvl_b, src10, src_minus10, src_minus11, src_minus11,
              src11, src_minus11, src10, src10, src_minus10, src_zero0,
              src_minus11, src_zero1);
    DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
              diff_minus11, offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
              offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
              src_zero0, offset, dst0);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    dst0 =  __lsx_vxori_b(dst0, 128);
    dst0 = __lsx_vsadd_b(dst0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_d(dst0, dst, 0, 0);
    __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
}

static void hevc_sao_edge_filter_90degree_16multiple_lsx(uint8_t *dst,
                                                         int32_t dst_stride,
                                                         const uint8_t *src,
                                                         int32_t src_stride,
                                                         const int16_t *
                                                         sao_offset_val,
                                                         int32_t width,
                                                         int32_t height)
{
    const uint8_t *src_orig = src;
    uint8_t *dst_orig = dst;
    int32_t h_cnt, v_cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i cmp_minus10, cmp_plus10, diff_minus10, diff_plus10, cmp_minus11;
    __m128i cmp_plus11, diff_minus11, diff_plus11, cmp_minus12, cmp_plus12;
    __m128i diff_minus12, diff_plus12, cmp_minus13, cmp_plus13, diff_minus13;
    __m128i diff_plus13;
    __m128i src10, src_minus10, dst0, src11, src_minus11, dst1;
    __m128i src12, dst2, src13, dst3;
    __m128i offset_mask0, offset_mask1, offset_mask2, offset_mask3, sao_offset;

    sao_offset = __lsx_vld(sao_offset_val, 0);
    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);

    for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
        src = src_orig + v_cnt;
        dst = dst_orig + v_cnt;

        DUP2_ARG2(__lsx_vld, src - src_stride, 0, src, 0,
                  src_minus10, src_minus11);

        for (h_cnt = (height >> 2); h_cnt--;) {
            DUP4_ARG2(__lsx_vldx, src, src_stride, src, src_stride_2x,
                      src, src_stride_3x, src, src_stride_4x,
                      src10, src11, src12, src13);
            DUP4_ARG2(__lsx_vseq_b, src_minus11, src_minus10, src_minus11,
                      src10, src10, src_minus11, src10, src11, cmp_minus10,
                      cmp_plus10, cmp_minus11, cmp_plus11);
            DUP4_ARG2(__lsx_vseq_b, src11, src10, src11, src12, src12, src11,
                      src12, src13, cmp_minus12, cmp_plus12,
                      cmp_minus13, cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13);
            DUP4_ARG2(__lsx_vsle_bu, src_minus11, src_minus10, src_minus11,
                      src10, src10, src_minus11, src10, src11, cmp_minus10,
                      cmp_plus10, cmp_minus11, cmp_plus11);
            DUP4_ARG2(__lsx_vsle_bu, src11, src10, src11, src12, src12, src11,
                      src12, src13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, cmp_minus10, cmp_plus10, cmp_minus11,
                      cmp_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
                      diff_plus10, const1, cmp_plus10, diff_minus11, const1,
                      cmp_minus11, diff_plus11, const1, cmp_plus11,
                      diff_minus10, diff_plus10, diff_minus11, diff_plus11);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus12, const1, cmp_minus12,
                      diff_plus12, const1, cmp_plus12, diff_minus13, const1,
                      cmp_minus13, diff_plus13, const1, cmp_plus13,
                      diff_minus12, diff_plus12, diff_minus13, diff_plus13);

            DUP4_ARG2(__lsx_vadd_b, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13, offset_mask0, offset_mask1, offset_mask2,
                      offset_mask3);
            DUP4_ARG2(__lsx_vaddi_bu, offset_mask0, 2, offset_mask1, 2,
                      offset_mask2, 2, offset_mask3, 2, offset_mask0,
                      offset_mask1, offset_mask2, offset_mask3);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask0,
                      sao_offset, sao_offset, offset_mask0,\
                      offset_mask0, offset_mask0);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask1,
                      sao_offset, sao_offset, offset_mask1, offset_mask1,
                      offset_mask1);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask2,
                      sao_offset, sao_offset, offset_mask2, offset_mask2,
                      offset_mask2);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask3,
                      sao_offset, sao_offset, offset_mask3, offset_mask3,
                      offset_mask3);

            src_minus10 = src12;
            DUP4_ARG2(__lsx_vxori_b, src_minus11, 128, src10, 128, src11, 128,
                      src12, 128, src_minus11, src10, src11, src12);
            DUP4_ARG2(__lsx_vsadd_b, src_minus11, offset_mask0, src10,
                      offset_mask1, src11, offset_mask2, src12,
                      offset_mask3, dst0, dst1, dst2, dst3);
            DUP4_ARG2(__lsx_vxori_b, dst0, 128, dst1, 128, dst2, 128, dst3,
                      128, dst0, dst1, dst2, dst3);
            src_minus11 = src13;

            __lsx_vst(dst0, dst, 0);
            __lsx_vstx(dst1, dst, dst_stride);
            __lsx_vstx(dst2, dst, dst_stride_2x);
            __lsx_vstx(dst3, dst, dst_stride_3x);
            src += src_stride_4x;
            dst += dst_stride_4x;
        }
    }
}

static void hevc_sao_edge_filter_45degree_4width_lsx(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     const uint8_t *src,
                                                     int32_t src_stride,
                                                     const int16_t *sao_offset_val,
                                                     int32_t height)
{
    const uint8_t *src_orig;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i offset, sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i cmp_minus10, diff_minus10, src_minus10, cmp_minus11, diff_minus11;
    __m128i src_minus11, src10, src11;
    __m128i src_plus0, src_zero0, src_plus1, src_zero1, dst0;
    __m128i offset_mask0, offset_mask1;
    __m128i zeros = {0};

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src_orig - src_stride, 0, src_orig, 0,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
              src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += src_stride_2x;

        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10,
                  shuf1, src_zero0, src_zero1);
        DUP2_ARG3(__lsx_vshuf_b, zeros, src10, shuf2, zeros, src11, shuf2,
                  src_plus0, src_plus1);

        DUP2_ARG2(__lsx_vilvl_b, src_plus0, src_minus10, src_plus1,
                  src_minus11, src_minus10, src_minus11);
        DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1,
                  src_zero1, src_zero0, src_zero1);
        DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
             diff_minus11, const1, cmp_minus11, diff_minus10, diff_minus11);

        DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
                  diff_minus11, offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
                  offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
                  src_zero0, offset, dst0);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);
        dst0 = __lsx_vxori_b(dst0, 128);
        dst0 = __lsx_vsadd_b(dst0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                  src10, src11);

        __lsx_vstelm_w(dst0, dst, 0, 0);
        __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
        dst += dst_stride_2x;
    }

    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10, shuf1,
              src_zero0, src_zero1);
    DUP2_ARG3(__lsx_vshuf_b, zeros, src10, shuf2, zeros, src11, shuf2,
              src_plus0, src_plus1);

    DUP2_ARG2(__lsx_vilvl_b, src_plus0, src_minus10, src_plus1, src_minus11,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
              src_zero0, src_zero1);
    DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
              diff_minus11, offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2, offset_mask0,
              offset_mask1);
    DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
              src_zero0, offset, dst0);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    dst0 = __lsx_vxori_b(dst0, 128);
    dst0 = __lsx_vsadd_b(dst0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_w(dst0, dst, 0, 0);
    __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
}

static void hevc_sao_edge_filter_45degree_8width_lsx(uint8_t *dst,
                                                     int32_t dst_stride,
                                                     const uint8_t *src,
                                                     int32_t src_stride,
                                                     const int16_t *sao_offset_val,
                                                     int32_t height)
{
    const uint8_t *src_orig;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i offset, sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    __m128i src_minus10, src10, src_minus11, src11;
    __m128i src_zero0, src_plus10, src_zero1, src_plus11, dst0;
    __m128i offset_mask0, offset_mask1;
    __m128i zeros = {0};

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src_orig - src_stride, 0, src_orig, 0, src_minus10,
              src_minus11);
    DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
              src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += src_stride_2x;

        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10,
                  shuf1, src_zero0, src_zero1);
        DUP2_ARG3(__lsx_vshuf_b, zeros, src10, shuf2, zeros, src11, shuf2,
                  src_plus10, src_plus11);

        DUP2_ARG2(__lsx_vilvl_b, src_plus10, src_minus10, src_plus11,
                  src_minus11, src_minus10, src_minus11);
        DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
                  src_zero0, src_zero1);
        DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
               diff_minus11, const1, cmp_minus11,  diff_minus10, diff_minus11);

        DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
                  diff_minus11, offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
                  offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
                  src_zero0, offset, dst0);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);
        dst0 = __lsx_vxori_b(dst0, 128);
        dst0 = __lsx_vsadd_b(dst0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                  src10, src11)
        __lsx_vstelm_d(dst0, dst, 0, 0);
        __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
        dst += dst_stride_2x;
    }

    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10, shuf1,
              src_zero0, src_zero1);
    DUP2_ARG3(__lsx_vshuf_b, zeros, src10, shuf2, zeros, src11, shuf2,
              src_plus10, src_plus11);
    DUP2_ARG2(__lsx_vilvl_b, src_plus10, src_minus10, src_plus11, src_minus11,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
              src_zero0, src_zero1);

    DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
              cmp_minus11, diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
              diff_minus11, offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2, offset_mask0,
              offset_mask1);
    DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
              src_zero0, offset, dst0);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    dst0 = __lsx_vxori_b(dst0, 128);
    dst0 = __lsx_vsadd_b(dst0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    src_minus10 = src10;
    src_minus11 = src11;

    /* load in advance */
    DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
              src10, src11);

    __lsx_vstelm_d(dst0, dst, 0, 0);
    __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
}

static void hevc_sao_edge_filter_45degree_16multiple_lsx(uint8_t *dst,
                                                         int32_t dst_stride,
                                                         const uint8_t *src,
                                                         int32_t src_stride,
                                                         const int16_t *
                                                         sao_offset_val,
                                                         int32_t width,
                                                         int32_t height)
{
    const uint8_t *src_orig = src;
    uint8_t *dst_orig = dst;
    int32_t v_cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i cmp_minus10, cmp_plus10, diff_minus10, diff_plus10, cmp_minus11;
    __m128i cmp_plus11, diff_minus11, diff_plus11, cmp_minus12, cmp_plus12;
    __m128i diff_minus12, diff_plus12, cmp_minus13, cmp_plus13, diff_minus13;
    __m128i diff_plus13, src_minus14, src_plus13;
    __m128i offset_mask0, offset_mask1, offset_mask2, offset_mask3;
    __m128i src10, src_minus10, dst0, src11, src_minus11, dst1;
    __m128i src12, src_minus12, dst2, src13, src_minus13, dst3;
    __m128i src_zero0, src_plus10, src_zero1, src_plus11, src_zero2;
    __m128i src_zero3, sao_offset, src_plus12;

    sao_offset = __lsx_vld(sao_offset_val, 0);
    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);

    for (; height; height -= 4) {
        src_orig = src - 1;
        dst_orig = dst;
        src_minus11 = __lsx_vld(src_orig, 0);
        DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                  src_minus12, src_minus13);
        src_minus14 = __lsx_vldx(src_orig, src_stride_3x);

        for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
            src_minus10 = __lsx_vld(src_orig - src_stride, 0);
            src_orig += 16;
            src10 = __lsx_vld(src_orig, 0);
            DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig,
                      src_stride_2x, src11, src12);
            src13 = __lsx_vldx(src_orig, src_stride_3x);
            src_plus13 = __lsx_vld(src + v_cnt + src_stride_4x, 1);

            DUP4_ARG3(__lsx_vshuf_b, src10, src_minus11, shuf1, src11,
                      src_minus12, shuf1, src12, src_minus13, shuf1,
                      src13, src_minus14, shuf1, src_zero0, src_zero1,
                      src_zero2, src_zero3);
            DUP2_ARG3(__lsx_vshuf_b, src11, src_minus12, shuf2, src12,
                      src_minus13, shuf2, src_plus10, src_plus11);
            src_plus12 = __lsx_vshuf_b(src13, src_minus14, shuf2);

            DUP4_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero0,
                      src_plus10, src_zero1, src_minus11, src_zero1,
                      src_plus11, cmp_minus10, cmp_plus10,
                      cmp_minus11, cmp_plus11);
            DUP4_ARG2(__lsx_vseq_b, src_zero2, src_minus12, src_zero2,
                      src_plus12, src_zero3, src_minus13, src_zero3,
                      src_plus13, cmp_minus12, cmp_plus12,
                      cmp_minus13, cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13);
            DUP4_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero0,
                      src_plus10, src_zero1, src_minus11, src_zero1,
                      src_plus11, cmp_minus10, cmp_plus10, cmp_minus11,
                      cmp_plus11);
            DUP4_ARG2(__lsx_vsle_bu, src_zero2, src_minus12, src_zero2,
                      src_plus12, src_zero3, src_minus13, src_zero3,
                      src_plus13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, cmp_minus10, cmp_plus10, cmp_minus11,
                      cmp_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
                      diff_plus10, const1, cmp_plus10, diff_minus11, const1,
                      cmp_minus11, diff_plus11, const1, cmp_plus11,
                      diff_minus10, diff_plus10, diff_minus11, diff_plus11);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus12, const1, cmp_minus12,
                      diff_plus12, const1, cmp_plus12, diff_minus13, const1,
                      cmp_minus13, diff_plus13, const1, cmp_plus13,
                      diff_minus12, diff_plus12, diff_minus13, diff_plus13);

            DUP4_ARG2(__lsx_vadd_b, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13, offset_mask0, offset_mask1, offset_mask2,
                      offset_mask3);
            DUP4_ARG2(__lsx_vaddi_bu, offset_mask0, 2, offset_mask1, 2,
                      offset_mask2, 2, offset_mask3, 2, offset_mask0,
                      offset_mask1, offset_mask2, offset_mask3);

            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask0,
                      sao_offset, sao_offset, offset_mask0, offset_mask0,
                      offset_mask0);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask1,
                      sao_offset, sao_offset, offset_mask1, offset_mask1,
                      offset_mask1);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask2,
                      sao_offset, sao_offset, offset_mask2, offset_mask2,
                      offset_mask2);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask3,
                      sao_offset, sao_offset, offset_mask3, offset_mask3,
                      offset_mask3);

            DUP4_ARG2(__lsx_vxori_b, src_zero0, 128, src_zero1, 128, src_zero2,
                      128, src_zero3, 128, src_zero0, src_zero1, src_zero2,
                      src_zero3);
            DUP4_ARG2(__lsx_vsadd_b, src_zero0, offset_mask0, src_zero1,
                      offset_mask1, src_zero2, offset_mask2, src_zero3,
                      offset_mask3, dst0, dst1, dst2, dst3);
            DUP4_ARG2(__lsx_vxori_b, dst0, 128, dst1, 128, dst2, 128, dst3,
                      128, dst0, dst1, dst2, dst3);

            src_minus11 = src10;
            src_minus12 = src11;
            src_minus13 = src12;
            src_minus14 = src13;

            __lsx_vst(dst0, dst_orig, 0);
            __lsx_vstx(dst1, dst_orig, dst_stride);
            __lsx_vstx(dst2, dst_orig, dst_stride_2x);
            __lsx_vstx(dst3, dst_orig, dst_stride_3x);
            dst_orig += 16;
        }
        src += src_stride_4x;
        dst += dst_stride_4x;
    }
}

static void hevc_sao_edge_filter_135degree_4width_lsx(uint8_t *dst,
                                                      int32_t dst_stride,
                                                      const uint8_t *src,
                                                      int32_t src_stride,
                                                      const int16_t *sao_offset_val,
                                                      int32_t height)
{
    const uint8_t *src_orig;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);

    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i offset, sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i src_zero0, src_zero1, dst0;
    __m128i cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    __m128i src_minus10, src10, src_minus11, src11;
    __m128i offset_mask0, offset_mask1;
    __m128i zeros = {0};

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src_orig - src_stride, 0, src_orig, 0,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
              src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += src_stride_2x;

        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10,
                  shuf1, src_zero0, src_zero1);
        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf2, zeros, src_minus11,
                  shuf2, src_minus10, src_minus11);

        DUP2_ARG2(__lsx_vilvl_b, src10, src_minus10, src11, src_minus11,
                  src_minus10, src_minus11);
        DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
                  src_zero0, src_zero1);
        DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
               diff_minus11, const1, cmp_minus11,  diff_minus10, diff_minus11);

        DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
                  diff_minus11, offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
                  offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
                  src_zero0, offset, dst0);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);
        dst0 = __lsx_vxori_b(dst0, 128);
        dst0 = __lsx_vsadd_b(dst0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                  src10, src11);

        __lsx_vstelm_w(dst0, dst, 0, 0);
        __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
        dst += dst_stride_2x;
    }

    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10, shuf1,
              src_zero0, src_zero1);
    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf2, zeros, src_minus11,
              shuf2, src_minus10, src_minus11);

    DUP2_ARG2(__lsx_vilvl_b, src10, src_minus10, src11, src_minus11,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
              src_zero0, src_zero1);
    DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
              cmp_minus11, diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
              diff_minus11, offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2, offset_mask0,
              offset_mask1);
    DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
              src_zero0, offset, dst0);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    dst0 = __lsx_vxori_b(dst0, 128);
    dst0 = __lsx_vsadd_b(dst0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_w(dst0, dst, 0, 0);
    __lsx_vstelm_w(dst0, dst + dst_stride, 0, 2);
    dst += dst_stride_2x;
}

static void hevc_sao_edge_filter_135degree_8width_lsx(uint8_t *dst,
                                                      int32_t dst_stride,
                                                      const uint8_t *src,
                                                      int32_t src_stride,
                                                      const int16_t *sao_offset_val,
                                                      int32_t height)
{
    const uint8_t *src_orig;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);

    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i offset, sao_offset = __lsx_vld(sao_offset_val, 0);
    __m128i cmp_minus10, diff_minus10, cmp_minus11, diff_minus11;
    __m128i src_minus10, src10, src_minus11, src11;
    __m128i src_zero0, src_zero1, dst0;
    __m128i offset_mask0, offset_mask1;
    __m128i zeros = {0};

    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);
    src_orig = src - 1;

    /* load in advance */
    DUP2_ARG2(__lsx_vld, src_orig - src_stride, 0, src_orig, 0,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
              src10, src11);

    for (height -= 2; height; height -= 2) {
        src_orig += src_stride_2x;

        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10,
                  shuf1, src_zero0, src_zero1);
        DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf2, zeros, src_minus11,
                  shuf2, src_minus10, src_minus11);

        DUP2_ARG2(__lsx_vilvl_b, src10, src_minus10, src11, src_minus11,
                  src_minus10, src_minus11);
        DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
                  src_zero0, src_zero1);
        DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
                  cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, diff_minus10, diff_minus11);
        DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1,
                  src_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11,
                  cmp_minus11, cmp_minus10, cmp_minus11);
        DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
              diff_minus11, const1, cmp_minus11,  diff_minus10, diff_minus11);

        DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
                  diff_minus11, offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2,
                  offset_mask0, offset_mask1);
        DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
                  src_zero0, offset, dst0);
        DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
                  sao_offset, offset, offset, offset);
        dst0 = __lsx_vxori_b(dst0, 128);
        dst0 = __lsx_vsadd_b(dst0, offset);
        dst0 = __lsx_vxori_b(dst0, 128);

        src_minus10 = src10;
        src_minus11 = src11;

        /* load in advance */
        DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                  src10, src11);

        __lsx_vstelm_d(dst0, dst, 0, 0);
        __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
        dst += dst_stride_2x;
    }

    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus11, shuf1, zeros, src10, shuf1,
              src_zero0, src_zero1);
    DUP2_ARG3(__lsx_vshuf_b, zeros, src_minus10, shuf2, zeros, src_minus11,
              shuf2, src_minus10, src_minus11);

    DUP2_ARG2(__lsx_vilvl_b, src10, src_minus10, src11, src_minus11,
              src_minus10, src_minus11);
    DUP2_ARG2(__lsx_vilvl_b, src_zero0, src_zero0, src_zero1, src_zero1,
              src_zero0, src_zero1);
    DUP2_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              diff_minus10, diff_minus11);
    DUP2_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero1, src_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_minus11, cmp_minus11,
              cmp_minus10, cmp_minus11);
    DUP2_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10, diff_minus11,
              const1, cmp_minus11, diff_minus10, diff_minus11);

    DUP2_ARG2(__lsx_vhaddw_hu_bu, diff_minus10, diff_minus10, diff_minus11,
              diff_minus11, offset_mask0, offset_mask1);
    DUP2_ARG2(__lsx_vaddi_hu, offset_mask0, 2, offset_mask1, 2, offset_mask0,
              offset_mask1);
    DUP2_ARG2(__lsx_vpickev_b, offset_mask1, offset_mask0, src_zero1,
              src_zero0, offset, dst0);
    DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset, sao_offset,
              sao_offset, offset, offset, offset);
    dst0 = __lsx_vxori_b(dst0, 128);
    dst0 = __lsx_vsadd_b(dst0, offset);
    dst0 = __lsx_vxori_b(dst0, 128);

    __lsx_vstelm_d(dst0, dst, 0, 0);
    __lsx_vstelm_d(dst0, dst + dst_stride, 0, 1);
}

static void hevc_sao_edge_filter_135degree_16multiple_lsx(uint8_t *dst,
                                                          int32_t dst_stride,
                                                          const uint8_t *src,
                                                          int32_t src_stride,
                                                          const int16_t *sao_offset_val,
                                                          int32_t width,
                                                          int32_t height)
{
    const uint8_t *src_orig;
    uint8_t *dst_orig;
    int32_t v_cnt;
    const int32_t src_stride_2x = (src_stride << 1);
    const int32_t dst_stride_2x = (dst_stride << 1);
    const int32_t src_stride_4x = (src_stride << 2);
    const int32_t dst_stride_4x = (dst_stride << 2);
    const int32_t src_stride_3x = src_stride_2x + src_stride;
    const int32_t dst_stride_3x = dst_stride_2x + dst_stride;

    __m128i shuf1 = {0x807060504030201, 0x100F0E0D0C0B0A09};
    __m128i shuf2 = {0x908070605040302, 0x11100F0E0D0C0B0A};
    __m128i edge_idx = {0x403000201, 0x0};
    __m128i const1 = __lsx_vldi(1);
    __m128i dst0, dst1, dst2, dst3;
    __m128i cmp_minus10, cmp_minus11, cmp_minus12, cmp_minus13, cmp_plus10;
    __m128i cmp_plus11, cmp_plus12, cmp_plus13, diff_minus10, diff_minus11;
    __m128i diff_minus12, diff_minus13, diff_plus10, diff_plus11, diff_plus12;
    __m128i diff_plus13, src10, src11, src12, src13, src_minus10, src_minus11;
    __m128i src_plus10, src_plus11, src_plus12, src_plus13;
    __m128i src_minus12, src_minus13, src_zero0, src_zero1, src_zero2, src_zero3;
    __m128i offset_mask0, offset_mask1, offset_mask2, offset_mask3, sao_offset;

    sao_offset = __lsx_vld(sao_offset_val, 0);
    sao_offset = __lsx_vpickev_b(sao_offset, sao_offset);

    for (; height; height -= 4) {
        src_orig = src - 1;
        dst_orig = dst;

        src_minus11 = __lsx_vld(src_orig, 0);
        DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                  src_plus10, src_plus11);
        src_plus12 = __lsx_vldx(src_orig, src_stride_3x);

        for (v_cnt = 0; v_cnt < width; v_cnt += 16) {
            src_minus10 = __lsx_vld(src_orig - src_stride, 2);
            src_plus13 = __lsx_vldx(src_orig, src_stride_4x);
            src_orig += 16;
            src10 = __lsx_vld(src_orig, 0);
            DUP2_ARG2(__lsx_vldx, src_orig, src_stride, src_orig, src_stride_2x,
                      src11, src12);
            src13 =__lsx_vldx(src_orig, src_stride_3x);

            DUP4_ARG3(__lsx_vshuf_b, src10, src_minus11, shuf1, src11,
                      src_plus10,  shuf1, src12, src_plus11, shuf1, src13,
                      src_plus12, shuf1, src_zero0, src_zero1, src_zero2,
                      src_zero3);
            src_minus11 = __lsx_vshuf_b(src10, src_minus11, shuf2);
            DUP2_ARG3(__lsx_vshuf_b, src11, src_plus10, shuf2, src12,
                      src_plus11, shuf2, src_minus12, src_minus13);

            DUP4_ARG2(__lsx_vseq_b, src_zero0, src_minus10, src_zero0,
                      src_plus10,  src_zero1, src_minus11, src_zero1,
                      src_plus11, cmp_minus10, cmp_plus10, cmp_minus11,
                      cmp_plus11);
            DUP4_ARG2(__lsx_vseq_b, src_zero2, src_minus12, src_zero2,
                      src_plus12, src_zero3, src_minus13, src_zero3,
                      src_plus13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13);
            DUP4_ARG2(__lsx_vsle_bu, src_zero0, src_minus10, src_zero0,
                      src_plus10, src_zero1, src_minus11, src_zero1, src_plus11,
                      cmp_minus10, cmp_plus10, cmp_minus11, cmp_plus11);
            DUP4_ARG2(__lsx_vsle_bu, src_zero2, src_minus12, src_zero2,
                      src_plus12, src_zero3, src_minus13, src_zero3, src_plus13,
                      cmp_minus12, cmp_plus12, cmp_minus13, cmp_plus13);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus10, cmp_minus10, cmp_plus10,
                      cmp_plus10, cmp_minus11, cmp_minus11, cmp_plus11,
                      cmp_plus11, cmp_minus10, cmp_plus10, cmp_minus11,
                      cmp_plus11);
            DUP4_ARG2(__lsx_vnor_v, cmp_minus12, cmp_minus12, cmp_plus12,
                      cmp_plus12, cmp_minus13, cmp_minus13, cmp_plus13,
                      cmp_plus13, cmp_minus12, cmp_plus12, cmp_minus13,
                      cmp_plus13);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus10, const1, cmp_minus10,
                      diff_plus10, const1, cmp_plus10, diff_minus11, const1,
                      cmp_minus11, diff_plus11, const1, cmp_plus11,
                      diff_minus10, diff_plus10, diff_minus11, diff_plus11);
            DUP4_ARG3(__lsx_vbitsel_v, diff_minus12, const1, cmp_minus12,
                      diff_plus12, const1, cmp_plus12, diff_minus13, const1,
                      cmp_minus13, diff_plus13, const1, cmp_plus13,
                      diff_minus12, diff_plus12, diff_minus13, diff_plus13);

            DUP4_ARG2(__lsx_vadd_b, diff_minus10, diff_plus10, diff_minus11,
                      diff_plus11, diff_minus12, diff_plus12, diff_minus13,
                      diff_plus13, offset_mask0, offset_mask1, offset_mask2,
                      offset_mask3);
            DUP4_ARG2(__lsx_vaddi_bu, offset_mask0, 2, offset_mask1, 2,
                      offset_mask2, 2, offset_mask3, 2, offset_mask0,
                      offset_mask1, offset_mask2, offset_mask3);

            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask0,
                      sao_offset, sao_offset, offset_mask0, offset_mask0,
                      offset_mask0);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask1,
                      sao_offset, sao_offset, offset_mask1, offset_mask1,
                      offset_mask1);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask2,
                      sao_offset, sao_offset, offset_mask2, offset_mask2,
                      offset_mask2);
            DUP2_ARG3(__lsx_vshuf_b, edge_idx, edge_idx, offset_mask3,
                      sao_offset, sao_offset, offset_mask3, offset_mask3,
                      offset_mask3);

            DUP4_ARG2(__lsx_vxori_b, src_zero0, 128, src_zero1, 128,
                      src_zero2, 128, src_zero3, 128, src_zero0, src_zero1,
                      src_zero2, src_zero3);
            DUP4_ARG2(__lsx_vsadd_b, src_zero0, offset_mask0, src_zero1,
                      offset_mask1, src_zero2, offset_mask2, src_zero3,
                      offset_mask3, dst0, dst1, dst2, dst3);
            DUP4_ARG2(__lsx_vxori_b, dst0, 128, dst1, 128, dst2, 128, dst3,
                      128, dst0, dst1, dst2, dst3);

            src_minus11 = src10;
            src_plus10 = src11;
            src_plus11 = src12;
            src_plus12 = src13;

            __lsx_vst(dst0, dst_orig, 0);
            __lsx_vstx(dst1, dst_orig, dst_stride);
            __lsx_vstx(dst2, dst_orig, dst_stride_2x);
            __lsx_vstx(dst3, dst_orig, dst_stride_3x);
            dst_orig += 16;
        }

        src += src_stride_4x;
        dst += dst_stride_4x;
    }
}

void ff_hevc_sao_edge_filter_8_lsx(uint8_t *dst, const uint8_t *src,
                                   ptrdiff_t stride_dst,
                                   const int16_t *sao_offset_val,
                                   int eo, int width, int height)
{
    ptrdiff_t stride_src = (2 * MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);

    switch (eo) {
    case 0:
        if (width >> 4) {
            hevc_sao_edge_filter_0degree_16multiple_lsx(dst, stride_dst,
                                                        src, stride_src,
                                                        sao_offset_val,
                                                        width - (width & 0x0F),
                                                        height);
            dst += width & 0xFFFFFFF0;
            src += width & 0xFFFFFFF0;
            width &= 0x0F;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_0degree_8width_lsx(dst, stride_dst,
                                                    src, stride_src,
                                                    sao_offset_val, height);
            dst += 8;
            src += 8;
            width &= 0x07;
        }

        if (width) {
            hevc_sao_edge_filter_0degree_4width_lsx(dst, stride_dst,
                                                    src, stride_src,
                                                    sao_offset_val, height);
        }
        break;

    case 1:
        if (width >> 4) {
            hevc_sao_edge_filter_90degree_16multiple_lsx(dst, stride_dst,
                                                         src, stride_src,
                                                         sao_offset_val,
                                                         width - (width & 0x0F),
                                                         height);
            dst += width & 0xFFFFFFF0;
            src += width & 0xFFFFFFF0;
            width &= 0x0F;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_90degree_8width_lsx(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
            dst += 8;
            src += 8;
            width &= 0x07;
        }

        if (width) {
            hevc_sao_edge_filter_90degree_4width_lsx(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
        }
        break;

    case 2:
        if (width >> 4) {
            hevc_sao_edge_filter_45degree_16multiple_lsx(dst, stride_dst,
                                                         src, stride_src,
                                                         sao_offset_val,
                                                         width - (width & 0x0F),
                                                         height);
            dst += width & 0xFFFFFFF0;
            src += width & 0xFFFFFFF0;
            width &= 0x0F;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_45degree_8width_lsx(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
            dst += 8;
            src += 8;
            width &= 0x07;
        }

        if (width) {
            hevc_sao_edge_filter_45degree_4width_lsx(dst, stride_dst,
                                                     src, stride_src,
                                                     sao_offset_val, height);
        }
        break;

    case 3:
        if (width >> 4) {
            hevc_sao_edge_filter_135degree_16multiple_lsx(dst, stride_dst,
                                                          src, stride_src,
                                                          sao_offset_val,
                                                          width - (width & 0x0F),
                                                          height);
            dst += width & 0xFFFFFFF0;
            src += width & 0xFFFFFFF0;
            width &= 0x0F;
        }

        if (width >> 3) {
            hevc_sao_edge_filter_135degree_8width_lsx(dst, stride_dst,
                                                      src, stride_src,
                                                      sao_offset_val, height);
            dst += 8;
            src += 8;
            width &= 0x07;
        }

        if (width) {
            hevc_sao_edge_filter_135degree_4width_lsx(dst, stride_dst,
                                                      src, stride_src,
                                                      sao_offset_val, height);
        }
        break;
    }
}
