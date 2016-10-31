/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "libavcodec/vp8dsp.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "vp8dsp_mips.h"

#define VP8_SIMPLE_MASK(p1, p0, q0, q1, b_limit, mask)           \
{                                                                \
    v16u8 p1_a_sub_q1, p0_a_sub_q0;                              \
                                                                 \
    p0_a_sub_q0 = __msa_asub_u_b(p0, q0);                        \
    p1_a_sub_q1 = __msa_asub_u_b(p1, q1);                        \
    p1_a_sub_q1 = (v16u8) __msa_srli_b((v16i8) p1_a_sub_q1, 1);  \
    p0_a_sub_q0 = __msa_adds_u_b(p0_a_sub_q0, p0_a_sub_q0);      \
    mask = __msa_adds_u_b(p0_a_sub_q0, p1_a_sub_q1);             \
    mask = ((v16u8) mask <= b_limit);                            \
}

#define VP8_LPF_FILTER4_4W(p1_in_out, p0_in_out, q0_in_out, q1_in_out,  \
                           mask_in, hev_in)                             \
{                                                                       \
    v16i8 p1_m, p0_m, q0_m, q1_m, q0_sub_p0, filt_sign;                 \
    v16i8 filt, filt1, filt2, cnst4b, cnst3b;                           \
    v8i16 q0_sub_p0_r, q0_sub_p0_l, filt_l, filt_r, cnst3h;             \
                                                                        \
    p1_m = (v16i8) __msa_xori_b(p1_in_out, 0x80);                       \
    p0_m = (v16i8) __msa_xori_b(p0_in_out, 0x80);                       \
    q0_m = (v16i8) __msa_xori_b(q0_in_out, 0x80);                       \
    q1_m = (v16i8) __msa_xori_b(q1_in_out, 0x80);                       \
                                                                        \
    filt = __msa_subs_s_b(p1_m, q1_m);                                  \
                                                                        \
    filt = filt & (v16i8) hev_in;                                       \
                                                                        \
    q0_sub_p0 = q0_m - p0_m;                                            \
    filt_sign = __msa_clti_s_b(filt, 0);                                \
                                                                        \
    cnst3h = __msa_ldi_h(3);                                            \
    q0_sub_p0_r = (v8i16) __msa_ilvr_b(q0_sub_p0, q0_sub_p0);           \
    q0_sub_p0_r = __msa_dotp_s_h((v16i8) q0_sub_p0_r, (v16i8) cnst3h);  \
    filt_r = (v8i16) __msa_ilvr_b(filt_sign, filt);                     \
    filt_r += q0_sub_p0_r;                                              \
    filt_r = __msa_sat_s_h(filt_r, 7);                                  \
                                                                        \
    q0_sub_p0_l = (v8i16) __msa_ilvl_b(q0_sub_p0, q0_sub_p0);           \
    q0_sub_p0_l = __msa_dotp_s_h((v16i8) q0_sub_p0_l, (v16i8) cnst3h);  \
    filt_l = (v8i16) __msa_ilvl_b(filt_sign, filt);                     \
    filt_l += q0_sub_p0_l;                                              \
    filt_l = __msa_sat_s_h(filt_l, 7);                                  \
                                                                        \
    filt = __msa_pckev_b((v16i8) filt_l, (v16i8) filt_r);               \
    filt = filt & (v16i8) mask_in;                                      \
                                                                        \
    cnst4b = __msa_ldi_b(4);                                            \
    filt1 = __msa_adds_s_b(filt, cnst4b);                               \
    filt1 >>= 3;                                                        \
                                                                        \
    cnst3b = __msa_ldi_b(3);                                            \
    filt2 = __msa_adds_s_b(filt, cnst3b);                               \
    filt2 >>= 3;                                                        \
                                                                        \
    q0_m = __msa_subs_s_b(q0_m, filt1);                                 \
    q0_in_out = __msa_xori_b((v16u8) q0_m, 0x80);                       \
    p0_m = __msa_adds_s_b(p0_m, filt2);                                 \
    p0_in_out = __msa_xori_b((v16u8) p0_m, 0x80);                       \
                                                                        \
    filt = __msa_srari_b(filt1, 1);                                     \
    hev_in = __msa_xori_b((v16u8) hev_in, 0xff);                        \
    filt = filt & (v16i8) hev_in;                                       \
                                                                        \
    q1_m = __msa_subs_s_b(q1_m, filt);                                  \
    q1_in_out = __msa_xori_b((v16u8) q1_m, 0x80);                       \
    p1_m = __msa_adds_s_b(p1_m, filt);                                  \
    p1_in_out = __msa_xori_b((v16u8) p1_m, 0x80);                       \
}

