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
 */

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "cavs.h"
#include "internal.h"
#include "mpeg12data.h"
#include "mpegvideo.h"

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
    int den = h->direct_den[col_mv->ref];
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
        av_log(NULL, AV_LOG_ERROR, "get_ue_code: value too larger\n");
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
    const uint8_t *scantab = h->scantable.permutated;

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
                                 int qp, uint8_t *dst, int stride)
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
    if ((ret = dequant(h, level_buf, run_buf, block, dequant_mul[qp],
                      dequant_shift[qp], i)) < 0)
        return ret;
    h->cdsp.cavs_idct8_add(dst, block, stride);
    h->bdsp.clear_block(block);
    return 0;
}


static inline void decode_residual_chroma(AVSContext *h)
{
    if (h->cbp & (1 << 4))
        decode_residual_block(h, &h->gb, chroma_dec, 0,
                              ff_cavs_chroma_qp[h->qp], h->cu, h->c_stride);
    if (h->cbp & (1 << 5))
        decode_residual_block(h, &h->gb, chroma_dec, 0,
                              ff_cavs_chroma_qp[h->qp], h->cv, h->c_stride);
}

static inline int decode_residual_inter(AVSContext *h)
{
    int block;

    /* get coded block pattern */
    int cbp = get_ue_golomb(&h->gb);
    if (cbp > 63U) {
        av_log(h->avctx, AV_LOG_ERROR, "illegal inter cbp %d\n", cbp);
        return AVERROR_INVALIDDATA;
    }
    h->cbp = cbp_tab[cbp][1];

    /* get quantizer */
    if (h->cbp && !h->qp_fixed)
        h->qp = (h->qp + get_se_golomb(&h->gb)) & 63;
    for (block = 0; block < 4; block++)
        if (h->cbp & (1 << block))
            decode_residual_block(h, &h->gb, inter_dec, 0, h->qp,
                                  h->cy + h->luma_scan[block], h->l_stride);
    decode_residual_chroma(h);

    return 0;
}

/*****************************************************************************
 *
 * macroblock level
 *
 ****************************************************************************/

static inline void set_mv_intra(AVSContext *h)
{
    h->mv[MV_FWD_X0] = ff_cavs_intra_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = ff_cavs_intra_mv;
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
        if (!get_bits1(gb)) {
            int rem_mode = get_bits(gb, 2);
            predpred     = rem_mode + (rem_mode >= predpred);
        }
        h->pred_mode_Y[pos] = predpred;
    }
    pred_mode_uv = get_ue_golomb(gb);
    if (pred_mode_uv > 6) {
        av_log(h->avctx, AV_LOG_ERROR, "illegal intra chroma pred mode\n");
        return AVERROR_INVALIDDATA;
    }
    ff_cavs_modify_mb_i(h, &pred_mode_uv);

    /* get coded block pattern */
    if (h->cur.f->pict_type == AV_PICTURE_TYPE_I)
        cbp_code = get_ue_golomb(gb);
    if (cbp_code > 63U) {
        av_log(h->avctx, AV_LOG_ERROR, "illegal intra cbp\n");
        return AVERROR_INVALIDDATA;
    }
    h->cbp = cbp_tab[cbp_code][0];
    if (h->cbp && !h->qp_fixed)
        h->qp = (h->qp + get_se_golomb(gb)) & 63; //qp_delta

    /* luma intra prediction interleaved with residual decode/transform/add */
    for (block = 0; block < 4; block++) {
        d = h->cy + h->luma_scan[block];
        ff_cavs_load_intra_pred_luma(h, top, &left, block);
        h->intra_pred_l[h->pred_mode_Y[scan3x3[block]]]
            (d, top, left, h->l_stride);
        if (h->cbp & (1<<block))
            decode_residual_block(h, gb, intra_dec, 1, h->qp, d, h->l_stride);
    }

    /* chroma intra prediction */
    ff_cavs_load_intra_pred_chroma(h);
    h->intra_pred_c[pred_mode_uv](h->cu, &h->top_border_u[h->mbx * 10],
                                  h->left_border_u, h->c_stride);
    h->intra_pred_c[pred_mode_uv](h->cv, &h->top_border_v[h->mbx * 10],
                                  h->left_border_v, h->c_stride);

    decode_residual_chroma(h);
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

static void decode_mb_p(AVSContext *h, enum cavs_mb mb_type)
{
    GetBitContext *gb = &h->gb;
    int ref[4];

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
    }
    ff_cavs_inter(h, mb_type);
    set_intra_mode_default(h);
    store_mvs(h);
    if (mb_type != P_SKIP)
        decode_residual_inter(h);
    ff_cavs_filter(h, mb_type);
    h->col_type_base[h->mbidx] = mb_type;
}

