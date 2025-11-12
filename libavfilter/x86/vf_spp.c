/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/vf_spp.h"

#if HAVE_SSE2_INLINE
static void store_slice_sse2(uint8_t *dst, const int16_t *src,
                             int dst_stride, int src_stride,
                             int width, int height, int log2_scale,
                             const uint8_t dither[8][8])
{
    int y;

    for (y = 0; y < height; y++) {
        uint8_t *dst1 = dst;
        const int16_t *src1 = src;
        __asm__ volatile(
            "movq         (%3), %%xmm1  \n"
            "movd           %4, %%xmm2  \n"
            "pxor       %%xmm0, %%xmm0  \n"
            "punpcklbw  %%xmm0, %%xmm1  \n"
            "psraw      %%xmm2, %%xmm1  \n"
            "movd           %5, %%xmm2  \n"
            "1:                         \n"
            "movdqa       (%0), %%xmm0  \n"
            "paddw      %%xmm1, %%xmm0  \n"
            "psraw      %%xmm2, %%xmm0  \n"
            "packuswb   %%xmm0, %%xmm0  \n"
            "movq       %%xmm0, (%1)    \n"
            "add $16, %0                \n"
            "add $8, %1                 \n"
            "cmp %2, %1                 \n"
            " jb 1b                     \n"
            : "+r" (src1), "+r"(dst1)
            : "r"(dst + width), "r"(dither[y]), "g"(log2_scale), "g"(MAX_LEVEL - log2_scale)
            XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2")
        );
        src += src_stride;
        dst += dst_stride;
    }
}

#endif /* HAVE_SSE2_INLINE */

av_cold void ff_spp_init_x86(SPPContext *s)
{
#if HAVE_SSE2_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_SSE2(cpu_flags)) {
        s->store_slice = store_slice_sse2;
    }
#endif
}
