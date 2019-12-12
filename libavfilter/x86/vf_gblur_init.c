/*
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
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/gblur.h"

void ff_horiz_slice_sse4(float *ptr, int width, int height, int steps, float nu, float bscale);
void ff_horiz_slice_avx2(float *ptr, int width, int height, int steps, float nu, float bscale);

av_cold void ff_gblur_init_x86(GBlurContext *s)
{
#if ARCH_X86_64
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE4(cpu_flags))
        s->horiz_slice = ff_horiz_slice_sse4;
    if (EXTERNAL_AVX2(cpu_flags))
        s->horiz_slice = ff_horiz_slice_avx2;
#endif
}
