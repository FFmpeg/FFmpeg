/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Jin Bo <jinbo@loongson.cn>
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
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "libavutil/common.h"
#include "vp9dsp_loongarch.h"

#define LSX_LD_8(_src, _stride, _stride2, _stride3, _stride4, _in0, _in1, _in2, \
                 _in3, _in4, _in5, _in6, _in7)                                  \
{                                                                               \
    _in0 = __lsx_vld(_src, 0);                                                  \
    _in1 = __lsx_vldx(_src, _stride);                                           \
    _in2 = __lsx_vldx(_src, _stride2);                                          \
    _in3 = __lsx_vldx(_src, _stride3);                                          \
    _src += _stride4;                                                           \
    _in4 = __lsx_vld(_src, 0);                                                  \
    _in5 = __lsx_vldx(_src, _stride);                                           \
    _in6 = __lsx_vldx(_src, _stride2);                                          \
    _in7 = __lsx_vldx(_src, _stride3);                                          \
}

#define LSX_ST_8(_dst0, _dst1, _dst2, _dst3, _dst4, _dst5, _dst6, _dst7,        \
                 _dst, _stride, _stride2, _stride3, _stride4)                   \
{                                                                               \
    __lsx_vst(_dst0, _dst, 0);                                                  \
    __lsx_vstx(_dst1, _dst, _stride);                                           \
    __lsx_vstx(_dst2, _dst, _stride2);                                          \
    __lsx_vstx(_dst3, _dst, _stride3);                                          \
    _dst += _stride4;                                                           \
    __lsx_vst(_dst4, _dst, 0);                                                  \
    __lsx_vstx(_dst5, _dst, _stride);                                           \
    __lsx_vstx(_dst6, _dst, _stride2);                                          \
    __lsx_vstx(_dst7, _dst, _stride3);                                          \
}

#define VP9_LPF_FILTER4_4W(p1_src, p0_src, q0_src, q1_src, mask_src, hev_src, \
                           p1_dst, p0_dst, q0_dst, q1_dst)                    \
{                                                                             \
    __m128i p1_tmp, p0_tmp, q0_tmp, q1_tmp, q0_sub_p0, filt, filt1, filt2;    \
    const __m128i cnst3b = __lsx_vldi(3);                                     \
    const __m128i cnst4b = __lsx_vldi(4);                                     \
                                                                              \
    p1_tmp = __lsx_vxori_b(p1_src, 0x80);                                     \
    p0_tmp = __lsx_vxori_b(p0_src, 0x80);                                     \
    q0_tmp = __lsx_vxori_b(q0_src, 0x80);                                     \
    q1_tmp = __lsx_vxori_b(q1_src, 0x80);                                     \
                                                                              \
    filt = __lsx_vssub_b(p1_tmp, q1_tmp);                                     \
                                                                              \
    filt = filt & hev_src;                                                    \
                                                                              \
    q0_sub_p0 = __lsx_vssub_b(q0_tmp, p0_tmp);                                \
    filt = __lsx_vsadd_b(filt, q0_sub_p0);                                    \
    filt = __lsx_vsadd_b(filt, q0_sub_p0);                                    \
    filt = __lsx_vsadd_b(filt, q0_sub_p0);                                    \
    filt = filt & mask_src;                                                   \
                                                                              \
    filt1 = __lsx_vsadd_b(filt, cnst4b);                                      \
    filt1 = __lsx_vsrai_b(filt1, 3);                                          \
                                                                              \
    filt2 = __lsx_vsadd_b(filt, cnst3b);                                      \
    filt2 = __lsx_vsrai_b(filt2, 3);                                          \
                                                                              \
    q0_tmp = __lsx_vssub_b(q0_tmp, filt1);                                    \
    q0_dst = __lsx_vxori_b(q0_tmp, 0x80);                                     \
    p0_tmp = __lsx_vsadd_b(p0_tmp, filt2);                                    \
    p0_dst = __lsx_vxori_b(p0_tmp, 0x80);                                     \
                                                                              \
    filt = __lsx_vsrari_b(filt1, 1);                                          \
    hev_src = __lsx_vxori_b(hev_src, 0xff);                                   \
    filt = filt & hev_src;                                                    \
                                                                              \
    q1_tmp = __lsx_vssub_b(q1_tmp, filt);                                     \
    q1_dst = __lsx_vxori_b(q1_tmp, 0x80);                                     \
    p1_tmp = __lsx_vsadd_b(p1_tmp, filt);                                     \
    p1_dst = __lsx_vxori_b(p1_tmp, 0x80);                                     \
}

#define VP9_FLAT4(p3_src, p2_src, p0_src, q0_src, q2_src, q3_src, flat_dst)  \
{                                                                            \
    __m128i f_tmp = __lsx_vldi(1);                                           \
    __m128i p2_a_sub_p0, q2_a_sub_q0, p3_a_sub_p0, q3_a_sub_q0;              \
                                                                             \
    p2_a_sub_p0 = __lsx_vabsd_bu(p2_src, p0_src);                            \
    q2_a_sub_q0 = __lsx_vabsd_bu(q2_src, q0_src);                            \
    p3_a_sub_p0 = __lsx_vabsd_bu(p3_src, p0_src);                            \
    q3_a_sub_q0 = __lsx_vabsd_bu(q3_src, q0_src);                            \
                                                                             \
    p2_a_sub_p0 = __lsx_vmax_bu(p2_a_sub_p0, q2_a_sub_q0);                   \
    flat_dst = __lsx_vmax_bu(p2_a_sub_p0, flat_dst);                         \
    p3_a_sub_p0 = __lsx_vmax_bu(p3_a_sub_p0, q3_a_sub_q0);                   \
    flat_dst = __lsx_vmax_bu(p3_a_sub_p0, flat_dst);                         \
                                                                             \
    flat_dst = __lsx_vslt_bu(f_tmp, flat_dst);                               \
    flat_dst = __lsx_vxori_b(flat_dst, 0xff);                                \
    flat_dst = flat_dst & mask;                                              \
}

#define VP9_FLAT5(p7_src, p6_src, p5_src, p4_src, p0_src, q0_src, q4_src, \
                  q5_src, q6_src, q7_src, flat_src, flat2_dst)            \
{                                                                         \
    __m128i f_tmp = __lsx_vldi(1);                                        \
    __m128i p4_a_sub_p0, q4_a_sub_q0, p5_a_sub_p0, q5_a_sub_q0;           \
    __m128i p6_a_sub_p0, q6_a_sub_q0, p7_a_sub_p0, q7_a_sub_q0;           \
                                                                          \
    p4_a_sub_p0 = __lsx_vabsd_bu(p4_src, p0_src);                         \
    q4_a_sub_q0 = __lsx_vabsd_bu(q4_src, q0_src);                         \
    p5_a_sub_p0 = __lsx_vabsd_bu(p5_src, p0_src);                         \
    q5_a_sub_q0 = __lsx_vabsd_bu(q5_src, q0_src);                         \
    p6_a_sub_p0 = __lsx_vabsd_bu(p6_src, p0_src);                         \
    q6_a_sub_q0 = __lsx_vabsd_bu(q6_src, q0_src);                         \
    p7_a_sub_p0 = __lsx_vabsd_bu(p7_src, p0_src);                         \
    q7_a_sub_q0 = __lsx_vabsd_bu(q7_src, q0_src);                         \
                                                                          \
    p4_a_sub_p0 = __lsx_vmax_bu(p4_a_sub_p0, q4_a_sub_q0);                \
    flat2_dst = __lsx_vmax_bu(p5_a_sub_p0, q5_a_sub_q0);                  \
    flat2_dst = __lsx_vmax_bu(p4_a_sub_p0, flat2_dst);                    \
    p6_a_sub_p0 = __lsx_vmax_bu(p6_a_sub_p0, q6_a_sub_q0);                \
    flat2_dst = __lsx_vmax_bu(p6_a_sub_p0, flat2_dst);                    \
    p7_a_sub_p0 = __lsx_vmax_bu(p7_a_sub_p0, q7_a_sub_q0);                \
    flat2_dst = __lsx_vmax_bu(p7_a_sub_p0, flat2_dst);                    \
                                                                          \
    flat2_dst = __lsx_vslt_bu(f_tmp, flat2_dst);                          \
    flat2_dst = __lsx_vxori_b(flat2_dst, 0xff);                           \
    flat2_dst = flat2_dst & flat_src;                                     \
}

#define VP9_FILTER8(p3_src, p2_src, p1_src, p0_src,            \
                    q0_src, q1_src, q2_src, q3_src,            \
                    p2_filt8_dst, p1_filt8_dst, p0_filt8_dst,  \
                    q0_filt8_dst, q1_filt8_dst, q2_filt8_dst)  \
{                                                              \
    __m128i tmp0, tmp1, tmp2;                                  \
                                                               \
    tmp2 = __lsx_vadd_h(p2_src, p1_src);                       \
    tmp2 = __lsx_vadd_h(tmp2, p0_src);                         \
    tmp0 = __lsx_vslli_h(p3_src, 1);                           \
                                                               \
    tmp0 = __lsx_vadd_h(tmp0, tmp2);                           \
    tmp0 = __lsx_vadd_h(tmp0, q0_src);                         \
    tmp1 = __lsx_vadd_h(tmp0, p3_src);                         \
    tmp1 = __lsx_vadd_h(tmp1, p2_src);                         \
    p2_filt8_dst = __lsx_vsrari_h(tmp1, 3);                    \
                                                               \
    tmp1 = __lsx_vadd_h(tmp0, p1_src);                         \
    tmp1 = __lsx_vadd_h(tmp1, q1_src);                         \
    p1_filt8_dst = __lsx_vsrari_h(tmp1, 3);                    \
                                                               \
    tmp1 = __lsx_vadd_h(q2_src, q1_src);                       \
    tmp1 = __lsx_vadd_h(tmp1, q0_src);                         \
    tmp2 = __lsx_vadd_h(tmp2, tmp1);                           \
    tmp0 = __lsx_vadd_h(tmp2, p0_src);                         \
    tmp0 = __lsx_vadd_h(tmp0, p3_src);                         \
    p0_filt8_dst = __lsx_vsrari_h(tmp0, 3);                    \
                                                               \
    tmp0 = __lsx_vadd_h(q2_src, q3_src);                       \
    tmp0 = __lsx_vadd_h(tmp0, p0_src);                         \
    tmp0 = __lsx_vadd_h(tmp0, tmp1);                           \
    tmp1 = __lsx_vadd_h(q3_src, q3_src);                       \
    tmp1 = __lsx_vadd_h(tmp1, tmp0);                           \
    q2_filt8_dst = __lsx_vsrari_h(tmp1, 3);                    \
                                                               \
    tmp0 = __lsx_vadd_h(tmp2, q3_src);                         \
    tmp1 = __lsx_vadd_h(tmp0, q0_src);                         \
    q0_filt8_dst = __lsx_vsrari_h(tmp1, 3);                    \
                                                               \
    tmp1 = __lsx_vsub_h(tmp0, p2_src);                         \
    tmp0 = __lsx_vadd_h(q1_src, q3_src);                       \
    tmp1 = __lsx_vadd_h(tmp0, tmp1);                           \
    q1_filt8_dst = __lsx_vsrari_h(tmp1, 3);                    \
}

#define LPF_MASK_HEV(p3_src, p2_src, p1_src, p0_src, q0_src, q1_src,        \
                     q2_src, q3_src, limit_src, b_limit_src, thresh_src,    \
                     hev_dst, mask_dst, flat_dst)                           \
{                                                                           \
    __m128i p3_asub_p2_tmp, p2_asub_p1_tmp, p1_asub_p0_tmp, q1_asub_q0_tmp; \
    __m128i p1_asub_q1_tmp, p0_asub_q0_tmp, q3_asub_q2_tmp, q2_asub_q1_tmp; \
                                                                            \
    /* absolute subtraction of pixel values */                              \
    p3_asub_p2_tmp = __lsx_vabsd_bu(p3_src, p2_src);                        \
    p2_asub_p1_tmp = __lsx_vabsd_bu(p2_src, p1_src);                        \
    p1_asub_p0_tmp = __lsx_vabsd_bu(p1_src, p0_src);                        \
    q1_asub_q0_tmp = __lsx_vabsd_bu(q1_src, q0_src);                        \
    q2_asub_q1_tmp = __lsx_vabsd_bu(q2_src, q1_src);                        \
    q3_asub_q2_tmp = __lsx_vabsd_bu(q3_src, q2_src);                        \
    p0_asub_q0_tmp = __lsx_vabsd_bu(p0_src, q0_src);                        \
    p1_asub_q1_tmp = __lsx_vabsd_bu(p1_src, q1_src);                        \
                                                                            \
    /* calculation of hev */                                                \
    flat_dst = __lsx_vmax_bu(p1_asub_p0_tmp, q1_asub_q0_tmp);               \
    hev_dst = __lsx_vslt_bu(thresh_src, flat_dst);                          \
                                                                            \
    /* calculation of mask */                                               \
    p0_asub_q0_tmp = __lsx_vsadd_bu(p0_asub_q0_tmp, p0_asub_q0_tmp);        \
    p1_asub_q1_tmp = __lsx_vsrli_b(p1_asub_q1_tmp, 1);                      \
    p0_asub_q0_tmp = __lsx_vsadd_bu(p0_asub_q0_tmp, p1_asub_q1_tmp);        \
                                                                            \
    mask_dst = __lsx_vslt_bu(b_limit_src, p0_asub_q0_tmp);                  \
    mask_dst = __lsx_vmax_bu(flat_dst, mask_dst);                           \
    p3_asub_p2_tmp = __lsx_vmax_bu(p3_asub_p2_tmp, p2_asub_p1_tmp);         \
    mask_dst = __lsx_vmax_bu(p3_asub_p2_tmp, mask_dst);                     \
    q2_asub_q1_tmp = __lsx_vmax_bu(q2_asub_q1_tmp, q3_asub_q2_tmp);         \
    mask_dst = __lsx_vmax_bu(q2_asub_q1_tmp, mask_dst);                     \
                                                                            \
    mask_dst = __lsx_vslt_bu(limit_src, mask_dst);                          \
    mask_dst = __lsx_vxori_b(mask_dst, 0xff);                               \
}

