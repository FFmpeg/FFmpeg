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
 * Author:  Stanislav Ocovaj (socovaj@mips.com)
 *          Szabolcs Pal     (sabolc@mips.com)
 *
 * AAC coefficients encoder optimized for MIPS floating-point architecture
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
 * Reference: libavcodec/aaccoder.c
 */

#include "libavutil/libm.h"

#include <float.h>
#include "libavutil/mathematics.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/aac.h"
#include "libavcodec/aacenc.h"
#include "libavcodec/aactab.h"
#include "libavcodec/aacenctab.h"
#include "libavcodec/aacenc_utils.h"

#if HAVE_INLINE_ASM
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
typedef struct BandCodingPath {
    int prev_idx;
    float cost;
    int run;
} BandCodingPath;

static const uint8_t uquad_sign_bits[81] = {
    0, 1, 1, 1, 2, 2, 1, 2, 2,
    1, 2, 2, 2, 3, 3, 2, 3, 3,
    1, 2, 2, 2, 3, 3, 2, 3, 3,
    1, 2, 2, 2, 3, 3, 2, 3, 3,
    2, 3, 3, 3, 4, 4, 3, 4, 4,
    2, 3, 3, 3, 4, 4, 3, 4, 4,
    1, 2, 2, 2, 3, 3, 2, 3, 3,
    2, 3, 3, 3, 4, 4, 3, 4, 4,
    2, 3, 3, 3, 4, 4, 3, 4, 4
};

static const uint8_t upair7_sign_bits[64] = {
    0, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2,
};

static const uint8_t upair12_sign_bits[169] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};

static const uint8_t esc_sign_bits[289] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};

/**
 * Functions developed from template function and optimized for quantizing and encoding band
 */
static void quantize_and_encode_band_cost_SQUAD_mips(struct AACEncContext *s,
                                                     PutBitContext *pb, const float *in, float *out,
                                                     const float *scaled, int size, int scale_idx,
                                                     int cb, const float lambda, const float uplim,
                                                     int *bits, float *energy, const float ROUNDING)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    float qenergy = 0.0f;

    uint8_t  *p_bits  = (uint8_t  *)ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t *)ff_aac_spectral_codes[cb-1];
    float    *p_vec   = (float    *)ff_aac_codebook_vectors[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx;
        int *in_int = (int *)&in[i];
        int t0, t1, t2, t3, t4, t5, t6, t7;
        const float *vec;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "slt    %[qc1], $zero,  %[qc1]  \n\t"
            "slt    %[qc2], $zero,  %[qc2]  \n\t"
            "slt    %[qc3], $zero,  %[qc3]  \n\t"
            "slt    %[qc4], $zero,  %[qc4]  \n\t"
            "lw     %[t0],  0(%[in_int])    \n\t"
            "lw     %[t1],  4(%[in_int])    \n\t"
            "lw     %[t2],  8(%[in_int])    \n\t"
            "lw     %[t3],  12(%[in_int])   \n\t"
            "srl    %[t0],  %[t0],  31      \n\t"
            "srl    %[t1],  %[t1],  31      \n\t"
            "srl    %[t2],  %[t2],  31      \n\t"
            "srl    %[t3],  %[t3],  31      \n\t"
            "subu   %[t4],  $zero,  %[qc1]  \n\t"
            "subu   %[t5],  $zero,  %[qc2]  \n\t"
            "subu   %[t6],  $zero,  %[qc3]  \n\t"
            "subu   %[t7],  $zero,  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t5],  %[t1]   \n\t"
            "movn   %[qc3], %[t6],  %[t2]   \n\t"
            "movn   %[qc4], %[t7],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4), [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = qc1;
        curidx *= 3;
        curidx += qc2;
        curidx *= 3;
        curidx += qc3;
        curidx *= 3;
        curidx += qc4;
        curidx += 40;

        put_bits(pb, p_bits[curidx], p_codes[curidx]);

        if (out || energy) {
            float e1,e2,e3,e4;
            vec = &p_vec[curidx*4];
            e1 = vec[0] * IQ;
            e2 = vec[1] * IQ;
            e3 = vec[2] * IQ;
            e4 = vec[3] * IQ;
            if (out) {
                out[i+0] = e1;
                out[i+1] = e2;
                out[i+2] = e3;
                out[i+3] = e4;
            }
            if (energy)
                qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
        }
    }
    if (energy)
        *energy = qenergy;
}

static void quantize_and_encode_band_cost_UQUAD_mips(struct AACEncContext *s,
                                                     PutBitContext *pb, const float *in, float *out,
                                                     const float *scaled, int size, int scale_idx,
                                                     int cb, const float lambda, const float uplim,
                                                     int *bits, float *energy, const float ROUNDING)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    float qenergy = 0.0f;

    uint8_t  *p_bits  = (uint8_t  *)ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t *)ff_aac_spectral_codes[cb-1];
    float    *p_vec   = (float    *)ff_aac_codebook_vectors[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx, sign, count;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;
        int t0, t1, t2, t3, t4;
        const float *vec;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                              \n\t"
            ".set noreorder                         \n\t"

            "ori    %[t4],      $zero,      2       \n\t"
            "ori    %[sign],    $zero,      0       \n\t"
            "slt    %[t0],      %[t4],      %[qc1]  \n\t"
            "slt    %[t1],      %[t4],      %[qc2]  \n\t"
            "slt    %[t2],      %[t4],      %[qc3]  \n\t"
            "slt    %[t3],      %[t4],      %[qc4]  \n\t"
            "movn   %[qc1],     %[t4],      %[t0]   \n\t"
            "movn   %[qc2],     %[t4],      %[t1]   \n\t"
            "movn   %[qc3],     %[t4],      %[t2]   \n\t"
            "movn   %[qc4],     %[t4],      %[t3]   \n\t"
            "lw     %[t0],      0(%[in_int])        \n\t"
            "lw     %[t1],      4(%[in_int])        \n\t"
            "lw     %[t2],      8(%[in_int])        \n\t"
            "lw     %[t3],      12(%[in_int])       \n\t"
            "slt    %[t0],      %[t0],      $zero   \n\t"
            "movn   %[sign],    %[t0],      %[qc1]  \n\t"
            "slt    %[t1],      %[t1],      $zero   \n\t"
            "slt    %[t2],      %[t2],      $zero   \n\t"
            "slt    %[t3],      %[t3],      $zero   \n\t"
            "sll    %[t0],      %[sign],    1       \n\t"
            "or     %[t0],      %[t0],      %[t1]   \n\t"
            "movn   %[sign],    %[t0],      %[qc2]  \n\t"
            "slt    %[t4],      $zero,      %[qc1]  \n\t"
            "slt    %[t1],      $zero,      %[qc2]  \n\t"
            "slt    %[count],   $zero,      %[qc3]  \n\t"
            "sll    %[t0],      %[sign],    1       \n\t"
            "or     %[t0],      %[t0],      %[t2]   \n\t"
            "movn   %[sign],    %[t0],      %[qc3]  \n\t"
            "slt    %[t2],      $zero,      %[qc4]  \n\t"
            "addu   %[count],   %[count],   %[t4]   \n\t"
            "addu   %[count],   %[count],   %[t1]   \n\t"
            "sll    %[t0],      %[sign],    1       \n\t"
            "or     %[t0],      %[t0],      %[t3]   \n\t"
            "movn   %[sign],    %[t0],      %[qc4]  \n\t"
            "addu   %[count],   %[count],   %[t2]   \n\t"

            ".set pop                               \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign]"=&r"(sign), [count]"=&r"(count),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = qc1;
        curidx *= 3;
        curidx += qc2;
        curidx *= 3;
        curidx += qc3;
        curidx *= 3;
        curidx += qc4;

        v_codes = (p_codes[curidx] << count) | (sign & ((1 << count) - 1));
        v_bits  = p_bits[curidx] + count;
        put_bits(pb, v_bits, v_codes);

        if (out || energy) {
            float e1,e2,e3,e4;
            vec = &p_vec[curidx*4];
            e1 = copysignf(vec[0] * IQ, in[i+0]);
            e2 = copysignf(vec[1] * IQ, in[i+1]);
            e3 = copysignf(vec[2] * IQ, in[i+2]);
            e4 = copysignf(vec[3] * IQ, in[i+3]);
            if (out) {
                out[i+0] = e1;
                out[i+1] = e2;
                out[i+2] = e3;
                out[i+3] = e4;
            }
            if (energy)
                qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
        }
    }
    if (energy)
        *energy = qenergy;
}

static void quantize_and_encode_band_cost_SPAIR_mips(struct AACEncContext *s,
                                                     PutBitContext *pb, const float *in, float *out,
                                                     const float *scaled, int size, int scale_idx,
                                                     int cb, const float lambda, const float uplim,
                                                     int *bits, float *energy, const float ROUNDING)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    float qenergy = 0.0f;

    uint8_t  *p_bits  = (uint8_t  *)ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t *)ff_aac_spectral_codes[cb-1];
    float    *p_vec   = (float    *)ff_aac_codebook_vectors[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx, curidx2;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;
        int t0, t1, t2, t3, t4, t5, t6, t7;
        const float *vec1, *vec2;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    %[t4],  $zero,  4       \n\t"
            "slt    %[t0],  %[t4],  %[qc1]  \n\t"
            "slt    %[t1],  %[t4],  %[qc2]  \n\t"
            "slt    %[t2],  %[t4],  %[qc3]  \n\t"
            "slt    %[t3],  %[t4],  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t4],  %[t1]   \n\t"
            "movn   %[qc3], %[t4],  %[t2]   \n\t"
            "movn   %[qc4], %[t4],  %[t3]   \n\t"
            "lw     %[t0],  0(%[in_int])    \n\t"
            "lw     %[t1],  4(%[in_int])    \n\t"
            "lw     %[t2],  8(%[in_int])    \n\t"
            "lw     %[t3],  12(%[in_int])   \n\t"
            "srl    %[t0],  %[t0],  31      \n\t"
            "srl    %[t1],  %[t1],  31      \n\t"
            "srl    %[t2],  %[t2],  31      \n\t"
            "srl    %[t3],  %[t3],  31      \n\t"
            "subu   %[t4],  $zero,  %[qc1]  \n\t"
            "subu   %[t5],  $zero,  %[qc2]  \n\t"
            "subu   %[t6],  $zero,  %[qc3]  \n\t"
            "subu   %[t7],  $zero,  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t5],  %[t1]   \n\t"
            "movn   %[qc3], %[t6],  %[t2]   \n\t"
            "movn   %[qc4], %[t7],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4), [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = 9 * qc1;
        curidx += qc2 + 40;

        curidx2 = 9 * qc3;
        curidx2 += qc4 + 40;

        v_codes = (p_codes[curidx] << p_bits[curidx2]) | (p_codes[curidx2]);
        v_bits  = p_bits[curidx] + p_bits[curidx2];
        put_bits(pb, v_bits, v_codes);

        if (out || energy) {
            float e1,e2,e3,e4;
            vec1 = &p_vec[curidx*2 ];
            vec2 = &p_vec[curidx2*2];
            e1 = vec1[0] * IQ;
            e2 = vec1[1] * IQ;
            e3 = vec2[0] * IQ;
            e4 = vec2[1] * IQ;
            if (out) {
                out[i+0] = e1;
                out[i+1] = e2;
                out[i+2] = e3;
                out[i+3] = e4;
            }
            if (energy)
                qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
        }
    }
    if (energy)
        *energy = qenergy;
}

