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

#if HAVE_INLINE_ASM
typedef struct BandCodingPath {
    int prev_idx;
    float cost;
    int run;
} BandCodingPath;

static const uint8_t run_value_bits_long[64] = {
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 15
};

static const uint8_t run_value_bits_short[16] = {
    3, 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 6, 6, 6, 6, 9
};

static const uint8_t *run_value_bits[2] = {
    run_value_bits_long, run_value_bits_short
};

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

static void abs_pow34_v(float *out, const float *in, const int size) {
#ifndef USE_REALLY_FULL_SEARCH
    int i;
    float a, b, c, d;
    float ax, bx, cx, dx;

    for (i = 0; i < size; i += 4) {
        a = fabsf(in[i  ]);
        b = fabsf(in[i+1]);
        c = fabsf(in[i+2]);
        d = fabsf(in[i+3]);

        ax = sqrtf(a);
        bx = sqrtf(b);
        cx = sqrtf(c);
        dx = sqrtf(d);

        a = a * ax;
        b = b * bx;
        c = c * cx;
        d = d * dx;

        out[i  ] = sqrtf(a);
        out[i+1] = sqrtf(b);
        out[i+2] = sqrtf(c);
        out[i+3] = sqrtf(d);
    }
#endif /* USE_REALLY_FULL_SEARCH */
}

static float find_max_val(int group_len, int swb_size, const float *scaled) {
    float maxval = 0.0f;
    int w2, i;
    for (w2 = 0; w2 < group_len; w2++) {
        for (i = 0; i < swb_size; i++) {
            maxval = FFMAX(maxval, scaled[w2*128+i]);
        }
    }
    return maxval;
}

static int find_min_book(float maxval, int sf) {
    float Q = ff_aac_pow2sf_tab[POW_SF2_ZERO - sf + SCALE_ONE_POS - SCALE_DIV_512];
    float Q34 = sqrtf(Q * sqrtf(Q));
    int qmaxval, cb;
    qmaxval = maxval * Q34 + 0.4054f;
    if      (qmaxval ==  0) cb = 0;
    else if (qmaxval ==  1) cb = 1;
    else if (qmaxval ==  2) cb = 3;
    else if (qmaxval <=  4) cb = 5;
    else if (qmaxval <=  7) cb = 7;
    else if (qmaxval <= 12) cb = 9;
    else                    cb = 11;
    return cb;
}

/**
 * Functions developed from template function and optimized for quantizing and encoding band
 */
static void quantize_and_encode_band_cost_SQUAD_mips(struct AACEncContext *s,
                                                     PutBitContext *pb, const float *in,
                                                     const float *scaled, int size, int scale_idx,
                                                     int cb, const float lambda, const float uplim,
                                                     int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;

    uint8_t  *p_bits  = (uint8_t  *)ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t *)ff_aac_spectral_codes[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx;
        int *in_int = (int *)&in[i];

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "slt    %[qc1], $zero,  %[qc1]  \n\t"
            "slt    %[qc2], $zero,  %[qc2]  \n\t"
            "slt    %[qc3], $zero,  %[qc3]  \n\t"
            "slt    %[qc4], $zero,  %[qc4]  \n\t"
            "lw     $t0,    0(%[in_int])    \n\t"
            "lw     $t1,    4(%[in_int])    \n\t"
            "lw     $t2,    8(%[in_int])    \n\t"
            "lw     $t3,    12(%[in_int])   \n\t"
            "srl    $t0,    $t0,    31      \n\t"
            "srl    $t1,    $t1,    31      \n\t"
            "srl    $t2,    $t2,    31      \n\t"
            "srl    $t3,    $t3,    31      \n\t"
            "subu   $t4,    $zero,  %[qc1]  \n\t"
            "subu   $t5,    $zero,  %[qc2]  \n\t"
            "subu   $t6,    $zero,  %[qc3]  \n\t"
            "subu   $t7,    $zero,  %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t5,    $t1     \n\t"
            "movn   %[qc3], $t6,    $t2     \n\t"
            "movn   %[qc4], $t7,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3",
              "t4", "t5", "t6", "t7",
              "memory"
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
    }
}

static void quantize_and_encode_band_cost_UQUAD_mips(struct AACEncContext *s,
                                                     PutBitContext *pb, const float *in,
                                                     const float *scaled, int size, int scale_idx,
                                                     int cb, const float lambda, const float uplim,
                                                     int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;

    uint8_t  *p_bits  = (uint8_t  *)ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t *)ff_aac_spectral_codes[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx, sign, count;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                              \n\t"
            ".set noreorder                         \n\t"

            "ori    $t4,        $zero,      2       \n\t"
            "ori    %[sign],    $zero,      0       \n\t"
            "slt    $t0,        $t4,        %[qc1]  \n\t"
            "slt    $t1,        $t4,        %[qc2]  \n\t"
            "slt    $t2,        $t4,        %[qc3]  \n\t"
            "slt    $t3,        $t4,        %[qc4]  \n\t"
            "movn   %[qc1],     $t4,        $t0     \n\t"
            "movn   %[qc2],     $t4,        $t1     \n\t"
            "movn   %[qc3],     $t4,        $t2     \n\t"
            "movn   %[qc4],     $t4,        $t3     \n\t"
            "lw     $t0,        0(%[in_int])        \n\t"
            "lw     $t1,        4(%[in_int])        \n\t"
            "lw     $t2,        8(%[in_int])        \n\t"
            "lw     $t3,        12(%[in_int])       \n\t"
            "slt    $t0,        $t0,        $zero   \n\t"
            "movn   %[sign],    $t0,        %[qc1]  \n\t"
            "slt    $t1,        $t1,        $zero   \n\t"
            "slt    $t2,        $t2,        $zero   \n\t"
            "slt    $t3,        $t3,        $zero   \n\t"
            "sll    $t0,        %[sign],    1       \n\t"
            "or     $t0,        $t0,        $t1     \n\t"
            "movn   %[sign],    $t0,        %[qc2]  \n\t"
            "slt    $t4,        $zero,      %[qc1]  \n\t"
            "slt    $t1,        $zero,      %[qc2]  \n\t"
            "slt    %[count],   $zero,      %[qc3]  \n\t"
            "sll    $t0,        %[sign],    1       \n\t"
            "or     $t0,        $t0,        $t2     \n\t"
            "movn   %[sign],    $t0,        %[qc3]  \n\t"
            "slt    $t2,        $zero,      %[qc4]  \n\t"
            "addu   %[count],   %[count],   $t4     \n\t"
            "addu   %[count],   %[count],   $t1     \n\t"
            "sll    $t0,        %[sign],    1       \n\t"
            "or     $t0,        $t0,        $t3     \n\t"
            "movn   %[sign],    $t0,        %[qc4]  \n\t"
            "addu   %[count],   %[count],   $t2     \n\t"

            ".set pop                               \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign]"=&r"(sign), [count]"=&r"(count)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3", "t4",
              "memory"
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
    }
}

