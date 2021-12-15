/*
 * Loongson LASX optimized h264dsp
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
 *                Xiwei  Gu  <guxiwei-hf@loongson.cn>
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
#include "h264dsp_lasx.h"

#define AVC_LPF_P1_OR_Q1(p0_or_q0_org_in, q0_or_p0_org_in,   \
                         p1_or_q1_org_in, p2_or_q2_org_in,   \
                         neg_tc_in, tc_in, p1_or_q1_out)     \
{                                                            \
    __m256i clip3, temp;                                     \
                                                             \
    clip3 = __lasx_xvavgr_hu(p0_or_q0_org_in,                \
                             q0_or_p0_org_in);               \
    temp = __lasx_xvslli_h(p1_or_q1_org_in, 1);              \
    clip3 = __lasx_xvsub_h(clip3, temp);                     \
    clip3 = __lasx_xvavg_h(p2_or_q2_org_in, clip3);          \
    clip3 = __lasx_xvclip_h(clip3, neg_tc_in, tc_in);        \
    p1_or_q1_out = __lasx_xvadd_h(p1_or_q1_org_in, clip3);   \
}

#define AVC_LPF_P0Q0(q0_or_p0_org_in, p0_or_q0_org_in,       \
                     p1_or_q1_org_in, q1_or_p1_org_in,       \
                     neg_threshold_in, threshold_in,         \
                     p0_or_q0_out, q0_or_p0_out)             \
{                                                            \
    __m256i q0_sub_p0, p1_sub_q1, delta;                     \
                                                             \
    q0_sub_p0 = __lasx_xvsub_h(q0_or_p0_org_in,              \
                               p0_or_q0_org_in);             \
    p1_sub_q1 = __lasx_xvsub_h(p1_or_q1_org_in,              \
                               q1_or_p1_org_in);             \
    q0_sub_p0 = __lasx_xvslli_h(q0_sub_p0, 2);               \
    p1_sub_q1 = __lasx_xvaddi_hu(p1_sub_q1, 4);              \
    delta = __lasx_xvadd_h(q0_sub_p0, p1_sub_q1);            \
    delta = __lasx_xvsrai_h(delta, 3);                       \
    delta = __lasx_xvclip_h(delta, neg_threshold_in,         \
           threshold_in);                                    \
    p0_or_q0_out = __lasx_xvadd_h(p0_or_q0_org_in, delta);   \
    q0_or_p0_out = __lasx_xvsub_h(q0_or_p0_org_in, delta);   \
                                                             \
    p0_or_q0_out = __lasx_xvclip255_h(p0_or_q0_out);         \
    q0_or_p0_out = __lasx_xvclip255_h(q0_or_p0_out);         \
}

void ff_h264_h_lpf_luma_8_lasx(uint8_t *data, ptrdiff_t img_width,
                               int alpha_in, int beta_in, int8_t *tc)
{
    ptrdiff_t img_width_2x = img_width << 1;
    ptrdiff_t img_width_4x = img_width << 2;
    ptrdiff_t img_width_8x = img_width << 3;
    ptrdiff_t img_width_3x = img_width_2x + img_width;
    __m256i tmp_vec0, bs_vec;
    __m256i tc_vec = {0x0101010100000000, 0x0303030302020202,
                      0x0101010100000000, 0x0303030302020202};

    tmp_vec0 = __lasx_xvldrepl_w((uint32_t*)tc, 0);
    tc_vec   = __lasx_xvshuf_b(tmp_vec0, tmp_vec0, tc_vec);
    bs_vec   = __lasx_xvslti_b(tc_vec, 0);
    bs_vec   = __lasx_xvxori_b(bs_vec, 255);
    bs_vec   = __lasx_xvandi_b(bs_vec, 1);

    if (__lasx_xbnz_v(bs_vec)) {
        uint8_t *src = data - 4;
        __m256i p3_org, p2_org, p1_org, p0_org, q0_org, q1_org, q2_org, q3_org;
        __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
        __m256i is_less_than, is_less_than_beta, is_less_than_alpha;
        __m256i is_bs_greater_than0;
        __m256i zero = __lasx_xvldi(0);

        is_bs_greater_than0 = __lasx_xvslt_bu(zero, bs_vec);

        {
            uint8_t *src_tmp = src + img_width_8x;
            __m256i row0, row1, row2, row3, row4, row5, row6, row7;
            __m256i row8, row9, row10, row11, row12, row13, row14, row15;

            DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                      src, img_width_3x, row0, row1, row2, row3);
            src += img_width_4x;
            DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                      src, img_width_3x, row4, row5, row6, row7);
            src -= img_width_4x;
            DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, img_width, src_tmp,
                      img_width_2x, src_tmp, img_width_3x,
                      row8, row9, row10, row11);
            src_tmp += img_width_4x;
            DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, img_width, src_tmp,
                      img_width_2x, src_tmp, img_width_3x,
                      row12, row13, row14, row15);
            src_tmp -= img_width_4x;

            LASX_TRANSPOSE16x8_B(row0, row1, row2, row3, row4, row5, row6,
                                 row7, row8, row9, row10, row11,
                                 row12, row13, row14, row15,
                                 p3_org, p2_org, p1_org, p0_org,
                                 q0_org, q1_org, q2_org, q3_org);
        }

        p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
        p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
        q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

        alpha = __lasx_xvreplgr2vr_b(alpha_in);
        beta  = __lasx_xvreplgr2vr_b(beta_in);

        is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
        is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
        is_less_than       = is_less_than_alpha & is_less_than_beta;
        is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
        is_less_than       = is_less_than_beta & is_less_than;
        is_less_than       = is_less_than & is_bs_greater_than0;

        if (__lasx_xbnz_v(is_less_than)) {
            __m256i neg_tc_h, tc_h, p1_org_h, p0_org_h, q0_org_h, q1_org_h;
            __m256i p2_asub_p0, q2_asub_q0;

            neg_tc_h = __lasx_xvneg_b(tc_vec);
            neg_tc_h = __lasx_vext2xv_h_b(neg_tc_h);
            tc_h     = __lasx_vext2xv_hu_bu(tc_vec);
            p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
            p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
            q0_org_h = __lasx_vext2xv_hu_bu(q0_org);

            p2_asub_p0 = __lasx_xvabsd_bu(p2_org, p0_org);
            is_less_than_beta = __lasx_xvslt_bu(p2_asub_p0, beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            if (__lasx_xbnz_v(is_less_than_beta)) {
                __m256i p2_org_h, p1_h;

                p2_org_h = __lasx_vext2xv_hu_bu(p2_org);
                AVC_LPF_P1_OR_Q1(p0_org_h, q0_org_h, p1_org_h, p2_org_h,
                                 neg_tc_h, tc_h, p1_h);
                p1_h = __lasx_xvpickev_b(p1_h, p1_h);
                p1_h = __lasx_xvpermi_d(p1_h, 0xd8);
                p1_org = __lasx_xvbitsel_v(p1_org, p1_h, is_less_than_beta);
                is_less_than_beta = __lasx_xvandi_b(is_less_than_beta, 1);
                tc_vec = __lasx_xvadd_b(tc_vec, is_less_than_beta);
            }

            q2_asub_q0 = __lasx_xvabsd_bu(q2_org, q0_org);
            is_less_than_beta = __lasx_xvslt_bu(q2_asub_q0, beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

            if (__lasx_xbnz_v(is_less_than_beta)) {
                __m256i q2_org_h, q1_h;

                q2_org_h = __lasx_vext2xv_hu_bu(q2_org);
                AVC_LPF_P1_OR_Q1(p0_org_h, q0_org_h, q1_org_h, q2_org_h,
                                 neg_tc_h, tc_h, q1_h);
                q1_h = __lasx_xvpickev_b(q1_h, q1_h);
                q1_h = __lasx_xvpermi_d(q1_h, 0xd8);
                q1_org = __lasx_xvbitsel_v(q1_org, q1_h, is_less_than_beta);

                is_less_than_beta = __lasx_xvandi_b(is_less_than_beta, 1);
                tc_vec = __lasx_xvadd_b(tc_vec, is_less_than_beta);
            }

            {
                __m256i neg_thresh_h, p0_h, q0_h;

                neg_thresh_h = __lasx_xvneg_b(tc_vec);
                neg_thresh_h = __lasx_vext2xv_h_b(neg_thresh_h);
                tc_h         = __lasx_vext2xv_hu_bu(tc_vec);

                AVC_LPF_P0Q0(q0_org_h, p0_org_h, p1_org_h, q1_org_h,
                             neg_thresh_h, tc_h, p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpickev_b, p0_h, p0_h, q0_h, q0_h,
                          p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpermi_d, p0_h, 0xd8, q0_h, 0xd8,
                          p0_h, q0_h);
                p0_org = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than);
                q0_org = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than);
            }

            {
                __m256i row0, row1, row2, row3, row4, row5, row6, row7;
                __m256i control = {0x0000000400000000, 0x0000000500000001,
                                   0x0000000600000002, 0x0000000700000003};

                DUP4_ARG3(__lasx_xvpermi_q, p0_org, q3_org, 0x02, p1_org,
                          q2_org, 0x02, p2_org, q1_org, 0x02, p3_org,
                          q0_org, 0x02, p0_org, p1_org, p2_org, p3_org);
                DUP2_ARG2(__lasx_xvilvl_b, p1_org, p3_org, p0_org, p2_org,
                          row0, row2);
                DUP2_ARG2(__lasx_xvilvh_b, p1_org, p3_org, p0_org, p2_org,
                          row1, row3);
                DUP2_ARG2(__lasx_xvilvl_b, row2, row0, row3, row1, row4, row6);
                DUP2_ARG2(__lasx_xvilvh_b, row2, row0, row3, row1, row5, row7);
                DUP4_ARG2(__lasx_xvperm_w, row4, control, row5, control, row6,
                          control, row7, control, row4, row5, row6, row7);
                __lasx_xvstelm_d(row4, src, 0, 0);
                __lasx_xvstelm_d(row4, src + img_width, 0, 1);
                src += img_width_2x;
                __lasx_xvstelm_d(row4, src, 0, 2);
                __lasx_xvstelm_d(row4, src + img_width, 0, 3);
                src += img_width_2x;
                __lasx_xvstelm_d(row5, src, 0, 0);
                __lasx_xvstelm_d(row5, src + img_width, 0, 1);
                src += img_width_2x;
                __lasx_xvstelm_d(row5, src, 0, 2);
                __lasx_xvstelm_d(row5, src + img_width, 0, 3);
                src += img_width_2x;
                __lasx_xvstelm_d(row6, src, 0, 0);
                __lasx_xvstelm_d(row6, src + img_width, 0, 1);
                src += img_width_2x;
                __lasx_xvstelm_d(row6, src, 0, 2);
                __lasx_xvstelm_d(row6, src + img_width, 0, 3);
                src += img_width_2x;
                __lasx_xvstelm_d(row7, src, 0, 0);
                __lasx_xvstelm_d(row7, src + img_width, 0, 1);
                src += img_width_2x;
                __lasx_xvstelm_d(row7, src, 0, 2);
                __lasx_xvstelm_d(row7, src + img_width, 0, 3);
            }
        }
    }
}

void ff_h264_v_lpf_luma_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                   int alpha_in, int beta_in, int8_t *tc)
{
    ptrdiff_t img_width_2x = img_width << 1;
    ptrdiff_t img_width_3x = img_width + img_width_2x;
    __m256i tmp_vec0, bs_vec;
    __m256i tc_vec = {0x0101010100000000, 0x0303030302020202,
                      0x0101010100000000, 0x0303030302020202};

    tmp_vec0 = __lasx_xvldrepl_w((uint32_t*)tc, 0);
    tc_vec   = __lasx_xvshuf_b(tmp_vec0, tmp_vec0, tc_vec);
    bs_vec   = __lasx_xvslti_b(tc_vec, 0);
    bs_vec   = __lasx_xvxori_b(bs_vec, 255);
    bs_vec   = __lasx_xvandi_b(bs_vec, 1);

    if (__lasx_xbnz_v(bs_vec)) {
        __m256i p2_org, p1_org, p0_org, q0_org, q1_org, q2_org;
        __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
        __m256i is_less_than, is_less_than_beta, is_less_than_alpha;
        __m256i p1_org_h, p0_org_h, q0_org_h, q1_org_h;
        __m256i is_bs_greater_than0;
        __m256i zero = __lasx_xvldi(0);

        alpha = __lasx_xvreplgr2vr_b(alpha_in);
        beta  = __lasx_xvreplgr2vr_b(beta_in);

        DUP2_ARG2(__lasx_xvldx, data, -img_width_3x, data, -img_width_2x,
                  p2_org, p1_org);
        p0_org = __lasx_xvldx(data, -img_width);
        DUP2_ARG2(__lasx_xvldx, data, 0, data, img_width, q0_org, q1_org);

        is_bs_greater_than0 = __lasx_xvslt_bu(zero, bs_vec);
        p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
        p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
        q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

        is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
        is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
        is_less_than       = is_less_than_alpha & is_less_than_beta;
        is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
        is_less_than       = is_less_than_beta & is_less_than;
        is_less_than       = is_less_than & is_bs_greater_than0;

        if (__lasx_xbnz_v(is_less_than)) {
            __m256i neg_tc_h, tc_h, p2_asub_p0, q2_asub_q0;

            q2_org = __lasx_xvldx(data, img_width_2x);

            neg_tc_h = __lasx_xvneg_b(tc_vec);
            neg_tc_h = __lasx_vext2xv_h_b(neg_tc_h);
            tc_h     = __lasx_vext2xv_hu_bu(tc_vec);
            p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
            p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
            q0_org_h = __lasx_vext2xv_hu_bu(q0_org);

            p2_asub_p0        = __lasx_xvabsd_bu(p2_org, p0_org);
            is_less_than_beta = __lasx_xvslt_bu(p2_asub_p0, beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            if (__lasx_xbnz_v(is_less_than_beta)) {
                __m256i p1_h, p2_org_h;

                p2_org_h = __lasx_vext2xv_hu_bu(p2_org);
                AVC_LPF_P1_OR_Q1(p0_org_h, q0_org_h, p1_org_h, p2_org_h,
                                 neg_tc_h, tc_h, p1_h);
                p1_h = __lasx_xvpickev_b(p1_h, p1_h);
                p1_h = __lasx_xvpermi_d(p1_h, 0xd8);
                p1_h   = __lasx_xvbitsel_v(p1_org, p1_h, is_less_than_beta);
                p1_org = __lasx_xvpermi_q(p1_org, p1_h, 0x30);
                __lasx_xvst(p1_org, data - img_width_2x, 0);

                is_less_than_beta = __lasx_xvandi_b(is_less_than_beta, 1);
                tc_vec = __lasx_xvadd_b(tc_vec, is_less_than_beta);
            }

            q2_asub_q0 = __lasx_xvabsd_bu(q2_org, q0_org);
            is_less_than_beta = __lasx_xvslt_bu(q2_asub_q0, beta);
            is_less_than_beta = is_less_than_beta & is_less_than;

            q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

            if (__lasx_xbnz_v(is_less_than_beta)) {
                __m256i q1_h, q2_org_h;

                q2_org_h = __lasx_vext2xv_hu_bu(q2_org);
                AVC_LPF_P1_OR_Q1(p0_org_h, q0_org_h, q1_org_h, q2_org_h,
                                 neg_tc_h, tc_h, q1_h);
                q1_h = __lasx_xvpickev_b(q1_h, q1_h);
                q1_h = __lasx_xvpermi_d(q1_h, 0xd8);
                q1_h = __lasx_xvbitsel_v(q1_org, q1_h, is_less_than_beta);
                q1_org = __lasx_xvpermi_q(q1_org, q1_h, 0x30);
                __lasx_xvst(q1_org, data + img_width, 0);

                is_less_than_beta = __lasx_xvandi_b(is_less_than_beta, 1);
                tc_vec = __lasx_xvadd_b(tc_vec, is_less_than_beta);

            }

            {
                __m256i neg_thresh_h, p0_h, q0_h;

                neg_thresh_h = __lasx_xvneg_b(tc_vec);
                neg_thresh_h = __lasx_vext2xv_h_b(neg_thresh_h);
                tc_h         = __lasx_vext2xv_hu_bu(tc_vec);

                AVC_LPF_P0Q0(q0_org_h, p0_org_h, p1_org_h, q1_org_h,
                             neg_thresh_h, tc_h, p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpickev_b, p0_h, p0_h, q0_h, q0_h,
                          p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpermi_d, p0_h, 0Xd8, q0_h, 0xd8,
                          p0_h, q0_h);
                p0_h = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than);
                q0_h = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than);
                p0_org = __lasx_xvpermi_q(p0_org, p0_h, 0x30);
                q0_org = __lasx_xvpermi_q(q0_org, q0_h, 0x30);
                __lasx_xvst(p0_org, data - img_width, 0);
                __lasx_xvst(q0_org, data, 0);
            }
        }
    }
}

void ff_h264_h_lpf_chroma_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                 int alpha_in, int beta_in, int8_t *tc)
{
    __m256i tmp_vec0, bs_vec;
    __m256i tc_vec = {0x0303020201010000, 0x0303020201010000, 0x0, 0x0};
    __m256i zero = __lasx_xvldi(0);
    ptrdiff_t img_width_2x = img_width << 1;
    ptrdiff_t img_width_4x = img_width << 2;
    ptrdiff_t img_width_3x = img_width_2x + img_width;

    tmp_vec0 = __lasx_xvldrepl_w((uint32_t*)tc, 0);
    tc_vec   = __lasx_xvshuf_b(tmp_vec0, tmp_vec0, tc_vec);
    bs_vec   = __lasx_xvslti_b(tc_vec, 0);
    bs_vec   = __lasx_xvxori_b(bs_vec, 255);
    bs_vec   = __lasx_xvandi_b(bs_vec, 1);
    bs_vec   = __lasx_xvpermi_q(zero, bs_vec, 0x30);

    if (__lasx_xbnz_v(bs_vec)) {
        uint8_t *src = data - 2;
        __m256i p1_org, p0_org, q0_org, q1_org;
        __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
        __m256i is_less_than, is_less_than_beta, is_less_than_alpha;
        __m256i is_bs_greater_than0;

        is_bs_greater_than0 = __lasx_xvslt_bu(zero, bs_vec);

        {
            __m256i row0, row1, row2, row3, row4, row5, row6, row7;

            DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                      src, img_width_3x, row0, row1, row2, row3);
            src += img_width_4x;
            DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                      src, img_width_3x, row4, row5, row6, row7);
            src -= img_width_4x;
            /* LASX_TRANSPOSE8x4_B */
            DUP4_ARG2(__lasx_xvilvl_b, row2, row0, row3, row1, row6, row4,
                      row7, row5, p1_org, p0_org, q0_org, q1_org);
            row0 = __lasx_xvilvl_b(p0_org, p1_org);
            row1 = __lasx_xvilvl_b(q1_org, q0_org);
            row3 = __lasx_xvilvh_w(row1, row0);
            row2 = __lasx_xvilvl_w(row1, row0);
            p1_org = __lasx_xvpermi_d(row2, 0x00);
            p0_org = __lasx_xvpermi_d(row2, 0x55);
            q0_org = __lasx_xvpermi_d(row3, 0x00);
            q1_org = __lasx_xvpermi_d(row3, 0x55);
        }

        p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
        p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
        q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

        alpha = __lasx_xvreplgr2vr_b(alpha_in);
        beta  = __lasx_xvreplgr2vr_b(beta_in);

        is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
        is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
        is_less_than       = is_less_than_alpha & is_less_than_beta;
        is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
        is_less_than       = is_less_than_beta & is_less_than;
        is_less_than       = is_less_than & is_bs_greater_than0;

        if (__lasx_xbnz_v(is_less_than)) {
            __m256i p1_org_h, p0_org_h, q0_org_h, q1_org_h;

            p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
            p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
            q0_org_h = __lasx_vext2xv_hu_bu(q0_org);
            q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

            {
                __m256i tc_h, neg_thresh_h, p0_h, q0_h;

                neg_thresh_h = __lasx_xvneg_b(tc_vec);
                neg_thresh_h = __lasx_vext2xv_h_b(neg_thresh_h);
                tc_h         = __lasx_vext2xv_hu_bu(tc_vec);

                AVC_LPF_P0Q0(q0_org_h, p0_org_h, p1_org_h, q1_org_h,
                             neg_thresh_h, tc_h, p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpickev_b, p0_h, p0_h, q0_h, q0_h,
                          p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpermi_d, p0_h, 0xd8, q0_h, 0xd8,
                          p0_h, q0_h);
                p0_org = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than);
                q0_org = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than);
            }

            p0_org = __lasx_xvilvl_b(q0_org, p0_org);
            src = data - 1;
            __lasx_xvstelm_h(p0_org, src, 0, 0);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 1);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 2);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 3);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 4);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 5);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 6);
            src += img_width;
            __lasx_xvstelm_h(p0_org, src, 0, 7);
        }
    }
}

