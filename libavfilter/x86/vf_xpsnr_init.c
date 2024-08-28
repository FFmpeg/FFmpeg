/*
 * Copyright (c) 2024 Christian R. Helmrich
 * Copyright (c) 2024 Christian Lehmann
 * Copyright (c) 2024 Christian Stoffers
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

/**
 * @file
 * SIMD initialization for calculation of extended perceptually weighted PSNR (XPSNR).
 *
 * Authors: Christian Helmrich, Lehmann, and Stoffers, Fraunhofer HHI, Berlin, Germany
 */

#include "libavutil/x86/cpu.h"
#include "libavfilter/xpsnr.h"

uint64_t ff_sse_line_16bit_sse2(const uint8_t *buf, const uint8_t *ref, const int w);

void ff_xpsnr_init_x86(PSNRDSPContext *dsp, const int bpp)
{
    if (bpp <= 15) { /* XPSNR always operates with 16-bit internal precision */
        const int cpu_flags = av_get_cpu_flags();

        if (EXTERNAL_SSE2(cpu_flags))
            dsp->sse_line = ff_sse_line_16bit_sse2;
    }
}