static void quantize_and_encode_band_cost_SPAIR_mips(struct AACEncContext *s,
                                                     PutBitContext *pb, const float *in,
                                                     const float *scaled, int size, int scale_idx,
                                                     int cb, const float lambda, const float uplim,
                                                     int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;

    uint8_t  *p_bits  = (uint8_t  *)ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t *)ff_aac_spectral_codes[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx, curidx2;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    $t4,    $zero,  4       \n\t"
            "slt    $t0,    $t4,    %[qc1]  \n\t"
            "slt    $t1,    $t4,    %[qc2]  \n\t"
            "slt    $t2,    $t4,    %[qc3]  \n\t"
            "slt    $t3,    $t4,    %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t4,    $t1     \n\t"
            "movn   %[qc3], $t4,    $t2     \n\t"
            "movn   %[qc4], $t4,    $t3     \n\t"
            "lw     $t0,    0(%[in_int])    \n\t"
            "lw     $t1,    4(%[in_int])    \n\t"
            "lw     $t2,    8(%[in_int])    \n\t"
            "lw     $t3,    12(%[in_int])   \n\t"
            "srl    $t0,    $t0,    31      \n\t"
            "srl    $t1,    $t1,    31      \n\t"
            "srl    $t2,    $t2,    31      \n\t"
            "srl    $t3,    $t3,    31      \n\t"
            "subu   $t4,    $zero,  %[qc1]  \n\t"
            "subu   $t5,    $zero,  %[qc2]  \n\t"
            "subu   $t6,    $zero,  %[qc3]  \n\t"
            "subu   $t7,    $zero,  %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t5,    $t1     \n\t"
            "movn   %[qc3], $t6,    $t2     \n\t"
            "movn   %[qc4], $t7,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3",
              "t4", "t5", "t6", "t7",
              "memory"
        );

        curidx = 9 * qc1;
        curidx += qc2 + 40;

        curidx2 = 9 * qc3;
        curidx2 += qc4 + 40;

        v_codes = (p_codes[curidx] << p_bits[curidx2]) | (p_codes[curidx2]);
        v_bits  = p_bits[curidx] + p_bits[curidx2];
        put_bits(pb, v_bits, v_codes);
    }
}

static void quantize_and_encode_band_cost_UPAIR7_mips(struct AACEncContext *s,
                                                      PutBitContext *pb, const float *in,
                                                      const float *scaled, int size, int scale_idx,
                                                      int cb, const float lambda, const float uplim,
                                                      int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;

    uint8_t  *p_bits  = (uint8_t*) ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t*)ff_aac_spectral_codes[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx, sign1, count1, sign2, count2;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                              \n\t"
            ".set noreorder                         \n\t"

            "ori    $t4,        $zero,      7       \n\t"
            "ori    %[sign1],   $zero,      0       \n\t"
            "ori    %[sign2],   $zero,      0       \n\t"
            "slt    $t0,        $t4,        %[qc1]  \n\t"
            "slt    $t1,        $t4,        %[qc2]  \n\t"
            "slt    $t2,        $t4,        %[qc3]  \n\t"
            "slt    $t3,        $t4,        %[qc4]  \n\t"
            "movn   %[qc1],     $t4,        $t0     \n\t"
            "movn   %[qc2],     $t4,        $t1     \n\t"
            "movn   %[qc3],     $t4,        $t2     \n\t"
            "movn   %[qc4],     $t4,        $t3     \n\t"
            "lw     $t0,        0(%[in_int])        \n\t"
            "lw     $t1,        4(%[in_int])        \n\t"
            "lw     $t2,        8(%[in_int])        \n\t"
            "lw     $t3,        12(%[in_int])       \n\t"
            "slt    $t0,        $t0,        $zero   \n\t"
            "movn   %[sign1],   $t0,        %[qc1]  \n\t"
            "slt    $t2,        $t2,        $zero   \n\t"
            "movn   %[sign2],   $t2,        %[qc3]  \n\t"
            "slt    $t1,        $t1,        $zero   \n\t"
            "sll    $t0,        %[sign1],   1       \n\t"
            "or     $t0,        $t0,        $t1     \n\t"
            "movn   %[sign1],   $t0,        %[qc2]  \n\t"
            "slt    $t3,        $t3,        $zero   \n\t"
            "sll    $t0,        %[sign2],   1       \n\t"
            "or     $t0,        $t0,        $t3     \n\t"
            "movn   %[sign2],   $t0,        %[qc4]  \n\t"
            "slt    %[count1],  $zero,      %[qc1]  \n\t"
            "slt    $t1,        $zero,      %[qc2]  \n\t"
            "slt    %[count2],  $zero,      %[qc3]  \n\t"
            "slt    $t2,        $zero,      %[qc4]  \n\t"
            "addu   %[count1],  %[count1],  $t1     \n\t"
            "addu   %[count2],  %[count2],  $t2     \n\t"

            ".set pop                               \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3", "t4",
              "memory"
        );

        curidx  = 8 * qc1;
        curidx += qc2;

        v_codes = (p_codes[curidx] << count1) | sign1;
        v_bits  = p_bits[curidx] + count1;
        put_bits(pb, v_bits, v_codes);

        curidx  = 8 * qc3;
        curidx += qc4;

        v_codes = (p_codes[curidx] << count2) | sign2;
        v_bits  = p_bits[curidx] + count2;
        put_bits(pb, v_bits, v_codes);
    }
}

static void quantize_and_encode_band_cost_UPAIR12_mips(struct AACEncContext *s,
                                                       PutBitContext *pb, const float *in,
                                                       const float *scaled, int size, int scale_idx,
                                                       int cb, const float lambda, const float uplim,
                                                       int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;

    uint8_t  *p_bits  = (uint8_t*) ff_aac_spectral_bits[cb-1];
    uint16_t *p_codes = (uint16_t*)ff_aac_spectral_codes[cb-1];

    abs_pow34_v(s->scoefs, in, size);
    scaled = s->scoefs;
    for (i = 0; i < size; i += 4) {
        int curidx, sign1, count1, sign2, count2;
        int *in_int = (int *)&in[i];
        uint8_t v_bits;
        unsigned int v_codes;

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                              \n\t"
            ".set noreorder                         \n\t"

            "ori    $t4,        $zero,      12      \n\t"
            "ori    %[sign1],   $zero,      0       \n\t"
            "ori    %[sign2],   $zero,      0       \n\t"
            "slt    $t0,        $t4,        %[qc1]  \n\t"
            "slt    $t1,        $t4,        %[qc2]  \n\t"
            "slt    $t2,        $t4,        %[qc3]  \n\t"
            "slt    $t3,        $t4,        %[qc4]  \n\t"
            "movn   %[qc1],     $t4,        $t0     \n\t"
            "movn   %[qc2],     $t4,        $t1     \n\t"
            "movn   %[qc3],     $t4,        $t2     \n\t"
            "movn   %[qc4],     $t4,        $t3     \n\t"
            "lw     $t0,        0(%[in_int])        \n\t"
            "lw     $t1,        4(%[in_int])        \n\t"
            "lw     $t2,        8(%[in_int])        \n\t"
            "lw     $t3,        12(%[in_int])       \n\t"
            "slt    $t0,        $t0,        $zero   \n\t"
            "movn   %[sign1],   $t0,        %[qc1]  \n\t"
            "slt    $t2,        $t2,        $zero   \n\t"
            "movn   %[sign2],   $t2,        %[qc3]  \n\t"
            "slt    $t1,        $t1,        $zero   \n\t"
            "sll    $t0,        %[sign1],   1       \n\t"
            "or     $t0,        $t0,        $t1     \n\t"
            "movn   %[sign1],   $t0,        %[qc2]  \n\t"
            "slt    $t3,        $t3,        $zero   \n\t"
            "sll    $t0,        %[sign2],   1       \n\t"
            "or     $t0,        $t0,        $t3     \n\t"
            "movn   %[sign2],   $t0,        %[qc4]  \n\t"
            "slt    %[count1],  $zero,      %[qc1]  \n\t"
            "slt    $t1,        $zero,      %[qc2]  \n\t"
            "slt    %[count2],  $zero,      %[qc3]  \n\t"
            "slt    $t2,        $zero,      %[qc4]  \n\t"
            "addu   %[count1],  %[count1],  $t1     \n\t"
            "addu   %[count2],  %[count2],  $t2     \n\t"

            ".set pop                               \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3", "t4",
              "memory"
        );

        curidx  = 13 * qc1;
        curidx += qc2;

        v_codes = (p_codes[curidx] << count1) | sign1;
        v_bits  = p_bits[curidx] + count1;
        put_bits(pb, v_bits, v_codes);

        curidx  = 13 * qc3;
        curidx += qc4;

        v_codes = (p_codes[curidx] << count2) | sign2;
        v_bits  = p_bits[curidx] + count2;
        put_bits(pb, v_bits, v_codes);
    }
}

