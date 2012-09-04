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
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of is
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
#include "config.h"
#include "libavcodec/dsputil.h"

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

av_cold void ff_dsputil_init_mips( DSPContext* c, AVCodecContext *avctx )
{
    c->vector_fmul_window = vector_fmul_window_mips;
}
