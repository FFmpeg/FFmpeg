/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2010 Justin Ruggles <justin.ruggles@gmail.com>
 * Copyright (c) 2006-2010 Prakash Punnoor <prakash@punnoor.de>
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
 * The simplest AC-3 encoder.
 */

//#define DEBUG

#include "libavcore/audioconvert.h"
#include "libavutil/crc.h"
#include "avcodec.h"
#include "put_bits.h"
#include "dsputil.h"
#include "ac3.h"
#include "audioconvert.h"


#ifndef CONFIG_AC3ENC_FLOAT
#define CONFIG_AC3ENC_FLOAT 0
#endif


/** Maximum number of exponent groups. +1 for separate DC exponent. */
#define AC3_MAX_EXP_GROUPS 85

/* stereo rematrixing algorithms */
#define AC3_REMATRIXING_IS_STATIC 0x1
#define AC3_REMATRIXING_SUMS    0
#define AC3_REMATRIXING_NONE    1
#define AC3_REMATRIXING_ALWAYS  3

/** Scale a float value by 2^bits and convert to an integer. */
#define SCALE_FLOAT(a, bits) lrintf((a) * (float)(1 << (bits)))


#if CONFIG_AC3ENC_FLOAT
#include "ac3enc_float.h"
#else
#include "ac3enc_fixed.h"
#endif


/**
 * Data for a single audio block.
 */
typedef struct AC3Block {
    uint8_t  **bap;                             ///< bit allocation pointers (bap)
    CoefType **mdct_coef;                       ///< MDCT coefficients
    int32_t  **fixed_coef;                      ///< fixed-point MDCT coefficients
    uint8_t  **exp;                             ///< original exponents
    uint8_t  **grouped_exp;                     ///< grouped exponents
    int16_t  **psd;                             ///< psd per frequency bin
    int16_t  **band_psd;                        ///< psd per critical band
    int16_t  **mask;                            ///< masking curve
    uint16_t **qmant;                           ///< quantized mantissas
    int8_t   exp_shift[AC3_MAX_CHANNELS];       ///< exponent shift values
    uint8_t  new_rematrixing_strategy;          ///< send new rematrixing flags in this block
    uint8_t  rematrixing_flags[4];              ///< rematrixing flags
} AC3Block;

/**
 * AC-3 encoder private context.
 */
typedef struct AC3EncodeContext {
    PutBitContext pb;                       ///< bitstream writer context
    DSPContext dsp;
    AC3MDCTContext mdct;                    ///< MDCT context

    AC3Block blocks[AC3_MAX_BLOCKS];        ///< per-block info

    int bitstream_id;                       ///< bitstream id                           (bsid)
    int bitstream_mode;                     ///< bitstream mode                         (bsmod)

    int bit_rate;                           ///< target bit rate, in bits-per-second
    int sample_rate;                        ///< sampling frequency, in Hz

    int frame_size_min;                     ///< minimum frame size in case rounding is necessary
    int frame_size;                         ///< current frame size in bytes
    int frame_size_code;                    ///< frame size code                        (frmsizecod)
    uint16_t crc_inv[2];
    int bits_written;                       ///< bit count    (used to avg. bitrate)
    int samples_written;                    ///< sample count (used to avg. bitrate)

    int fbw_channels;                       ///< number of full-bandwidth channels      (nfchans)
    int channels;                           ///< total number of channels               (nchans)
    int lfe_on;                             ///< indicates if there is an LFE channel   (lfeon)
    int lfe_channel;                        ///< channel index of the LFE channel
    int channel_mode;                       ///< channel mode                           (acmod)
    const uint8_t *channel_map;             ///< channel map used to reorder channels

    int cutoff;                             ///< user-specified cutoff frequency, in Hz
    int bandwidth_code[AC3_MAX_CHANNELS];   ///< bandwidth code (0 to 60)               (chbwcod)
    int nb_coefs[AC3_MAX_CHANNELS];

    int rematrixing;                        ///< determines how rematrixing strategy is calculated

    /* bitrate allocation control */
    int slow_gain_code;                     ///< slow gain code                         (sgaincod)
    int slow_decay_code;                    ///< slow decay code                        (sdcycod)
    int fast_decay_code;                    ///< fast decay code                        (fdcycod)
    int db_per_bit_code;                    ///< dB/bit code                            (dbpbcod)
    int floor_code;                         ///< floor code                             (floorcod)
    AC3BitAllocParameters bit_alloc;        ///< bit allocation parameters
    int coarse_snr_offset;                  ///< coarse SNR offsets                     (csnroffst)
    int fast_gain_code[AC3_MAX_CHANNELS];   ///< fast gain codes (signal-to-mask ratio) (fgaincod)
    int fine_snr_offset[AC3_MAX_CHANNELS];  ///< fine SNR offsets                       (fsnroffst)
    int frame_bits_fixed;                   ///< number of non-coefficient bits for fixed parameters
    int frame_bits;                         ///< all frame bits except exponents and mantissas
    int exponent_bits;                      ///< number of bits used for exponents

    /* mantissa encoding */
    int mant1_cnt, mant2_cnt, mant4_cnt;    ///< mantissa counts for bap=1,2,4
    uint16_t *qmant1_ptr, *qmant2_ptr, *qmant4_ptr; ///< mantissa pointers for bap=1,2,4

    SampleType **planar_samples;
    uint8_t *bap_buffer;
    uint8_t *bap1_buffer;
    CoefType *mdct_coef_buffer;
    int32_t *fixed_coef_buffer;
    uint8_t *exp_buffer;
    uint8_t *grouped_exp_buffer;
    int16_t *psd_buffer;
    int16_t *band_psd_buffer;
    int16_t *mask_buffer;
    uint16_t *qmant_buffer;

    uint8_t exp_strategy[AC3_MAX_CHANNELS][AC3_MAX_BLOCKS]; ///< exponent strategies

    DECLARE_ALIGNED(16, SampleType, windowed_samples)[AC3_WINDOW_SIZE];
} AC3EncodeContext;


/* prototypes for functions in ac3enc_fixed.c and ac3enc_float.c */

static av_cold void mdct_end(AC3MDCTContext *mdct);

static av_cold int mdct_init(AVCodecContext *avctx, AC3MDCTContext *mdct,
                             int nbits);

static void mdct512(AC3MDCTContext *mdct, CoefType *out, SampleType *in);

static void apply_window(SampleType *output, const SampleType *input,
                         const SampleType *window, int n);

static int normalize_samples(AC3EncodeContext *s);

static void scale_coefficients(AC3EncodeContext *s);


/**
 * LUT for number of exponent groups.
 * exponent_group_tab[exponent strategy-1][number of coefficients]
 */
static uint8_t exponent_group_tab[3][256];


/**
 * List of supported channel layouts.
 */
static const int64_t ac3_channel_layouts[] = {
     AV_CH_LAYOUT_MONO,
     AV_CH_LAYOUT_STEREO,
     AV_CH_LAYOUT_2_1,
     AV_CH_LAYOUT_SURROUND,
     AV_CH_LAYOUT_2_2,
     AV_CH_LAYOUT_QUAD,
     AV_CH_LAYOUT_4POINT0,
     AV_CH_LAYOUT_5POINT0,
     AV_CH_LAYOUT_5POINT0_BACK,
    (AV_CH_LAYOUT_MONO     | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_STEREO   | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_2_1      | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_SURROUND | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_2_2      | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_QUAD     | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_4POINT0  | AV_CH_LOW_FREQUENCY),
     AV_CH_LAYOUT_5POINT1,
     AV_CH_LAYOUT_5POINT1_BACK,
     0
};


/**
 * Adjust the frame size to make the average bit rate match the target bit rate.
 * This is only needed for 11025, 22050, and 44100 sample rates.
 */