static void quantize_and_encode_band_cost_ESC_mips(struct AACEncContext *s,
                                                   PutBitContext *pb, const float *in,
                                                   const float *scaled, int size, int scale_idx,
                                                   int cb, const float lambda, const float uplim,
                                                   int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    int i;
    int qc1, qc2, qc3, qc4;

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

            qc1 = scaled[i  ] * Q34 + 0.4054f;
            qc2 = scaled[i+1] * Q34 + 0.4054f;
            qc3 = scaled[i+2] * Q34 + 0.4054f;
            qc4 = scaled[i+3] * Q34 + 0.4054f;

            __asm__ volatile (
                ".set push                                  \n\t"
                ".set noreorder                             \n\t"

                "ori        $t4,        $zero,      16      \n\t"
                "ori        %[sign1],   $zero,      0       \n\t"
                "ori        %[sign2],   $zero,      0       \n\t"
                "slt        $t0,        $t4,        %[qc1]  \n\t"
                "slt        $t1,        $t4,        %[qc2]  \n\t"
                "slt        $t2,        $t4,        %[qc3]  \n\t"
                "slt        $t3,        $t4,        %[qc4]  \n\t"
                "movn       %[qc1],     $t4,        $t0     \n\t"
                "movn       %[qc2],     $t4,        $t1     \n\t"
                "movn       %[qc3],     $t4,        $t2     \n\t"
                "movn       %[qc4],     $t4,        $t3     \n\t"
                "lw         $t0,        0(%[in_int])        \n\t"
                "lw         $t1,        4(%[in_int])        \n\t"
                "lw         $t2,        8(%[in_int])        \n\t"
                "lw         $t3,        12(%[in_int])       \n\t"
                "slt        $t0,        $t0,        $zero   \n\t"
                "movn       %[sign1],   $t0,        %[qc1]  \n\t"
                "slt        $t2,        $t2,        $zero   \n\t"
                "movn       %[sign2],   $t2,        %[qc3]  \n\t"
                "slt        $t1,        $t1,        $zero   \n\t"
                "sll        $t0,        %[sign1],   1       \n\t"
                "or         $t0,        $t0,        $t1     \n\t"
                "movn       %[sign1],   $t0,        %[qc2]  \n\t"
                "slt        $t3,        $t3,        $zero   \n\t"
                "sll        $t0,        %[sign2],   1       \n\t"
                "or         $t0,        $t0,        $t3     \n\t"
                "movn       %[sign2],   $t0,        %[qc4]  \n\t"
                "slt        %[count1],  $zero,      %[qc1]  \n\t"
                "slt        $t1,        $zero,      %[qc2]  \n\t"
                "slt        %[count2],  $zero,      %[qc3]  \n\t"
                "slt        $t2,        $zero,      %[qc4]  \n\t"
                "addu       %[count1],  %[count1],  $t1     \n\t"
                "addu       %[count2],  %[count2],  $t2     \n\t"

                ".set pop                                   \n\t"

                : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
                  [qc3]"+r"(qc3), [qc4]"+r"(qc4),
                  [sign1]"=&r"(sign1), [count1]"=&r"(count1),
                  [sign2]"=&r"(sign2), [count2]"=&r"(count2)
                : [in_int]"r"(in_int)
                : "t0", "t1", "t2", "t3", "t4",
                  "memory"
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
        }
    } else {
        for (i = 0; i < size; i += 4) {
            int curidx, curidx2, sign1, count1, sign2, count2;
            int *in_int = (int *)&in[i];
            uint8_t v_bits;
            unsigned int v_codes;
            int c1, c2, c3, c4;

            qc1 = scaled[i  ] * Q34 + 0.4054f;
            qc2 = scaled[i+1] * Q34 + 0.4054f;
            qc3 = scaled[i+2] * Q34 + 0.4054f;
            qc4 = scaled[i+3] * Q34 + 0.4054f;

            __asm__ volatile (
                ".set push                                  \n\t"
                ".set noreorder                             \n\t"

                "ori        $t4,        $zero,      16      \n\t"
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
                "slt        $t0,        $t4,        %[qc1]  \n\t"
                "slt        $t1,        $t4,        %[qc2]  \n\t"
                "slt        $t2,        $t4,        %[qc3]  \n\t"
                "slt        $t3,        $t4,        %[qc4]  \n\t"
                "movn       %[qc1],     $t4,        $t0     \n\t"
                "movn       %[qc2],     $t4,        $t1     \n\t"
                "movn       %[qc3],     $t4,        $t2     \n\t"
                "movn       %[qc4],     $t4,        $t3     \n\t"
                "lw         $t0,        0(%[in_int])        \n\t"
                "lw         $t1,        4(%[in_int])        \n\t"
                "lw         $t2,        8(%[in_int])        \n\t"
                "lw         $t3,        12(%[in_int])       \n\t"
                "slt        $t0,        $t0,        $zero   \n\t"
                "movn       %[sign1],   $t0,        %[qc1]  \n\t"
                "slt        $t2,        $t2,        $zero   \n\t"
                "movn       %[sign2],   $t2,        %[qc3]  \n\t"
                "slt        $t1,        $t1,        $zero   \n\t"
                "sll        $t0,        %[sign1],   1       \n\t"
                "or         $t0,        $t0,        $t1     \n\t"
                "movn       %[sign1],   $t0,        %[qc2]  \n\t"
                "slt        $t3,        $t3,        $zero   \n\t"
                "sll        $t0,        %[sign2],   1       \n\t"
                "or         $t0,        $t0,        $t3     \n\t"
                "movn       %[sign2],   $t0,        %[qc4]  \n\t"
                "slt        %[count1],  $zero,      %[qc1]  \n\t"
                "slt        $t1,        $zero,      %[qc2]  \n\t"
                "slt        %[count2],  $zero,      %[qc3]  \n\t"
                "slt        $t2,        $zero,      %[qc4]  \n\t"
                "addu       %[count1],  %[count1],  $t1     \n\t"
                "addu       %[count2],  %[count2],  $t2     \n\t"

                ".set pop                                   \n\t"

                : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
                  [qc3]"+r"(qc3), [qc4]"+r"(qc4),
                  [sign1]"=&r"(sign1), [count1]"=&r"(count1),
                  [sign2]"=&r"(sign2), [count2]"=&r"(count2),
                  [c1]"=&r"(c1), [c2]"=&r"(c2),
                  [c3]"=&r"(c3), [c4]"=&r"(c4)
                : [in_int]"r"(in_int)
                : "t0", "t1", "t2", "t3", "t4",
                  "memory"
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
        }
    }
}

static void (*const quantize_and_encode_band_cost_arr[])(struct AACEncContext *s,
                                                         PutBitContext *pb, const float *in,
                                                         const float *scaled, int size, int scale_idx,
                                                         int cb, const float lambda, const float uplim,
                                                         int *bits) = {
    NULL,
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
};

