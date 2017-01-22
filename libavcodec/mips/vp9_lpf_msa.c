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

#define VP9_LPF_FILTER4_8W(p1_in, p0_in, q0_in, q1_in, mask_in, hev_in,  \
                           p1_out, p0_out, q0_out, q1_out)               \
{                                                                        \
    v16i8 p1_m, p0_m, q0_m, q1_m, q0_sub_p0, filt_sign;                  \
    v16i8 filt, filt1, filt2, cnst4b, cnst3b;                            \
    v8i16 q0_sub_p0_r, filt_r, cnst3h;                                   \
                                                                         \
    p1_m = (v16i8) __msa_xori_b(p1_in, 0x80);                            \
    p0_m = (v16i8) __msa_xori_b(p0_in, 0x80);                            \
    q0_m = (v16i8) __msa_xori_b(q0_in, 0x80);                            \
    q1_m = (v16i8) __msa_xori_b(q1_in, 0x80);                            \
                                                                         \
    filt = __msa_subs_s_b(p1_m, q1_m);                                   \
    filt = filt & (v16i8) hev_in;                                        \
    q0_sub_p0 = q0_m - p0_m;                                             \
    filt_sign = __msa_clti_s_b(filt, 0);                                 \
                                                                         \
    cnst3h = __msa_ldi_h(3);                                             \
    q0_sub_p0_r = (v8i16) __msa_ilvr_b(q0_sub_p0, q0_sub_p0);            \
    q0_sub_p0_r = __msa_dotp_s_h((v16i8) q0_sub_p0_r, (v16i8) cnst3h);   \
    filt_r = (v8i16) __msa_ilvr_b(filt_sign, filt);                      \
    filt_r += q0_sub_p0_r;                                               \
    filt_r = __msa_sat_s_h(filt_r, 7);                                   \
                                                                         \
    /* combine left and right part */                                    \
    filt = __msa_pckev_b((v16i8) filt_r, (v16i8) filt_r);                \
                                                                         \
    filt = filt & (v16i8) mask_in;                                       \
    cnst4b = __msa_ldi_b(4);                                             \
    filt1 = __msa_adds_s_b(filt, cnst4b);                                \
    filt1 >>= 3;                                                         \
                                                                         \
    cnst3b = __msa_ldi_b(3);                                             \
    filt2 = __msa_adds_s_b(filt, cnst3b);                                \
    filt2 >>= 3;                                                         \
                                                                         \
    q0_m = __msa_subs_s_b(q0_m, filt1);                                  \
    q0_out = __msa_xori_b((v16u8) q0_m, 0x80);                           \
    p0_m = __msa_adds_s_b(p0_m, filt2);                                  \
    p0_out = __msa_xori_b((v16u8) p0_m, 0x80);                           \
                                                                         \
    filt = __msa_srari_b(filt1, 1);                                      \
    hev_in = __msa_xori_b((v16u8) hev_in, 0xff);                         \
    filt = filt & (v16i8) hev_in;                                        \
                                                                         \
    q1_m = __msa_subs_s_b(q1_m, filt);                                   \
    q1_out = __msa_xori_b((v16u8) q1_m, 0x80);                           \
    p1_m = __msa_adds_s_b(p1_m, filt);                                   \
    p1_out = __msa_xori_b((v16u8) p1_m, 0x80);                           \
}

#define VP9_LPF_FILTER4_4W(p1_in, p0_in, q0_in, q1_in, mask_in, hev_in,  \
                           p1_out, p0_out, q0_out, q1_out)               \
{                                                                        \
    v16i8 p1_m, p0_m, q0_m, q1_m, q0_sub_p0, filt_sign;                  \
    v16i8 filt, filt1, filt2, cnst4b, cnst3b;                            \
    v8i16 q0_sub_p0_r, q0_sub_p0_l, filt_l, filt_r, cnst3h;              \
                                                                         \
    p1_m = (v16i8) __msa_xori_b(p1_in, 0x80);                            \
    p0_m = (v16i8) __msa_xori_b(p0_in, 0x80);                            \
    q0_m = (v16i8) __msa_xori_b(q0_in, 0x80);                            \
    q1_m = (v16i8) __msa_xori_b(q1_in, 0x80);                            \
                                                                         \
    filt = __msa_subs_s_b(p1_m, q1_m);                                   \
                                                                         \
    filt = filt & (v16i8) hev_in;                                        \
                                                                         \
    q0_sub_p0 = q0_m - p0_m;                                             \
    filt_sign = __msa_clti_s_b(filt, 0);                                 \
                                                                         \
    cnst3h = __msa_ldi_h(3);                                             \
    q0_sub_p0_r = (v8i16) __msa_ilvr_b(q0_sub_p0, q0_sub_p0);            \
    q0_sub_p0_r = __msa_dotp_s_h((v16i8) q0_sub_p0_r, (v16i8) cnst3h);   \
    filt_r = (v8i16) __msa_ilvr_b(filt_sign, filt);                      \
    filt_r += q0_sub_p0_r;                                               \
    filt_r = __msa_sat_s_h(filt_r, 7);                                   \
                                                                         \
    q0_sub_p0_l = (v8i16) __msa_ilvl_b(q0_sub_p0, q0_sub_p0);            \
    q0_sub_p0_l = __msa_dotp_s_h((v16i8) q0_sub_p0_l, (v16i8) cnst3h);   \
    filt_l = (v8i16) __msa_ilvl_b(filt_sign, filt);                      \
    filt_l += q0_sub_p0_l;                                               \
    filt_l = __msa_sat_s_h(filt_l, 7);                                   \
                                                                         \
    filt = __msa_pckev_b((v16i8) filt_l, (v16i8) filt_r);                \
    filt = filt & (v16i8) mask_in;                                       \
                                                                         \
    cnst4b = __msa_ldi_b(4);                                             \
    filt1 = __msa_adds_s_b(filt, cnst4b);                                \
    filt1 >>= 3;                                                         \
                                                                         \
    cnst3b = __msa_ldi_b(3);                                             \
    filt2 = __msa_adds_s_b(filt, cnst3b);                                \
    filt2 >>= 3;                                                         \
                                                                         \
    q0_m = __msa_subs_s_b(q0_m, filt1);                                  \
    q0_out = __msa_xori_b((v16u8) q0_m, 0x80);                           \
    p0_m = __msa_adds_s_b(p0_m, filt2);                                  \
    p0_out = __msa_xori_b((v16u8) p0_m, 0x80);                           \
                                                                         \
    filt = __msa_srari_b(filt1, 1);                                      \
    hev_in = __msa_xori_b((v16u8) hev_in, 0xff);                         \
    filt = filt & (v16i8) hev_in;                                        \
                                                                         \
    q1_m = __msa_subs_s_b(q1_m, filt);                                   \
    q1_out = __msa_xori_b((v16u8) q1_m, 0x80);                           \
    p1_m = __msa_adds_s_b(p1_m, filt);                                   \
    p1_out = __msa_xori_b((v16u8) p1_m, 0x80);                           \
}

#define VP9_FLAT4(p3_in, p2_in, p0_in, q0_in, q2_in, q3_in, flat_out)  \
{                                                                      \
    v16u8 tmp, p2_a_sub_p0, q2_a_sub_q0, p3_a_sub_p0, q3_a_sub_q0;     \
    v16u8 zero_in = { 0 };                                             \
                                                                       \
    tmp = __msa_ori_b(zero_in, 1);                                     \
    p2_a_sub_p0 = __msa_asub_u_b(p2_in, p0_in);                        \
    q2_a_sub_q0 = __msa_asub_u_b(q2_in, q0_in);                        \
    p3_a_sub_p0 = __msa_asub_u_b(p3_in, p0_in);                        \
    q3_a_sub_q0 = __msa_asub_u_b(q3_in, q0_in);                        \
                                                                       \
    p2_a_sub_p0 = __msa_max_u_b(p2_a_sub_p0, q2_a_sub_q0);             \
    flat_out = __msa_max_u_b(p2_a_sub_p0, flat_out);                   \
    p3_a_sub_p0 = __msa_max_u_b(p3_a_sub_p0, q3_a_sub_q0);             \
    flat_out = __msa_max_u_b(p3_a_sub_p0, flat_out);                   \
                                                                       \
    flat_out = (tmp < (v16u8) flat_out);                               \
    flat_out = __msa_xori_b(flat_out, 0xff);                           \
    flat_out = flat_out & (mask);                                      \
}

#define VP9_FLAT5(p7_in, p6_in, p5_in, p4_in, p0_in, q0_in, q4_in,  \
                  q5_in, q6_in, q7_in, flat_in, flat2_out)          \
{                                                                   \
    v16u8 tmp, zero_in = { 0 };                                     \
    v16u8 p4_a_sub_p0, q4_a_sub_q0, p5_a_sub_p0, q5_a_sub_q0;       \
    v16u8 p6_a_sub_p0, q6_a_sub_q0, p7_a_sub_p0, q7_a_sub_q0;       \
                                                                    \
    tmp = __msa_ori_b(zero_in, 1);                                  \
    p4_a_sub_p0 = __msa_asub_u_b(p4_in, p0_in);                     \
    q4_a_sub_q0 = __msa_asub_u_b(q4_in, q0_in);                     \
    p5_a_sub_p0 = __msa_asub_u_b(p5_in, p0_in);                     \
    q5_a_sub_q0 = __msa_asub_u_b(q5_in, q0_in);                     \
    p6_a_sub_p0 = __msa_asub_u_b(p6_in, p0_in);                     \
    q6_a_sub_q0 = __msa_asub_u_b(q6_in, q0_in);                     \
    p7_a_sub_p0 = __msa_asub_u_b(p7_in, p0_in);                     \
    q7_a_sub_q0 = __msa_asub_u_b(q7_in, q0_in);                     \
                                                                    \
    p4_a_sub_p0 = __msa_max_u_b(p4_a_sub_p0, q4_a_sub_q0);          \
    flat2_out = __msa_max_u_b(p5_a_sub_p0, q5_a_sub_q0);            \
    flat2_out = __msa_max_u_b(p4_a_sub_p0, flat2_out);              \
    p6_a_sub_p0 = __msa_max_u_b(p6_a_sub_p0, q6_a_sub_q0);          \
    flat2_out = __msa_max_u_b(p6_a_sub_p0, flat2_out);              \
    p7_a_sub_p0 = __msa_max_u_b(p7_a_sub_p0, q7_a_sub_q0);          \
    flat2_out = __msa_max_u_b(p7_a_sub_p0, flat2_out);              \
                                                                    \
    flat2_out = (tmp < (v16u8) flat2_out);                          \
    flat2_out = __msa_xori_b(flat2_out, 0xff);                      \
    flat2_out = flat2_out & flat_in;                                \
}

