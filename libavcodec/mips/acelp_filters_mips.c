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
 * various filters for ACELP-based codecs optimized for MIPS
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
 * Reference: libavcodec/acelp_filters.c
 */
#include "libavutil/attributes.h"
#include "libavcodec/acelp_filters.h"

static void ff_acelp_interpolatef_mips(float *out, const float *in,
                           const float *filter_coeffs, int precision,
                           int frac_pos, int filter_length, int length)
{
    int n, i;
    int prec = precision * 4;
    int fc_offset = precision - frac_pos;
    float in_val_p, in_val_m, fc_val_p, fc_val_m;

    for (n = 0; n < length; n++) {
        /**
        * four pointers are defined in order to minimize number of
        * computations done in inner loop
        */
        const float *p_in_p = &in[n];
        const float *p_in_m = &in[n-1];
        const float *p_filter_coeffs_p = &filter_coeffs[frac_pos];
        const float *p_filter_coeffs_m = filter_coeffs + fc_offset;
        float v = 0;

        for (i = 0; i < filter_length;i++) {
            __asm__ volatile (
                "lwc1   %[in_val_p],           0(%[p_in_p])                    \n\t"
                "lwc1   %[fc_val_p],           0(%[p_filter_coeffs_p])         \n\t"
                "lwc1   %[in_val_m],           0(%[p_in_m])                    \n\t"
                "lwc1   %[fc_val_m],           0(%[p_filter_coeffs_m])         \n\t"
                "addiu  %[p_in_p],             %[p_in_p],              4       \n\t"
                "madd.s %[v],%[v],             %[in_val_p],%[fc_val_p]         \n\t"
                "addiu  %[p_in_m],             %[p_in_m],              -4      \n\t"
                "addu   %[p_filter_coeffs_p],  %[p_filter_coeffs_p],   %[prec] \n\t"
                "addu   %[p_filter_coeffs_m],  %[p_filter_coeffs_m],   %[prec] \n\t"
                "madd.s %[v],%[v],%[in_val_m], %[fc_val_m]                     \n\t"

                : [v] "=&f" (v),[p_in_p] "+r" (p_in_p), [p_in_m] "+r" (p_in_m),
                  [p_filter_coeffs_p] "+r" (p_filter_coeffs_p),
                  [in_val_p] "=&f" (in_val_p), [in_val_m] "=&f" (in_val_m),
                  [fc_val_p] "=&f" (fc_val_p), [fc_val_m] "=&f" (fc_val_m),
                  [p_filter_coeffs_m] "+r" (p_filter_coeffs_m)
                : [prec] "r" (prec)
            );
        }
        out[n] = v;
    }
}

