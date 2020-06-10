/*
 * Copyright (c) 2017 Google Inc.
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/mem_internal.h"
#include "libavutil/aarch64/cpu.h"
#include "vp9dsp_init.h"

#define declare_fpel(type, sz, suffix)                                          \
void ff_vp9_##type##sz##suffix##_neon(uint8_t *dst, ptrdiff_t dst_stride,       \
                                      const uint8_t *src, ptrdiff_t src_stride, \
                                      int h, int mx, int my)

#define decl_mc_func(op, filter, dir, sz, bpp)                                                   \
void ff_vp9_##op##_##filter##sz##_##dir##_##bpp##_neon(uint8_t *dst, ptrdiff_t dst_stride,       \
                                                       const uint8_t *src, ptrdiff_t src_stride, \
                                                       int h, int mx, int my)

#define define_8tap_2d_fn(op, filter, sz, bpp)                                      \
static void op##_##filter##sz##_hv_##bpp##_neon(uint8_t *dst, ptrdiff_t dst_stride, \
                                                const uint8_t *src,                 \
                                                ptrdiff_t src_stride,               \
                                                int h, int mx, int my)              \
{                                                                                   \
    LOCAL_ALIGNED_16(uint8_t, temp, [((1 + (sz < 64)) * sz + 8) * sz * 2]);         \
    /* We only need h + 7 lines, but the horizontal filter assumes an               \
     * even number of rows, so filter h + 8 lines here. */                          \
    ff_vp9_put_##filter##sz##_h_##bpp##_neon(temp, 2 * sz,                          \
                                             src - 3 * src_stride, src_stride,      \
                                             h + 8, mx, 0);                         \
    ff_vp9_##op##_##filter##sz##_v_##bpp##_neon(dst, dst_stride,                    \
                                                temp + 3 * 2 * sz, 2 * sz,          \
                                                h, 0, my);                          \
}

#define decl_filter_funcs(op, dir, sz, bpp)  \
    decl_mc_func(op, regular, dir, sz, bpp); \
    decl_mc_func(op, sharp,   dir, sz, bpp); \
    decl_mc_func(op, smooth,  dir, sz, bpp)

#define decl_mc_funcs(sz, bpp)           \
    decl_filter_funcs(put, h,  sz, bpp); \
    decl_filter_funcs(avg, h,  sz, bpp); \
    decl_filter_funcs(put, v,  sz, bpp); \
    decl_filter_funcs(avg, v,  sz, bpp); \
    decl_filter_funcs(put, hv, sz, bpp); \
    decl_filter_funcs(avg, hv, sz, bpp)

#define ff_vp9_copy32_neon  ff_vp9_copy32_aarch64
#define ff_vp9_copy64_neon  ff_vp9_copy64_aarch64
#define ff_vp9_copy128_neon ff_vp9_copy128_aarch64

declare_fpel(copy, 128, );
declare_fpel(copy, 64,  );
declare_fpel(copy, 32,  );
declare_fpel(copy, 16,  );
declare_fpel(copy, 8,   );
declare_fpel(avg, 64, _16);
declare_fpel(avg, 32, _16);
declare_fpel(avg, 16, _16);
declare_fpel(avg, 8,  _16);
declare_fpel(avg, 4,  _16);

decl_mc_funcs(64, BPP);
decl_mc_funcs(32, BPP);
decl_mc_funcs(16, BPP);
decl_mc_funcs(8, BPP);
decl_mc_funcs(4, BPP);

#define define_8tap_2d_funcs(sz, bpp)        \
    define_8tap_2d_fn(put, regular, sz, bpp) \
    define_8tap_2d_fn(put, sharp,   sz, bpp) \
    define_8tap_2d_fn(put, smooth,  sz, bpp) \
    define_8tap_2d_fn(avg, regular, sz, bpp) \
    define_8tap_2d_fn(avg, sharp,   sz, bpp) \
    define_8tap_2d_fn(avg, smooth,  sz, bpp)

define_8tap_2d_funcs(64, BPP)
define_8tap_2d_funcs(32, BPP)
define_8tap_2d_funcs(16, BPP)
define_8tap_2d_funcs(8,  BPP)
define_8tap_2d_funcs(4,  BPP)

