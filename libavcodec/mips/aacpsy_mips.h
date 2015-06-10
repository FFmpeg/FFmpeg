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
 * Author:  Bojan Zivkovic   (bojan@mips.com)
 *
 * AAC encoder psychoacoustic model routines optimized
 * for MIPS floating-point architecture
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
 * Reference: libavcodec/aacpsy.c
 */

#ifndef AVCODEC_MIPS_AACPSY_MIPS_H
#define AVCODEC_MIPS_AACPSY_MIPS_H

#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM && HAVE_MIPSFPU && ( PSY_LAME_FIR_LEN == 21 )
static void calc_thr_3gpp_mips(const FFPsyWindowInfo *wi, const int num_bands,
                               AacPsyChannel *pch, const uint8_t *band_sizes,
                               const float *coefs)
{
    int i, w, g;
    int start = 0;
    for (w = 0; w < wi->num_windows*16; w += 16) {
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band = &pch->band[w+g];

            float form_factor = 0.0f;
            float Temp;
            band->energy = 0.0f;
            for (i = 0; i < band_sizes[g]; i+=4) {
                float a, b, c, d;
                float ax, bx, cx, dx;
                float *cf = (float *)&coefs[start+i];

                __asm__ volatile (
                    "lwc1   %[a],   0(%[cf])                \n\t"
                    "lwc1   %[b],   4(%[cf])                \n\t"
                    "lwc1   %[c],   8(%[cf])                \n\t"
                    "lwc1   %[d],   12(%[cf])               \n\t"
                    "abs.s  %[a],   %[a]                    \n\t"
                    "abs.s  %[b],   %[b]                    \n\t"
                    "abs.s  %[c],   %[c]                    \n\t"
                    "abs.s  %[d],   %[d]                    \n\t"
                    "sqrt.s %[ax],  %[a]                    \n\t"
                    "sqrt.s %[bx],  %[b]                    \n\t"
                    "sqrt.s %[cx],  %[c]                    \n\t"
                    "sqrt.s %[dx],  %[d]                    \n\t"
                    "madd.s %[e],   %[e],   %[a],   %[a]    \n\t"
                    "madd.s %[e],   %[e],   %[b],   %[b]    \n\t"
                    "madd.s %[e],   %[e],   %[c],   %[c]    \n\t"
                    "madd.s %[e],   %[e],   %[d],   %[d]    \n\t"
                    "add.s  %[f],   %[f],   %[ax]           \n\t"
                    "add.s  %[f],   %[f],   %[bx]           \n\t"
                    "add.s  %[f],   %[f],   %[cx]           \n\t"
                    "add.s  %[f],   %[f],   %[dx]           \n\t"

                    : [a]"=&f"(a), [b]"=&f"(b),
                      [c]"=&f"(c), [d]"=&f"(d),
                      [e]"+f"(band->energy), [f]"+f"(form_factor),
                      [ax]"=&f"(ax), [bx]"=&f"(bx),
                      [cx]"=&f"(cx), [dx]"=&f"(dx)
                    : [cf]"r"(cf)
                    : "memory"
                );
            }

            Temp = sqrtf((float)band_sizes[g] / band->energy);
            band->thr      = band->energy * 0.001258925f;
            band->nz_lines = form_factor * sqrtf(Temp);
            start += band_sizes[g];
        }
    }
}

