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
 * adaptive and fixed codebook vector operations for ACELP-based codecs
 * optimized for MIPS
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
 * Reference: libavcodec/acelp_vectors.c
 */
#include "config.h"
#include "libavcodec/acelp_vectors.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM
static void ff_weighted_vector_sumf_mips(
                  float *out, const float *in_a, const float *in_b,
                  float weight_coeff_a, float weight_coeff_b, int length)
{
    const float *a_end = in_a + length;

    /* loop unrolled two times */
    __asm__ volatile (
        "blez   %[length], ff_weighted_vector_sumf_end%=                     \n\t"

        "ff_weighted_vector_sumf_madd%=:                                     \n\t"
        "lwc1   $f0,       0(%[in_a])                                        \n\t"
        "lwc1   $f3,       4(%[in_a])                                        \n\t"
        "lwc1   $f1,       0(%[in_b])                                        \n\t"
        "lwc1   $f4,       4(%[in_b])                                        \n\t"
        "mul.s  $f2,       %[weight_coeff_a], $f0                            \n\t"
        "mul.s  $f5,       %[weight_coeff_a], $f3                            \n\t"
        "madd.s $f2,       $f2,               %[weight_coeff_b], $f1         \n\t"
        "madd.s $f5,       $f5,               %[weight_coeff_b], $f4         \n\t"
        PTR_ADDIU "%[in_a],8                                                 \n\t"
        PTR_ADDIU "%[in_b],8                                                 \n\t"
        "swc1   $f2,       0(%[out])                                         \n\t"
        "swc1   $f5,       4(%[out])                                         \n\t"
        PTR_ADDIU "%[out], 8                                                 \n\t"
        "bne   %[in_a],    %[a_end],          ff_weighted_vector_sumf_madd%= \n\t"

        "ff_weighted_vector_sumf_end%=:                                      \n\t"

        : [out] "+r" (out), [in_a] "+r" (in_a),   [in_b] "+r" (in_b)
        : [weight_coeff_a] "f" (weight_coeff_a),
          [weight_coeff_b] "f" (weight_coeff_b),
          [length] "r" (length), [a_end]"r"(a_end)
        : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "memory"
    );
}
#endif /* HAVE_INLINE_ASM */

void ff_acelp_vectors_init_mips(ACELPVContext *c)
{
#if HAVE_INLINE_ASM
    c->weighted_vector_sumf = ff_weighted_vector_sumf_mips;
#endif
}