void ff_h264_v_lpf_chroma_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                 int alpha_in, int beta_in, int8_t *tc)
{
    int img_width_2x = img_width << 1;
    __m256i tmp_vec0, bs_vec;
    __m256i tc_vec = {0x0303020201010000, 0x0303020201010000, 0x0, 0x0};
    __m256i zero = __lasx_xvldi(0);

    tmp_vec0 = __lasx_xvldrepl_w((uint32_t*)tc, 0);
    tc_vec   = __lasx_xvshuf_b(tmp_vec0, tmp_vec0, tc_vec);
    bs_vec   = __lasx_xvslti_b(tc_vec, 0);
    bs_vec   = __lasx_xvxori_b(bs_vec, 255);
    bs_vec   = __lasx_xvandi_b(bs_vec, 1);
    bs_vec   = __lasx_xvpermi_q(zero, bs_vec, 0x30);

    if (__lasx_xbnz_v(bs_vec)) {
        __m256i p1_org, p0_org, q0_org, q1_org;
        __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
        __m256i is_less_than, is_less_than_beta, is_less_than_alpha;
        __m256i is_bs_greater_than0;

        alpha = __lasx_xvreplgr2vr_b(alpha_in);
        beta  = __lasx_xvreplgr2vr_b(beta_in);

        DUP2_ARG2(__lasx_xvldx, data, -img_width_2x, data, -img_width,
                  p1_org, p0_org);
        DUP2_ARG2(__lasx_xvldx, data, 0, data, img_width, q0_org, q1_org);

        is_bs_greater_than0 = __lasx_xvslt_bu(zero, bs_vec);
        p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
        p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
        q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

        is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
        is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
        is_less_than       = is_less_than_alpha & is_less_than_beta;
        is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
        is_less_than       = is_less_than_beta & is_less_than;
        is_less_than       = is_less_than & is_bs_greater_than0;

        if (__lasx_xbnz_v(is_less_than)) {
            __m256i p1_org_h, p0_org_h, q0_org_h, q1_org_h;

            p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
            p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
            q0_org_h = __lasx_vext2xv_hu_bu(q0_org);
            q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

            {
                __m256i neg_thresh_h, tc_h, p0_h, q0_h;

                neg_thresh_h = __lasx_xvneg_b(tc_vec);
                neg_thresh_h = __lasx_vext2xv_h_b(neg_thresh_h);
                tc_h         = __lasx_vext2xv_hu_bu(tc_vec);

                AVC_LPF_P0Q0(q0_org_h, p0_org_h, p1_org_h, q1_org_h,
                             neg_thresh_h, tc_h, p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpickev_b, p0_h, p0_h, q0_h, q0_h,
                          p0_h, q0_h);
                DUP2_ARG2(__lasx_xvpermi_d, p0_h, 0xd8, q0_h, 0xd8,
                          p0_h, q0_h);
                p0_h = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than);
                q0_h = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than);
                __lasx_xvstelm_d(p0_h, data - img_width, 0, 0);
                __lasx_xvstelm_d(q0_h, data, 0, 0);
            }
        }
    }
}