static int decode_mb_b(AVSContext *h, enum cavs_mb mb_type)
{
    int block;
    enum cavs_sub_mb sub_type[4];
    int flags;

    ff_cavs_init_mb(h);

    /* reset all MVs */
    h->mv[MV_FWD_X0] = ff_cavs_dir_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = ff_cavs_dir_mv;
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
    if (mb_type != B_SKIP)
        decode_residual_inter(h);
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
    if (h->stc > 0xAF)
        av_log(h->avctx, AV_LOG_ERROR, "unexpected start code 0x%02x\n", h->stc);

    if (h->stc >= h->mb_height) {
        av_log(h->avctx, AV_LOG_ERROR, "stc 0x%02x is too large\n", h->stc);
        return AVERROR_INVALIDDATA;
    }

    h->mby   = h->stc;
    h->mbidx = h->mby * h->mb_width;

    /* mark top macroblocks as unavailable */
    h->flags &= ~(B_AVAIL | C_AVAIL);
    if (!h->pic_qp_fixed) {
        h->qp_fixed = get_bits1(gb);
        h->qp       = get_bits(gb, 6);
    }
    /* inter frame or second slice can have weighting params */
    if ((h->cur.f->pict_type != AV_PICTURE_TYPE_I) ||
        (!h->pic_structure && h->mby >= h->mb_width / 2))
        if (get_bits1(gb)) { //slice_weighting_flag
            av_log(h->avctx, AV_LOG_ERROR,
                   "weighted prediction not yet supported\n");
        }
    return 0;
}

static inline int check_for_slice(AVSContext *h)
{
    GetBitContext *gb = &h->gb;
    int align;

    if (h->mbx)
        return 0;
    align = (-get_bits_count(gb)) & 7;
    /* check for stuffing byte */
    if (!align && (show_bits(gb, 8) == 0x80))
        align = 8;
    if ((show_bits_long(gb, 24 + align) & 0xFFFFFF) == 0x000001) {
        skip_bits_long(gb, 24 + align);
        h->stc = get_bits(gb, 8);
        if (h->stc >= h->mb_height)
            return 0;
        decode_slice_header(h, gb);
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

    skip_bits(&h->gb, 16);//bbv_dwlay
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
        /* old sample clips were all progressive and no low_delay,
           bump stream revision if detected otherwise */
        if (h->low_delay || !(show_bits(&h->gb, 9) & 1))
            h->stream_revision = 1;
        /* similarly test top_field_first and repeat_first_field */
        else if (show_bits(&h->gb, 11) & 3)
            h->stream_revision = 1;
        if (h->stream_revision > 0)
            skip_bits(&h->gb, 1); //marker_bit
    }

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
    } else {
        h->direct_den[0] = h->dist[0] ? 16384 / h->dist[0] : 0;
        h->direct_den[1] = h->dist[1] ? 16384 / h->dist[1] : 0;
    }

    if (h->low_delay)
        get_ue_golomb(&h->gb); //bbv_check_times
    h->progressive   = get_bits1(&h->gb);
    h->pic_structure = 1;
    if (!h->progressive)
        h->pic_structure = get_bits1(&h->gb);
    if (!h->pic_structure && h->stc == PIC_PB_START_CODE)
        skip_bits1(&h->gb);     //advanced_pred_mode_disable
    skip_bits1(&h->gb);        //top_field_first
    skip_bits1(&h->gb);        //repeat_first_field
    h->pic_qp_fixed =
    h->qp_fixed = get_bits1(&h->gb);
    h->qp       = get_bits(&h->gb, 6);
    if (h->cur.f->pict_type == AV_PICTURE_TYPE_I) {
        if (!h->progressive && !h->pic_structure)
            skip_bits1(&h->gb);//what is this?
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
    } else {
        h->alpha_offset = h->beta_offset  = 0;
    }
    if (h->cur.f->pict_type == AV_PICTURE_TYPE_I) {
        do {
            check_for_slice(h);
            decode_mb_i(h, 0);
        } while (ff_cavs_next_mb(h));
    } else if (h->cur.f->pict_type == AV_PICTURE_TYPE_P) {
        do {
            if (check_for_slice(h))
                skip_count = -1;
            if (h->skip_mode_flag && (skip_count < 0))
                skip_count = get_ue_golomb(&h->gb);
            if (h->skip_mode_flag && skip_count--) {
                decode_mb_p(h, P_SKIP);
            } else {
                mb_type = get_ue_golomb(&h->gb) + P_SKIP + h->skip_mode_flag;
                if (mb_type > P_8X8)
                    decode_mb_i(h, mb_type - P_8X8 - 1);
                else
                    decode_mb_p(h, mb_type);
            }
        } while (ff_cavs_next_mb(h));
    } else { /* AV_PICTURE_TYPE_B */
        do {
            if (check_for_slice(h))
                skip_count = -1;
            if (h->skip_mode_flag && (skip_count < 0))
                skip_count = get_ue_golomb(&h->gb);
            if (h->skip_mode_flag && skip_count--) {
                decode_mb_b(h, B_SKIP);
            } else {
                mb_type = get_ue_golomb(&h->gb) + B_SKIP + h->skip_mode_flag;
                if (mb_type > B_8X8)
                    decode_mb_i(h, mb_type - B_8X8 - 1);
                else
                    decode_mb_b(h, mb_type);
            }
        } while (ff_cavs_next_mb(h));
    }
    if (h->cur.f->pict_type != AV_PICTURE_TYPE_B) {
        av_frame_unref(h->DPB[1].f);
        FFSWAP(AVSFrame, h->cur, h->DPB[1]);
        FFSWAP(AVSFrame, h->DPB[0], h->DPB[1]);
    }
    return 0;
}

