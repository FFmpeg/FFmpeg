/*
 * AC-3 encoder float/fixed template
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2011 Justin Ruggles <justin.ruggles@gmail.com>
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
 * AC-3 encoder float/fixed template
 */

#include "config_components.h"

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/mem_internal.h"

#include "audiodsp.h"
#include "ac3enc.h"
#include "eac3enc.h"

#if AC3ENC_FLOAT
#define RENAME(element) element ## _float
#else
#define RENAME(element) element ## _fixed
#endif

/* power ratios for the -42 dB band floor and the 12 dB difference margin. */
#define PHASE_BAND_ENERGY_DENOMINATOR (1 << 14)
#define PHASE_DIFF_ENERGY_FACTOR       (1 << 4)

/*
 * Apply the MDCT to input samples to generate frequency coefficients.
 * This applies the KBD window and normalizes the input to reduce precision
 * loss due to fixed-point calculations.
 */
static void apply_mdct(AC3EncodeContext *s, uint8_t * const *samples)
{
    av_assert1(s->num_blocks > 0);

    for (int ch = 0; ch < s->channels; ch++) {
        const SampleType *input_samples0 = (const SampleType*)s->planar_samples[ch];
        /* Reorder channels from native order to AC-3 order. */
        const SampleType *input_samples1 = (const SampleType*)samples[s->channel_map[ch]];
        int blk = 0;

        do {
            AC3Block *block = &s->blocks[blk];
            SampleType *windowed_samples = s->RENAME(windowed_samples);

            s->fdsp->vector_fmul(windowed_samples, input_samples0,
                                 s->RENAME(mdct_window), AC3_BLOCK_SIZE);
            s->fdsp->vector_fmul_reverse(windowed_samples + AC3_BLOCK_SIZE,
                                         input_samples1,
                                         s->RENAME(mdct_window), AC3_BLOCK_SIZE);

            s->tx_fn(s->tx, block->mdct_coef[ch+1],
                     windowed_samples, sizeof(*windowed_samples));
            input_samples0  = input_samples1;
            input_samples1 += AC3_BLOCK_SIZE;
        } while (++blk < s->num_blocks);

        /* Store last 256 samples of current frame */
        memcpy(s->planar_samples[ch], input_samples0,
               AC3_BLOCK_SIZE * sizeof(*input_samples0));
    }
}


/*
 * Calculate coupling channel and coupling coordinates.
 */
