/*
 * Copyright (c) 2016 Google Inc.
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
#include "libavcodec/vp9dsp.h"
#include "vp9dsp_init.h"

#define declare_fpel(type, sz)                                          \
void ff_vp9_##type##sz##_neon(uint8_t *dst, ptrdiff_t dst_stride,       \
                              const uint8_t *src, ptrdiff_t src_stride, \
                              int h, int mx, int my)

#define declare_copy_avg(sz) \
    declare_fpel(copy, sz);  \
    declare_fpel(avg , sz)

#define decl_mc_func(op, filter, dir, sz)                                                \
void ff_vp9_##op##_##filter##sz##_##dir##_neon(uint8_t *dst, ptrdiff_t dst_stride,       \
                                               const uint8_t *src, ptrdiff_t src_stride, \
                                               int h, int mx, int my)

#define define_8tap_2d_fn(op, filter, sz)                                         \
static void op##_##filter##sz##_hv_neon(uint8_t *dst, ptrdiff_t dst_stride,       \
                                        const uint8_t *src, ptrdiff_t src_stride, \
                                        int h, int mx, int my)                    \
{                                                                                 \
    LOCAL_ALIGNED_16(uint8_t, temp, [((1 + (sz < 64)) * sz + 8) * sz]);           \
    /* We only need h + 7 lines, but the horizontal filter assumes an             \
     * even number of rows, so filter h + 8 lines here. */                        \
    ff_vp9_put_##filter##sz##_h_neon(temp, sz,                                    \
                                     src - 3 * src_stride, src_stride,            \
                                     h + 8, mx, 0);                               \
    ff_vp9_##op##_##filter##sz##_v_neon(dst, dst_stride,                          \
                                        temp + 3 * sz, sz,                        \
                                        h, 0, my);                                \
}

#define decl_filter_funcs(op, dir, sz)  \
    decl_mc_func(op, regular, dir, sz); \
    decl_mc_func(op, sharp,   dir, sz); \
    decl_mc_func(op, smooth,  dir, sz)

#define decl_mc_funcs(sz)           \
    decl_filter_funcs(put, h,  sz); \
    decl_filter_funcs(avg, h,  sz); \
    decl_filter_funcs(put, v,  sz); \
    decl_filter_funcs(avg, v,  sz); \
    decl_filter_funcs(put, hv, sz); \
    decl_filter_funcs(avg, hv, sz)

#define ff_vp9_copy32_neon ff_vp9_copy32_aarch64
#define ff_vp9_copy64_neon ff_vp9_copy64_aarch64

declare_copy_avg(64);
declare_copy_avg(32);
declare_copy_avg(16);
declare_copy_avg(8);
declare_copy_avg(4);

decl_mc_funcs(64);
decl_mc_funcs(32);
decl_mc_funcs(16);
decl_mc_funcs(8);
decl_mc_funcs(4);

#define define_8tap_2d_funcs(sz)        \
    define_8tap_2d_fn(put, regular, sz) \
    define_8tap_2d_fn(put, sharp,   sz) \
    define_8tap_2d_fn(put, smooth,  sz) \
    define_8tap_2d_fn(avg, regular, sz) \
    define_8tap_2d_fn(avg, sharp,   sz) \
    define_8tap_2d_fn(avg, smooth,  sz)

define_8tap_2d_funcs(64)
define_8tap_2d_funcs(32)
define_8tap_2d_funcs(16)
define_8tap_2d_funcs(8)
define_8tap_2d_funcs(4)

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

#define init_copy_avg(idx, sz) \
    init_copy(idx, sz, _neon); \
    init_avg (idx, sz, _neon)

    if (have_armv8(cpu_flags)) {
        init_copy(0, 64, _aarch64);
        init_copy(1, 32, _aarch64);
    }

    if (have_neon(cpu_flags)) {
#define init_mc_func(idx1, idx2, op, filter, fname, dir, mx, my, sz, pfx) \
    dsp->mc[idx1][filter][idx2][mx][my] = pfx##op##_##fname##sz##_##dir##_neon

#define init_mc_funcs(idx, dir, mx, my, sz, pfx)                                   \
    init_mc_func(idx, 0, put, FILTER_8TAP_REGULAR, regular, dir, mx, my, sz, pfx); \
    init_mc_func(idx, 0, put, FILTER_8TAP_SHARP,   sharp,   dir, mx, my, sz, pfx); \
    init_mc_func(idx, 0, put, FILTER_8TAP_SMOOTH,  smooth,  dir, mx, my, sz, pfx); \
    init_mc_func(idx, 1, avg, FILTER_8TAP_REGULAR, regular, dir, mx, my, sz, pfx); \
    init_mc_func(idx, 1, avg, FILTER_8TAP_SHARP,   sharp,   dir, mx, my, sz, pfx); \
    init_mc_func(idx, 1, avg, FILTER_8TAP_SMOOTH,  smooth,  dir, mx, my, sz, pfx)

#define init_mc_funcs_dirs(idx, sz)            \
    init_mc_funcs(idx, h,  1, 0, sz, ff_vp9_); \
    init_mc_funcs(idx, v,  0, 1, sz, ff_vp9_); \
    init_mc_funcs(idx, hv, 1, 1, sz,)

        init_avg(0, 64, _neon);
        init_avg(1, 32, _neon);
        init_copy_avg(2, 16);
        init_copy_avg(3, 8);
        init_copy_avg(4, 4);

        init_mc_funcs_dirs(0, 64);
        init_mc_funcs_dirs(1, 32);
        init_mc_funcs_dirs(2, 16);
        init_mc_funcs_dirs(3, 8);
        init_mc_funcs_dirs(4, 4);
    }
}