#define quantize_and_encode_band_cost(                                  \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits)                    \
    quantize_and_encode_band_cost_arr[cb](                              \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits)

static void quantize_and_encode_band_mips(struct AACEncContext *s, PutBitContext *pb,
                                          const float *in, int size, int scale_idx,
                                          int cb, const float lambda)
{
    quantize_and_encode_band_cost(s, pb, in, NULL, size, scale_idx, cb, lambda,
                                  INFINITY, NULL);
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "slt    %[qc1], $zero,  %[qc1]  \n\t"
            "slt    %[qc2], $zero,  %[qc2]  \n\t"
            "slt    %[qc3], $zero,  %[qc3]  \n\t"
            "slt    %[qc4], $zero,  %[qc4]  \n\t"
            "lw     $t0,    0(%[in_int])    \n\t"
            "lw     $t1,    4(%[in_int])    \n\t"
            "lw     $t2,    8(%[in_int])    \n\t"
            "lw     $t3,    12(%[in_int])   \n\t"
            "srl    $t0,    $t0,    31      \n\t"
            "srl    $t1,    $t1,    31      \n\t"
            "srl    $t2,    $t2,    31      \n\t"
            "srl    $t3,    $t3,    31      \n\t"
            "subu   $t4,    $zero,  %[qc1]  \n\t"
            "subu   $t5,    $zero,  %[qc2]  \n\t"
            "subu   $t6,    $zero,  %[qc3]  \n\t"
            "subu   $t7,    $zero,  %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t5,    $t1     \n\t"
            "movn   %[qc3], $t6,    $t2     \n\t"
            "movn   %[qc4], $t7,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3",
              "t4", "t5", "t6", "t7",
              "memory"
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    $t4,    $zero,  2       \n\t"
            "slt    $t0,    $t4,    %[qc1]  \n\t"
            "slt    $t1,    $t4,    %[qc2]  \n\t"
            "slt    $t2,    $t4,    %[qc3]  \n\t"
            "slt    $t3,    $t4,    %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t4,    $t1     \n\t"
            "movn   %[qc3], $t4,    $t2     \n\t"
            "movn   %[qc4], $t4,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            :
            : "t0", "t1", "t2", "t3", "t4"
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    $t4,    $zero,  4       \n\t"
            "slt    $t0,    $t4,    %[qc1]  \n\t"
            "slt    $t1,    $t4,    %[qc2]  \n\t"
            "slt    $t2,    $t4,    %[qc3]  \n\t"
            "slt    $t3,    $t4,    %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t4,    $t1     \n\t"
            "movn   %[qc3], $t4,    $t2     \n\t"
            "movn   %[qc4], $t4,    $t3     \n\t"
            "lw     $t0,    0(%[in_int])    \n\t"
            "lw     $t1,    4(%[in_int])    \n\t"
            "lw     $t2,    8(%[in_int])    \n\t"
            "lw     $t3,    12(%[in_int])   \n\t"
            "srl    $t0,    $t0,    31      \n\t"
            "srl    $t1,    $t1,    31      \n\t"
            "srl    $t2,    $t2,    31      \n\t"
            "srl    $t3,    $t3,    31      \n\t"
            "subu   $t4,    $zero,  %[qc1]  \n\t"
            "subu   $t5,    $zero,  %[qc2]  \n\t"
            "subu   $t6,    $zero,  %[qc3]  \n\t"
            "subu   $t7,    $zero,  %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t5,    $t1     \n\t"
            "movn   %[qc3], $t6,    $t2     \n\t"
            "movn   %[qc4], $t7,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3",
              "t4", "t5", "t6", "t7",
              "memory"
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    $t4,    $zero,  7       \n\t"
            "slt    $t0,    $t4,    %[qc1]  \n\t"
            "slt    $t1,    $t4,    %[qc2]  \n\t"
            "slt    $t2,    $t4,    %[qc3]  \n\t"
            "slt    $t3,    $t4,    %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t4,    $t1     \n\t"
            "movn   %[qc3], $t4,    $t2     \n\t"
            "movn   %[qc4], $t4,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            :
            : "t0", "t1", "t2", "t3", "t4"
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                      \n\t"
            ".set noreorder                 \n\t"

            "ori    $t4,    $zero,  12      \n\t"
            "slt    $t0,    $t4,    %[qc1]  \n\t"
            "slt    $t1,    $t4,    %[qc2]  \n\t"
            "slt    $t2,    $t4,    %[qc3]  \n\t"
            "slt    $t3,    $t4,    %[qc4]  \n\t"
            "movn   %[qc1], $t4,    $t0     \n\t"
            "movn   %[qc2], $t4,    $t1     \n\t"
            "movn   %[qc3], $t4,    $t2     \n\t"
            "movn   %[qc4], $t4,    $t3     \n\t"

            ".set pop                       \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            :
            : "t0", "t1", "t2", "t3", "t4"
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        $t4,        $zero,  15          \n\t"
            "ori        $t5,        $zero,  16          \n\t"
            "shll_s.w   %[c1],      %[qc1], 18          \n\t"
            "shll_s.w   %[c2],      %[qc2], 18          \n\t"
            "shll_s.w   %[c3],      %[qc3], 18          \n\t"
            "shll_s.w   %[c4],      %[qc4], 18          \n\t"
            "srl        %[c1],      %[c1],  18          \n\t"
            "srl        %[c2],      %[c2],  18          \n\t"
            "srl        %[c3],      %[c3],  18          \n\t"
            "srl        %[c4],      %[c4],  18          \n\t"
            "slt        %[cond0],   $t4,    %[qc1]      \n\t"
            "slt        %[cond1],   $t4,    %[qc2]      \n\t"
            "slt        %[cond2],   $t4,    %[qc3]      \n\t"
            "slt        %[cond3],   $t4,    %[qc4]      \n\t"
            "movn       %[qc1],     $t5,    %[cond0]    \n\t"
            "movn       %[qc2],     $t5,    %[cond1]    \n\t"
            "movn       %[qc3],     $t5,    %[cond2]    \n\t"
            "movn       %[qc4],     $t5,    %[cond3]    \n\t"
            "ori        $t5,        $zero,  31          \n\t"
            "clz        %[c1],      %[c1]               \n\t"
            "clz        %[c2],      %[c2]               \n\t"
            "clz        %[c3],      %[c3]               \n\t"
            "clz        %[c4],      %[c4]               \n\t"
            "subu       %[c1],      $t5,    %[c1]       \n\t"
            "subu       %[c2],      $t5,    %[c2]       \n\t"
            "subu       %[c3],      $t5,    %[c3]       \n\t"
            "subu       %[c4],      $t5,    %[c4]       \n\t"
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
              [c3]"=&r"(c3), [c4]"=&r"(c4)
            :
            : "t4", "t5"
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
                                     int *bits)
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
                                     int *bits)
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
    return cost * lambda;
}

