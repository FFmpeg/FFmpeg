/*
 * Copyright (c) 2015 Paul B Mahol
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
#include "libavfilter/threshold.h"

#define THRESHOLD_FUNC(depth, opt) \
void ff_threshold##depth##_##opt(const uint8_t *in, const uint8_t *threshold,\
                                const uint8_t *min, const uint8_t *max,     \
                                uint8_t *out,                               \
                                ptrdiff_t ilinesize, ptrdiff_t tlinesize,   \
                                ptrdiff_t flinesize, ptrdiff_t slinesize,   \
                                ptrdiff_t olinesize,                        \
                                int w, int h);

THRESHOLD_FUNC(8, sse4)
THRESHOLD_FUNC(8, avx2)
THRESHOLD_FUNC(16, sse4)
THRESHOLD_FUNC(16, avx2)

av_cold void ff_threshold_init_x86(ThresholdContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (s->depth == 8) {
        if (EXTERNAL_SSE4(cpu_flags)) {
            s->threshold = ff_threshold8_sse4;
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            s->threshold = ff_threshold8_avx2;
        }
    } else if (s->depth == 16) {
        if (EXTERNAL_SSE4(cpu_flags)) {
            s->threshold = ff_threshold16_sse4;
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            s->threshold = ff_threshold16_avx2;
        }
    }
}