#define AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_or_q3_org_in, p0_or_q0_org_in,          \
                                 q3_or_p3_org_in, p1_or_q1_org_in,          \
                                 p2_or_q2_org_in, q1_or_p1_org_in,          \
                                 p0_or_q0_out, p1_or_q1_out, p2_or_q2_out)  \
{                                                                           \
    __m256i threshold;                                                      \
    __m256i const2, const3 = __lasx_xvldi(0);                               \
                                                                            \
    const2 = __lasx_xvaddi_hu(const3, 2);                                   \
    const3 = __lasx_xvaddi_hu(const3, 3);                                   \
    threshold = __lasx_xvadd_h(p0_or_q0_org_in, q3_or_p3_org_in);           \
    threshold = __lasx_xvadd_h(p1_or_q1_org_in, threshold);                 \
                                                                            \
    p0_or_q0_out = __lasx_xvslli_h(threshold, 1);                           \
    p0_or_q0_out = __lasx_xvadd_h(p0_or_q0_out, p2_or_q2_org_in);           \
    p0_or_q0_out = __lasx_xvadd_h(p0_or_q0_out, q1_or_p1_org_in);           \
    p0_or_q0_out = __lasx_xvsrar_h(p0_or_q0_out, const3);                   \
                                                                            \
    p1_or_q1_out = __lasx_xvadd_h(p2_or_q2_org_in, threshold);              \
    p1_or_q1_out = __lasx_xvsrar_h(p1_or_q1_out, const2);                   \
                                                                            \
    p2_or_q2_out = __lasx_xvmul_h(p2_or_q2_org_in, const3);                 \
    p2_or_q2_out = __lasx_xvadd_h(p2_or_q2_out, p3_or_q3_org_in);           \
    p2_or_q2_out = __lasx_xvadd_h(p2_or_q2_out, p3_or_q3_org_in);           \
    p2_or_q2_out = __lasx_xvadd_h(p2_or_q2_out, threshold);                 \
    p2_or_q2_out = __lasx_xvsrar_h(p2_or_q2_out, const3);                   \
}

/* data[-u32_img_width] = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2); */
#define AVC_LPF_P0_OR_Q0(p0_or_q0_org_in, q1_or_p1_org_in,             \
                         p1_or_q1_org_in, p0_or_q0_out)                \
{                                                                      \
    __m256i const2 = __lasx_xvldi(0);                                  \
    const2 = __lasx_xvaddi_hu(const2, 2);                              \
    p0_or_q0_out = __lasx_xvadd_h(p0_or_q0_org_in, q1_or_p1_org_in);   \
    p0_or_q0_out = __lasx_xvadd_h(p0_or_q0_out, p1_or_q1_org_in);      \
    p0_or_q0_out = __lasx_xvadd_h(p0_or_q0_out, p1_or_q1_org_in);      \
    p0_or_q0_out = __lasx_xvsrar_h(p0_or_q0_out, const2);              \
}

void ff_h264_h_lpf_luma_intra_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                     int alpha_in, int beta_in)
{
    ptrdiff_t img_width_2x = img_width << 1;
    ptrdiff_t img_width_4x = img_width << 2;
    ptrdiff_t img_width_3x = img_width_2x + img_width;
    uint8_t *src = data - 4;
    __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
    __m256i is_less_than, is_less_than_beta, is_less_than_alpha;
    __m256i p3_org, p2_org, p1_org, p0_org, q0_org, q1_org, q2_org, q3_org;
    __m256i zero = __lasx_xvldi(0);

    {
        __m256i row0, row1, row2, row3, row4, row5, row6, row7;
        __m256i row8, row9, row10, row11, row12, row13, row14, row15;

        DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                  src, img_width_3x, row0, row1, row2, row3);
        src += img_width_4x;
        DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                  src, img_width_3x, row4, row5, row6, row7);
        src += img_width_4x;
        DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                  src, img_width_3x, row8, row9, row10, row11);
        src += img_width_4x;
        DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
                  src, img_width_3x, row12, row13, row14, row15);
        src += img_width_4x;

        LASX_TRANSPOSE16x8_B(row0, row1, row2, row3,
                             row4, row5, row6, row7,
                             row8, row9, row10, row11,
                             row12, row13, row14, row15,
                             p3_org, p2_org, p1_org, p0_org,
                             q0_org, q1_org, q2_org, q3_org);
    }

    alpha = __lasx_xvreplgr2vr_b(alpha_in);
    beta  = __lasx_xvreplgr2vr_b(beta_in);
    p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
    p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
    q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

    is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
    is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
    is_less_than       = is_less_than_beta & is_less_than_alpha;
    is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
    is_less_than       = is_less_than_beta & is_less_than;
    is_less_than       = __lasx_xvpermi_q(zero, is_less_than, 0x30);

    if (__lasx_xbnz_v(is_less_than)) {
        __m256i p2_asub_p0, q2_asub_q0, p0_h, q0_h, negate_is_less_than_beta;
        __m256i p1_org_h, p0_org_h, q0_org_h, q1_org_h;
        __m256i less_alpha_shift2_add2 = __lasx_xvsrli_b(alpha, 2);

        less_alpha_shift2_add2 = __lasx_xvaddi_bu(less_alpha_shift2_add2, 2);
        less_alpha_shift2_add2 = __lasx_xvslt_bu(p0_asub_q0,
                                                 less_alpha_shift2_add2);

        p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
        p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
        q0_org_h = __lasx_vext2xv_hu_bu(q0_org);
        q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

        p2_asub_p0               = __lasx_xvabsd_bu(p2_org, p0_org);
        is_less_than_beta        = __lasx_xvslt_bu(p2_asub_p0, beta);
        is_less_than_beta        = is_less_than_beta & less_alpha_shift2_add2;
        negate_is_less_than_beta = __lasx_xvxori_b(is_less_than_beta, 0xff);
        is_less_than_beta        = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        /* combine and store */
        if (__lasx_xbnz_v(is_less_than_beta)) {
            __m256i p2_org_h, p3_org_h, p1_h, p2_h;

            p2_org_h   = __lasx_vext2xv_hu_bu(p2_org);
            p3_org_h   = __lasx_vext2xv_hu_bu(p3_org);

            AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_org_h, p0_org_h, q0_org_h, p1_org_h,
                                     p2_org_h, q1_org_h, p0_h, p1_h, p2_h);

            p0_h = __lasx_xvpickev_b(p0_h, p0_h);
            p0_h = __lasx_xvpermi_d(p0_h, 0xd8);
            DUP2_ARG2(__lasx_xvpickev_b, p1_h, p1_h, p2_h, p2_h, p1_h, p2_h);
            DUP2_ARG2(__lasx_xvpermi_d, p1_h, 0xd8, p2_h, 0xd8, p1_h, p2_h);
            p0_org = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than_beta);
            p1_org = __lasx_xvbitsel_v(p1_org, p1_h, is_less_than_beta);
            p2_org = __lasx_xvbitsel_v(p2_org, p2_h, is_less_than_beta);
        }

        AVC_LPF_P0_OR_Q0(p0_org_h, q1_org_h, p1_org_h, p0_h);
        /* combine */
        p0_h = __lasx_xvpickev_b(p0_h, p0_h);
        p0_h = __lasx_xvpermi_d(p0_h, 0xd8);
        p0_org = __lasx_xvbitsel_v(p0_org, p0_h, negate_is_less_than_beta);

        /* if (tmpFlag && (unsigned)ABS(q2-q0) < thresholds->beta_in) */
        q2_asub_q0 = __lasx_xvabsd_bu(q2_org, q0_org);
        is_less_than_beta = __lasx_xvslt_bu(q2_asub_q0, beta);
        is_less_than_beta = is_less_than_beta & less_alpha_shift2_add2;
        negate_is_less_than_beta = __lasx_xvxori_b(is_less_than_beta, 0xff);
        is_less_than_beta = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        /* combine and store */
        if (__lasx_xbnz_v(is_less_than_beta)) {
            __m256i q2_org_h, q3_org_h, q1_h, q2_h;

            q2_org_h   = __lasx_vext2xv_hu_bu(q2_org);
            q3_org_h   = __lasx_vext2xv_hu_bu(q3_org);

            AVC_LPF_P0P1P2_OR_Q0Q1Q2(q3_org_h, q0_org_h, p0_org_h, q1_org_h,
                                     q2_org_h, p1_org_h, q0_h, q1_h, q2_h);

            q0_h = __lasx_xvpickev_b(q0_h, q0_h);
            q0_h = __lasx_xvpermi_d(q0_h, 0xd8);
            DUP2_ARG2(__lasx_xvpickev_b, q1_h, q1_h, q2_h, q2_h, q1_h, q2_h);
            DUP2_ARG2(__lasx_xvpermi_d, q1_h, 0xd8, q2_h, 0xd8, q1_h, q2_h);
            q0_org = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than_beta);
            q1_org = __lasx_xvbitsel_v(q1_org, q1_h, is_less_than_beta);
            q2_org = __lasx_xvbitsel_v(q2_org, q2_h, is_less_than_beta);

        }

        AVC_LPF_P0_OR_Q0(q0_org_h, p1_org_h, q1_org_h, q0_h);

        /* combine */
        q0_h = __lasx_xvpickev_b(q0_h, q0_h);
        q0_h = __lasx_xvpermi_d(q0_h, 0xd8);
        q0_org = __lasx_xvbitsel_v(q0_org, q0_h, negate_is_less_than_beta);

        /* transpose and store */
        {
            __m256i row0, row1, row2, row3, row4, row5, row6, row7;
            __m256i control = {0x0000000400000000, 0x0000000500000001,
                               0x0000000600000002, 0x0000000700000003};

            DUP4_ARG3(__lasx_xvpermi_q, p0_org, q3_org, 0x02, p1_org, q2_org,
                      0x02, p2_org, q1_org, 0x02, p3_org, q0_org, 0x02,
                      p0_org, p1_org, p2_org, p3_org);
            DUP2_ARG2(__lasx_xvilvl_b, p1_org, p3_org, p0_org, p2_org,
                      row0, row2);
            DUP2_ARG2(__lasx_xvilvh_b, p1_org, p3_org, p0_org, p2_org,
                      row1, row3);
            DUP2_ARG2(__lasx_xvilvl_b, row2, row0, row3, row1, row4, row6);
            DUP2_ARG2(__lasx_xvilvh_b, row2, row0, row3, row1, row5, row7);
            DUP4_ARG2(__lasx_xvperm_w, row4, control, row5, control, row6,
                      control, row7, control, row4, row5, row6, row7);
            src = data - 4;
            __lasx_xvstelm_d(row4, src, 0, 0);
            __lasx_xvstelm_d(row4, src + img_width, 0, 1);
            src += img_width_2x;
            __lasx_xvstelm_d(row4, src, 0, 2);
            __lasx_xvstelm_d(row4, src + img_width, 0, 3);
            src += img_width_2x;
            __lasx_xvstelm_d(row5, src, 0, 0);
            __lasx_xvstelm_d(row5, src + img_width, 0, 1);
            src += img_width_2x;
            __lasx_xvstelm_d(row5, src, 0, 2);
            __lasx_xvstelm_d(row5, src + img_width, 0, 3);
            src += img_width_2x;
            __lasx_xvstelm_d(row6, src, 0, 0);
            __lasx_xvstelm_d(row6, src + img_width, 0, 1);
            src += img_width_2x;
            __lasx_xvstelm_d(row6, src, 0, 2);
            __lasx_xvstelm_d(row6, src + img_width, 0, 3);
            src += img_width_2x;
            __lasx_xvstelm_d(row7, src, 0, 0);
            __lasx_xvstelm_d(row7, src + img_width, 0, 1);
            src += img_width_2x;
            __lasx_xvstelm_d(row7, src, 0, 2);
            __lasx_xvstelm_d(row7, src + img_width, 0, 3);
        }
    }
}