static void quantize_and_encode_band_cost_UPAIR7_mips(struct AACEncContext *s,
                                                      PutBitContext *pb, const float *in, float *out,
                                                      const float *scaled, int size, int scale_idx,
                                                      int cb, const float lambda, const float uplim,
                                                      int *bits, float *energy, const float ROUNDING)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    float qenergy = 0.0f;

    uint8_t  *p_bits  = (uint8_t*) ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t*)ff_aac_spectral_codes[cb-1];
    float    *p_vec   = (float    *)ff_aac_codebook_vectors[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx1, curidx2, sign1, count1, sign2, count2;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;
        int t0, t1, t2, t3, t4;
        const float *vec1, *vec2;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                              \n\t"
            ".set noreorder                         \n\t"

            "ori    %[t4],      $zero,      7       \n\t"
            "ori    %[sign1],   $zero,      0       \n\t"
            "ori    %[sign2],   $zero,      0       \n\t"
            "slt    %[t0],      %[t4],      %[qc1]  \n\t"
            "slt    %[t1],      %[t4],      %[qc2]  \n\t"
            "slt    %[t2],      %[t4],      %[qc3]  \n\t"
            "slt    %[t3],      %[t4],      %[qc4]  \n\t"
            "movn   %[qc1],     %[t4],      %[t0]   \n\t"
            "movn   %[qc2],     %[t4],      %[t1]   \n\t"
            "movn   %[qc3],     %[t4],      %[t2]   \n\t"
            "movn   %[qc4],     %[t4],      %[t3]   \n\t"
            "lw     %[t0],      0(%[in_int])        \n\t"
            "lw     %[t1],      4(%[in_int])        \n\t"
            "lw     %[t2],      8(%[in_int])        \n\t"
            "lw     %[t3],      12(%[in_int])       \n\t"
            "slt    %[t0],      %[t0],      $zero   \n\t"
            "movn   %[sign1],   %[t0],      %[qc1]  \n\t"
            "slt    %[t2],      %[t2],      $zero   \n\t"
            "movn   %[sign2],   %[t2],      %[qc3]  \n\t"
            "slt    %[t1],      %[t1],      $zero   \n\t"
            "sll    %[t0],      %[sign1],   1       \n\t"
            "or     %[t0],      %[t0],      %[t1]   \n\t"
            "movn   %[sign1],   %[t0],      %[qc2]  \n\t"
            "slt    %[t3],      %[t3],      $zero   \n\t"
            "sll    %[t0],      %[sign2],   1       \n\t"
            "or     %[t0],      %[t0],      %[t3]   \n\t"
            "movn   %[sign2],   %[t0],      %[qc4]  \n\t"
            "slt    %[count1],  $zero,      %[qc1]  \n\t"
            "slt    %[t1],      $zero,      %[qc2]  \n\t"
            "slt    %[count2],  $zero,      %[qc3]  \n\t"
            "slt    %[t2],      $zero,      %[qc4]  \n\t"
            "addu   %[count1],  %[count1],  %[t1]   \n\t"
            "addu   %[count2],  %[count2],  %[t2]   \n\t"

            ".set pop                               \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3", "t4",
              "memory"
        );

        curidx1  = 8 * qc1;
        curidx1 += qc2;

        v_codes = (p_codes[curidx1] << count1) | sign1;
        v_bits  = p_bits[curidx1] + count1;
        put_bits(pb, v_bits, v_codes);

        curidx2  = 8 * qc3;
        curidx2 += qc4;

        v_codes = (p_codes[curidx2] << count2) | sign2;
        v_bits  = p_bits[curidx2] + count2;
        put_bits(pb, v_bits, v_codes);

        if (out || energy) {
            float e1,e2,e3,e4;
            vec1 = &p_vec[curidx1*2];
            vec2 = &p_vec[curidx2*2];
            e1 = copysignf(vec1[0] * IQ, in[i+0]);
            e2 = copysignf(vec1[1] * IQ, in[i+1]);
            e3 = copysignf(vec2[0] * IQ, in[i+2]);
            e4 = copysignf(vec2[1] * IQ, in[i+3]);
            if (out) {
                out[i+0] = e1;
                out[i+1] = e2;
                out[i+2] = e3;
                out[i+3] = e4;
            }
            if (energy)
                qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
        }
    }
    if (energy)
        *energy = qenergy;
}

static void quantize_and_encode_band_cost_UPAIR12_mips(struct AACEncContext *s,
                                                       PutBitContext *pb, const float *in, float *out,
                                                       const float *scaled, int size, int scale_idx,
                                                       int cb, const float lambda, const float uplim,
                                                       int *bits, float *energy, const float ROUNDING)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    float qenergy = 0.0f;

    uint8_t  *p_bits  = (uint8_t*) ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t*)ff_aac_spectral_codes[cb-1];
    float    *p_vec   = (float   *)ff_aac_codebook_vectors[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx1, curidx2, sign1, count1, sign2, count2;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;
        int t0, t1, t2, t3, t4;
        const float *vec1, *vec2;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                              \n\t"
            ".set noreorder                         \n\t"

            "ori    %[t4],      $zero,      12      \n\t"
            "ori    %[sign1],   $zero,      0       \n\t"
            "ori    %[sign2],   $zero,      0       \n\t"
            "slt    %[t0],      %[t4],      %[qc1]  \n\t"
            "slt    %[t1],      %[t4],      %[qc2]  \n\t"
            "slt    %[t2],      %[t4],      %[qc3]  \n\t"
            "slt    %[t3],      %[t4],      %[qc4]  \n\t"
            "movn   %[qc1],     %[t4],      %[t0]   \n\t"
            "movn   %[qc2],     %[t4],      %[t1]   \n\t"
            "movn   %[qc3],     %[t4],      %[t2]   \n\t"
            "movn   %[qc4],     %[t4],      %[t3]   \n\t"
            "lw     %[t0],      0(%[in_int])        \n\t"
            "lw     %[t1],      4(%[in_int])        \n\t"
            "lw     %[t2],      8(%[in_int])        \n\t"
            "lw     %[t3],      12(%[in_int])       \n\t"
            "slt    %[t0],      %[t0],      $zero   \n\t"
            "movn   %[sign1],   %[t0],      %[qc1]  \n\t"
            "slt    %[t2],      %[t2],      $zero   \n\t"
            "movn   %[sign2],   %[t2],      %[qc3]  \n\t"
            "slt    %[t1],      %[t1],      $zero   \n\t"
            "sll    %[t0],      %[sign1],   1       \n\t"
            "or     %[t0],      %[t0],      %[t1]   \n\t"
            "movn   %[sign1],   %[t0],      %[qc2]  \n\t"
            "slt    %[t3],      %[t3],      $zero   \n\t"
            "sll    %[t0],      %[sign2],   1       \n\t"
            "or     %[t0],      %[t0],      %[t3]   \n\t"
            "movn   %[sign2],   %[t0],      %[qc4]  \n\t"
            "slt    %[count1],  $zero,      %[qc1]  \n\t"
            "slt    %[t1],      $zero,      %[qc2]  \n\t"
            "slt    %[count2],  $zero,      %[qc3]  \n\t"
            "slt    %[t2],      $zero,      %[qc4]  \n\t"
            "addu   %[count1],  %[count1],  %[t1]   \n\t"
            "addu   %[count2],  %[count2],  %[t2]   \n\t"

            ".set pop                               \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx1  = 13 * qc1;
        curidx1 += qc2;

        v_codes = (p_codes[curidx1] << count1) | sign1;
        v_bits  = p_bits[curidx1] + count1;
        put_bits(pb, v_bits, v_codes);

        curidx2  = 13 * qc3;
        curidx2 += qc4;

        v_codes = (p_codes[curidx2] << count2) | sign2;
        v_bits  = p_bits[curidx2] + count2;
        put_bits(pb, v_bits, v_codes);

        if (out || energy) {
            float e1,e2,e3,e4;
            vec1 = &p_vec[curidx1*2];
            vec2 = &p_vec[curidx2*2];
            e1 = copysignf(vec1[0] * IQ, in[i+0]);
            e2 = copysignf(vec1[1] * IQ, in[i+1]);
            e3 = copysignf(vec2[0] * IQ, in[i+2]);
            e4 = copysignf(vec2[1] * IQ, in[i+3]);
            if (out) {
                out[i+0] = e1;
                out[i+1] = e2;
                out[i+2] = e3;
                out[i+3] = e4;
            }
            if (energy)
                qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
        }
    }
    if (energy)
        *energy = qenergy;
}

