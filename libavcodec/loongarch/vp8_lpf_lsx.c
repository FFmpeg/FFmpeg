/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hecai Yuan <yuanhecai@loongson.cn>
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
#include "vp8dsp_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

#define VP8_LPF_FILTER4_4W(p1_in_out, p0_in_out, q0_in_out, q1_in_out,  \
                           mask_in, hev_in)                             \
{                                                                       \
    __m128i p1_m, p0_m, q0_m, q1_m, q0_sub_p0, filt_sign;               \
    __m128i filt, filt1, filt2, cnst4b, cnst3b;                         \
    __m128i q0_sub_p0_l, q0_sub_p0_h, filt_h, filt_l, cnst3h;           \
                                                                        \
    p1_m = __lsx_vxori_b(p1_in_out, 0x80);                              \
    p0_m = __lsx_vxori_b(p0_in_out, 0x80);                              \
    q0_m = __lsx_vxori_b(q0_in_out, 0x80);                              \
    q1_m = __lsx_vxori_b(q1_in_out, 0x80);                              \
    filt = __lsx_vssub_b(p1_m, q1_m);                                   \
    filt = filt & hev_in;                                               \
                                                                        \
    q0_sub_p0 = __lsx_vsub_b(q0_m, p0_m);                               \
    filt_sign = __lsx_vslti_b(filt, 0);                                 \
                                                                        \
    cnst3h = __lsx_vreplgr2vr_h(3);                                     \
    q0_sub_p0_l = __lsx_vilvl_b(q0_sub_p0, q0_sub_p0);                  \
    q0_sub_p0_l = __lsx_vdp2_h_b(q0_sub_p0_l, cnst3h);                  \
    filt_l = __lsx_vilvl_b(filt_sign, filt);                            \
    filt_l = __lsx_vadd_h(filt_l, q0_sub_p0_l);                         \
    filt_l = __lsx_vsat_h(filt_l, 7);                                   \
                                                                        \
    q0_sub_p0_h = __lsx_vilvh_b(q0_sub_p0, q0_sub_p0);                  \
    q0_sub_p0_h = __lsx_vdp2_h_b(q0_sub_p0_h, cnst3h);                  \
    filt_h = __lsx_vilvh_b(filt_sign, filt);                            \
    filt_h = __lsx_vadd_h(filt_h, q0_sub_p0_h);                         \
    filt_h = __lsx_vsat_h(filt_h, 7);                                   \
                                                                        \
    filt = __lsx_vpickev_b(filt_h, filt_l);                             \
    filt = filt & mask_in;                                              \
    cnst4b = __lsx_vreplgr2vr_b(4);                                     \
    filt1 = __lsx_vsadd_b(filt, cnst4b);                                \
    filt1 = __lsx_vsrai_b(filt1, 3);                                    \
                                                                        \
    cnst3b = __lsx_vreplgr2vr_b(3);                                     \
    filt2 = __lsx_vsadd_b(filt, cnst3b);                                \
    filt2 = __lsx_vsrai_b(filt2, 3);                                    \
                                                                        \
    q0_m = __lsx_vssub_b(q0_m, filt1);                                  \
    q0_in_out = __lsx_vxori_b(q0_m, 0x80);                              \
    p0_m = __lsx_vsadd_b(p0_m, filt2);                                  \
    p0_in_out = __lsx_vxori_b(p0_m, 0x80);                              \
                                                                        \
    filt = __lsx_vsrari_b(filt1, 1);                                    \
    hev_in = __lsx_vxori_b(hev_in, 0xff);                               \
    filt = filt & hev_in;                                               \
                                                                        \
    q1_m = __lsx_vssub_b(q1_m, filt);                                   \
    q1_in_out = __lsx_vxori_b(q1_m, 0x80);                              \
    p1_m = __lsx_vsadd_b(p1_m, filt);                                   \
    p1_in_out = __lsx_vxori_b(p1_m, 0x80);                              \
}

