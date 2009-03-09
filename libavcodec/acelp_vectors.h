/*
 * adaptive and fixed codebook vector operations for ACELP-based codecs
 *
 * Copyright (c) 2008 Vladimir Voroshilov
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

#ifndef AVCODEC_ACELP_VECTORS_H
#define AVCODEC_ACELP_VECTORS_H

#include <stdint.h>

/**
 * Track|Pulse|        Positions
 * -------------------------------------------------------------------------
 *  1   | 0   | 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75
 * -------------------------------------------------------------------------
 *  2   | 1   | 1, 6, 11, 16, 21, 26, 31, 36, 41, 46, 51, 56, 61, 66, 71, 76
 * -------------------------------------------------------------------------
 *  3   | 2   | 2, 7, 12, 17, 22, 27, 32, 37, 42, 47, 52, 57, 62, 67, 72, 77
 * -------------------------------------------------------------------------
 *
 * Table contains only first the pulse indexes.
 *
 * Used in G.729 @8k, G.729 @4.4k, AMR @7.95k, AMR @7.40k
 */
extern const uint8_t ff_fc_4pulses_8bits_tracks_13[16];

/**
 * Track|Pulse|        Positions
 * -------------------------------------------------------------------------
 *  4   | 3   | 3, 8, 13, 18, 23, 28, 33, 38, 43, 48, 53, 58, 63, 68, 73, 78
 *      |     | 4, 9, 14, 19, 24, 29, 34, 39, 44, 49, 54, 59, 64, 69, 74, 79
 * -------------------------------------------------------------------------
 *
 * @remark Track in the table should be read top-to-bottom, left-to-right.
 *
 * Used in G.729 @8k, G.729 @4.4k, AMR @7.95k, AMR @7.40k
 */
extern const uint8_t ff_fc_4pulses_8bits_track_4[32];

/**
 * Track|Pulse|        Positions
 * -----------------------------------------
 *  1   | 0   | 1, 6, 11, 16, 21, 26, 31, 36
 *      |     | 3, 8, 13, 18, 23, 28, 33, 38
 * -----------------------------------------
 *
 * @remark Track in the table should be read top-to-bottom, left-to-right.
 *
 * @note (EE) Reference G.729D code also uses gray decoding for each
 *            pulse index before looking up the value in the table.
 *
 * Used in G.729 @6.4k (with gray coding), AMR @5.9k (without gray coding)
 */
extern const uint8_t ff_fc_2pulses_9bits_track1[16];
extern const uint8_t ff_fc_2pulses_9bits_track1_gray[16];

/**
 * Track|Pulse|        Positions
 * -----------------------------------------
 *  2   | 1   | 0, 7, 14, 20, 27, 34,  1, 21
 *      |     | 2, 9, 15, 22, 29, 35,  6, 26
 *      |     | 4,10, 17, 24, 30, 37, 11, 31
 *      |     | 5,12, 19, 25, 32, 39, 16, 36
 * -----------------------------------------
 *
 * @remark Track in the table should be read top-to-bottom, left-to-right.
 *
 * @note (EE.1) This table (from the reference code) does not comply with
 *              the specification.
 *              The specification contains the following table:
 *
 * Track|Pulse|        Positions
 * -----------------------------------------
 *  2   | 1   | 0, 5, 10, 15, 20, 25, 30, 35
 *      |     | 1, 6, 11, 16, 21, 26, 31, 36
 *      |     | 2, 7, 12, 17, 22, 27, 32, 37
 *      |     | 4, 9, 14, 19, 24, 29, 34, 39
 *
 * -----------------------------------------
 *
 * @note (EE.2) Reference G.729D code also uses gray decoding for each
 *              pulse index before looking up the value in the table.
 *
 * Used in G.729 @6.4k (with gray coding)
 */
extern const uint8_t ff_fc_2pulses_9bits_track2_gray[32];

/**
 * Decode fixed-codebook vector (3.8 and D.5.8 of G.729, 5.7.1 of AMR).
 * @param fc_v [out] decoded fixed codebook vector (2.13)
 * @param tab1 table used for first pulse_count pulses
 * @param tab2 table used for last pulse
 * @param pulse_indexes fixed codebook indexes
 * @param pulse_signs signs of the excitation pulses (0 bit value
 *                     means negative sign)
 * @param bits number of bits per one pulse index
 * @param pulse_count number of pulses decoded using first table
 * @param bits length of one pulse index in bits
 *
 * Used in G.729 @8k, G.729 @4.4k, G.729 @6.4k, AMR @7.95k, AMR @7.40k
 */
void ff_acelp_fc_pulse_per_track(
        int16_t* fc_v,
        const uint8_t *tab1,
        const uint8_t *tab2,
        int pulse_indexes,
        int pulse_signs,
        int pulse_count,
        int bits);

/**
 * weighted sum of two vectors with rounding.
 * @param out [out] result of addition
 * @param in_a first vector
 * @param in_b second vector
 * @param weight_coeff_a first vector weight coefficient
 * @param weight_coeff_a second vector weight coefficient
 * @param rounder this value will be added to the sum of the two vectors
 * @param shift result will be shifted to right by this value
 * @param length vectors length
 *
 * @note It is safe to pass the same buffer for out and in_a or in_b.
 *
 *  out[i] = (in_a[i]*weight_a + in_b[i]*weight_b + rounder) >> shift
 */
void ff_acelp_weighted_vector_sum(
        int16_t* out,
        const int16_t *in_a,
        const int16_t *in_b,
        int16_t weight_coeff_a,
        int16_t weight_coeff_b,
        int16_t rounder,
        int shift,
        int length);

/**
 * float implementation of weighted sum of two vectors.
 * @param out [out] result of addition
 * @param in_a first vector
 * @param in_b second vector
 * @param weight_coeff_a first vector weight coefficient
 * @param weight_coeff_a second vector weight coefficient
 * @param length vectors length
 *
 * @note It is safe to pass the same buffer for out and in_a or in_b.
 */
void ff_weighted_vector_sumf(float *out, const float *in_a, const float *in_b,
                             float weight_coeff_a, float weight_coeff_b, int length);

#endif /* AVCODEC_ACELP_VECTORS_H */