void ff_loop_filter_v_4_8_lsx(uint8_t *dst, ptrdiff_t stride,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i mask, hev, flat, thresh, b_limit, limit;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0, p1_out, p0_out, q0_out, q1_out;

    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);

    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    __lsx_vstelm_d(p1_out, dst - stride2, 0, 0);
    __lsx_vstelm_d(p0_out, dst -  stride, 0, 0);
    __lsx_vstelm_d(q0_out, dst          , 0, 0);
    __lsx_vstelm_d(q1_out, dst +  stride, 0, 0);
}

void ff_loop_filter_v_44_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i mask, hev, flat, thresh0, b_limit0;
    __m128i limit0, thresh1, b_limit1, limit1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;

    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh0 = __lsx_vreplgr2vr_b(thresh_ptr);
    thresh1 = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh0 = __lsx_vilvl_d(thresh1, thresh0);

    b_limit0 = __lsx_vreplgr2vr_b(b_limit_ptr);
    b_limit1 = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit0 = __lsx_vilvl_d(b_limit1, b_limit0);

    limit0 = __lsx_vreplgr2vr_b(limit_ptr);
    limit1 = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit0 = __lsx_vilvl_d(limit1, limit0);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit0, b_limit0, thresh0,
                 hev, mask, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1, p0, q0, q1);

    __lsx_vst(p1, dst - stride2, 0);
    __lsx_vst(p0, dst -  stride, 0);
    __lsx_vst(q0, dst          , 0);
    __lsx_vst(q1, dst +  stride, 0);
}

void ff_loop_filter_v_8_8_lsx(uint8_t *dst, ptrdiff_t stride,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i mask, hev, flat, thresh, b_limit, limit;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i p2_filter8, p1_filter8, p0_filter8;
    __m128i q0_filter8, q1_filter8, q2_filter8;
    __m128i p3_l, p2_l, p1_l, p0_l, q3_l, q2_l, q1_l, q0_l;
    __m128i zero = __lsx_vldi(0);

    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvl_d(zero, flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        __lsx_vstelm_d(p1_out, dst - stride2, 0, 0);
        __lsx_vstelm_d(p0_out, dst -  stride, 0, 0);
        __lsx_vstelm_d(q0_out, dst          , 0, 0);
        __lsx_vstelm_d(q1_out, dst +  stride, 0, 0);
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filter8,
                    p1_filter8, p0_filter8, q0_filter8, q1_filter8, q2_filter8);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, zero, p2_filter8, zero, p1_filter8,
                  zero, p0_filter8, zero, q0_filter8, p2_filter8,
                  p1_filter8, p0_filter8, q0_filter8);
        DUP2_ARG2(__lsx_vpickev_b, zero, q1_filter8, zero, q2_filter8,
                  q1_filter8, q2_filter8);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filter8, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filter8, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filter8, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filter8, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filter8, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filter8, flat);

        __lsx_vstelm_d(p2_out, dst - stride3, 0, 0);
        __lsx_vstelm_d(p1_out, dst - stride2, 0, 0);
        __lsx_vstelm_d(p0_out, dst - stride, 0, 0);
        __lsx_vstelm_d(q0_out, dst, 0, 0);
        __lsx_vstelm_d(q1_out, dst + stride, 0, 0);
        __lsx_vstelm_d(q2_out, dst + stride2, 0, 0);
    }
}

void ff_loop_filter_v_88_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i flat, mask, hev, tmp, thresh, b_limit, limit;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i p2_filt8_h, p1_filt8_h, p0_filt8_h;
    __m128i q0_filt8_h, q1_filt8_h, q2_filt8_h;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements */
    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh = __lsx_vreplgr2vr_b(thresh_ptr);
    tmp    = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh = __lsx_vilvl_d(tmp, thresh);

    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    tmp     = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit = __lsx_vilvl_d(tmp, b_limit);

    limit = __lsx_vreplgr2vr_b(limit_ptr);
    tmp   = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit = __lsx_vilvl_d(tmp, limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        __lsx_vst(p1_out, dst - stride2, 0);
        __lsx_vst(p0_out, dst - stride, 0);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vst(q1_out, dst + stride, 0);
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        DUP4_ARG2(__lsx_vilvh_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_h, p2_h, p1_h, p0_h);
        DUP4_ARG2(__lsx_vilvh_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_h, q1_h, q2_h, q3_h);
        VP9_FILTER8(p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h, p2_filt8_h,
                    p1_filt8_h, p0_filt8_h, q0_filt8_h, q1_filt8_h, q2_filt8_h);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_h, p2_filt8_l, p1_filt8_h,
                  p1_filt8_l, p0_filt8_h, p0_filt8_l, q0_filt8_h, q0_filt8_l,
                  p2_filt8_l, p1_filt8_l, p0_filt8_l, q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_h, q1_filt8_l, q2_filt8_h,
                  q2_filt8_l, q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filt8_l, flat);


        __lsx_vstx(p2_out, dst, -stride3);
        __lsx_vstx(p1_out, dst, -stride2);
        __lsx_vstx(p0_out, dst, -stride);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vstx(q1_out, dst, stride);
        __lsx_vstx(q2_out, dst, stride2);
    }
}

void ff_loop_filter_v_84_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i flat, mask, hev, tmp, thresh, b_limit, limit;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements */
    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh = __lsx_vreplgr2vr_b(thresh_ptr);
    tmp    = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh = __lsx_vilvl_d(tmp, thresh);

    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    tmp     = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit = __lsx_vilvl_d(tmp, b_limit);

    limit = __lsx_vreplgr2vr_b(limit_ptr);
    tmp   = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit = __lsx_vilvl_d(tmp, limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvl_d(zero, flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        __lsx_vstx(p1_out, dst, -stride2);
        __lsx_vstx(p0_out, dst, -stride);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vstx(q1_out, dst, stride);
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_l, p2_filt8_l, p1_filt8_l,
                  p1_filt8_l, p0_filt8_l, p0_filt8_l, q0_filt8_l, q0_filt8_l,
                  p2_filt8_l, p1_filt8_l, p0_filt8_l, q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_l, q1_filt8_l, q2_filt8_l,
                  q2_filt8_l, q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filt8_l, flat);

        __lsx_vstx(p2_out, dst, -stride3);
        __lsx_vstx(p1_out, dst, -stride2);
        __lsx_vstx(p0_out, dst, -stride);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vstx(q1_out, dst, stride);
        __lsx_vstx(q2_out, dst, stride2);
    }
}

void ff_loop_filter_v_48_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i flat, mask, hev, tmp, thresh, b_limit, limit;
    __m128i p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h;
    __m128i p2_filt8_h, p1_filt8_h, p0_filt8_h;
    __m128i q0_filt8_h, q1_filt8_h, q2_filt8_h;
    __m128i zero = { 0 };

    /* load vector elements */
    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh = __lsx_vreplgr2vr_b(thresh_ptr);
    tmp    = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh = __lsx_vilvl_d(tmp, thresh);

    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    tmp     = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit = __lsx_vilvl_d(tmp, b_limit);

    limit = __lsx_vreplgr2vr_b(limit_ptr);
    tmp   = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit = __lsx_vilvl_d(tmp, limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvh_d(flat, zero);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        __lsx_vstx(p1_out, dst, -stride2);
        __lsx_vstx(p0_out, dst, -stride);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vstx(q1_out, dst, stride);
    } else {
        DUP4_ARG2(__lsx_vilvh_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_h, p2_h, p1_h, p0_h);
        DUP4_ARG2(__lsx_vilvh_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_h, q1_h, q2_h, q3_h);
        VP9_FILTER8(p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h, p2_filt8_h,
                    p1_filt8_h, p0_filt8_h, q0_filt8_h, q1_filt8_h, q2_filt8_h);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_h, p2_filt8_h, p1_filt8_h,
                  p1_filt8_h, p0_filt8_h, p0_filt8_h, q0_filt8_h, q0_filt8_h,
                  p2_filt8_h, p1_filt8_h, p0_filt8_h, q0_filt8_h);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_h, q1_filt8_h, q2_filt8_h,
                  q2_filt8_h, q1_filt8_h, q2_filt8_h);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filt8_h, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filt8_h, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filt8_h, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filt8_h, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filt8_h, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filt8_h, flat);

        __lsx_vstx(p2_out, dst, -stride3);
        __lsx_vstx(p1_out, dst, -stride2);
        __lsx_vstx(p0_out, dst, -stride);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vstx(q1_out, dst, stride);
        __lsx_vstx(q2_out, dst, stride2);
    }
}

static int32_t vp9_hz_lpf_t4_and_t8_16w(uint8_t *dst, ptrdiff_t stride,
                                        uint8_t *filter48,
                                        int32_t b_limit_ptr,
                                        int32_t limit_ptr,
                                        int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i p2_filt8_h, p1_filt8_h, p0_filt8_h;
    __m128i q0_filt8_h, q1_filt8_h, q2_filt8_h;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements */
    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        __lsx_vstx(p1_out, dst, -stride2);
        __lsx_vstx(p0_out, dst, -stride);
        __lsx_vst(q0_out, dst, 0);
        __lsx_vstx(q1_out, dst, stride);
        return 1;
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        DUP4_ARG2(__lsx_vilvh_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_h, p2_h, p1_h, p0_h);
        DUP4_ARG2(__lsx_vilvh_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_h, q1_h, q2_h, q3_h);
        VP9_FILTER8(p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h, p2_filt8_h,
                    p1_filt8_h, p0_filt8_h, q0_filt8_h, q1_filt8_h, q2_filt8_h);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_h, p2_filt8_l, p1_filt8_h,
                  p1_filt8_l, p0_filt8_h, p0_filt8_l, q0_filt8_h, q0_filt8_l,
                  p2_filt8_l, p1_filt8_l, p0_filt8_l, q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_h, q1_filt8_l, q2_filt8_h,
                  q2_filt8_l, q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filt8_l, flat);

        __lsx_vst(p2_out, filter48, 0);
        __lsx_vst(p1_out, filter48, 16);
        __lsx_vst(p0_out, filter48, 32);
        __lsx_vst(q0_out, filter48, 48);
        __lsx_vst(q1_out, filter48, 64);
        __lsx_vst(q2_out, filter48, 80);
        __lsx_vst(flat, filter48, 96);

        return 0;
    }
}