#define VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev)             \
{                                                                   \
    __m128i p2_m, p1_m, p0_m, q2_m, q1_m, q0_m;                     \
    __m128i filt, q0_sub_p0, cnst4b, cnst3b;                        \
    __m128i u, filt1, filt2, filt_sign, q0_sub_p0_sign;             \
    __m128i q0_sub_p0_l, q0_sub_p0_h, filt_l, u_l, u_h, filt_h;     \
    __m128i cnst3h, cnst27h, cnst18h, cnst63h;                      \
                                                                    \
    cnst3h = __lsx_vreplgr2vr_h(3);                                 \
                                                                    \
    p2_m = __lsx_vxori_b(p2, 0x80);                                 \
    p1_m = __lsx_vxori_b(p1, 0x80);                                 \
    p0_m = __lsx_vxori_b(p0, 0x80);                                 \
    q0_m = __lsx_vxori_b(q0, 0x80);                                 \
    q1_m = __lsx_vxori_b(q1, 0x80);                                 \
    q2_m = __lsx_vxori_b(q2, 0x80);                                 \
                                                                    \
    filt = __lsx_vssub_b(p1_m, q1_m);                               \
    q0_sub_p0 = __lsx_vsub_b(q0_m, p0_m);                           \
    q0_sub_p0_sign = __lsx_vslti_b(q0_sub_p0, 0);                   \
    filt_sign = __lsx_vslti_b(filt, 0);                             \
                                                                    \
    /* right part */                                                \
    q0_sub_p0_l = __lsx_vilvl_b(q0_sub_p0_sign, q0_sub_p0);         \
    q0_sub_p0_l = __lsx_vmul_h(q0_sub_p0_l, cnst3h);                \
    filt_l = __lsx_vilvl_b(filt_sign, filt);                        \
    filt_l = __lsx_vadd_h(filt_l, q0_sub_p0_l);                     \
    filt_l = __lsx_vsat_h(filt_l, 7);                               \
                                                                    \
    /* left part */                                                 \
    q0_sub_p0_h = __lsx_vilvh_b(q0_sub_p0_sign, q0_sub_p0);         \
    q0_sub_p0_h = __lsx_vmul_h(q0_sub_p0_h, cnst3h);                \
    filt_h = __lsx_vilvh_b(filt_sign, filt);                        \
    filt_h = __lsx_vadd_h(filt_h,  q0_sub_p0_h);                    \
    filt_h = __lsx_vsat_h(filt_h, 7);                               \
                                                                    \
    /* combine left and right part */                               \
    filt = __lsx_vpickev_b(filt_h, filt_l);                         \
    filt = filt & mask;                                             \
    filt2 = filt & hev;                                             \
    /* filt_val &= ~hev */                                          \
    hev = __lsx_vxori_b(hev, 0xff);                                 \
    filt = filt & hev;                                              \
    cnst4b = __lsx_vreplgr2vr_b(4);                                 \
    filt1 = __lsx_vsadd_b(filt2, cnst4b);                           \
    filt1 = __lsx_vsrai_b(filt1, 3);                                \
    cnst3b = __lsx_vreplgr2vr_b(3);                                 \
    filt2 = __lsx_vsadd_b(filt2, cnst3b);                           \
    filt2 = __lsx_vsrai_b(filt2, 3);                                \
    q0_m = __lsx_vssub_b(q0_m, filt1);                              \
    p0_m = __lsx_vsadd_b(p0_m, filt2);                              \
                                                                    \
    filt_sign = __lsx_vslti_b(filt, 0);                             \
    filt_l = __lsx_vilvl_b(filt_sign, filt);                        \
    filt_h = __lsx_vilvh_b(filt_sign, filt);                        \
                                                                    \
    cnst27h = __lsx_vreplgr2vr_h(27);                               \
    cnst63h = __lsx_vreplgr2vr_h(63);                               \
                                                                    \
    /* right part */                                                \
    u_l = __lsx_vmul_h(filt_l, cnst27h);                            \
    u_l = __lsx_vadd_h(u_l, cnst63h);                               \
    u_l = __lsx_vsrai_h(u_l, 7);                                    \
    u_l = __lsx_vsat_h(u_l, 7);                                     \
    /* left part */                                                 \
    u_h = __lsx_vmul_h(filt_h, cnst27h);                            \
    u_h = __lsx_vadd_h(u_h, cnst63h);                               \
    u_h = __lsx_vsrai_h(u_h, 7);                                    \
    u_h = __lsx_vsat_h(u_h, 7);                                     \
    /* combine left and right part */                               \
    u = __lsx_vpickev_b(u_h, u_l);                                  \
    q0_m = __lsx_vssub_b(q0_m, u);                                  \
    q0 = __lsx_vxori_b(q0_m, 0x80);                                 \
    p0_m = __lsx_vsadd_b(p0_m, u);                                  \
    p0 = __lsx_vxori_b(p0_m, 0x80);                                 \
    cnst18h = __lsx_vreplgr2vr_h(18);                               \
    u_l = __lsx_vmul_h(filt_l, cnst18h);                            \
    u_l = __lsx_vadd_h(u_l, cnst63h);                               \
    u_l = __lsx_vsrai_h(u_l, 7);                                    \
    u_l = __lsx_vsat_h(u_l, 7);                                     \
                                                                    \
    /* left part */                                                 \
    u_h = __lsx_vmul_h(filt_h, cnst18h);                            \
    u_h = __lsx_vadd_h(u_h, cnst63h);                               \
    u_h = __lsx_vsrai_h(u_h, 7);                                    \
    u_h = __lsx_vsat_h(u_h, 7);                                     \
    /* combine left and right part */                               \
    u = __lsx_vpickev_b(u_h, u_l);                                  \
    q1_m = __lsx_vssub_b(q1_m, u);                                  \
    q1 = __lsx_vxori_b(q1_m, 0x80);                                 \
    p1_m = __lsx_vsadd_b(p1_m, u);                                  \
    p1 = __lsx_vxori_b(p1_m, 0x80);                                 \
    u_l = __lsx_vslli_h(filt_l, 3);                                 \
    u_l = __lsx_vadd_h(u_l, filt_l);                                \
    u_l = __lsx_vadd_h(u_l, cnst63h);                               \
    u_l = __lsx_vsrai_h(u_l, 7);                                    \
    u_l = __lsx_vsat_h(u_l, 7);                                     \
                                                                    \
    /* left part */                                                 \
    u_h = __lsx_vslli_h(filt_h, 3);                                 \
    u_h = __lsx_vadd_h(u_h, filt_h);                                \
    u_h = __lsx_vadd_h(u_h, cnst63h);                               \
    u_h = __lsx_vsrai_h(u_h, 7);                                    \
    u_h = __lsx_vsat_h(u_h, 7);                                     \
    /* combine left and right part */                               \
    u = __lsx_vpickev_b(u_h, u_l);                                  \
    q2_m = __lsx_vssub_b(q2_m, u);                                  \
    q2 = __lsx_vxori_b(q2_m, 0x80);                                 \
    p2_m = __lsx_vsadd_b(p2_m, u);                                  \
    p2 = __lsx_vxori_b(p2_m, 0x80);                                 \
}

