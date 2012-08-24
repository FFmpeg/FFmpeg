/*
 * MPEG Audio common tables
 * copyright (c) 2002 Fabrice Bellard
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
 * mpeg audio layer common tables.
 */

#include "mpegaudiodata.h"


const uint16_t avpriv_mpa_bitrate_tab[2][3][15] = {
    { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384 },
      {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320 } },
    { {0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160},
      {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160}
    }
};

const uint16_t avpriv_mpa_freq_tab[3] = { 44100, 48000, 32000 };

/*******************************************************/
/* layer 2 tables */

const int ff_mpa_sblimit_table[5] = { 27 , 30 , 8, 12 , 30 };

const int ff_mpa_quant_steps[17] = {
    3,     5,    7,    9,    15,
    31,    63,  127,  255,   511,
    1023,  2047, 4095, 8191, 16383,
    32767, 65535
};

/* we use a negative value if grouped */
const int ff_mpa_quant_bits[17] = {
    -5,  -7,  3, -10, 4,
     5,  6,  7,  8,  9,
    10, 11, 12, 13, 14,
    15, 16
};

/* encoding tables which give the quantization index. Note how it is
   possible to store them efficiently ! */
static const unsigned char alloc_table_1[] = {
 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
};

static const unsigned char alloc_table_3[] = {
 4,  0,  1,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
 4,  0,  1,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
};

static const unsigned char alloc_table_4[] = {
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
};

const unsigned char * const ff_mpa_alloc_tables[5] =
{ alloc_table_1, alloc_table_1, alloc_table_3, alloc_table_3, alloc_table_4, };
