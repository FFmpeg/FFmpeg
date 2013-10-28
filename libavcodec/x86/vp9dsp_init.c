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

#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/vp9dsp.h"

#if HAVE_YASM

#define fpel_func(avg, sz, opt) \
void ff_##avg##sz##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                          const uint8_t *src, ptrdiff_t src_stride, \
                          int h, int mx, int my)
fpel_func(put,  4, mmx);
fpel_func(put,  8, mmx);
fpel_func(put, 16, sse);
fpel_func(put, 32, sse);
fpel_func(put, 64, sse);
fpel_func(avg,  4, sse);
fpel_func(avg,  8, sse);
fpel_func(avg, 16, sse2);
fpel_func(avg, 32, sse2);
fpel_func(avg, 64, sse2);
#undef fpel_func

#define mc_func(avg, sz, dir, opt) \
void ff_##avg##_8tap_1d_##dir##_##sz##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                             const uint8_t *src, ptrdiff_t src_stride, \
                                             int h, const int8_t (*filter)[16])
#define mc_funcs(sz) \
mc_func(put, sz, h, ssse3); \
mc_func(avg, sz, h, ssse3); \
mc_func(put, sz, v, ssse3); \
mc_func(avg, sz, v, ssse3)

mc_funcs(4);
mc_funcs(8);

#undef mc_funcs
#undef mc_func

#define mc_rep_func(avg, sz, hsz, dir, opt) \
static av_always_inline void \
ff_##avg##_8tap_1d_##dir##_##sz##_##opt(uint8_t *dst, ptrdiff_t dst_stride, \
                                        const uint8_t *src, ptrdiff_t src_stride, \
                                        int h, const int8_t (*filter)[16]) \
{ \
    ff_##avg##_8tap_1d_##dir##_##hsz##_##opt(dst,       dst_stride, src, \
                                             src_stride, h, filter); \
    ff_##avg##_8tap_1d_##dir##_##hsz##_##opt(dst + hsz, dst_stride, src + hsz, \
                                             src_stride, h, filter); \
}

#define mc_rep_funcs(sz, hsz) \
mc_rep_func(put, sz, hsz, h, ssse3); \
mc_rep_func(avg, sz, hsz, h, ssse3); \
mc_rep_func(put, sz, hsz, v, ssse3); \
mc_rep_func(avg, sz, hsz, v, ssse3)

mc_rep_funcs(16, 8);
mc_rep_funcs(32, 16);
mc_rep_funcs(64, 32);

#undef mc_rep_funcs
#undef mc_rep_func

extern const int8_t ff_filters_ssse3[3][15][4][16];

#define filter_8tap_2d_fn(op, sz, f, fname) \
static void op##_8tap_##fname##_##sz##hv_ssse3(uint8_t *dst, ptrdiff_t dst_stride, \
                                               const uint8_t *src, ptrdiff_t src_stride, \
                                               int h, int mx, int my) \
{ \
    LOCAL_ALIGNED_16(uint8_t, temp, [71 * 64]); \
    ff_put_8tap_1d_h_##sz##_ssse3(temp, 64, src - 3 * src_stride, src_stride, \
                                  h + 7, ff_filters_ssse3[f][mx - 1]); \
    ff_##op##_8tap_1d_v_##sz##_ssse3(dst, dst_stride, temp + 3 * 64, 64, \
                                     h, ff_filters_ssse3[f][my - 1]); \
}

#define filters_8tap_2d_fn(op, sz) \
filter_8tap_2d_fn(op, sz, FILTER_8TAP_REGULAR, regular) \
filter_8tap_2d_fn(op, sz, FILTER_8TAP_SHARP,   sharp) \
filter_8tap_2d_fn(op, sz, FILTER_8TAP_SMOOTH,  smooth)

#define filters_8tap_2d_fn2(op) \
filters_8tap_2d_fn(op, 64) \
filters_8tap_2d_fn(op, 32) \
filters_8tap_2d_fn(op, 16) \
filters_8tap_2d_fn(op, 8) \
filters_8tap_2d_fn(op, 4)

filters_8tap_2d_fn2(put)
filters_8tap_2d_fn2(avg)

