/*
 * Copyright (c) 2013 Diego Biurrun <diego@biurrun.de>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/h263dsp.h"

void ff_h263_h_loop_filter_mmx(uint8_t *src, int stride, int qscale);
void ff_h263_v_loop_filter_mmx(uint8_t *src, int stride, int qscale);

av_cold void ff_h263dsp_init_x86(H263DSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        c->h263_h_loop_filter = ff_h263_h_loop_filter_mmx;
        c->h263_v_loop_filter = ff_h263_v_loop_filter_mmx;
    }
}
