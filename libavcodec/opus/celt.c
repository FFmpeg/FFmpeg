/*
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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

#include <stdint.h>

#include "celt.h"
#include "pvq.h"
#include "tab.h"

void ff_celt_quant_bands(CeltFrame *f, OpusRangeCoder *rc)
{
    float lowband_scratch[8 * 22];
    float norm1[2 * 8 * 100];
    float *norm2 = norm1 + 8 * 100;

    int totalbits = (f->framebits << 3) - f->anticollapse_needed;

    int update_lowband = 1;
    int lowband_offset = 0;

    int i, j;

    for (i = f->start_band; i < f->end_band; i++) {
        uint32_t cm[2] = { (1 << f->blocks) - 1, (1 << f->blocks) - 1 };
        int band_offset = ff_celt_freq_bands[i] << f->size;
        int band_size   = ff_celt_freq_range[i] << f->size;
        float *X = f->block[0].coeffs + band_offset;
        float *Y = (f->channels == 2) ? f->block[1].coeffs + band_offset : NULL;
        float *norm_loc1, *norm_loc2;

        int consumed = opus_rc_tell_frac(rc);
        int effective_lowband = -1;
        int b = 0;

        /* Compute how many bits we want to allocate to this band */
        if (i != f->start_band)
            f->remaining -= consumed;
        f->remaining2 = totalbits - consumed - 1;
        if (i <= f->coded_bands - 1) {
            int curr_balance = f->remaining / FFMIN(3, f->coded_bands-i);
            b = av_clip_uintp2(FFMIN(f->remaining2 + 1, f->pulses[i] + curr_balance), 14);
        }

        if ((ff_celt_freq_bands[i] - ff_celt_freq_range[i] >= ff_celt_freq_bands[f->start_band] ||
            i == f->start_band + 1) && (update_lowband || lowband_offset == 0))
            lowband_offset = i;

        if (i == f->start_band + 1) {
            /* Special Hybrid Folding (RFC 8251 section 9). Copy the first band into
            the second to ensure the second band never has to use the LCG. */
            int count = (ff_celt_freq_range[i] - ff_celt_freq_range[i-1]) << f->size;

            memcpy(&norm1[band_offset], &norm1[band_offset - count], count * sizeof(float));

            if (f->channels == 2)
                memcpy(&norm2[band_offset], &norm2[band_offset - count], count * sizeof(float));
        }

        /* Get a conservative estimate of the collapse_mask's for the bands we're
           going to be folding from. */
        if (lowband_offset != 0 && (f->spread != CELT_SPREAD_AGGRESSIVE ||
                                    f->blocks > 1 || f->tf_change[i] < 0)) {
            int foldstart, foldend;

            /* This ensures we never repeat spectral content within one band */
            effective_lowband = FFMAX(ff_celt_freq_bands[f->start_band],
                                      ff_celt_freq_bands[lowband_offset] - ff_celt_freq_range[i]);
            foldstart = lowband_offset;
            while (ff_celt_freq_bands[--foldstart] > effective_lowband);
            foldend = lowband_offset - 1;
            while (++foldend < i && ff_celt_freq_bands[foldend] < effective_lowband + ff_celt_freq_range[i]);

            cm[0] = cm[1] = 0;
            for (j = foldstart; j < foldend; j++) {
                cm[0] |= f->block[0].collapse_masks[j];
                cm[1] |= f->block[f->channels - 1].collapse_masks[j];
            }
        }

        if (f->dual_stereo && i == f->intensity_stereo) {
            /* Switch off dual stereo to do intensity */
            f->dual_stereo = 0;
            for (j = ff_celt_freq_bands[f->start_band] << f->size; j < band_offset; j++)
                norm1[j] = (norm1[j] + norm2[j]) / 2;
        }

        norm_loc1 = effective_lowband != -1 ? norm1 + (effective_lowband << f->size) : NULL;
        norm_loc2 = effective_lowband != -1 ? norm2 + (effective_lowband << f->size) : NULL;

        if (f->dual_stereo) {
            cm[0] = f->pvq->quant_band(f->pvq, f, rc, i, X, NULL, band_size, b >> 1,
                                       f->blocks, norm_loc1, f->size,
                                       norm1 + band_offset, 0, 1.0f,
                                       lowband_scratch, cm[0]);

            cm[1] = f->pvq->quant_band(f->pvq, f, rc, i, Y, NULL, band_size, b >> 1,
                                       f->blocks, norm_loc2, f->size,
                                       norm2 + band_offset, 0, 1.0f,
                                       lowband_scratch, cm[1]);
        } else {
            cm[0] = f->pvq->quant_band(f->pvq, f, rc, i, X,    Y, band_size, b >> 0,
                                       f->blocks, norm_loc1, f->size,
                                       norm1 + band_offset, 0, 1.0f,
                                       lowband_scratch, cm[0] | cm[1]);
            cm[1] = cm[0];
        }

        f->block[0].collapse_masks[i]               = (uint8_t)cm[0];
        f->block[f->channels - 1].collapse_masks[i] = (uint8_t)cm[1];
        f->remaining += f->pulses[i] + consumed;

        /* Update the folding position only as long as we have 1 bit/sample depth */
        update_lowband = (b > band_size << 3);
    }
}