static void quantize_and_encode_band_cost_ESC_mips(struct AACEncContext *s,
                                                   PutBitContext *pb, const float *in, float *out,
                                                   const float *scaled, int size, int scale_idx,
                                                   int cb, const float lambda, const float uplim,
                                                   int *bits, float *energy, const float ROUNDING)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    float qenergy = 0.0f;

    uint8_t  *p_bits    = (uint8_t* )ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes   = (uint16_t*)ff_aac_spectral_codes[cb-1];
    float    *p_vectors = (float*   )ff_aac_codebook_vectors[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;

    if (cb < 11) {
        for (i = 0; i < size; i += 4) {
            int curidx, curidx2, sign1, count1, sign2, count2;
            int *in_int = (int *)&in[i];
            uint8_t v_bits;
            unsigned int v_codes;
            int t0, t1, t2, t3, t4;
            const float *vec1, *vec2;

            qc1 = scaled[i  ] * Q34 + ROUNDING;
            qc2 = scaled[i+1] * Q34 + ROUNDING;
            qc3 = scaled[i+2] * Q34 + ROUNDING;
            qc4 = scaled[i+3] * Q34 + ROUNDING;

            __asm__ volatile (
                ".set push                                  \n\t"
                ".set noreorder                             \n\t"

                "ori        %[t4],      $zero,      16      \n\t"
                "ori        %[sign1],   $zero,      0       \n\t"
                "ori        %[sign2],   $zero,      0       \n\t"
                "slt        %[t0],      %[t4],      %[qc1]  \n\t"
                "slt        %[t1],      %[t4],      %[qc2]  \n\t"
                "slt        %[t2],      %[t4],      %[qc3]  \n\t"
                "slt        %[t3],      %[t4],      %[qc4]  \n\t"
                "movn       %[qc1],     %[t4],      %[t0]   \n\t"
                "movn       %[qc2],     %[t4],      %[t1]   \n\t"
                "movn       %[qc3],     %[t4],      %[t2]   \n\t"
                "movn       %[qc4],     %[t4],      %[t3]   \n\t"
                "lw         %[t0],      0(%[in_int])        \n\t"
                "lw         %[t1],      4(%[in_int])        \n\t"
                "lw         %[t2],      8(%[in_int])        \n\t"
                "lw         %[t3],      12(%[in_int])       \n\t"
                "slt        %[t0],      %[t0],      $zero   \n\t"
                "movn       %[sign1],   %[t0],      %[qc1]  \n\t"
                "slt        %[t2],      %[t2],      $zero   \n\t"
                "movn       %[sign2],   %[t2],      %[qc3]  \n\t"
                "slt        %[t1],      %[t1],      $zero   \n\t"
                "sll        %[t0],      %[sign1],   1       \n\t"
                "or         %[t0],      %[t0],      %[t1]   \n\t"
                "movn       %[sign1],   %[t0],      %[qc2]  \n\t"
                "slt        %[t3],      %[t3],      $zero   \n\t"
                "sll        %[t0],      %[sign2],   1       \n\t"
                "or         %[t0],      %[t0],      %[t3]   \n\t"
                "movn       %[sign2],   %[t0],      %[qc4]  \n\t"
                "slt        %[count1],  $zero,      %[qc1]  \n\t"
                "slt        %[t1],      $zero,      %[qc2]  \n\t"
                "slt        %[count2],  $zero,      %[qc3]  \n\t"
                "slt        %[t2],      $zero,      %[qc4]  \n\t"
                "addu       %[count1],  %[count1],  %[t1]   \n\t"
                "addu       %[count2],  %[count2],  %[t2]   \n\t"

                ".set pop                                   \n\t"

                : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
                  [qc3]"+r"(qc3), [qc4]"+r"(qc4),
                  [sign1]"=&r"(sign1), [count1]"=&r"(count1),
                  [sign2]"=&r"(sign2), [count2]"=&r"(count2),
                  [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
                  [t4]"=&r"(t4)
                : [in_int]"r"(in_int)
                : "memory"
            );

            curidx = 17 * qc1;
            curidx += qc2;
            curidx2 = 17 * qc3;
            curidx2 += qc4;

            v_codes = (p_codes[curidx] << count1) | sign1;
            v_bits  = p_bits[curidx] + count1;
            put_bits(pb, v_bits, v_codes);

            v_codes = (p_codes[curidx2] << count2) | sign2;
            v_bits  = p_bits[curidx2] + count2;
            put_bits(pb, v_bits, v_codes);

            if (out || energy) {
                float e1,e2,e3,e4;
                vec1 = &p_vectors[curidx*2 ];
                vec2 = &p_vectors[curidx2*2];
                e1 = copysignf(vec1[0] * IQ, in[i+0]);
                e2 = copysignf(vec1[1] * IQ, in[i+1]);
                e3 = copysignf(vec2[0] * IQ, in[i+2]);
                e4 = copysignf(vec2[1] * IQ, in[i+3]);
                if (out) {
                    out[i+0] = e1;
                    out[i+1] = e2;
                    out[i+2] = e3;
                    out[i+3] = e4;
                }
                if (energy)
                    qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
            }
        }
    } else {
        for (i = 0; i < size; i += 4) {
            int curidx, curidx2, sign1, count1, sign2, count2;
            int *in_int = (int *)&in[i];
            uint8_t v_bits;
            unsigned int v_codes;
            int c1, c2, c3, c4;
            int t0, t1, t2, t3, t4;

            qc1 = scaled[i  ] * Q34 + ROUNDING;
            qc2 = scaled[i+1] * Q34 + ROUNDING;
            qc3 = scaled[i+2] * Q34 + ROUNDING;
            qc4 = scaled[i+3] * Q34 + ROUNDING;

            __asm__ volatile (
                ".set push                                  \n\t"
                ".set noreorder                             \n\t"

                "ori        %[t4],      $zero,      16      \n\t"
                "ori        %[sign1],   $zero,      0       \n\t"
                "ori        %[sign2],   $zero,      0       \n\t"
                "shll_s.w   %[c1],      %[qc1],     18      \n\t"
                "shll_s.w   %[c2],      %[qc2],     18      \n\t"
                "shll_s.w   %[c3],      %[qc3],     18      \n\t"
                "shll_s.w   %[c4],      %[qc4],     18      \n\t"
                "srl        %[c1],      %[c1],      18      \n\t"
                "srl        %[c2],      %[c2],      18      \n\t"
                "srl        %[c3],      %[c3],      18      \n\t"
                "srl        %[c4],      %[c4],      18      \n\t"
                "slt        %[t0],      %[t4],      %[qc1]  \n\t"
                "slt        %[t1],      %[t4],      %[qc2]  \n\t"
                "slt        %[t2],      %[t4],      %[qc3]  \n\t"
                "slt        %[t3],      %[t4],      %[qc4]  \n\t"
                "movn       %[qc1],     %[t4],      %[t0]   \n\t"
                "movn       %[qc2],     %[t4],      %[t1]   \n\t"
                "movn       %[qc3],     %[t4],      %[t2]   \n\t"
                "movn       %[qc4],     %[t4],      %[t3]   \n\t"
                "lw         %[t0],      0(%[in_int])        \n\t"
                "lw         %[t1],      4(%[in_int])        \n\t"
                "lw         %[t2],      8(%[in_int])        \n\t"
                "lw         %[t3],      12(%[in_int])       \n\t"
                "slt        %[t0],      %[t0],      $zero   \n\t"
                "movn       %[sign1],   %[t0],      %[qc1]  \n\t"
                "slt        %[t2],      %[t2],      $zero   \n\t"
                "movn       %[sign2],   %[t2],      %[qc3]  \n\t"
                "slt        %[t1],      %[t1],      $zero   \n\t"
                "sll        %[t0],      %[sign1],   1       \n\t"
                "or         %[t0],      %[t0],      %[t1]   \n\t"
                "movn       %[sign1],   %[t0],      %[qc2]  \n\t"
                "slt        %[t3],      %[t3],      $zero   \n\t"
                "sll        %[t0],      %[sign2],   1       \n\t"
                "or         %[t0],      %[t0],      %[t3]   \n\t"
                "movn       %[sign2],   %[t0],      %[qc4]  \n\t"
                "slt        %[count1],  $zero,      %[qc1]  \n\t"
                "slt        %[t1],      $zero,      %[qc2]  \n\t"
                "slt        %[count2],  $zero,      %[qc3]  \n\t"
                "slt        %[t2],      $zero,      %[qc4]  \n\t"
                "addu       %[count1],  %[count1],  %[t1]   \n\t"
                "addu       %[count2],  %[count2],  %[t2]   \n\t"

                ".set pop                                   \n\t"

                : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
                  [qc3]"+r"(qc3), [qc4]"+r"(qc4),
                  [sign1]"=&r"(sign1), [count1]"=&r"(count1),
                  [sign2]"=&r"(sign2), [count2]"=&r"(count2),
                  [c1]"=&r"(c1), [c2]"=&r"(c2),
                  [c3]"=&r"(c3), [c4]"=&r"(c4),
                  [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
                  [t4]"=&r"(t4)
                : [in_int]"r"(in_int)
                : "memory"
            );

            curidx = 17 * qc1;
            curidx += qc2;

            curidx2 = 17 * qc3;
            curidx2 += qc4;

            v_codes = (p_codes[curidx] << count1) | sign1;
            v_bits  = p_bits[curidx] + count1;
            put_bits(pb, v_bits, v_codes);

            if (p_vectors[curidx*2  ] == 64.0f) {
                int len = av_log2(c1);
                v_codes = (((1 << (len - 3)) - 2) << len) | (c1 & ((1 << len) - 1));
                put_bits(pb, len * 2 - 3, v_codes);
            }
            if (p_vectors[curidx*2+1] == 64.0f) {
                int len = av_log2(c2);
                v_codes = (((1 << (len - 3)) - 2) << len) | (c2 & ((1 << len) - 1));
                put_bits(pb, len*2-3, v_codes);
            }

            v_codes = (p_codes[curidx2] << count2) | sign2;
            v_bits  = p_bits[curidx2] + count2;
            put_bits(pb, v_bits, v_codes);

            if (p_vectors[curidx2*2  ] == 64.0f) {
                int len = av_log2(c3);
                v_codes = (((1 << (len - 3)) - 2) << len) | (c3 & ((1 << len) - 1));
                put_bits(pb, len* 2 - 3, v_codes);
            }
            if (p_vectors[curidx2*2+1] == 64.0f) {
                int len = av_log2(c4);
                v_codes = (((1 << (len - 3)) - 2) << len) | (c4 & ((1 << len) - 1));
                put_bits(pb, len * 2 - 3, v_codes);
            }

            if (out || energy) {
                float e1, e2, e3, e4;
                e1 = copysignf(c1 * cbrtf(c1) * IQ, in[i+0]);
                e2 = copysignf(c2 * cbrtf(c2) * IQ, in[i+1]);
                e3 = copysignf(c3 * cbrtf(c3) * IQ, in[i+2]);
                e4 = copysignf(c4 * cbrtf(c4) * IQ, in[i+3]);
                if (out) {
                    out[i+0] = e1;
                    out[i+1] = e2;
                    out[i+2] = e3;
                    out[i+3] = e4;
                }
                if (energy)
                    qenergy += (e1*e1 + e2*e2) + (e3*e3 + e4*e4);
            }
        }
    }
    if (energy)
        *energy = qenergy;
}

static void quantize_and_encode_band_cost_NONE_mips(struct AACEncContext *s,
                                                         PutBitContext *pb, const float *in, float *out,
                                                         const float *scaled, int size, int scale_idx,
                                                         int cb, const float lambda, const float uplim,
                                                         int *bits, float *energy, const float ROUNDING) {
    av_assert0(0);
}

static void quantize_and_encode_band_cost_ZERO_mips(struct AACEncContext *s,
                                                         PutBitContext *pb, const float *in, float *out,
                                                         const float *scaled, int size, int scale_idx,
                                                         int cb, const float lambda, const float uplim,
                                                         int *bits, float *energy, const float ROUNDING) {
    int i;
    if (bits)
        *bits = 0;
    if (out) {
        for (i = 0; i < size; i += 4) {
           out[i  ] = 0.0f;
           out[i+1] = 0.0f;
           out[i+2] = 0.0f;
           out[i+3] = 0.0f;
        }
    }
    if (energy)
        *energy = 0.0f;
}

static void (*const quantize_and_encode_band_cost_arr[])(struct AACEncContext *s,
                                                         PutBitContext *pb, const float *in, float *out,
                                                         const float *scaled, int size, int scale_idx,
                                                         int cb, const float lambda, const float uplim,
                                                         int *bits, float *energy, const float ROUNDING) = {
    quantize_and_encode_band_cost_ZERO_mips,
    quantize_and_encode_band_cost_SQUAD_mips,
    quantize_and_encode_band_cost_SQUAD_mips,
    quantize_and_encode_band_cost_UQUAD_mips,
    quantize_and_encode_band_cost_UQUAD_mips,
    quantize_and_encode_band_cost_SPAIR_mips,
    quantize_and_encode_band_cost_SPAIR_mips,
    quantize_and_encode_band_cost_UPAIR7_mips,
    quantize_and_encode_band_cost_UPAIR7_mips,
    quantize_and_encode_band_cost_UPAIR12_mips,
    quantize_and_encode_band_cost_UPAIR12_mips,
    quantize_and_encode_band_cost_ESC_mips,
    quantize_and_encode_band_cost_NONE_mips, /* cb 12 doesn't exist */
    quantize_and_encode_band_cost_ZERO_mips,
    quantize_and_encode_band_cost_ZERO_mips,
    quantize_and_encode_band_cost_ZERO_mips,
};

#define quantize_and_encode_band_cost(                                       \
                                s, pb, in, out, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits, energy, ROUNDING)       \
    quantize_and_encode_band_cost_arr[cb](                                   \
                                s, pb, in, out, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits, energy, ROUNDING)

static void quantize_and_encode_band_mips(struct AACEncContext *s, PutBitContext *pb,
                                          const float *in, float *out, int size, int scale_idx,
                                          int cb, const float lambda, int rtz)
{
    quantize_and_encode_band_cost(s, pb, in, out, NULL, size, scale_idx, cb, lambda,
                                  INFINITY, NULL, NULL, (rtz) ? ROUND_TO_ZERO : ROUND_STANDARD);
}

/**
 * Functions developed from template function and optimized for getting the number of bits
 */
static float get_band_numbits_ZERO_mips(struct AACEncContext *s,
                                        PutBitContext *pb, const float *in,
                                        const float *scaled, int size, int scale_idx,
                                        int cb, const float lambda, const float uplim,
                                        int *bits)
{
    return 0;
}

static float get_band_numbits_NONE_mips(struct AACEncContext *s,
                                        PutBitContext *pb, const float *in,
                                        const float *scaled, int size, int scale_idx,
                                        int cb, const float lambda, const float uplim,
                                        int *bits)
{
    av_assert0(0);
    return 0;
}

static float get_band_numbits_SQUAD_mips(struct AACEncContext *s,
                                         PutBitContext *pb, const float *in,
                                         const float *scaled, int size, int scale_idx,
                                         int cb, const float lambda, const float uplim,
                                         int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits = (uint8_t *)ff_aac_spectral_bits[cb-1];

    for (i = 0; i < size; i += 4) {
        int curidx;
        int *in_int = (int *)&in[i];
        int t0, t1, t2, t3, t4, t5, t6, t7;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "slt    %[qc1], $zero,  %[qc1]  \n\t"
            "slt    %[qc2], $zero,  %[qc2]  \n\t"
            "slt    %[qc3], $zero,  %[qc3]  \n\t"
            "slt    %[qc4], $zero,  %[qc4]  \n\t"
            "lw     %[t0],  0(%[in_int])    \n\t"
            "lw     %[t1],  4(%[in_int])    \n\t"
            "lw     %[t2],  8(%[in_int])    \n\t"
            "lw     %[t3],  12(%[in_int])   \n\t"
            "srl    %[t0],  %[t0],  31      \n\t"
            "srl    %[t1],  %[t1],  31      \n\t"
            "srl    %[t2],  %[t2],  31      \n\t"
            "srl    %[t3],  %[t3],  31      \n\t"
            "subu   %[t4],  $zero,  %[qc1]  \n\t"
            "subu   %[t5],  $zero,  %[qc2]  \n\t"
            "subu   %[t6],  $zero,  %[qc3]  \n\t"
            "subu   %[t7],  $zero,  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t5],  %[t1]   \n\t"
            "movn   %[qc3], %[t6],  %[t2]   \n\t"
            "movn   %[qc4], %[t7],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4), [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = qc1;
        curidx *= 3;
        curidx += qc2;
        curidx *= 3;
        curidx += qc3;
        curidx *= 3;
        curidx += qc4;
        curidx += 40;

        curbits += p_bits[curidx];
    }
    return curbits;
}

static float get_band_numbits_UQUAD_mips(struct AACEncContext *s,
                                         PutBitContext *pb, const float *in,
                                         const float *scaled, int size, int scale_idx,
                                         int cb, const float lambda, const float uplim,
                                         int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int curbits = 0;
    int qc1, qc2, qc3, qc4;

    uint8_t *p_bits = (uint8_t *)ff_aac_spectral_bits[cb-1];

    for (i = 0; i < size; i += 4) {
        int curidx;
        int t0, t1, t2, t3, t4;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    %[t4],  $zero,  2       \n\t"
            "slt    %[t0],  %[t4],  %[qc1]  \n\t"
            "slt    %[t1],  %[t4],  %[qc2]  \n\t"
            "slt    %[t2],  %[t4],  %[qc3]  \n\t"
            "slt    %[t3],  %[t4],  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t4],  %[t1]   \n\t"
            "movn   %[qc3], %[t4],  %[t2]   \n\t"
            "movn   %[qc4], %[t4],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
        );

        curidx = qc1;
        curidx *= 3;
        curidx += qc2;
        curidx *= 3;
        curidx += qc3;
        curidx *= 3;
        curidx += qc4;

        curbits += p_bits[curidx];
        curbits += uquad_sign_bits[curidx];
    }
    return curbits;
}

