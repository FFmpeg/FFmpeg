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
#include "libavresample/audio_convert.h"

extern void ff_conv_fltp_to_flt_6ch_mmx (float *dst, float *const *src, int len);
extern void ff_conv_fltp_to_flt_6ch_sse4(float *dst, float *const *src, int len);
extern void ff_conv_fltp_to_flt_6ch_avx (float *dst, float *const *src, int len);

av_cold void ff_audio_convert_init_x86(AudioConvert *ac)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_MMX && HAVE_MMX) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                                  6, 1, 4, "MMX", ff_conv_fltp_to_flt_6ch_mmx);
    }
    if (mm_flags & AV_CPU_FLAG_SSE4 && HAVE_SSE) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                                  6, 16, 4, "SSE4", ff_conv_fltp_to_flt_6ch_sse4);
    }
    if (mm_flags & AV_CPU_FLAG_AVX && HAVE_AVX) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                                  6, 16, 4, "AVX", ff_conv_fltp_to_flt_6ch_avx);
    }
#endif
}
