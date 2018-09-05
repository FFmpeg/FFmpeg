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

#include "pixelutils.h"
#include "cpu.h"

int ff_pixelutils_sad_8x8_mmx(const uint8_t *src1, ptrdiff_t stride1,
                              const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_8x8_mmxext(const uint8_t *src1, ptrdiff_t stride1,
                                 const uint8_t *src2, ptrdiff_t stride2);

int ff_pixelutils_sad_16x16_mmxext(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_16x16_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                 const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_a_16x16_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_u_16x16_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);

int ff_pixelutils_sad_32x32_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                 const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_a_32x32_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_u_32x32_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);

int ff_pixelutils_sad_32x32_avx2(const uint8_t *src1, ptrdiff_t stride1,
                                 const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_a_32x32_avx2(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);
int ff_pixelutils_sad_u_32x32_avx2(const uint8_t *src1, ptrdiff_t stride1,
                                   const uint8_t *src2, ptrdiff_t stride2);

void ff_pixelutils_sad_init_x86(av_pixelutils_sad_fn *sad, int aligned)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        sad[2] = ff_pixelutils_sad_8x8_mmx;
    }

    // The best way to use SSE2 would be to do 2 SADs in parallel,
    // but we'd have to modify the pixelutils API to return SIMD functions.

    // It's probably not faster to shuffle data around
    // to get two lines of 8 pixels into a single 16byte register,
    // so just use the MMX 8x8 version even when SSE2 is available.
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        sad[2] = ff_pixelutils_sad_8x8_mmxext;
        sad[3] = ff_pixelutils_sad_16x16_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        switch (aligned) {
        case 0: sad[3] = ff_pixelutils_sad_16x16_sse2;   break; // src1 unaligned, src2 unaligned
        case 1: sad[3] = ff_pixelutils_sad_u_16x16_sse2; break; // src1   aligned, src2 unaligned
        case 2: sad[3] = ff_pixelutils_sad_a_16x16_sse2; break; // src1   aligned, src2   aligned
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        switch (aligned) {
        case 0: sad[4] = ff_pixelutils_sad_32x32_sse2;   break; // src1 unaligned, src2 unaligned
        case 1: sad[4] = ff_pixelutils_sad_u_32x32_sse2; break; // src1   aligned, src2 unaligned
        case 2: sad[4] = ff_pixelutils_sad_a_32x32_sse2; break; // src1   aligned, src2   aligned
        }
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        switch (aligned) {
        case 0: sad[4] = ff_pixelutils_sad_32x32_avx2;   break; // src1 unaligned, src2 unaligned
        case 1: sad[4] = ff_pixelutils_sad_u_32x32_avx2; break; // src1   aligned, src2 unaligned
        case 2: sad[4] = ff_pixelutils_sad_a_32x32_avx2; break; // src1   aligned, src2   aligned
        }
    }
}
