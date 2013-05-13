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
 * Reference: libavcodec/aacdec.c
 */

#include "libavcodec/aac.h"
#include "aacdec_mips.h"
#include "libavcodec/aactab.h"
#include "libavcodec/sinewin.h"

#if HAVE_INLINE_ASM
static av_always_inline int lcg_random(unsigned previous_val)
{
    union { unsigned u; int s; } v = { previous_val * 1664525u + 1013904223 };
    return v.s;
}

static void imdct_and_windowing_mips(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    float *in    = sce->coeffs;
    float *out   = sce->ret;
    float *saved = sce->saved;
    const float *swindow      = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *lwindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *buf  = ac->buf_mdct;
    int i;

    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 1024; i += 128)
            ac->mdct_small.imdct_half(&ac->mdct_small, buf + i, in + i);
    } else
        ac->mdct.imdct_half(&ac->mdct, buf, in);

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */
    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
            (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp.vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 512);
    } else {
        {
            float *buf1 = saved;
            float *buf2 = out;
            int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
            int loop_end;

            /* loop unrolled 8 times */
            __asm__ volatile (
                ".set push                               \n\t"
                ".set noreorder                          \n\t"
                "addiu   %[loop_end], %[src],      1792  \n\t"
            "1:                                          \n\t"
                "lw      %[temp0],    0(%[src])          \n\t"
                "lw      %[temp1],    4(%[src])          \n\t"
                "lw      %[temp2],    8(%[src])          \n\t"
                "lw      %[temp3],    12(%[src])         \n\t"
                "lw      %[temp4],    16(%[src])         \n\t"
                "lw      %[temp5],    20(%[src])         \n\t"
                "lw      %[temp6],    24(%[src])         \n\t"
                "lw      %[temp7],    28(%[src])         \n\t"
                "addiu   %[src],      %[src],      32    \n\t"
                "sw      %[temp0],    0(%[dst])          \n\t"
                "sw      %[temp1],    4(%[dst])          \n\t"
                "sw      %[temp2],    8(%[dst])          \n\t"
                "sw      %[temp3],    12(%[dst])         \n\t"
                "sw      %[temp4],    16(%[dst])         \n\t"
                "sw      %[temp5],    20(%[dst])         \n\t"
                "sw      %[temp6],    24(%[dst])         \n\t"
                "sw      %[temp7],    28(%[dst])         \n\t"
                "bne     %[src],      %[loop_end], 1b    \n\t"
                " addiu  %[dst],      %[dst],      32    \n\t"
                ".set pop                                \n\t"

                : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                  [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                  [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                  [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                  [loop_end]"=&r"(loop_end), [src]"+r"(buf1),
                  [dst]"+r"(buf2)
                :
                : "memory"
            );
        }

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            {
                float wi;
                float wj;
                int i;
                float temp0, temp1, temp2, temp3;
                float *dst0 = out + 448 + 0*128;
                float *dst1 = dst0 + 64 + 63;
                float *dst2 = saved + 63;
                float *win0 = (float*)swindow;
                float *win1 = win0 + 64 + 63;
                float *win0_prev = (float*)swindow_prev;
                float *win1_prev = win0_prev + 64 + 63;
                float *src0_prev = saved + 448;
                float *src1_prev = buf + 0*128 + 63;
                float *src0 = buf + 0*128 + 64;
                float *src1 = buf + 1*128 + 63;

                for(i = 0; i < 64; i++)
                {
                    temp0 = src0_prev[0];
                    temp1 = src1_prev[0];
                    wi = *win0_prev;
                    wj = *win1_prev;
                    temp2 = src0[0];
                    temp3 = src1[0];
                    dst0[0] = temp0 * wj - temp1 * wi;
                    dst1[0] = temp0 * wi + temp1 * wj;

                    wi = *win0;
                    wj = *win1;

                    temp0 = src0[128];
                    temp1 = src1[128];
                    dst0[128] = temp2 * wj - temp3 * wi;
                    dst1[128] = temp2 * wi + temp3 * wj;

                    temp2 = src0[256];
                    temp3 = src1[256];
                    dst0[256] = temp0 * wj - temp1 * wi;
                    dst1[256] = temp0 * wi + temp1 * wj;
                    dst0[384] = temp2 * wj - temp3 * wi;
                    dst1[384] = temp2 * wi + temp3 * wj;

                    temp0 = src0[384];
                    temp1 = src1[384];
                    dst0[512] = temp0 * wj - temp1 * wi;
                    dst2[0] = temp0 * wi + temp1 * wj;

                    src0++;
                    src1--;
                    src0_prev++;
                    src1_prev--;
                    win0++;
                    win1--;
                    win0_prev++;
                    win1_prev--;
                    dst0++;
                    dst1--;
                    dst2--;
                }
            }
        } else {
            ac->fdsp.vector_fmul_window(out + 448,         saved + 448,      buf,         swindow_prev, 64);
            {
                float *buf1 = buf + 64;
                float *buf2 = out + 576;
                int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
                int loop_end;

                /* loop unrolled 8 times */
                __asm__ volatile (
                    ".set push                               \n\t"
                    ".set noreorder                          \n\t"
                    "addiu   %[loop_end], %[src],      1792  \n\t"
                "1:                                          \n\t"
                    "lw      %[temp0],    0(%[src])          \n\t"
                    "lw      %[temp1],    4(%[src])          \n\t"
                    "lw      %[temp2],    8(%[src])          \n\t"
                    "lw      %[temp3],    12(%[src])         \n\t"
                    "lw      %[temp4],    16(%[src])         \n\t"
                    "lw      %[temp5],    20(%[src])         \n\t"
                    "lw      %[temp6],    24(%[src])         \n\t"
                    "lw      %[temp7],    28(%[src])         \n\t"
                    "addiu   %[src],      %[src],      32    \n\t"
                    "sw      %[temp0],    0(%[dst])          \n\t"
                    "sw      %[temp1],    4(%[dst])          \n\t"
                    "sw      %[temp2],    8(%[dst])          \n\t"
                    "sw      %[temp3],    12(%[dst])         \n\t"
                    "sw      %[temp4],    16(%[dst])         \n\t"
                    "sw      %[temp5],    20(%[dst])         \n\t"
                    "sw      %[temp6],    24(%[dst])         \n\t"
                    "sw      %[temp7],    28(%[dst])         \n\t"
                    "bne     %[src],      %[loop_end], 1b    \n\t"
                    " addiu  %[dst],      %[dst],      32    \n\t"
                    ".set pop                                \n\t"

                    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                      [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                      [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                      [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                      [loop_end]"=&r"(loop_end), [src]"+r"(buf1),
                      [dst]"+r"(buf2)
                    :
                    : "memory"
                );
            }
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        ac->fdsp.vector_fmul_window(saved + 64,  buf + 4*128 + 64, buf + 5*128, swindow, 64);
        ac->fdsp.vector_fmul_window(saved + 192, buf + 5*128 + 64, buf + 6*128, swindow, 64);
        ac->fdsp.vector_fmul_window(saved + 320, buf + 6*128 + 64, buf + 7*128, swindow, 64);
        {
            float *buf1 = buf + 7*128 + 64;
            float *buf2 = saved + 448;
            int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
            int loop_end;

            /* loop unrolled 8 times */
            __asm__ volatile (
                ".set push                                \n\t"
                ".set noreorder                           \n\t"
                "addiu   %[loop_end], %[src],       256   \n\t"
            "1:                                           \n\t"
                "lw      %[temp0],    0(%[src])           \n\t"
                "lw      %[temp1],    4(%[src])           \n\t"
                "lw      %[temp2],    8(%[src])           \n\t"
                "lw      %[temp3],    12(%[src])          \n\t"
                "lw      %[temp4],    16(%[src])          \n\t"
                "lw      %[temp5],    20(%[src])          \n\t"
                "lw      %[temp6],    24(%[src])          \n\t"
                "lw      %[temp7],    28(%[src])          \n\t"
                "addiu   %[src],      %[src],       32    \n\t"
                "sw      %[temp0],    0(%[dst])           \n\t"
                "sw      %[temp1],    4(%[dst])           \n\t"
                "sw      %[temp2],    8(%[dst])           \n\t"
                "sw      %[temp3],    12(%[dst])          \n\t"
                "sw      %[temp4],    16(%[dst])          \n\t"
                "sw      %[temp5],    20(%[dst])          \n\t"
                "sw      %[temp6],    24(%[dst])          \n\t"
                "sw      %[temp7],    28(%[dst])          \n\t"
                "bne     %[src],      %[loop_end],  1b    \n\t"
                " addiu  %[dst],      %[dst],       32    \n\t"
                ".set pop                                 \n\t"

                : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                  [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                  [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                  [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                  [loop_end]"=&r"(loop_end), [src]"+r"(buf1),
                  [dst]"+r"(buf2)
                :
                : "memory"
            );
        }
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        float *buf1 = buf + 512;
        float *buf2 = saved;
        int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
        int loop_end;

        /* loop unrolled 8 times */
        __asm__ volatile (
            ".set push                                \n\t"
            ".set noreorder                           \n\t"
            "addiu   %[loop_end], %[src],       1792  \n\t"
        "1:                                           \n\t"
            "lw      %[temp0],    0(%[src])           \n\t"
            "lw      %[temp1],    4(%[src])           \n\t"
            "lw      %[temp2],    8(%[src])           \n\t"
            "lw      %[temp3],    12(%[src])          \n\t"
            "lw      %[temp4],    16(%[src])          \n\t"
            "lw      %[temp5],    20(%[src])          \n\t"
            "lw      %[temp6],    24(%[src])          \n\t"
            "lw      %[temp7],    28(%[src])          \n\t"
            "addiu   %[src],      %[src],       32    \n\t"
            "sw      %[temp0],    0(%[dst])           \n\t"
            "sw      %[temp1],    4(%[dst])           \n\t"
            "sw      %[temp2],    8(%[dst])           \n\t"
            "sw      %[temp3],    12(%[dst])          \n\t"
            "sw      %[temp4],    16(%[dst])          \n\t"
            "sw      %[temp5],    20(%[dst])          \n\t"
            "sw      %[temp6],    24(%[dst])          \n\t"
            "sw      %[temp7],    28(%[dst])          \n\t"
            "bne     %[src],      %[loop_end],  1b    \n\t"
            " addiu  %[dst],      %[dst],       32    \n\t"
            ".set pop                                 \n\t"

            : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
              [loop_end]"=&r"(loop_end), [src]"+r"(buf1),
              [dst]"+r"(buf2)
            :
            : "memory"
        );
        {
            float *buf1 = buf + 7*128 + 64;
            float *buf2 = saved + 448;
            int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
            int loop_end;

            /* loop unrolled 8 times */
            __asm__ volatile (
                ".set push                                 \n\t"
                ".set noreorder                            \n\t"
                "addiu   %[loop_end], %[src],        256   \n\t"
            "1:                                            \n\t"
                "lw      %[temp0],    0(%[src])            \n\t"
                "lw      %[temp1],    4(%[src])            \n\t"
                "lw      %[temp2],    8(%[src])            \n\t"
                "lw      %[temp3],    12(%[src])           \n\t"
                "lw      %[temp4],    16(%[src])           \n\t"
                "lw      %[temp5],    20(%[src])           \n\t"
                "lw      %[temp6],    24(%[src])           \n\t"
                "lw      %[temp7],    28(%[src])           \n\t"
                "addiu   %[src],      %[src],        32    \n\t"
                "sw      %[temp0],    0(%[dst])            \n\t"
                "sw      %[temp1],    4(%[dst])            \n\t"
                "sw      %[temp2],    8(%[dst])            \n\t"
                "sw      %[temp3],    12(%[dst])           \n\t"
                "sw      %[temp4],    16(%[dst])           \n\t"
                "sw      %[temp5],    20(%[dst])           \n\t"
                "sw      %[temp6],    24(%[dst])           \n\t"
                "sw      %[temp7],    28(%[dst])           \n\t"
                "bne     %[src],      %[loop_end],   1b    \n\t"
                " addiu  %[dst],      %[dst],        32    \n\t"
                ".set pop                                  \n\t"

                : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
                  [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
                  [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
                  [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
                  [loop_end]"=&r"(loop_end), [src]"+r"(buf1),
                  [dst]"+r"(buf2)
                :
                : "memory"
            );
        }
    } else { // LONG_STOP or ONLY_LONG
        float *buf1 = buf + 512;
        float *buf2 = saved;
        int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
        int loop_end;

        /* loop unrolled 8 times */
        __asm__ volatile (
            ".set push                                 \n\t"
            ".set noreorder                            \n\t"
            "addiu   %[loop_end], %[src],        2048  \n\t"
        "1:                                            \n\t"
            "lw      %[temp0],    0(%[src])            \n\t"
            "lw      %[temp1],    4(%[src])            \n\t"
            "lw      %[temp2],    8(%[src])            \n\t"
            "lw      %[temp3],    12(%[src])           \n\t"
            "lw      %[temp4],    16(%[src])           \n\t"
            "lw      %[temp5],    20(%[src])           \n\t"
            "lw      %[temp6],    24(%[src])           \n\t"
            "lw      %[temp7],    28(%[src])           \n\t"
            "addiu   %[src],      %[src],        32    \n\t"
            "sw      %[temp0],    0(%[dst])            \n\t"
            "sw      %[temp1],    4(%[dst])            \n\t"
            "sw      %[temp2],    8(%[dst])            \n\t"
            "sw      %[temp3],    12(%[dst])           \n\t"
            "sw      %[temp4],    16(%[dst])           \n\t"
            "sw      %[temp5],    20(%[dst])           \n\t"
            "sw      %[temp6],    24(%[dst])           \n\t"
            "sw      %[temp7],    28(%[dst])           \n\t"
            "bne     %[src],      %[loop_end],   1b    \n\t"
            " addiu  %[dst],      %[dst],        32    \n\t"
            ".set pop                                  \n\t"

            : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
              [loop_end]"=&r"(loop_end), [src]"+r"(buf1),
              [dst]"+r"(buf2)
            :
            : "memory"
        );
    }
}

static void apply_ltp_mips(AACContext *ac, SingleChannelElement *sce)
{
    const LongTermPrediction *ltp = &sce->ics.ltp;
    const uint16_t *offsets = sce->ics.swb_offset;
    int i, sfb;
    int j, k;

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        float *predTime = sce->ret;
        float *predFreq = ac->buf_mdct;
        float *p_predTime;
        int16_t num_samples = 2048;

        if (ltp->lag < 1024)
            num_samples = ltp->lag + 1024;
            j = (2048 - num_samples) >> 2;
            k = (2048 - num_samples) & 3;
            p_predTime = &predTime[num_samples];

        for (i = 0; i < num_samples; i++)
            predTime[i] = sce->ltp_state[i + 2048 - ltp->lag] * ltp->coef;
        for (i = 0; i < j; i++) {

            /* loop unrolled 4 times */
            __asm__ volatile (
                "sw      $0,              0(%[p_predTime])        \n\t"
                "sw      $0,              4(%[p_predTime])        \n\t"
                "sw      $0,              8(%[p_predTime])        \n\t"
                "sw      $0,              12(%[p_predTime])       \n\t"
                "addiu   %[p_predTime],   %[p_predTime],     16   \n\t"

                : [p_predTime]"+r"(p_predTime)
                :
                : "memory"
            );
        }
        for (i = 0; i < k; i++) {

            __asm__ volatile (
                "sw      $0,              0(%[p_predTime])        \n\t"
                "addiu   %[p_predTime],   %[p_predTime],     4    \n\t"

                : [p_predTime]"+r"(p_predTime)
                :
                : "memory"
            );
        }

        ac->windowing_and_mdct_ltp(ac, predFreq, predTime, &sce->ics);

        if (sce->tns.present)
            ac->apply_tns(predFreq, &sce->tns, &sce->ics, 0);

        for (sfb = 0; sfb < FFMIN(sce->ics.max_sfb, MAX_LTP_LONG_SFB); sfb++)
            if (ltp->used[sfb])
                for (i = offsets[sfb]; i < offsets[sfb + 1]; i++)
                    sce->coeffs[i] += predFreq[i];
    }
}

#if HAVE_MIPSFPU
static void update_ltp_mips(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    float *saved     = sce->saved;
    float *saved_ltp = sce->coeffs;
    const float *lwindow = ics->use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    int i;
    int loop_end, loop_end1, loop_end2;
    float temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7, temp8, temp9, temp10, temp11;

    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        float *buf = saved;
        float *buf0 = saved_ltp;
        float *p_saved_ltp = saved_ltp + 576;
        float *ptr1 = &saved_ltp[512];
        float *ptr2 = &ac->buf_mdct[1023];
        float *ptr3 = (float*)&swindow[63];
        loop_end1 = (int)(p_saved_ltp + 448);

        /* loop unrolled 8 times */
        __asm__ volatile (
            ".set push                                     \n\t"
            ".set noreorder                                \n\t"
            "addiu   %[loop_end],   %[src],         2048   \n\t"
        "1:                                                \n\t"
            "lw      %[temp0],      0(%[src])              \n\t"
            "lw      %[temp1],      4(%[src])              \n\t"
            "lw      %[temp2],      8(%[src])              \n\t"
            "lw      %[temp3],      12(%[src])             \n\t"
            "lw      %[temp4],      16(%[src])             \n\t"
            "lw      %[temp5],      20(%[src])             \n\t"
            "lw      %[temp6],      24(%[src])             \n\t"
            "lw      %[temp7],      28(%[src])             \n\t"
            "addiu   %[src],        %[src],         32     \n\t"
            "sw      %[temp0],      0(%[dst])              \n\t"
            "sw      %[temp1],      4(%[dst])              \n\t"
            "sw      %[temp2],      8(%[dst])              \n\t"
            "sw      %[temp3],      12(%[dst])             \n\t"
            "sw      %[temp4],      16(%[dst])             \n\t"
            "sw      %[temp5],      20(%[dst])             \n\t"
            "sw      %[temp6],      24(%[dst])             \n\t"
            "sw      %[temp7],      28(%[dst])             \n\t"
            "bne     %[src],        %[loop_end],    1b     \n\t"
            " addiu  %[dst],        %[dst],         32     \n\t"
            ".set pop                                      \n\t"

            : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
              [loop_end]"=&r"(loop_end), [src]"+r"(buf),
              [dst]"+r"(buf0)
            :
            : "memory"
        );

        /* loop unrolled 8 times */
        __asm__ volatile (
        "1:                                                   \n\t"
            "sw     $0,              0(%[p_saved_ltp])        \n\t"
            "sw     $0,              4(%[p_saved_ltp])        \n\t"
            "sw     $0,              8(%[p_saved_ltp])        \n\t"
            "sw     $0,              12(%[p_saved_ltp])       \n\t"
            "sw     $0,              16(%[p_saved_ltp])       \n\t"
            "sw     $0,              20(%[p_saved_ltp])       \n\t"
            "sw     $0,              24(%[p_saved_ltp])       \n\t"
            "sw     $0,              28(%[p_saved_ltp])       \n\t"
            "addiu  %[p_saved_ltp],  %[p_saved_ltp],     32   \n\t"
            "bne    %[p_saved_ltp],  %[loop_end1],       1b   \n\t"

            : [p_saved_ltp]"+r"(p_saved_ltp)
            : [loop_end1]"r"(loop_end1)
            : "memory"
        );

        ac->fdsp.vector_fmul_reverse(saved_ltp + 448, ac->buf_mdct + 960,     &swindow[64],      64);
        for (i = 0; i < 16; i++){
            /* loop unrolled 4 times */
            __asm__ volatile (
                "lwc1    %[temp0],    0(%[ptr2])                \n\t"
                "lwc1    %[temp1],    -4(%[ptr2])               \n\t"
                "lwc1    %[temp2],    -8(%[ptr2])               \n\t"
                "lwc1    %[temp3],    -12(%[ptr2])              \n\t"
                "lwc1    %[temp4],    0(%[ptr3])                \n\t"
                "lwc1    %[temp5],    -4(%[ptr3])               \n\t"
                "lwc1    %[temp6],    -8(%[ptr3])               \n\t"
                "lwc1    %[temp7],    -12(%[ptr3])              \n\t"
                "mul.s   %[temp8],    %[temp0],     %[temp4]    \n\t"
                "mul.s   %[temp9],    %[temp1],     %[temp5]    \n\t"
                "mul.s   %[temp10],   %[temp2],     %[temp6]    \n\t"
                "mul.s   %[temp11],   %[temp3],     %[temp7]    \n\t"
                "swc1    %[temp8],    0(%[ptr1])                \n\t"
                "swc1    %[temp9],    4(%[ptr1])                \n\t"
                "swc1    %[temp10],   8(%[ptr1])                \n\t"
                "swc1    %[temp11],   12(%[ptr1])               \n\t"
                "addiu   %[ptr1],     %[ptr1],      16          \n\t"
                "addiu   %[ptr2],     %[ptr2],      -16         \n\t"
                "addiu   %[ptr3],     %[ptr3],      -16         \n\t"

                : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
                  [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
                  [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
                  [temp6]"=&f"(temp6), [temp7]"=&f"(temp7),
                  [temp8]"=&f"(temp8), [temp9]"=&f"(temp9),
                  [temp10]"=&f"(temp10), [temp11]"=&f"(temp11),
                  [ptr1]"+r"(ptr1), [ptr2]"+r"(ptr2), [ptr3]"+r"(ptr3)
                :
                : "memory"
            );
        }
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        float *buff0 = saved;
        float *buff1 = saved_ltp;
        float *ptr1 = &saved_ltp[512];
        float *ptr2 = &ac->buf_mdct[1023];
        float *ptr3 = (float*)&swindow[63];
        loop_end = (int)(saved + 448);

        /* loop unrolled 8 times */
        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"
        "1:                                             \n\t"
            "lw      %[temp0],    0(%[src])             \n\t"
            "lw      %[temp1],    4(%[src])             \n\t"
            "lw      %[temp2],    8(%[src])             \n\t"
            "lw      %[temp3],    12(%[src])            \n\t"
            "lw      %[temp4],    16(%[src])            \n\t"
            "lw      %[temp5],    20(%[src])            \n\t"
            "lw      %[temp6],    24(%[src])            \n\t"
            "lw      %[temp7],    28(%[src])            \n\t"
            "addiu   %[src],      %[src],         32    \n\t"
            "sw      %[temp0],    0(%[dst])             \n\t"
            "sw      %[temp1],    4(%[dst])             \n\t"
            "sw      %[temp2],    8(%[dst])             \n\t"
            "sw      %[temp3],    12(%[dst])            \n\t"
            "sw      %[temp4],    16(%[dst])            \n\t"
            "sw      %[temp5],    20(%[dst])            \n\t"
            "sw      %[temp6],    24(%[dst])            \n\t"
            "sw      %[temp7],    28(%[dst])            \n\t"
            "sw      $0,          2304(%[dst])          \n\t"
            "sw      $0,          2308(%[dst])          \n\t"
            "sw      $0,          2312(%[dst])          \n\t"
            "sw      $0,          2316(%[dst])          \n\t"
            "sw      $0,          2320(%[dst])          \n\t"
            "sw      $0,          2324(%[dst])          \n\t"
            "sw      $0,          2328(%[dst])          \n\t"
            "sw      $0,          2332(%[dst])          \n\t"
            "bne     %[src],      %[loop_end],    1b    \n\t"
            " addiu  %[dst],      %[dst],         32    \n\t"
            ".set pop                                   \n\t"

            : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
              [src]"+r"(buff0), [dst]"+r"(buff1)
            : [loop_end]"r"(loop_end)
            : "memory"
        );
        ac->fdsp.vector_fmul_reverse(saved_ltp + 448, ac->buf_mdct + 960,     &swindow[64],      64);
        for (i = 0; i < 16; i++){
            /* loop unrolled 8 times */
            __asm__ volatile (
                "lwc1    %[temp0],    0(%[ptr2])                \n\t"
                "lwc1    %[temp1],    -4(%[ptr2])               \n\t"
                "lwc1    %[temp2],    -8(%[ptr2])               \n\t"
                "lwc1    %[temp3],    -12(%[ptr2])              \n\t"
                "lwc1    %[temp4],    0(%[ptr3])                \n\t"
                "lwc1    %[temp5],    -4(%[ptr3])               \n\t"
                "lwc1    %[temp6],    -8(%[ptr3])               \n\t"
                "lwc1    %[temp7],    -12(%[ptr3])              \n\t"
                "mul.s   %[temp8],    %[temp0],     %[temp4]    \n\t"
                "mul.s   %[temp9],    %[temp1],     %[temp5]    \n\t"
                "mul.s   %[temp10],   %[temp2],     %[temp6]    \n\t"
                "mul.s   %[temp11],   %[temp3],     %[temp7]    \n\t"
                "swc1    %[temp8],    0(%[ptr1])                \n\t"
                "swc1    %[temp9],    4(%[ptr1])                \n\t"
                "swc1    %[temp10],   8(%[ptr1])                \n\t"
                "swc1    %[temp11],   12(%[ptr1])               \n\t"
                "addiu   %[ptr1],     %[ptr1],      16          \n\t"
                "addiu   %[ptr2],     %[ptr2],      -16         \n\t"
                "addiu   %[ptr3],     %[ptr3],      -16         \n\t"

                : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
                  [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
                  [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
                  [temp6]"=&f"(temp6), [temp7]"=&f"(temp7),
                  [temp8]"=&f"(temp8), [temp9]"=&f"(temp9),
                  [temp10]"=&f"(temp10), [temp11]"=&f"(temp11),
                  [ptr1]"+r"(ptr1), [ptr2]"+r"(ptr2), [ptr3]"+r"(ptr3)
                :
                : "memory"
            );
        }
    } else { // LONG_STOP or ONLY_LONG
        float *ptr1, *ptr2, *ptr3;
        ac->fdsp.vector_fmul_reverse(saved_ltp,       ac->buf_mdct + 512,     &lwindow[512],     512);

        ptr1 = &saved_ltp[512];
        ptr2 = &ac->buf_mdct[1023];
        ptr3 = (float*)&lwindow[511];

        for (i = 0; i < 512; i+=4){
            /* loop unrolled 4 times */
            __asm__ volatile (
                "lwc1    %[temp0],    0(%[ptr2])                \n\t"
                "lwc1    %[temp1],    -4(%[ptr2])               \n\t"
                "lwc1    %[temp2],    -8(%[ptr2])               \n\t"
                "lwc1    %[temp3],    -12(%[ptr2])              \n\t"
                "lwc1    %[temp4],    0(%[ptr3])                \n\t"
                "lwc1    %[temp5],    -4(%[ptr3])               \n\t"
                "lwc1    %[temp6],    -8(%[ptr3])               \n\t"
                "lwc1    %[temp7],    -12(%[ptr3])              \n\t"
                "mul.s   %[temp8],    %[temp0],     %[temp4]    \n\t"
                "mul.s   %[temp9],    %[temp1],     %[temp5]    \n\t"
                "mul.s   %[temp10],   %[temp2],     %[temp6]    \n\t"
                "mul.s   %[temp11],   %[temp3],     %[temp7]    \n\t"
                "swc1    %[temp8],    0(%[ptr1])                \n\t"
                "swc1    %[temp9],    4(%[ptr1])                \n\t"
                "swc1    %[temp10],   8(%[ptr1])                \n\t"
                "swc1    %[temp11],   12(%[ptr1])               \n\t"
                "addiu   %[ptr1],     %[ptr1],      16          \n\t"
                "addiu   %[ptr2],     %[ptr2],      -16         \n\t"
                "addiu   %[ptr3],     %[ptr3],      -16         \n\t"

                : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
                  [temp2]"=&f"(temp2), [temp3]"=&f"(temp3),
                  [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
                  [temp6]"=&f"(temp6), [temp7]"=&f"(temp7),
                  [temp8]"=&f"(temp8), [temp9]"=&f"(temp9),
                  [temp10]"=&f"(temp10), [temp11]"=&f"(temp11),
                  [ptr1]"+r"(ptr1), [ptr2]"+r"(ptr2),
                  [ptr3]"+r"(ptr3)
                :
                : "memory"
            );
        }
    }

    {
        float *buf1 = sce->ltp_state+1024;
        float *buf2 = sce->ltp_state;
        float *buf3 = sce->ret;
        float *buf4 = sce->ltp_state+1024;
        float *buf5 = saved_ltp;
        float *buf6 = sce->ltp_state+2048;

        /* loops unrolled 8 times */
        __asm__ volatile (
            ".set push                                    \n\t"
            ".set noreorder                               \n\t"
            "addiu   %[loop_end],   %[src],         4096  \n\t"
            "addiu   %[loop_end1],  %[src1],        4096  \n\t"
            "addiu   %[loop_end2],  %[src2],        4096  \n\t"
        "1:                                               \n\t"
            "lw      %[temp0],      0(%[src])             \n\t"
            "lw      %[temp1],      4(%[src])             \n\t"
            "lw      %[temp2],      8(%[src])             \n\t"
            "lw      %[temp3],      12(%[src])            \n\t"
            "lw      %[temp4],      16(%[src])            \n\t"
            "lw      %[temp5],      20(%[src])            \n\t"
            "lw      %[temp6],      24(%[src])            \n\t"
            "lw      %[temp7],      28(%[src])            \n\t"
            "addiu   %[src],        %[src],         32    \n\t"
            "sw      %[temp0],      0(%[dst])             \n\t"
            "sw      %[temp1],      4(%[dst])             \n\t"
            "sw      %[temp2],      8(%[dst])             \n\t"
            "sw      %[temp3],      12(%[dst])            \n\t"
            "sw      %[temp4],      16(%[dst])            \n\t"
            "sw      %[temp5],      20(%[dst])            \n\t"
            "sw      %[temp6],      24(%[dst])            \n\t"
            "sw      %[temp7],      28(%[dst])            \n\t"
            "bne     %[src],        %[loop_end],    1b    \n\t"
            " addiu  %[dst],        %[dst],         32    \n\t"
        "2:                                               \n\t"
            "lw      %[temp0],      0(%[src1])            \n\t"
            "lw      %[temp1],      4(%[src1])            \n\t"
            "lw      %[temp2],      8(%[src1])            \n\t"
            "lw      %[temp3],      12(%[src1])           \n\t"
            "lw      %[temp4],      16(%[src1])           \n\t"
            "lw      %[temp5],      20(%[src1])           \n\t"
            "lw      %[temp6],      24(%[src1])           \n\t"
            "lw      %[temp7],      28(%[src1])           \n\t"
            "addiu   %[src1],       %[src1],        32    \n\t"
            "sw      %[temp0],      0(%[dst1])            \n\t"
            "sw      %[temp1],      4(%[dst1])            \n\t"
            "sw      %[temp2],      8(%[dst1])            \n\t"
            "sw      %[temp3],      12(%[dst1])           \n\t"
            "sw      %[temp4],      16(%[dst1])           \n\t"
            "sw      %[temp5],      20(%[dst1])           \n\t"
            "sw      %[temp6],      24(%[dst1])           \n\t"
            "sw      %[temp7],      28(%[dst1])           \n\t"
            "bne     %[src1],       %[loop_end1],   2b    \n\t"
            " addiu  %[dst1],       %[dst1],        32    \n\t"
        "3:                                               \n\t"
            "lw      %[temp0],      0(%[src2])            \n\t"
            "lw      %[temp1],      4(%[src2])            \n\t"
            "lw      %[temp2],      8(%[src2])            \n\t"
            "lw      %[temp3],      12(%[src2])           \n\t"
            "lw      %[temp4],      16(%[src2])           \n\t"
            "lw      %[temp5],      20(%[src2])           \n\t"
            "lw      %[temp6],      24(%[src2])           \n\t"
            "lw      %[temp7],      28(%[src2])           \n\t"
            "addiu   %[src2],       %[src2],        32    \n\t"
            "sw      %[temp0],      0(%[dst2])            \n\t"
            "sw      %[temp1],      4(%[dst2])            \n\t"
            "sw      %[temp2],      8(%[dst2])            \n\t"
            "sw      %[temp3],      12(%[dst2])           \n\t"
            "sw      %[temp4],      16(%[dst2])           \n\t"
            "sw      %[temp5],      20(%[dst2])           \n\t"
            "sw      %[temp6],      24(%[dst2])           \n\t"
            "sw      %[temp7],      28(%[dst2])           \n\t"
            "bne     %[src2],       %[loop_end2],   3b    \n\t"
            " addiu  %[dst2],       %[dst2],        32    \n\t"
            ".set pop                                     \n\t"

            : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
              [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
              [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
              [temp6]"=&r"(temp6), [temp7]"=&r"(temp7),
              [loop_end]"=&r"(loop_end), [loop_end1]"=&r"(loop_end1),
              [loop_end2]"=&r"(loop_end2), [src]"+r"(buf1),
              [dst]"+r"(buf2), [src1]"+r"(buf3), [dst1]"+r"(buf4),
              [src2]"+r"(buf5), [dst2]"+r"(buf6)
            :
            : "memory"
        );
    }
}
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */

void ff_aacdec_init_mips(AACContext *c)
{
#if HAVE_INLINE_ASM
    c->imdct_and_windowing         = imdct_and_windowing_mips;
    c->apply_ltp                   = apply_ltp_mips;
#if HAVE_MIPSFPU
    c->update_ltp                  = update_ltp_mips;
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */
}