static float get_band_numbits_SPAIR_mips(struct AACEncContext *s,
                                         PutBitContext *pb, const float *in,
                                         const float *scaled, int size, int scale_idx,
                                         int cb, const float lambda, const float uplim,
                                         int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits = (uint8_t*)ff_aac_spectral_bits[cb-1];

    for (i = 0; i < size; i += 4) {
        int curidx, curidx2;
        int *in_int = (int *)&in[i];
        int t0, t1, t2, t3, t4, t5, t6, t7;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    %[t4],  $zero,  4       \n\t"
            "slt    %[t0],  %[t4],  %[qc1]  \n\t"
            "slt    %[t1],  %[t4],  %[qc2]  \n\t"
            "slt    %[t2],  %[t4],  %[qc3]  \n\t"
            "slt    %[t3],  %[t4],  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t4],  %[t1]   \n\t"
            "movn   %[qc3], %[t4],  %[t2]   \n\t"
            "movn   %[qc4], %[t4],  %[t3]   \n\t"
            "lw     %[t0],  0(%[in_int])    \n\t"
            "lw     %[t1],  4(%[in_int])    \n\t"
            "lw     %[t2],  8(%[in_int])    \n\t"
            "lw     %[t3],  12(%[in_int])   \n\t"
            "srl    %[t0],  %[t0],  31      \n\t"
            "srl    %[t1],  %[t1],  31      \n\t"
            "srl    %[t2],  %[t2],  31      \n\t"
            "srl    %[t3],  %[t3],  31      \n\t"
            "subu   %[t4],  $zero,  %[qc1]  \n\t"
            "subu   %[t5],  $zero,  %[qc2]  \n\t"
            "subu   %[t6],  $zero,  %[qc3]  \n\t"
            "subu   %[t7],  $zero,  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t5],  %[t1]   \n\t"
            "movn   %[qc3], %[t6],  %[t2]   \n\t"
            "movn   %[qc4], %[t7],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4), [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx  = 9 * qc1;
        curidx += qc2 + 40;

        curidx2  = 9 * qc3;
        curidx2 += qc4 + 40;

        curbits += p_bits[curidx] + p_bits[curidx2];
    }
    return curbits;
}

static float get_band_numbits_UPAIR7_mips(struct AACEncContext *s,
                                          PutBitContext *pb, const float *in,
                                          const float *scaled, int size, int scale_idx,
                                          int cb, const float lambda, const float uplim,
                                          int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits = (uint8_t *)ff_aac_spectral_bits[cb-1];

    for (i = 0; i < size; i += 4) {
        int curidx, curidx2;
        int t0, t1, t2, t3, t4;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    %[t4],  $zero,  7       \n\t"
            "slt    %[t0],  %[t4],  %[qc1]  \n\t"
            "slt    %[t1],  %[t4],  %[qc2]  \n\t"
            "slt    %[t2],  %[t4],  %[qc3]  \n\t"
            "slt    %[t3],  %[t4],  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t4],  %[t1]   \n\t"
            "movn   %[qc3], %[t4],  %[t2]   \n\t"
            "movn   %[qc4], %[t4],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
        );

        curidx  = 8 * qc1;
        curidx += qc2;

        curidx2  = 8 * qc3;
        curidx2 += qc4;

        curbits += p_bits[curidx] +
                   upair7_sign_bits[curidx] +
                   p_bits[curidx2] +
                   upair7_sign_bits[curidx2];
    }
    return curbits;
}

static float get_band_numbits_UPAIR12_mips(struct AACEncContext *s,
                                           PutBitContext *pb, const float *in,
                                           const float *scaled, int size, int scale_idx,
                                           int cb, const float lambda, const float uplim,
                                           int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits = (uint8_t *)ff_aac_spectral_bits[cb-1];

    for (i = 0; i < size; i += 4) {
        int curidx, curidx2;
        int t0, t1, t2, t3, t4;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    %[t4],  $zero,  12      \n\t"
            "slt    %[t0],  %[t4],  %[qc1]  \n\t"
            "slt    %[t1],  %[t4],  %[qc2]  \n\t"
            "slt    %[t2],  %[t4],  %[qc3]  \n\t"
            "slt    %[t3],  %[t4],  %[qc4]  \n\t"
            "movn   %[qc1], %[t4],  %[t0]   \n\t"
            "movn   %[qc2], %[t4],  %[t1]   \n\t"
            "movn   %[qc3], %[t4],  %[t2]   \n\t"
            "movn   %[qc4], %[t4],  %[t3]   \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
        );

        curidx  = 13 * qc1;
        curidx += qc2;

        curidx2  = 13 * qc3;
        curidx2 += qc4;

        curbits += p_bits[curidx] +
                   p_bits[curidx2] +
                   upair12_sign_bits[curidx] +
                   upair12_sign_bits[curidx2];
    }
    return curbits;
}

