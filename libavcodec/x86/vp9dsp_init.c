/*
 * VP9 SIMD optimizations
 *
 * Copyright (c) 2013 Ronald S. Bultje <rsbultje gmail com>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/vp9dsp.h"

#if HAVE_YASM

#define fpel_func(avg, sz, opt) \
void ff_vp9_##avg##sz##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                              const uint8_t *src, ptrdiff_t src_stride, \
                              int h, int mx, int my)
fpel_func(put,  4, mmx);
fpel_func(put,  8, mmx);
fpel_func(put, 16, sse);
fpel_func(put, 32, sse);
fpel_func(put, 64, sse);
fpel_func(avg,  4, mmxext);
fpel_func(avg,  8, mmxext);
fpel_func(avg, 16, sse2);
fpel_func(avg, 32, sse2);
fpel_func(avg, 64, sse2);
fpel_func(put, 32, avx);
fpel_func(put, 64, avx);
fpel_func(avg, 32, avx2);
fpel_func(avg, 64, avx2);
#undef fpel_func

#define mc_func(avg, sz, dir, opt) \
void ff_vp9_##avg##_8tap_1d_##dir##_##sz##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                                 const uint8_t *src, ptrdiff_t src_stride, \
                                                 int h, const int8_t (*filter)[32])
#define mc_funcs(sz, opt) \
mc_func(put, sz, h, opt); \
mc_func(avg, sz, h, opt); \
mc_func(put, sz, v, opt); \
mc_func(avg, sz, v, opt)

mc_funcs(4, ssse3);
mc_funcs(8, ssse3);
#if ARCH_X86_64
mc_funcs(16, ssse3);
mc_funcs(32, avx2);
#endif

#undef mc_funcs
#undef mc_func

#define mc_rep_func(avg, sz, hsz, dir, opt) \
static av_always_inline void \
ff_vp9_##avg##_8tap_1d_##dir##_##sz##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                            const uint8_t *src, ptrdiff_t src_stride, \
                                            int h, const int8_t (*filter)[32]) \
{ \
    ff_vp9_##avg##_8tap_1d_##dir##_##hsz##_##opt(dst,       dst_stride, src, \
                                                 src_stride, h, filter); \
    ff_vp9_##avg##_8tap_1d_##dir##_##hsz##_##opt(dst + hsz, dst_stride, src + hsz, \
                                                 src_stride, h, filter); \
}

#define mc_rep_funcs(sz, hsz, opt) \
mc_rep_func(put, sz, hsz, h, opt); \
mc_rep_func(avg, sz, hsz, h, opt); \
mc_rep_func(put, sz, hsz, v, opt); \
mc_rep_func(avg, sz, hsz, v, opt)

#if ARCH_X86_32
mc_rep_funcs(16, 8, ssse3);
#endif
mc_rep_funcs(32, 16, ssse3);
mc_rep_funcs(64, 32, ssse3);
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
mc_rep_funcs(64, 32, avx2);
#endif

#undef mc_rep_funcs
#undef mc_rep_func

extern const int8_t ff_filters_ssse3[3][15][4][32];

#define filter_8tap_2d_fn(op, sz, f, fname, align, opt) \
static void op##_8tap_##fname##_##sz##hv_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                               const uint8_t *src, ptrdiff_t src_stride, \
                                               int h, int mx, int my) \
{ \
    LOCAL_ALIGNED_##align(uint8_t, temp, [71 * 64]); \
    ff_vp9_put_8tap_1d_h_##sz##_##opt(temp, 64, src - 3 * src_stride, src_stride, \
                                      h + 7, ff_filters_ssse3[f][mx - 1]); \
    ff_vp9_##op##_8tap_1d_v_##sz##_##opt(dst, dst_stride, temp + 3 * 64, 64, \
                                         h, ff_filters_ssse3[f][my - 1]); \
}

#define filters_8tap_2d_fn(op, sz, align, opt) \
filter_8tap_2d_fn(op, sz, FILTER_8TAP_REGULAR, regular, align, opt) \
filter_8tap_2d_fn(op, sz, FILTER_8TAP_SHARP,   sharp, align, opt) \
filter_8tap_2d_fn(op, sz, FILTER_8TAP_SMOOTH,  smooth, align, opt)

#define filters_8tap_2d_fn2(op, align, opt) \
filters_8tap_2d_fn(op, 64, align, opt) \
filters_8tap_2d_fn(op, 32, align, opt) \
filters_8tap_2d_fn(op, 16, align, opt) \
filters_8tap_2d_fn(op, 8, align, opt) \
filters_8tap_2d_fn(op, 4, align, opt)

filters_8tap_2d_fn2(put, 16, ssse3)
filters_8tap_2d_fn2(avg, 16, ssse3)
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
filters_8tap_2d_fn(put, 64, 32, avx2)
filters_8tap_2d_fn(put, 32, 32, avx2)
filters_8tap_2d_fn(avg, 64, 32, avx2)
filters_8tap_2d_fn(avg, 32, 32, avx2)
#endif

#undef filters_8tap_2d_fn2
#undef filters_8tap_2d_fn
#undef filter_8tap_2d_fn

#define filter_8tap_1d_fn(op, sz, f, fname, dir, dvar, opt) \
static void op##_8tap_##fname##_##sz##dir##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                                  const uint8_t *src, ptrdiff_t src_stride, \
                                                  int h, int mx, int my) \
{ \
    ff_vp9_##op##_8tap_1d_##dir##_##sz##_##opt(dst, dst_stride, src, src_stride, \
                                               h, ff_filters_ssse3[f][dvar - 1]); \
}

#define filters_8tap_1d_fn(op, sz, dir, dvar, opt) \
filter_8tap_1d_fn(op, sz, FILTER_8TAP_REGULAR, regular, dir, dvar, opt) \
filter_8tap_1d_fn(op, sz, FILTER_8TAP_SHARP,   sharp,   dir, dvar, opt) \
filter_8tap_1d_fn(op, sz, FILTER_8TAP_SMOOTH,  smooth,  dir, dvar, opt)

#define filters_8tap_1d_fn2(op, sz, opt) \
filters_8tap_1d_fn(op, sz, h, mx, opt) \
filters_8tap_1d_fn(op, sz, v, my, opt)

#define filters_8tap_1d_fn3(op, opt) \
filters_8tap_1d_fn2(op, 64, opt) \
filters_8tap_1d_fn2(op, 32, opt) \
filters_8tap_1d_fn2(op, 16, opt) \
filters_8tap_1d_fn2(op, 8, opt) \
filters_8tap_1d_fn2(op, 4, opt)

filters_8tap_1d_fn3(put, ssse3)
filters_8tap_1d_fn3(avg, ssse3)
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
filters_8tap_1d_fn2(put, 64, avx2)
filters_8tap_1d_fn2(put, 32, avx2)
filters_8tap_1d_fn2(avg, 64, avx2)
filters_8tap_1d_fn2(avg, 32, avx2)
#endif

#undef filters_8tap_1d_fn
#undef filters_8tap_1d_fn2
#undef filters_8tap_1d_fn3
#undef filter_8tap_1d_fn

#define itxfm_func(typea, typeb, size, opt) \
void ff_vp9_##typea##_##typeb##_##size##x##size##_add_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                            int16_t *block, int eob)
#define itxfm_funcs(size, opt) \
itxfm_func(idct,  idct,  size, opt); \
itxfm_func(iadst, idct,  size, opt); \
itxfm_func(idct,  iadst, size, opt); \
itxfm_func(iadst, iadst, size, opt)

itxfm_funcs(4, ssse3);
itxfm_funcs(8, ssse3);
itxfm_funcs(8, avx);
itxfm_funcs(16, ssse3);
itxfm_funcs(16, avx);
itxfm_func(idct, idct, 32, ssse3);
itxfm_func(idct, idct, 32, avx);
itxfm_func(iwht, iwht, 4, mmx);

#undef itxfm_func
#undef itxfm_funcs

#define lpf_funcs(size1, size2, opt) \
void ff_vp9_loop_filter_v_##size1##_##size2##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                    int E, int I, int H); \
void ff_vp9_loop_filter_h_##size1##_##size2##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                    int E, int I, int H)

lpf_funcs(16, 16, sse2);
lpf_funcs(16, 16, ssse3);
lpf_funcs(16, 16, avx);
lpf_funcs(44, 16, sse2);
lpf_funcs(44, 16, ssse3);
lpf_funcs(44, 16, avx);
lpf_funcs(84, 16, sse2);
lpf_funcs(84, 16, ssse3);
lpf_funcs(84, 16, avx);
lpf_funcs(48, 16, sse2);
lpf_funcs(48, 16, ssse3);
lpf_funcs(48, 16, avx);
lpf_funcs(88, 16, sse2);
lpf_funcs(88, 16, ssse3);
lpf_funcs(88, 16, avx);

#undef lpf_funcs

#define ipred_func(size, type, opt) \
void ff_vp9_ipred_##type##_##size##x##size##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                   const uint8_t *l, const uint8_t *a)

#define ipred_funcs(type, opt) \
ipred_func(4, type, opt); \
ipred_func(8, type, opt); \
ipred_func(16, type, opt); \
ipred_func(32, type, opt)

ipred_funcs(dc, ssse3);
ipred_funcs(dc_left, ssse3);
ipred_funcs(dc_top, ssse3);

#undef ipred_funcs

ipred_func(8, v, mmx);
ipred_func(16, v, sse2);
ipred_func(32, v, sse2);

#define ipred_func_set(size, type, opt1, opt2) \
ipred_func(size, type, opt1); \
ipred_func(size, type, opt2)

#define ipred_funcs(type, opt1, opt2) \
ipred_func(4, type, opt1); \
ipred_func_set(8, type, opt1, opt2); \
ipred_func_set(16, type, opt1, opt2); \
ipred_func_set(32, type, opt1, opt2)

ipred_funcs(h, ssse3, avx);
ipred_funcs(tm, ssse3, avx);
ipred_funcs(dl, ssse3, avx);
ipred_funcs(dr, ssse3, avx);
ipred_funcs(hu, ssse3, avx);
ipred_funcs(hd, ssse3, avx);
ipred_funcs(vl, ssse3, avx);
ipred_funcs(vr, ssse3, avx);

ipred_func(32, dc, avx2);
ipred_func(32, dc_left, avx2);
ipred_func(32, dc_top, avx2);
ipred_func(32, v, avx2);
ipred_func(32, h, avx2);
ipred_func(32, tm, avx2);

#undef ipred_funcs
#undef ipred_func_set
#undef ipred_func

#endif /* HAVE_YASM */

