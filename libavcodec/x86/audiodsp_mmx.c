/*
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

#include "config.h"
#include "libavutil/x86/asm.h"
#include "audiodsp.h"

#if HAVE_INLINE_ASM

void ff_vector_clipf_sse(float *dst, const float *src,
                         float min, float max, int len)
{
    x86_reg i = (len - 16) * 4;
    __asm__ volatile (
        "movss          %3, %%xmm4      \n\t"
        "movss          %4, %%xmm5      \n\t"
        "shufps $0, %%xmm4, %%xmm4      \n\t"
        "shufps $0, %%xmm5, %%xmm5      \n\t"
        "1:                             \n\t"
        "movaps   (%2, %0), %%xmm0      \n\t" // 3/1 on intel
        "movaps 16(%2, %0), %%xmm1      \n\t"
        "movaps 32(%2, %0), %%xmm2      \n\t"
        "movaps 48(%2, %0), %%xmm3      \n\t"
        "maxps      %%xmm4, %%xmm0      \n\t"
        "maxps      %%xmm4, %%xmm1      \n\t"
        "maxps      %%xmm4, %%xmm2      \n\t"
        "maxps      %%xmm4, %%xmm3      \n\t"
        "minps      %%xmm5, %%xmm0      \n\t"
        "minps      %%xmm5, %%xmm1      \n\t"
        "minps      %%xmm5, %%xmm2      \n\t"
        "minps      %%xmm5, %%xmm3      \n\t"
        "movaps     %%xmm0,   (%1, %0)  \n\t"
        "movaps     %%xmm1, 16(%1, %0)  \n\t"
        "movaps     %%xmm2, 32(%1, %0)  \n\t"
        "movaps     %%xmm3, 48(%1, %0)  \n\t"
        "sub           $64, %0          \n\t"
        "jge            1b              \n\t"
        : "+&r" (i)
        : "r" (dst), "r" (src), "m" (min), "m" (max)
        : "memory");
}

#endif /* HAVE_INLINE_ASM */
