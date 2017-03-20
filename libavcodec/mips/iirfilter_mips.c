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
 * Author:  Bojan Zivkovic (bojan@mips.com)
 *
 * IIR filter optimized for MIPS floating-point architecture
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
 * Reference: libavcodec/iirfilter.c
 */

#include "libavcodec/iirfilter.h"

#if HAVE_INLINE_ASM
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
typedef struct FFIIRFilterCoeffs {
    int   order;
    float gain;
    int   *cx;
    float *cy;
} FFIIRFilterCoeffs;

typedef struct FFIIRFilterState {
    float x[1];
} FFIIRFilterState;

static void iir_filter_flt_mips(const struct FFIIRFilterCoeffs *c,
                                struct FFIIRFilterState *s, int size,
                                const float *src, ptrdiff_t sstep, float *dst, ptrdiff_t dstep)
{
    if (c->order == 2) {
        int i;
        const float *src0 = src;
        float       *dst0 = dst;
        for (i = 0; i < size; i++) {
            float in = *src0 * c->gain  + s->x[0] * c->cy[0] + s->x[1] * c->cy[1];
            *dst0 = s->x[0] + in + s->x[1] * c->cx[1];
            s->x[0] = s->x[1];
            s->x[1] = in;
            src0 += sstep;
            dst0 += dstep;
        }
    } else if (c->order == 4) {
        int i;
        const float *src0 = src;
        float       *dst0 = dst;
        float four = 4.0;
        float six  = 6.0;
        for (i = 0; i < size; i += 4) {
            float in1, in2, in3, in4;
            float res1, res2, res3, res4;
            float *x  = s->x;
            float *cy = c->cy;
            float gain = c->gain;
            float src0_0 = src0[0      ];
            float src0_1 = src0[sstep  ];
            float src0_2 = src0[2*sstep];
            float src0_3 = src0[3*sstep];

            __asm__ volatile (
                "lwc1   $f0,        0(%[cy])                    \n\t"
                "lwc1   $f4,        0(%[x])                     \n\t"
                "lwc1   $f5,        4(%[x])                     \n\t"
                "lwc1   $f6,        8(%[x])                     \n\t"
                "lwc1   $f7,        12(%[x])                    \n\t"
                "mul.s  %[in1],     %[src0_0],  %[gain]         \n\t"
                "mul.s  %[in2],     %[src0_1],  %[gain]         \n\t"
                "mul.s  %[in3],     %[src0_2],  %[gain]         \n\t"
                "mul.s  %[in4],     %[src0_3],  %[gain]         \n\t"
                "lwc1   $f1,        4(%[cy])                    \n\t"
                "madd.s %[in1],     %[in1],     $f0,    $f4     \n\t"
                "madd.s %[in2],     %[in2],     $f0,    $f5     \n\t"
                "madd.s %[in3],     %[in3],     $f0,    $f6     \n\t"
                "madd.s %[in4],     %[in4],     $f0,    $f7     \n\t"
                "lwc1   $f2,        8(%[cy])                    \n\t"
                "madd.s %[in1],     %[in1],     $f1,    $f5     \n\t"
                "madd.s %[in2],     %[in2],     $f1,    $f6     \n\t"
                "madd.s %[in3],     %[in3],     $f1,    $f7     \n\t"
                "lwc1   $f3,        12(%[cy])                   \n\t"
                "add.s  $f8,        $f5,        $f7             \n\t"
                "madd.s %[in1],     %[in1],     $f2,    $f6     \n\t"
                "madd.s %[in2],     %[in2],     $f2,    $f7     \n\t"
                "mul.s  $f9,        $f6,        %[six]          \n\t"
                "mul.s  $f10,       $f7,        %[six]          \n\t"
                "madd.s %[in1],     %[in1],     $f3,    $f7     \n\t"
                "madd.s %[in2],     %[in2],     $f3,    %[in1]  \n\t"
                "madd.s %[in3],     %[in3],     $f2,    %[in1]  \n\t"
                "madd.s %[in4],     %[in4],     $f1,    %[in1]  \n\t"
                "add.s  %[res1],    $f4,        %[in1]          \n\t"
                "swc1   %[in1],     0(%[x])                     \n\t"
                "add.s  $f0,        $f6,        %[in1]          \n\t"
                "madd.s %[in3],     %[in3],     $f3,    %[in2]  \n\t"
                "madd.s %[in4],     %[in4],     $f2,    %[in2]  \n\t"
                "add.s  %[res2],    $f5,        %[in2]          \n\t"
                "madd.s %[res1],    %[res1],    $f8,    %[four] \n\t"
                "add.s  $f8,        $f7,        %[in2]          \n\t"
                "swc1   %[in2],     4(%[x])                     \n\t"
                "madd.s %[in4],     %[in4],     $f3,    %[in3]  \n\t"
                "add.s  %[res3],    $f6,        %[in3]          \n\t"
                "add.s  %[res1],    %[res1],    $f9             \n\t"
                "madd.s %[res2],    %[res2],    $f0,    %[four] \n\t"
                "swc1   %[in3],     8(%[x])                     \n\t"
                "add.s  %[res4],    $f7,        %[in4]          \n\t"
                "madd.s %[res3],    %[res3],    $f8,    %[four] \n\t"
                "swc1   %[in4],     12(%[x])                    \n\t"
                "add.s  %[res2],    %[res2],    $f10            \n\t"
                "add.s  $f8,        %[in1],     %[in3]          \n\t"
                "madd.s %[res3],    %[res3],    %[in1], %[six]  \n\t"
                "madd.s %[res4],    %[res4],    $f8,    %[four] \n\t"
                "madd.s %[res4],    %[res4],    %[in2], %[six]  \n\t"

                : [in1]"=&f"(in1), [in2]"=&f"(in2),
                  [in3]"=&f"(in3), [in4]"=&f"(in4),
                  [res1]"=&f"(res1), [res2]"=&f"(res2),
                  [res3]"=&f"(res3), [res4]"=&f"(res4)
                : [src0_0]"f"(src0_0), [src0_1]"f"(src0_1),
                  [src0_2]"f"(src0_2), [src0_3]"f"(src0_3),
                  [gain]"f"(gain), [x]"r"(x), [cy]"r"(cy),
                  [four]"f"(four), [six]"f"(six)
                : "$f0", "$f1", "$f2", "$f3",
                  "$f4", "$f5", "$f6", "$f7",
                  "$f8", "$f9", "$f10",
                  "memory"
            );

            dst0[0      ] = res1;
            dst0[sstep  ] = res2;
            dst0[2*sstep] = res3;
            dst0[3*sstep] = res4;

            src0 += 4*sstep;
            dst0 += 4*dstep;
        }
    } else {
        int i;
        const float *src0 = src;
        float       *dst0 = dst;
        for (i = 0; i < size; i++) {
            int j;
            float in, res;
            in = *src0 * c->gain;
            for(j = 0; j < c->order; j++)
                in += c->cy[j] * s->x[j];
            res = s->x[0] + in + s->x[c->order >> 1] * c->cx[c->order >> 1];
            for(j = 1; j < c->order >> 1; j++)
                res += (s->x[j] + s->x[c->order - j]) * c->cx[j];
            for(j = 0; j < c->order - 1; j++)
                s->x[j] = s->x[j + 1];
            *dst0 = res;
            s->x[c->order - 1] = in;
            src0 += sstep;
            dst0 += dstep;
        }
    }
}
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_INLINE_ASM */

void ff_iir_filter_init_mips(FFIIRFilterContext *f) {
#if HAVE_INLINE_ASM
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
    f->filter_flt = iir_filter_flt_mips;
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_INLINE_ASM */
}