static void adjust_frame_size(AC3EncodeContext *s)
{
    while (s->bits_written >= s->bit_rate && s->samples_written >= s->sample_rate) {
        s->bits_written    -= s->bit_rate;
        s->samples_written -= s->sample_rate;
    }
    s->frame_size = s->frame_size_min +
                    2 * (s->bits_written * s->sample_rate < s->samples_written * s->bit_rate);
    s->bits_written    += s->frame_size * 8;
    s->samples_written += AC3_FRAME_SIZE;
}


/**
 * Deinterleave input samples.
 * Channels are reordered from FFmpeg's default order to AC-3 order.
 */
static void deinterleave_input_samples(AC3EncodeContext *s,
                                       const SampleType *samples)
{
    int ch, i;

    /* deinterleave and remap input samples */
    for (ch = 0; ch < s->channels; ch++) {
        const SampleType *sptr;
        int sinc;

        /* copy last 256 samples of previous frame to the start of the current frame */
        memcpy(&s->planar_samples[ch][0], &s->planar_samples[ch][AC3_FRAME_SIZE],
               AC3_BLOCK_SIZE * sizeof(s->planar_samples[0][0]));

        /* deinterleave */
        sinc = s->channels;
        sptr = samples + s->channel_map[ch];
        for (i = AC3_BLOCK_SIZE; i < AC3_FRAME_SIZE+AC3_BLOCK_SIZE; i++) {
            s->planar_samples[ch][i] = *sptr;
            sptr += sinc;
        }
    }
}


/**
 * Apply the MDCT to input samples to generate frequency coefficients.
 * This applies the KBD window and normalizes the input to reduce precision
 * loss due to fixed-point calculations.
 */
static void apply_mdct(AC3EncodeContext *s)
{
    int blk, ch;

    for (ch = 0; ch < s->channels; ch++) {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            const SampleType *input_samples = &s->planar_samples[ch][blk * AC3_BLOCK_SIZE];

            apply_window(s->windowed_samples, input_samples, s->mdct.window, AC3_WINDOW_SIZE);

            block->exp_shift[ch] = normalize_samples(s);

            mdct512(&s->mdct, block->mdct_coef[ch], s->windowed_samples);
        }
    }
}


/**
 * Initialize stereo rematrixing.
 * If the strategy does not change for each frame, set the rematrixing flags.
 */
static void rematrixing_init(AC3EncodeContext *s)
{
    if (s->channel_mode == AC3_CHMODE_STEREO)
        s->rematrixing = AC3_REMATRIXING_SUMS;
    else
        s->rematrixing = AC3_REMATRIXING_NONE;
    /* NOTE: AC3_REMATRIXING_ALWAYS might be used in
             the future in conjunction with channel coupling. */

    if (s->rematrixing & AC3_REMATRIXING_IS_STATIC) {
        int flag = (s->rematrixing == AC3_REMATRIXING_ALWAYS);
        s->blocks[0].new_rematrixing_strategy = 1;
        memset(s->blocks[0].rematrixing_flags, flag,
               sizeof(s->blocks[0].rematrixing_flags));
    }
}


/**
 * Determine rematrixing flags for each block and band.
 */
static void compute_rematrixing_strategy(AC3EncodeContext *s)
{
    int nb_coefs;
    int blk, bnd, i;
    AC3Block *block, *block0;

    if (s->rematrixing & AC3_REMATRIXING_IS_STATIC)
        return;

    nb_coefs = FFMIN(s->nb_coefs[0], s->nb_coefs[1]);

    s->blocks[0].new_rematrixing_strategy = 1;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        block = &s->blocks[blk];
        for (bnd = 0; bnd < 4; bnd++) {
            /* calculate calculate sum of squared coeffs for one band in one block */
            int start = ff_ac3_rematrix_band_tab[bnd];
            int end   = FFMIN(nb_coefs, ff_ac3_rematrix_band_tab[bnd+1]);
            CoefSumType sum[4] = {0,};
            for (i = start; i < end; i++) {
                CoefType lt = block->mdct_coef[0][i];
                CoefType rt = block->mdct_coef[1][i];
                CoefType md = lt + rt;
                CoefType sd = lt - rt;
                sum[0] += lt * lt;
                sum[1] += rt * rt;
                sum[2] += md * md;
                sum[3] += sd * sd;
            }

            /* compare sums to determine if rematrixing will be used for this band */
            if (FFMIN(sum[2], sum[3]) < FFMIN(sum[0], sum[1]))
                block->rematrixing_flags[bnd] = 1;
            else
                block->rematrixing_flags[bnd] = 0;

            /* determine if new rematrixing flags will be sent */
            if (blk &&
                !block->new_rematrixing_strategy &&
                block->rematrixing_flags[bnd] != block0->rematrixing_flags[bnd]) {
                block->new_rematrixing_strategy = 1;
            }
        }
        block0 = block;
    }
}


/**
 * Apply stereo rematrixing to coefficients based on rematrixing flags.
 */
static void apply_rematrixing(AC3EncodeContext *s)
{
    int nb_coefs;
    int blk, bnd, i;
    int start, end;
    uint8_t *flags;

    if (s->rematrixing == AC3_REMATRIXING_NONE)
        return;

    nb_coefs = FFMIN(s->nb_coefs[0], s->nb_coefs[1]);

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (block->new_rematrixing_strategy)
            flags = block->rematrixing_flags;
        for (bnd = 0; bnd < 4; bnd++) {
            if (flags[bnd]) {
                start = ff_ac3_rematrix_band_tab[bnd];
                end   = FFMIN(nb_coefs, ff_ac3_rematrix_band_tab[bnd+1]);
                for (i = start; i < end; i++) {
                    int32_t lt = block->fixed_coef[0][i];
                    int32_t rt = block->fixed_coef[1][i];
                    block->fixed_coef[0][i] = (lt + rt) >> 1;
                    block->fixed_coef[1][i] = (lt - rt) >> 1;
                }
            }
        }
    }
}


/**
 * Initialize exponent tables.
 */
static av_cold void exponent_init(AC3EncodeContext *s)
{
    int i;
    for (i = 73; i < 256; i++) {
        exponent_group_tab[0][i] = (i - 1) /  3;
        exponent_group_tab[1][i] = (i + 2) /  6;
        exponent_group_tab[2][i] = (i + 8) / 12;
    }
    /* LFE */
    exponent_group_tab[0][7] = 2;
}


/**
 * Extract exponents from the MDCT coefficients.
 * This takes into account the normalization that was done to the input samples
 * by adjusting the exponents by the exponent shift values.
 */
static void extract_exponents(AC3EncodeContext *s)
{
    int blk, ch, i;

    for (ch = 0; ch < s->channels; ch++) {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            uint8_t *exp   = block->exp[ch];
            int32_t *coef = block->fixed_coef[ch];
            int exp_shift  = block->exp_shift[ch];
            for (i = 0; i < AC3_MAX_COEFS; i++) {
                int e;
                int v = abs(coef[i]);
                if (v == 0)
                    e = 24;
                else {
                    e = 23 - av_log2(v) + exp_shift;
                    if (e >= 24) {
                        e = 24;
                        coef[i] = 0;
                    }
                }
                exp[i] = e;
            }
        }
    }
}


/**
 * Exponent Difference Threshold.
 * New exponents are sent if their SAD exceed this number.
 */
#define EXP_DIFF_THRESHOLD 1000


/**
 * Calculate exponent strategies for all blocks in a single channel.
 */