#define NORMC(bits) ((bits) << (f->channels - 1) << f->size >> 2)

void ff_celt_bitalloc(CeltFrame *f, OpusRangeCoder *rc, int encode)
{
    int i, j, low, high, total, done, bandbits, remaining, tbits_8ths;
    int skip_startband      = f->start_band;
    int skip_bit            = 0;
    int intensitystereo_bit = 0;
    int dualstereo_bit      = 0;
    int dynalloc            = 6;
    int extrabits           = 0;

    int boost[CELT_MAX_BANDS] = { 0 };
    int trim_offset[CELT_MAX_BANDS];
    int threshold[CELT_MAX_BANDS];
    int bits1[CELT_MAX_BANDS];
    int bits2[CELT_MAX_BANDS];

    /* Spread */
    if (opus_rc_tell(rc) + 4 <= f->framebits) {
        if (encode)
            ff_opus_rc_enc_cdf(rc, f->spread, ff_celt_model_spread);
        else
            f->spread = ff_opus_rc_dec_cdf(rc, ff_celt_model_spread);
    } else {
        f->spread = CELT_SPREAD_NORMAL;
    }

    /* Initialize static allocation caps */
    for (i = 0; i < CELT_MAX_BANDS; i++)
        f->caps[i] = NORMC((ff_celt_static_caps[f->size][f->channels - 1][i] + 64) * ff_celt_freq_range[i]);

    /* Band boosts */
    tbits_8ths = f->framebits << 3;
    for (i = f->start_band; i < f->end_band; i++) {
        int quanta = ff_celt_freq_range[i] << (f->channels - 1) << f->size;
        int b_dynalloc = dynalloc;
        int boost_amount = f->alloc_boost[i];
        quanta = FFMIN(quanta << 3, FFMAX(6 << 3, quanta));

        while (opus_rc_tell_frac(rc) + (b_dynalloc << 3) < tbits_8ths && boost[i] < f->caps[i]) {
            int is_boost;
            if (encode) {
                is_boost = boost_amount--;
                ff_opus_rc_enc_log(rc, is_boost, b_dynalloc);
            } else {
                is_boost = ff_opus_rc_dec_log(rc, b_dynalloc);
            }

            if (!is_boost)
                break;

            boost[i]   += quanta;
            tbits_8ths -= quanta;

            b_dynalloc = 1;
        }

        if (boost[i])
            dynalloc = FFMAX(dynalloc - 1, 2);
    }

    /* Allocation trim */
    if (!encode)
        f->alloc_trim = 5;
    if (opus_rc_tell_frac(rc) + (6 << 3) <= tbits_8ths)
        if (encode)
            ff_opus_rc_enc_cdf(rc, f->alloc_trim, ff_celt_model_alloc_trim);
        else
            f->alloc_trim = ff_opus_rc_dec_cdf(rc, ff_celt_model_alloc_trim);

    /* Anti-collapse bit reservation */
    tbits_8ths = (f->framebits << 3) - opus_rc_tell_frac(rc) - 1;
    f->anticollapse_needed = 0;
    if (f->transient && f->size >= 2 && tbits_8ths >= ((f->size + 2) << 3))
        f->anticollapse_needed = 1 << 3;
    tbits_8ths -= f->anticollapse_needed;

    /* Band skip bit reservation */
    if (tbits_8ths >= 1 << 3)
        skip_bit = 1 << 3;
    tbits_8ths -= skip_bit;

    /* Intensity/dual stereo bit reservation */
    if (f->channels == 2) {
        intensitystereo_bit = ff_celt_log2_frac[f->end_band - f->start_band];
        if (intensitystereo_bit <= tbits_8ths) {
            tbits_8ths -= intensitystereo_bit;
            if (tbits_8ths >= 1 << 3) {
                dualstereo_bit = 1 << 3;
                tbits_8ths -= 1 << 3;
            }
        } else {
            intensitystereo_bit = 0;
        }
    }

    /* Trim offsets */
    for (i = f->start_band; i < f->end_band; i++) {
        int trim     = f->alloc_trim - 5 - f->size;
        int band     = ff_celt_freq_range[i] * (f->end_band - i - 1);
        int duration = f->size + 3;
        int scale    = duration + f->channels - 1;

        /* PVQ minimum allocation threshold, below this value the band is
         * skipped */
        threshold[i] = FFMAX(3 * ff_celt_freq_range[i] << duration >> 4,
                             f->channels << 3);

        trim_offset[i] = trim * (band << scale) >> 6;

        if (ff_celt_freq_range[i] << f->size == 1)
            trim_offset[i] -= f->channels << 3;
    }

    /* Bisection */
    low  = 1;
    high = CELT_VECTORS - 1;
    while (low <= high) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (i = f->end_band - 1; i >= f->start_band; i--) {
            bandbits = NORMC(ff_celt_freq_range[i] * ff_celt_static_alloc[center][i]);

            if (bandbits)
                bandbits = FFMAX(bandbits + trim_offset[i], 0);
            bandbits += boost[i];

            if (bandbits >= threshold[i] || done) {
                done = 1;
                total += FFMIN(bandbits, f->caps[i]);
            } else if (bandbits >= f->channels << 3) {
                total += f->channels << 3;
            }
        }

        if (total > tbits_8ths)
            high = center - 1;
        else
            low = center + 1;
    }
    high = low--;

    /* Bisection */
    for (i = f->start_band; i < f->end_band; i++) {
        bits1[i] = NORMC(ff_celt_freq_range[i] * ff_celt_static_alloc[low][i]);
        bits2[i] = high >= CELT_VECTORS ? f->caps[i] :
                   NORMC(ff_celt_freq_range[i] * ff_celt_static_alloc[high][i]);

        if (bits1[i])
            bits1[i] = FFMAX(bits1[i] + trim_offset[i], 0);
        if (bits2[i])
            bits2[i] = FFMAX(bits2[i] + trim_offset[i], 0);

        if (low)
            bits1[i] += boost[i];
        bits2[i] += boost[i];

        if (boost[i])
            skip_startband = i;
        bits2[i] = FFMAX(bits2[i] - bits1[i], 0);
    }

    /* Bisection */
    low  = 0;
    high = 1 << CELT_ALLOC_STEPS;
    for (i = 0; i < CELT_ALLOC_STEPS; i++) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (j = f->end_band - 1; j >= f->start_band; j--) {
            bandbits = bits1[j] + (center * bits2[j] >> CELT_ALLOC_STEPS);

            if (bandbits >= threshold[j] || done) {
                done = 1;
                total += FFMIN(bandbits, f->caps[j]);
            } else if (bandbits >= f->channels << 3)
                total += f->channels << 3;
        }
        if (total > tbits_8ths)
            high = center;
        else
            low = center;
    }

    /* Bisection */
    done = total = 0;
    for (i = f->end_band - 1; i >= f->start_band; i--) {
        bandbits = bits1[i] + (low * bits2[i] >> CELT_ALLOC_STEPS);

        if (bandbits >= threshold[i] || done)
            done = 1;
        else
            bandbits = (bandbits >= f->channels << 3) ?
            f->channels << 3 : 0;

        bandbits     = FFMIN(bandbits, f->caps[i]);
        f->pulses[i] = bandbits;
        total      += bandbits;
    }

    /* Band skipping */
    for (f->coded_bands = f->end_band; ; f->coded_bands--) {
        int allocation;
        j = f->coded_bands - 1;

        if (j == skip_startband) {
            /* all remaining bands are not skipped */
            tbits_8ths += skip_bit;
            break;
        }

        /* determine the number of bits available for coding "do not skip" markers */
        remaining   = tbits_8ths - total;
        bandbits    = remaining / (ff_celt_freq_bands[j+1] - ff_celt_freq_bands[f->start_band]);
        remaining  -= bandbits  * (ff_celt_freq_bands[j+1] - ff_celt_freq_bands[f->start_band]);
        allocation  = f->pulses[j] + bandbits * ff_celt_freq_range[j];
        allocation += FFMAX(remaining - (ff_celt_freq_bands[j] - ff_celt_freq_bands[f->start_band]), 0);

        /* a "do not skip" marker is only coded if the allocation is
         * above the chosen threshold */
        if (allocation >= FFMAX(threshold[j], (f->channels + 1) << 3)) {
            int do_not_skip;
            if (encode) {
                do_not_skip = f->coded_bands <= f->skip_band_floor;
                ff_opus_rc_enc_log(rc, do_not_skip, 1);
            } else {
                do_not_skip = ff_opus_rc_dec_log(rc, 1);
            }

            if (do_not_skip)
                break;

            total      += 1 << 3;
            allocation -= 1 << 3;
        }

        /* the band is skipped, so reclaim its bits */
        total -= f->pulses[j];
        if (intensitystereo_bit) {
            total -= intensitystereo_bit;
            intensitystereo_bit = ff_celt_log2_frac[j - f->start_band];
            total += intensitystereo_bit;
        }

        total += f->pulses[j] = (allocation >= f->channels << 3) ? f->channels << 3 : 0;
    }

    /* IS start band */
    if (encode) {
        if (intensitystereo_bit) {
            f->intensity_stereo = FFMIN(f->intensity_stereo, f->coded_bands);
            ff_opus_rc_enc_uint(rc, f->intensity_stereo, f->coded_bands + 1 - f->start_band);
        }
    } else {
        f->intensity_stereo = f->dual_stereo = 0;
        if (intensitystereo_bit)
            f->intensity_stereo = f->start_band + ff_opus_rc_dec_uint(rc, f->coded_bands + 1 - f->start_band);
    }

    /* DS flag */
    if (f->intensity_stereo <= f->start_band)
        tbits_8ths += dualstereo_bit; /* no intensity stereo means no dual stereo */
    else if (dualstereo_bit)
        if (encode)
            ff_opus_rc_enc_log(rc, f->dual_stereo, 1);
        else
            f->dual_stereo = ff_opus_rc_dec_log(rc, 1);

    /* Supply the remaining bits in this frame to lower bands */
    remaining = tbits_8ths - total;
    bandbits  = remaining / (ff_celt_freq_bands[f->coded_bands] - ff_celt_freq_bands[f->start_band]);
    remaining -= bandbits * (ff_celt_freq_bands[f->coded_bands] - ff_celt_freq_bands[f->start_band]);
    for (i = f->start_band; i < f->coded_bands; i++) {
        const int bits = FFMIN(remaining, ff_celt_freq_range[i]);
        f->pulses[i] += bits + bandbits * ff_celt_freq_range[i];
        remaining    -= bits;
    }

    /* Finally determine the allocation */
    for (i = f->start_band; i < f->coded_bands; i++) {
        int N = ff_celt_freq_range[i] << f->size;
        int prev_extra = extrabits;
        f->pulses[i] += extrabits;

        if (N > 1) {
            int dof;        /* degrees of freedom */
            int temp;       /* dof * channels * log(dof) */
            int fine_bits;
            int max_bits;
            int offset;     /* fine energy quantization offset, i.e.
                             * extra bits assigned over the standard
                             * totalbits/dof */

            extrabits = FFMAX(f->pulses[i] - f->caps[i], 0);
            f->pulses[i] -= extrabits;

            /* intensity stereo makes use of an extra degree of freedom */
            dof = N * f->channels + (f->channels == 2 && N > 2 && !f->dual_stereo && i < f->intensity_stereo);
            temp = dof * (ff_celt_log_freq_range[i] + (f->size << 3));
            offset = (temp >> 1) - dof * CELT_FINE_OFFSET;
            if (N == 2) /* dof=2 is the only case that doesn't fit the model */
                offset += dof << 1;

            /* grant an additional bias for the first and second pulses */
            if (f->pulses[i] + offset < 2 * (dof << 3))
                offset += temp >> 2;
            else if (f->pulses[i] + offset < 3 * (dof << 3))
                offset += temp >> 3;

            fine_bits = (f->pulses[i] + offset + (dof << 2)) / (dof << 3);
            max_bits  = FFMIN((f->pulses[i] >> 3) >> (f->channels - 1), CELT_MAX_FINE_BITS);
            max_bits  = FFMAX(max_bits, 0);
            f->fine_bits[i] = av_clip(fine_bits, 0, max_bits);

            /* If fine_bits was rounded down or capped,
             * give priority for the final fine energy pass */
            f->fine_priority[i] = (f->fine_bits[i] * (dof << 3) >= f->pulses[i] + offset);

            /* the remaining bits are assigned to PVQ */
            f->pulses[i] -= f->fine_bits[i] << (f->channels - 1) << 3;
        } else {
            /* all bits go to fine energy except for the sign bit */
            extrabits = FFMAX(f->pulses[i] - (f->channels << 3), 0);
            f->pulses[i] -= extrabits;
            f->fine_bits[i] = 0;
            f->fine_priority[i] = 1;
        }

        /* hand back a limited number of extra fine energy bits to this band */
        if (extrabits > 0) {
            int fineextra = FFMIN(extrabits >> (f->channels + 2),
                                  CELT_MAX_FINE_BITS - f->fine_bits[i]);
            f->fine_bits[i] += fineextra;

            fineextra <<= f->channels + 2;
            f->fine_priority[i] = (fineextra >= extrabits - prev_extra);
            extrabits -= fineextra;
        }
    }
    f->remaining = extrabits;

    /* skipped bands dedicate all of their bits for fine energy */
    for (; i < f->end_band; i++) {
        f->fine_bits[i]     = f->pulses[i] >> (f->channels - 1) >> 3;
        f->pulses[i]        = 0;
        f->fine_priority[i] = f->fine_bits[i] < 1;
    }
}
