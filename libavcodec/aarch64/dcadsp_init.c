/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavutil/aarch64/cpu.h"
#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavcodec/dcadsp.h"
#include "libavcodec/fft.h"

#include "asm-offsets.h"

#if HAVE_NEON || HAVE_VFP
AV_CHECK_OFFSET(FFTContext, imdct_half, IMDCT_HALF);
#endif

void ff_dca_lfe_fir0_neon(float *out, const float *in, const float *coefs);
void ff_dca_lfe_fir1_neon(float *out, const float *in, const float *coefs);

void ff_synth_filter_float_neon(FFTContext *imdct,
                                float *synth_buf_ptr, int *synth_buf_offset,
                                float synth_buf2[32], const float window[512],
                                float out[32], const float in[32],
                                float scale);

av_cold void ff_dcadsp_init_aarch64(DCADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->lfe_fir[0] = ff_dca_lfe_fir0_neon;
        s->lfe_fir[1] = ff_dca_lfe_fir1_neon;
    }
}

av_cold void ff_synth_filter_init_aarch64(SynthFilterContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags))
        s->synth_filter_float = ff_synth_filter_float_neon;
}