#define VP9_FILTER8(p3_in, p2_in, p1_in, p0_in,                \
                    q0_in, q1_in, q2_in, q3_in,                \
                    p2_filt8_out, p1_filt8_out, p0_filt8_out,  \
                    q0_filt8_out, q1_filt8_out, q2_filt8_out)  \
{                                                              \
    v8u16 tmp0, tmp1, tmp2;                                    \
                                                               \
    tmp2 = p2_in + p1_in + p0_in;                              \
    tmp0 = p3_in << 1;                                         \
                                                               \
    tmp0 = tmp0 + tmp2 + q0_in;                                \
    tmp1 = tmp0 + p3_in + p2_in;                               \
    p2_filt8_out = (v8i16) __msa_srari_h((v8i16) tmp1, 3);     \
                                                               \
    tmp1 = tmp0 + p1_in + q1_in;                               \
    p1_filt8_out = (v8i16) __msa_srari_h((v8i16) tmp1, 3);     \
                                                               \
    tmp1 = q2_in + q1_in + q0_in;                              \
    tmp2 = tmp2 + tmp1;                                        \
    tmp0 = tmp2 + (p0_in);                                     \
    tmp0 = tmp0 + (p3_in);                                     \
    p0_filt8_out = (v8i16) __msa_srari_h((v8i16) tmp0, 3);     \
                                                               \
    tmp0 = q2_in + q3_in;                                      \
    tmp0 = p0_in + tmp1 + tmp0;                                \
    tmp1 = q3_in + q3_in;                                      \
    tmp1 = tmp1 + tmp0;                                        \
    q2_filt8_out = (v8i16) __msa_srari_h((v8i16) tmp1, 3);     \
                                                               \
    tmp0 = tmp2 + q3_in;                                       \
    tmp1 = tmp0 + q0_in;                                       \
    q0_filt8_out = (v8i16) __msa_srari_h((v8i16) tmp1, 3);     \
                                                               \
    tmp1 = tmp0 - p2_in;                                       \
    tmp0 = q1_in + q3_in;                                      \
    tmp1 = tmp0 + tmp1;                                        \
    q1_filt8_out = (v8i16) __msa_srari_h((v8i16) tmp1, 3);     \
}

#define LPF_MASK_HEV(p3_in, p2_in, p1_in, p0_in,                   \
                     q0_in, q1_in, q2_in, q3_in,                   \
                     limit_in, b_limit_in, thresh_in,              \
                     hev_out, mask_out, flat_out)                  \
{                                                                  \
    v16u8 p3_asub_p2_m, p2_asub_p1_m, p1_asub_p0_m, q1_asub_q0_m;  \
    v16u8 p1_asub_q1_m, p0_asub_q0_m, q3_asub_q2_m, q2_asub_q1_m;  \
                                                                   \
    /* absolute subtraction of pixel values */                     \
    p3_asub_p2_m = __msa_asub_u_b(p3_in, p2_in);                   \
    p2_asub_p1_m = __msa_asub_u_b(p2_in, p1_in);                   \
    p1_asub_p0_m = __msa_asub_u_b(p1_in, p0_in);                   \
    q1_asub_q0_m = __msa_asub_u_b(q1_in, q0_in);                   \
    q2_asub_q1_m = __msa_asub_u_b(q2_in, q1_in);                   \
    q3_asub_q2_m = __msa_asub_u_b(q3_in, q2_in);                   \
    p0_asub_q0_m = __msa_asub_u_b(p0_in, q0_in);                   \
    p1_asub_q1_m = __msa_asub_u_b(p1_in, q1_in);                   \
                                                                   \
    /* calculation of hev */                                       \
    flat_out = __msa_max_u_b(p1_asub_p0_m, q1_asub_q0_m);          \
    hev_out = thresh_in < (v16u8) flat_out;                        \
                                                                   \
    /* calculation of mask */                                      \
    p0_asub_q0_m = __msa_adds_u_b(p0_asub_q0_m, p0_asub_q0_m);     \
    p1_asub_q1_m >>= 1;                                            \
    p0_asub_q0_m = __msa_adds_u_b(p0_asub_q0_m, p1_asub_q1_m);     \
                                                                   \
    mask_out = b_limit_in < p0_asub_q0_m;                          \
    mask_out = __msa_max_u_b(flat_out, mask_out);                  \
    p3_asub_p2_m = __msa_max_u_b(p3_asub_p2_m, p2_asub_p1_m);      \
    mask_out = __msa_max_u_b(p3_asub_p2_m, mask_out);              \
    q2_asub_q1_m = __msa_max_u_b(q2_asub_q1_m, q3_asub_q2_m);      \
    mask_out = __msa_max_u_b(q2_asub_q1_m, mask_out);              \
                                                                   \
    mask_out = limit_in < (v16u8) mask_out;                        \
    mask_out = __msa_xori_b(mask_out, 0xff);                       \
}

void ff_loop_filter_v_4_8_msa(uint8_t *src, ptrdiff_t pitch,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    uint64_t p1_d, p0_d, q0_d, q1_d;
    v16u8 mask, hev, flat, thresh, b_limit, limit;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0, p1_out, p0_out, q0_out, q1_out;

    /* load vector elements */
    LD_UB8((src - 4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_LPF_FILTER4_8W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    p1_d = __msa_copy_u_d((v2i64) p1_out, 0);
    p0_d = __msa_copy_u_d((v2i64) p0_out, 0);
    q0_d = __msa_copy_u_d((v2i64) q0_out, 0);
    q1_d = __msa_copy_u_d((v2i64) q1_out, 0);
    SD4(p1_d, p0_d, q0_d, q1_d, (src - 2 * pitch), pitch);
}


void ff_loop_filter_v_44_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    v16u8 mask, hev, flat, thresh0, b_limit0, limit0, thresh1, b_limit1, limit1;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;

    /* load vector elements */
    LD_UB8((src - 4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh0 = (v16u8) __msa_fill_b(thresh_ptr);
    thresh1 = (v16u8) __msa_fill_b(thresh_ptr >> 8);
    thresh0 = (v16u8) __msa_ilvr_d((v2i64) thresh1, (v2i64) thresh0);

    b_limit0 = (v16u8) __msa_fill_b(b_limit_ptr);
    b_limit1 = (v16u8) __msa_fill_b(b_limit_ptr >> 8);
    b_limit0 = (v16u8) __msa_ilvr_d((v2i64) b_limit1, (v2i64) b_limit0);

    limit0 = (v16u8) __msa_fill_b(limit_ptr);
    limit1 = (v16u8) __msa_fill_b(limit_ptr >> 8);
    limit0 = (v16u8) __msa_ilvr_d((v2i64) limit1, (v2i64) limit0);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit0, b_limit0, thresh0,
                 hev, mask, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1, p0, q0, q1);

    ST_UB4(p1, p0, q0, q1, (src - 2 * pitch), pitch);
}

void ff_loop_filter_v_8_8_msa(uint8_t *src, ptrdiff_t pitch,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    uint64_t p2_d, p1_d, p0_d, q0_d, q1_d, q2_d;
    v16u8 mask, hev, flat, thresh, b_limit, limit;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v8i16 p2_filter8, p1_filter8, p0_filter8;
    v8i16 q0_filter8, q1_filter8, q2_filter8;
    v8u16 p3_r, p2_r, p1_r, p0_r, q3_r, q2_r, q1_r, q0_r;
    v16i8 zero = { 0 };

    /* load vector elements */
    LD_UB8((src - 4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_8W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        p1_d = __msa_copy_u_d((v2i64) p1_out, 0);
        p0_d = __msa_copy_u_d((v2i64) p0_out, 0);
        q0_d = __msa_copy_u_d((v2i64) q0_out, 0);
        q1_d = __msa_copy_u_d((v2i64) q1_out, 0);
        SD4(p1_d, p0_d, q0_d, q1_d, (src - 2 * pitch), pitch);
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r,
                   q2_r, q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filter8,
                    p1_filter8, p0_filter8, q0_filter8, q1_filter8, q2_filter8);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(zero, p2_filter8, zero, p1_filter8, zero, p0_filter8,
                    zero, q0_filter8, p2_filter8, p1_filter8, p0_filter8,
                    q0_filter8);
        PCKEV_B2_SH(zero, q1_filter8, zero, q2_filter8, q1_filter8, q2_filter8);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filter8, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filter8, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filter8, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filter8, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filter8, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filter8, flat);

        p2_d = __msa_copy_u_d((v2i64) p2_out, 0);
        p1_d = __msa_copy_u_d((v2i64) p1_out, 0);
        p0_d = __msa_copy_u_d((v2i64) p0_out, 0);
        q0_d = __msa_copy_u_d((v2i64) q0_out, 0);
        q1_d = __msa_copy_u_d((v2i64) q1_out, 0);
        q2_d = __msa_copy_u_d((v2i64) q2_out, 0);

        src -= 3 * pitch;

        SD4(p2_d, p1_d, p0_d, q0_d, src, pitch);
        src += (4 * pitch);
        SD(q1_d, src);
        src += pitch;
        SD(q2_d, src);
    }
}

void ff_loop_filter_v_88_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 flat, mask, hev, tmp, thresh, b_limit, limit;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8u16 p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v8i16 p2_filt8_l, p1_filt8_l, p0_filt8_l;
    v8i16 q0_filt8_l, q1_filt8_l, q2_filt8_l;
    v16u8 zero = { 0 };

    /* load vector elements */
    LD_UB8(src - (4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    tmp = (v16u8) __msa_fill_b(thresh_ptr >> 8);
    thresh = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) thresh);

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    tmp = (v16u8) __msa_fill_b(b_limit_ptr >> 8);
    b_limit = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) b_limit);

    limit = (v16u8) __msa_fill_b(limit_ptr);
    tmp = (v16u8) __msa_fill_b(limit_ptr >> 8);
    limit = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ST_UB4(p1_out, p0_out, q0_out, q1_out, (src - 2 * pitch), pitch);
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r,
                   q2_r, q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);

        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l, p2_l, p1_l,
                   p0_l);
        ILVL_B4_UH(zero, q0, zero, q1, zero, q2, zero, q3, q0_l, q1_l, q2_l,
                   q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_l, p2_filt8_r, p1_filt8_l, p1_filt8_r, p0_filt8_l,
                    p0_filt8_r, q0_filt8_l, q0_filt8_r, p2_filt8_r, p1_filt8_r,
                    p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_l, q1_filt8_r, q2_filt8_l, q2_filt8_r,
                    q1_filt8_r, q2_filt8_r);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        src -= 3 * pitch;

        ST_UB4(p2_out, p1_out, p0_out, q0_out, src, pitch);
        src += (4 * pitch);
        ST_UB2(q1_out, q2_out, src, pitch);
        src += (2 * pitch);
    }
}