static float get_band_cost_SQUAD_mips(struct AACEncContext *s,
                                      PutBitContext *pb, const float *in,
                                      const float *scaled, int size, int scale_idx,
                                      int cb, const float lambda, const float uplim,
                                      int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "slt        %[qc1], $zero,  %[qc1]          \n\t"
            "slt        %[qc2], $zero,  %[qc2]          \n\t"
            "slt        %[qc3], $zero,  %[qc3]          \n\t"
            "slt        %[qc4], $zero,  %[qc4]          \n\t"
            "lw         $t0,    0(%[in_int])            \n\t"
            "lw         $t1,    4(%[in_int])            \n\t"
            "lw         $t2,    8(%[in_int])            \n\t"
            "lw         $t3,    12(%[in_int])           \n\t"
            "srl        $t0,    $t0,    31              \n\t"
            "srl        $t1,    $t1,    31              \n\t"
            "srl        $t2,    $t2,    31              \n\t"
            "srl        $t3,    $t3,    31              \n\t"
            "subu       $t4,    $zero,  %[qc1]          \n\t"
            "subu       $t5,    $zero,  %[qc2]          \n\t"
            "subu       $t6,    $zero,  %[qc3]          \n\t"
            "subu       $t7,    $zero,  %[qc4]          \n\t"
            "movn       %[qc1], $t4,    $t0             \n\t"
            "movn       %[qc2], $t5,    $t1             \n\t"
            "movn       %[qc3], $t6,    $t2             \n\t"
            "movn       %[qc4], $t7,    $t3             \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3",
              "t4", "t5", "t6", "t7",
              "memory"
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
    return cost * lambda + curbits;
}

static float get_band_cost_UQUAD_mips(struct AACEncContext *s,
                                      PutBitContext *pb, const float *in,
                                      const float *scaled, int size, int scale_idx,
                                      int cb, const float lambda, const float uplim,
                                      int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
    int curbits = 0;
    int qc1, qc2, qc3, qc4;

    uint8_t *p_bits  = (uint8_t*)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float  *)ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec;
        int curidx;
        float *in_pos = (float *)&in[i];
        float di0, di1, di2, di3;

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        $t4,    $zero,  2               \n\t"
            "slt        $t0,    $t4,    %[qc1]          \n\t"
            "slt        $t1,    $t4,    %[qc2]          \n\t"
            "slt        $t2,    $t4,    %[qc3]          \n\t"
            "slt        $t3,    $t4,    %[qc4]          \n\t"
            "movn       %[qc1], $t4,    $t0             \n\t"
            "movn       %[qc2], $t4,    $t1             \n\t"
            "movn       %[qc3], $t4,    $t2             \n\t"
            "movn       %[qc4], $t4,    $t3             \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            :
            : "t0", "t1", "t2", "t3", "t4"
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
    return cost * lambda + curbits;
}

static float get_band_cost_SPAIR_mips(struct AACEncContext *s,
                                      PutBitContext *pb, const float *in,
                                      const float *scaled, int size, int scale_idx,
                                      int cb, const float lambda, const float uplim,
                                      int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        $t4,    $zero,  4               \n\t"
            "slt        $t0,    $t4,    %[qc1]          \n\t"
            "slt        $t1,    $t4,    %[qc2]          \n\t"
            "slt        $t2,    $t4,    %[qc3]          \n\t"
            "slt        $t3,    $t4,    %[qc4]          \n\t"
            "movn       %[qc1], $t4,    $t0             \n\t"
            "movn       %[qc2], $t4,    $t1             \n\t"
            "movn       %[qc3], $t4,    $t2             \n\t"
            "movn       %[qc4], $t4,    $t3             \n\t"
            "lw         $t0,    0(%[in_int])            \n\t"
            "lw         $t1,    4(%[in_int])            \n\t"
            "lw         $t2,    8(%[in_int])            \n\t"
            "lw         $t3,    12(%[in_int])           \n\t"
            "srl        $t0,    $t0,    31              \n\t"
            "srl        $t1,    $t1,    31              \n\t"
            "srl        $t2,    $t2,    31              \n\t"
            "srl        $t3,    $t3,    31              \n\t"
            "subu       $t4,    $zero,  %[qc1]          \n\t"
            "subu       $t5,    $zero,  %[qc2]          \n\t"
            "subu       $t6,    $zero,  %[qc3]          \n\t"
            "subu       $t7,    $zero,  %[qc4]          \n\t"
            "movn       %[qc1], $t4,    $t0             \n\t"
            "movn       %[qc2], $t5,    $t1             \n\t"
            "movn       %[qc3], $t6,    $t2             \n\t"
            "movn       %[qc4], $t7,    $t3             \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3",
              "t4", "t5", "t6", "t7",
              "memory"
        );

        curidx = 9 * qc1;
        curidx += qc2 + 40;

        curidx2 = 9 * qc3;
        curidx2 += qc4 + 40;

        curbits += p_bits[curidx];
        curbits += p_bits[curidx2];

        vec     = &p_codes[curidx*2];
        vec2    = &p_codes[curidx2*2];

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
    return cost * lambda + curbits;
}

static float get_band_cost_UPAIR7_mips(struct AACEncContext *s,
                                       PutBitContext *pb, const float *in,
                                       const float *scaled, int size, int scale_idx,
                                       int cb, const float lambda, const float uplim,
                                       int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                          \n\t"
            ".set noreorder                                     \n\t"

            "ori        $t4,        $zero,      7               \n\t"
            "ori        %[sign1],   $zero,      0               \n\t"
            "ori        %[sign2],   $zero,      0               \n\t"
            "slt        $t0,        $t4,        %[qc1]          \n\t"
            "slt        $t1,        $t4,        %[qc2]          \n\t"
            "slt        $t2,        $t4,        %[qc3]          \n\t"
            "slt        $t3,        $t4,        %[qc4]          \n\t"
            "movn       %[qc1],     $t4,        $t0             \n\t"
            "movn       %[qc2],     $t4,        $t1             \n\t"
            "movn       %[qc3],     $t4,        $t2             \n\t"
            "movn       %[qc4],     $t4,        $t3             \n\t"
            "lw         $t0,        0(%[in_int])                \n\t"
            "lw         $t1,        4(%[in_int])                \n\t"
            "lw         $t2,        8(%[in_int])                \n\t"
            "lw         $t3,        12(%[in_int])               \n\t"
            "slt        $t0,        $t0,        $zero           \n\t"
            "movn       %[sign1],   $t0,        %[qc1]          \n\t"
            "slt        $t2,        $t2,        $zero           \n\t"
            "movn       %[sign2],   $t2,        %[qc3]          \n\t"
            "slt        $t1,        $t1,        $zero           \n\t"
            "sll        $t0,        %[sign1],   1               \n\t"
            "or         $t0,        $t0,        $t1             \n\t"
            "movn       %[sign1],   $t0,        %[qc2]          \n\t"
            "slt        $t3,        $t3,        $zero           \n\t"
            "sll        $t0,        %[sign2],   1               \n\t"
            "or         $t0,        $t0,        $t3             \n\t"
            "movn       %[sign2],   $t0,        %[qc4]          \n\t"
            "slt        %[count1],  $zero,      %[qc1]          \n\t"
            "slt        $t1,        $zero,      %[qc2]          \n\t"
            "slt        %[count2],  $zero,      %[qc3]          \n\t"
            "slt        $t2,        $zero,      %[qc4]          \n\t"
            "addu       %[count1],  %[count1],  $t1             \n\t"
            "addu       %[count2],  %[count2],  $t2             \n\t"

            ".set pop                                           \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3", "t4",
              "memory"
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
    return cost * lambda + curbits;
}

