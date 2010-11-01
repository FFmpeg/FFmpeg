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
 * @file
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
#define PSY_3GPP_SPREAD_HI   1.5f // spreading factor for ascending threshold spreading  (15 dB/Bark)
#define PSY_3GPP_SPREAD_LOW  3.0f // spreading factor for descending threshold spreading (30 dB/Bark)

#define PSY_3GPP_RPEMIN      0.01f
#define PSY_3GPP_RPELEV      2.0f

/* LAME psy model constants */
#define PSY_LAME_FIR_LEN 21         ///< LAME psy model FIR order
#define AAC_BLOCK_SIZE_LONG 1024    ///< long block size
#define AAC_BLOCK_SIZE_SHORT 128    ///< short block size
#define AAC_NUM_BLOCKS_SHORT 8      ///< number of blocks in a short sequence
#define PSY_LAME_NUM_SUBBLOCKS 3    ///< Number of sub-blocks in each short block

/**
 * @}
 */

/**
 * information for single band used by 3GPP TS26.403-inspired psychoacoustic model
 */
typedef struct AacPsyBand{
    float energy;    ///< band energy
    float ffac;      ///< form factor
    float thr;       ///< energy threshold
    float min_snr;   ///< minimal SNR
    float thr_quiet; ///< threshold in quiet
}AacPsyBand;

/**
 * single/pair channel context for psychoacoustic model
 */
typedef struct AacPsyChannel{
    AacPsyBand band[128];               ///< bands information
    AacPsyBand prev_band[128];          ///< bands information from the previous frame

    float       win_energy;              ///< sliding average of channel energy
    float       iir_state[2];            ///< hi-pass IIR filter state
    uint8_t     next_grouping;           ///< stored grouping scheme for the next frame (in case of 8 short window sequence)
    enum WindowSequence next_window_seq; ///< window sequence to be used in the next frame
    /* LAME psy model specific members */
    float attack_threshold;              ///< attack threshold for this channel
    float prev_energy_subshort[AAC_NUM_BLOCKS_SHORT * PSY_LAME_NUM_SUBBLOCKS];
    int   prev_attack;                   ///< attack value for the last short block in the previous sequence
}AacPsyChannel;

/**
 * psychoacoustic model frame type-dependent coefficients
 */
typedef struct AacPsyCoeffs{
    float ath       [64]; ///< absolute threshold of hearing per bands
    float barks     [64]; ///< Bark value for each spectral band in long frame
    float spread_low[64]; ///< spreading factor for low-to-high threshold spreading in long frame
    float spread_hi [64]; ///< spreading factor for high-to-low threshold spreading in long frame
}AacPsyCoeffs;

/**
 * 3GPP TS26.403-inspired psychoacoustic model specific data
 */
typedef struct AacPsyContext{
    AacPsyCoeffs psy_coef[2];
    AacPsyChannel *ch;
}AacPsyContext;

/**
 * LAME psy model preset struct
 */
typedef struct {
    int   quality;  ///< Quality to map the rest of the vaules to.
     /* This is overloaded to be both kbps per channel in ABR mode, and
      * requested quality in constant quality mode.
      */
    float st_lrm;   ///< short threshold for L, R, and M channels
} PsyLamePreset;

/**
 * LAME psy model preset table for ABR
 */
static const PsyLamePreset psy_abr_map[] = {
/* TODO: Tuning. These were taken from LAME. */
/* kbps/ch st_lrm   */
    {  8,  6.60},
    { 16,  6.60},
    { 24,  6.60},
    { 32,  6.60},
    { 40,  6.60},
    { 48,  6.60},
    { 56,  6.60},
    { 64,  6.40},
    { 80,  6.00},
    { 96,  5.60},
    {112,  5.20},
    {128,  5.20},
    {160,  5.20}
};

/**
* LAME psy model preset table for constant quality
*/
static const PsyLamePreset psy_vbr_map[] = {
/* vbr_q  st_lrm    */
    { 0,  4.20},
    { 1,  4.20},
    { 2,  4.20},
    { 3,  4.20},
    { 4,  4.20},
    { 5,  4.20},
    { 6,  4.20},
    { 7,  4.20},
    { 8,  4.20},
    { 9,  4.20},
    {10,  4.20}
};

/**
 * LAME psy model FIR coefficient table
 */
