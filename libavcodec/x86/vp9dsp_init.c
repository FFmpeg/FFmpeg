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
#include "libavutil/x86/cpu.h"
#include "libavcodec/vp9dsp.h"
#include "libavcodec/x86/vp9dsp_init.h"

#if HAVE_X86ASM

decl_fpel_func(put,  4,   , mmx);
decl_fpel_func(put,  8,   , mmx);
decl_fpel_func(put, 16,   , sse);
decl_fpel_func(put, 32,   , sse);
decl_fpel_func(put, 64,   , sse);
decl_fpel_func(avg,  4, _8, mmxext);
decl_fpel_func(avg,  8, _8, mmxext);
decl_fpel_func(avg, 16, _8, sse2);
decl_fpel_func(avg, 32, _8, sse2);
decl_fpel_func(avg, 64, _8, sse2);
decl_fpel_func(put, 32,   , avx);
decl_fpel_func(put, 64,   , avx);
decl_fpel_func(avg, 32, _8, avx2);
decl_fpel_func(avg, 64, _8, avx2);

decl_mc_funcs(4, mmxext, int16_t, 8, 8);
decl_mc_funcs(8, sse2, int16_t,  8, 8);
decl_mc_funcs(4, ssse3, int8_t, 32, 8);
decl_mc_funcs(8, ssse3, int8_t, 32, 8);
#if ARCH_X86_64
decl_mc_funcs(16, ssse3, int8_t, 32, 8);
decl_mc_funcs(32, avx2, int8_t, 32, 8);
#endif

mc_rep_funcs(16,  8,  8,  sse2, int16_t,  8, 8)
#if ARCH_X86_32
mc_rep_funcs(16,  8,  8, ssse3, int8_t,  32, 8)
#endif
mc_rep_funcs(32, 16, 16, sse2,  int16_t,  8, 8)
mc_rep_funcs(32, 16, 16, ssse3, int8_t,  32, 8)
mc_rep_funcs(64, 32, 32, sse2,  int16_t,  8, 8)
mc_rep_funcs(64, 32, 32, ssse3, int8_t,  32, 8)
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
mc_rep_funcs(64, 32, 32, avx2,  int8_t,  32, 8)
#endif

extern const int8_t ff_filters_ssse3[3][15][4][32];
extern const int16_t ff_filters_sse2[3][15][8][8];

filters_8tap_2d_fn2(put, 16, 8, 1, mmxext, sse2, sse2)
filters_8tap_2d_fn2(avg, 16, 8, 1, mmxext, sse2, sse2)
filters_8tap_2d_fn2(put, 16, 8, 1, ssse3, ssse3, ssse3)
filters_8tap_2d_fn2(avg, 16, 8, 1, ssse3, ssse3, ssse3)
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
filters_8tap_2d_fn(put, 64, 32, 8, 1, avx2, ssse3)
filters_8tap_2d_fn(put, 32, 32, 8, 1, avx2, ssse3)
filters_8tap_2d_fn(avg, 64, 32, 8, 1, avx2, ssse3)
filters_8tap_2d_fn(avg, 32, 32, 8, 1, avx2, ssse3)
#endif

filters_8tap_1d_fn3(put, 8, mmxext, sse2, sse2)
filters_8tap_1d_fn3(avg, 8, mmxext, sse2, sse2)
filters_8tap_1d_fn3(put, 8, ssse3, ssse3, ssse3)
filters_8tap_1d_fn3(avg, 8, ssse3, ssse3, ssse3)
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
filters_8tap_1d_fn2(put, 64, 8, avx2, ssse3)
filters_8tap_1d_fn2(put, 32, 8, avx2, ssse3)
filters_8tap_1d_fn2(avg, 64, 8, avx2, ssse3)
filters_8tap_1d_fn2(avg, 32, 8, avx2, ssse3)
#endif

#define itxfm_func(typea, typeb, size, opt) \
void ff_vp9_##typea##_##typeb##_##size##x##size##_add_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                            int16_t *block, int eob)
#define itxfm_funcs(size, opt) \
itxfm_func(idct,  idct,  size, opt); \
itxfm_func(iadst, idct,  size, opt); \
itxfm_func(idct,  iadst, size, opt); \
itxfm_func(iadst, iadst, size, opt)