static float get_band_cost_UPAIR12_mips(struct AACEncContext *s,
                                        PutBitContext *pb, const float *in,
                                        const float *scaled, int size, int scale_idx,
                                        int cb, const float lambda, const float uplim,
                                        int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    int i;
    float cost = 0;
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

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                          \n\t"
            ".set noreorder                                     \n\t"

            "ori        $t4,        $zero,      12              \n\t"
            "ori        %[sign1],   $zero,      0               \n\t"
            "ori        %[sign2],   $zero,      0               \n\t"
            "slt        $t0,        $t4,        %[qc1]          \n\t"
            "slt        $t1,        $t4,        %[qc2]          \n\t"
            "slt        $t2,        $t4,        %[qc3]          \n\t"
            "slt        $t3,        $t4,        %[qc4]          \n\t"
            "movn       %[qc1],     $t4,        $t0             \n\t"
            "movn       %[qc2],     $t4,        $t1             \n\t"
            "movn       %[qc3],     $t4,        $t2             \n\t"
            "movn       %[qc4],     $t4,        $t3             \n\t"
            "lw         $t0,        0(%[in_int])                \n\t"
            "lw         $t1,        4(%[in_int])                \n\t"
            "lw         $t2,        8(%[in_int])                \n\t"
            "lw         $t3,        12(%[in_int])               \n\t"
            "slt        $t0,        $t0,        $zero           \n\t"
            "movn       %[sign1],   $t0,        %[qc1]          \n\t"
            "slt        $t2,        $t2,        $zero           \n\t"
            "movn       %[sign2],   $t2,        %[qc3]          \n\t"
            "slt        $t1,        $t1,        $zero           \n\t"
            "sll        $t0,        %[sign1],   1               \n\t"
            "or         $t0,        $t0,        $t1             \n\t"
            "movn       %[sign1],   $t0,        %[qc2]          \n\t"
            "slt        $t3,        $t3,        $zero           \n\t"
            "sll        $t0,        %[sign2],   1               \n\t"
            "or         $t0,        $t0,        $t3             \n\t"
            "movn       %[sign2],   $t0,        %[qc4]          \n\t"
            "slt        %[count1],  $zero,      %[qc1]          \n\t"
            "slt        $t1,        $zero,      %[qc2]          \n\t"
            "slt        %[count2],  $zero,      %[qc3]          \n\t"
            "slt        $t2,        $zero,      %[qc4]          \n\t"
            "addu       %[count1],  %[count1],  $t1             \n\t"
            "addu       %[count2],  %[count2],  $t2             \n\t"

            ".set pop                                           \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [sign1]"=&r"(sign1), [count1]"=&r"(count1),
              [sign2]"=&r"(sign2), [count2]"=&r"(count2)
            : [in_int]"r"(in_int)
            : "t0", "t1", "t2", "t3", "t4",
              "memory"
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
    return cost * lambda + curbits;
}

