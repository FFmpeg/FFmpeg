/*
 * Copyright (c) 2015 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "libavutil/x86/cpu.h"

#include "libavfilter/psnr.h"

uint64_t ff_sse_line_8bit_sse2(const uint8_t *buf, const uint8_t *ref, int w);
uint64_t ff_sse_line_16bit_sse2(const uint8_t *buf, const uint8_t *ref, int w);

void ff_psnr_init_x86(PSNRDSPContext *dsp, int bpp)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        if (bpp <= 8) {
            dsp->sse_line = ff_sse_line_8bit_sse2;
        } else if (bpp <= 15) {
            dsp->sse_line = ff_sse_line_16bit_sse2;
        }
    }
}
