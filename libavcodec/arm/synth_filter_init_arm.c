/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

#include "config.h"

#include "libavutil/arm/cpu.h"
#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavcodec/fft.h"
#include "libavcodec/synth_filter.h"

void ff_synth_filter_float_vfp(FFTContext *imdct,
                               float *synth_buf_ptr, int *synth_buf_offset,
                               float synth_buf2[32], const float window[512],
                               float out[32], const float in[32],
                               float scale);

void ff_synth_filter_float_neon(FFTContext *imdct,
                                float *synth_buf_ptr, int *synth_buf_offset,
                                float synth_buf2[32], const float window[512],
                                float out[32], const float in[32],
                                float scale);

av_cold void ff_synth_filter_init_arm(SynthFilterContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_vfp_vm(cpu_flags))
        s->synth_filter_float = ff_synth_filter_float_vfp;
    if (have_neon(cpu_flags))
        s->synth_filter_float = ff_synth_filter_float_neon;
}