static void vp9_hz_lpf_t16_16w(uint8_t *dst, ptrdiff_t stride,
                               uint8_t *filter48)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - stride4;
    uint8_t *dst_tmp1 = dst + stride4;
    __m128i p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    __m128i flat, flat2, filter8;
    __m128i zero = __lsx_vldi(0);
    __m128i out_h, out_l;
    v8u16 p7_l_in, p6_l_in, p5_l_in, p4_l_in;
    v8u16 p3_l_in, p2_l_in, p1_l_in, p0_l_in;
    v8u16 q7_l_in, q6_l_in, q5_l_in, q4_l_in;
    v8u16 q3_l_in, q2_l_in, q1_l_in, q0_l_in;
    v8u16 p7_h_in, p6_h_in, p5_h_in, p4_h_in;
    v8u16 p3_h_in, p2_h_in, p1_h_in, p0_h_in;
    v8u16 q7_h_in, q6_h_in, q5_h_in, q4_h_in;
    v8u16 q3_h_in, q2_h_in, q1_h_in, q0_h_in;
    v8u16 tmp0_l, tmp1_l, tmp0_h, tmp1_h;

    flat = __lsx_vld(filter48, 96);

    DUP4_ARG2(__lsx_vldx, dst_tmp, -stride4, dst_tmp, -stride3, dst_tmp,
              -stride2, dst_tmp, -stride, p7, p6, p5, p4);
    p3 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, p2, p1);
    p0 = __lsx_vldx(dst_tmp, stride3);

    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    q4 = __lsx_vld(dst_tmp1, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp1, stride, dst_tmp1, stride2, q5, q6);
    q7 = __lsx_vldx(dst_tmp1, stride3);
    VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

    /* if flat2 is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat2)) {
        DUP4_ARG2(__lsx_vld, filter48, 0, filter48, 16, filter48, 32, filter48,
                  48, p2, p1, p0, q0);
        DUP2_ARG2(__lsx_vld, filter48, 64, filter48, 80, q1, q2);

        __lsx_vstx(p2, dst, -stride3);
        __lsx_vstx(p1, dst, -stride2);
        __lsx_vstx(p0, dst, -stride);
        __lsx_vst(q0, dst, 0);
        __lsx_vstx(q1, dst, stride);
        __lsx_vstx(q2, dst, stride2);
    } else {
        dst = dst_tmp - stride3;

        p7_l_in = (v8u16)__lsx_vilvl_b(zero, p7);
        p6_l_in = (v8u16)__lsx_vilvl_b(zero, p6);
        p5_l_in = (v8u16)__lsx_vilvl_b(zero, p5);
        p4_l_in = (v8u16)__lsx_vilvl_b(zero, p4);
        p3_l_in = (v8u16)__lsx_vilvl_b(zero, p3);
        p2_l_in = (v8u16)__lsx_vilvl_b(zero, p2);
        p1_l_in = (v8u16)__lsx_vilvl_b(zero, p1);
        p0_l_in = (v8u16)__lsx_vilvl_b(zero, p0);

        q0_l_in = (v8u16)__lsx_vilvl_b(zero, q0);

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

        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        p7_h_in = (v8u16)__lsx_vilvh_b(zero, p7);
        p6_h_in = (v8u16)__lsx_vilvh_b(zero, p6);
        p5_h_in = (v8u16)__lsx_vilvh_b(zero, p5);
        p4_h_in = (v8u16)__lsx_vilvh_b(zero, p4);

        p3_h_in = (v8u16)__lsx_vilvh_b(zero, p3);
        p2_h_in = (v8u16)__lsx_vilvh_b(zero, p2);
        p1_h_in = (v8u16)__lsx_vilvh_b(zero, p1);
        p0_h_in = (v8u16)__lsx_vilvh_b(zero, p0);
        q0_h_in = (v8u16)__lsx_vilvh_b(zero, q0);

        tmp0_h = p7_h_in << 3;
        tmp0_h -= p7_h_in;
        tmp0_h += p6_h_in;
        tmp0_h += q0_h_in;
        tmp1_h = p6_h_in + p5_h_in;
        tmp1_h += p4_h_in;
        tmp1_h += p3_h_in;
        tmp1_h += p2_h_in;
        tmp1_h += p1_h_in;
        tmp1_h += p0_h_in;
        tmp1_h += tmp0_h;

        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        p6 = __lsx_vbitsel_v(p6, out_l, flat2);
        __lsx_vst(p6, dst, 0);
        dst += stride;

        /* p5 */
        q1_l_in = (v8u16)__lsx_vilvl_b(zero, q1);
        tmp0_l = p5_l_in - p6_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q1_h_in = (v8u16)__lsx_vilvh_b(zero, q1);
        tmp0_h = p5_h_in - p6_h_in;
        tmp0_h += q1_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        p5 = __lsx_vbitsel_v(p5, out_l, flat2);
        __lsx_vst(p5, dst, 0);
        dst += stride;

        /* p4 */
        q2_l_in = (v8u16)__lsx_vilvl_b(zero, q2);
        tmp0_l = p4_l_in - p5_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q2_h_in = (v8u16)__lsx_vilvh_b(zero, q2);
        tmp0_h = p4_h_in - p5_h_in;
        tmp0_h += q2_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        p4 = __lsx_vbitsel_v(p4, out_l, flat2);
        __lsx_vst(p4, dst, 0);
        dst += stride;

        /* p3 */
        q3_l_in = (v8u16)__lsx_vilvl_b(zero, q3);
        tmp0_l = p3_l_in - p4_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q3_h_in = (v8u16)__lsx_vilvh_b(zero, q3);
        tmp0_h = p3_h_in - p4_h_in;
        tmp0_h += q3_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        p3 = __lsx_vbitsel_v(p3, out_l, flat2);
        __lsx_vst(p3, dst, 0);
        dst += stride;

        /* p2 */
        q4_l_in = (v8u16)__lsx_vilvl_b(zero, q4);
        filter8 = __lsx_vld(filter48, 0);
        tmp0_l = p2_l_in - p3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q4_h_in = (v8u16)__lsx_vilvh_b(zero, q4);
        tmp0_h = p2_h_in - p3_h_in;
        tmp0_h += q4_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 0);
        dst += stride;

        /* p1 */
        q5_l_in = (v8u16)__lsx_vilvl_b(zero, q5);
        filter8 = __lsx_vld(filter48, 16);
        tmp0_l = p1_l_in - p2_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q5_h_in = (v8u16)__lsx_vilvh_b(zero, q5);
        tmp0_h = p1_h_in - p2_h_in;
        tmp0_h += q5_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 0);
        dst += stride;

        /* p0 */
        q6_l_in = (v8u16)__lsx_vilvl_b(zero, q6);
        filter8 = __lsx_vld(filter48, 32);
        tmp0_l = p0_l_in - p1_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q6_h_in = (v8u16)__lsx_vilvh_b(zero, q6);
        tmp0_h = p0_h_in - p1_h_in;
        tmp0_h += q6_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 0);
        dst += stride;

        /* q0 */
        q7_l_in = (v8u16)__lsx_vilvl_b(zero, q7);
        filter8 = __lsx_vld(filter48, 48);
        tmp0_l = q7_l_in - p0_l_in;
        tmp0_l += q0_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        q7_h_in = (v8u16)__lsx_vilvh_b(zero, q7);
        tmp0_h = q7_h_in - p0_h_in;
        tmp0_h += q0_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 0);
        dst += stride;

        /* q1 */
        filter8 = __lsx_vld(filter48, 64);
        tmp0_l = q7_l_in - q0_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p6_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        tmp0_h = q7_h_in - q0_h_in;
        tmp0_h += q1_h_in;
        tmp0_h -= p6_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 0);
        dst += stride;

        /* q2 */
        filter8 = __lsx_vld(filter48, 80);
        tmp0_l = q7_l_in - q1_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p5_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        tmp0_h = q7_h_in - q1_h_in;
        tmp0_h += q2_h_in;
        tmp0_h -= p5_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 0);
        dst += stride;

        /* q3 */
        tmp0_l = q7_l_in - q2_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p4_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        tmp0_h = q7_h_in - q2_h_in;
        tmp0_h += q3_h_in;
        tmp0_h -= p4_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        q3 = __lsx_vbitsel_v(q3, out_l, flat2);
        __lsx_vst(q3, dst, 0);
        dst += stride;

        /* q4 */
        tmp0_l = q7_l_in - q3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p3_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        tmp0_h = q7_h_in - q3_h_in;
        tmp0_h += q4_h_in;
        tmp0_h -= p3_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        q4 = __lsx_vbitsel_v(q4, out_l, flat2);
        __lsx_vst(q4, dst, 0);
        dst += stride;

        /* q5 */
        tmp0_l = q7_l_in - q4_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p2_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        tmp0_h = q7_h_in - q4_h_in;
        tmp0_h += q5_h_in;
        tmp0_h -= p2_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        q5 = __lsx_vbitsel_v(q5, out_l, flat2);
        __lsx_vst(q5, dst, 0);
        dst += stride;

        /* q6 */
        tmp0_l = q7_l_in - q5_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p1_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        tmp0_h = q7_h_in - q5_h_in;
        tmp0_h += q6_h_in;
        tmp0_h -= p1_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        q6 = __lsx_vbitsel_v(q6, out_l, flat2);
        __lsx_vst(q6, dst, 0);
    }
}

void ff_loop_filter_v_16_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t filter48[16 * 8] __attribute__ ((aligned(16)));
    uint8_t early_exit = 0;

    early_exit = vp9_hz_lpf_t4_and_t8_16w(dst, stride, &filter48[0],
                                          b_limit_ptr, limit_ptr, thresh_ptr);

    if (0 == early_exit) {
        vp9_hz_lpf_t16_16w(dst, stride, filter48);
    }
}

