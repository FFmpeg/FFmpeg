/*
 * Copyright (c) 2021 Mark Reid <mindmark@gmail.com>
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
#include "libavfilter/lut3d.h"

#define DEFINE_INTERP_FUNC(name, format, opt)                                                                                                       \
void ff_interp_##name##_##format##_##opt(LUT3DContext *lut3d, Lut3DPreLut *prelut, AVFrame *src, AVFrame *dst, int slice_start, int slice_end, int has_alpha); \
static int interp_##name##_##format##_##opt(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)                                                \
{                                                                                                                                                   \
    LUT3DContext *lut3d = ctx->priv;                                                                                                                \
    Lut3DPreLut *prelut = lut3d->prelut.size > 0? &lut3d->prelut: NULL;                                                                             \
    ThreadData *td = arg;                                                                                                                           \
    AVFrame *in  = td->in;                                                                                                                          \
    AVFrame *out = td->out;                                                                                                                         \
    int has_alpha = in->linesize[3] && out != in;                                                                                                   \
    int slice_start = (in->height *  jobnr   ) / nb_jobs;                                                                                           \
    int slice_end   = (in->height * (jobnr+1)) / nb_jobs;                                                                                           \
    ff_interp_##name##_##format##_##opt(lut3d, prelut, in, out, slice_start, slice_end, has_alpha);                                                            \
    return 0;                                                                                                                                       \
}

#if ARCH_X86_64
#if HAVE_AVX2_EXTERNAL
    DEFINE_INTERP_FUNC(tetrahedral, pf32, avx2)
    DEFINE_INTERP_FUNC(tetrahedral, p16,  avx2)
#endif
#if HAVE_AVX_EXTERNAL
    DEFINE_INTERP_FUNC(tetrahedral, pf32, avx)
    DEFINE_INTERP_FUNC(tetrahedral, p16,  avx)
#endif
#if HAVE_SSE2_EXTERNAL
    DEFINE_INTERP_FUNC(tetrahedral, pf32, sse2)
    DEFINE_INTERP_FUNC(tetrahedral, p16,  sse2)
#endif
#endif


av_cold void ff_lut3d_init_x86(LUT3DContext *s, const AVPixFmtDescriptor *desc)
{
    int cpu_flags = av_get_cpu_flags();
    int planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;
    int isfloat = desc->flags & AV_PIX_FMT_FLAG_FLOAT;
    int depth = desc->comp[0].depth;

#if ARCH_X86_64
    if (EXTERNAL_AVX2_FAST(cpu_flags) && EXTERNAL_FMA3(cpu_flags) && s->interpolation == INTERPOLATE_TETRAHEDRAL && planar) {
#if HAVE_AVX2_EXTERNAL
        if (isfloat && planar) {
            s->interp = interp_tetrahedral_pf32_avx2;
        } else if (depth == 16) {
            s->interp = interp_tetrahedral_p16_avx2;
        }
#endif
    } else if (EXTERNAL_AVX_FAST(cpu_flags) && s->interpolation == INTERPOLATE_TETRAHEDRAL && planar) {
#if HAVE_AVX_EXTERNAL
        if (isfloat) {
            s->interp = interp_tetrahedral_pf32_avx;
        } else if (depth == 16) {
            s->interp = interp_tetrahedral_p16_avx;
        }
#endif
    } else if (EXTERNAL_SSE2(cpu_flags) && s->interpolation == INTERPOLATE_TETRAHEDRAL && planar) {
#if HAVE_SSE2_EXTERNAL
        if (isfloat) {
            s->interp = interp_tetrahedral_pf32_sse2;
        } else if (depth == 16) {
            s->interp = interp_tetrahedral_p16_sse2;
        }
#endif
    }
#endif
}