#define LPF_MASK_HEV(p3_src, p2_src, p1_src, p0_src,                \
                     q0_src, q1_src, q2_src, q3_src,                \
                     limit_src, b_limit_src, thresh_src,            \
                     hev_dst, mask_dst, flat_dst)                   \
{                                                                   \
    __m128i p3_asub_p2_m, p2_asub_p1_m, p1_asub_p0_m, q1_asub_q0_m; \
    __m128i p1_asub_q1_m, p0_asub_q0_m, q3_asub_q2_m, q2_asub_q1_m; \
                                                                    \
    /* absolute subtraction of pixel values */                      \
    p3_asub_p2_m = __lsx_vabsd_bu(p3_src, p2_src);                  \
    p2_asub_p1_m = __lsx_vabsd_bu(p2_src, p1_src);                  \
    p1_asub_p0_m = __lsx_vabsd_bu(p1_src, p0_src);                  \
    q1_asub_q0_m = __lsx_vabsd_bu(q1_src, q0_src);                  \
    q2_asub_q1_m = __lsx_vabsd_bu(q2_src, q1_src);                  \
    q3_asub_q2_m = __lsx_vabsd_bu(q3_src, q2_src);                  \
    p0_asub_q0_m = __lsx_vabsd_bu(p0_src, q0_src);                  \
    p1_asub_q1_m = __lsx_vabsd_bu(p1_src, q1_src);                  \
                                                                    \
    /* calculation of hev */                                        \
    flat_dst = __lsx_vmax_bu(p1_asub_p0_m, q1_asub_q0_m);           \
    hev_dst = __lsx_vslt_bu(thresh_src, flat_dst);                  \
    /* calculation of mask */                                       \
    p0_asub_q0_m = __lsx_vsadd_bu(p0_asub_q0_m, p0_asub_q0_m);      \
    p1_asub_q1_m = __lsx_vsrli_b(p1_asub_q1_m, 1);                  \
    p0_asub_q0_m = __lsx_vsadd_bu(p0_asub_q0_m, p1_asub_q1_m);      \
    mask_dst = __lsx_vslt_bu(b_limit_src, p0_asub_q0_m);            \
    mask_dst = __lsx_vmax_bu(flat_dst, mask_dst);                   \
    p3_asub_p2_m = __lsx_vmax_bu(p3_asub_p2_m, p2_asub_p1_m);       \
    mask_dst = __lsx_vmax_bu(p3_asub_p2_m, mask_dst);               \
    q2_asub_q1_m = __lsx_vmax_bu(q2_asub_q1_m, q3_asub_q2_m);       \
    mask_dst = __lsx_vmax_bu(q2_asub_q1_m, mask_dst);               \
    mask_dst = __lsx_vslt_bu(limit_src, mask_dst);                  \
    mask_dst = __lsx_vxori_b(mask_dst, 0xff);                       \
}