void ff_loop_filter_v_84_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 flat, mask, hev, tmp, thresh, b_limit, limit;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v16u8 zero = { 0 };

    /* load vector elements */
    LD_UB8(src - (4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    tmp = (v16u8) __msa_fill_b(thresh_ptr >> 8);
    thresh = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) thresh);

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    tmp = (v16u8) __msa_fill_b(b_limit_ptr >> 8);
    b_limit = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) b_limit);

    limit = (v16u8) __msa_fill_b(limit_ptr);
    tmp = (v16u8) __msa_fill_b(limit_ptr >> 8);
    limit = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ST_UB4(p1_out, p0_out, q0_out, q1_out, (src - 2 * pitch), pitch);
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r,
                   q2_r, q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_r, p2_filt8_r, p1_filt8_r, p1_filt8_r,
                    p0_filt8_r, p0_filt8_r, q0_filt8_r, q0_filt8_r,
                    p2_filt8_r, p1_filt8_r, p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_r, q1_filt8_r, q2_filt8_r, q2_filt8_r,
                    q1_filt8_r, q2_filt8_r);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        src -= 3 * pitch;

        ST_UB4(p2_out, p1_out, p0_out, q0_out, src, pitch);
        src += (4 * pitch);
        ST_UB2(q1_out, q2_out, src, pitch);
        src += (2 * pitch);
    }
}

void ff_loop_filter_v_48_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 flat, mask, hev, tmp, thresh, b_limit, limit;
    v8u16 p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    v8i16 p2_filt8_l, p1_filt8_l, p0_filt8_l;
    v8i16 q0_filt8_l, q1_filt8_l, q2_filt8_l;
    v16u8 zero = { 0 };

    /* load vector elements */
    LD_UB8(src - (4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    tmp = (v16u8) __msa_fill_b(thresh_ptr >> 8);
    thresh = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) thresh);

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    tmp = (v16u8) __msa_fill_b(b_limit_ptr >> 8);
    b_limit = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) b_limit);

    limit = (v16u8) __msa_fill_b(limit_ptr);
    tmp = (v16u8) __msa_fill_b(limit_ptr >> 8);
    limit = (v16u8) __msa_ilvr_d((v2i64) tmp, (v2i64) limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_insve_d((v2i64) flat, 0, (v2i64) zero);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ST_UB4(p1_out, p0_out, q0_out, q1_out, (src - 2 * pitch), pitch);
    } else {
        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l, p2_l, p1_l,
                   p0_l);
        ILVL_B4_UH(zero, q0, zero, q1, zero, q2, zero, q3, q0_l, q1_l, q2_l,
                   q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_l, p2_filt8_l, p1_filt8_l, p1_filt8_l,
                    p0_filt8_l, p0_filt8_l, q0_filt8_l, q0_filt8_l,
                    p2_filt8_l, p1_filt8_l, p0_filt8_l, q0_filt8_l);
        PCKEV_B2_SH(q1_filt8_l, q1_filt8_l, q2_filt8_l, q2_filt8_l,
                    q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filt8_l, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_l, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_l, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_l, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_l, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filt8_l, flat);

        src -= 3 * pitch;

        ST_UB4(p2_out, p1_out, p0_out, q0_out, src, pitch);
        src += (4 * pitch);
        ST_UB2(q1_out, q2_out, src, pitch);
        src += (2 * pitch);
    }
}

static int32_t vp9_hz_lpf_t4_and_t8_16w(uint8_t *src, ptrdiff_t pitch,
                                        uint8_t *filter48,
                                        int32_t b_limit_ptr,
                                        int32_t limit_ptr,
                                        int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8u16 p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v8i16 p2_filt8_l, p1_filt8_l, p0_filt8_l;
    v8i16 q0_filt8_l, q1_filt8_l, q2_filt8_l;
    v16u8 zero = { 0 };

    /* load vector elements */
    LD_UB8(src - (4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ST_UB4(p1_out, p0_out, q0_out, q1_out, (src - 2 * pitch), pitch);

        return 1;
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r,
                   q2_r, q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);

        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l, p2_l, p1_l,
                   p0_l);
        ILVL_B4_UH(zero, q0, zero, q1, zero, q2, zero, q3, q0_l, q1_l, q2_l,
                   q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_l, p2_filt8_r, p1_filt8_l, p1_filt8_r, p0_filt8_l,
                    p0_filt8_r, q0_filt8_l, q0_filt8_r, p2_filt8_r, p1_filt8_r,
                    p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_l, q1_filt8_r, q2_filt8_l, q2_filt8_r, q1_filt8_r,
                    q2_filt8_r);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        ST_UB4(p2_out, p1_out, p0_out, q0_out, filter48, 16);
        filter48 += (4 * 16);
        ST_UB2(q1_out, q2_out, filter48, 16);
        filter48 += (2 * 16);
        ST_UB(flat, filter48);

        return 0;
    }
}

static void vp9_hz_lpf_t16_16w(uint8_t *src, ptrdiff_t pitch, uint8_t *filter48)
{
    v16u8 flat, flat2, filter8;
    v16i8 zero = { 0 };
    v16u8 p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    v8u16 p7_r_in, p6_r_in, p5_r_in, p4_r_in;
    v8u16 p3_r_in, p2_r_in, p1_r_in, p0_r_in;
    v8u16 q7_r_in, q6_r_in, q5_r_in, q4_r_in;
    v8u16 q3_r_in, q2_r_in, q1_r_in, q0_r_in;
    v8u16 p7_l_in, p6_l_in, p5_l_in, p4_l_in;
    v8u16 p3_l_in, p2_l_in, p1_l_in, p0_l_in;
    v8u16 q7_l_in, q6_l_in, q5_l_in, q4_l_in;
    v8u16 q3_l_in, q2_l_in, q1_l_in, q0_l_in;
    v8u16 tmp0_r, tmp1_r, tmp0_l, tmp1_l;
    v8i16 l_out, r_out;

    flat = LD_UB(filter48 + 96);

    LD_UB8((src - 8 * pitch), pitch, p7, p6, p5, p4, p3, p2, p1, p0);
    LD_UB8(src, pitch, q0, q1, q2, q3, q4, q5, q6, q7);
    VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

    /* if flat2 is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat2)) {
        LD_UB4(filter48, 16, p2, p1, p0, q0);
        LD_UB2(filter48 + 4 * 16, 16, q1, q2);

        src -= 3 * pitch;
        ST_UB4(p2, p1, p0, q0, src, pitch);
        src += (4 * pitch);
        ST_UB2(q1, q2, src, pitch);
    } else {
        src -= 7 * pitch;

        ILVR_B8_UH(zero, p7, zero, p6, zero, p5, zero, p4, zero, p3, zero, p2,
                   zero, p1, zero, p0, p7_r_in, p6_r_in, p5_r_in, p4_r_in,
                   p3_r_in, p2_r_in, p1_r_in, p0_r_in);

        q0_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q0);

        tmp0_r = p7_r_in << 3;
        tmp0_r -= p7_r_in;
        tmp0_r += p6_r_in;
        tmp0_r += q0_r_in;
        tmp1_r = p6_r_in + p5_r_in;
        tmp1_r += p4_r_in;
        tmp1_r += p3_r_in;
        tmp1_r += p2_r_in;
        tmp1_r += p1_r_in;
        tmp1_r += p0_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        ILVL_B4_UH(zero, p7, zero, p6, zero, p5, zero, p4, p7_l_in, p6_l_in,
                   p5_l_in, p4_l_in);
        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l_in, p2_l_in,
                   p1_l_in, p0_l_in);
        q0_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q0);

        tmp0_l = p7_l_in << 3;
        tmp0_l -= p7_l_in;
        tmp0_l += p6_l_in;
        tmp0_l += q0_l_in;
        tmp1_l = p6_l_in + p5_l_in;
        tmp1_l += p4_l_in;
        tmp1_l += p3_l_in;
        tmp1_l += p2_l_in;
        tmp1_l += p1_l_in;
        tmp1_l += p0_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p6 = __msa_bmnz_v(p6, (v16u8) r_out, flat2);
        ST_UB(p6, src);
        src += pitch;

        /* p5 */
        q1_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q1);
        tmp0_r = p5_r_in - p6_r_in;
        tmp0_r += q1_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        q1_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q1);
        tmp0_l = p5_l_in - p6_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p5 = __msa_bmnz_v(p5, (v16u8) r_out, flat2);
        ST_UB(p5, src);
        src += pitch;

        /* p4 */
        q2_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q2);
        tmp0_r = p4_r_in - p5_r_in;
        tmp0_r += q2_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = (v8i16) __msa_srari_h((v8i16) tmp1_r, 4);

        q2_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q2);
        tmp0_l = p4_l_in - p5_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p4 = __msa_bmnz_v(p4, (v16u8) r_out, flat2);
        ST_UB(p4, src);
        src += pitch;

        /* p3 */
        q3_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q3);
        tmp0_r = p3_r_in - p4_r_in;
        tmp0_r += q3_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        q3_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q3);
        tmp0_l = p3_l_in - p4_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p3 = __msa_bmnz_v(p3, (v16u8) r_out, flat2);
        ST_UB(p3, src);
        src += pitch;

        /* p2 */
        q4_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q4);
        filter8 = LD_UB(filter48);
        tmp0_r = p2_r_in - p3_r_in;
        tmp0_r += q4_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        q4_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q4);
        tmp0_l = p2_l_in - p3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += pitch;

        /* p1 */
        q5_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q5);
        filter8 = LD_UB(filter48 + 16);
        tmp0_r = p1_r_in - p2_r_in;
        tmp0_r += q5_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        q5_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q5);
        tmp0_l = p1_l_in - p2_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += pitch;

        /* p0 */
        q6_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q6);
        filter8 = LD_UB(filter48 + 32);
        tmp0_r = p0_r_in - p1_r_in;
        tmp0_r += q6_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        q6_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q6);
        tmp0_l = p0_l_in - p1_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += pitch;

        /* q0 */
        q7_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q7);
        filter8 = LD_UB(filter48 + 48);
        tmp0_r = q7_r_in - p0_r_in;
        tmp0_r += q0_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        q7_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q7);
        tmp0_l = q7_l_in - p0_l_in;
        tmp0_l += q0_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += pitch;

        /* q1 */
        filter8 = LD_UB(filter48 + 64);
        tmp0_r = q7_r_in - q0_r_in;
        tmp0_r += q1_r_in;
        tmp0_r -= p6_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        tmp0_l = q7_l_in - q0_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p6_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += pitch;

        /* q2 */
        filter8 = LD_UB(filter48 + 80);
        tmp0_r = q7_r_in - q1_r_in;
        tmp0_r += q2_r_in;
        tmp0_r -= p5_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        tmp0_l = q7_l_in - q1_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p5_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += pitch;

        /* q3 */
        tmp0_r = q7_r_in - q2_r_in;
        tmp0_r += q3_r_in;
        tmp0_r -= p4_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        tmp0_l = q7_l_in - q2_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p4_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q3 = __msa_bmnz_v(q3, (v16u8) r_out, flat2);
        ST_UB(q3, src);
        src += pitch;

        /* q4 */
        tmp0_r = q7_r_in - q3_r_in;
        tmp0_r += q4_r_in;
        tmp0_r -= p3_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        tmp0_l = q7_l_in - q3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p3_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q4 = __msa_bmnz_v(q4, (v16u8) r_out, flat2);
        ST_UB(q4, src);
        src += pitch;

        /* q5 */
        tmp0_r = q7_r_in - q4_r_in;
        tmp0_r += q5_r_in;
        tmp0_r -= p2_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        tmp0_l = q7_l_in - q4_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p2_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q5 = __msa_bmnz_v(q5, (v16u8) r_out, flat2);
        ST_UB(q5, src);
        src += pitch;

        /* q6 */
        tmp0_r = q7_r_in - q5_r_in;
        tmp0_r += q6_r_in;
        tmp0_r -= p1_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        tmp0_l = q7_l_in - q5_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p1_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q6 = __msa_bmnz_v(q6, (v16u8) r_out, flat2);
        ST_UB(q6, src);
    }
}

