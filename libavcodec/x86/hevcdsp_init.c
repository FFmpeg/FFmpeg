/*
 * Copyright (c) 2013 Seppo Tomperi
 * Copyright (c) 2013 - 2014 Pierre-Edouard Lepere
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"

#include "libavcodec/hevcdsp.h"

#define LFC_FUNC(DIR, DEPTH, OPT) \
void ff_hevc_ ## DIR ## _loop_filter_chroma_ ## DEPTH ## _ ## OPT(uint8_t *pix, ptrdiff_t stride, int *tc, uint8_t *no_p, uint8_t *no_q);

#define LFL_FUNC(DIR, DEPTH, OPT) \
void ff_hevc_ ## DIR ## _loop_filter_luma_ ## DEPTH ## _ ## OPT(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q);

#define LFC_FUNCS(type, depth) \
    LFC_FUNC(h, depth, sse2)   \
    LFC_FUNC(v, depth, sse2)

#define LFL_FUNCS(type, depth) \
    LFL_FUNC(h, depth, ssse3)  \
    LFL_FUNC(v, depth, ssse3)

LFC_FUNCS(uint8_t, 8)
LFC_FUNCS(uint8_t, 10)
LFL_FUNCS(uint8_t, 8)
LFL_FUNCS(uint8_t, 10)

#define idct_dc_proto(size, bitd, opt) \
                void ff_hevc_idct_ ## size ## _dc_add_ ## bitd ## _ ## opt(uint8_t *dst, int16_t *coeffs, ptrdiff_t stride)

idct_dc_proto(4, 8,mmxext);
idct_dc_proto(8, 8,mmxext);
idct_dc_proto(16,8,  sse2);
idct_dc_proto(32,8,  sse2);

idct_dc_proto(32,8,  avx2);

idct_dc_proto(4, 10,mmxext);
idct_dc_proto(8, 10,  sse2);
idct_dc_proto(16,10,  sse2);
idct_dc_proto(32,10,  sse2);
idct_dc_proto(8, 10,   avx);
idct_dc_proto(16,10,   avx);
idct_dc_proto(32,10,   avx);

idct_dc_proto(16,10,  avx2);
idct_dc_proto(32,10,  avx2);

#define IDCT_FUNCS(W, opt) \
void ff_hevc_idct_ ## W ## _dc_8_ ## opt(int16_t *coeffs); \
void ff_hevc_idct_ ## W ## _dc_10_ ## opt(int16_t *coeffs)

IDCT_FUNCS(4x4,   mmxext);
IDCT_FUNCS(8x8,   mmxext);
IDCT_FUNCS(8x8,   sse2);
IDCT_FUNCS(16x16, sse2);
IDCT_FUNCS(32x32, sse2);
IDCT_FUNCS(16x16, avx2);
IDCT_FUNCS(32x32, avx2);

#define GET_PIXELS(width, depth, cf)                                                                      \
void ff_hevc_get_pixels_ ## width ## _ ## depth ## _ ## cf(int16_t *dst, ptrdiff_t dststride,             \
                                                           uint8_t *src, ptrdiff_t srcstride,             \
                                                           int height, int mx, int my, int16_t *mcbuffer);

GET_PIXELS(4,  8, sse2)
GET_PIXELS(8,  8, sse2)
GET_PIXELS(12, 8, sse2)
GET_PIXELS(16, 8, sse2)
GET_PIXELS(24, 8, sse2)
GET_PIXELS(32, 8, sse2)
GET_PIXELS(48, 8, sse2)
GET_PIXELS(64, 8, sse2)

GET_PIXELS(4,  10, sse2)
GET_PIXELS(8,  10, sse2)
GET_PIXELS(12, 10, sse2)
GET_PIXELS(16, 10, sse2)
GET_PIXELS(24, 10, sse2)
GET_PIXELS(32, 10, sse2)
GET_PIXELS(48, 10, sse2)
GET_PIXELS(64, 10, sse2)

/* those are independent of the bit depth, so declared separately */
#define INTERP_HV_FUNC(width, cf)                                                         \
void ff_hevc_qpel_hv_ ## width ## _ ## cf(int16_t *dst, ptrdiff_t dststride,              \
                                          int16_t *src, ptrdiff_t srcstride,              \
                                          int height, int mx, int my, int16_t *mcbuffer); \
void ff_hevc_epel_hv_ ## width ## _ ## cf(int16_t *dst, ptrdiff_t dststride,              \
                                          int16_t *src, ptrdiff_t srcstride,              \
                                          int height, int mx, int my, int16_t *mcbuffer);

