/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
#include "libavutil/cpu.h"
#include "libavresample/audio_mix.h"

extern void ff_mix_2_to_1_fltp_flt_sse(float **src, float **matrix, int len,
                                       int out_ch, int in_ch);
extern void ff_mix_2_to_1_fltp_flt_avx(float **src, float **matrix, int len,
                                       int out_ch, int in_ch);

av_cold void ff_audio_mix_init_x86(AudioMix *am)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_SSE && HAVE_SSE) {
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                              2, 1, 16, 8, "SSE", ff_mix_2_to_1_fltp_flt_sse);
    }
    if (mm_flags & AV_CPU_FLAG_AVX && HAVE_AVX) {
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                              2, 1, 32, 16, "AVX", ff_mix_2_to_1_fltp_flt_avx);
    }
#endif
}
