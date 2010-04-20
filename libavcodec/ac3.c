/*
 * Common code between the AC-3 encoder and decoder
 * Copyright (c) 2000 Fabrice Bellard
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
 * Common code between the AC-3 encoder and decoder.
 */

#include "avcodec.h"
#include "ac3.h"
#include "get_bits.h"

#if CONFIG_HARDCODED_TABLES

/**
 * Starting frequency coefficient bin for each critical band.
 */
static const uint8_t band_start_tab[51] = {
      0,  1,   2,   3,   4,   5,   6,   7,   8,   9,
     10,  11, 12,  13,  14,  15,  16,  17,  18,  19,
     20,  21, 22,  23,  24,  25,  26,  27,  28,  31,
     34,  37, 40,  43,  46,  49,  55,  61,  67,  73,
     79,  85, 97, 109, 121, 133, 157, 181, 205, 229, 253
};

/**
 * Maps each frequency coefficient bin to the critical band that contains it.
 */
static const uint8_t bin_to_band_tab[253] = {
     0,
     1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 28, 28, 29, 29, 29, 30, 30, 30,
    31, 31, 31, 32, 32, 32, 33, 33, 33, 34, 34, 34,
    35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36,
    37, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38, 38,
    39, 39, 39, 39, 39, 39, 40, 40, 40, 40, 40, 40,
    41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41,
    42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
    43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
    45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
    46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46,
    46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46,
    47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47,
    47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47,
    48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,
    48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,
    49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49,
    49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49
};

#else /* CONFIG_HARDCODED_TABLES */
static uint8_t band_start_tab[51];
static uint8_t bin_to_band_tab[253];
#endif

static inline int calc_lowcomp1(int a, int b0, int b1, int c)
{
    if ((b0 + 256) == b1) {
        a = c;
    } else if (b0 > b1) {
        a = FFMAX(a - 64, 0);
    }
    return a;
}

static inline int calc_lowcomp(int a, int b0, int b1, int bin)
{
    if (bin < 7) {
        return calc_lowcomp1(a, b0, b1, 384);
    } else if (bin < 20) {
        return calc_lowcomp1(a, b0, b1, 320);
    } else {
        return FFMAX(a - 128, 0);
    }
}

void ff_ac3_bit_alloc_calc_psd(int8_t *exp, int start, int end, int16_t *psd,
                               int16_t *band_psd)
{
    int bin, band;

    /* exponent mapping to PSD */
    for (bin = start; bin < end; bin++) {
        psd[bin]=(3072 - (exp[bin] << 7));
    }

    /* PSD integration */
    bin  = start;
    band = bin_to_band_tab[start];
    do {
        int v = psd[bin++];
        int band_end = FFMIN(band_start_tab[band+1], end);
        for (; bin < band_end; bin++) {
            int max = FFMAX(v, psd[bin]);
            /* logadd */
            int adr = FFMIN(max - ((v + psd[bin] + 1) >> 1), 255);
            v = max + ff_ac3_log_add_tab[adr];
        }
        band_psd[band++] = v;
    } while (end > band_start_tab[band]);
}

