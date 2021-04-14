/*
 * Opus encoder assembly optimizations
 * Copyright (C) 2017 Ivan Kalvachev <ikalvachev@gmail.com>
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

#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/opus_pvq.h"

extern float ff_pvq_search_approx_sse2(float *X, int *y, int K, int N);
extern float ff_pvq_search_approx_sse4(float *X, int *y, int K, int N);
extern float ff_pvq_search_exact_avx  (float *X, int *y, int K, int N);

av_cold void ff_celt_pvq_init_x86(CeltPVQ *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags))
        s->pvq_search = ff_pvq_search_approx_sse2;

    if (EXTERNAL_SSE4(cpu_flags))
        s->pvq_search = ff_pvq_search_approx_sse4;

    if (EXTERNAL_AVX_FAST(cpu_flags))
        s->pvq_search = ff_pvq_search_exact_avx;
}