static float get_band_cost_ESC_mips(struct AACEncContext *s,
                                    PutBitContext *pb, const float *in,
                                    const float *scaled, int size, int scale_idx,
                                    int cb, const float lambda, const float uplim,
                                    int *bits)
{
    const float Q34 = ff_aac_pow34sf_tab[POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    const float CLIPPED_ESCAPE = 165140.0f * IQ;
    int i;
    float cost = 0;
    int qc1, qc2, qc3, qc4;
    int curbits = 0;

    uint8_t *p_bits  = (uint8_t*)ff_aac_spectral_bits[cb-1];
    float   *p_codes = (float*  )ff_aac_codebook_vectors[cb-1];

    for (i = 0; i < size; i += 4) {
        const float *vec, *vec2;
        int curidx, curidx2;
        float t1, t2, t3, t4;
        float di1, di2, di3, di4;
        int cond0, cond1, cond2, cond3;
        int c1, c2, c3, c4;

        qc1 = scaled[i  ] * Q34 + 0.4054f;
        qc2 = scaled[i+1] * Q34 + 0.4054f;
        qc3 = scaled[i+2] * Q34 + 0.4054f;
        qc4 = scaled[i+3] * Q34 + 0.4054f;

        __asm__ volatile (
            ".set push                                  \n\t"
            ".set noreorder                             \n\t"

            "ori        $t4,        $zero,  15          \n\t"
            "ori        $t5,        $zero,  16          \n\t"
            "shll_s.w   %[c1],      %[qc1], 18          \n\t"
            "shll_s.w   %[c2],      %[qc2], 18          \n\t"
            "shll_s.w   %[c3],      %[qc3], 18          \n\t"
            "shll_s.w   %[c4],      %[qc4], 18          \n\t"
            "srl        %[c1],      %[c1],  18          \n\t"
            "srl        %[c2],      %[c2],  18          \n\t"
            "srl        %[c3],      %[c3],  18          \n\t"
            "srl        %[c4],      %[c4],  18          \n\t"
            "slt        %[cond0],   $t4,    %[qc1]      \n\t"
            "slt        %[cond1],   $t4,    %[qc2]      \n\t"
            "slt        %[cond2],   $t4,    %[qc3]      \n\t"
            "slt        %[cond3],   $t4,    %[qc4]      \n\t"
            "movn       %[qc1],     $t5,    %[cond0]    \n\t"
            "movn       %[qc2],     $t5,    %[cond1]    \n\t"
            "movn       %[qc3],     $t5,    %[cond2]    \n\t"
            "movn       %[qc4],     $t5,    %[cond3]    \n\t"

            ".set pop                                   \n\t"

            : [qc1]"+r"(qc1), [qc2]"+r"(qc2),
              [qc3]"+r"(qc3), [qc4]"+r"(qc4),
              [cond0]"=&r"(cond0), [cond1]"=&r"(cond1),
              [cond2]"=&r"(cond2), [cond3]"=&r"(cond3),
              [c1]"=&r"(c1), [c2]"=&r"(c2),
              [c3]"=&r"(c3), [c4]"=&r"(c4)
            :
            : "t4", "t5"
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
            } else {
                di1 = t1 - c1 * cbrtf(c1) * IQ;
            }
        } else
            di1 = t1 - vec[0] * IQ;

        if (cond1) {
            if (t2 >= CLIPPED_ESCAPE) {
                di2 = t2 - CLIPPED_ESCAPE;
            } else {
                di2 = t2 - c2 * cbrtf(c2) * IQ;
            }
        } else
            di2 = t2 - vec[1] * IQ;

        if (cond2) {
            if (t3 >= CLIPPED_ESCAPE) {
                di3 = t3 - CLIPPED_ESCAPE;
            } else {
                di3 = t3 - c3 * cbrtf(c3) * IQ;
            }
        } else
            di3 = t3 - vec2[0] * IQ;

        if (cond3) {
            if (t4 >= CLIPPED_ESCAPE) {
                di4 = t4 - CLIPPED_ESCAPE;
            } else {
                di4 = t4 - c4 * cbrtf(c4) * IQ;
            }
        } else
            di4 = t4 - vec2[1]*IQ;

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
                                          int *bits) = {
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
};

#define get_band_cost(                                  \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits)                    \
    get_band_cost_arr[cb](                              \
                                s, pb, in, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits)

static float quantize_band_cost(struct AACEncContext *s, const float *in,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits)
{
    return get_band_cost(s, NULL, in, scaled, size, scale_idx, cb, lambda, uplim, bits);
}

static void search_for_quantizers_twoloop_mips(AVCodecContext *avctx,
                                               AACEncContext *s,
                                               SingleChannelElement *sce,
                                               const float lambda)
{
    int start = 0, i, w, w2, g;
    int destbits = avctx->bit_rate * 1024.0 / avctx->sample_rate / avctx->channels;
    float dists[128] = { 0 }, uplims[128];
    float maxvals[128];
    int fflag, minscaler;
    int its  = 0;
    int allz = 0;
    float minthr = INFINITY;

    destbits = FFMIN(destbits, 5800);
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (g = 0;  g < sce->ics.num_swb; g++) {
            int nz = 0;
            float uplim = 0.0f;
            for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                FFPsyBand *band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
                uplim += band->threshold;
                if (band->energy <= band->threshold || band->threshold == 0.0f) {
                    sce->zeroes[(w+w2)*16+g] = 1;
                    continue;
                }
                nz = 1;
            }
            uplims[w*16+g] = uplim *512;
            sce->zeroes[w*16+g] = !nz;
            if (nz)
                minthr = FFMIN(minthr, uplim);
            allz |= nz;
        }
    }
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (g = 0;  g < sce->ics.num_swb; g++) {
            if (sce->zeroes[w*16+g]) {
                sce->sf_idx[w*16+g] = SCALE_ONE_POS;
                continue;
            }
            sce->sf_idx[w*16+g] = SCALE_ONE_POS + FFMIN(log2f(uplims[w*16+g]/minthr)*4,59);
        }
    }

    if (!allz)
        return;
    abs_pow34_v(s->scoefs, sce->coeffs, 1024);

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        start = w*128;
        for (g = 0;  g < sce->ics.num_swb; g++) {
            const float *scaled = s->scoefs + start;
            maxvals[w*16+g] = find_max_val(sce->ics.group_len[w], sce->ics.swb_sizes[g], scaled);
            start += sce->ics.swb_sizes[g];
        }
    }

    do {
        int tbits, qstep;
        minscaler = sce->sf_idx[0];
        qstep = its ? 1 : 32;
        do {
            int prev = -1;
            tbits = 0;
            fflag = 0;

            if (qstep > 1) {
                for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
                    start = w*128;
                    for (g = 0;  g < sce->ics.num_swb; g++) {
                        const float *coefs = sce->coeffs + start;
                        const float *scaled = s->scoefs + start;
                        int bits = 0;
                        int cb;

                        if (sce->zeroes[w*16+g] || sce->sf_idx[w*16+g] >= 218) {
                            start += sce->ics.swb_sizes[g];
                            continue;
                        }
                        minscaler = FFMIN(minscaler, sce->sf_idx[w*16+g]);
                        cb = find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]);
                        for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                            int b;
                            bits += quantize_band_cost_bits(s, coefs + w2*128,
                                                            scaled + w2*128,
                                                            sce->ics.swb_sizes[g],
                                                            sce->sf_idx[w*16+g],
                                                            cb,
                                                            1.0f,
                                                            INFINITY,
                                                            &b);
                        }
                        if (prev != -1) {
                            bits += ff_aac_scalefactor_bits[sce->sf_idx[w*16+g] - prev + SCALE_DIFF_ZERO];
                        }
                        tbits += bits;
                        start += sce->ics.swb_sizes[g];
                        prev = sce->sf_idx[w*16+g];
                    }
                }
            }
            else {
                for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
                    start = w*128;
                    for (g = 0;  g < sce->ics.num_swb; g++) {
                        const float *coefs = sce->coeffs + start;
                        const float *scaled = s->scoefs + start;
                        int bits = 0;
                        int cb;
                        float dist = 0.0f;

                        if (sce->zeroes[w*16+g] || sce->sf_idx[w*16+g] >= 218) {
                            start += sce->ics.swb_sizes[g];
                            continue;
                        }
                        minscaler = FFMIN(minscaler, sce->sf_idx[w*16+g]);
                        cb = find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]);
                        for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
                            int b;
                            dist += quantize_band_cost(s, coefs + w2*128,
                                                       scaled + w2*128,
                                                       sce->ics.swb_sizes[g],
                                                       sce->sf_idx[w*16+g],
                                                       cb,
                                                       1.0f,
                                                       INFINITY,
                                                       &b);
                            bits += b;
                        }
                        dists[w*16+g] = dist - bits;
                        if (prev != -1) {
                            bits += ff_aac_scalefactor_bits[sce->sf_idx[w*16+g] - prev + SCALE_DIFF_ZERO];
                        }
                        tbits += bits;
                        start += sce->ics.swb_sizes[g];
                        prev = sce->sf_idx[w*16+g];
                    }
                }
            }
            if (tbits > destbits) {
                for (i = 0; i < 128; i++)
                    if (sce->sf_idx[i] < 218 - qstep)
                        sce->sf_idx[i] += qstep;
            } else {
                for (i = 0; i < 128; i++)
                    if (sce->sf_idx[i] > 60 - qstep)
                        sce->sf_idx[i] -= qstep;
            }
            qstep >>= 1;
            if (!qstep && tbits > destbits*1.02 && sce->sf_idx[0] < 217)
                qstep = 1;
        } while (qstep);

        fflag = 0;
        minscaler = av_clip(minscaler, 60, 255 - SCALE_MAX_DIFF);
        for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
            for (g = 0; g < sce->ics.num_swb; g++) {
                int prevsc = sce->sf_idx[w*16+g];
                if (dists[w*16+g] > uplims[w*16+g] && sce->sf_idx[w*16+g] > 60) {
                    if (find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]-1))
                        sce->sf_idx[w*16+g]--;
                    else
                        sce->sf_idx[w*16+g]-=2;
                }
                sce->sf_idx[w*16+g] = av_clip(sce->sf_idx[w*16+g], minscaler, minscaler + SCALE_MAX_DIFF);
                sce->sf_idx[w*16+g] = FFMIN(sce->sf_idx[w*16+g], 219);
                if (sce->sf_idx[w*16+g] != prevsc)
                    fflag = 1;
                sce->band_type[w*16+g] = find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]);
            }
        }
        its++;
    } while (fflag && its < 10);
}

static void search_for_ms_mips(AACEncContext *s, ChannelElement *cpe,
                               const float lambda)
{
    int start = 0, i, w, w2, g;
    float M[128], S[128];
    float *L34 = s->scoefs, *R34 = s->scoefs + 128, *M34 = s->scoefs + 128*2, *S34 = s->scoefs + 128*3;
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    if (!cpe->common_window)
        return;
    for (w = 0; w < sce0->ics.num_windows; w += sce0->ics.group_len[w]) {
        for (g = 0;  g < sce0->ics.num_swb; g++) {
            if (!cpe->ch[0].zeroes[w*16+g] && !cpe->ch[1].zeroes[w*16+g]) {
                float dist1 = 0.0f, dist2 = 0.0f;
                for (w2 = 0; w2 < sce0->ics.group_len[w]; w2++) {
                    FFPsyBand *band0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
                    FFPsyBand *band1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
                    float minthr = FFMIN(band0->threshold, band1->threshold);
                    float maxthr = FFMAX(band0->threshold, band1->threshold);
                    for (i = 0; i < sce0->ics.swb_sizes[g]; i+=4) {
                        M[i  ] = (sce0->coeffs[start+w2*128+i  ]
                                + sce1->coeffs[start+w2*128+i  ]) * 0.5;
                        M[i+1] = (sce0->coeffs[start+w2*128+i+1]
                                + sce1->coeffs[start+w2*128+i+1]) * 0.5;
                        M[i+2] = (sce0->coeffs[start+w2*128+i+2]
                                + sce1->coeffs[start+w2*128+i+2]) * 0.5;
                        M[i+3] = (sce0->coeffs[start+w2*128+i+3]
                                + sce1->coeffs[start+w2*128+i+3]) * 0.5;

                        S[i  ] =  M[i  ]
                                - sce1->coeffs[start+w2*128+i  ];
                        S[i+1] =  M[i+1]
                                - sce1->coeffs[start+w2*128+i+1];
                        S[i+2] =  M[i+2]
                                - sce1->coeffs[start+w2*128+i+2];
                        S[i+3] =  M[i+3]
                                - sce1->coeffs[start+w2*128+i+3];
                   }
                    abs_pow34_v(L34, sce0->coeffs+start+w2*128, sce0->ics.swb_sizes[g]);
                    abs_pow34_v(R34, sce1->coeffs+start+w2*128, sce0->ics.swb_sizes[g]);
                    abs_pow34_v(M34, M,                         sce0->ics.swb_sizes[g]);
                    abs_pow34_v(S34, S,                         sce0->ics.swb_sizes[g]);
                    dist1 += quantize_band_cost(s, sce0->coeffs + start + w2*128,
                                                L34,
                                                sce0->ics.swb_sizes[g],
                                                sce0->sf_idx[(w+w2)*16+g],
                                                sce0->band_type[(w+w2)*16+g],
                                                lambda / band0->threshold, INFINITY, NULL);
                    dist1 += quantize_band_cost(s, sce1->coeffs + start + w2*128,
                                                R34,
                                                sce1->ics.swb_sizes[g],
                                                sce1->sf_idx[(w+w2)*16+g],
                                                sce1->band_type[(w+w2)*16+g],
                                                lambda / band1->threshold, INFINITY, NULL);
                    dist2 += quantize_band_cost(s, M,
                                                M34,
                                                sce0->ics.swb_sizes[g],
                                                sce0->sf_idx[(w+w2)*16+g],
                                                sce0->band_type[(w+w2)*16+g],
                                                lambda / maxthr, INFINITY, NULL);
                    dist2 += quantize_band_cost(s, S,
                                                S34,
                                                sce1->ics.swb_sizes[g],
                                                sce1->sf_idx[(w+w2)*16+g],
                                                sce1->band_type[(w+w2)*16+g],
                                                lambda / minthr, INFINITY, NULL);
                }
                cpe->ms_mask[w*16+g] = dist2 < dist1;
            }
            start += sce0->ics.swb_sizes[g];
        }
    }
}
#endif /*HAVE_MIPSFPU */