static av_cold void vp9dsp_mc_init_aarch64(VP9DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

#define init_fpel(idx1, idx2, sz, type, suffix)      \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][0][0] = ff_vp9_##type##sz##suffix

#define init_copy(idx, sz, suffix) \
    init_fpel(idx, 0, sz, copy, suffix)

#define init_avg(idx, sz, suffix) \
    init_fpel(idx, 1, sz, avg,  suffix)

#define init_copy_avg(idx, sz1, sz2) \
    init_copy(idx, sz2, _neon);      \
    init_avg (idx, sz1, _16_neon)

    if (have_armv8(cpu_flags)) {
        init_copy(0, 128, _aarch64);
        init_copy(1, 64,  _aarch64);
        init_copy(2, 32,  _aarch64);
    }

    if (have_neon(cpu_flags)) {
#define init_mc_func(idx1, idx2, op, filter, fname, dir, mx, my, sz, pfx, bpp) \
    dsp->mc[idx1][filter][idx2][mx][my] = pfx##op##_##fname##sz##_##dir##_##bpp##_neon

#define init_mc_funcs(idx, dir, mx, my, sz, pfx, bpp)                                   \
    init_mc_func(idx, 0, put, FILTER_8TAP_REGULAR, regular, dir, mx, my, sz, pfx, bpp); \
    init_mc_func(idx, 0, put, FILTER_8TAP_SHARP,   sharp,   dir, mx, my, sz, pfx, bpp); \
    init_mc_func(idx, 0, put, FILTER_8TAP_SMOOTH,  smooth,  dir, mx, my, sz, pfx, bpp); \
    init_mc_func(idx, 1, avg, FILTER_8TAP_REGULAR, regular, dir, mx, my, sz, pfx, bpp); \
    init_mc_func(idx, 1, avg, FILTER_8TAP_SHARP,   sharp,   dir, mx, my, sz, pfx, bpp); \
    init_mc_func(idx, 1, avg, FILTER_8TAP_SMOOTH,  smooth,  dir, mx, my, sz, pfx, bpp)

#define init_mc_funcs_dirs(idx, sz, bpp)            \
    init_mc_funcs(idx, v,  0, 1, sz, ff_vp9_, bpp); \
    init_mc_funcs(idx, h,  1, 0, sz, ff_vp9_, bpp); \
    init_mc_funcs(idx, hv, 1, 1, sz,        , bpp)


        init_avg(0, 64, _16_neon);
        init_avg(1, 32, _16_neon);
        init_avg(2, 16, _16_neon);
        init_copy_avg(3, 8, 16);
        init_copy_avg(4, 4, 8);

        init_mc_funcs_dirs(0, 64, BPP);
        init_mc_funcs_dirs(1, 32, BPP);
        init_mc_funcs_dirs(2, 16, BPP);
        init_mc_funcs_dirs(3, 8,  BPP);
        init_mc_funcs_dirs(4, 4,  BPP);
    }
}

#define define_itxfm2(type_a, type_b, sz, bpp)                                     \
void ff_vp9_##type_a##_##type_b##_##sz##x##sz##_add_##bpp##_neon(uint8_t *_dst,    \
                                                                 ptrdiff_t stride, \
                                                                 int16_t *_block, int eob)
#define define_itxfm(type_a, type_b, sz, bpp) define_itxfm2(type_a, type_b, sz, bpp)

#define define_itxfm_funcs(sz, bpp)      \
    define_itxfm(idct,  idct,  sz, bpp); \
    define_itxfm(iadst, idct,  sz, bpp); \
    define_itxfm(idct,  iadst, sz, bpp); \
    define_itxfm(iadst, iadst, sz, bpp)

define_itxfm_funcs(4,  BPP);
define_itxfm_funcs(8,  BPP);
define_itxfm_funcs(16, BPP);
define_itxfm(idct, idct, 32, BPP);
define_itxfm(iwht, iwht, 4,  BPP);


