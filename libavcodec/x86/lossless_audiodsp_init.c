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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/lossless_audiodsp.h"

int32_t ff_scalarproduct_and_madd_int16_mmxext(int16_t *v1, const int16_t *v2,
                                               const int16_t *v3,
                                               int order, int mul);
int32_t ff_scalarproduct_and_madd_int16_sse2(int16_t *v1, const int16_t *v2,
                                             const int16_t *v3,
                                             int order, int mul);
int32_t ff_scalarproduct_and_madd_int16_ssse3(int16_t *v1, const int16_t *v2,
                                              const int16_t *v3,
                                              int order, int mul);

int32_t ff_scalarproduct_and_madd_int32_sse4(int16_t *v1, const int32_t *v2,
                                             const int16_t *v3,
                                             int order, int mul);

av_cold void ff_llauddsp_init_x86(LLAudDSPContext *c)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMXEXT(cpu_flags))
        c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_mmxext;

    if (EXTERNAL_SSE2(cpu_flags))
        c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_sse2;

    if (EXTERNAL_SSSE3(cpu_flags) &&
        !(cpu_flags & (AV_CPU_FLAG_SSE42 | AV_CPU_FLAG_3DNOW))) // cachesplit
        c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_ssse3;

    if (EXTERNAL_SSE4(cpu_flags))
        c->scalarproduct_and_madd_int32 = ff_scalarproduct_and_madd_int32_sse4;
#endif
}