static void compute_exp_strategy_ch(AC3EncodeContext *s, uint8_t *exp_strategy,
                                    uint8_t *exp)
{
    int blk, blk1;
    int exp_diff;

    /* estimate if the exponent variation & decide if they should be
       reused in the next frame */
    exp_strategy[0] = EXP_NEW;
    exp += AC3_MAX_COEFS;
    for (blk = 1; blk < AC3_MAX_BLOCKS; blk++) {
        exp_diff = s->dsp.sad[0](NULL, exp, exp - AC3_MAX_COEFS, 16, 16);
        if (exp_diff > EXP_DIFF_THRESHOLD)
            exp_strategy[blk] = EXP_NEW;
        else
            exp_strategy[blk] = EXP_REUSE;
        exp += AC3_MAX_COEFS;
    }
    emms_c();

    /* now select the encoding strategy type : if exponents are often
       recoded, we use a coarse encoding */
    blk = 0;
    while (blk < AC3_MAX_BLOCKS) {
        blk1 = blk + 1;
        while (blk1 < AC3_MAX_BLOCKS && exp_strategy[blk1] == EXP_REUSE)
            blk1++;
        switch (blk1 - blk) {
        case 1:  exp_strategy[blk] = EXP_D45; break;
        case 2:
        case 3:  exp_strategy[blk] = EXP_D25; break;
        default: exp_strategy[blk] = EXP_D15; break;
        }
        blk = blk1;
    }
}


/**
 * Calculate exponent strategies for all channels.
 * Array arrangement is reversed to simplify the per-channel calculation.
 */
static void compute_exp_strategy(AC3EncodeContext *s)
{
    int ch, blk;

    for (ch = 0; ch < s->fbw_channels; ch++) {
        compute_exp_strategy_ch(s, s->exp_strategy[ch], s->blocks[0].exp[ch]);
    }
    if (s->lfe_on) {
        ch = s->lfe_channel;
        s->exp_strategy[ch][0] = EXP_D15;
        for (blk = 1; blk < AC3_MAX_BLOCKS; blk++)
            s->exp_strategy[ch][blk] = EXP_REUSE;
    }
}


/**
 * Set each encoded exponent in a block to the minimum of itself and the
 * exponents in the same frequency bin of up to 5 following blocks.
 */
static void exponent_min(uint8_t *exp, int num_reuse_blocks, int nb_coefs)
{
    int blk, i;

    if (!num_reuse_blocks)
        return;

    for (i = 0; i < nb_coefs; i++) {
        uint8_t min_exp = *exp;
        uint8_t *exp1 = exp + AC3_MAX_COEFS;
        for (blk = 0; blk < num_reuse_blocks; blk++) {
            uint8_t next_exp = *exp1;
            if (next_exp < min_exp)
                min_exp = next_exp;
            exp1 += AC3_MAX_COEFS;
        }
        *exp++ = min_exp;
    }
}


/**
 * Update the exponents so that they are the ones the decoder will decode.
 */
static void encode_exponents_blk_ch(uint8_t *exp, int nb_exps, int exp_strategy)
{
    int nb_groups, i, k;

    nb_groups = exponent_group_tab[exp_strategy-1][nb_exps] * 3;

    /* for each group, compute the minimum exponent */
    switch(exp_strategy) {
    case EXP_D25:
        for (i = 1, k = 1; i <= nb_groups; i++) {
            uint8_t exp_min = exp[k];
            if (exp[k+1] < exp_min)
                exp_min = exp[k+1];
            exp[i] = exp_min;
            k += 2;
        }
        break;
    case EXP_D45:
        for (i = 1, k = 1; i <= nb_groups; i++) {
            uint8_t exp_min = exp[k];
            if (exp[k+1] < exp_min)
                exp_min = exp[k+1];
            if (exp[k+2] < exp_min)
                exp_min = exp[k+2];
            if (exp[k+3] < exp_min)
                exp_min = exp[k+3];
            exp[i] = exp_min;
            k += 4;
        }
        break;
    }

    /* constraint for DC exponent */
    if (exp[0] > 15)
        exp[0] = 15;

    /* decrease the delta between each groups to within 2 so that they can be
       differentially encoded */
    for (i = 1; i <= nb_groups; i++)
        exp[i] = FFMIN(exp[i], exp[i-1] + 2);
    i--;
    while (--i >= 0)
        exp[i] = FFMIN(exp[i], exp[i+1] + 2);

    /* now we have the exponent values the decoder will see */
    switch (exp_strategy) {
    case EXP_D25:
        for (i = nb_groups, k = nb_groups * 2; i > 0; i--) {
            uint8_t exp1 = exp[i];
            exp[k--] = exp1;
            exp[k--] = exp1;
        }
        break;
    case EXP_D45:
        for (i = nb_groups, k = nb_groups * 4; i > 0; i--) {
            exp[k] = exp[k-1] = exp[k-2] = exp[k-3] = exp[i];
            k -= 4;
        }
        break;
    }
}


/**
 * Encode exponents from original extracted form to what the decoder will see.
 * This copies and groups exponents based on exponent strategy and reduces
 * deltas between adjacent exponent groups so that they can be differentially
 * encoded.
 */
static void encode_exponents(AC3EncodeContext *s)
{
    int blk, blk1, ch;
    uint8_t *exp, *exp1, *exp_strategy;
    int nb_coefs, num_reuse_blocks;

    for (ch = 0; ch < s->channels; ch++) {
        exp          = s->blocks[0].exp[ch];
        exp_strategy = s->exp_strategy[ch];
        nb_coefs     = s->nb_coefs[ch];

        blk = 0;
        while (blk < AC3_MAX_BLOCKS) {
            blk1 = blk + 1;

            /* count the number of EXP_REUSE blocks after the current block */
            while (blk1 < AC3_MAX_BLOCKS && exp_strategy[blk1] == EXP_REUSE)
                blk1++;
            num_reuse_blocks = blk1 - blk - 1;

            /* for the EXP_REUSE case we select the min of the exponents */
            exponent_min(exp, num_reuse_blocks, nb_coefs);

            encode_exponents_blk_ch(exp, nb_coefs, exp_strategy[blk]);

            /* copy encoded exponents for reuse case */
            exp1 = exp + AC3_MAX_COEFS;
            while (blk < blk1-1) {
                memcpy(exp1, exp, nb_coefs * sizeof(*exp));
                exp1 += AC3_MAX_COEFS;
                blk++;
            }
            blk = blk1;
            exp = exp1;
        }
    }
}


/**
 * Group exponents.
 * 3 delta-encoded exponents are in each 7-bit group. The number of groups
 * varies depending on exponent strategy and bandwidth.
 */
static void group_exponents(AC3EncodeContext *s)
{
    int blk, ch, i;
    int group_size, nb_groups, bit_count;
    uint8_t *p;
    int delta0, delta1, delta2;
    int exp0, exp1;

    bit_count = 0;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 0; ch < s->channels; ch++) {
            int exp_strategy = s->exp_strategy[ch][blk];
            if (exp_strategy == EXP_REUSE)
                continue;
            group_size = exp_strategy + (exp_strategy == EXP_D45);
            nb_groups = exponent_group_tab[exp_strategy-1][s->nb_coefs[ch]];
            bit_count += 4 + (nb_groups * 7);
            p = block->exp[ch];

            /* DC exponent */
            exp1 = *p++;
            block->grouped_exp[ch][0] = exp1;

            /* remaining exponents are delta encoded */
            for (i = 1; i <= nb_groups; i++) {
                /* merge three delta in one code */
                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta0 = exp1 - exp0 + 2;

                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta1 = exp1 - exp0 + 2;

                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta2 = exp1 - exp0 + 2;

                block->grouped_exp[ch][i] = ((delta0 * 5 + delta1) * 5) + delta2;
            }
        }
    }

    s->exponent_bits = bit_count;
}


/**
 * Calculate final exponents from the supplied MDCT coefficients and exponent shift.
 * Extract exponents from MDCT coefficients, calculate exponent strategies,
 * and encode final exponents.
 */
static void process_exponents(AC3EncodeContext *s)
{
    extract_exponents(s);

    compute_exp_strategy(s);

    encode_exponents(s);

    group_exponents(s);
}


