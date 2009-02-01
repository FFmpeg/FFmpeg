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
 * @file libavcodec/ac3.c
 * Common code between the AC-3 encoder and decoder.
 */

#include "avcodec.h"
#include "ac3.h"
#include "bitstream.h"

static uint8_t band_start_tab[51];
static uint8_t bin_to_band_tab[253];

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
    int bin, i, j, k, end1, v;

    /* exponent mapping to PSD */
    for(bin=start;bin<end;bin++) {
        psd[bin]=(3072 - (exp[bin] << 7));
    }

    /* PSD integration */
    j=start;
    k=bin_to_band_tab[start];
    do {
        v=psd[j];
        j++;
        end1 = FFMIN(band_start_tab[k+1], end);
        for(i=j;i<end1;i++) {
            /* logadd */
            int adr = FFMIN(FFABS(v - psd[j]) >> 1, 255);
            v = FFMAX(v, psd[j]) + ff_ac3_log_add_tab[adr];
            j++;
        }
        band_psd[k]=v;
        k++;
    } while (end > band_start_tab[k]);
}

int ff_ac3_bit_alloc_calc_mask(AC3BitAllocParameters *s, int16_t *band_psd,
                               int start, int end, int fast_gain, int is_lfe,
                               int dba_mode, int dba_nsegs, uint8_t *dba_offsets,
                               uint8_t *dba_lengths, uint8_t *dba_values,
                               int16_t *mask)
{
    int16_t excite[50]; /* excitation */
    int bin, k;
    int bndstrt, bndend, begin, end1, tmp;
    int lowcomp, fastleak, slowleak;

    /* excitation function */
    bndstrt = bin_to_band_tab[start];
    bndend = bin_to_band_tab[end-1] + 1;

    if (bndstrt == 0) {
        lowcomp = 0;
        lowcomp = calc_lowcomp1(lowcomp, band_psd[0], band_psd[1], 384);
        excite[0] = band_psd[0] - fast_gain - lowcomp;
        lowcomp = calc_lowcomp1(lowcomp, band_psd[1], band_psd[2], 384);
        excite[1] = band_psd[1] - fast_gain - lowcomp;
        begin = 7;
        for (bin = 2; bin < 7; bin++) {
            if (!(is_lfe && bin == 6))
                lowcomp = calc_lowcomp1(lowcomp, band_psd[bin], band_psd[bin+1], 384);
            fastleak = band_psd[bin] - fast_gain;
            slowleak = band_psd[bin] - s->slow_gain;
            excite[bin] = fastleak - lowcomp;
            if (!(is_lfe && bin == 6)) {
                if (band_psd[bin] <= band_psd[bin+1]) {
                    begin = bin + 1;
                    break;
                }
            }
        }

        end1=bndend;
        if (end1 > 22) end1=22;

        for (bin = begin; bin < end1; bin++) {
            if (!(is_lfe && bin == 6))
                lowcomp = calc_lowcomp(lowcomp, band_psd[bin], band_psd[bin+1], bin);

            fastleak = FFMAX(fastleak - s->fast_decay, band_psd[bin] - fast_gain);
            slowleak = FFMAX(slowleak - s->slow_decay, band_psd[bin] - s->slow_gain);
            excite[bin] = FFMAX(fastleak - lowcomp, slowleak);
        }
        begin = 22;
    } else {
        /* coupling channel */
        begin = bndstrt;

        fastleak = (s->cpl_fast_leak << 8) + 768;
        slowleak = (s->cpl_slow_leak << 8) + 768;
    }

    for (bin = begin; bin < bndend; bin++) {
        fastleak = FFMAX(fastleak - s->fast_decay, band_psd[bin] - fast_gain);
        slowleak = FFMAX(slowleak - s->slow_decay, band_psd[bin] - s->slow_gain);
        excite[bin] = FFMAX(fastleak, slowleak);
    }

    /* compute masking curve */

    for (bin = bndstrt; bin < bndend; bin++) {
        tmp = s->db_per_bit - band_psd[bin];
        if (tmp > 0) {
            excite[bin] += tmp >> 2;
        }
        mask[bin] = FFMAX(ff_ac3_hearing_threshold_tab[bin >> s->sr_shift][s->sr_code], excite[bin]);
    }

    /* delta bit allocation */

    if (dba_mode == DBA_REUSE || dba_mode == DBA_NEW) {
        int band, seg, delta;
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
            for (k = 0; k < dba_lengths[seg]; k++) {
                mask[band] += delta;
                band++;
            }
        }
    }
    return 0;
}

void ff_ac3_bit_alloc_calc_bap(int16_t *mask, int16_t *psd, int start, int end,
                               int snr_offset, int floor,
                               const uint8_t *bap_tab, uint8_t *bap)
{
    int i, j, k, end1, v, address;

    /* special case, if snr offset is -960, set all bap's to zero */
    if(snr_offset == -960) {
        memset(bap, 0, 256);
        return;
    }

    i = start;
    j = bin_to_band_tab[start];
    do {
        v = (FFMAX(mask[j] - snr_offset - floor, 0) & 0x1FE0) + floor;
        end1 = FFMIN(band_start_tab[j] + ff_ac3_critical_band_size_tab[j], end);
        for (k = i; k < end1; k++) {
            address = av_clip((psd[i] - v) >> 5, 0, 63);
            bap[i] = bap_tab[address];
            i++;
        }
    } while (end > band_start_tab[j++]);
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
                               dba_mode, dba_nsegs, dba_offsets, dba_lengths, dba_values,
                               mask);

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
    int i, j, k, l, v;
    /* compute bndtab and masktab from bandsz */
    k = 0;
    l = 0;
    for(i=0;i<50;i++) {
        band_start_tab[i] = l;
        v = ff_ac3_critical_band_size_tab[i];
        for(j=0;j<v;j++) bin_to_band_tab[k++]=i;
        l += v;
    }
    band_start_tab[50] = l;
}
