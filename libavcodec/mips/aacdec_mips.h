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
 * Authors:  Darko Laus      (darko@mips.com)
 *           Djordje Pesut   (djordje@mips.com)
 *           Mirjana Vulin   (mvulin@mips.com)
 *
 * AAC Spectral Band Replication decoding functions optimized for MIPS
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
 * Reference: libavcodec/aacdec.c
 */

#ifndef AVCODEC_MIPS_AACDEC_FLOAT_H
#define AVCODEC_MIPS_AACDEC_FLOAT_H

#include "libavcodec/aac.h"

#if HAVE_INLINE_ASM && HAVE_MIPSFPU
static inline float *VMUL2_mips(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float temp0, temp1, temp2;
    int temp3, temp4;
    float *ret;

    __asm__ volatile(
        "andi    %[temp3],  %[idx],       15           \n\t"
        "ext     %[temp4],  %[idx],       4,      4    \n\t"
        "sll     %[temp3],  %[temp3],     2            \n\t"
        "sll     %[temp4],  %[temp4],     2            \n\t"
        "lwc1    %[temp2],  0(%[scale])                \n\t"
        "lwxc1   %[temp0],  %[temp3](%[v])             \n\t"
        "lwxc1   %[temp1],  %[temp4](%[v])             \n\t"
        "mul.s   %[temp0],  %[temp0],     %[temp2]     \n\t"
        "mul.s   %[temp1],  %[temp1],     %[temp2]     \n\t"
        "addiu   %[ret],    %[dst],       8            \n\t"
        "swc1    %[temp0],  0(%[dst])                  \n\t"
        "swc1    %[temp1],  4(%[dst])                  \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
          [temp2]"=&f"(temp2), [temp3]"=&r"(temp3),
          [temp4]"=&r"(temp4), [ret]"=&r"(ret)
        : [idx]"r"(idx), [scale]"r"(scale), [v]"r"(v),
          [dst]"r"(dst)
        : "memory"
    );
    return ret;
}

