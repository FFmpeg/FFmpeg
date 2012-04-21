/*
 * Copyright (c) 2011 Mans Rullgard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/arm/cpu.h"
#include "libavcodec/mpegaudiodsp.h"
#include "config.h"

void ff_mpadsp_apply_window_fixed_armv6(int32_t *synth_buf, int32_t *window,
                                        int *dither, int16_t *out, int incr);

void ff_mpadsp_init_arm(MPADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags)) {
        s->apply_window_fixed = ff_mpadsp_apply_window_fixed_armv6;
    }
}
