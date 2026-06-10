/*
 * Copyright (c) 2026 DROOdotFOO <drew@axol.io>
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
#include "libavutil/aarch64/cpu.h"
#include "libavfilter/threshold.h"

void ff_threshold8_neon(const uint8_t *in, const uint8_t *threshold,
                        const uint8_t *min, const uint8_t *max,
                        uint8_t *out,
                        ptrdiff_t ilinesize, ptrdiff_t tlinesize,
                        ptrdiff_t flinesize, ptrdiff_t slinesize,
                        ptrdiff_t olinesize,
                        int w, int h);

void ff_threshold16_neon(const uint8_t *in, const uint8_t *threshold,
                         const uint8_t *min, const uint8_t *max,
                         uint8_t *out,
                         ptrdiff_t ilinesize, ptrdiff_t tlinesize,
                         ptrdiff_t flinesize, ptrdiff_t slinesize,
                         ptrdiff_t olinesize,
                         int w, int h);

av_cold void ff_threshold_init_aarch64(ThresholdContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        if (s->depth == 8)
            s->threshold = ff_threshold8_neon;
        else
            s->threshold = ff_threshold16_neon;
    }
}