#define VP8_SIMPLE_FILT(p1_in, p0_in, q0_in, q1_in, mask)           \
{                                                                   \
    v16i8 p1_m, p0_m, q0_m, q1_m, q0_sub_p0, q0_sub_p0_sign;        \
    v16i8 filt, filt1, filt2, cnst4b, cnst3b, filt_sign;            \
    v8i16 q0_sub_p0_r, q0_sub_p0_l, filt_l, filt_r, cnst3h;         \
                                                                    \
    p1_m = (v16i8) __msa_xori_b(p1_in, 0x80);                       \
    p0_m = (v16i8) __msa_xori_b(p0_in, 0x80);                       \
    q0_m = (v16i8) __msa_xori_b(q0_in, 0x80);                       \
    q1_m = (v16i8) __msa_xori_b(q1_in, 0x80);                       \
                                                                    \
    filt = __msa_subs_s_b(p1_m, q1_m);                              \
                                                                    \
    q0_sub_p0 = q0_m - p0_m;                                        \
    filt_sign = __msa_clti_s_b(filt, 0);                            \
                                                                    \
    cnst3h = __msa_ldi_h(3);                                        \
    q0_sub_p0_sign = __msa_clti_s_b(q0_sub_p0, 0);                  \
    q0_sub_p0_r = (v8i16) __msa_ilvr_b(q0_sub_p0_sign, q0_sub_p0);  \
    q0_sub_p0_r *= cnst3h;                                          \
    filt_r = (v8i16) __msa_ilvr_b(filt_sign, filt);                 \
    filt_r += q0_sub_p0_r;                                          \
    filt_r = __msa_sat_s_h(filt_r, 7);                              \
                                                                    \
    q0_sub_p0_l = (v8i16) __msa_ilvl_b(q0_sub_p0_sign, q0_sub_p0);  \
    q0_sub_p0_l *= cnst3h;                                          \
    filt_l = (v8i16) __msa_ilvl_b(filt_sign, filt);                 \
    filt_l += q0_sub_p0_l;                                          \
    filt_l = __msa_sat_s_h(filt_l, 7);                              \
                                                                    \
    filt = __msa_pckev_b((v16i8) filt_l, (v16i8) filt_r);           \
    filt = filt & (v16i8) (mask);                                   \
                                                                    \
    cnst4b = __msa_ldi_b(4);                                        \
    filt1 = __msa_adds_s_b(filt, cnst4b);                           \
    filt1 >>= 3;                                                    \
                                                                    \
    cnst3b = __msa_ldi_b(3);                                        \
    filt2 = __msa_adds_s_b(filt, cnst3b);                           \
    filt2 >>= 3;                                                    \
                                                                    \
    q0_m = __msa_subs_s_b(q0_m, filt1);                             \
    p0_m = __msa_adds_s_b(p0_m, filt2);                             \
    q0_in = __msa_xori_b((v16u8) q0_m, 0x80);                       \
    p0_in = __msa_xori_b((v16u8) p0_m, 0x80);                       \
}