#define VP8_ST6x1_UB(in0, in0_idx, in1, in1_idx, pdst, stride)      \
{                                                                   \
    __lsx_vstelm_w(in0, pdst, 0, in0_idx);                          \
    __lsx_vstelm_h(in1, pdst + stride, 0, in1_idx);                 \
}

#define ST_W4(in, idx0, idx1, idx2, idx3, pdst, stride)     \
{                                                           \
    __lsx_vstelm_w(in, pdst, 0, idx0);                      \
    pdst += stride;                                         \
    __lsx_vstelm_w(in, pdst, 0, idx1);                      \
    pdst += stride;                                         \
    __lsx_vstelm_w(in, pdst, 0, idx2);                      \
    pdst += stride;                                         \
    __lsx_vstelm_w(in, pdst, 0, idx3);                      \
    pdst += stride;                                         \
}

void ff_vp8_v_loop_filter16_lsx(uint8_t *dst, ptrdiff_t stride, int b_limit_in,
                                int limit_in, int thresh_in)
{
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i mask, hev, flat, thresh, limit, b_limit;

    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;

    b_limit = __lsx_vreplgr2vr_b(b_limit_in);
    limit = __lsx_vreplgr2vr_b(limit_in);
    thresh = __lsx_vreplgr2vr_b(thresh_in);

    /*load vector elements*/
    DUP4_ARG2(__lsx_vld, dst - stride4, 0, dst - stride3, 0, dst - stride2, 0,
              dst - stride, 0, p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vld, dst, 0, dst + stride, 0, dst + stride2, 0, dst + stride3, 0,
              q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh, hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);

    /*store vector elements*/
    __lsx_vst(p2, dst - stride3, 0);
    __lsx_vst(p1, dst - stride2, 0);
    __lsx_vst(p0, dst - stride,  0);
    __lsx_vst(q0, dst,           0);

    __lsx_vst(q1, dst + stride,  0);
    __lsx_vst(q2, dst + stride2, 0);
}