/**
 * Count frame bits that are based solely on fixed parameters.
 * This only has to be run once when the encoder is initialized.
 */
static void count_frame_bits_fixed(AC3EncodeContext *s)
{
    static const int frame_bits_inc[8] = { 0, 0, 2, 2, 2, 4, 2, 4 };
    int blk;
    int frame_bits;

    /* assumptions:
     *   no dynamic range codes
     *   no channel coupling
     *   bit allocation parameters do not change between blocks
     *   SNR offsets do not change between blocks
     *   no delta bit allocation
     *   no skipped data
     *   no auxilliary data
     */

    /* header size */
    frame_bits = 65;
    frame_bits += frame_bits_inc[s->channel_mode];

    /* audio blocks */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        frame_bits += s->fbw_channels * 2 + 2; /* blksw * c, dithflag * c, dynrnge, cplstre */
        if (s->channel_mode == AC3_CHMODE_STEREO) {
            frame_bits++; /* rematstr */
        }
        frame_bits += 2 * s->fbw_channels; /* chexpstr[2] * c */
        if (s->lfe_on)
            frame_bits++; /* lfeexpstr */
        frame_bits++; /* baie */
        frame_bits++; /* snr */
        frame_bits += 2; /* delta / skip */
    }
    frame_bits++; /* cplinu for block 0 */
    /* bit alloc info */
    /* sdcycod[2], fdcycod[2], sgaincod[2], dbpbcod[2], floorcod[3] */
    /* csnroffset[6] */
    /* (fsnoffset[4] + fgaincod[4]) * c */
    frame_bits += 2*4 + 3 + 6 + s->channels * (4 + 3);

    /* auxdatae, crcrsv */
    frame_bits += 2;

    /* CRC */
    frame_bits += 16;

    s->frame_bits_fixed = frame_bits;
}


/**
 * Initialize bit allocation.
 * Set default parameter codes and calculate parameter values.
 */
static void bit_alloc_init(AC3EncodeContext *s)
{
    int ch;

    /* init default parameters */
    s->slow_decay_code = 2;
    s->fast_decay_code = 1;
    s->slow_gain_code  = 1;
    s->db_per_bit_code = 3;
    s->floor_code      = 4;
    for (ch = 0; ch < s->channels; ch++)
        s->fast_gain_code[ch] = 4;

    /* initial snr offset */
    s->coarse_snr_offset = 40;

    /* compute real values */
    /* currently none of these values change during encoding, so we can just
       set them once at initialization */
    s->bit_alloc.slow_decay = ff_ac3_slow_decay_tab[s->slow_decay_code] >> s->bit_alloc.sr_shift;
    s->bit_alloc.fast_decay = ff_ac3_fast_decay_tab[s->fast_decay_code] >> s->bit_alloc.sr_shift;
    s->bit_alloc.slow_gain  = ff_ac3_slow_gain_tab[s->slow_gain_code];
    s->bit_alloc.db_per_bit = ff_ac3_db_per_bit_tab[s->db_per_bit_code];
    s->bit_alloc.floor      = ff_ac3_floor_tab[s->floor_code];

    count_frame_bits_fixed(s);
}


/**
 * Count the bits used to encode the frame, minus exponents and mantissas.
 * Bits based on fixed parameters have already been counted, so now we just
 * have to add the bits based on parameters that change during encoding.
 */
static void count_frame_bits(AC3EncodeContext *s)
{
    int blk, ch;
    int frame_bits = 0;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        /* stereo rematrixing */
        if (s->channel_mode == AC3_CHMODE_STEREO &&
            s->blocks[blk].new_rematrixing_strategy) {
            frame_bits += 4;
        }

        for (ch = 0; ch < s->fbw_channels; ch++) {
            if (s->exp_strategy[ch][blk] != EXP_REUSE)
                frame_bits += 6 + 2; /* chbwcod[6], gainrng[2] */
        }
    }
    s->frame_bits = s->frame_bits_fixed + frame_bits;
}


/**
 * Calculate the number of bits needed to encode a set of mantissas.
 */
static int compute_mantissa_size(int mant_cnt[5], uint8_t *bap, int nb_coefs)
{
    int bits, b, i;

    bits = 0;
    for (i = 0; i < nb_coefs; i++) {
        b = bap[i];
        if (b <= 4) {
            // bap=1 to bap=4 will be counted in compute_mantissa_size_final
            mant_cnt[b]++;
        } else if (b <= 13) {
            // bap=5 to bap=13 use (bap-1) bits
            bits += b - 1;
        } else {
            // bap=14 uses 14 bits and bap=15 uses 16 bits
            bits += (b == 14) ? 14 : 16;
        }
    }
    return bits;
}


/**
 * Finalize the mantissa bit count by adding in the grouped mantissas.
 */
static int compute_mantissa_size_final(int mant_cnt[5])
{
    // bap=1 : 3 mantissas in 5 bits
    int bits = (mant_cnt[1] / 3) * 5;
    // bap=2 : 3 mantissas in 7 bits
    // bap=4 : 2 mantissas in 7 bits
    bits += ((mant_cnt[2] / 3) + (mant_cnt[4] >> 1)) * 7;
    // bap=3 : each mantissa is 3 bits
    bits += mant_cnt[3] * 3;
    return bits;
}


/**
 * Calculate masking curve based on the final exponents.
 * Also calculate the power spectral densities to use in future calculations.
 */
static void bit_alloc_masking(AC3EncodeContext *s)
{
    int blk, ch;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 0; ch < s->channels; ch++) {
            /* We only need psd and mask for calculating bap.
               Since we currently do not calculate bap when exponent
               strategy is EXP_REUSE we do not need to calculate psd or mask. */
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                ff_ac3_bit_alloc_calc_psd(block->exp[ch], 0,
                                          s->nb_coefs[ch],
                                          block->psd[ch], block->band_psd[ch]);
                ff_ac3_bit_alloc_calc_mask(&s->bit_alloc, block->band_psd[ch],
                                           0, s->nb_coefs[ch],
                                           ff_ac3_fast_gain_tab[s->fast_gain_code[ch]],
                                           ch == s->lfe_channel,
                                           DBA_NONE, 0, NULL, NULL, NULL,
                                           block->mask[ch]);
            }
        }
    }
}


/**
 * Ensure that bap for each block and channel point to the current bap_buffer.
 * They may have been switched during the bit allocation search.
 */
static void reset_block_bap(AC3EncodeContext *s)
{
    int blk, ch;
    if (s->blocks[0].bap[0] == s->bap_buffer)
        return;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        for (ch = 0; ch < s->channels; ch++) {
            s->blocks[blk].bap[ch] = &s->bap_buffer[AC3_MAX_COEFS * (blk * s->channels + ch)];
        }
    }
}


/**
 * Run the bit allocation with a given SNR offset.
 * This calculates the bit allocation pointers that will be used to determine
 * the quantization of each mantissa.
 * @return the number of bits needed for mantissas if the given SNR offset is
 *         is used.
 */
static int bit_alloc(AC3EncodeContext *s, int snr_offset)
{
    int blk, ch;
    int mantissa_bits;
    int mant_cnt[5];

    snr_offset = (snr_offset - 240) << 2;

    reset_block_bap(s);
    mantissa_bits = 0;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        // initialize grouped mantissa counts. these are set so that they are
        // padded to the next whole group size when bits are counted in
        // compute_mantissa_size_final
        mant_cnt[0] = mant_cnt[3] = 0;
        mant_cnt[1] = mant_cnt[2] = 2;
        mant_cnt[4] = 1;
        for (ch = 0; ch < s->channels; ch++) {
            /* Currently the only bit allocation parameters which vary across
               blocks within a frame are the exponent values.  We can take
               advantage of that by reusing the bit allocation pointers
               whenever we reuse exponents. */
            if (s->exp_strategy[ch][blk] == EXP_REUSE) {
                memcpy(block->bap[ch], s->blocks[blk-1].bap[ch], AC3_MAX_COEFS);
            } else {
                ff_ac3_bit_alloc_calc_bap(block->mask[ch], block->psd[ch], 0,
                                          s->nb_coefs[ch], snr_offset,
                                          s->bit_alloc.floor, ff_ac3_bap_tab,
                                          block->bap[ch]);
            }
            mantissa_bits += compute_mantissa_size(mant_cnt, block->bap[ch], s->nb_coefs[ch]);
        }
        mantissa_bits += compute_mantissa_size_final(mant_cnt);
    }
    return mantissa_bits;
}


