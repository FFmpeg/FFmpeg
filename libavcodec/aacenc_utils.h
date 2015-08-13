/*
 * AAC encoder utilities
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
 * AAC encoder utilities
 * @author Rostislav Pehlivanov ( atomnuker gmail com )
 */

#ifndef AVCODEC_AACENC_UTILS_H
#define AVCODEC_AACENC_UTILS_H

#include "aac.h"
#include "aac_tablegen_decl.h"
#include "aacenctab.h"

#define ROUND_STANDARD 0.4054f
#define ROUND_TO_ZERO 0.1054f
#define C_QUANT 0.4054f

static inline void abs_pow34_v(float *out, const float *in, const int size)
{
    int i;
    for (i = 0; i < size; i++) {
        float a = fabsf(in[i]);
        out[i] = sqrtf(a * sqrtf(a));
    }
}

/**
 * Quantize one coefficient.
 * @return absolute value of the quantized coefficient
 * @see 3GPP TS26.403 5.6.2 "Scalefactor determination"
 */
static inline int quant(float coef, const float Q, const float rounding)
{
    float a = coef * Q;
    return sqrtf(a * sqrtf(a)) + rounding;
}

static inline void quantize_bands(int *out, const float *in, const float *scaled,
                                  int size, float Q34, int is_signed, int maxval,
                                  const float rounding)
{
    int i;
    double qc;
    for (i = 0; i < size; i++) {
        qc = scaled[i] * Q34;
        out[i] = (int)FFMIN(qc + rounding, (double)maxval);
        if (is_signed && in[i] < 0.0f) {
            out[i] = -out[i];
        }
    }
}

static inline float find_max_val(int group_len, int swb_size, const float *scaled)
{
    float maxval = 0.0f;
    int w2, i;
    for (w2 = 0; w2 < group_len; w2++) {
        for (i = 0; i < swb_size; i++) {
            maxval = FFMAX(maxval, scaled[w2*128+i]);
        }
    }
    return maxval;
}

static inline int find_min_book(float maxval, int sf)
{
    float Q = ff_aac_pow2sf_tab[POW_SF2_ZERO - sf + SCALE_ONE_POS - SCALE_DIV_512];
    float Q34 = sqrtf(Q * sqrtf(Q));
    int qmaxval, cb;
    qmaxval = maxval * Q34 + C_QUANT;
    if      (qmaxval ==  0) cb = 0;
    else if (qmaxval ==  1) cb = 1;
    else if (qmaxval ==  2) cb = 3;
    else if (qmaxval <=  4) cb = 5;
    else if (qmaxval <=  7) cb = 7;
    else if (qmaxval <= 12) cb = 9;
    else                    cb = 11;
    return cb;
}

/** Return the minimum scalefactor where the quantized coef does not clip. */
static inline uint8_t coef2minsf(float coef)
{
    return av_clip_uint8(log2f(coef)*4 - 69 + SCALE_ONE_POS - SCALE_DIV_512);
}

/** Return the maximum scalefactor where the quantized coef is not zero. */
static inline uint8_t coef2maxsf(float coef)
{
    return av_clip_uint8(log2f(coef)*4 +  6 + SCALE_ONE_POS - SCALE_DIV_512);
}

/*
 * Returns the closest possible index to an array of float values, given a value.
 */
static inline int quant_array_idx(const float val, const float *arr, const int num)
{
    int i, index = 0;
    float quant_min_err = INFINITY;
    for (i = 0; i < num; i++) {
        float error = (val - arr[i])*(val - arr[i]);
        if (error < quant_min_err) {
            quant_min_err = error;
            index = i;
        }
    }
    return index;
}

#define ERROR_IF(cond, ...) \
    if (cond) { \
        av_log(avctx, AV_LOG_ERROR, __VA_ARGS__); \
        return AVERROR(EINVAL); \
    }

#define WARN_IF(cond, ...) \
    if (cond) { \
        av_log(avctx, AV_LOG_WARNING, __VA_ARGS__); \
    }


#endif /* AVCODEC_AACENC_UTILS_H */