av_cold void ff_vp9dsp_init_x86(VP9DSPContext *dsp)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#define init_fpel(idx1, idx2, sz, type, opt) \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][0][0] = ff_vp9_##type##sz##_##opt

#define init_subpel1(idx1, idx2, idxh, idxv, sz, dir, type, opt) \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][idxh][idxv] = type##_8tap_smooth_##sz##dir##_##opt; \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][idxh][idxv] = type##_8tap_regular_##sz##dir##_##opt; \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][idxh][idxv] = type##_8tap_sharp_##sz##dir##_##opt

#define init_subpel2_32_64(idx, idxh, idxv, dir, type, opt) \
    init_subpel1(0, idx, idxh, idxv, 64, dir, type, opt); \
    init_subpel1(1, idx, idxh, idxv, 32, dir, type, opt)

#define init_subpel2(idx, idxh, idxv, dir, type, opt) \
    init_subpel2_32_64(idx, idxh, idxv, dir, type, opt); \
    init_subpel1(2, idx, idxh, idxv, 16, dir, type, opt); \
    init_subpel1(3, idx, idxh, idxv,  8, dir, type, opt); \
    init_subpel1(4, idx, idxh, idxv,  4, dir, type, opt)

#define init_subpel3(idx, type, opt) \
    init_subpel2(idx, 1, 1, hv, type, opt); \
    init_subpel2(idx, 0, 1, v, type, opt); \
    init_subpel2(idx, 1, 0, h, type, opt)