itxfm_func(idct,  idct,  4, mmxext);
itxfm_func(idct,  iadst, 4, sse2);
itxfm_func(iadst, idct,  4, sse2);
itxfm_func(iadst, iadst, 4, sse2);
itxfm_funcs(4, ssse3);
itxfm_funcs(8, sse2);
itxfm_funcs(8, ssse3);
itxfm_funcs(8, avx);
itxfm_funcs(16, sse2);
itxfm_funcs(16, ssse3);
itxfm_funcs(16, avx);
itxfm_func(idct, idct, 32, sse2);
itxfm_func(idct, idct, 32, ssse3);
itxfm_func(idct, idct, 32, avx);
itxfm_func(iwht, iwht, 4, mmx);
itxfm_funcs(16, avx2);
itxfm_func(idct, idct, 32, avx2);

#undef itxfm_func
#undef itxfm_funcs

#define lpf_funcs(size1, size2, opt) \
void ff_vp9_loop_filter_v_##size1##_##size2##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                    int E, int I, int H); \
void ff_vp9_loop_filter_h_##size1##_##size2##_##opt(uint8_t *dst, ptrdiff_t stride, \
                                                    int E, int I, int H)

lpf_funcs(4, 8, mmxext);
lpf_funcs(8, 8, mmxext);
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

ipred_func(8, v, mmx);

#define ipred_dc_funcs(size, opt) \
ipred_func(size, dc, opt); \
ipred_func(size, dc_left, opt); \
ipred_func(size, dc_top, opt)

ipred_dc_funcs(4, mmxext);
ipred_dc_funcs(8, mmxext);

#define ipred_dir_tm_funcs(size, opt) \
ipred_func(size, tm, opt); \
ipred_func(size, dl, opt); \
ipred_func(size, dr, opt); \
ipred_func(size, hd, opt); \
ipred_func(size, hu, opt); \
ipred_func(size, vl, opt); \
ipred_func(size, vr, opt)

ipred_dir_tm_funcs(4, mmxext);

ipred_func(16, v, sse);
ipred_func(32, v, sse);

ipred_dc_funcs(16, sse2);
ipred_dc_funcs(32, sse2);

#define ipred_dir_tm_h_funcs(size, opt) \
ipred_dir_tm_funcs(size, opt); \
ipred_func(size, h, opt)

ipred_dir_tm_h_funcs(8, sse2);
ipred_dir_tm_h_funcs(16, sse2);
ipred_dir_tm_h_funcs(32, sse2);

ipred_func(4, h, sse2);

#define ipred_all_funcs(size, opt) \
ipred_dc_funcs(size, opt); \
ipred_dir_tm_h_funcs(size, opt)

// FIXME hd/vl_4x4_ssse3 does not exist
ipred_all_funcs(4, ssse3);
ipred_all_funcs(8, ssse3);
ipred_all_funcs(16, ssse3);
ipred_all_funcs(32, ssse3);

ipred_dir_tm_h_funcs(8, avx);
ipred_dir_tm_h_funcs(16, avx);
ipred_dir_tm_h_funcs(32, avx);

ipred_func(32, v, avx);

ipred_dc_funcs(32, avx2);
ipred_func(32, h, avx2);
ipred_func(32, tm, avx2);

#undef ipred_func
#undef ipred_dir_tm_h_funcs
#undef ipred_dir_tm_funcs
#undef ipred_dc_funcs

#endif /* HAVE_X86ASM */

av_cold void ff_vp9dsp_init_x86(VP9DSPContext *dsp, int bpp, int bitexact)
{
#if HAVE_X86ASM
    int cpu_flags;

    if (bpp == 10) {
        ff_vp9dsp_init_10bpp_x86(dsp, bitexact);
        return;
    } else if (bpp == 12) {
        ff_vp9dsp_init_12bpp_x86(dsp, bitexact);
        return;
    }

    cpu_flags = av_get_cpu_flags();

#define init_lpf(opt) do { \
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
} while (0)

