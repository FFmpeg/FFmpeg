/*
 * AAC encoder psychoacoustic model
 * Copyright (C) 2008 Konstantin Shishkov
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
 * @file libavcodec/aacpsy.c
 * AAC encoder psychoacoustic model
 */

#include "avcodec.h"
#include "aacpsy.h"
#include "aactab.h"

/***********************************
 *              TODOs:
 * General:
 * better audio preprocessing (add DC highpass filter?)
 * more psy models
 * maybe improve coefficient quantization function in some way
 *
 * 3GPP-based psy model:
 * thresholds linearization after their modifications for attaining given bitrate
 * try other bitrate controlling mechanism (maybe use ratecontrol.c?)
 * control quality for quality-based output
 **********************************/

/**
 * Quantize one coefficient.
 * @return absolute value of the quantized coefficient
 * @see 3GPP TS26.403 5.6.2 "Scalefactor determination"
 */
static av_always_inline int quant(float coef, const float Q)
{
    return av_clip((int)(pow(fabsf(coef) * Q, 0.75) + 0.4054), 0, 8191);
}

static inline float get_approximate_quant_error(float *c, int size, int scale_idx)
{
    int i;
    int q;
    float coef, unquant, sum = 0.0f;
    const float Q  = ff_aac_pow2sf_tab[200 - scale_idx + SCALE_ONE_POS - SCALE_DIV_512];
    const float IQ = ff_aac_pow2sf_tab[200 + scale_idx - SCALE_ONE_POS + SCALE_DIV_512];
    for(i = 0; i < size; i++){
        coef = fabs(c[i]);
        q = quant(c[i], Q);
        unquant = (q * cbrt(q)) * IQ;
        sum += (coef - unquant) * (coef - unquant);
    }
    return sum;
}

/**
 * constants for 3GPP AAC psychoacoustic model
 * @{
 */
#define PSY_3GPP_SPREAD_LOW  1.5f // spreading factor for ascending threshold spreading  (15 dB/Bark)
#define PSY_3GPP_SPREAD_HI   3.0f // spreading factor for descending threshold spreading (30 dB/Bark)
/**
 * @}
 */

/**
 * information for single band used by 3GPP TS26.403-inspired psychoacoustic model
 */
typedef struct Psy3gppBand{
    float energy;    ///< band energy
    float ffac;      ///< form factor
}Psy3gppBand;

/**
 * psychoacoustic model frame type-dependent coefficients
 */
typedef struct Psy3gppCoeffs{
    float ath       [64]; ///< absolute threshold of hearing per bands
    float barks     [64]; ///< Bark value for each spectral band in long frame
    float spread_low[64]; ///< spreading factor for low-to-high threshold spreading in long frame
    float spread_hi [64]; ///< spreading factor for high-to-low threshold spreading in long frame
}Psy3gppCoeffs;

/**
 * Calculate Bark value for given line.
 */
static inline float calc_bark(float f)
{
    return 13.3f * atanf(0.00076f * f) + 3.5f * atanf((f / 7500.0f) * (f / 7500.0f));
}
