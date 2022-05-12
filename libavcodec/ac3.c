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

#include "libavutil/error.h"
#include "libavutil/macros.h"

#include "ac3.h"
#include "ac3defs.h"

/**
 * Starting frequency coefficient bin for each critical band.
 */
const uint8_t ff_ac3_band_start_tab[AC3_CRITICAL_BANDS+1] = {
      0,  1,   2,   3,   4,   5,   6,   7,   8,   9,
     10,  11, 12,  13,  14,  15,  16,  17,  18,  19,
     20,  21, 22,  23,  24,  25,  26,  27,  28,  31,
     34,  37, 40,  43,  46,  49,  55,  61,  67,  73,
     79,  85, 97, 109, 121, 133, 157, 181, 205, 229, 253
};

/**
 * Map each frequency coefficient bin to the critical band that contains it.
 */
const uint8_t ff_ac3_bin_to_band_tab[253] = {
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

static const uint8_t ac3_log_add_tab[260]= {
0x40,0x3f,0x3e,0x3d,0x3c,0x3b,0x3a,0x39,0x38,0x37,
0x36,0x35,0x34,0x34,0x33,0x32,0x31,0x30,0x2f,0x2f,
0x2e,0x2d,0x2c,0x2c,0x2b,0x2a,0x29,0x29,0x28,0x27,
0x26,0x26,0x25,0x24,0x24,0x23,0x23,0x22,0x21,0x21,
0x20,0x20,0x1f,0x1e,0x1e,0x1d,0x1d,0x1c,0x1c,0x1b,
0x1b,0x1a,0x1a,0x19,0x19,0x18,0x18,0x17,0x17,0x16,
0x16,0x15,0x15,0x15,0x14,0x14,0x13,0x13,0x13,0x12,
0x12,0x12,0x11,0x11,0x11,0x10,0x10,0x10,0x0f,0x0f,
0x0f,0x0e,0x0e,0x0e,0x0d,0x0d,0x0d,0x0d,0x0c,0x0c,
0x0c,0x0c,0x0b,0x0b,0x0b,0x0b,0x0a,0x0a,0x0a,0x0a,
0x0a,0x09,0x09,0x09,0x09,0x09,0x08,0x08,0x08,0x08,
0x08,0x08,0x07,0x07,0x07,0x07,0x07,0x07,0x06,0x06,
0x06,0x06,0x06,0x06,0x06,0x06,0x05,0x05,0x05,0x05,
0x05,0x05,0x05,0x05,0x04,0x04,0x04,0x04,0x04,0x04,
0x04,0x04,0x04,0x04,0x04,0x03,0x03,0x03,0x03,0x03,
0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x02,
0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

static const uint16_t ac3_hearing_threshold_tab[AC3_CRITICAL_BANDS][3]= {
{ 0x04d0,0x04f0,0x0580 },
{ 0x04d0,0x04f0,0x0580 },
{ 0x0440,0x0460,0x04b0 },
{ 0x0400,0x0410,0x0450 },
{ 0x03e0,0x03e0,0x0420 },
{ 0x03c0,0x03d0,0x03f0 },
{ 0x03b0,0x03c0,0x03e0 },
{ 0x03b0,0x03b0,0x03d0 },
{ 0x03a0,0x03b0,0x03c0 },
{ 0x03a0,0x03a0,0x03b0 },
{ 0x03a0,0x03a0,0x03b0 },
{ 0x03a0,0x03a0,0x03b0 },
{ 0x03a0,0x03a0,0x03a0 },
{ 0x0390,0x03a0,0x03a0 },
{ 0x0390,0x0390,0x03a0 },
{ 0x0390,0x0390,0x03a0 },
{ 0x0380,0x0390,0x03a0 },
{ 0x0380,0x0380,0x03a0 },
{ 0x0370,0x0380,0x03a0 },
{ 0x0370,0x0380,0x03a0 },
{ 0x0360,0x0370,0x0390 },
{ 0x0360,0x0370,0x0390 },
{ 0x0350,0x0360,0x0390 },
{ 0x0350,0x0360,0x0390 },
{ 0x0340,0x0350,0x0380 },
{ 0x0340,0x0350,0x0380 },
{ 0x0330,0x0340,0x0380 },
{ 0x0320,0x0340,0x0370 },
{ 0x0310,0x0320,0x0360 },
{ 0x0300,0x0310,0x0350 },
{ 0x02f0,0x0300,0x0340 },
{ 0x02f0,0x02f0,0x0330 },
{ 0x02f0,0x02f0,0x0320 },
{ 0x02f0,0x02f0,0x0310 },
{ 0x0300,0x02f0,0x0300 },
{ 0x0310,0x0300,0x02f0 },
{ 0x0340,0x0320,0x02f0 },
{ 0x0390,0x0350,0x02f0 },
{ 0x03e0,0x0390,0x0300 },
{ 0x0420,0x03e0,0x0310 },
{ 0x0460,0x0420,0x0330 },
{ 0x0490,0x0450,0x0350 },
{ 0x04a0,0x04a0,0x03c0 },
{ 0x0460,0x0490,0x0410 },
{ 0x0440,0x0460,0x0470 },
{ 0x0440,0x0440,0x04a0 },
{ 0x0520,0x0480,0x0460 },
{ 0x0800,0x0630,0x0440 },
{ 0x0840,0x0840,0x0450 },
{ 0x0840,0x0840,0x04e0 },
};

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
    band = ff_ac3_bin_to_band_tab[start];
    do {
        int v = psd[bin++];
        int band_end = FFMIN(ff_ac3_band_start_tab[band+1], end);
        for (; bin < band_end; bin++) {
            int max = FFMAX(v, psd[bin]);
            /* logadd */
            int adr = FFMIN(max - ((v + psd[bin] + 1) >> 1), 255);
            v = max + ac3_log_add_tab[adr];
        }
        band_psd[band++] = v;
    } while (end > ff_ac3_band_start_tab[band]);
}

int ff_ac3_bit_alloc_calc_mask(AC3BitAllocParameters *s, int16_t *band_psd,
                               int start, int end, int fast_gain, int is_lfe,
                               int dba_mode, int dba_nsegs, uint8_t *dba_offsets,
                               uint8_t *dba_lengths, uint8_t *dba_values,
                               int16_t *mask)
{
    int16_t excite[AC3_CRITICAL_BANDS]; /* excitation */
    int band;
    int band_start, band_end, begin, end1;
    int lowcomp, fastleak, slowleak;

    if (end <= 0)
        return AVERROR_INVALIDDATA;

    /* excitation function */
    band_start = ff_ac3_bin_to_band_tab[start];
    band_end   = ff_ac3_bin_to_band_tab[end-1] + 1;

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
        mask[band] = FFMAX(ac3_hearing_threshold_tab[band >> s->sr_shift][s->sr_code], excite[band]);
    }

    /* delta bit allocation */

    if (dba_mode == DBA_REUSE || dba_mode == DBA_NEW) {
        int i, seg, delta;
        if (dba_nsegs > 8)
            return -1;
        band = band_start;
        for (seg = 0; seg < dba_nsegs; seg++) {
            band += dba_offsets[seg];
            if (band >= AC3_CRITICAL_BANDS || dba_lengths[seg] > AC3_CRITICAL_BANDS-band)
                return -1;
            if (dba_values[seg] >= 4) {
                delta = (dba_values[seg] - 3) * 128;
            } else {
                delta = (dba_values[seg] - 4) * 128;
            }
            for (i = 0; i < dba_lengths[seg]; i++) {
                mask[band++] += delta;
            }
        }
    }
    return 0;
}
