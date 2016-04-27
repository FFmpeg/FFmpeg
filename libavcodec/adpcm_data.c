/*
 * Copyright (c) 2001-2003 The FFmpeg project
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * ADPCM tables
 */

#include <stdint.h>

/* ff_adpcm_step_table[] and ff_adpcm_index_table[] are from the ADPCM
   reference source */
/* This is the index table: */
const int8_t ff_adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

/**
 * This is the step table. Note that many programs use slight deviations from
 * this table, but such deviations are negligible:
 */
const int16_t ff_adpcm_step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

/* These are for MS-ADPCM */
/* ff_adpcm_AdaptationTable[], ff_adpcm_AdaptCoeff1[], and
   ff_adpcm_AdaptCoeff2[] are from libsndfile */
const int16_t ff_adpcm_AdaptationTable[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

/** Divided by 4 to fit in 8-bit integers */
const uint8_t ff_adpcm_AdaptCoeff1[] = {
    64, 128, 0, 48, 60, 115, 98
};

/** Divided by 4 to fit in 8-bit integers */
const int8_t ff_adpcm_AdaptCoeff2[] = {
    0, -64, 0, 16, 0, -52, -58
};

const int16_t ff_adpcm_yamaha_indexscale[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    230, 230, 230, 230, 307, 409, 512, 614
};

const int8_t ff_adpcm_yamaha_difflookup[] = {
     1,  3,  5,  7,  9,  11,  13,  15,
    -1, -3, -5, -7, -9, -11, -13, -15
};
