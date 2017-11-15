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
#include "libavcodec/v210dec.h"

extern void ff_v210_planar_unpack_unaligned_ssse3(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width);
extern void ff_v210_planar_unpack_unaligned_avx(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width);

extern void ff_v210_planar_unpack_aligned_ssse3(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width);
extern void ff_v210_planar_unpack_aligned_avx(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width);

av_cold void ff_v210_x86_init(V210DecContext *s)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (s->aligned_input) {
        if (cpu_flags & AV_CPU_FLAG_SSSE3)
            s->unpack_frame = ff_v210_planar_unpack_aligned_ssse3;

        if (HAVE_AVX_EXTERNAL && cpu_flags & AV_CPU_FLAG_AVX)
            s->unpack_frame = ff_v210_planar_unpack_aligned_avx;
    }
    else {
        if (cpu_flags & AV_CPU_FLAG_SSSE3)
            s->unpack_frame = ff_v210_planar_unpack_unaligned_ssse3;

        if (HAVE_AVX_EXTERNAL && cpu_flags & AV_CPU_FLAG_AVX)
            s->unpack_frame = ff_v210_planar_unpack_unaligned_avx;
    }
#endif
}