static void apply_channel_coupling(AC3EncodeContext *s)
{
    LOCAL_ALIGNED_32(CoefType, cpl_coords,      [AC3_MAX_BLOCKS], [AC3_MAX_CHANNELS][16]);
#if AC3ENC_FLOAT
    LOCAL_ALIGNED_32(int32_t, fixed_cpl_coords, [AC3_MAX_BLOCKS], [AC3_MAX_CHANNELS][16]);
#else
    int32_t (*fixed_cpl_coords)[AC3_MAX_CHANNELS][16] = cpl_coords;
#endif
    int av_uninit(blk), ch, bnd, i, j;
    CoefSumType energy[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][16] = {{{0}}};
    int cpl_start, num_cpl_coefs;

    s->phase_flags_in_use = 0;
    memset(cpl_coords,       0, AC3_MAX_BLOCKS * sizeof(*cpl_coords));
#if AC3ENC_FLOAT
    memset(fixed_cpl_coords, 0, AC3_MAX_BLOCKS * sizeof(*cpl_coords));
#endif

    /* align start to 16-byte boundary. align length to multiple of 32.
        note: coupling start bin % 4 will always be 1 */
    cpl_start     = s->start_freq[CPL_CH] - 1;
    num_cpl_coefs = FFALIGN(s->num_cpl_subbands * 12 + 1, 32);
    cpl_start     = FFMIN(256, cpl_start + num_cpl_coefs) - num_cpl_coefs;

    if (s->channel_mode == AC3_CHMODE_STEREO) {
        uint8_t phase_flags[AC3_MAX_CPL_BANDS];
        int cpl_blocks = 0;

        /* use a single phase strategy for the frame. a difference carrier is
         * selected only when it has at least 12 dB more energy and the band
         * is within 42 dB of the coded channel energy in every coupling
         * block. */
        memset(phase_flags, 1, s->num_cpl_bands);
        for (blk = 0; blk < s->num_blocks; blk++) {
            AC3Block *block = &s->blocks[blk];
            CoefSumType sum[AC3_MAX_CPL_BANDS][4];
            /* the DSP also returns sum and difference energy in slots 2/3. */
            CoefSumType block_energy[4];
            CoefSumType max_energy;

            if (!block->cpl_in_use)
                continue;
            cpl_blocks++;
            sum_square_butterfly(s, block_energy,
                                 block->mdct_coef[1], block->mdct_coef[2],
                                 s->start_freq[CPL_CH]);
            i = s->start_freq[CPL_CH];
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                sum_square_butterfly(s, sum[bnd],
                                     block->mdct_coef[1] + i,
                                     block->mdct_coef[2] + i,
                                     s->cpl_band_sizes[bnd]);
                /* reused by the coupling coordinate calculation below. */
                energy[blk][1][bnd] = sum[bnd][0];
                energy[blk][2][bnd] = sum[bnd][1];
                block_energy[0] += sum[bnd][0];
                block_energy[1] += sum[bnd][1];
                i += s->cpl_band_sizes[bnd];
            }
            max_energy = FFMAX(block_energy[0], block_energy[1]);
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                CoefSumType band_energy = FFMAX(sum[bnd][0], sum[bnd][1]);
                int significant = band_energy >
                    max_energy / PHASE_BAND_ENERGY_DENOMINATOR;

                phase_flags[bnd] &= significant &&
                    sum[bnd][3] / PHASE_DIFF_ENERGY_FACTOR > sum[bnd][2];
            }
        }
        for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
            int phase = phase_flags[bnd] && cpl_blocks;

            s->phase_flags[bnd] = phase;
            s->phase_flags_in_use |= phase;
        }
    }

    /* calculate coupling channel from fbw channels */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        CoefType *cpl_coef = &block->mdct_coef[CPL_CH][cpl_start];
        if (!block->cpl_in_use)
            continue;
        memset(cpl_coef, 0, num_cpl_coefs * sizeof(*cpl_coef));
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            CoefType *ch_coef = &block->mdct_coef[ch][cpl_start];
            if (!block->channel_in_cpl[ch])
                continue;
            for (i = 0; i < num_cpl_coefs; i++)
                cpl_coef[i] += ch_coef[i];
        }

        if (s->channel_mode == AC3_CHMODE_STEREO) {
            CoefType *left  = block->mdct_coef[1];
            CoefType *right = block->mdct_coef[2];

            i = s->start_freq[CPL_CH];
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                if (s->phase_flags[bnd]) {
                    for (j = 0; j < s->cpl_band_sizes[bnd]; j++)
                        block->mdct_coef[CPL_CH][i + j] = left[i + j] - right[i + j];
                }
                i += s->cpl_band_sizes[bnd];
            }
        }

        /* coefficients must be clipped in order to be encoded */
        clip_coefficients(&s->adsp, cpl_coef, num_cpl_coefs);
    }

    /* calculate energy in each band in coupling channel and each fbw channel */
    /* TODO: possibly use SIMD to speed up energy calculation */
    bnd = 0;
    i = s->start_freq[CPL_CH];
    while (i < s->cpl_end_freq) {
        int band_size = s->cpl_band_sizes[bnd];
        /* stereo channel energies were filled during phase analysis above. */
        int last_ch = s->channel_mode == AC3_CHMODE_STEREO ?
                      CPL_CH : s->fbw_channels;
        for (ch = CPL_CH; ch <= last_ch; ch++) {
            for (blk = 0; blk < s->num_blocks; blk++) {
                AC3Block *block = &s->blocks[blk];
                if (!block->cpl_in_use || (ch > CPL_CH && !block->channel_in_cpl[ch]))
                    continue;
                for (j = 0; j < band_size; j++) {
                    CoefType v = block->mdct_coef[ch][i+j];
                    MAC_COEF(energy[blk][ch][bnd], v, v);
                }
            }
        }
        i += band_size;
        bnd++;
    }

    /* calculate coupling coordinates for all blocks for all channels */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block  = &s->blocks[blk];
        if (!block->cpl_in_use)
            continue;
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (!block->channel_in_cpl[ch])
                continue;
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                cpl_coords[blk][ch][bnd] = calc_cpl_coord(energy[blk][ch][bnd],
                                                          energy[blk][CPL_CH][bnd]);
            }
        }
    }

    /* determine which blocks to send new coupling coordinates for */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block  = &s->blocks[blk];
        AC3Block *block0 = blk ? &s->blocks[blk-1] : NULL;

        memset(block->new_cpl_coords, 0, sizeof(block->new_cpl_coords));

        if (block->cpl_in_use) {
            /* send new coordinates if this is the first block, if previous
             * block did not use coupling but this block does, the channels
             * using coupling has changed from the previous block, or the
             * coordinate difference from the last block for any channel is
             * greater than a threshold value. */
            if (blk == 0 || !block0->cpl_in_use) {
                for (ch = 1; ch <= s->fbw_channels; ch++)
                    block->new_cpl_coords[ch] = 1;
            } else {
                for (ch = 1; ch <= s->fbw_channels; ch++) {
                    if (!block->channel_in_cpl[ch])
                        continue;
                    if (!block0->channel_in_cpl[ch]) {
                        block->new_cpl_coords[ch] = 1;
                    } else {
                        CoefSumType coord_diff = 0;
                        for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                            coord_diff += FFABS(cpl_coords[blk-1][ch][bnd] -
                                                cpl_coords[blk  ][ch][bnd]);
                        }
                        coord_diff /= s->num_cpl_bands;
                        if (coord_diff > NEW_CPL_COORD_THRESHOLD)
                            block->new_cpl_coords[ch] = 1;
                    }
                }
            }
        }
    }

    av_assert1(s->fbw_channels > 0);

    /* calculate final coupling coordinates, taking into account reusing of
       coordinates in successive blocks */
    for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
        blk = 0;
        while (blk < s->num_blocks) {
            int av_uninit(blk1);
            AC3Block *block  = &s->blocks[blk];

            if (!block->cpl_in_use) {
                blk++;
                continue;
            }

            for (ch = 1; ch <= s->fbw_channels; ch++) {
                CoefSumType energy_ch, energy_cpl;
                if (!block->channel_in_cpl[ch])
                    continue;
                energy_cpl = energy[blk][CPL_CH][bnd];
                energy_ch = energy[blk][ch][bnd];
                blk1 = blk+1;
                while (blk1 < s->num_blocks && !s->blocks[blk1].new_cpl_coords[ch]) {
                    if (s->blocks[blk1].cpl_in_use) {
                        energy_cpl += energy[blk1][CPL_CH][bnd];
                        energy_ch += energy[blk1][ch][bnd];
                    }
                    blk1++;
                }
                cpl_coords[blk][ch][bnd] = calc_cpl_coord(energy_ch, energy_cpl);
            }
            blk = blk1;
        }
    }

    /* calculate exponents/mantissas for coupling coordinates */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (!block->cpl_in_use)
            continue;