void ff_loop_filter_v_16_8_lsx(uint8_t *dst, ptrdiff_t stride,
                               int32_t b_limit_ptr,
                               int32_t limit_ptr,
                               int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - stride4;
    uint8_t *dst_tmp1 = dst + stride4;
    __m128i zero = __lsx_vldi(0);
    __m128i flat2, mask, hev, flat, thresh, b_limit, limit;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0, p7, p6, p5, p4, q4, q5, q6, q7;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i p0_filter16, p1_filter16;
    __m128i p2_filter8, p1_filter8, p0_filter8;
    __m128i q0_filter8, q1_filter8, q2_filter8;
    __m128i p7_l, p6_l, p5_l, p4_l, q7_l, q6_l, q5_l, q4_l;
    __m128i p3_l, p2_l, p1_l, p0_l, q3_l, q2_l, q1_l, q0_l;
    __m128i tmp0, tmp1, tmp2;

    /* load vector elements */
    DUP4_ARG2(__lsx_vldx, dst, -stride4, dst, -stride3, dst, -stride2,
              dst, -stride, p3, p2, p1, p0);
    q0 = __lsx_vld(dst, 0);
    DUP2_ARG2(__lsx_vldx, dst, stride, dst, stride2, q1, q2);
    q3 = __lsx_vldx(dst, stride3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvl_d(zero, flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        __lsx_vstelm_d(p1_out, dst - stride2, 0, 0);
        __lsx_vstelm_d(p0_out, dst -   stride, 0, 0);
        __lsx_vstelm_d(q0_out, dst           , 0, 0);
        __lsx_vstelm_d(q1_out, dst +   stride, 0, 0);
    } else {
        /* convert 8 bit input data into 16 bit */
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l,
                    p2_filter8, p1_filter8, p0_filter8, q0_filter8,
                    q1_filter8, q2_filter8);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, zero, p2_filter8, zero, p1_filter8,
                  zero, p0_filter8, zero, q0_filter8, p2_filter8,
                  p1_filter8, p0_filter8, q0_filter8);
        DUP2_ARG2(__lsx_vpickev_b, zero, q1_filter8, zero, q2_filter8,
                  q1_filter8, q2_filter8);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filter8, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filter8, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filter8, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filter8, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filter8, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filter8, flat);

        /* load 16 vector elements */
        DUP4_ARG2(__lsx_vld, dst_tmp - stride4, 0, dst_tmp - stride3, 0,
                  dst_tmp - stride2, 0, dst_tmp - stride, 0, p7, p6, p5, p4);
        DUP4_ARG2(__lsx_vld, dst_tmp1, 0, dst_tmp1 + stride, 0,
                dst_tmp1 + stride2, 0, dst_tmp1 + stride3, 0, q4, q5, q6, q7);

        VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

        /* if flat2 is zero for all pixels, then no need to calculate other filter */
        if (__lsx_bz_v(flat2)) {
            dst -= stride3;
            __lsx_vstelm_d(p2_out, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_out, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p0_out, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(q0_out, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(q1_out, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(q2_out, dst, 0, 0);
        } else {
            /* LSB(right) 8 pixel operation */
            DUP4_ARG2(__lsx_vilvl_b, zero, p7, zero, p6, zero, p5, zero, p4,
                      p7_l, p6_l, p5_l, p4_l);
            DUP4_ARG2(__lsx_vilvl_b, zero, q4, zero, q5, zero, q6, zero, q7,
                      q4_l, q5_l, q6_l, q7_l);

            tmp0 = __lsx_vslli_h(p7_l, 3);
            tmp0 = __lsx_vsub_h(tmp0, p7_l);
            tmp0 = __lsx_vadd_h(tmp0, p6_l);
            tmp0 = __lsx_vadd_h(tmp0, q0_l);

            dst = dst_tmp - stride3;

            /* calculation of p6 and p5 */
            tmp1 = __lsx_vadd_h(p6_l, p5_l);
            tmp1 = __lsx_vadd_h(tmp1, p4_l);
            tmp1 = __lsx_vadd_h(tmp1, p3_l);
            tmp1 = __lsx_vadd_h(tmp1, p2_l);
            tmp1 = __lsx_vadd_h(tmp1, p1_l);
            tmp1 = __lsx_vadd_h(tmp1, p0_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);

            p0_filter16 = __lsx_vsrari_h(tmp1, 4);
            tmp0 = __lsx_vsub_h(p5_l, p6_l);
            tmp0 = __lsx_vadd_h(tmp0, q1_l);
            tmp0 = __lsx_vsub_h(tmp0, p7_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);

            p1_filter16 = __lsx_vsrari_h(tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(p6, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(p5, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
            dst += stride;

            /* calculation of p4 and p3 */
            tmp0 = __lsx_vsub_h(p4_l, p5_l);
            tmp0 = __lsx_vadd_h(tmp0, q2_l);
            tmp0 = __lsx_vsub_h(tmp0, p7_l);
            tmp2 = __lsx_vsub_h(p3_l, p4_l);
            tmp2 = __lsx_vadd_h(tmp2, q3_l);
            tmp2 = __lsx_vsub_h(tmp2, p7_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);
            p0_filter16 = __lsx_vsrari_h(tmp1, 4);
            tmp1 = __lsx_vadd_h(tmp1, tmp2);
            p1_filter16 = __lsx_vsrari_h(tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(p4, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(p3, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
            dst += stride;

            /* calculation of p2 and p1 */
            tmp0 = __lsx_vsub_h(p2_l, p3_l);
            tmp0 = __lsx_vadd_h(tmp0, q4_l);
            tmp0 = __lsx_vsub_h(tmp0, p7_l);
            tmp2 = __lsx_vsub_h(p1_l, p2_l);
            tmp2 = __lsx_vadd_h(tmp2, q5_l);
            tmp2 = __lsx_vsub_h(tmp2, p7_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);
            p0_filter16 = __lsx_vsrari_h(tmp1, 4);
            tmp1 = __lsx_vadd_h(tmp1, tmp2);
            p1_filter16 = __lsx_vsrari_h(tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(p2_out, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(p1_out, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
            dst += stride;

            /* calculation of p0 and q0 */
            tmp0 = __lsx_vsub_h(p0_l, p1_l);
            tmp0 = __lsx_vadd_h(tmp0, q6_l);
            tmp0 = __lsx_vsub_h(tmp0, p7_l);
            tmp2 = __lsx_vsub_h(q7_l, p0_l);
            tmp2 = __lsx_vadd_h(tmp2, q0_l);
            tmp2 = __lsx_vsub_h(tmp2, p7_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);
            p0_filter16 = __lsx_vsrari_h((__m128i)tmp1, 4);
            tmp1 = __lsx_vadd_h(tmp1, tmp2);
            p1_filter16 = __lsx_vsrari_h((__m128i)tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(p0_out, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(q0_out, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
            dst += stride;

            /* calculation of q1 and q2 */
            tmp0 = __lsx_vsub_h(q7_l, q0_l);
            tmp0 = __lsx_vadd_h(tmp0, q1_l);
            tmp0 = __lsx_vsub_h(tmp0, p6_l);
            tmp2 = __lsx_vsub_h(q7_l, q1_l);
            tmp2 = __lsx_vadd_h(tmp2, q2_l);
            tmp2 = __lsx_vsub_h(tmp2, p5_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);
            p0_filter16 = __lsx_vsrari_h(tmp1, 4);
            tmp1 = __lsx_vadd_h(tmp1, tmp2);
            p1_filter16 = __lsx_vsrari_h(tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(q1_out, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(q2_out, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
            dst += stride;

            /* calculation of q3 and q4 */
            tmp0 = __lsx_vsub_h(q7_l, q2_l);
            tmp0 = __lsx_vadd_h(tmp0, q3_l);
            tmp0 = __lsx_vsub_h(tmp0, p4_l);
            tmp2 = __lsx_vsub_h(q7_l, q3_l);
            tmp2 = __lsx_vadd_h(tmp2, q4_l);
            tmp2 = __lsx_vsub_h(tmp2, p3_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);
            p0_filter16 = __lsx_vsrari_h(tmp1, 4);
            tmp1 = __lsx_vadd_h(tmp1, tmp2);
            p1_filter16 = __lsx_vsrari_h(tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(q3, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(q4, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
            dst += stride;

            /* calculation of q5 and q6 */
            tmp0 = __lsx_vsub_h(q7_l, q4_l);
            tmp0 = __lsx_vadd_h(tmp0, q5_l);
            tmp0 = __lsx_vsub_h(tmp0, p2_l);
            tmp2 = __lsx_vsub_h(q7_l, q5_l);
            tmp2 = __lsx_vadd_h(tmp2, q6_l);
            tmp2 = __lsx_vsub_h(tmp2, p1_l);
            tmp1 = __lsx_vadd_h(tmp1, tmp0);
            p0_filter16 = __lsx_vsrari_h(tmp1, 4);
            tmp1 = __lsx_vadd_h(tmp1, tmp2);
            p1_filter16 = __lsx_vsrari_h(tmp1, 4);
            DUP2_ARG2(__lsx_vpickev_b, zero, p0_filter16, zero,
                      p1_filter16, p0_filter16, p1_filter16);
            p0_filter16 = __lsx_vbitsel_v(q5, p0_filter16, flat2);
            p1_filter16 = __lsx_vbitsel_v(q6, p1_filter16, flat2);
            __lsx_vstelm_d(p0_filter16, dst, 0, 0);
            dst += stride;
            __lsx_vstelm_d(p1_filter16, dst, 0, 0);
        }
    }
}

void ff_loop_filter_h_4_8_lsx(uint8_t *dst, ptrdiff_t stride,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp1 = dst - 4;
    uint8_t *dst_tmp2 = dst_tmp1 + stride4;
    __m128i mask, hev, flat, limit, thresh, b_limit;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i vec0, vec1, vec2, vec3;

    p3 = __lsx_vld(dst_tmp1, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp1, stride, dst_tmp1, stride2, p2, p1);
    p0 = __lsx_vldx(dst_tmp1, stride3);
    q0 = __lsx_vld(dst_tmp2, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp2, stride, dst_tmp2, stride2, q1, q2);
    q3 = __lsx_vldx(dst_tmp2, stride3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    LSX_TRANSPOSE8x8_B(p3, p2, p1, p0, q0, q1, q2, q3,
                       p3, p2, p1, p0, q0, q1, q2, q3);
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1, p0, q0, q1);
    DUP2_ARG2(__lsx_vilvl_b, p0, p1, q1, q0, vec0, vec1);
    vec2 = __lsx_vilvl_h(vec1, vec0);
    vec3 = __lsx_vilvh_h(vec1, vec0);

    dst -= 2;
    __lsx_vstelm_w(vec2, dst, 0, 0);
    __lsx_vstelm_w(vec2, dst + stride, 0, 1);
    __lsx_vstelm_w(vec2, dst + stride2, 0, 2);
    __lsx_vstelm_w(vec2, dst + stride3, 0, 3);
    dst += stride4;
    __lsx_vstelm_w(vec3, dst, 0, 0);
    __lsx_vstelm_w(vec3, dst + stride, 0, 1);
    __lsx_vstelm_w(vec3, dst + stride2, 0, 2);
    __lsx_vstelm_w(vec3, dst + stride3, 0, 3);
}

void ff_loop_filter_h_44_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - 4;
    __m128i mask, hev, flat;
    __m128i thresh0, b_limit0, limit0, thresh1, b_limit1, limit1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i row0, row1, row2, row3, row4, row5, row6, row7;
    __m128i row8, row9, row10, row11, row12, row13, row14, row15;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

    row0 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row1, row2);
    row3 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row4 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row5, row6);
    row7 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row8 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row9, row10);
    row11 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row12 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row13, row14);
    row15 = __lsx_vldx(dst_tmp, stride3);

    LSX_TRANSPOSE16x8_B(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh0 = __lsx_vreplgr2vr_b(thresh_ptr);
    thresh1 = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh0 = __lsx_vilvl_d(thresh1, thresh0);

    b_limit0 = __lsx_vreplgr2vr_b(b_limit_ptr);
    b_limit1 = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit0 = __lsx_vilvl_d(b_limit1, b_limit0);

    limit0 = __lsx_vreplgr2vr_b(limit_ptr);
    limit1 = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit0 = __lsx_vilvl_d(limit1, limit0);

    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit0, b_limit0, thresh0,
                 hev, mask, flat);
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1, p0, q0, q1);
    DUP2_ARG2(__lsx_vilvl_b, p0, p1, q1, q0, tmp0, tmp1);
    tmp2 = __lsx_vilvl_h(tmp1, tmp0);
    tmp3 = __lsx_vilvh_h(tmp1, tmp0);
    DUP2_ARG2(__lsx_vilvh_b, p0, p1, q1, q0, tmp0, tmp1);
    tmp4 = __lsx_vilvl_h(tmp1, tmp0);
    tmp5 = __lsx_vilvh_h(tmp1, tmp0);

    dst -= 2;
    __lsx_vstelm_w(tmp2, dst, 0, 0);
    __lsx_vstelm_w(tmp2, dst + stride, 0, 1);
    __lsx_vstelm_w(tmp2, dst + stride2, 0, 2);
    __lsx_vstelm_w(tmp2, dst + stride3, 0, 3);
    dst += stride4;
    __lsx_vstelm_w(tmp3, dst, 0, 0);
    __lsx_vstelm_w(tmp3, dst + stride, 0, 1);
    __lsx_vstelm_w(tmp3, dst + stride2, 0, 2);
    __lsx_vstelm_w(tmp3, dst + stride3, 0, 3);
    dst += stride4;
    __lsx_vstelm_w(tmp4, dst, 0, 0);
    __lsx_vstelm_w(tmp4, dst + stride, 0, 1);
    __lsx_vstelm_w(tmp4, dst + stride2, 0, 2);
    __lsx_vstelm_w(tmp4, dst + stride3, 0, 3);
    dst += stride4;
    __lsx_vstelm_w(tmp5, dst, 0, 0);
    __lsx_vstelm_w(tmp5, dst + stride, 0, 1);
    __lsx_vstelm_w(tmp5, dst + stride2, 0, 2);
    __lsx_vstelm_w(tmp5, dst + stride3, 0, 3);
}

void ff_loop_filter_h_8_8_lsx(uint8_t *dst, ptrdiff_t stride,
                              int32_t b_limit_ptr,
                              int32_t limit_ptr,
                              int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - 4;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p1_out, p0_out, q0_out, q1_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i vec0, vec1, vec2, vec3, vec4;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements */
    p3 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, p2, p1);
    p0 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    q0 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, q1, q2);
    q3 = __lsx_vldx(dst_tmp, stride3);

    LSX_TRANSPOSE8x8_B(p3, p2, p1, p0, q0, q1, q2, q3,
                       p3, p2, p1, p0, q0, q1, q2, q3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvl_d(zero, flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        /* Store 4 pixels p1-_q1 */
        DUP2_ARG2(__lsx_vilvl_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);

        dst -= 2;
        __lsx_vstelm_w(vec2, dst, 0, 0);
        __lsx_vstelm_w(vec2, dst + stride, 0, 1);
        __lsx_vstelm_w(vec2, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec2, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_w(vec3, dst + stride, 0, 1);
        __lsx_vstelm_w(vec3, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec3, dst + stride3, 0, 3);
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);
        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_l, p2_filt8_l, p1_filt8_l,
                  p1_filt8_l, p0_filt8_l, p0_filt8_l, q0_filt8_l,
                  q0_filt8_l, p2_filt8_l, p1_filt8_l, p0_filt8_l,
                  q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_l, q1_filt8_l, q2_filt8_l,
                  q2_filt8_l, q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2 = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1 = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0 = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0 = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1 = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2 = __lsx_vbitsel_v(q2, q2_filt8_l, flat);

        /* Store 6 pixels p2-_q2 */
        DUP2_ARG2(__lsx_vilvl_b, p1, p2, q0, p0, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);
        vec4 = __lsx_vilvl_b(q2, q1);

        dst -= 3;
        __lsx_vstelm_w(vec2, dst, 0, 0);
        __lsx_vstelm_h(vec4, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec2, dst, 0, 1);
        __lsx_vstelm_h(vec4, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec2, dst, 0, 2);
        __lsx_vstelm_h(vec4, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec2, dst, 0, 3);
        __lsx_vstelm_h(vec4, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_h(vec4, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 1);
        __lsx_vstelm_h(vec4, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 2);
        __lsx_vstelm_h(vec4, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 3);
        __lsx_vstelm_h(vec4, dst, 4, 7);
    }
}

void ff_loop_filter_h_88_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - 4;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p1_out, p0_out, q0_out, q1_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i row4, row5, row6, row7, row12, row13, row14, row15;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i p2_filt8_h, p1_filt8_h, p0_filt8_h;
    __m128i q0_filt8_h, q1_filt8_h, q2_filt8_h;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i zero = __lsx_vldi(0);

    p0 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, p1, p2);
    p3 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row4 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row5, row6);
    row7 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    q3 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, q2, q1);
    q0 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row12 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row13, row14);
    row15 = __lsx_vldx(dst_tmp, stride3);

    /* transpose 16x8 matrix into 8x16 */
    LSX_TRANSPOSE16x8_B(p0, p1, p2, p3, row4, row5, row6, row7,
                        q3, q2, q1, q0, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = __lsx_vreplgr2vr_b(thresh_ptr);
    vec0   = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh = __lsx_vilvl_d(vec0, thresh);

    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    vec0    = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit = __lsx_vilvl_d(vec0, b_limit);

    limit = __lsx_vreplgr2vr_b(limit_ptr);
    vec0  = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit = __lsx_vilvl_d(vec0, limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        DUP2_ARG2(__lsx_vilvl_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec4 = __lsx_vilvl_h(vec1, vec0);
        vec5 = __lsx_vilvh_h(vec1, vec0);

        dst -= 2;
        __lsx_vstelm_w(vec2, dst, 0, 0);
        __lsx_vstelm_w(vec2, dst + stride, 0, 1);
        __lsx_vstelm_w(vec2, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec2, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_w(vec3, dst + stride, 0, 1);
        __lsx_vstelm_w(vec3, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec3, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec4, dst, 0, 0);
        __lsx_vstelm_w(vec4, dst + stride, 0, 1);
        __lsx_vstelm_w(vec4, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec4, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec5, dst, 0, 0);
        __lsx_vstelm_w(vec5, dst + stride, 0, 1);
        __lsx_vstelm_w(vec5, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec5, dst + stride3, 0, 3);
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        DUP4_ARG2(__lsx_vilvh_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_h, p2_h, p1_h, p0_h);
        DUP4_ARG2(__lsx_vilvh_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_h, q1_h, q2_h, q3_h);

        /* filter8 */
        VP9_FILTER8(p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h, p2_filt8_h,
                    p1_filt8_h, p0_filt8_h, q0_filt8_h, q1_filt8_h, q2_filt8_h);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_h, p2_filt8_l, p1_filt8_h,
                  p1_filt8_l, p0_filt8_h, p0_filt8_l, q0_filt8_h, q0_filt8_l,
                  p2_filt8_l, p1_filt8_l, p0_filt8_l, q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_h, q1_filt8_l, q2_filt8_h,
                  q2_filt8_l, q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2 = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1 = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0 = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0 = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1 = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2 = __lsx_vbitsel_v(q2, q2_filt8_l, flat);

        DUP2_ARG2(__lsx_vilvl_b, p1, p2, q0, p0, vec0, vec1);
        vec3 = __lsx_vilvl_h(vec1, vec0);
        vec4 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p1, p2, q0, p0, vec0, vec1);
        vec6 = __lsx_vilvl_h(vec1, vec0);
        vec7 = __lsx_vilvh_h(vec1, vec0);
        vec2 = __lsx_vilvl_b(q2, q1);
        vec5 = __lsx_vilvh_b(q2, q1);

        dst -= 3;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_h(vec2, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 1);
        __lsx_vstelm_h(vec2, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 2);
        __lsx_vstelm_h(vec2, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 3);
        __lsx_vstelm_h(vec2, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 0);
        __lsx_vstelm_h(vec2, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 1);
        __lsx_vstelm_h(vec2, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 2);
        __lsx_vstelm_h(vec2, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 3);
        __lsx_vstelm_h(vec2, dst, 4, 7);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 0);
        __lsx_vstelm_h(vec5, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 1);
        __lsx_vstelm_h(vec5, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 2);
        __lsx_vstelm_h(vec5, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 3);
        __lsx_vstelm_h(vec5, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 0);
        __lsx_vstelm_h(vec5, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 1);
        __lsx_vstelm_h(vec5, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 2);
        __lsx_vstelm_h(vec5, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 3);
        __lsx_vstelm_h(vec5, dst, 4, 7);
    }
}

void ff_loop_filter_h_84_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - 4;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p1_out, p0_out, q0_out, q1_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i row4, row5, row6, row7, row12, row13, row14, row15;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i zero = __lsx_vldi(0);

    p0 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, p1, p2);
    p3 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row4 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row5, row6);
    row7 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    q3 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, q2, q1);
    q0 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row12 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row13, row14);
    row15 = __lsx_vldx(dst_tmp, stride3);

    /* transpose 16x8 matrix into 8x16 */
    LSX_TRANSPOSE16x8_B(p0, p1, p2, p3, row4, row5, row6, row7,
                        q3, q2, q1, q0, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = __lsx_vreplgr2vr_b(thresh_ptr);
    vec0   = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh = __lsx_vilvl_d(vec0, thresh);

    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    vec0    = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit = __lsx_vilvl_d(vec0, b_limit);

    limit = __lsx_vreplgr2vr_b(limit_ptr);
    vec0  = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit = __lsx_vilvl_d(vec0, limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvl_d(zero, flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        DUP2_ARG2(__lsx_vilvl_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec4 = __lsx_vilvl_h(vec1, vec0);
        vec5 = __lsx_vilvh_h(vec1, vec0);

        dst -= 2;
        __lsx_vstelm_w(vec2, dst, 0, 0);
        __lsx_vstelm_w(vec2, dst + stride, 0, 1);
        __lsx_vstelm_w(vec2, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec2, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_w(vec3, dst + stride, 0, 1);
        __lsx_vstelm_w(vec3, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec3, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec4, dst, 0, 0);
        __lsx_vstelm_w(vec4, dst + stride, 0, 1);
        __lsx_vstelm_w(vec4, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec4, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec5, dst, 0, 0);
        __lsx_vstelm_w(vec5, dst + stride, 0, 1);
        __lsx_vstelm_w(vec5, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec5, dst + stride3, 0, 3);
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_l, p2_filt8_l, p1_filt8_l, p1_filt8_l,
                  p0_filt8_l, p0_filt8_l, q0_filt8_l, q0_filt8_l, p2_filt8_l,
                  p1_filt8_l, p0_filt8_l, q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_l, q1_filt8_l, q2_filt8_l, q2_filt8_l,
                  q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2 = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1 = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0 = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0 = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1 = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2 = __lsx_vbitsel_v(q2, q2_filt8_l, flat);

        DUP2_ARG2(__lsx_vilvl_b, p1, p2, q0, p0, vec0, vec1);
        vec3 = __lsx_vilvl_h(vec1, vec0);
        vec4 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p1, p2, q0, p0, vec0, vec1);
        vec6 = __lsx_vilvl_h(vec1, vec0);
        vec7 = __lsx_vilvh_h(vec1, vec0);
        vec2 = __lsx_vilvl_b(q2, q1);
        vec5 = __lsx_vilvh_b(q2, q1);

        dst -= 3;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_h(vec2, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 1);
        __lsx_vstelm_h(vec2, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 2);
        __lsx_vstelm_h(vec2, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 3);
        __lsx_vstelm_h(vec2, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 0);
        __lsx_vstelm_h(vec2, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 1);
        __lsx_vstelm_h(vec2, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 2);
        __lsx_vstelm_h(vec2, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 3);
        __lsx_vstelm_h(vec2, dst, 4, 7);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 0);
        __lsx_vstelm_h(vec5, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 1);
        __lsx_vstelm_h(vec5, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 2);
        __lsx_vstelm_h(vec5, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 3);
        __lsx_vstelm_h(vec5, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 0);
        __lsx_vstelm_h(vec5, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 1);
        __lsx_vstelm_h(vec5, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 2);
        __lsx_vstelm_h(vec5, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 3);
        __lsx_vstelm_h(vec5, dst, 4, 7);
    }
}

void ff_loop_filter_h_48_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    uint8_t *dst_tmp = dst - 4;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p1_out, p0_out, q0_out, q1_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i row4, row5, row6, row7, row12, row13, row14, row15;
    __m128i p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h;
    __m128i p2_filt8_h, p1_filt8_h, p0_filt8_h;
    __m128i q0_filt8_h, q1_filt8_h, q2_filt8_h;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;
    __m128i zero = __lsx_vldi(0);

    p0 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, p1, p2);
    p3 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row4 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row5, row6);
    row7 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    q3 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, q2, q1);
    q0 = __lsx_vldx(dst_tmp, stride3);
    dst_tmp += stride4;
    row12 = __lsx_vld(dst_tmp, 0);
    DUP2_ARG2(__lsx_vldx, dst_tmp, stride, dst_tmp, stride2, row13, row14);
    row15 = __lsx_vldx(dst_tmp, stride3);

    /* transpose 16x8 matrix into 8x16 */
    LSX_TRANSPOSE16x8_B(p0, p1, p2, p3, row4, row5, row6, row7,
                        q3, q2, q1, q0, row12, row13, row14, row15,
                        p3, p2, p1, p0, q0, q1, q2, q3);

    thresh = __lsx_vreplgr2vr_b(thresh_ptr);
    vec0   = __lsx_vreplgr2vr_b(thresh_ptr >> 8);
    thresh = __lsx_vilvl_d(vec0, thresh);

    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    vec0    = __lsx_vreplgr2vr_b(b_limit_ptr >> 8);
    b_limit = __lsx_vilvl_d(vec0, b_limit);

    limit = __lsx_vreplgr2vr_b(limit_ptr);
    vec0  = __lsx_vreplgr2vr_b(limit_ptr >> 8);
    limit = __lsx_vilvl_d(vec0, limit);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvh_d(flat, zero);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        DUP2_ARG2(__lsx_vilvl_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec4 = __lsx_vilvl_h(vec1, vec0);
        vec5 = __lsx_vilvh_h(vec1, vec0);

        dst -= 2;
        __lsx_vstelm_w(vec2, dst, 0, 0);
        __lsx_vstelm_w(vec2, dst + stride, 0, 1);
        __lsx_vstelm_w(vec2, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec2, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_w(vec3, dst + stride, 0, 1);
        __lsx_vstelm_w(vec3, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec3, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec4, dst, 0, 0);
        __lsx_vstelm_w(vec4, dst + stride, 0, 1);
        __lsx_vstelm_w(vec4, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec4, dst + stride3, 0, 3);
        dst += stride4;
        __lsx_vstelm_w(vec5, dst, 0, 0);
        __lsx_vstelm_w(vec5, dst + stride, 0, 1);
        __lsx_vstelm_w(vec5, dst + stride2, 0, 2);
        __lsx_vstelm_w(vec5, dst + stride3, 0, 3);
    } else {
        DUP4_ARG2(__lsx_vilvh_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_h, p2_h, p1_h, p0_h);
        DUP4_ARG2(__lsx_vilvh_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_h, q1_h, q2_h, q3_h);

        VP9_FILTER8(p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h, p2_filt8_h,
                    p1_filt8_h, p0_filt8_h, q0_filt8_h, q1_filt8_h, q2_filt8_h);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_h, p2_filt8_h, p1_filt8_h,
                  p1_filt8_h, p0_filt8_h, p0_filt8_h, q0_filt8_h, q0_filt8_h,
                  p2_filt8_h, p1_filt8_h, p0_filt8_h, q0_filt8_h);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_h, q1_filt8_h, q2_filt8_h,
                  q2_filt8_h, q1_filt8_h, q2_filt8_h);

        /* store pixel values */
        p2 = __lsx_vbitsel_v(p2, p2_filt8_h, flat);
        p1 = __lsx_vbitsel_v(p1_out, p1_filt8_h, flat);
        p0 = __lsx_vbitsel_v(p0_out, p0_filt8_h, flat);
        q0 = __lsx_vbitsel_v(q0_out, q0_filt8_h, flat);
        q1 = __lsx_vbitsel_v(q1_out, q1_filt8_h, flat);
        q2 = __lsx_vbitsel_v(q2, q2_filt8_h, flat);

        DUP2_ARG2(__lsx_vilvl_b, p1, p2, q0, p0, vec0, vec1);
        vec3 = __lsx_vilvl_h(vec1, vec0);
        vec4 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p1, p2, q0, p0, vec0, vec1);
        vec6 = __lsx_vilvl_h(vec1, vec0);
        vec7 = __lsx_vilvh_h(vec1, vec0);
        vec2 = __lsx_vilvl_b(q2, q1);
        vec5 = __lsx_vilvh_b(q2, q1);

        dst -= 3;
        __lsx_vstelm_w(vec3, dst, 0, 0);
        __lsx_vstelm_h(vec2, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 1);
        __lsx_vstelm_h(vec2, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 2);
        __lsx_vstelm_h(vec2, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec3, dst, 0, 3);
        __lsx_vstelm_h(vec2, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 0);
        __lsx_vstelm_h(vec2, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 1);
        __lsx_vstelm_h(vec2, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 2);
        __lsx_vstelm_h(vec2, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec4, dst, 0, 3);
        __lsx_vstelm_h(vec2, dst, 4, 7);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 0);
        __lsx_vstelm_h(vec5, dst, 4, 0);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 1);
        __lsx_vstelm_h(vec5, dst, 4, 1);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 2);
        __lsx_vstelm_h(vec5, dst, 4, 2);
        dst += stride;
        __lsx_vstelm_w(vec6, dst, 0, 3);
        __lsx_vstelm_h(vec5, dst, 4, 3);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 0);
        __lsx_vstelm_h(vec5, dst, 4, 4);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 1);
        __lsx_vstelm_h(vec5, dst, 4, 5);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 2);
        __lsx_vstelm_h(vec5, dst, 4, 6);
        dst += stride;
        __lsx_vstelm_w(vec7, dst, 0, 3);
        __lsx_vstelm_h(vec5, dst, 4, 7);
    }
}

