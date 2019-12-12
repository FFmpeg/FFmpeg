/*
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

#include "libavutil/x86/cpu.h"

#include "libavfilter/limiter.h"

void ff_limiter_8bit_sse2(const uint8_t *src, uint8_t *dst,
                          ptrdiff_t slinesize, ptrdiff_t dlinesize,
                          int w, int h, int min, int max);
void ff_limiter_16bit_sse4(const uint8_t *src, uint8_t *dst,
                           ptrdiff_t slinesize, ptrdiff_t dlinesize,
                           int w, int h, int min, int max);

void ff_limiter_init_x86(LimiterDSPContext *dsp, int bpp)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        if (bpp <= 8) {
            dsp->limiter = ff_limiter_8bit_sse2;
        }
    }
    if (EXTERNAL_SSE4(cpu_flags)) {
        if (bpp > 8) {
            dsp->limiter = ff_limiter_16bit_sse4;
        }
    }
}
