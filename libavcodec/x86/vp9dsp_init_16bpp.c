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

decl_fpel_func(put,   8,    , mmx);
decl_fpel_func(avg,   8, _16, mmxext);
decl_fpel_func(put,  16,    , sse);
decl_fpel_func(put,  32,    , sse);
decl_fpel_func(put,  64,    , sse);
decl_fpel_func(put, 128,    , sse);
decl_fpel_func(avg,  16, _16, sse2);
decl_fpel_func(avg,  32, _16, sse2);
decl_fpel_func(avg,  64, _16, sse2);
decl_fpel_func(avg, 128, _16, sse2);
decl_fpel_func(put,  32,    , avx);
decl_fpel_func(put,  64,    , avx);
decl_fpel_func(put, 128,    , avx);
decl_fpel_func(avg,  32, _16, avx2);
decl_fpel_func(avg,  64, _16, avx2);
decl_fpel_func(avg, 128, _16, avx2);

#endif /* HAVE_YASM */

av_cold void ff_vp9dsp_init_16bpp_x86(VP9DSPContext *dsp)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        init_fpel_func(4, 0,   8, put, , mmx);
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        init_fpel_func(4, 1,   8, avg, _16, mmxext);
    }

    if (EXTERNAL_SSE(cpu_flags)) {
        init_fpel_func(3, 0,  16, put, , sse);
        init_fpel_func(2, 0,  32, put, , sse);
        init_fpel_func(1, 0,  64, put, , sse);
        init_fpel_func(0, 0, 128, put, , sse);
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        init_fpel_func(3, 1,  16, avg, _16, sse2);
        init_fpel_func(2, 1,  32, avg, _16, sse2);
        init_fpel_func(1, 1,  64, avg, _16, sse2);
        init_fpel_func(0, 1, 128, avg, _16, sse2);
    }

    if (EXTERNAL_AVX_FAST(cpu_flags)) {
        init_fpel_func(2, 0,  32, put, , avx);
        init_fpel_func(1, 0,  64, put, , avx);
        init_fpel_func(0, 0, 128, put, , avx);
    }

    if (EXTERNAL_AVX2(cpu_flags)) {
        init_fpel_func(2, 1,  32, avg, _16, avx2);
        init_fpel_func(1, 1,  64, avg, _16, avx2);
        init_fpel_func(0, 1, 128, avg, _16, avx2);
    }

#endif /* HAVE_YASM */
}
