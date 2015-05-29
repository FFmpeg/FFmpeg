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
 * various filters for CELP-based codecs optimized for MIPS
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
 * Reference: libavcodec/celp_filters.c
 */
#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavcodec/celp_filters.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM
static void ff_celp_lp_synthesis_filterf_mips(float *out,
                                  const float *filter_coeffs,
                                  const float* in, int buffer_length,
                                  int filter_length)
{
    int i,n;

    float out0, out1, out2, out3;
    float old_out0, old_out1, old_out2, old_out3;
    float a,b,c;
    const float *p_filter_coeffs;
    float *p_out;

    a = filter_coeffs[0];
    b = filter_coeffs[1];
    c = filter_coeffs[2];
    b -= filter_coeffs[0] * filter_coeffs[0];
    c -= filter_coeffs[1] * filter_coeffs[0];
    c -= filter_coeffs[0] * b;

    old_out0 = out[-4];
    old_out1 = out[-3];
    old_out2 = out[-2];
    old_out3 = out[-1];
    for (n = 0; n <= buffer_length - 4; n+=4) {
        p_filter_coeffs = filter_coeffs;
        p_out = out;

        out0 = in[0];
        out1 = in[1];
        out2 = in[2];
        out3 = in[3];

        __asm__ volatile(
            "lwc1       $f2,     8(%[filter_coeffs])                        \n\t"
            "lwc1       $f1,     4(%[filter_coeffs])                        \n\t"
            "lwc1       $f0,     0(%[filter_coeffs])                        \n\t"
            "nmsub.s    %[out0], %[out0],             $f2, %[old_out1]      \n\t"
            "nmsub.s    %[out1], %[out1],             $f2, %[old_out2]      \n\t"
            "nmsub.s    %[out2], %[out2],             $f2, %[old_out3]      \n\t"
            "lwc1       $f3,     12(%[filter_coeffs])                       \n\t"
            "nmsub.s    %[out0], %[out0],             $f1, %[old_out2]      \n\t"
            "nmsub.s    %[out1], %[out1],             $f1, %[old_out3]      \n\t"
            "nmsub.s    %[out2], %[out2],             $f3, %[old_out2]      \n\t"
            "nmsub.s    %[out0], %[out0],             $f0, %[old_out3]      \n\t"
            "nmsub.s    %[out3], %[out3],             $f3, %[old_out3]      \n\t"
            "nmsub.s    %[out1], %[out1],             $f3, %[old_out1]      \n\t"
            "nmsub.s    %[out0], %[out0],             $f3, %[old_out0]      \n\t"

            : [out0]"+f"(out0), [out1]"+f"(out1),
              [out2]"+f"(out2), [out3]"+f"(out3)
            : [old_out0]"f"(old_out0), [old_out1]"f"(old_out1),
              [old_out2]"f"(old_out2), [old_out3]"f"(old_out3),
              [filter_coeffs]"r"(filter_coeffs)
            : "$f0", "$f1", "$f2", "$f3", "$f4", "memory"
        );

        for (i = 5; i <= filter_length; i += 2) {
            __asm__ volatile(
                "lwc1    %[old_out3], -20(%[p_out])                         \n\t"
                "lwc1    $f5,         16(%[p_filter_coeffs])                \n\t"
                PTR_ADDIU "%[p_out],  -8                                    \n\t"
                PTR_ADDIU "%[p_filter_coeffs], 8                            \n\t"
                "nmsub.s %[out1],     %[out1],      $f5, %[old_out0]        \n\t"
                "nmsub.s %[out3],     %[out3],      $f5, %[old_out2]        \n\t"
                "lwc1    $f4,         12(%[p_filter_coeffs])                \n\t"
                "lwc1    %[old_out2], -16(%[p_out])                         \n\t"
                "nmsub.s %[out0],     %[out0],      $f5, %[old_out3]        \n\t"
                "nmsub.s %[out2],     %[out2],      $f5, %[old_out1]        \n\t"
                "nmsub.s %[out1],     %[out1],      $f4, %[old_out3]        \n\t"
                "nmsub.s %[out3],     %[out3],      $f4, %[old_out1]        \n\t"
                "mov.s   %[old_out1], %[old_out3]                           \n\t"
                "nmsub.s %[out0],     %[out0],      $f4, %[old_out2]        \n\t"
                "nmsub.s %[out2],     %[out2],      $f4, %[old_out0]        \n\t"

                : [out0]"+f"(out0), [out1]"+f"(out1),
                  [out2]"+f"(out2), [out3]"+f"(out3), [old_out0]"+f"(old_out0),
                  [old_out1]"+f"(old_out1), [old_out2]"+f"(old_out2),
                  [old_out3]"+f"(old_out3),[p_filter_coeffs]"+r"(p_filter_coeffs),
                  [p_out]"+r"(p_out)
                :
                : "$f4", "$f5", "memory"
            );
            FFSWAP(float, old_out0, old_out2);
        }

        __asm__ volatile(
            "nmsub.s    %[out3], %[out3], %[a], %[out2]                     \n\t"
            "nmsub.s    %[out2], %[out2], %[a], %[out1]                     \n\t"
            "nmsub.s    %[out3], %[out3], %[b], %[out1]                     \n\t"
            "nmsub.s    %[out1], %[out1], %[a], %[out0]                     \n\t"
            "nmsub.s    %[out2], %[out2], %[b], %[out0]                     \n\t"
            "nmsub.s    %[out3], %[out3], %[c], %[out0]                     \n\t"

            : [out0]"+f"(out0), [out1]"+f"(out1),
              [out2]"+f"(out2), [out3]"+f"(out3)
            : [a]"f"(a), [b]"f"(b), [c]"f"(c)
        );

        out[0] = out0;
        out[1] = out1;
        out[2] = out2;
        out[3] = out3;

        old_out0 = out0;
        old_out1 = out1;
        old_out2 = out2;
        old_out3 = out3;

        out += 4;
        in  += 4;
    }

    out -= n;
    in -= n;
    for (; n < buffer_length; n++) {
        float out_val, out_val_i, fc_val;
        p_filter_coeffs = filter_coeffs;
        p_out = &out[n];
        out_val = in[n];
        for (i = 1; i <= filter_length; i++) {
            __asm__ volatile(
                "lwc1    %[fc_val],          0(%[p_filter_coeffs])                        \n\t"
                "lwc1    %[out_val_i],       -4(%[p_out])                                 \n\t"
                PTR_ADDIU "%[p_filter_coeffs], 4                                          \n\t"
                PTR_ADDIU "%[p_out],         -4                                           \n\t"
                "nmsub.s %[out_val],         %[out_val],          %[fc_val], %[out_val_i] \n\t"

                : [fc_val]"=&f"(fc_val), [out_val]"+f"(out_val),
                  [out_val_i]"=&f"(out_val_i), [p_out]"+r"(p_out),
                  [p_filter_coeffs]"+r"(p_filter_coeffs)
                :
                : "memory"
            );
        }
        out[n] = out_val;
    }
}

