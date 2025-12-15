/*
 * Copyright © 2025 Rémi Denis-Courmont.
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavfilter/vf_blackdetect.h"

unsigned ff_count_pixels_8_rvv(const uint8_t *src, ptrdiff_t stride,
                               ptrdiff_t width, ptrdiff_t height,
                               unsigned threshold);
unsigned ff_count_pixels_16_rvv(const uint8_t *src, ptrdiff_t stride,
                                ptrdiff_t width, ptrdiff_t height,
                                unsigned threshold);

ff_blackdetect_fn ff_blackdetect_get_fn_riscv(int depth)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32) {
        if (depth <= 8)
            return ff_count_pixels_8_rvv;
        if ((flags & AV_CPU_FLAG_RVB) && (depth <= 16))
            return ff_count_pixels_16_rvv;
    }
#endif
    return NULL;
}