static float get_band_numbits_ESC_mips(struct AACEncContext *s,
                                       PutBitContext *pb, const float *in,
                                       const float *scaled, int size, int scale_idx,
                                       int cb, const float lambda, const float uplim,
                                       int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits = (uint8_t*)ff_aac_spectral_bits[cb-1];

    for (i = 0; i < size; i += 4) {
        int curidx, curidx2;
        int cond0, cond1, cond2, cond3;
        int c1, c2, c3, c4;
        int t4, t5;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        %[t4],      $zero,  15          \n\t"
            "ori        %[t5],      $zero,  16          \n\t"
            "shll_s.w   %[c1],      %[qc1], 18          \n\t"
            "shll_s.w   %[c2],      %[qc2], 18          \n\t"
            "shll_s.w   %[c3],      %[qc3], 18          \n\t"
            "shll_s.w   %[c4],      %[qc4], 18          \n\t"
            "srl        %[c1],      %[c1],  18          \n\t"
            "srl        %[c2],      %[c2],  18          \n\t"
            "srl        %[c3],      %[c3],  18          \n\t"
            "srl        %[c4],      %[c4],  18          \n\t"
            "slt        %[cond0],   %[t4],  %[qc1]      \n\t"
            "slt        %[cond1],   %[t4],  %[qc2]      \n\t"
            "slt        %[cond2],   %[t4],  %[qc3]      \n\t"
            "slt        %[cond3],   %[t4],  %[qc4]      \n\t"
            "movn       %[qc1],     %[t5],  %[cond0]    \n\t"
            "movn       %[qc2],     %[t5],  %[cond1]    \n\t"
            "movn       %[qc3],     %[t5],  %[cond2]    \n\t"
            "movn       %[qc4],     %[t5],  %[cond3]    \n\t"
            "ori        %[t5],      $zero,  31          \n\t"
            "clz        %[c1],      %[c1]               \n\t"
            "clz        %[c2],      %[c2]               \n\t"
            "clz        %[c3],      %[c3]               \n\t"
            "clz        %[c4],      %[c4]               \n\t"
            "subu       %[c1],      %[t5],  %[c1]       \n\t"
            "subu       %[c2],      %[t5],  %[c2]       \n\t"
            "subu       %[c3],      %[t5],  %[c3]       \n\t"
            "subu       %[c4],      %[t5],  %[c4]       \n\t"
            "sll        %[c1],      %[c1],  1           \n\t"
            "sll        %[c2],      %[c2],  1           \n\t"
            "sll        %[c3],      %[c3],  1           \n\t"
            "sll        %[c4],      %[c4],  1           \n\t"
            "addiu      %[c1],      %[c1],  -3          \n\t"
            "addiu      %[c2],      %[c2],  -3          \n\t"
            "addiu      %[c3],      %[c3],  -3          \n\t"
            "addiu      %[c4],      %[c4],  -3          \n\t"
            "subu       %[cond0],   $zero,  %[cond0]    \n\t"
            "subu       %[cond1],   $zero,  %[cond1]    \n\t"
            "subu       %[cond2],   $zero,  %[cond2]    \n\t"
            "subu       %[cond3],   $zero,  %[cond3]    \n\t"
            "and        %[c1],      %[c1],  %[cond0]    \n\t"
            "and        %[c2],      %[c2],  %[cond1]    \n\t"
            "and        %[c3],      %[c3],  %[cond2]    \n\t"
            "and        %[c4],      %[c4],  %[cond3]    \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [cond0]"=&r"(cond0), [cond1]"=&r"(cond1),
              [cond2]"=&r"(cond2), [cond3]"=&r"(cond3),
              [c1]"=&r"(c1), [c2]"=&r"(c2),
              [c3]"=&r"(c3), [c4]"=&r"(c4),
              [t4]"=&r"(t4), [t5]"=&r"(t5)
        );

        curidx = 17 * qc1;
        curidx += qc2;

        curidx2 = 17 * qc3;
        curidx2 += qc4;

        curbits += p_bits[curidx];
        curbits += esc_sign_bits[curidx];
        curbits += p_bits[curidx2];
        curbits += esc_sign_bits[curidx2];

        curbits += c1;
        curbits += c2;
        curbits += c3;
        curbits += c4;
    }
    return curbits;
}

static float (*const get_band_numbits_arr[])(struct AACEncContext *s,
                                             PutBitContext *pb, const float *in,
                                             const float *scaled, int size, int scale_idx,
                                             int cb, const float lambda, const float uplim,
                                             int *bits) = {
    get_band_numbits_ZERO_mips,
    get_band_numbits_SQUAD_mips,
    get_band_numbits_SQUAD_mips,
    get_band_numbits_UQUAD_mips,
    get_band_numbits_UQUAD_mips,
    get_band_numbits_SPAIR_mips,
    get_band_numbits_SPAIR_mips,
    get_band_numbits_UPAIR7_mips,
    get_band_numbits_UPAIR7_mips,
    get_band_numbits_UPAIR12_mips,
    get_band_numbits_UPAIR12_mips,
    get_band_numbits_ESC_mips,
    get_band_numbits_NONE_mips, /* cb 12 doesn't exist */
    get_band_numbits_ZERO_mips,
    get_band_numbits_ZERO_mips,
    get_band_numbits_ZERO_mips,
};

#define get_band_numbits(                                  \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits)                    \
    get_band_numbits_arr[cb](                              \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits)

static float quantize_band_cost_bits(struct AACEncContext *s, const float *in,
                                     const float *scaled, int size, int scale_idx,
                                     int cb, const float lambda, const float uplim,
                                     int *bits, float *energy, int rtz)
{
    return get_band_numbits(s, NULL, in, scaled, size, scale_idx, cb, lambda, uplim, bits);
}

/**
 * Functions developed from template function and optimized for getting the band cost
 */
#if HAVE_MIPSFPU
static float get_band_cost_ZERO_mips(struct AACEncContext *s,
                                     PutBitContext *pb, const float *in,
                                     const float *scaled, int size, int scale_idx,
                                     int cb, const float lambda, const float uplim,
                                     int *bits, float *energy)
{
    int i;
    float cost = 0;

    for (i = 0; i < size; i += 4) {
        cost += in[i  ] * in[i  ];
        cost += in[i+1] * in[i+1];
        cost += in[i+2] * in[i+2];
        cost += in[i+3] * in[i+3];
    }
    if (bits)
        *bits = 0;
    if (energy)
        *energy = 0.0f;
    return cost * lambda;
}

static float get_band_cost_NONE_mips(struct AACEncContext *s,
                                     PutBitContext *pb, const float *in,
                                     const float *scaled, int size, int scale_idx,
                                     int cb, const float lambda, const float uplim,
                                     int *bits, float *energy)
{
    av_assert0(0);
    return 0;
}

static float get_band_cost_SQUAD_mips(struct AACEncContext *s,
                                      PutBitContext *pb, const float *in,
                                      const float *scaled, int size, int scale_idx,
                                      int cb, const float lambda, const float uplim,
                                      int *bits, float *energy)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
    float qenergy = 0.0f;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits  = (uint8_t *)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float   *)ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec;
        int curidx;
        int   *in_int = (int   *)&in[i];
        float *in_pos = (float *)&in[i];
        float di0, di1, di2, di3;
        int t0, t1, t2, t3, t4, t5, t6, t7;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "slt        %[qc1], $zero,  %[qc1]          \n\t"
            "slt        %[qc2], $zero,  %[qc2]          \n\t"
            "slt        %[qc3], $zero,  %[qc3]          \n\t"
            "slt        %[qc4], $zero,  %[qc4]          \n\t"
            "lw         %[t0],  0(%[in_int])            \n\t"
            "lw         %[t1],  4(%[in_int])            \n\t"
            "lw         %[t2],  8(%[in_int])            \n\t"
            "lw         %[t3],  12(%[in_int])           \n\t"
            "srl        %[t0],  %[t0],  31              \n\t"
            "srl        %[t1],  %[t1],  31              \n\t"
            "srl        %[t2],  %[t2],  31              \n\t"
            "srl        %[t3],  %[t3],  31              \n\t"
            "subu       %[t4],  $zero,  %[qc1]          \n\t"
            "subu       %[t5],  $zero,  %[qc2]          \n\t"
            "subu       %[t6],  $zero,  %[qc3]          \n\t"
            "subu       %[t7],  $zero,  %[qc4]          \n\t"
            "movn       %[qc1], %[t4],  %[t0]           \n\t"
            "movn       %[qc2], %[t5],  %[t1]           \n\t"
            "movn       %[qc3], %[t6],  %[t2]           \n\t"
            "movn       %[qc4], %[t7],  %[t3]           \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4), [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = qc1;
        curidx *= 3;
        curidx += qc2;
        curidx *= 3;
        curidx += qc3;
        curidx *= 3;
        curidx += qc4;
        curidx += 40;

        curbits += p_bits[curidx];
        vec     = &p_codes[curidx*4];

        qenergy += vec[0]*vec[0] + vec[1]*vec[1]
                +  vec[2]*vec[2] + vec[3]*vec[3];

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "lwc1       $f0,    0(%[in_pos])            \n\t"
            "lwc1       $f1,    0(%[vec])               \n\t"
            "lwc1       $f2,    4(%[in_pos])            \n\t"
            "lwc1       $f3,    4(%[vec])               \n\t"
            "lwc1       $f4,    8(%[in_pos])            \n\t"
            "lwc1       $f5,    8(%[vec])               \n\t"
            "lwc1       $f6,    12(%[in_pos])           \n\t"
            "lwc1       $f7,    12(%[vec])              \n\t"
            "nmsub.s    %[di0], $f0,    $f1,    %[IQ]   \n\t"
            "nmsub.s    %[di1], $f2,    $f3,    %[IQ]   \n\t"
            "nmsub.s    %[di2], $f4,    $f5,    %[IQ]   \n\t"
            "nmsub.s    %[di3], $f6,    $f7,    %[IQ]   \n\t"

            ".set pop                                   \n\t"

            : [di0]"=&f"(di0), [di1]"=&f"(di1),
              [di2]"=&f"(di2), [di3]"=&f"(di3)
            : [in_pos]"r"(in_pos), [vec]"r"(vec),
              [IQ]"f"(IQ)
            : "$f0", "$f1", "$f2", "$f3",
              "$f4", "$f5", "$f6", "$f7",
              "memory"
        );

        cost += di0 * di0 + di1 * di1
                + di2 * di2 + di3 * di3;
    }

    if (bits)
        *bits = curbits;
    if (energy)
        *energy = qenergy * (IQ*IQ);
    return cost * lambda + curbits;
}

