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

int swri_resample_common_int16_mmx2 (ResampleContext *c, int16_t *dst, const int16_t *src, int n, int update_ctx);
int swri_resample_linear_int16_mmx2 (ResampleContext *c, int16_t *dst, const int16_t *src, int n, int update_ctx);
int swri_resample_common_int16_sse2 (ResampleContext *c, int16_t *dst, const int16_t *src, int n, int update_ctx);
int swri_resample_linear_int16_sse2 (ResampleContext *c, int16_t *dst, const int16_t *src, int n, int update_ctx);
int swri_resample_common_float_sse  (ResampleContext *c,   float *dst, const   float *src, int n, int update_ctx);
int swri_resample_linear_float_sse  (ResampleContext *c,   float *dst, const   float *src, int n, int update_ctx);
int swri_resample_common_float_avx  (ResampleContext *c,   float *dst, const   float *src, int n, int update_ctx);
int swri_resample_linear_float_avx  (ResampleContext *c,   float *dst, const   float *src, int n, int update_ctx);
int swri_resample_common_double_sse2(ResampleContext *c,  double *dst, const  double *src, int n, int update_ctx);
int swri_resample_linear_double_sse2(ResampleContext *c,  double *dst, const  double *src, int n, int update_ctx);

#if HAVE_MMXEXT_INLINE

#define DO_RESAMPLE_ONE 0

#include "resample_mmx.h"

#if ARCH_X86_32
#define TEMPLATE_RESAMPLE_S16_MMX2
#include "libswresample/resample_template.c"
#undef TEMPLATE_RESAMPLE_S16_MMX2
#endif

#if HAVE_SSE_INLINE
#define TEMPLATE_RESAMPLE_FLT_SSE
#include "libswresample/resample_template.c"
#undef TEMPLATE_RESAMPLE_FLT_SSE
#endif

#if HAVE_SSE2_INLINE
#define TEMPLATE_RESAMPLE_S16_SSE2
#include "libswresample/resample_template.c"
#undef TEMPLATE_RESAMPLE_S16_SSE2

#define TEMPLATE_RESAMPLE_DBL_SSE2
#include "libswresample/resample_template.c"
#undef TEMPLATE_RESAMPLE_DBL_SSE2
#endif

#if HAVE_AVX_INLINE
#define TEMPLATE_RESAMPLE_FLT_AVX
#include "libswresample/resample_template.c"
#undef TEMPLATE_RESAMPLE_FLT_AVX
#endif

#undef DO_RESAMPLE_ONE

#endif // HAVE_MMXEXT_INLINE

void swresample_dsp_x86_init(ResampleContext *c)
{
    int av_unused mm_flags = av_get_cpu_flags();

#define FNIDX(fmt) (AV_SAMPLE_FMT_##fmt - AV_SAMPLE_FMT_S16P)
    if (ARCH_X86_32 && HAVE_MMXEXT_INLINE && mm_flags & AV_CPU_FLAG_MMX2) {
        c->dsp.resample_common[FNIDX(S16P)] = (resample_fn) swri_resample_common_int16_mmx2;
        c->dsp.resample_linear[FNIDX(S16P)] = (resample_fn) swri_resample_linear_int16_mmx2;
    }
    if (HAVE_SSE_INLINE && mm_flags & AV_CPU_FLAG_SSE) {
        c->dsp.resample_common[FNIDX(FLTP)] = (resample_fn) swri_resample_common_float_sse;
        c->dsp.resample_linear[FNIDX(FLTP)] = (resample_fn) swri_resample_linear_float_sse;
    }
    if (HAVE_SSE2_INLINE && mm_flags & AV_CPU_FLAG_SSE2) {
        c->dsp.resample_common[FNIDX(S16P)] = (resample_fn) swri_resample_common_int16_sse2;
        c->dsp.resample_linear[FNIDX(S16P)] = (resample_fn) swri_resample_linear_int16_sse2;
        c->dsp.resample_common[FNIDX(DBLP)] = (resample_fn) swri_resample_common_double_sse2;
        c->dsp.resample_linear[FNIDX(DBLP)] = (resample_fn) swri_resample_linear_double_sse2;
    }
    if (HAVE_AVX_INLINE && mm_flags & AV_CPU_FLAG_AVX) {
        c->dsp.resample_common[FNIDX(FLTP)] = (resample_fn) swri_resample_common_float_avx;
        c->dsp.resample_linear[FNIDX(FLTP)] = (resample_fn) swri_resample_linear_float_avx;
    }
}