/**
 * Constant bitrate bit allocation search.
 * Find the largest SNR offset that will allow data to fit in the frame.
 */
static int cbr_bit_allocation(AC3EncodeContext *s)
{
    int ch;
    int bits_left;
    int snr_offset, snr_incr;

    bits_left = 8 * s->frame_size - (s->frame_bits + s->exponent_bits);

    snr_offset = s->coarse_snr_offset << 4;

    /* if previous frame SNR offset was 1023, check if current frame can also
       use SNR offset of 1023. if so, skip the search. */
    if ((snr_offset | s->fine_snr_offset[0]) == 1023) {
        if (bit_alloc(s, 1023) <= bits_left)
            return 0;
    }

    while (snr_offset >= 0 &&
           bit_alloc(s, snr_offset) > bits_left) {
        snr_offset -= 64;
    }
    if (snr_offset < 0)
        return AVERROR(EINVAL);

    FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
    for (snr_incr = 64; snr_incr > 0; snr_incr >>= 2) {
        while (snr_offset + snr_incr <= 1023 &&
               bit_alloc(s, snr_offset + snr_incr) <= bits_left) {
            snr_offset += snr_incr;
            FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
        }
    }
    FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
    reset_block_bap(s);

    s->coarse_snr_offset = snr_offset >> 4;
    for (ch = 0; ch < s->channels; ch++)
        s->fine_snr_offset[ch] = snr_offset & 0xF;

    return 0;
}


/**
 * Downgrade exponent strategies to reduce the bits used by the exponents.
 * This is a fallback for when bit allocation fails with the normal exponent
 * strategies.  Each time this function is run it only downgrades the
 * strategy in 1 channel of 1 block.
 * @return non-zero if downgrade was unsuccessful
 */
static int downgrade_exponents(AC3EncodeContext *s)
{
    int ch, blk;

    for (ch = 0; ch < s->fbw_channels; ch++) {
        for (blk = AC3_MAX_BLOCKS-1; blk >= 0; blk--) {
            if (s->exp_strategy[ch][blk] == EXP_D15) {
                s->exp_strategy[ch][blk] = EXP_D25;
                return 0;
            }
        }
    }
    for (ch = 0; ch < s->fbw_channels; ch++) {
        for (blk = AC3_MAX_BLOCKS-1; blk >= 0; blk--) {
            if (s->exp_strategy[ch][blk] == EXP_D25) {
                s->exp_strategy[ch][blk] = EXP_D45;
                return 0;
            }
        }
    }
    for (ch = 0; ch < s->fbw_channels; ch++) {
        /* block 0 cannot reuse exponents, so only downgrade D45 to REUSE if
           the block number > 0 */
        for (blk = AC3_MAX_BLOCKS-1; blk > 0; blk--) {
            if (s->exp_strategy[ch][blk] > EXP_REUSE) {
                s->exp_strategy[ch][blk] = EXP_REUSE;
                return 0;
            }
        }
    }
    return -1;
}


/**
 * Reduce the bandwidth to reduce the number of bits used for a given SNR offset.
 * This is a second fallback for when bit allocation still fails after exponents
 * have been downgraded.
 * @return non-zero if bandwidth reduction was unsuccessful
 */
static int reduce_bandwidth(AC3EncodeContext *s, int min_bw_code)
{
    int ch;

    if (s->bandwidth_code[0] > min_bw_code) {
        for (ch = 0; ch < s->fbw_channels; ch++) {
            s->bandwidth_code[ch]--;
            s->nb_coefs[ch] = s->bandwidth_code[ch] * 3 + 73;
        }
        return 0;
    }
    return -1;
}


/**
 * Perform bit allocation search.
 * Finds the SNR offset value that maximizes quality and fits in the specified
 * frame size.  Output is the SNR offset and a set of bit allocation pointers
 * used to quantize the mantissas.
 */
static int compute_bit_allocation(AC3EncodeContext *s)
{
    int ret;

    count_frame_bits(s);

    bit_alloc_masking(s);

    ret = cbr_bit_allocation(s);
    while (ret) {
        /* fallback 1: downgrade exponents */
        if (!downgrade_exponents(s)) {
            extract_exponents(s);
            encode_exponents(s);
            group_exponents(s);
            ret = compute_bit_allocation(s);
            continue;
        }

        /* fallback 2: reduce bandwidth */
        /* only do this if the user has not specified a specific cutoff
           frequency */
        if (!s->cutoff && !reduce_bandwidth(s, 0)) {
            process_exponents(s);
            ret = compute_bit_allocation(s);
            continue;
        }

        /* fallbacks were not enough... */
        break;
    }

    return ret;
}


/**
 * Symmetric quantization on 'levels' levels.
 */
static inline int sym_quant(int c, int e, int levels)
{
    int v;

    if (c >= 0) {
        v = (levels * (c << e)) >> 24;
        v = (v + 1) >> 1;
        v = (levels >> 1) + v;
    } else {
        v = (levels * ((-c) << e)) >> 24;
        v = (v + 1) >> 1;
        v = (levels >> 1) - v;
    }
    assert(v >= 0 && v < levels);
    return v;
}


/**
 * Asymmetric quantization on 2^qbits levels.
 */
static inline int asym_quant(int c, int e, int qbits)
{
    int lshift, m, v;

    lshift = e + qbits - 24;
    if (lshift >= 0)
        v = c << lshift;
    else
        v = c >> (-lshift);
    /* rounding */
    v = (v + 1) >> 1;
    m = (1 << (qbits-1));
    if (v >= m)
        v = m - 1;
    assert(v >= -m);
    return v & ((1 << qbits)-1);
}


/**
 * Quantize a set of mantissas for a single channel in a single block.
 */
