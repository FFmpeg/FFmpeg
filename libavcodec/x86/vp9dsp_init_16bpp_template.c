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

mc_rep_funcs(16,  8, 16, sse2, int16_t, 16, BPC);
mc_rep_funcs(32, 16, 32, sse2, int16_t, 16, BPC);
mc_rep_funcs(64, 32, 64, sse2, int16_t, 16, BPC);
mc_rep_funcs(32, 16, 32, avx2, int16_t, 16, BPC);
mc_rep_funcs(64, 32, 64, avx2, int16_t, 16, BPC);

filters_8tap_2d_fn2(put, 16, BPC, 2, sse2, sse2, 16bpp)
filters_8tap_2d_fn2(avg, 16, BPC, 2, sse2, sse2, 16bpp)
filters_8tap_2d_fn(put, 64, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(avg, 64, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(put, 32, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(avg, 32, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(put, 16, 32, BPC, 2, avx2, 16bpp)
filters_8tap_2d_fn(avg, 16, 32, BPC, 2, avx2, 16bpp)

filters_8tap_1d_fn3(put, BPC, sse2, sse2, 16bpp)
filters_8tap_1d_fn3(avg, BPC, sse2, sse2, 16bpp)
filters_8tap_1d_fn2(put, 64, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(avg, 64, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(put, 32, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(avg, 32, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(put, 16, BPC, avx2, 16bpp)
filters_8tap_1d_fn2(avg, 16, BPC, avx2, 16bpp)

#endif /* HAVE_YASM */

av_cold void INIT_FUNC(VP9DSPContext *dsp)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        init_subpel3(0, put, BPC, sse2);
        init_subpel3(1, avg, BPC, sse2);
    }

    if (EXTERNAL_AVX2(cpu_flags)) {
        init_subpel3_32_64(0,  put, BPC, avx2);
        init_subpel3_32_64(1,  avg, BPC, avx2);
        init_subpel2(2, 0, 16, put, BPC, avx2);
        init_subpel2(2, 1, 16, avg, BPC, avx2);
    }

#endif /* HAVE_YASM */

    ff_vp9dsp_init_16bpp_x86(dsp);
}
