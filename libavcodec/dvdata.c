/*
 * Constants for DV codec
 * Copyright (c) 2002 Fabrice Bellard
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
 * Constants for DV codec.
 */

#include "avcodec.h"
#include "dvdata.h"

/* unquant tables (not used directly) */
const uint8_t ff_dv_quant_shifts[22][4] = {
  { 3,3,4,4 },
  { 3,3,4,4 },
  { 2,3,3,4 },
  { 2,3,3,4 },
  { 2,2,3,3 },
  { 2,2,3,3 },
  { 1,2,2,3 },
  { 1,2,2,3 },
  { 1,1,2,2 },
  { 1,1,2,2 },
  { 0,1,1,2 },
  { 0,1,1,2 },
  { 0,0,1,1 },
  { 0,0,1,1 },
  { 0,0,0,1 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
  { 0,0,0,0 },
};

const uint8_t ff_dv_quant_offset[4] = { 6,  3,  0,  1 };

const int ff_dv_iweight_88[64] = {
 32768, 16710, 16710, 17735, 17015, 17735, 18197, 18079,
 18079, 18197, 18725, 18559, 19196, 18559, 18725, 19284,
 19108, 19692, 19692, 19108, 19284, 21400, 19645, 20262,
 20214, 20262, 19645, 21400, 22733, 21845, 20867, 20815,
 20815, 20867, 21845, 22733, 23173, 23173, 21400, 21400,
 21400, 23173, 23173, 24600, 23764, 22017, 22017, 23764,
 24600, 25267, 24457, 22672, 24457, 25267, 25971, 25191,
 25191, 25971, 26715, 27962, 26715, 29642, 29642, 31536,
};
const int ff_dv_iweight_248[64] = {
 32768, 17735, 16710, 18079, 18725, 21400, 17735, 19196,
 19108, 21845, 16384, 17735, 18725, 21400, 16710, 18079,
 20262, 23173, 18197, 19692, 18725, 20262, 20815, 23764,
 17735, 19196, 19108, 21845, 20262, 23173, 18197, 19692,
 21400, 24457, 19284, 20867, 21400, 23173, 22017, 25191,
 18725, 20262, 20815, 23764, 21400, 24457, 19284, 20867,
 24457, 27962, 22733, 24600, 25971, 29642, 21400, 23173,
 22017, 25191, 24457, 27962, 22733, 24600, 25971, 29642,
};

/**
 * The "inverse" DV100 weights are actually just the spec weights (zig-zagged).
 */
const int ff_dv_iweight_1080_y[64] = {
    128,  16,  16,  17,  17,  17,  18,  18,
     18,  18,  18,  18,  19,  18,  18,  19,
     19,  19,  19,  19,  19,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  45,  45,  42,  42,
     42,  45,  45,  48,  46,  43,  43,  46,
     48,  49,  48,  44,  48,  49, 101,  98,
     98, 101, 104, 109, 104, 116, 116, 123,
};
const int ff_dv_iweight_1080_c[64] = {
    128,  16,  16,  17,  17,  17,  25,  25,
     25,  25,  26,  25,  26,  25,  26,  26,
     26,  27,  27,  26,  26,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  91,  91,  84,  84,
     84,  91,  91,  96,  93,  86,  86,  93,
     96, 197, 191, 177, 191, 197, 203, 197,
    197, 203, 209, 219, 209, 232, 232, 246,
};
const int ff_dv_iweight_720_y[64] = {
    128,  16,  16,  17,  17,  17,  18,  18,
     18,  18,  18,  18,  19,  18,  18,  19,
     19,  19,  19,  19,  19,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  68,  68,  63,  63,
     63,  68,  68,  96,  92,  86,  86,  92,
     96,  98,  96,  88,  96,  98, 202, 196,
    196, 202, 208, 218, 208, 232, 232, 246,
};
const int ff_dv_iweight_720_c[64] = {
    128,  24,  24,  26,  26,  26,  36,  36,
     36,  36,  36,  36,  38,  36,  36,  38,
     38,  38,  38,  38,  38,  84,  76,  80,
     80,  80,  76,  84,  88,  86,  82,  82,
     82,  82,  86,  88, 182, 182, 168, 168,
    168, 182, 182, 192, 186, 192, 172, 186,
    192, 394, 382, 354, 382, 394, 406, 394,
    394, 406, 418, 438, 418, 464, 464, 492,
};