void ff_loop_filter_v_16_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t filter48[16 * 8] ALLOC_ALIGNED(ALIGNMENT);
    uint8_t early_exit = 0;

    early_exit = vp9_hz_lpf_t4_and_t8_16w(src, pitch, &filter48[0],
                                          b_limit_ptr, limit_ptr, thresh_ptr);

    if (0 == early_exit) {
        vp9_hz_lpf_t16_16w(src, pitch, filter48);
    }
}

void ff_loop_filter_v_16_8_msa(uint8_t *src, ptrdiff_t pitch,
                               int32_t b_limit_ptr,
                               int32_t limit_ptr,
                               int32_t thresh_ptr)
{
    uint64_t p2_d, p1_d, p0_d, q0_d, q1_d, q2_d;
    uint64_t dword0, dword1;
    v16u8 flat2, mask, hev, flat, thresh, b_limit, limit;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0, p7, p6, p5, p4, q4, q5, q6, q7;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 p0_filter16, p1_filter16;
    v8i16 p2_filter8, p1_filter8, p0_filter8;
    v8i16 q0_filter8, q1_filter8, q2_filter8;
    v8u16 p7_r, p6_r, p5_r, p4_r, q7_r, q6_r, q5_r, q4_r;
    v8u16 p3_r, p2_r, p1_r, p0_r, q3_r, q2_r, q1_r, q0_r;
    v16i8 zero = { 0 };
    v8u16 tmp0, tmp1, tmp2;

    /* load vector elements */
    LD_UB8((src - 4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_8W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        p1_d = __msa_copy_u_d((v2i64) p1_out, 0);
        p0_d = __msa_copy_u_d((v2i64) p0_out, 0);
        q0_d = __msa_copy_u_d((v2i64) q0_out, 0);
        q1_d = __msa_copy_u_d((v2i64) q1_out, 0);
        SD4(p1_d, p0_d, q0_d, q1_d, src - 2 * pitch, pitch);
    } else {
        /* convert 8 bit input data into 16 bit */
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero,
                   q1, zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r,
                   q1_r, q2_r, q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r,
                    p2_filter8, p1_filter8, p0_filter8, q0_filter8,
                    q1_filter8, q2_filter8);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(zero, p2_filter8, zero, p1_filter8, zero, p0_filter8,
                    zero, q0_filter8, p2_filter8, p1_filter8, p0_filter8,
                    q0_filter8);
        PCKEV_B2_SH(zero, q1_filter8, zero, q2_filter8, q1_filter8,
                    q2_filter8);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filter8, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filter8, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filter8, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filter8, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filter8, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filter8, flat);

        /* load 16 vector elements */
        LD_UB4((src - 8 * pitch), pitch, p7, p6, p5, p4);
        LD_UB4(src + (4 * pitch), pitch, q4, q5, q6, q7);

        VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

        /* if flat2 is zero for all pixels, then no need to calculate other filter */
        if (__msa_test_bz_v(flat2)) {
            p2_d = __msa_copy_u_d((v2i64) p2_out, 0);
            p1_d = __msa_copy_u_d((v2i64) p1_out, 0);
            p0_d = __msa_copy_u_d((v2i64) p0_out, 0);
            q0_d = __msa_copy_u_d((v2i64) q0_out, 0);
            q1_d = __msa_copy_u_d((v2i64) q1_out, 0);
            q2_d = __msa_copy_u_d((v2i64) q2_out, 0);

            SD4(p2_d, p1_d, p0_d, q0_d, src - 3 * pitch, pitch);
            SD(q1_d, src + pitch);
            SD(q2_d, src + 2 * pitch);
        } else {
            /* LSB(right) 8 pixel operation */
            ILVR_B8_UH(zero, p7, zero, p6, zero, p5, zero, p4, zero, q4,
                       zero, q5, zero, q6, zero, q7, p7_r, p6_r, p5_r, p4_r,
                       q4_r, q5_r, q6_r, q7_r);

            tmp0 = p7_r << 3;
            tmp0 -= p7_r;
            tmp0 += p6_r;
            tmp0 += q0_r;

            src -= 7 * pitch;

            /* calculation of p6 and p5 */
            tmp1 = p6_r + p5_r + p4_r + p3_r;
            tmp1 += (p2_r + p1_r + p0_r);
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp0 = p5_r - p6_r + q1_r - p7_r;
            tmp1 += tmp0;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(p6, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(p5, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
            src += pitch;

            /* calculation of p4 and p3 */
            tmp0 = p4_r - p5_r + q2_r - p7_r;
            tmp2 = p3_r - p4_r + q3_r - p7_r;
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp1 += tmp2;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(p4, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(p3, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
            src += pitch;

            /* calculation of p2 and p1 */
            tmp0 = p2_r - p3_r + q4_r - p7_r;
            tmp2 = p1_r - p2_r + q5_r - p7_r;
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp1 += tmp2;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(p2_out, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(p1_out, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
            src += pitch;

            /* calculation of p0 and q0 */
            tmp0 = (p0_r - p1_r) + (q6_r - p7_r);
            tmp2 = (q7_r - p0_r) + (q0_r - p7_r);
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp1 += tmp2;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(p0_out, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(q0_out, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
            src += pitch;

            /* calculation of q1 and q2 */
            tmp0 = q7_r - q0_r + q1_r - p6_r;
            tmp2 = q7_r - q1_r + q2_r - p5_r;
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp1 += tmp2;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(q1_out, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(q2_out, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
            src += pitch;

            /* calculation of q3 and q4 */
            tmp0 = (q7_r - q2_r) + (q3_r - p4_r);
            tmp2 = (q7_r - q3_r) + (q4_r - p3_r);
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp1 += tmp2;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(q3, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(q4, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
            src += pitch;

            /* calculation of q5 and q6 */
            tmp0 = (q7_r - q4_r) + (q5_r - p2_r);
            tmp2 = (q7_r - q5_r) + (q6_r - p1_r);
            tmp1 += tmp0;
            p0_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            tmp1 += tmp2;
            p1_filter16 = (v16u8) __msa_srari_h((v8i16) tmp1, 4);
            PCKEV_B2_UB(zero, p0_filter16, zero, p1_filter16,
                        p0_filter16, p1_filter16);
            p0_filter16 = __msa_bmnz_v(q5, p0_filter16, flat2);
            p1_filter16 = __msa_bmnz_v(q6, p1_filter16, flat2);
            dword0 = __msa_copy_u_d((v2i64) p0_filter16, 0);
            dword1 = __msa_copy_u_d((v2i64) p1_filter16, 0);
            SD(dword0, src);
            src += pitch;
            SD(dword1, src);
        }
    }
}

void ff_loop_filter_h_4_8_msa(uint8_t *src, ptrdiff_t pitch,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    v16u8 mask, hev, flat, limit, thresh, b_limit;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v8i16 vec0, vec1, vec2, vec3;

    LD_UB8((src - 4), pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    TRANSPOSE8x8_UB_UB(p3, p2, p1, p0, q0, q1, q2, q3,
                       p3, p2, p1, p0, q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_LPF_FILTER4_8W(p1, p0, q0, q1, mask, hev, p1, p0, q0, q1);
    ILVR_B2_SH(p0, p1, q1, q0, vec0, vec1);
    ILVRL_H2_SH(vec1, vec0, vec2, vec3);

    src -= 2;
    ST4x4_UB(vec2, vec2, 0, 1, 2, 3, src, pitch);
    src += 4 * pitch;
    ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src, pitch);
}

void ff_loop_filter_h_44_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    v16u8 mask, hev, flat;
    v16u8 thresh0, b_limit0, limit0, thresh1, b_limit1, limit1;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7;
    v16u8 row8, row9, row10, row11, row12, row13, row14, row15;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

    LD_UB8(src - 4, pitch, row0, row1, row2, row3, row4, row5, row6, row7);
    LD_UB8(src - 4 + (8 * pitch), pitch,
           row8, row9, row10, row11, row12, row13, row14, row15);

    TRANSPOSE16x8_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh0 = (v16u8) __msa_fill_b(thresh_ptr);
    thresh1 = (v16u8) __msa_fill_b(thresh_ptr >> 8);
    thresh0 = (v16u8) __msa_ilvr_d((v2i64) thresh1, (v2i64) thresh0);

    b_limit0 = (v16u8) __msa_fill_b(b_limit_ptr);
    b_limit1 = (v16u8) __msa_fill_b(b_limit_ptr >> 8);
    b_limit0 = (v16u8) __msa_ilvr_d((v2i64) b_limit1, (v2i64) b_limit0);

    limit0 = (v16u8) __msa_fill_b(limit_ptr);
    limit1 = (v16u8) __msa_fill_b(limit_ptr >> 8);
    limit0 = (v16u8) __msa_ilvr_d((v2i64) limit1, (v2i64) limit0);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit0, b_limit0, thresh0,
                 hev, mask, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1, p0, q0, q1);
    ILVR_B2_SH(p0, p1, q1, q0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp2, tmp3);
    ILVL_B2_SH(p0, p1, q1, q0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp4, tmp5);

    src -= 2;

    ST4x8_UB(tmp2, tmp3, src, pitch);
    src += (8 * pitch);
    ST4x8_UB(tmp4, tmp5, src, pitch);
}

void ff_loop_filter_h_8_8_msa(uint8_t *src, ptrdiff_t pitch,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p1_out, p0_out, q0_out, q1_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v16u8 zero = { 0 };
    v8i16 vec0, vec1, vec2, vec3, vec4;

    /* load vector elements */
    LD_UB8(src - 4, pitch, p3, p2, p1, p0, q0, q1, q2, q3);

    TRANSPOSE8x8_UB_UB(p3, p2, p1, p0, q0, q1, q2, q3,
                       p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_8W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        /* Store 4 pixels p1-_q1 */
        ILVR_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);

        src -= 2;
        ST4x4_UB(vec2, vec2, 0, 1, 2, 3, src, pitch);
        src += 4 * pitch;
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src, pitch);
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r,
                   q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);
        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_r, p2_filt8_r, p1_filt8_r, p1_filt8_r, p0_filt8_r,
                    p0_filt8_r, q0_filt8_r, q0_filt8_r, p2_filt8_r, p1_filt8_r,
                    p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_r, q1_filt8_r, q2_filt8_r, q2_filt8_r, q1_filt8_r,
                    q2_filt8_r);

        /* store pixel values */
        p2 = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1 = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0 = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0 = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1 = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2 = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        /* Store 6 pixels p2-_q2 */
        ILVR_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);
        vec4 = (v8i16) __msa_ilvr_b((v16i8) q2, (v16i8) q1);

        src -= 3;
        ST4x4_UB(vec2, vec2, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec4, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec4, 4, src + 4, pitch);
    }
}

void ff_loop_filter_h_88_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t *temp_src;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p1_out, p0_out, q0_out, q1_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v16u8 row4, row5, row6, row7, row12, row13, row14, row15;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8u16 p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v8i16 p2_filt8_l, p1_filt8_l, p0_filt8_l;
    v8i16 q0_filt8_l, q1_filt8_l, q2_filt8_l;
    v16u8 zero = { 0 };
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;

    temp_src = src - 4;

    LD_UB8(temp_src, pitch, p0, p1, p2, p3, row4, row5, row6, row7);
    temp_src += (8 * pitch);
    LD_UB8(temp_src, pitch, q3, q2, q1, q0, row12, row13, row14, row15);

    /* transpose 16x8 matrix into 8x16 */
    TRANSPOSE16x8_UB_UB(p0, p1, p2, p3, row4, row5, row6, row7,
                        q3, q2, q1, q0, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    vec0 = (v8i16) __msa_fill_b(thresh_ptr >> 8);
    thresh = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) thresh);

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    vec0 = (v8i16) __msa_fill_b(b_limit_ptr >> 8);
    b_limit = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) b_limit);

    limit = (v16u8) __msa_fill_b(limit_ptr);
    vec0 = (v8i16) __msa_fill_b(limit_ptr >> 8);
    limit = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ILVR_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);
        ILVL_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec4, vec5);

        src -= 2;
        ST4x8_UB(vec2, vec3, src, pitch);
        src += 8 * pitch;
        ST4x8_UB(vec4, vec5, src, pitch);
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r,
                   q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);

        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l, p2_l, p1_l,
                   p0_l);
        ILVL_B4_UH(zero, q0, zero, q1, zero, q2, zero, q3, q0_l, q1_l, q2_l,
                   q3_l);

        /* filter8 */
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_l, p2_filt8_r, p1_filt8_l, p1_filt8_r, p0_filt8_l,
                    p0_filt8_r, q0_filt8_l, q0_filt8_r, p2_filt8_r, p1_filt8_r,
                    p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_l, q1_filt8_r, q2_filt8_l, q2_filt8_r, q1_filt8_r,
                    q2_filt8_r);

        /* store pixel values */
        p2 = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1 = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0 = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0 = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1 = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2 = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        ILVR_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec3, vec4);
        ILVL_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec6, vec7);
        ILVRL_B2_SH(q2, q1, vec2, vec5);

        src -= 3;
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec2, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec4, vec4, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec2, 4, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec6, vec6, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec5, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec7, vec7, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec5, 4, src + 4, pitch);
    }
}