#if AC3ENC_FLOAT
        s->ac3dsp.float_to_fixed24(fixed_cpl_coords[blk][1],
                                   cpl_coords[blk][1],
                                   s->fbw_channels * 16);
#endif
        s->ac3dsp.extract_exponents(block->cpl_coord_exp[1],
                                    fixed_cpl_coords[blk][1],
                                    s->fbw_channels * 16);

        for (ch = 1; ch <= s->fbw_channels; ch++) {
            int bnd, min_exp, max_exp, master_exp;

            if (!block->new_cpl_coords[ch])
                continue;

            /* determine master exponent */
            min_exp = max_exp = block->cpl_coord_exp[ch][0];
            for (bnd = 1; bnd < s->num_cpl_bands; bnd++) {
                int exp = block->cpl_coord_exp[ch][bnd];
                min_exp = FFMIN(exp, min_exp);
                max_exp = FFMAX(exp, max_exp);
            }
            master_exp = ((max_exp - 15) + 2) / 3;
            master_exp = FFMAX(master_exp, 0);
            while (min_exp < master_exp * 3)
                master_exp--;
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                block->cpl_coord_exp[ch][bnd] = av_clip(block->cpl_coord_exp[ch][bnd] -
                                                        master_exp * 3, 0, 15);
            }
            block->cpl_master_exp[ch] = master_exp;

            /* quantize mantissas */
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                int cpl_exp  = block->cpl_coord_exp[ch][bnd];
                int cpl_mant = (fixed_cpl_coords[blk][ch][bnd] << (5 + cpl_exp + master_exp * 3)) >> 24;
                if (cpl_exp == 15)
                    cpl_mant >>= 1;
                else
                    cpl_mant -= 16;

                block->cpl_coord_mant[ch][bnd] = cpl_mant;
            }
        }
    }

    if (AC3ENC_FLOAT && CONFIG_EAC3_ENCODER && s->eac3)
        ff_eac3_set_cpl_states(s);
}