static void vp9_transpose_16x8_to_8x16(uint8_t *input, ptrdiff_t in_pitch,
                                       uint8_t *output)
{
    __m128i p7_org, p6_org, p5_org, p4_org, p3_org, p2_org, p1_org, p0_org;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m128i p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    ptrdiff_t in_pitch2 = in_pitch << 1;
    ptrdiff_t in_pitch3 = in_pitch2 + in_pitch;
    ptrdiff_t in_pitch4 = in_pitch2 << 1;

    LSX_LD_8(input, in_pitch, in_pitch2, in_pitch3, in_pitch4,
             p7_org, p6_org, p5_org, p4_org, p3_org, p2_org, p1_org, p0_org);
    /* 8x8 transpose */
    LSX_TRANSPOSE8x8_B(p7_org, p6_org, p5_org, p4_org, p3_org, p2_org, p1_org,
                       p0_org, p7, p6, p5, p4, p3, p2, p1, p0);
    /* 8x8 transpose */
    DUP4_ARG2(__lsx_vilvh_b, p5_org, p7_org, p4_org, p6_org, p1_org,
              p3_org, p0_org, p2_org, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vilvl_b, tmp1, tmp0, tmp3, tmp2, tmp4, tmp6);
    DUP2_ARG2(__lsx_vilvh_b, tmp1, tmp0, tmp3, tmp2, tmp5, tmp7);
    DUP2_ARG2(__lsx_vilvl_w, tmp6, tmp4, tmp7, tmp5, q0, q4);
    DUP2_ARG2(__lsx_vilvh_w, tmp6, tmp4, tmp7, tmp5, q2, q6);
    DUP4_ARG2(__lsx_vbsrl_v, q0, 8, q2, 8, q4, 8, q6, 8, q1, q3, q5, q7);

    __lsx_vst(p7, output, 0);
    __lsx_vst(p6, output, 16);
    __lsx_vst(p5, output, 32);
    __lsx_vst(p4, output, 48);
    __lsx_vst(p3, output, 64);
    __lsx_vst(p2, output, 80);
    __lsx_vst(p1, output, 96);
    __lsx_vst(p0, output, 112);
    __lsx_vst(q0, output, 128);
    __lsx_vst(q1, output, 144);
    __lsx_vst(q2, output, 160);
    __lsx_vst(q3, output, 176);
    __lsx_vst(q4, output, 192);
    __lsx_vst(q5, output, 208);
    __lsx_vst(q6, output, 224);
    __lsx_vst(q7, output, 240);
}