void ff_loop_filter_h_84_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t *temp_src;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p1_out, p0_out, q0_out, q1_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v16u8 row4, row5, row6, row7, row12, row13, row14, row15;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v16u8 zero = { 0 };
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;

    temp_src = src - 4;

    LD_UB8(temp_src, pitch, p0, p1, p2, p3, row4, row5, row6, row7);
    temp_src += (8 * pitch);
    LD_UB8(temp_src, pitch, q3, q2, q1, q0, row12, row13, row14, row15);

    /* transpose 16x8 matrix into 8x16 */
    TRANSPOSE16x8_UB_UB(p0, p1, p2, p3, row4, row5, row6, row7,
                        q3, q2, q1, q0, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    vec0 = (v8i16) __msa_fill_b(thresh_ptr >> 8);
    thresh = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) thresh);

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    vec0 = (v8i16) __msa_fill_b(b_limit_ptr >> 8);
    b_limit = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) b_limit);

    limit = (v16u8) __msa_fill_b(limit_ptr);
    vec0 = (v8i16) __msa_fill_b(limit_ptr >> 8);
    limit = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ILVR_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);
        ILVL_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec4, vec5);

        src -= 2;
        ST4x8_UB(vec2, vec3, src, pitch);
        src += 8 * pitch;
        ST4x8_UB(vec4, vec5, src, pitch);
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r,
                   q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_r, p2_filt8_r, p1_filt8_r, p1_filt8_r,
                    p0_filt8_r, p0_filt8_r, q0_filt8_r, q0_filt8_r,
                    p2_filt8_r, p1_filt8_r, p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_r, q1_filt8_r, q2_filt8_r, q2_filt8_r,
                    q1_filt8_r, q2_filt8_r);

        /* store pixel values */
        p2 = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1 = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0 = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0 = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1 = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2 = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        ILVR_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec3, vec4);
        ILVL_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec6, vec7);
        ILVRL_B2_SH(q2, q1, vec2, vec5);

        src -= 3;
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec2, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec4, vec4, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec2, 4, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec6, vec6, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec5, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec7, vec7, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec5, 4, src + 4, pitch);
    }
}