static const float psy_fir_coeffs[] = {
    -8.65163e-18 * 2, -0.00851586 * 2, -6.74764e-18 * 2, 0.0209036 * 2,
    -3.36639e-17 * 2, -0.0438162 * 2,  -1.54175e-17 * 2, 0.0931738 * 2,
    -5.52212e-17 * 2, -0.313819 * 2
};

/**
 * calculates the attack threshold for ABR from the above table for the LAME psy model
 */
static float lame_calc_attack_threshold(int bitrate)
{
    /* Assume max bitrate to start with */
    int lower_range = 12, upper_range = 12;
    int lower_range_kbps = psy_abr_map[12].quality;
    int upper_range_kbps = psy_abr_map[12].quality;
    int i;

    /* Determine which bitrates the value specified falls between.
     * If the loop ends without breaking our above assumption of 320kbps was correct.
     */
    for (i = 1; i < 13; i++) {
        if (FFMAX(bitrate, psy_abr_map[i].quality) != bitrate) {
            upper_range = i;
            upper_range_kbps = psy_abr_map[i    ].quality;
            lower_range = i - 1;
            lower_range_kbps = psy_abr_map[i - 1].quality;
            break; /* Upper range found */
        }
    }

    /* Determine which range the value specified is closer to */
    if ((upper_range_kbps - bitrate) > (bitrate - lower_range_kbps))
        return psy_abr_map[lower_range].st_lrm;
    return psy_abr_map[upper_range].st_lrm;
}

/**
 * LAME psy model specific initialization
 */