static void ff_acelp_apply_order_2_transfer_function_mips(float *out, const float *in,
                                              const float zero_coeffs[2],
                                              const float pole_coeffs[2],
                                              float gain, float mem[2], int n)
{
    /**
    * loop is unrolled eight times
    */

    __asm__ volatile (
        "lwc1   $f0,    0(%[mem])                                              \n\t"
        "blez   %[n],   ff_acelp_apply_order_2_transfer_function_end%=         \n\t"
        "lwc1   $f1,    4(%[mem])                                              \n\t"
        "lwc1   $f2,    0(%[pole_coeffs])                                      \n\t"
        "lwc1   $f3,    4(%[pole_coeffs])                                      \n\t"
        "lwc1   $f4,    0(%[zero_coeffs])                                      \n\t"
        "lwc1   $f5,    4(%[zero_coeffs])                                      \n\t"

        "ff_acelp_apply_order_2_transfer_function_madd%=:                      \n\t"

        "lwc1   $f6,    0(%[in])                                               \n\t"
        "mul.s  $f9,    $f3,      $f1                                          \n\t"
        "mul.s  $f7,    $f2,      $f0                                          \n\t"
        "msub.s $f7,    $f7,      %[gain], $f6                                 \n\t"
        "sub.s  $f7,    $f7,      $f9                                          \n\t"
        "madd.s $f8,    $f7,      $f4,     $f0                                 \n\t"
        "madd.s $f8,    $f8,      $f5,     $f1                                 \n\t"
        "lwc1   $f11,   4(%[in])                                               \n\t"
        "mul.s  $f12,   $f3,      $f0                                          \n\t"
        "mul.s  $f13,   $f2,      $f7                                          \n\t"
        "msub.s $f13,   $f13,     %[gain], $f11                                \n\t"
        "sub.s  $f13,   $f13,     $f12                                         \n\t"
        "madd.s $f14,   $f13,     $f4,     $f7                                 \n\t"
        "madd.s $f14,   $f14,     $f5,     $f0                                 \n\t"
        "swc1   $f8,    0(%[out])                                              \n\t"
        "lwc1   $f6,    8(%[in])                                               \n\t"
        "mul.s  $f9,    $f3,      $f7                                          \n\t"
        "mul.s  $f15,   $f2,      $f13                                         \n\t"
        "msub.s $f15,   $f15,     %[gain], $f6                                 \n\t"
        "sub.s  $f15,   $f15,     $f9                                          \n\t"
        "madd.s $f8,    $f15,     $f4,     $f13                                \n\t"
        "madd.s $f8,    $f8,      $f5,     $f7                                 \n\t"
        "swc1   $f14,   4(%[out])                                              \n\t"
        "lwc1   $f11,   12(%[in])                                              \n\t"
        "mul.s  $f12,   $f3,      $f13                                         \n\t"
        "mul.s  $f16,   $f2,      $f15                                         \n\t"
        "msub.s $f16,   $f16,     %[gain], $f11                                \n\t"
        "sub.s  $f16,   $f16,     $f12                                         \n\t"
        "madd.s $f14,   $f16,     $f4,     $f15                                \n\t"
        "madd.s $f14,   $f14,     $f5,     $f13                                \n\t"
        "swc1   $f8,    8(%[out])                                              \n\t"
        "lwc1   $f6,    16(%[in])                                              \n\t"
        "mul.s  $f9,    $f3,      $f15                                         \n\t"
        "mul.s  $f7,    $f2,      $f16                                         \n\t"
        "msub.s $f7,    $f7,      %[gain], $f6                                 \n\t"
        "sub.s  $f7,    $f7,      $f9                                          \n\t"
        "madd.s $f8,    $f7,      $f4,     $f16                                \n\t"
        "madd.s $f8,    $f8,      $f5,     $f15                                \n\t"
        "swc1   $f14,   12(%[out])                                             \n\t"
        "lwc1   $f11,   20(%[in])                                              \n\t"
        "mul.s  $f12,   $f3,      $f16                                         \n\t"
        "mul.s  $f13,   $f2,      $f7                                          \n\t"
        "msub.s $f13,   $f13,     %[gain], $f11                                \n\t"
        "sub.s  $f13,   $f13,     $f12                                         \n\t"
        "madd.s $f14,   $f13,     $f4,     $f7                                 \n\t"
        "madd.s $f14,   $f14,     $f5,     $f16                                \n\t"
        "swc1   $f8,    16(%[out])                                             \n\t"
        "lwc1   $f6,    24(%[in])                                              \n\t"
        "mul.s  $f9,    $f3,      $f7                                          \n\t"
        "mul.s  $f15,   $f2,      $f13                                         \n\t"
        "msub.s $f15,   $f15,     %[gain], $f6                                 \n\t"
        "sub.s  $f1,    $f15,     $f9                                          \n\t"
        "madd.s $f8,    $f1,      $f4,     $f13                                \n\t"
        "madd.s $f8,    $f8,      $f5,     $f7                                 \n\t"
        "swc1   $f14,   20(%[out])                                             \n\t"
        "lwc1   $f11,   28(%[in])                                              \n\t"
        "mul.s  $f12,   $f3,      $f13                                         \n\t"
        "mul.s  $f16,   $f2,      $f1                                          \n\t"
        "msub.s $f16,   $f16,     %[gain], $f11                                \n\t"
        "sub.s  $f0,    $f16,     $f12                                         \n\t"
        "madd.s $f14,   $f0,      $f4,     $f1                                 \n\t"
        "madd.s $f14,   $f14,     $f5,     $f13                                \n\t"
        "swc1   $f8,    24(%[out])                                             \n\t"
        "addiu  %[out], 32                                                     \n\t"
        "addiu  %[in],  32                                                     \n\t"
        "addiu  %[n],   -8                                                     \n\t"
        "swc1   $f14,   -4(%[out])                                             \n\t"
        "bnez   %[n],   ff_acelp_apply_order_2_transfer_function_madd%=        \n\t"
        "swc1   $f1,    4(%[mem])                                              \n\t"
        "swc1   $f0,    0(%[mem])                                              \n\t"

        "ff_acelp_apply_order_2_transfer_function_end%=:                       \n\t"

         : [out] "+r" (out),
           [in] "+r" (in), [gain] "+f" (gain),
           [n] "+r" (n), [mem] "+r" (mem)
         : [zero_coeffs] "r" (zero_coeffs),
           [pole_coeffs] "r" (pole_coeffs)
         : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5",
           "$f6", "$f7",  "$f8", "$f9", "$f10", "$f11",
           "$f12", "$f13", "$f14", "$f15", "$f16"
    );
}

void ff_acelp_filter_init_mips(ACELPFContext *c)
{
    c->acelp_interpolatef                      = ff_acelp_interpolatef_mips;
    c->acelp_apply_order_2_transfer_function   = ff_acelp_apply_order_2_transfer_function_mips;
}