void ff_loop_filter_h_48_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t *temp_src;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p1_out, p0_out, q0_out, q1_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v16u8 row4, row5, row6, row7, row12, row13, row14, row15;
    v8u16 p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    v8i16 p2_filt8_l, p1_filt8_l, p0_filt8_l;
    v8i16 q0_filt8_l, q1_filt8_l, q2_filt8_l;
    v16u8 zero = { 0 };
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;

    temp_src = src - 4;

    LD_UB8(temp_src, pitch, p0, p1, p2, p3, row4, row5, row6, row7);
    temp_src += (8 * pitch);
    LD_UB8(temp_src, pitch, q3, q2, q1, q0, row12, row13, row14, row15);

    /* transpose 16x8 matrix into 8x16 */
    TRANSPOSE16x8_UB_UB(p0, p1, p2, p3, row4, row5, row6, row7,
                        q3, q2, q1, q0, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    vec0 = (v8i16) __msa_fill_b(thresh_ptr >> 8);
    thresh = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) thresh);

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    vec0 = (v8i16) __msa_fill_b(b_limit_ptr >> 8);
    b_limit = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) b_limit);

    limit = (v16u8) __msa_fill_b(limit_ptr);
    vec0 = (v8i16) __msa_fill_b(limit_ptr >> 8);
    limit = (v16u8) __msa_ilvr_d((v2i64) vec0, (v2i64) limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_insve_d((v2i64) flat, 0, (v2i64) zero);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ILVR_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);
        ILVL_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec4, vec5);

        src -= 2;
        ST4x8_UB(vec2, vec3, src, pitch);
        src += 8 * pitch;
        ST4x8_UB(vec4, vec5, src, pitch);
    } else {
        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l, p2_l, p1_l,
                   p0_l);
        ILVL_B4_UH(zero, q0, zero, q1, zero, q2, zero, q3, q0_l, q1_l, q2_l,
                   q3_l);

        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_l, p2_filt8_l, p1_filt8_l, p1_filt8_l,
                    p0_filt8_l, p0_filt8_l, q0_filt8_l, q0_filt8_l,
                    p2_filt8_l, p1_filt8_l, p0_filt8_l, q0_filt8_l);
        PCKEV_B2_SH(q1_filt8_l, q1_filt8_l, q2_filt8_l, q2_filt8_l,
                    q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2 = __msa_bmnz_v(p2, (v16u8) p2_filt8_l, flat);
        p1 = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_l, flat);
        p0 = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_l, flat);
        q0 = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_l, flat);
        q1 = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_l, flat);
        q2 = __msa_bmnz_v(q2, (v16u8) q2_filt8_l, flat);

        ILVR_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec3, vec4);
        ILVL_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec6, vec7);
        ILVRL_B2_SH(q2, q1, vec2, vec5);

        src -= 3;
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec2, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec4, vec4, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec2, 4, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec6, vec6, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec5, 0, src + 4, pitch);
        src += (4 * pitch);
        ST4x4_UB(vec7, vec7, 0, 1, 2, 3, src, pitch);
        ST2x4_UB(vec5, 4, src + 4, pitch);
    }
}

static void vp9_transpose_16x8_to_8x16(uint8_t *input, int32_t in_pitch,
                                       uint8_t *output, int32_t out_pitch)
{
    v16u8 p7_org, p6_org, p5_org, p4_org, p3_org, p2_org, p1_org, p0_org;
    v16i8 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    v16u8 p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;

    LD_UB8(input, in_pitch,
           p7_org, p6_org, p5_org, p4_org, p3_org, p2_org, p1_org, p0_org);
    /* 8x8 transpose */
    TRANSPOSE8x8_UB_UB(p7_org, p6_org, p5_org, p4_org, p3_org, p2_org, p1_org,
                       p0_org, p7, p6, p5, p4, p3, p2, p1, p0);
    /* 8x8 transpose */
    ILVL_B4_SB(p5_org, p7_org, p4_org, p6_org, p1_org, p3_org, p0_org, p2_org,
               tmp0, tmp1, tmp2, tmp3);
    ILVR_B2_SB(tmp1, tmp0, tmp3, tmp2, tmp4, tmp6);
    ILVL_B2_SB(tmp1, tmp0, tmp3, tmp2, tmp5, tmp7);
    ILVR_W2_UB(tmp6, tmp4, tmp7, tmp5, q0, q4);
    ILVL_W2_UB(tmp6, tmp4, tmp7, tmp5, q2, q6);
    SLDI_B4_0_UB(q0, q2, q4, q6, q1, q3, q5, q7, 8);

    ST_UB8(p7, p6, p5, p4, p3, p2, p1, p0, output, out_pitch);
    output += (8 * out_pitch);
    ST_UB8(q0, q1, q2, q3, q4, q5, q6, q7, output, out_pitch);
}

static void vp9_transpose_8x16_to_16x8(uint8_t *input, int32_t in_pitch,
                                       uint8_t *output, int32_t out_pitch)
{
    v16u8 p7_o, p6_o, p5_o, p4_o, p3_o, p2_o, p1_o, p0_o;
    v16u8 p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;

    LD_UB8(input, in_pitch, p7, p6, p5, p4, p3, p2, p1, p0);
    LD_UB8(input + (8 * in_pitch), in_pitch, q0, q1, q2, q3, q4, q5, q6, q7);
    TRANSPOSE16x8_UB_UB(p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5,
                        q6, q7, p7_o, p6_o, p5_o, p4_o, p3_o, p2_o, p1_o, p0_o);
    ST_UB8(p7_o, p6_o, p5_o, p4_o, p3_o, p2_o, p1_o, p0_o, output, out_pitch);
}

static void vp9_transpose_16x16(uint8_t *input, int32_t in_pitch,
                                uint8_t *output, int32_t out_pitch)
{
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7;
    v16u8 row8, row9, row10, row11, row12, row13, row14, row15;
    v8i16 tmp0, tmp1, tmp4, tmp5, tmp6, tmp7;
    v4i32 tmp2, tmp3;
    v16u8 p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;

    LD_UB8(input, in_pitch, row0, row1, row2, row3, row4, row5, row6, row7);
    input += (8 * in_pitch);
    LD_UB8(input, in_pitch,
           row8, row9, row10, row11, row12, row13, row14, row15);

    TRANSPOSE16x8_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p7, p6, p5, p4, p3, p2, p1, p0);

    /* transpose 16x8 matrix into 8x16 */
    /* total 8 intermediate register and 32 instructions */
    q7 = (v16u8) __msa_ilvod_d((v2i64) row8, (v2i64) row0);
    q6 = (v16u8) __msa_ilvod_d((v2i64) row9, (v2i64) row1);
    q5 = (v16u8) __msa_ilvod_d((v2i64) row10, (v2i64) row2);
    q4 = (v16u8) __msa_ilvod_d((v2i64) row11, (v2i64) row3);
    q3 = (v16u8) __msa_ilvod_d((v2i64) row12, (v2i64) row4);
    q2 = (v16u8) __msa_ilvod_d((v2i64) row13, (v2i64) row5);
    q1 = (v16u8) __msa_ilvod_d((v2i64) row14, (v2i64) row6);
    q0 = (v16u8) __msa_ilvod_d((v2i64) row15, (v2i64) row7);

    ILVEV_B2_SH(q7, q6, q5, q4, tmp0, tmp1);
    tmp4 = (v8i16) __msa_ilvod_b((v16i8) q6, (v16i8) q7);
    tmp5 = (v8i16) __msa_ilvod_b((v16i8) q4, (v16i8) q5);

    ILVEV_B2_UB(q3, q2, q1, q0, q5, q7);
    tmp6 = (v8i16) __msa_ilvod_b((v16i8) q2, (v16i8) q3);
    tmp7 = (v8i16) __msa_ilvod_b((v16i8) q0, (v16i8) q1);

    ILVEV_H2_SW(tmp0, tmp1, q5, q7, tmp2, tmp3);
    q0 = (v16u8) __msa_ilvev_w(tmp3, tmp2);
    q4 = (v16u8) __msa_ilvod_w(tmp3, tmp2);

    tmp2 = (v4i32) __msa_ilvod_h(tmp1, tmp0);
    tmp3 = (v4i32) __msa_ilvod_h((v8i16) q7, (v8i16) q5);
    q2 = (v16u8) __msa_ilvev_w(tmp3, tmp2);
    q6 = (v16u8) __msa_ilvod_w(tmp3, tmp2);

    ILVEV_H2_SW(tmp4, tmp5, tmp6, tmp7, tmp2, tmp3);
    q1 = (v16u8) __msa_ilvev_w(tmp3, tmp2);
    q5 = (v16u8) __msa_ilvod_w(tmp3, tmp2);

    tmp2 = (v4i32) __msa_ilvod_h(tmp5, tmp4);
    tmp3 = (v4i32) __msa_ilvod_h(tmp7, tmp6);
    q3 = (v16u8) __msa_ilvev_w(tmp3, tmp2);
    q7 = (v16u8) __msa_ilvod_w(tmp3, tmp2);

    ST_UB8(p7, p6, p5, p4, p3, p2, p1, p0, output, out_pitch);
    output += (8 * out_pitch);
    ST_UB8(q0, q1, q2, q3, q4, q5, q6, q7, output, out_pitch);
}

static int32_t vp9_vt_lpf_t4_and_t8_8w(uint8_t *src, uint8_t *filter48,
                                       uint8_t *src_org, int32_t pitch_org,
                                       int32_t b_limit_ptr,
                                       int32_t limit_ptr,
                                       int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v16i8 zero = { 0 };
    v8i16 vec0, vec1, vec2, vec3;

    /* load vector elements */
    LD_UB8(src - (4 * 16), 16, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_8W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = (v16u8) __msa_ilvr_d((v2i64) zero, (v2i64) flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ILVR_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);
        ST4x8_UB(vec2, vec3, (src_org - 2), pitch_org);
        return 1;
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r,
                   q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);

        /* convert 16 bit output data into 8 bit */
        p2_r = (v8u16) __msa_pckev_b((v16i8) p2_filt8_r, (v16i8) p2_filt8_r);
        p1_r = (v8u16) __msa_pckev_b((v16i8) p1_filt8_r, (v16i8) p1_filt8_r);
        p0_r = (v8u16) __msa_pckev_b((v16i8) p0_filt8_r, (v16i8) p0_filt8_r);
        q0_r = (v8u16) __msa_pckev_b((v16i8) q0_filt8_r, (v16i8) q0_filt8_r);
        q1_r = (v8u16) __msa_pckev_b((v16i8) q1_filt8_r, (v16i8) q1_filt8_r);
        q2_r = (v8u16) __msa_pckev_b((v16i8) q2_filt8_r, (v16i8) q2_filt8_r);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_r, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_r, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_r, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_r, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_r, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_r, flat);

        ST_UB4(p2_out, p1_out, p0_out, q0_out, filter48, 16);
        filter48 += (4 * 16);
        ST_UB2(q1_out, q2_out, filter48, 16);
        filter48 += (2 * 16);
        ST_UB(flat, filter48);

        return 0;
    }
}