static float get_band_cost_UQUAD_mips(struct AACEncContext *s,
                                      PutBitContext *pb, const float *in,
                                      const float *scaled, int size, int scale_idx,
                                      int cb, const float lambda, const float uplim,
                                      int *bits, float *energy)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
    float qenergy = 0.0f;
    int curbits = 0;
    int qc1, qc2, qc3, qc4;

    uint8_t *p_bits  = (uint8_t*)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float  *)ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec;
        int curidx;
        float *in_pos = (float *)&in[i];
        float di0, di1, di2, di3;
        int t0, t1, t2, t3, t4;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        %[t4],  $zero,  2               \n\t"
            "slt        %[t0],  %[t4],  %[qc1]          \n\t"
            "slt        %[t1],  %[t4],  %[qc2]          \n\t"
            "slt        %[t2],  %[t4],  %[qc3]          \n\t"
            "slt        %[t3],  %[t4],  %[qc4]          \n\t"
            "movn       %[qc1], %[t4],  %[t0]           \n\t"
            "movn       %[qc2], %[t4],  %[t1]           \n\t"
            "movn       %[qc3], %[t4],  %[t2]           \n\t"
            "movn       %[qc4], %[t4],  %[t3]           \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
        );

        curidx = qc1;
        curidx *= 3;
        curidx += qc2;
        curidx *= 3;
        curidx += qc3;
        curidx *= 3;
        curidx += qc4;

        curbits += p_bits[curidx];
        curbits += uquad_sign_bits[curidx];
        vec     = &p_codes[curidx*4];

        qenergy += vec[0]*vec[0] + vec[1]*vec[1]
                +  vec[2]*vec[2] + vec[3]*vec[3];

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "lwc1       %[di0], 0(%[in_pos])            \n\t"
            "lwc1       %[di1], 4(%[in_pos])            \n\t"
            "lwc1       %[di2], 8(%[in_pos])            \n\t"
            "lwc1       %[di3], 12(%[in_pos])           \n\t"
            "abs.s      %[di0], %[di0]                  \n\t"
            "abs.s      %[di1], %[di1]                  \n\t"
            "abs.s      %[di2], %[di2]                  \n\t"
            "abs.s      %[di3], %[di3]                  \n\t"
            "lwc1       $f0,    0(%[vec])               \n\t"
            "lwc1       $f1,    4(%[vec])               \n\t"
            "lwc1       $f2,    8(%[vec])               \n\t"
            "lwc1       $f3,    12(%[vec])              \n\t"
            "nmsub.s    %[di0], %[di0], $f0,    %[IQ]   \n\t"
            "nmsub.s    %[di1], %[di1], $f1,    %[IQ]   \n\t"
            "nmsub.s    %[di2], %[di2], $f2,    %[IQ]   \n\t"
            "nmsub.s    %[di3], %[di3], $f3,    %[IQ]   \n\t"

            ".set pop                                   \n\t"

            : [di0]"=&f"(di0), [di1]"=&f"(di1),
              [di2]"=&f"(di2), [di3]"=&f"(di3)
            : [in_pos]"r"(in_pos), [vec]"r"(vec),
              [IQ]"f"(IQ)
            : "$f0", "$f1", "$f2", "$f3",
              "memory"
        );

        cost += di0 * di0 + di1 * di1
                + di2 * di2 + di3 * di3;
    }

    if (bits)
        *bits = curbits;
    if (energy)
        *energy = qenergy * (IQ*IQ);
    return cost * lambda + curbits;
}

static float get_band_cost_SPAIR_mips(struct AACEncContext *s,
                                      PutBitContext *pb, const float *in,
                                      const float *scaled, int size, int scale_idx,
                                      int cb, const float lambda, const float uplim,
                                      int *bits, float *energy)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
    float qenergy = 0.0f;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits  = (uint8_t *)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float   *)ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec, *vec2;
        int curidx, curidx2;
        int   *in_int = (int   *)&in[i];
        float *in_pos = (float *)&in[i];
        float di0, di1, di2, di3;
        int t0, t1, t2, t3, t4, t5, t6, t7;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        %[t4],  $zero,  4               \n\t"
            "slt        %[t0],  %[t4],  %[qc1]          \n\t"
            "slt        %[t1],  %[t4],  %[qc2]          \n\t"
            "slt        %[t2],  %[t4],  %[qc3]          \n\t"
            "slt        %[t3],  %[t4],  %[qc4]          \n\t"
            "movn       %[qc1], %[t4],  %[t0]           \n\t"
            "movn       %[qc2], %[t4],  %[t1]           \n\t"
            "movn       %[qc3], %[t4],  %[t2]           \n\t"
            "movn       %[qc4], %[t4],  %[t3]           \n\t"
            "lw         %[t0],  0(%[in_int])            \n\t"
            "lw         %[t1],  4(%[in_int])            \n\t"
            "lw         %[t2],  8(%[in_int])            \n\t"
            "lw         %[t3],  12(%[in_int])           \n\t"
            "srl        %[t0],  %[t0],  31              \n\t"
            "srl        %[t1],  %[t1],  31              \n\t"
            "srl        %[t2],  %[t2],  31              \n\t"
            "srl        %[t3],  %[t3],  31              \n\t"
            "subu       %[t4],  $zero,  %[qc1]          \n\t"
            "subu       %[t5],  $zero,  %[qc2]          \n\t"
            "subu       %[t6],  $zero,  %[qc3]          \n\t"
            "subu       %[t7],  $zero,  %[qc4]          \n\t"
            "movn       %[qc1], %[t4],  %[t0]           \n\t"
            "movn       %[qc2], %[t5],  %[t1]           \n\t"
            "movn       %[qc3], %[t6],  %[t2]           \n\t"
            "movn       %[qc4], %[t7],  %[t3]           \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4), [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = 9 * qc1;
        curidx += qc2 + 40;

        curidx2 = 9 * qc3;
        curidx2 += qc4 + 40;

        curbits += p_bits[curidx];
        curbits += p_bits[curidx2];

        vec     = &p_codes[curidx*2];
        vec2    = &p_codes[curidx2*2];

        qenergy += vec[0]*vec[0] + vec[1]*vec[1]
                +  vec2[0]*vec2[0] + vec2[1]*vec2[1];

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "lwc1       $f0,    0(%[in_pos])            \n\t"
            "lwc1       $f1,    0(%[vec])               \n\t"
            "lwc1       $f2,    4(%[in_pos])            \n\t"
            "lwc1       $f3,    4(%[vec])               \n\t"
            "lwc1       $f4,    8(%[in_pos])            \n\t"
            "lwc1       $f5,    0(%[vec2])              \n\t"
            "lwc1       $f6,    12(%[in_pos])           \n\t"
            "lwc1       $f7,    4(%[vec2])              \n\t"
            "nmsub.s    %[di0], $f0,    $f1,    %[IQ]   \n\t"
            "nmsub.s    %[di1], $f2,    $f3,    %[IQ]   \n\t"
            "nmsub.s    %[di2], $f4,    $f5,    %[IQ]   \n\t"
            "nmsub.s    %[di3], $f6,    $f7,    %[IQ]   \n\t"

            ".set pop                                   \n\t"

            : [di0]"=&f"(di0), [di1]"=&f"(di1),
              [di2]"=&f"(di2), [di3]"=&f"(di3)
            : [in_pos]"r"(in_pos), [vec]"r"(vec),
              [vec2]"r"(vec2), [IQ]"f"(IQ)
            : "$f0", "$f1", "$f2", "$f3",
              "$f4", "$f5", "$f6", "$f7",
              "memory"
        );

        cost += di0 * di0 + di1 * di1
                + di2 * di2 + di3 * di3;
    }

    if (bits)
        *bits = curbits;
    if (energy)
        *energy = qenergy * (IQ*IQ);
    return cost * lambda + curbits;
}

static float get_band_cost_UPAIR7_mips(struct AACEncContext *s,
                                       PutBitContext *pb, const float *in,
                                       const float *scaled, int size, int scale_idx,
                                       int cb, const float lambda, const float uplim,
                                       int *bits, float *energy)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
    float qenergy = 0.0f;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits  = (uint8_t *)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float   *)ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec, *vec2;
        int curidx, curidx2, sign1, count1, sign2, count2;
        int   *in_int = (int   *)&in[i];
        float *in_pos = (float *)&in[i];
        float di0, di1, di2, di3;
        int t0, t1, t2, t3, t4;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                          \n\t"
            ".set noreorder                                     \n\t"

            "ori        %[t4],      $zero,      7               \n\t"
            "ori        %[sign1],   $zero,      0               \n\t"
            "ori        %[sign2],   $zero,      0               \n\t"
            "slt        %[t0],      %[t4],      %[qc1]          \n\t"
            "slt        %[t1],      %[t4],      %[qc2]          \n\t"
            "slt        %[t2],      %[t4],      %[qc3]          \n\t"
            "slt        %[t3],      %[t4],      %[qc4]          \n\t"
            "movn       %[qc1],     %[t4],      %[t0]           \n\t"
            "movn       %[qc2],     %[t4],      %[t1]           \n\t"
            "movn       %[qc3],     %[t4],      %[t2]           \n\t"
            "movn       %[qc4],     %[t4],      %[t3]           \n\t"
            "lw         %[t0],      0(%[in_int])                \n\t"
            "lw         %[t1],      4(%[in_int])                \n\t"
            "lw         %[t2],      8(%[in_int])                \n\t"
            "lw         %[t3],      12(%[in_int])               \n\t"
            "slt        %[t0],      %[t0],      $zero           \n\t"
            "movn       %[sign1],   %[t0],      %[qc1]          \n\t"
            "slt        %[t2],      %[t2],      $zero           \n\t"
            "movn       %[sign2],   %[t2],      %[qc3]          \n\t"
            "slt        %[t1],      %[t1],      $zero           \n\t"
            "sll        %[t0],      %[sign1],   1               \n\t"
            "or         %[t0],      %[t0],      %[t1]           \n\t"
            "movn       %[sign1],   %[t0],      %[qc2]          \n\t"
            "slt        %[t3],      %[t3],      $zero           \n\t"
            "sll        %[t0],      %[sign2],   1               \n\t"
            "or         %[t0],      %[t0],      %[t3]           \n\t"
            "movn       %[sign2],   %[t0],      %[qc4]          \n\t"
            "slt        %[count1],  $zero,      %[qc1]          \n\t"
            "slt        %[t1],      $zero,      %[qc2]          \n\t"
            "slt        %[count2],  $zero,      %[qc3]          \n\t"
            "slt        %[t2],      $zero,      %[qc4]          \n\t"
            "addu       %[count1],  %[count1],  %[t1]           \n\t"
            "addu       %[count2],  %[count2],  %[t2]           \n\t"

            ".set pop                                           \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = 8 * qc1;
        curidx += qc2;

        curidx2 = 8 * qc3;
        curidx2 += qc4;

        curbits += p_bits[curidx];
        curbits += upair7_sign_bits[curidx];
        vec     = &p_codes[curidx*2];

        curbits += p_bits[curidx2];
        curbits += upair7_sign_bits[curidx2];
        vec2    = &p_codes[curidx2*2];

        qenergy += vec[0]*vec[0] + vec[1]*vec[1]
                +  vec2[0]*vec2[0] + vec2[1]*vec2[1];

        __asm__ volatile (
            ".set push                                          \n\t"
            ".set noreorder                                     \n\t"

            "lwc1       %[di0],     0(%[in_pos])                \n\t"
            "lwc1       %[di1],     4(%[in_pos])                \n\t"
            "lwc1       %[di2],     8(%[in_pos])                \n\t"
            "lwc1       %[di3],     12(%[in_pos])               \n\t"
            "abs.s      %[di0],     %[di0]                      \n\t"
            "abs.s      %[di1],     %[di1]                      \n\t"
            "abs.s      %[di2],     %[di2]                      \n\t"
            "abs.s      %[di3],     %[di3]                      \n\t"
            "lwc1       $f0,        0(%[vec])                   \n\t"
            "lwc1       $f1,        4(%[vec])                   \n\t"
            "lwc1       $f2,        0(%[vec2])                  \n\t"
            "lwc1       $f3,        4(%[vec2])                  \n\t"
            "nmsub.s    %[di0],     %[di0],     $f0,    %[IQ]   \n\t"
            "nmsub.s    %[di1],     %[di1],     $f1,    %[IQ]   \n\t"
            "nmsub.s    %[di2],     %[di2],     $f2,    %[IQ]   \n\t"
            "nmsub.s    %[di3],     %[di3],     $f3,    %[IQ]   \n\t"

            ".set pop                                           \n\t"

            : [di0]"=&f"(di0), [di1]"=&f"(di1),
              [di2]"=&f"(di2), [di3]"=&f"(di3)
            : [in_pos]"r"(in_pos), [vec]"r"(vec),
              [vec2]"r"(vec2), [IQ]"f"(IQ)
            : "$f0", "$f1", "$f2", "$f3",
              "memory"
        );

        cost += di0 * di0 + di1 * di1
                + di2 * di2 + di3 * di3;
    }

    if (bits)
        *bits = curbits;
    if (energy)
        *energy = qenergy * (IQ*IQ);
    return cost * lambda + curbits;
}

