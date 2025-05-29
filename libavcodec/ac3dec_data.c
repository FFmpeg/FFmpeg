/*
 * AC-3 and E-AC-3 decoder tables
 * Copyright (c) 2007 Bartlomiej Wolowiec <bartek.wolowiec@gmail.com>
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
 * Tables taken directly from the AC-3 spec or derived from it.
 */

#include "ac3dec_data.h"
#include "libavutil/thread.h"

/**
 * Table used to ungroup 3 values stored in 5 bits.
 * Used by bap=1 mantissas and GAQ.
 * ff_ac3_ungroup_3_in_5_bits_tab[i] = { i/9, (i%9)/3, (i%9)%3 }
 */
const uint8_t ff_ac3_ungroup_3_in_5_bits_tab[32][3] = {
    { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 2 }, { 0, 1, 0 },
    { 0, 1, 1 }, { 0, 1, 2 }, { 0, 2, 0 }, { 0, 2, 1 },
    { 0, 2, 2 }, { 1, 0, 0 }, { 1, 0, 1 }, { 1, 0, 2 },
    { 1, 1, 0 }, { 1, 1, 1 }, { 1, 1, 2 }, { 1, 2, 0 },
    { 1, 2, 1 }, { 1, 2, 2 }, { 2, 0, 0 }, { 2, 0, 1 },
    { 2, 0, 2 }, { 2, 1, 0 }, { 2, 1, 1 }, { 2, 1, 2 },
    { 2, 2, 0 }, { 2, 2, 1 }, { 2, 2, 2 }, { 3, 0, 0 },
    { 3, 0, 1 }, { 3, 0, 2 }, { 3, 1, 0 }, { 3, 1, 1 }
};

/**
 * table for ungrouping 3 values in 7 bits.
 * used for exponents and bap=2 mantissas
 */
uint8_t ff_ac3_ungroup_3_in_7_bits_tab[128][3];

/**
 * Symmetrical Dequantization
 * reference: Section 7.3.3 Expansion of Mantissas for Symmetrical Quantization
 *            Tables 7.19 to 7.23
 */
#define SYMMETRIC_DEQUANT(code, levels) (((code - (levels >> 1)) * (1 << 24)) / levels)
/**
 * Ungrouped mantissa tables; the extra entry is padding to avoid range checks
 */
/**
 * Table 7.21
 */
const int ff_ac3_bap3_mantissas[7 + 1] = {
    SYMMETRIC_DEQUANT(0, 7),
    SYMMETRIC_DEQUANT(1, 7),
    SYMMETRIC_DEQUANT(2, 7),
    SYMMETRIC_DEQUANT(3, 7),
    SYMMETRIC_DEQUANT(4, 7),
    SYMMETRIC_DEQUANT(5, 7),
    SYMMETRIC_DEQUANT(6, 7),
};
/**
 * Table 7.23
 */
const int ff_ac3_bap5_mantissas[15 + 1] = {
    SYMMETRIC_DEQUANT(0,  15),
    SYMMETRIC_DEQUANT(1,  15),
    SYMMETRIC_DEQUANT(2,  15),
    SYMMETRIC_DEQUANT(3,  15),
    SYMMETRIC_DEQUANT(4,  15),
    SYMMETRIC_DEQUANT(5,  15),
    SYMMETRIC_DEQUANT(6,  15),
    SYMMETRIC_DEQUANT(7,  15),
    SYMMETRIC_DEQUANT(8,  15),
    SYMMETRIC_DEQUANT(9,  15),
    SYMMETRIC_DEQUANT(10, 15),
    SYMMETRIC_DEQUANT(11, 15),
    SYMMETRIC_DEQUANT(12, 15),
    SYMMETRIC_DEQUANT(13, 15),
    SYMMETRIC_DEQUANT(14, 15),
};

int ff_ac3_bap1_mantissas[32][3];
int ff_ac3_bap2_mantissas[128][3];
int ff_ac3_bap4_mantissas[128][2];

static inline int
symmetric_dequant(int code, int levels)
{
    return SYMMETRIC_DEQUANT(code, levels);
}

