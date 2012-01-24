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
 * try other bitrate controlling mechanism (maybe use ratecontrol.c?)
 * control quality for quality-based output
 **********************************/

/**
 * constants for 3GPP AAC psychoacoustic model
 * @{
 */
#define PSY_3GPP_THR_SPREAD_HI   1.5f // spreading factor for low-to-hi threshold spreading  (15 dB/Bark)
#define PSY_3GPP_THR_SPREAD_LOW  3.0f // spreading factor for hi-to-low threshold spreading  (30 dB/Bark)
/* spreading factor for low-to-hi energy spreading, long block, > 22kbps/channel (20dB/Bark) */
#define PSY_3GPP_EN_SPREAD_HI_L1 2.0f
/* spreading factor for low-to-hi energy spreading, long block, <= 22kbps/channel (15dB/Bark) */
#define PSY_3GPP_EN_SPREAD_HI_L2 1.5f
/* spreading factor for low-to-hi energy spreading, short block (15 dB/Bark) */
#define PSY_3GPP_EN_SPREAD_HI_S  1.5f
/* spreading factor for hi-to-low energy spreading, long block (30dB/Bark) */
#define PSY_3GPP_EN_SPREAD_LOW_L 3.0f
/* spreading factor for hi-to-low energy spreading, short block (20dB/Bark) */
#define PSY_3GPP_EN_SPREAD_LOW_S 2.0f

#define PSY_3GPP_RPEMIN      0.01f
#define PSY_3GPP_RPELEV      2.0f

#define PSY_3GPP_C1          3.0f           /* log2(8) */
#define PSY_3GPP_C2          1.3219281f     /* log2(2.5) */
#define PSY_3GPP_C3          0.55935729f    /* 1 - C2 / C1 */

#define PSY_SNR_1DB          7.9432821e-1f  /* -1dB */
#define PSY_SNR_25DB         3.1622776e-3f  /* -25dB */

#define PSY_3GPP_SAVE_SLOPE_L  -0.46666667f
#define PSY_3GPP_SAVE_SLOPE_S  -0.36363637f
#define PSY_3GPP_SAVE_ADD_L    -0.84285712f
#define PSY_3GPP_SAVE_ADD_S    -0.75f
#define PSY_3GPP_SPEND_SLOPE_L  0.66666669f
#define PSY_3GPP_SPEND_SLOPE_S  0.81818181f
#define PSY_3GPP_SPEND_ADD_L   -0.35f
#define PSY_3GPP_SPEND_ADD_S   -0.26111111f
#define PSY_3GPP_CLIP_LO_L      0.2f
#define PSY_3GPP_CLIP_LO_S      0.2f
#define PSY_3GPP_CLIP_HI_L      0.95f
#define PSY_3GPP_CLIP_HI_S      0.75f

#define PSY_3GPP_AH_THR_LONG    0.5f
#define PSY_3GPP_AH_THR_SHORT   0.63f

enum {
    PSY_3GPP_AH_NONE,
    PSY_3GPP_AH_INACTIVE,
    PSY_3GPP_AH_ACTIVE
};

#define PSY_3GPP_BITS_TO_PE(bits) ((bits) * 1.18f)

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
    float energy;       ///< band energy
    float thr;          ///< energy threshold
    float thr_quiet;    ///< threshold in quiet
    float nz_lines;     ///< number of non-zero spectral lines
    float active_lines; ///< number of active spectral lines
    float pe;           ///< perceptual entropy
    float pe_const;     ///< constant part of the PE calculation
    float norm_fac;     ///< normalization factor for linearization
    int   avoid_holes;  ///< hole avoidance flag
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
    float ath;           ///< absolute threshold of hearing per bands
    float barks;         ///< Bark value for each spectral band in long frame
    float spread_low[2]; ///< spreading factor for low-to-high threshold spreading in long frame
    float spread_hi [2]; ///< spreading factor for high-to-low threshold spreading in long frame
    float min_snr;       ///< minimal SNR
}AacPsyCoeffs;

/**
 * 3GPP TS26.403-inspired psychoacoustic model specific data
 */