static int32_t vp9_vt_lpf_t16_8w(uint8_t *src, uint8_t *src_org, ptrdiff_t pitch,
                                 uint8_t *filter48)
{
    v16i8 zero = { 0 };
    v16u8 filter8, flat, flat2;
    v16u8 p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    v8u16 p7_r_in, p6_r_in, p5_r_in, p4_r_in;
    v8u16 p3_r_in, p2_r_in, p1_r_in, p0_r_in;
    v8u16 q7_r_in, q6_r_in, q5_r_in, q4_r_in;
    v8u16 q3_r_in, q2_r_in, q1_r_in, q0_r_in;
    v8u16 tmp0_r, tmp1_r;
    v8i16 r_out;

    flat = LD_UB(filter48 + 6 * 16);

    LD_UB8((src - 8 * 16), 16, p7, p6, p5, p4, p3, p2, p1, p0);
    LD_UB8(src, 16, q0, q1, q2, q3, q4, q5, q6, q7);

    VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

    /* if flat2 is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat2)) {
        v8i16 vec0, vec1, vec2, vec3, vec4;

        LD_UB4(filter48, 16, p2, p1, p0, q0);
        LD_UB2(filter48 + 4 * 16, 16, q1, q2);

        ILVR_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec3, vec4);
        vec2 = (v8i16) __msa_ilvr_b((v16i8) q2, (v16i8) q1);

        src_org -= 3;
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src_org, pitch);
        ST2x4_UB(vec2, 0, (src_org + 4), pitch);
        src_org += (4 * pitch);
        ST4x4_UB(vec4, vec4, 0, 1, 2, 3, src_org, pitch);
        ST2x4_UB(vec2, 4, (src_org + 4), pitch);

        return 1;
    } else {
        src -= 7 * 16;

        ILVR_B8_UH(zero, p7, zero, p6, zero, p5, zero, p4, zero, p3, zero, p2,
                   zero, p1, zero, p0, p7_r_in, p6_r_in, p5_r_in, p4_r_in,
                   p3_r_in, p2_r_in, p1_r_in, p0_r_in);
        q0_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q0);

        tmp0_r = p7_r_in << 3;
        tmp0_r -= p7_r_in;
        tmp0_r += p6_r_in;
        tmp0_r += q0_r_in;
        tmp1_r = p6_r_in + p5_r_in;
        tmp1_r += p4_r_in;
        tmp1_r += p3_r_in;
        tmp1_r += p2_r_in;
        tmp1_r += p1_r_in;
        tmp1_r += p0_r_in;
        tmp1_r += tmp0_r;

        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        p6 = __msa_bmnz_v(p6, (v16u8) r_out, flat2);
        ST8x1_UB(p6, src);
        src += 16;

        /* p5 */
        q1_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q1);
        tmp0_r = p5_r_in - p6_r_in;
        tmp0_r += q1_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        p5 = __msa_bmnz_v(p5, (v16u8) r_out, flat2);
        ST8x1_UB(p5, src);
        src += 16;

        /* p4 */
        q2_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q2);
        tmp0_r = p4_r_in - p5_r_in;
        tmp0_r += q2_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        p4 = __msa_bmnz_v(p4, (v16u8) r_out, flat2);
        ST8x1_UB(p4, src);
        src += 16;

        /* p3 */
        q3_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q3);
        tmp0_r = p3_r_in - p4_r_in;
        tmp0_r += q3_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        p3 = __msa_bmnz_v(p3, (v16u8) r_out, flat2);
        ST8x1_UB(p3, src);
        src += 16;

        /* p2 */
        q4_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q4);
        filter8 = LD_UB(filter48);
        tmp0_r = p2_r_in - p3_r_in;
        tmp0_r += q4_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST8x1_UB(filter8, src);
        src += 16;

        /* p1 */
        q5_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q5);
        filter8 = LD_UB(filter48 + 16);
        tmp0_r = p1_r_in - p2_r_in;
        tmp0_r += q5_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST8x1_UB(filter8, src);
        src += 16;

        /* p0 */
        q6_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q6);
        filter8 = LD_UB(filter48 + 32);
        tmp0_r = p0_r_in - p1_r_in;
        tmp0_r += q6_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST8x1_UB(filter8, src);
        src += 16;

        /* q0 */
        q7_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q7);
        filter8 = LD_UB(filter48 + 48);
        tmp0_r = q7_r_in - p0_r_in;
        tmp0_r += q0_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST8x1_UB(filter8, src);
        src += 16;

        /* q1 */
        filter8 = LD_UB(filter48 + 64);
        tmp0_r = q7_r_in - q0_r_in;
        tmp0_r += q1_r_in;
        tmp0_r -= p6_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST8x1_UB(filter8, src);
        src += 16;

        /* q2 */
        filter8 = LD_UB(filter48 + 80);
        tmp0_r = q7_r_in - q1_r_in;
        tmp0_r += q2_r_in;
        tmp0_r -= p5_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST8x1_UB(filter8, src);
        src += 16;

        /* q3 */
        tmp0_r = q7_r_in - q2_r_in;
        tmp0_r += q3_r_in;
        tmp0_r -= p4_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        q3 = __msa_bmnz_v(q3, (v16u8) r_out, flat2);
        ST8x1_UB(q3, src);
        src += 16;

        /* q4 */
        tmp0_r = q7_r_in - q3_r_in;
        tmp0_r += q4_r_in;
        tmp0_r -= p3_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        q4 = __msa_bmnz_v(q4, (v16u8) r_out, flat2);
        ST8x1_UB(q4, src);
        src += 16;

        /* q5 */
        tmp0_r = q7_r_in - q4_r_in;
        tmp0_r += q5_r_in;
        tmp0_r -= p2_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        q5 = __msa_bmnz_v(q5, (v16u8) r_out, flat2);
        ST8x1_UB(q5, src);
        src += 16;

        /* q6 */
        tmp0_r = q7_r_in - q5_r_in;
        tmp0_r += q6_r_in;
        tmp0_r -= p1_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) r_out, (v16i8) r_out);
        q6 = __msa_bmnz_v(q6, (v16u8) r_out, flat2);
        ST8x1_UB(q6, src);

        return 0;
    }
}

void ff_loop_filter_h_16_8_msa(uint8_t *src, ptrdiff_t pitch,
                               int32_t b_limit_ptr,
                               int32_t limit_ptr,
                               int32_t thresh_ptr)
{
    uint8_t early_exit = 0;
    uint8_t transposed_input[16 * 24] ALLOC_ALIGNED(ALIGNMENT);
    uint8_t *filter48 = &transposed_input[16 * 16];

    vp9_transpose_16x8_to_8x16(src - 8, pitch, transposed_input, 16);

    early_exit = vp9_vt_lpf_t4_and_t8_8w((transposed_input + 16 * 8),
                                         &filter48[0], src, pitch,
                                         b_limit_ptr, limit_ptr, thresh_ptr);

    if (0 == early_exit) {
        early_exit = vp9_vt_lpf_t16_8w((transposed_input + 16 * 8), src, pitch,
                                       &filter48[0]);

        if (0 == early_exit) {
            vp9_transpose_8x16_to_16x8(transposed_input, 16, src - 8, pitch);
        }
    }
}

static int32_t vp9_vt_lpf_t4_and_t8_16w(uint8_t *src, uint8_t *filter48,
                                        uint8_t *src_org, ptrdiff_t pitch,
                                        int32_t b_limit_ptr,
                                        int32_t limit_ptr,
                                        int32_t thresh_ptr)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    v16u8 flat, mask, hev, thresh, b_limit, limit;
    v8u16 p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r;
    v8u16 p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    v8i16 p2_filt8_r, p1_filt8_r, p0_filt8_r;
    v8i16 q0_filt8_r, q1_filt8_r, q2_filt8_r;
    v8i16 p2_filt8_l, p1_filt8_l, p0_filt8_l;
    v8i16 q0_filt8_l, q1_filt8_l, q2_filt8_l;
    v16i8 zero = { 0 };
    v8i16 vec0, vec1, vec2, vec3, vec4, vec5;

    /* load vector elements */
    LD_UB8(src - (4 * 16), 16, p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = (v16u8) __msa_fill_b(thresh_ptr);
    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    limit = (v16u8) __msa_fill_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat)) {
        ILVR_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec2, vec3);
        ILVL_B2_SH(p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec4, vec5);

        src_org -= 2;
        ST4x8_UB(vec2, vec3, src_org, pitch);
        src_org += 8 * pitch;
        ST4x8_UB(vec4, vec5, src_org, pitch);

        return 1;
    } else {
        ILVR_B8_UH(zero, p3, zero, p2, zero, p1, zero, p0, zero, q0, zero, q1,
                   zero, q2, zero, q3, p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r,
                   q3_r);
        VP9_FILTER8(p3_r, p2_r, p1_r, p0_r, q0_r, q1_r, q2_r, q3_r, p2_filt8_r,
                    p1_filt8_r, p0_filt8_r, q0_filt8_r, q1_filt8_r, q2_filt8_r);
        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l, p2_l, p1_l,
                   p0_l);
        ILVL_B4_UH(zero, q0, zero, q1, zero, q2, zero, q3, q0_l, q1_l, q2_l,
                   q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        PCKEV_B4_SH(p2_filt8_l, p2_filt8_r, p1_filt8_l, p1_filt8_r, p0_filt8_l,
                    p0_filt8_r, q0_filt8_l, q0_filt8_r, p2_filt8_r, p1_filt8_r,
                    p0_filt8_r, q0_filt8_r);
        PCKEV_B2_SH(q1_filt8_l, q1_filt8_r, q2_filt8_l, q2_filt8_r, q1_filt8_r,
                    q2_filt8_r);

        /* store pixel values */
        p2_out = __msa_bmnz_v(p2, (v16u8) p2_filt8_r, flat);
        p1_out = __msa_bmnz_v(p1_out, (v16u8) p1_filt8_r, flat);
        p0_out = __msa_bmnz_v(p0_out, (v16u8) p0_filt8_r, flat);
        q0_out = __msa_bmnz_v(q0_out, (v16u8) q0_filt8_r, flat);
        q1_out = __msa_bmnz_v(q1_out, (v16u8) q1_filt8_r, flat);
        q2_out = __msa_bmnz_v(q2, (v16u8) q2_filt8_r, flat);

        ST_UB4(p2_out, p1_out, p0_out, q0_out, filter48, 16);
        filter48 += (4 * 16);
        ST_UB2(q1_out, q2_out, filter48, 16);
        filter48 += (2 * 16);
        ST_UB(flat, filter48);

        return 0;
    }
}