void ff_h264_v_lpf_luma_intra_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                     int alpha_in, int beta_in)
{
    ptrdiff_t img_width_2x = img_width << 1;
    ptrdiff_t img_width_3x = img_width_2x + img_width;
    uint8_t *src = data - img_width_2x;
    __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
    __m256i is_less_than, is_less_than_beta, is_less_than_alpha;
    __m256i p1_org, p0_org, q0_org, q1_org;
    __m256i zero = __lasx_xvldi(0);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x,
              src, img_width_3x, p1_org, p0_org, q0_org, q1_org);
    alpha = __lasx_xvreplgr2vr_b(alpha_in);
    beta  = __lasx_xvreplgr2vr_b(beta_in);
    p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
    p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
    q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

    is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
    is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
    is_less_than       = is_less_than_beta & is_less_than_alpha;
    is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
    is_less_than       = is_less_than_beta & is_less_than;
    is_less_than       = __lasx_xvpermi_q(zero, is_less_than, 0x30);

    if (__lasx_xbnz_v(is_less_than)) {
        __m256i p2_asub_p0, q2_asub_q0, p0_h, q0_h, negate_is_less_than_beta;
        __m256i p1_org_h, p0_org_h, q0_org_h, q1_org_h;
        __m256i p2_org = __lasx_xvldx(src, -img_width);
        __m256i q2_org = __lasx_xvldx(data, img_width_2x);
        __m256i less_alpha_shift2_add2 = __lasx_xvsrli_b(alpha, 2);
        less_alpha_shift2_add2 = __lasx_xvaddi_bu(less_alpha_shift2_add2, 2);
        less_alpha_shift2_add2 = __lasx_xvslt_bu(p0_asub_q0,
                                                 less_alpha_shift2_add2);

        p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
        p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
        q0_org_h = __lasx_vext2xv_hu_bu(q0_org);
        q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

        p2_asub_p0               = __lasx_xvabsd_bu(p2_org, p0_org);
        is_less_than_beta        = __lasx_xvslt_bu(p2_asub_p0, beta);
        is_less_than_beta        = is_less_than_beta & less_alpha_shift2_add2;
        negate_is_less_than_beta = __lasx_xvxori_b(is_less_than_beta, 0xff);
        is_less_than_beta        = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        /* combine and store */
        if (__lasx_xbnz_v(is_less_than_beta)) {
            __m256i p2_org_h, p3_org_h, p1_h, p2_h;
            __m256i p3_org = __lasx_xvldx(src, -img_width_2x);

            p2_org_h   = __lasx_vext2xv_hu_bu(p2_org);
            p3_org_h   = __lasx_vext2xv_hu_bu(p3_org);

            AVC_LPF_P0P1P2_OR_Q0Q1Q2(p3_org_h, p0_org_h, q0_org_h, p1_org_h,
                                     p2_org_h, q1_org_h, p0_h, p1_h, p2_h);

            p0_h = __lasx_xvpickev_b(p0_h, p0_h);
            p0_h =  __lasx_xvpermi_d(p0_h, 0xd8);
            DUP2_ARG2(__lasx_xvpickev_b, p1_h, p1_h, p2_h, p2_h, p1_h, p2_h);
            DUP2_ARG2(__lasx_xvpermi_d, p1_h, 0xd8, p2_h, 0xd8, p1_h, p2_h);
            p0_org = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than_beta);
            p1_org = __lasx_xvbitsel_v(p1_org, p1_h, is_less_than_beta);
            p2_org = __lasx_xvbitsel_v(p2_org, p2_h, is_less_than_beta);

            __lasx_xvst(p1_org, src, 0);
            __lasx_xvst(p2_org, src - img_width, 0);
        }

        AVC_LPF_P0_OR_Q0(p0_org_h, q1_org_h, p1_org_h, p0_h);
        /* combine */
        p0_h = __lasx_xvpickev_b(p0_h, p0_h);
        p0_h = __lasx_xvpermi_d(p0_h, 0xd8);
        p0_org = __lasx_xvbitsel_v(p0_org, p0_h, negate_is_less_than_beta);
        __lasx_xvst(p0_org, data - img_width, 0);

        /* if (tmpFlag && (unsigned)ABS(q2-q0) < thresholds->beta_in) */
        q2_asub_q0 = __lasx_xvabsd_bu(q2_org, q0_org);
        is_less_than_beta = __lasx_xvslt_bu(q2_asub_q0, beta);
        is_less_than_beta = is_less_than_beta & less_alpha_shift2_add2;
        negate_is_less_than_beta = __lasx_xvxori_b(is_less_than_beta, 0xff);
        is_less_than_beta = is_less_than_beta & is_less_than;
        negate_is_less_than_beta = negate_is_less_than_beta & is_less_than;

        /* combine and store */
        if (__lasx_xbnz_v(is_less_than_beta)) {
            __m256i q2_org_h, q3_org_h, q1_h, q2_h;
            __m256i q3_org = __lasx_xvldx(data, img_width_2x + img_width);

            q2_org_h   = __lasx_vext2xv_hu_bu(q2_org);
            q3_org_h   = __lasx_vext2xv_hu_bu(q3_org);

            AVC_LPF_P0P1P2_OR_Q0Q1Q2(q3_org_h, q0_org_h, p0_org_h, q1_org_h,
                                     q2_org_h, p1_org_h, q0_h, q1_h, q2_h);

            q0_h = __lasx_xvpickev_b(q0_h, q0_h);
            q0_h = __lasx_xvpermi_d(q0_h, 0xd8);
            DUP2_ARG2(__lasx_xvpickev_b, q1_h, q1_h, q2_h, q2_h, q1_h, q2_h);
            DUP2_ARG2(__lasx_xvpermi_d, q1_h, 0xd8, q2_h, 0xd8, q1_h, q2_h);
            q0_org = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than_beta);
            q1_org = __lasx_xvbitsel_v(q1_org, q1_h, is_less_than_beta);
            q2_org = __lasx_xvbitsel_v(q2_org, q2_h, is_less_than_beta);

            __lasx_xvst(q1_org, data + img_width, 0);
            __lasx_xvst(q2_org, data + img_width_2x, 0);
        }

        AVC_LPF_P0_OR_Q0(q0_org_h, p1_org_h, q1_org_h, q0_h);

        /* combine */
        q0_h = __lasx_xvpickev_b(q0_h, q0_h);
        q0_h = __lasx_xvpermi_d(q0_h, 0xd8);
        q0_org = __lasx_xvbitsel_v(q0_org, q0_h, negate_is_less_than_beta);

        __lasx_xvst(q0_org, data, 0);
    }
}

void ff_h264_h_lpf_chroma_intra_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                       int alpha_in, int beta_in)
{
    uint8_t *src = data - 2;
    ptrdiff_t img_width_2x = img_width << 1;
    ptrdiff_t img_width_4x = img_width << 2;
    ptrdiff_t img_width_3x = img_width_2x + img_width;
    __m256i p1_org, p0_org, q0_org, q1_org;
    __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
    __m256i is_less_than, is_less_than_beta, is_less_than_alpha;

    {
        __m256i row0, row1, row2, row3, row4, row5, row6, row7;

        DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x, src,
                  img_width_3x, row0, row1, row2, row3);
        src += img_width_4x;
        DUP4_ARG2(__lasx_xvldx, src, 0, src, img_width, src, img_width_2x, src,
                  img_width_3x, row4, row5, row6, row7);

        /* LASX_TRANSPOSE8x4_B */
        DUP4_ARG2(__lasx_xvilvl_b, row2, row0, row3, row1, row6, row4, row7, row5,
                  p1_org, p0_org, q0_org, q1_org);
        row0 = __lasx_xvilvl_b(p0_org, p1_org);
        row1 = __lasx_xvilvl_b(q1_org, q0_org);
        row3 = __lasx_xvilvh_w(row1, row0);
        row2 = __lasx_xvilvl_w(row1, row0);
        p1_org = __lasx_xvpermi_d(row2, 0x00);
        p0_org = __lasx_xvpermi_d(row2, 0x55);
        q0_org = __lasx_xvpermi_d(row3, 0x00);
        q1_org = __lasx_xvpermi_d(row3, 0x55);
    }

    alpha = __lasx_xvreplgr2vr_b(alpha_in);
    beta  = __lasx_xvreplgr2vr_b(beta_in);

    p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
    p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
    q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

    is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
    is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
    is_less_than       = is_less_than_alpha & is_less_than_beta;
    is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
    is_less_than       = is_less_than_beta & is_less_than;

    if (__lasx_xbnz_v(is_less_than)) {
        __m256i p0_h, q0_h, p1_org_h, p0_org_h, q0_org_h, q1_org_h;

        p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
        p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
        q0_org_h = __lasx_vext2xv_hu_bu(q0_org);
        q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

        AVC_LPF_P0_OR_Q0(p0_org_h, q1_org_h, p1_org_h, p0_h);
        AVC_LPF_P0_OR_Q0(q0_org_h, p1_org_h, q1_org_h, q0_h);
        DUP2_ARG2(__lasx_xvpickev_b, p0_h, p0_h, q0_h, q0_h, p0_h, q0_h);
        DUP2_ARG2(__lasx_xvpermi_d, p0_h, 0xd8, q0_h, 0xd8, p0_h, q0_h);
        p0_org = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than);
        q0_org = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than);
    }
    p0_org = __lasx_xvilvl_b(q0_org, p0_org);
    src = data - 1;
    __lasx_xvstelm_h(p0_org, src, 0, 0);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 1);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 2);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 3);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 4);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 5);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 6);
    src += img_width;
    __lasx_xvstelm_h(p0_org, src, 0, 7);
}

void ff_h264_v_lpf_chroma_intra_8_lasx(uint8_t *data, ptrdiff_t img_width,
                                       int alpha_in, int beta_in)
{
    ptrdiff_t img_width_2x = img_width << 1;
    __m256i p1_org, p0_org, q0_org, q1_org;
    __m256i p0_asub_q0, p1_asub_p0, q1_asub_q0, alpha, beta;
    __m256i is_less_than, is_less_than_beta, is_less_than_alpha;

    alpha = __lasx_xvreplgr2vr_b(alpha_in);
    beta  = __lasx_xvreplgr2vr_b(beta_in);

    p1_org = __lasx_xvldx(data, -img_width_2x);
    p0_org = __lasx_xvldx(data, -img_width);
    DUP2_ARG2(__lasx_xvldx, data, 0, data, img_width, q0_org, q1_org);

    p0_asub_q0 = __lasx_xvabsd_bu(p0_org, q0_org);
    p1_asub_p0 = __lasx_xvabsd_bu(p1_org, p0_org);
    q1_asub_q0 = __lasx_xvabsd_bu(q1_org, q0_org);

    is_less_than_alpha = __lasx_xvslt_bu(p0_asub_q0, alpha);
    is_less_than_beta  = __lasx_xvslt_bu(p1_asub_p0, beta);
    is_less_than       = is_less_than_alpha & is_less_than_beta;
    is_less_than_beta  = __lasx_xvslt_bu(q1_asub_q0, beta);
    is_less_than       = is_less_than_beta & is_less_than;

    if (__lasx_xbnz_v(is_less_than)) {
        __m256i p0_h, q0_h, p1_org_h, p0_org_h, q0_org_h, q1_org_h;

        p1_org_h = __lasx_vext2xv_hu_bu(p1_org);
        p0_org_h = __lasx_vext2xv_hu_bu(p0_org);
        q0_org_h = __lasx_vext2xv_hu_bu(q0_org);
        q1_org_h = __lasx_vext2xv_hu_bu(q1_org);

        AVC_LPF_P0_OR_Q0(p0_org_h, q1_org_h, p1_org_h, p0_h);
        AVC_LPF_P0_OR_Q0(q0_org_h, p1_org_h, q1_org_h, q0_h);
        DUP2_ARG2(__lasx_xvpickev_b, p0_h, p0_h, q0_h, q0_h, p0_h, q0_h);
        DUP2_ARG2(__lasx_xvpermi_d, p0_h, 0xd8, q0_h, 0xd8, p0_h, q0_h);
        p0_h = __lasx_xvbitsel_v(p0_org, p0_h, is_less_than);
        q0_h = __lasx_xvbitsel_v(q0_org, q0_h, is_less_than);
        __lasx_xvstelm_d(p0_h, data - img_width, 0, 0);
        __lasx_xvstelm_d(q0_h, data, 0, 0);
    }
}