INTERP_HV_FUNC(4,  avx)
INTERP_HV_FUNC(8,  avx)
INTERP_HV_FUNC(12, avx)
INTERP_HV_FUNC(16, avx)
INTERP_HV_FUNC(24, avx)
INTERP_HV_FUNC(32, avx)
INTERP_HV_FUNC(48, avx)
INTERP_HV_FUNC(64, avx)

#if ARCH_X86_64 && HAVE_AVX_EXTERNAL
#define QPEL_FUNC_HV(width, depth, cf_h, cf_v, cf_hv)                                                         \
static void hevc_qpel_hv_ ## width ## _ ## depth ## _ ## cf_hv(int16_t *dst, ptrdiff_t dststride,             \
                                                               uint8_t *src, ptrdiff_t srcstride,             \
                                                               int height, int mx, int my, int16_t *mcbuffer) \
{                                                                                                             \
    const ptrdiff_t stride = FFALIGN(width + 7, 8);                                                           \
    ff_hevc_qpel_h_ ## width ## _ ## depth ## _ ## cf_h(mcbuffer, 2 * stride, src - 3 * srcstride, srcstride, \
                                                        height + 7, mx, my, mcbuffer);                        \
    ff_hevc_qpel_hv_ ## width ## _ ## cf_hv(dst, dststride, mcbuffer + 3 * stride, 2 * stride,                \
                                            height, mx, my, mcbuffer);                                        \
}
#else
#define QPEL_FUNC_HV(width, depth, cf_h, cf_v, cf_hv)
#endif /* ARCH_X86_64 && HAVE_AVX_EXTERNAL */

#define QPEL_FUNCS(width, depth, cf_h, cf_v, cf_hv)                                                           \
void ff_hevc_qpel_h_ ## width ## _ ## depth ## _ ## cf_h(int16_t *dst, ptrdiff_t dststride,                   \
                                                         uint8_t *src, ptrdiff_t srcstride,                   \
                                                         int height, int mx, int my, int16_t *mcbuffer);      \
void ff_hevc_qpel_v_ ## width ## _ ## depth ## _ ## cf_v(int16_t *dst, ptrdiff_t dststride,                   \
                                                         uint8_t *src, ptrdiff_t srcstride,                   \
                                                         int height, int mx, int my, int16_t *mcbuffer);      \
QPEL_FUNC_HV(width, depth, cf_h, cf_v, cf_hv)

QPEL_FUNCS(4,  8, ssse3, ssse3, avx)
QPEL_FUNCS(8,  8, ssse3, ssse3, avx)
QPEL_FUNCS(12, 8, ssse3, ssse3, avx)
QPEL_FUNCS(16, 8, ssse3, ssse3, avx)
QPEL_FUNCS(24, 8, ssse3, ssse3, avx)
QPEL_FUNCS(32, 8, ssse3, ssse3, avx)
QPEL_FUNCS(48, 8, ssse3, ssse3, avx)
QPEL_FUNCS(64, 8, ssse3, ssse3, avx)

QPEL_FUNCS(4,  10, avx, avx, avx)
QPEL_FUNCS(8,  10, avx, avx, avx)
QPEL_FUNCS(12, 10, avx, avx, avx)
QPEL_FUNCS(16, 10, avx, avx, avx)
QPEL_FUNCS(24, 10, avx, avx, avx)
QPEL_FUNCS(32, 10, avx, avx, avx)
QPEL_FUNCS(48, 10, avx, avx, avx)
QPEL_FUNCS(64, 10, avx, avx, avx)

#if ARCH_X86_64 && HAVE_AVX_EXTERNAL
#define EPEL_FUNC_HV(width, depth, cf_h, cf_v, cf_hv)                                                         \
static void hevc_epel_hv_ ## width ## _ ## depth ## _ ## cf_hv(int16_t *dst, ptrdiff_t dststride,             \
                                                               uint8_t *src, ptrdiff_t srcstride,             \
                                                               int height, int mx, int my, int16_t *mcbuffer) \
{                                                                                                             \
    const ptrdiff_t stride = FFALIGN(width + 3, 8);                                                           \
    ff_hevc_epel_h_ ## width ## _ ## depth ## _ ## cf_h(mcbuffer, 2 * stride, src - srcstride, srcstride,     \
                                                        height + 3, mx, my, mcbuffer);                        \
    ff_hevc_epel_hv_ ## width ## _ ## cf_hv(dst, dststride, mcbuffer + stride, 2 * stride,                    \
                                            height, mx, my, mcbuffer);                                        \
}
#else
#define EPEL_FUNC_HV(width, depth, cf_h, cf_v, cf_hv)
#endif /* ARCH_X86_64 && HAVE_AVX_EXTERNAL */

