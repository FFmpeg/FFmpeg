/*
 * SIMD optimized MPEG-4 Parametric Stereo decoding functions
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

#include <stddef.h>

#include "config.h"

#include "libavutil/x86/cpu.h"
#include "libavutil/attributes.h"
#include "libavcodec/aacpsdsp.h"

void ff_ps_add_squares_sse  (float *dst, const float (*src)[2], int n);
void ff_ps_add_squares_sse3 (float *dst, const float (*src)[2], int n);
void ff_ps_mul_pair_single_sse (float (*dst)[2], float (*src0)[2],
                                float *src1, int n);
void ff_ps_hybrid_analysis_sse (float (*out)[2], float (*in)[2],
                                const float (*filter)[8][2],
                                ptrdiff_t stride, int n);
void ff_ps_hybrid_analysis_sse3(float (*out)[2], float (*in)[2],
                                const float (*filter)[8][2],
                                ptrdiff_t stride, int n);
void ff_ps_stereo_interpolate_sse3(float (*l)[2], float (*r)[2],
                                   float h[2][4], float h_step[2][4],
                                   int len);
void ff_ps_stereo_interpolate_ipdopd_sse3(float (*l)[2], float (*r)[2],
                                          float h[2][4], float h_step[2][4],
                                          int len);
void ff_ps_hybrid_synthesis_deint_sse(float out[2][38][64], float (*in)[32][2],
                                      int i, int len);
void ff_ps_hybrid_synthesis_deint_sse4(float out[2][38][64], float (*in)[32][2],
                                       int i, int len);
void ff_ps_hybrid_analysis_ileave_sse(float (*out)[32][2], float L[2][38][64],
                                      int i, int len);

av_cold void ff_psdsp_init_x86(PSDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
        s->add_squares            = ff_ps_add_squares_sse;
        s->mul_pair_single        = ff_ps_mul_pair_single_sse;
        s->hybrid_analysis_ileave = ff_ps_hybrid_analysis_ileave_sse;
        s->hybrid_synthesis_deint = ff_ps_hybrid_synthesis_deint_sse;
        s->hybrid_analysis        = ff_ps_hybrid_analysis_sse;
    }
    if (EXTERNAL_SSE3(cpu_flags)) {
        s->add_squares            = ff_ps_add_squares_sse3;
        s->stereo_interpolate[0]  = ff_ps_stereo_interpolate_sse3;
        s->stereo_interpolate[1]  = ff_ps_stereo_interpolate_ipdopd_sse3;
        s->hybrid_analysis        = ff_ps_hybrid_analysis_sse3;
    }
    if (EXTERNAL_SSE4(cpu_flags)) {
        s->hybrid_synthesis_deint = ff_ps_hybrid_synthesis_deint_sse4;
    }
}
