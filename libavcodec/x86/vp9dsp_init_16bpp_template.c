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
#include "libavutil/x86/cpu.h"
#include "libavcodec/vp9dsp.h"
#include "libavcodec/x86/vp9dsp_init.h"

#if HAVE_YASM

extern const int16_t ff_filters_16bpp[3][15][4][16];

decl_mc_funcs(4, sse2, int16_t, 16, BPC);
decl_mc_funcs(8, sse2, int16_t, 16, BPC);
decl_mc_funcs(16, avx2, int16_t, 16, BPC);

mc_rep_funcs(16,  8, 16, sse2, int16_t, 16, BPC)
mc_rep_funcs(32, 16, 32, sse2, int16_t, 16, BPC)
mc_rep_funcs(64, 32, 64, sse2, int16_t, 16, BPC)
#if HAVE_AVX2_EXTERNAL
mc_rep_funcs(32, 16, 32, avx2, int16_t, 16, BPC)
mc_rep_funcs(64, 32, 64, avx2, int16_t, 16, BPC)
#endif

filters_8tap_2d_fn2(put, 16, BPC, 2, sse2, sse2, 16bpp)
filters_8tap_2d_fn2(avg, 16, BPC, 2, sse2, sse2, 16bpp)
#if HAVE_AVX2_EXTERNAL
filters_8tap_2d_fn(put, 64, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(avg, 64, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(put, 32, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(avg, 32, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(put, 16, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(avg, 16, 32, BPC, 2, avx2, 16bpp)
#endif

filters_8tap_1d_fn3(put, BPC, sse2, sse2, 16bpp)
filters_8tap_1d_fn3(avg, BPC, sse2, sse2, 16bpp)
#if HAVE_AVX2_EXTERNAL
filters_8tap_1d_fn2(put, 64, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(avg, 64, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(put, 32, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(avg, 32, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(put, 16, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(avg, 16, BPC, avx2, 16bpp)
#endif

#define decl_lpf_func(dir, wd, bpp, opt) \
void ff_vp9_loop_filter_##dir##_##wd##_##bpp##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                     int E, int I, int H)

#define decl_lpf_funcs(dir, wd, bpp) \
decl_lpf_func(dir, wd, bpp, sse2); \
decl_lpf_func(dir, wd, bpp, ssse3); \
decl_lpf_func(dir, wd, bpp, avx)

#define decl_lpf_funcs_wd(dir) \
decl_lpf_funcs(dir,  4, BPC); \
decl_lpf_funcs(dir,  8, BPC); \
decl_lpf_funcs(dir, 16, BPC)

decl_lpf_funcs_wd(h);
decl_lpf_funcs_wd(v);

#define lpf_16_wrapper(dir, off, bpp, opt) \
static void loop_filter_##dir##_16_##bpp##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                 int E, int I, int H) \
{ \
    ff_vp9_loop_filter_##dir##_16_##bpp##_##opt(dst,       stride, E, I, H); \
    ff_vp9_loop_filter_##dir##_16_##bpp##_##opt(dst + off, stride, E, I, H); \
}

#define lpf_16_wrappers(bpp, opt) \
lpf_16_wrapper(h, 8 * stride, bpp, opt) \
lpf_16_wrapper(v, 16,         bpp, opt)

lpf_16_wrappers(BPC, sse2)
lpf_16_wrappers(BPC, ssse3)
lpf_16_wrappers(BPC, avx)

#define lpf_mix2_wrapper(dir, off, wd1, wd2, bpp, opt) \
static void loop_filter_##dir##_##wd1##wd2##_##bpp##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                           int E, int I, int H) \
{ \
    ff_vp9_loop_filter_##dir##_##wd1##_##bpp##_##opt(dst,       stride, \
                                                     E & 0xff, I & 0xff, H & 0xff); \
    ff_vp9_loop_filter_##dir##_##wd2##_##bpp##_##opt(dst + off, stride, \
                                                     E >> 8,   I >> 8,   H >> 8); \
}

#define lpf_mix2_wrappers(wd1, wd2, bpp, opt) \
lpf_mix2_wrapper(h, 8 * stride, wd1, wd2, bpp, opt) \
lpf_mix2_wrapper(v, 16,         wd1, wd2, bpp, opt)

#define lpf_mix2_wrappers_set(bpp, opt) \
lpf_mix2_wrappers(4, 4, bpp, opt) \
lpf_mix2_wrappers(4, 8, bpp, opt) \
lpf_mix2_wrappers(8, 4, bpp, opt) \
lpf_mix2_wrappers(8, 8, bpp, opt) \

lpf_mix2_wrappers_set(BPC, sse2)
lpf_mix2_wrappers_set(BPC, ssse3)
lpf_mix2_wrappers_set(BPC, avx)

decl_ipred_fns(tm, BPC, mmxext, sse2);

decl_itxfm_func(iwht, iwht, 4, BPC, mmxext);
#if BPC == 10
decl_itxfm_func(idct,  idct,  4, BPC, mmxext);
decl_itxfm_funcs(4, BPC, ssse3);
#else
decl_itxfm_func(idct,  idct,  4, BPC, sse2);
#endif
decl_itxfm_func(idct,  iadst, 4, BPC, sse2);
decl_itxfm_func(iadst, idct,  4, BPC, sse2);
decl_itxfm_func(iadst, iadst, 4, BPC, sse2);
decl_itxfm_funcs(8, BPC, sse2);
decl_itxfm_funcs(16, BPC, sse2);
decl_itxfm_func(idct,  idct, 32, BPC, sse2);
#endif /* HAVE_YASM */

av_cold void INIT_FUNC(VP9DSPContext *dsp, int bitexact)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#define init_lpf_8_func(idx1, idx2, dir, wd, bpp, opt) \
    dsp->loop_filter_8[idx1][idx2] = ff_vp9_loop_filter_##dir##_##wd##_##bpp##_##opt
#define init_lpf_16_func(idx, dir, bpp, opt) \
    dsp->loop_filter_16[idx] = loop_filter_##dir##_16_##bpp##_##opt
#define init_lpf_mix2_func(idx1, idx2, idx3, dir, wd1, wd2, bpp, opt) \
    dsp->loop_filter_mix2[idx1][idx2][idx3] = loop_filter_##dir##_##wd1##wd2##_##bpp##_##opt

#define init_lpf_funcs(bpp, opt) \
    init_lpf_8_func(0, 0, h,  4, bpp, opt); \
    init_lpf_8_func(0, 1, v,  4, bpp, opt); \
    init_lpf_8_func(1, 0, h,  8, bpp, opt); \
    init_lpf_8_func(1, 1, v,  8, bpp, opt); \
    init_lpf_8_func(2, 0, h, 16, bpp, opt); \
    init_lpf_8_func(2, 1, v, 16, bpp, opt); \
    init_lpf_16_func(0, h, bpp, opt); \
    init_lpf_16_func(1, v, bpp, opt); \
    init_lpf_mix2_func(0, 0, 0, h, 4, 4, bpp, opt); \
    init_lpf_mix2_func(0, 1, 0, h, 4, 8, bpp, opt); \
    init_lpf_mix2_func(1, 0, 0, h, 8, 4, bpp, opt); \
    init_lpf_mix2_func(1, 1, 0, h, 8, 8, bpp, opt); \
    init_lpf_mix2_func(0, 0, 1, v, 4, 4, bpp, opt); \
    init_lpf_mix2_func(0, 1, 1, v, 4, 8, bpp, opt); \
    init_lpf_mix2_func(1, 0, 1, v, 8, 4, bpp, opt); \
    init_lpf_mix2_func(1, 1, 1, v, 8, 8, bpp, opt)

#define init_itx_func(idxa, idxb, typea, typeb, size, bpp, opt) \
    dsp->itxfm_add[idxa][idxb] = \
        cat(ff_vp9_##typea##_##typeb##_##size##x##size##_add_, bpp, _##opt);
#define init_itx_func_one(idx, typea, typeb, size, bpp, opt) \
    init_itx_func(idx, DCT_DCT,   typea, typeb, size, bpp, opt); \
    init_itx_func(idx, ADST_DCT,  typea, typeb, size, bpp, opt); \
    init_itx_func(idx, DCT_ADST,  typea, typeb, size, bpp, opt); \
    init_itx_func(idx, ADST_ADST, typea, typeb, size, bpp, opt)
#define init_itx_funcs(idx, size, bpp, opt) \
    init_itx_func(idx, DCT_DCT,   idct,  idct,  size, bpp, opt); \
    init_itx_func(idx, ADST_DCT,  idct,  iadst, size, bpp, opt); \
    init_itx_func(idx, DCT_ADST,  iadst, idct,  size, bpp, opt); \
    init_itx_func(idx, ADST_ADST, iadst, iadst, size, bpp, opt); \

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        init_ipred_func(tm, TM_VP8, 4, BPC, mmxext);
        if (!bitexact) {
            init_itx_func_one(4 /* lossless */, iwht, iwht, 4, BPC, mmxext);
#if BPC == 10
            init_itx_func(TX_4X4, DCT_DCT, idct, idct, 4, 10, mmxext);
#endif
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        init_subpel3(0, put, BPC, sse2);
        init_subpel3(1, avg, BPC, sse2);
        init_lpf_funcs(BPC, sse2);
        init_8_16_32_ipred_funcs(tm, TM_VP8, BPC, sse2);
#if BPC == 10
        if (!bitexact) {
            init_itx_func(TX_4X4, ADST_DCT,  idct,  iadst, 4, 10, sse2);
            init_itx_func(TX_4X4, DCT_ADST,  iadst, idct,  4, 10, sse2);
            init_itx_func(TX_4X4, ADST_ADST, iadst, iadst, 4, 10, sse2);
        }
#else
        init_itx_funcs(TX_4X4, 4, 12, sse2);
#endif
        init_itx_funcs(TX_8X8, 8, BPC, sse2);
        init_itx_funcs(TX_16X16, 16, BPC, sse2);
        init_itx_func_one(TX_32X32, idct, idct, 32, BPC, sse2);
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        init_lpf_funcs(BPC, ssse3);
#if BPC == 10
        if (!bitexact) {
            init_itx_funcs(TX_4X4, 4, BPC, ssse3);
        }
#endif
    }

    if (EXTERNAL_AVX(cpu_flags)) {
        init_lpf_funcs(BPC, avx);
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
#if HAVE_AVX2_EXTERNAL
        init_subpel3_32_64(0,  put, BPC, avx2);
        init_subpel3_32_64(1,  avg, BPC, avx2);
        init_subpel2(2, 0, 16, put, BPC, avx2);
        init_subpel2(2, 1, 16, avg, BPC, avx2);
#endif
    }

#endif /* HAVE_YASM */

    ff_vp9dsp_init_16bpp_x86(dsp);
}