static void quantize_mantissas_blk_ch(AC3EncodeContext *s, int32_t *fixed_coef,
                                      int8_t exp_shift, uint8_t *exp,
                                      uint8_t *bap, uint16_t *qmant, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        int v;
        int c = fixed_coef[i];
        int e = exp[i] - exp_shift;
        int b = bap[i];
        switch (b) {
        case 0:
            v = 0;
            break;
        case 1:
            v = sym_quant(c, e, 3);
            switch (s->mant1_cnt) {
            case 0:
                s->qmant1_ptr = &qmant[i];
                v = 9 * v;
                s->mant1_cnt = 1;
                break;
            case 1:
                *s->qmant1_ptr += 3 * v;
                s->mant1_cnt = 2;
                v = 128;
                break;
            default:
                *s->qmant1_ptr += v;
                s->mant1_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 2:
            v = sym_quant(c, e, 5);
            switch (s->mant2_cnt) {
            case 0:
                s->qmant2_ptr = &qmant[i];
                v = 25 * v;
                s->mant2_cnt = 1;
                break;
            case 1:
                *s->qmant2_ptr += 5 * v;
                s->mant2_cnt = 2;
                v = 128;
                break;
            default:
                *s->qmant2_ptr += v;
                s->mant2_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 3:
            v = sym_quant(c, e, 7);
            break;
        case 4:
            v = sym_quant(c, e, 11);
            switch (s->mant4_cnt) {
            case 0:
                s->qmant4_ptr = &qmant[i];
                v = 11 * v;
                s->mant4_cnt = 1;
                break;
            default:
                *s->qmant4_ptr += v;
                s->mant4_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 5:
            v = sym_quant(c, e, 15);
            break;
        case 14:
            v = asym_quant(c, e, 14);
            break;
        case 15:
            v = asym_quant(c, e, 16);
            break;
        default:
            v = asym_quant(c, e, b - 1);
            break;
        }
        qmant[i] = v;
    }
}


/**
 * Quantize mantissas using coefficients, exponents, and bit allocation pointers.
 */
static void quantize_mantissas(AC3EncodeContext *s)
{
    int blk, ch;


    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        s->mant1_cnt  = s->mant2_cnt  = s->mant4_cnt  = 0;
        s->qmant1_ptr = s->qmant2_ptr = s->qmant4_ptr = NULL;

        for (ch = 0; ch < s->channels; ch++) {
            quantize_mantissas_blk_ch(s, block->fixed_coef[ch], block->exp_shift[ch],
                                      block->exp[ch], block->bap[ch],
                                      block->qmant[ch], s->nb_coefs[ch]);
        }
    }
}


/**
 * Write the AC-3 frame header to the output bitstream.
 */
static void output_frame_header(AC3EncodeContext *s)
{
    put_bits(&s->pb, 16, 0x0b77);   /* frame header */
    put_bits(&s->pb, 16, 0);        /* crc1: will be filled later */
    put_bits(&s->pb, 2,  s->bit_alloc.sr_code);
    put_bits(&s->pb, 6,  s->frame_size_code + (s->frame_size - s->frame_size_min) / 2);
    put_bits(&s->pb, 5,  s->bitstream_id);
    put_bits(&s->pb, 3,  s->bitstream_mode);
    put_bits(&s->pb, 3,  s->channel_mode);
    if ((s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO)
        put_bits(&s->pb, 2, 1);     /* XXX -4.5 dB */
    if (s->channel_mode & 0x04)
        put_bits(&s->pb, 2, 1);     /* XXX -6 dB */
    if (s->channel_mode == AC3_CHMODE_STEREO)
        put_bits(&s->pb, 2, 0);     /* surround not indicated */
    put_bits(&s->pb, 1, s->lfe_on); /* LFE */
    put_bits(&s->pb, 5, 31);        /* dialog norm: -31 db */
    put_bits(&s->pb, 1, 0);         /* no compression control word */
    put_bits(&s->pb, 1, 0);         /* no lang code */
    put_bits(&s->pb, 1, 0);         /* no audio production info */
    put_bits(&s->pb, 1, 0);         /* no copyright */
    put_bits(&s->pb, 1, 1);         /* original bitstream */
    put_bits(&s->pb, 1, 0);         /* no time code 1 */
    put_bits(&s->pb, 1, 0);         /* no time code 2 */
    put_bits(&s->pb, 1, 0);         /* no additional bit stream info */
}


/**
 * Write one audio block to the output bitstream.
 */
static void output_audio_block(AC3EncodeContext *s, int blk)
{
    int ch, i, baie, rbnd;
    AC3Block *block = &s->blocks[blk];

    /* block switching */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 1, 0);

    /* dither flags */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 1, 1);

    /* dynamic range codes */
    put_bits(&s->pb, 1, 0);

    /* channel coupling */
    if (!blk) {
        put_bits(&s->pb, 1, 1); /* coupling strategy present */
        put_bits(&s->pb, 1, 0); /* no coupling strategy */
    } else {
        put_bits(&s->pb, 1, 0); /* no new coupling strategy */
    }

    /* stereo rematrixing */
    if (s->channel_mode == AC3_CHMODE_STEREO) {
        put_bits(&s->pb, 1, block->new_rematrixing_strategy);
        if (block->new_rematrixing_strategy) {
            /* rematrixing flags */
            for (rbnd = 0; rbnd < 4; rbnd++)
                put_bits(&s->pb, 1, block->rematrixing_flags[rbnd]);
        }
    }

    /* exponent strategy */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 2, s->exp_strategy[ch][blk]);
    if (s->lfe_on)
        put_bits(&s->pb, 1, s->exp_strategy[s->lfe_channel][blk]);

    /* bandwidth */
    for (ch = 0; ch < s->fbw_channels; ch++) {
        if (s->exp_strategy[ch][blk] != EXP_REUSE)
            put_bits(&s->pb, 6, s->bandwidth_code[ch]);
    }

    /* exponents */
    for (ch = 0; ch < s->channels; ch++) {
        int nb_groups;

        if (s->exp_strategy[ch][blk] == EXP_REUSE)
            continue;

        /* DC exponent */
        put_bits(&s->pb, 4, block->grouped_exp[ch][0]);

        /* exponent groups */
        nb_groups = exponent_group_tab[s->exp_strategy[ch][blk]-1][s->nb_coefs[ch]];
        for (i = 1; i <= nb_groups; i++)
            put_bits(&s->pb, 7, block->grouped_exp[ch][i]);

        /* gain range info */
        if (ch != s->lfe_channel)
            put_bits(&s->pb, 2, 0);
    }

    /* bit allocation info */
    baie = (blk == 0);
    put_bits(&s->pb, 1, baie);
    if (baie) {
        put_bits(&s->pb, 2, s->slow_decay_code);
        put_bits(&s->pb, 2, s->fast_decay_code);
        put_bits(&s->pb, 2, s->slow_gain_code);
        put_bits(&s->pb, 2, s->db_per_bit_code);
        put_bits(&s->pb, 3, s->floor_code);
    }

    /* snr offset */
    put_bits(&s->pb, 1, baie);
    if (baie) {
        put_bits(&s->pb, 6, s->coarse_snr_offset);
        for (ch = 0; ch < s->channels; ch++) {
            put_bits(&s->pb, 4, s->fine_snr_offset[ch]);
            put_bits(&s->pb, 3, s->fast_gain_code[ch]);
        }
    }

    put_bits(&s->pb, 1, 0); /* no delta bit allocation */
    put_bits(&s->pb, 1, 0); /* no data to skip */

    /* mantissas */
    for (ch = 0; ch < s->channels; ch++) {
        int b, q;
        for (i = 0; i < s->nb_coefs[ch]; i++) {
            q = block->qmant[ch][i];
            b = block->bap[ch][i];
            switch (b) {
            case 0:                                         break;
            case 1: if (q != 128) put_bits(&s->pb,   5, q); break;
            case 2: if (q != 128) put_bits(&s->pb,   7, q); break;
            case 3:               put_bits(&s->pb,   3, q); break;
            case 4: if (q != 128) put_bits(&s->pb,   7, q); break;
            case 14:              put_bits(&s->pb,  14, q); break;
            case 15:              put_bits(&s->pb,  16, q); break;
            default:              put_bits(&s->pb, b-1, q); break;
            }
        }
    }
}


/** CRC-16 Polynomial */
#define CRC16_POLY ((1 << 0) | (1 << 2) | (1 << 15) | (1 << 16))


static unsigned int mul_poly(unsigned int a, unsigned int b, unsigned int poly)
{
    unsigned int c;

    c = 0;
    while (a) {
        if (a & 1)
            c ^= b;
        a = a >> 1;
        b = b << 1;
        if (b & (1 << 16))
            b ^= poly;
    }
    return c;
}


static unsigned int pow_poly(unsigned int a, unsigned int n, unsigned int poly)
{
    unsigned int r;
    r = 1;
    while (n) {
        if (n & 1)
            r = mul_poly(r, a, poly);
        a = mul_poly(a, a, poly);
        n >>= 1;
    }
    return r;
}


/**
 * Fill the end of the frame with 0's and compute the two CRCs.
 */
