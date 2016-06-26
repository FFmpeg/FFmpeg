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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/audiodsp.h"

int32_t ff_scalarproduct_int16_mmxext(const int16_t *v1, const int16_t *v2,
                                      int order);
int32_t ff_scalarproduct_int16_sse2(const int16_t *v1, const int16_t *v2,
                                    int order);

void ff_vector_clip_int32_mmx(int32_t *dst, const int32_t *src,
                              int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse2(int32_t *dst, const int32_t *src,
                               int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_int_sse2(int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse4(int32_t *dst, const int32_t *src,
                               int32_t min, int32_t max, unsigned int len);
void ff_vector_clipf_sse(float *dst, const float *src,
                         float min, float max, int len);

av_cold void ff_audiodsp_init_x86(AudioDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags))
        c->vector_clip_int32 = ff_vector_clip_int32_mmx;

    if (EXTERNAL_MMXEXT(cpu_flags))
        c->scalarproduct_int16 = ff_scalarproduct_int16_mmxext;

    if (EXTERNAL_SSE(cpu_flags))
        c->vector_clipf = ff_vector_clipf_sse;

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->scalarproduct_int16 = ff_scalarproduct_int16_sse2;
        if (cpu_flags & AV_CPU_FLAG_ATOM)
            c->vector_clip_int32 = ff_vector_clip_int32_int_sse2;
        else
            c->vector_clip_int32 = ff_vector_clip_int32_sse2;
    }

    if (EXTERNAL_SSE4(cpu_flags))
        c->vector_clip_int32 = ff_vector_clip_int32_sse4;
}