void ff_biweight_h264_pixels16_8_lasx(uint8_t *dst, uint8_t *src,
                                      ptrdiff_t stride, int height,
                                      int log2_denom, int weight_dst,
                                      int weight_src, int offset_in)
{
    __m256i wgt;
    __m256i src0, src1, src2, src3;
    __m256i dst0, dst1, dst2, dst3;
    __m256i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m256i denom, offset;
    int stride_2x = stride << 1;
    int stride_4x = stride << 2;
    int stride_3x = stride_2x + stride;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp4, tmp5, tmp6, tmp7);
    src += stride_4x;
    DUP4_ARG3(__lasx_xvpermi_q, tmp1, tmp0, 0x20, tmp3, tmp2, 0x20, tmp5, tmp4,
              0x20, tmp7, tmp6, 0x20, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
              dst, stride_3x, tmp0, tmp1, tmp2, tmp3);
    dst += stride_4x;
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
              dst, stride_3x, tmp4, tmp5, tmp6, tmp7);
    dst -= stride_4x;
    DUP4_ARG3(__lasx_xvpermi_q, tmp1, tmp0, 0x20, tmp3, tmp2, 0x20, tmp5, tmp4,
              0x20, tmp7, tmp6, 0x20, dst0, dst1, dst2, dst3);

    DUP4_ARG2(__lasx_xvxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvxori_b, dst0, 128, dst1, 128, dst2, 128, dst3, 128,
              dst0, dst1, dst2, dst3);
    DUP4_ARG2(__lasx_xvilvl_b, dst0, src0, dst1, src1, dst2, src2,
              dst3, src3, vec0, vec2, vec4, vec6);
    DUP4_ARG2(__lasx_xvilvh_b, dst0, src0, dst1, src1, dst2, src2,
              dst3, src3, vec1, vec3, vec5, vec7);

    DUP4_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec0, offset, wgt, vec1,
              offset, wgt, vec2, offset, wgt, vec3, tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec4, offset, wgt, vec5,
              offset, wgt, vec6, offset, wgt, vec7, tmp4, tmp5, tmp6, tmp7);

    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp1 = __lasx_xvsra_h(tmp1, denom);
    tmp2 = __lasx_xvsra_h(tmp2, denom);
    tmp3 = __lasx_xvsra_h(tmp3, denom);
    tmp4 = __lasx_xvsra_h(tmp4, denom);
    tmp5 = __lasx_xvsra_h(tmp5, denom);
    tmp6 = __lasx_xvsra_h(tmp6, denom);
    tmp7 = __lasx_xvsra_h(tmp7, denom);

    DUP4_ARG1(__lasx_xvclip255_h, tmp0, tmp1, tmp2, tmp3,
                                  tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG1(__lasx_xvclip255_h, tmp4, tmp5, tmp6, tmp7,
                                  tmp4, tmp5, tmp6, tmp7);
    DUP4_ARG2(__lasx_xvpickev_b, tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6,
              dst0, dst1, dst2, dst3);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst, 8, 1);
    dst += stride;
    __lasx_xvstelm_d(dst0, dst, 0, 2);
    __lasx_xvstelm_d(dst0, dst, 8, 3);
    dst += stride;
    __lasx_xvstelm_d(dst1, dst, 0, 0);
    __lasx_xvstelm_d(dst1, dst, 8, 1);
    dst += stride;
    __lasx_xvstelm_d(dst1, dst, 0, 2);
    __lasx_xvstelm_d(dst1, dst, 8, 3);
    dst += stride;
    __lasx_xvstelm_d(dst2, dst, 0, 0);
    __lasx_xvstelm_d(dst2, dst, 8, 1);
    dst += stride;
    __lasx_xvstelm_d(dst2, dst, 0, 2);
    __lasx_xvstelm_d(dst2, dst, 8, 3);
    dst += stride;
    __lasx_xvstelm_d(dst3, dst, 0, 0);
    __lasx_xvstelm_d(dst3, dst, 8, 1);
    dst += stride;
    __lasx_xvstelm_d(dst3, dst, 0, 2);
    __lasx_xvstelm_d(dst3, dst, 8, 3);
    dst += stride;

    if (16 == height) {
        DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
                  src, stride_3x, tmp0, tmp1, tmp2, tmp3);
        src += stride_4x;
        DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
                  src, stride_3x, tmp4, tmp5, tmp6, tmp7);
        src += stride_4x;
        DUP4_ARG3(__lasx_xvpermi_q, tmp1, tmp0, 0x20, tmp3, tmp2, 0x20, tmp5,
                  tmp4, 0x20, tmp7, tmp6, 0x20, src0, src1, src2, src3);
        DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
                  dst, stride_3x, tmp0, tmp1, tmp2, tmp3);
        dst += stride_4x;
        DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
                  dst, stride_3x, tmp4, tmp5, tmp6, tmp7);
        dst -= stride_4x;
        DUP4_ARG3(__lasx_xvpermi_q, tmp1, tmp0, 0x20, tmp3, tmp2, 0x20, tmp5,
                  tmp4, 0x20, tmp7, tmp6, 0x20, dst0, dst1, dst2, dst3);

        DUP4_ARG2(__lasx_xvxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        DUP4_ARG2(__lasx_xvxori_b, dst0, 128, dst1, 128, dst2, 128, dst3, 128,
                  dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lasx_xvilvl_b, dst0, src0, dst1, src1, dst2, src2,
                  dst3, src3, vec0, vec2, vec4, vec6);
        DUP4_ARG2(__lasx_xvilvh_b, dst0, src0, dst1, src1, dst2, src2,
                  dst3, src3, vec1, vec3, vec5, vec7);

        DUP4_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec0, offset, wgt, vec1,
                  offset, wgt, vec2, offset, wgt, vec3, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec4, offset, wgt, vec5,
                  offset, wgt, vec6, offset, wgt, vec7, tmp4, tmp5, tmp6, tmp7);

        tmp0 = __lasx_xvsra_h(tmp0, denom);
        tmp1 = __lasx_xvsra_h(tmp1, denom);
        tmp2 = __lasx_xvsra_h(tmp2, denom);
        tmp3 = __lasx_xvsra_h(tmp3, denom);
        tmp4 = __lasx_xvsra_h(tmp4, denom);
        tmp5 = __lasx_xvsra_h(tmp5, denom);
        tmp6 = __lasx_xvsra_h(tmp6, denom);
        tmp7 = __lasx_xvsra_h(tmp7, denom);

        DUP4_ARG1(__lasx_xvclip255_h, tmp0, tmp1, tmp2, tmp3,
                                      tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG1(__lasx_xvclip255_h, tmp4, tmp5, tmp6, tmp7,
                                      tmp4, tmp5, tmp6, tmp7);
        DUP4_ARG2(__lasx_xvpickev_b, tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7,
                  tmp6, dst0, dst1, dst2, dst3);
        __lasx_xvstelm_d(dst0, dst, 0, 0);
        __lasx_xvstelm_d(dst0, dst, 8, 1);
        dst += stride;
        __lasx_xvstelm_d(dst0, dst, 0, 2);
        __lasx_xvstelm_d(dst0, dst, 8, 3);
        dst += stride;
        __lasx_xvstelm_d(dst1, dst, 0, 0);
        __lasx_xvstelm_d(dst1, dst, 8, 1);
        dst += stride;
        __lasx_xvstelm_d(dst1, dst, 0, 2);
        __lasx_xvstelm_d(dst1, dst, 8, 3);
        dst += stride;
        __lasx_xvstelm_d(dst2, dst, 0, 0);
        __lasx_xvstelm_d(dst2, dst, 8, 1);
        dst += stride;
        __lasx_xvstelm_d(dst2, dst, 0, 2);
        __lasx_xvstelm_d(dst2, dst, 8, 3);
        dst += stride;
        __lasx_xvstelm_d(dst3, dst, 0, 0);
        __lasx_xvstelm_d(dst3, dst, 8, 1);
        dst += stride;
        __lasx_xvstelm_d(dst3, dst, 0, 2);
        __lasx_xvstelm_d(dst3, dst, 8, 3);
    }
}

static void avc_biwgt_8x4_lasx(uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                               int32_t log2_denom, int32_t weight_src,
                               int32_t weight_dst, int32_t offset_in)
{
    __m256i wgt, vec0, vec1;
    __m256i src0, dst0;
    __m256i tmp0, tmp1, tmp2, tmp3, denom, offset;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
              dst, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP2_ARG2(__lasx_xvxori_b, src0, 128, dst0, 128, src0, dst0);
    vec0 = __lasx_xvilvl_b(dst0, src0);
    vec1 = __lasx_xvilvh_b(dst0, src0);
    DUP2_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec0, offset, wgt, vec1,
              tmp0, tmp1);
    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp1 = __lasx_xvsra_h(tmp1, denom);
    DUP2_ARG1(__lasx_xvclip255_h, tmp0, tmp1, tmp0, tmp1);
    dst0 = __lasx_xvpickev_b(tmp1, tmp0);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst0, dst + stride_3x, 0, 3);
}

static void avc_biwgt_8x8_lasx(uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                               int32_t log2_denom, int32_t weight_src,
                               int32_t weight_dst, int32_t offset_in)
{
    __m256i wgt, vec0, vec1, vec2, vec3;
    __m256i src0, src1, dst0, dst1;
    __m256i tmp0, tmp1, tmp2, tmp3, denom, offset;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;
    uint8_t* dst_tmp = dst;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src1 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    tmp0 = __lasx_xvld(dst_tmp, 0);
    DUP2_ARG2(__lasx_xvldx, dst_tmp, stride, dst_tmp, stride_2x, tmp1, tmp2);
    tmp3 = __lasx_xvldx(dst_tmp, stride_3x);
    dst_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, dst_tmp, 0, dst_tmp, stride, dst_tmp, stride_2x,
              dst_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst1 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);

    DUP4_ARG2(__lasx_xvxori_b, src0, 128, src1, 128, dst0, 128, dst1, 128,
              src0, src1, dst0, dst1);
    DUP2_ARG2(__lasx_xvilvl_b, dst0, src0, dst1, src1, vec0, vec2);
    DUP2_ARG2(__lasx_xvilvh_b, dst0, src0, dst1, src1, vec1, vec3);
    DUP4_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec0, offset, wgt, vec1,
              offset, wgt, vec2, offset, wgt, vec3, tmp0, tmp1, tmp2, tmp3);
    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp1 = __lasx_xvsra_h(tmp1, denom);
    tmp2 = __lasx_xvsra_h(tmp2, denom);
    tmp3 = __lasx_xvsra_h(tmp3, denom);
    DUP4_ARG1(__lasx_xvclip255_h, tmp0, tmp1, tmp2, tmp3,
                                  tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvpickev_b, tmp1, tmp0, tmp3, tmp2, dst0, dst1);
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(dst1, dst, 0, 0);
    __lasx_xvstelm_d(dst1, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst1, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst1, dst + stride_3x, 0, 3);
}