static void lame_window_init(AacPsyContext *ctx, AVCodecContext *avctx) {
    int i, j;

    for (i = 0; i < avctx->channels; i++) {
        AacPsyChannel *pch = &ctx->ch[i];

        if (avctx->flags & CODEC_FLAG_QSCALE)
            pch->attack_threshold = psy_vbr_map[avctx->global_quality / FF_QP2LAMBDA].st_lrm;
        else
            pch->attack_threshold = lame_calc_attack_threshold(avctx->bit_rate / avctx->channels / 1000);

        for (j = 0; j < AAC_NUM_BLOCKS_SHORT * PSY_LAME_NUM_SUBBLOCKS; j++)
            pch->prev_energy_subshort[j] = 10.0f;
    }
}

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
    AacPsyContext *pctx;
    float bark;
    int i, j, g, start;
    float prev, minscale, minath;

    ctx->model_priv_data = av_mallocz(sizeof(AacPsyContext));
    pctx = (AacPsyContext*) ctx->model_priv_data;

    minath = ath(3410, ATH_ADD);
    for (j = 0; j < 2; j++) {
        AacPsyCoeffs *coeffs = &pctx->psy_coef[j];
        float line_to_frequency = ctx->avctx->sample_rate / (j ? 256.f : 2048.0f);
        i = 0;
        prev = 0.0;
        for (g = 0; g < ctx->num_bands[j]; g++) {
            i += ctx->bands[j][g];
            bark = calc_bark((i-1) * line_to_frequency);
            coeffs->barks[g] = (bark + prev) / 2.0;
            prev = bark;
        }
        for (g = 0; g < ctx->num_bands[j] - 1; g++) {
            coeffs->spread_low[g] = pow(10.0, -(coeffs->barks[g+1] - coeffs->barks[g]) * PSY_3GPP_SPREAD_LOW);
            coeffs->spread_hi [g] = pow(10.0, -(coeffs->barks[g+1] - coeffs->barks[g]) * PSY_3GPP_SPREAD_HI);
        }
        start = 0;
        for (g = 0; g < ctx->num_bands[j]; g++) {
            minscale = ath(start * line_to_frequency, ATH_ADD);
            for (i = 1; i < ctx->bands[j][g]; i++)
                minscale = FFMIN(minscale, ath((start + i) * line_to_frequency, ATH_ADD));
            coeffs->ath[g] = minscale - minath;
            start += ctx->bands[j][g];
        }
    }

    pctx->ch = av_mallocz(sizeof(AacPsyChannel) * ctx->avctx->channels);

    lame_window_init(pctx, ctx->avctx);

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
    AacPsyContext *pctx = (AacPsyContext*) ctx->model_priv_data;
    AacPsyChannel *pch  = &pctx->ch[channel];
    uint8_t grouping     = 0;
    int next_type        = pch->next_window_seq;
    FFPsyWindowInfo wi;

    memset(&wi, 0, sizeof(wi));
    if (la) {
        float s[8], v;
        int switch_to_eight = 0;
        float sum = 0.0, sum2 = 0.0;
        int attack_n = 0;
        int stay_short = 0;
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 128; j++) {
                v = iir_filter(la[(i*128+j)*ctx->avctx->channels], pch->iir_state);
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
            next_type = switch_to_eight ? EIGHT_SHORT_SEQUENCE : ONLY_LONG_SEQUENCE;
            break;
        case LONG_START_SEQUENCE:
            wi.window_type[0] = EIGHT_SHORT_SEQUENCE;
            grouping = pch->next_grouping;
            next_type = switch_to_eight ? EIGHT_SHORT_SEQUENCE : LONG_STOP_SEQUENCE;
            break;
        case LONG_STOP_SEQUENCE:
            wi.window_type[0] = switch_to_eight ? LONG_START_SEQUENCE : ONLY_LONG_SEQUENCE;
            next_type = switch_to_eight ? EIGHT_SHORT_SEQUENCE : ONLY_LONG_SEQUENCE;
            break;
        case EIGHT_SHORT_SEQUENCE:
            stay_short = next_type == EIGHT_SHORT_SEQUENCE || switch_to_eight;
            wi.window_type[0] = stay_short ? EIGHT_SHORT_SEQUENCE : LONG_STOP_SEQUENCE;
            grouping = next_type == EIGHT_SHORT_SEQUENCE ? pch->next_grouping : 0;
            next_type = switch_to_eight ? EIGHT_SHORT_SEQUENCE : LONG_STOP_SEQUENCE;
            break;
        }

        pch->next_grouping = window_grouping[attack_n];
        pch->next_window_seq = next_type;
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
                             const float *coefs, const FFPsyWindowInfo *wi)
{
    AacPsyContext *pctx = (AacPsyContext*) ctx->model_priv_data;
    AacPsyChannel *pch  = &pctx->ch[channel];
    int start = 0;
    int i, w, g;
    const int num_bands       = ctx->num_bands[wi->num_windows == 8];
    const uint8_t* band_sizes = ctx->bands[wi->num_windows == 8];
    AacPsyCoeffs *coeffs     = &pctx->psy_coef[wi->num_windows == 8];

    //calculate energies, initial thresholds and related values - 5.4.2 "Threshold Calculation"
    for (w = 0; w < wi->num_windows*16; w += 16) {
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band = &pch->band[w+g];
            band->energy = 0.0f;
            for (i = 0; i < band_sizes[g]; i++)
                band->energy += coefs[start+i] * coefs[start+i];
            band->thr     = band->energy * 0.001258925f;
            start        += band_sizes[g];

            ctx->psy_bands[channel*PSY_MAX_BANDS+w+g].energy = band->energy;
        }
    }
    //modify thresholds - spread, threshold in quiet - 5.4.3 "Spreaded Energy Calculation"
    for (w = 0; w < wi->num_windows*16; w += 16) {
        AacPsyBand *band = &pch->band[w];
        for (g = 1; g < num_bands; g++)
            band[g].thr = FFMAX(band[g].thr, band[g-1].thr * coeffs->spread_hi [g]);
        for (g = num_bands - 2; g >= 0; g--)
            band[g].thr = FFMAX(band[g].thr, band[g+1].thr * coeffs->spread_low[g]);
        for (g = 0; g < num_bands; g++) {
            band[g].thr_quiet = band[g].thr = FFMAX(band[g].thr, coeffs->ath[g]);
            if (!(wi->window_type[0] == LONG_STOP_SEQUENCE || (wi->window_type[1] == LONG_START_SEQUENCE && !w)))
                band[g].thr = FFMAX(PSY_3GPP_RPEMIN*band[g].thr, FFMIN(band[g].thr,
                                    PSY_3GPP_RPELEV*pch->prev_band[w+g].thr_quiet));

            ctx->psy_bands[channel*PSY_MAX_BANDS+w+g].threshold = band[g].thr;
        }
    }
    memcpy(pch->prev_band, pch->band, sizeof(pch->band));
}

static av_cold void psy_3gpp_end(FFPsyContext *apc)
{
    AacPsyContext *pctx = (AacPsyContext*) apc->model_priv_data;
    av_freep(&pctx->ch);
    av_freep(&apc->model_priv_data);
}

