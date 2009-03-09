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

#include <inttypes.h>
#include "avcodec.h"
#include "acelp_vectors.h"

const uint8_t ff_fc_2pulses_9bits_track1[16] =
{
    1,  3,
    6,  8,
    11, 13,
    16, 18,
    21, 23,
    26, 28,
    31, 33,
    36, 38
};
const uint8_t ff_fc_2pulses_9bits_track1_gray[16] =
{
  1,  3,
  8,  6,
  18, 16,
  11, 13,
  38, 36,
  31, 33,
  21, 23,
  28, 26,
};

const uint8_t ff_fc_2pulses_9bits_track2_gray[32] =
{
  0,  2,
  5,  4,
  12, 10,
  7,  9,
  25, 24,
  20, 22,
  14, 15,
  19, 17,
  36, 31,
  21, 26,
  1,  6,
  16, 11,
  27, 29,
  32, 30,
  39, 37,
  34, 35,
};

const uint8_t ff_fc_4pulses_8bits_tracks_13[16] =
{
  0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75,
};

const uint8_t ff_fc_4pulses_8bits_track_4[32] =
{
    3,  4,
    8,  9,
    13, 14,
    18, 19,
    23, 24,
    28, 29,
    33, 34,
    38, 39,
    43, 44,
    48, 49,
    53, 54,
    58, 59,
    63, 64,
    68, 69,
    73, 74,
    78, 79,
};

#if 0
static uint8_t gray_decode[32] =
{
    0,  1,  3,  2,  7,  6,  4,  5,
   15, 14, 12, 13,  8,  9, 11, 10,
   31, 30, 28, 29, 24, 25, 27, 26,
   16, 17, 19, 18, 23, 22, 20, 21
};
#endif

void ff_acelp_fc_pulse_per_track(
        int16_t* fc_v,
        const uint8_t *tab1,
        const uint8_t *tab2,
        int pulse_indexes,
        int pulse_signs,
        int pulse_count,
        int bits)
{
    int mask = (1 << bits) - 1;
    int i;

    for(i=0; i<pulse_count; i++)
    {
        fc_v[i + tab1[pulse_indexes & mask]] +=
                (pulse_signs & 1) ? 8191 : -8192; // +/-1 in (2.13)

        pulse_indexes >>= bits;
        pulse_signs >>= 1;
    }

    fc_v[tab2[pulse_indexes]] += (pulse_signs & 1) ? 8191 : -8192;
}

void ff_acelp_weighted_vector_sum(
        int16_t* out,
        const int16_t *in_a,
        const int16_t *in_b,
        int16_t weight_coeff_a,
        int16_t weight_coeff_b,
        int16_t rounder,
        int shift,
        int length)
{
    int i;

    // Clipping required here; breaks OVERFLOW test.
    for(i=0; i<length; i++)
        out[i] = av_clip_int16((
                 in_a[i] * weight_coeff_a +
                 in_b[i] * weight_coeff_b +
                 rounder) >> shift);
}

void ff_weighted_vector_sumf(float *out, const float *in_a, const float *in_b,
                             float weight_coeff_a, float weight_coeff_b, int length)
{
    int i;

    for(i=0; i<length; i++)
        out[i] = weight_coeff_a * in_a[i]
               + weight_coeff_b * in_b[i];
}
