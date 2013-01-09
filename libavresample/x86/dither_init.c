/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavresample/dither.h"

extern void ff_quantize_sse2(int16_t *dst, const float *src, float *dither,
                             int len);

av_cold void ff_dither_init_x86(DitherDSPContext *ddsp,
                                enum AVResampleDitherMethod method)
{
    int mm_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(mm_flags)) {
        ddsp->quantize      = ff_quantize_sse2;
        ddsp->ptr_align     = 16;
        ddsp->samples_align = 8;
    }
}
