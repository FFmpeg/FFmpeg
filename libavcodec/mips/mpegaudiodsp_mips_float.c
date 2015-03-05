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
 * MPEG Audio decoder optimized for MIPS floating-point architecture
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
 *            libavcodec/dct32.c
 */

#include <string.h>

#include "libavutil/mips/asmdefs.h"
#include "libavcodec/mpegaudiodsp.h"

static void ff_mpadsp_apply_window_mips_float(float *synth_buf, float *window,
                               int *dither_state, float *samples, int incr)
{
    register const float *w, *w2, *p;
    int j;
    float *samples2;
    float sum, sum2;
    /* temporary variables */
    int incr1 = incr << 2;
    int t_sample;
    float in1, in2, in3, in4, in5, in6, in7, in8;
    float *p2;

    /* copy to avoid wrap */
    memcpy(synth_buf + 512, synth_buf, 32 * sizeof(*synth_buf));

    /**
    * instructions are scheduled to minimize pipeline stall.
    * use of round_sample function from the original code is
    * changed with appropriate assembly instructions.
    */

    __asm__ volatile (
        "lwc1    %[sum],      0(%[dither_state])                            \t\n"
        "sll     %[t_sample], %[incr1],     5                               \t\n"
        "sub     %[t_sample], %[t_sample],  %[incr1]                        \n\t"
        "li      %[j],        4                                             \t\n"
        "lwc1    %[in1],      0(%[window])                                  \t\n"
        "lwc1    %[in2],      16*4(%[synth_buf])                            \t\n"
        "sw      $zero,       0(%[dither_state])                            \t\n"
        "lwc1    %[in3],      64*4(%[window])                               \t\n"
        "lwc1    %[in4],      80*4(%[synth_buf])                            \t\n"
        PTR_ADDU "%[samples2],%[samples],   %[t_sample]                     \t\n"
        "madd.s  %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in5],      128*4(%[window])                              \t\n"
        "lwc1    %[in6],      144*4(%[synth_buf])                           \t\n"
        "lwc1    %[in7],      192*4(%[window])                              \t\n"
        "madd.s  %[sum],      %[sum],       %[in3], %[in4]                  \t\n"
        "lwc1    %[in8],      208*4(%[synth_buf])                           \t\n"
        "lwc1    %[in1],      256*4(%[window])                              \t\n"
        "lwc1    %[in2],      272*4(%[synth_buf])                           \t\n"
        "madd.s  %[sum],      %[sum],       %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      320*4(%[window])                              \t\n"
        "lwc1    %[in4],      336*4(%[synth_buf])                           \t\n"
        "lwc1    %[in5],      384*4(%[window])                              \t\n"
        "madd.s  %[sum],      %[sum],       %[in7], %[in8]                  \t\n"
        "lwc1    %[in6],      400*4(%[synth_buf])                           \t\n"
        "lwc1    %[in7],      448*4(%[window])                              \t\n"
        "lwc1    %[in8],      464*4(%[synth_buf])                           \t\n"
        "madd.s  %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in1],      32*4(%[window])                               \t\n"
        "lwc1    %[in2],      48*4(%[synth_buf])                            \t\n"
        "madd.s  %[sum],      %[sum],       %[in3], %[in4]                  \t\n"
        "lwc1    %[in3],      96*4(%[window])                               \t\n"
        "lwc1    %[in4],      112*4(%[synth_buf])                           \t\n"
        "madd.s  %[sum],      %[sum],       %[in5], %[in6]                  \t\n"
        "lwc1    %[in5],      160*4(%[window])                              \t\n"
        "lwc1    %[in6],      176*4(%[synth_buf])                           \t\n"
        "madd.s  %[sum],      %[sum],       %[in7], %[in8]                  \t\n"
        "lwc1    %[in7],      224*4(%[window])                              \t\n"
        "lwc1    %[in8],      240*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in1],      288*4(%[window])                              \t\n"
        "lwc1    %[in2],      304*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in3], %[in4]                  \t\n"
        "lwc1    %[in3],      352*4(%[window])                              \t\n"
        "lwc1    %[in4],      368*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in5], %[in6]                  \t\n"
        "lwc1    %[in5],      416*4(%[window])                              \t\n"
        "lwc1    %[in6],      432*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in7], %[in8]                  \t\n"
        "lwc1    %[in7],      480*4(%[window])                              \t\n"
        "lwc1    %[in8],      496*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        PTR_ADDU "%[w],       %[window],    4                               \t\n"
        "nmsub.s %[sum],      %[sum],       %[in3], %[in4]                  \t\n"
        PTR_ADDU "%[w2],      %[window],    124                             \t\n"
        PTR_ADDIU "%[p],      %[synth_buf], 68                              \t\n"
        PTR_ADDIU "%[p2],     %[synth_buf], 188                             \t\n"
        "nmsub.s %[sum],      %[sum],       %[in5], %[in6]                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in7], %[in8]                  \t\n"
        "swc1    %[sum],      0(%[samples])                                 \t\n"
        PTR_ADDU "%[samples], %[samples],   %[incr1]                        \t\n"

        /* calculate two samples at the same time to avoid one memory
           access per two sample */

        "ff_mpadsp_apply_window_loop%=:                                     \t\n"
        "lwc1    %[in1],      0(%[w])                                       \t\n"
        "lwc1    %[in2],      0(%[p])                                       \t\n"
        "lwc1    %[in3],      0(%[w2])                                      \t\n"
        "lwc1    %[in4],      64*4(%[w])                                    \t\n"
        "lwc1    %[in5],      64*4(%[p])                                    \t\n"
        "lwc1    %[in6],      64*4(%[w2])                                   \t\n"
        "mul.s   %[sum],      %[in1],       %[in2]                          \t\n"
        "mul.s   %[sum2],     %[in2],       %[in3]                          \t\n"
        "lwc1    %[in1],      128*4(%[w])                                   \t\n"
        "lwc1    %[in2],      128*4(%[p])                                   \t\n"
        "madd.s  %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "nmadd.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      128*4(%[w2])                                  \t\n"
        "lwc1    %[in4],      192*4(%[w])                                   \t\n"
        "madd.s  %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in5],      192*4(%[p])                                   \t\n"
        "lwc1    %[in6],      192*4(%[w2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        "lwc1    %[in1],      256*4(%[w])                                   \t\n"
        "lwc1    %[in2],      256*4(%[p])                                   \t\n"
        "madd.s  %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      256*4(%[w2])                                  \t\n"
        "lwc1    %[in4],      320*4(%[w])                                   \t\n"
        "madd.s  %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in5],      320*4(%[p])                                   \t\n"
        "lwc1    %[in6],      320*4(%[w2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        "lwc1    %[in1],      384*4(%[w])                                   \t\n"
        "lwc1    %[in2],      384*4(%[p])                                   \t\n"
        "madd.s  %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      384*4(%[w2])                                  \t\n"
        "lwc1    %[in4],      448*4(%[w])                                   \t\n"
        "madd.s  %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in5],      448*4(%[p])                                   \t\n"
        "lwc1    %[in6],      448*4(%[w2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        "madd.s  %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "lwc1    %[in1],      32*4(%[w])                                    \t\n"
        "lwc1    %[in2],      0(%[p2])                                      \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      32*4(%[w2])                                   \t\n"
        "lwc1    %[in4],      96*4(%[w])                                    \t\n"
        "lwc1    %[in5],      64*4(%[p2])                                   \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in6],      96*4(%[w2])                                   \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        "lwc1    %[in1],      160*4(%[w])                                   \t\n"
        "nmsub.s %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "lwc1    %[in2],      128*4(%[p2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      160*4(%[w2])                                  \t\n"
        "lwc1    %[in4],      224*4(%[w])                                   \t\n"
        "lwc1    %[in5],      192*4(%[p2])                                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in6],      224*4(%[w2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        "lwc1    %[in1],      288*4(%[w])                                   \t\n"
        "nmsub.s %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "lwc1    %[in2],      256*4(%[p2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      288*4(%[w2])                                  \t\n"
        "lwc1    %[in4],      352*4(%[w])                                   \t\n"
        "lwc1    %[in5],      320*4(%[p2])                                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in6],      352*4(%[w2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        "lwc1    %[in1],      416*4(%[w])                                   \t\n"
        "nmsub.s %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        "lwc1    %[in2],      384*4(%[p2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "lwc1    %[in3],      416*4(%[w2])                                  \t\n"
        "lwc1    %[in4],      480*4(%[w])                                   \t\n"
        "lwc1    %[in5],      448*4(%[p2])                                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in6],      480*4(%[w2])                                  \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in2], %[in3]                  \t\n"
        PTR_ADDIU "%[w],      %[w],         4                               \t\n"
        "nmsub.s %[sum],      %[sum],       %[in4], %[in5]                  \t\n"
        PTR_ADDIU "%[w2],     %[w2],        -4                              \t\n"
        "nmsub.s %[sum2],     %[sum2],      %[in5], %[in6]                  \t\n"
        "addu    %[j],        %[j],         4                               \t\n"
        PTR_ADDIU "%[p],      4                                             \t\n"
        "swc1    %[sum],      0(%[samples])                                 \t\n"
        PTR_ADDIU "%[p2],     -4                                            \t\n"
        "swc1    %[sum2],     0(%[samples2])                                \t\n"
        PTR_ADDU "%[samples], %[samples],   %[incr1]                        \t\n"
        PTR_SUBU "%[samples2],%[samples2],  %[incr1]                        \t\n"
        "bne     %[j],        64,           ff_mpadsp_apply_window_loop%=   \t\n"

        "lwc1    %[in1],      48*4(%[window])                               \t\n"
        "lwc1    %[in2],      32*4(%[synth_buf])                            \t\n"
        "lwc1    %[in3],      112*4(%[window])                              \t\n"
        "lwc1    %[in4],      96*4(%[synth_buf])                            \t\n"
        "lwc1    %[in5],      176*4(%[window])                              \t\n"
        "lwc1    %[in6],      160*4(%[synth_buf])                           \t\n"
        "mul.s   %[sum],      %[in1],       %[in2]                          \t\n"
        "lwc1    %[in7],      240*4(%[window])                              \t\n"
        "lwc1    %[in8],      224*4(%[synth_buf])                           \t\n"
        "lwc1    %[in1],      304*4(%[window])                              \t\n"
        "nmadd.s %[sum],      %[sum],       %[in3], %[in4]                  \t\n"
        "lwc1    %[in2],      288*4(%[synth_buf])                           \t\n"
        "lwc1    %[in3],      368*4(%[window])                              \t\n"
        "lwc1    %[in4],      352*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in5], %[in6]                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in7], %[in8]                  \t\n"
        "lwc1    %[in5],      432*4(%[window])                              \t\n"
        "lwc1    %[in6],      416*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in1], %[in2]                  \t\n"
        "lwc1    %[in7],      496*4(%[window])                              \t\n"
        "lwc1    %[in8],      480*4(%[synth_buf])                           \t\n"
        "nmsub.s %[sum],      %[sum],       %[in3], %[in4]                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in5], %[in6]                  \t\n"
        "nmsub.s %[sum],      %[sum],       %[in7], %[in8]                  \t\n"
        "swc1    %[sum],      0(%[samples])                                 \t\n"

        : [sum] "=&f" (sum), [sum2] "=&f" (sum2),
          [w2] "=&r" (w2),   [w] "=&r" (w),
          [p] "=&r" (p), [p2] "=&r" (p2), [j] "=&r" (j),
          [samples] "+r" (samples), [samples2] "=&r" (samples2),
          [in1] "=&f" (in1), [in2] "=&f" (in2),
          [in3] "=&f" (in3), [in4] "=&f" (in4),
          [in5] "=&f" (in5), [in6] "=&f" (in6),
          [in7] "=&f" (in7), [in8] "=&f" (in8),
          [t_sample] "=&r" (t_sample)
        : [synth_buf] "r" (synth_buf), [window] "r" (window),
          [dither_state] "r" (dither_state), [incr1] "r" (incr1)
        : "memory"
    );
}

static void ff_dct32_mips_float(float *out, const float *tab)
{
    float val0 , val1 , val2 , val3 , val4 , val5 , val6 , val7,
          val8 , val9 , val10, val11, val12, val13, val14, val15,
          val16, val17, val18, val19, val20, val21, val22, val23,
          val24, val25, val26, val27, val28, val29, val30, val31;
    float fTmp1, fTmp2, fTmp3, fTmp4, fTmp5, fTmp6, fTmp7, fTmp8,
          fTmp9, fTmp10, fTmp11;

    /**
    * instructions are scheduled to minimize pipeline stall.
    */
    __asm__ volatile (
        "lwc1       %[fTmp1],       0*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       31*4(%[tab])                            \n\t"
        "lwc1       %[fTmp3],       15*4(%[tab])                            \n\t"
        "lwc1       %[fTmp4],       16*4(%[tab])                            \n\t"
        "li.s       %[fTmp7],       0.50241928618815570551                  \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp10],      0.50060299823519630134                  \n\t"
        "li.s       %[fTmp11],      10.19000812354805681150                 \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "add.s      %[val0],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val15],       %[fTmp5],       %[fTmp6]                \n\t"
        "lwc1       %[fTmp1],       7*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       24*4(%[tab])                            \n\t"
        "madd.s     %[val16],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val31],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "mul.s      %[val15],       %[val15],       %[fTmp7]                \n\t"
        "lwc1       %[fTmp3],       8*4(%[tab])                             \n\t"
        "lwc1       %[fTmp4],       23*4(%[tab])                            \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "mul.s      %[val31],       %[val31],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp7],       5.10114861868916385802                  \n\t"
        "li.s       %[fTmp10],      0.67480834145500574602                  \n\t"
        "li.s       %[fTmp11],      0.74453627100229844977                  \n\t"
        "add.s      %[val7],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val8],        %[fTmp5],       %[fTmp6]                \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "li.s       %[fTmp1],       0.50979557910415916894                  \n\t"
        "sub.s      %[fTmp2],       %[val0],        %[val7]                 \n\t"
        "mul.s      %[val8],        %[val8],        %[fTmp7]                \n\t"
        "madd.s     %[val23],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val24],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "add.s      %[val0],        %[val0],        %[val7]                 \n\t"
        "mul.s      %[val7],        %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp2],       %[val15],       %[val8]                 \n\t"
        "add.s      %[val8],        %[val15],       %[val8]                 \n\t"
        "mul.s      %[val24],       %[val24],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp3],       %[val16],       %[val23]                \n\t"
        "add.s      %[val16],       %[val16],       %[val23]                \n\t"
        "mul.s      %[val15],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp4],       %[val31],       %[val24]                \n\t"
        "mul.s      %[val23],       %[fTmp1],       %[fTmp3]                \n\t"
        "add.s      %[val24],       %[val31],       %[val24]                \n\t"
        "mul.s      %[val31],       %[fTmp1],       %[fTmp4]                \n\t"

        : [fTmp1]  "=&f" (fTmp1),  [fTmp2] "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3),
          [fTmp4]  "=&f" (fTmp4),  [fTmp5] "=&f" (fTmp5), [fTmp6] "=&f" (fTmp6),
          [fTmp7]  "=&f" (fTmp7),  [fTmp8] "=&f" (fTmp8), [fTmp9] "=&f" (fTmp9),
          [fTmp10] "=&f" (fTmp10), [fTmp11] "=&f" (fTmp11),
          [val0]  "=f" (val0),  [val7]  "=f" (val7),
          [val8]  "=f" (val8),  [val15] "=f" (val15),
          [val16] "=f" (val16), [val23] "=f" (val23),
          [val24] "=f" (val24), [val31] "=f" (val31)
        : [tab] "r" (tab)
        : "memory"
    );

    __asm__ volatile (
        "lwc1       %[fTmp1],       3*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       28*4(%[tab])                            \n\t"
        "lwc1       %[fTmp3],       12*4(%[tab])                            \n\t"
        "lwc1       %[fTmp4],       19*4(%[tab])                            \n\t"
        "li.s       %[fTmp7],       0.64682178335999012954                  \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp10],      0.53104259108978417447                  \n\t"
        "li.s       %[fTmp11],      1.48416461631416627724                  \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "add.s      %[val3],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val12],       %[fTmp5],       %[fTmp6]                \n\t"
        "lwc1       %[fTmp1],       4*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       27*4(%[tab])                            \n\t"
        "madd.s     %[val19],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val28],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "mul.s      %[val12],       %[val12],       %[fTmp7]                \n\t"
        "lwc1       %[fTmp3],       11*4(%[tab])                            \n\t"
        "lwc1       %[fTmp4],       20*4(%[tab])                            \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "mul.s      %[val28],       %[val28],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "li.s       %[fTmp7],       0.78815462345125022473                  \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp10],      0.55310389603444452782                  \n\t"
        "li.s       %[fTmp11],      1.16943993343288495515                  \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "add.s      %[val4],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val11],       %[fTmp5],       %[fTmp6]                \n\t"
        "li.s       %[fTmp1],       2.56291544774150617881                  \n\t"
        "madd.s     %[val20],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val27],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "mul.s      %[val11],       %[val11],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp2],       %[val3],        %[val4]                 \n\t"
        "add.s      %[val3],        %[val3],        %[val4]                 \n\t"
        "sub.s      %[fTmp4],       %[val19],       %[val20]                \n\t"
        "mul.s      %[val27],       %[val27],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp3],       %[val12],       %[val11]                \n\t"
        "mul.s      %[val4],        %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val11],       %[val12],       %[val11]                \n\t"
        "add.s      %[val19],       %[val19],       %[val20]                \n\t"
        "mul.s      %[val20],       %[fTmp1],       %[fTmp4]                \n\t"
        "mul.s      %[val12],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val28],       %[val27]                \n\t"
        "add.s      %[val27],       %[val28],       %[val27]                \n\t"
        "mul.s      %[val28],       %[fTmp1],       %[fTmp2]                \n\t"

        : [fTmp1]  "=&f" (fTmp1),  [fTmp2]  "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3),
          [fTmp4]  "=&f" (fTmp4),  [fTmp5]  "=&f" (fTmp5), [fTmp6] "=&f" (fTmp6),
          [fTmp7]  "=&f" (fTmp7),  [fTmp8]  "=&f" (fTmp8), [fTmp9] "=&f" (fTmp9),
          [fTmp10] "=&f" (fTmp10), [fTmp11] "=&f" (fTmp11),
          [val3]  "=f" (val3),  [val4]  "=f" (val4),
          [val11] "=f" (val11), [val12] "=f" (val12),
          [val19] "=f" (val19), [val20] "=f" (val20),
          [val27] "=f" (val27), [val28] "=f" (val28)
        : [tab] "r" (tab)
        : "memory"
    );

    __asm__ volatile (
        "li.s       %[fTmp1],       0.54119610014619698439                  \n\t"
        "sub.s      %[fTmp2],       %[val0],        %[val3]                 \n\t"
        "add.s      %[val0],        %[val0],        %[val3]                 \n\t"
        "sub.s      %[fTmp3],       %[val7],        %[val4]                 \n\t"
        "add.s      %[val4],        %[val7],        %[val4]                 \n\t"
        "sub.s      %[fTmp4],       %[val8],        %[val11]                \n\t"
        "mul.s      %[val3],        %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val8],        %[val8],        %[val11]                \n\t"
        "mul.s      %[val7],        %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val15],       %[val12]                \n\t"
        "mul.s      %[val11],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val12],       %[val15],       %[val12]                \n\t"
        "mul.s      %[val15],       %[fTmp1],       %[fTmp2]                \n\t"

        : [val0]  "+f" (val0),   [val3] "+f" (val3),
          [val4]  "+f" (val4),   [val7] "+f" (val7),
          [val8]  "+f" (val8),   [val11] "+f" (val11),
          [val12] "+f" (val12),  [val15] "+f" (val15),
          [fTmp1] "=f"  (fTmp1), [fTmp2] "=&f" (fTmp2),
          [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4)
        :
    );

    __asm__ volatile (
        "sub.s      %[fTmp2],       %[val16],       %[val19]                \n\t"
        "add.s      %[val16],       %[val16],       %[val19]                \n\t"
        "sub.s      %[fTmp3],       %[val23],       %[val20]                \n\t"
        "add.s      %[val20],       %[val23],       %[val20]                \n\t"
        "sub.s      %[fTmp4],       %[val24],       %[val27]                \n\t"
        "mul.s      %[val19],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val24],       %[val24],       %[val27]                \n\t"
        "mul.s      %[val23],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val31],       %[val28]                \n\t"
        "mul.s      %[val27],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val28],       %[val31],       %[val28]                \n\t"
        "mul.s      %[val31],       %[fTmp1],       %[fTmp2]                \n\t"

        : [fTmp2] "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val16] "+f" (val16), [val19] "+f" (val19), [val20] "+f" (val20),
          [val23] "+f" (val23), [val24] "+f" (val24), [val27] "+f" (val27),
          [val28] "+f" (val28), [val31] "+f" (val31)
        : [fTmp1] "f" (fTmp1)
    );

    __asm__ volatile (
        "lwc1       %[fTmp1],       1*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       30*4(%[tab])                            \n\t"
        "lwc1       %[fTmp3],       14*4(%[tab])                            \n\t"
        "lwc1       %[fTmp4],       17*4(%[tab])                            \n\t"
        "li.s       %[fTmp7],       0.52249861493968888062                  \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp10],      0.50547095989754365998                  \n\t"
        "li.s       %[fTmp11],      3.40760841846871878570                  \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "add.s      %[val1],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val14],       %[fTmp5],       %[fTmp6]                \n\t"
        "lwc1       %[fTmp1],       6*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       25*4(%[tab])                            \n\t"
        "madd.s     %[val17],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val30],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "mul.s      %[val14],       %[val14],       %[fTmp7]                \n\t"
        "lwc1       %[fTmp3],       9*4(%[tab])                             \n\t"
        "lwc1       %[fTmp4],       22*4(%[tab])                            \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "mul.s      %[val30],       %[val30],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp7],       1.72244709823833392782                  \n\t"
        "li.s       %[fTmp10],      0.62250412303566481615                  \n\t"
        "li.s       %[fTmp11],      0.83934964541552703873                  \n\t"
        "add.s      %[val6],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val9],        %[fTmp5],       %[fTmp6]                \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "li.s       %[fTmp1],       0.60134488693504528054                  \n\t"
        "sub.s      %[fTmp2],       %[val1],        %[val6]                 \n\t"
        "add.s      %[val1],        %[val1],        %[val6]                 \n\t"
        "mul.s      %[val9],        %[val9],        %[fTmp7]                \n\t"
        "madd.s     %[val22],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val25],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "mul.s      %[val6],        %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp2],       %[val14],       %[val9]                 \n\t"
        "add.s      %[val9],        %[val14],       %[val9]                 \n\t"
        "mul.s      %[val25],       %[val25],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp3],       %[val17],       %[val22]                \n\t"
        "add.s      %[val17],       %[val17],       %[val22]                \n\t"
        "mul.s      %[val14],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp2],       %[val30],       %[val25]                \n\t"
        "mul.s      %[val22],       %[fTmp1],       %[fTmp3]                \n\t"
        "add.s      %[val25],       %[val30],       %[val25]                \n\t"
        "mul.s      %[val30],       %[fTmp1],       %[fTmp2]                \n\t"

        : [fTmp1]  "=&f" (fTmp1),  [fTmp2]  "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3),
          [fTmp4]  "=&f" (fTmp4),  [fTmp5]  "=&f" (fTmp5), [fTmp6] "=&f" (fTmp6),
          [fTmp7]  "=&f" (fTmp7),  [fTmp8]  "=&f" (fTmp8), [fTmp9] "=&f" (fTmp9),
          [fTmp10] "=&f" (fTmp10), [fTmp11] "=&f" (fTmp11),
          [val1]  "=f" (val1),  [val6]  "=f" (val6),
          [val9]  "=f" (val9),  [val14] "=f" (val14),
          [val17] "=f" (val17), [val22] "=f" (val22),
          [val25] "=f" (val25), [val30] "=f" (val30)
        : [tab] "r" (tab)
        : "memory"
    );

    __asm__ volatile (
        "lwc1       %[fTmp1],       2*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       29*4(%[tab])                            \n\t"
        "lwc1       %[fTmp3],       13*4(%[tab])                            \n\t"
        "lwc1       %[fTmp4],       18*4(%[tab])                            \n\t"
        "li.s       %[fTmp7],       0.56694403481635770368                  \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp10],      0.51544730992262454697                  \n\t"
        "li.s       %[fTmp11],      2.05778100995341155085                  \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "add.s      %[val2],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val13],       %[fTmp5],       %[fTmp6]                \n\t"
        "lwc1       %[fTmp1],       5*4(%[tab])                             \n\t"
        "lwc1       %[fTmp2],       26*4(%[tab])                            \n\t"
        "madd.s     %[val18],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val29],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "mul.s      %[val13],       %[val13],       %[fTmp7]                \n\t"
        "lwc1       %[fTmp3],       10*4(%[tab])                            \n\t"
        "lwc1       %[fTmp4],       21*4(%[tab])                            \n\t"
        "mul.s      %[val29],       %[val29],       %[fTmp7]                \n\t"
        "add.s      %[fTmp5],       %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp8],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[fTmp6],       %[fTmp3],       %[fTmp4]                \n\t"
        "sub.s      %[fTmp9],       %[fTmp3],       %[fTmp4]                \n\t"
        "li.s       %[fTmp7],       1.06067768599034747134                  \n\t"
        "li.s       %[fTmp10],      0.58293496820613387367                  \n\t"
        "li.s       %[fTmp11],      0.97256823786196069369                  \n\t"
        "add.s      %[val5],        %[fTmp5],       %[fTmp6]                \n\t"
        "sub.s      %[val10],       %[fTmp5],       %[fTmp6]                \n\t"
        "mul.s      %[fTmp8],       %[fTmp8],       %[fTmp10]               \n\t"
        "li.s       %[fTmp1],       0.89997622313641570463                  \n\t"
        "sub.s      %[fTmp2],       %[val2],        %[val5]                 \n\t"
        "mul.s      %[val10],       %[val10],       %[fTmp7]                \n\t"
        "madd.s     %[val21],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "nmsub.s    %[val26],       %[fTmp8],       %[fTmp9],   %[fTmp11]   \n\t"
        "add.s      %[val2],        %[val2],        %[val5]                 \n\t"
        "mul.s      %[val5],        %[fTmp1],       %[fTmp2]                \n\t"
        "sub.s      %[fTmp3],       %[val13],       %[val10]                \n\t"
        "add.s      %[val10],       %[val13],       %[val10]                \n\t"
        "mul.s      %[val26],       %[val26],       %[fTmp7]                \n\t"
        "sub.s      %[fTmp4],       %[val18],       %[val21]                \n\t"
        "add.s      %[val18],       %[val18],       %[val21]                \n\t"
        "mul.s      %[val13],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val29],       %[val26]                \n\t"
        "add.s      %[val26],       %[val29],       %[val26]                \n\t"
        "mul.s      %[val21],       %[fTmp1],       %[fTmp4]                \n\t"
        "mul.s      %[val29],       %[fTmp1],       %[fTmp2]                \n\t"

        : [fTmp1]  "=&f" (fTmp1),  [fTmp2]  "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3),
          [fTmp4]  "=&f" (fTmp4),  [fTmp5]  "=&f" (fTmp5), [fTmp6] "=&f" (fTmp6),
          [fTmp7]  "=&f" (fTmp7),  [fTmp8]  "=&f" (fTmp8), [fTmp9] "=&f" (fTmp9),
          [fTmp10] "=&f" (fTmp10), [fTmp11] "=&f" (fTmp11),
          [val2]  "=f" (val2),  [val5]  "=f" (val5),
          [val10] "=f" (val10), [val13] "=f" (val13),
          [val18] "=f" (val18), [val21] "=f" (val21),
          [val26] "=f" (val26), [val29] "=f" (val29)
        : [tab] "r" (tab)
        : "memory"
    );

    __asm__ volatile (
        "li.s       %[fTmp1],       1.30656296487637652785                  \n\t"
        "sub.s      %[fTmp2],       %[val1],        %[val2]                 \n\t"
        "add.s      %[val1],        %[val1],        %[val2]                 \n\t"
        "sub.s      %[fTmp3],       %[val6],        %[val5]                 \n\t"
        "add.s      %[val5],        %[val6],        %[val5]                 \n\t"
        "sub.s      %[fTmp4],       %[val9],        %[val10]                \n\t"
        "mul.s      %[val2],        %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val9],        %[val9],        %[val10]                \n\t"
        "mul.s      %[val6],        %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val14],       %[val13]                \n\t"
        "mul.s      %[val10],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val13],       %[val14],       %[val13]                \n\t"
        "mul.s      %[val14],       %[fTmp1],       %[fTmp2]                \n\t"

        : [fTmp1] "=f"  (fTmp1), [fTmp2] "=&f" (fTmp2),
          [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val1]  "+f" (val1),  [val2]  "+f" (val2),
          [val5]  "+f" (val5),  [val6]  "+f" (val6),
          [val9]  "+f" (val9),  [val10] "+f" (val10),
          [val13] "+f" (val13), [val14] "+f" (val14)
        :
    );

    __asm__ volatile (
        "sub.s      %[fTmp2],       %[val17],       %[val18]                \n\t"
        "add.s      %[val17],       %[val17],       %[val18]                \n\t"
        "sub.s      %[fTmp3],       %[val22],       %[val21]                \n\t"
        "add.s      %[val21],       %[val22],       %[val21]                \n\t"
        "sub.s      %[fTmp4],       %[val25],       %[val26]                \n\t"
        "mul.s      %[val18],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val25],       %[val25],       %[val26]                \n\t"
        "mul.s      %[val22],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val30],       %[val29]                \n\t"
        "mul.s      %[val26],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val29],       %[val30],       %[val29]                \n\t"
        "mul.s      %[val30],       %[fTmp1],       %[fTmp2]                \n\t"

        : [fTmp2] "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val17] "+f" (val17), [val18] "+f" (val18), [val21] "+f" (val21),
          [val22] "+f" (val22), [val25] "+f" (val25), [val26] "+f" (val26),
          [val29] "+f" (val29), [val30] "+f" (val30)
        : [fTmp1] "f" (fTmp1)
    );

    __asm__ volatile (
        "li.s       %[fTmp1],       0.70710678118654752439                  \n\t"
        "sub.s      %[fTmp2],       %[val0],        %[val1]                 \n\t"
        "add.s      %[val0],        %[val0],        %[val1]                 \n\t"
        "sub.s      %[fTmp3],       %[val3],        %[val2]                 \n\t"
        "add.s      %[val2],        %[val3],        %[val2]                 \n\t"
        "sub.s      %[fTmp4],       %[val4],        %[val5]                 \n\t"
        "mul.s      %[val1],        %[fTmp1],       %[fTmp2]                \n\t"
        "swc1       %[val0],        0(%[out])                               \n\t"
        "mul.s      %[val3],        %[fTmp3],       %[fTmp1]                \n\t"
        "add.s      %[val4],        %[val4],        %[val5]                 \n\t"
        "mul.s      %[val5],        %[fTmp1],       %[fTmp4]                \n\t"
        "swc1       %[val1],        16*4(%[out])                            \n\t"
        "sub.s      %[fTmp2],       %[val7],        %[val6]                 \n\t"
        "add.s      %[val2],        %[val2],        %[val3]                 \n\t"
        "swc1       %[val3],        24*4(%[out])                            \n\t"
        "add.s      %[val6],        %[val7],        %[val6]                 \n\t"
        "mul.s      %[val7],        %[fTmp1],       %[fTmp2]                \n\t"
        "swc1       %[val2],        8*4(%[out])                             \n\t"
        "add.s      %[val6],        %[val6],        %[val7]                 \n\t"
        "swc1       %[val7],        28*4(%[out])                            \n\t"
        "add.s      %[val4],        %[val4],        %[val6]                 \n\t"
        "add.s      %[val6],        %[val6],        %[val5]                 \n\t"
        "add.s      %[val5],        %[val5],        %[val7]                 \n\t"
        "swc1       %[val4],        4*4(%[out])                             \n\t"
        "swc1       %[val5],        20*4(%[out])                            \n\t"
        "swc1       %[val6],        12*4(%[out])                            \n\t"

        : [fTmp1] "=f"  (fTmp1), [fTmp2] "=&f" (fTmp2),
          [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val0] "+f" (val0), [val1] "+f" (val1),
          [val2] "+f" (val2), [val3] "+f" (val3),
          [val4] "+f" (val4), [val5] "+f" (val5),
          [val6] "+f" (val6), [val7] "+f" (val7)
        : [out] "r" (out)
    );

    __asm__ volatile (
        "sub.s      %[fTmp2],       %[val8],        %[val9]                 \n\t"
        "add.s      %[val8],        %[val8],        %[val9]                 \n\t"
        "sub.s      %[fTmp3],       %[val11],       %[val10]                \n\t"
        "add.s      %[val10],       %[val11],       %[val10]                \n\t"
        "sub.s      %[fTmp4],       %[val12],       %[val13]                \n\t"
        "mul.s      %[val9],        %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val12],       %[val12],       %[val13]                \n\t"
        "mul.s      %[val11],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val15],       %[val14]                \n\t"
        "mul.s      %[val13],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val14],       %[val15],       %[val14]                \n\t"
        "add.s      %[val10],       %[val10],       %[val11]                \n\t"
        "mul.s      %[val15],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val14],       %[val14],       %[val15]                \n\t"
        "add.s      %[val12],       %[val12],       %[val14]                \n\t"
        "add.s      %[val14],       %[val14],       %[val13]                \n\t"
        "add.s      %[val13],       %[val13],       %[val15]                \n\t"
        "add.s      %[val8],        %[val8],        %[val12]                \n\t"
        "add.s      %[val12],       %[val12],       %[val10]                \n\t"
        "add.s      %[val10],       %[val10],       %[val14]                \n\t"
        "add.s      %[val14],       %[val14],       %[val9]                 \n\t"
        "add.s      %[val9],        %[val9],        %[val13]                \n\t"
        "add.s      %[val13],       %[val13],       %[val11]                \n\t"
        "add.s      %[val11],       %[val11],       %[val15]                \n\t"
        "swc1       %[val8],         2*4(%[out])                            \n\t"
        "swc1       %[val9],        18*4(%[out])                            \n\t"
        "swc1       %[val10],       10*4(%[out])                            \n\t"
        "swc1       %[val11],       26*4(%[out])                            \n\t"
        "swc1       %[val12],        6*4(%[out])                            \n\t"
        "swc1       %[val13],       22*4(%[out])                            \n\t"
        "swc1       %[val14],       14*4(%[out])                            \n\t"
        "swc1       %[val15],       30*4(%[out])                            \n\t"

        : [fTmp2] "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val8]  "+f" (val8),  [val9]  "+f" (val9),  [val10] "+f" (val10),
          [val11] "+f" (val11), [val12] "+f" (val12), [val13] "+f" (val13),
          [val14] "+f" (val14), [val15] "+f" (val15)
        : [fTmp1] "f" (fTmp1), [out] "r" (out)
    );

    __asm__ volatile (
        "sub.s      %[fTmp2],       %[val16],       %[val17]                \n\t"
        "add.s      %[val16],       %[val16],       %[val17]                \n\t"
        "sub.s      %[fTmp3],       %[val19],       %[val18]                \n\t"
        "add.s      %[val18],       %[val19],       %[val18]                \n\t"
        "sub.s      %[fTmp4],       %[val20],       %[val21]                \n\t"
        "mul.s      %[val17],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val20],       %[val20],       %[val21]                \n\t"
        "mul.s      %[val19],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val23],       %[val22]                \n\t"
        "mul.s      %[val21],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val22],       %[val23],       %[val22]                \n\t"
        "add.s      %[val18],       %[val18],       %[val19]                \n\t"
        "mul.s      %[val23],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val22],       %[val22],       %[val23]                \n\t"
        "add.s      %[val20],       %[val20],       %[val22]                \n\t"
        "add.s      %[val22],       %[val22],       %[val21]                \n\t"
        "add.s      %[val21],       %[val21],       %[val23]                \n\t"

        : [fTmp2] "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val16] "+f" (val16), [val17] "+f" (val17), [val18] "+f" (val18),
          [val19] "+f" (val19), [val20] "+f" (val20), [val21] "+f" (val21),
          [val22] "+f" (val22), [val23] "+f" (val23)
        : [fTmp1] "f" (fTmp1)
    );

    __asm__ volatile (
        "sub.s      %[fTmp2],       %[val24],       %[val25]                \n\t"
        "add.s      %[val24],       %[val24],       %[val25]                \n\t"
        "sub.s      %[fTmp3],       %[val27],       %[val26]                \n\t"
        "add.s      %[val26],       %[val27],       %[val26]                \n\t"
        "sub.s      %[fTmp4],       %[val28],       %[val29]                \n\t"
        "mul.s      %[val25],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val28],       %[val28],       %[val29]                \n\t"
        "mul.s      %[val27],       %[fTmp1],       %[fTmp3]                \n\t"
        "sub.s      %[fTmp2],       %[val31],       %[val30]                \n\t"
        "mul.s      %[val29],       %[fTmp1],       %[fTmp4]                \n\t"
        "add.s      %[val30],       %[val31],       %[val30]                \n\t"
        "add.s      %[val26],       %[val26],       %[val27]                \n\t"
        "mul.s      %[val31],       %[fTmp1],       %[fTmp2]                \n\t"
        "add.s      %[val30],       %[val30],       %[val31]                \n\t"
        "add.s      %[val28],       %[val28],       %[val30]                \n\t"
        "add.s      %[val30],       %[val30],       %[val29]                \n\t"
        "add.s      %[val29],       %[val29],       %[val31]                \n\t"
        "add.s      %[val24],       %[val24],       %[val28]                \n\t"
        "add.s      %[val28],       %[val28],       %[val26]                \n\t"
        "add.s      %[val26],       %[val26],       %[val30]                \n\t"
        "add.s      %[val30],       %[val30],       %[val25]                \n\t"
        "add.s      %[val25],       %[val25],       %[val29]                \n\t"
        "add.s      %[val29],       %[val29],       %[val27]                \n\t"
        "add.s      %[val27],       %[val27],       %[val31]                \n\t"

        : [fTmp2] "=&f" (fTmp2), [fTmp3] "=&f" (fTmp3), [fTmp4] "=&f" (fTmp4),
          [val24] "+f" (val24), [val25] "+f" (val25), [val26] "+f" (val26),
          [val27] "+f" (val27), [val28] "+f" (val28), [val29] "+f" (val29),
          [val30] "+f" (val30), [val31] "+f" (val31)
        : [fTmp1] "f" (fTmp1)
    );

    out[ 1] = val16 + val24;
    out[17] = val17 + val25;
    out[ 9] = val18 + val26;
    out[25] = val19 + val27;
    out[ 5] = val20 + val28;
    out[21] = val21 + val29;
    out[13] = val22 + val30;
    out[29] = val23 + val31;
    out[ 3] = val24 + val20;
    out[19] = val25 + val21;
    out[11] = val26 + val22;
    out[27] = val27 + val23;
    out[ 7] = val28 + val18;
    out[23] = val29 + val19;
    out[15] = val30 + val17;
    out[31] = val31;
}