static void avc_biwgt_8x16_lasx(uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                                int32_t log2_denom, int32_t weight_src,
                                int32_t weight_dst, int32_t offset_in)
{
    __m256i wgt, vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m256i src0, src1, src2, src3, dst0, dst1, dst2, dst3;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom, offset;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;
    uint8_t* dst_tmp = dst;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src1 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src2 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src3 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);

    DUP4_ARG2(__lasx_xvldx, dst_tmp, 0, dst_tmp, stride, dst_tmp, stride_2x,
              dst_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    dst_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, dst_tmp, 0, dst_tmp, stride, dst_tmp, stride_2x,
              dst_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    dst_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst1 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, dst_tmp, 0, dst_tmp, stride, dst_tmp, stride_2x,
              dst_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    dst_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst2 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, dst_tmp, 0, dst_tmp, stride, dst_tmp, stride_2x,
              dst_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst3 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);

    DUP4_ARG2(__lasx_xvxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvxori_b, dst0, 128, dst1, 128, dst2, 128, dst3, 128,
              dst0, dst1, dst2, dst3);
    DUP4_ARG2(__lasx_xvilvl_b, dst0, src0, dst1, src1, dst2, src2,
              dst3, src3, vec0, vec2, vec4, vec6);
    DUP4_ARG2(__lasx_xvilvh_b, dst0, src0, dst1, src1, dst2, src2,
              dst3, src3, vec1, vec3, vec5, vec7);
    DUP4_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec0, offset, wgt, vec1,
              offset, wgt, vec2, offset, wgt, vec3, tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG3(__lasx_xvdp2add_h_b,offset, wgt, vec4, offset, wgt, vec5,
              offset, wgt, vec6, offset, wgt, vec7, tmp4, tmp5, tmp6, tmp7);
    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp1 = __lasx_xvsra_h(tmp1, denom);
    tmp2 = __lasx_xvsra_h(tmp2, denom);
    tmp3 = __lasx_xvsra_h(tmp3, denom);
    tmp4 = __lasx_xvsra_h(tmp4, denom);
    tmp5 = __lasx_xvsra_h(tmp5, denom);
    tmp6 = __lasx_xvsra_h(tmp6, denom);
    tmp7 = __lasx_xvsra_h(tmp7, denom);
    DUP4_ARG1(__lasx_xvclip255_h, tmp0, tmp1, tmp2, tmp3,
                                  tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG1(__lasx_xvclip255_h, tmp4, tmp5, tmp6, tmp7,
                                  tmp4, tmp5, tmp6, tmp7);
    DUP4_ARG2(__lasx_xvpickev_b, tmp1, tmp0, tmp3, tmp2, tmp5, tmp4, tmp7, tmp6,
                   dst0, dst1, dst2, dst3)
    __lasx_xvstelm_d(dst0, dst, 0, 0);
    __lasx_xvstelm_d(dst0, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(dst1, dst, 0, 0);
    __lasx_xvstelm_d(dst1, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst1, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst1, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(dst2, dst, 0, 0);
    __lasx_xvstelm_d(dst2, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst2, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst2, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_d(dst3, dst, 0, 0);
    __lasx_xvstelm_d(dst3, dst + stride, 0, 1);
    __lasx_xvstelm_d(dst3, dst + stride_2x, 0, 2);
    __lasx_xvstelm_d(dst3, dst + stride_3x, 0, 3);
}

void ff_biweight_h264_pixels8_8_lasx(uint8_t *dst, uint8_t *src,
                                     ptrdiff_t stride, int height,
                                     int log2_denom, int weight_dst,
                                     int weight_src, int offset)
{
    if (4 == height) {
        avc_biwgt_8x4_lasx(src, dst, stride, log2_denom, weight_src, weight_dst,
                           offset);
    } else if (8 == height) {
        avc_biwgt_8x8_lasx(src, dst, stride, log2_denom, weight_src, weight_dst,
                           offset);
    } else {
        avc_biwgt_8x16_lasx(src, dst, stride, log2_denom, weight_src, weight_dst,
                            offset);
    }
}

static void avc_biwgt_4x2_lasx(uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                               int32_t log2_denom, int32_t weight_src,
                               int32_t weight_dst, int32_t offset_in)
{
    __m256i wgt, vec0;
    __m256i src0, dst0;
    __m256i tmp0, tmp1, denom, offset;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP2_ARG2(__lasx_xvldx, src, 0, src, stride, tmp0, tmp1);
    src0 = __lasx_xvilvl_w(tmp1, tmp0);
    DUP2_ARG2(__lasx_xvldx, dst, 0, dst, stride, tmp0, tmp1);
    dst0 = __lasx_xvilvl_w(tmp1, tmp0);
    DUP2_ARG2(__lasx_xvxori_b, src0, 128, dst0, 128, src0, dst0);
    vec0 = __lasx_xvilvl_b(dst0, src0);
    tmp0 = __lasx_xvdp2add_h_b(offset, wgt, vec0);
    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp0 = __lasx_xvclip255_h(tmp0);
    tmp0 = __lasx_xvpickev_b(tmp0, tmp0);
    __lasx_xvstelm_w(tmp0, dst, 0, 0);
    __lasx_xvstelm_w(tmp0, dst + stride, 0, 1);
}

static void avc_biwgt_4x4_lasx(uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                               int32_t log2_denom, int32_t weight_src,
                               int32_t weight_dst, int32_t offset_in)
{
    __m256i wgt, vec0;
    __m256i src0, dst0;
    __m256i tmp0, tmp1, tmp2, tmp3, denom, offset;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_w, tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
    src0 = __lasx_xvilvl_w(tmp1, tmp0);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
              dst, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_w, tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
    dst0 = __lasx_xvilvl_w(tmp1, tmp0);
    DUP2_ARG2(__lasx_xvxori_b, src0, 128, dst0, 128, src0, dst0);
    vec0 = __lasx_xvilvl_b(dst0, src0);
    dst0 = __lasx_xvilvh_b(dst0, src0);
    vec0 = __lasx_xvpermi_q(vec0, dst0, 0x02);
    tmp0 = __lasx_xvdp2add_h_b(offset, wgt, vec0);
    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp0 = __lasx_xvclip255_h(tmp0);
    tmp0 = __lasx_xvpickev_b(tmp0, tmp0);
    __lasx_xvstelm_w(tmp0, dst, 0, 0);
    __lasx_xvstelm_w(tmp0, dst + stride, 0, 1);
    __lasx_xvstelm_w(tmp0, dst + stride_2x, 0, 4);
    __lasx_xvstelm_w(tmp0, dst + stride_3x, 0, 5);
}

static void avc_biwgt_4x8_lasx(uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                               int32_t log2_denom, int32_t weight_src,
                               int32_t weight_dst, int32_t offset_in)
{
    __m256i wgt, vec0, vec1;
    __m256i src0, dst0;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom, offset;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_in   = (unsigned) ((offset_in + 1) | 1) << log2_denom;
    offset_in  += ((weight_src + weight_dst) << 7);
    log2_denom += 1;

    tmp0   = __lasx_xvreplgr2vr_b(weight_src);
    tmp1   = __lasx_xvreplgr2vr_b(weight_dst);
    wgt    = __lasx_xvilvh_b(tmp1, tmp0);
    offset = __lasx_xvreplgr2vr_h(offset_in);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp4, tmp5, tmp6, tmp7);
    DUP4_ARG2(__lasx_xvilvl_w, tmp2, tmp0, tmp3, tmp1, tmp6, tmp4, tmp7, tmp5,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_w, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
              dst, stride_3x, tmp0, tmp1, tmp2, tmp3);
    dst += stride_4x;
    DUP4_ARG2(__lasx_xvldx, dst, 0, dst, stride, dst, stride_2x,
              dst, stride_3x, tmp4, tmp5, tmp6, tmp7);
    dst -= stride_4x;
    DUP4_ARG2(__lasx_xvilvl_w, tmp2, tmp0, tmp3, tmp1, tmp6, tmp4, tmp7, tmp5,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_w, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    dst0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP2_ARG2(__lasx_xvxori_b, src0, 128, dst0, 128, src0, dst0);
    vec0 = __lasx_xvilvl_b(dst0, src0);
    vec1 = __lasx_xvilvh_b(dst0, src0);
    DUP2_ARG3(__lasx_xvdp2add_h_b, offset, wgt, vec0, offset, wgt, vec1,
              tmp0, tmp1);
    tmp0 = __lasx_xvsra_h(tmp0, denom);
    tmp1 = __lasx_xvsra_h(tmp1, denom);
    DUP2_ARG1(__lasx_xvclip255_h, tmp0, tmp1, tmp0, tmp1);
    tmp0 = __lasx_xvpickev_b(tmp1, tmp0);
    __lasx_xvstelm_w(tmp0, dst, 0, 0);
    __lasx_xvstelm_w(tmp0, dst + stride, 0, 1);
    __lasx_xvstelm_w(tmp0, dst + stride_2x, 0, 2);
    __lasx_xvstelm_w(tmp0, dst + stride_3x, 0, 3);
    dst += stride_4x;
    __lasx_xvstelm_w(tmp0, dst, 0, 4);
    __lasx_xvstelm_w(tmp0, dst + stride, 0, 5);
    __lasx_xvstelm_w(tmp0, dst + stride_2x, 0, 6);
    __lasx_xvstelm_w(tmp0, dst + stride_3x, 0, 7);
}

void ff_biweight_h264_pixels4_8_lasx(uint8_t *dst, uint8_t *src,
                                     ptrdiff_t stride, int height,
                                     int log2_denom, int weight_dst,
                                     int weight_src, int offset)
{
    if (2 == height) {
        avc_biwgt_4x2_lasx(src, dst, stride, log2_denom, weight_src,
                           weight_dst, offset);
    } else if (4 == height) {
        avc_biwgt_4x4_lasx(src, dst, stride, log2_denom, weight_src,
                           weight_dst, offset);
    } else {
        avc_biwgt_4x8_lasx(src, dst, stride, log2_denom, weight_src,
                           weight_dst, offset);
    }
}

void ff_weight_h264_pixels16_8_lasx(uint8_t *src, ptrdiff_t stride,
                                    int height, int log2_denom,
                                    int weight_src, int offset_in)
{
    uint32_t offset_val;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;
    __m256i zero = __lasx_xvldi(0);
    __m256i src0, src1, src2, src3;
    __m256i src0_l, src1_l, src2_l, src3_l, src0_h, src1_h, src2_h, src3_h;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m256i wgt, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(weight_src);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp4, tmp5, tmp6, tmp7);
    src -= stride_4x;
    DUP4_ARG3(__lasx_xvpermi_q, tmp1, tmp0, 0x20, tmp3, tmp2, 0x20, tmp5, tmp4,
              0x20, tmp7, tmp6, 0x20, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvilvl_b, zero, src0, zero, src1, zero, src2,
              zero, src3, src0_l, src1_l, src2_l, src3_l);
    DUP4_ARG2(__lasx_xvilvh_b, zero, src0, zero, src1, zero, src2,
              zero, src3, src0_h, src1_h, src2_h, src3_h);
    src0_l = __lasx_xvmul_h(wgt, src0_l);
    src0_h = __lasx_xvmul_h(wgt, src0_h);
    src1_l = __lasx_xvmul_h(wgt, src1_l);
    src1_h = __lasx_xvmul_h(wgt, src1_h);
    src2_l = __lasx_xvmul_h(wgt, src2_l);
    src2_h = __lasx_xvmul_h(wgt, src2_h);
    src3_l = __lasx_xvmul_h(wgt, src3_l);
    src3_h = __lasx_xvmul_h(wgt, src3_h);
    DUP4_ARG2(__lasx_xvsadd_h, src0_l, offset, src0_h, offset, src1_l, offset,
              src1_h, offset, src0_l, src0_h, src1_l, src1_h);
    DUP4_ARG2(__lasx_xvsadd_h, src2_l, offset, src2_h, offset, src3_l, offset,
              src3_h, offset, src2_l, src2_h, src3_l, src3_h);
    src0_l = __lasx_xvmaxi_h(src0_l, 0);
    src0_h = __lasx_xvmaxi_h(src0_h, 0);
    src1_l = __lasx_xvmaxi_h(src1_l, 0);
    src1_h = __lasx_xvmaxi_h(src1_h, 0);
    src2_l = __lasx_xvmaxi_h(src2_l, 0);
    src2_h = __lasx_xvmaxi_h(src2_h, 0);
    src3_l = __lasx_xvmaxi_h(src3_l, 0);
    src3_h = __lasx_xvmaxi_h(src3_h, 0);
    src0_l = __lasx_xvssrlrn_bu_h(src0_l, denom);
    src0_h = __lasx_xvssrlrn_bu_h(src0_h, denom);
    src1_l = __lasx_xvssrlrn_bu_h(src1_l, denom);
    src1_h = __lasx_xvssrlrn_bu_h(src1_h, denom);
    src2_l = __lasx_xvssrlrn_bu_h(src2_l, denom);
    src2_h = __lasx_xvssrlrn_bu_h(src2_h, denom);
    src3_l = __lasx_xvssrlrn_bu_h(src3_l, denom);
    src3_h = __lasx_xvssrlrn_bu_h(src3_h, denom);
    __lasx_xvstelm_d(src0_l, src, 0, 0);
    __lasx_xvstelm_d(src0_h, src, 8, 0);
    src += stride;
    __lasx_xvstelm_d(src0_l, src, 0, 2);
    __lasx_xvstelm_d(src0_h, src, 8, 2);
    src += stride;
    __lasx_xvstelm_d(src1_l, src, 0, 0);
    __lasx_xvstelm_d(src1_h, src, 8, 0);
    src += stride;
    __lasx_xvstelm_d(src1_l, src, 0, 2);
    __lasx_xvstelm_d(src1_h, src, 8, 2);
    src += stride;
    __lasx_xvstelm_d(src2_l, src, 0, 0);
    __lasx_xvstelm_d(src2_h, src, 8, 0);
    src += stride;
    __lasx_xvstelm_d(src2_l, src, 0, 2);
    __lasx_xvstelm_d(src2_h, src, 8, 2);
    src += stride;
    __lasx_xvstelm_d(src3_l, src, 0, 0);
    __lasx_xvstelm_d(src3_h, src, 8, 0);
    src += stride;
    __lasx_xvstelm_d(src3_l, src, 0, 2);
    __lasx_xvstelm_d(src3_h, src, 8, 2);
    src += stride;

    if (16 == height) {
        DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
                  src, stride_3x, tmp0, tmp1, tmp2, tmp3);
        src += stride_4x;
        DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
                  src, stride_3x, tmp4, tmp5, tmp6, tmp7);
        src -= stride_4x;
        DUP4_ARG3(__lasx_xvpermi_q, tmp1, tmp0, 0x20, tmp3, tmp2, 0x20, tmp5,
                  tmp4, 0x20, tmp7, tmp6, 0x20, src0, src1, src2, src3);
        DUP4_ARG2(__lasx_xvilvl_b, zero, src0, zero, src1, zero, src2,
                  zero, src3, src0_l, src1_l, src2_l, src3_l);
        DUP4_ARG2(__lasx_xvilvh_b, zero, src0, zero, src1, zero, src2,
                  zero, src3, src0_h, src1_h, src2_h, src3_h);
        src0_l = __lasx_xvmul_h(wgt, src0_l);
        src0_h = __lasx_xvmul_h(wgt, src0_h);
        src1_l = __lasx_xvmul_h(wgt, src1_l);
        src1_h = __lasx_xvmul_h(wgt, src1_h);
        src2_l = __lasx_xvmul_h(wgt, src2_l);
        src2_h = __lasx_xvmul_h(wgt, src2_h);
        src3_l = __lasx_xvmul_h(wgt, src3_l);
        src3_h = __lasx_xvmul_h(wgt, src3_h);
        DUP4_ARG2(__lasx_xvsadd_h, src0_l, offset, src0_h, offset, src1_l,
                  offset, src1_h, offset, src0_l, src0_h, src1_l, src1_h);
        DUP4_ARG2(__lasx_xvsadd_h, src2_l, offset, src2_h, offset, src3_l,
                  offset, src3_h, offset, src2_l, src2_h, src3_l, src3_h);
        src0_l = __lasx_xvmaxi_h(src0_l, 0);
        src0_h = __lasx_xvmaxi_h(src0_h, 0);
        src1_l = __lasx_xvmaxi_h(src1_l, 0);
        src1_h = __lasx_xvmaxi_h(src1_h, 0);
        src2_l = __lasx_xvmaxi_h(src2_l, 0);
        src2_h = __lasx_xvmaxi_h(src2_h, 0);
        src3_l = __lasx_xvmaxi_h(src3_l, 0);
        src3_h = __lasx_xvmaxi_h(src3_h, 0);
        src0_l = __lasx_xvssrlrn_bu_h(src0_l, denom);
        src0_h = __lasx_xvssrlrn_bu_h(src0_h, denom);
        src1_l = __lasx_xvssrlrn_bu_h(src1_l, denom);
        src1_h = __lasx_xvssrlrn_bu_h(src1_h, denom);
        src2_l = __lasx_xvssrlrn_bu_h(src2_l, denom);
        src2_h = __lasx_xvssrlrn_bu_h(src2_h, denom);
        src3_l = __lasx_xvssrlrn_bu_h(src3_l, denom);
        src3_h = __lasx_xvssrlrn_bu_h(src3_h, denom);
        __lasx_xvstelm_d(src0_l, src, 0, 0);
        __lasx_xvstelm_d(src0_h, src, 8, 0);
        src += stride;
        __lasx_xvstelm_d(src0_l, src, 0, 2);
        __lasx_xvstelm_d(src0_h, src, 8, 2);
        src += stride;
        __lasx_xvstelm_d(src1_l, src, 0, 0);
        __lasx_xvstelm_d(src1_h, src, 8, 0);
        src += stride;
        __lasx_xvstelm_d(src1_l, src, 0, 2);
        __lasx_xvstelm_d(src1_h, src, 8, 2);
        src += stride;
        __lasx_xvstelm_d(src2_l, src, 0, 0);
        __lasx_xvstelm_d(src2_h, src, 8, 0);
        src += stride;
        __lasx_xvstelm_d(src2_l, src, 0, 2);
        __lasx_xvstelm_d(src2_h, src, 8, 2);
        src += stride;
        __lasx_xvstelm_d(src3_l, src, 0, 0);
        __lasx_xvstelm_d(src3_h, src, 8, 0);
        src += stride;
        __lasx_xvstelm_d(src3_l, src, 0, 2);
        __lasx_xvstelm_d(src3_h, src, 8, 2);
    }
}

