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

#if HAVE_X86ASM

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

decl_ipred_fns(v,       16, mmx,    sse);
decl_ipred_fns(h,       16, mmxext, sse2);
decl_ipred_fns(dc,      16, mmxext, sse2);
decl_ipred_fns(dc_top,  16, mmxext, sse2);
decl_ipred_fns(dc_left, 16, mmxext, sse2);
decl_ipred_fn(dl,       16,     16, avx2);
decl_ipred_fn(dl,       32,     16, avx2);
decl_ipred_fn(dr,       16,     16, avx2);
decl_ipred_fn(dr,       32,     16, avx2);

#define decl_ipred_dir_funcs(type) \
decl_ipred_fns(type, 16, sse2,  sse2); \
decl_ipred_fns(type, 16, ssse3, ssse3); \
decl_ipred_fns(type, 16, avx,   avx)

decl_ipred_dir_funcs(dl);
decl_ipred_dir_funcs(dr);
decl_ipred_dir_funcs(vl);
decl_ipred_dir_funcs(vr);
decl_ipred_dir_funcs(hu);
decl_ipred_dir_funcs(hd);
#endif /* HAVE_X86ASM */

av_cold void ff_vp9dsp_init_16bpp_x86(VP9DSPContext *dsp)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        init_fpel_func(4, 0,   8, put, , mmx);
        init_ipred_func(v, VERT, 4, 16, mmx);
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        init_fpel_func(4, 1,   8, avg, _16, mmxext);
        init_ipred_func(h, HOR, 4, 16, mmxext);
        init_ipred_func(dc, DC, 4, 16, mmxext);
        init_ipred_func(dc_top,  TOP_DC,  4, 16, mmxext);
        init_ipred_func(dc_left, LEFT_DC, 4, 16, mmxext);
    }

    if (EXTERNAL_SSE(cpu_flags)) {
        init_fpel_func(3, 0,  16, put, , sse);
        init_fpel_func(2, 0,  32, put, , sse);
        init_fpel_func(1, 0,  64, put, , sse);
        init_fpel_func(0, 0, 128, put, , sse);
        init_8_16_32_ipred_funcs(v, VERT, 16, sse);
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        init_fpel_func(3, 1,  16, avg, _16, sse2);
        init_fpel_func(2, 1,  32, avg, _16, sse2);
        init_fpel_func(1, 1,  64, avg, _16, sse2);
        init_fpel_func(0, 1, 128, avg, _16, sse2);
        init_8_16_32_ipred_funcs(h, HOR, 16, sse2);
        init_8_16_32_ipred_funcs(dc, DC, 16, sse2);
        init_8_16_32_ipred_funcs(dc_top,  TOP_DC,  16, sse2);
        init_8_16_32_ipred_funcs(dc_left, LEFT_DC, 16, sse2);
        init_ipred_funcs(dl, DIAG_DOWN_LEFT, 16, sse2);
        init_ipred_funcs(dr, DIAG_DOWN_RIGHT, 16, sse2);
        init_ipred_funcs(vl, VERT_LEFT, 16, sse2);
        init_ipred_funcs(vr, VERT_RIGHT, 16, sse2);
        init_ipred_funcs(hu, HOR_UP, 16, sse2);
        init_ipred_funcs(hd, HOR_DOWN, 16, sse2);
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        init_ipred_funcs(dl, DIAG_DOWN_LEFT, 16, ssse3);
        init_ipred_funcs(dr, DIAG_DOWN_RIGHT, 16, ssse3);
        init_ipred_funcs(vl, VERT_LEFT, 16, ssse3);
        init_ipred_funcs(vr, VERT_RIGHT, 16, ssse3);
        init_ipred_funcs(hu, HOR_UP, 16, ssse3);
        init_ipred_funcs(hd, HOR_DOWN, 16, ssse3);
    }

    if (EXTERNAL_AVX_FAST(cpu_flags)) {
        init_fpel_func(2, 0,  32, put, , avx);
        init_fpel_func(1, 0,  64, put, , avx);
        init_fpel_func(0, 0, 128, put, , avx);
        init_ipred_funcs(dl, DIAG_DOWN_LEFT, 16, avx);
        init_ipred_funcs(dr, DIAG_DOWN_RIGHT, 16, avx);
        init_ipred_funcs(vl, VERT_LEFT, 16, avx);
        init_ipred_funcs(vr, VERT_RIGHT, 16, avx);
        init_ipred_funcs(hu, HOR_UP, 16, avx);
        init_ipred_funcs(hd, HOR_DOWN, 16, avx);
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        init_fpel_func(2, 1,  32, avg, _16, avx2);
        init_fpel_func(1, 1,  64, avg, _16, avx2);
        init_fpel_func(0, 1, 128, avg, _16, avx2);
        init_ipred_func(dl, DIAG_DOWN_LEFT, 16, 16, avx2);
        init_ipred_func(dl, DIAG_DOWN_LEFT, 32, 16, avx2);
        init_ipred_func(dr, DIAG_DOWN_RIGHT, 16, 16, avx2);
#if ARCH_X86_64
        init_ipred_func(dr, DIAG_DOWN_RIGHT, 32, 16, avx2);
#endif
    }

#endif /* HAVE_X86ASM */
}