static void lame_apply_block_type(AacPsyChannel *ctx, FFPsyWindowInfo *wi, int uselongblock)
{
    int blocktype = ONLY_LONG_SEQUENCE;
    if (uselongblock) {
        if (ctx->next_window_seq == EIGHT_SHORT_SEQUENCE)
            blocktype = LONG_STOP_SEQUENCE;
    } else {
        blocktype = EIGHT_SHORT_SEQUENCE;
        if (ctx->next_window_seq == ONLY_LONG_SEQUENCE)
            ctx->next_window_seq = LONG_START_SEQUENCE;
        if (ctx->next_window_seq == LONG_STOP_SEQUENCE)
            ctx->next_window_seq = EIGHT_SHORT_SEQUENCE;
    }

    wi->window_type[0] = ctx->next_window_seq;
    ctx->next_window_seq = blocktype;
}

static FFPsyWindowInfo psy_lame_window(FFPsyContext *ctx,
                                       const int16_t *audio, const int16_t *la,
                                       int channel, int prev_type)
{
    AacPsyContext *pctx = (AacPsyContext*) ctx->model_priv_data;
    AacPsyChannel *pch  = &pctx->ch[channel];
    int grouping     = 0;
    int uselongblock = 1;
    int attacks[AAC_NUM_BLOCKS_SHORT + 1] = { 0 };
    int i;
    FFPsyWindowInfo wi;

    memset(&wi, 0, sizeof(wi));
    if (la) {
        float hpfsmpl[AAC_BLOCK_SIZE_LONG];
        float const *pf = hpfsmpl;
        float attack_intensity[(AAC_NUM_BLOCKS_SHORT + 1) * PSY_LAME_NUM_SUBBLOCKS];
        float energy_subshort[(AAC_NUM_BLOCKS_SHORT + 1) * PSY_LAME_NUM_SUBBLOCKS];
        float energy_short[AAC_NUM_BLOCKS_SHORT + 1] = { 0 };
        int chans = ctx->avctx->channels;
        const int16_t *firbuf = la + (AAC_BLOCK_SIZE_SHORT/4 - PSY_LAME_FIR_LEN) * chans;
        int j, att_sum = 0;

        /* LAME comment: apply high pass filter of fs/4 */
        for (i = 0; i < AAC_BLOCK_SIZE_LONG; i++) {
            float sum1, sum2;
            sum1 = firbuf[(i + ((PSY_LAME_FIR_LEN - 1) / 2)) * chans];
            sum2 = 0.0;
            for (j = 0; j < ((PSY_LAME_FIR_LEN - 1) / 2) - 1; j += 2) {
                sum1 += psy_fir_coeffs[j] * (firbuf[(i + j) * chans] + firbuf[(i + PSY_LAME_FIR_LEN - j) * chans]);
                sum2 += psy_fir_coeffs[j + 1] * (firbuf[(i + j + 1) * chans] + firbuf[(i + PSY_LAME_FIR_LEN - j - 1) * chans]);
            }
            hpfsmpl[i] = sum1 + sum2;
        }

        /* Calculate the energies of each sub-shortblock */
        for (i = 0; i < PSY_LAME_NUM_SUBBLOCKS; i++) {
            energy_subshort[i] = pch->prev_energy_subshort[i + ((AAC_NUM_BLOCKS_SHORT - 1) * PSY_LAME_NUM_SUBBLOCKS)];
            assert(pch->prev_energy_subshort[i + ((AAC_NUM_BLOCKS_SHORT - 2) * PSY_LAME_NUM_SUBBLOCKS + 1)] > 0);
            attack_intensity[i] = energy_subshort[i] / pch->prev_energy_subshort[i + ((AAC_NUM_BLOCKS_SHORT - 2) * PSY_LAME_NUM_SUBBLOCKS + 1)];
            energy_short[0] += energy_subshort[i];
        }

        for (i = 0; i < AAC_NUM_BLOCKS_SHORT * PSY_LAME_NUM_SUBBLOCKS; i++) {
            float const *const pfe = pf + AAC_BLOCK_SIZE_LONG / (AAC_NUM_BLOCKS_SHORT * PSY_LAME_NUM_SUBBLOCKS);
            float p = 1.0f;
            for (; pf < pfe; pf++)
                if (p < fabsf(*pf))
                    p = fabsf(*pf);
            pch->prev_energy_subshort[i] = energy_subshort[i + PSY_LAME_NUM_SUBBLOCKS] = p;
            energy_short[1 + i / PSY_LAME_NUM_SUBBLOCKS] += p;
            /* FIXME: The indexes below are [i + 3 - 2] in the LAME source.
             *          Obviously the 3 and 2 have some significance, or this would be just [i + 1]
             *          (which is what we use here). What the 3 stands for is ambigious, as it is both
             *          number of short blocks, and the number of sub-short blocks.
             *          It seems that LAME is comparing each sub-block to sub-block + 1 in the
             *          previous block.
             */
            if (p > energy_subshort[i + 1])
                p = p / energy_subshort[i + 1];
            else if (energy_subshort[i + 1] > p * 10.0f)
                p = energy_subshort[i + 1] / (p * 10.0f);
            else
                p = 0.0;
            attack_intensity[i + PSY_LAME_NUM_SUBBLOCKS] = p;
        }

        /* compare energy between sub-short blocks */
        for (i = 0; i < (AAC_NUM_BLOCKS_SHORT + 1) * PSY_LAME_NUM_SUBBLOCKS; i++)
            if (!attacks[i / PSY_LAME_NUM_SUBBLOCKS])
                if (attack_intensity[i] > pch->attack_threshold)
                    attacks[i / PSY_LAME_NUM_SUBBLOCKS] = (i % PSY_LAME_NUM_SUBBLOCKS) + 1;

        /* should have energy change between short blocks, in order to avoid periodic signals */
        /* Good samples to show the effect are Trumpet test songs */
        /* GB: tuned (1) to avoid too many short blocks for test sample TRUMPET */
        /* RH: tuned (2) to let enough short blocks through for test sample FSOL and SNAPS */
        for (i = 1; i < AAC_NUM_BLOCKS_SHORT + 1; i++) {
            float const u = energy_short[i - 1];
            float const v = energy_short[i];
            float const m = FFMAX(u, v);
            if (m < 40000) {                          /* (2) */
                if (u < 1.7f * v && v < 1.7f * u) {   /* (1) */
                    if (i == 1 && attacks[0] < attacks[i])
                        attacks[0] = 0;
                    attacks[i] = 0;
                }
            }
            att_sum += attacks[i];
        }

        if (attacks[0] <= pch->prev_attack)
            attacks[0] = 0;

        att_sum += attacks[0];
        /* 3 below indicates the previous attack happened in the last sub-block of the previous sequence */
        if (pch->prev_attack == 3 || att_sum) {
            uselongblock = 0;

            if (attacks[1] && attacks[0])
                attacks[1] = 0;
            if (attacks[2] && attacks[1])
                attacks[2] = 0;
            if (attacks[3] && attacks[2])
                attacks[3] = 0;
            if (attacks[4] && attacks[3])
                attacks[4] = 0;
            if (attacks[5] && attacks[4])
                attacks[5] = 0;
            if (attacks[6] && attacks[5])
                attacks[6] = 0;
            if (attacks[7] && attacks[6])
                attacks[7] = 0;
            if (attacks[8] && attacks[7])
                attacks[8] = 0;
        }
    } else {
        /* We have no lookahead info, so just use same type as the previous sequence. */
        uselongblock = !(prev_type == EIGHT_SHORT_SEQUENCE);
    }

    lame_apply_block_type(pch, &wi, uselongblock);

    wi.window_type[1] = prev_type;
    if (wi.window_type[0] != EIGHT_SHORT_SEQUENCE) {
        wi.num_windows  = 1;
        wi.grouping[0]  = 1;
        if (wi.window_type[0] == LONG_START_SEQUENCE)
            wi.window_shape = 0;
        else
            wi.window_shape = 1;
    } else {
        int lastgrp = 0;

        wi.num_windows = 8;
        wi.window_shape = 0;
        for (i = 0; i < 8; i++) {
            if (!((pch->next_grouping >> i) & 1))
                lastgrp = i;
            wi.grouping[lastgrp]++;
        }
    }

    /* Determine grouping, based on the location of the first attack, and save for
     * the next frame.
     * FIXME: Move this to analysis.
     * TODO: Tune groupings depending on attack location
     * TODO: Handle more than one attack in a group
     */
    for (i = 0; i < 9; i++) {
        if (attacks[i]) {
            grouping = i;
            break;
        }
    }
    pch->next_grouping = window_grouping[grouping];

    pch->prev_attack = attacks[8];

    return wi;
}

const FFPsyModel ff_aac_psy_model =
{
    .name    = "3GPP TS 26.403-inspired model",
    .init    = psy_3gpp_init,
    .window  = psy_lame_window,
    .analyze = psy_3gpp_analyze,
    .end     = psy_3gpp_end,
};