static void output_frame_end(AC3EncodeContext *s)
{
    const AVCRC *crc_ctx = av_crc_get_table(AV_CRC_16_ANSI);
    int frame_size_58, pad_bytes, crc1, crc2_partial, crc2, crc_inv;
    uint8_t *frame;

    frame_size_58 = ((s->frame_size >> 2) + (s->frame_size >> 4)) << 1;

    /* pad the remainder of the frame with zeros */
    flush_put_bits(&s->pb);
    frame = s->pb.buf;
    pad_bytes = s->frame_size - (put_bits_ptr(&s->pb) - frame) - 2;
    assert(pad_bytes >= 0);
    if (pad_bytes > 0)
        memset(put_bits_ptr(&s->pb), 0, pad_bytes);

    /* compute crc1 */
    /* this is not so easy because it is at the beginning of the data... */
    crc1    = av_bswap16(av_crc(crc_ctx, 0, frame + 4, frame_size_58 - 4));
    crc_inv = s->crc_inv[s->frame_size > s->frame_size_min];
    crc1    = mul_poly(crc_inv, crc1, CRC16_POLY);
    AV_WB16(frame + 2, crc1);

    /* compute crc2 */
    crc2_partial = av_crc(crc_ctx, 0, frame + frame_size_58,
                          s->frame_size - frame_size_58 - 3);
    crc2 = av_crc(crc_ctx, crc2_partial, frame + s->frame_size - 3, 1);
    /* ensure crc2 does not match sync word by flipping crcrsv bit if needed */
    if (crc2 == 0x770B) {
        frame[s->frame_size - 3] ^= 0x1;
        crc2 = av_crc(crc_ctx, crc2_partial, frame + s->frame_size - 3, 1);
    }
    crc2 = av_bswap16(crc2);
    AV_WB16(frame + s->frame_size - 2, crc2);
}


/**
 * Write the frame to the output bitstream.
 */
static void output_frame(AC3EncodeContext *s, unsigned char *frame)
{
    int blk;

    init_put_bits(&s->pb, frame, AC3_MAX_CODED_FRAME_SIZE);

    output_frame_header(s);

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
        output_audio_block(s, blk);

    output_frame_end(s);
}


/**
 * Encode a single AC-3 frame.
 */
static int ac3_encode_frame(AVCodecContext *avctx, unsigned char *frame,
                            int buf_size, void *data)
{
    AC3EncodeContext *s = avctx->priv_data;
    const SampleType *samples = data;
    int ret;

    if (s->bit_alloc.sr_code == 1)
        adjust_frame_size(s);

    deinterleave_input_samples(s, samples);

    apply_mdct(s);

    compute_rematrixing_strategy(s);

    scale_coefficients(s);

    apply_rematrixing(s);

    process_exponents(s);

    ret = compute_bit_allocation(s);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Bit allocation failed. Try increasing the bitrate.\n");
        return ret;
    }

    quantize_mantissas(s);

    output_frame(s, frame);

    return s->frame_size;
}


/**
 * Finalize encoding and free any memory allocated by the encoder.
 */
static av_cold int ac3_encode_close(AVCodecContext *avctx)
{
    int blk, ch;
    AC3EncodeContext *s = avctx->priv_data;

    for (ch = 0; ch < s->channels; ch++)
        av_freep(&s->planar_samples[ch]);
    av_freep(&s->planar_samples);
    av_freep(&s->bap_buffer);
    av_freep(&s->bap1_buffer);
    av_freep(&s->mdct_coef_buffer);
    av_freep(&s->fixed_coef_buffer);
    av_freep(&s->exp_buffer);
    av_freep(&s->grouped_exp_buffer);
    av_freep(&s->psd_buffer);
    av_freep(&s->band_psd_buffer);
    av_freep(&s->mask_buffer);
    av_freep(&s->qmant_buffer);
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        av_freep(&block->bap);
        av_freep(&block->mdct_coef);
        av_freep(&block->fixed_coef);
        av_freep(&block->exp);
        av_freep(&block->grouped_exp);
        av_freep(&block->psd);
        av_freep(&block->band_psd);
        av_freep(&block->mask);
        av_freep(&block->qmant);
    }

    mdct_end(&s->mdct);

    av_freep(&avctx->coded_frame);
    return 0;
}


/**
 * Set channel information during initialization.
 */
static av_cold int set_channel_info(AC3EncodeContext *s, int channels,
                                    int64_t *channel_layout)
{
    int ch_layout;

    if (channels < 1 || channels > AC3_MAX_CHANNELS)
        return AVERROR(EINVAL);
    if ((uint64_t)*channel_layout > 0x7FF)
        return AVERROR(EINVAL);
    ch_layout = *channel_layout;
    if (!ch_layout)
        ch_layout = avcodec_guess_channel_layout(channels, CODEC_ID_AC3, NULL);
    if (av_get_channel_layout_nb_channels(ch_layout) != channels)
        return AVERROR(EINVAL);

    s->lfe_on       = !!(ch_layout & AV_CH_LOW_FREQUENCY);
    s->channels     = channels;
    s->fbw_channels = channels - s->lfe_on;
    s->lfe_channel  = s->lfe_on ? s->fbw_channels : -1;
    if (s->lfe_on)
        ch_layout -= AV_CH_LOW_FREQUENCY;

    switch (ch_layout) {
    case AV_CH_LAYOUT_MONO:           s->channel_mode = AC3_CHMODE_MONO;   break;
    case AV_CH_LAYOUT_STEREO:         s->channel_mode = AC3_CHMODE_STEREO; break;
    case AV_CH_LAYOUT_SURROUND:       s->channel_mode = AC3_CHMODE_3F;     break;
    case AV_CH_LAYOUT_2_1:            s->channel_mode = AC3_CHMODE_2F1R;   break;
    case AV_CH_LAYOUT_4POINT0:        s->channel_mode = AC3_CHMODE_3F1R;   break;
    case AV_CH_LAYOUT_QUAD:
    case AV_CH_LAYOUT_2_2:            s->channel_mode = AC3_CHMODE_2F2R;   break;
    case AV_CH_LAYOUT_5POINT0:
    case AV_CH_LAYOUT_5POINT0_BACK:   s->channel_mode = AC3_CHMODE_3F2R;   break;
    default:
        return AVERROR(EINVAL);
    }

    s->channel_map  = ff_ac3_enc_channel_map[s->channel_mode][s->lfe_on];
    *channel_layout = ch_layout;
    if (s->lfe_on)
        *channel_layout |= AV_CH_LOW_FREQUENCY;

    return 0;
}


static av_cold int validate_options(AVCodecContext *avctx, AC3EncodeContext *s)
{
    int i, ret;

    /* validate channel layout */
    if (!avctx->channel_layout) {
        av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The "
                                      "encoder will guess the layout, but it "
                                      "might be incorrect.\n");
    }
    ret = set_channel_info(s, avctx->channels, &avctx->channel_layout);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "invalid channel layout\n");
        return ret;
    }

    /* validate sample rate */
    for (i = 0; i < 9; i++) {
        if ((ff_ac3_sample_rate_tab[i / 3] >> (i % 3)) == avctx->sample_rate)
            break;
    }
    if (i == 9) {
        av_log(avctx, AV_LOG_ERROR, "invalid sample rate\n");
        return AVERROR(EINVAL);
    }
    s->sample_rate        = avctx->sample_rate;
    s->bit_alloc.sr_shift = i % 3;
    s->bit_alloc.sr_code  = i / 3;

    /* validate bit rate */
    for (i = 0; i < 19; i++) {
        if ((ff_ac3_bitrate_tab[i] >> s->bit_alloc.sr_shift)*1000 == avctx->bit_rate)
            break;
    }
    if (i == 19) {
        av_log(avctx, AV_LOG_ERROR, "invalid bit rate\n");
        return AVERROR(EINVAL);
    }
    s->bit_rate        = avctx->bit_rate;
    s->frame_size_code = i << 1;

    /* validate cutoff */
    if (avctx->cutoff < 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid cutoff frequency\n");
        return AVERROR(EINVAL);
    }
    s->cutoff = avctx->cutoff;
    if (s->cutoff > (s->sample_rate >> 1))
        s->cutoff = s->sample_rate >> 1;

    return 0;
}


