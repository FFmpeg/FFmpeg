/*
 * AAC encoder quantizer
 * Copyright (C) 2015 Rostislav Pehlivanov
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
 * AAC encoder quantizer
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#ifndef AVCODEC_AACENC_QUANTIZATION_H
#define AVCODEC_AACENC_QUANTIZATION_H

#include "aactab.h"
#include "aacenc.h"
#include "aacenctab.h"
#include "aacenc_utils.h"

/**
 * Calculate rate distortion cost for quantizing with given codebook
 *
 * @return quantization distortion
 */
static av_always_inline float quantize_and_encode_band_cost_template(
                                struct AACEncContext *s,
                                PutBitContext *pb, const float *in, float *out,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits, float *energy, int BT_ZERO, int BT_UNSIGNED,
                                int BT_PAIR, int BT_ESC, int BT_NOISE, int BT_STEREO,
                                const float ROUNDING)
{
    const int q_idx = POW_SF2_ZERO - scale_idx + SCALE_ONE_POS - SCALE_DIV_512;
    const float Q   = ff_aac_pow2sf_tab [q_idx];
    const float Q34 = ff_aac_pow34sf_tab[q_idx];
    const float IQ  = ff_aac_pow2sf_tab [POW_SF2_ZERO + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    const float CLIPPED_ESCAPE = 165140.0f*IQ;
    int i, j;
    float cost = 0;
    float qenergy = 0;
    const int dim = BT_PAIR ? 2 : 4;
    int resbits = 0;
    int off;

    if (BT_ZERO || BT_NOISE || BT_STEREO) {
        for (i = 0; i < size; i++)
            cost += in[i]*in[i];
        if (bits)
            *bits = 0;
        if (energy)
            *energy = qenergy;
        if (out) {
            for (i = 0; i < size; i += dim)
                for (j = 0; j < dim; j++)
                    out[i+j] = 0.0f;
        }
        return cost * lambda;
    }
    if (!scaled) {
        abs_pow34_v(s->scoefs, in, size);
        scaled = s->scoefs;
    }
    quantize_bands(s->qcoefs, in, scaled, size, Q34, !BT_UNSIGNED, aac_cb_maxval[cb], ROUNDING);
    if (BT_UNSIGNED) {
        off = 0;
    } else {
        off = aac_cb_maxval[cb];
    }
    for (i = 0; i < size; i += dim) {
        const float *vec;
        int *quants = s->qcoefs + i;
        int curidx = 0;
        int curbits;
        float quantized, rd = 0.0f;
        for (j = 0; j < dim; j++) {
            curidx *= aac_cb_range[cb];
            curidx += quants[j] + off;
        }
        curbits =  ff_aac_spectral_bits[cb-1][curidx];
        vec     = &ff_aac_codebook_vectors[cb-1][curidx*dim];
        if (BT_UNSIGNED) {
            for (j = 0; j < dim; j++) {
                float t = fabsf(in[i+j]);
                float di;
                if (BT_ESC && vec[j] == 64.0f) { //FIXME: slow
                    if (t >= CLIPPED_ESCAPE) {
                        quantized = CLIPPED_ESCAPE;
                        curbits += 21;
                    } else {
                        int c = av_clip_uintp2(quant(t, Q, ROUNDING), 13);
                        quantized = c*cbrtf(c)*IQ;
                        curbits += av_log2(c)*2 - 4 + 1;
                    }
                } else {
                    quantized = vec[j]*IQ;
                }
                di = t - quantized;
                if (out)
                    out[i+j] = in[i+j] >= 0 ? quantized : -quantized;
                if (vec[j] != 0.0f)
                    curbits++;
                qenergy += quantized*quantized;
                rd += di*di;
            }
        } else {
            for (j = 0; j < dim; j++) {
                quantized = vec[j]*IQ;
                qenergy += quantized*quantized;
                if (out)
                    out[i+j] = quantized;
                rd += (in[i+j] - quantized)*(in[i+j] - quantized);
            }
        }
        cost    += rd * lambda + curbits;
        resbits += curbits;
        if (cost >= uplim)
            return uplim;
        if (pb) {
            put_bits(pb, ff_aac_spectral_bits[cb-1][curidx], ff_aac_spectral_codes[cb-1][curidx]);
            if (BT_UNSIGNED)
                for (j = 0; j < dim; j++)
                    if (ff_aac_codebook_vectors[cb-1][curidx*dim+j] != 0.0f)
                        put_bits(pb, 1, in[i+j] < 0.0f);
            if (BT_ESC) {
                for (j = 0; j < 2; j++) {
                    if (ff_aac_codebook_vectors[cb-1][curidx*2+j] == 64.0f) {
                        int coef = av_clip_uintp2(quant(fabsf(in[i+j]), Q, ROUNDING), 13);
                        int len = av_log2(coef);

                        put_bits(pb, len - 4 + 1, (1 << (len - 4 + 1)) - 2);
                        put_sbits(pb, len, coef);
                    }
                }
            }
        }
    }

    if (bits)
        *bits = resbits;
    if (energy)
        *energy = qenergy;
    return cost;
}

static inline float quantize_and_encode_band_cost_NONE(struct AACEncContext *s, PutBitContext *pb,
                                                const float *in, float *quant, const float *scaled,
                                                int size, int scale_idx, int cb,
                                                const float lambda, const float uplim,
                                                int *bits, float *energy) {
    av_assert0(0);
    return 0.0f;
}

#define QUANTIZE_AND_ENCODE_BAND_COST_FUNC(NAME, BT_ZERO, BT_UNSIGNED, BT_PAIR, BT_ESC, BT_NOISE, BT_STEREO, ROUNDING) \
static float quantize_and_encode_band_cost_ ## NAME(                                         \
                                struct AACEncContext *s,                                     \
                                PutBitContext *pb, const float *in, float *quant,            \
                                const float *scaled, int size, int scale_idx,                \
                                int cb, const float lambda, const float uplim,               \
                                int *bits, float *energy) {                                  \
    return quantize_and_encode_band_cost_template(                                           \
                                s, pb, in, quant, scaled, size, scale_idx,                   \
                                BT_ESC ? ESC_BT : cb, lambda, uplim, bits, energy,           \
                                BT_ZERO, BT_UNSIGNED, BT_PAIR, BT_ESC, BT_NOISE, BT_STEREO,  \
                                ROUNDING);                                                   \
}

