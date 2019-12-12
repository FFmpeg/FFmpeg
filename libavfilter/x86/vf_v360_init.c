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
#include "libavfilter/v360.h"

void ff_remap1_8bit_line_avx2(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
                              const uint16_t *u, const uint16_t *v, const int16_t *ker);

void ff_remap2_8bit_line_avx2(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
                              const uint16_t *u, const uint16_t *v, const int16_t *ker);

void ff_remap4_8bit_line_avx2(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
                              const uint16_t *u, const uint16_t *v, const int16_t *ker);

void ff_remap1_16bit_line_avx2(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
                               const uint16_t *u, const uint16_t *v, const int16_t *ker);

void ff_remap2_16bit_line_avx2(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
                              const uint16_t *u, const uint16_t *v, const int16_t *ker);

av_cold void ff_v360_init_x86(V360Context *s, int depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2_FAST(cpu_flags) && s->interp == NEAREST && depth <= 8)
        s->remap_line = ff_remap1_8bit_line_avx2;

    if (EXTERNAL_AVX2_FAST(cpu_flags) && s->interp == BILINEAR && depth <= 8)
        s->remap_line = ff_remap2_8bit_line_avx2;

    if (EXTERNAL_AVX2_FAST(cpu_flags) && s->interp == NEAREST && depth > 8)
        s->remap_line = ff_remap1_16bit_line_avx2;

    if (EXTERNAL_AVX2_FAST(cpu_flags) && s->interp == BILINEAR && depth > 8)
        s->remap_line = ff_remap2_16bit_line_avx2;

#if ARCH_X86_64
    if (EXTERNAL_AVX2_FAST(cpu_flags) && (s->interp == BICUBIC ||
                                          s->interp == LANCZOS) && depth <= 8)
        s->remap_line = ff_remap4_8bit_line_avx2;
#endif
}
