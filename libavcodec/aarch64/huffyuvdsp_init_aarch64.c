/*
 * Copyright (c) 2025
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/huffyuvdsp.h"

void ff_add_int16_neon(uint16_t *dst, const uint16_t *src, unsigned mask, int w);

av_cold void ff_huffyuvdsp_init_aarch64(HuffYUVDSPContext *c, enum AVPixelFormat pix_fmt)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->add_int16 = ff_add_int16_neon;
    }
}