#define VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev)             \
{                                                                   \
    v16i8 p2_m, p1_m, p0_m, q2_m, q1_m, q0_m;                       \
    v16i8 filt, q0_sub_p0, cnst4b, cnst3b;                          \
    v16i8 u, filt1, filt2, filt_sign, q0_sub_p0_sign;               \
    v8i16 q0_sub_p0_r, q0_sub_p0_l, filt_r, u_r, u_l, filt_l;       \
    v8i16 cnst3h, cnst27h, cnst18h, cnst63h;                        \
                                                                    \
    cnst3h = __msa_ldi_h(3);                                        \
                                                                    \
    p2_m = (v16i8) __msa_xori_b(p2, 0x80);                          \
    p1_m = (v16i8) __msa_xori_b(p1, 0x80);                          \
    p0_m = (v16i8) __msa_xori_b(p0, 0x80);                          \
    q0_m = (v16i8) __msa_xori_b(q0, 0x80);                          \
    q1_m = (v16i8) __msa_xori_b(q1, 0x80);                          \
    q2_m = (v16i8) __msa_xori_b(q2, 0x80);                          \
                                                                    \
    filt = __msa_subs_s_b(p1_m, q1_m);                              \
    q0_sub_p0 = q0_m - p0_m;                                        \
    q0_sub_p0_sign = __msa_clti_s_b(q0_sub_p0, 0);                  \
    filt_sign = __msa_clti_s_b(filt, 0);                            \
                                                                    \
    /* right part */                                                \
    q0_sub_p0_r = (v8i16) __msa_ilvr_b(q0_sub_p0_sign, q0_sub_p0);  \
    q0_sub_p0_r *= cnst3h;                                          \
    filt_r = (v8i16) __msa_ilvr_b(filt_sign, filt);                 \
    filt_r = filt_r + q0_sub_p0_r;                                  \
    filt_r = __msa_sat_s_h(filt_r, 7);                              \
                                                                    \
    /* left part */                                                 \
    q0_sub_p0_l = (v8i16) __msa_ilvl_b(q0_sub_p0_sign, q0_sub_p0);  \
    q0_sub_p0_l *= cnst3h;                                          \
    filt_l = (v8i16) __msa_ilvl_b(filt_sign, filt);                 \
    filt_l = filt_l + q0_sub_p0_l;                                  \
    filt_l = __msa_sat_s_h(filt_l, 7);                              \
                                                                    \
    /* combine left and right part */                               \
    filt = __msa_pckev_b((v16i8) filt_l, (v16i8) filt_r);           \
    filt = filt & (v16i8) mask;                                     \
    filt2 = filt & (v16i8) hev;                                     \
                                                                    \
    /* filt_val &= ~hev */                                          \
    hev = __msa_xori_b(hev, 0xff);                                  \
    filt = filt & (v16i8) hev;                                      \
    cnst4b = __msa_ldi_b(4);                                        \
    filt1 = __msa_adds_s_b(filt2, cnst4b);                          \
    filt1 >>= 3;                                                    \
    cnst3b = __msa_ldi_b(3);                                        \
    filt2 = __msa_adds_s_b(filt2, cnst3b);                          \
    filt2 >>= 3;                                                    \
    q0_m = __msa_subs_s_b(q0_m, filt1);                             \
    p0_m = __msa_adds_s_b(p0_m, filt2);                             \
                                                                    \
    filt_sign = __msa_clti_s_b(filt, 0);                            \
    ILVRL_B2_SH(filt_sign, filt, filt_r, filt_l);                   \
                                                                    \
    cnst27h = __msa_ldi_h(27);                                      \
    cnst63h = __msa_ldi_h(63);                                      \
                                                                    \
    /* right part */                                                \
    u_r = filt_r * cnst27h;                                         \
    u_r += cnst63h;                                                 \
    u_r >>= 7;                                                      \
    u_r = __msa_sat_s_h(u_r, 7);                                    \
    /* left part */                                                 \
    u_l = filt_l * cnst27h;                                         \
    u_l += cnst63h;                                                 \
    u_l >>= 7;                                                      \
    u_l = __msa_sat_s_h(u_l, 7);                                    \
    /* combine left and right part */                               \
    u = __msa_pckev_b((v16i8) u_l, (v16i8) u_r);                    \
    q0_m = __msa_subs_s_b(q0_m, u);                                 \
    q0 = __msa_xori_b((v16u8) q0_m, 0x80);                          \
    p0_m = __msa_adds_s_b(p0_m, u);                                 \
    p0 = __msa_xori_b((v16u8) p0_m, 0x80);                          \
    cnst18h = __msa_ldi_h(18);                                      \
    u_r = filt_r * cnst18h;                                         \
    u_r += cnst63h;                                                 \
    u_r >>= 7;                                                      \
    u_r = __msa_sat_s_h(u_r, 7);                                    \
                                                                    \
    /* left part */                                                 \
    u_l = filt_l * cnst18h;                                         \
    u_l += cnst63h;                                                 \
    u_l >>= 7;                                                      \
    u_l = __msa_sat_s_h(u_l, 7);                                    \
    /* combine left and right part */                               \
    u = __msa_pckev_b((v16i8) u_l, (v16i8) u_r);                    \
    q1_m = __msa_subs_s_b(q1_m, u);                                 \
    q1 = __msa_xori_b((v16u8) q1_m, 0x80);                          \
    p1_m = __msa_adds_s_b(p1_m, u);                                 \
    p1 = __msa_xori_b((v16u8) p1_m, 0x80);                          \
    u_r = filt_r << 3;                                              \
    u_r += filt_r + cnst63h;                                        \
    u_r >>= 7;                                                      \
    u_r = __msa_sat_s_h(u_r, 7);                                    \
                                                                    \
    /* left part */                                                 \
    u_l = filt_l << 3;                                              \
    u_l += filt_l + cnst63h;                                        \
    u_l >>= 7;                                                      \
    u_l = __msa_sat_s_h(u_l, 7);                                    \
    /* combine left and right part */                               \
    u = __msa_pckev_b((v16i8) u_l, (v16i8) u_r);                    \
    q2_m = __msa_subs_s_b(q2_m, u);                                 \
    q2 = __msa_xori_b((v16u8) q2_m, 0x80);                          \
    p2_m = __msa_adds_s_b(p2_m, u);                                 \
    p2 = __msa_xori_b((v16u8) p2_m, 0x80);                          \
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
    p3_asub_p2_m = __msa_asub_u_b((p3_in), (p2_in));               \
    p2_asub_p1_m = __msa_asub_u_b((p2_in), (p1_in));               \
    p1_asub_p0_m = __msa_asub_u_b((p1_in), (p0_in));               \
    q1_asub_q0_m = __msa_asub_u_b((q1_in), (q0_in));               \
    q2_asub_q1_m = __msa_asub_u_b((q2_in), (q1_in));               \
    q3_asub_q2_m = __msa_asub_u_b((q3_in), (q2_in));               \
    p0_asub_q0_m = __msa_asub_u_b((p0_in), (q0_in));               \
    p1_asub_q1_m = __msa_asub_u_b((p1_in), (q1_in));               \
    /* calculation of hev */                                       \
    flat_out = __msa_max_u_b(p1_asub_p0_m, q1_asub_q0_m);          \
    hev_out = (thresh_in) < (v16u8) flat_out;                      \
    /* calculation of mask */                                      \
    p0_asub_q0_m = __msa_adds_u_b(p0_asub_q0_m, p0_asub_q0_m);     \
    p1_asub_q1_m >>= 1;                                            \
    p0_asub_q0_m = __msa_adds_u_b(p0_asub_q0_m, p1_asub_q1_m);     \
    mask_out = (b_limit_in) < p0_asub_q0_m;                        \
    mask_out = __msa_max_u_b(flat_out, mask_out);                  \
    p3_asub_p2_m = __msa_max_u_b(p3_asub_p2_m, p2_asub_p1_m);      \
    mask_out = __msa_max_u_b(p3_asub_p2_m, mask_out);              \
    q2_asub_q1_m = __msa_max_u_b(q2_asub_q1_m, q3_asub_q2_m);      \
    mask_out = __msa_max_u_b(q2_asub_q1_m, mask_out);              \
    mask_out = (limit_in) < (v16u8) mask_out;                      \
    mask_out = __msa_xori_b(mask_out, 0xff);                       \
}

