/*
 * Apple ProRes compatible decoder
 *
 * Copyright (c) 2010-2011 Maxim Poliakovski
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

#include "proresdata.h"

const uint8_t ff_prores_progressive_scan[64] = {
     0,  1,  8,  9,  2,  3, 10, 11,
    16, 17, 24, 25, 18, 19, 26, 27,
     4,  5, 12, 20, 13,  6,  7, 14,
    21, 28, 29, 22, 15, 23, 30, 31,
    32, 33, 40, 48, 41, 34, 35, 42,
    49, 56, 57, 50, 43, 36, 37, 44,
    51, 58, 59, 52, 45, 38, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

const uint8_t ff_prores_interlaced_scan[64] = {
     0,  8,  1,  9, 16, 24, 17, 25,
     2, 10,  3, 11, 18, 26, 19, 27,
    32, 40, 33, 34, 41, 48, 56, 49,
    42, 35, 43, 50, 57, 58, 51, 59,
     4, 12,  5,  6, 13, 20, 28, 21,
    14,  7, 15, 22, 29, 36, 44, 37,
    30, 23, 31, 38, 45, 52, 60, 53,
    46, 39, 47, 54, 61, 62, 55, 63
};


const uint8_t ff_prores_dc_codebook[4] = {
    0x04, // rice_order = 0, exp_golomb_order = 1, switch_bits = 0
    0x28, // rice_order = 1, exp_golomb_order = 2, switch_bits = 0
    0x4D, // rice_order = 2, exp_golomb_order = 3, switch_bits = 1
    0x70  // rice_order = 3, exp_golomb_order = 4, switch_bits = 0
};

const uint8_t ff_prores_ac_codebook[7] = {
    0x04, // rice_order = 0, exp_golomb_order = 1, switch_bits = 0
    0x28, // rice_order = 1, exp_golomb_order = 2, switch_bits = 0
    0x4C, // rice_order = 2, exp_golomb_order = 3, switch_bits = 0
    0x05, // rice_order = 0, exp_golomb_order = 1, switch_bits = 1
    0x29, // rice_order = 1, exp_golomb_order = 2, switch_bits = 1
    0x06, // rice_order = 0, exp_golomb_order = 1, switch_bits = 2
    0x0A, // rice_order = 0, exp_golomb_order = 2, switch_bits = 2
};

/**
 * Lookup tables for adaptive switching between codebooks
 * according with previous run/level value.
 */
const uint8_t ff_prores_run_to_cb_index[16] =
    { 5, 5, 3, 3, 0, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 2 };

const uint8_t ff_prores_lev_to_cb_index[10] = { 0, 6, 3, 5, 0, 1, 1, 1, 1, 2 };
