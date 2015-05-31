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
#include "libavutil/float_dsp.h"
#include "cpu.h"
#include "asm.h"

void ff_vector_fmul_sse(float *dst, const float *src0, const float *src1,
                        int len);
void ff_vector_fmul_avx(float *dst, const float *src0, const float *src1,
                        int len);

void ff_vector_fmac_scalar_sse(float *dst, const float *src, float mul,
                               int len);
void ff_vector_fmac_scalar_avx(float *dst, const float *src, float mul,
                               int len);
void ff_vector_fmac_scalar_fma3(float *dst, const float *src, float mul,
                                int len);

void ff_vector_fmul_scalar_sse(float *dst, const float *src, float mul,
                               int len);

void ff_vector_dmul_scalar_sse2(double *dst, const double *src,
                                double mul, int len);
void ff_vector_dmul_scalar_avx(double *dst, const double *src,
                               double mul, int len);

void ff_vector_fmul_window_3dnowext(float *dst, const float *src0,
                                    const float *src1, const float *win, int len);
void ff_vector_fmul_window_sse(float *dst, const float *src0,
                               const float *src1, const float *win, int len);

void ff_vector_fmul_add_sse(float *dst, const float *src0, const float *src1,
                            const float *src2, int len);
void ff_vector_fmul_add_avx(float *dst, const float *src0, const float *src1,
                            const float *src2, int len);
void ff_vector_fmul_add_fma3(float *dst, const float *src0, const float *src1,
                             const float *src2, int len);

void ff_vector_fmul_reverse_sse(float *dst, const float *src0,
                                const float *src1, int len);
void ff_vector_fmul_reverse_avx(float *dst, const float *src0,
                                const float *src1, int len);

float ff_scalarproduct_float_sse(const float *v1, const float *v2, int order);

void ff_butterflies_float_sse(float *src0, float *src1, int len);

av_cold void ff_float_dsp_init_x86(AVFloatDSPContext *fdsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AMD3DNOWEXT(cpu_flags)) {
        fdsp->vector_fmul_window = ff_vector_fmul_window_3dnowext;
    }
    if (EXTERNAL_SSE(cpu_flags)) {
        fdsp->vector_fmul = ff_vector_fmul_sse;
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_sse;
        fdsp->vector_fmul_scalar = ff_vector_fmul_scalar_sse;
        fdsp->vector_fmul_window = ff_vector_fmul_window_sse;
        fdsp->vector_fmul_add    = ff_vector_fmul_add_sse;
        fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_sse;
        fdsp->scalarproduct_float = ff_scalarproduct_float_sse;
        fdsp->butterflies_float   = ff_butterflies_float_sse;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        fdsp->vector_dmul_scalar = ff_vector_dmul_scalar_sse2;
    }
    if (EXTERNAL_AVX_FAST(cpu_flags)) {
        fdsp->vector_fmul = ff_vector_fmul_avx;
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_avx;
        fdsp->vector_dmul_scalar = ff_vector_dmul_scalar_avx;
        fdsp->vector_fmul_add    = ff_vector_fmul_add_avx;
        fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_avx;
    }
    if (EXTERNAL_FMA3(cpu_flags) && !(cpu_flags & AV_CPU_FLAG_AVXSLOW)) {
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_fma3;
        fdsp->vector_fmul_add    = ff_vector_fmul_add_fma3;
    }
}