#define VP8_ST6x1_UB(in0, in0_idx, in1, in1_idx, pdst, stride)  \
{                                                               \
    uint16_t tmp0_h;                                            \
    uint32_t tmp0_w;                                            \
                                                                \
    tmp0_w = __msa_copy_u_w((v4i32) in0, in0_idx);              \
    tmp0_h = __msa_copy_u_h((v8i16) in1, in1_idx);              \
    SW(tmp0_w, pdst);                                           \
    SH(tmp0_h, pdst + stride);                                  \
}

void ff_vp8_v_loop_filter16_msa(uint8_t *src, ptrdiff_t pitch, int b_limit_in,
                                int limit_in, int thresh_in)
{
    uint8_t *temp_src;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 mask, hev, flat, thresh, limit, b_limit;

    b_limit = (v16u8) __msa_fill_b(b_limit_in);
    limit = (v16u8) __msa_fill_b(limit_in);
    thresh = (v16u8) __msa_fill_b(thresh_in);
    /* load vector elements */
    temp_src = src - (pitch << 2);
    LD_UB8(temp_src, pitch, p3, p2, p1, p0, q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);
    /* store vector elements */
    temp_src = src - 3 * pitch;
    ST_UB4(p2, p1, p0, q0, temp_src, pitch);
    temp_src += (4 * pitch);
    ST_UB2(q1, q2, temp_src, pitch);
}

