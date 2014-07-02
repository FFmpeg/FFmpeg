/*
 * audio resampling
 * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * audio resampling
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libswresample/resample.h"

#define RESAMPLE_FUNCS(type, opt) \
int ff_resample_common_##type##_##opt(ResampleContext *c, uint8_t *dst, \
                                      const uint8_t *src, int sz, int upd); \
int ff_resample_linear_##type##_##opt(ResampleContext *c, uint8_t *dst, \
                                      const uint8_t *src, int sz, int upd)

RESAMPLE_FUNCS(int16,  mmxext);
RESAMPLE_FUNCS(int16,  sse2);
RESAMPLE_FUNCS(int16,  xop);
RESAMPLE_FUNCS(float,  sse);
RESAMPLE_FUNCS(float,  avx);
RESAMPLE_FUNCS(float,  fma3);
RESAMPLE_FUNCS(float,  fma4);
RESAMPLE_FUNCS(double, sse2);

void swri_resample_dsp_x86_init(ResampleContext *c)
{
    int av_unused mm_flags = av_get_cpu_flags();

#define FNIDX(fmt) (AV_SAMPLE_FMT_##fmt - AV_SAMPLE_FMT_S16P)
    if (ARCH_X86_32 && HAVE_MMXEXT_EXTERNAL && mm_flags & AV_CPU_FLAG_MMX2) {
        c->dsp.resample_common[FNIDX(S16P)] = ff_resample_common_int16_mmxext;
        c->dsp.resample_linear[FNIDX(S16P)] = ff_resample_linear_int16_mmxext;
    }
    if (HAVE_SSE_EXTERNAL && mm_flags & AV_CPU_FLAG_SSE) {
        c->dsp.resample_common[FNIDX(FLTP)] = ff_resample_common_float_sse;
        c->dsp.resample_linear[FNIDX(FLTP)] = ff_resample_linear_float_sse;
    }
    if (HAVE_SSE2_EXTERNAL && mm_flags & AV_CPU_FLAG_SSE2) {
        c->dsp.resample_common[FNIDX(S16P)] = ff_resample_common_int16_sse2;
        c->dsp.resample_linear[FNIDX(S16P)] = ff_resample_linear_int16_sse2;

        c->dsp.resample_common[FNIDX(DBLP)] = ff_resample_common_double_sse2;
        c->dsp.resample_linear[FNIDX(DBLP)] = ff_resample_linear_double_sse2;
    }
    if (HAVE_AVX_EXTERNAL && mm_flags & AV_CPU_FLAG_AVX) {
        c->dsp.resample_common[FNIDX(FLTP)] = ff_resample_common_float_avx;
        c->dsp.resample_linear[FNIDX(FLTP)] = ff_resample_linear_float_avx;
    }
    if (HAVE_FMA3_EXTERNAL && mm_flags & AV_CPU_FLAG_FMA3) {
        c->dsp.resample_common[FNIDX(FLTP)] = ff_resample_common_float_fma3;
        c->dsp.resample_linear[FNIDX(FLTP)] = ff_resample_linear_float_fma3;
    }
    if (HAVE_FMA4_EXTERNAL && mm_flags & AV_CPU_FLAG_FMA4) {
        c->dsp.resample_common[FNIDX(FLTP)] = ff_resample_common_float_fma4;
        c->dsp.resample_linear[FNIDX(FLTP)] = ff_resample_linear_float_fma4;
    }
    if (HAVE_XOP_EXTERNAL && mm_flags & AV_CPU_FLAG_XOP) {
        c->dsp.resample_common[FNIDX(S16P)] = ff_resample_common_int16_xop;
        c->dsp.resample_linear[FNIDX(S16P)] = ff_resample_linear_int16_xop;
    }
}
