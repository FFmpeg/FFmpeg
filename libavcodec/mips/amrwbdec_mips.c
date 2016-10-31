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
 * Author:  Nedeljko Babic (nbabic@mips.com)
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
 * Reference: libavcodec/amrwbdec.c
 */
#include "libavutil/avutil.h"
#include "libavcodec/amrwbdata.h"
#include "amrwbdec_mips.h"

#if HAVE_INLINE_ASM
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
void ff_hb_fir_filter_mips(float *out, const float fir_coef[HB_FIR_SIZE + 1],
                          float mem[HB_FIR_SIZE], const float *in)
{
    int i;
    float data[AMRWB_SFR_SIZE_16k + HB_FIR_SIZE]; // past and current samples

    memcpy(data, mem, HB_FIR_SIZE * sizeof(float));
    memcpy(data + HB_FIR_SIZE, in, AMRWB_SFR_SIZE_16k * sizeof(float));

    for (i = 0; i < AMRWB_SFR_SIZE_16k; i++) {
        float output;
        float * p_data = (data+i);

        /**
        * inner loop is entirely unrolled and instructions are scheduled
        * to minimize pipeline stall
        */
        __asm__ volatile(
            "mtc1       $zero,     %[output]                      \n\t"
            "lwc1       $f0,       0(%[p_data])                   \n\t"
            "lwc1       $f1,       0(%[fir_coef])                 \n\t"
            "lwc1       $f2,       4(%[p_data])                   \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f3,       4(%[fir_coef])                 \n\t"
            "lwc1       $f4,       8(%[p_data])                   \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"
            "lwc1       $f5,       8(%[fir_coef])                 \n\t"

            "lwc1       $f0,       12(%[p_data])                  \n\t"
            "lwc1       $f1,       12(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f2,       16(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f3,       16(%[fir_coef])                \n\t"
            "lwc1       $f4,       20(%[p_data])                  \n\t"
            "lwc1       $f5,       20(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"

            "lwc1       $f0,       24(%[p_data])                  \n\t"
            "lwc1       $f1,       24(%[fir_coef])                \n\t"
            "lwc1       $f2,       28(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f3,       28(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f4,       32(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"
            "lwc1       $f5,       32(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"

            "lwc1       $f0,       36(%[p_data])                  \n\t"
            "lwc1       $f1,       36(%[fir_coef])                \n\t"
            "lwc1       $f2,       40(%[p_data])                  \n\t"
            "lwc1       $f3,       40(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f4,       44(%[p_data])                  \n\t"
            "lwc1       $f5,       44(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"

            "lwc1       $f0,       48(%[p_data])                  \n\t"
            "lwc1       $f1,       48(%[fir_coef])                \n\t"
            "lwc1       $f2,       52(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f3,       52(%[fir_coef])                \n\t"
            "lwc1       $f4,       56(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f5,       56(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"

            "lwc1       $f0,       60(%[p_data])                  \n\t"
            "lwc1       $f1,       60(%[fir_coef])                \n\t"
            "lwc1       $f2,       64(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f3,       64(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f4,       68(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"
            "lwc1       $f5,       68(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"

            "lwc1       $f0,       72(%[p_data])                  \n\t"
            "lwc1       $f1,       72(%[fir_coef])                \n\t"
            "lwc1       $f2,       76(%[p_data])                  \n\t"
            "lwc1       $f3,       76(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f4,       80(%[p_data])                  \n\t"
            "lwc1       $f5,       80(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"

            "lwc1       $f0,       84(%[p_data])                  \n\t"
            "lwc1       $f1,       84(%[fir_coef])                \n\t"
            "lwc1       $f2,       88(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f3,       88(%[fir_coef])                \n\t"
            "lwc1       $f4,       92(%[p_data])                  \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f5,       92(%[fir_coef])                \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"

            "lwc1       $f0,       96(%[p_data])                  \n\t"
            "lwc1       $f1,       96(%[fir_coef])                \n\t"
            "lwc1       $f2,       100(%[p_data])                 \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f3,       100(%[fir_coef])               \n\t"
            "lwc1       $f4,       104(%[p_data])                 \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f5,       104(%[fir_coef])               \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"

            "lwc1       $f0,       108(%[p_data])                 \n\t"
            "lwc1       $f1,       108(%[fir_coef])               \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "lwc1       $f2,       112(%[p_data])                 \n\t"
            "lwc1       $f3,       112(%[fir_coef])               \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"
            "lwc1       $f4,       116(%[p_data])                 \n\t"
            "lwc1       $f5,       116(%[fir_coef])               \n\t"
            "lwc1       $f0,       120(%[p_data])                 \n\t"
            "madd.s     %[output], %[output],       $f2, $f3      \n\t"
            "lwc1       $f1,       120(%[fir_coef])               \n\t"
            "madd.s     %[output], %[output],       $f4, $f5      \n\t"
            "madd.s     %[output], %[output],       $f0, $f1      \n\t"

            : [output]"=&f"(output)
            : [fir_coef]"r"(fir_coef), [p_data]"r"(p_data)
            : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "memory"
        );
        out[i] = output;
    }
    memcpy(mem, data + AMRWB_SFR_SIZE_16k, HB_FIR_SIZE * sizeof(float));
}
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_INLINE_ASM */
