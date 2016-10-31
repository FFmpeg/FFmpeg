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
 * Reference: libavcodec/aacpsdsp.c
 */

#include "config.h"
#include "libavcodec/aacpsdsp.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM
static void ps_hybrid_analysis_ileave_mips(float (*out)[32][2], float L[2][38][64],
                                        int i, int len)
{
    int temp0, temp1, temp2, temp3;
    int temp4, temp5, temp6, temp7;
    float *out1=&out[i][0][0];
    float *L1=&L[0][0][i];
    float *j=out1+ len*2;

    for (; i < 64; i++) {

        /* loop unrolled 8 times */
        __asm__ volatile (
        "1:                                          \n\t"
            "lw      %[temp0],   0(%[L1])            \n\t"
            "lw      %[temp1],   9728(%[L1])         \n\t"
            "lw      %[temp2],   256(%[L1])          \n\t"
            "lw      %[temp3],   9984(%[L1])         \n\t"
            "lw      %[temp4],   512(%[L1])          \n\t"
            "lw      %[temp5],   10240(%[L1])        \n\t"
            "lw      %[temp6],   768(%[L1])          \n\t"
            "lw      %[temp7],   10496(%[L1])        \n\t"
            "sw      %[temp0],   0(%[out1])          \n\t"
            "sw      %[temp1],   4(%[out1])          \n\t"
            "sw      %[temp2],   8(%[out1])          \n\t"
            "sw      %[temp3],   12(%[out1])         \n\t"
            "sw      %[temp4],   16(%[out1])         \n\t"
            "sw      %[temp5],   20(%[out1])         \n\t"
            "sw      %[temp6],   24(%[out1])         \n\t"
            "sw      %[temp7],   28(%[out1])         \n\t"
            PTR_ADDIU "%[out1],  %[out1],      32    \n\t"
            PTR_ADDIU "%[L1],    %[L1],        1024  \n\t"
            "bne     %[out1],    %[j],         1b    \n\t"

            : [out1]"+r"(out1), [L1]"+r"(L1), [j]"+r"(j),
              [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7)
            : [len]"r"(len)
            : "memory"
        );
        out1-=(len<<1)-64;
        L1-=(len<<6)-1;
        j+=len*2;
    }
}

