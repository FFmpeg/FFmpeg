/*
 * Copyright (c) 2012
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author:  Branimir Vasic (bvasic@mips.com)
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
 * Reference: libavutil/float_dsp.c
 */

#include "config.h"
#include "libavutil/float_dsp.h"

#if HAVE_INLINE_ASM && HAVE_MIPSFPU
static void vector_fmul_mips(float *dst, const float *src0, const float *src1,
                             int len)
{
    int i;

    if (len & 3) {
        for (i = 0; i < len; i++)
            dst[i] = src0[i] * src1[i];
    } else {
        float *d     = (float *)dst;
        float *d_end = d + len;
        float *s0    = (float *)src0;
        float *s1    = (float *)src1;

        float src0_0, src0_1, src0_2, src0_3;
        float src1_0, src1_1, src1_2, src1_3;

        __asm__ volatile (
            "1:                                         \n\t"
            "lwc1   %[src0_0],  0(%[s0])                \n\t"
            "lwc1   %[src1_0],  0(%[s1])                \n\t"
            "lwc1   %[src0_1],  4(%[s0])                \n\t"
            "lwc1   %[src1_1],  4(%[s1])                \n\t"
            "lwc1   %[src0_2],  8(%[s0])                \n\t"
            "lwc1   %[src1_2],  8(%[s1])                \n\t"
            "lwc1   %[src0_3],  12(%[s0])               \n\t"
            "lwc1   %[src1_3],  12(%[s1])               \n\t"
            "mul.s  %[src0_0],  %[src0_0],  %[src1_0]   \n\t"
            "mul.s  %[src0_1],  %[src0_1],  %[src1_1]   \n\t"
            "mul.s  %[src0_2],  %[src0_2],  %[src1_2]   \n\t"
            "mul.s  %[src0_3],  %[src0_3],  %[src1_3]   \n\t"
            "swc1   %[src0_0],  0(%[d])                 \n\t"
            "swc1   %[src0_1],  4(%[d])                 \n\t"
            "swc1   %[src0_2],  8(%[d])                 \n\t"
            "swc1   %[src0_3],  12(%[d])                \n\t"
            "addiu  %[s0],      %[s0],      16          \n\t"
            "addiu  %[s1],      %[s1],      16          \n\t"
            "addiu  %[d],       %[d],       16          \n\t"
            "bne    %[d],       %[d_end],   1b          \n\t"

            : [src0_0]"=&f"(src0_0), [src0_1]"=&f"(src0_1),
              [src0_2]"=&f"(src0_2), [src0_3]"=&f"(src0_3),
              [src1_0]"=&f"(src1_0), [src1_1]"=&f"(src1_1),
              [src1_2]"=&f"(src1_2), [src1_3]"=&f"(src1_3),
              [d]"+r"(d), [s0]"+r"(s0), [s1]"+r"(s1)
            : [d_end]"r"(d_end)
            : "memory"
        );
    }
}
#endif /* HAVE_INLINE_ASM && HAVE_MIPSFPU */

void ff_float_dsp_init_mips(AVFloatDSPContext *fdsp) {
#if HAVE_INLINE_ASM && HAVE_MIPSFPU
    fdsp->vector_fmul = vector_fmul_mips;
#endif /* HAVE_INLINE_ASM && HAVE_MIPSFPU */
}