static float get_band_cost_UPAIR12_mips(struct AACEncContext *s,
                                        PutBitContext *pb, const float *in,
                                        const float *scaled, int size, int scale_idx,
                                        int cb, const float lambda, const float uplim,
                                        int *bits, float *energy)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
    float qenergy = 0.0f;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits  = (uint8_t *)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float   *)ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec, *vec2;
        int curidx, curidx2;
        int sign1, count1, sign2, count2;
        int   *in_int = (int   *)&in[i];
        float *in_pos = (float *)&in[i];
        float di0, di1, di2, di3;
        int t0, t1, t2, t3, t4;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                          \n\t"
            ".set noreorder                                     \n\t"

            "ori        %[t4],      $zero,      12              \n\t"
            "ori        %[sign1],   $zero,      0               \n\t"
            "ori        %[sign2],   $zero,      0               \n\t"
            "slt        %[t0],      %[t4],      %[qc1]          \n\t"
            "slt        %[t1],      %[t4],      %[qc2]          \n\t"
            "slt        %[t2],      %[t4],      %[qc3]          \n\t"
            "slt        %[t3],      %[t4],      %[qc4]          \n\t"
            "movn       %[qc1],     %[t4],      %[t0]           \n\t"
            "movn       %[qc2],     %[t4],      %[t1]           \n\t"
            "movn       %[qc3],     %[t4],      %[t2]           \n\t"
            "movn       %[qc4],     %[t4],      %[t3]           \n\t"
            "lw         %[t0],      0(%[in_int])                \n\t"
            "lw         %[t1],      4(%[in_int])                \n\t"
            "lw         %[t2],      8(%[in_int])                \n\t"
            "lw         %[t3],      12(%[in_int])               \n\t"
            "slt        %[t0],      %[t0],      $zero           \n\t"
            "movn       %[sign1],   %[t0],      %[qc1]          \n\t"
            "slt        %[t2],      %[t2],      $zero           \n\t"
            "movn       %[sign2],   %[t2],      %[qc3]          \n\t"
            "slt        %[t1],      %[t1],      $zero           \n\t"
            "sll        %[t0],      %[sign1],   1               \n\t"
            "or         %[t0],      %[t0],      %[t1]           \n\t"
            "movn       %[sign1],   %[t0],      %[qc2]          \n\t"
            "slt        %[t3],      %[t3],      $zero           \n\t"
            "sll        %[t0],      %[sign2],   1               \n\t"
            "or         %[t0],      %[t0],      %[t3]           \n\t"
            "movn       %[sign2],   %[t0],      %[qc4]          \n\t"
            "slt        %[count1],  $zero,      %[qc1]          \n\t"
            "slt        %[t1],      $zero,      %[qc2]          \n\t"
            "slt        %[count2],  $zero,      %[qc3]          \n\t"
            "slt        %[t2],      $zero,      %[qc4]          \n\t"
            "addu       %[count1],  %[count1],  %[t1]           \n\t"
            "addu       %[count2],  %[count2],  %[t2]           \n\t"

            ".set pop                                           \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2),
              [t0]"=&r"(t0), [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3),
              [t4]"=&r"(t4)
            : [in_int]"r"(in_int)
            : "memory"
        );

        curidx = 13 * qc1;
        curidx += qc2;

        curidx2 = 13 * qc3;
        curidx2 += qc4;

        curbits += p_bits[curidx];
        curbits += p_bits[curidx2];
        curbits += upair12_sign_bits[curidx];
        curbits += upair12_sign_bits[curidx2];
        vec     = &p_codes[curidx*2];
        vec2    = &p_codes[curidx2*2];

        qenergy += vec[0]*vec[0] + vec[1]*vec[1]
                +  vec2[0]*vec2[0] + vec2[1]*vec2[1];

        __asm__ volatile (
            ".set push                                          \n\t"
            ".set noreorder                                     \n\t"

            "lwc1       %[di0],     0(%[in_pos])                \n\t"
            "lwc1       %[di1],     4(%[in_pos])                \n\t"
            "lwc1       %[di2],     8(%[in_pos])                \n\t"
            "lwc1       %[di3],     12(%[in_pos])               \n\t"
            "abs.s      %[di0],     %[di0]                      \n\t"
            "abs.s      %[di1],     %[di1]                      \n\t"
            "abs.s      %[di2],     %[di2]                      \n\t"
            "abs.s      %[di3],     %[di3]                      \n\t"
            "lwc1       $f0,        0(%[vec])                   \n\t"
            "lwc1       $f1,        4(%[vec])                   \n\t"
            "lwc1       $f2,        0(%[vec2])                  \n\t"
            "lwc1       $f3,        4(%[vec2])                  \n\t"
            "nmsub.s    %[di0],     %[di0],     $f0,    %[IQ]   \n\t"
            "nmsub.s    %[di1],     %[di1],     $f1,    %[IQ]   \n\t"
            "nmsub.s    %[di2],     %[di2],     $f2,    %[IQ]   \n\t"
            "nmsub.s    %[di3],     %[di3],     $f3,    %[IQ]   \n\t"

            ".set pop                                           \n\t"

            : [di0]"=&f"(di0), [di1]"=&f"(di1),
              [di2]"=&f"(di2), [di3]"=&f"(di3)
            : [in_pos]"r"(in_pos), [vec]"r"(vec),
              [vec2]"r"(vec2), [IQ]"f"(IQ)
            : "$f0", "$f1", "$f2", "$f3",
              "memory"
        );

        cost += di0 * di0 + di1 * di1
                + di2 * di2 + di3 * di3;
    }

    if (bits)
        *bits = curbits;
    if (energy)
        *energy = qenergy * (IQ*IQ);
    return cost * lambda + curbits;
}

static float get_band_cost_ESC_mips(struct AACEncContext *s,
                                    PutBitContext *pb, const float *in,
                                    const float *scaled, int size, int scale_idx,
                                    int cb, const float lambda, const float uplim,
                                    int *bits, float *energy)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    const float CLIPPED_ESCAPE = 165140.0f * IQ;
    int i;
    float cost = 0;
    float qenergy = 0.0f;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits  = (uint8_t*)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float*  )ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec, *vec2;
        int curidx, curidx2;
        float t1, t2, t3, t4, V;
        float di1, di2, di3, di4;
        int cond0, cond1, cond2, cond3;
        int c1, c2, c3, c4;
        int t6, t7;

        qc1 = scaled[i  ] * Q34 + ROUND_STANDARD;
        qc2 = scaled[i+1] * Q34 + ROUND_STANDARD;
        qc3 = scaled[i+2] * Q34 + ROUND_STANDARD;
        qc4 = scaled[i+3] * Q34 + ROUND_STANDARD;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        %[t6],      $zero,  15          \n\t"
            "ori        %[t7],      $zero,  16          \n\t"
            "shll_s.w   %[c1],      %[qc1], 18          \n\t"
            "shll_s.w   %[c2],      %[qc2], 18          \n\t"
            "shll_s.w   %[c3],      %[qc3], 18          \n\t"
            "shll_s.w   %[c4],      %[qc4], 18          \n\t"
            "srl        %[c1],      %[c1],  18          \n\t"
            "srl        %[c2],      %[c2],  18          \n\t"
            "srl        %[c3],      %[c3],  18          \n\t"
            "srl        %[c4],      %[c4],  18          \n\t"
            "slt        %[cond0],   %[t6],  %[qc1]      \n\t"
            "slt        %[cond1],   %[t6],  %[qc2]      \n\t"
            "slt        %[cond2],   %[t6],  %[qc3]      \n\t"
            "slt        %[cond3],   %[t6],  %[qc4]      \n\t"
            "movn       %[qc1],     %[t7],  %[cond0]    \n\t"
            "movn       %[qc2],     %[t7],  %[cond1]    \n\t"
            "movn       %[qc3],     %[t7],  %[cond2]    \n\t"
            "movn       %[qc4],     %[t7],  %[cond3]    \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [cond0]"=&r"(cond0), [cond1]"=&r"(cond1),
              [cond2]"=&r"(cond2), [cond3]"=&r"(cond3),
              [c1]"=&r"(c1), [c2]"=&r"(c2),
              [c3]"=&r"(c3), [c4]"=&r"(c4),
              [t6]"=&r"(t6), [t7]"=&r"(t7)
        );

        curidx = 17 * qc1;
        curidx += qc2;

        curidx2 = 17 * qc3;
        curidx2 += qc4;

        curbits += p_bits[curidx];
        curbits += esc_sign_bits[curidx];
        vec     = &p_codes[curidx*2];

        curbits += p_bits[curidx2];
        curbits += esc_sign_bits[curidx2];
        vec2     = &p_codes[curidx2*2];

        curbits += (av_log2(c1) * 2 - 3) & (-cond0);
        curbits += (av_log2(c2) * 2 - 3) & (-cond1);
        curbits += (av_log2(c3) * 2 - 3) & (-cond2);
        curbits += (av_log2(c4) * 2 - 3) & (-cond3);

        t1 = fabsf(in[i  ]);
        t2 = fabsf(in[i+1]);
        t3 = fabsf(in[i+2]);
        t4 = fabsf(in[i+3]);

        if (cond0) {
            if (t1 >= CLIPPED_ESCAPE) {
                di1 = t1 - CLIPPED_ESCAPE;
                qenergy += CLIPPED_ESCAPE*CLIPPED_ESCAPE;
            } else {
                di1 = t1 - (V = c1 * cbrtf(c1) * IQ);
                qenergy += V*V;
            }
        } else {
            di1 = t1 - (V = vec[0] * IQ);
            qenergy += V*V;
        }

        if (cond1) {
            if (t2 >= CLIPPED_ESCAPE) {
                di2 = t2 - CLIPPED_ESCAPE;
                qenergy += CLIPPED_ESCAPE*CLIPPED_ESCAPE;
            } else {
                di2 = t2 - (V = c2 * cbrtf(c2) * IQ);
                qenergy += V*V;
            }
        } else {
            di2 = t2 - (V = vec[1] * IQ);
            qenergy += V*V;
        }

        if (cond2) {
            if (t3 >= CLIPPED_ESCAPE) {
                di3 = t3 - CLIPPED_ESCAPE;
                qenergy += CLIPPED_ESCAPE*CLIPPED_ESCAPE;
            } else {
                di3 = t3 - (V = c3 * cbrtf(c3) * IQ);
                qenergy += V*V;
            }
        } else {
            di3 = t3 - (V = vec2[0] * IQ);
            qenergy += V*V;
        }

        if (cond3) {
            if (t4 >= CLIPPED_ESCAPE) {
                di4 = t4 - CLIPPED_ESCAPE;
                qenergy += CLIPPED_ESCAPE*CLIPPED_ESCAPE;
            } else {
                di4 = t4 - (V = c4 * cbrtf(c4) * IQ);
                qenergy += V*V;
            }
        } else {
            di4 = t4 - (V = vec2[1]*IQ);
            qenergy += V*V;
        }

        cost += di1 * di1 + di2 * di2
                + di3 * di3 + di4 * di4;
    }

    if (bits)
        *bits = curbits;
    return cost * lambda + curbits;
}

