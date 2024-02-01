/*
 * DSP for HEVC/VVC
 *
 * Copyright (C) 2022-2024 Nuo Mi
 * Copyright (c) 2023-2024 Wu Jianhua
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

#include "h2656dsp.h"

#define mc_rep_func(name, bitd, step, W, opt) \
void ff_h2656_put_##name##W##_##bitd##_##opt(int16_t *_dst, ptrdiff_t dststride,                                \
    const uint8_t *_src, ptrdiff_t _srcstride, int height, const int8_t *hf, const int8_t *vf, int width)       \
{                                                                                                               \
    int i;                                                                                                      \
    int16_t *dst;                                                                                               \
    for (i = 0; i < W; i += step) {                                                                             \
        const uint8_t *src  = _src + (i * ((bitd + 7) / 8));                                                    \
        dst = _dst + i;                                                                                         \
        ff_h2656_put_##name##step##_##bitd##_##opt(dst, dststride, src, _srcstride, height, hf, vf, width);     \
    }                                                                                                           \
}

#define mc_rep_uni_func(name, bitd, step, W, opt) \
void ff_h2656_put_uni_##name##W##_##bitd##_##opt(uint8_t *_dst, ptrdiff_t dststride,                            \
    const uint8_t *_src, ptrdiff_t _srcstride, int height, const int8_t *hf, const int8_t *vf, int width)       \
{                                                                                                               \
    int i;                                                                                                      \
    uint8_t *dst;                                                                                               \
    for (i = 0; i < W; i += step) {                                                                             \
        const uint8_t *src = _src + (i * ((bitd + 7) / 8));                                                     \
        dst = _dst + (i * ((bitd + 7) / 8));                                                                    \
        ff_h2656_put_uni_##name##step##_##bitd##_##opt(dst, dststride, src, _srcstride,                         \
                                                          height, hf, vf, width);                               \
    }                                                                                                           \
}

#define mc_rep_funcs(name, bitd, step, W, opt)      \
    mc_rep_func(name, bitd, step, W, opt)           \
    mc_rep_uni_func(name, bitd, step, W, opt)

#define MC_REP_FUNCS_SSE4(fname)                 \
    mc_rep_funcs(fname,  8, 16,128, sse4)        \
    mc_rep_funcs(fname,  8, 16, 64, sse4)        \
    mc_rep_funcs(fname,  8, 16, 32, sse4)        \
    mc_rep_funcs(fname, 10,  8,128, sse4)        \
    mc_rep_funcs(fname, 10,  8, 64, sse4)        \
    mc_rep_funcs(fname, 10,  8, 32, sse4)        \
    mc_rep_funcs(fname, 10,  8, 16, sse4)        \
    mc_rep_funcs(fname, 12,  8,128, sse4)        \
    mc_rep_funcs(fname, 12,  8, 64, sse4)        \
    mc_rep_funcs(fname, 12,  8, 32, sse4)        \
    mc_rep_funcs(fname, 12,  8, 16, sse4)        \

#if ARCH_X86_64 && HAVE_SSE4_EXTERNAL

MC_REP_FUNCS_SSE4(pixels)
MC_REP_FUNCS_SSE4(4tap_h)
MC_REP_FUNCS_SSE4(4tap_v)
MC_REP_FUNCS_SSE4(4tap_hv)
MC_REP_FUNCS_SSE4(8tap_h)
MC_REP_FUNCS_SSE4(8tap_v)
MC_REP_FUNCS_SSE4(8tap_hv)
mc_rep_funcs(8tap_hv, 8, 8, 16, sse4)

#if HAVE_AVX2_EXTERNAL

#define MC_REP_FUNCS_AVX2(fname)               \
    mc_rep_funcs(fname, 8, 32, 64, avx2)       \
    mc_rep_funcs(fname, 8, 32,128, avx2)       \
    mc_rep_funcs(fname,10, 16, 32, avx2)       \
    mc_rep_funcs(fname,10, 16, 64, avx2)       \
    mc_rep_funcs(fname,10, 16,128, avx2)       \
    mc_rep_funcs(fname,12, 16, 32, avx2)       \
    mc_rep_funcs(fname,12, 16, 64, avx2)       \
    mc_rep_funcs(fname,12, 16,128, avx2)       \

MC_REP_FUNCS_AVX2(pixels)
MC_REP_FUNCS_AVX2(8tap_h)
MC_REP_FUNCS_AVX2(8tap_v)
MC_REP_FUNCS_AVX2(8tap_hv)
MC_REP_FUNCS_AVX2(4tap_h)
MC_REP_FUNCS_AVX2(4tap_v)
MC_REP_FUNCS_AVX2(4tap_hv)
#endif
#endif