static void vp9_transpose_8x16_to_16x8(uint8_t *input, uint8_t *output,
                                       ptrdiff_t out_pitch)
{
    __m128i p7_o, p6_o, p5_o, p4_o, p3_o, p2_o, p1_o, p0_o;
    __m128i p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    ptrdiff_t out_pitch2 = out_pitch << 1;
    ptrdiff_t out_pitch3 = out_pitch2 + out_pitch;
    ptrdiff_t out_pitch4 = out_pitch2 << 1;

    DUP4_ARG2(__lsx_vld, input, 0, input, 16, input, 32, input, 48,
              p7, p6, p5, p4);
    DUP4_ARG2(__lsx_vld, input, 64, input, 80, input, 96, input, 112,
              p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vld, input, 128, input, 144, input, 160, input, 176,
              q0, q1, q2, q3);
    DUP4_ARG2(__lsx_vld, input, 192, input, 208, input, 224, input, 240,
              q4, q5, q6, q7);
    LSX_TRANSPOSE16x8_B(p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5,
                        q6, q7, p7_o, p6_o, p5_o, p4_o, p3_o, p2_o, p1_o, p0_o);
    LSX_ST_8(p7_o, p6_o, p5_o, p4_o, p3_o, p2_o, p1_o, p0_o,
             output, out_pitch, out_pitch2, out_pitch3, out_pitch4);
}

static void vp9_transpose_16x16(uint8_t *input, int32_t in_stride,
                                uint8_t *output, int32_t out_stride)
{
    __m128i row0, row1, row2, row3, row4, row5, row6, row7;
    __m128i row8, row9, row10, row11, row12, row13, row14, row15;
    __m128i tmp0, tmp1, tmp4, tmp5, tmp6, tmp7;
    __m128i tmp2, tmp3;
    __m128i p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    int32_t in_stride2 = in_stride << 1;
    int32_t in_stride3 = in_stride2 + in_stride;
    int32_t in_stride4 = in_stride2 << 1;
    int32_t out_stride2 = out_stride << 1;
    int32_t out_stride3 = out_stride2 + out_stride;
    int32_t out_stride4 = out_stride2 << 1;

    LSX_LD_8(input, in_stride, in_stride2, in_stride3, in_stride4,
             row0, row1, row2, row3, row4, row5, row6, row7);
    input += in_stride4;
    LSX_LD_8(input, in_stride, in_stride2, in_stride3, in_stride4,
             row8, row9, row10, row11, row12, row13, row14, row15);

    LSX_TRANSPOSE16x8_B(row0, row1, row2, row3, row4, row5, row6, row7,
                        row8, row9, row10, row11, row12, row13, row14, row15,
                        p7, p6, p5, p4, p3, p2, p1, p0);

    /* transpose 16x8 matrix into 8x16 */
    /* total 8 intermediate register and 32 instructions */
    q7 = __lsx_vpackod_d(row8, row0);
    q6 = __lsx_vpackod_d(row9, row1);
    q5 = __lsx_vpackod_d(row10, row2);
    q4 = __lsx_vpackod_d(row11, row3);
    q3 = __lsx_vpackod_d(row12, row4);
    q2 = __lsx_vpackod_d(row13, row5);
    q1 = __lsx_vpackod_d(row14, row6);
    q0 = __lsx_vpackod_d(row15, row7);

    DUP2_ARG2(__lsx_vpackev_b, q6, q7, q4, q5, tmp0, tmp1);
    DUP2_ARG2(__lsx_vpackod_b, q6, q7, q4, q5, tmp4, tmp5);

    DUP2_ARG2(__lsx_vpackev_b, q2, q3, q0, q1, q5, q7);
    DUP2_ARG2(__lsx_vpackod_b, q2, q3, q0, q1, tmp6, tmp7);

    DUP2_ARG2(__lsx_vpackev_h, tmp1, tmp0, q7, q5, tmp2, tmp3);
    q0 = __lsx_vpackev_w(tmp3, tmp2);
    q4 = __lsx_vpackod_w(tmp3, tmp2);

    tmp2 = __lsx_vpackod_h(tmp1, tmp0);
    tmp3 = __lsx_vpackod_h(q7, q5);
    q2 = __lsx_vpackev_w(tmp3, tmp2);
    q6 = __lsx_vpackod_w(tmp3, tmp2);

    DUP2_ARG2(__lsx_vpackev_h, tmp5, tmp4, tmp7, tmp6, tmp2, tmp3);
    q1 = __lsx_vpackev_w(tmp3, tmp2);
    q5 = __lsx_vpackod_w(tmp3, tmp2);

    tmp2 = __lsx_vpackod_h(tmp5, tmp4);
    tmp3 = __lsx_vpackod_h(tmp7, tmp6);
    q3 = __lsx_vpackev_w(tmp3, tmp2);
    q7 = __lsx_vpackod_w(tmp3, tmp2);

    LSX_ST_8(p7, p6, p5, p4, p3, p2, p1, p0, output, out_stride,
             out_stride2, out_stride3, out_stride4);
    output += out_stride4;
    LSX_ST_8(q0, q1, q2, q3, q4, q5, q6, q7, output, out_stride,
             out_stride2, out_stride3, out_stride4);
}

static int32_t vp9_vt_lpf_t4_and_t8_8w(uint8_t *src, uint8_t *filter48,
                                       uint8_t *src_org, int32_t pitch_org,
                                       int32_t b_limit_ptr,
                                       int32_t limit_ptr,
                                       int32_t thresh_ptr)
{
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i vec0, vec1, vec2, vec3;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements */
    DUP4_ARG2(__lsx_vld, src, -64, src, -48, src, -32, src, -16,
              p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48, q0, q1, q2, q3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    flat = __lsx_vilvl_d(zero, flat);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        DUP2_ARG2(__lsx_vilvl_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);

        src_org -= 2;
        __lsx_vstelm_w(vec2, src_org, 0, 0);
        src_org += pitch_org;
        __lsx_vstelm_w(vec2, src_org, 0, 1);
        src_org += pitch_org;
        __lsx_vstelm_w(vec2, src_org, 0, 2);
        src_org += pitch_org;
        __lsx_vstelm_w(vec2, src_org, 0, 3);
        src_org += pitch_org;
        __lsx_vstelm_w(vec3, src_org, 0, 0);
        src_org += pitch_org;
        __lsx_vstelm_w(vec3, src_org, 0, 1);
        src_org += pitch_org;
        __lsx_vstelm_w(vec3, src_org, 0, 2);
        src_org += pitch_org;
        __lsx_vstelm_w(vec3, src_org, 0, 3);
        return 1;
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);

        /* convert 16 bit output data into 8 bit */
        p2_l = __lsx_vpickev_b(p2_filt8_l, p2_filt8_l);
        p1_l = __lsx_vpickev_b(p1_filt8_l, p1_filt8_l);
        p0_l = __lsx_vpickev_b(p0_filt8_l, p0_filt8_l);
        q0_l = __lsx_vpickev_b(q0_filt8_l, q0_filt8_l);
        q1_l = __lsx_vpickev_b(q1_filt8_l, q1_filt8_l);
        q2_l = __lsx_vpickev_b(q2_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_l, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_l, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_l, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_l, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_l, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_l, flat);

        __lsx_vst(p2_out, filter48, 0);
        __lsx_vst(p1_out, filter48, 16);
        __lsx_vst(p0_out, filter48, 32);
        __lsx_vst(q0_out, filter48, 48);
        __lsx_vst(q1_out, filter48, 64);
        __lsx_vst(q2_out, filter48, 80);
        __lsx_vst(flat, filter48, 96);

        return 0;
    }
}