/*
 * Determine rematrixing flags for each block and band.
 */
static void compute_rematrixing_strategy(AC3EncodeContext *s)
{
    int nb_coefs;
    int blk, bnd;
    AC3Block *block, *block0 = NULL;

    if (s->channel_mode != AC3_CHMODE_STEREO)
        return;

    for (blk = 0; blk < s->num_blocks; blk++) {
        block = &s->blocks[blk];
        block->new_rematrixing_strategy = !blk;

        block->num_rematrixing_bands = 4;
        if (block->cpl_in_use) {
            block->num_rematrixing_bands -= (s->start_freq[CPL_CH] <= 61);
            block->num_rematrixing_bands -= (s->start_freq[CPL_CH] == 37);
            if (blk && block->num_rematrixing_bands != block0->num_rematrixing_bands)
                block->new_rematrixing_strategy = 1;
        }
        nb_coefs = FFMIN(block->end_freq[1], block->end_freq[2]);

        if (!s->rematrixing_enabled) {
            block0 = block;
            continue;
        }

        for (bnd = 0; bnd < block->num_rematrixing_bands; bnd++) {
            /* calculate sum of squared coeffs for one band in one block */
            int start = ff_ac3_rematrix_band_tab[bnd];
            int end   = FFMIN(nb_coefs, ff_ac3_rematrix_band_tab[bnd+1]);
            CoefSumType sum[4];
            sum_square_butterfly(s, sum, block->mdct_coef[1] + start,
                                 block->mdct_coef[2] + start, end - start);

            /* compare sums to determine if rematrixing will be used for this band */
            if (FFMIN(sum[2], sum[3]) < FFMIN(sum[0], sum[1]))
                block->rematrixing_flags[bnd] = 1;
            else
                block->rematrixing_flags[bnd] = 0;

            /* determine if new rematrixing flags will be sent */
            if (blk &&
                block->rematrixing_flags[bnd] != block0->rematrixing_flags[bnd]) {
                block->new_rematrixing_strategy = 1;
            }
        }
        block0 = block;
    }
}

static void copy_input_samples(AC3EncodeContext *s, const AVFrame *frame)
{
    int end = frame ? frame->nb_samples : 0;

    /* copy new samples and zero any remaining samples */
    if (frame) {
        av_samples_copy(s->input_samples, frame->extended_data, 0, 0,
                        frame->nb_samples, s->channels,
                        s->avctx->sample_fmt);
    }
    av_samples_set_silence(s->input_samples, end,
                           s->avctx->frame_size - end,
                           s->channels, s->avctx->sample_fmt);
}

static void encode_frame(AC3EncodeContext *s, const AVFrame *frame)
{
    uint8_t **samples;

    if (!frame || frame->nb_samples < s->avctx->frame_size) {
        copy_input_samples(s, frame);
        samples = s->input_samples;
    } else
        samples = frame->extended_data;

    apply_mdct(s, samples);

    s->cpl_on = s->cpl_enabled;
    ff_ac3_compute_coupling_strategy(s);

    if (s->cpl_on)
        apply_channel_coupling(s);

    compute_rematrixing_strategy(s);

#if AC3ENC_FLOAT
    scale_coefficients(s);
#endif
}