QUANTIZE_AND_ENCODE_BAND_COST_FUNC(ZERO,  1, 0, 0, 0, 0, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(SQUAD, 0, 0, 0, 0, 0, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(UQUAD, 0, 1, 0, 0, 0, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(SPAIR, 0, 0, 1, 0, 0, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(UPAIR, 0, 1, 1, 0, 0, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(ESC,   0, 1, 1, 1, 0, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(ESC_RTZ, 0, 1, 1, 1, 0, 0, ROUND_TO_ZERO)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(NOISE, 0, 0, 0, 0, 1, 0, ROUND_STANDARD)
QUANTIZE_AND_ENCODE_BAND_COST_FUNC(STEREO,0, 0, 0, 0, 0, 1, ROUND_STANDARD)

static float (*const quantize_and_encode_band_cost_arr[])(
                                struct AACEncContext *s,
                                PutBitContext *pb, const float *in, float *quant,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits, float *energy) = {
    quantize_and_encode_band_cost_ZERO,
    quantize_and_encode_band_cost_SQUAD,
    quantize_and_encode_band_cost_SQUAD,
    quantize_and_encode_band_cost_UQUAD,
    quantize_and_encode_band_cost_UQUAD,
    quantize_and_encode_band_cost_SPAIR,
    quantize_and_encode_band_cost_SPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_ESC,
    quantize_and_encode_band_cost_NONE,     /* CB 12 doesn't exist */
    quantize_and_encode_band_cost_NOISE,
    quantize_and_encode_band_cost_STEREO,
    quantize_and_encode_band_cost_STEREO,
};

static float (*const quantize_and_encode_band_cost_rtz_arr[])(
                                struct AACEncContext *s,
                                PutBitContext *pb, const float *in, float *quant,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits, float *energy) = {
    quantize_and_encode_band_cost_ZERO,
    quantize_and_encode_band_cost_SQUAD,
    quantize_and_encode_band_cost_SQUAD,
    quantize_and_encode_band_cost_UQUAD,
    quantize_and_encode_band_cost_UQUAD,
    quantize_and_encode_band_cost_SPAIR,
    quantize_and_encode_band_cost_SPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_UPAIR,
    quantize_and_encode_band_cost_ESC_RTZ,
    quantize_and_encode_band_cost_NONE,     /* CB 12 doesn't exist */
    quantize_and_encode_band_cost_NOISE,
    quantize_and_encode_band_cost_STEREO,
    quantize_and_encode_band_cost_STEREO,
};

#define quantize_and_encode_band_cost(                                  \
                                s, pb, in, quant, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits, energy, rtz)               \
    ((rtz) ? quantize_and_encode_band_cost_rtz_arr : quantize_and_encode_band_cost_arr)[cb]( \
                                s, pb, in, quant, scaled, size, scale_idx, cb, \
                                lambda, uplim, bits, energy)

static inline float quantize_band_cost(struct AACEncContext *s, const float *in,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits, float *energy, int rtz)
{
    return quantize_and_encode_band_cost(s, NULL, in, NULL, scaled, size, scale_idx,
                                         cb, lambda, uplim, bits, energy, rtz);
}

static inline int quantize_band_cost_bits(struct AACEncContext *s, const float *in,
                                const float *scaled, int size, int scale_idx,
                                int cb, const float lambda, const float uplim,
                                int *bits, float *energy, int rtz)
{
    int auxbits;
    quantize_and_encode_band_cost(s, NULL, in, NULL, scaled, size, scale_idx,
                                         cb, 0.0f, uplim, &auxbits, energy, rtz);
    if (bits) {
        *bits = auxbits;
    }
    return auxbits;
}

static inline void quantize_and_encode_band(struct AACEncContext *s, PutBitContext *pb,
                                            const float *in, float *out, int size, int scale_idx,
                                            int cb, const float lambda, int rtz)
{
    quantize_and_encode_band_cost(s, pb, in, out, NULL, size, scale_idx, cb, lambda,
                                  INFINITY, NULL, NULL, rtz);
}

#include "aacenc_quantization_misc.h"

#endif /* AVCODEC_AACENC_QUANTIZATION_H */
