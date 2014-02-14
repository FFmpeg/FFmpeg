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

void ff_int8x8_fmul_int32_sse(float *dst, const int8_t *src, int scale);
void ff_int8x8_fmul_int32_sse2(float *dst, const int8_t *src, int scale);
void ff_int8x8_fmul_int32_sse4(float *dst, const int8_t *src, int scale);
void ff_dca_lfe_fir0_sse(float *out, const float *in, const float *coefs);
void ff_dca_lfe_fir1_sse(float *out, const float *in, const float *coefs);

av_cold void ff_dcadsp_init_x86(DCADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
#if ARCH_X86_32
        s->int8x8_fmul_int32 = ff_int8x8_fmul_int32_sse;
#endif
        s->lfe_fir[0]        = ff_dca_lfe_fir0_sse;
        s->lfe_fir[1]        = ff_dca_lfe_fir1_sse;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        s->int8x8_fmul_int32 = ff_int8x8_fmul_int32_sse2;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        s->int8x8_fmul_int32 = ff_int8x8_fmul_int32_sse4;
    }
}

void ff_synth_filter_inner_sse2(float *synth_buf_ptr, float synth_buf2[32],
                                const float window[512],
                                float out[32], intptr_t offset, float scale);

static void synth_filter_sse2(FFTContext *imdct,
                              float *synth_buf_ptr, int *synth_buf_offset,
                              float synth_buf2[32], const float window[512],
                              float out[32], const float in[32], float scale)
{
    float *synth_buf= synth_buf_ptr + *synth_buf_offset;

    imdct->imdct_half(imdct, synth_buf, in);

    ff_synth_filter_inner_sse2(synth_buf, synth_buf2, window,
                               out, *synth_buf_offset, scale);

    *synth_buf_offset= (*synth_buf_offset - 32)&511;
}

av_cold void ff_synth_filter_init_x86(SynthFilterContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        s->synth_filter_float = synth_filter_sse2;
    }
}
