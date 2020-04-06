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

#include "libavcodec/aac.h"
#include "libavcodec/aacsbr.h"
#include "libavutil/mips/asmdefs.h"

#define ENVELOPE_ADJUSTMENT_OFFSET 2

#if HAVE_INLINE_ASM
#if HAVE_MIPSFPU
static int sbr_lf_gen_mips(AACContext *ac, SpectralBandReplication *sbr,
                      float X_low[32][40][2], const float W[2][32][32][2],
                      int buf_idx)
{
    int i, k;
    int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
    float *p_x_low = &X_low[0][8][0];
    float *p_w = (float*)&W[buf_idx][0][0][0];
    float *p_x1_low = &X_low[0][0][0];
    float *p_w1 = (float*)&W[1-buf_idx][24][0][0];

    float *loop_end=p_x1_low + 2560;

    /* loop unrolled 8 times */
    __asm__ volatile (
    "1:                                                 \n\t"
        "sw     $0,            0(%[p_x1_low])           \n\t"
        "sw     $0,            4(%[p_x1_low])           \n\t"
        "sw     $0,            8(%[p_x1_low])           \n\t"
        "sw     $0,            12(%[p_x1_low])          \n\t"
        "sw     $0,            16(%[p_x1_low])          \n\t"
        "sw     $0,            20(%[p_x1_low])          \n\t"
        "sw     $0,            24(%[p_x1_low])          \n\t"
        "sw     $0,            28(%[p_x1_low])          \n\t"
        PTR_ADDIU "%[p_x1_low],%[p_x1_low],      32     \n\t"
        "bne    %[p_x1_low],   %[loop_end],      1b     \n\t"
        PTR_ADDIU "%[p_x1_low],%[p_x1_low],      -10240 \n\t"

        : [p_x1_low]"+r"(p_x1_low)
        : [loop_end]"r"(loop_end)
        : "memory"
    );

    for (k = 0; k < sbr->kx[1]; k++) {
        for (i = 0; i < 32; i+=4) {
            /* loop unrolled 4 times */
            __asm__ volatile (
                "lw     %[temp0],   0(%[p_w])               \n\t"
                "lw     %[temp1],   4(%[p_w])               \n\t"
                "lw     %[temp2],   256(%[p_w])             \n\t"
                "lw     %[temp3],   260(%[p_w])             \n\t"
                "lw     %[temp4],   512(%[p_w])             \n\t"
                "lw     %[temp5],   516(%[p_w])             \n\t"
                "lw     %[temp6],   768(%[p_w])             \n\t"
                "lw     %[temp7],   772(%[p_w])             \n\t"
                "sw     %[temp0],   0(%[p_x_low])           \n\t"
                "sw     %[temp1],   4(%[p_x_low])           \n\t"
                "sw     %[temp2],   8(%[p_x_low])           \n\t"
                "sw     %[temp3],   12(%[p_x_low])          \n\t"
                "sw     %[temp4],   16(%[p_x_low])          \n\t"
                "sw     %[temp5],   20(%[p_x_low])          \n\t"
                "sw     %[temp6],   24(%[p_x_low])          \n\t"
                "sw     %[temp7],   28(%[p_x_low])          \n\t"
                PTR_ADDIU "%[p_x_low], %[p_x_low],  32      \n\t"
                PTR_ADDIU "%[p_w],     %[p_w],      1024    \n\t"

                : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                  [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                  [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                  [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                  [p_w]"+r"(p_w), [p_x_low]"+r"(p_x_low)
                :
                : "memory"
            );
        }
        p_x_low += 16;
        p_w -= 2046;
    }

    for (k = 0; k < sbr->kx[0]; k++) {
        for (i = 0; i < 2; i++) {

            /* loop unrolled 4 times */
            __asm__ volatile (
                "lw     %[temp0],    0(%[p_w1])             \n\t"
                "lw     %[temp1],    4(%[p_w1])             \n\t"
                "lw     %[temp2],    256(%[p_w1])           \n\t"
                "lw     %[temp3],    260(%[p_w1])           \n\t"
                "lw     %[temp4],    512(%[p_w1])           \n\t"
                "lw     %[temp5],    516(%[p_w1])           \n\t"
                "lw     %[temp6],    768(%[p_w1])           \n\t"
                "lw     %[temp7],    772(%[p_w1])           \n\t"
                "sw     %[temp0],    0(%[p_x1_low])         \n\t"
                "sw     %[temp1],    4(%[p_x1_low])         \n\t"
                "sw     %[temp2],    8(%[p_x1_low])         \n\t"
                "sw     %[temp3],    12(%[p_x1_low])        \n\t"
                "sw     %[temp4],    16(%[p_x1_low])        \n\t"
                "sw     %[temp5],    20(%[p_x1_low])        \n\t"
                "sw     %[temp6],    24(%[p_x1_low])        \n\t"
                "sw     %[temp7],    28(%[p_x1_low])        \n\t"
                PTR_ADDIU "%[p_x1_low], %[p_x1_low], 32     \n\t"
                PTR_ADDIU "%[p_w1],     %[p_w1],     1024   \n\t"

                : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                  [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                  [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                  [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                  [p_w1]"+r"(p_w1), [p_x1_low]"+r"(p_x1_low)
                :
                : "memory"
            );
        }
        p_x1_low += 64;
        p_w1 -= 510;
    }
    return 0;
}

static int sbr_x_gen_mips(SpectralBandReplication *sbr, float X[2][38][64],
                     const float Y0[38][64][2], const float Y1[38][64][2],
                     const float X_low[32][40][2], int ch)
{
    int k, i;
    const int i_f = 32;
    int temp0, temp1, temp2, temp3;
    const float *X_low1, *Y01, *Y11;
    float *x1=&X[0][0][0];
    float *j=x1+4864;
    const int i_Temp = FFMAX(2*sbr->data[ch].t_env_num_env_old - i_f, 0);

    /* loop unrolled 8 times */
    __asm__ volatile (
    "1:                                       \n\t"
        "sw     $0,      0(%[x1])             \n\t"
        "sw     $0,      4(%[x1])             \n\t"
        "sw     $0,      8(%[x1])             \n\t"
        "sw     $0,      12(%[x1])            \n\t"
        "sw     $0,      16(%[x1])            \n\t"
        "sw     $0,      20(%[x1])            \n\t"
        "sw     $0,      24(%[x1])            \n\t"
        "sw     $0,      28(%[x1])            \n\t"
        PTR_ADDIU "%[x1],%[x1],      32       \n\t"
        "bne    %[x1],   %[j],       1b       \n\t"
        PTR_ADDIU "%[x1],%[x1],      -19456   \n\t"

        : [x1]"+r"(x1)
        : [j]"r"(j)
        : "memory"
    );

    if (i_Temp != 0) {

        X_low1=&X_low[0][2][0];

        for (k = 0; k < sbr->kx[0]; k++) {

            __asm__ volatile (
                "move    %[i],        $zero                  \n\t"
            "2:                                              \n\t"
                "lw      %[temp0],    0(%[X_low1])           \n\t"
                "lw      %[temp1],    4(%[X_low1])           \n\t"
                "sw      %[temp0],    0(%[x1])               \n\t"
                "sw      %[temp1],    9728(%[x1])            \n\t"
                PTR_ADDIU "%[x1],     %[x1],         256     \n\t"
                PTR_ADDIU "%[X_low1], %[X_low1],     8       \n\t"
                "addiu   %[i],        %[i],          1       \n\t"
                "bne     %[i],        %[i_Temp],     2b      \n\t"

                : [x1]"+r"(x1), [X_low1]"+r"(X_low1), [i]"=&r"(i),
                  [temp0]"=&r"(temp0), [temp1]"=&r"(temp1)
                : [i_Temp]"r"(i_Temp)
                : "memory"
            );
            x1-=(i_Temp<<6)-1;
            X_low1-=(i_Temp<<1)-80;
        }

        x1=&X[0][0][k];
        Y01=(float*)&Y0[32][k][0];

        for (; k < sbr->kx[0] + sbr->m[0]; k++) {
            __asm__ volatile (
                "move    %[i],       $zero               \n\t"
            "3:                                          \n\t"
                "lw      %[temp0],   0(%[Y01])           \n\t"
                "lw      %[temp1],   4(%[Y01])           \n\t"
                "sw      %[temp0],   0(%[x1])            \n\t"
                "sw      %[temp1],   9728(%[x1])         \n\t"
                PTR_ADDIU "%[x1],    %[x1],      256     \n\t"
                PTR_ADDIU "%[Y01],   %[Y01],     512     \n\t"
                "addiu   %[i],       %[i],       1       \n\t"
                "bne     %[i],       %[i_Temp],  3b      \n\t"

                : [x1]"+r"(x1), [Y01]"+r"(Y01), [i]"=&r"(i),
                  [temp0]"=&r"(temp0), [temp1]"=&r"(temp1)
                : [i_Temp]"r"(i_Temp)
                : "memory"
            );
            x1 -=(i_Temp<<6)-1;
            Y01 -=(i_Temp<<7)-2;
        }
    }

    x1=&X[0][i_Temp][0];
    X_low1=&X_low[0][i_Temp+2][0];
    temp3=38;

    for (k = 0; k < sbr->kx[1]; k++) {

        __asm__ volatile (
            "move    %[i],       %[i_Temp]              \n\t"
        "4:                                             \n\t"
            "lw      %[temp0],   0(%[X_low1])           \n\t"
            "lw      %[temp1],   4(%[X_low1])           \n\t"
            "sw      %[temp0],   0(%[x1])               \n\t"
            "sw      %[temp1],   9728(%[x1])            \n\t"
            PTR_ADDIU "%[x1],    %[x1],         256     \n\t"
            PTR_ADDIU "%[X_low1],%[X_low1],     8       \n\t"
            "addiu   %[i],       %[i],          1       \n\t"
            "bne     %[i],       %[temp3],      4b      \n\t"

            : [x1]"+r"(x1), [X_low1]"+r"(X_low1), [i]"=&r"(i),
              [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2)
            : [i_Temp]"r"(i_Temp), [temp3]"r"(temp3)
            : "memory"
        );
        x1 -= ((38-i_Temp)<<6)-1;
        X_low1 -= ((38-i_Temp)<<1)- 80;
    }

    x1=&X[0][i_Temp][k];
    Y11=&Y1[i_Temp][k][0];
    temp2=32;

    for (; k < sbr->kx[1] + sbr->m[1]; k++) {

        __asm__ volatile (
           "move    %[i],       %[i_Temp]               \n\t"
        "5:                                             \n\t"
           "lw      %[temp0],   0(%[Y11])               \n\t"
           "lw      %[temp1],   4(%[Y11])               \n\t"
           "sw      %[temp0],   0(%[x1])                \n\t"
           "sw      %[temp1],   9728(%[x1])             \n\t"
           PTR_ADDIU "%[x1],    %[x1],          256     \n\t"
           PTR_ADDIU "%[Y11],   %[Y11],         512     \n\t"
           "addiu   %[i],       %[i],           1       \n\t"
           "bne     %[i],       %[temp2],       5b      \n\t"

           : [x1]"+r"(x1), [Y11]"+r"(Y11), [i]"=&r"(i),
             [temp0]"=&r"(temp0), [temp1]"=&r"(temp1)
           : [i_Temp]"r"(i_Temp), [temp3]"r"(temp3),
             [temp2]"r"(temp2)
           : "memory"
        );

        x1 -= ((32-i_Temp)<<6)-1;
        Y11 -= ((32-i_Temp)<<7)-2;
   }
      return 0;
}

#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
static void sbr_hf_assemble_mips(float Y1[38][64][2],
                            const float X_high[64][40][2],
                            SpectralBandReplication *sbr, SBRData *ch_data,
                            const int e_a[2])
{
    int e, i, j, m;
    const int h_SL = 4 * !sbr->bs_smoothing_mode;
    const int kx = sbr->kx[1];
    const int m_max = sbr->m[1];
    static const float h_smooth[5] = {
        0.33333333333333,
        0.30150283239582,
        0.21816949906249,
        0.11516383427084,
        0.03183050093751,
    };

    float (*g_temp)[48] = ch_data->g_temp, (*q_temp)[48] = ch_data->q_temp;
    int indexnoise = ch_data->f_indexnoise;
    int indexsine  = ch_data->f_indexsine;
    float *g_temp1, *q_temp1, *pok, *pok1;
    float temp1, temp2, temp3, temp4;
    int size = m_max;

    if (sbr->reset) {
        for (i = 0; i < h_SL; i++) {
            memcpy(g_temp[i + 2*ch_data->t_env[0]], sbr->gain[0], m_max * sizeof(sbr->gain[0][0]));
            memcpy(q_temp[i + 2*ch_data->t_env[0]], sbr->q_m[0],  m_max * sizeof(sbr->q_m[0][0]));
        }
    } else if (h_SL) {
        memcpy(g_temp[2*ch_data->t_env[0]], g_temp[2*ch_data->t_env_num_env_old], 4*sizeof(g_temp[0]));
        memcpy(q_temp[2*ch_data->t_env[0]], q_temp[2*ch_data->t_env_num_env_old], 4*sizeof(q_temp[0]));
    }

    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e + 1]; i++) {
            g_temp1 = g_temp[h_SL + i];
            pok = sbr->gain[e];
            q_temp1 = q_temp[h_SL + i];
            pok1 = sbr->q_m[e];

            /* loop unrolled 4 times */
            for (j=0; j<(size>>2); j++) {
                __asm__ volatile (
                    "lw      %[temp1],   0(%[pok])               \n\t"
                    "lw      %[temp2],   4(%[pok])               \n\t"
                    "lw      %[temp3],   8(%[pok])               \n\t"
                    "lw      %[temp4],   12(%[pok])              \n\t"
                    "sw      %[temp1],   0(%[g_temp1])           \n\t"
                    "sw      %[temp2],   4(%[g_temp1])           \n\t"
                    "sw      %[temp3],   8(%[g_temp1])           \n\t"
                    "sw      %[temp4],   12(%[g_temp1])          \n\t"
                    "lw      %[temp1],   0(%[pok1])              \n\t"
                    "lw      %[temp2],   4(%[pok1])              \n\t"
                    "lw      %[temp3],   8(%[pok1])              \n\t"
                    "lw      %[temp4],   12(%[pok1])             \n\t"
                    "sw      %[temp1],   0(%[q_temp1])           \n\t"
                    "sw      %[temp2],   4(%[q_temp1])           \n\t"
                    "sw      %[temp3],   8(%[q_temp1])           \n\t"
                    "sw      %[temp4],   12(%[q_temp1])          \n\t"
                    PTR_ADDIU "%[pok],     %[pok],         16    \n\t"
                    PTR_ADDIU "%[g_temp1], %[g_temp1],     16    \n\t"
                    PTR_ADDIU "%[pok1],    %[pok1],        16    \n\t"
                    PTR_ADDIU "%[q_temp1], %[q_temp1],     16    \n\t"

                    : [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
                      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4),
                      [pok]"+r"(pok), [g_temp1]"+r"(g_temp1),
                      [pok1]"+r"(pok1), [q_temp1]"+r"(q_temp1)
                    :
                    : "memory"
                );
            }

            for (j=0; j<(size&3); j++) {
                __asm__ volatile (
                    "lw      %[temp1],   0(%[pok])              \n\t"
                    "lw      %[temp2],   0(%[pok1])             \n\t"
                    "sw      %[temp1],   0(%[g_temp1])          \n\t"
                    "sw      %[temp2],   0(%[q_temp1])          \n\t"
                    PTR_ADDIU "%[pok],     %[pok],        4     \n\t"
                    PTR_ADDIU "%[g_temp1], %[g_temp1],    4     \n\t"
                    PTR_ADDIU "%[pok1],    %[pok1],       4     \n\t"
                    PTR_ADDIU "%[q_temp1], %[q_temp1],    4     \n\t"

                    : [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
                      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4),
                      [pok]"+r"(pok), [g_temp1]"+r"(g_temp1),
                      [pok1]"+r"(pok1), [q_temp1]"+r"(q_temp1)
                    :
                    : "memory"
                );
            }
        }
    }

    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e + 1]; i++) {
            LOCAL_ALIGNED_16(float, g_filt_tab, [48]);
            LOCAL_ALIGNED_16(float, q_filt_tab, [48]);
            float *g_filt, *q_filt;

            if (h_SL && e != e_a[0] && e != e_a[1]) {
                g_filt = g_filt_tab;
                q_filt = q_filt_tab;

                for (m = 0; m < m_max; m++) {
                    const int idx1 = i + h_SL;
                    g_filt[m] = 0.0f;
                    q_filt[m] = 0.0f;

                    for (j = 0; j <= h_SL; j++) {
                        g_filt[m] += g_temp[idx1 - j][m] * h_smooth[j];
                        q_filt[m] += q_temp[idx1 - j][m] * h_smooth[j];
                    }
                }
            } else {
                g_filt = g_temp[i + h_SL];
                q_filt = q_temp[i];
            }

            sbr->dsp.hf_g_filt(Y1[i] + kx, X_high + kx, g_filt, m_max,
                               i + ENVELOPE_ADJUSTMENT_OFFSET);

            if (e != e_a[0] && e != e_a[1]) {
                sbr->dsp.hf_apply_noise[indexsine](Y1[i] + kx, sbr->s_m[e],
                                                   q_filt, indexnoise,
                                                   kx, m_max);
            } else {
                int idx = indexsine&1;
                int A = (1-((indexsine+(kx & 1))&2));
                int B = (A^(-idx)) + idx;
                float *out = &Y1[i][kx][idx];
                float *in  = sbr->s_m[e];
                float temp0, temp1, temp2, temp3, temp4, temp5;
                float A_f = (float)A;
                float B_f = (float)B;

                for (m = 0; m+1 < m_max; m+=2) {

                    temp2 = out[0];
                    temp3 = out[2];

                    __asm__ volatile(
                        "lwc1    %[temp0],  0(%[in])                     \n\t"
                        "lwc1    %[temp1],  4(%[in])                     \n\t"
                        "madd.s  %[temp4],  %[temp2],  %[temp0], %[A_f]  \n\t"
                        "madd.s  %[temp5],  %[temp3],  %[temp1], %[B_f]  \n\t"
                        "swc1    %[temp4],  0(%[out])                    \n\t"
                        "swc1    %[temp5],  8(%[out])                    \n\t"
                        PTR_ADDIU "%[in],   %[in],     8                 \n\t"
                        PTR_ADDIU "%[out],  %[out],    16                \n\t"

                        : [temp0]"=&f" (temp0), [temp1]"=&f"(temp1),
                          [temp4]"=&f" (temp4), [temp5]"=&f"(temp5),
                          [in]"+r"(in), [out]"+r"(out)
                        : [A_f]"f"(A_f), [B_f]"f"(B_f), [temp2]"f"(temp2),
                          [temp3]"f"(temp3)
                        : "memory"
                    );
                }
                if(m_max&1)
                    out[2*m  ] += in[m  ] * A;
            }
            indexnoise = (indexnoise + m_max) & 0x1ff;
            indexsine = (indexsine + 1) & 3;
        }
    }
    ch_data->f_indexnoise = indexnoise;
    ch_data->f_indexsine  = indexsine;
}