static void avc_wgt_8x4_lasx(uint8_t *src, ptrdiff_t stride,
                             int32_t log2_denom, int32_t weight_src,
                             int32_t offset_in)
{
    uint32_t offset_val;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;
    __m256i wgt, zero = __lasx_xvldi(0);
    __m256i src0, src0_h, src0_l;
    __m256i tmp0, tmp1, tmp2, tmp3, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(weight_src);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    src0_l = __lasx_xvilvl_b(zero, src0);
    src0_h = __lasx_xvilvh_b(zero, src0);
    src0_l = __lasx_xvmul_h(wgt, src0_l);
    src0_h = __lasx_xvmul_h(wgt, src0_h);
    src0_l = __lasx_xvsadd_h(src0_l, offset);
    src0_h = __lasx_xvsadd_h(src0_h, offset);
    src0_l = __lasx_xvmaxi_h(src0_l, 0);
    src0_h = __lasx_xvmaxi_h(src0_h, 0);
    src0_l = __lasx_xvssrlrn_bu_h(src0_l, denom);
    src0_h = __lasx_xvssrlrn_bu_h(src0_h, denom);

    src0 = __lasx_xvpickev_d(src0_h, src0_l);
    __lasx_xvstelm_d(src0, src, 0, 0);
    __lasx_xvstelm_d(src0, src + stride, 0, 1);
    __lasx_xvstelm_d(src0, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, src + stride_3x, 0, 3);
}

static void avc_wgt_8x8_lasx(uint8_t *src, ptrdiff_t stride, int32_t log2_denom,
                             int32_t src_weight, int32_t offset_in)
{
    __m256i src0, src1, src0_h, src0_l, src1_h, src1_l, zero = __lasx_xvldi(0);
    __m256i tmp0, tmp1, tmp2, tmp3, denom, offset, wgt;
    uint32_t offset_val;
    uint8_t* src_tmp = src;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(src_weight);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, stride, src_tmp, stride_2x,
              src_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, stride, src_tmp, stride_2x,
              src_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src1 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP2_ARG2(__lasx_xvilvl_b, zero, src0, zero, src1, src0_l, src1_l);
    DUP2_ARG2(__lasx_xvilvh_b, zero, src0, zero, src1, src0_h, src1_h);
    src0_l = __lasx_xvmul_h(wgt, src0_l);
    src0_h = __lasx_xvmul_h(wgt, src0_h);
    src1_l = __lasx_xvmul_h(wgt, src1_l);
    src1_h = __lasx_xvmul_h(wgt, src1_h);
    DUP4_ARG2(__lasx_xvsadd_h, src0_l, offset, src0_h, offset, src1_l, offset,
              src1_h, offset, src0_l, src0_h, src1_l, src1_h);
    src0_l = __lasx_xvmaxi_h(src0_l, 0);
    src0_h = __lasx_xvmaxi_h(src0_h, 0);
    src1_l = __lasx_xvmaxi_h(src1_l, 0);
    src1_h = __lasx_xvmaxi_h(src1_h, 0);
    src0_l = __lasx_xvssrlrn_bu_h(src0_l, denom);
    src0_h = __lasx_xvssrlrn_bu_h(src0_h, denom);
    src1_l = __lasx_xvssrlrn_bu_h(src1_l, denom);
    src1_h = __lasx_xvssrlrn_bu_h(src1_h, denom);

    DUP2_ARG2(__lasx_xvpickev_d, src0_h, src0_l, src1_h, src1_l, src0, src1);
    __lasx_xvstelm_d(src0, src, 0, 0);
    __lasx_xvstelm_d(src0, src + stride, 0, 1);
    __lasx_xvstelm_d(src0, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, src + stride_3x, 0, 3);
    src += stride_4x;
    __lasx_xvstelm_d(src1, src, 0, 0);
    __lasx_xvstelm_d(src1, src + stride, 0, 1);
    __lasx_xvstelm_d(src1, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src1, src + stride_3x, 0, 3);
}

static void avc_wgt_8x16_lasx(uint8_t *src, ptrdiff_t stride,
                              int32_t log2_denom, int32_t src_weight,
                              int32_t offset_in)
{
    __m256i src0, src1, src2, src3;
    __m256i src0_h, src0_l, src1_h, src1_l, src2_h, src2_l, src3_h, src3_l;
    __m256i tmp0, tmp1, tmp2, tmp3, denom, offset, wgt;
    __m256i zero = __lasx_xvldi(0);
    uint32_t offset_val;
    uint8_t* src_tmp = src;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(src_weight);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, stride, src_tmp, stride_2x,
              src_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, stride, src_tmp, stride_2x,
              src_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src1 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, stride, src_tmp, stride_2x,
              src_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src_tmp += stride_4x;
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src2 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    DUP4_ARG2(__lasx_xvldx, src_tmp, 0, src_tmp, stride, src_tmp, stride_2x,
              src_tmp, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src3 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);

    DUP4_ARG2(__lasx_xvilvl_b, zero, src0, zero, src1, zero, src2, zero, src3,
              src0_l, src1_l, src2_l, src3_l);
    DUP4_ARG2(__lasx_xvilvh_b, zero, src0, zero, src1, zero, src2, zero, src3,
              src0_h, src1_h, src2_h, src3_h);
    src0_l = __lasx_xvmul_h(wgt, src0_l);
    src0_h = __lasx_xvmul_h(wgt, src0_h);
    src1_l = __lasx_xvmul_h(wgt, src1_l);
    src1_h = __lasx_xvmul_h(wgt, src1_h);
    src2_l = __lasx_xvmul_h(wgt, src2_l);
    src2_h = __lasx_xvmul_h(wgt, src2_h);
    src3_l = __lasx_xvmul_h(wgt, src3_l);
    src3_h = __lasx_xvmul_h(wgt, src3_h);

    DUP4_ARG2(__lasx_xvsadd_h, src0_l, offset, src0_h, offset, src1_l, offset,
              src1_h, offset, src0_l, src0_h, src1_l, src1_h);
    DUP4_ARG2(__lasx_xvsadd_h, src2_l, offset, src2_h, offset, src3_l, offset,
              src3_h, offset, src2_l, src2_h, src3_l, src3_h);

    src0_l = __lasx_xvmaxi_h(src0_l, 0);
    src0_h = __lasx_xvmaxi_h(src0_h, 0);
    src1_l = __lasx_xvmaxi_h(src1_l, 0);
    src1_h = __lasx_xvmaxi_h(src1_h, 0);
    src2_l = __lasx_xvmaxi_h(src2_l, 0);
    src2_h = __lasx_xvmaxi_h(src2_h, 0);
    src3_l = __lasx_xvmaxi_h(src3_l, 0);
    src3_h = __lasx_xvmaxi_h(src3_h, 0);
    src0_l = __lasx_xvssrlrn_bu_h(src0_l, denom);
    src0_h = __lasx_xvssrlrn_bu_h(src0_h, denom);
    src1_l = __lasx_xvssrlrn_bu_h(src1_l, denom);
    src1_h = __lasx_xvssrlrn_bu_h(src1_h, denom);
    src2_l = __lasx_xvssrlrn_bu_h(src2_l, denom);
    src2_h = __lasx_xvssrlrn_bu_h(src2_h, denom);
    src3_l = __lasx_xvssrlrn_bu_h(src3_l, denom);
    src3_h = __lasx_xvssrlrn_bu_h(src3_h, denom);
    DUP4_ARG2(__lasx_xvpickev_d, src0_h, src0_l, src1_h, src1_l, src2_h, src2_l,
              src3_h, src3_l, src0, src1, src2, src3);

    __lasx_xvstelm_d(src0, src, 0, 0);
    __lasx_xvstelm_d(src0, src + stride, 0, 1);
    __lasx_xvstelm_d(src0, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, src + stride_3x, 0, 3);
    src += stride_4x;
    __lasx_xvstelm_d(src1, src, 0, 0);
    __lasx_xvstelm_d(src1, src + stride, 0, 1);
    __lasx_xvstelm_d(src1, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src1, src + stride_3x, 0, 3);
    src += stride_4x;
    __lasx_xvstelm_d(src2, src, 0, 0);
    __lasx_xvstelm_d(src2, src + stride, 0, 1);
    __lasx_xvstelm_d(src2, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src2, src + stride_3x, 0, 3);
    src += stride_4x;
    __lasx_xvstelm_d(src3, src, 0, 0);
    __lasx_xvstelm_d(src3, src + stride, 0, 1);
    __lasx_xvstelm_d(src3, src + stride_2x, 0, 2);
    __lasx_xvstelm_d(src3, src + stride_3x, 0, 3);
}