static void ps_hybrid_synthesis_deint_mips(float out[2][38][64],
                                        float (*in)[32][2],
                                        int i, int len)
{
    int n;
    int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
    float *out1 = (float*)out + i;
    float *out2 = (float*)out + 2432 + i;
    float *in1 = (float*)in + 64 * i;
    float *in2 = (float*)in + 64 * i + 1;

    for (; i < 64; i++) {
        for (n = 0; n < 7; n++) {

            /* loop unrolled 8 times */
            __asm__ volatile (
                 "lw      %[temp0],   0(%[in1])               \n\t"
                 "lw      %[temp1],   0(%[in2])               \n\t"
                 "lw      %[temp2],   8(%[in1])               \n\t"
                 "lw      %[temp3],   8(%[in2])               \n\t"
                 "lw      %[temp4],   16(%[in1])              \n\t"
                 "lw      %[temp5],   16(%[in2])              \n\t"
                 "lw      %[temp6],   24(%[in1])              \n\t"
                 "lw      %[temp7],   24(%[in2])              \n\t"
                 PTR_ADDIU "%[out1],  %[out1],         1024   \n\t"
                 PTR_ADDIU "%[out2],  %[out2],         1024   \n\t"
                 PTR_ADDIU "%[in1],   %[in1],          32     \n\t"
                 PTR_ADDIU "%[in2],   %[in2],          32     \n\t"
                 "sw      %[temp0],   -1024(%[out1])          \n\t"
                 "sw      %[temp1],   -1024(%[out2])          \n\t"
                 "sw      %[temp2],   -768(%[out1])           \n\t"
                 "sw      %[temp3],   -768(%[out2])           \n\t"
                 "sw      %[temp4],   -512(%[out1])           \n\t"
                 "sw      %[temp5],   -512(%[out2])           \n\t"
                 "sw      %[temp6],   -256(%[out1])           \n\t"
                 "sw      %[temp7],   -256(%[out2])           \n\t"

                 : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                   [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                   [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                   [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                   [out1]"+r"(out1), [out2]"+r"(out2),
                   [in1]"+r"(in1), [in2]"+r"(in2)
                 :
                 : "memory"
            );
        }
        /* loop unrolled 8 times */
        __asm__ volatile (
            "lw      %[temp0],   0(%[in1])               \n\t"
            "lw      %[temp1],   0(%[in2])               \n\t"
            "lw      %[temp2],   8(%[in1])               \n\t"
            "lw      %[temp3],   8(%[in2])               \n\t"
            "lw      %[temp4],   16(%[in1])              \n\t"
            "lw      %[temp5],   16(%[in2])              \n\t"
            "lw      %[temp6],   24(%[in1])              \n\t"
            "lw      %[temp7],   24(%[in2])              \n\t"
            PTR_ADDIU "%[out1],  %[out1],        -7164   \n\t"
            PTR_ADDIU "%[out2],  %[out2],        -7164   \n\t"
            PTR_ADDIU "%[in1],   %[in1],         32      \n\t"
            PTR_ADDIU "%[in2],   %[in2],         32      \n\t"
            "sw      %[temp0],   7164(%[out1])           \n\t"
            "sw      %[temp1],   7164(%[out2])           \n\t"
            "sw      %[temp2],   7420(%[out1])           \n\t"
            "sw      %[temp3],   7420(%[out2])           \n\t"
            "sw      %[temp4],   7676(%[out1])           \n\t"
            "sw      %[temp5],   7676(%[out2])           \n\t"
            "sw      %[temp6],   7932(%[out1])           \n\t"
            "sw      %[temp7],   7932(%[out2])           \n\t"

            : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
              [out1]"+r"(out1), [out2]"+r"(out2),
              [in1]"+r"(in1), [in2]"+r"(in2)
            :
            : "memory"
        );
    }
}

#if HAVE_MIPSFPU
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
static void ps_add_squares_mips(float *dst, const float (*src)[2], int n)
{
    int i;
    float temp0, temp1, temp2, temp3, temp4, temp5;
    float temp6, temp7, temp8, temp9, temp10, temp11;
    float *src0 = (float*)&src[0][0];
    float *dst0 = &dst[0];

    for (i = 0; i < 8; i++) {
        /* loop unrolled 4 times */
        __asm__ volatile (
            "lwc1     %[temp0],    0(%[src0])                          \n\t"
            "lwc1     %[temp1],    4(%[src0])                          \n\t"
            "lwc1     %[temp2],    8(%[src0])                          \n\t"
            "lwc1     %[temp3],    12(%[src0])                         \n\t"
            "lwc1     %[temp4],    16(%[src0])                         \n\t"
            "lwc1     %[temp5],    20(%[src0])                         \n\t"
            "lwc1     %[temp6],    24(%[src0])                         \n\t"
            "lwc1     %[temp7],    28(%[src0])                         \n\t"
            "lwc1     %[temp8],    0(%[dst0])                          \n\t"
            "lwc1     %[temp9],    4(%[dst0])                          \n\t"
            "lwc1     %[temp10],   8(%[dst0])                          \n\t"
            "lwc1     %[temp11],   12(%[dst0])                         \n\t"
            "mul.s    %[temp1],    %[temp1],    %[temp1]               \n\t"
            "mul.s    %[temp3],    %[temp3],    %[temp3]               \n\t"
            "mul.s    %[temp5],    %[temp5],    %[temp5]               \n\t"
            "mul.s    %[temp7],    %[temp7],    %[temp7]               \n\t"
            "madd.s   %[temp0],    %[temp1],    %[temp0],   %[temp0]   \n\t"
            "madd.s   %[temp2],    %[temp3],    %[temp2],   %[temp2]   \n\t"
            "madd.s   %[temp4],    %[temp5],    %[temp4],   %[temp4]   \n\t"
            "madd.s   %[temp6],    %[temp7],    %[temp6],   %[temp6]   \n\t"
            "add.s    %[temp0],    %[temp8],    %[temp0]               \n\t"
            "add.s    %[temp2],    %[temp9],    %[temp2]               \n\t"
            "add.s    %[temp4],    %[temp10],   %[temp4]               \n\t"
            "add.s    %[temp6],    %[temp11],   %[temp6]               \n\t"
            "swc1     %[temp0],    0(%[dst0])                          \n\t"
            "swc1     %[temp2],    4(%[dst0])                          \n\t"
            "swc1     %[temp4],    8(%[dst0])                          \n\t"
            "swc1     %[temp6],    12(%[dst0])                         \n\t"
            PTR_ADDIU "%[dst0],    %[dst0],     16                     \n\t"
            PTR_ADDIU "%[src0],    %[src0],     32                     \n\t"

            : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
              [temp6]"=&f"(temp6), [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
              [temp9]"=&f"(temp9), [dst0]"+r"(dst0), [src0]"+r"(src0),
              [temp10]"=&f"(temp10), [temp11]"=&f"(temp11)
            :
            : "memory"
        );
   }
}

static void ps_mul_pair_single_mips(float (*dst)[2], float (*src0)[2], float *src1,
                                 int n)
{
    float temp0, temp1, temp2;
    float *p_d, *p_s0, *p_s1, *end;
    p_d = &dst[0][0];
    p_s0 = &src0[0][0];
    p_s1 = &src1[0];
    end = p_s1 + n;

    __asm__ volatile(
        ".set push                                      \n\t"
        ".set noreorder                                 \n\t"
        "1:                                             \n\t"
        "lwc1     %[temp2],   0(%[p_s1])                \n\t"
        "lwc1     %[temp0],   0(%[p_s0])                \n\t"
        "lwc1     %[temp1],   4(%[p_s0])                \n\t"
        PTR_ADDIU "%[p_d],    %[p_d],       8           \n\t"
        "mul.s    %[temp0],   %[temp0],     %[temp2]    \n\t"
        "mul.s    %[temp1],   %[temp1],     %[temp2]    \n\t"
        PTR_ADDIU "%[p_s0],   %[p_s0],      8           \n\t"
        "swc1     %[temp0],   -8(%[p_d])                \n\t"
        "swc1     %[temp1],   -4(%[p_d])                \n\t"
        "bne      %[p_s1],    %[end],       1b          \n\t"
        PTR_ADDIU "%[p_s1],   %[p_s1],      4           \n\t"
        ".set pop                                       \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
          [temp2]"=&f"(temp2), [p_d]"+r"(p_d),
          [p_s0]"+r"(p_s0), [p_s1]"+r"(p_s1)
        : [end]"r"(end)
        : "memory"
    );
}

static void ps_decorrelate_mips(float (*out)[2], float (*delay)[2],
                             float (*ap_delay)[PS_QMF_TIME_SLOTS + PS_MAX_AP_DELAY][2],
                             const float phi_fract[2], const float (*Q_fract)[2],
                             const float *transient_gain,
                             float g_decay_slope,
                             int len)
{
    float *p_delay = &delay[0][0];
    float *p_out = &out[0][0];
    float *p_ap_delay = &ap_delay[0][0][0];
    const float *p_t_gain = transient_gain;
    const float *p_Q_fract = &Q_fract[0][0];
    float ag0, ag1, ag2;
    float phi_fract0 = phi_fract[0];
    float phi_fract1 = phi_fract[1];
    float temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7, temp8, temp9;

    float *p_delay_end = (p_delay + (len << 1));

    /* merged 2 loops */
    __asm__ volatile(
        ".set    push                                                    \n\t"
        ".set    noreorder                                               \n\t"
        "li.s    %[ag0],        0.65143905753106                         \n\t"
        "li.s    %[ag1],        0.56471812200776                         \n\t"
        "li.s    %[ag2],        0.48954165955695                         \n\t"
        "mul.s   %[ag0],        %[ag0],        %[g_decay_slope]          \n\t"
        "mul.s   %[ag1],        %[ag1],        %[g_decay_slope]          \n\t"
        "mul.s   %[ag2],        %[ag2],        %[g_decay_slope]          \n\t"
    "1:                                                                  \n\t"
        "lwc1    %[temp0],      0(%[p_delay])                            \n\t"
        "lwc1    %[temp1],      4(%[p_delay])                            \n\t"
        "lwc1    %[temp4],      16(%[p_ap_delay])                        \n\t"
        "lwc1    %[temp5],      20(%[p_ap_delay])                        \n\t"
        "mul.s   %[temp3],      %[temp0],      %[phi_fract1]             \n\t"
        "lwc1    %[temp6],      0(%[p_Q_fract])                          \n\t"
        "mul.s   %[temp2],      %[temp1],      %[phi_fract1]             \n\t"
        "lwc1    %[temp7],      4(%[p_Q_fract])                          \n\t"
        "madd.s  %[temp3],      %[temp3],      %[temp1], %[phi_fract0]   \n\t"
        "msub.s  %[temp2],      %[temp2],      %[temp0], %[phi_fract0]   \n\t"
        "mul.s   %[temp8],      %[temp5],      %[temp7]                  \n\t"
        "mul.s   %[temp9],      %[temp4],      %[temp7]                  \n\t"
        "lwc1    %[temp7],      12(%[p_Q_fract])                         \n\t"
        "mul.s   %[temp0],      %[ag0],        %[temp2]                  \n\t"
        "mul.s   %[temp1],      %[ag0],        %[temp3]                  \n\t"
        "msub.s  %[temp8],      %[temp8],      %[temp4], %[temp6]        \n\t"
        "lwc1    %[temp4],      304(%[p_ap_delay])                       \n\t"
        "madd.s  %[temp9],      %[temp9],      %[temp5], %[temp6]        \n\t"
        "lwc1    %[temp5],      308(%[p_ap_delay])                       \n\t"
        "sub.s   %[temp0],      %[temp8],      %[temp0]                  \n\t"
        "sub.s   %[temp1],      %[temp9],      %[temp1]                  \n\t"
        "madd.s  %[temp2],      %[temp2],      %[ag0],   %[temp0]        \n\t"
        "lwc1    %[temp6],      8(%[p_Q_fract])                          \n\t"
        "madd.s  %[temp3],      %[temp3],      %[ag0],   %[temp1]        \n\t"
        "mul.s   %[temp8],      %[temp5],      %[temp7]                  \n\t"
        "mul.s   %[temp9],      %[temp4],      %[temp7]                  \n\t"
        "lwc1    %[temp7],      20(%[p_Q_fract])                         \n\t"
        "msub.s  %[temp8],      %[temp8],      %[temp4], %[temp6]        \n\t"
        "swc1    %[temp2],      40(%[p_ap_delay])                        \n\t"
        "mul.s   %[temp2],      %[ag1],        %[temp0]                  \n\t"
        "swc1    %[temp3],      44(%[p_ap_delay])                        \n\t"
        "mul.s   %[temp3],      %[ag1],        %[temp1]                  \n\t"
        "lwc1    %[temp4],      592(%[p_ap_delay])                       \n\t"
        "madd.s  %[temp9],      %[temp9],      %[temp5], %[temp6]        \n\t"
        "lwc1    %[temp5],      596(%[p_ap_delay])                       \n\t"
        "sub.s   %[temp2],      %[temp8],      %[temp2]                  \n\t"
        "sub.s   %[temp3],      %[temp9],      %[temp3]                  \n\t"
        "lwc1    %[temp6],      16(%[p_Q_fract])                         \n\t"
        "madd.s  %[temp0],      %[temp0],      %[ag1],   %[temp2]        \n\t"
        "madd.s  %[temp1],      %[temp1],      %[ag1],   %[temp3]        \n\t"
        "mul.s   %[temp8],      %[temp5],      %[temp7]                  \n\t"
        "mul.s   %[temp9],      %[temp4],      %[temp7]                  \n\t"
        "msub.s  %[temp8],      %[temp8],      %[temp4], %[temp6]        \n\t"
        "madd.s  %[temp9],      %[temp9],      %[temp5], %[temp6]        \n\t"
        "swc1    %[temp0],      336(%[p_ap_delay])                       \n\t"
        "mul.s   %[temp0],      %[ag2],        %[temp2]                  \n\t"
        "swc1    %[temp1],      340(%[p_ap_delay])                       \n\t"
        "mul.s   %[temp1],      %[ag2],        %[temp3]                  \n\t"
        "lwc1    %[temp4],      0(%[p_t_gain])                           \n\t"
        "sub.s   %[temp0],      %[temp8],      %[temp0]                  \n\t"
        PTR_ADDIU "%[p_ap_delay], %[p_ap_delay], 8                       \n\t"
        "sub.s   %[temp1],      %[temp9],      %[temp1]                  \n\t"
        PTR_ADDIU "%[p_t_gain], %[p_t_gain],   4                         \n\t"
        "madd.s  %[temp2],      %[temp2],      %[ag2],   %[temp0]        \n\t"
        PTR_ADDIU "%[p_delay],  %[p_delay],    8                         \n\t"
        "madd.s  %[temp3],      %[temp3],      %[ag2],   %[temp1]        \n\t"
        PTR_ADDIU "%[p_out],    %[p_out],      8                         \n\t"
        "mul.s   %[temp5],      %[temp4],      %[temp0]                  \n\t"
        "mul.s   %[temp6],      %[temp4],      %[temp1]                  \n\t"
        "swc1    %[temp2],      624(%[p_ap_delay])                       \n\t"
        "swc1    %[temp3],      628(%[p_ap_delay])                       \n\t"
        "swc1    %[temp5],      -8(%[p_out])                             \n\t"
        "swc1    %[temp6],      -4(%[p_out])                             \n\t"
        "bne     %[p_delay],    %[p_delay_end],1b                        \n\t"
        " swc1   %[temp6],      -4(%[p_out])                             \n\t"
        ".set    pop                                                     \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
          [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
          [temp6]"=&f"(temp6), [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
          [temp9]"=&f"(temp9), [p_delay]"+r"(p_delay), [p_ap_delay]"+r"(p_ap_delay),
          [p_Q_fract]"+r"(p_Q_fract), [p_t_gain]"+r"(p_t_gain), [p_out]"+r"(p_out),
          [ag0]"=&f"(ag0), [ag1]"=&f"(ag1), [ag2]"=&f"(ag2)
        : [phi_fract0]"f"(phi_fract0), [phi_fract1]"f"(phi_fract1),
          [p_delay_end]"r"(p_delay_end), [g_decay_slope]"f"(g_decay_slope)
        : "memory"
    );
}

static void ps_stereo_interpolate_mips(float (*l)[2], float (*r)[2],
                                    float h[2][4], float h_step[2][4],
                                    int len)
{
    float h0 = h[0][0];
    float h1 = h[0][1];
    float h2 = h[0][2];
    float h3 = h[0][3];
    float hs0 = h_step[0][0];
    float hs1 = h_step[0][1];
    float hs2 = h_step[0][2];
    float hs3 = h_step[0][3];
    float temp0, temp1, temp2, temp3;
    float l_re, l_im, r_re, r_im;

    float *l_end = ((float *)l + (len << 1));

    __asm__ volatile(
        ".set    push                                     \n\t"
        ".set    noreorder                                \n\t"
    "1:                                                   \n\t"
        "add.s   %[h0],     %[h0],     %[hs0]             \n\t"
        "lwc1    %[l_re],   0(%[l])                       \n\t"
        "add.s   %[h1],     %[h1],     %[hs1]             \n\t"
        "lwc1    %[r_re],   0(%[r])                       \n\t"
        "add.s   %[h2],     %[h2],     %[hs2]             \n\t"
        "lwc1    %[l_im],   4(%[l])                       \n\t"
        "add.s   %[h3],     %[h3],     %[hs3]             \n\t"
        "lwc1    %[r_im],   4(%[r])                       \n\t"
        "mul.s   %[temp0],  %[h0],     %[l_re]            \n\t"
        PTR_ADDIU "%[l],    %[l],      8                  \n\t"
        "mul.s   %[temp2],  %[h1],     %[l_re]            \n\t"
        PTR_ADDIU "%[r],    %[r],      8                  \n\t"
        "madd.s  %[temp0],  %[temp0],  %[h2],   %[r_re]   \n\t"
        "madd.s  %[temp2],  %[temp2],  %[h3],   %[r_re]   \n\t"
        "mul.s   %[temp1],  %[h0],     %[l_im]            \n\t"
        "mul.s   %[temp3],  %[h1],     %[l_im]            \n\t"
        "madd.s  %[temp1],  %[temp1],  %[h2],   %[r_im]   \n\t"
        "madd.s  %[temp3],  %[temp3],  %[h3],   %[r_im]   \n\t"
        "swc1    %[temp0],  -8(%[l])                      \n\t"
        "swc1    %[temp2],  -8(%[r])                      \n\t"
        "swc1    %[temp1],  -4(%[l])                      \n\t"
        "bne     %[l],      %[l_end],  1b                 \n\t"
        " swc1   %[temp3],  -4(%[r])                      \n\t"
        ".set    pop                                      \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
          [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
          [h0]"+f"(h0), [h1]"+f"(h1), [h2]"+f"(h2),
          [h3]"+f"(h3), [l]"+r"(l), [r]"+r"(r),
          [l_re]"=&f"(l_re), [l_im]"=&f"(l_im),
          [r_re]"=&f"(r_re), [r_im]"=&f"(r_im)
        : [hs0]"f"(hs0), [hs1]"f"(hs1), [hs2]"f"(hs2),
          [hs3]"f"(hs3), [l_end]"r"(l_end)
        : "memory"
    );
}
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */

void ff_psdsp_init_mips(PSDSPContext *s)
{
#if HAVE_INLINE_ASM
    s->hybrid_analysis_ileave = ps_hybrid_analysis_ileave_mips;
    s->hybrid_synthesis_deint = ps_hybrid_synthesis_deint_mips;
#if HAVE_MIPSFPU
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
    s->add_squares            = ps_add_squares_mips;
    s->mul_pair_single        = ps_mul_pair_single_mips;
    s->decorrelate            = ps_decorrelate_mips;
    s->stereo_interpolate[0]  = ps_stereo_interpolate_mips;
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */
}