static av_cold void ac3_init_static(void)
{
    /* generate table for ungrouping 3 values in 7 bits
       reference: Section 7.1.3 Exponent Decoding */
    for (int i = 0; i < 128; ++i) {
        ff_ac3_ungroup_3_in_7_bits_tab[i][0] =  i / 25;
        ff_ac3_ungroup_3_in_7_bits_tab[i][1] = (i % 25) / 5;
        ff_ac3_ungroup_3_in_7_bits_tab[i][2] = (i % 25) % 5;
    }

    /* generate grouped mantissa tables
       reference: Section 7.3.5 Ungrouping of Mantissas */
    for (int i = 0; i < 32; ++i) {
        /* bap=1 mantissas */
        ff_ac3_bap1_mantissas[i][0] = symmetric_dequant(ff_ac3_ungroup_3_in_5_bits_tab[i][0], 3);
        ff_ac3_bap1_mantissas[i][1] = symmetric_dequant(ff_ac3_ungroup_3_in_5_bits_tab[i][1], 3);
        ff_ac3_bap1_mantissas[i][2] = symmetric_dequant(ff_ac3_ungroup_3_in_5_bits_tab[i][2], 3);
    }
    for (int i = 0; i < 128; ++i) {
        /* bap=2 mantissas */
        ff_ac3_bap2_mantissas[i][0] = symmetric_dequant(ff_ac3_ungroup_3_in_7_bits_tab[i][0], 5);
        ff_ac3_bap2_mantissas[i][1] = symmetric_dequant(ff_ac3_ungroup_3_in_7_bits_tab[i][1], 5);
        ff_ac3_bap2_mantissas[i][2] = symmetric_dequant(ff_ac3_ungroup_3_in_7_bits_tab[i][2], 5);

        /* bap=4 mantissas */
        ff_ac3_bap4_mantissas[i][0] = symmetric_dequant(i / 11, 11);
        ff_ac3_bap4_mantissas[i][1] = symmetric_dequant(i % 11, 11);
    }
}

av_cold void ff_ac3_init_static(void)
{
    static AVOnce ac3_init_static_once = AV_ONCE_INIT;
    ff_thread_once(&ac3_init_static_once, ac3_init_static);
}

/**
 * Quantization table: levels for symmetric. bits for asymmetric.
 * reference: Table 7.18 Mapping of bap to Quantizer
 */
const uint8_t ff_ac3_quantization_tab[16] = {
    0, 3, 5, 7, 11, 15,
    5, 6, 7, 8, 9, 10, 11, 12, 14, 16
};

/**
 * Table for default stereo downmixing coefficients
 * reference: Section 7.8.2 Downmixing Into Two Channels
 */
const uint8_t ff_ac3_default_coeffs[8][5][2] = {
    { { 2, 7 }, { 7, 2 },                               },
    { { 4, 4 },                                         },
    { { 2, 7 }, { 7, 2 },                               },
    { { 2, 7 }, { 5, 5 }, { 7, 2 },                     },
    { { 2, 7 }, { 7, 2 }, { 6, 6 },                     },
    { { 2, 7 }, { 5, 5 }, { 7, 2 }, { 8, 8 },           },
    { { 2, 7 }, { 7, 2 }, { 6, 7 }, { 7, 6 },           },
    { { 2, 7 }, { 5, 5 }, { 7, 2 }, { 6, 7 }, { 7, 6 }, },
};

const uint8_t ff_eac3_hebap_tab[64] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 8,
    8, 8, 9, 9, 9, 10, 10, 10, 10, 11,
    11, 11, 11, 12, 12, 12, 12, 13, 13, 13,
    13, 14, 14, 14, 14, 15, 15, 15, 15, 16,
    16, 16, 16, 17, 17, 17, 17, 18, 18, 18,
    18, 18, 18, 18, 18, 19, 19, 19, 19, 19,
    19, 19, 19, 19,
};

/**
 * Table E2.15 Default Spectral Extension Banding Structure
 */
const uint8_t ff_eac3_default_spx_band_struct[17] =
{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 };

/** Adjustments in dB gain (LFE, +10 to -21 dB) */
const float ff_eac3_gain_levels_lfe[32] = {
    3.162275, 2.818382, 2.511886, 2.238719, 1.995261, 1.778278, 1.584893,
    1.412536, 1.258924, 1.122018, 1.000000, 0.891251, 0.794328, 0.707946,
    0.630957, 0.562341, 0.501187, 0.446683, 0.398107, 0.354813, 0.316227,
    0.281838, 0.251188, 0.223872, 0.199526, 0.177828, 0.158489, 0.141253,
    0.125892, 0.112201, 0.100000, 0.089125
};