void ff_weight_h264_pixels8_8_lasx(uint8_t *src, ptrdiff_t stride,
                                   int height, int log2_denom,
                                   int weight_src, int offset)
{
    if (4 == height) {
        avc_wgt_8x4_lasx(src, stride, log2_denom, weight_src, offset);
    } else if (8 == height) {
        avc_wgt_8x8_lasx(src, stride, log2_denom, weight_src, offset);
    } else {
        avc_wgt_8x16_lasx(src, stride, log2_denom, weight_src, offset);
    }
}

static void avc_wgt_4x2_lasx(uint8_t *src, ptrdiff_t stride,
                             int32_t log2_denom, int32_t weight_src,
                             int32_t offset_in)
{
    uint32_t offset_val;
    __m256i wgt, zero = __lasx_xvldi(0);
    __m256i src0, tmp0, tmp1, denom, offset;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(weight_src);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP2_ARG2(__lasx_xvldx, src, 0, src, stride, tmp0, tmp1);
    src0 = __lasx_xvilvl_w(tmp1, tmp0);
    src0 = __lasx_xvilvl_b(zero, src0);
    src0 = __lasx_xvmul_h(wgt, src0);
    src0 = __lasx_xvsadd_h(src0, offset);
    src0 = __lasx_xvmaxi_h(src0, 0);
    src0 = __lasx_xvssrlrn_bu_h(src0, denom);
    __lasx_xvstelm_w(src0, src, 0, 0);
    __lasx_xvstelm_w(src0, src + stride, 0, 1);
}

static void avc_wgt_4x4_lasx(uint8_t *src, ptrdiff_t stride,
                             int32_t log2_denom, int32_t weight_src,
                             int32_t offset_in)
{
    __m256i wgt;
    __m256i src0, tmp0, tmp1, tmp2, tmp3, denom, offset;
    uint32_t offset_val;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(weight_src);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_w, tmp2, tmp0, tmp3, tmp1, tmp0, tmp1);
    src0 = __lasx_xvilvl_w(tmp1, tmp0);
    src0 = __lasx_vext2xv_hu_bu(src0);
    src0 = __lasx_xvmul_h(wgt, src0);
    src0 = __lasx_xvsadd_h(src0, offset);
    src0 = __lasx_xvmaxi_h(src0, 0);
    src0 = __lasx_xvssrlrn_bu_h(src0, denom);
    __lasx_xvstelm_w(src0, src, 0, 0);
    __lasx_xvstelm_w(src0, src + stride, 0, 1);
    __lasx_xvstelm_w(src0, src + stride_2x, 0, 4);
    __lasx_xvstelm_w(src0, src + stride_3x, 0, 5);
}

static void avc_wgt_4x8_lasx(uint8_t *src, ptrdiff_t stride,
                             int32_t log2_denom, int32_t weight_src,
                             int32_t offset_in)
{
    __m256i src0, src0_h, src0_l;
    __m256i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, denom, offset;
    __m256i wgt, zero = __lasx_xvldi(0);
    uint32_t offset_val;
    ptrdiff_t stride_2x = stride << 1;
    ptrdiff_t stride_4x = stride << 2;
    ptrdiff_t stride_3x = stride_2x + stride;

    offset_val = (unsigned) offset_in << log2_denom;

    wgt    = __lasx_xvreplgr2vr_h(weight_src);
    offset = __lasx_xvreplgr2vr_h(offset_val);
    denom  = __lasx_xvreplgr2vr_h(log2_denom);

    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp0, tmp1, tmp2, tmp3);
    src += stride_4x;
    DUP4_ARG2(__lasx_xvldx, src, 0, src, stride, src, stride_2x,
              src, stride_3x, tmp4, tmp5, tmp6, tmp7);
    src -= stride_4x;
    DUP4_ARG2(__lasx_xvilvl_w, tmp2, tmp0, tmp3, tmp1, tmp6, tmp4, tmp7,
              tmp5, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lasx_xvilvl_w, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    src0 = __lasx_xvpermi_q(tmp1, tmp0, 0x20);
    src0_l = __lasx_xvilvl_b(zero, src0);
    src0_h = __lasx_xvilvh_b(zero, src0);
    src0_l = __lasx_xvmul_h(wgt, src0_l);
    src0_h = __lasx_xvmul_h(wgt, src0_h);
    src0_l = __lasx_xvsadd_h(src0_l, offset);
    src0_h = __lasx_xvsadd_h(src0_h, offset);
    src0_l = __lasx_xvmaxi_h(src0_l, 0);
    src0_h = __lasx_xvmaxi_h(src0_h, 0);
    src0_l = __lasx_xvssrlrn_bu_h(src0_l, denom);
    src0_h = __lasx_xvssrlrn_bu_h(src0_h, denom);
    __lasx_xvstelm_w(src0_l, src, 0, 0);
    __lasx_xvstelm_w(src0_l, src + stride, 0, 1);
    __lasx_xvstelm_w(src0_h, src + stride_2x, 0, 0);
    __lasx_xvstelm_w(src0_h, src + stride_3x, 0, 1);
    src += stride_4x;
    __lasx_xvstelm_w(src0_l, src, 0, 4);
    __lasx_xvstelm_w(src0_l, src + stride, 0, 5);
    __lasx_xvstelm_w(src0_h, src + stride_2x, 0, 4);
    __lasx_xvstelm_w(src0_h, src + stride_3x, 0, 5);
}

void ff_weight_h264_pixels4_8_lasx(uint8_t *src, ptrdiff_t stride,
                                   int height, int log2_denom,
                                   int weight_src, int offset)
{
    if (2 == height) {
        avc_wgt_4x2_lasx(src, stride, log2_denom, weight_src, offset);
    } else if (4 == height) {
        avc_wgt_4x4_lasx(src, stride, log2_denom, weight_src, offset);
    } else {
        avc_wgt_4x8_lasx(src, stride, log2_denom, weight_src, offset);
    }
}

void ff_h264_add_pixels4_8_lasx(uint8_t *_dst, int16_t *_src, int stride)
{
    __m256i src0, dst0, dst1, dst2, dst3, zero;
    __m256i tmp0, tmp1;
    uint8_t* _dst1 = _dst + stride;
    uint8_t* _dst2 = _dst1 + stride;
    uint8_t* _dst3 = _dst2 + stride;

    src0 = __lasx_xvld(_src, 0);
    dst0 = __lasx_xvldrepl_w(_dst, 0);
    dst1 = __lasx_xvldrepl_w(_dst1, 0);
    dst2 = __lasx_xvldrepl_w(_dst2, 0);
    dst3 = __lasx_xvldrepl_w(_dst3, 0);
    tmp0 = __lasx_xvilvl_w(dst1, dst0);
    tmp1 = __lasx_xvilvl_w(dst3, dst2);
    dst0 = __lasx_xvilvl_d(tmp1, tmp0);
    tmp0 = __lasx_vext2xv_hu_bu(dst0);
    zero = __lasx_xvldi(0);
    tmp1 = __lasx_xvadd_h(src0, tmp0);
    dst0 = __lasx_xvpickev_b(tmp1, tmp1);
    __lasx_xvstelm_w(dst0, _dst, 0, 0);
    __lasx_xvstelm_w(dst0, _dst1, 0, 1);
    __lasx_xvstelm_w(dst0, _dst2, 0, 4);
    __lasx_xvstelm_w(dst0, _dst3, 0, 5);
    __lasx_xvst(zero, _src, 0);
}

void ff_h264_add_pixels8_8_lasx(uint8_t *_dst, int16_t *_src, int stride)
{
    __m256i src0, src1, src2, src3;
    __m256i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    __m256i tmp0, tmp1, tmp2, tmp3;
    __m256i zero = __lasx_xvldi(0);
    uint8_t *_dst1 = _dst + stride;
    uint8_t *_dst2 = _dst1 + stride;
    uint8_t *_dst3 = _dst2 + stride;
    uint8_t *_dst4 = _dst3 + stride;
    uint8_t *_dst5 = _dst4 + stride;
    uint8_t *_dst6 = _dst5 + stride;
    uint8_t *_dst7 = _dst6 + stride;

    src0 = __lasx_xvld(_src, 0);
    src1 = __lasx_xvld(_src, 32);
    src2 = __lasx_xvld(_src, 64);
    src3 = __lasx_xvld(_src, 96);
    dst0 = __lasx_xvldrepl_d(_dst, 0);
    dst1 = __lasx_xvldrepl_d(_dst1, 0);
    dst2 = __lasx_xvldrepl_d(_dst2, 0);
    dst3 = __lasx_xvldrepl_d(_dst3, 0);
    dst4 = __lasx_xvldrepl_d(_dst4, 0);
    dst5 = __lasx_xvldrepl_d(_dst5, 0);
    dst6 = __lasx_xvldrepl_d(_dst6, 0);
    dst7 = __lasx_xvldrepl_d(_dst7, 0);
    tmp0 = __lasx_xvilvl_d(dst1, dst0);
    tmp1 = __lasx_xvilvl_d(dst3, dst2);
    tmp2 = __lasx_xvilvl_d(dst5, dst4);
    tmp3 = __lasx_xvilvl_d(dst7, dst6);
    dst0 = __lasx_vext2xv_hu_bu(tmp0);
    dst1 = __lasx_vext2xv_hu_bu(tmp1);
    dst1 = __lasx_vext2xv_hu_bu(tmp1);
    dst2 = __lasx_vext2xv_hu_bu(tmp2);
    dst3 = __lasx_vext2xv_hu_bu(tmp3);
    tmp0 = __lasx_xvadd_h(src0, dst0);
    tmp1 = __lasx_xvadd_h(src1, dst1);
    tmp2 = __lasx_xvadd_h(src2, dst2);
    tmp3 = __lasx_xvadd_h(src3, dst3);
    dst1 = __lasx_xvpickev_b(tmp1, tmp0);
    dst2 = __lasx_xvpickev_b(tmp3, tmp2);
    __lasx_xvst(zero, _src, 0);
    __lasx_xvst(zero, _src, 32);
    __lasx_xvst(zero, _src, 64);
    __lasx_xvst(zero, _src, 96);
    __lasx_xvstelm_d(dst1, _dst, 0, 0);
    __lasx_xvstelm_d(dst1, _dst1, 0, 2);
    __lasx_xvstelm_d(dst1, _dst2, 0, 1);
    __lasx_xvstelm_d(dst1, _dst3, 0, 3);
    __lasx_xvstelm_d(dst2, _dst4, 0, 0);
    __lasx_xvstelm_d(dst2, _dst5, 0, 2);
    __lasx_xvstelm_d(dst2, _dst6, 0, 1);
    __lasx_xvstelm_d(dst2, _dst7, 0, 3);
}