void ff_vp8_v_loop_filter8uv_lsx(uint8_t *dst_u, uint8_t *dst_v,
                                 ptrdiff_t stride, int b_limit_in,
                                 int limit_in, int thresh_in)
{
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i mask, hev, flat, thresh, limit, b_limit;
    __m128i p3_u, p2_u, p1_u, p0_u, q3_u, q2_u, q1_u, q0_u;
    __m128i p3_v, p2_v, p1_v, p0_v, q3_v, q2_v, q1_v, q0_v;

    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;

    b_limit = __lsx_vreplgr2vr_b(b_limit_in);
    limit = __lsx_vreplgr2vr_b(limit_in);
    thresh = __lsx_vreplgr2vr_b(thresh_in);

    DUP4_ARG2(__lsx_vld, dst_u - stride4, 0, dst_u - stride3, 0, dst_u - stride2, 0,
              dst_u - stride, 0, p3_u, p2_u, p1_u, p0_u);
    DUP4_ARG2(__lsx_vld, dst_u, 0, dst_u + stride, 0, dst_u + stride2, 0,
              dst_u + stride3, 0, q0_u, q1_u, q2_u, q3_u);

    DUP4_ARG2(__lsx_vld, dst_v - stride4, 0, dst_v - stride3, 0, dst_v - stride2, 0,
              dst_v - stride, 0, p3_v, p2_v, p1_v, p0_v);
    DUP4_ARG2(__lsx_vld, dst_v, 0, dst_v + stride, 0, dst_v + stride2, 0,
              dst_v + stride3, 0, q0_v, q1_v, q2_v, q3_v);

    /* rht 8 element of p3 are u pixel and left 8 element of p3 are v pixei */
    DUP4_ARG2(__lsx_vilvl_d, p3_v, p3_u, p2_v, p2_u, p1_v, p1_u, p0_v, p0_u, p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vilvl_d, q0_v, q0_u, q1_v, q1_u, q2_v, q2_u, q3_v, q3_u, q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);

    __lsx_vstelm_d(p2, dst_u - stride3, 0, 0);
    __lsx_vstelm_d(p1, dst_u - stride2, 0, 0);
    __lsx_vstelm_d(p0, dst_u - stride , 0, 0);
    __lsx_vstelm_d(q0, dst_u,           0, 0);

    __lsx_vstelm_d(q1, dst_u + stride,  0, 0);
    __lsx_vstelm_d(q2, dst_u + stride2, 0, 0);

    __lsx_vstelm_d(p2, dst_v - stride3, 0, 1);
    __lsx_vstelm_d(p1, dst_v - stride2, 0, 1);
    __lsx_vstelm_d(p0, dst_v - stride , 0, 1);
    __lsx_vstelm_d(q0, dst_v,           0, 1);

    __lsx_vstelm_d(q1, dst_v + stride,  0, 1);
    __lsx_vstelm_d(q2, dst_v + stride2, 0, 1);
}