/*****************************************************************************
 *
 * headers and interface
 *
 ****************************************************************************/

static int decode_seq_header(AVSContext *h)
{
    int frame_rate_code;
    int width, height;
    int ret;

    h->profile = get_bits(&h->gb, 8);
    h->level   = get_bits(&h->gb, 8);
    skip_bits1(&h->gb); //progressive sequence

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
    h->aspect_ratio = get_bits(&h->gb, 4);
    frame_rate_code = get_bits(&h->gb, 4);
    if (frame_rate_code == 0 || frame_rate_code > 13) {
        av_log(h->avctx, AV_LOG_WARNING,
               "frame_rate_code %d is invalid\n", frame_rate_code);
        frame_rate_code = 1;
    }

    skip_bits(&h->gb, 18); //bit_rate_lower
    skip_bits1(&h->gb);    //marker_bit
    skip_bits(&h->gb, 12); //bit_rate_upper
    h->low_delay =  get_bits1(&h->gb);

    ret = ff_set_dimensions(h->avctx, width, height);
    if (ret < 0)
        return ret;

    h->width  = width;
    h->height = height;
    h->mb_width  = (h->width  + 15) >> 4;
    h->mb_height = (h->height + 15) >> 4;
    h->avctx->framerate = ff_mpeg12_frame_rate_tab[frame_rate_code];
    if (!h->top_qp)
        return ff_cavs_init_top_lines(h);
    return 0;
}

static void cavs_flush(AVCodecContext * avctx)
{
    AVSContext *h = avctx->priv_data;
    h->got_keyframe = 0;
}

static int cavs_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    AVSContext *h      = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    uint32_t stc       = -1;
    int input_size, ret;
    const uint8_t *buf_end;
    const uint8_t *buf_ptr;

    if (buf_size == 0) {
        if (!h->low_delay && h->DPB[0].f->data[0]) {
            *got_frame = 1;
            av_frame_move_ref(data, h->DPB[0].f);
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
        input_size = (buf_end - buf_ptr) * 8;
        switch (stc) {
        case CAVS_START_CODE:
            init_get_bits(&h->gb, buf_ptr, input_size);
            decode_seq_header(h);
            break;
        case PIC_I_START_CODE:
            if (!h->got_keyframe) {
                av_frame_unref(h->DPB[0].f);
                av_frame_unref(h->DPB[1].f);
                h->got_keyframe = 1;
            }
        case PIC_PB_START_CODE:
            *got_frame = 0;
            if (!h->got_keyframe)
                break;
            init_get_bits(&h->gb, buf_ptr, input_size);
            h->stc = stc;
            if (decode_pic(h))
                break;
            *got_frame = 1;
            if (h->cur.f->pict_type != AV_PICTURE_TYPE_B) {
                if (h->DPB[!h->low_delay].f->data[0]) {
                    if ((ret = av_frame_ref(data, h->DPB[!h->low_delay].f)) < 0)
                        return ret;
                } else {
                    *got_frame = 0;
                }
            } else {
                av_frame_move_ref(data, h->cur.f);
            }
            break;
        case EXT_START_CODE:
            //mpeg_decode_extension(avctx, buf_ptr, input_size);
            break;
        case USER_START_CODE:
            //mpeg_decode_user_data(avctx, buf_ptr, input_size);
            break;
        default:
            if (stc <= SLICE_MAX_START_CODE) {
                init_get_bits(&h->gb, buf_ptr, input_size);
                decode_slice_header(h, &h->gb);
            }
            break;
        }
    }
}

AVCodec ff_cavs_decoder = {
    .name           = "cavs",
    .long_name      = NULL_IF_CONFIG_SMALL("Chinese AVS (Audio Video Standard) (AVS1-P2, JiZhun profile)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CAVS,
    .priv_data_size = sizeof(AVSContext),
    .init           = ff_cavs_init,
    .close          = ff_cavs_end,
    .decode         = cavs_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .flush          = cavs_flush,
};