static void imdct36_mips_float(float *out, float *buf, float *in, float *win)
{
    float t0, t1, t2, t3, s0, s1, s2, s3;
    float tmp[18];
    /* temporary variables */
    float in1, in2, in3, in4, in5, in6;
    float out1, out2, out3, out4, out5;
    float c1, c2, c3, c4, c5, c6, c7, c8, c9;

    /**
    * all loops are unrolled totally, and instructions are scheduled to
    * minimize pipeline stall. instructions of the first two loops are
    * reorganized, in order to eliminate unnecessary readings and
    * writings into array. values defined in macros and tables are
    * eliminated - they are directly loaded in appropriate variables
    */

    /* loop 1 and 2 */
    __asm__ volatile (
        "lwc1   %[in1],  17*4(%[in])                                    \t\n"
        "lwc1   %[in2],  16*4(%[in])                                    \t\n"
        "lwc1   %[in3],  15*4(%[in])                                    \t\n"
        "lwc1   %[in4],  14*4(%[in])                                    \t\n"
        "lwc1   %[in5],  13*4(%[in])                                    \t\n"
        "lwc1   %[in6],  12*4(%[in])                                    \t\n"
        "add.s  %[out1], %[in1],  %[in2]                                \t\n"
        "add.s  %[out2], %[in2],  %[in3]                                \t\n"
        "add.s  %[out3], %[in3],  %[in4]                                \t\n"
        "add.s  %[out4], %[in4],  %[in5]                                \t\n"
        "add.s  %[out5], %[in5],  %[in6]                                \t\n"
        "lwc1   %[in1],  11*4(%[in])                                    \t\n"
        "swc1   %[out2], 16*4(%[in])                                    \t\n"
        "add.s  %[out1], %[out1], %[out3]                               \t\n"
        "swc1   %[out4], 14*4(%[in])                                    \t\n"
        "add.s  %[out3], %[out3], %[out5]                               \t\n"
        "lwc1   %[in2],  10*4(%[in])                                    \t\n"
        "lwc1   %[in3],  9*4(%[in])                                     \t\n"
        "swc1   %[out1], 17*4(%[in])                                    \t\n"
        "lwc1   %[in4],  8*4(%[in])                                     \t\n"
        "swc1   %[out3], 15*4(%[in])                                    \t\n"
        "add.s  %[out1], %[in6],  %[in1]                                \t\n"
        "add.s  %[out2], %[in1],  %[in2]                                \t\n"
        "add.s  %[out3], %[in2],  %[in3]                                \t\n"
        "add.s  %[out4], %[in3],  %[in4]                                \t\n"
        "lwc1   %[in5],  7*4(%[in])                                     \t\n"
        "swc1   %[out1], 12*4(%[in])                                    \t\n"
        "add.s  %[out5], %[out5], %[out2]                               \t\n"
        "swc1   %[out3], 10*4(%[in])                                    \t\n"
        "add.s  %[out2], %[out2], %[out4]                               \t\n"
        "lwc1   %[in6],  6*4(%[in])                                     \t\n"
        "lwc1   %[in1],  5*4(%[in])                                     \t\n"
        "swc1   %[out5], 13*4(%[in])                                    \t\n"
        "lwc1   %[in2],  4*4(%[in])                                     \t\n"
        "swc1   %[out2], 11*4(%[in])                                    \t\n"
        "add.s  %[out5], %[in4],  %[in5]                                \t\n"
        "add.s  %[out1], %[in5],  %[in6]                                \t\n"
        "add.s  %[out2], %[in6],  %[in1]                                \t\n"
        "add.s  %[out3], %[in1],  %[in2]                                \t\n"
        "lwc1   %[in3],  3*4(%[in])                                     \t\n"
        "swc1   %[out5], 8*4(%[in])                                     \t\n"
        "add.s  %[out4], %[out4], %[out1]                               \t\n"
        "swc1   %[out2], 6*4(%[in])                                     \t\n"
        "add.s  %[out1], %[out1], %[out3]                               \t\n"
        "lwc1   %[in4],  2*4(%[in])                                     \t\n"
        "lwc1   %[in5],  1*4(%[in])                                     \t\n"
        "swc1   %[out4], 9*4(%[in])                                     \t\n"
        "lwc1   %[in6],  0(%[in])                                       \t\n"
        "swc1   %[out1], 7*4(%[in])                                     \t\n"
        "add.s  %[out4], %[in2],  %[in3]                                \t\n"
        "add.s  %[out5], %[in3],  %[in4]                                \t\n"
        "add.s  %[out1], %[in4],  %[in5]                                \t\n"
        "add.s  %[out2], %[in5],  %[in6]                                \t\n"
        "swc1   %[out4], 4*4(%[in])                                     \t\n"
        "add.s  %[out3], %[out3], %[out5]                               \t\n"
        "swc1   %[out1], 2*4(%[in])                                     \t\n"
        "add.s  %[out5], %[out5], %[out2]                               \t\n"
        "swc1   %[out2], 1*4(%[in])                                     \t\n"
        "swc1   %[out3], 5*4(%[in])                                     \t\n"
        "swc1   %[out5], 3*4(%[in])                                     \t\n"

        : [in1] "=&f" (in1), [in2] "=&f" (in2),
          [in3] "=&f" (in3), [in4] "=&f" (in4),
          [in5] "=&f" (in5), [in6] "=&f" (in6),
          [out1] "=&f" (out1), [out2] "=&f" (out2),
          [out3] "=&f" (out3), [out4] "=&f" (out4),
          [out5] "=&f" (out5)
        : [in] "r" (in)
        : "memory"
    );

    /* loop 3 */
    __asm__ volatile (
        "li.s    %[c1],   0.5                                           \t\n"
        "lwc1    %[in1],  8*4(%[in])                                    \t\n"
        "lwc1    %[in2],  16*4(%[in])                                   \t\n"
        "lwc1    %[in3],  4*4(%[in])                                    \t\n"
        "lwc1    %[in4],  0(%[in])                                      \t\n"
        "lwc1    %[in5],  12*4(%[in])                                   \t\n"
        "li.s    %[c2],   0.93969262078590838405                        \t\n"
        "add.s   %[t2],   %[in1],  %[in2]                               \t\n"
        "add.s   %[t0],   %[in1],  %[in3]                               \t\n"
        "li.s    %[c3],   -0.76604444311897803520                       \t\n"
        "madd.s  %[t3],   %[in4],  %[in5], %[c1]                        \t\n"
        "sub.s   %[t1],   %[in4],  %[in5]                               \t\n"
        "sub.s   %[t2],   %[t2],   %[in3]                               \t\n"
        "mul.s   %[t0],   %[t0],   %[c2]                                \t\n"
        "li.s    %[c4],   -0.17364817766693034885                       \t\n"
        "li.s    %[c5],   -0.86602540378443864676                       \t\n"
        "li.s    %[c6],   0.98480775301220805936                        \t\n"
        "nmsub.s %[out1], %[t1],   %[t2],  %[c1]                        \t\n"
        "add.s   %[out2], %[t1],   %[t2]                                \t\n"
        "add.s   %[t2],   %[in2],  %[in3]                               \t\n"
        "sub.s   %[t1],   %[in1],  %[in2]                               \t\n"
        "sub.s   %[out3], %[t3],   %[t0]                                \t\n"
        "swc1    %[out1], 6*4(%[tmp])                                   \t\n"
        "swc1    %[out2], 16*4(%[tmp])                                  \t\n"
        "mul.s   %[t2],   %[t2],   %[c3]                                \t\n"
        "mul.s   %[t1],   %[t1],   %[c4]                                \t\n"
        "add.s   %[out1], %[t3],   %[t0]                                \t\n"
        "lwc1    %[in1],  10*4(%[in])                                   \t\n"
        "lwc1    %[in2],  14*4(%[in])                                   \t\n"
        "sub.s   %[out3], %[out3], %[t2]                                \t\n"
        "add.s   %[out2], %[t3],   %[t2]                                \t\n"
        "add.s   %[out1], %[out1], %[t1]                                \t\n"
        "lwc1    %[in3],  2*4(%[in])                                    \t\n"
        "lwc1    %[in4],  6*4(%[in])                                    \t\n"
        "swc1    %[out3], 10*4(%[tmp])                                  \t\n"
        "sub.s   %[out2], %[out2], %[t1]                                \t\n"
        "swc1    %[out1], 2*4(%[tmp])                                   \t\n"
        "add.s   %[out1], %[in1],  %[in2]                               \t\n"
        "add.s   %[t2],   %[in1],  %[in3]                               \t\n"
        "sub.s   %[t3],   %[in1],  %[in2]                               \t\n"
        "swc1    %[out2], 14*4(%[tmp])                                  \t\n"
        "li.s    %[c7],   -0.34202014332566873304                       \t\n"
        "sub.s   %[out1], %[out1], %[in3]                               \t\n"
        "mul.s   %[t2],   %[t2],   %[c6]                                \t\n"
        "mul.s   %[t3],   %[t3],   %[c7]                                \t\n"
        "li.s    %[c8],   0.86602540378443864676                        \t\n"
        "mul.s   %[t0],   %[in4],  %[c8]                                \t\n"
        "mul.s   %[out1], %[out1], %[c5]                                \t\n"
        "add.s   %[t1],   %[in2],  %[in3]                               \t\n"
        "li.s    %[c9],   -0.64278760968653932632                       \t\n"
        "add.s   %[out2], %[t2],   %[t3]                                \t\n"
        "lwc1    %[in1],  9*4(%[in])                                    \t\n"
        "swc1    %[out1], 4*4(%[tmp])                                   \t\n"
        "mul.s   %[t1],   %[t1],   %[c9]                                \t\n"
        "lwc1    %[in2],  17*4(%[in])                                   \t\n"
        "add.s   %[out2], %[out2], %[t0]                                \t\n"
        "lwc1    %[in3],  5*4(%[in])                                    \t\n"
        "lwc1    %[in4],  1*4(%[in])                                    \t\n"
        "add.s   %[out3], %[t2],   %[t1]                                \t\n"
        "sub.s   %[out1], %[t3],   %[t1]                                \t\n"
        "swc1    %[out2], 0(%[tmp])                                     \t\n"
        "lwc1    %[in5],  13*4(%[in])                                   \t\n"
        "add.s   %[t2],   %[in1],  %[in2]                               \t\n"
        "sub.s   %[out3], %[out3], %[t0]                                \t\n"
        "sub.s   %[out1], %[out1], %[t0]                                \t\n"
        "add.s   %[t0],   %[in1],  %[in3]                               \t\n"
        "madd.s  %[t3],   %[in4],  %[in5], %[c1]                        \t\n"
        "sub.s   %[t2],   %[t2],   %[in3]                               \t\n"
        "swc1    %[out3], 12*4(%[tmp])                                  \t\n"
        "swc1    %[out1], 8*4(%[tmp])                                   \t\n"
        "sub.s   %[t1],   %[in4],  %[in5]                               \t\n"
        "mul.s   %[t0],   %[t0],   %[c2]                                \t\n"
        "nmsub.s %[out1], %[t1],   %[t2],  %[c1]                        \t\n"
        "add.s   %[out2], %[t1],   %[t2]                                \t\n"
        "add.s   %[t2],   %[in2],  %[in3]                               \t\n"
        "sub.s   %[t1],   %[in1],  %[in2]                               \t\n"
        "sub.s   %[out3], %[t3],   %[t0]                                \t\n"
        "swc1    %[out1], 7*4(%[tmp])                                   \t\n"
        "swc1    %[out2], 17*4(%[tmp])                                  \t\n"
        "mul.s   %[t2],   %[t2],   %[c3]                                \t\n"
        "mul.s   %[t1],   %[t1],   %[c4]                                \t\n"
        "add.s   %[out1], %[t3],   %[t0]                                \t\n"
        "lwc1    %[in1],  11*4(%[in])                                   \t\n"
        "lwc1    %[in2],  15*4(%[in])                                   \t\n"
        "sub.s   %[out3], %[out3], %[t2]                                \t\n"
        "add.s   %[out2], %[t3],   %[t2]                                \t\n"
        "add.s   %[out1], %[out1], %[t1]                                \t\n"
        "lwc1    %[in3],  3*4(%[in])                                    \t\n"
        "lwc1    %[in4],  7*4(%[in])                                    \t\n"
        "swc1    %[out3], 11*4(%[tmp])                                  \t\n"
        "sub.s   %[out2], %[out2], %[t1]                                \t\n"
        "swc1    %[out1], 3*4(%[tmp])                                   \t\n"
        "add.s   %[out3], %[in1],  %[in2]                               \t\n"
        "add.s   %[t2],   %[in1],  %[in3]                               \t\n"
        "sub.s   %[t3],   %[in1],  %[in2]                               \t\n"
        "swc1    %[out2], 15*4(%[tmp])                                  \t\n"
        "mul.s   %[t0],   %[in4],  %[c8]                                \t\n"
        "sub.s   %[out3], %[out3], %[in3]                               \t\n"
        "mul.s   %[t2],   %[t2],   %[c6]                                \t\n"
        "mul.s   %[t3],   %[t3],   %[c7]                                \t\n"
        "add.s   %[t1],   %[in2],  %[in3]                               \t\n"
        "mul.s   %[out3], %[out3], %[c5]                                \t\n"
        "add.s   %[out1], %[t2],   %[t3]                                \t\n"
        "mul.s   %[t1],   %[t1],   %[c9]                                \t\n"
        "swc1    %[out3], 5*4(%[tmp])                                   \t\n"
        "add.s   %[out1], %[out1], %[t0]                                \t\n"
        "add.s   %[out2], %[t2],   %[t1]                                \t\n"
        "sub.s   %[out3], %[t3],   %[t1]                                \t\n"
        "swc1    %[out1], 1*4(%[tmp])                                   \t\n"
        "sub.s   %[out2], %[out2], %[t0]                                \t\n"
        "sub.s   %[out3], %[out3], %[t0]                                \t\n"
        "swc1    %[out2], 13*4(%[tmp])                                  \t\n"
        "swc1    %[out3], 9*4(%[tmp])                                   \t\n"

        : [t0] "=&f" (t0), [t1] "=&f" (t1),
          [t2] "=&f" (t2), [t3] "=&f" (t3),
          [in1] "=&f" (in1), [in2] "=&f" (in2),
          [in3] "=&f" (in3), [in4] "=&f" (in4),
          [in5] "=&f" (in5),
          [out1] "=&f" (out1), [out2] "=&f" (out2),
          [out3] "=&f" (out3),
          [c1] "=&f" (c1), [c2] "=&f" (c2),
          [c3] "=&f" (c3), [c4] "=&f" (c4),
          [c5] "=&f" (c5), [c6] "=&f" (c6),
          [c7] "=&f" (c7), [c8] "=&f" (c8),
          [c9] "=&f" (c9)
        : [in] "r" (in), [tmp] "r" (tmp)
        : "memory"
    );

    /* loop 4 */
    __asm__ volatile (
        "lwc1   %[in1],  2*4(%[tmp])                                    \t\n"
        "lwc1   %[in2],  0(%[tmp])                                      \t\n"
        "lwc1   %[in3],  3*4(%[tmp])                                    \t\n"
        "lwc1   %[in4],  1*4(%[tmp])                                    \t\n"
        "li.s   %[c1],   0.50190991877167369479                         \t\n"
        "li.s   %[c2],   5.73685662283492756461                         \t\n"
        "add.s  %[s0],   %[in1], %[in2]                                 \t\n"
        "sub.s  %[s2],   %[in1], %[in2]                                 \t\n"
        "add.s  %[s1],   %[in3], %[in4]                                 \t\n"
        "sub.s  %[s3],   %[in3], %[in4]                                 \t\n"
        "lwc1   %[in1],  9*4(%[win])                                    \t\n"
        "lwc1   %[in2],  4*9*4(%[buf])                                  \t\n"
        "lwc1   %[in3],  8*4(%[win])                                    \t\n"
        "mul.s  %[s1],   %[s1],  %[c1]                                  \t\n"
        "mul.s  %[s3],   %[s3],  %[c2]                                  \t\n"
        "lwc1   %[in4],  4*8*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  29*4(%[win])                                   \t\n"
        "lwc1   %[in6],  28*4(%[win])                                   \t\n"
        "add.s  %[t0],   %[s0],  %[s1]                                  \t\n"
        "sub.s  %[t1],   %[s0],  %[s1]                                  \t\n"
        "li.s   %[c1],   0.51763809020504152469                         \t\n"
        "li.s   %[c2],   1.93185165257813657349                         \t\n"
        "mul.s  %[out3], %[in5], %[t0]                                  \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out4], %[in6], %[t0]                                  \t\n"
        "add.s  %[t0],   %[s2],  %[s3]                                  \t\n"
        "swc1   %[out3], 4*9*4(%[buf])                                  \t\n"
        "swc1   %[out1], 288*4(%[out])                                  \t\n"
        "swc1   %[out2], 256*4(%[out])                                  \t\n"
        "swc1   %[out4], 4*8*4(%[buf])                                  \t\n"
        "sub.s  %[t1],   %[s2],  %[s3]                                  \t\n"
        "lwc1   %[in1],  17*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*17*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  0(%[win])                                      \t\n"
        "lwc1   %[in4],  0(%[buf])                                      \t\n"
        "lwc1   %[in5],  37*4(%[win])                                   \t\n"
        "lwc1   %[in6],  20*4(%[win])                                   \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "lwc1   %[in1],  6*4(%[tmp])                                    \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out3], %[t0],  %[in5]                                 \t\n"
        "mul.s  %[out4], %[t0],  %[in6]                                 \t\n"
        "swc1   %[out1], 544*4(%[out])                                  \t\n"
        "lwc1   %[in2],  4*4(%[tmp])                                    \t\n"
        "swc1   %[out2], 0(%[out])                                      \t\n"
        "swc1   %[out3], 4*17*4(%[buf])                                 \t\n"
        "swc1   %[out4], 0(%[buf])                                      \t\n"
        "lwc1   %[in3],  7*4(%[tmp])                                    \t\n"
        "add.s  %[s0],   %[in1], %[in2]                                 \t\n"
        "sub.s  %[s2],   %[in1], %[in2]                                 \t\n"
        "lwc1   %[in4],  5*4(%[tmp])                                    \t\n"
        "add.s  %[s1],   %[in3], %[in4]                                 \t\n"
        "sub.s  %[s3],   %[in3], %[in4]                                 \t\n"
        "lwc1   %[in1],  10*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*10*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  7*4(%[win])                                    \t\n"
        "mul.s  %[s1],   %[s1],  %[c1]                                  \t\n"
        "mul.s  %[s3],   %[s3],  %[c2]                                  \t\n"
        "add.s  %[t0],   %[s0],  %[s1]                                  \t\n"
        "sub.s  %[t1],   %[s0],  %[s1]                                  \t\n"
        "lwc1   %[in4],  4*7*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  30*4(%[win])                                   \t\n"
        "lwc1   %[in6],  27*4(%[win])                                   \t\n"
        "li.s   %[c1],   0.55168895948124587824                         \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out3], %[t0],  %[in5]                                 \t\n"
        "mul.s  %[out4], %[t0],  %[in6]                                 \t\n"
        "add.s  %[t0],   %[s2],  %[s3]                                  \t\n"
        "swc1   %[out1], 320*4(%[out])                                  \t\n"
        "swc1   %[out2], 224*4(%[out])                                  \t\n"
        "swc1   %[out3], 4*10*4(%[buf])                                 \t\n"
        "swc1   %[out4], 4*7*4(%[buf])                                  \t\n"
        "sub.s  %[t1],   %[s2],  %[s3]                                  \t\n"
        "lwc1   %[in1],  16*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*16*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  1*4(%[win])                                    \t\n"
        "lwc1   %[in4],  4*1*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  36*4(%[win])                                   \t\n"
        "lwc1   %[in6],  21*4(%[win])                                   \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "lwc1   %[in1],  10*4(%[tmp])                                   \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out3], %[in5], %[t0]                                  \t\n"
        "mul.s  %[out4], %[in6], %[t0]                                  \t\n"
        "swc1   %[out1], 512*4(%[out])                                  \t\n"
        "lwc1   %[in2],  8*4(%[tmp])                                    \t\n"
        "swc1   %[out2], 32*4(%[out])                                   \t\n"
        "swc1   %[out3], 4*16*4(%[buf])                                 \t\n"
        "swc1   %[out4], 4*1*4(%[buf])                                  \t\n"
        "li.s   %[c2],   1.18310079157624925896                         \t\n"
        "add.s  %[s0],   %[in1], %[in2]                                 \t\n"
        "sub.s  %[s2],   %[in1], %[in2]                                 \t\n"
        "lwc1   %[in3],  11*4(%[tmp])                                   \t\n"
        "lwc1   %[in4],  9*4(%[tmp])                                    \t\n"
        "add.s  %[s1],   %[in3], %[in4]                                 \t\n"
        "sub.s  %[s3],   %[in3], %[in4]                                 \t\n"
        "lwc1   %[in1],  11*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*11*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  6*4(%[win])                                    \t\n"
        "mul.s  %[s1],   %[s1],  %[c1]                                  \t\n"
        "mul.s  %[s3],   %[s3],  %[c2]                                  \t\n"
        "lwc1   %[in4],  4*6*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  31*4(%[win])                                   \t\n"
        "lwc1   %[in6],  26*4(%[win])                                   \t\n"
        "add.s  %[t0],   %[s0],  %[s1]                                  \t\n"
        "sub.s  %[t1],   %[s0],  %[s1]                                  \t\n"
        "mul.s  %[out3], %[t0],  %[in5]                                 \t\n"
        "mul.s  %[out4], %[t0],  %[in6]                                 \t\n"
        "add.s  %[t0],   %[s2],  %[s3]                                  \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "swc1   %[out3], 4*11*4(%[buf])                                 \t\n"
        "swc1   %[out4], 4*6*4(%[buf])                                  \t\n"
        "sub.s  %[t1],   %[s2],  %[s3]                                  \t\n"
        "swc1   %[out1], 352*4(%[out])                                  \t\n"
        "swc1   %[out2], 192*4(%[out])                                  \t\n"
        "lwc1   %[in1],  15*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*15*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  2*4(%[win])                                    \t\n"
        "lwc1   %[in4],  4*2*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  35*4(%[win])                                   \t\n"
        "lwc1   %[in6],  22*4(%[win])                                   \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "lwc1   %[in1],  14*4(%[tmp])                                   \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out3], %[t0],  %[in5]                                 \t\n"
        "mul.s  %[out4], %[t0],  %[in6]                                 \t\n"
        "swc1   %[out1], 480*4(%[out])                                  \t\n"
        "lwc1   %[in2],  12*4(%[tmp])                                   \t\n"
        "swc1   %[out2], 64*4(%[out])                                   \t\n"
        "swc1   %[out3], 4*15*4(%[buf])                                 \t\n"
        "swc1   %[out4], 4*2*4(%[buf])                                  \t\n"
        "lwc1   %[in3],  15*4(%[tmp])                                   \t\n"
        "add.s  %[s0],   %[in1], %[in2]                                 \t\n"
        "sub.s  %[s2],   %[in1], %[in2]                                 \t\n"
        "lwc1   %[in4],  13*4(%[tmp])                                   \t\n"
        "li.s   %[c1],   0.61038729438072803416                         \t\n"
        "li.s   %[c2],   0.87172339781054900991                         \t\n"
        "add.s  %[s1],   %[in3], %[in4]                                 \t\n"
        "sub.s  %[s3],   %[in3], %[in4]                                 \t\n"
        "lwc1   %[in1],  12*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*12*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  5*4(%[win])                                    \t\n"
        "mul.s  %[s1],   %[s1],  %[c1]                                  \t\n"
        "mul.s  %[s3],   %[s3],  %[c2]                                  \t\n"
        "lwc1   %[in4],  4*5*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  32*4(%[win])                                   \t\n"
        "lwc1   %[in6],  25*4(%[win])                                   \t\n"
        "add.s  %[t0],   %[s0],  %[s1]                                  \t\n"
        "sub.s  %[t1],   %[s0],  %[s1]                                  \t\n"
        "lwc1   %[s0],   16*4(%[tmp])                                   \t\n"
        "lwc1   %[s1],   17*4(%[tmp])                                   \t\n"
        "li.s   %[c1],   0.70710678118654752439                         \t\n"
        "mul.s  %[out3], %[t0],  %[in5]                                 \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out4], %[t0],  %[in6]                                 \t\n"
        "add.s  %[t0],   %[s2],  %[s3]                                  \t\n"
        "swc1   %[out3], 4*12*4(%[buf])                                 \t\n"
        "swc1   %[out1], 384*4(%[out])                                  \t\n"
        "swc1   %[out2], 160*4(%[out])                                  \t\n"
        "swc1   %[out4], 4*5*4(%[buf])                                  \t\n"
        "sub.s  %[t1],   %[s2],  %[s3]                                  \t\n"
        "lwc1   %[in1],  14*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*14*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  3*4(%[win])                                    \t\n"
        "lwc1   %[in4],  4*3*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  34*4(%[win])                                   \t\n"
        "lwc1   %[in6],  23*4(%[win])                                   \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "mul.s  %[s1],   %[s1],  %[c1]                                  \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out3], %[in5], %[t0]                                  \t\n"
        "mul.s  %[out4], %[in6], %[t0]                                  \t\n"
        "swc1   %[out1], 448*4(%[out])                                  \t\n"
        "add.s  %[t0],   %[s0],  %[s1]                                  \t\n"
        "swc1   %[out2], 96*4(%[out])                                   \t\n"
        "swc1   %[out3], 4*14*4(%[buf])                                 \t\n"
        "swc1   %[out4], 4*3*4(%[buf])                                  \t\n"
        "sub.s  %[t1],   %[s0],  %[s1]                                  \t\n"
        "lwc1   %[in1],  13*4(%[win])                                   \t\n"
        "lwc1   %[in2],  4*13*4(%[buf])                                 \t\n"
        "lwc1   %[in3],  4*4(%[win])                                    \t\n"
        "lwc1   %[in4],  4*4*4(%[buf])                                  \t\n"
        "lwc1   %[in5],  33*4(%[win])                                   \t\n"
        "lwc1   %[in6],  24*4(%[win])                                   \t\n"
        "madd.s %[out1], %[in2], %[in1], %[t1]                          \t\n"
        "madd.s %[out2], %[in4], %[in3], %[t1]                          \t\n"
        "mul.s  %[out3], %[t0],  %[in5]                                 \t\n"
        "mul.s  %[out4], %[t0],  %[in6]                                 \t\n"
        "swc1   %[out1], 416*4(%[out])                                  \t\n"
        "swc1   %[out2], 128*4(%[out])                                  \t\n"
        "swc1   %[out3], 4*13*4(%[buf])                                 \t\n"
        "swc1   %[out4], 4*4*4(%[buf])                                  \t\n"

        : [c1] "=&f" (c1), [c2] "=&f" (c2),
          [in1] "=&f" (in1), [in2] "=&f" (in2),
          [in3] "=&f" (in3), [in4] "=&f" (in4),
          [in5] "=&f" (in5), [in6] "=&f" (in6),
          [out1] "=&f" (out1), [out2] "=&f" (out2),
          [out3] "=&f" (out3), [out4] "=&f" (out4),
          [t0] "=&f" (t0), [t1] "=&f" (t1),
          [t2] "=&f" (t2), [t3] "=&f" (t3),
          [s0] "=&f" (s0), [s1] "=&f" (s1),
          [s2] "=&f" (s2), [s3] "=&f" (s3)
        : [tmp] "r" (tmp), [win] "r" (win),
          [buf] "r" (buf), [out] "r" (out)
        : "memory"
    );
}

static void ff_imdct36_blocks_mips_float(float *out, float *buf, float *in,
                               int count, int switch_point, int block_type)
{
    int j;
    for (j=0 ; j < count; j++) {
        /* apply window & overlap with previous buffer */

        /* select window */
        int win_idx = (switch_point && j < 2) ? 0 : block_type;
        float *win = ff_mdct_win_float[win_idx + (4 & -(j & 1))];

        imdct36_mips_float(out, buf, in, win);

        in  += 18;
        buf += ((j&3) != 3 ? 1 : (72-3));
        out++;
    }
}

void ff_mpadsp_init_mipsfpu(MPADSPContext *s)
{
    s->apply_window_float   = ff_mpadsp_apply_window_mips_float;
    s->imdct36_blocks_float = ff_imdct36_blocks_mips_float;
    s->dct32_float          = ff_dct32_mips_float;
}