static float (*const get_band_cost_arr[])(struct AACEncContext *s,
                                          PutBitContext *pb, const float *in,
                                          const float *scaled, int size, int scale_idx,
                                          int cb, const float lambda, const float uplim,
                                          int *bits, float *energy) = {
    get_band_cost_ZERO_mips,
    get_band_cost_SQUAD_mips,
    get_band_cost_SQUAD_mips,
    get_band_cost_UQUAD_mips,
    get_band_cost_UQUAD_mips,
    get_band_cost_SPAIR_mips,
    get_band_cost_SPAIR_mips,
    get_band_cost_UPAIR7_mips,
    get_band_cost_UPAIR7_mips,
    get_band_cost_UPAIR12_mips,
    get_band_cost_UPAIR12_mips,
    get_band_cost_ESC_mips,
    get_band_cost_NONE_mips, /* cb 12 doesn't exist */
    get_band_cost_ZERO_mips,
    get_band_cost_ZERO_mips,
    get_band_cost_ZERO_mips,
};

#define get_band_cost(                                  \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits, energy)            \
    get_band_cost_arr[cb](                              \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits, energy)

static float quantize_band_cost(struct AACEncContext *s, const float *in,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits, float *energy, int rtz)
{
    return get_band_cost(s, NULL, in, scaled, size, scale_idx, cb, lambda, uplim, bits, energy);
}

#include "libavcodec/aacenc_quantization_misc.h"

#include "libavcodec/aaccoder_twoloop.h"

static void search_for_ms_mips(AACEncContext *s, ChannelElement *cpe)
{
    int start = 0, i, w, w2, g, sid_sf_boost, prev_mid, prev_side;
    uint8_t nextband0[128], nextband1[128];
    float M[128], S[128];
    float *L34 = s->scoefs, *R34 = s->scoefs + 128, *M34 = s->scoefs + 128*2, *S34 = s->scoefs + 128*3;
    const float lambda = s->lambda;
    const float mslambda = FFMIN(1.0f, lambda / 120.f);
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    if (!cpe->common_window)
        return;

    /** Scout out next nonzero bands */
    ff_init_nextband_map(sce0, nextband0);
    ff_init_nextband_map(sce1, nextband1);

    prev_mid = sce0->sf_idx[0];
    prev_side = sce1->sf_idx[0];
    for (w = 0; w < sce0->ics.num_windows; w += sce0->ics.group_len[w]) {
        start = 0;
        for (g = 0;  g < sce0->ics.num_swb; g++) {
            float bmax = bval2bmax(g * 17.0f / sce0->ics.num_swb) / 0.0045f;
            if (!cpe->is_mask[w*16+g])
                cpe->ms_mask[w*16+g] = 0;
            if (!sce0->zeroes[w*16+g] && !sce1->zeroes[w*16+g] && !cpe->is_mask[w*16+g]) {
                float Mmax = 0.0f, Smax = 0.0f;

                /* Must compute mid/side SF and book for the whole window group */
                for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                    for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
                        M[i] = (sce0->coeffs[start+(w+w2)*128+i]
                              + sce1->coeffs[start+(w+w2)*128+i]) * 0.5;
                        S[i] =  M[i]
                              - sce1->coeffs[start+(w+w2)*128+i];
                    }
                    abs_pow34_v(M34, M, sce0->ics.swb_sizes[g]);
                    abs_pow34_v(S34, S, sce0->ics.swb_sizes[g]);
                    for (i = 0; i < sce0->ics.swb_sizes[g]; i++ ) {
                        Mmax = FFMAX(Mmax, M34[i]);
                        Smax = FFMAX(Smax, S34[i]);
                    }
                }

                for (sid_sf_boost = 0; sid_sf_boost < 4; sid_sf_boost++) {
                    float dist1 = 0.0f, dist2 = 0.0f;
                    int B0 = 0, B1 = 0;
                    int minidx;
                    int mididx, sididx;
                    int midcb, sidcb;

                    minidx = FFMIN(sce0->sf_idx[w*16+g], sce1->sf_idx[w*16+g]);
                    mididx = av_clip(minidx, 0, SCALE_MAX_POS - SCALE_DIV_512);
                    sididx = av_clip(minidx - sid_sf_boost * 3, 0, SCALE_MAX_POS - SCALE_DIV_512);
                    if (sce0->band_type[w*16+g] != NOISE_BT && sce1->band_type[w*16+g] != NOISE_BT
                        && (   !ff_sfdelta_can_replace(sce0, nextband0, prev_mid, mididx, w*16+g)
                            || !ff_sfdelta_can_replace(sce1, nextband1, prev_side, sididx, w*16+g))) {
                        /* scalefactor range violation, bad stuff, will decrease quality unacceptably */
                        continue;
                    }

                    midcb = find_min_book(Mmax, mididx);
                    sidcb = find_min_book(Smax, sididx);

                    /* No CB can be zero */
                    midcb = FFMAX(1,midcb);
                    sidcb = FFMAX(1,sidcb);

                    for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                        FFPsyBand *band0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
                        FFPsyBand *band1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
                        float minthr = FFMIN(band0->threshold, band1->threshold);
                        int b1,b2,b3,b4;
                        for (i = 0; i < sce0->ics.swb_sizes[g]; i++) {
                            M[i] = (sce0->coeffs[start+(w+w2)*128+i]
                                  + sce1->coeffs[start+(w+w2)*128+i]) * 0.5;
                            S[i] =  M[i]
                                  - sce1->coeffs[start+(w+w2)*128+i];
                        }

                        abs_pow34_v(L34, sce0->coeffs+start+(w+w2)*128, sce0->ics.swb_sizes[g]);
                        abs_pow34_v(R34, sce1->coeffs+start+(w+w2)*128, sce0->ics.swb_sizes[g]);
                        abs_pow34_v(M34, M,                         sce0->ics.swb_sizes[g]);
                        abs_pow34_v(S34, S,                         sce0->ics.swb_sizes[g]);
                        dist1 += quantize_band_cost(s, &sce0->coeffs[start + (w+w2)*128],
                                                    L34,
                                                    sce0->ics.swb_sizes[g],
                                                    sce0->sf_idx[w*16+g],
                                                    sce0->band_type[w*16+g],
                                                    lambda / band0->threshold, INFINITY, &b1, NULL, 0);
                        dist1 += quantize_band_cost(s, &sce1->coeffs[start + (w+w2)*128],
                                                    R34,
                                                    sce1->ics.swb_sizes[g],
                                                    sce1->sf_idx[w*16+g],
                                                    sce1->band_type[w*16+g],
                                                    lambda / band1->threshold, INFINITY, &b2, NULL, 0);
                        dist2 += quantize_band_cost(s, M,
                                                    M34,
                                                    sce0->ics.swb_sizes[g],
                                                    mididx,
                                                    midcb,
                                                    lambda / minthr, INFINITY, &b3, NULL, 0);
                        dist2 += quantize_band_cost(s, S,
                                                    S34,
                                                    sce1->ics.swb_sizes[g],
                                                    sididx,
                                                    sidcb,
                                                    mslambda / (minthr * bmax), INFINITY, &b4, NULL, 0);
                        B0 += b1+b2;
                        B1 += b3+b4;
                        dist1 -= b1+b2;
                        dist2 -= b3+b4;
                    }
                    cpe->ms_mask[w*16+g] = dist2 <= dist1 && B1 < B0;
                    if (cpe->ms_mask[w*16+g]) {
                        if (sce0->band_type[w*16+g] != NOISE_BT && sce1->band_type[w*16+g] != NOISE_BT) {
                            sce0->sf_idx[w*16+g] = mididx;
                            sce1->sf_idx[w*16+g] = sididx;
                            sce0->band_type[w*16+g] = midcb;
                            sce1->band_type[w*16+g] = sidcb;
                        } else if ((sce0->band_type[w*16+g] != NOISE_BT) ^ (sce1->band_type[w*16+g] != NOISE_BT)) {
                            /* ms_mask unneeded, and it confuses some decoders */
                            cpe->ms_mask[w*16+g] = 0;
                        }
                        break;
                    } else if (B1 > B0) {
                        /* More boost won't fix this */
                        break;
                    }
                }
            }
            if (!sce0->zeroes[w*16+g] && sce0->band_type[w*16+g] < RESERVED_BT)
                prev_mid = sce0->sf_idx[w*16+g];
            if (!sce1->zeroes[w*16+g] && !cpe->is_mask[w*16+g] && sce1->band_type[w*16+g] < RESERVED_BT)
                prev_side = sce1->sf_idx[w*16+g];
            start += sce0->ics.swb_sizes[g];
        }
    }
}
#endif /*HAVE_MIPSFPU */

#include "libavcodec/aaccoder_trellis.h"

#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_INLINE_ASM */

void ff_aac_coder_init_mips(AACEncContext *c) {
#if HAVE_INLINE_ASM
#if !HAVE_MIPS32R6 && !HAVE_MIPS64R6
    AACCoefficientsEncoder *e = c->coder;
    int option = c->options.coder;

    if (option == 2) {
        e->quantize_and_encode_band = quantize_and_encode_band_mips;
        e->encode_window_bands_info = codebook_trellis_rate;
#if HAVE_MIPSFPU
        e->search_for_quantizers    = search_for_quantizers_twoloop;
#endif /* HAVE_MIPSFPU */
    }
#if HAVE_MIPSFPU
    e->search_for_ms            = search_for_ms_mips;
#endif /* HAVE_MIPSFPU */
#endif /* !HAVE_MIPS32R6 && !HAVE_MIPS64R6 */
#endif /* HAVE_INLINE_ASM */
}
