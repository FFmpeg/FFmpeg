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
#include "aactab.h"
#include "psymodel.h"

/***********************************
 *              TODOs:
 * thresholds linearization after their modifications for attaining given bitrate
 * try other bitrate controlling mechanism (maybe use ratecontrol.c?)
 * control quality for quality-based output
 **********************************/

/**
 * constants for 3GPP AAC psychoacoustic model
 * @{
 */
#define PSY_3GPP_SPREAD_LOW  1.5f // spreading factor for ascending threshold spreading  (15 dB/Bark)
#define PSY_3GPP_SPREAD_HI   3.0f // spreading factor for descending threshold spreading (30 dB/Bark)

#define PSY_3GPP_RPEMIN      0.01f
#define PSY_3GPP_RPELEV      2.0f
/**
 * @}
 */

/**
 * information for single band used by 3GPP TS26.403-inspired psychoacoustic model
 */
typedef struct Psy3gppBand{
    float energy;    ///< band energy
    float ffac;      ///< form factor
    float thr;       ///< energy threshold
    float min_snr;   ///< minimal SNR
    float thr_quiet; ///< threshold in quiet
}Psy3gppBand;

/**
 * single/pair channel context for psychoacoustic model
 */
typedef struct Psy3gppChannel{
    Psy3gppBand band[128];               ///< bands information
    Psy3gppBand prev_band[128];          ///< bands information from the previous frame

    float       win_energy;              ///< sliding average of channel energy
    float       iir_state[2];            ///< hi-pass IIR filter state
    uint8_t     next_grouping;           ///< stored grouping scheme for the next frame (in case of 8 short window sequence)
    enum WindowSequence next_window_seq; ///< window sequence to be used in the next frame
}Psy3gppChannel;

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
 * 3GPP TS26.403-inspired psychoacoustic model specific data
 */
typedef struct Psy3gppContext{
    Psy3gppCoeffs psy_coef[2];
    Psy3gppChannel *ch;
}Psy3gppContext;

/**
 * Calculate Bark value for given line.
 */
static av_cold float calc_bark(float f)
{
    return 13.3f * atanf(0.00076f * f) + 3.5f * atanf((f / 7500.0f) * (f / 7500.0f));
}

#define ATH_ADD 4
/**
 * Calculate ATH value for given frequency.
 * Borrowed from Lame.
 */
static av_cold float ath(float f, float add)
{
    f /= 1000.0f;
    return    3.64 * pow(f, -0.8)
            - 6.8  * exp(-0.6  * (f - 3.4) * (f - 3.4))
            + 6.0  * exp(-0.15 * (f - 8.7) * (f - 8.7))
            + (0.6 + 0.04 * add) * 0.001 * f * f * f * f;
}

static av_cold int psy_3gpp_init(FFPsyContext *ctx) {
    Psy3gppContext *pctx;
    float barks[1024];
    int i, j, g, start;
    float prev, minscale, minath;

    ctx->model_priv_data = av_mallocz(sizeof(Psy3gppContext));
    pctx = (Psy3gppContext*) ctx->model_priv_data;

    for (i = 0; i < 1024; i++)
        barks[i] = calc_bark(i * ctx->avctx->sample_rate / 2048.0);
    minath = ath(3410, ATH_ADD);
    for (j = 0; j < 2; j++) {
        Psy3gppCoeffs *coeffs = &pctx->psy_coef[j];
        i = 0;
        prev = 0.0;
        for (g = 0; g < ctx->num_bands[j]; g++) {
            i += ctx->bands[j][g];
            coeffs->barks[g] = (barks[i - 1] + prev) / 2.0;
            prev = barks[i - 1];
        }
        for (g = 0; g < ctx->num_bands[j] - 1; g++) {
            coeffs->spread_low[g] = pow(10.0, -(coeffs->barks[g+1] - coeffs->barks[g]) * PSY_3GPP_SPREAD_LOW);
            coeffs->spread_hi [g] = pow(10.0, -(coeffs->barks[g+1] - coeffs->barks[g]) * PSY_3GPP_SPREAD_HI);
        }
        start = 0;
        for (g = 0; g < ctx->num_bands[j]; g++) {
            minscale = ath(ctx->avctx->sample_rate * start / 1024.0, ATH_ADD);
            for (i = 1; i < ctx->bands[j][g]; i++)
                minscale = FFMIN(minscale, ath(ctx->avctx->sample_rate * (start + i) / 1024.0 / 2.0, ATH_ADD));
            coeffs->ath[g] = minscale - minath;
            start += ctx->bands[j][g];
        }
    }

    pctx->ch = av_mallocz(sizeof(Psy3gppChannel) * ctx->avctx->channels);
    return 0;
}

/**
 * IIR filter used in block switching decision
 */
static float iir_filter(int in, float state[2])
{
    float ret;

    ret = 0.7548f * (in - state[0]) + 0.5095f * state[1];
    state[0] = in;
    state[1] = ret;
    return ret;
}

/**
 * window grouping information stored as bits (0 - new group, 1 - group continues)
 */
static const uint8_t window_grouping[9] = {
    0xB6, 0x6C, 0xD8, 0xB2, 0x66, 0xC6, 0x96, 0x36, 0x36
};

/**
 * Tell encoder which window types to use.
 * @see 3GPP TS26.403 5.4.1 "Blockswitching"
 */