#define init_lpf(opt) do { \
    if (ARCH_X86_64) { \
        dsp->loop_filter_16[0] = ff_vp9_loop_filter_h_16_16_##opt; \
        dsp->loop_filter_16[1] = ff_vp9_loop_filter_v_16_16_##opt; \
        dsp->loop_filter_mix2[0][0][0] = ff_vp9_loop_filter_h_44_16_##opt; \
        dsp->loop_filter_mix2[0][0][1] = ff_vp9_loop_filter_v_44_16_##opt; \
        dsp->loop_filter_mix2[0][1][0] = ff_vp9_loop_filter_h_48_16_##opt; \
        dsp->loop_filter_mix2[0][1][1] = ff_vp9_loop_filter_v_48_16_##opt; \
        dsp->loop_filter_mix2[1][0][0] = ff_vp9_loop_filter_h_84_16_##opt; \
        dsp->loop_filter_mix2[1][0][1] = ff_vp9_loop_filter_v_84_16_##opt; \
        dsp->loop_filter_mix2[1][1][0] = ff_vp9_loop_filter_h_88_16_##opt; \
        dsp->loop_filter_mix2[1][1][1] = ff_vp9_loop_filter_v_88_16_##opt; \
    } \
} while (0)

#define init_ipred(tx, sz, opt) do { \
    dsp->intra_pred[tx][HOR_PRED]             = ff_vp9_ipred_h_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][DIAG_DOWN_LEFT_PRED]  = ff_vp9_ipred_dl_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][DIAG_DOWN_RIGHT_PRED] = ff_vp9_ipred_dr_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][HOR_DOWN_PRED]        = ff_vp9_ipred_hd_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][VERT_LEFT_PRED]       = ff_vp9_ipred_vl_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][HOR_UP_PRED]          = ff_vp9_ipred_hu_##sz##x##sz##_##opt; \
    if (ARCH_X86_64 || tx != TX_32X32) { \
        dsp->intra_pred[tx][VERT_RIGHT_PRED]      = ff_vp9_ipred_vr_##sz##x##sz##_##opt; \
        dsp->intra_pred[tx][TM_VP8_PRED]          = ff_vp9_ipred_tm_##sz##x##sz##_##opt; \
    } \
} while (0)
#define init_dc_ipred(tx, sz, opt) do { \
    init_ipred(tx, sz, opt); \
    dsp->intra_pred[tx][DC_PRED]              = ff_vp9_ipred_dc_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][LEFT_DC_PRED]         = ff_vp9_ipred_dc_left_##sz##x##sz##_##opt; \
    dsp->intra_pred[tx][TOP_DC_PRED]          = ff_vp9_ipred_dc_top_##sz##x##sz##_##opt; \
} while (0)

    if (EXTERNAL_MMX(cpu_flags)) {
        init_fpel(4, 0,  4, put, mmx);
        init_fpel(3, 0,  8, put, mmx);
        dsp->itxfm_add[4 /* lossless */][DCT_DCT] =
        dsp->itxfm_add[4 /* lossless */][ADST_DCT] =
        dsp->itxfm_add[4 /* lossless */][DCT_ADST] =
        dsp->itxfm_add[4 /* lossless */][ADST_ADST] = ff_vp9_iwht_iwht_4x4_add_mmx;
        dsp->intra_pred[TX_8X8][VERT_PRED] = ff_vp9_ipred_v_8x8_mmx;
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        init_fpel(4, 1,  4, avg, mmxext);
        init_fpel(3, 1,  8, avg, mmxext);
    }

    if (EXTERNAL_SSE(cpu_flags)) {
        init_fpel(2, 0, 16, put, sse);
        init_fpel(1, 0, 32, put, sse);
        init_fpel(0, 0, 64, put, sse);
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        init_fpel(2, 1, 16, avg, sse2);
        init_fpel(1, 1, 32, avg, sse2);
        init_fpel(0, 1, 64, avg, sse2);
        init_lpf(sse2);
        dsp->intra_pred[TX_16X16][VERT_PRED] = ff_vp9_ipred_v_16x16_sse2;
        dsp->intra_pred[TX_32X32][VERT_PRED] = ff_vp9_ipred_v_32x32_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        init_subpel3(0, put, ssse3);
        init_subpel3(1, avg, ssse3);
        dsp->itxfm_add[TX_4X4][DCT_DCT] = ff_vp9_idct_idct_4x4_add_ssse3;
        dsp->itxfm_add[TX_4X4][ADST_DCT]  = ff_vp9_idct_iadst_4x4_add_ssse3;
        dsp->itxfm_add[TX_4X4][DCT_ADST]  = ff_vp9_iadst_idct_4x4_add_ssse3;
        dsp->itxfm_add[TX_4X4][ADST_ADST] = ff_vp9_iadst_iadst_4x4_add_ssse3;
        if (ARCH_X86_64) {
            dsp->itxfm_add[TX_8X8][DCT_DCT] = ff_vp9_idct_idct_8x8_add_ssse3;
            dsp->itxfm_add[TX_8X8][ADST_DCT]  = ff_vp9_idct_iadst_8x8_add_ssse3;
            dsp->itxfm_add[TX_8X8][DCT_ADST]  = ff_vp9_iadst_idct_8x8_add_ssse3;
            dsp->itxfm_add[TX_8X8][ADST_ADST] = ff_vp9_iadst_iadst_8x8_add_ssse3;
            dsp->itxfm_add[TX_16X16][DCT_DCT]   = ff_vp9_idct_idct_16x16_add_ssse3;
            dsp->itxfm_add[TX_16X16][ADST_DCT]  = ff_vp9_idct_iadst_16x16_add_ssse3;
            dsp->itxfm_add[TX_16X16][DCT_ADST]  = ff_vp9_iadst_idct_16x16_add_ssse3;
            dsp->itxfm_add[TX_16X16][ADST_ADST] = ff_vp9_iadst_iadst_16x16_add_ssse3;
            dsp->itxfm_add[TX_32X32][ADST_ADST] =
            dsp->itxfm_add[TX_32X32][ADST_DCT] =
            dsp->itxfm_add[TX_32X32][DCT_ADST] =
            dsp->itxfm_add[TX_32X32][DCT_DCT] = ff_vp9_idct_idct_32x32_add_ssse3;
        }
        init_lpf(ssse3);
        init_dc_ipred(TX_4X4,    4, ssse3);
        init_dc_ipred(TX_8X8,    8, ssse3);
        init_dc_ipred(TX_16X16, 16, ssse3);
        init_dc_ipred(TX_32X32, 32, ssse3);
    }

    if (EXTERNAL_AVX(cpu_flags)) {
        if (ARCH_X86_64) {
            dsp->itxfm_add[TX_8X8][DCT_DCT] = ff_vp9_idct_idct_8x8_add_avx;
            dsp->itxfm_add[TX_8X8][ADST_DCT]  = ff_vp9_idct_iadst_8x8_add_avx;
            dsp->itxfm_add[TX_8X8][DCT_ADST]  = ff_vp9_iadst_idct_8x8_add_avx;
            dsp->itxfm_add[TX_8X8][ADST_ADST] = ff_vp9_iadst_iadst_8x8_add_avx;
            dsp->itxfm_add[TX_16X16][DCT_DCT] = ff_vp9_idct_idct_16x16_add_avx;
            dsp->itxfm_add[TX_16X16][ADST_DCT]  = ff_vp9_idct_iadst_16x16_add_avx;
            dsp->itxfm_add[TX_16X16][DCT_ADST]  = ff_vp9_iadst_idct_16x16_add_avx;
            dsp->itxfm_add[TX_16X16][ADST_ADST] = ff_vp9_iadst_iadst_16x16_add_avx;
            dsp->itxfm_add[TX_32X32][ADST_ADST] =
            dsp->itxfm_add[TX_32X32][ADST_DCT] =
            dsp->itxfm_add[TX_32X32][DCT_ADST] =
            dsp->itxfm_add[TX_32X32][DCT_DCT] = ff_vp9_idct_idct_32x32_add_avx;
        }
        init_fpel(1, 0, 32, put, avx);
        init_fpel(0, 0, 64, put, avx);
        init_lpf(avx);
        init_ipred(TX_8X8,    8, avx);
        init_ipred(TX_16X16, 16, avx);
        init_ipred(TX_32X32, 32, avx);
    }

    if (EXTERNAL_AVX2(cpu_flags)) {
        init_fpel(1, 1, 32, avg, avx2);
        init_fpel(0, 1, 64, avg, avx2);
        if (ARCH_X86_64) {
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
            init_subpel2_32_64(0, 1, 1, hv, put, avx2);
            init_subpel2_32_64(0, 0, 1, v,  put, avx2);
            init_subpel2_32_64(0, 1, 0, h,  put, avx2);
            init_subpel2_32_64(1, 1, 1, hv, avg, avx2);
            init_subpel2_32_64(1, 0, 1, v,  avg, avx2);
            init_subpel2_32_64(1, 1, 0, h,  avg, avx2);
#endif
        }
        dsp->intra_pred[TX_32X32][DC_PRED] = ff_vp9_ipred_dc_32x32_avx2;
        dsp->intra_pred[TX_32X32][LEFT_DC_PRED] = ff_vp9_ipred_dc_left_32x32_avx2;
        dsp->intra_pred[TX_32X32][TOP_DC_PRED] = ff_vp9_ipred_dc_top_32x32_avx2;
        dsp->intra_pred[TX_32X32][VERT_PRED] = ff_vp9_ipred_v_32x32_avx2;
        dsp->intra_pred[TX_32X32][HOR_PRED] = ff_vp9_ipred_h_32x32_avx2;
        dsp->intra_pred[TX_32X32][TM_VP8_PRED] = ff_vp9_ipred_tm_32x32_avx2;
    }

#undef init_fpel
#undef init_subpel1
#undef init_subpel2
#undef init_subpel3

#endif /* HAVE_YASM */
}