#define EPEL_FUNCS(width, depth, cf_h, cf_v, cf_hv)                                                           \
void ff_hevc_epel_h_ ## width ## _ ## depth ## _ ## cf_h(int16_t *dst, ptrdiff_t dststride,                   \
                                                         uint8_t *src, ptrdiff_t srcstride,                   \
                                                         int height, int mx, int my, int16_t *mcbuffer);      \
void ff_hevc_epel_v_ ## width ## _ ## depth ## _ ## cf_v(int16_t *dst, ptrdiff_t dststride,                   \
                                                         uint8_t *src, ptrdiff_t srcstride,                   \
                                                         int height, int mx, int my, int16_t *mcbuffer);      \
EPEL_FUNC_HV(width, depth, cf_h, cf_v, cf_hv)

EPEL_FUNCS(4,  8, ssse3, ssse3, avx)
EPEL_FUNCS(8,  8, ssse3, ssse3, avx)
EPEL_FUNCS(12, 8, ssse3, ssse3, avx)
EPEL_FUNCS(16, 8, ssse3, ssse3, avx)
EPEL_FUNCS(24, 8, ssse3, ssse3, avx)
EPEL_FUNCS(32, 8, ssse3, ssse3, avx)

EPEL_FUNCS(4,  10, avx, avx, avx)
EPEL_FUNCS(8,  10, avx, avx, avx)
EPEL_FUNCS(12, 10, avx, avx, avx)
EPEL_FUNCS(16, 10, avx, avx, avx)
EPEL_FUNCS(24, 10, avx, avx, avx)
EPEL_FUNCS(32, 10, avx, avx, avx)

#define PUT_PRED(width, depth, cf_uw, cf_w) \
void ff_hevc_put_unweighted_pred_ ## width ## _ ## depth ## _ ## cf_uw(uint8_t *dst, ptrdiff_t dststride,                   \
                                                                       int16_t *src, ptrdiff_t srcstride,                   \
                                                                       int height);                                         \
void ff_hevc_put_unweighted_pred_avg_ ## width ## _ ## depth ## _ ## cf_uw(uint8_t *dst, ptrdiff_t dststride,               \
                                                                           int16_t *src1, int16_t *src2,                    \
                                                                           ptrdiff_t srcstride, int height);                \
void ff_hevc_put_weighted_pred_ ## width ## _ ## depth ## _ ## cf_w(uint8_t denom, int16_t weight, int16_t offset,          \
                                                                    uint8_t *dst, ptrdiff_t dststride,                      \
                                                                    int16_t *src, ptrdiff_t srcstride,                      \
                                                                    int height);                                            \
void ff_hevc_put_weighted_pred_avg_ ## width ## _ ## depth ## _ ## cf_w(uint8_t denom, int16_t weight0, int16_t weight1,    \
                                                                        int16_t offset0, int16_t offset1,                   \
                                                                        uint8_t *dst, ptrdiff_t dststride,                  \
                                                                        int16_t *src0, int16_t *src1, ptrdiff_t srcstride,  \
                                                                        int height);

PUT_PRED(4,  8, sse2, sse4)
PUT_PRED(8,  8, sse2, sse4)
PUT_PRED(12, 8, sse2, sse4)
PUT_PRED(16, 8, sse2, sse4)
PUT_PRED(24, 8, sse2, sse4)
PUT_PRED(32, 8, sse2, sse4)
PUT_PRED(48, 8, sse2, sse4)
PUT_PRED(64, 8, sse2, sse4)

PUT_PRED(4,  10, sse2, sse4)
PUT_PRED(8,  10, sse2, sse4)
PUT_PRED(12, 10, sse2, sse4)
PUT_PRED(16, 10, sse2, sse4)
PUT_PRED(24, 10, sse2, sse4)
PUT_PRED(32, 10, sse2, sse4)
PUT_PRED(48, 10, sse2, sse4)
PUT_PRED(64, 10, sse2, sse4)