void ff_vp8_v_loop_filter8uv_msa(uint8_t *src_u, uint8_t *src_v,
                                 ptrdiff_t pitch, int b_limit_in, int limit_in,
                                 int thresh_in)
{
    uint8_t *temp_src;
    uint64_t p2_d, p1_d, p0_d, q0_d, q1_d, q2_d;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 mask, hev, flat, thresh, limit, b_limit;
    v16u8 p3_u, p2_u, p1_u, p0_u, q3_u, q2_u, q1_u, q0_u;
    v16u8 p3_v, p2_v, p1_v, p0_v, q3_v, q2_v, q1_v, q0_v;

    b_limit = (v16u8) __msa_fill_b(b_limit_in);
    limit = (v16u8) __msa_fill_b(limit_in);
    thresh = (v16u8) __msa_fill_b(thresh_in);

    temp_src = src_u - (pitch << 2);
    LD_UB8(temp_src, pitch, p3_u, p2_u, p1_u, p0_u, q0_u, q1_u, q2_u, q3_u);
    temp_src = src_v - (pitch << 2);
    LD_UB8(temp_src, pitch, p3_v, p2_v, p1_v, p0_v, q0_v, q1_v, q2_v, q3_v);

    /* rht 8 element of p3 are u pixel and left 8 element of p3 are v pixel */
    ILVR_D4_UB(p3_v, p3_u, p2_v, p2_u, p1_v, p1_u, p0_v, p0_u, p3, p2, p1, p0);
    ILVR_D4_UB(q0_v, q0_u, q1_v, q1_u, q2_v, q2_u, q3_v, q3_u, q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);

    p2_d = __msa_copy_u_d((v2i64) p2, 0);
    p1_d = __msa_copy_u_d((v2i64) p1, 0);
    p0_d = __msa_copy_u_d((v2i64) p0, 0);
    q0_d = __msa_copy_u_d((v2i64) q0, 0);
    q1_d = __msa_copy_u_d((v2i64) q1, 0);
    q2_d = __msa_copy_u_d((v2i64) q2, 0);
    src_u -= (pitch * 3);
    SD4(p2_d, p1_d, p0_d, q0_d, src_u, pitch);
    src_u += 4 * pitch;
    SD(q1_d, src_u);
    src_u += pitch;
    SD(q2_d, src_u);

    p2_d = __msa_copy_u_d((v2i64) p2, 1);
    p1_d = __msa_copy_u_d((v2i64) p1, 1);
    p0_d = __msa_copy_u_d((v2i64) p0, 1);
    q0_d = __msa_copy_u_d((v2i64) q0, 1);
    q1_d = __msa_copy_u_d((v2i64) q1, 1);
    q2_d = __msa_copy_u_d((v2i64) q2, 1);
    src_v -= (pitch * 3);
    SD4(p2_d, p1_d, p0_d, q0_d, src_v, pitch);
    src_v += 4 * pitch;
    SD(q1_d, src_v);
    src_v += pitch;
    SD(q2_d, src_v);
}

