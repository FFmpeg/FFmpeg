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

#include <stdint.h>

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

static void threshold8_tail(const uint8_t *in, const uint8_t *threshold,
                            const uint8_t *min, const uint8_t *max,
                            uint8_t *out,
                            ptrdiff_t ilinesize, ptrdiff_t tlinesize,
                            ptrdiff_t flinesize, ptrdiff_t slinesize,
                            ptrdiff_t olinesize,
                            int start, int end, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = start; x < end; x++)
            out[x] = in[x] <= threshold[x] ? min[x] : max[x];
        in        += ilinesize;
        threshold += tlinesize;
        min       += flinesize;
        max       += slinesize;
        out       += olinesize;
    }
}

static void threshold16_tail(const uint8_t *iin, const uint8_t *tthreshold,
                             const uint8_t *ffirst, const uint8_t *ssecond,
                             uint8_t *oout,
                             ptrdiff_t ilinesize, ptrdiff_t tlinesize,
                             ptrdiff_t flinesize, ptrdiff_t slinesize,
                             ptrdiff_t olinesize,
                             int start, int end, int h)
{
    const uint16_t *in        = (const uint16_t *)iin;
    const uint16_t *threshold = (const uint16_t *)tthreshold;
    const uint16_t *min       = (const uint16_t *)ffirst;
    const uint16_t *max       = (const uint16_t *)ssecond;
    uint16_t       *out       = (uint16_t *)oout;

    for (int y = 0; y < h; y++) {
        for (int x = start; x < end; x++)
            out[x] = in[x] <= threshold[x] ? min[x] : max[x];
        in        += ilinesize / 2;
        threshold += tlinesize / 2;
        min       += flinesize / 2;
        max       += slinesize / 2;
        out       += olinesize / 2;
    }
}

#define THRESHOLD_X86(depth, opt, vec_elements)                                              \
static void threshold##depth##_##opt##_x86(const uint8_t *in, const uint8_t *threshold,      \
                                           const uint8_t *min, const uint8_t *max,           \
                                           uint8_t *out,                                     \
                                           ptrdiff_t ilinesize, ptrdiff_t tlinesize,         \
                                           ptrdiff_t flinesize, ptrdiff_t slinesize,         \
                                           ptrdiff_t olinesize,                              \
                                           int w, int h)                                     \
{                                                                                            \
    int aligned_w = w & ~((vec_elements) - 1);                                               \
    if (aligned_w > 0)                                                                       \
        ff_threshold##depth##_##opt(in, threshold, min, max, out,                            \
                                    ilinesize, tlinesize, flinesize, slinesize, olinesize,   \
                                    aligned_w, h);                                           \
    if (aligned_w < w)                                                                       \
        threshold##depth##_tail(in, threshold, min, max, out,                                \
                                ilinesize, tlinesize, flinesize, slinesize, olinesize,       \
                                aligned_w, w, h);                                            \
}

THRESHOLD_X86(8,  sse4, 16)
THRESHOLD_X86(8,  avx2, 32)
THRESHOLD_X86(16, sse4,  8)
THRESHOLD_X86(16, avx2, 16)

av_cold void ff_threshold_init_x86(ThresholdContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (s->depth == 8) {
        if (EXTERNAL_SSE4(cpu_flags)) {
            s->threshold = threshold8_sse4_x86;
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            s->threshold = threshold8_avx2_x86;
        }
    } else if (s->depth == 16) {
        if (EXTERNAL_SSE4(cpu_flags)) {
            s->threshold = threshold16_sse4_x86;
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            s->threshold = threshold16_avx2_x86;
        }
    }
}