static int32_t vp9_vt_lpf_t16_8w(uint8_t *dst, uint8_t *dst_org,
                                 ptrdiff_t stride,
                                 uint8_t *filter48)
{
    __m128i zero = __lsx_vldi(0);
    __m128i filter8, flat, flat2;
    __m128i p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    v8u16 p7_l_in, p6_l_in, p5_l_in, p4_l_in;
    v8u16 p3_l_in, p2_l_in, p1_l_in, p0_l_in;
    v8u16 q7_l_in, q6_l_in, q5_l_in, q4_l_in;
    v8u16 q3_l_in, q2_l_in, q1_l_in, q0_l_in;
    v8u16 tmp0_l, tmp1_l;
    __m128i out_l;
    uint8_t *dst_tmp = dst - 128;

    /* load vector elements */
    DUP4_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst_tmp, 32,
              dst_tmp, 48, p7, p6, p5, p4);
    DUP4_ARG2(__lsx_vld, dst_tmp, 64, dst_tmp, 80, dst_tmp, 96,
              dst_tmp, 112, p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vld, dst, 0, dst, 16, dst, 32, dst, 48, q0, q1, q2, q3);
    DUP4_ARG2(__lsx_vld, dst, 64, dst, 80, dst, 96, dst, 112, q4, q5, q6, q7);

    flat = __lsx_vld(filter48, 96);


    VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

    /* if flat2 is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat2)) {
        __m128i vec0, vec1, vec2, vec3, vec4;

        DUP4_ARG2(__lsx_vld, filter48, 0, filter48, 16, filter48, 32,
                  filter48, 48, p2, p1, p0, q0);
        DUP2_ARG2(__lsx_vld, filter48, 64, filter48, 80, q1, q2);

        DUP2_ARG2(__lsx_vilvl_b, p1, p2, q0, p0, vec0, vec1);
        vec3 = __lsx_vilvl_h(vec1, vec0);
        vec4 = __lsx_vilvh_h(vec1, vec0);
        vec2 = __lsx_vilvl_b(q2, q1);

        dst_org -= 3;
        __lsx_vstelm_w(vec3, dst_org, 0, 0);
        __lsx_vstelm_h(vec2, dst_org, 4, 0);
        dst_org += stride;
        __lsx_vstelm_w(vec3, dst_org, 0, 1);
        __lsx_vstelm_h(vec2, dst_org, 4, 1);
        dst_org += stride;
        __lsx_vstelm_w(vec3, dst_org, 0, 2);
        __lsx_vstelm_h(vec2, dst_org, 4, 2);
        dst_org += stride;
        __lsx_vstelm_w(vec3, dst_org, 0, 3);
        __lsx_vstelm_h(vec2, dst_org, 4, 3);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 0);
        __lsx_vstelm_h(vec2, dst_org, 4, 4);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 1);
        __lsx_vstelm_h(vec2, dst_org, 4, 5);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 2);
        __lsx_vstelm_h(vec2, dst_org, 4, 6);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 3);
        __lsx_vstelm_h(vec2, dst_org, 4, 7);
        return 1;
    } else {
        dst -= 7 * 16;

        p7_l_in = (v8u16)__lsx_vilvl_b(zero, p7);
        p6_l_in = (v8u16)__lsx_vilvl_b(zero, p6);
        p5_l_in = (v8u16)__lsx_vilvl_b(zero, p5);
        p4_l_in = (v8u16)__lsx_vilvl_b(zero, p4);
        p3_l_in = (v8u16)__lsx_vilvl_b(zero, p3);
        p2_l_in = (v8u16)__lsx_vilvl_b(zero, p2);
        p1_l_in = (v8u16)__lsx_vilvl_b(zero, p1);
        p0_l_in = (v8u16)__lsx_vilvl_b(zero, p0);
        q0_l_in = (v8u16)__lsx_vilvl_b(zero, q0);

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

        out_l =__lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l =__lsx_vpickev_b(out_l, out_l);
        p6 = __lsx_vbitsel_v(p6, out_l, flat2);
        __lsx_vstelm_d(p6, dst, 0, 0);
        dst += 16;

        /* p5 */
        q1_l_in = (v8u16)__lsx_vilvl_b(zero, q1);
        tmp0_l = p5_l_in - p6_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        p5 = __lsx_vbitsel_v(p5, out_l, flat2);
        __lsx_vstelm_d(p5, dst, 0, 0);
        dst += 16;

        /* p4 */
        q2_l_in = (v8u16)__lsx_vilvl_b(zero, q2);
        tmp0_l = p4_l_in - p5_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        p4 = __lsx_vbitsel_v(p4, out_l, flat2);
        __lsx_vstelm_d(p4, dst, 0, 0);
        dst += 16;

        /* p3 */
        q3_l_in = (v8u16)__lsx_vilvl_b(zero, q3);
        tmp0_l = p3_l_in - p4_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        p3 = __lsx_vbitsel_v(p3, out_l, flat2);
        __lsx_vstelm_d(p3, dst, 0, 0);
        dst += 16;

        /* p2 */
        q4_l_in = (v8u16)__lsx_vilvl_b(zero, q4);
        filter8 = __lsx_vld(filter48, 0);
        tmp0_l = p2_l_in - p3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vstelm_d(filter8, dst, 0, 0);
        dst += 16;

        /* p1 */
        q5_l_in = (v8u16)__lsx_vilvl_b(zero, q5);
        filter8 = __lsx_vld(filter48, 16);
        tmp0_l = p1_l_in - p2_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vstelm_d(filter8, dst, 0, 0);
        dst += 16;

        /* p0 */
        q6_l_in = (v8u16)__lsx_vilvl_b(zero, q6);
        filter8 = __lsx_vld(filter48, 32);
        tmp0_l = p0_l_in - p1_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vstelm_d(filter8, dst, 0, 0);
        dst += 16;

        /* q0 */
        q7_l_in = (v8u16)__lsx_vilvl_b(zero, q7);
        filter8 = __lsx_vld(filter48, 48);
        tmp0_l = q7_l_in - p0_l_in;
        tmp0_l += q0_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((v8i16) tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vstelm_d(filter8, dst, 0, 0);
        dst += 16;

        /* q1 */
        filter8 = __lsx_vld(filter48, 64);
        tmp0_l = q7_l_in - q0_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p6_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vstelm_d(filter8, dst, 0, 0);
        dst += 16;

        /* q2 */
        filter8 = __lsx_vld(filter48, 80);
        tmp0_l = q7_l_in - q1_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p5_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vstelm_d(filter8, dst, 0, 0);
        dst += 16;

        /* q3 */
        tmp0_l = q7_l_in - q2_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p4_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        q3 = __lsx_vbitsel_v(q3, out_l, flat2);
        __lsx_vstelm_d(q3, dst, 0, 0);
        dst += 16;

        /* q4 */
        tmp0_l = q7_l_in - q3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p3_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        q4 = __lsx_vbitsel_v(q4, out_l, flat2);
        __lsx_vstelm_d(q4, dst, 0, 0);
        dst += 16;

        /* q5 */
        tmp0_l = q7_l_in - q4_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p2_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        q5 = __lsx_vbitsel_v(q5, out_l, flat2);
        __lsx_vstelm_d(q5, dst, 0, 0);
        dst += 16;

        /* q6 */
        tmp0_l = q7_l_in - q5_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p1_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        out_l = __lsx_vpickev_b(out_l, out_l);
        q6 = __lsx_vbitsel_v(q6, out_l, flat2);
        __lsx_vstelm_d(q6, dst, 0, 0);

        return 0;
    }
}

void ff_loop_filter_h_16_8_lsx(uint8_t *dst, ptrdiff_t stride,
                               int32_t b_limit_ptr,
                               int32_t limit_ptr,
                               int32_t thresh_ptr)
{
    uint8_t early_exit = 0;
    uint8_t transposed_input[16 * 24] __attribute__ ((aligned(16)));
    uint8_t *filter48 = &transposed_input[16 * 16];

    vp9_transpose_16x8_to_8x16(dst - 8, stride, transposed_input);

    early_exit = vp9_vt_lpf_t4_and_t8_8w((transposed_input + 16 * 8),
                                         &filter48[0], dst, stride,
                                         b_limit_ptr, limit_ptr, thresh_ptr);

    if (0 == early_exit) {
        early_exit = vp9_vt_lpf_t16_8w((transposed_input + 16 * 8), dst, stride,
                                       &filter48[0]);

        if (0 == early_exit) {
            vp9_transpose_8x16_to_16x8(transposed_input, dst - 8, stride);
        }
    }
}

static int32_t vp9_vt_lpf_t4_and_t8_16w(uint8_t *dst, uint8_t *filter48,
                                        uint8_t *dst_org, ptrdiff_t stride,
                                        int32_t b_limit_ptr,
                                        int32_t limit_ptr,
                                        int32_t thresh_ptr)
{
    ptrdiff_t stride2 = stride << 1;
    ptrdiff_t stride3 = stride2 + stride;
    ptrdiff_t stride4 = stride2 << 1;
    __m128i p3, p2, p1, p0, q3, q2, q1, q0;
    __m128i p2_out, p1_out, p0_out, q0_out, q1_out, q2_out;
    __m128i flat, mask, hev, thresh, b_limit, limit;
    __m128i p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l;
    __m128i p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h;
    __m128i p2_filt8_l, p1_filt8_l, p0_filt8_l;
    __m128i q0_filt8_l, q1_filt8_l, q2_filt8_l;
    __m128i p2_filt8_h, p1_filt8_h, p0_filt8_h;
    __m128i q0_filt8_h, q1_filt8_h, q2_filt8_h;
    __m128i vec0, vec1, vec2, vec3, vec4, vec5;
    __m128i zero = __lsx_vldi(0);

    /* load vector elements */
    DUP4_ARG2(__lsx_vld, dst, -64, dst, -48, dst, -32, dst, -16,
              p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vld, dst, 0, dst, 16, dst, 32, dst, 48, q0, q1, q2, q3);

    thresh  = __lsx_vreplgr2vr_b(thresh_ptr);
    b_limit = __lsx_vreplgr2vr_b(b_limit_ptr);
    limit   = __lsx_vreplgr2vr_b(limit_ptr);

    /* mask and hev */
    LPF_MASK_HEV(p3, p2, p1, p0, q0, q1, q2, q3, limit, b_limit, thresh,
                 hev, mask, flat);
    /* flat4 */
    VP9_FLAT4(p3, p2, p0, q0, q2, q3, flat);
    /* filter4 */
    VP9_LPF_FILTER4_4W(p1, p0, q0, q1, mask, hev, p1_out, p0_out, q0_out,
                       q1_out);

    /* if flat is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat)) {
        DUP2_ARG2(__lsx_vilvl_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec2 = __lsx_vilvl_h(vec1, vec0);
        vec3 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p0_out, p1_out, q1_out, q0_out, vec0, vec1);
        vec4 = __lsx_vilvl_h(vec1, vec0);
        vec5 = __lsx_vilvh_h(vec1, vec0);

        dst_org -= 2;
        __lsx_vstelm_w(vec2, dst_org, 0, 0);
        __lsx_vstelm_w(vec2, dst_org + stride, 0, 1);
        __lsx_vstelm_w(vec2, dst_org + stride2, 0, 2);
        __lsx_vstelm_w(vec2, dst_org + stride3, 0, 3);
        dst_org += stride4;
        __lsx_vstelm_w(vec3, dst_org, 0, 0);
        __lsx_vstelm_w(vec3, dst_org + stride, 0, 1);
        __lsx_vstelm_w(vec3, dst_org + stride2, 0, 2);
        __lsx_vstelm_w(vec3, dst_org + stride3, 0, 3);
        dst_org += stride4;
        __lsx_vstelm_w(vec4, dst_org, 0, 0);
        __lsx_vstelm_w(vec4, dst_org + stride, 0, 1);
        __lsx_vstelm_w(vec4, dst_org + stride2, 0, 2);
        __lsx_vstelm_w(vec4, dst_org + stride3, 0, 3);
        dst_org += stride4;
        __lsx_vstelm_w(vec5, dst_org, 0, 0);
        __lsx_vstelm_w(vec5, dst_org + stride, 0, 1);
        __lsx_vstelm_w(vec5, dst_org + stride2, 0, 2);
        __lsx_vstelm_w(vec5, dst_org + stride3, 0, 3);

        return 1;
    } else {
        DUP4_ARG2(__lsx_vilvl_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_l, p2_l, p1_l, p0_l);
        DUP4_ARG2(__lsx_vilvl_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_l, q1_l, q2_l, q3_l);
        VP9_FILTER8(p3_l, p2_l, p1_l, p0_l, q0_l, q1_l, q2_l, q3_l, p2_filt8_l,
                    p1_filt8_l, p0_filt8_l, q0_filt8_l, q1_filt8_l, q2_filt8_l);
        DUP4_ARG2(__lsx_vilvh_b, zero, p3, zero, p2, zero, p1, zero, p0,
                  p3_h, p2_h, p1_h, p0_h);
        DUP4_ARG2(__lsx_vilvh_b, zero, q0, zero, q1, zero, q2, zero, q3,
                  q0_h, q1_h, q2_h, q3_h);
        VP9_FILTER8(p3_h, p2_h, p1_h, p0_h, q0_h, q1_h, q2_h, q3_h, p2_filt8_h,
                    p1_filt8_h, p0_filt8_h, q0_filt8_h, q1_filt8_h, q2_filt8_h);

        /* convert 16 bit output data into 8 bit */
        DUP4_ARG2(__lsx_vpickev_b, p2_filt8_h, p2_filt8_l, p1_filt8_h,
                      p1_filt8_l, p0_filt8_h, p0_filt8_l, q0_filt8_h,
                      q0_filt8_l, p2_filt8_l, p1_filt8_l, p0_filt8_l,
                      q0_filt8_l);
        DUP2_ARG2(__lsx_vpickev_b, q1_filt8_h, q1_filt8_l, q2_filt8_h,
                  q2_filt8_l, q1_filt8_l, q2_filt8_l);

        /* store pixel values */
        p2_out = __lsx_vbitsel_v(p2, p2_filt8_l, flat);
        p1_out = __lsx_vbitsel_v(p1_out, p1_filt8_l, flat);
        p0_out = __lsx_vbitsel_v(p0_out, p0_filt8_l, flat);
        q0_out = __lsx_vbitsel_v(q0_out, q0_filt8_l, flat);
        q1_out = __lsx_vbitsel_v(q1_out, q1_filt8_l, flat);
        q2_out = __lsx_vbitsel_v(q2, q2_filt8_l, flat);

        __lsx_vst(p2_out, filter48, 0);
        __lsx_vst(p1_out, filter48, 16);
        __lsx_vst(p0_out, filter48, 32);
        __lsx_vst(q0_out, filter48, 48);
        __lsx_vst(q1_out, filter48, 64);
        __lsx_vst(q2_out, filter48, 80);
        __lsx_vst(flat, filter48, 96);

        return 0;
    }
}

