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
 * Authors:  Djordje Pesut   (djordje@mips.com)
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
 * Reference: libavcodec/aacsbr.c
 */

#ifndef AVCODEC_MIPS_AACSBR_FLOAT_H
#define AVCODEC_MIPS_AACSBR_FLOAT_H

#include "libavcodec/aac.h"
#include "libavcodec/sbr.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM
static void sbr_qmf_analysis_mips(AVFloatDSPContext *fdsp, FFTContext *mdct,
                             SBRDSPContext *sbrdsp, const float *in, float *x,
                             float z[320], float W[2][32][32][2], int buf_idx)
{
    int i;
    float *w0;
    float *w1;
    int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;

    w0 = x;
    w1 = x + 1024;
    for(i = 0; i < 36; i++)
    {
        /* loop unrolled 8 times */
        __asm__ volatile(
            "lw      %[temp0],   0(%[w1])         \n\t"
            "lw      %[temp1],   4(%[w1])         \n\t"
            "lw      %[temp2],   8(%[w1])         \n\t"
            "lw      %[temp3],   12(%[w1])        \n\t"
            "lw      %[temp4],   16(%[w1])        \n\t"
            "lw      %[temp5],   20(%[w1])        \n\t"
            "lw      %[temp6],   24(%[w1])        \n\t"
            "lw      %[temp7],   28(%[w1])        \n\t"
            "sw      %[temp0],   0(%[w0])         \n\t"
            "sw      %[temp1],   4(%[w0])         \n\t"
            "sw      %[temp2],   8(%[w0])         \n\t"
            "sw      %[temp3],   12(%[w0])        \n\t"
            "sw      %[temp4],   16(%[w0])        \n\t"
            "sw      %[temp5],   20(%[w0])        \n\t"
            "sw      %[temp6],   24(%[w0])        \n\t"
            "sw      %[temp7],   28(%[w0])        \n\t"
            PTR_ADDIU " %[w0],      %[w0],     32 \n\t"
            PTR_ADDIU " %[w1],      %[w1],     32 \n\t"

            : [w0]"+r"(w0), [w1]"+r"(w1),
              [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7)
            :
            : "memory"
        );
    }

    w0 = x + 288;
    w1 = (float*)in;
    for(i = 0; i < 128; i++)
    {
        /* loop unrolled 8 times */
        __asm__ volatile(
            "lw       %[temp0],    0(%[w1])        \n\t"
            "lw       %[temp1],    4(%[w1])        \n\t"
            "lw       %[temp2],    8(%[w1])        \n\t"
            "lw       %[temp3],    12(%[w1])       \n\t"
            "lw       %[temp4],    16(%[w1])       \n\t"
            "lw       %[temp5],    20(%[w1])       \n\t"
            "lw       %[temp6],    24(%[w1])       \n\t"
            "lw       %[temp7],    28(%[w1])       \n\t"
            "sw       %[temp0],    0(%[w0])        \n\t"
            "sw       %[temp1],    4(%[w0])        \n\t"
            "sw       %[temp2],    8(%[w0])        \n\t"
            "sw       %[temp3],    12(%[w0])       \n\t"
            "sw       %[temp4],    16(%[w0])       \n\t"
            "sw       %[temp5],    20(%[w0])       \n\t"
            "sw       %[temp6],    24(%[w0])       \n\t"
            "sw       %[temp7],    28(%[w0])       \n\t"
            PTR_ADDIU "  %[w0],       %[w0],    32 \n\t"
            PTR_ADDIU "  %[w1],       %[w1],    32 \n\t"

            : [w0]"+r"(w0), [w1]"+r"(w1),
              [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7)
            :
            : "memory"
        );
    }

    for (i = 0; i < 32; i++) { // numTimeSlots*RATE = 16*2 as 960 sample frames
                               // are not supported
        fdsp->vector_fmul_reverse(z, sbr_qmf_window_ds, x, 320);
        sbrdsp->sum64x5(z);
        sbrdsp->qmf_pre_shuffle(z);
        mdct->imdct_half(mdct, z, z+64);
        sbrdsp->qmf_post_shuffle(W[buf_idx][i], z);
        x += 32;
    }
}

