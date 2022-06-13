/*
 * VP6 MMX/SSE2 optimizations
 * Copyright (C) 2009  Sebastien Lucas <sebastien.lucas@gmail.com>
 * Copyright (C) 2009  Zuxy Meng <zuxy.meng@gmail.com>
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
#include "libavcodec/vp56dsp.h"

void ff_vp6_filter_diag4_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                              const int16_t *h_weights,const int16_t *v_weights);

av_cold void ff_vp6dsp_init_x86(VP56DSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->vp6_filter_diag4 = ff_vp6_filter_diag4_sse2;
    }
}