#define init_ipred(sz, opt, t, e) \
    dsp->intra_pred[TX_##sz##X##sz][e##_PRED] = ff_vp9_ipred_##t##_##sz##x##sz##_##opt

#define ff_vp9_ipred_hd_4x4_ssse3 ff_vp9_ipred_hd_4x4_mmxext
#define ff_vp9_ipred_vl_4x4_ssse3 ff_vp9_ipred_vl_4x4_mmxext
#define init_dir_tm_ipred(sz, opt) do { \
    init_ipred(sz, opt, dl, DIAG_DOWN_LEFT); \
    init_ipred(sz, opt, dr, DIAG_DOWN_RIGHT); \
    init_ipred(sz, opt, hd, HOR_DOWN); \
    init_ipred(sz, opt, vl, VERT_LEFT); \
    init_ipred(sz, opt, hu, HOR_UP); \
    init_ipred(sz, opt, tm, TM_VP8); \
    init_ipred(sz, opt, vr, VERT_RIGHT); \
} while (0)
#define init_dir_tm_h_ipred(sz, opt) do { \
    init_dir_tm_ipred(sz, opt); \
    init_ipred(sz, opt, h,  HOR); \
} while (0)
#define init_dc_ipred(sz, opt) do { \
    init_ipred(sz, opt, dc,      DC); \
    init_ipred(sz, opt, dc_left, LEFT_DC); \
    init_ipred(sz, opt, dc_top,  TOP_DC); \
} while (0)
#define init_all_ipred(sz, opt) do { \
    init_dc_ipred(sz, opt); \
    init_dir_tm_h_ipred(sz, opt); \
} while (0)

    if (EXTERNAL_MMX(cpu_flags)) {
        init_fpel_func(4, 0,  4, put, , mmx);
        init_fpel_func(3, 0,  8, put, , mmx);
        if (!bitexact) {
            dsp->itxfm_add[4 /* lossless */][DCT_DCT] =
            dsp->itxfm_add[4 /* lossless */][ADST_DCT] =
            dsp->itxfm_add[4 /* lossless */][DCT_ADST] =
            dsp->itxfm_add[4 /* lossless */][ADST_ADST] = ff_vp9_iwht_iwht_4x4_add_mmx;
        }
        init_ipred(8, mmx, v, VERT);
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        dsp->loop_filter_8[0][0] = ff_vp9_loop_filter_h_4_8_mmxext;
        dsp->loop_filter_8[0][1] = ff_vp9_loop_filter_v_4_8_mmxext;
        dsp->loop_filter_8[1][0] = ff_vp9_loop_filter_h_8_8_mmxext;
        dsp->loop_filter_8[1][1] = ff_vp9_loop_filter_v_8_8_mmxext;
        init_subpel2(4, 0, 4, put, 8, mmxext);
        init_subpel2(4, 1, 4, avg, 8, mmxext);
        init_fpel_func(4, 1,  4, avg, _8, mmxext);
        init_fpel_func(3, 1,  8, avg, _8, mmxext);
        dsp->itxfm_add[TX_4X4][DCT_DCT] = ff_vp9_idct_idct_4x4_add_mmxext;
        init_dc_ipred(4, mmxext);
        init_dc_ipred(8, mmxext);
        init_dir_tm_ipred(4, mmxext);
    }

    if (EXTERNAL_SSE(cpu_flags)) {
        init_fpel_func(2, 0, 16, put, , sse);
        init_fpel_func(1, 0, 32, put, , sse);
        init_fpel_func(0, 0, 64, put, , sse);
        init_ipred(16, sse, v, VERT);
        init_ipred(32, sse, v, VERT);
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        init_subpel3_8to64(0, put, 8, sse2);
        init_subpel3_8to64(1, avg, 8, sse2);
        init_fpel_func(2, 1, 16, avg,  _8, sse2);
        init_fpel_func(1, 1, 32, avg,  _8, sse2);
        init_fpel_func(0, 1, 64, avg,  _8, sse2);
        init_lpf(sse2);
        dsp->itxfm_add[TX_4X4][ADST_DCT]  = ff_vp9_idct_iadst_4x4_add_sse2;
        dsp->itxfm_add[TX_4X4][DCT_ADST]  = ff_vp9_iadst_idct_4x4_add_sse2;
        dsp->itxfm_add[TX_4X4][ADST_ADST] = ff_vp9_iadst_iadst_4x4_add_sse2;
        dsp->itxfm_add[TX_8X8][DCT_DCT] = ff_vp9_idct_idct_8x8_add_sse2;
        dsp->itxfm_add[TX_8X8][ADST_DCT]  = ff_vp9_idct_iadst_8x8_add_sse2;
        dsp->itxfm_add[TX_8X8][DCT_ADST]  = ff_vp9_iadst_idct_8x8_add_sse2;
        dsp->itxfm_add[TX_8X8][ADST_ADST] = ff_vp9_iadst_iadst_8x8_add_sse2;
        dsp->itxfm_add[TX_16X16][DCT_DCT]   = ff_vp9_idct_idct_16x16_add_sse2;
        dsp->itxfm_add[TX_16X16][ADST_DCT]  = ff_vp9_idct_iadst_16x16_add_sse2;
        dsp->itxfm_add[TX_16X16][DCT_ADST]  = ff_vp9_iadst_idct_16x16_add_sse2;
        dsp->itxfm_add[TX_16X16][ADST_ADST] = ff_vp9_iadst_iadst_16x16_add_sse2;
        dsp->itxfm_add[TX_32X32][ADST_ADST] =
        dsp->itxfm_add[TX_32X32][ADST_DCT] =
        dsp->itxfm_add[TX_32X32][DCT_ADST] =
        dsp->itxfm_add[TX_32X32][DCT_DCT] = ff_vp9_idct_idct_32x32_add_sse2;
        init_dc_ipred(16, sse2);
        init_dc_ipred(32, sse2);
        init_dir_tm_h_ipred(8, sse2);
        init_dir_tm_h_ipred(16, sse2);
        init_dir_tm_h_ipred(32, sse2);
        init_ipred(4, sse2, h, HOR);
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        init_subpel3(0, put, 8, ssse3);
        init_subpel3(1, avg, 8, ssse3);
        dsp->itxfm_add[TX_4X4][DCT_DCT] = ff_vp9_idct_idct_4x4_add_ssse3;
        dsp->itxfm_add[TX_4X4][ADST_DCT]  = ff_vp9_idct_iadst_4x4_add_ssse3;
        dsp->itxfm_add[TX_4X4][DCT_ADST]  = ff_vp9_iadst_idct_4x4_add_ssse3;
        dsp->itxfm_add[TX_4X4][ADST_ADST] = ff_vp9_iadst_iadst_4x4_add_ssse3;
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
        init_lpf(ssse3);
        init_all_ipred(4, ssse3);
        init_all_ipred(8, ssse3);
        init_all_ipred(16, ssse3);
        init_all_ipred(32, ssse3);
    }

    if (EXTERNAL_AVX(cpu_flags)) {
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
        init_lpf(avx);
        init_dir_tm_h_ipred(8, avx);
        init_dir_tm_h_ipred(16, avx);
        init_dir_tm_h_ipred(32, avx);
    }
    if (EXTERNAL_AVX_FAST(cpu_flags)) {
        init_fpel_func(1, 0, 32, put, , avx);
        init_fpel_func(0, 0, 64, put, , avx);
        init_ipred(32, avx, v, VERT);
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        init_fpel_func(1, 1, 32, avg, _8, avx2);
        init_fpel_func(0, 1, 64, avg, _8, avx2);
        if (ARCH_X86_64) {
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
            dsp->itxfm_add[TX_16X16][DCT_DCT] = ff_vp9_idct_idct_16x16_add_avx2;
            dsp->itxfm_add[TX_16X16][ADST_DCT]  = ff_vp9_idct_iadst_16x16_add_avx2;
            dsp->itxfm_add[TX_16X16][DCT_ADST]  = ff_vp9_iadst_idct_16x16_add_avx2;
            dsp->itxfm_add[TX_16X16][ADST_ADST] = ff_vp9_iadst_iadst_16x16_add_avx2;
            dsp->itxfm_add[TX_32X32][ADST_ADST] =
            dsp->itxfm_add[TX_32X32][ADST_DCT] =
            dsp->itxfm_add[TX_32X32][DCT_ADST] =
            dsp->itxfm_add[TX_32X32][DCT_DCT] = ff_vp9_idct_idct_32x32_add_avx2;
            init_subpel3_32_64(0, put, 8, avx2);
            init_subpel3_32_64(1, avg, 8, avx2);
#endif
        }
        init_dc_ipred(32, avx2);
        init_ipred(32, avx2, h,  HOR);
        init_ipred(32, avx2, tm, TM_VP8);
    }

#undef init_fpel
#undef init_subpel1
#undef init_subpel2
#undef init_subpel3

#endif /* HAVE_X86ASM */
}