void ff_vp8_h_loop_filter16_msa(uint8_t *src, ptrdiff_t pitch, int b_limit_in,
                                int limit_in, int thresh_in)
{
    uint8_t *temp_src;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 mask, hev, flat, thresh, limit, b_limit;
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7, row8;
    v16u8 row9, row10, row11, row12, row13, row14, row15;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;

    b_limit = (v16u8) __msa_fill_b(b_limit_in);
    limit = (v16u8) __msa_fill_b(limit_in);
    thresh = (v16u8) __msa_fill_b(thresh_in);
    temp_src = src - 4;
    LD_UB8(temp_src, pitch, row0, row1, row2, row3, row4, row5, row6, row7);
    temp_src += (8 * pitch);
    LD_UB8(temp_src, pitch,
           row8, row9, row10, row11, row12, row13, row14, row15);
    TRANSPOSE16x8_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);
    ILVR_B2_SH(p1, p2, q0, p0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp3, tmp4);
    ILVL_B2_SH(p1, p2, q0, p0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp6, tmp7);
    ILVRL_B2_SH(q2, q1, tmp2, tmp5);

    temp_src = src - 3;
    VP8_ST6x1_UB(tmp3, 0, tmp2, 0, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp3, 1, tmp2, 1, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp3, 2, tmp2, 2, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp3, 3, tmp2, 3, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp4, 0, tmp2, 4, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp4, 1, tmp2, 5, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp4, 2, tmp2, 6, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp4, 3, tmp2, 7, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp6, 0, tmp5, 0, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp6, 1, tmp5, 1, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp6, 2, tmp5, 2, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp6, 3, tmp5, 3, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp7, 0, tmp5, 4, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp7, 1, tmp5, 5, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp7, 2, tmp5, 6, temp_src, 4);
    temp_src += pitch;
    VP8_ST6x1_UB(tmp7, 3, tmp5, 7, temp_src, 4);
}

void ff_vp8_h_loop_filter8uv_msa(uint8_t *src_u, uint8_t *src_v,
                                 ptrdiff_t pitch, int b_limit_in, int limit_in,
                                 int thresh_in)
{
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 mask, hev, flat, thresh, limit, b_limit;
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7, row8;
    v16u8 row9, row10, row11, row12, row13, row14, row15;
    v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;

    b_limit = (v16u8) __msa_fill_b(b_limit_in);
    limit = (v16u8) __msa_fill_b(limit_in);
    thresh = (v16u8) __msa_fill_b(thresh_in);

    LD_UB8(src_u - 4, pitch, row0, row1, row2, row3, row4, row5, row6, row7);
    LD_UB8(src_v - 4, pitch,
           row8, row9, row10, row11, row12, row13, row14, row15);
    TRANSPOSE16x8_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);

    ILVR_B2_SH(p1, p2, q0, p0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp3, tmp4);
    ILVL_B2_SH(p1, p2, q0, p0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp6, tmp7);
    ILVRL_B2_SH(q2, q1, tmp2, tmp5);

    src_u -= 3;
    VP8_ST6x1_UB(tmp3, 0, tmp2, 0, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp3, 1, tmp2, 1, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp3, 2, tmp2, 2, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp3, 3, tmp2, 3, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp4, 0, tmp2, 4, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp4, 1, tmp2, 5, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp4, 2, tmp2, 6, src_u, 4);
    src_u += pitch;
    VP8_ST6x1_UB(tmp4, 3, tmp2, 7, src_u, 4);

    src_v -= 3;
    VP8_ST6x1_UB(tmp6, 0, tmp5, 0, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp6, 1, tmp5, 1, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp6, 2, tmp5, 2, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp6, 3, tmp5, 3, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp7, 0, tmp5, 4, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp7, 1, tmp5, 5, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp7, 2, tmp5, 6, src_v, 4);
    src_v += pitch;
    VP8_ST6x1_UB(tmp7, 3, tmp5, 7, src_v, 4);
}

