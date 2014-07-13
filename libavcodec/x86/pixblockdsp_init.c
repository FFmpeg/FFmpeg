/*
 * SIMD-optimized pixel operations
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/pixblockdsp.h"

void ff_get_pixels_mmx(int16_t *block, const uint8_t *pixels, int line_size);
void ff_get_pixels_sse2(int16_t *block, const uint8_t *pixels, int line_size);
void ff_diff_pixels_mmx(int16_t *block, const uint8_t *s1, const uint8_t *s2,
                        int stride);
void ff_diff_pixels_sse2(int16_t *block, const uint8_t *s1, const uint8_t *s2,
                         int stride);

av_cold void ff_pixblockdsp_init_x86(PixblockDSPContext *c,
                                     AVCodecContext *avctx,
                                     unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        if (!high_bit_depth)
            c->get_pixels = ff_get_pixels_mmx;
        c->diff_pixels = ff_diff_pixels_mmx;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        if (!high_bit_depth)
            c->get_pixels = ff_get_pixels_sse2;
        c->diff_pixels = ff_diff_pixels_sse2;
    }
}
