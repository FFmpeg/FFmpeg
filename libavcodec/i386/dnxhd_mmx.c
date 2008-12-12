/*
 * VC3/DNxHD SIMD functions
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 *
 * VC-3 encoder funded by the British Broadcasting Corporation
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

#include "libavutil/x86_cpu.h"
#include "libavcodec/dnxhdenc.h"

static void get_pixels_8x4_sym_sse2(DCTELEM *block, const uint8_t *pixels, int line_size)
{
    __asm__ volatile(
        "pxor %%xmm7,      %%xmm7       \n\t"
        "movq (%0),        %%xmm0       \n\t"
        "add  %2,          %0           \n\t"
        "movq (%0),        %%xmm1       \n\t"
        "movq (%0, %2),    %%xmm2       \n\t"
        "movq (%0, %2,2),  %%xmm3       \n\t"
        "punpcklbw %%xmm7, %%xmm0       \n\t"
        "punpcklbw %%xmm7, %%xmm1       \n\t"
        "punpcklbw %%xmm7, %%xmm2       \n\t"
        "punpcklbw %%xmm7, %%xmm3       \n\t"
        "movdqa %%xmm0,      (%1)       \n\t"
        "movdqa %%xmm1,    16(%1)       \n\t"
        "movdqa %%xmm2,    32(%1)       \n\t"
        "movdqa %%xmm3,    48(%1)       \n\t"
        "movdqa %%xmm3 ,   64(%1)       \n\t"
        "movdqa %%xmm2 ,   80(%1)       \n\t"
        "movdqa %%xmm1 ,   96(%1)       \n\t"
        "movdqa %%xmm0,   112(%1)       \n\t"
        : "+r" (pixels)
        : "r" (block), "r" ((x86_reg)line_size)
    );
}

void ff_dnxhd_init_mmx(DNXHDEncContext *ctx)
{
    if (mm_flags & FF_MM_SSE2) {
        ctx->get_pixels_8x4_sym = get_pixels_8x4_sym_sse2;
    }
}
