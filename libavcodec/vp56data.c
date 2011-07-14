/*
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
 * VP5 and VP6 compatible video decoder (common data)
 */

#include "vp56data.h"

const uint8_t vp56_b2p[]   = { 0, 0, 0, 0, 1, 2, 3, 3, 3, 3 };
const uint8_t vp56_b6to4[] = { 0, 0, 1, 1, 2, 3 };

const uint8_t vp56_coeff_parse_table[6][11] = {
    { 159,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    { 145, 165,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    { 140, 148, 173,   0,   0,   0,   0,   0,   0,   0,   0 },
    { 135, 140, 155, 176,   0,   0,   0,   0,   0,   0,   0 },
    { 130, 134, 141, 157, 180,   0,   0,   0,   0,   0,   0 },
    { 129, 130, 133, 140, 153, 177, 196, 230, 243, 254, 254 },
};

const uint8_t vp56_def_mb_types_stats[3][10][2] = {
    { {  69, 42 }, {   1,  2 }, {  1,   7 }, {  44, 42 }, {  6, 22 },
      {   1,  3 }, {   0,  2 }, {  1,   5 }, {   0,  1 }, {  0,  0 }, },
    { { 229,  8 }, {   1,  1 }, {  0,   8 }, {   0,  0 }, {  0,  0 },
      {   1,  2 }, {   0,  1 }, {  0,   0 }, {   1,  1 }, {  0,  0 }, },
    { { 122, 35 }, {   1,  1 }, {  1,   6 }, {  46, 34 }, {  0,  0 },
      {   1,  2 }, {   0,  1 }, {  0,   1 }, {   1,  1 }, {  0,  0 }, },
};

const VP56Tree vp56_pva_tree[] = {
    { 8, 0},
    { 4, 1},
    { 2, 2}, {-0}, {-1},
    { 2, 3}, {-2}, {-3},
    { 4, 4},
    { 2, 5}, {-4}, {-5},
    { 2, 6}, {-6}, {-7},
};

const VP56Tree vp56_pc_tree[] = {
    { 4, 6},
    { 2, 7}, {-0}, {-1},
    { 4, 8},
    { 2, 9}, {-2}, {-3},
    { 2,10}, {-4}, {-5},
};

const uint8_t vp56_coeff_bias[] = { 0, 1, 2, 3, 4, 5, 7, 11, 19, 35, 67 };
const uint8_t vp56_coeff_bit_length[] = { 0, 1, 2, 3, 4, 10 };