/**
 * Set bandwidth for all channels.
 * The user can optionally supply a cutoff frequency. Otherwise an appropriate
 * default value will be used.
 */
static av_cold void set_bandwidth(AC3EncodeContext *s)
{
    int ch, bw_code;

    if (s->cutoff) {
        /* calculate bandwidth based on user-specified cutoff frequency */
        int fbw_coeffs;
        fbw_coeffs     = s->cutoff * 2 * AC3_MAX_COEFS / s->sample_rate;
        bw_code        = av_clip((fbw_coeffs - 73) / 3, 0, 60);
    } else {
        /* use default bandwidth setting */
        /* XXX: should compute the bandwidth according to the frame
           size, so that we avoid annoying high frequency artifacts */
        bw_code = 50;
    }

    /* set number of coefficients for each channel */
    for (ch = 0; ch < s->fbw_channels; ch++) {
        s->bandwidth_code[ch] = bw_code;
        s->nb_coefs[ch]       = bw_code * 3 + 73;
    }
    if (s->lfe_on)
        s->nb_coefs[s->lfe_channel] = 7; /* LFE channel always has 7 coefs */
}


static av_cold int allocate_buffers(AVCodecContext *avctx)
{
    int blk, ch;
    AC3EncodeContext *s = avctx->priv_data;

    FF_ALLOC_OR_GOTO(avctx, s->planar_samples, s->channels * sizeof(*s->planar_samples),
                     alloc_fail);
    for (ch = 0; ch < s->channels; ch++) {
        FF_ALLOCZ_OR_GOTO(avctx, s->planar_samples[ch],
                          (AC3_FRAME_SIZE+AC3_BLOCK_SIZE) * sizeof(**s->planar_samples),
                          alloc_fail);
    }
    FF_ALLOC_OR_GOTO(avctx, s->bap_buffer,  AC3_MAX_BLOCKS * s->channels *
                     AC3_MAX_COEFS * sizeof(*s->bap_buffer),  alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->bap1_buffer, AC3_MAX_BLOCKS * s->channels *
                     AC3_MAX_COEFS * sizeof(*s->bap1_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->mdct_coef_buffer, AC3_MAX_BLOCKS * s->channels *
                     AC3_MAX_COEFS * sizeof(*s->mdct_coef_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->exp_buffer, AC3_MAX_BLOCKS * s->channels *
                     AC3_MAX_COEFS * sizeof(*s->exp_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->grouped_exp_buffer, AC3_MAX_BLOCKS * s->channels *
                     128 * sizeof(*s->grouped_exp_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->psd_buffer, AC3_MAX_BLOCKS * s->channels *
                     AC3_MAX_COEFS * sizeof(*s->psd_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->band_psd_buffer, AC3_MAX_BLOCKS * s->channels *
                     64 * sizeof(*s->band_psd_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->mask_buffer, AC3_MAX_BLOCKS * s->channels *
                     64 * sizeof(*s->mask_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->qmant_buffer, AC3_MAX_BLOCKS * s->channels *
                     AC3_MAX_COEFS * sizeof(*s->qmant_buffer), alloc_fail);
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        FF_ALLOC_OR_GOTO(avctx, block->bap, s->channels * sizeof(*block->bap),
                         alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->mdct_coef, s->channels * sizeof(*block->mdct_coef),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->exp, s->channels * sizeof(*block->exp),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->grouped_exp, s->channels * sizeof(*block->grouped_exp),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->psd, s->channels * sizeof(*block->psd),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->band_psd, s->channels * sizeof(*block->band_psd),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->mask, s->channels * sizeof(*block->mask),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->qmant, s->channels * sizeof(*block->qmant),
                          alloc_fail);

        for (ch = 0; ch < s->channels; ch++) {
            /* arrangement: block, channel, coeff */
            block->bap[ch]         = &s->bap_buffer        [AC3_MAX_COEFS * (blk * s->channels + ch)];
            block->mdct_coef[ch]   = &s->mdct_coef_buffer  [AC3_MAX_COEFS * (blk * s->channels + ch)];
            block->grouped_exp[ch] = &s->grouped_exp_buffer[128           * (blk * s->channels + ch)];
            block->psd[ch]         = &s->psd_buffer        [AC3_MAX_COEFS * (blk * s->channels + ch)];
            block->band_psd[ch]    = &s->band_psd_buffer   [64            * (blk * s->channels + ch)];
            block->mask[ch]        = &s->mask_buffer       [64            * (blk * s->channels + ch)];
            block->qmant[ch]       = &s->qmant_buffer      [AC3_MAX_COEFS * (blk * s->channels + ch)];

            /* arrangement: channel, block, coeff */
            block->exp[ch]         = &s->exp_buffer        [AC3_MAX_COEFS * (AC3_MAX_BLOCKS * ch + blk)];
        }
    }

    if (CONFIG_AC3ENC_FLOAT) {
        FF_ALLOC_OR_GOTO(avctx, s->fixed_coef_buffer, AC3_MAX_BLOCKS * s->channels *
                         AC3_MAX_COEFS * sizeof(*s->fixed_coef_buffer), alloc_fail);
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            FF_ALLOCZ_OR_GOTO(avctx, block->fixed_coef, s->channels *
                              sizeof(*block->fixed_coef), alloc_fail);
            for (ch = 0; ch < s->channels; ch++)
                block->fixed_coef[ch] = &s->fixed_coef_buffer[AC3_MAX_COEFS * (blk * s->channels + ch)];
        }
    } else {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            FF_ALLOCZ_OR_GOTO(avctx, block->fixed_coef, s->channels *
                              sizeof(*block->fixed_coef), alloc_fail);
            for (ch = 0; ch < s->channels; ch++)
                block->fixed_coef[ch] = (int32_t *)block->mdct_coef[ch];
        }
    }

    return 0;
alloc_fail:
    return AVERROR(ENOMEM);
}


/**
 * Initialize the encoder.
 */
static av_cold int ac3_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    int ret, frame_size_58;

    avctx->frame_size = AC3_FRAME_SIZE;

    ac3_common_init();

    ret = validate_options(avctx, s);
    if (ret)
        return ret;

    s->bitstream_id   = 8 + s->bit_alloc.sr_shift;
    s->bitstream_mode = 0; /* complete main audio service */

    s->frame_size_min  = 2 * ff_ac3_frame_size_tab[s->frame_size_code][s->bit_alloc.sr_code];
    s->bits_written    = 0;
    s->samples_written = 0;
    s->frame_size      = s->frame_size_min;

    /* calculate crc_inv for both possible frame sizes */
    frame_size_58 = (( s->frame_size    >> 2) + ( s->frame_size    >> 4)) << 1;
    s->crc_inv[0] = pow_poly((CRC16_POLY >> 1), (8 * frame_size_58) - 16, CRC16_POLY);
    if (s->bit_alloc.sr_code == 1) {
        frame_size_58 = (((s->frame_size+2) >> 2) + ((s->frame_size+2) >> 4)) << 1;
        s->crc_inv[1] = pow_poly((CRC16_POLY >> 1), (8 * frame_size_58) - 16, CRC16_POLY);
    }

    set_bandwidth(s);

    rematrixing_init(s);

    exponent_init(s);

    bit_alloc_init(s);

    ret = mdct_init(avctx, &s->mdct, 9);
    if (ret)
        goto init_fail;

    ret = allocate_buffers(avctx);
    if (ret)
        goto init_fail;

    avctx->coded_frame= avcodec_alloc_frame();

    dsputil_init(&s->dsp, avctx);

    return 0;
init_fail:
    ac3_encode_close(avctx);
    return ret;
}