static void psy_hp_filter_mips(const float *firbuf, float *hpfsmpl, const float * psy_fir_coeffs)
{
    float sum1, sum2, sum3, sum4;
    float *fb = (float*)firbuf;
    float *fb_end = fb + AAC_BLOCK_SIZE_LONG;
    float *hp = hpfsmpl;

    float coeff0 = psy_fir_coeffs[1];
    float coeff1 = psy_fir_coeffs[3];
    float coeff2 = psy_fir_coeffs[5];
    float coeff3 = psy_fir_coeffs[7];
    float coeff4 = psy_fir_coeffs[9];

    __asm__ volatile (
        ".set push                                          \n\t"
        ".set noreorder                                     \n\t"

        "li.s   $f12,       32768                           \n\t"
        "1:                                                 \n\t"
        "lwc1   $f0,        40(%[fb])                       \n\t"
        "lwc1   $f1,        4(%[fb])                        \n\t"
        "lwc1   $f2,        80(%[fb])                       \n\t"
        "lwc1   $f3,        44(%[fb])                       \n\t"
        "lwc1   $f4,        8(%[fb])                        \n\t"
        "madd.s %[sum1],    $f0,        $f1,    %[coeff0]   \n\t"
        "lwc1   $f5,        84(%[fb])                       \n\t"
        "lwc1   $f6,        48(%[fb])                       \n\t"
        "madd.s %[sum2],    $f3,        $f4,    %[coeff0]   \n\t"
        "lwc1   $f7,        12(%[fb])                       \n\t"
        "madd.s %[sum1],    %[sum1],    $f2,    %[coeff0]   \n\t"
        "lwc1   $f8,        88(%[fb])                       \n\t"
        "lwc1   $f9,        52(%[fb])                       \n\t"
        "madd.s %[sum2],    %[sum2],    $f5,    %[coeff0]   \n\t"
        "madd.s %[sum3],    $f6,        $f7,    %[coeff0]   \n\t"
        "lwc1   $f10,       16(%[fb])                       \n\t"
        "lwc1   $f11,       92(%[fb])                       \n\t"
        "madd.s %[sum1],    %[sum1],    $f7,    %[coeff1]   \n\t"
        "lwc1   $f1,        72(%[fb])                       \n\t"
        "madd.s %[sum3],    %[sum3],    $f8,    %[coeff0]   \n\t"
        "madd.s %[sum4],    $f9,        $f10,   %[coeff0]   \n\t"
        "madd.s %[sum2],    %[sum2],    $f10,   %[coeff1]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f1,    %[coeff1]   \n\t"
        "lwc1   $f4,        76(%[fb])                       \n\t"
        "lwc1   $f8,        20(%[fb])                       \n\t"
        "madd.s %[sum4],    %[sum4],    $f11,   %[coeff0]   \n\t"
        "lwc1   $f11,       24(%[fb])                       \n\t"
        "madd.s %[sum2],    %[sum2],    $f4,    %[coeff1]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f8,    %[coeff2]   \n\t"
        "madd.s %[sum3],    %[sum3],    $f8,    %[coeff1]   \n\t"
        "madd.s %[sum4],    %[sum4],    $f11,   %[coeff1]   \n\t"
        "lwc1   $f7,        64(%[fb])                       \n\t"
        "madd.s %[sum2],    %[sum2],    $f11,   %[coeff2]   \n\t"
        "lwc1   $f10,       68(%[fb])                       \n\t"
        "madd.s %[sum3],    %[sum3],    $f2,    %[coeff1]   \n\t"
        "madd.s %[sum4],    %[sum4],    $f5,    %[coeff1]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f7,    %[coeff2]   \n\t"
        "madd.s %[sum2],    %[sum2],    $f10,   %[coeff2]   \n\t"
        "lwc1   $f2,        28(%[fb])                       \n\t"
        "lwc1   $f5,        32(%[fb])                       \n\t"
        "lwc1   $f8,        56(%[fb])                       \n\t"
        "lwc1   $f11,       60(%[fb])                       \n\t"
        "madd.s %[sum3],    %[sum3],    $f2,    %[coeff2]   \n\t"
        "madd.s %[sum4],    %[sum4],    $f5,    %[coeff2]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f2,    %[coeff3]   \n\t"
        "madd.s %[sum2],    %[sum2],    $f5,    %[coeff3]   \n\t"
        "madd.s %[sum3],    %[sum3],    $f1,    %[coeff2]   \n\t"
        "madd.s %[sum4],    %[sum4],    $f4,    %[coeff2]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f8,    %[coeff3]   \n\t"
        "madd.s %[sum2],    %[sum2],    $f11,   %[coeff3]   \n\t"
        "lwc1   $f1,        36(%[fb])                       \n\t"
        PTR_ADDIU "%[fb],   %[fb],      16                  \n\t"
        "madd.s %[sum4],    %[sum4],    $f0,    %[coeff3]   \n\t"
        "madd.s %[sum3],    %[sum3],    $f1,    %[coeff3]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f1,    %[coeff4]   \n\t"
        "madd.s %[sum2],    %[sum2],    $f0,    %[coeff4]   \n\t"
        "madd.s %[sum4],    %[sum4],    $f10,   %[coeff3]   \n\t"
        "madd.s %[sum3],    %[sum3],    $f7,    %[coeff3]   \n\t"
        "madd.s %[sum1],    %[sum1],    $f6,    %[coeff4]   \n\t"
        "madd.s %[sum2],    %[sum2],    $f9,    %[coeff4]   \n\t"
        "madd.s %[sum4],    %[sum4],    $f6,    %[coeff4]   \n\t"
        "madd.s %[sum3],    %[sum3],    $f3,    %[coeff4]   \n\t"
        "mul.s  %[sum1],    %[sum1],    $f12                \n\t"
        "mul.s  %[sum2],    %[sum2],    $f12                \n\t"
        "madd.s %[sum4],    %[sum4],    $f11,   %[coeff4]   \n\t"
        "madd.s %[sum3],    %[sum3],    $f8,    %[coeff4]   \n\t"
        "swc1   %[sum1],    0(%[hp])                        \n\t"
        "swc1   %[sum2],    4(%[hp])                        \n\t"
        "mul.s  %[sum4],    %[sum4],    $f12                \n\t"
        "mul.s  %[sum3],    %[sum3],    $f12                \n\t"
        "swc1   %[sum4],    12(%[hp])                       \n\t"
        "swc1   %[sum3],    8(%[hp])                        \n\t"
        "bne    %[fb],      %[fb_end],  1b                  \n\t"
        PTR_ADDIU "%[hp],   %[hp],      16                  \n\t"

        ".set pop                                           \n\t"

        : [sum1]"=&f"(sum1), [sum2]"=&f"(sum2),
          [sum3]"=&f"(sum3), [sum4]"=&f"(sum4),
          [fb]"+r"(fb), [hp]"+r"(hp)
        : [coeff0]"f"(coeff0), [coeff1]"f"(coeff1),
          [coeff2]"f"(coeff2), [coeff3]"f"(coeff3),
          [coeff4]"f"(coeff4), [fb_end]"r"(fb_end)
        : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6",
          "$f7", "$f8", "$f9", "$f10", "$f11", "$f12",
          "memory"
    );
}

#define calc_thr_3gpp calc_thr_3gpp_mips
#define psy_hp_filter psy_hp_filter_mips

#endif /* HAVE_INLINE_ASM && HAVE_MIPSFPU */
#endif /* AVCODEC_MIPS_AACPSY_MIPS_H */