static av_cold void vp9dsp_itxfm_init_aarch64(VP9DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
#define init_itxfm2(tx, sz, bpp)                                               \
    dsp->itxfm_add[tx][DCT_DCT]   = ff_vp9_idct_idct_##sz##_add_##bpp##_neon;  \
    dsp->itxfm_add[tx][DCT_ADST]  = ff_vp9_iadst_idct_##sz##_add_##bpp##_neon; \
    dsp->itxfm_add[tx][ADST_DCT]  = ff_vp9_idct_iadst_##sz##_add_##bpp##_neon; \
    dsp->itxfm_add[tx][ADST_ADST] = ff_vp9_iadst_iadst_##sz##_add_##bpp##_neon
#define init_itxfm(tx, sz, bpp) init_itxfm2(tx, sz, bpp)

#define init_idct2(tx, nm, bpp)     \
    dsp->itxfm_add[tx][DCT_DCT]   = \
    dsp->itxfm_add[tx][ADST_DCT]  = \
    dsp->itxfm_add[tx][DCT_ADST]  = \
    dsp->itxfm_add[tx][ADST_ADST] = ff_vp9_##nm##_add_##bpp##_neon
#define init_idct(tx, nm, bpp) init_idct2(tx, nm, bpp)

        init_itxfm(TX_4X4,   4x4,   BPP);
        init_itxfm(TX_8X8,   8x8,   BPP);
        init_itxfm(TX_16X16, 16x16, BPP);
        init_idct(TX_32X32, idct_idct_32x32, BPP);
        init_idct(4,        iwht_iwht_4x4,   BPP);
    }
}

#define define_loop_filter(dir, wd, size, bpp) \
void ff_vp9_loop_filter_##dir##_##wd##_##size##_##bpp##_neon(uint8_t *dst, ptrdiff_t stride, int E, int I, int H)

#define define_loop_filters(wd, size, bpp) \
    define_loop_filter(h, wd, size, bpp);  \
    define_loop_filter(v, wd, size, bpp)

define_loop_filters(4,  8,  BPP);
define_loop_filters(8,  8,  BPP);
define_loop_filters(16, 8,  BPP);

define_loop_filters(16, 16, BPP);

define_loop_filters(44, 16, BPP);
define_loop_filters(48, 16, BPP);
define_loop_filters(84, 16, BPP);
define_loop_filters(88, 16, BPP);

static av_cold void vp9dsp_loopfilter_init_aarch64(VP9DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
#define init_lpf_func_8(idx1, idx2, dir, wd, bpp) \
    dsp->loop_filter_8[idx1][idx2] = ff_vp9_loop_filter_##dir##_##wd##_8_##bpp##_neon

#define init_lpf_func_16(idx, dir, bpp) \
    dsp->loop_filter_16[idx] = ff_vp9_loop_filter_##dir##_16_16_##bpp##_neon

#define init_lpf_func_mix2(idx1, idx2, idx3, dir, wd, bpp) \
    dsp->loop_filter_mix2[idx1][idx2][idx3] = ff_vp9_loop_filter_##dir##_##wd##_16_##bpp##_neon

#define init_lpf_funcs_8_wd(idx, wd, bpp) \
    init_lpf_func_8(idx, 0, h, wd, bpp);  \
    init_lpf_func_8(idx, 1, v, wd, bpp)

#define init_lpf_funcs_16(bpp)   \
    init_lpf_func_16(0, h, bpp); \
    init_lpf_func_16(1, v, bpp)

#define init_lpf_funcs_mix2_wd(idx1, idx2, wd, bpp) \
    init_lpf_func_mix2(idx1, idx2, 0, h, wd, bpp);  \
    init_lpf_func_mix2(idx1, idx2, 1, v, wd, bpp)

#define init_lpf_funcs_8(bpp)        \
    init_lpf_funcs_8_wd(0, 4,  bpp); \
    init_lpf_funcs_8_wd(1, 8,  bpp); \
    init_lpf_funcs_8_wd(2, 16, bpp)

#define init_lpf_funcs_mix2(bpp)           \
    init_lpf_funcs_mix2_wd(0, 0, 44, bpp); \
    init_lpf_funcs_mix2_wd(0, 1, 48, bpp); \
    init_lpf_funcs_mix2_wd(1, 0, 84, bpp); \
    init_lpf_funcs_mix2_wd(1, 1, 88, bpp)

        init_lpf_funcs_8(BPP);
        init_lpf_funcs_16(BPP);
        init_lpf_funcs_mix2(BPP);
    }
}

av_cold void INIT_FUNC(VP9DSPContext *dsp)
{
    vp9dsp_mc_init_aarch64(dsp);
    vp9dsp_loopfilter_init_aarch64(dsp);
    vp9dsp_itxfm_init_aarch64(dsp);
}