static inline float *VMUL4_mips(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    int temp0, temp1, temp2, temp3;
    float temp4, temp5, temp6, temp7, temp8;
    float *ret;

    __asm__ volatile(
        "andi    %[temp0],  %[idx],       3           \n\t"
        "ext     %[temp1],  %[idx],       2,      2   \n\t"
        "ext     %[temp2],  %[idx],       4,      2   \n\t"
        "ext     %[temp3],  %[idx],       6,      2   \n\t"
        "sll     %[temp0],  %[temp0],     2           \n\t"
        "sll     %[temp1],  %[temp1],     2           \n\t"
        "sll     %[temp2],  %[temp2],     2           \n\t"
        "sll     %[temp3],  %[temp3],     2           \n\t"
        "lwc1    %[temp4],  0(%[scale])               \n\t"
        "lwxc1   %[temp5],  %[temp0](%[v])            \n\t"
        "lwxc1   %[temp6],  %[temp1](%[v])            \n\t"
        "lwxc1   %[temp7],  %[temp2](%[v])            \n\t"
        "lwxc1   %[temp8],  %[temp3](%[v])            \n\t"
        "mul.s   %[temp5],  %[temp5],     %[temp4]    \n\t"
        "mul.s   %[temp6],  %[temp6],     %[temp4]    \n\t"
        "mul.s   %[temp7],  %[temp7],     %[temp4]    \n\t"
        "mul.s   %[temp8],  %[temp8],     %[temp4]    \n\t"
        "addiu   %[ret],    %[dst],       16          \n\t"
        "swc1    %[temp5],  0(%[dst])                 \n\t"
        "swc1    %[temp6],  4(%[dst])                 \n\t"
        "swc1    %[temp7],  8(%[dst])                 \n\t"
        "swc1    %[temp8],  12(%[dst])                \n\t"

        : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
          [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
          [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
          [temp6]"=&f"(temp6), [temp7]"=&f"(temp7),
          [temp8]"=&f"(temp8), [ret]"=&r"(ret)
        : [idx]"r"(idx), [scale]"r"(scale), [v]"r"(v),
          [dst]"r"(dst)
        : "memory"
    );
    return ret;
}

static inline float *VMUL2S_mips(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    int temp0, temp1, temp2, temp3, temp4, temp5;
    float temp6, temp7, temp8, temp9;
    float *ret;

    __asm__ volatile(
        "andi    %[temp0],  %[idx],       15         \n\t"
        "ext     %[temp1],  %[idx],       4,     4   \n\t"
        "lw      %[temp4],  0(%[scale])              \n\t"
        "srl     %[temp2],  %[sign],      1          \n\t"
        "sll     %[temp3],  %[sign],      31         \n\t"
        "sll     %[temp2],  %[temp2],     31         \n\t"
        "sll     %[temp0],  %[temp0],     2          \n\t"
        "sll     %[temp1],  %[temp1],     2          \n\t"
        "lwxc1   %[temp8],  %[temp0](%[v])           \n\t"
        "lwxc1   %[temp9],  %[temp1](%[v])           \n\t"
        "xor     %[temp5],  %[temp4],     %[temp2]   \n\t"
        "xor     %[temp4],  %[temp4],     %[temp3]   \n\t"
        "mtc1    %[temp5],  %[temp6]                 \n\t"
        "mtc1    %[temp4],  %[temp7]                 \n\t"
        "mul.s   %[temp8],  %[temp8],     %[temp6]   \n\t"
        "mul.s   %[temp9],  %[temp9],     %[temp7]   \n\t"
        "addiu   %[ret],    %[dst],       8          \n\t"
        "swc1    %[temp8],  0(%[dst])                \n\t"
        "swc1    %[temp9],  4(%[dst])                \n\t"

        : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
          [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
          [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
          [temp6]"=&f"(temp6), [temp7]"=&f"(temp7),
          [temp8]"=&f"(temp8), [temp9]"=&f"(temp9),
          [ret]"=&r"(ret)
        : [idx]"r"(idx), [scale]"r"(scale), [v]"r"(v),
          [dst]"r"(dst), [sign]"r"(sign)
        : "memory"
    );
    return ret;
}

static inline float *VMUL4S_mips(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    int temp0, temp1, temp2, temp3, temp4;
    float temp10, temp11, temp12, temp13, temp14, temp15, temp16, temp17;
    float *ret;
    unsigned int mask = 1U << 31;

    __asm__ volatile(
        "lw      %[temp0],   0(%[scale])               \n\t"
        "and     %[temp1],   %[idx],       3           \n\t"
        "ext     %[temp2],   %[idx],       2,      2   \n\t"
        "ext     %[temp3],   %[idx],       4,      2   \n\t"
        "ext     %[temp4],   %[idx],       6,      2   \n\t"
        "sll     %[temp1],   %[temp1],     2           \n\t"
        "sll     %[temp2],   %[temp2],     2           \n\t"
        "sll     %[temp3],   %[temp3],     2           \n\t"
        "sll     %[temp4],   %[temp4],     2           \n\t"
        "lwxc1   %[temp10],  %[temp1](%[v])            \n\t"
        "lwxc1   %[temp11],  %[temp2](%[v])            \n\t"
        "lwxc1   %[temp12],  %[temp3](%[v])            \n\t"
        "lwxc1   %[temp13],  %[temp4](%[v])            \n\t"
        "and     %[temp1],   %[sign],      %[mask]     \n\t"
        "ext     %[temp2],   %[idx],       12,     1   \n\t"
        "ext     %[temp3],   %[idx],       13,     1   \n\t"
        "ext     %[temp4],   %[idx],       14,     1   \n\t"
        "sllv    %[sign],    %[sign],      %[temp2]    \n\t"
        "xor     %[temp1],   %[temp0],     %[temp1]    \n\t"
        "and     %[temp2],   %[sign],      %[mask]     \n\t"
        "mtc1    %[temp1],   %[temp14]                 \n\t"
        "xor     %[temp2],   %[temp0],     %[temp2]    \n\t"
        "sllv    %[sign],    %[sign],      %[temp3]    \n\t"
        "mtc1    %[temp2],   %[temp15]                 \n\t"
        "and     %[temp3],   %[sign],      %[mask]     \n\t"
        "sllv    %[sign],    %[sign],      %[temp4]    \n\t"
        "xor     %[temp3],   %[temp0],     %[temp3]    \n\t"
        "and     %[temp4],   %[sign],      %[mask]     \n\t"
        "mtc1    %[temp3],   %[temp16]                 \n\t"
        "xor     %[temp4],   %[temp0],     %[temp4]    \n\t"
        "mtc1    %[temp4],   %[temp17]                 \n\t"
        "mul.s   %[temp10],  %[temp10],    %[temp14]   \n\t"
        "mul.s   %[temp11],  %[temp11],    %[temp15]   \n\t"
        "mul.s   %[temp12],  %[temp12],    %[temp16]   \n\t"
        "mul.s   %[temp13],  %[temp13],    %[temp17]   \n\t"
        "addiu   %[ret],     %[dst],       16          \n\t"
        "swc1    %[temp10],  0(%[dst])                 \n\t"
        "swc1    %[temp11],  4(%[dst])                 \n\t"
        "swc1    %[temp12],  8(%[dst])                 \n\t"
        "swc1    %[temp13],  12(%[dst])                \n\t"

        : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
          [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
          [temp4]"=&r"(temp4), [temp10]"=&f"(temp10),
          [temp11]"=&f"(temp11), [temp12]"=&f"(temp12),
          [temp13]"=&f"(temp13), [temp14]"=&f"(temp14),
          [temp15]"=&f"(temp15), [temp16]"=&f"(temp16),
          [temp17]"=&f"(temp17), [ret]"=&r"(ret),
          [sign]"+r"(sign)
        : [idx]"r"(idx), [scale]"r"(scale), [v]"r"(v),
          [dst]"r"(dst), [mask]"r"(mask)
        : "memory"
    );
    return ret;
}

#define VMUL2 VMUL2_mips
#define VMUL4 VMUL4_mips
#define VMUL2S VMUL2S_mips
#define VMUL4S VMUL4S_mips
#endif /* HAVE_INLINE_ASM && HAVE_MIPSFPU */

#endif /* AVCODEC_MIPS_AACDEC_FLOAT_H */
