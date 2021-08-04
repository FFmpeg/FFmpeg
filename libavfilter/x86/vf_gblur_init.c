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

void ff_horiz_slice_sse4(float *ptr, int width, int height, int steps, float nu, float bscale, float *localbuf);
void ff_horiz_slice_avx2(float *ptr, int width, int height, int steps, float nu, float bscale, float *localbuf);
void ff_horiz_slice_avx512(float *ptr, int width, int height, int steps, float nu, float bscale, float *localbuf);

void ff_postscale_slice_sse(float *ptr, int length, float postscale, float min, float max);
void ff_postscale_slice_avx2(float *ptr, int length, float postscale, float min, float max);
void ff_postscale_slice_avx512(float *ptr, int length, float postscale, float min, float max);

void ff_verti_slice_avx2(float *buffer, int width, int height, int column_begin, int column_end,
                        int steps, float nu, float bscale);
void ff_verti_slice_avx512(float *buffer, int width, int height, int column_begin, int column_end,
                        int steps, float nu, float bscale);

av_cold void ff_gblur_init_x86(GBlurContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
        s->postscale_slice = ff_postscale_slice_sse;
    }
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        s->postscale_slice = ff_postscale_slice_avx2;
    }
#if ARCH_X86_64
    if (EXTERNAL_SSE4(cpu_flags)) {
        s->horiz_slice = ff_horiz_slice_sse4;
    }
    if (EXTERNAL_AVX2(cpu_flags)) {
        s->verti_slice = ff_verti_slice_avx2;
    }
    if (EXTERNAL_AVX512(cpu_flags)) {
        s->postscale_slice = ff_postscale_slice_avx512;
        s->verti_slice = ff_verti_slice_avx512;
    }
    if (EXTERNAL_AVX2(cpu_flags)) {
        s->stride = EXTERNAL_AVX512(cpu_flags) ? 16 : 8;
        s->localbuf = av_malloc(s->stride * sizeof(float) * s->planewidth[0] * s->planeheight[0]);
        if (!s->localbuf)
            return;

        s->horiz_slice = ff_horiz_slice_avx2;
        if (EXTERNAL_AVX512(cpu_flags)) {
            s->horiz_slice = ff_horiz_slice_avx512;
        }
    }
#endif
}