static void sbr_hf_inverse_filter_mips(SBRDSPContext *dsp,
                                  float (*alpha0)[2], float (*alpha1)[2],
                                  const float X_low[32][40][2], int k0)
{
    int k;
    float temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7, c;
    float *phi1, *alpha_1, *alpha_0, res1, res2, temp_real, temp_im;

    c = 1.000001f;

    for (k = 0; k < k0; k++) {
        LOCAL_ALIGNED_16(float, phi, [3], [2][2]);
        float dk;
        phi1 = &phi[0][0][0];
        alpha_1 = &alpha1[k][0];
        alpha_0 = &alpha0[k][0];
        dsp->autocorrelate(X_low[k], phi);

        __asm__ volatile (
            "lwc1    %[temp0],  40(%[phi1])                       \n\t"
            "lwc1    %[temp1],  16(%[phi1])                       \n\t"
            "lwc1    %[temp2],  24(%[phi1])                       \n\t"
            "lwc1    %[temp3],  28(%[phi1])                       \n\t"
            "mul.s   %[dk],     %[temp0],    %[temp1]             \n\t"
            "lwc1    %[temp4],  0(%[phi1])                        \n\t"
            "mul.s   %[res2],   %[temp2],    %[temp2]             \n\t"
            "lwc1    %[temp5],  4(%[phi1])                        \n\t"
            "madd.s  %[res2],   %[res2],     %[temp3],  %[temp3]  \n\t"
            "lwc1    %[temp6],  8(%[phi1])                        \n\t"
            "div.s   %[res2],   %[res2],     %[c]                 \n\t"
            "lwc1    %[temp0],  12(%[phi1])                       \n\t"
            "sub.s   %[dk],     %[dk],       %[res2]              \n\t"

            : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
              [temp6]"=&f"(temp6), [res2]"=&f"(res2), [dk]"=&f"(dk)
            : [phi1]"r"(phi1), [c]"f"(c)
            : "memory"
        );

        if (!dk) {
            alpha_1[0] = 0;
            alpha_1[1] = 0;
        } else {
            __asm__ volatile (
                "mul.s   %[temp_real], %[temp4],     %[temp2]            \n\t"
                "nmsub.s %[temp_real], %[temp_real], %[temp5], %[temp3]  \n\t"
                "nmsub.s %[temp_real], %[temp_real], %[temp6], %[temp1]  \n\t"
                "mul.s   %[temp_im],   %[temp4],     %[temp3]            \n\t"
                "madd.s  %[temp_im],   %[temp_im],   %[temp5], %[temp2]  \n\t"
                "nmsub.s %[temp_im],   %[temp_im],   %[temp0], %[temp1]  \n\t"
                "div.s   %[temp_real], %[temp_real], %[dk]               \n\t"
                "div.s   %[temp_im],   %[temp_im],   %[dk]               \n\t"
                "swc1    %[temp_real], 0(%[alpha_1])                     \n\t"
                "swc1    %[temp_im],   4(%[alpha_1])                     \n\t"

                : [temp_real]"=&f" (temp_real), [temp_im]"=&f"(temp_im)
                : [phi1]"r"(phi1), [temp0]"f"(temp0), [temp1]"f"(temp1),
                  [temp2]"f"(temp2), [temp3]"f"(temp3), [temp4]"f"(temp4),
                  [temp5]"f"(temp5), [temp6]"f"(temp6),
                  [alpha_1]"r"(alpha_1), [dk]"f"(dk)
                : "memory"
            );
        }

        if (!phi1[4]) {
            alpha_0[0] = 0;
            alpha_0[1] = 0;
        } else {
            __asm__ volatile (
                "lwc1    %[temp6],     0(%[alpha_1])                     \n\t"
                "lwc1    %[temp7],     4(%[alpha_1])                     \n\t"
                "mul.s   %[temp_real], %[temp6],     %[temp2]            \n\t"
                "add.s   %[temp_real], %[temp_real], %[temp4]            \n\t"
                "madd.s  %[temp_real], %[temp_real], %[temp7], %[temp3]  \n\t"
                "mul.s   %[temp_im],   %[temp7],     %[temp2]            \n\t"
                "add.s   %[temp_im],   %[temp_im],   %[temp5]            \n\t"
                "nmsub.s %[temp_im],   %[temp_im],   %[temp6], %[temp3]  \n\t"
                "div.s   %[temp_real], %[temp_real], %[temp1]            \n\t"
                "div.s   %[temp_im],   %[temp_im],   %[temp1]            \n\t"
                "neg.s   %[temp_real], %[temp_real]                      \n\t"
                "neg.s   %[temp_im],   %[temp_im]                        \n\t"
                "swc1    %[temp_real], 0(%[alpha_0])                     \n\t"
                "swc1    %[temp_im],   4(%[alpha_0])                     \n\t"

                : [temp_real]"=&f"(temp_real), [temp_im]"=&f"(temp_im),
                  [temp6]"=&f"(temp6), [temp7]"=&f"(temp7),
                  [res1]"=&f"(res1), [res2]"=&f"(res2)
                : [alpha_1]"r"(alpha_1), [alpha_0]"r"(alpha_0),
                  [temp0]"f"(temp0), [temp1]"f"(temp1), [temp2]"f"(temp2),
                  [temp3]"f"(temp3), [temp4]"f"(temp4), [temp5]"f"(temp5)
                : "memory"
            );
        }

        __asm__ volatile (
            "lwc1    %[temp1],      0(%[alpha_1])                           \n\t"
            "lwc1    %[temp2],      4(%[alpha_1])                           \n\t"
            "lwc1    %[temp_real],  0(%[alpha_0])                           \n\t"
            "lwc1    %[temp_im],    4(%[alpha_0])                           \n\t"
            "mul.s   %[res1],       %[temp1],      %[temp1]                 \n\t"
            "madd.s  %[res1],       %[res1],       %[temp2],    %[temp2]    \n\t"
            "mul.s   %[res2],       %[temp_real],  %[temp_real]             \n\t"
            "madd.s  %[res2],       %[res2],       %[temp_im],  %[temp_im]  \n\t"

            : [temp_real]"=&f"(temp_real), [temp_im]"=&f"(temp_im),
              [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [res1]"=&f"(res1), [res2]"=&f"(res2)
            : [alpha_1]"r"(alpha_1), [alpha_0]"r"(alpha_0)
            : "memory"
        );

        if (res1 >= 16.0f || res2 >= 16.0f) {
            alpha_1[0] = 0;
            alpha_1[1] = 0;
            alpha_0[0] = 0;
            alpha_0[1] = 0;
        }
    }
}
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */

void ff_aacsbr_func_ptr_init_mips(AACSBRContext *c)
{
#if HAVE_INLINE_ASM
#if HAVE_MIPSFPU
    c->sbr_lf_gen            = sbr_lf_gen_mips;
    c->sbr_x_gen             = sbr_x_gen_mips;
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
    c->sbr_hf_inverse_filter = sbr_hf_inverse_filter_mips;
    c->sbr_hf_assemble       = sbr_hf_assemble_mips;
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */
}