int ff_ac3_bit_alloc_calc_mask(AC3BitAllocParameters *s, int16_t *band_psd,
                               int start, int end, int fast_gain, int is_lfe,
                               int dba_mode, int dba_nsegs, uint8_t *dba_offsets,
                               uint8_t *dba_lengths, uint8_t *dba_values,
                               int16_t *mask)
{
    int16_t excite[50]; /* excitation */
    int band;
    int band_start, band_end, begin, end1;
    int lowcomp, fastleak, slowleak;

    /* excitation function */
    band_start = bin_to_band_tab[start];
    band_end   = bin_to_band_tab[end-1] + 1;

    if (band_start == 0) {
        lowcomp = 0;
        lowcomp = calc_lowcomp1(lowcomp, band_psd[0], band_psd[1], 384);
        excite[0] = band_psd[0] - fast_gain - lowcomp;
        lowcomp = calc_lowcomp1(lowcomp, band_psd[1], band_psd[2], 384);
        excite[1] = band_psd[1] - fast_gain - lowcomp;
        begin = 7;
        for (band = 2; band < 7; band++) {
            if (!(is_lfe && band == 6))
                lowcomp = calc_lowcomp1(lowcomp, band_psd[band], band_psd[band+1], 384);
            fastleak = band_psd[band] - fast_gain;
            slowleak = band_psd[band] - s->slow_gain;
            excite[band] = fastleak - lowcomp;
            if (!(is_lfe && band == 6)) {
                if (band_psd[band] <= band_psd[band+1]) {
                    begin = band + 1;
                    break;
                }
            }
        }

        end1 = FFMIN(band_end, 22);
        for (band = begin; band < end1; band++) {
            if (!(is_lfe && band == 6))
                lowcomp = calc_lowcomp(lowcomp, band_psd[band], band_psd[band+1], band);
            fastleak = FFMAX(fastleak - s->fast_decay, band_psd[band] - fast_gain);
            slowleak = FFMAX(slowleak - s->slow_decay, band_psd[band] - s->slow_gain);
            excite[band] = FFMAX(fastleak - lowcomp, slowleak);
        }
        begin = 22;
    } else {
        /* coupling channel */
        begin = band_start;
        fastleak = (s->cpl_fast_leak << 8) + 768;
        slowleak = (s->cpl_slow_leak << 8) + 768;
    }

    for (band = begin; band < band_end; band++) {
        fastleak = FFMAX(fastleak - s->fast_decay, band_psd[band] - fast_gain);
        slowleak = FFMAX(slowleak - s->slow_decay, band_psd[band] - s->slow_gain);
        excite[band] = FFMAX(fastleak, slowleak);
    }

    /* compute masking curve */

    for (band = band_start; band < band_end; band++) {
        int tmp = s->db_per_bit - band_psd[band];
        if (tmp > 0) {
            excite[band] += tmp >> 2;
        }
        mask[band] = FFMAX(ff_ac3_hearing_threshold_tab[band >> s->sr_shift][s->sr_code], excite[band]);
    }

    /* delta bit allocation */

    if (dba_mode == DBA_REUSE || dba_mode == DBA_NEW) {
        int i, seg, delta;
        if (dba_nsegs >= 8)
            return -1;
        band = 0;
        for (seg = 0; seg < dba_nsegs; seg++) {
            band += dba_offsets[seg];
            if (band >= 50 || dba_lengths[seg] > 50-band)
                return -1;
            if (dba_values[seg] >= 4) {
                delta = (dba_values[seg] - 3) << 7;
            } else {
                delta = (dba_values[seg] - 4) << 7;
            }
            for (i = 0; i < dba_lengths[seg]; i++) {
                mask[band++] += delta;
            }
        }
    }
    return 0;
}

void ff_ac3_bit_alloc_calc_bap(int16_t *mask, int16_t *psd, int start, int end,
                               int snr_offset, int floor,
                               const uint8_t *bap_tab, uint8_t *bap)
{
    int bin, band;

    /* special case, if snr offset is -960, set all bap's to zero */
    if (snr_offset == -960) {
        memset(bap, 0, 256);
        return;
    }

    bin  = start;
    band = bin_to_band_tab[start];
    do {
        int m = (FFMAX(mask[band] - snr_offset - floor, 0) & 0x1FE0) + floor;
        int band_end = FFMIN(band_start_tab[band+1], end);
        for (; bin < band_end; bin++) {
            int address = av_clip((psd[bin] - m) >> 5, 0, 63);
            bap[bin] = bap_tab[address];
        }
    } while (end > band_start_tab[band++]);
}

/* AC-3 bit allocation. The algorithm is the one described in the AC-3
   spec. */
void ac3_parametric_bit_allocation(AC3BitAllocParameters *s, uint8_t *bap,
                                   int8_t *exp, int start, int end,
                                   int snr_offset, int fast_gain, int is_lfe,
                                   int dba_mode, int dba_nsegs,
                                   uint8_t *dba_offsets, uint8_t *dba_lengths,
                                   uint8_t *dba_values)
{
    int16_t psd[256];   /* scaled exponents */
    int16_t band_psd[50]; /* interpolated exponents */
    int16_t mask[50];   /* masking value */

    ff_ac3_bit_alloc_calc_psd(exp, start, end, psd, band_psd);

    ff_ac3_bit_alloc_calc_mask(s, band_psd, start, end, fast_gain, is_lfe,
                               dba_mode, dba_nsegs, dba_offsets, dba_lengths,
                               dba_values, mask);

    ff_ac3_bit_alloc_calc_bap(mask, psd, start, end, snr_offset, s->floor,
                              ff_ac3_bap_tab, bap);
}

/**
 * Initializes some tables.
 * note: This function must remain thread safe because it is called by the
 *       AVParser init code.
 */
av_cold void ac3_common_init(void)
{
#if !CONFIG_HARDCODED_TABLES
    /* compute bndtab and masktab from bandsz */
    int bin = 0, band;
    for (band = 0; band < 50; band++) {
        int band_end = bin + ff_ac3_critical_band_size_tab[band];
        band_start_tab[band] = bin;
        while (bin < band_end)
            bin_to_band_tab[bin++] = band;
    }
    band_start_tab[50] = bin;
#endif /* !CONFIG_HARDCODED_TABLES */
}