static FFPsyWindowInfo psy_3gpp_window(FFPsyContext *ctx,
                                       const int16_t *audio, const int16_t *la,
                                       int channel, int prev_type)
{
    int i, j;
    int br               = ctx->avctx->bit_rate / ctx->avctx->channels;
    int attack_ratio     = br <= 16000 ? 18 : 10;
    Psy3gppContext *pctx = (Psy3gppContext*) ctx->model_priv_data;
    Psy3gppChannel *pch  = &pctx->ch[channel];
    uint8_t grouping     = 0;
    FFPsyWindowInfo wi;

    memset(&wi, 0, sizeof(wi));
    if (la) {
        float s[8], v;
        int switch_to_eight = 0;
        float sum = 0.0, sum2 = 0.0;
        int attack_n = 0;
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 128; j++) {
                v = iir_filter(audio[(i*128+j)*ctx->avctx->channels], pch->iir_state);
                sum += v*v;
            }
            s[i]  = sum;
            sum2 += sum;
        }
        for (i = 0; i < 8; i++) {
            if (s[i] > pch->win_energy * attack_ratio) {
                attack_n        = i + 1;
                switch_to_eight = 1;
                break;
            }
        }
        pch->win_energy = pch->win_energy*7/8 + sum2/64;

        wi.window_type[1] = prev_type;
        switch (prev_type) {
        case ONLY_LONG_SEQUENCE:
            wi.window_type[0] = switch_to_eight ? LONG_START_SEQUENCE : ONLY_LONG_SEQUENCE;
            break;
        case LONG_START_SEQUENCE:
            wi.window_type[0] = EIGHT_SHORT_SEQUENCE;
            grouping = pch->next_grouping;
            break;
        case LONG_STOP_SEQUENCE:
            wi.window_type[0] = ONLY_LONG_SEQUENCE;
            break;
        case EIGHT_SHORT_SEQUENCE:
            wi.window_type[0] = switch_to_eight ? EIGHT_SHORT_SEQUENCE : LONG_STOP_SEQUENCE;
            grouping = switch_to_eight ? pch->next_grouping : 0;
            break;
        }
        pch->next_grouping = window_grouping[attack_n];
    } else {
        for (i = 0; i < 3; i++)
            wi.window_type[i] = prev_type;
        grouping = (prev_type == EIGHT_SHORT_SEQUENCE) ? window_grouping[0] : 0;
    }

    wi.window_shape   = 1;
    if (wi.window_type[0] != EIGHT_SHORT_SEQUENCE) {
        wi.num_windows = 1;
        wi.grouping[0] = 1;
    } else {
        int lastgrp = 0;
        wi.num_windows = 8;
        for (i = 0; i < 8; i++) {
            if (!((grouping >> i) & 1))
                lastgrp = i;
            wi.grouping[lastgrp]++;
        }
    }

    return wi;
}

/**
 * Calculate band thresholds as suggested in 3GPP TS26.403
 */
static void psy_3gpp_analyze(FFPsyContext *ctx, int channel,
                             const float *coefs, FFPsyWindowInfo *wi)
{
    Psy3gppContext *pctx = (Psy3gppContext*) ctx->model_priv_data;
    Psy3gppChannel *pch  = &pctx->ch[channel];
    int start = 0;
    int i, w, g;
    const int num_bands       = ctx->num_bands[wi->num_windows == 8];
    const uint8_t* band_sizes = ctx->bands[wi->num_windows == 8];
    Psy3gppCoeffs *coeffs     = &pctx->psy_coef[wi->num_windows == 8];

    //calculate energies, initial thresholds and related values - 5.4.2 "Threshold Calculation"
    for (w = 0; w < wi->num_windows*16; w += 16) {
        for (g = 0; g < num_bands; g++) {
            Psy3gppBand *band = &pch->band[w+g];
            band->energy = 0.0f;
            for (i = 0; i < band_sizes[g]; i++)
                band->energy += coefs[start+i] * coefs[start+i];
            band->energy *= 1.0f / (512*512);
            band->thr     = band->energy * 0.001258925f;
            start        += band_sizes[g];

            ctx->psy_bands[channel*PSY_MAX_BANDS+w+g].energy = band->energy;
        }
    }
    //modify thresholds - spread, threshold in quiet - 5.4.3 "Spreaded Energy Calculation"
    for (w = 0; w < wi->num_windows*16; w += 16) {
        Psy3gppBand *band = &pch->band[w];
        for (g = 1; g < num_bands; g++)
            band[g].thr = FFMAX(band[g].thr, band[g-1].thr * coeffs->spread_low[g-1]);
        for (g = num_bands - 2; g >= 0; g--)
            band[g].thr = FFMAX(band[g].thr, band[g+1].thr * coeffs->spread_hi [g]);
        for (g = 0; g < num_bands; g++) {
            band[g].thr_quiet = FFMAX(band[g].thr, coeffs->ath[g]);
            if (wi->num_windows != 8 && wi->window_type[1] != EIGHT_SHORT_SEQUENCE)
                band[g].thr_quiet = FFMAX(PSY_3GPP_RPEMIN*band[g].thr_quiet,
                                          FFMIN(band[g].thr_quiet,
                                          PSY_3GPP_RPELEV*pch->prev_band[w+g].thr_quiet));
            band[g].thr = FFMAX(band[g].thr, band[g].thr_quiet * 0.25);

            ctx->psy_bands[channel*PSY_MAX_BANDS+w+g].threshold = band[g].thr;
        }
    }
    memcpy(pch->prev_band, pch->band, sizeof(pch->band));
}

static av_cold void psy_3gpp_end(FFPsyContext *apc)
{
    Psy3gppContext *pctx = (Psy3gppContext*) apc->model_priv_data;
    av_freep(&pctx->ch);
    av_freep(&apc->model_priv_data);
}


const FFPsyModel ff_aac_psy_model =
{
    .name    = "3GPP TS 26.403-inspired model",
    .init    = psy_3gpp_init,
    .window  = psy_3gpp_window,
    .analyze = psy_3gpp_analyze,
    .end     = psy_3gpp_end,
};
