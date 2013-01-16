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
 * Author:  Zoran Lukic (zoranl@mips.com)
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

static void vector_fmul_scalar_mips(float *dst, const float *src, float mul,
                                 int len)
{
    float temp0, temp1, temp2, temp3;
    float *local_src = (float*)src;
    float *end = local_src + len;

    /* loop unrolled 4 times */
    __asm__ volatile(
        ".set    push                             \n\t"
        ".set    noreorder                        \n\t"
    "1:                                           \n\t"
        "lwc1    %[temp0],   0(%[src])            \n\t"
        "lwc1    %[temp1],   4(%[src])            \n\t"
        "lwc1    %[temp2],   8(%[src])            \n\t"
        "lwc1    %[temp3],   12(%[src])           \n\t"
        "addiu   %[dst],     %[dst],     16       \n\t"
        "mul.s   %[temp0],   %[temp0],   %[mul]   \n\t"
        "mul.s   %[temp1],   %[temp1],   %[mul]   \n\t"
        "mul.s   %[temp2],   %[temp2],   %[mul]   \n\t"
        "mul.s   %[temp3],   %[temp3],   %[mul]   \n\t"
        "addiu   %[src],     %[src],     16       \n\t"
        "swc1    %[temp0],   -16(%[dst])          \n\t"
        "swc1    %[temp1],   -12(%[dst])          \n\t"
        "swc1    %[temp2],   -8(%[dst])           \n\t"
        "bne     %[src],     %[end],     1b       \n\t"
        " swc1   %[temp3],   -4(%[dst])           \n\t"
        ".set    pop                              \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
          [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
          [dst]"+r"(dst), [src]"+r"(local_src)
        : [end]"r"(end), [mul]"f"(mul)
        : "memory"
    );
}

static void vector_fmul_window_mips(float *dst, const float *src0,
        const float *src1, const float *win, int len)
{
    int i, j;
    /*
     * variables used in inline assembler
     */
    float * dst_i, * dst_j, * dst_i2, * dst_j2;
    float temp, temp1, temp2, temp3, temp4, temp5, temp6, temp7;

    dst  += len;
    win  += len;
    src0 += len;

    for (i = -len, j = len - 1; i < 0; i += 8, j -= 8) {

        dst_i = dst + i;
        dst_j = dst + j;

        dst_i2 = dst + i + 4;
        dst_j2 = dst + j - 4;

        __asm__ volatile (
            "mul.s   %[temp],   %[s1],       %[wi]            \n\t"
            "mul.s   %[temp1],  %[s1],       %[wj]            \n\t"
            "mul.s   %[temp2],  %[s11],      %[wi1]           \n\t"
            "mul.s   %[temp3],  %[s11],      %[wj1]           \n\t"

            "msub.s  %[temp],   %[temp],     %[s0],  %[wj]    \n\t"
            "madd.s  %[temp1],  %[temp1],    %[s0],  %[wi]    \n\t"
            "msub.s  %[temp2],  %[temp2],    %[s01], %[wj1]   \n\t"
            "madd.s  %[temp3],  %[temp3],    %[s01], %[wi1]   \n\t"

            "swc1    %[temp],   0(%[dst_i])                   \n\t" /* dst[i] = s0*wj - s1*wi; */
            "swc1    %[temp1],  0(%[dst_j])                   \n\t" /* dst[j] = s0*wi + s1*wj; */
            "swc1    %[temp2],  4(%[dst_i])                   \n\t" /* dst[i+1] = s01*wj1 - s11*wi1; */
            "swc1    %[temp3], -4(%[dst_j])                   \n\t" /* dst[j-1] = s01*wi1 + s11*wj1; */

            "mul.s   %[temp4],  %[s12],      %[wi2]           \n\t"
            "mul.s   %[temp5],  %[s12],      %[wj2]           \n\t"
            "mul.s   %[temp6],  %[s13],      %[wi3]           \n\t"
            "mul.s   %[temp7],  %[s13],      %[wj3]           \n\t"

            "msub.s  %[temp4],  %[temp4],    %[s02], %[wj2]   \n\t"
            "madd.s  %[temp5],  %[temp5],    %[s02], %[wi2]   \n\t"
            "msub.s  %[temp6],  %[temp6],    %[s03], %[wj3]   \n\t"
            "madd.s  %[temp7],  %[temp7],    %[s03], %[wi3]   \n\t"

            "swc1    %[temp4],  8(%[dst_i])                   \n\t" /* dst[i+2] = s02*wj2 - s12*wi2; */
            "swc1    %[temp5], -8(%[dst_j])                   \n\t" /* dst[j-2] = s02*wi2 + s12*wj2; */
            "swc1    %[temp6],  12(%[dst_i])                  \n\t" /* dst[i+2] = s03*wj3 - s13*wi3; */
            "swc1    %[temp7], -12(%[dst_j])                  \n\t" /* dst[j-3] = s03*wi3 + s13*wj3; */
            : [temp]"=&f"(temp),  [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
              [temp6]"=&f"(temp6), [temp7]"=&f"(temp7)
            : [dst_j]"r"(dst_j),     [dst_i]"r" (dst_i),
              [s0] "f"(src0[i]),     [wj] "f"(win[j]),     [s1] "f"(src1[j]),
              [wi] "f"(win[i]),      [s01]"f"(src0[i + 1]),[wj1]"f"(win[j - 1]),
              [s11]"f"(src1[j - 1]), [wi1]"f"(win[i + 1]), [s02]"f"(src0[i + 2]),
              [wj2]"f"(win[j - 2]),  [s12]"f"(src1[j - 2]),[wi2]"f"(win[i + 2]),
              [s03]"f"(src0[i + 3]), [wj3]"f"(win[j - 3]), [s13]"f"(src1[j - 3]),
              [wi3]"f"(win[i + 3])
            : "memory"
        );

        __asm__ volatile (
            "mul.s  %[temp],   %[s1],       %[wi]            \n\t"
            "mul.s  %[temp1],  %[s1],       %[wj]            \n\t"
            "mul.s  %[temp2],  %[s11],      %[wi1]           \n\t"
            "mul.s  %[temp3],  %[s11],      %[wj1]           \n\t"

            "msub.s %[temp],   %[temp],     %[s0],  %[wj]    \n\t"
            "madd.s %[temp1],  %[temp1],    %[s0],  %[wi]    \n\t"
            "msub.s %[temp2],  %[temp2],    %[s01], %[wj1]   \n\t"
            "madd.s %[temp3],  %[temp3],    %[s01], %[wi1]   \n\t"

            "swc1   %[temp],   0(%[dst_i2])                  \n\t" /* dst[i] = s0*wj - s1*wi; */
            "swc1   %[temp1],  0(%[dst_j2])                  \n\t" /* dst[j] = s0*wi + s1*wj; */
            "swc1   %[temp2],  4(%[dst_i2])                  \n\t" /* dst[i+1] = s01*wj1 - s11*wi1; */
            "swc1   %[temp3], -4(%[dst_j2])                  \n\t" /* dst[j-1] = s01*wi1 + s11*wj1; */

            "mul.s  %[temp4],  %[s12],      %[wi2]           \n\t"
            "mul.s  %[temp5],  %[s12],      %[wj2]           \n\t"
            "mul.s  %[temp6],  %[s13],      %[wi3]           \n\t"
            "mul.s  %[temp7],  %[s13],      %[wj3]           \n\t"

            "msub.s %[temp4],  %[temp4],    %[s02], %[wj2]   \n\t"
            "madd.s %[temp5],  %[temp5],    %[s02], %[wi2]   \n\t"
            "msub.s %[temp6],  %[temp6],    %[s03], %[wj3]   \n\t"
            "madd.s %[temp7],  %[temp7],    %[s03], %[wi3]   \n\t"

            "swc1   %[temp4],  8(%[dst_i2])                  \n\t" /* dst[i+2] = s02*wj2 - s12*wi2; */
            "swc1   %[temp5], -8(%[dst_j2])                  \n\t" /* dst[j-2] = s02*wi2 + s12*wj2; */
            "swc1   %[temp6],  12(%[dst_i2])                 \n\t" /* dst[i+2] = s03*wj3 - s13*wi3; */
            "swc1   %[temp7], -12(%[dst_j2])                 \n\t" /* dst[j-3] = s03*wi3 + s13*wj3; */
            : [temp]"=&f"(temp),
              [temp1]"=&f"(temp1), [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
              [temp4]"=&f"(temp4), [temp5]"=&f"(temp5), [temp6]"=&f"(temp6),
              [temp7]  "=&f" (temp7)
            : [dst_j2]"r"(dst_j2),   [dst_i2]"r"(dst_i2),
              [s0] "f"(src0[i + 4]), [wj] "f"(win[j - 4]), [s1] "f"(src1[j - 4]),
              [wi] "f"(win[i + 4]),  [s01]"f"(src0[i + 5]),[wj1]"f"(win[j - 5]),
              [s11]"f"(src1[j - 5]), [wi1]"f"(win[i + 5]), [s02]"f"(src0[i + 6]),
              [wj2]"f"(win[j - 6]),  [s12]"f"(src1[j - 6]),[wi2]"f"(win[i + 6]),
              [s03]"f"(src0[i + 7]), [wj3]"f"(win[j - 7]), [s13]"f"(src1[j - 7]),
              [wi3]"f"(win[i + 7])
            : "memory"
        );
    }
}

static void butterflies_float_mips(float *av_restrict v1, float *av_restrict v2,
                                int len)
{
    float temp0, temp1, temp2, temp3, temp4;
    float temp5, temp6, temp7, temp8, temp9;
    float temp10, temp11, temp12, temp13, temp14, temp15;
    int pom;
    pom = (len >> 2)-1;

    /* loop unrolled 4 times */
    __asm__ volatile (
        "lwc1     %[temp0],    0(%[v1])                 \n\t"
        "lwc1     %[temp1],    4(%[v1])                 \n\t"
        "lwc1     %[temp2],    8(%[v1])                 \n\t"
        "lwc1     %[temp3],    12(%[v1])                \n\t"
        "lwc1     %[temp4],    0(%[v2])                 \n\t"
        "lwc1     %[temp5],    4(%[v2])                 \n\t"
        "lwc1     %[temp6],    8(%[v2])                 \n\t"
        "lwc1     %[temp7],    12(%[v2])                \n\t"
        "beq      %[pom],      $zero,       2f          \n\t"
    "1:                                                 \n\t"
        "sub.s    %[temp8],    %[temp0],    %[temp4]    \n\t"
        "add.s    %[temp9],    %[temp0],    %[temp4]    \n\t"
        "sub.s    %[temp10],   %[temp1],    %[temp5]    \n\t"
        "add.s    %[temp11],   %[temp1],    %[temp5]    \n\t"
        "sub.s    %[temp12],   %[temp2],    %[temp6]    \n\t"
        "add.s    %[temp13],   %[temp2],    %[temp6]    \n\t"
        "sub.s    %[temp14],   %[temp3],    %[temp7]    \n\t"
        "add.s    %[temp15],   %[temp3],    %[temp7]    \n\t"
        "addiu    %[v1],       %[v1],       16          \n\t"
        "addiu    %[v2],       %[v2],       16          \n\t"
        "addiu    %[pom],      %[pom],      -1          \n\t"
        "lwc1     %[temp0],    0(%[v1])                 \n\t"
        "lwc1     %[temp1],    4(%[v1])                 \n\t"
        "lwc1     %[temp2],    8(%[v1])                 \n\t"
        "lwc1     %[temp3],    12(%[v1])                \n\t"
        "lwc1     %[temp4],    0(%[v2])                 \n\t"
        "lwc1     %[temp5],    4(%[v2])                 \n\t"
        "lwc1     %[temp6],    8(%[v2])                 \n\t"
        "lwc1     %[temp7],    12(%[v2])                \n\t"
        "swc1     %[temp9],    -16(%[v1])               \n\t"
        "swc1     %[temp8],    -16(%[v2])               \n\t"
        "swc1     %[temp11],   -12(%[v1])               \n\t"
        "swc1     %[temp10],   -12(%[v2])               \n\t"
        "swc1     %[temp13],   -8(%[v1])                \n\t"
        "swc1     %[temp12],   -8(%[v2])                \n\t"
        "swc1     %[temp15],   -4(%[v1])                \n\t"
        "swc1     %[temp14],   -4(%[v2])                \n\t"
        "bgtz     %[pom],      1b                       \n\t"
    "2:                                                 \n\t"
        "sub.s    %[temp8],    %[temp0],    %[temp4]    \n\t"
        "add.s    %[temp9],    %[temp0],    %[temp4]    \n\t"
        "sub.s    %[temp10],   %[temp1],    %[temp5]    \n\t"
        "add.s    %[temp11],   %[temp1],    %[temp5]    \n\t"
        "sub.s    %[temp12],   %[temp2],    %[temp6]    \n\t"
        "add.s    %[temp13],   %[temp2],    %[temp6]    \n\t"
        "sub.s    %[temp14],   %[temp3],    %[temp7]    \n\t"
        "add.s    %[temp15],   %[temp3],    %[temp7]    \n\t"
        "swc1     %[temp9],    0(%[v1])                 \n\t"
        "swc1     %[temp8],    0(%[v2])                 \n\t"
        "swc1     %[temp11],   4(%[v1])                 \n\t"
        "swc1     %[temp10],   4(%[v2])                 \n\t"
        "swc1     %[temp13],   8(%[v1])                 \n\t"
        "swc1     %[temp12],   8(%[v2])                 \n\t"
        "swc1     %[temp15],   12(%[v1])                \n\t"
        "swc1     %[temp14],   12(%[v2])                \n\t"

        : [v1]"+r"(v1), [v2]"+r"(v2), [pom]"+r"(pom), [temp0] "=&f" (temp0),
          [temp1]"=&f"(temp1), [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
          [temp4]"=&f"(temp4), [temp5]"=&f"(temp5), [temp6]"=&f"(temp6),
          [temp7]"=&f"(temp7), [temp8]"=&f"(temp8), [temp9]"=&f"(temp9),
          [temp10]"=&f"(temp10), [temp11]"=&f"(temp11), [temp12]"=&f"(temp12),
          [temp13]"=&f"(temp13), [temp14]"=&f"(temp14), [temp15]"=&f"(temp15)
        :
        : "memory"
    );
}
#endif /* HAVE_INLINE_ASM && HAVE_MIPSFPU */

void ff_float_dsp_init_mips(AVFloatDSPContext *fdsp) {
#if HAVE_INLINE_ASM && HAVE_MIPSFPU
    fdsp->vector_fmul = vector_fmul_mips;
    fdsp->vector_fmul_scalar  = vector_fmul_scalar_mips;
    fdsp->vector_fmul_window = vector_fmul_window_mips;
    fdsp->butterflies_float = butterflies_float_mips;
#endif /* HAVE_INLINE_ASM && HAVE_MIPSFPU */
}
