/*
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * copyright (c) 2004 Maarten Daniels
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
 * H.261 tables.
 */

#include <stdint.h>

#include "mpegutils.h"
#include "rl.h"
#include "h261.h"

// H.261 VLC table for macroblock addressing
const uint8_t ff_h261_mba_code[35] = {
     1,  3,  2,  3,
     2,  3,  2,  7,
     6, 11, 10,  9,
     8,  7,  6, 23,
    22, 21, 20, 19,
    18, 35, 34, 33,
    32, 31, 30, 29,
    28, 27, 26, 25,
    24,
    15, // (MBA stuffing)
     1  // (start code)
};

const uint8_t ff_h261_mba_bits[35] = {
     1,  3,  3,  4,
     4,  5,  5,  7,
     7,  8,  8,  8,
     8,  8,  8, 10,
    10, 10, 10, 10,
    10, 11, 11, 11,
    11, 11, 11, 11,
    11, 11, 11, 11,
    11,
    11, // (MBA stuffing)
    16  // (start code)
};

// H.261 VLC table for macroblock type
const uint8_t ff_h261_mtype_code[10] = {
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1
};

const uint8_t ff_h261_mtype_bits[10] = {
    4, 7,  1, 5,
    9, 8, 10, 3,
    2, 6
};

const int ff_h261_mtype_map[10] = {
    MB_TYPE_INTRA4x4,
    MB_TYPE_INTRA4x4 | MB_TYPE_QUANT,
    MB_TYPE_CBP,
    MB_TYPE_CBP | MB_TYPE_QUANT,
    MB_TYPE_16x16,
    MB_TYPE_16x16 | MB_TYPE_CBP,
    MB_TYPE_16x16 | MB_TYPE_CBP | MB_TYPE_QUANT,
    MB_TYPE_16x16 | MB_TYPE_H261_FIL,
    MB_TYPE_16x16 | MB_TYPE_H261_FIL | MB_TYPE_CBP,
    MB_TYPE_16x16 | MB_TYPE_H261_FIL | MB_TYPE_CBP | MB_TYPE_QUANT
};

// H.261 VLC table for motion vectors
const uint8_t ff_h261_mv_tab[17][2] = {
    {  1, 1 }, {  1, 2 }, { 1, 3 }, {  1,  4 }, {  3,  6 }, {  5,  7 }, {  4,  7 }, {  3,  7 },
    { 11, 9 }, { 10, 9 }, { 9, 9 }, { 17, 10 }, { 16, 10 }, { 15, 10 }, { 14, 10 }, { 13, 10 }, { 12, 10 }
};

// H.261 VLC table for coded block pattern
const uint8_t ff_h261_cbp_tab[63][2] = {
    { 11, 5 }, {  9, 5 }, { 13, 6 }, { 13, 4 }, { 23, 7 }, { 19, 7 }, { 31, 8 }, { 12, 4 },
    { 22, 7 }, { 18, 7 }, { 30, 8 }, { 19, 5 }, { 27, 8 }, { 23, 8 }, { 19, 8 }, { 11, 4 },
    { 21, 7 }, { 17, 7 }, { 29, 8 }, { 17, 5 }, { 25, 8 }, { 21, 8 }, { 17, 8 }, { 15, 6 },
    { 15, 8 }, { 13, 8 }, {  3, 9 }, { 15, 5 }, { 11, 8 }, {  7, 8 }, {  7, 9 }, { 10, 4 },
    { 20, 7 }, { 16, 7 }, { 28, 8 }, { 14, 6 }, { 14, 8 }, { 12, 8 }, {  2, 9 }, { 16, 5 },
    { 24, 8 }, { 20, 8 }, { 16, 8 }, { 14, 5 }, { 10, 8 }, {  6, 8 }, {  6, 9 }, { 18, 5 },
    { 26, 8 }, { 22, 8 }, { 18, 8 }, { 13, 5 }, {  9, 8 }, {  5, 8 }, {  5, 9 }, { 12, 5 },
    {  8, 8 }, {  4, 8 }, {  4, 9 }, {  7, 3 }, { 10, 5 }, {  8, 5 }, { 12, 6 }
};

// H.261 VLC table for transform coefficients
static const uint16_t h261_tcoeff_vlc[65][2] = {
    {  0x2,  2 }, {  0x3,  2 }, {  0x4,  4 }, {  0x5,  5 },
    {  0x6,  7 }, { 0x26,  8 }, { 0x21,  8 }, {  0xa, 10 },
    { 0x1d, 12 }, { 0x18, 12 }, { 0x13, 12 }, { 0x10, 12 },
    { 0x1a, 13 }, { 0x19, 13 }, { 0x18, 13 }, { 0x17, 13 },
    {  0x3,  3 }, {  0x6,  6 }, { 0x25,  8 }, {  0xc, 10 },
    { 0x1b, 12 }, { 0x16, 13 }, { 0x15, 13 }, {  0x5,  4 },
    {  0x4,  7 }, {  0xb, 10 }, { 0x14, 12 }, { 0x14, 13 },
    {  0x7,  5 }, { 0x24,  8 }, { 0x1c, 12 }, { 0x13, 13 },
    {  0x6,  5 }, {  0xf, 10 }, { 0x12, 12 }, {  0x7,  6 },
    {  0x9, 10 }, { 0x12, 13 }, {  0x5,  6 }, { 0x1e, 12 },
    {  0x4,  6 }, { 0x15, 12 }, {  0x7,  7 }, { 0x11, 12 },
    {  0x5,  7 }, { 0x11, 13 }, { 0x27,  8 }, { 0x10, 13 },
    { 0x23,  8 }, { 0x22,  8 }, { 0x20,  8 }, {  0xe, 10 },
    {  0xd, 10 }, {  0x8, 10 }, { 0x1f, 12 }, { 0x1a, 12 },
    { 0x19, 12 }, { 0x17, 12 }, { 0x16, 12 }, { 0x1f, 13 },
    { 0x1e, 13 }, { 0x1d, 13 }, { 0x1c, 13 }, { 0x1b, 13 },
    {  0x1,  6 }  // escape
};

static const int8_t h261_tcoeff_level[64] = {
    0, 1,  2,  3,  4,  5,  6,  7,
    8, 9, 10, 11, 12, 13, 14, 15,
    1, 2,  3,  4,  5,  6,  7,  1,
    2, 3,  4,  5,  1,  2,  3,  4,
    1, 2,  3,  1,  2,  3,  1,  2,
    1, 2,  1,  2,  1,  2,  1,  2,
    1, 1,  1,  1,  1,  1,  1,  1,
    1, 1,  1,  1,  1,  1,  1,  1
};

static const int8_t h261_tcoeff_run[64] = {
     0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  1,
     1,  1,  1,  1,  1,  1,  2,  2,
     2,  2,  2,  3,  3,  3,  3,  4,
     4,  4,  5,  5,  5,  6,  6,  7,
     7,  8,  8,  9,  9, 10, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26
};

RLTable ff_h261_rl_tcoeff = {
    64,
    64,
    h261_tcoeff_vlc,
    h261_tcoeff_run,
    h261_tcoeff_level,
};