static int32_t vp9_vt_lpf_t16_16w(uint8_t *src, uint8_t *src_org, ptrdiff_t pitch,
                                  uint8_t *filter48)
{
    v16u8 flat, flat2, filter8;
    v16i8 zero = { 0 };
    v16u8 p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    v8u16 p7_r_in, p6_r_in, p5_r_in, p4_r_in;
    v8u16 p3_r_in, p2_r_in, p1_r_in, p0_r_in;
    v8u16 q7_r_in, q6_r_in, q5_r_in, q4_r_in;
    v8u16 q3_r_in, q2_r_in, q1_r_in, q0_r_in;
    v8u16 p7_l_in, p6_l_in, p5_l_in, p4_l_in;
    v8u16 p3_l_in, p2_l_in, p1_l_in, p0_l_in;
    v8u16 q7_l_in, q6_l_in, q5_l_in, q4_l_in;
    v8u16 q3_l_in, q2_l_in, q1_l_in, q0_l_in;
    v8u16 tmp0_r, tmp1_r, tmp0_l, tmp1_l;
    v8i16 l_out, r_out;

    flat = LD_UB(filter48 + 6 * 16);

    LD_UB8((src - 8 * 16), 16, p7, p6, p5, p4, p3, p2, p1, p0);
    LD_UB8(src, 16, q0, q1, q2, q3, q4, q5, q6, q7);

    VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

    /* if flat2 is zero for all pixels, then no need to calculate other filter */
    if (__msa_test_bz_v(flat2)) {
        v8i16 vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;

        LD_UB4(filter48, 16, p2, p1, p0, q0);
        LD_UB2(filter48 + 4 * 16, 16, q1, q2);

        ILVR_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec3, vec4);
        ILVL_B2_SH(p1, p2, q0, p0, vec0, vec1);
        ILVRL_H2_SH(vec1, vec0, vec6, vec7);
        ILVRL_B2_SH(q2, q1, vec2, vec5);

        src_org -= 3;
        ST4x4_UB(vec3, vec3, 0, 1, 2, 3, src_org, pitch);
        ST2x4_UB(vec2, 0, (src_org + 4), pitch);
        src_org += (4 * pitch);
        ST4x4_UB(vec4, vec4, 0, 1, 2, 3, src_org, pitch);
        ST2x4_UB(vec2, 4, (src_org + 4), pitch);
        src_org += (4 * pitch);
        ST4x4_UB(vec6, vec6, 0, 1, 2, 3, src_org, pitch);
        ST2x4_UB(vec5, 0, (src_org + 4), pitch);
        src_org += (4 * pitch);
        ST4x4_UB(vec7, vec7, 0, 1, 2, 3, src_org, pitch);
        ST2x4_UB(vec5, 4, (src_org + 4), pitch);

        return 1;
    } else {
        src -= 7 * 16;

        ILVR_B8_UH(zero, p7, zero, p6, zero, p5, zero, p4, zero, p3, zero, p2,
                   zero, p1, zero, p0, p7_r_in, p6_r_in, p5_r_in, p4_r_in,
                   p3_r_in, p2_r_in, p1_r_in, p0_r_in);
        q0_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q0);

        tmp0_r = p7_r_in << 3;
        tmp0_r -= p7_r_in;
        tmp0_r += p6_r_in;
        tmp0_r += q0_r_in;
        tmp1_r = p6_r_in + p5_r_in;
        tmp1_r += p4_r_in;
        tmp1_r += p3_r_in;
        tmp1_r += p2_r_in;
        tmp1_r += p1_r_in;
        tmp1_r += p0_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);

        ILVL_B4_UH(zero, p7, zero, p6, zero, p5, zero, p4, p7_l_in, p6_l_in,
                   p5_l_in, p4_l_in);
        ILVL_B4_UH(zero, p3, zero, p2, zero, p1, zero, p0, p3_l_in, p2_l_in,
                   p1_l_in, p0_l_in);
        q0_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q0);

        tmp0_l = p7_l_in << 3;
        tmp0_l -= p7_l_in;
        tmp0_l += p6_l_in;
        tmp0_l += q0_l_in;
        tmp1_l = p6_l_in + p5_l_in;
        tmp1_l += p4_l_in;
        tmp1_l += p3_l_in;
        tmp1_l += p2_l_in;
        tmp1_l += p1_l_in;
        tmp1_l += p0_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);

        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p6 = __msa_bmnz_v(p6, (v16u8) r_out, flat2);
        ST_UB(p6, src);
        src += 16;

        /* p5 */
        q1_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q1);
        tmp0_r = p5_r_in - p6_r_in;
        tmp0_r += q1_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q1_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q1);
        tmp0_l = p5_l_in - p6_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p5 = __msa_bmnz_v(p5, (v16u8) r_out, flat2);
        ST_UB(p5, src);
        src += 16;

        /* p4 */
        q2_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q2);
        tmp0_r = p4_r_in - p5_r_in;
        tmp0_r += q2_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q2_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q2);
        tmp0_l = p4_l_in - p5_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p4 = __msa_bmnz_v(p4, (v16u8) r_out, flat2);
        ST_UB(p4, src);
        src += 16;

        /* p3 */
        q3_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q3);
        tmp0_r = p3_r_in - p4_r_in;
        tmp0_r += q3_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q3_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q3);
        tmp0_l = p3_l_in - p4_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        p3 = __msa_bmnz_v(p3, (v16u8) r_out, flat2);
        ST_UB(p3, src);
        src += 16;

        /* p2 */
        q4_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q4);
        filter8 = LD_UB(filter48);
        tmp0_r = p2_r_in - p3_r_in;
        tmp0_r += q4_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q4_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q4);
        tmp0_l = p2_l_in - p3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += 16;

        /* p1 */
        q5_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q5);
        filter8 = LD_UB(filter48 + 16);
        tmp0_r = p1_r_in - p2_r_in;
        tmp0_r += q5_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q5_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q5);
        tmp0_l = p1_l_in - p2_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) (tmp1_l), 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += 16;

        /* p0 */
        q6_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q6);
        filter8 = LD_UB(filter48 + 32);
        tmp0_r = p0_r_in - p1_r_in;
        tmp0_r += q6_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q6_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q6);
        tmp0_l = p0_l_in - p1_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += 16;

        /* q0 */
        q7_r_in = (v8u16) __msa_ilvr_b(zero, (v16i8) q7);
        filter8 = LD_UB(filter48 + 48);
        tmp0_r = q7_r_in - p0_r_in;
        tmp0_r += q0_r_in;
        tmp0_r -= p7_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        q7_l_in = (v8u16) __msa_ilvl_b(zero, (v16i8) q7);
        tmp0_l = q7_l_in - p0_l_in;
        tmp0_l += q0_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += 16;

        /* q1 */
        filter8 = LD_UB(filter48 + 64);
        tmp0_r = q7_r_in - q0_r_in;
        tmp0_r += q1_r_in;
        tmp0_r -= p6_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        tmp0_l = q7_l_in - q0_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p6_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += 16;

        /* q2 */
        filter8 = LD_UB(filter48 + 80);
        tmp0_r = q7_r_in - q1_r_in;
        tmp0_r += q2_r_in;
        tmp0_r -= p5_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        tmp0_l = q7_l_in - q1_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p5_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        filter8 = __msa_bmnz_v(filter8, (v16u8) r_out, flat2);
        ST_UB(filter8, src);
        src += 16;

        /* q3 */
        tmp0_r = q7_r_in - q2_r_in;
        tmp0_r += q3_r_in;
        tmp0_r -= p4_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        tmp0_l = q7_l_in - q2_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p4_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q3 = __msa_bmnz_v(q3, (v16u8) r_out, flat2);
        ST_UB(q3, src);
        src += 16;

        /* q4 */
        tmp0_r = q7_r_in - q3_r_in;
        tmp0_r += q4_r_in;
        tmp0_r -= p3_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        tmp0_l = q7_l_in - q3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p3_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q4 = __msa_bmnz_v(q4, (v16u8) r_out, flat2);
        ST_UB(q4, src);
        src += 16;

        /* q5 */
        tmp0_r = q7_r_in - q4_r_in;
        tmp0_r += q5_r_in;
        tmp0_r -= p2_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        tmp0_l = q7_l_in - q4_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p2_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q5 = __msa_bmnz_v(q5, (v16u8) r_out, flat2);
        ST_UB(q5, src);
        src += 16;

        /* q6 */
        tmp0_r = q7_r_in - q5_r_in;
        tmp0_r += q6_r_in;
        tmp0_r -= p1_r_in;
        tmp1_r += tmp0_r;
        r_out = __msa_srari_h((v8i16) tmp1_r, 4);
        tmp0_l = q7_l_in - q5_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p1_l_in;
        tmp1_l += tmp0_l;
        l_out = __msa_srari_h((v8i16) tmp1_l, 4);
        r_out = (v8i16) __msa_pckev_b((v16i8) l_out, (v16i8) r_out);
        q6 = __msa_bmnz_v(q6, (v16u8) r_out, flat2);
        ST_UB(q6, src);

        return 0;
    }
}

void ff_loop_filter_h_16_16_msa(uint8_t *src, ptrdiff_t pitch,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t early_exit = 0;
    uint8_t transposed_input[16 * 24] ALLOC_ALIGNED(ALIGNMENT);
    uint8_t *filter48 = &transposed_input[16 * 16];

    vp9_transpose_16x16((src - 8), pitch, &transposed_input[0], 16);

    early_exit = vp9_vt_lpf_t4_and_t8_16w((transposed_input + 16 * 8),
                                          &filter48[0], src, pitch,
                                          b_limit_ptr, limit_ptr, thresh_ptr);

    if (0 == early_exit) {
        early_exit = vp9_vt_lpf_t16_16w((transposed_input + 16 * 8), src, pitch,
                                        &filter48[0]);

        if (0 == early_exit) {
            vp9_transpose_16x16(transposed_input, 16, (src - 8), pitch);
        }
    }
}