static void codebook_trellis_rate_mips(AACEncContext *s, SingleChannelElement *sce,
                                       int win, int group_len, const float lambda)
{
    BandCodingPath path[120][12];
    int w, swb, cb, start, size;
    int i, j;
    const int max_sfb  = sce->ics.max_sfb;
    const int run_bits = sce->ics.num_windows == 1 ? 5 : 3;
    const int run_esc  = (1 << run_bits) - 1;
    int idx, ppos, count;
    int stackrun[120], stackcb[120], stack_len;
    float next_minbits = INFINITY;
    int next_mincb = 0;

    abs_pow34_v(s->scoefs, sce->coeffs, 1024);
    start = win*128;
    for (cb = 0; cb < 12; cb++) {
        path[0][cb].cost     = run_bits+4;
        path[0][cb].prev_idx = -1;
        path[0][cb].run      = 0;
    }
    for (swb = 0; swb < max_sfb; swb++) {
        size = sce->ics.swb_sizes[swb];
        if (sce->zeroes[win*16 + swb]) {
            float cost_stay_here = path[swb][0].cost;
            float cost_get_here  = next_minbits + run_bits + 4;
            if (   run_value_bits[sce->ics.num_windows == 8][path[swb][0].run]
                != run_value_bits[sce->ics.num_windows == 8][path[swb][0].run+1])
                cost_stay_here += run_bits;
            if (cost_get_here < cost_stay_here) {
                path[swb+1][0].prev_idx = next_mincb;
                path[swb+1][0].cost     = cost_get_here;
                path[swb+1][0].run      = 1;
            } else {
                path[swb+1][0].prev_idx = 0;
                path[swb+1][0].cost     = cost_stay_here;
                path[swb+1][0].run      = path[swb][0].run + 1;
            }
            next_minbits = path[swb+1][0].cost;
            next_mincb = 0;
            for (cb = 1; cb < 12; cb++) {
                path[swb+1][cb].cost = 61450;
                path[swb+1][cb].prev_idx = -1;
                path[swb+1][cb].run = 0;
            }
        } else {
            float minbits = next_minbits;
            int mincb = next_mincb;
            int startcb = sce->band_type[win*16+swb];
            next_minbits = INFINITY;
            next_mincb = 0;
            for (cb = 0; cb < startcb; cb++) {
                path[swb+1][cb].cost = 61450;
                path[swb+1][cb].prev_idx = -1;
                path[swb+1][cb].run = 0;
            }
            for (cb = startcb; cb < 12; cb++) {
                float cost_stay_here, cost_get_here;
                float bits = 0.0f;
                for (w = 0; w < group_len; w++) {
                    bits += quantize_band_cost_bits(s, sce->coeffs + start + w*128,
                                                    s->scoefs + start + w*128, size,
                                                    sce->sf_idx[(win+w)*16+swb], cb,
                                                    0, INFINITY, NULL);
                }
                cost_stay_here = path[swb][cb].cost + bits;
                cost_get_here  = minbits            + bits + run_bits + 4;
                if (   run_value_bits[sce->ics.num_windows == 8][path[swb][cb].run]
                    != run_value_bits[sce->ics.num_windows == 8][path[swb][cb].run+1])
                    cost_stay_here += run_bits;
                if (cost_get_here < cost_stay_here) {
                    path[swb+1][cb].prev_idx = mincb;
                    path[swb+1][cb].cost     = cost_get_here;
                    path[swb+1][cb].run      = 1;
                } else {
                    path[swb+1][cb].prev_idx = cb;
                    path[swb+1][cb].cost     = cost_stay_here;
                    path[swb+1][cb].run      = path[swb][cb].run + 1;
                }
                if (path[swb+1][cb].cost < next_minbits) {
                    next_minbits = path[swb+1][cb].cost;
                    next_mincb = cb;
                }
            }
        }
        start += sce->ics.swb_sizes[swb];
    }

    stack_len = 0;
    idx       = 0;
    for (cb = 1; cb < 12; cb++)
        if (path[max_sfb][cb].cost < path[max_sfb][idx].cost)
            idx = cb;
    ppos = max_sfb;
    while (ppos > 0) {
        av_assert1(idx >= 0);
        cb = idx;
        stackrun[stack_len] = path[ppos][cb].run;
        stackcb [stack_len] = cb;
        idx = path[ppos-path[ppos][cb].run+1][cb].prev_idx;
        ppos -= path[ppos][cb].run;
        stack_len++;
    }

    start = 0;
    for (i = stack_len - 1; i >= 0; i--) {
        put_bits(&s->pb, 4, stackcb[i]);
        count = stackrun[i];
        memset(sce->zeroes + win*16 + start, !stackcb[i], count);
        for (j = 0; j < count; j++) {
            sce->band_type[win*16 + start] =  stackcb[i];
            start++;
        }
        while (count >= run_esc) {
            put_bits(&s->pb, run_bits, run_esc);
            count -= run_esc;
        }
        put_bits(&s->pb, run_bits, count);
    }
}
#endif /* HAVE_INLINE_ASM */

void ff_aac_coder_init_mips(AACEncContext *c) {
#if HAVE_INLINE_ASM
    AACCoefficientsEncoder *e = c->coder;
    int option = c->options.aac_coder;

    if (option == 2) {
        e->quantize_and_encode_band = quantize_and_encode_band_mips;
        e->encode_window_bands_info = codebook_trellis_rate_mips;
#if HAVE_MIPSFPU
        e->search_for_quantizers    = search_for_quantizers_twoloop_mips;
        e->search_for_ms            = search_for_ms_mips;
#endif /* HAVE_MIPSFPU */
    }
#endif /* HAVE_INLINE_ASM */
}
