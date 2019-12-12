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
 * Tables taken directly from the AC-3 spec.
 */

#include "ac3dec_data.h"
#include "ac3.h"

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