#define define_itxfm(type_a, type_b, sz)                                   \
void ff_vp9_##type_a##_##type_b##_##sz##x##sz##_add_neon(uint8_t *_dst,    \
                                                         ptrdiff_t stride, \
                                                         int16_t *_block, int eob)

#define define_itxfm_funcs(sz)      \
    define_itxfm(idct,  idct,  sz); \
    define_itxfm(iadst, idct,  sz); \
    define_itxfm(idct,  iadst, sz); \
    define_itxfm(iadst, iadst, sz)

define_itxfm_funcs(4);
define_itxfm_funcs(8);
define_itxfm_funcs(16);
define_itxfm(idct, idct, 32);
define_itxfm(iwht, iwht, 4);


static av_cold void vp9dsp_itxfm_init_aarch64(VP9DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
#define init_itxfm(tx, sz)                                             \
    dsp->itxfm_add[tx][DCT_DCT]   = ff_vp9_idct_idct_##sz##_add_neon;  \
    dsp->itxfm_add[tx][DCT_ADST]  = ff_vp9_iadst_idct_##sz##_add_neon; \
    dsp->itxfm_add[tx][ADST_DCT]  = ff_vp9_idct_iadst_##sz##_add_neon; \
    dsp->itxfm_add[tx][ADST_ADST] = ff_vp9_iadst_iadst_##sz##_add_neon

#define init_idct(tx, nm)           \
    dsp->itxfm_add[tx][DCT_DCT]   = \
    dsp->itxfm_add[tx][ADST_DCT]  = \
    dsp->itxfm_add[tx][DCT_ADST]  = \
    dsp->itxfm_add[tx][ADST_ADST] = ff_vp9_##nm##_add_neon

        init_itxfm(TX_4X4, 4x4);
        init_itxfm(TX_8X8, 8x8);
        init_itxfm(TX_16X16, 16x16);
        init_idct(TX_32X32, idct_idct_32x32);
        init_idct(4, iwht_iwht_4x4);
    }
}

#define define_loop_filter(dir, wd, len) \
void ff_vp9_loop_filter_##dir##_##wd##_##len##_neon(uint8_t *dst, ptrdiff_t stride, int E, int I, int H)

#define define_loop_filters(wd, len) \
    define_loop_filter(h, wd, len);  \
    define_loop_filter(v, wd, len)

define_loop_filters(4, 8);
define_loop_filters(8, 8);
define_loop_filters(16, 8);

define_loop_filters(16, 16);

define_loop_filters(44, 16);
define_loop_filters(48, 16);
define_loop_filters(84, 16);
define_loop_filters(88, 16);

static av_cold void vp9dsp_loopfilter_init_aarch64(VP9DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        dsp->loop_filter_8[0][1] = ff_vp9_loop_filter_v_4_8_neon;
        dsp->loop_filter_8[0][0] = ff_vp9_loop_filter_h_4_8_neon;
        dsp->loop_filter_8[1][1] = ff_vp9_loop_filter_v_8_8_neon;
        dsp->loop_filter_8[1][0] = ff_vp9_loop_filter_h_8_8_neon;
        dsp->loop_filter_8[2][1] = ff_vp9_loop_filter_v_16_8_neon;
        dsp->loop_filter_8[2][0] = ff_vp9_loop_filter_h_16_8_neon;

        dsp->loop_filter_16[0] = ff_vp9_loop_filter_h_16_16_neon;
        dsp->loop_filter_16[1] = ff_vp9_loop_filter_v_16_16_neon;

        dsp->loop_filter_mix2[0][0][0] = ff_vp9_loop_filter_h_44_16_neon;
        dsp->loop_filter_mix2[0][0][1] = ff_vp9_loop_filter_v_44_16_neon;
        dsp->loop_filter_mix2[0][1][0] = ff_vp9_loop_filter_h_48_16_neon;
        dsp->loop_filter_mix2[0][1][1] = ff_vp9_loop_filter_v_48_16_neon;
        dsp->loop_filter_mix2[1][0][0] = ff_vp9_loop_filter_h_84_16_neon;
        dsp->loop_filter_mix2[1][0][1] = ff_vp9_loop_filter_v_84_16_neon;
        dsp->loop_filter_mix2[1][1][0] = ff_vp9_loop_filter_h_88_16_neon;
        dsp->loop_filter_mix2[1][1][1] = ff_vp9_loop_filter_v_88_16_neon;
    }
}

av_cold void ff_vp9dsp_init_aarch64(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 10) {
        ff_vp9dsp_init_10bpp_aarch64(dsp);
        return;
    } else if (bpp == 12) {
        ff_vp9dsp_init_12bpp_aarch64(dsp);
        return;
    } else if (bpp != 8)
        return;

    vp9dsp_mc_init_aarch64(dsp);
    vp9dsp_loopfilter_init_aarch64(dsp);
    vp9dsp_itxfm_init_aarch64(dsp);
}
