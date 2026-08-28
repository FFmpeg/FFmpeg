/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder
 * @author Stefan Gehrer <stefan.gehrer@gmx.de>
 *
 * Clause, table and figure numbers refer to GY/T 257.1-2012.
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/intmath.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "cavs.h"
#include "codec_internal.h"
#include "profiles.h"
#include "decode.h"
#include "mathops.h"
#include "mpeg12data.h"
#include "put_bits.h"
#include "startcode.h"

static const uint8_t mv_scan[4] = {
    MV_FWD_X0, MV_FWD_X1,
    MV_FWD_X2, MV_FWD_X3
};

static const uint8_t cbp_tab[64][2] = {
  { 63,  0 }, { 15, 15 }, { 31, 63 }, { 47, 31 }, {  0, 16 }, { 14, 32 }, { 13, 47 }, { 11, 13 },
  {  7, 14 }, {  5, 11 }, { 10, 12 }, {  8,  5 }, { 12, 10 }, { 61,  7 }, {  4, 48 }, { 55,  3 },
  {  1,  2 }, {  2,  8 }, { 59,  4 }, {  3,  1 }, { 62, 61 }, {  9, 55 }, {  6, 59 }, { 29, 62 },
  { 45, 29 }, { 51, 27 }, { 23, 23 }, { 39, 19 }, { 27, 30 }, { 46, 28 }, { 53,  9 }, { 30,  6 },
  { 43, 60 }, { 37, 21 }, { 60, 44 }, { 16, 26 }, { 21, 51 }, { 28, 35 }, { 19, 18 }, { 35, 20 },
  { 42, 24 }, { 26, 53 }, { 44, 17 }, { 32, 37 }, { 58, 39 }, { 24, 45 }, { 20, 58 }, { 17, 43 },
  { 18, 42 }, { 48, 46 }, { 22, 36 }, { 33, 33 }, { 25, 34 }, { 49, 40 }, { 40, 52 }, { 36, 49 },
  { 34, 50 }, { 50, 56 }, { 52, 25 }, { 54, 22 }, { 41, 54 }, { 56, 57 }, { 38, 41 }, { 57, 38 }
};

static const uint8_t scan3x3[4] = { 4, 5, 7, 8 };

static const uint8_t dequant_shift[64] = {
  14, 14, 14, 14, 14, 14, 14, 14,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 12, 12, 12, 12, 12, 12, 12,
  11, 11, 11, 11, 11, 11, 11, 11,
  11, 10, 10, 10, 10, 10, 10, 10,
  10,  9,  9,  9,  9,  9,  9,  9,
  9,   8,  8,  8,  8,  8,  8,  8,
  7,   7,  7,  7,  7,  7,  7,  7
};

static const uint16_t dequant_mul[64] = {
  32768, 36061, 38968, 42495, 46341, 50535, 55437, 60424,
  32932, 35734, 38968, 42495, 46177, 50535, 55109, 59933,
  65535, 35734, 38968, 42577, 46341, 50617, 55027, 60097,
  32809, 35734, 38968, 42454, 46382, 50576, 55109, 60056,
  65535, 35734, 38968, 42495, 46320, 50515, 55109, 60076,
  65535, 35744, 38968, 42495, 46341, 50535, 55099, 60087,
  65535, 35734, 38973, 42500, 46341, 50535, 55109, 60097,
  32771, 35734, 38965, 42497, 46341, 50535, 55109, 60099
};

#define EOB 0, 0, 0