void ff_vp8_h_loop_filter16_lsx(uint8_t *dst, ptrdiff_t stride, int b_limit_in,
                                int limit_in, int thresh_in)
{
    uint8_t *temp_src;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i mask, hev, flat, thresh, limit, b_limit;
    __m128i row0, row1, row2, row3, row4, row5, row6, row7, row8;
    __m128i row9, row10, row11, row12, row13, row14, row15;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;

    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;

    b_limit = __lsx_vreplgr2vr_b(b_limit_in);
    limit = __lsx_vreplgr2vr_b(limit_in);
    thresh = __lsx_vreplgr2vr_b(thresh_in);

    temp_src = dst - 4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row0, row1, row2, row3);
    temp_src += stride4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row4, row5, row6, row7);

    temp_src += stride4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row8, row9, row10, row11);
    temp_src += stride4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row12, row13, row14, row15);
    LSX_TRANSPOSE16x8_B(row0, row1, row2, row3, row4, row5, row6, row7, row8, row9, row10,
                        row11, row12, row13, row14, row15, p3, p2, p1, p0, q0, q1, q2, q3);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh, hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);

    tmp0 = __lsx_vilvl_b(p1, p2);
    tmp1 = __lsx_vilvl_b(q0, p0);

    tmp3 = __lsx_vilvl_h(tmp1, tmp0);
    tmp4 = __lsx_vilvh_h(tmp1, tmp0);

    tmp0 = __lsx_vilvh_b(p1, p2);
    tmp1 = __lsx_vilvh_b(q0, p0);

    tmp6 = __lsx_vilvl_h(tmp1, tmp0);
    tmp7 = __lsx_vilvh_h(tmp1, tmp0);

    tmp2 = __lsx_vilvl_b(q2, q1);
    tmp5 = __lsx_vilvh_b(q2, q1);

    temp_src = dst - 3;
    VP8_ST6x1_UB(tmp3, 0, tmp2, 0, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp3, 1, tmp2, 1, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp3, 2, tmp2, 2, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp3, 3, tmp2, 3, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp4, 0, tmp2, 4, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp4, 1, tmp2, 5, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp4, 2, tmp2, 6, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp4, 3, tmp2, 7, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp6, 0, tmp5, 0, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp6, 1, tmp5, 1, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp6, 2, tmp5, 2, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp6, 3, tmp5, 3, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp7, 0, tmp5, 4, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp7, 1, tmp5, 5, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp7, 2, tmp5, 6, temp_src, 4);
    temp_src += stride;
    VP8_ST6x1_UB(tmp7, 3, tmp5, 7, temp_src, 4);
}

void ff_vp8_h_loop_filter8uv_lsx(uint8_t *dst_u, uint8_t *dst_v,
                                 ptrdiff_t stride, int b_limit_in,
                                 int limit_in, int thresh_in)
{
    uint8_t *temp_src;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i mask, hev, flat, thresh, limit, b_limit;
    __m128i row0, row1, row2, row3, row4, row5, row6, row7, row8;
    __m128i row9, row10, row11, row12, row13, row14, row15;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;

    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;

    b_limit = __lsx_vreplgr2vr_b(b_limit_in);
    limit = __lsx_vreplgr2vr_b(limit_in);
    thresh = __lsx_vreplgr2vr_b(thresh_in);

    temp_src = dst_u - 4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row0, row1, row2, row3);
    temp_src += stride4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row4, row5, row6, row7);

    temp_src = dst_v - 4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row8, row9, row10, row11);
    temp_src += stride4;
    DUP4_ARG2(__lsx_vld, temp_src, 0, temp_src + stride, 0, temp_src + stride2, 0,
              temp_src + stride3, 0, row12, row13, row14, row15);

    LSX_TRANSPOSE16x8_B(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh, hev, mask, flat);
    VP8_MBFILTER(p2, p1, p0, q0, q1, q2, mask, hev);

    tmp0 = __lsx_vilvl_b(p1, p2);
    tmp1 = __lsx_vilvl_b(q0, p0);

    tmp3 = __lsx_vilvl_h(tmp1, tmp0);
    tmp4 = __lsx_vilvh_h(tmp1, tmp0);

    tmp0 = __lsx_vilvh_b(p1, p2);
    tmp1 = __lsx_vilvh_b(q0, p0);

    tmp6 = __lsx_vilvl_h(tmp1, tmp0);
    tmp7 = __lsx_vilvh_h(tmp1, tmp0);

    tmp2 = __lsx_vilvl_b(q2, q1);
    tmp5 = __lsx_vilvh_b(q2, q1);

    dst_u -= 3;
    VP8_ST6x1_UB(tmp3, 0, tmp2, 0, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp3, 1, tmp2, 1, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp3, 2, tmp2, 2, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp3, 3, tmp2, 3, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp4, 0, tmp2, 4, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp4, 1, tmp2, 5, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp4, 2, tmp2, 6, dst_u, 4);
    dst_u += stride;
    VP8_ST6x1_UB(tmp4, 3, tmp2, 7, dst_u, 4);

    dst_v -= 3;
    VP8_ST6x1_UB(tmp6, 0, tmp5, 0, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp6, 1, tmp5, 1, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp6, 2, tmp5, 2, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp6, 3, tmp5, 3, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp7, 0, tmp5, 4, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp7, 1, tmp5, 5, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp7, 2, tmp5, 6, dst_v, 4);
    dst_v += stride;
    VP8_ST6x1_UB(tmp7, 3, tmp5, 7, dst_v, 4);
}

