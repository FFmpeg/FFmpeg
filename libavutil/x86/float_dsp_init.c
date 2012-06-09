/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavutil/cpu.h"
#include "libavutil/float_dsp.h"

extern void ff_vector_fmul_sse(float *dst, const float *src0, const float *src1,
                               int len);
extern void ff_vector_fmul_avx(float *dst, const float *src0, const float *src1,
                               int len);

extern void ff_vector_fmac_scalar_sse(float *dst, const float *src, float mul,
                                      int len);
extern void ff_vector_fmac_scalar_avx(float *dst, const float *src, float mul,
                                      int len);

void ff_float_dsp_init_x86(AVFloatDSPContext *fdsp)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_SSE && HAVE_SSE) {
        fdsp->vector_fmul = ff_vector_fmul_sse;
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_sse;
    }
    if (mm_flags & AV_CPU_FLAG_AVX && HAVE_AVX) {
        fdsp->vector_fmul = ff_vector_fmul_avx;
        fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_avx;
    }
#endif
}
