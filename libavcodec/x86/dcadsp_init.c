/*
 * Copyright (c) 2012-2014 Christophe Gisquet <christophe.gisquet@gmail.com>
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
#include "libavcodec/dcadsp.h"

void ff_dca_lfe_fir0_sse(float *out, const float *in, const float *coefs);
void ff_dca_lfe_fir1_sse(float *out, const float *in, const float *coefs);
void ff_dca_lfe_fir0_fma3(float *out, const float *in, const float *coefs);

av_cold void ff_dcadsp_init_x86(DCADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
        s->lfe_fir[0]        = ff_dca_lfe_fir0_sse;
        s->lfe_fir[1]        = ff_dca_lfe_fir1_sse;
    }

    if (EXTERNAL_FMA3(cpu_flags)) {
        s->lfe_fir[0]        = ff_dca_lfe_fir0_fma3;
    }
}


#define SYNTH_FILTER_FUNC(opt)                                                 \
void ff_synth_filter_inner_##opt(float *synth_buf_ptr, float synth_buf2[32],   \
                                 const float window[512],                      \
                                 float out[32], intptr_t offset, float scale); \
static void synth_filter_##opt(FFTContext *imdct,                              \
                               float *synth_buf_ptr, int *synth_buf_offset,    \
                               float synth_buf2[32], const float window[512],  \
                               float out[32], const float in[32], float scale) \
{                                                                              \
    float *synth_buf= synth_buf_ptr + *synth_buf_offset;                       \
                                                                               \
    imdct->imdct_half(imdct, synth_buf, in);                                   \
                                                                               \
    ff_synth_filter_inner_##opt(synth_buf, synth_buf2, window,                 \
                                out, *synth_buf_offset, scale);                \
                                                                               \
    *synth_buf_offset = (*synth_buf_offset - 32) & 511;                        \
}                                                                              \

#if HAVE_YASM
#if ARCH_X86_32
SYNTH_FILTER_FUNC(sse)
#endif
SYNTH_FILTER_FUNC(sse2)
SYNTH_FILTER_FUNC(avx)
SYNTH_FILTER_FUNC(fma3)
#endif /* HAVE_YASM */

av_cold void ff_synth_filter_init_x86(SynthFilterContext *s)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (EXTERNAL_SSE(cpu_flags)) {
        s->synth_filter_float = synth_filter_sse;
    }
#endif
    if (EXTERNAL_SSE2(cpu_flags)) {
        s->synth_filter_float = synth_filter_sse2;
    }
    if (EXTERNAL_AVX_FAST(cpu_flags)) {
        s->synth_filter_float = synth_filter_avx;
    }
    if (EXTERNAL_FMA3(cpu_flags) && !(cpu_flags & AV_CPU_FLAG_AVXSLOW)) {
        s->synth_filter_float = synth_filter_fma3;
    }
#endif /* HAVE_YASM */
}
