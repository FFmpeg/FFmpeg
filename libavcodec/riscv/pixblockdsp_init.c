/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/pixblockdsp.h"

void ff_get_pixels_8_rvi(int16_t *block, const uint8_t *pixels,
                         ptrdiff_t stride);
void ff_get_pixels_16_rvi(int16_t *block, const uint8_t *pixels,
                          ptrdiff_t stride);

void ff_get_pixels_8_rvv(int16_t *block, const uint8_t *pixels,
                         ptrdiff_t stride);
void ff_get_pixels_unaligned_8_rvv(int16_t *block, const uint8_t *pixels,
                                   ptrdiff_t stride);
void ff_diff_pixels_rvv(int16_t *block, const uint8_t *s1,
                        const uint8_t *s2, ptrdiff_t stride);
void ff_diff_pixels_unaligned_rvv(int16_t *block, const uint8_t *s1,
                                  const uint8_t *s2, ptrdiff_t stride);

av_cold void ff_pixblockdsp_init_riscv(PixblockDSPContext *c,
                                       unsigned high_bit_depth)
{
#if HAVE_RV
    int cpu_flags = av_get_cpu_flags();

#if __riscv_xlen >= 64
    if (cpu_flags & AV_CPU_FLAG_RVI) {
        if (high_bit_depth)
            c->get_pixels = ff_get_pixels_16_rvi;
        else
            c->get_pixels = ff_get_pixels_8_rvi;
    }

    if (cpu_flags & AV_CPU_FLAG_RV_MISALIGNED) {
        if (high_bit_depth)
            c->get_pixels_unaligned = ff_get_pixels_16_rvi;
        else
            c->get_pixels_unaligned = ff_get_pixels_8_rvi;
    }
#endif
#if HAVE_RVV
    if ((cpu_flags & AV_CPU_FLAG_RVV_I32) && ff_rv_vlen_least(128)) {
        c->diff_pixels = ff_diff_pixels_unaligned_rvv;
        c->diff_pixels_unaligned = ff_diff_pixels_unaligned_rvv;
    }

    if ((cpu_flags & AV_CPU_FLAG_RVV_I64) && ff_get_rv_vlenb() >= 16) {
        if (!high_bit_depth) {
            c->get_pixels = ff_get_pixels_8_rvv;
            c->get_pixels_unaligned = ff_get_pixels_unaligned_8_rvv;
        }

        c->diff_pixels = ff_diff_pixels_rvv;
    }
#endif
#endif
}
