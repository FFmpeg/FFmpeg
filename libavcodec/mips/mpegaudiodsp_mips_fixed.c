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
 * MPEG Audio decoder optimized for MIPS fixed-point architecture
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
 * Reference: libavcodec/mpegaudiodsp_template.c
 */

#include <string.h>

#include "libavutil/mips/asmdefs.h"
#include "libavcodec/mpegaudiodsp.h"

static void ff_mpadsp_apply_window_mips_fixed(int32_t *synth_buf, int32_t *window,
                               int *dither_state, int16_t *samples, int incr)
{
    register const int32_t *w, *w2, *p;
    int j;
    int16_t *samples2;
    int w_asm, p_asm, w_asm1, p_asm1, w_asm2, p_asm2;
    int w2_asm, w2_asm1, *p_temp1, *p_temp2;
    int sum1 = 0;
    int const min_asm = -32768, max_asm = 32767;
    int temp1, temp2 = 0, temp3 = 0;
    int64_t sum;

    /* copy to avoid wrap */
    memcpy(synth_buf + 512, synth_buf, 32 * sizeof(*synth_buf));
    samples2 = samples + 31 * incr;
    w = window;
    w2 = window + 31;
    sum = *dither_state;
    p = synth_buf + 16;
    p_temp1 = synth_buf + 16;
    p_temp2 = synth_buf + 48;
    temp1 = sum;

    /**
    * use of round_sample function from the original code is eliminated,
    * changed with appropriate assembly instructions.
    */
    __asm__ volatile (
         "mthi   $zero                                                    \n\t"
         "mtlo   %[temp1]                                                 \n\t"
         "lw     %[w_asm],  0(%[w])                                       \n\t"
         "lw     %[p_asm],  0(%[p])                                       \n\t"
         "lw     %[w_asm1], 64*4(%[w])                                    \n\t"
         "lw     %[p_asm1], 64*4(%[p])                                    \n\t"
         "lw     %[w_asm2], 128*4(%[w])                                   \n\t"
         "lw     %[p_asm2], 128*4(%[p])                                   \n\t"
         "madd   %[w_asm],  %[p_asm]                                      \n\t"
         "madd   %[w_asm1], %[p_asm1]                                     \n\t"
         "madd   %[w_asm2], %[p_asm2]                                     \n\t"
         "lw     %[w_asm],  192*4(%[w])                                   \n\t"
         "lw     %[p_asm],  192*4(%[p])                                   \n\t"
         "lw     %[w_asm1], 256*4(%[w])                                   \n\t"
         "lw     %[p_asm1], 256*4(%[p])                                   \n\t"
         "lw     %[w_asm2], 320*4(%[w])                                   \n\t"
         "lw     %[p_asm2], 320*4(%[p])                                   \n\t"
         "madd   %[w_asm],  %[p_asm]                                      \n\t"
         "madd   %[w_asm1], %[p_asm1]                                     \n\t"
         "madd   %[w_asm2], %[p_asm2]                                     \n\t"
         "lw     %[w_asm],  384*4(%[w])                                   \n\t"
         "lw     %[p_asm],  384*4(%[p])                                   \n\t"
         "lw     %[w_asm1], 448*4(%[w])                                   \n\t"
         "lw     %[p_asm1], 448*4(%[p])                                   \n\t"
         "lw     %[w_asm2], 32*4(%[w])                                    \n\t"
         "lw     %[p_asm2], 32*4(%[p])                                    \n\t"
         "madd   %[w_asm],  %[p_asm]                                      \n\t"
         "madd   %[w_asm1], %[p_asm1]                                     \n\t"
         "msub   %[w_asm2], %[p_asm2]                                     \n\t"
         "lw     %[w_asm],  96*4(%[w])                                    \n\t"
         "lw     %[p_asm],  96*4(%[p])                                    \n\t"
         "lw     %[w_asm1], 160*4(%[w])                                   \n\t"
         "lw     %[p_asm1], 160*4(%[p])                                   \n\t"
         "lw     %[w_asm2], 224*4(%[w])                                   \n\t"
         "lw     %[p_asm2], 224*4(%[p])                                   \n\t"
         "msub   %[w_asm],  %[p_asm]                                      \n\t"
         "msub   %[w_asm1], %[p_asm1]                                     \n\t"
         "msub   %[w_asm2], %[p_asm2]                                     \n\t"
         "lw     %[w_asm],  288*4(%[w])                                   \n\t"
         "lw     %[p_asm],  288*4(%[p])                                   \n\t"
         "lw     %[w_asm1], 352*4(%[w])                                   \n\t"
         "lw     %[p_asm1], 352*4(%[p])                                   \n\t"
         "msub   %[w_asm],  %[p_asm]                                      \n\t"
         "lw     %[w_asm],  480*4(%[w])                                   \n\t"
         "lw     %[p_asm],  480*4(%[p])                                   \n\t"
         "lw     %[w_asm2], 416*4(%[w])                                   \n\t"
         "lw     %[p_asm2], 416*4(%[p])                                   \n\t"
         "msub   %[w_asm],  %[p_asm]                                      \n\t"
         "msub   %[w_asm1], %[p_asm1]                                     \n\t"
         "msub   %[w_asm2], %[p_asm2]                                     \n\t"

         /*round_sample function from the original code is eliminated,
          * changed with appropriate assembly instructions
          * code example:

         "extr.w  %[sum1],$ac0,24                                       \n\t"
         "mflo %[temp3],  $ac0                                          \n\t"
         "and  %[temp1],  %[temp3],  0x00ffffff                         \n\t"
         "slt  %[temp2],  %[sum1],   %[min_asm]                         \n\t"
         "movn %[sum1],   %[min_asm],%[temp2]                           \n\t"
         "slt  %[temp2],  %[max_asm],%[sum1]                            \n\t"
         "movn %[sum1],   %[max_asm],%[temp2]                           \n\t"
         "sh   %[sum1],   0(%[samples])                                 \n\t"
         */

         "extr.w %[sum1],   $ac0,       24                                \n\t"
         "mflo   %[temp3]                                                 \n\t"
         PTR_ADDIU "%[w],   %[w],       4                                 \n\t"
         "and    %[temp1],  %[temp3],   0x00ffffff                        \n\t"
         "slt    %[temp2],  %[sum1],    %[min_asm]                        \n\t"
         "movn   %[sum1],   %[min_asm], %[temp2]                          \n\t"
         "slt    %[temp2],  %[max_asm], %[sum1]                           \n\t"
         "movn   %[sum1],   %[max_asm], %[temp2]                          \n\t"
         "sh     %[sum1],   0(%[samples])                                 \n\t"

        : [w_asm] "=&r" (w_asm), [p_asm] "=&r" (p_asm), [w_asm1] "=&r" (w_asm1),
          [p_asm1] "=&r" (p_asm1), [temp1] "+r" (temp1), [temp2] "+r" (temp2),
          [w_asm2] "=&r" (w_asm2), [p_asm2] "=&r" (p_asm2),
          [sum1] "+r" (sum1), [w] "+r" (w), [temp3] "+r" (temp3)
        : [p] "r" (p), [samples] "r" (samples), [min_asm] "r" (min_asm),
          [max_asm] "r" (max_asm)
        : "memory", "hi","lo"
     );

     samples += incr;

    /* we calculate two samples at the same time to avoid one memory
       access per two sample */

    for(j = 1; j < 16; j++) {
        __asm__ volatile (
             "mthi   $0,         $ac1                                      \n\t"
             "mtlo   $0,         $ac1                                      \n\t"
             "mthi   $0                                                    \n\t"
             "mtlo   %[temp1]                                              \n\t"
             PTR_ADDIU "%[p_temp1], %[p_temp1],    4                       \n\t"
             "lw     %[w_asm],   0(%[w])                                   \n\t"
             "lw     %[p_asm],   0(%[p_temp1])                             \n\t"
             "lw     %[w2_asm],  0(%[w2])                                  \n\t"
             "lw     %[w_asm1],  64*4(%[w])                                \n\t"
             "lw     %[p_asm1],  64*4(%[p_temp1])                          \n\t"
             "lw     %[w2_asm1], 64*4(%[w2])                               \n\t"
             "madd   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "madd   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             "lw     %[w_asm],   128*4(%[w])                               \n\t"
             "lw     %[p_asm],   128*4(%[p_temp1])                         \n\t"
             "lw     %[w2_asm],  128*4(%[w2])                              \n\t"
             "lw     %[w_asm1],  192*4(%[w])                               \n\t"
             "lw     %[p_asm1],  192*4(%[p_temp1])                         \n\t"
             "lw     %[w2_asm1], 192*4(%[w2])                              \n\t"
             "madd   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "madd   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             "lw     %[w_asm],   256*4(%[w])                               \n\t"
             "lw     %[p_asm],   256*4(%[p_temp1])                         \n\t"
             "lw     %[w2_asm],  256*4(%[w2])                              \n\t"
             "lw     %[w_asm1],  320*4(%[w])                               \n\t"
             "lw     %[p_asm1],  320*4(%[p_temp1])                         \n\t"
             "lw     %[w2_asm1], 320*4(%[w2])                              \n\t"
             "madd   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "madd   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             "lw     %[w_asm],   384*4(%[w])                               \n\t"
             "lw     %[p_asm],   384*4(%[p_temp1])                         \n\t"
             "lw     %[w2_asm],  384*4(%[w2])                              \n\t"
             "lw     %[w_asm1],  448*4(%[w])                               \n\t"
             "lw     %[p_asm1],  448*4(%[p_temp1])                         \n\t"
             "lw     %[w2_asm1], 448*4(%[w2])                              \n\t"
             "madd   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "madd   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             PTR_ADDIU "%[p_temp2], %[p_temp2],   -4                      \n\t"
             "lw     %[w_asm],   32*4(%[w])                                \n\t"
             "lw     %[p_asm],   0(%[p_temp2])                             \n\t"
             "lw     %[w2_asm],  32*4(%[w2])                               \n\t"
             "lw     %[w_asm1],  96*4(%[w])                                \n\t"
             "lw     %[p_asm1],  64*4(%[p_temp2])                          \n\t"
             "lw     %[w2_asm1], 96*4(%[w2])                               \n\t"
             "msub   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "msub   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             "lw     %[w_asm],   160*4(%[w])                               \n\t"
             "lw     %[p_asm],   128*4(%[p_temp2])                         \n\t"
             "lw     %[w2_asm],  160*4(%[w2])                              \n\t"
             "lw     %[w_asm1],  224*4(%[w])                               \n\t"
             "lw     %[p_asm1],  192*4(%[p_temp2])                         \n\t"
             "lw     %[w2_asm1], 224*4(%[w2])                              \n\t"
             "msub   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "msub   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             "lw     %[w_asm],   288*4(%[w])                               \n\t"
             "lw     %[p_asm],   256*4(%[p_temp2])                         \n\t"
             "lw     %[w2_asm],  288*4(%[w2])                              \n\t"
             "lw     %[w_asm1],  352*4(%[w])                               \n\t"
             "lw     %[p_asm1],  320*4(%[p_temp2])                         \n\t"
             "lw     %[w2_asm1], 352*4(%[w2])                              \n\t"
             "msub   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "msub   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             "lw     %[w_asm],   416*4(%[w])                               \n\t"
             "lw     %[p_asm],   384*4(%[p_temp2])                         \n\t"
             "lw     %[w2_asm],  416*4(%[w2])                              \n\t"
             "lw     %[w_asm1],  480*4(%[w])                               \n\t"
             "lw     %[p_asm1],  448*4(%[p_temp2])                         \n\t"
             "lw     %[w2_asm1], 480*4(%[w2])                              \n\t"
             "msub   %[w_asm],   %[p_asm]                                  \n\t"
             "msub   %[w_asm1],  %[p_asm1]                                 \n\t"
             "msub   $ac1,       %[w2_asm],        %[p_asm]                \n\t"
             "msub   $ac1,       %[w2_asm1],       %[p_asm1]               \n\t"
             PTR_ADDIU "%[w],    %[w],             4                       \n\t"
             PTR_ADDIU "%[w2],   %[w2],            -4                      \n\t"
             "mflo   %[temp2]                                              \n\t"
             "extr.w %[sum1],    $ac0,             24                      \n\t"
             "li     %[temp3],   1                                         \n\t"
             "and    %[temp1],   %[temp2],         0x00ffffff              \n\t"
             "madd   $ac1,       %[temp1],         %[temp3]                \n\t"
             "slt    %[temp2],   %[sum1],          %[min_asm]              \n\t"
             "movn   %[sum1],    %[min_asm],       %[temp2]                \n\t"
             "slt    %[temp2],   %[max_asm],       %[sum1]                 \n\t"
             "movn   %[sum1],    %[max_asm],       %[temp2]                \n\t"
             "sh     %[sum1],    0(%[samples])                             \n\t"
             "mflo   %[temp3],   $ac1                                      \n\t"
             "extr.w %[sum1],    $ac1,             24                      \n\t"
             "and    %[temp1],   %[temp3],         0x00ffffff              \n\t"
             "slt    %[temp2],   %[sum1],          %[min_asm]              \n\t"
             "movn   %[sum1],    %[min_asm],       %[temp2]                \n\t"
             "slt    %[temp2],   %[max_asm],       %[sum1]                 \n\t"
             "movn   %[sum1],    %[max_asm],       %[temp2]                \n\t"
             "sh     %[sum1],    0(%[samples2])                            \n\t"

            : [w_asm] "=&r" (w_asm), [p_asm] "=&r" (p_asm), [w_asm1] "=&r" (w_asm1),
              [p_asm1] "=&r" (p_asm1), [w2_asm1] "=&r" (w2_asm1),
              [w2_asm] "=&r" (w2_asm), [temp1] "+r" (temp1), [temp2] "+r" (temp2),
              [p_temp1] "+r" (p_temp1), [p_temp2] "+r" (p_temp2), [sum1] "+r" (sum1),
              [w] "+r" (w), [w2] "+r" (w2), [samples] "+r" (samples),
              [samples2] "+r" (samples2), [temp3] "+r" (temp3)
            : [min_asm] "r" (min_asm), [max_asm] "r" (max_asm)
            : "memory", "hi", "lo", "$ac1hi", "$ac1lo"
        );

        samples += incr;
        samples2 -= incr;
    }

    p = synth_buf + 32;

    __asm__ volatile (
        "mthi   $0                                                        \n\t"
        "mtlo   %[temp1]                                                  \n\t"
        "lw     %[w_asm],  32*4(%[w])                                     \n\t"
        "lw     %[p_asm],  0(%[p])                                        \n\t"
        "lw     %[w_asm1], 96*4(%[w])                                     \n\t"
        "lw     %[p_asm1], 64*4(%[p])                                     \n\t"
        "lw     %[w_asm2], 160*4(%[w])                                    \n\t"
        "lw     %[p_asm2], 128*4(%[p])                                    \n\t"
        "msub   %[w_asm],  %[p_asm]                                       \n\t"
        "msub   %[w_asm1], %[p_asm1]                                      \n\t"
        "msub   %[w_asm2], %[p_asm2]                                      \n\t"
        "lw     %[w_asm],  224*4(%[w])                                    \n\t"
        "lw     %[p_asm],  192*4(%[p])                                    \n\t"
        "lw     %[w_asm1], 288*4(%[w])                                    \n\t"
        "lw     %[p_asm1], 256*4(%[p])                                    \n\t"
        "lw     %[w_asm2], 352*4(%[w])                                    \n\t"
        "lw     %[p_asm2], 320*4(%[p])                                    \n\t"
        "msub   %[w_asm],  %[p_asm]                                       \n\t"
        "msub   %[w_asm1], %[p_asm1]                                      \n\t"
        "msub   %[w_asm2], %[p_asm2]                                      \n\t"
        "lw     %[w_asm],  416*4(%[w])                                    \n\t"
        "lw     %[p_asm],  384*4(%[p])                                    \n\t"
        "lw     %[w_asm1], 480*4(%[w])                                    \n\t"
        "lw     %[p_asm1], 448*4(%[p])                                    \n\t"
        "msub   %[w_asm],  %[p_asm]                                       \n\t"
        "msub   %[w_asm1], %[p_asm1]                                      \n\t"
        "extr.w %[sum1],   $ac0,       24                                 \n\t"
        "mflo   %[temp2]                                                  \n\t"
        "and    %[temp1],  %[temp2],   0x00ffffff                         \n\t"
        "slt    %[temp2],  %[sum1],    %[min_asm]                         \n\t"
        "movn   %[sum1],   %[min_asm], %[temp2]                           \n\t"
        "slt    %[temp2],  %[max_asm], %[sum1]                            \n\t"
        "movn   %[sum1],   %[max_asm], %[temp2]                           \n\t"
        "sh     %[sum1],   0(%[samples])                                  \n\t"

        : [w_asm] "=&r" (w_asm), [p_asm] "=&r" (p_asm), [w_asm1] "=&r" (w_asm1),
          [p_asm1] "=&r" (p_asm1), [temp1] "+r" (temp1), [temp2] "+r" (temp2),
          [w_asm2] "=&r" (w_asm2), [p_asm2] "=&r" (p_asm2), [sum1] "+r" (sum1)
        : [w] "r" (w), [p] "r" (p), [samples] "r" (samples), [min_asm] "r" (min_asm),
          [max_asm] "r" (max_asm)
        : "memory", "hi", "lo", "$ac1hi", "$ac1lo"
     );

    *dither_state= temp1;
}

