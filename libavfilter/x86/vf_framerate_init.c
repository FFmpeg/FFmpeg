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

#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/framerate.h"

void ff_blend_frames_ssse3(BLEND_FUNC_PARAMS);
void ff_blend_frames_avx2(BLEND_FUNC_PARAMS);
void ff_blend_frames16_sse4(BLEND_FUNC_PARAMS);
void ff_blend_frames16_avx2(BLEND_FUNC_PARAMS);

void ff_framerate_init_x86(FrameRateContext *s)
{
    int cpu_flags = av_get_cpu_flags();
    if (s->bitdepth == 8) {
        if (EXTERNAL_AVX2_FAST(cpu_flags))
            s->blend = ff_blend_frames_avx2;
        else if (EXTERNAL_SSSE3(cpu_flags))
            s->blend = ff_blend_frames_ssse3;
    } else {
        if (EXTERNAL_AVX2_FAST(cpu_flags))
            s->blend = ff_blend_frames16_avx2;
        else if (EXTERNAL_SSE4(cpu_flags))
            s->blend = ff_blend_frames16_sse4;
    }
}