typedef struct AacPsyContext{
    int chan_bitrate;     ///< bitrate per channel
    int frame_bits;       ///< average bits per frame
    int fill_level;       ///< bit reservoir fill level
    struct {
        float min;        ///< minimum allowed PE for bit factor calculation
        float max;        ///< maximum allowed PE for bit factor calculation
        float previous;   ///< allowed PE of the previous frame
        float correction; ///< PE correction factor
    } pe;
    AacPsyCoeffs psy_coef[2][64];
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
 * Calculate the ABR attack threshold from the above LAME psymodel table.
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
    float prev, minscale, minath, minsnr, pe_min;
    const int chan_bitrate = ctx->avctx->bit_rate / ctx->avctx->channels;
    const int bandwidth    = ctx->avctx->cutoff ? ctx->avctx->cutoff : ctx->avctx->sample_rate / 2;
    const float num_bark   = calc_bark((float)bandwidth);

    ctx->model_priv_data = av_mallocz(sizeof(AacPsyContext));
    pctx = (AacPsyContext*) ctx->model_priv_data;

    pctx->chan_bitrate = chan_bitrate;
    pctx->frame_bits   = chan_bitrate * AAC_BLOCK_SIZE_LONG / ctx->avctx->sample_rate;
    pctx->pe.min       =  8.0f * AAC_BLOCK_SIZE_LONG * bandwidth / (ctx->avctx->sample_rate * 2.0f);
    pctx->pe.max       = 12.0f * AAC_BLOCK_SIZE_LONG * bandwidth / (ctx->avctx->sample_rate * 2.0f);
    ctx->bitres.size   = 6144 - pctx->frame_bits;
    ctx->bitres.size  -= ctx->bitres.size % 8;
    pctx->fill_level   = ctx->bitres.size;
    minath = ath(3410, ATH_ADD);
    for (j = 0; j < 2; j++) {
        AacPsyCoeffs *coeffs = pctx->psy_coef[j];
        const uint8_t *band_sizes = ctx->bands[j];
        float line_to_frequency = ctx->avctx->sample_rate / (j ? 256.f : 2048.0f);
        float avg_chan_bits = chan_bitrate / ctx->avctx->sample_rate * (j ? 128.0f : 1024.0f);
        /* reference encoder uses 2.4% here instead of 60% like the spec says */
        float bark_pe = 0.024f * PSY_3GPP_BITS_TO_PE(avg_chan_bits) / num_bark;
        float en_spread_low = j ? PSY_3GPP_EN_SPREAD_LOW_S : PSY_3GPP_EN_SPREAD_LOW_L;
        /* High energy spreading for long blocks <= 22kbps/channel and short blocks are the same. */
        float en_spread_hi  = (j || (chan_bitrate <= 22.0f)) ? PSY_3GPP_EN_SPREAD_HI_S : PSY_3GPP_EN_SPREAD_HI_L1;

        i = 0;
        prev = 0.0;
        for (g = 0; g < ctx->num_bands[j]; g++) {
            i += band_sizes[g];
            bark = calc_bark((i-1) * line_to_frequency);
            coeffs[g].barks = (bark + prev) / 2.0;
            prev = bark;
        }
        for (g = 0; g < ctx->num_bands[j] - 1; g++) {
            AacPsyCoeffs *coeff = &coeffs[g];
            float bark_width = coeffs[g+1].barks - coeffs->barks;
            coeff->spread_low[0] = pow(10.0, -bark_width * PSY_3GPP_THR_SPREAD_LOW);
            coeff->spread_hi [0] = pow(10.0, -bark_width * PSY_3GPP_THR_SPREAD_HI);
            coeff->spread_low[1] = pow(10.0, -bark_width * en_spread_low);
            coeff->spread_hi [1] = pow(10.0, -bark_width * en_spread_hi);
            pe_min = bark_pe * bark_width;
            minsnr = pow(2.0f, pe_min / band_sizes[g]) - 1.5f;
            coeff->min_snr = av_clipf(1.0f / minsnr, PSY_SNR_25DB, PSY_SNR_1DB);
        }
        start = 0;
        for (g = 0; g < ctx->num_bands[j]; g++) {
            minscale = ath(start * line_to_frequency, ATH_ADD);
            for (i = 1; i < band_sizes[g]; i++)
                minscale = FFMIN(minscale, ath((start + i) * line_to_frequency, ATH_ADD));
            coeffs[g].ath = minscale - minath;
            start += band_sizes[g];
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
static av_unused FFPsyWindowInfo psy_3gpp_window(FFPsyContext *ctx,
                                                 const int16_t *audio,
                                                 const int16_t *la,
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
                v = iir_filter(la[i*128+j], pch->iir_state);
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

/* 5.6.1.2 "Calculation of Bit Demand" */
static int calc_bit_demand(AacPsyContext *ctx, float pe, int bits, int size,
                           int short_window)
{
    const float bitsave_slope  = short_window ? PSY_3GPP_SAVE_SLOPE_S  : PSY_3GPP_SAVE_SLOPE_L;
    const float bitsave_add    = short_window ? PSY_3GPP_SAVE_ADD_S    : PSY_3GPP_SAVE_ADD_L;
    const float bitspend_slope = short_window ? PSY_3GPP_SPEND_SLOPE_S : PSY_3GPP_SPEND_SLOPE_L;
    const float bitspend_add   = short_window ? PSY_3GPP_SPEND_ADD_S   : PSY_3GPP_SPEND_ADD_L;
    const float clip_low       = short_window ? PSY_3GPP_CLIP_LO_S     : PSY_3GPP_CLIP_LO_L;
    const float clip_high      = short_window ? PSY_3GPP_CLIP_HI_S     : PSY_3GPP_CLIP_HI_L;
    float clipped_pe, bit_save, bit_spend, bit_factor, fill_level;

    ctx->fill_level += ctx->frame_bits - bits;
    ctx->fill_level  = av_clip(ctx->fill_level, 0, size);
    fill_level = av_clipf((float)ctx->fill_level / size, clip_low, clip_high);
    clipped_pe = av_clipf(pe, ctx->pe.min, ctx->pe.max);
    bit_save   = (fill_level + bitsave_add) * bitsave_slope;
    assert(bit_save <= 0.3f && bit_save >= -0.05000001f);
    bit_spend  = (fill_level + bitspend_add) * bitspend_slope;
    assert(bit_spend <= 0.5f && bit_spend >= -0.1f);
    /* The bit factor graph in the spec is obviously incorrect.
     *      bit_spend + ((bit_spend - bit_spend))...
     * The reference encoder subtracts everything from 1, but also seems incorrect.
     *      1 - bit_save + ((bit_spend + bit_save))...
     * Hopefully below is correct.
     */
    bit_factor = 1.0f - bit_save + ((bit_spend - bit_save) / (ctx->pe.max - ctx->pe.min)) * (clipped_pe - ctx->pe.min);
    /* NOTE: The reference encoder attempts to center pe max/min around the current pe. */
    ctx->pe.max = FFMAX(pe, ctx->pe.max);
    ctx->pe.min = FFMIN(pe, ctx->pe.min);

    return FFMIN(ctx->frame_bits * bit_factor, ctx->frame_bits + size - bits);
}

static float calc_pe_3gpp(AacPsyBand *band)
{
    float pe, a;

    band->pe           = 0.0f;
    band->pe_const     = 0.0f;
    band->active_lines = 0.0f;
    if (band->energy > band->thr) {
        a  = log2f(band->energy);
        pe = a - log2f(band->thr);
        band->active_lines = band->nz_lines;
        if (pe < PSY_3GPP_C1) {
            pe = pe * PSY_3GPP_C3 + PSY_3GPP_C2;
            a  = a  * PSY_3GPP_C3 + PSY_3GPP_C2;
            band->active_lines *= PSY_3GPP_C3;
        }
        band->pe       = pe * band->nz_lines;
        band->pe_const = a  * band->nz_lines;
    }

    return band->pe;
}

static float calc_reduction_3gpp(float a, float desired_pe, float pe,
                                 float active_lines)
{
    float thr_avg, reduction;

    thr_avg   = powf(2.0f, (a - pe) / (4.0f * active_lines));
    reduction = powf(2.0f, (a - desired_pe) / (4.0f * active_lines)) - thr_avg;

    return FFMAX(reduction, 0.0f);
}

static float calc_reduced_thr_3gpp(AacPsyBand *band, float min_snr,
                                   float reduction)
{
    float thr = band->thr;

    if (band->energy > thr) {
        thr = powf(thr, 0.25f) + reduction;
        thr = powf(thr, 4.0f);

        /* This deviates from the 3GPP spec to match the reference encoder.
         * It performs min(thr_reduced, max(thr, energy/min_snr)) only for bands
         * that have hole avoidance on (active or inactive). It always reduces the
         * threshold of bands with hole avoidance off.
         */
        if (thr > band->energy * min_snr && band->avoid_holes != PSY_3GPP_AH_NONE) {
            thr = FFMAX(band->thr, band->energy * min_snr);
            band->avoid_holes = PSY_3GPP_AH_ACTIVE;
        }
    }

    return thr;
}

/**
 * Calculate band thresholds as suggested in 3GPP TS26.403
 */
static void psy_3gpp_analyze_channel(FFPsyContext *ctx, int channel,
                                     const float *coefs, const FFPsyWindowInfo *wi)
{
    AacPsyContext *pctx = (AacPsyContext*) ctx->model_priv_data;
    AacPsyChannel *pch  = &pctx->ch[channel];
    int start = 0;
    int i, w, g;
    float desired_bits, desired_pe, delta_pe, reduction, spread_en[128] = {0};
    float a = 0.0f, active_lines = 0.0f, norm_fac = 0.0f;
    float pe = pctx->chan_bitrate > 32000 ? 0.0f : FFMAX(50.0f, 100.0f - pctx->chan_bitrate * 100.0f / 32000.0f);
    const int      num_bands   = ctx->num_bands[wi->num_windows == 8];
    const uint8_t *band_sizes  = ctx->bands[wi->num_windows == 8];
    AacPsyCoeffs  *coeffs      = pctx->psy_coef[wi->num_windows == 8];
    const float avoid_hole_thr = wi->num_windows == 8 ? PSY_3GPP_AH_THR_SHORT : PSY_3GPP_AH_THR_LONG;

    //calculate energies, initial thresholds and related values - 5.4.2 "Threshold Calculation"
    for (w = 0; w < wi->num_windows*16; w += 16) {
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band = &pch->band[w+g];

            float form_factor = 0.0f;
            band->energy = 0.0f;
            for (i = 0; i < band_sizes[g]; i++) {
                band->energy += coefs[start+i] * coefs[start+i];
                form_factor  += sqrtf(fabs(coefs[start+i]));
            }
            band->thr      = band->energy * 0.001258925f;
            band->nz_lines = form_factor / powf(band->energy / band_sizes[g], 0.25f);

            start += band_sizes[g];
        }
    }
    //modify thresholds and energies - spread, threshold in quiet, pre-echo control
    for (w = 0; w < wi->num_windows*16; w += 16) {
        AacPsyBand *bands = &pch->band[w];

        //5.4.2.3 "Spreading" & 5.4.3 "Spreaded Energy Calculation"
        spread_en[0] = bands[0].energy;
        for (g = 1; g < num_bands; g++) {
            bands[g].thr   = FFMAX(bands[g].thr,    bands[g-1].thr * coeffs[g].spread_hi[0]);
            spread_en[w+g] = FFMAX(bands[g].energy, spread_en[w+g-1] * coeffs[g].spread_hi[1]);
        }
        for (g = num_bands - 2; g >= 0; g--) {
            bands[g].thr   = FFMAX(bands[g].thr,   bands[g+1].thr * coeffs[g].spread_low[0]);
            spread_en[w+g] = FFMAX(spread_en[w+g], spread_en[w+g+1] * coeffs[g].spread_low[1]);
        }
        //5.4.2.4 "Threshold in quiet"
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band = &bands[g];

            band->thr_quiet = band->thr = FFMAX(band->thr, coeffs[g].ath);
            //5.4.2.5 "Pre-echo control"
            if (!(wi->window_type[0] == LONG_STOP_SEQUENCE || (wi->window_type[1] == LONG_START_SEQUENCE && !w)))
                band->thr = FFMAX(PSY_3GPP_RPEMIN*band->thr, FFMIN(band->thr,
                                  PSY_3GPP_RPELEV*pch->prev_band[w+g].thr_quiet));

            /* 5.6.1.3.1 "Prepatory steps of the perceptual entropy calculation" */
            pe += calc_pe_3gpp(band);
            a  += band->pe_const;
            active_lines += band->active_lines;

            /* 5.6.1.3.3 "Selection of the bands for avoidance of holes" */
            if (spread_en[w+g] * avoid_hole_thr > band->energy || coeffs[g].min_snr > 1.0f)
                band->avoid_holes = PSY_3GPP_AH_NONE;
            else
                band->avoid_holes = PSY_3GPP_AH_INACTIVE;
        }
    }

    /* 5.6.1.3.2 "Calculation of the desired perceptual entropy" */
    ctx->ch[channel].entropy = pe;
    desired_bits = calc_bit_demand(pctx, pe, ctx->bitres.bits, ctx->bitres.size, wi->num_windows == 8);
    desired_pe = PSY_3GPP_BITS_TO_PE(desired_bits);
    /* NOTE: PE correction is kept simple. During initial testing it had very
     *       little effect on the final bitrate. Probably a good idea to come
     *       back and do more testing later.
     */
    if (ctx->bitres.bits > 0)
        desired_pe *= av_clipf(pctx->pe.previous / PSY_3GPP_BITS_TO_PE(ctx->bitres.bits),
                               0.85f, 1.15f);
    pctx->pe.previous = PSY_3GPP_BITS_TO_PE(desired_bits);

    if (desired_pe < pe) {
        /* 5.6.1.3.4 "First Estimation of the reduction value" */
        for (w = 0; w < wi->num_windows*16; w += 16) {
            reduction = calc_reduction_3gpp(a, desired_pe, pe, active_lines);
            pe = 0.0f;
            a  = 0.0f;
            active_lines = 0.0f;
            for (g = 0; g < num_bands; g++) {
                AacPsyBand *band = &pch->band[w+g];

                band->thr = calc_reduced_thr_3gpp(band, coeffs[g].min_snr, reduction);
                /* recalculate PE */
                pe += calc_pe_3gpp(band);
                a  += band->pe_const;
                active_lines += band->active_lines;
            }
        }

        /* 5.6.1.3.5 "Second Estimation of the reduction value" */
        for (i = 0; i < 2; i++) {
            float pe_no_ah = 0.0f, desired_pe_no_ah;
            active_lines = a = 0.0f;
            for (w = 0; w < wi->num_windows*16; w += 16) {
                for (g = 0; g < num_bands; g++) {
                    AacPsyBand *band = &pch->band[w+g];

                    if (band->avoid_holes != PSY_3GPP_AH_ACTIVE) {
                        pe_no_ah += band->pe;
                        a        += band->pe_const;
                        active_lines += band->active_lines;
                    }
                }
            }
            desired_pe_no_ah = FFMAX(desired_pe - (pe - pe_no_ah), 0.0f);
            if (active_lines > 0.0f)
                reduction += calc_reduction_3gpp(a, desired_pe_no_ah, pe_no_ah, active_lines);

            pe = 0.0f;
            for (w = 0; w < wi->num_windows*16; w += 16) {
                for (g = 0; g < num_bands; g++) {
                    AacPsyBand *band = &pch->band[w+g];

                    if (active_lines > 0.0f)
                        band->thr = calc_reduced_thr_3gpp(band, coeffs[g].min_snr, reduction);
                    pe += calc_pe_3gpp(band);
                    band->norm_fac = band->active_lines / band->thr;
                    norm_fac += band->norm_fac;
                }
            }
            delta_pe = desired_pe - pe;
            if (fabs(delta_pe) > 0.05f * desired_pe)
                break;
        }

        if (pe < 1.15f * desired_pe) {
            /* 6.6.1.3.6 "Final threshold modification by linearization" */
            norm_fac = 1.0f / norm_fac;
            for (w = 0; w < wi->num_windows*16; w += 16) {
                for (g = 0; g < num_bands; g++) {
                    AacPsyBand *band = &pch->band[w+g];

                    if (band->active_lines > 0.5f) {
                        float delta_sfb_pe = band->norm_fac * norm_fac * delta_pe;
                        float thr = band->thr;

                        thr *= powf(2.0f, delta_sfb_pe / band->active_lines);
                        if (thr > coeffs[g].min_snr * band->energy && band->avoid_holes == PSY_3GPP_AH_INACTIVE)
                            thr = FFMAX(band->thr, coeffs[g].min_snr * band->energy);
                        band->thr = thr;
                    }
                }
            }
        } else {
            /* 5.6.1.3.7 "Further perceptual entropy reduction" */
            g = num_bands;
            while (pe > desired_pe && g--) {
                for (w = 0; w < wi->num_windows*16; w+= 16) {
                    AacPsyBand *band = &pch->band[w+g];
                    if (band->avoid_holes != PSY_3GPP_AH_NONE && coeffs[g].min_snr < PSY_SNR_1DB) {
                        coeffs[g].min_snr = PSY_SNR_1DB;
                        band->thr = band->energy * PSY_SNR_1DB;
                        pe += band->active_lines * 1.5f - band->pe;
                    }
                }
            }
            /* TODO: allow more holes (unused without mid/side) */
        }
    }

    for (w = 0; w < wi->num_windows*16; w += 16) {
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band     = &pch->band[w+g];
            FFPsyBand  *psy_band = &ctx->ch[channel].psy_bands[w+g];

            psy_band->threshold = band->thr;
            psy_band->energy    = band->energy;
        }
    }

    memcpy(pch->prev_band, pch->band, sizeof(pch->band));
}

static void psy_3gpp_analyze(FFPsyContext *ctx, int channel,
                                   const float **coeffs, const FFPsyWindowInfo *wi)
{
    int ch;
    FFPsyChannelGroup *group = ff_psy_find_group(ctx, channel);

    for (ch = 0; ch < group->num_ch; ch++)
        psy_3gpp_analyze_channel(ctx, channel + ch, coeffs[ch], &wi[ch]);
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

static FFPsyWindowInfo psy_lame_window(FFPsyContext *ctx, const float *audio,
                                       const float *la, int channel, int prev_type)
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
        const float *firbuf = la + (AAC_BLOCK_SIZE_SHORT/4 - PSY_LAME_FIR_LEN);
        int j, att_sum = 0;

        /* LAME comment: apply high pass filter of fs/4 */
        for (i = 0; i < AAC_BLOCK_SIZE_LONG; i++) {
            float sum1, sum2;
            sum1 = firbuf[i + (PSY_LAME_FIR_LEN - 1) / 2];
            sum2 = 0.0;
            for (j = 0; j < ((PSY_LAME_FIR_LEN - 1) / 2) - 1; j += 2) {
                sum1 += psy_fir_coeffs[j] * (firbuf[i + j] + firbuf[i + PSY_LAME_FIR_LEN - j]);
                sum2 += psy_fir_coeffs[j + 1] * (firbuf[i + j + 1] + firbuf[i + PSY_LAME_FIR_LEN - j - 1]);
            }
            /* NOTE: The LAME psymodel expects it's input in the range -32768 to 32768. Tuning this for normalized floats would be difficult. */
            hpfsmpl[i] = (sum1 + sum2) * 32768.0f;
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
                p = FFMAX(p, fabsf(*pf));
            pch->prev_energy_subshort[i] = energy_subshort[i + PSY_LAME_NUM_SUBBLOCKS] = p;
            energy_short[1 + i / PSY_LAME_NUM_SUBBLOCKS] += p;
            /* NOTE: The indexes below are [i + 3 - 2] in the LAME source.
             *       Obviously the 3 and 2 have some significance, or this would be just [i + 1]
             *       (which is what we use here). What the 3 stands for is ambiguous, as it is both
             *       number of short blocks, and the number of sub-short blocks.
             *       It seems that LAME is comparing each sub-block to sub-block + 1 in the
             *       previous block.
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

            for (i = 1; i < AAC_NUM_BLOCKS_SHORT + 1; i++)
                if (attacks[i] && attacks[i-1])
                    attacks[i] = 0;
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