static void imdct36_mips_fixed(int *out, int *buf, int *in, int *win)
{
    int j;
    int t0, t1, t2, t3, s0, s1, s2, s3;
    int tmp[18], *tmp1, *in1;
    /* temporary variables */
    int temp_reg1, temp_reg2, temp_reg3, temp_reg4, temp_reg5, temp_reg6;
    int t4, t5, t6, t8, t7;

   /* values defined in macros and tables are
    * eliminated - they are directly loaded in appropriate variables
    */
    int const C_1  =  4229717092; /* cos(pi*1/18)*2  */
    int const C_2  =  4035949074; /* cos(pi*2/18)*2  */
    int const C_3  =  575416510;  /* -cos(pi*3/18)*2 */
    int const C_3A =  3719550786; /* cos(pi*3/18)*2  */
    int const C_4  =  1004831466; /* -cos(pi*4/18)*2 */
    int const C_5  =  1534215534; /* -cos(pi*5/18)*2 */
    int const C_7  = -1468965330; /* -cos(pi*7/18)*2 */
    int const C_8  = -745813244;  /* -cos(pi*8/18)*2 */

   /*
    * instructions of the first two loops are reorganized and loops are unrolled,
    * in order to eliminate unnecessary readings and writings in array
    */

    __asm__ volatile (
        "lw   %[t1], 17*4(%[in])                                         \n\t"
        "lw   %[t2], 16*4(%[in])                                         \n\t"
        "lw   %[t3], 15*4(%[in])                                         \n\t"
        "lw   %[t4], 14*4(%[in])                                         \n\t"
        "addu %[t1], %[t1],      %[t2]                                   \n\t"
        "addu %[t2], %[t2],      %[t3]                                   \n\t"
        "addu %[t3], %[t3],      %[t4]                                   \n\t"
        "lw   %[t5], 13*4(%[in])                                         \n\t"
        "addu %[t1], %[t1],      %[t3]                                   \n\t"
        "sw   %[t2], 16*4(%[in])                                         \n\t"
        "lw   %[t6], 12*4(%[in])                                         \n\t"
        "sw   %[t1], 17*4(%[in])                                         \n\t"
        "addu %[t4], %[t4],      %[t5]                                   \n\t"
        "addu %[t5], %[t5],      %[t6]                                   \n\t"
        "lw   %[t7], 11*4(%[in])                                         \n\t"
        "addu %[t3], %[t3],      %[t5]                                   \n\t"
        "sw   %[t4], 14*4(%[in])                                         \n\t"
        "lw   %[t8], 10*4(%[in])                                         \n\t"
        "sw   %[t3], 15*4(%[in])                                         \n\t"
        "addu %[t6], %[t6],      %[t7]                                   \n\t"
        "addu %[t7], %[t7],      %[t8]                                   \n\t"
        "sw   %[t6], 12*4(%[in])                                         \n\t"
        "addu %[t5], %[t5],      %[t7]                                   \n\t"
        "lw   %[t1], 9*4(%[in])                                          \n\t"
        "lw   %[t2], 8*4(%[in])                                          \n\t"
        "sw   %[t5], 13*4(%[in])                                         \n\t"
        "addu %[t8], %[t8],      %[t1]                                   \n\t"
        "addu %[t1], %[t1],      %[t2]                                   \n\t"
        "sw   %[t8], 10*4(%[in])                                         \n\t"
        "addu %[t7], %[t7],      %[t1]                                   \n\t"
        "lw   %[t3], 7*4(%[in])                                          \n\t"
        "lw   %[t4], 6*4(%[in])                                          \n\t"
        "sw   %[t7], 11*4(%[in])                                         \n\t"
        "addu %[t2], %[t2],      %[t3]                                   \n\t"
        "addu %[t3], %[t3],      %[t4]                                   \n\t"
        "sw   %[t2], 8*4(%[in])                                          \n\t"
        "addu %[t1], %[t1],      %[t3]                                   \n\t"
        "lw   %[t5], 5*4(%[in])                                          \n\t"
        "lw   %[t6], 4*4(%[in])                                          \n\t"
        "sw   %[t1], 9*4(%[in])                                          \n\t"
        "addu %[t4], %[t4],      %[t5]                                   \n\t"
        "addu %[t5], %[t5],      %[t6]                                   \n\t"
        "sw   %[t4], 6*4(%[in])                                          \n\t"
        "addu %[t3], %[t3],      %[t5]                                   \n\t"
        "lw   %[t7], 3*4(%[in])                                          \n\t"
        "lw   %[t8], 2*4(%[in])                                          \n\t"
        "sw   %[t3], 7*4(%[in])                                          \n\t"
        "addu %[t6], %[t6],      %[t7]                                   \n\t"
        "addu %[t7], %[t7],      %[t8]                                   \n\t"
        "sw   %[t6], 4*4(%[in])                                          \n\t"
        "addu %[t5], %[t5],      %[t7]                                   \n\t"
        "lw   %[t1], 1*4(%[in])                                          \n\t"
        "lw   %[t2], 0*4(%[in])                                          \n\t"
        "sw   %[t5], 5*4(%[in])                                          \n\t"
        "addu %[t8], %[t8],      %[t1]                                   \n\t"
        "addu %[t1], %[t1],      %[t2]                                   \n\t"
        "sw   %[t8], 2*4(%[in])                                          \n\t"
        "addu %[t7], %[t7],      %[t1]                                   \n\t"
        "sw   %[t7], 3*4(%[in])                                          \n\t"
        "sw   %[t1], 1*4(%[in])                                          \n\t"

        : [in] "+r" (in), [t1] "=&r" (t1), [t2] "=&r" (t2), [t3] "=&r" (t3),
          [t4] "=&r" (t4), [t5] "=&r" (t5), [t6] "=&r" (t6),
          [t7] "=&r" (t7), [t8] "=&r" (t8)
        :
        : "memory"
    );

    for(j = 0; j < 2; j++) {

        tmp1 = tmp + j;
        in1 = in + j;

         /**
         *  Original constants are multiplied by two in advanced
         *  for assembly optimization (e.g. C_2 = 2 * C2).
         *  That can lead to overflow in operations where they are used.
         *
         *  Example of the solution:
         *
         *  in original code:
         *  t0 = ((int64_t)(in1[2*2] + in1[2*4]) * (int64_t)(2*C2))>>32
         *
         *  in assembly:
         *  C_2 = 2 * C2;
         *   .
         *   .
         *  "lw   %[t7],       4*4(%[in1])                               \n\t"
         *  "lw   %[t8],       8*4(%[in1])                               \n\t"
         *  "addu %[temp_reg2],%[t7],       %[t8]                        \n\t"
         *  "multu %[C_2],     %[temp_reg2]                              \n\t"
         *  "mfhi %[temp_reg1]                                           \n\t"
         *  "sra  %[temp_reg2],%[temp_reg2],31                           \n\t"
         *  "move %[t0],       $0                                        \n\t"
         *  "movn %[t0],       %[C_2],      %[temp_reg2]                 \n\t"
         *  "sub  %[t0],       %[temp_reg1],%[t0]                        \n\t"
         */

        __asm__ volatile (
            "lw    %[t7],        4*4(%[in1])                               \n\t"
            "lw    %[t8],        8*4(%[in1])                               \n\t"
            "lw    %[t6],        16*4(%[in1])                              \n\t"
            "lw    %[t4],        0*4(%[in1])                               \n\t"
            "addu  %[temp_reg2], %[t7],        %[t8]                       \n\t"
            "addu  %[t2],        %[t6],        %[t8]                       \n\t"
            "multu %[C_2],       %[temp_reg2]                              \n\t"
            "lw    %[t5],        12*4(%[in1])                              \n\t"
            "sub   %[t2],        %[t2],        %[t7]                       \n\t"
            "sub   %[t1],        %[t4],        %[t5]                       \n\t"
            "sra   %[t3],        %[t5],        1                           \n\t"
            "sra   %[temp_reg1], %[t2],        1                           \n\t"
            "addu  %[t3],        %[t3],        %[t4]                       \n\t"
            "sub   %[temp_reg1], %[t1],        %[temp_reg1]                \n\t"
            "sra   %[temp_reg2], %[temp_reg2], 31                          \n\t"
            "sw    %[temp_reg1], 6*4(%[tmp1])                              \n\t"
            "move  %[t0],        $0                                        \n\t"
            "movn  %[t0],        %[C_2],       %[temp_reg2]                \n\t"
            "mfhi  %[temp_reg1]                                            \n\t"
            "addu  %[t1],        %[t1],        %[t2]                       \n\t"
            "sw    %[t1],        16*4(%[tmp1])                             \n\t"
            "sub   %[temp_reg4], %[t8],        %[t6]                       \n\t"
            "add   %[temp_reg2], %[t7],        %[t6]                       \n\t"
            "mult  $ac1,         %[C_8],       %[temp_reg4]                \n\t"
            "multu $ac2,         %[C_4],       %[temp_reg2]                \n\t"
            "sub   %[t0],        %[temp_reg1], %[t0]                       \n\t"
            "sra   %[temp_reg1], %[temp_reg2], 31                          \n\t"
            "move  %[t2],        $0                                        \n\t"
            "movn  %[t2],        %[C_4],       %[temp_reg1]                \n\t"
            "mfhi  %[t1],        $ac1                                      \n\t"
            "mfhi  %[temp_reg1], $ac2                                      \n\t"
            "lw    %[t6],        10*4(%[in1])                              \n\t"
            "lw    %[t8],        14*4(%[in1])                              \n\t"
            "lw    %[t7],        2*4(%[in1])                               \n\t"
            "lw    %[t4],        6*4(%[in1])                               \n\t"
            "sub   %[temp_reg3], %[t3],        %[t0]                       \n\t"
            "add   %[temp_reg4], %[t3],        %[t0]                       \n\t"
            "sub   %[temp_reg1], %[temp_reg1], %[temp_reg2]                \n\t"
            "add   %[temp_reg4], %[temp_reg4], %[t1]                       \n\t"
            "sub   %[t2],        %[temp_reg1], %[t2]                       \n\t"
            "sw    %[temp_reg4], 2*4(%[tmp1])                              \n\t"
            "sub   %[temp_reg3], %[temp_reg3], %[t2]                       \n\t"
            "add   %[temp_reg1], %[t3],        %[t2]                       \n\t"
            "sw    %[temp_reg3], 10*4(%[tmp1])                             \n\t"
            "sub   %[temp_reg1], %[temp_reg1], %[t1]                       \n\t"
            "addu  %[temp_reg2], %[t6],        %[t8]                       \n\t"
            "sw    %[temp_reg1], 14*4(%[tmp1])                             \n\t"
            "sub   %[temp_reg2], %[temp_reg2], %[t7]                       \n\t"
            "addu  %[temp_reg3], %[t7],        %[t6]                       \n\t"
            "multu $ac3,         %[C_3],       %[temp_reg2]                \n\t"
            "multu %[C_1],       %[temp_reg3]                              \n\t"
            "sra   %[temp_reg1], %[temp_reg2], 31                          \n\t"
            "move  %[t1],        $0                                        \n\t"
            "sra   %[temp_reg3], %[temp_reg3], 31                          \n\t"
            "movn  %[t1],        %[C_3],       %[temp_reg1]                \n\t"
            "mfhi  %[temp_reg1], $ac3                                      \n\t"
            "mfhi  %[temp_reg4]                                            \n\t"
            "move  %[t2],        $0                                        \n\t"
            "movn  %[t2],        %[C_1],       %[temp_reg3]                \n\t"
            "sub   %[temp_reg3], %[t6],        %[t8]                       \n\t"
            "sub   %[t2],        %[temp_reg4], %[t2]                       \n\t"
            "multu $ac1,         %[C_7],       %[temp_reg3]                \n\t"
            "sub   %[temp_reg1], %[temp_reg1], %[temp_reg2]                \n\t"
            "sra   %[temp_reg4], %[temp_reg3], 31                          \n\t"
            "sub   %[t1],        %[temp_reg1], %[t1]                       \n\t"
            "move  %[t3],        $0                                        \n\t"
            "sw    %[t1],        4*4(%[tmp1])                              \n\t"
            "movn  %[t3],        %[C_7],       %[temp_reg4]                \n\t"
            "multu $ac2,         %[C_3A],      %[t4]                       \n\t"
            "add   %[temp_reg2], %[t7],        %[t8]                       \n\t"
            "move  %[t1],        $0                                        \n\t"
            "mfhi  %[temp_reg4], $ac1                                      \n\t"
            "multu $ac3,%[C_5],  %[temp_reg2]                              \n\t"
            "move  %[t0],        $0                                        \n\t"
            "sra   %[temp_reg1], %[temp_reg2], 31                          \n\t"
            "movn  %[t1],%[C_5], %[temp_reg1]                              \n\t"
            "sub   %[temp_reg4], %[temp_reg4], %[temp_reg3]                \n\t"
            "mfhi  %[temp_reg1], $ac3                                      \n\t"
            "sra   %[temp_reg3], %[t4],        31                          \n\t"
            "movn  %[t0],        %[C_3A],      %[temp_reg3]                \n\t"
            "mfhi  %[temp_reg3], $ac2                                      \n\t"
            "sub   %[t3],        %[temp_reg4], %[t3]                       \n\t"
            "add   %[temp_reg4], %[t3],        %[t2]                       \n\t"
            "sub   %[temp_reg1], %[temp_reg1], %[temp_reg2]                \n\t"
            "sub   %[t1],        %[temp_reg1], %[t1]                       \n\t"
            "sub   %[t0],        %[temp_reg3], %[t0]                       \n\t"
            "add   %[temp_reg1], %[t2],        %[t1]                       \n\t"
            "add   %[temp_reg4], %[temp_reg4], %[t0]                       \n\t"
            "sub   %[temp_reg2], %[t3],        %[t1]                       \n\t"
            "sw    %[temp_reg4], 0*4(%[tmp1])                              \n\t"
            "sub   %[temp_reg1], %[temp_reg1], %[t0]                       \n\t"
            "sub   %[temp_reg2], %[temp_reg2], %[t0]                       \n\t"
            "sw    %[temp_reg1], 12*4(%[tmp1])                             \n\t"
            "sw    %[temp_reg2], 8*4(%[tmp1])                              \n\t"

            : [t7] "=&r" (t7), [temp_reg1] "=&r" (temp_reg1),
              [temp_reg2] "=&r" (temp_reg2), [temp_reg4] "=&r" (temp_reg4),
              [temp_reg3] "=&r" (temp_reg3), [t8] "=&r" (t8), [t0] "=&r" (t0),
              [t4] "=&r" (t4), [t5] "=&r" (t5), [t6] "=&r"(t6), [t2] "=&r" (t2),
              [t3] "=&r" (t3), [t1] "=&r" (t1)
            : [C_2] "r" (C_2), [in1] "r" (in1), [tmp1] "r" (tmp1), [C_8] "r" (C_8),
              [C_4] "r" (C_4), [C_3] "r" (C_3), [C_1] "r" (C_1), [C_7] "r" (C_7),
              [C_3A] "r" (C_3A), [C_5] "r" (C_5)
            : "memory", "hi", "lo", "$ac1hi", "$ac1lo", "$ac2hi", "$ac2lo",
              "$ac3hi", "$ac3lo"
         );
    }

    /**
    * loop is unrolled four times
    *
    * values defined in tables(icos36[] and icos36h[]) are not loaded from
    * these tables - they are directly loaded in appropriate registers
    *
    */

    __asm__ volatile (
        "lw     %[t2],        1*4(%[tmp])                                  \n\t"
        "lw     %[t3],        3*4(%[tmp])                                  \n\t"
        "lw     %[t0],        0*4(%[tmp])                                  \n\t"
        "lw     %[t1],        2*4(%[tmp])                                  \n\t"
        "addu   %[temp_reg1], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg2], 0x807D2B1E                                   \n\t"
        "move   %[s1],        $0                                           \n\t"
        "multu  %[temp_reg2], %[temp_reg1]                                 \n\t"
        "sra    %[temp_reg1], %[temp_reg1], 31                             \n\t"
        "movn   %[s1],        %[temp_reg2], %[temp_reg1]                   \n\t"
        "sub    %[temp_reg3], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg4], 0x2de5151                                    \n\t"
        "mfhi   %[temp_reg2]                                               \n\t"
        "addu   %[s0],        %[t1],        %[t0]                          \n\t"
        "lw     %[temp_reg5], 9*4(%[win])                                  \n\t"
        "mult   $ac1,         %[temp_reg4], %[temp_reg3]                   \n\t"
        "lw     %[temp_reg6], 4*9*4(%[buf])                                \n\t"
        "sub    %[s2],        %[t1],        %[t0]                          \n\t"
        "lw     %[temp_reg3], 29*4(%[win])                                 \n\t"
        "subu   %[s1],        %[temp_reg2], %[s1]                          \n\t"
        "lw     %[temp_reg4], 28*4(%[win])                                 \n\t"
        "add    %[t0],        %[s0],        %[s1]                          \n\t"
        "extr.w %[s3],        $ac1,23                                      \n\t"
        "mult   $ac2,         %[t0],        %[temp_reg3]                   \n\t"
        "sub    %[t1],        %[s0],        %[s1]                          \n\t"
        "lw     %[temp_reg1], 4*8*4(%[buf])                                \n\t"
        "mult   %[t1],        %[temp_reg5]                                 \n\t"
        "lw     %[temp_reg2], 8*4(%[win])                                  \n\t"
        "mfhi   %[temp_reg3], $ac2                                         \n\t"
        "mult   $ac3,         %[t0],        %[temp_reg4]                   \n\t"
        "add    %[t0],        %[s2],        %[s3]                          \n\t"
        "mfhi   %[temp_reg5]                                               \n\t"
        "mult   $ac1,         %[t1],        %[temp_reg2]                   \n\t"
        "sub    %[t1],        %[s2],        %[s3]                          \n\t"
        "sw     %[temp_reg3], 4*9*4(%[buf])                                \n\t"
        "mfhi   %[temp_reg4], $ac3                                         \n\t"
        "lw     %[temp_reg3], 37*4(%[win])                                 \n\t"
        "mfhi   %[temp_reg2], $ac1                                         \n\t"
        "add    %[temp_reg5], %[temp_reg5], %[temp_reg6]                   \n\t"
        "lw     %[temp_reg6], 17*4(%[win])                                 \n\t"
        "sw     %[temp_reg5], 32*9*4(%[out])                               \n\t"
        "sw     %[temp_reg4], 4*8*4(%[buf])                                \n\t"
        "mult   %[t1],        %[temp_reg6]                                 \n\t"
        "add    %[temp_reg1], %[temp_reg1], %[temp_reg2]                   \n\t"
        "lw     %[temp_reg2], 0*4(%[win])                                  \n\t"
        "lw     %[temp_reg5], 4*17*4(%[buf])                               \n\t"
        "sw     %[temp_reg1], 8*32*4(%[out])                               \n\t"
        "mfhi   %[temp_reg6]                                               \n\t"
        "mult   $ac1,         %[t1],        %[temp_reg2]                   \n\t"
        "lw     %[temp_reg4], 20*4(%[win])                                 \n\t"
        "lw     %[temp_reg1], 0(%[buf])                                    \n\t"
        "mult   $ac2,         %[t0],        %[temp_reg3]                   \n\t"
        "mult   %[t0],        %[temp_reg4]                                 \n\t"
        "mfhi   %[temp_reg2], $ac1                                         \n\t"
        "lw     %[t0],        4*4(%[tmp])                                  \n\t"
        "add    %[temp_reg5], %[temp_reg5], %[temp_reg6]                   \n\t"
        "mfhi   %[temp_reg3], $ac2                                         \n\t"
        "mfhi   %[temp_reg4]                                               \n\t"
        "sw     %[temp_reg5], 17*32*4(%[out])                              \n\t"
        "lw     %[t1],        6*4(%[tmp])                                  \n\t"
        "add    %[temp_reg1], %[temp_reg1], %[temp_reg2]                   \n\t"
        "lw     %[t2],        5*4(%[tmp])                                  \n\t"
        "sw     %[temp_reg1], 0*32*4(%[out])                               \n\t"
        "addu   %[s0],        %[t1],        %[t0]                          \n\t"
        "sw     %[temp_reg3], 4*17*4(%[buf])                               \n\t"
        "lw     %[t3],        7*4(%[tmp])                                  \n\t"
        "sub    %[s2],        %[t1],        %[t0]                          \n\t"
        "sw     %[temp_reg4], 0(%[buf])                                    \n\t"
        "addu   %[temp_reg5], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg6], 0x8483EE0C                                   \n\t"
        "move   %[s1],        $0                                           \n\t"
        "multu  %[temp_reg6], %[temp_reg5]                                 \n\t"
        "sub    %[temp_reg1], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg2], 0xf746ea                                     \n\t"
        "sra    %[temp_reg5], %[temp_reg5], 31                             \n\t"
        "mult   $ac1,         %[temp_reg2], %[temp_reg1]                   \n\t"
        "movn   %[s1],        %[temp_reg6], %[temp_reg5]                   \n\t"
        "mfhi   %[temp_reg5]                                               \n\t"
        "lw     %[temp_reg3], 10*4(%[win])                                 \n\t"
        "lw     %[temp_reg4], 4*10*4(%[buf])                               \n\t"
        "extr.w %[s3],        $ac1,         23                             \n\t"
        "lw     %[temp_reg1], 4*7*4(%[buf])                                \n\t"
        "lw     %[temp_reg2], 7*4(%[win])                                  \n\t"
        "lw     %[temp_reg6], 30*4(%[win])                                 \n\t"
        "subu   %[s1],        %[temp_reg5], %[s1]                          \n\t"
        "sub    %[t1],        %[s0],        %[s1]                          \n\t"
        "add    %[t0],        %[s0],        %[s1]                          \n\t"
        "mult   $ac2,         %[t1],        %[temp_reg3]                   \n\t"
        "mult   $ac3,         %[t1],        %[temp_reg2]                   \n\t"
        "mult   %[t0],        %[temp_reg6]                                 \n\t"
        "lw     %[temp_reg5], 27*4(%[win])                                 \n\t"
        "mult   $ac1,         %[t0],        %[temp_reg5]                   \n\t"
        "mfhi   %[temp_reg3], $ac2                                         \n\t"
        "mfhi   %[temp_reg2], $ac3                                         \n\t"
        "mfhi   %[temp_reg6]                                               \n\t"
        "add    %[t0],        %[s2],        %[s3]                          \n\t"
        "sub    %[t1],        %[s2],        %[s3]                          \n\t"
        "add    %[temp_reg3], %[temp_reg3], %[temp_reg4]                   \n\t"
        "lw     %[temp_reg4], 16*4(%[win])                                 \n\t"
        "mfhi   %[temp_reg5], $ac1                                         \n\t"
        "sw     %[temp_reg3], 32*10*4(%[out])                              \n\t"
        "add    %[temp_reg1], %[temp_reg1], %[temp_reg2]                   \n\t"
        "lw     %[temp_reg3], 4*16*4(%[buf])                               \n\t"
        "sw     %[temp_reg6], 4*10*4(%[buf])                               \n\t"
        "sw     %[temp_reg1], 7*32*4(%[out])                               \n\t"
        "mult   $ac2,         %[t1],        %[temp_reg4]                   \n\t"
        "sw     %[temp_reg5], 4*7*4(%[buf])                                \n\t"
        "lw     %[temp_reg6], 1*4(%[win])                                  \n\t"
        "lw     %[temp_reg5], 4*1*4(%[buf])                                \n\t"
        "lw     %[temp_reg1], 36*4(%[win])                                 \n\t"
        "mult   $ac3,         %[t1],        %[temp_reg6]                   \n\t"
        "lw     %[temp_reg2], 21*4(%[win])                                 \n\t"
        "mfhi   %[temp_reg4], $ac2                                         \n\t"
        "mult   %[t0],        %[temp_reg1]                                 \n\t"
        "mult   $ac1,         %[t0],%[temp_reg2]                           \n\t"
        "lw     %[t0],        8*4(%[tmp])                                  \n\t"
        "mfhi   %[temp_reg6], $ac3                                         \n\t"
        "lw     %[t1],        10*4(%[tmp])                                 \n\t"
        "lw     %[t3],        11*4(%[tmp])                                 \n\t"
        "mfhi   %[temp_reg1]                                               \n\t"
        "add    %[temp_reg3], %[temp_reg3], %[temp_reg4]                   \n\t"
        "lw     %[t2],        9*4(%[tmp])                                  \n\t"
        "mfhi   %[temp_reg2], $ac1                                         \n\t"
        "add    %[temp_reg5], %[temp_reg5], %[temp_reg6]                   \n\t"
        "sw     %[temp_reg3], 16*32*4(%[out])                              \n\t"
        "sw     %[temp_reg5], 1*32*4(%[out])                               \n\t"
        "sw     %[temp_reg1], 4*16*4(%[buf])                               \n\t"
        "addu   %[temp_reg3], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg4], 0x8D3B7CD6                                   \n\t"
        "sw     %[temp_reg2], 4*1*4(%[buf])                                \n\t"
        "multu  %[temp_reg4],%[temp_reg3]                                  \n\t"
        "sra    %[temp_reg3], %[temp_reg3], 31                             \n\t"
        "move   %[s1],        $0                                           \n\t"
        "movn   %[s1],        %[temp_reg4], %[temp_reg3]                   \n\t"
        "addu   %[s0],        %[t1],        %[t0]                          \n\t"
        "mfhi   %[temp_reg3]                                               \n\t"
        "sub    %[s2],        %[t1],        %[t0]                          \n\t"
        "sub    %[temp_reg5], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg6], 0x976fd9                                     \n\t"
        "lw     %[temp_reg2], 11*4(%[win])                                 \n\t"
        "lw     %[temp_reg1], 4*11*4(%[buf])                               \n\t"
        "mult   $ac1,         %[temp_reg6], %[temp_reg5]                   \n\t"
        "subu   %[s1],        %[temp_reg3], %[s1]                          \n\t"
        "lw     %[temp_reg5], 31*4(%[win])                                 \n\t"
        "sub    %[t1],        %[s0],        %[s1]                          \n\t"
        "add    %[t0],        %[s0],        %[s1]                          \n\t"
        "mult   $ac2,         %[t1],        %[temp_reg2]                   \n\t"
        "mult   %[t0],        %[temp_reg5]                                 \n\t"
        "lw     %[temp_reg4], 6*4(%[win])                                  \n\t"
        "extr.w %[s3],        $ac1,         23                             \n\t"
        "lw     %[temp_reg3], 4*6*4(%[buf])                                \n\t"
        "mfhi   %[temp_reg2], $ac2                                         \n\t"
        "lw     %[temp_reg6], 26*4(%[win])                                 \n\t"
        "mfhi   %[temp_reg5]                                               \n\t"
        "mult   $ac3,         %[t1],        %[temp_reg4]                   \n\t"
        "mult   $ac1,         %[t0],        %[temp_reg6]                   \n\t"
        "add    %[t0],        %[s2],        %[s3]                          \n\t"
        "sub    %[t1],        %[s2],        %[s3]                          \n\t"
        "add    %[temp_reg2], %[temp_reg2], %[temp_reg1]                   \n\t"
        "mfhi   %[temp_reg4], $ac3                                         \n\t"
        "mfhi   %[temp_reg6], $ac1                                         \n\t"
        "sw     %[temp_reg5], 4*11*4(%[buf])                               \n\t"
        "sw     %[temp_reg2], 32*11*4(%[out])                              \n\t"
        "lw     %[temp_reg1], 4*15*4(%[buf])                               \n\t"
        "add    %[temp_reg3], %[temp_reg3], %[temp_reg4]                   \n\t"
        "lw     %[temp_reg2], 15*4(%[win])                                 \n\t"
        "sw     %[temp_reg3], 6*32*4(%[out])                               \n\t"
        "sw     %[temp_reg6], 4*6*4(%[buf])                                \n\t"
        "mult   %[t1],        %[temp_reg2]                                 \n\t"
        "lw     %[temp_reg3], 2*4(%[win])                                  \n\t"
        "lw     %[temp_reg4], 4*2*4(%[buf])                                \n\t"
        "lw     %[temp_reg5], 35*4(%[win])                                 \n\t"
        "mult   $ac1,         %[t1],        %[temp_reg3]                   \n\t"
        "mfhi   %[temp_reg2]                                               \n\t"
        "lw     %[temp_reg6], 22*4(%[win])                                 \n\t"
        "mult   $ac2,         %[t0],        %[temp_reg5]                   \n\t"
        "lw     %[t1],        14*4(%[tmp])                                 \n\t"
        "mult   $ac3,         %[t0],        %[temp_reg6]                   \n\t"
        "lw     %[t0],        12*4(%[tmp])                                 \n\t"
        "mfhi   %[temp_reg3], $ac1                                         \n\t"
        "add    %[temp_reg1], %[temp_reg1], %[temp_reg2]                   \n\t"
        "mfhi   %[temp_reg5], $ac2                                         \n\t"
        "sw     %[temp_reg1], 15*32*4(%[out])                              \n\t"
        "mfhi   %[temp_reg6], $ac3                                         \n\t"
        "lw     %[t2],        13*4(%[tmp])                                 \n\t"
        "lw     %[t3],        15*4(%[tmp])                                 \n\t"
        "add    %[temp_reg4], %[temp_reg4], %[temp_reg3]                   \n\t"
        "sw     %[temp_reg5], 4*15*4(%[buf])                               \n\t"
        "addu   %[temp_reg1], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg2], 0x9C42577C                                   \n\t"
        "move   %[s1],        $0                                           \n\t"
        "multu  %[temp_reg2], %[temp_reg1]                                 \n\t"
        "sw     %[temp_reg4], 2*32*4(%[out])                               \n\t"
        "sra    %[temp_reg1], %[temp_reg1], 31                             \n\t"
        "movn   %[s1],        %[temp_reg2], %[temp_reg1]                   \n\t"
        "sub    %[temp_reg3], %[t3],        %[t2]                          \n\t"
        "li     %[temp_reg4], 0x6f94a2                                     \n\t"
        "mfhi   %[temp_reg1]                                               \n\t"
        "addu   %[s0],        %[t1],        %[t0]                          \n\t"
        "sw     %[temp_reg6], 4*2*4(%[buf])                                \n\t"
        "mult   $ac1,         %[temp_reg4], %[temp_reg3]                   \n\t"
        "sub    %[s2],        %[t1],        %[t0]                          \n\t"
        "lw     %[temp_reg5], 12*4(%[win])                                 \n\t"
        "lw     %[temp_reg6], 4*12*4(%[buf])                               \n\t"
        "subu   %[s1],        %[temp_reg1], %[s1]                          \n\t"
        "sub    %[t1],        %[s0],        %[s1]                          \n\t"
        "lw     %[temp_reg3], 32*4(%[win])                                 \n\t"
        "mult   $ac2,         %[t1],        %[temp_reg5]                   \n\t"
        "add    %[t0],        %[s0],        %[s1]                          \n\t"
        "extr.w %[s3],        $ac1,         23                             \n\t"
        "lw     %[temp_reg2], 5*4(%[win])                                  \n\t"
        "mult   %[t0],        %[temp_reg3]                                 \n\t"
        "mfhi   %[temp_reg5], $ac2                                         \n\t"
        "lw     %[temp_reg4], 25*4(%[win])                                 \n\t"
        "lw     %[temp_reg1], 4*5*4(%[buf])                                \n\t"
        "mult   $ac3,         %[t1],        %[temp_reg2]                   \n\t"
        "mult   $ac1,         %[t0],        %[temp_reg4]                   \n\t"
        "mfhi   %[temp_reg3]                                               \n\t"
        "add    %[t0],        %[s2],        %[s3]                          \n\t"
        "add    %[temp_reg5], %[temp_reg5], %[temp_reg6]                   \n\t"
        "mfhi   %[temp_reg2], $ac3                                         \n\t"
        "mfhi   %[temp_reg4], $ac1                                         \n\t"
        "sub    %[t1],        %[s2],        %[s3]                          \n\t"
        "sw     %[temp_reg5], 32*12*4(%[out])                              \n\t"
        "sw     %[temp_reg3], 4*12*4(%[buf])                               \n\t"
        "lw     %[temp_reg6], 14*4(%[win])                                 \n\t"
        "lw     %[temp_reg5], 4*14*4(%[buf])                               \n\t"
        "add    %[temp_reg1], %[temp_reg1], %[temp_reg2]                   \n\t"
        "sw     %[temp_reg4], 4*5*4(%[buf])                                \n\t"
        "sw     %[temp_reg1], 5*32*4(%[out])                               \n\t"
        "mult   %[t1],        %[temp_reg6]                                 \n\t"
        "lw     %[temp_reg4], 34*4(%[win])                                 \n\t"
        "lw     %[temp_reg2], 3*4(%[win])                                  \n\t"
        "lw     %[temp_reg1], 4*3*4(%[buf])                                \n\t"
        "mult   $ac2,         %[t0],        %[temp_reg4]                   \n\t"
        "mfhi   %[temp_reg6]                                               \n\t"
        "mult   $ac1,         %[t1],        %[temp_reg2]                   \n\t"
        "lw     %[temp_reg3], 23*4(%[win])                                 \n\t"
        "lw     %[s0],        16*4(%[tmp])                                 \n\t"
        "mfhi   %[temp_reg4], $ac2                                         \n\t"
        "lw     %[t1],        17*4(%[tmp])                                 \n\t"
        "mult   $ac3,         %[t0],        %[temp_reg3]                   \n\t"
        "move   %[s1],        $0                                           \n\t"
        "add    %[temp_reg5], %[temp_reg5], %[temp_reg6]                   \n\t"
        "mfhi   %[temp_reg2], $ac1                                         \n\t"
        "sw     %[temp_reg5], 14*32*4(%[out])                              \n\t"
        "sw     %[temp_reg4], 4*14*4(%[buf])                               \n\t"
        "mfhi   %[temp_reg3], $ac3                                         \n\t"
        "li     %[temp_reg5], 0xB504F334                                   \n\t"
        "add    %[temp_reg1], %[temp_reg1], %[temp_reg2]                   \n\t"
        "multu  %[temp_reg5], %[t1]                                        \n\t"
        "lw     %[temp_reg2], 4*13*4(%[buf])                               \n\t"
        "sw     %[temp_reg1], 3*32*4(%[out])                               \n\t"
        "sra    %[t1],        %[t1],        31                             \n\t"
        "mfhi   %[temp_reg6]                                               \n\t"
        "movn   %[s1],        %[temp_reg5], %[t1]                          \n\t"
        "sw     %[temp_reg3], 4*3*4(%[buf])                                \n\t"
        "lw     %[temp_reg1], 13*4(%[win])                                 \n\t"
        "lw     %[temp_reg4], 4*4*4(%[buf])                                \n\t"
        "lw     %[temp_reg3], 4*4(%[win])                                  \n\t"
        "lw     %[temp_reg5], 33*4(%[win])                                 \n\t"
        "subu   %[s1],        %[temp_reg6], %[s1]                          \n\t"
        "lw     %[temp_reg6], 24*4(%[win])                                 \n\t"
        "sub    %[t1],        %[s0],        %[s1]                          \n\t"
        "add    %[t0],        %[s0],        %[s1]                          \n\t"
        "mult   $ac1,         %[t1],        %[temp_reg1]                   \n\t"
        "mult   $ac2,         %[t1],        %[temp_reg3]                   \n\t"
        "mult   $ac3,         %[t0],        %[temp_reg5]                   \n\t"
        "mult   %[t0],        %[temp_reg6]                                 \n\t"
        "mfhi   %[temp_reg1], $ac1                                         \n\t"
        "mfhi   %[temp_reg3], $ac2                                         \n\t"
        "mfhi   %[temp_reg5], $ac3                                         \n\t"
        "mfhi   %[temp_reg6]                                               \n\t"
        "add    %[temp_reg2], %[temp_reg2], %[temp_reg1]                   \n\t"
        "add    %[temp_reg4], %[temp_reg4], %[temp_reg3]                   \n\t"
        "sw     %[temp_reg2], 13*32*4(%[out])                              \n\t"
        "sw     %[temp_reg4], 4*32*4(%[out])                               \n\t"
        "sw     %[temp_reg5], 4*13*4(%[buf])                               \n\t"
        "sw     %[temp_reg6], 4*4*4(%[buf])                                \n\t"

        : [t0] "=&r" (t0), [t1] "=&r" (t1), [t2] "=&r" (t2), [t3] "=&r" (t3),
          [s0] "=&r" (s0), [s2] "=&r" (s2), [temp_reg1] "=&r" (temp_reg1),
          [temp_reg2] "=&r" (temp_reg2), [s1] "=&r" (s1), [s3] "=&r" (s3),
          [temp_reg3] "=&r" (temp_reg3), [temp_reg4] "=&r" (temp_reg4),
          [temp_reg5] "=&r" (temp_reg5), [temp_reg6] "=&r" (temp_reg6),
          [out] "+r" (out)
        : [tmp] "r" (tmp), [win] "r" (win), [buf] "r" (buf)
        : "memory", "hi", "lo", "$ac1hi", "$ac1lo", "$ac2hi", "$ac2lo",
          "$ac3hi", "$ac3lo"
    );
}

static void ff_imdct36_blocks_mips_fixed(int *out, int *buf, int *in,
                               int count, int switch_point, int block_type)
{
    int j;
    for (j=0 ; j < count; j++) {
        /* apply window & overlap with previous buffer */

        /* select window */
        int win_idx = (switch_point && j < 2) ? 0 : block_type;
        int *win = ff_mdct_win_fixed[win_idx + (4 & -(j & 1))];

        imdct36_mips_fixed(out, buf, in, win);

        in  += 18;
        buf += ((j&3) != 3 ? 1 : (72-3));
        out++;
    }
}

void ff_mpadsp_init_mipsdspr1(MPADSPContext *s)
{
    s->apply_window_fixed   = ff_mpadsp_apply_window_mips_fixed;
    s->imdct36_blocks_fixed = ff_imdct36_blocks_mips_fixed;
}