void ff_vp8_v_loop_filter16_inner_lsx(uint8_t *src, ptrdiff_t stride,
                                      int32_t e, int32_t i, int32_t h)
{
    __m128i mask, hev, flat;
    __m128i thresh, b_limit, limit;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;

    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;

    /* load vector elements */
    src -= stride4;
    DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride2, 0,
              src + stride3, 0, p3, p2, p1, p0);
    src += stride4;
    DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride2, 0,
              src + stride3, 0, q0, q1, q2, q3);
    thresh = __lsx_vreplgr2vr_b(h);
    b_limit = __lsx_vreplgr2vr_b(e);
    limit = __lsx_vreplgr2vr_b(i);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev);

    __lsx_vst(p1, src - stride2, 0);
    __lsx_vst(p0, src - stride,  0);
    __lsx_vst(q0, src,           0);
    __lsx_vst(q1, src + stride,  0);
}

void ff_vp8_h_loop_filter16_inner_lsx(uint8_t *src, ptrdiff_t stride,
                                      int32_t e, int32_t i, int32_t h)
{
    __m128i mask, hev, flat;
    __m128i thresh, b_limit, limit;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m128i tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;

    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;

    src -= 4;
    DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride2, 0,
              src + stride3, 0, tmp0, tmp1, tmp2, tmp3);
    src += stride4;
    DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride2, 0,
              src + stride3, 0, tmp4, tmp5, tmp6, tmp7);
    src += stride4;
    DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride2, 0,
              src + stride3, 0, tmp8, tmp9, tmp10, tmp11);
    src += stride4;
    DUP4_ARG2(__lsx_vld, src, 0, src + stride, 0, src + stride2, 0,
              src + stride3, 0, tmp12, tmp13, tmp14, tmp15);
    src -= 3 * stride4;

    LSX_TRANSPOSE16x8_B(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7,
                        tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = __lsx_vreplgr2vr_b(h);
    b_limit = __lsx_vreplgr2vr_b(e);
    limit = __lsx_vreplgr2vr_b(i);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP8_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev);

    DUP2_ARG2(__lsx_vilvl_b, p0, p1, q1, q0, tmp0, tmp1);
    tmp2 = __lsx_vilvl_h(tmp1, tmp0);
    tmp3 = __lsx_vilvh_h(tmp1, tmp0);

    src += 2;
    ST_W4(tmp2, 0, 1, 2, 3, src, stride);
    ST_W4(tmp3, 0, 1, 2, 3, src, stride);

    DUP2_ARG2(__lsx_vilvh_b, p0, p1, q1, q0, tmp0, tmp1);
    tmp2 = __lsx_vilvl_h(tmp1, tmp0);
    tmp3 = __lsx_vilvh_h(tmp1, tmp0);

    ST_W4(tmp2, 0, 1, 2, 3, src, stride);
    ST_W4(tmp3, 0, 1, 2, 3, src, stride);
    src -= 4 * stride4;
}
