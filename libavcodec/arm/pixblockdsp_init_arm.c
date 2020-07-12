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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/pixblockdsp.h"

void ff_get_pixels_armv6(int16_t *block, const uint8_t *pixels,
                         ptrdiff_t stride);
void ff_diff_pixels_armv6(int16_t *block, const uint8_t *s1,
                          const uint8_t *s2, ptrdiff_t stride);

void ff_get_pixels_neon(int16_t *block, const uint8_t *pixels,
                        ptrdiff_t stride);
void ff_get_pixels_unaligned_neon(int16_t *block, const uint8_t *pixels,
                                  ptrdiff_t stride);
void ff_diff_pixels_neon(int16_t *block, const uint8_t *s1,
                         const uint8_t *s2, ptrdiff_t stride);
void ff_diff_pixels_unaligned_neon(int16_t *block, const uint8_t *s1,
                                   const uint8_t *s2, ptrdiff_t stride);

av_cold void ff_pixblockdsp_init_arm(PixblockDSPContext *c,
                                     AVCodecContext *avctx,
                                     unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags)) {
        if (!high_bit_depth)
            c->get_pixels = ff_get_pixels_armv6;
        c->diff_pixels = ff_diff_pixels_armv6;
    }

    if (have_neon(cpu_flags)) {
        if (!high_bit_depth) {
            c->get_pixels_unaligned = ff_get_pixels_unaligned_neon;
            c->get_pixels = ff_get_pixels_neon;
        }
        c->diff_pixels_unaligned = ff_diff_pixels_unaligned_neon;
        c->diff_pixels = ff_diff_pixels_neon;
    }
}