#if (HAVE_MIPSFPU && !HAVE_LOONGSON3)
static void sbr_qmf_synthesis_mips(FFTContext *mdct,
                              SBRDSPContext *sbrdsp, AVFloatDSPContext *fdsp,
                              float *out, float X[2][38][64],
                              float mdct_buf[2][64],
                              float *v0, int *v_off, const unsigned int div)
{
    int i, n;
    const float *sbr_qmf_window = div ? sbr_qmf_window_ds : sbr_qmf_window_us;
    const int step = 128 >> div;
    float *v;
    float temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7, temp8, temp9, temp10, temp11, temp12, temp13;
    float temp14, temp15, temp16, temp17, temp18, temp19;
    float *vv0, *s0, *dst;
    dst = out;

    for (i = 0; i < 32; i++) {
        if (*v_off < step) {
            int saved_samples = (1280 - 128) >> div;
            memcpy(&v0[SBR_SYNTHESIS_BUF_SIZE - saved_samples], v0, saved_samples * sizeof(float));
            *v_off = SBR_SYNTHESIS_BUF_SIZE - saved_samples - step;
        } else {
            *v_off -= step;
        }
        v = v0 + *v_off;
        if (div) {
            for (n = 0; n < 32; n++) {
                X[0][i][   n] = -X[0][i][n];
                X[0][i][32+n] =  X[1][i][31-n];
            }
            mdct->imdct_half(mdct, mdct_buf[0], X[0][i]);
            sbrdsp->qmf_deint_neg(v, mdct_buf[0]);
        } else {
            sbrdsp->neg_odd_64(X[1][i]);
            mdct->imdct_half(mdct, mdct_buf[0], X[0][i]);
            mdct->imdct_half(mdct, mdct_buf[1], X[1][i]);
            sbrdsp->qmf_deint_bfly(v, mdct_buf[1], mdct_buf[0]);
        }

        if(div == 0)
        {
            float *v0_end;
            vv0 = v;
            v0_end = v + 60;
            s0 = (float*)sbr_qmf_window;

            /* 10 calls of function vector_fmul_add merged into one loop
               and loop unrolled 4 times */
            __asm__ volatile(
                ".set    push                                           \n\t"
                ".set    noreorder                                      \n\t"
                "lwc1    %[temp4],   0(%[v0])                           \n\t"
                "lwc1    %[temp5],   0(%[s0])                           \n\t"
                "lwc1    %[temp6],   4(%[v0])                           \n\t"
                "lwc1    %[temp7],   4(%[s0])                           \n\t"
                "lwc1    %[temp8],   8(%[v0])                           \n\t"
                "lwc1    %[temp9],   8(%[s0])                           \n\t"
                "lwc1    %[temp10],  12(%[v0])                          \n\t"
                "lwc1    %[temp11],  12(%[s0])                          \n\t"
                "lwc1    %[temp12],  768(%[v0])                         \n\t"
                "lwc1    %[temp13],  256(%[s0])                         \n\t"
                "lwc1    %[temp14],  772(%[v0])                         \n\t"
                "lwc1    %[temp15],  260(%[s0])                         \n\t"
                "lwc1    %[temp16],  776(%[v0])                         \n\t"
                "lwc1    %[temp17],  264(%[s0])                         \n\t"
                "lwc1    %[temp18],  780(%[v0])                         \n\t"
                "lwc1    %[temp19],  268(%[s0])                         \n\t"
            "1:                                                         \n\t"
                "mul.s   %[temp0],   %[temp4],   %[temp5]               \n\t"
                "lwc1    %[temp4],   1024(%[v0])                        \n\t"
                "mul.s   %[temp1],   %[temp6],   %[temp7]               \n\t"
                "lwc1    %[temp5],   512(%[s0])                         \n\t"
                "mul.s   %[temp2],   %[temp8],   %[temp9]               \n\t"
                "lwc1    %[temp6],   1028(%[v0])                        \n\t"
                "mul.s   %[temp3],   %[temp10],  %[temp11]              \n\t"
                "lwc1    %[temp7],   516(%[s0])                         \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   1032(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   520(%[s0])                         \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  1036(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  524(%[s0])                         \n\t"
                "lwc1    %[temp12],  1792(%[v0])                        \n\t"
                "lwc1    %[temp13],  768(%[s0])                         \n\t"
                "lwc1    %[temp14],  1796(%[v0])                        \n\t"
                "lwc1    %[temp15],  772(%[s0])                         \n\t"
                "lwc1    %[temp16],  1800(%[v0])                        \n\t"
                "lwc1    %[temp17],  776(%[s0])                         \n\t"
                "lwc1    %[temp18],  1804(%[v0])                        \n\t"
                "lwc1    %[temp19],  780(%[s0])                         \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   2048(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   1024(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   2052(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   1028(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   2056(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   1032(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  2060(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  1036(%[s0])                        \n\t"
                "lwc1    %[temp12],  2816(%[v0])                        \n\t"
                "lwc1    %[temp13],  1280(%[s0])                        \n\t"
                "lwc1    %[temp14],  2820(%[v0])                        \n\t"
                "lwc1    %[temp15],  1284(%[s0])                        \n\t"
                "lwc1    %[temp16],  2824(%[v0])                        \n\t"
                "lwc1    %[temp17],  1288(%[s0])                        \n\t"
                "lwc1    %[temp18],  2828(%[v0])                        \n\t"
                "lwc1    %[temp19],  1292(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   3072(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   1536(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   3076(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   1540(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   3080(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   1544(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  3084(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  1548(%[s0])                        \n\t"
                "lwc1    %[temp12],  3840(%[v0])                        \n\t"
                "lwc1    %[temp13],  1792(%[s0])                        \n\t"
                "lwc1    %[temp14],  3844(%[v0])                        \n\t"
                "lwc1    %[temp15],  1796(%[s0])                        \n\t"
                "lwc1    %[temp16],  3848(%[v0])                        \n\t"
                "lwc1    %[temp17],  1800(%[s0])                        \n\t"
                "lwc1    %[temp18],  3852(%[v0])                        \n\t"
                "lwc1    %[temp19],  1804(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   4096(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   2048(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   4100(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   2052(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   4104(%[v0])                        \n\t"
                PTR_ADDIU "%[dst],     %[dst],      16                  \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   2056(%[s0])                        \n\t"
                PTR_ADDIU " %[s0],      %[s0],      16                  \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  4108(%[v0])                        \n\t"
                PTR_ADDIU " %[v0],      %[v0],      16                  \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  2044(%[s0])                        \n\t"
                "lwc1    %[temp12],  4848(%[v0])                        \n\t"
                "lwc1    %[temp13],  2288(%[s0])                        \n\t"
                "lwc1    %[temp14],  4852(%[v0])                        \n\t"
                "lwc1    %[temp15],  2292(%[s0])                        \n\t"
                "lwc1    %[temp16],  4856(%[v0])                        \n\t"
                "lwc1    %[temp17],  2296(%[s0])                        \n\t"
                "lwc1    %[temp18],  4860(%[v0])                        \n\t"
                "lwc1    %[temp19],  2300(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   0(%[v0])                           \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   0(%[s0])                           \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   4(%[v0])                           \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   4(%[s0])                           \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   8(%[v0])                           \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   8(%[s0])                           \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  12(%[v0])                          \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  12(%[s0])                          \n\t"
                "lwc1    %[temp12],  768(%[v0])                         \n\t"
                "lwc1    %[temp13],  256(%[s0])                         \n\t"
                "lwc1    %[temp14],  772(%[v0])                         \n\t"
                "lwc1    %[temp15],  260(%[s0])                         \n\t"
                "lwc1    %[temp16],  776(%[v0])                         \n\t"
                "lwc1    %[temp17],  264(%[s0])                         \n\t"
                "lwc1    %[temp18],  780(%[v0])                         \n\t"
                "lwc1    %[temp19],  268(%[s0])                         \n\t"
                "swc1    %[temp0],   -16(%[dst])                        \n\t"
                "swc1    %[temp1],   -12(%[dst])                        \n\t"
                "swc1    %[temp2],   -8(%[dst])                         \n\t"
                "bne     %[v0],      %[v0_end],  1b                     \n\t"
                " swc1   %[temp3],   -4(%[dst])                         \n\t"
                "mul.s   %[temp0],   %[temp4],   %[temp5]               \n\t"
                "lwc1    %[temp4],   1024(%[v0])                        \n\t"
                "mul.s   %[temp1],   %[temp6],   %[temp7]               \n\t"
                "lwc1    %[temp5],   512(%[s0])                         \n\t"
                "mul.s   %[temp2],   %[temp8],   %[temp9]               \n\t"
                "lwc1    %[temp6],   1028(%[v0])                        \n\t"
                "mul.s   %[temp3],   %[temp10],  %[temp11]              \n\t"
                "lwc1    %[temp7],   516(%[s0])                         \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   1032(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   520(%[s0])                         \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  1036(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  524(%[s0])                         \n\t"
                "lwc1    %[temp12],  1792(%[v0])                        \n\t"
                "lwc1    %[temp13],  768(%[s0])                         \n\t"
                "lwc1    %[temp14],  1796(%[v0])                        \n\t"
                "lwc1    %[temp15],  772(%[s0])                         \n\t"
                "lwc1    %[temp16],  1800(%[v0])                        \n\t"
                "lwc1    %[temp17],  776(%[s0])                         \n\t"
                "lwc1    %[temp18],  1804(%[v0])                        \n\t"
                "lwc1    %[temp19],  780(%[s0])                         \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   2048(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   1024(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   2052(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   1028(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   2056(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   1032(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  2060(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  1036(%[s0])                        \n\t"
                "lwc1    %[temp12],  2816(%[v0])                        \n\t"
                "lwc1    %[temp13],  1280(%[s0])                        \n\t"
                "lwc1    %[temp14],  2820(%[v0])                        \n\t"
                "lwc1    %[temp15],  1284(%[s0])                        \n\t"
                "lwc1    %[temp16],  2824(%[v0])                        \n\t"
                "lwc1    %[temp17],  1288(%[s0])                        \n\t"
                "lwc1    %[temp18],  2828(%[v0])                        \n\t"
                "lwc1    %[temp19],  1292(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   3072(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   1536(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   3076(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   1540(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   3080(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   1544(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  3084(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  1548(%[s0])                        \n\t"
                "lwc1    %[temp12],  3840(%[v0])                        \n\t"
                "lwc1    %[temp13],  1792(%[s0])                        \n\t"
                "lwc1    %[temp14],  3844(%[v0])                        \n\t"
                "lwc1    %[temp15],  1796(%[s0])                        \n\t"
                "lwc1    %[temp16],  3848(%[v0])                        \n\t"
                "lwc1    %[temp17],  1800(%[s0])                        \n\t"
                "lwc1    %[temp18],  3852(%[v0])                        \n\t"
                "lwc1    %[temp19],  1804(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp4],   4096(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp5],   2048(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp6],   4100(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp7],   2052(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                "lwc1    %[temp8],   4104(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "lwc1    %[temp9],   2056(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "lwc1    %[temp10],  4108(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "lwc1    %[temp11],  2060(%[s0])                        \n\t"
                "lwc1    %[temp12],  4864(%[v0])                        \n\t"
                "lwc1    %[temp13],  2304(%[s0])                        \n\t"
                "lwc1    %[temp14],  4868(%[v0])                        \n\t"
                "lwc1    %[temp15],  2308(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp4],   %[temp5]   \n\t"
                "lwc1    %[temp16],  4872(%[v0])                        \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp6],   %[temp7]   \n\t"
                "lwc1    %[temp17],  2312(%[s0])                        \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp8],   %[temp9]   \n\t"
                "lwc1    %[temp18],  4876(%[v0])                        \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp10],  %[temp11]  \n\t"
                "lwc1    %[temp19],  2316(%[s0])                        \n\t"
                "madd.s  %[temp0],   %[temp0],   %[temp12],  %[temp13]  \n\t"
                PTR_ADDIU "%[dst],     %[dst],     16                   \n\t"
                "madd.s  %[temp1],   %[temp1],   %[temp14],  %[temp15]  \n\t"
                "madd.s  %[temp2],   %[temp2],   %[temp16],  %[temp17]  \n\t"
                "madd.s  %[temp3],   %[temp3],   %[temp18],  %[temp19]  \n\t"
                "swc1    %[temp0],   -16(%[dst])                        \n\t"
                "swc1    %[temp1],   -12(%[dst])                        \n\t"
                "swc1    %[temp2],   -8(%[dst])                         \n\t"
                "swc1    %[temp3],   -4(%[dst])                         \n\t"
                ".set    pop                                            \n\t"

                : [dst]"+r"(dst), [v0]"+r"(vv0), [s0]"+r"(s0),
                  [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
                  [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
                  [temp6]"=&f"(temp6), [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
                  [temp9]"=&f"(temp9), [temp10]"=&f"(temp10), [temp11]"=&f"(temp11),
                  [temp12]"=&f"(temp12), [temp13]"=&f"(temp13), [temp14]"=&f"(temp14),
                  [temp15]"=&f"(temp15), [temp16]"=&f"(temp16), [temp17]"=&f"(temp17),
                  [temp18]"=&f"(temp18), [temp19]"=&f"(temp19)
                : [v0_end]"r"(v0_end)
                : "memory"
            );
        }
        else
        {
            fdsp->vector_fmul   (out, v                , sbr_qmf_window                       , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 192 >> div), sbr_qmf_window + ( 64 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 256 >> div), sbr_qmf_window + (128 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 448 >> div), sbr_qmf_window + (192 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 512 >> div), sbr_qmf_window + (256 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 704 >> div), sbr_qmf_window + (320 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 768 >> div), sbr_qmf_window + (384 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + ( 960 >> div), sbr_qmf_window + (448 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + (1024 >> div), sbr_qmf_window + (512 >> div), out   , 64 >> div);
            fdsp->vector_fmul_add(out, v + (1216 >> div), sbr_qmf_window + (576 >> div), out   , 64 >> div);
            out += 64 >> div;
        }
    }
}

#define sbr_qmf_analysis sbr_qmf_analysis_mips
#define sbr_qmf_synthesis sbr_qmf_synthesis_mips

#endif /* (HAVE_MIPSFPU && !HAVE_LOONGSON3) */
#endif /* HAVE_INLINE_ASM */

#endif /* AVCODEC_MIPS_AACSBR_FLOAT_H */