void ff_vp8_v_loop_filter_simple_msa(uint8_t *src, ptrdiff_t pitch,
                                     int b_limit_ptr)
{
    v16u8 p1, p0, q1, q0;
    v16u8 mask, b_limit;

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    /* load vector elements */
    LD_UB4(src - (pitch << 1), pitch, p1, p0, q0, q1);
    VP8_SIMPLE_MASK(p1, p0, q0, q1, b_limit, mask);
    VP8_SIMPLE_FILT(p1, p0, q0, q1, mask);
    ST_UB2(p0, q0, (src - pitch), pitch);
}

void ff_vp8_h_loop_filter_simple_msa(uint8_t *src, ptrdiff_t pitch,
                                     int b_limit_ptr)
{
    uint8_t *temp_src;
    v16u8 p1, p0, q1, q0;
    v16u8 mask, b_limit;
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7, row8;
    v16u8 row9, row10, row11, row12, row13, row14, row15;
    v8i16 tmp0, tmp1;

    b_limit = (v16u8) __msa_fill_b(b_limit_ptr);
    temp_src = src - 2;
    LD_UB8(temp_src, pitch, row0, row1, row2, row3, row4, row5, row6, row7);
    temp_src += (8 * pitch);
    LD_UB8(temp_src, pitch,
           row8, row9, row10, row11, row12, row13, row14, row15);
    TRANSPOSE16x4_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p1, p0, q0, q1);
    VP8_SIMPLE_MASK(p1, p0, q0, q1, b_limit, mask);
    VP8_SIMPLE_FILT(p1, p0, q0, q1, mask);
    ILVRL_B2_SH(q0, p0, tmp1, tmp0);

    src -= 1;
    ST2x4_UB(tmp1, 0, src, pitch);
    src += 4 * pitch;
    ST2x4_UB(tmp1, 4, src, pitch);
    src += 4 * pitch;
    ST2x4_UB(tmp0, 0, src, pitch);
    src += 4 * pitch;
    ST2x4_UB(tmp0, 4, src, pitch);
    src += 4 * pitch;
}

void ff_vp8_v_loop_filter8uv_inner_msa(uint8_t *src_u, uint8_t *src_v,
                                       ptrdiff_t pitch, int b_limit_in,
                                       int limit_in, int thresh_in)
{
    uint64_t p1_d, p0_d, q0_d, q1_d;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 mask, hev, flat, thresh, limit, b_limit;
    v16u8 p3_u, p2_u, p1_u, p0_u, q3_u, q2_u, q1_u, q0_u;
    v16u8 p3_v, p2_v, p1_v, p0_v, q3_v, q2_v, q1_v, q0_v;

    thresh = (v16u8) __msa_fill_b(thresh_in);
    limit = (v16u8) __msa_fill_b(limit_in);
    b_limit = (v16u8) __msa_fill_b(b_limit_in);

    src_u = src_u - (pitch << 2);
    LD_UB8(src_u, pitch, p3_u, p2_u, p1_u, p0_u, q0_u, q1_u, q2_u, q3_u);
    src_u += (5 * pitch);
    src_v = src_v - (pitch << 2);
    LD_UB8(src_v, pitch, p3_v, p2_v, p1_v, p0_v, q0_v, q1_v, q2_v, q3_v);
    src_v += (5 * pitch);

    /* right 8 element of p3 are u pixel and
       left 8 element of p3 are v pixel */
    ILVR_D4_UB(p3_v, p3_u, p2_v, p2_u, p1_v, p1_u, p0_v, p0_u, p3, p2, p1, p0);
    ILVR_D4_UB(q0_v, q0_u, q1_v, q1_u, q2_v, q2_u, q3_v, q3_u, q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev);

    p1_d = __msa_copy_u_d((v2i64) p1, 0);
    p0_d = __msa_copy_u_d((v2i64) p0, 0);
    q0_d = __msa_copy_u_d((v2i64) q0, 0);
    q1_d = __msa_copy_u_d((v2i64) q1, 0);
    SD4(q1_d, q0_d, p0_d, p1_d, src_u, (- pitch));

    p1_d = __msa_copy_u_d((v2i64) p1, 1);
    p0_d = __msa_copy_u_d((v2i64) p0, 1);
    q0_d = __msa_copy_u_d((v2i64) q0, 1);
    q1_d = __msa_copy_u_d((v2i64) q1, 1);
    SD4(q1_d, q0_d, p0_d, p1_d, src_v, (- pitch));
}