static void ff_celp_lp_zero_synthesis_filterf_mips(float *out,
                                       const float *filter_coeffs,
                                       const float *in, int buffer_length,
                                       int filter_length)
{
    int i,n;
    float sum_out8, sum_out7, sum_out6, sum_out5, sum_out4, fc_val;
    float sum_out3, sum_out2, sum_out1;
    const float *p_filter_coeffs, *p_in;

    for (n = 0; n < buffer_length; n+=8) {
        p_in = &in[n];
        p_filter_coeffs = filter_coeffs;
        sum_out8 = in[n+7];
        sum_out7 = in[n+6];
        sum_out6 = in[n+5];
        sum_out5 = in[n+4];
        sum_out4 = in[n+3];
        sum_out3 = in[n+2];
        sum_out2 = in[n+1];
        sum_out1 = in[n];
        i = filter_length;

        /* i is always greater than 0
        * outer loop is unrolled eight times so there is less memory access
        * inner loop is unrolled two times
        */
        __asm__ volatile(
            "filt_lp_inner%=:                                               \n\t"
            "lwc1   %[fc_val],   0(%[p_filter_coeffs])                      \n\t"
            "lwc1   $f7,         6*4(%[p_in])                               \n\t"
            "lwc1   $f6,         5*4(%[p_in])                               \n\t"
            "lwc1   $f5,         4*4(%[p_in])                               \n\t"
            "lwc1   $f4,         3*4(%[p_in])                               \n\t"
            "lwc1   $f3,         2*4(%[p_in])                               \n\t"
            "lwc1   $f2,         4(%[p_in])                                 \n\t"
            "lwc1   $f1,         0(%[p_in])                                 \n\t"
            "lwc1   $f0,         -4(%[p_in])                                \n\t"
            "addiu  %[i],        -2                                         \n\t"
            "madd.s %[sum_out8], %[sum_out8],          %[fc_val], $f7       \n\t"
            "madd.s %[sum_out7], %[sum_out7],          %[fc_val], $f6       \n\t"
            "madd.s %[sum_out6], %[sum_out6],          %[fc_val], $f5       \n\t"
            "madd.s %[sum_out5], %[sum_out5],          %[fc_val], $f4       \n\t"
            "madd.s %[sum_out4], %[sum_out4],          %[fc_val], $f3       \n\t"
            "madd.s %[sum_out3], %[sum_out3],          %[fc_val], $f2       \n\t"
            "madd.s %[sum_out2], %[sum_out2],          %[fc_val], $f1       \n\t"
            "madd.s %[sum_out1], %[sum_out1],          %[fc_val], $f0       \n\t"
            "lwc1   %[fc_val],   4(%[p_filter_coeffs])                      \n\t"
            "lwc1   $f7,         -8(%[p_in])                                \n\t"
            PTR_ADDIU "%[p_filter_coeffs], 8                                \n\t"
            PTR_ADDIU "%[p_in],  -8                                         \n\t"
            "madd.s %[sum_out8], %[sum_out8],          %[fc_val], $f6       \n\t"
            "madd.s %[sum_out7], %[sum_out7],          %[fc_val], $f5       \n\t"
            "madd.s %[sum_out6], %[sum_out6],          %[fc_val], $f4       \n\t"
            "madd.s %[sum_out5], %[sum_out5],          %[fc_val], $f3       \n\t"
            "madd.s %[sum_out4], %[sum_out4],          %[fc_val], $f2       \n\t"
            "madd.s %[sum_out3], %[sum_out3],          %[fc_val], $f1       \n\t"
            "madd.s %[sum_out2], %[sum_out2],          %[fc_val], $f0       \n\t"
            "madd.s %[sum_out1], %[sum_out1],          %[fc_val], $f7       \n\t"
            "bgtz   %[i],        filt_lp_inner%=                            \n\t"

            : [sum_out8]"+f"(sum_out8), [sum_out7]"+f"(sum_out7),
              [sum_out6]"+f"(sum_out6), [sum_out5]"+f"(sum_out5),
              [sum_out4]"+f"(sum_out4), [sum_out3]"+f"(sum_out3),
              [sum_out2]"+f"(sum_out2), [sum_out1]"+f"(sum_out1),
              [fc_val]"=&f"(fc_val), [p_filter_coeffs]"+r"(p_filter_coeffs),
              [p_in]"+r"(p_in), [i]"+r"(i)
            :
            : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "memory"
        );

        out[n+7] = sum_out8;
        out[n+6] = sum_out7;
        out[n+5] = sum_out6;
        out[n+4] = sum_out5;
        out[n+3] = sum_out4;
        out[n+2] = sum_out3;
        out[n+1] = sum_out2;
        out[n] = sum_out1;
    }
}
#endif /* HAVE_INLINE_ASM */

void ff_celp_filter_init_mips(CELPFContext *c)
{
#if HAVE_INLINE_ASM
    c->celp_lp_synthesis_filterf        = ff_celp_lp_synthesis_filterf_mips;
    c->celp_lp_zero_synthesis_filterf   = ff_celp_lp_zero_synthesis_filterf_mips;
#endif
}
