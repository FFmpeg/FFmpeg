/*
 * Copyright (c) 2012-2014 Christophe Gisquet <christophe.gisquet@gmail.com>
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

#ifndef AVCODEC_X86_DCA_H
#define AVCODEC_X86_DCA_H

#include "config.h"

#if ARCH_X86_64 && HAVE_SSE2_INLINE
# include "libavutil/x86/asm.h"
# include "libavutil/mem.h"
#include "libavcodec/dcadsp.h"

# define int8x8_fmul_int32 int8x8_fmul_int32
static inline void int8x8_fmul_int32(av_unused DCADSPContext *dsp,
                                     float *dst, const int8_t *src, int scale)
{
    DECLARE_ALIGNED(16, static const uint32_t, inverse16) = 0x3D800000;
    __asm__ volatile (
        "cvtsi2ss        %2, %%xmm0 \n\t"
        "mulss           %3, %%xmm0 \n\t"
        "movq          (%1), %%xmm1 \n\t"
        "punpcklbw   %%xmm1, %%xmm1 \n\t"
        "movaps      %%xmm1, %%xmm2 \n\t"
        "punpcklwd   %%xmm1, %%xmm1 \n\t"
        "punpckhwd   %%xmm2, %%xmm2 \n\t"
        "psrad          $24, %%xmm1 \n\t"
        "psrad          $24, %%xmm2 \n\t"
        "shufps  $0, %%xmm0, %%xmm0 \n\t"
        "cvtdq2ps    %%xmm1, %%xmm1 \n\t"
        "cvtdq2ps    %%xmm2, %%xmm2 \n\t"
        "mulps       %%xmm0, %%xmm1 \n\t"
        "mulps       %%xmm0, %%xmm2 \n\t"
        "movaps      %%xmm1,  0(%0) \n\t"
        "movaps      %%xmm2, 16(%0) \n\t"
        :: "r"(dst), "r"(src), "m"(scale), "m"(inverse16)
        XMM_CLOBBERS_ONLY("xmm0", "xmm1", "xmm2")
    );
}

#endif /* ARCH_X86_64 && HAVE_SSE2_INLINE */

#endif /* AVCODEC_X86_DCA_H */