#undef filters_8tap_2d_fn2
#undef filters_8tap_2d_fn
#undef filter_8tap_2d_fn

#define filter_8tap_1d_fn(op, sz, f, fname, dir, dvar) \
static void op##_8tap_##fname##_##sz##dir##_ssse3(uint8_t *dst, ptrdiff_t dst_stride, \
                                                  const uint8_t *src, ptrdiff_t src_stride, \
                                                  int h, int mx, int my) \
{ \
    ff_##op##_8tap_1d_##dir##_##sz##_ssse3(dst, dst_stride, src, src_stride, \
                                           h, ff_filters_ssse3[f][dvar - 1]); \
}

#define filters_8tap_1d_fn(op, sz, dir, dvar) \
filter_8tap_1d_fn(op, sz, FILTER_8TAP_REGULAR, regular, dir, dvar) \
filter_8tap_1d_fn(op, sz, FILTER_8TAP_SHARP,   sharp,   dir, dvar) \
filter_8tap_1d_fn(op, sz, FILTER_8TAP_SMOOTH,  smooth,  dir, dvar)

#define filters_8tap_1d_fn2(op, sz) \
filters_8tap_1d_fn(op, sz, h, mx) \
filters_8tap_1d_fn(op, sz, v, my)

#define filters_8tap_1d_fn3(op) \
filters_8tap_1d_fn2(op, 64) \
filters_8tap_1d_fn2(op, 32) \
filters_8tap_1d_fn2(op, 16) \
filters_8tap_1d_fn2(op, 8) \
filters_8tap_1d_fn2(op, 4)

filters_8tap_1d_fn3(put)
filters_8tap_1d_fn3(avg)

#undef filters_8tap_1d_fn
#undef filters_8tap_1d_fn2
#undef filters_8tap_1d_fn3
#undef filter_8tap_1d_fn

#endif /* HAVE_YASM */

av_cold void ff_vp9dsp_init_x86(VP9DSPContext *dsp)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#define init_fpel(idx1, idx2, sz, type, opt) \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][0][0] = \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][0][0] = \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][0][0] = ff_##type##sz##_##opt


#define init_subpel1(idx1, idx2, idxh, idxv, sz, dir, type, opt) \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][idxh][idxv] = type##_8tap_smooth_##sz##dir##_##opt; \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][idxh][idxv] = type##_8tap_regular_##sz##dir##_##opt; \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][idxh][idxv] = type##_8tap_sharp_##sz##dir##_##opt

#define init_subpel2(idx, idxh, idxv, dir, type, opt) \
    init_subpel1(0, idx, idxh, idxv, 64, dir, type, opt); \
    init_subpel1(1, idx, idxh, idxv, 32, dir, type, opt); \
    init_subpel1(2, idx, idxh, idxv, 16, dir, type, opt); \
    init_subpel1(3, idx, idxh, idxv,  8, dir, type, opt); \
    init_subpel1(4, idx, idxh, idxv,  4, dir, type, opt)

#define init_subpel3(idx, type, opt) \
    init_subpel2(idx, 1, 1, hv, type, opt); \
    init_subpel2(idx, 0, 1, v, type, opt); \
    init_subpel2(idx, 1, 0, h, type, opt)

    if (cpu_flags & AV_CPU_FLAG_MMX) {
        init_fpel(4, 0,  4, put, mmx);
        init_fpel(3, 0,  8, put, mmx);
    }

    if (cpu_flags & AV_CPU_FLAG_SSE) {
        init_fpel(2, 0, 16, put, sse);
        init_fpel(1, 0, 32, put, sse);
        init_fpel(0, 0, 64, put, sse);
        init_fpel(4, 1,  4, avg, sse);
        init_fpel(3, 1,  8, avg, sse);
    }

    if (cpu_flags & AV_CPU_FLAG_SSE2) {
        init_fpel(2, 1, 16, avg, sse2);
        init_fpel(1, 1, 32, avg, sse2);
        init_fpel(0, 1, 64, avg, sse2);
    }

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        init_subpel3(0, put, ssse3);
        init_subpel3(1, avg, ssse3);
    }

#undef init_fpel
#undef init_subpel1
#undef init_subpel2
#undef init_subpel3

#endif /* HAVE_YASM */
}