static int32_t vp9_vt_lpf_t16_16w(uint8_t *dst, uint8_t *dst_org,
                                  ptrdiff_t stride,
                                  uint8_t *filter48)
{
    __m128i zero = __lsx_vldi(0);
    __m128i flat, flat2, filter8;
    __m128i p7, p6, p5, p4, p3, p2, p1, p0, q0, q1, q2, q3, q4, q5, q6, q7;
    v8u16 p7_l_in, p6_l_in, p5_l_in, p4_l_in;
    v8u16 p3_l_in, p2_l_in, p1_l_in, p0_l_in;
    v8u16 q7_l_in, q6_l_in, q5_l_in, q4_l_in;
    v8u16 q3_l_in, q2_l_in, q1_l_in, q0_l_in;
    v8u16 p7_h_in, p6_h_in, p5_h_in, p4_h_in;
    v8u16 p3_h_in, p2_h_in, p1_h_in, p0_h_in;
    v8u16 q7_h_in, q6_h_in, q5_h_in, q4_h_in;
    v8u16 q3_h_in, q2_h_in, q1_h_in, q0_h_in;
    v8u16 tmp0_l, tmp1_l, tmp0_h, tmp1_h;
    __m128i out_l, out_h;
    uint8_t *dst_tmp = dst - 128;

    flat = __lsx_vld(filter48, 96);

    DUP4_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst_tmp, 32,
              dst_tmp, 48, p7, p6, p5, p4);
    DUP4_ARG2(__lsx_vld, dst_tmp, 64, dst_tmp, 80, dst_tmp, 96,
              dst_tmp, 112, p3, p2, p1, p0);
    DUP4_ARG2(__lsx_vld, dst, 0, dst, 16, dst, 32, dst, 48, q0, q1, q2, q3);
    DUP4_ARG2(__lsx_vld, dst, 64, dst, 80, dst, 96, dst, 112, q4, q5, q6, q7);

    VP9_FLAT5(p7, p6, p5, p4, p0, q0, q4, q5, q6, q7, flat, flat2);

    /* if flat2 is zero for all pixels, then no need to calculate other filter */
    if (__lsx_bz_v(flat2)) {
        __m128i vec0, vec1, vec2, vec3, vec4, vec5, vec6, vec7;

        DUP4_ARG2(__lsx_vld, filter48, 0, filter48, 16, filter48, 32,
                  filter48, 48, p2, p1, p0, q0);
        DUP2_ARG2(__lsx_vld, filter48, 64, filter48, 80, q1, q2);

        DUP2_ARG2(__lsx_vilvl_b, p1, p2, q0, p0, vec0, vec1);
        vec3 = __lsx_vilvl_h(vec1, vec0);
        vec4 = __lsx_vilvh_h(vec1, vec0);
        DUP2_ARG2(__lsx_vilvh_b, p1, p2, q0, p0, vec0, vec1);
        vec6 = __lsx_vilvl_h(vec1, vec0);
        vec7 = __lsx_vilvh_h(vec1, vec0);
        vec2 = __lsx_vilvl_b(q2, q1);
        vec5 = __lsx_vilvh_b(q2, q1);

        dst_org -= 3;
        __lsx_vstelm_w(vec3, dst_org, 0, 0);
        __lsx_vstelm_h(vec2, dst_org, 4, 0);
        dst_org += stride;
        __lsx_vstelm_w(vec3, dst_org, 0, 1);
        __lsx_vstelm_h(vec2, dst_org, 4, 1);
        dst_org += stride;
        __lsx_vstelm_w(vec3, dst_org, 0, 2);
        __lsx_vstelm_h(vec2, dst_org, 4, 2);
        dst_org += stride;
        __lsx_vstelm_w(vec3, dst_org, 0, 3);
        __lsx_vstelm_h(vec2, dst_org, 4, 3);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 0);
        __lsx_vstelm_h(vec2, dst_org, 4, 4);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 1);
        __lsx_vstelm_h(vec2, dst_org, 4, 5);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 2);
        __lsx_vstelm_h(vec2, dst_org, 4, 6);
        dst_org += stride;
        __lsx_vstelm_w(vec4, dst_org, 0, 3);
        __lsx_vstelm_h(vec2, dst_org, 4, 7);
        dst_org += stride;
        __lsx_vstelm_w(vec6, dst_org, 0, 0);
        __lsx_vstelm_h(vec5, dst_org, 4, 0);
        dst_org += stride;
        __lsx_vstelm_w(vec6, dst_org, 0, 1);
        __lsx_vstelm_h(vec5, dst_org, 4, 1);
        dst_org += stride;
        __lsx_vstelm_w(vec6, dst_org, 0, 2);
        __lsx_vstelm_h(vec5, dst_org, 4, 2);
        dst_org += stride;
        __lsx_vstelm_w(vec6, dst_org, 0, 3);
        __lsx_vstelm_h(vec5, dst_org, 4, 3);
        dst_org += stride;
        __lsx_vstelm_w(vec7, dst_org, 0, 0);
        __lsx_vstelm_h(vec5, dst_org, 4, 4);
        dst_org += stride;
        __lsx_vstelm_w(vec7, dst_org, 0, 1);
        __lsx_vstelm_h(vec5, dst_org, 4, 5);
        dst_org += stride;
        __lsx_vstelm_w(vec7, dst_org, 0, 2);
        __lsx_vstelm_h(vec5, dst_org, 4, 6);
        dst_org += stride;
        __lsx_vstelm_w(vec7, dst_org, 0, 3);
        __lsx_vstelm_h(vec5, dst_org, 4, 7);

        return 1;
    } else {
        dst -= 7 * 16;

        p7_l_in = (v8u16)__lsx_vilvl_b(zero, p7);
        p6_l_in = (v8u16)__lsx_vilvl_b(zero, p6);
        p5_l_in = (v8u16)__lsx_vilvl_b(zero, p5);
        p4_l_in = (v8u16)__lsx_vilvl_b(zero, p4);
        p3_l_in = (v8u16)__lsx_vilvl_b(zero, p3);
        p2_l_in = (v8u16)__lsx_vilvl_b(zero, p2);
        p1_l_in = (v8u16)__lsx_vilvl_b(zero, p1);
        p0_l_in = (v8u16)__lsx_vilvl_b(zero, p0);
        q0_l_in = (v8u16)__lsx_vilvl_b(zero, q0);

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
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);

        p7_h_in = (v8u16)__lsx_vilvh_b(zero, p7);
        p6_h_in = (v8u16)__lsx_vilvh_b(zero, p6);
        p5_h_in = (v8u16)__lsx_vilvh_b(zero, p5);
        p4_h_in = (v8u16)__lsx_vilvh_b(zero, p4);
        p3_h_in = (v8u16)__lsx_vilvh_b(zero, p3);
        p2_h_in = (v8u16)__lsx_vilvh_b(zero, p2);
        p1_h_in = (v8u16)__lsx_vilvh_b(zero, p1);
        p0_h_in = (v8u16)__lsx_vilvh_b(zero, p0);
        q0_h_in = (v8u16)__lsx_vilvh_b(zero, q0);

        tmp0_h = p7_h_in << 3;
        tmp0_h -= p7_h_in;
        tmp0_h += p6_h_in;
        tmp0_h += q0_h_in;
        tmp1_h = p6_h_in + p5_h_in;
        tmp1_h += p4_h_in;
        tmp1_h += p3_h_in;
        tmp1_h += p2_h_in;
        tmp1_h += p1_h_in;
        tmp1_h += p0_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);

        out_l = __lsx_vpickev_b(out_h, out_l);
        p6 = __lsx_vbitsel_v(p6, out_l, flat2);
        __lsx_vst(p6, dst, 0);

        /* p5 */
        q1_l_in = (v8u16)__lsx_vilvl_b(zero, q1);
        tmp0_l = p5_l_in - p6_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q1_h_in = (v8u16)__lsx_vilvh_b(zero, q1);
        tmp0_h = p5_h_in - p6_h_in;
        tmp0_h += q1_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        p5 = __lsx_vbitsel_v(p5, out_l, flat2);
        __lsx_vst(p5, dst, 16);

        /* p4 */
        q2_l_in = (v8u16)__lsx_vilvl_b(zero, q2);
        tmp0_l = p4_l_in - p5_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q2_h_in = (v8u16)__lsx_vilvh_b(zero, q2);
        tmp0_h = p4_h_in - p5_h_in;
        tmp0_h += q2_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        p4 = __lsx_vbitsel_v(p4, out_l, flat2);
        __lsx_vst(p4, dst, 16*2);

        /* p3 */
        q3_l_in = (v8u16)__lsx_vilvl_b(zero, q3);
        tmp0_l = p3_l_in - p4_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q3_h_in = (v8u16)__lsx_vilvh_b(zero, q3);
        tmp0_h = p3_h_in - p4_h_in;
        tmp0_h += q3_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        p3 = __lsx_vbitsel_v(p3, out_l, flat2);
        __lsx_vst(p3, dst, 16*3);

        /* p2 */
        q4_l_in = (v8u16)__lsx_vilvl_b(zero, q4);
        filter8 = __lsx_vld(filter48, 0);
        tmp0_l = p2_l_in - p3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q4_h_in = (v8u16)__lsx_vilvh_b(zero, q4);
        tmp0_h = p2_h_in - p3_h_in;
        tmp0_h += q4_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 16*4);

        /* p1 */
        q5_l_in = (v8u16)__lsx_vilvl_b(zero, q5);
        filter8 = __lsx_vld(filter48, 16);
        tmp0_l = p1_l_in - p2_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q5_h_in = (v8u16)__lsx_vilvh_b(zero, q5);
        tmp0_h = p1_h_in - p2_h_in;
        tmp0_h += q5_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)(tmp1_h), 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 16*5);

        /* p0 */
        q6_l_in = (v8u16)__lsx_vilvl_b(zero, q6);
        filter8 = __lsx_vld(filter48, 32);
        tmp0_l = p0_l_in - p1_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q6_h_in = (v8u16)__lsx_vilvh_b(zero, q6);
        tmp0_h = p0_h_in - p1_h_in;
        tmp0_h += q6_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 16*6);

        /* q0 */
        q7_l_in = (v8u16)__lsx_vilvl_b(zero, q7);
        filter8 = __lsx_vld(filter48, 48);
        tmp0_l = q7_l_in - p0_l_in;
        tmp0_l += q0_l_in;
        tmp0_l -= p7_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        q7_h_in = (v8u16)__lsx_vilvh_b(zero, q7);
        tmp0_h = q7_h_in - p0_h_in;
        tmp0_h += q0_h_in;
        tmp0_h -= p7_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 16*7);

        /* q1 */
        filter8 = __lsx_vld(filter48, 64);
        tmp0_l = q7_l_in - q0_l_in;
        tmp0_l += q1_l_in;
        tmp0_l -= p6_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        tmp0_h = q7_h_in - q0_h_in;
        tmp0_h += q1_h_in;
        tmp0_h -= p6_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 16*8);

        /* q2 */
        filter8 = __lsx_vld(filter48, 80);
        tmp0_l = q7_l_in - q1_l_in;
        tmp0_l += q2_l_in;
        tmp0_l -= p5_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        tmp0_h = q7_h_in - q1_h_in;
        tmp0_h += q2_h_in;
        tmp0_h -= p5_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        filter8 = __lsx_vbitsel_v(filter8, out_l, flat2);
        __lsx_vst(filter8, dst, 16*9);

        /* q3 */
        tmp0_l = q7_l_in - q2_l_in;
        tmp0_l += q3_l_in;
        tmp0_l -= p4_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        tmp0_h = q7_h_in - q2_h_in;
        tmp0_h += q3_h_in;
        tmp0_h -= p4_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        q3 = __lsx_vbitsel_v(q3, out_l, flat2);
        __lsx_vst(q3, dst, 16*10);

        /* q4 */
        tmp0_l = q7_l_in - q3_l_in;
        tmp0_l += q4_l_in;
        tmp0_l -= p3_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        tmp0_h = q7_h_in - q3_h_in;
        tmp0_h += q4_h_in;
        tmp0_h -= p3_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        q4 = __lsx_vbitsel_v(q4, out_l, flat2);
        __lsx_vst(q4, dst, 16*11);

        /* q5 */
        tmp0_l = q7_l_in - q4_l_in;
        tmp0_l += q5_l_in;
        tmp0_l -= p2_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        tmp0_h = q7_h_in - q4_h_in;
        tmp0_h += q5_h_in;
        tmp0_h -= p2_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        q5 = __lsx_vbitsel_v(q5, out_l, flat2);
        __lsx_vst(q5, dst, 16*12);

        /* q6 */
        tmp0_l = q7_l_in - q5_l_in;
        tmp0_l += q6_l_in;
        tmp0_l -= p1_l_in;
        tmp1_l += tmp0_l;
        out_l = __lsx_vsrari_h((__m128i)tmp1_l, 4);
        tmp0_h = q7_h_in - q5_h_in;
        tmp0_h += q6_h_in;
        tmp0_h -= p1_h_in;
        tmp1_h += tmp0_h;
        out_h = __lsx_vsrari_h((__m128i)tmp1_h, 4);
        out_l = __lsx_vpickev_b(out_h, out_l);
        q6 = __lsx_vbitsel_v(q6, out_l, flat2);
        __lsx_vst(q6, dst, 16*13);

        return 0;
    }
}

void ff_loop_filter_h_16_16_lsx(uint8_t *dst, ptrdiff_t stride,
                                int32_t b_limit_ptr,
                                int32_t limit_ptr,
                                int32_t thresh_ptr)
{
    uint8_t early_exit = 0;
    uint8_t transposed_input[16 * 24] __attribute__ ((aligned(16)));
    uint8_t *filter48 = &transposed_input[16 * 16];

    vp9_transpose_16x16((dst - 8), stride, &transposed_input[0], 16);

    early_exit = vp9_vt_lpf_t4_and_t8_16w((transposed_input + 16 * 8),
                                          &filter48[0], dst, stride,
                                          b_limit_ptr, limit_ptr, thresh_ptr);

    if (0 == early_exit) {
        early_exit = vp9_vt_lpf_t16_16w((transposed_input + 16 * 8), dst,
                                         stride, &filter48[0]);

        if (0 == early_exit) {
            vp9_transpose_16x16(transposed_input, 16, (dst - 8), stride);
        }
    }
}
