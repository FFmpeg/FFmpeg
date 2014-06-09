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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/dct.h"

void ff_dct32_float_sse(FFTSample *out, const FFTSample *in);
void ff_dct32_float_sse2(FFTSample *out, const FFTSample *in);
void ff_dct32_float_avx(FFTSample *out, const FFTSample *in);

av_cold void ff_dct_init_x86(DCTContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_32 && EXTERNAL_SSE(cpu_flags))
        s->dct32 = ff_dct32_float_sse;
    if (EXTERNAL_SSE2(cpu_flags))
        s->dct32 = ff_dct32_float_sse2;
    if (EXTERNAL_AVX(cpu_flags))
        s->dct32 = ff_dct32_float_avx;
}