void ff_hevc_dsp_init_x86(HEVCDSPContext *c, const int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

#define SET_LUMA_FUNCS(tabname, funcname, depth, cf)      \
    c->tabname[0] = funcname ## _4_  ## depth ## _ ## cf; \
    c->tabname[1] = funcname ## _8_  ## depth ## _ ## cf; \
    c->tabname[2] = funcname ## _12_ ## depth ## _ ## cf; \
    c->tabname[3] = funcname ## _16_ ## depth ## _ ## cf; \
    c->tabname[4] = funcname ## _24_ ## depth ## _ ## cf; \
    c->tabname[5] = funcname ## _32_ ## depth ## _ ## cf; \
    c->tabname[6] = funcname ## _48_ ## depth ## _ ## cf; \
    c->tabname[7] = funcname ## _64_ ## depth ## _ ## cf;

#define SET_CHROMA_FUNCS(tabname, funcname, depth, cf)    \
    c->tabname[1] = funcname ## _4_  ## depth ## _ ## cf; \
    c->tabname[3] = funcname ## _8_  ## depth ## _ ## cf; \
    c->tabname[4] = funcname ## _12_ ## depth ## _ ## cf; \
    c->tabname[5] = funcname ## _16_ ## depth ## _ ## cf; \
    c->tabname[6] = funcname ## _24_ ## depth ## _ ## cf; \
    c->tabname[7] = funcname ## _32_ ## depth ## _ ## cf;

#define SET_QPEL_FUNCS(v, h, depth, cf, name) SET_LUMA_FUNCS  (put_hevc_qpel[v][h], name, depth, cf)
#define SET_EPEL_FUNCS(v, h, depth, cf, name) SET_CHROMA_FUNCS(put_hevc_epel[v][h], name, depth, cf)

    if (bit_depth == 8) {
        if (EXTERNAL_MMXEXT(cpu_flags)) {
            c->idct_dc[0] = ff_hevc_idct_4x4_dc_8_mmxext;
            c->idct_dc[1] = ff_hevc_idct_8x8_dc_8_mmxext;
        }
        if (EXTERNAL_SSE2(cpu_flags)) {
            c->hevc_v_loop_filter_chroma = ff_hevc_v_loop_filter_chroma_8_sse2;
            c->hevc_h_loop_filter_chroma = ff_hevc_h_loop_filter_chroma_8_sse2;

            c->idct_dc[1] = ff_hevc_idct_8x8_dc_8_sse2;
            c->idct_dc[2] = ff_hevc_idct_16x16_dc_8_sse2;
            c->idct_dc[3] = ff_hevc_idct_32x32_dc_8_sse2;
            SET_QPEL_FUNCS(0, 0, 8, sse2, ff_hevc_get_pixels);
            SET_EPEL_FUNCS(0, 0, 8, sse2, ff_hevc_get_pixels);

            SET_LUMA_FUNCS(put_unweighted_pred,              ff_hevc_put_unweighted_pred,     8, sse2);
            SET_LUMA_FUNCS(put_unweighted_pred_avg,          ff_hevc_put_unweighted_pred_avg, 8, sse2);
            SET_CHROMA_FUNCS(put_unweighted_pred_chroma,     ff_hevc_put_unweighted_pred,     8, sse2);
            SET_CHROMA_FUNCS(put_unweighted_pred_avg_chroma, ff_hevc_put_unweighted_pred_avg, 8, sse2);
        }
        if (EXTERNAL_SSSE3(cpu_flags)) {
            SET_QPEL_FUNCS(0, 1, 8, ssse3, ff_hevc_qpel_h);
            SET_QPEL_FUNCS(1, 0, 8, ssse3, ff_hevc_qpel_v);
            SET_EPEL_FUNCS(0, 1, 8, ssse3, ff_hevc_epel_h);
            SET_EPEL_FUNCS(1, 0, 8, ssse3, ff_hevc_epel_v);

        }
    } else if (bit_depth == 10) {
        if (EXTERNAL_MMXEXT(cpu_flags)) {
            c->idct_dc[0] = ff_hevc_idct_4x4_dc_10_mmxext;
            c->idct_dc[1] = ff_hevc_idct_8x8_dc_10_mmxext;
        }
        if (EXTERNAL_SSE2(cpu_flags)) {
            c->hevc_v_loop_filter_chroma = ff_hevc_v_loop_filter_chroma_10_sse2;
            c->hevc_h_loop_filter_chroma = ff_hevc_h_loop_filter_chroma_10_sse2;

            c->idct_dc[1] = ff_hevc_idct_8x8_dc_10_sse2;
            c->idct_dc[2] = ff_hevc_idct_16x16_dc_10_sse2;
            c->idct_dc[3] = ff_hevc_idct_32x32_dc_10_sse2;

            SET_QPEL_FUNCS(0, 0, 10, sse2, ff_hevc_get_pixels);
            SET_EPEL_FUNCS(0, 0, 10, sse2, ff_hevc_get_pixels);

            SET_LUMA_FUNCS(put_unweighted_pred,              ff_hevc_put_unweighted_pred,     10, sse2);
            SET_LUMA_FUNCS(put_unweighted_pred_avg,          ff_hevc_put_unweighted_pred_avg, 10, sse2);
            SET_CHROMA_FUNCS(put_unweighted_pred_chroma,     ff_hevc_put_unweighted_pred,     10, sse2);
            SET_CHROMA_FUNCS(put_unweighted_pred_avg_chroma, ff_hevc_put_unweighted_pred_avg, 10, sse2);
        }
    }

#if ARCH_X86_64
    if (bit_depth == 8) {
        if (EXTERNAL_SSSE3(cpu_flags)) {
            c->hevc_v_loop_filter_luma = ff_hevc_v_loop_filter_luma_8_ssse3;
            c->hevc_h_loop_filter_luma = ff_hevc_h_loop_filter_luma_8_ssse3;
        }

        if (EXTERNAL_SSE4(cpu_flags)) {
            SET_LUMA_FUNCS(weighted_pred,              ff_hevc_put_weighted_pred,     8, sse4);
            SET_CHROMA_FUNCS(weighted_pred_chroma,     ff_hevc_put_weighted_pred,     8, sse4);
            SET_LUMA_FUNCS(weighted_pred_avg,          ff_hevc_put_weighted_pred_avg, 8, sse4);
            SET_CHROMA_FUNCS(weighted_pred_avg_chroma, ff_hevc_put_weighted_pred_avg, 8, sse4);
        }

        if (EXTERNAL_AVX(cpu_flags)) {
#if HAVE_AVX_EXTERNAL
            SET_QPEL_FUNCS(1, 1, 8, avx, hevc_qpel_hv);
            SET_EPEL_FUNCS(1, 1, 8, avx, hevc_epel_hv);
#endif /* HAVE_AVX_EXTERNAL */
        }
        if (EXTERNAL_AVX2(cpu_flags)) {
            c->idct_dc[2] = ff_hevc_idct_16x16_dc_8_avx2;
            c->idct_dc[3] = ff_hevc_idct_32x32_dc_8_avx2;
        }
    } else if (bit_depth == 10) {
        if (EXTERNAL_SSSE3(cpu_flags)) {
            c->hevc_v_loop_filter_luma = ff_hevc_v_loop_filter_luma_10_ssse3;
            c->hevc_h_loop_filter_luma = ff_hevc_h_loop_filter_luma_10_ssse3;
        }
        if (EXTERNAL_SSE4(cpu_flags)) {
            SET_LUMA_FUNCS(weighted_pred,              ff_hevc_put_weighted_pred,     10, sse4);
            SET_CHROMA_FUNCS(weighted_pred_chroma,     ff_hevc_put_weighted_pred,     10, sse4);
            SET_LUMA_FUNCS(weighted_pred_avg,          ff_hevc_put_weighted_pred_avg, 10, sse4);
            SET_CHROMA_FUNCS(weighted_pred_avg_chroma, ff_hevc_put_weighted_pred_avg, 10, sse4);
        }
        if (EXTERNAL_AVX(cpu_flags)) {
#if HAVE_AVX_EXTERNAL
            SET_QPEL_FUNCS(0, 1, 10, avx, ff_hevc_qpel_h);
            SET_QPEL_FUNCS(1, 0, 10, avx, ff_hevc_qpel_v);
            SET_QPEL_FUNCS(1, 1, 10, avx, hevc_qpel_hv);
            SET_EPEL_FUNCS(0, 1, 10, avx, ff_hevc_epel_h);
            SET_EPEL_FUNCS(1, 0, 10, avx, ff_hevc_epel_v);
            SET_EPEL_FUNCS(1, 1, 10, avx, hevc_epel_hv);
#endif /* HAVE_AVX_EXTERNAL */
        }
        if (EXTERNAL_AVX2(cpu_flags)) {
            c->idct_dc[2] = ff_hevc_idct_16x16_dc_10_avx2;
            c->idct_dc[3] = ff_hevc_idct_32x32_dc_10_avx2;
        }
    }
#endif /* ARCH_X86_64 */
}