static const struct dec_2dvlc intra_dec[7] = {
    {
        { //level / run / table_inc
            {  1,  1,  1 }, { -1,  1,  1 }, {  1,  2,  1 }, { -1,  2,  1 }, {  1,  3,  1 }, { -1,  3, 1 },
            {  1,  4,  1 }, { -1,  4,  1 }, {  1,  5,  1 }, { -1,  5,  1 }, {  1,  6,  1 }, { -1,  6, 1 },
            {  1,  7,  1 }, { -1,  7,  1 }, {  1,  8,  1 }, { -1,  8,  1 }, {  1,  9,  1 }, { -1,  9, 1 },
            {  1, 10,  1 }, { -1, 10,  1 }, {  1, 11,  1 }, { -1, 11,  1 }, {  2,  1,  2 }, { -2,  1, 2 },
            {  1, 12,  1 }, { -1, 12,  1 }, {  1, 13,  1 }, { -1, 13,  1 }, {  1, 14,  1 }, { -1, 14, 1 },
            {  1, 15,  1 }, { -1, 15,  1 }, {  2,  2,  2 }, { -2,  2,  2 }, {  1, 16,  1 }, { -1, 16, 1 },
            {  1, 17,  1 }, { -1, 17,  1 }, {  3,  1,  3 }, { -3,  1,  3 }, {  1, 18,  1 }, { -1, 18, 1 },
            {  1, 19,  1 }, { -1, 19,  1 }, {  2,  3,  2 }, { -2,  3,  2 }, {  1, 20,  1 }, { -1, 20, 1 },
            {  1, 21,  1 }, { -1, 21,  1 }, {  2,  4,  2 }, { -2,  4,  2 }, {  1, 22,  1 }, { -1, 22, 1 },
            {  2,  5,  2 }, { -2,  5,  2 }, {  1, 23,  1 }, { -1, 23,  1 }, {   EOB    }
        },
        //level_add
        { 0, 4, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, -1, -1, -1 },
        2, //golomb_order
        0, //inc_limit
        23, //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, {  2,  1,  1 }, { -2,  1,  1 },
            {  1,  3,  0 }, { -1,  3,  0 }, {     EOB    }, {  1,  4,  0 }, { -1,  4,  0 }, {  1,  5,  0 },
            { -1,  5,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, {  3,  1,  2 }, { -3,  1,  2 }, {  2,  2,  1 },
            { -2,  2,  1 }, {  1,  7,  0 }, { -1,  7,  0 }, {  1,  8,  0 }, { -1,  8,  0 }, {  1,  9,  0 },
            { -1,  9,  0 }, {  2,  3,  1 }, { -2,  3,  1 }, {  4,  1,  2 }, { -4,  1,  2 }, {  1, 10,  0 },
            { -1, 10,  0 }, {  1, 11,  0 }, { -1, 11,  0 }, {  2,  4,  1 }, { -2,  4,  1 }, {  3,  2,  2 },
            { -3,  2,  2 }, {  1, 12,  0 }, { -1, 12,  0 }, {  2,  5,  1 }, { -2,  5,  1 }, {  5,  1,  3 },
            { -5,  1,  3 }, {  1, 13,  0 }, { -1, 13,  0 }, {  2,  6,  1 }, { -2,  6,  1 }, {  1, 14,  0 },
            { -1, 14,  0 }, {  2,  7,  1 }, { -2,  7,  1 }, {  2,  8,  1 }, { -2,  8,  1 }, {  3,  3,  2 },
            { -3,  3,  2 }, {  6,  1,  3 }, { -6,  1,  3 }, {  1, 15,  0 }, { -1, 15,  0 }
        },
        //level_add
        { 0, 7, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        1, //inc_limit
        15, //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {  2,  1,  0 }, { -2,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 },
            {  3,  1,  1 }, { -3,  1,  1 }, {     EOB    }, {  1,  3,  0 }, { -1,  3,  0 }, {  2,  2,  0 },
            { -2,  2,  0 }, {  4,  1,  1 }, { -4,  1,  1 }, {  1,  4,  0 }, { -1,  4,  0 }, {  5,  1,  2 },
            { -5,  1,  2 }, {  1,  5,  0 }, { -1,  5,  0 }, {  3,  2,  1 }, { -3,  2,  1 }, {  2,  3,  0 },
            { -2,  3,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, {  6,  1,  2 }, { -6,  1,  2 }, {  2,  4,  0 },
            { -2,  4,  0 }, {  1,  7,  0 }, { -1,  7,  0 }, {  4,  2,  1 }, { -4,  2,  1 }, {  7,  1,  2 },
            { -7,  1,  2 }, {  3,  3,  1 }, { -3,  3,  1 }, {  2,  5,  0 }, { -2,  5,  0 }, {  1,  8,  0 },
            { -1,  8,  0 }, {  2,  6,  0 }, { -2,  6,  0 }, {  8,  1,  3 }, { -8,  1,  3 }, {  1,  9,  0 },
            { -1,  9,  0 }, {  5,  2,  2 }, { -5,  2,  2 }, {  3,  4,  1 }, { -3,  4,  1 }, {  2,  7,  0 },
            { -2,  7,  0 }, {  9,  1,  3 }, { -9,  1,  3 }, {  1, 10,  0 }, { -1, 10,  0 }
        },
        //level_add
        { 0, 10, 6, 4, 4, 3, 3, 3, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        2, //inc_limit
        10, //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {  2,  1,  0 }, { -2,  1,  0 }, {  3,  1,  0 }, { -3,  1,  0 },
            {  1,  2,  0 }, { -1,  2,  0 }, {     EOB    }, {  4,  1,  0 }, { -4,  1,  0 }, {  5,  1,  1 },
            { -5,  1,  1 }, {  2,  2,  0 }, { -2,  2,  0 }, {  1,  3,  0 }, { -1,  3,  0 }, {  6,  1,  1 },
            { -6,  1,  1 }, {  3,  2,  0 }, { -3,  2,  0 }, {  7,  1,  1 }, { -7,  1,  1 }, {  1,  4,  0 },
            { -1,  4,  0 }, {  8,  1,  2 }, { -8,  1,  2 }, {  2,  3,  0 }, { -2,  3,  0 }, {  4,  2,  0 },
            { -4,  2,  0 }, {  1,  5,  0 }, { -1,  5,  0 }, {  9,  1,  2 }, { -9,  1,  2 }, {  5,  2,  1 },
            { -5,  2,  1 }, {  2,  4,  0 }, { -2,  4,  0 }, { 10,  1,  2 }, {-10,  1,  2 }, {  3,  3,  0 },
            { -3,  3,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, { 11,  1,  3 }, {-11,  1,  3 }, {  6,  2,  1 },
            { -6,  2,  1 }, {  1,  7,  0 }, { -1,  7,  0 }, {  2,  5,  0 }, { -2,  5,  0 }, {  3,  4,  0 },
            { -3,  4,  0 }, { 12,  1,  3 }, {-12,  1,  3 }, {  4,  3,  0 }, { -4,  3,  0 }
         },
        //level_add
        { 0, 13, 7, 5, 4, 3, 2, 2, -1, -1, -1 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        4, //inc_limit
        7, //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {  2,  1,  0 }, { -2,  1,  0 }, {  3,  1,  0 }, { -3,  1,  0 },
            {     EOB    }, {  4,  1,  0 }, { -4,  1,  0 }, {  5,  1,  0 }, { -5,  1,  0 }, {  6,  1,  0 },
            { -6,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, {  7,  1,  0 }, { -7,  1,  0 }, {  8,  1,  1 },
            { -8,  1,  1 }, {  2,  2,  0 }, { -2,  2,  0 }, {  9,  1,  1 }, { -9,  1,  1 }, { 10,  1,  1 },
            {-10,  1,  1 }, {  1,  3,  0 }, { -1,  3,  0 }, {  3,  2,  0 }, { -3,  2,  0 }, { 11,  1,  2 },
            {-11,  1,  2 }, {  4,  2,  0 }, { -4,  2,  0 }, { 12,  1,  2 }, {-12,  1,  2 }, { 13,  1,  2 },
            {-13,  1,  2 }, {  5,  2,  0 }, { -5,  2,  0 }, {  1,  4,  0 }, { -1,  4,  0 }, {  2,  3,  0 },
            { -2,  3,  0 }, { 14,  1,  2 }, {-14,  1,  2 }, {  6,  2,  0 }, { -6,  2,  0 }, { 15,  1,  2 },
            {-15,  1,  2 }, { 16,  1,  2 }, {-16,  1,  2 }, {  3,  3,  0 }, { -3,  3,  0 }, {  1,  5,  0 },
            { -1,  5,  0 }, {  7,  2,  0 }, { -7,  2,  0 }, { 17,  1,  2 }, {-17,  1,  2 }
        },
        //level_add
        { 0,18, 8, 4, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        7, //inc_limit
        5, //max_run
    },
    {
        { //level / run
            {     EOB    }, {  1,  1,  0 }, { -1,  1,  0 }, {  2,  1,  0 }, { -2,  1,  0 }, {  3,  1,  0 },
            { -3,  1,  0 }, {  4,  1,  0 }, { -4,  1,  0 }, {  5,  1,  0 }, { -5,  1,  0 }, {  6,  1,  0 },
            { -6,  1,  0 }, {  7,  1,  0 }, { -7,  1,  0 }, {  8,  1,  0 }, { -8,  1,  0 }, {  9,  1,  0 },
            { -9,  1,  0 }, { 10,  1,  0 }, {-10,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, { 11,  1,  1 },
            {-11,  1,  1 }, { 12,  1,  1 }, {-12,  1,  1 }, { 13,  1,  1 }, {-13,  1,  1 }, {  2,  2,  0 },
            { -2,  2,  0 }, { 14,  1,  1 }, {-14,  1,  1 }, { 15,  1,  1 }, {-15,  1,  1 }, {  3,  2,  0 },
            { -3,  2,  0 }, { 16,  1,  1 }, {-16,  1,  1 }, {  1,  3,  0 }, { -1,  3,  0 }, { 17,  1,  1 },
            {-17,  1,  1 }, {  4,  2,  0 }, { -4,  2,  0 }, { 18,  1,  1 }, {-18,  1,  1 }, {  5,  2,  0 },
            { -5,  2,  0 }, { 19,  1,  1 }, {-19,  1,  1 }, { 20,  1,  1 }, {-20,  1,  1 }, {  6,  2,  0 },
            { -6,  2,  0 }, { 21,  1,  1 }, {-21,  1,  1 }, {  2,  3,  0 }, { -2,  3,  0 }
        },
        //level_add
        { 0, 22, 7, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        10, //inc_limit
        3, //max_run
    },
    {
        { //level / run
            {     EOB    }, {  1,  1,  0 }, { -1,  1,  0 }, {  2,  1,  0 }, { -2,  1,  0 }, {  3,  1,  0 },
            { -3,  1,  0 }, {  4,  1,  0 }, { -4,  1,  0 }, {  5,  1,  0 }, { -5,  1,  0 }, {  6,  1,  0 },
            { -6,  1,  0 }, {  7,  1,  0 }, { -7,  1,  0 }, {  8,  1,  0 }, { -8,  1,  0 }, {  9,  1,  0 },
            { -9,  1,  0 }, { 10,  1,  0 }, {-10,  1,  0 }, { 11,  1,  0 }, {-11,  1,  0 }, { 12,  1,  0 },
            {-12,  1,  0 }, { 13,  1,  0 }, {-13,  1,  0 }, { 14,  1,  0 }, {-14,  1,  0 }, { 15,  1,  0 },
            {-15,  1,  0 }, { 16,  1,  0 }, {-16,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, { 17,  1,  0 },
            {-17,  1,  0 }, { 18,  1,  0 }, {-18,  1,  0 }, { 19,  1,  0 }, {-19,  1,  0 }, { 20,  1,  0 },
            {-20,  1,  0 }, { 21,  1,  0 }, {-21,  1,  0 }, {  2,  2,  0 }, { -2,  2,  0 }, { 22,  1,  0 },
            {-22,  1,  0 }, { 23,  1,  0 }, {-23,  1,  0 }, { 24,  1,  0 }, {-24,  1,  0 }, { 25,  1,  0 },
            {-25,  1,  0 }, {  3,  2,  0 }, { -3,  2,  0 }, { 26,  1,  0 }, {-26,  1,  0 }
        },
        //level_add
        { 0, 27, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        INT_MAX, //inc_limit
        2, //max_run
    }
};

static const struct dec_2dvlc inter_dec[7] = {
    {
        { //level / run
            {  1,  1,  1 }, { -1,  1,  1 }, {  1,  2,  1 }, { -1,  2,  1 }, {  1,  3,  1 }, { -1,  3,  1 },
            {  1,  4,  1 }, { -1,  4,  1 }, {  1,  5,  1 }, { -1,  5,  1 }, {  1,  6,  1 }, { -1,  6,  1 },
            {  1,  7,  1 }, { -1,  7,  1 }, {  1,  8,  1 }, { -1,  8,  1 }, {  1,  9,  1 }, { -1,  9,  1 },
            {  1, 10,  1 }, { -1, 10,  1 }, {  1, 11,  1 }, { -1, 11,  1 }, {  1, 12,  1 }, { -1, 12,  1 },
            {  1, 13,  1 }, { -1, 13,  1 }, {  2,  1,  2 }, { -2,  1,  2 }, {  1, 14,  1 }, { -1, 14,  1 },
            {  1, 15,  1 }, { -1, 15,  1 }, {  1, 16,  1 }, { -1, 16,  1 }, {  1, 17,  1 }, { -1, 17,  1 },
            {  1, 18,  1 }, { -1, 18,  1 }, {  1, 19,  1 }, { -1, 19,  1 }, {  3,  1,  3 }, { -3,  1,  3 },
            {  1, 20,  1 }, { -1, 20,  1 }, {  1, 21,  1 }, { -1, 21,  1 }, {  2,  2,  2 }, { -2,  2,  2 },
            {  1, 22,  1 }, { -1, 22,  1 }, {  1, 23,  1 }, { -1, 23,  1 }, {  1, 24,  1 }, { -1, 24,  1 },
            {  1, 25,  1 }, { -1, 25,  1 }, {  1, 26,  1 }, { -1, 26,  1 }, {   EOB    }
        },
        //level_add
        { 0, 4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
        3, //golomb_order
        0, //inc_limit
        26 //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {     EOB    }, {  1,  2,  0 }, { -1,  2,  0 }, {  1,  3,  0 },
            { -1,  3,  0 }, {  1,  4,  0 }, { -1,  4,  0 }, {  1,  5,  0 }, { -1,  5,  0 }, {  1,  6,  0 },
            { -1,  6,  0 }, {  2,  1,  1 }, { -2,  1,  1 }, {  1,  7,  0 }, { -1,  7,  0 }, {  1,  8,  0 },
            { -1,  8,  0 }, {  1,  9,  0 }, { -1,  9,  0 }, {  1, 10,  0 }, { -1, 10,  0 }, {  2,  2,  1 },
            { -2,  2,  1 }, {  1, 11,  0 }, { -1, 11,  0 }, {  1, 12,  0 }, { -1, 12,  0 }, {  3,  1,  2 },
            { -3,  1,  2 }, {  1, 13,  0 }, { -1, 13,  0 }, {  1, 14,  0 }, { -1, 14,  0 }, {  2,  3,  1 },
            { -2,  3,  1 }, {  1, 15,  0 }, { -1, 15,  0 }, {  2,  4,  1 }, { -2,  4,  1 }, {  1, 16,  0 },
            { -1, 16,  0 }, {  2,  5,  1 }, { -2,  5,  1 }, {  1, 17,  0 }, { -1, 17,  0 }, {  4,  1,  3 },
            { -4,  1,  3 }, {  2,  6,  1 }, { -2,  6,  1 }, {  1, 18,  0 }, { -1, 18,  0 }, {  1, 19,  0 },
            { -1, 19,  0 }, {  2,  7,  1 }, { -2,  7,  1 }, {  3,  2,  2 }, { -3,  2,  2 }
        },
        //level_add
        { 0, 5, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        1, //inc_limit
        19 //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {     EOB    }, {  1,  2,  0 }, { -1,  2,  0 }, {  2,  1,  0 },
            { -2,  1,  0 }, {  1,  3,  0 }, { -1,  3,  0 }, {  1,  4,  0 }, { -1,  4,  0 }, {  3,  1,  1 },
            { -3,  1,  1 }, {  2,  2,  0 }, { -2,  2,  0 }, {  1,  5,  0 }, { -1,  5,  0 }, {  1,  6,  0 },
            { -1,  6,  0 }, {  1,  7,  0 }, { -1,  7,  0 }, {  2,  3,  0 }, { -2,  3,  0 }, {  4,  1,  2 },
            { -4,  1,  2 }, {  1,  8,  0 }, { -1,  8,  0 }, {  3,  2,  1 }, { -3,  2,  1 }, {  2,  4,  0 },
            { -2,  4,  0 }, {  1,  9,  0 }, { -1,  9,  0 }, {  1, 10,  0 }, { -1, 10,  0 }, {  5,  1,  2 },
            { -5,  1,  2 }, {  2,  5,  0 }, { -2,  5,  0 }, {  1, 11,  0 }, { -1, 11,  0 }, {  2,  6,  0 },
            { -2,  6,  0 }, {  1, 12,  0 }, { -1, 12,  0 }, {  3,  3,  1 }, { -3,  3,  1 }, {  6,  1,  2 },
            { -6,  1,  2 }, {  4,  2,  2 }, { -4,  2,  2 }, {  1, 13,  0 }, { -1, 13,  0 }, {  2,  7,  0 },
            { -2,  7,  0 }, {  3,  4,  1 }, { -3,  4,  1 }, {  1, 14,  0 }, { -1, 14,  0 }
        },
        //level_add
        { 0, 7, 5, 4, 4, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        2, //inc_limit
        14 //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {     EOB    }, {  2,  1,  0 }, { -2,  1,  0 }, {  1,  2,  0 },
            { -1,  2,  0 }, {  3,  1,  0 }, { -3,  1,  0 }, {  1,  3,  0 }, { -1,  3,  0 }, {  2,  2,  0 },
            { -2,  2,  0 }, {  4,  1,  1 }, { -4,  1,  1 }, {  1,  4,  0 }, { -1,  4,  0 }, {  5,  1,  1 },
            { -5,  1,  1 }, {  1,  5,  0 }, { -1,  5,  0 }, {  3,  2,  0 }, { -3,  2,  0 }, {  2,  3,  0 },
            { -2,  3,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, {  6,  1,  1 }, { -6,  1,  1 }, {  2,  4,  0 },
            { -2,  4,  0 }, {  1,  7,  0 }, { -1,  7,  0 }, {  4,  2,  1 }, { -4,  2,  1 }, {  7,  1,  2 },
            { -7,  1,  2 }, {  3,  3,  0 }, { -3,  3,  0 }, {  1,  8,  0 }, { -1,  8,  0 }, {  2,  5,  0 },
            { -2,  5,  0 }, {  8,  1,  2 }, { -8,  1,  2 }, {  1,  9,  0 }, { -1,  9,  0 }, {  3,  4,  0 },
            { -3,  4,  0 }, {  2,  6,  0 }, { -2,  6,  0 }, {  5,  2,  1 }, { -5,  2,  1 }, {  1, 10,  0 },
            { -1, 10,  0 }, {  9,  1,  2 }, { -9,  1,  2 }, {  4,  3,  1 }, { -4,  3,  1 }
        },
        //level_add
        { 0,10, 6, 5, 4, 3, 3, 2, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        3, //inc_limit
        10 //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {     EOB    }, {  2,  1,  0 }, { -2,  1,  0 }, {  3,  1,  0 },
            { -3,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, {  4,  1,  0 }, { -4,  1,  0 }, {  5,  1,  0 },
            { -5,  1,  0 }, {  2,  2,  0 }, { -2,  2,  0 }, {  1,  3,  0 }, { -1,  3,  0 }, {  6,  1,  0 },
            { -6,  1,  0 }, {  3,  2,  0 }, { -3,  2,  0 }, {  7,  1,  1 }, { -7,  1,  1 }, {  1,  4,  0 },
            { -1,  4,  0 }, {  8,  1,  1 }, { -8,  1,  1 }, {  2,  3,  0 }, { -2,  3,  0 }, {  4,  2,  0 },
            { -4,  2,  0 }, {  1,  5,  0 }, { -1,  5,  0 }, {  9,  1,  1 }, { -9,  1,  1 }, {  5,  2,  0 },
            { -5,  2,  0 }, {  2,  4,  0 }, { -2,  4,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, { 10,  1,  2 },
            {-10,  1,  2 }, {  3,  3,  0 }, { -3,  3,  0 }, { 11,  1,  2 }, {-11,  1,  2 }, {  1,  7,  0 },
            { -1,  7,  0 }, {  6,  2,  0 }, { -6,  2,  0 }, {  3,  4,  0 }, { -3,  4,  0 }, {  2,  5,  0 },
            { -2,  5,  0 }, { 12,  1,  2 }, {-12,  1,  2 }, {  4,  3,  0 }, { -4,  3,  0 }
        },
        //level_add
        { 0, 13, 7, 5, 4, 3, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        6, //inc_limit
        7  //max_run
    },
    {
        { //level / run
            {      EOB    }, {  1,  1,  0 }, {  -1,  1,  0 }, {  2,  1,  0 }, {  -2,  1,  0 }, {  3,  1,  0 },
            {  -3,  1,  0 }, {  4,  1,  0 }, {  -4,  1,  0 }, {  5,  1,  0 }, {  -5,  1,  0 }, {  1,  2,  0 },
            {  -1,  2,  0 }, {  6,  1,  0 }, {  -6,  1,  0 }, {  7,  1,  0 }, {  -7,  1,  0 }, {  8,  1,  0 },
            {  -8,  1,  0 }, {  2,  2,  0 }, {  -2,  2,  0 }, {  9,  1,  0 }, {  -9,  1,  0 }, {  1,  3,  0 },
            {  -1,  3,  0 }, { 10,  1,  1 }, { -10,  1,  1 }, {  3,  2,  0 }, {  -3,  2,  0 }, { 11,  1,  1 },
            { -11,  1,  1 }, {  4,  2,  0 }, {  -4,  2,  0 }, { 12,  1,  1 }, { -12,  1,  1 }, {  1,  4,  0 },
            {  -1,  4,  0 }, {  2,  3,  0 }, {  -2,  3,  0 }, { 13,  1,  1 }, { -13,  1,  1 }, {  5,  2,  0 },
            {  -5,  2,  0 }, { 14,  1,  1 }, { -14,  1,  1 }, {  6,  2,  0 }, {  -6,  2,  0 }, {  1,  5,  0 },
            {  -1,  5,  0 }, { 15,  1,  1 }, { -15,  1,  1 }, {  3,  3,  0 }, {  -3,  3,  0 }, { 16,  1,  1 },
            { -16,  1,  1 }, {  2,  4,  0 }, {  -2,  4,  0 }, {  7,  2,  0 }, {  -7,  2,  0 }
        },
        //level_add
        { 0, 17, 8, 4, 3, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        9, //inc_limit
        5  //max_run
    },
    {
        { //level / run
            {      EOB    }, {  1,  1,  0 }, {  -1,  1,  0 }, {  2,  1,  0 }, {  -2,  1,  0 }, {   3,  1,  0 },
            {  -3,  1,  0 }, {  4,  1,  0 }, {  -4,  1,  0 }, {  5,  1,  0 }, {  -5,  1,  0 }, {   6,  1,  0 },
            {  -6,  1,  0 }, {  7,  1,  0 }, {  -7,  1,  0 }, {  1,  2,  0 }, {  -1,  2,  0 }, {   8,  1,  0 },
            {  -8,  1,  0 }, {  9,  1,  0 }, {  -9,  1,  0 }, { 10,  1,  0 }, { -10,  1,  0 }, {  11,  1,  0 },
            { -11,  1,  0 }, { 12,  1,  0 }, { -12,  1,  0 }, {  2,  2,  0 }, {  -2,  2,  0 }, {  13,  1,  0 },
            { -13,  1,  0 }, {  1,  3,  0 }, {  -1,  3,  0 }, { 14,  1,  0 }, { -14,  1,  0 }, {  15,  1,  0 },
            { -15,  1,  0 }, {  3,  2,  0 }, {  -3,  2,  0 }, { 16,  1,  0 }, { -16,  1,  0 }, {  17,  1,  0 },
            { -17,  1,  0 }, { 18,  1,  0 }, { -18,  1,  0 }, {  4,  2,  0 }, {  -4,  2,  0 }, {  19,  1,  0 },
            { -19,  1,  0 }, { 20,  1,  0 }, { -20,  1,  0 }, {  2,  3,  0 }, {  -2,  3,  0 }, {   1,  4,  0 },
            {  -1,  4,  0 }, {  5,  2,  0 }, {  -5,  2,  0 }, { 21,  1,  0 }, { -21,  1,  0 }
        },
        //level_add
        { 0, 22, 6, 3, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        2, //golomb_order
        INT_MAX, //inc_limit
        4 //max_run
    }
};

static const struct dec_2dvlc chroma_dec[5] = {
    {
        { //level / run
            {  1,  1,  1 }, { -1,  1,  1 }, {  1,  2,  1 }, { -1,  2,  1 }, {  1,  3,  1 }, { -1,  3,  1 },
            {  1,  4,  1 }, { -1,  4,  1 }, {  1,  5,  1 }, { -1,  5,  1 }, {  1,  6,  1 }, { -1,  6,  1 },
            {  1,  7,  1 }, { -1,  7,  1 }, {  2,  1,  2 }, { -2,  1,  2 }, {  1,  8,  1 }, { -1,  8,  1 },
            {  1,  9,  1 }, { -1,  9,  1 }, {  1, 10,  1 }, { -1, 10,  1 }, {  1, 11,  1 }, { -1, 11,  1 },
            {  1, 12,  1 }, { -1, 12,  1 }, {  1, 13,  1 }, { -1, 13,  1 }, {  1, 14,  1 }, { -1, 14,  1 },
            {  1, 15,  1 }, { -1, 15,  1 }, {  3,  1,  3 }, { -3,  1,  3 }, {  1, 16,  1 }, { -1, 16,  1 },
            {  1, 17,  1 }, { -1, 17,  1 }, {  1, 18,  1 }, { -1, 18,  1 }, {  1, 19,  1 }, { -1, 19,  1 },
            {  1, 20,  1 }, { -1, 20,  1 }, {  1, 21,  1 }, { -1, 21,  1 }, {  1, 22,  1 }, { -1, 22,  1 },
            {  2,  2,  2 }, { -2,  2,  2 }, {  1, 23,  1 }, { -1, 23,  1 }, {  1, 24,  1 }, { -1, 24,  1 },
            {  1, 25,  1 }, { -1, 25,  1 }, {  4,  1,  3 }, { -4,  1,  3 }, {   EOB    }
        },
        //level_add
        { 0, 5, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, -1 },
        2, //golomb_order
        0, //inc_limit
        25 //max_run
    },
    {
        { //level / run
            {     EOB    }, {  1,  1,  0 }, { -1,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, {  2,  1,  1 },
            { -2,  1,  1 }, {  1,  3,  0 }, { -1,  3,  0 }, {  1,  4,  0 }, { -1,  4,  0 }, {  1,  5,  0 },
            { -1,  5,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, {  3,  1,  2 }, { -3,  1,  2 }, {  1,  7,  0 },
            { -1,  7,  0 }, {  1,  8,  0 }, { -1,  8,  0 }, {  2,  2,  1 }, { -2,  2,  1 }, {  1,  9,  0 },
            { -1,  9,  0 }, {  1, 10,  0 }, { -1, 10,  0 }, {  1, 11,  0 }, { -1, 11,  0 }, {  4,  1,  2 },
            { -4,  1,  2 }, {  1, 12,  0 }, { -1, 12,  0 }, {  1, 13,  0 }, { -1, 13,  0 }, {  1, 14,  0 },
            { -1, 14,  0 }, {  2,  3,  1 }, { -2,  3,  1 }, {  1, 15,  0 }, { -1, 15,  0 }, {  2,  4,  1 },
            { -2,  4,  1 }, {  5,  1,  3 }, { -5,  1,  3 }, {  3,  2,  2 }, { -3,  2,  2 }, {  1, 16,  0 },
            { -1, 16,  0 }, {  1, 17,  0 }, { -1, 17,  0 }, {  1, 18,  0 }, { -1, 18,  0 }, {  2,  5,  1 },
            { -2,  5,  1 }, {  1, 19,  0 }, { -1, 19,  0 }, {  1, 20,  0 }, { -1, 20,  0 }
        },
        //level_add
        { 0, 6, 4, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, -1, -1, -1, -1, -1, -1 },
        0, //golomb_order
        1, //inc_limit
        20 //max_run
    },
    {
        { //level / run
            {  1,  1,  0 }, { -1,  1,  0 }, {     EOB    }, {  2,  1,  0 }, { -2,  1,  0 }, {  1,  2,  0 },
            { -1,  2,  0 }, {  3,  1,  1 }, { -3,  1,  1 }, {  1,  3,  0 }, { -1,  3,  0 }, {  4,  1,  1 },
            { -4,  1,  1 }, {  2,  2,  0 }, { -2,  2,  0 }, {  1,  4,  0 }, { -1,  4,  0 }, {  5,  1,  2 },
            { -5,  1,  2 }, {  1,  5,  0 }, { -1,  5,  0 }, {  3,  2,  1 }, { -3,  2,  1 }, {  2,  3,  0 },
            { -2,  3,  0 }, {  1,  6,  0 }, { -1,  6,  0 }, {  6,  1,  2 }, { -6,  1,  2 }, {  1,  7,  0 },
            { -1,  7,  0 }, {  2,  4,  0 }, { -2,  4,  0 }, {  7,  1,  2 }, { -7,  1,  2 }, {  1,  8,  0 },
            { -1,  8,  0 }, {  4,  2,  1 }, { -4,  2,  1 }, {  1,  9,  0 }, { -1,  9,  0 }, {  3,  3,  1 },
            { -3,  3,  1 }, {  2,  5,  0 }, { -2,  5,  0 }, {  2,  6,  0 }, { -2,  6,  0 }, {  8,  1,  2 },
            { -8,  1,  2 }, {  1, 10,  0 }, { -1, 10,  0 }, {  1, 11,  0 }, { -1, 11,  0 }, {  9,  1,  2 },
            { -9,  1,  2 }, {  5,  2,  2 }, { -5,  2,  2 }, {  3,  4,  1 }, { -3,  4,  1 },
        },
        //level_add
        { 0,10, 6, 4, 4, 3, 3, 2, 2, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        1, //golomb_order
        2, //inc_limit
        11 //max_run
    },
    {
        { //level / run
            {     EOB    }, {  1,  1,  0 }, { -1,  1,  0 }, {  2,  1,  0 }, { -2,  1,  0 }, {  3,  1,  0 },
            { -3,  1,  0 }, {  4,  1,  0 }, { -4,  1,  0 }, {  1,  2,  0 }, { -1,  2,  0 }, {  5,  1,  1 },
            { -5,  1,  1 }, {  2,  2,  0 }, { -2,  2,  0 }, {  6,  1,  1 }, { -6,  1,  1 }, {  1,  3,  0 },
            { -1,  3,  0 }, {  7,  1,  1 }, { -7,  1,  1 }, {  3,  2,  0 }, { -3,  2,  0 }, {  8,  1,  1 },
            { -8,  1,  1 }, {  1,  4,  0 }, { -1,  4,  0 }, {  2,  3,  0 }, { -2,  3,  0 }, {  9,  1,  1 },
            { -9,  1,  1 }, {  4,  2,  0 }, { -4,  2,  0 }, {  1,  5,  0 }, { -1,  5,  0 }, { 10,  1,  1 },
            {-10,  1,  1 }, {  3,  3,  0 }, { -3,  3,  0 }, {  5,  2,  1 }, { -5,  2,  1 }, {  2,  4,  0 },
            { -2,  4,  0 }, { 11,  1,  1 }, {-11,  1,  1 }, {  1,  6,  0 }, { -1,  6,  0 }, { 12,  1,  1 },
            {-12,  1,  1 }, {  1,  7,  0 }, { -1,  7,  0 }, {  6,  2,  1 }, { -6,  2,  1 }, { 13,  1,  1 },
            {-13,  1,  1 }, {  2,  5,  0 }, { -2,  5,  0 }, {  1,  8,  0 }, { -1,  8,  0 },
        },
        //level_add
        { 0, 14, 7, 4, 3, 3, 2, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        1, //golomb_order
        4, //inc_limit
        8  //max_run
    },
    {
        { //level / run
            {      EOB    }, {  1,  1,  0 }, {  -1,  1,  0 }, {  2,  1,  0 }, {  -2,  1,  0 }, {  3,  1,  0 },
            {  -3,  1,  0 }, {  4,  1,  0 }, {  -4,  1,  0 }, {  5,  1,  0 }, {  -5,  1,  0 }, {  6,  1,  0 },
            {  -6,  1,  0 }, {  7,  1,  0 }, {  -7,  1,  0 }, {  8,  1,  0 }, {  -8,  1,  0 }, {  1,  2,  0 },
            {  -1,  2,  0 }, {  9,  1,  0 }, {  -9,  1,  0 }, { 10,  1,  0 }, { -10,  1,  0 }, { 11,  1,  0 },
            { -11,  1,  0 }, {  2,  2,  0 }, {  -2,  2,  0 }, { 12,  1,  0 }, { -12,  1,  0 }, { 13,  1,  0 },
            { -13,  1,  0 }, {  3,  2,  0 }, {  -3,  2,  0 }, { 14,  1,  0 }, { -14,  1,  0 }, {  1,  3,  0 },
            {  -1,  3,  0 }, { 15,  1,  0 }, { -15,  1,  0 }, {  4,  2,  0 }, {  -4,  2,  0 }, { 16,  1,  0 },
            { -16,  1,  0 }, { 17,  1,  0 }, { -17,  1,  0 }, {  5,  2,  0 }, {  -5,  2,  0 }, {  1,  4,  0 },
            {  -1,  4,  0 }, {  2,  3,  0 }, {  -2,  3,  0 }, { 18,  1,  0 }, { -18,  1,  0 }, {  6,  2,  0 },
            {  -6,  2,  0 }, { 19,  1,  0 }, { -19,  1,  0 }, {  1,  5,  0 }, {  -1,  5,  0 },
        },
        //level_add
        { 0, 20, 7, 3, 2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        0, //golomb_order
        INT_MAX, //inc_limit
        5, //max_run
    }
};

#undef EOB

/*****************************************************************************
 *
 * motion vector prediction
 *
 ****************************************************************************/

static inline void store_mvs(AVSContext *h)
{
    h->col_mv[h->mbidx * 4 + 0] = h->mv[MV_FWD_X0];
    h->col_mv[h->mbidx * 4 + 1] = h->mv[MV_FWD_X1];
    h->col_mv[h->mbidx * 4 + 2] = h->mv[MV_FWD_X2];
    h->col_mv[h->mbidx * 4 + 3] = h->mv[MV_FWD_X3];
}

static inline void mv_pred_direct(AVSContext *h, cavs_vector *pmv_fw,
                                  cavs_vector *col_mv)
{
    cavs_vector *pmv_bw = pmv_fw + MV_BWD_OFFS;
    unsigned den = h->direct_den[col_mv->dist];
    int m = FF_SIGNBIT(col_mv->x);

    pmv_fw->dist = h->dist[1];
    pmv_bw->dist = h->dist[0];
    pmv_fw->ref = 1;
    pmv_bw->ref = 0;
    /* scale the co-located motion vector according to its temporal span */
    pmv_fw->x =     (((den + (den * col_mv->x * pmv_fw->dist ^ m) - m - 1) >> 14) ^ m) - m;
    pmv_bw->x = m - (((den + (den * col_mv->x * pmv_bw->dist ^ m) - m - 1) >> 14) ^ m);
    m = FF_SIGNBIT(col_mv->y);
    pmv_fw->y =     (((den + (den * col_mv->y * pmv_fw->dist ^ m) - m - 1) >> 14) ^ m) - m;
    pmv_bw->y = m - (((den + (den * col_mv->y * pmv_bw->dist ^ m) - m - 1) >> 14) ^ m);
}

static inline void mv_pred_sym(AVSContext *h, cavs_vector *src,
                               enum cavs_block size)
{
    cavs_vector *dst = src + MV_BWD_OFFS;

    /* backward mv is the scaled and negated forward mv */
    dst->x = -((src->x * h->sym_factor + 256) >> 9);
    dst->y = -((src->y * h->sym_factor + 256) >> 9);
    dst->ref = 0;
    dst->dist = h->dist[0];
    set_mvs(dst, size);
}

/*****************************************************************************
 *
 * residual data decoding
 *
 ****************************************************************************/

/** kth-order exponential golomb code */
static inline int get_ue_code(GetBitContext *gb, int order)
{
    unsigned ret = get_ue_golomb(gb);
    if (ret >= ((1U<<31)>>order)) {
        av_log(NULL, AV_LOG_ERROR, "get_ue_code: value too large\n");
        return AVERROR_INVALIDDATA;
    }
    if (order) {
        return (ret<<order) + get_bits(gb, order);
    }
    return ret;
}

static inline int dequant(AVSContext *h, int16_t *level_buf, uint8_t *run_buf,
                          int16_t *dst, int mul, int shift, int coeff_num)
{
    int round = 1 << (shift - 1);
    int pos = -1;
    const uint8_t *scantab = h->scantable;

    /* inverse scan and dequantization */
    while (--coeff_num >= 0) {
        pos += run_buf[coeff_num];
        if (pos > 63) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "position out of block bounds at pic %d MB(%d,%d)\n",
                   h->cur.poc, h->mbx, h->mby);
            return AVERROR_INVALIDDATA;
        }
        dst[scantab[pos]] = (level_buf[coeff_num] * mul + round) >> shift;
    }
    return 0;
}

/**
 * decode coefficients from one 8x8 block, dequantize, inverse transform
 *  and add them to sample block
 * @param r pointer to 2D VLC table
 * @param esc_golomb_order escape codes are k-golomb with this order k
 * @param qp quantizer
 * @param dst location of sample block
 * @param stride line stride in frame buffer
 */
static int decode_residual_block(AVSContext *h, GetBitContext *gb,
                                 const struct dec_2dvlc *r, int esc_golomb_order,
                                 int qp, uint8_t *dst, ptrdiff_t stride)
{
    int i, esc_code, level, mask, ret;
    unsigned int level_code, run;
    int16_t level_buf[65];
    uint8_t run_buf[65];
    int16_t *block = h->block;

    for (i = 0; i < 65; i++) {
        level_code = get_ue_code(gb, r->golomb_order);
        if (level_code >= ESCAPE_CODE) {
            run      = ((level_code - ESCAPE_CODE) >> 1) + 1;
            if(run > 64) {
                av_log(h->avctx, AV_LOG_ERROR, "run %d is too large\n", run);
                return AVERROR_INVALIDDATA;
            }
            esc_code = get_ue_code(gb, esc_golomb_order);
            if (esc_code < 0 || esc_code > 32767) {
                av_log(h->avctx, AV_LOG_ERROR, "esc_code invalid\n");
                return AVERROR_INVALIDDATA;
            }

            level    = esc_code + (run > r->max_run ? 1 : r->level_add[run]);
            while (level > r->inc_limit)
                r++;
            mask  = -(level_code & 1);
            level = (level ^ mask) - mask;
        } else {
            level = r->rltab[level_code][0];
            if (!level) //end of block signal
                break;
            run = r->rltab[level_code][1];
            r  += r->rltab[level_code][2];
        }
        level_buf[i] = level;
        run_buf[i]   = run;
    }
    ret = dequant(h, level_buf, run_buf, block, dequant_mul[qp],
                  dequant_shift[qp], i);
    if (ret >= 0)
        h->cdsp.cavs_idct8_add(dst, block, stride);
    /* dequant() can write before it fails, so always clear the block */
    h->bdsp.clear_block(block);
    return ret;
}


/*****************************************************************************
 *
 * advanced entropy coding (AEC), clause 8.4
 *
 ****************************************************************************/

/* Table 51: start index of each ae(v) element's context models */
#define AEC_MB_SKIP_RUN               0
#define AEC_MB_TYPE                   4
#define AEC_MB_PART_TYPE             19
#define AEC_INTRA_LUMA_PRED_MODE     22
#define AEC_INTRA_CHROMA_PRED_MODE   26
#define AEC_MB_REFERENCE_INDEX       30
#define AEC_MV_DIFF_X                36
#define AEC_MV_DIFF_Y                42
#define AEC_CBP                      48
#define AEC_MB_QP_DELTA              54
#define AEC_COEFF_FRAME_LUMA         58
#define AEC_COEFF_FRAME_CHROMA      124
#define AEC_COEFF_FIELD_LUMA        190
#define AEC_COEFF_FIELD_CHROMA      256

/* a conforming block's quantized coefficients span -2^(n+3)..2^(n+3)-1
 * with n the sample precision, 8 for both profiles (clause 9.6.2) */
#define CAVS_MAX_ABS_LEVEL          2048

/* one trans_coefficient set: 14 + 32 + 20 models, coeffRun from index 46 */
#define AEC_COEFF_NUM_CTX            66
#define AEC_COEFF_RUN                46
#define AEC_COEFF_RUN_NUM_CTX        20

/** Table 52, priIdx as a function of lMax, which saturates at 5. */
static const uint8_t cavs_aec_pri_idx[6] = { 0, 1, 2, 3, 3, 4 };

/**
 * Decode intra_luma_pred_mode (table 47) and derive IntraLumaPredMode
 * from predIntraPredMode per clause 9.4.4.2 a).
 */
static int aec_intra_luma_pred_mode(AVSContext *h, int pred)
{
    int v = 0;

    while (v < 4 &&
           !ff_cavs_aec_decode_decision(&h->aec, AEC_INTRA_LUMA_PRED_MODE + v))
        v++;

    if (v == 0)
        return pred;
    if (v == 4)             /* 4 stands for 0 (9.4.4.2 a) 2)) */
        v = 0;
    return v < pred ? v : v + 1;
}

/**
 * Decode intra_chroma_pred_mode (table 48, 8.4.4.2 e)).
 * @return IntraChromaPredMode, 0 to 3 (table 60)
 */
static int aec_intra_chroma_pred_mode(AVSContext *h)
{
    /* a and b of 8.4.4.2 e); non-intra neighbours hold INTRA_C_LP */
    const int a = (h->flags & A_AVAIL) &&
                  h->left_c_pred_mode != INTRA_C_LP;
    const int b = (h->flags & B_AVAIL) &&
                  h->top_c_pred_mode[h->mbx] != INTRA_C_LP;
    int v;

    if (!ff_cavs_aec_decode_decision(&h->aec,
                                     AEC_INTRA_CHROMA_PRED_MODE + a + b))
        v = 0;
    else if (!ff_cavs_aec_decode_decision(&h->aec,
                                          AEC_INTRA_CHROMA_PRED_MODE + 3))
        v = 1;
    else
        v = 2 + ff_cavs_aec_decode_decision(&h->aec,
                                            AEC_INTRA_CHROMA_PRED_MODE + 3);

    h->left_c_pred_mode        = v;
    h->top_c_pred_mode[h->mbx] = v;
    return v;
}

/**
 * Decode cbp (table 50, 8.4.4.2 h), clause 9.4.7).
 * @return MbCBP, a six bit mask over the block order numbers
 */
static int aec_cbp(AVSContext *h)
{
    const int left = (h->flags & A_AVAIL) ? ~h->left_cbp        : 0;
    const int top  = (h->flags & B_AVAIL) ? ~h->top_cbp[h->mbx] : 0;
    int cbp = 0;
    int a, b;

    a = (left >> 1) & 1;                        /* left macroblock, block 1 */
    b = (top  >> 2) & 1;                        /* above macroblock, block 2 */
    cbp |= ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + a + 2 * b) << 0;

    a = ~cbp & 1;                               /* this macroblock, block 0 */
    b = (top >> 3) & 1;                         /* above macroblock, block 3 */
    cbp |= ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + a + 2 * b) << 1;

    a = (left >> 3) & 1;                        /* left macroblock, block 3 */
    b = ~cbp & 1;                               /* this macroblock, block 0 */
    cbp |= ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + a + 2 * b) << 2;

    a = (~cbp >> 2) & 1;                        /* this macroblock, block 2 */
    b = (~cbp >> 1) & 1;                        /* this macroblock, block 1 */
    cbp |= ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + a + 2 * b) << 3;

    if (ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + 4)) {
        if (ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + 5))
            cbp |= 3 << 4;
        else
            cbp |= 1 << (4 + ff_cavs_aec_decode_decision(&h->aec, AEC_CBP + 5));
    }

    h->left_cbp        = cbp;
    h->top_cbp[h->mbx] = cbp;
    return cbp;
}

/**
 * Decode mb_qp_delta (table 44 unary, 8.4.4.2 i)) and map the code
 * number to the signed delta of clause 9.4.8.
 */
static int aec_mb_qp_delta(AVSContext *h, int *delta)
{
    int ctx = AEC_MB_QP_DELTA + (h->prev_delta_qp != 0);
    int v   = 0;

    while (!ff_cavs_aec_decode_decision(&h->aec, ctx)) {
        v++;
        ctx = AEC_MB_QP_DELTA + (v == 1 ? 2 : 3);
        if (v > 64) {
            av_log(h->avctx, AV_LOG_ERROR, "mb_qp_delta out of range\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (v)
        v = (v & 1) ? (v + 1) / 2 : -((v + 1) / 2);
    if (v < -32 || v > 31) {
        av_log(h->avctx, AV_LOG_ERROR, "mb_qp_delta out of range\n");
        return AVERROR_INVALIDDATA;
    }
    *delta = v;
    return 0;
}

/**
 * Decode the trans_coefficient pairs of one 8x8 block (8.4.4.2 j) and k)).
 * @param chroma selects the chroma half of the context sets of table 51
 */
static int decode_residual_block_aec(AVSContext *h, int chroma, int qp,
                                     uint8_t *dst, ptrdiff_t stride)
{
    int16_t level_buf[65];
    uint8_t run_buf[65];
    int16_t *block = h->block;
    int lmax = 0, pos = 0, i, base, ret;

    if (h->pic_structure)
        base = chroma ? AEC_COEFF_FRAME_CHROMA : AEC_COEFF_FRAME_LUMA;
    else
        base = chroma ? AEC_COEFF_FIELD_CHROMA : AEC_COEFF_FIELD_LUMA;

    for (i = 0; i < 65; i++) {
        const int pri = cavs_aec_pri_idx[FFMIN(lmax, 5)];
        int ctx_lvl = ff_cavs_aec_ctx_idx(base, AEC_COEFF_NUM_CTX,
                                          pri * 3 - (pri != 0));
        int ctx_run = ff_cavs_aec_ctx_idx(base + AEC_COEFF_RUN,
                                          AEC_COEFF_RUN_NUM_CTX, pri * 4);
        int abs_level, run_val;

        if (ctx_lvl < 0 || ctx_run < 0)
            return FFMIN(ctx_lvl, ctx_run);
        if (get_bits_left(&h->gb) <= 0)
            return AVERROR_INVALIDDATA;

        /* coeffLevel, table 44 unary; secIdx as in IEEE 1857-2023 table 36,
         * whose lMax == 0 column has no secIdx 2 */
        if (!lmax) {
            abs_level = 0;
            if (!ff_cavs_aec_decode_decision(&h->aec, ctx_lvl)) {
                do {
                    abs_level++;
                    if (abs_level >= CAVS_MAX_ABS_LEVEL) {
                        av_log(h->avctx, AV_LOG_ERROR, "AbsLevel too large\n");
                        return AVERROR_INVALIDDATA;
                    }
                } while (!ff_cavs_aec_decode_decision(&h->aec, ctx_lvl + 1));
            }
            abs_level++;                        /* clause 9.5.2 a) */
        } else {
            int ctx_w = ff_cavs_aec_ctx_idx(base, AEC_COEFF_NUM_CTX,
                                            14 + (pos >> 1));
            if (ctx_w < 0)
                return ctx_w;
            if (ff_cavs_aec_decode_decision_w(&h->aec, ctx_lvl, ctx_w))
                break;                          /* AbsLevel 0, end of block */
            abs_level = 1;
            if (!ff_cavs_aec_decode_decision(&h->aec, ctx_lvl + 1)) {
                do {
                    abs_level++;
                    if (abs_level > CAVS_MAX_ABS_LEVEL) {
                        av_log(h->avctx, AV_LOG_ERROR, "AbsLevel too large\n");
                        return AVERROR_INVALIDDATA;
                    }
                } while (!ff_cavs_aec_decode_decision(&h->aec, ctx_lvl + 2));
            }
        }

        if (ff_cavs_aec_decode_bypass(&h->aec))
            level_buf[i] = -abs_level;
        else
            level_buf[i] = abs_level;
        if (level_buf[i] < -CAVS_MAX_ABS_LEVEL ||
            level_buf[i] >= CAVS_MAX_ABS_LEVEL) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "coefficient level out of range\n");
            return AVERROR_INVALIDDATA;
        }

        ctx_run += abs_level > 1 ? 2 : 0;
        run_val  = 0;
        if (!ff_cavs_aec_decode_decision(&h->aec, ctx_run)) {
            do {
                run_val++;
                if (run_val > 64) {
                    av_log(h->avctx, AV_LOG_ERROR, "RunVal too large\n");
                    return AVERROR_INVALIDDATA;
                }
            } while (!ff_cavs_aec_decode_decision(&h->aec, ctx_run + 1));
        }
        run_buf[i] = run_val + 1;   /* scan positions consumed, 9.5.3 b) */

        /* lMax is updated with this coefficient before the next pair */
        lmax = FFMAX(lmax, abs_level);
        pos  = FFMIN(pos + run_val + 1, 63);
    }

    ret = dequant(h, level_buf, run_buf, block, dequant_mul[qp],
                  dequant_shift[qp], i);
    if (ret >= 0)
        h->cdsp.cavs_idct8_add(dst, block, stride);
    h->bdsp.clear_block(block);
    return ret;
}


/**
 * Tell whether the byte aligned 00 00 01 at @p offset is a start code: in a
 * de-emulated payload it may be a coincidence (annex A).
 */
static int is_start_code_offset(AVSContext *h, int offset)
{
    int lo = 0, hi = h->nb_stc_offset;

    if (h->gb.buffer != h->deemulated_buf)
        return 1;
    /* stc_offset[] is sorted; a crafted payload can hold one every 4 bytes */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        if (h->stc_offset[mid] == offset)
            return 1;
        if (h->stc_offset[mid] < offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

/**
 * Position the reader on the start code that ends an AEC slice: the first
 * one at or after the slice's own payload. The reader position is not
 * reliable: the arithmetic decoder's look ahead has run past it.
 */
static void aec_end_of_slice(AVSContext *h)
{
    GetBitContext *gb = &h->gb;
    const uint8_t *buf = gb->buffer;
    int size = gb->size_in_bits >> 3;
    int i    = h->aec_start;

    /* release gb; check_for_slice() waits for this */
    h->aec.gb = NULL;

    for (; i + 2 < size; i++)
        if (!buf[i] && !buf[i + 1] && buf[i + 2] == 1 &&
            is_start_code_offset(h, i))
            break;
    if (init_get_bits8(gb, buf, size) >= 0)
        skip_bits_long(gb, i * 8);
}

/**
 * Read mb_qp_delta when the macroblock has coefficients and the picture's qp
 * is not fixed, and keep PreviousDeltaQP for the AEC contexts (clause 9.4.8).
 */
static int decode_mb_qp_delta(AVSContext *h)
{
    int delta = 0;

    if (h->cbp && !h->qp_fixed) {
        if (h->aec_enable) {
            int ret = aec_mb_qp_delta(h, &delta);
            if (ret < 0)
                return ret;
        } else {
            delta = get_se_golomb(&h->gb);
        }
        h->qp = (h->qp + (unsigned)delta) & 63;
    }
    h->prev_delta_qp = delta;
    return 0;
}

/** Decode one 8x8 block's residual with the picture's entropy coder. */
static int decode_residual(AVSContext *h, const struct dec_2dvlc *r,
                           int esc_golomb_order, int chroma, int qp,
                           uint8_t *dst, ptrdiff_t stride)
{
    if (h->aec_enable)
        return decode_residual_block_aec(h, chroma, qp, dst, stride);
    return decode_residual_block(h, &h->gb, r, esc_golomb_order, qp, dst,
                                 stride);
}

static inline int decode_residual_chroma(AVSContext *h)
{
    const int qp = ff_cavs_chroma_qp[h->qp];
    int ret;

    if (h->cbp & (1 << 4)) {
        ret = decode_residual(h, chroma_dec, 0, 1, qp, h->cu, h->c_stride);
        if (ret < 0)
            return ret;
    }
    if (h->cbp & (1 << 5)) {
        ret = decode_residual(h, chroma_dec, 0, 1, qp, h->cv, h->c_stride);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static inline int decode_residual_inter(AVSContext *h)
{
    int block, ret;

    /* get coded block pattern */
    int cbp = get_ue_golomb(&h->gb);
    if (cbp > 63U) {
        av_log(h->avctx, AV_LOG_ERROR, "illegal inter cbp %d\n", cbp);
        return AVERROR_INVALIDDATA;
    }
    h->cbp = cbp_tab[cbp][1];

    /* get quantizer */
    if (h->cbp && !h->qp_fixed)
        h->qp = (h->qp + (unsigned)get_se_golomb(&h->gb)) & 63;
    for (block = 0; block < 4; block++)
        if (h->cbp & (1 << block)) {
            ret = decode_residual_block(h, &h->gb, inter_dec, 0, h->qp,
                                        h->cy + h->luma_scan[block],
                                        h->l_stride);
            if (ret < 0)
                return ret;
        }

    return decode_residual_chroma(h);
}

/*****************************************************************************
 *
 * macroblock level
 *
 ****************************************************************************/

static inline void set_mv_intra(AVSContext *h)
{
    h->mv[MV_FWD_X0] = CAVS_INTRA_MV;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = CAVS_INTRA_MV;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    if (h->cur.f->pict_type != AV_PICTURE_TYPE_B)
        h->col_type_base[h->mbidx] = I_8X8;
}

static int decode_mb_i(AVSContext *h, int cbp_code)
{
    GetBitContext *gb = &h->gb;
    unsigned pred_mode_uv;
    int block;
    uint8_t top[18];
    uint8_t *left = NULL;
    uint8_t *d;
    int ret;

    ff_cavs_init_mb(h);

    /* get intra prediction modes from stream */
    for (block = 0; block < 4; block++) {
        int nA, nB, predpred;
        int pos = scan3x3[block];

        nA = h->pred_mode_Y[pos - 1];
        nB = h->pred_mode_Y[pos - 3];
        predpred = FFMIN(nA, nB);
        if (predpred == NOT_AVAIL) // if either is not available
            predpred = INTRA_L_LP;
        if (h->aec_enable) {
            predpred = aec_intra_luma_pred_mode(h, predpred);
        } else if (!get_bits1(gb)) {
            int rem_mode = get_bits(gb, 2);
            predpred     = rem_mode + (rem_mode >= predpred);
        }
        h->pred_mode_Y[pos] = predpred;
    }
    if (h->aec_enable) {
        pred_mode_uv = aec_intra_chroma_pred_mode(h);
    } else {
        pred_mode_uv = get_ue_golomb_31(gb);
        if (pred_mode_uv > 6) {
            av_log(h->avctx, AV_LOG_ERROR, "illegal intra chroma pred mode\n");
            return AVERROR_INVALIDDATA;
        }
    }
    ff_cavs_modify_mb_i(h, &pred_mode_uv);

    /* get coded block pattern */
    if (h->aec_enable) {
        /* under AEC an intra MB codes cbp too (table 26, clause 9.4.7) */
        h->cbp = aec_cbp(h);
    } else {
        if (h->cur.f->pict_type == AV_PICTURE_TYPE_I)
            cbp_code = get_ue_golomb(gb);
        if (cbp_code > 63U) {
            av_log(h->avctx, AV_LOG_ERROR, "illegal intra cbp\n");
            return AVERROR_INVALIDDATA;
        }
        h->cbp = cbp_tab[cbp_code][0];
    }
    ret = decode_mb_qp_delta(h);
    if (ret < 0)
        return ret;

    /* luma intra prediction interleaved with residual decode/transform/add */
    for (block = 0; block < 4; block++) {
        d = h->cy + h->luma_scan[block];
        ff_cavs_load_intra_pred_luma(h, top, &left, block);
        h->intra_pred_l[h->pred_mode_Y[scan3x3[block]]]
            (d, top, left, h->l_stride);
        if (h->cbp & (1<<block)) {
            ret = decode_residual(h, intra_dec, 1, 0, h->qp, d, h->l_stride);
            if (ret < 0)
                return ret;
        }
    }

    /* chroma intra prediction */
    ff_cavs_load_intra_pred_chroma(h);
    h->intra_pred_c[pred_mode_uv](h->cu, &h->top_border_u[h->mbx * 10],
                                  h->left_border_u, h->c_stride);
    h->intra_pred_c[pred_mode_uv](h->cv, &h->top_border_v[h->mbx * 10],
                                  h->left_border_v, h->c_stride);

    ret = decode_residual_chroma(h);
    if (ret < 0)
        return ret;
    ff_cavs_filter(h, I_8X8);
    set_mv_intra(h);
    return 0;
}

static inline void set_intra_mode_default(AVSContext *h)
{
    if (h->stream_revision > 0) {
        h->pred_mode_Y[3] =  h->pred_mode_Y[6] = NOT_AVAIL;
        h->top_pred_Y[h->mbx * 2 + 0] = h->top_pred_Y[h->mbx * 2 + 1] = NOT_AVAIL;
    } else {
        h->pred_mode_Y[3] =  h->pred_mode_Y[6] = INTRA_L_LP;
        h->top_pred_Y[h->mbx * 2 + 0] = h->top_pred_Y[h->mbx * 2 + 1] = INTRA_L_LP;
    }
}

static int decode_mb_p(AVSContext *h, enum cavs_mb mb_type)
{
    GetBitContext *gb = &h->gb;
    int ref[4];
    int ret;

    ff_cavs_init_mb(h);
    switch (mb_type) {
    case P_SKIP:
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_PSKIP,  BLK_16X16, 0);
        break;
    case P_16X16:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, ref[0]);
        break;
    case P_16X8:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[2] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_TOP,    BLK_16X8, ref[0]);
        ff_cavs_mv(h, MV_FWD_X2, MV_FWD_A1, MV_PRED_LEFT,   BLK_16X8, ref[2]);
        break;
    case P_8X16:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[1] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_LEFT,     BLK_8X16, ref[0]);
        ff_cavs_mv(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_TOPRIGHT, BLK_8X16, ref[1]);
        break;
    case P_8X8:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[1] = h->ref_flag ? 0 : get_bits1(gb);
        ref[2] = h->ref_flag ? 0 : get_bits1(gb);
        ref[3] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_MEDIAN,   BLK_8X8, ref[0]);
        ff_cavs_mv(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_MEDIAN,   BLK_8X8, ref[1]);
        ff_cavs_mv(h, MV_FWD_X2, MV_FWD_X1, MV_PRED_MEDIAN,   BLK_8X8, ref[2]);
        ff_cavs_mv(h, MV_FWD_X3, MV_FWD_X0, MV_PRED_MEDIAN,   BLK_8X8, ref[3]);
        break;
    default:
        av_log(h->avctx, AV_LOG_ERROR, "Invalid mb_type %d in P frame\n",
               mb_type);
        return AVERROR_INVALIDDATA;
    }
    ff_cavs_inter(h, mb_type);
    set_intra_mode_default(h);
    store_mvs(h);
    if (mb_type != P_SKIP) {
        ret = decode_residual_inter(h);
        if (ret < 0)
            return ret;
    }
    ff_cavs_filter(h, mb_type);
    h->col_type_base[h->mbidx] = mb_type;

    return 0;
}

static int decode_mb_b(AVSContext *h, enum cavs_mb mb_type)
{
    int block;
    enum cavs_sub_mb sub_type[4];
    int flags;
    int ret;

    ff_cavs_init_mb(h);

    /* reset all MVs */
    h->mv[MV_FWD_X0] = CAVS_DIR_MV;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = CAVS_DIR_MV;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    switch (mb_type) {
    case B_SKIP:
    case B_DIRECT:
        if (!h->col_type_base[h->mbidx]) {
            /* intra MB at co-location, do in-plane prediction */
            ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_BSKIP, BLK_16X16, 1);
            ff_cavs_mv(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_BSKIP, BLK_16X16, 0);
        } else
            /* direct prediction from co-located P MB, block-wise */
            for (block = 0; block < 4; block++)
                mv_pred_direct(h, &h->mv[mv_scan[block]],
                               &h->col_mv[h->mbidx * 4 + block]);
        break;
    case B_FWD_16X16:
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, 1);
        break;
    case B_SYM_16X16:
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, 1);
        mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_16X16);
        break;
    case B_BWD_16X16:
        ff_cavs_mv(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_MEDIAN, BLK_16X16, 0);
        break;
    case B_8X8:
#define TMP_UNUSED_INX  7
        flags = 0;
        for (block = 0; block < 4; block++)
            sub_type[block] = get_bits(&h->gb, 2);
        for (block = 0; block < 4; block++) {
            switch (sub_type[block]) {
            case B_SUB_DIRECT:
                if (!h->col_type_base[h->mbidx]) {
                    /* intra MB at co-location, do in-plane prediction */
                    if(flags==0) {
                        // if col-MB is a Intra MB, current Block size is 16x16.
                        // AVS standard section 9.9.1
                        if(block>0){
                            h->mv[TMP_UNUSED_INX              ] = h->mv[MV_FWD_X0              ];
                            h->mv[TMP_UNUSED_INX + MV_BWD_OFFS] = h->mv[MV_FWD_X0 + MV_BWD_OFFS];
                        }
                        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2,
                                   MV_PRED_BSKIP, BLK_8X8, 1);
                        ff_cavs_mv(h, MV_FWD_X0+MV_BWD_OFFS,
                                   MV_FWD_C2+MV_BWD_OFFS,
                                   MV_PRED_BSKIP, BLK_8X8, 0);
                        if(block>0) {
                            flags = mv_scan[block];
                            h->mv[flags              ] = h->mv[MV_FWD_X0              ];
                            h->mv[flags + MV_BWD_OFFS] = h->mv[MV_FWD_X0 + MV_BWD_OFFS];
                            h->mv[MV_FWD_X0              ] = h->mv[TMP_UNUSED_INX              ];
                            h->mv[MV_FWD_X0 + MV_BWD_OFFS] = h->mv[TMP_UNUSED_INX + MV_BWD_OFFS];
                        } else
                            flags = MV_FWD_X0;
                    } else {
                        h->mv[mv_scan[block]              ] = h->mv[flags              ];
                        h->mv[mv_scan[block] + MV_BWD_OFFS] = h->mv[flags + MV_BWD_OFFS];
                    }
                } else
                    mv_pred_direct(h, &h->mv[mv_scan[block]],
                                   &h->col_mv[h->mbidx * 4 + block]);
                break;
            case B_SUB_FWD:
                ff_cavs_mv(h, mv_scan[block], mv_scan[block] - 3,
                           MV_PRED_MEDIAN, BLK_8X8, 1);
                break;
            case B_SUB_SYM:
                ff_cavs_mv(h, mv_scan[block], mv_scan[block] - 3,
                           MV_PRED_MEDIAN, BLK_8X8, 1);
                mv_pred_sym(h, &h->mv[mv_scan[block]], BLK_8X8);
                break;
            }
        }
#undef TMP_UNUSED_INX
        for (block = 0; block < 4; block++) {
            if (sub_type[block] == B_SUB_BWD)
                ff_cavs_mv(h, mv_scan[block] + MV_BWD_OFFS,
                           mv_scan[block] + MV_BWD_OFFS - 3,
                           MV_PRED_MEDIAN, BLK_8X8, 0);
        }
        break;
    default:
        if (mb_type <= B_SYM_16X16) {
            av_log(h->avctx, AV_LOG_ERROR, "Invalid mb_type %d in B frame\n", mb_type);
            return AVERROR_INVALIDDATA;
        }
        av_assert2(mb_type < B_8X8);
        flags = ff_cavs_partition_flags[mb_type];
        if (mb_type & 1) { /* 16x8 macroblock types */
            if (flags & FWD0)
                ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_TOP,  BLK_16X8, 1);
            if (flags & SYM0)
                mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_16X8);
            if (flags & FWD1)
                ff_cavs_mv(h, MV_FWD_X2, MV_FWD_A1, MV_PRED_LEFT, BLK_16X8, 1);
            if (flags & SYM1)
                mv_pred_sym(h, &h->mv[MV_FWD_X2], BLK_16X8);
            if (flags & BWD0)
                ff_cavs_mv(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_TOP,  BLK_16X8, 0);
            if (flags & BWD1)
                ff_cavs_mv(h, MV_BWD_X2, MV_BWD_A1, MV_PRED_LEFT, BLK_16X8, 0);
        } else {          /* 8x16 macroblock types */
            if (flags & FWD0)
                ff_cavs_mv(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_LEFT, BLK_8X16, 1);
            if (flags & SYM0)
                mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_8X16);
            if (flags & FWD1)
                ff_cavs_mv(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_TOPRIGHT, BLK_8X16, 1);
            if (flags & SYM1)
                mv_pred_sym(h, &h->mv[MV_FWD_X1], BLK_8X16);
            if (flags & BWD0)
                ff_cavs_mv(h, MV_BWD_X0, MV_BWD_B3, MV_PRED_LEFT, BLK_8X16, 0);
            if (flags & BWD1)
                ff_cavs_mv(h, MV_BWD_X1, MV_BWD_C2, MV_PRED_TOPRIGHT, BLK_8X16, 0);
        }
    }
    ff_cavs_inter(h, mb_type);
    set_intra_mode_default(h);
    if (mb_type != B_SKIP) {
        ret = decode_residual_inter(h);
        if (ret < 0)
            return ret;
    }
    ff_cavs_filter(h, mb_type);

    return 0;
}

/*****************************************************************************
 *
 * slice level
 *
 ****************************************************************************/

static inline int decode_slice_header(AVSContext *h, GetBitContext *gb)
{
    int mby = h->stc;

    /* over 2800 lines MbRow carries high bits here (clause 7.2.4) */
    if (h->height > 2800)
        mby += get_bits(gb, 3) << 7; //slice_vertical_position_extension

    if (mby >= h->mb_height) {
        av_log(h->avctx, AV_LOG_ERROR, "MbRow %d is too large\n", mby);
        return AVERROR_INVALIDDATA;
    }

    h->mby   = mby;
    h->mbidx = h->mby * h->mb_width;

    /* mark top macroblocks as unavailable */
    h->flags &= ~(B_AVAIL | C_AVAIL);
    if (!h->pic_qp_fixed) {
        h->qp_fixed = get_bits1(gb);
        h->qp       = get_bits(gb, 6);
    }
    /* inter frame or second slice can have weighting params */
    if ((h->cur.f->pict_type != AV_PICTURE_TYPE_I) ||
        (!h->pic_structure && h->mbidx >= h->mb_width * h->mb_height / 2))
        if (get_bits1(gb)) { //slice_weighting_flag
            avpriv_request_sample(h->avctx, "Weighted prediction");
            return AVERROR_PATCHWELCOME;
        }
    return 0;
}

static inline int check_for_slice(AVSContext *h)
{
    GetBitContext *gb = &h->gb;
    int align;
    int ret;

    if (h->mbx)
        return 0;
    /* an AEC slice ends with its stuffing bit, not at a start code (7.2.4) */
    if (h->aec_enable && h->aec.gb)
        return 0;
    align = (-get_bits_count(gb)) & 7;
    /* check for stuffing byte */
    if (!align && (show_bits(gb, 8) == 0x80))
        align = 8;
    if ((show_bits_long(gb, 24 + align) & 0xFFFFFF) == 0x000001) {
        GetBitContext peek;
        int stc;

        if (!is_start_code_offset(h, (get_bits_count(gb) + align) >> 3))
            return 0;
        peek = *gb;
        skip_bits_long(&peek, 24 + align);
        stc = get_bits(&peek, 8);
        /* any other unit ends the picture's data; leave it to the caller */
        if (stc > (SLICE_MAX_START_CODE & 0xFF)) {
            h->pic_data_end = 1;
            return 0;
        }
        *gb    = peek;
        h->stc = stc;
        ret = decode_slice_header(h, gb);
        if (ret < 0)
            return ret;
        if (h->aec_enable) {
            /* aec_byte_alignment_bit padding, then 8.4.1 */
            align_get_bits(gb);
            h->aec_start = get_bits_count(gb) >> 3;
            ff_cavs_aec_init(&h->aec, gb);
            h->prev_delta_qp = 0;   /* reset with PreviousQP, clause 9.3 */
        }
        return 1;
    }
    return 0;
}

/*****************************************************************************
 *
 * frame level
 *
 ****************************************************************************/

static int decode_pic(AVSContext *h)
{
    int ret;
    int skip_count    = -1;
    enum cavs_mb mb_type;

    if (!h->top_qp) {
        av_log(h->avctx, AV_LOG_ERROR, "No sequence header decoded yet\n");
        return AVERROR_INVALIDDATA;
    }

    av_frame_unref(h->cur.f);

    skip_bits(&h->gb, 16);//bbv_delay
    if (h->profile == AV_PROFILE_CAVS_BROADCASTING)
        skip_bits(&h->gb, 8); //marker_bit, bbv_delay_extension (tables 21, 22)
    if (h->stc == PIC_PB_START_CODE) {
        h->cur.f->pict_type = get_bits(&h->gb, 2) + AV_PICTURE_TYPE_I;
        if (h->cur.f->pict_type > AV_PICTURE_TYPE_B) {
            av_log(h->avctx, AV_LOG_ERROR, "illegal picture type\n");
            return AVERROR_INVALIDDATA;
        }
        /* make sure we have the reference frames we need */
        if (!h->DPB[0].f->data[0] ||
           (!h->DPB[1].f->data[0] && h->cur.f->pict_type == AV_PICTURE_TYPE_B))
            return AVERROR_INVALIDDATA;
    } else {
        h->cur.f->pict_type = AV_PICTURE_TYPE_I;
        if (get_bits1(&h->gb))
            skip_bits(&h->gb, 24);//time_code
        /* a Broadcasting stream always has the marker bit (table 21); old
           JiZhun sample clips were all progressive and no low_delay, bump
           stream revision if detected otherwise */
        if (h->profile == AV_PROFILE_CAVS_BROADCASTING)
            h->stream_revision = 1;
        else if (h->low_delay || !(show_bits(&h->gb, 9) & 1))
            h->stream_revision = 1;
        /* similarly test top_field_first and repeat_first_field */
        else if (show_bits(&h->gb, 11) & 3)
            h->stream_revision = 1;
        if (h->stream_revision > 0)
            skip_bits(&h->gb, 1); //marker_bit
    }

    if (get_bits_left(&h->gb) < 23)
        return AVERROR_INVALIDDATA;

    ret = ff_get_buffer(h->avctx, h->cur.f, h->cur.f->pict_type == AV_PICTURE_TYPE_B ?
                        0 : AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    if (!h->edge_emu_buffer) {
        int alloc_size = FFALIGN(FFABS(h->cur.f->linesize[0]) + 32, 32);
        h->edge_emu_buffer = av_mallocz(alloc_size * 2 * 24);
        if (!h->edge_emu_buffer)
            return AVERROR(ENOMEM);
    }

    if ((ret = ff_cavs_init_pic(h)) < 0)
        return ret;
    h->cur.poc = get_bits(&h->gb, 8) * 2;

    /* get temporal distances and MV scaling factors */
    if (h->cur.f->pict_type != AV_PICTURE_TYPE_B) {
        h->dist[0] = (h->cur.poc - h->DPB[0].poc) & 511;
    } else {
        h->dist[0] = (h->DPB[0].poc  - h->cur.poc) & 511;
    }
    h->dist[1] = (h->cur.poc - h->DPB[1].poc) & 511;
    h->scale_den[0] = h->dist[0] ? 512/h->dist[0] : 0;
    h->scale_den[1] = h->dist[1] ? 512/h->dist[1] : 0;
    if (h->cur.f->pict_type == AV_PICTURE_TYPE_B) {
        h->sym_factor = h->dist[0] * h->scale_den[1];
        if (FFABS(h->sym_factor) > 32768) {
            av_log(h->avctx, AV_LOG_ERROR, "sym_factor %d too large\n", h->sym_factor);
            return AVERROR_INVALIDDATA;
        }
    }

    if (h->low_delay)
        get_ue_golomb(&h->gb); //bbv_check_times
    h->progressive   = get_bits1(&h->gb);
    h->pic_structure = 1;
    if (!h->progressive)
        h->pic_structure = get_bits1(&h->gb);
    /* a field coded picture uses inverse block scan method 2 (9.5.3 c)) */
    h->scantable = h->pic_structure ? h->permutated_scantable
                                    : h->permutated_scantable_field;
    if (!h->pic_structure && h->stc == PIC_PB_START_CODE)
        skip_bits1(&h->gb);     //advanced_pred_mode_disable
    skip_bits1(&h->gb);        //top_field_first
    skip_bits1(&h->gb);        //repeat_first_field
    h->pic_qp_fixed =
    h->qp_fixed = get_bits1(&h->gb);
    h->qp       = get_bits(&h->gb, 6);
    if (h->cur.f->pict_type == AV_PICTURE_TYPE_I) {
        if (!h->progressive && !h->pic_structure)
            skip_bits1(&h->gb); //skip_mode_flag
        skip_bits(&h->gb, 4);   //reserved bits
    } else {
        if (!(h->cur.f->pict_type == AV_PICTURE_TYPE_B && h->pic_structure == 1))
            h->ref_flag        = get_bits1(&h->gb);
        skip_bits(&h->gb, 4);   //reserved bits
        h->skip_mode_flag      = get_bits1(&h->gb);
    }
    h->loop_filter_disable     = get_bits1(&h->gb);
    if (!h->loop_filter_disable && get_bits1(&h->gb)) {
        h->alpha_offset        = get_se_golomb(&h->gb);
        h->beta_offset         = get_se_golomb(&h->gb);
        if (   h->alpha_offset < -64 || h->alpha_offset > 64
            || h-> beta_offset < -64 || h-> beta_offset > 64) {
            h->alpha_offset = h->beta_offset  = 0;
            return AVERROR_INVALIDDATA;
        }
    } else {
        h->alpha_offset = h->beta_offset  = 0;
    }

    /* AWQ parameters and aec_enable end the header (tables 21, 22) */
    h->aec_enable = 0;
    if (h->profile == AV_PROFILE_CAVS_BROADCASTING) {
        if (get_bits1(&h->gb)) { //weighting_quant_flag
            avpriv_request_sample(h->avctx, "Adaptive weighted quantization");
            return AVERROR_PATCHWELCOME;
        }
        h->aec_enable = get_bits1(&h->gb);
    }

    ret = 0;
    h->aec.gb = NULL;   /* started by the first slice header */
    h->pic_data_end = 0;
    if (h->cur.f->pict_type == AV_PICTURE_TYPE_I) {
        do {
            ret = check_for_slice(h);
            if (ret < 0)
                break;
            if (h->aec_enable && !h->aec.gb) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "AEC picture without a slice header\n");
                ret = AVERROR_INVALIDDATA;
                break;
            }
            if (h->pic_data_end) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "picture data ended before MB %d\n", h->mbidx);
                ret = AVERROR_INVALIDDATA;
                break;
            }
            ret = decode_mb_i(h, 0);
            if (ret < 0)
                break;
            if (h->aec_enable && ff_cavs_aec_decode_stuffing_bit(&h->aec))
                aec_end_of_slice(h);
        } while (ff_cavs_next_mb(h));
    } else if (h->cur.f->pict_type == AV_PICTURE_TYPE_P) {
        do {
            ret = check_for_slice(h);
            if (ret < 0)
                break;
            if (ret)
                skip_count = -1;
            /* past the picture's data, only a pending skip run can code MBs */
            if (h->pic_data_end && !(h->skip_mode_flag && skip_count > 0)) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "picture data ended before MB %d\n", h->mbidx);
                ret = AVERROR_INVALIDDATA;
                break;
            }
            if (h->skip_mode_flag && (skip_count < 0)) {
                if (get_bits_left(&h->gb) < 1) {
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
                skip_count = get_ue_golomb(&h->gb);
                if (skip_count < 0) {
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
            }
            if (h->skip_mode_flag && skip_count--) {
                ret = decode_mb_p(h, P_SKIP);
            } else {
                if (get_bits_left(&h->gb) < 1) {
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
                mb_type = get_ue_golomb(&h->gb) + P_SKIP + h->skip_mode_flag;
                if (mb_type > P_8X8)
                    ret = decode_mb_i(h, mb_type - P_8X8 - 1);
                else
                    ret = decode_mb_p(h, mb_type);
            }
            if (ret < 0)
                break;
        } while (ff_cavs_next_mb(h));
    } else { /* AV_PICTURE_TYPE_B */
        do {
            ret = check_for_slice(h);
            if (ret < 0)
                break;
            if (ret)
                skip_count = -1;
            /* past the picture's data, only a pending skip run can code MBs */
            if (h->pic_data_end && !(h->skip_mode_flag && skip_count > 0)) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "picture data ended before MB %d\n", h->mbidx);
                ret = AVERROR_INVALIDDATA;
                break;
            }
            if (h->skip_mode_flag && (skip_count < 0)) {
                if (get_bits_left(&h->gb) < 1) {
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
                skip_count = get_ue_golomb(&h->gb);
                if (skip_count < 0) {
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
            }
            if (h->skip_mode_flag && skip_count--) {
                ret = decode_mb_b(h, B_SKIP);
            } else {
                if (get_bits_left(&h->gb) < 1) {
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
                mb_type = get_ue_golomb(&h->gb) + B_SKIP + h->skip_mode_flag;
                if (mb_type > B_8X8)
                    ret = decode_mb_i(h, mb_type - B_8X8 - 1);
                else
                    ret = decode_mb_b(h, mb_type);
            }
            if (ret < 0)
                break;
        } while (ff_cavs_next_mb(h));
    }
    if (ret >= 0 && h->cur.f->pict_type != AV_PICTURE_TYPE_B) {
        av_frame_unref(h->DPB[1].f);
        FFSWAP(AVSFrame, h->cur, h->DPB[1]);
        FFSWAP(AVSFrame, h->DPB[0], h->DPB[1]);
    }
    return ret;
}

/*****************************************************************************
 *
 * headers and interface
 *
 ****************************************************************************/

static int decode_seq_header(AVSContext *h)
{
    int frame_rate_code;
    int profile, level, aspect_ratio;
    int width, height;
    int mb_width, mb_height;
    int progressive_seq, low_delay;
    int ret;

    profile = get_bits(&h->gb, 8);
    if (profile != AV_PROFILE_CAVS_JIZHUN) {
        avpriv_report_missing_feature(h->avctx, "Profile %#x", profile);
        return AVERROR_PATCHWELCOME;
    }
    level = get_bits(&h->gb, 8);
    progressive_seq = get_bits1(&h->gb);

    width  = get_bits(&h->gb, 14);
    height = get_bits(&h->gb, 14);
    if ((h->width || h->height) && (h->width != width || h->height != height)) {
        avpriv_report_missing_feature(h->avctx,
                                      "Width/height changing in CAVS");
        return AVERROR_PATCHWELCOME;
    }
    if (width <= 0 || height <= 0) {
        av_log(h->avctx, AV_LOG_ERROR, "Dimensions invalid\n");
        return AVERROR_INVALIDDATA;
    }
    skip_bits(&h->gb, 2); //chroma format
    skip_bits(&h->gb, 3); //sample_precision
    aspect_ratio = get_bits(&h->gb, 4);
    frame_rate_code = get_bits(&h->gb, 4);
    if (frame_rate_code == 0 || frame_rate_code > 13) {
        av_log(h->avctx, AV_LOG_WARNING,
               "frame_rate_code %d is invalid\n", frame_rate_code);
        frame_rate_code = 1;
    }

    skip_bits(&h->gb, 18); //bit_rate_lower
    skip_bits1(&h->gb);    //marker_bit
    skip_bits(&h->gb, 12); //bit_rate_upper
    low_delay = get_bits1(&h->gb);

    mb_width  = (width  + 15) >> 4;
    /* interlaced: an even number of MB rows (clause 7.2.2) */
    mb_height = progressive_seq ? (height + 15) >> 4
                                : ((height + 31) >> 5) * 2;
    /* the grid sizes the arrays of ff_cavs_init_top_lines(), allocated once */
    if (h->top_qp && (mb_width != h->mb_width || mb_height != h->mb_height)) {
        avpriv_report_missing_feature(h->avctx, "Macroblock grid changing");
        return AVERROR_PATCHWELCOME;
    }

    /* horizontal_size and vertical_size are only the displayed part */
    ret = ff_set_dimensions(h->avctx, mb_width * 16, mb_height * 16);
    if (ret < 0)
        return ret;
    h->avctx->width  = width;
    h->avctx->height = height;

    /* commit last: a rejected header must leave the decoder as it was */
    h->profile         = profile;
    h->level           = level;
    h->aspect_ratio    = aspect_ratio;
    h->width           = width;
    h->height          = height;
    h->mb_width        = mb_width;
    h->mb_height       = mb_height;
    h->low_delay       = low_delay;
    h->avctx->profile  = profile;
    h->avctx->framerate = ff_mpeg12_frame_rate_tab[frame_rate_code];
    if (!h->top_qp)
        return ff_cavs_init_top_lines(h);
    return 0;
}

/** annex A applies to picture headers and slices, not to other units */
static int annex_a_applies(int stc)
{
    return stc <= (SLICE_MAX_START_CODE & 0xFF) ||
           stc == (PIC_I_START_CODE  & 0xFF)    ||
           stc == (PIC_PB_START_CODE & 0xFF);
}

/**
 * Copy one region of a picture payload, removing the annex A pseudo start
 * codes; the clause 5.8.2.4 stuffing is rewritten to keep the next start
 * code aligned. With pb NULL they are only counted.
 * @return the number of pseudo start codes found
 */
static int deemulate_region(PutBitContext *pb, const uint8_t *buf, int size,
                            int deemulated)
{
    int found = 0;
    int stuffing = -1;
    int i;

    if (!deemulated) {
        for (i = 0; i < size; i++)
            if (pb)
                put_bits(pb, 8, buf[i]);
        return 0;
    }

    /* the stuffing bit is the last set bit of the region */
    for (i = size; i > 0 && !buf[i - 1]; i--)
        ;
    if (i > 0) {
        stuffing = i - 1;
        size     = stuffing;
    }

    for (i = 0; i < size; i++) {
        if (i + 2 < size && !buf[i] && !buf[i + 1] && buf[i + 2] == 2) {
            /* drop the two low bits of the 02 */
            if (pb) {
                put_bits(pb, 16, 0);
                put_bits(pb,  6, 0);
            }
            found++;
            i += 2;
        } else if (pb) {
            put_bits(pb, 8, buf[i]);
        }
    }

    if (pb) {
        if (stuffing >= 0) {
            int n = ff_ctz(buf[stuffing]) + 1;
            if (n < 8)
                put_bits(pb, 8 - n, buf[stuffing] >> n);
            put_bits(pb, 1, 1);     //stuffing_bit
        }
        align_put_bits(pb);         /* stuffing_bit until byte aligned */
    }
    return found;
}

/**
 * Walk the payload region by region, removing the annex A pseudo start
 * codes; on the writing pass, record every start code prefix offset in
 * h->stc_offset.
 * @return the number of pseudo start codes found
 */
static int deemulate_picture(AVSContext *h, PutBitContext *pb,
                             const uint8_t *buf, int size)
{
    int deemulated = 1;     /* buf starts inside a picture header */
    int found      = 0;
    int pos        = 0;

    while (pos < size) {
        int end;

        for (end = pos; end + 2 < size; end++) {
            /* nothing can start at end, end + 1 or end + 2 (annex A) */
            if (buf[end + 2] > 1) {
                end += 2;
                continue;
            }
            if (!buf[end] && !buf[end + 1] && buf[end + 2] == 1)
                break;
        }
        if (end + 2 >= size)
            end = size;

        found += deemulate_region(pb, buf + pos, end - pos, deemulated);
        if (end == size)
            break;

        if (pb) {
            h->stc_offset[h->nb_stc_offset++] = put_bits_count(pb) >> 3;
            put_bits(pb, 24, 1);    /* start code prefix */
        }
        pos = end + 3;
        if (pos < size) {
            if (pb)
                put_bits(pb, 8, buf[pos]);
            deemulated = annex_a_applies(buf[pos]);
            pos++;
        }
    }
    return found;
}

/**
 * Remove the annex A pseudo start codes from a payload: a byte forming
 * 00 00 02 with the two before it loses its two least significant bits. buf
 * runs from just after a picture start code to the packet end.
 * @return the size of h->deemulated_buf, 0 if buf needs no copy, or < 0
 */
static int remove_pseudo_start_codes(AVSContext *h, const uint8_t *buf,
                                     int size)
{
    PutBitContext pb;

    if (size <= 0 || size > INT_MAX / 8 ||
        !deemulate_picture(h, NULL, buf, size))
        return 0;

    av_fast_padded_malloc(&h->deemulated_buf, &h->deemulated_buf_size, size);
    if (!h->deemulated_buf)
        return AVERROR(ENOMEM);

    av_fast_malloc(&h->stc_offset, &h->stc_offset_size,
                   (size / 3 + 1) * sizeof(*h->stc_offset));
    if (!h->stc_offset)
        return AVERROR(ENOMEM);
    h->nb_stc_offset = 0;

    init_put_bits(&pb, h->deemulated_buf, size + AV_INPUT_BUFFER_PADDING_SIZE);
    deemulate_picture(h, &pb, buf, size);
    flush_put_bits(&pb);
    av_assert0(put_bytes_output(&pb) <= size);
    /* de-emulation shrinks, so restore the padding the reader over-reads */
    memset(h->deemulated_buf + put_bytes_output(&pb), 0,
           AV_INPUT_BUFFER_PADDING_SIZE);
    return put_bytes_output(&pb);
}

static av_cold void cavs_flush(AVCodecContext * avctx)
{
    AVSContext *h = avctx->priv_data;
    h->got_keyframe = 0;
}

static int cavs_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                             int *got_frame, AVPacket *avpkt)
{
    AVSContext *h      = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    uint32_t stc       = -1;
    int input_size, ret;
    const uint8_t *buf_end;
    const uint8_t *buf_ptr;
    int frame_start = 0;

    if (buf_size == 0) {
        if (!h->low_delay && h->DPB[0].f->data[0]) {
            *got_frame = 1;
            av_frame_move_ref(rframe, h->DPB[0].f);
        }
        return 0;
    }

    h->stc = 0;

    buf_ptr = buf;
    buf_end = buf + buf_size;
    for(;;) {
        buf_ptr = avpriv_find_start_code(buf_ptr, buf_end, &stc);
        if ((stc & 0xFFFFFE00) || buf_ptr == buf_end) {
            if (!h->stc)
                av_log(h->avctx, AV_LOG_WARNING, "no frame decoded\n");
            return FFMAX(0, buf_ptr - buf);
        }
        input_size = buf_end - buf_ptr;
        if ((ret = init_get_bits8(&h->gb, buf_ptr, input_size)) < 0)
            return ret;
        switch (stc) {
        case CAVS_START_CODE:
            ret = decode_seq_header(h);
            if (ret < 0)
                return ret;
            break;
        case PIC_I_START_CODE:
            if (!h->got_keyframe) {
                av_frame_unref(h->DPB[0].f);
                av_frame_unref(h->DPB[1].f);
                h->got_keyframe = 1;
            }
            av_fallthrough;
        case PIC_PB_START_CODE:
            if (frame_start > 1)
                return AVERROR_INVALIDDATA;
            frame_start ++;
            if (*got_frame)
                av_frame_unref(rframe);
            *got_frame = 0;
            if (!h->got_keyframe)
                break;
            /* annex A, normative for decoders */
            ret = remove_pseudo_start_codes(h, buf_ptr, input_size);
            if (ret < 0)
                return ret;
            if (ret > 0 &&
                (ret = init_get_bits8(&h->gb, h->deemulated_buf, ret)) < 0)
                return ret;
            h->stc = stc;
            ret = decode_pic(h);
            if (ret < 0)
                return ret;
            *got_frame = 1;
            if (h->cur.f->pict_type != AV_PICTURE_TYPE_B) {
                if (h->DPB[!h->low_delay].f->data[0]) {
                    if ((ret = av_frame_ref(rframe, h->DPB[!h->low_delay].f)) < 0)
                        return ret;
                } else {
                    *got_frame = 0;
                }
            } else {
                av_frame_move_ref(rframe, h->cur.f);
            }
            break;
        case EXT_START_CODE:
            //mpeg_decode_extension(avctx, buf_ptr, input_size);
            break;
        case USER_START_CODE:
            //mpeg_decode_user_data(avctx, buf_ptr, input_size);
            break;
        default:
            /* slice start codes are consumed by check_for_slice() */
            break;
        }
    }
}

const FFCodec ff_cavs_decoder = {
    .p.name         = "cavs",
    CODEC_LONG_NAME("Chinese AVS (Audio Video Standard) (AVS1-P2, JiZhun profile)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_CAVS,
    .priv_data_size = sizeof(AVSContext),
    .init           = ff_cavs_init,
    .close          = ff_cavs_end,
    FF_CODEC_DECODE_CB(cavs_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .flush          = cavs_flush,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_cavs_profiles),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