void ff_vp8_h_loop_filter8uv_inner_msa(uint8_t *src_u, uint8_t *src_v,
                                       ptrdiff_t pitch, int b_limit_in,
                                       int limit_in, int thresh_in)
{
    uint8_t *temp_src_u, *temp_src_v;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;
    v16u8 mask, hev, flat, thresh, limit, b_limit;
    v16u8 row0, row1, row2, row3, row4, row5, row6, row7, row8;
    v16u8 row9, row10, row11, row12, row13, row14, row15;
    v4i32 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

    thresh = (v16u8) __msa_fill_b(thresh_in);
    limit = (v16u8) __msa_fill_b(limit_in);
    b_limit = (v16u8) __msa_fill_b(b_limit_in);

    LD_UB8(src_u - 4, pitch, row0, row1, row2, row3, row4, row5, row6, row7);
    LD_UB8(src_v - 4, pitch,
           row8, row9, row10, row11, row12, row13, row14, row15);
    TRANSPOSE16x8_UB_UB(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev);
    ILVR_B2_SW(p0, p1, q1, q0, tmp0, tmp1);
    ILVRL_H2_SW(tmp1, tmp0, tmp2, tmp3);
    tmp0 = (v4i32) __msa_ilvl_b((v16i8) p0, (v16i8) p1);
    tmp1 = (v4i32) __msa_ilvl_b((v16i8) q1, (v16i8) q0);
    ILVRL_H2_SW(tmp1, tmp0, tmp4, tmp5);

    temp_src_u = src_u - 2;
    ST4x4_UB(tmp2, tmp2, 0, 1, 2, 3, temp_src_u, pitch);
    temp_src_u += 4 * pitch;
    ST4x4_UB(tmp3, tmp3, 0, 1, 2, 3, temp_src_u, pitch);

    temp_src_v = src_v - 2;
    ST4x4_UB(tmp4, tmp4, 0, 1, 2, 3, temp_src_v, pitch);
    temp_src_v += 4 * pitch;
    ST4x4_UB(tmp5, tmp5, 0, 1, 2, 3, temp_src_v, pitch);
}

void ff_vp8_v_loop_filter16_inner_msa(uint8_t *src, ptrdiff_t pitch,
                                      int32_t e, int32_t i, int32_t h)
{
    v16u8 mask, hev, flat;
    v16u8 thresh, b_limit, limit;
    v16u8 p3, p2, p1, p0, q3, q2, q1, q0;

    /* load vector elements */
    LD_UB8((src - 4 * pitch), pitch, p3, p2, p1, p0, q0, q1, q2, q3);
    thresh = (v16u8) __msa_fill_b(h);
    b_limit = (v16u8) __msa_fill_b(e);
    limit = (v16u8) __msa_fill_b(i);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev);

    ST_UB4(p1, p0, q0, q1, (src - 2 * pitch), pitch);
}

void ff_vp8_h_loop_filter16_inner_msa(uint8_t *src, ptrdiff_t pitch,
                                      int32_t e, int32_t i, int32_t h)
{
    v16u8 mask, hev, flat;
    v16u8 thresh, b_limit, limit;
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

    thresh = (v16u8) __msa_fill_b(h);
    b_limit = (v16u8) __msa_fill_b(e);
    limit = (v16u8) __msa_fill_b(i);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev);
    ILVR_B2_SH(p0, p1, q1, q0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp2, tmp3);
    ILVL_B2_SH(p0, p1, q1, q0, tmp0, tmp1);
    ILVRL_H2_SH(tmp1, tmp0, tmp4, tmp5);

    src -= 2;
    ST4x8_UB(tmp2, tmp3, src, pitch);
    src += (8 * pitch);
    ST4x8_UB(tmp4, tmp5, src, pitch);
}
