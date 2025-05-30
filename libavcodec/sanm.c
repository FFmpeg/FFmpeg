/*
 * LucasArts Smush video decoder
 * Copyright (c) 2006 Cyril Zorin
 * Copyright (c) 2011 Konstantin Shishkov
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

#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "bytestream.h"
#include "copy_block.h"
#include "codec_internal.h"
#include "decode.h"

#define NGLYPHS 256
#define GLYPH_COORD_VECT_SIZE 16
#define PALETTE_SIZE 256
#define PALETTE_DELTA 768

static const int8_t glyph4_x[GLYPH_COORD_VECT_SIZE] = {
    0, 1, 2, 3, 3, 3, 3, 2, 1, 0, 0, 0, 1, 2, 2, 1
};

static const int8_t glyph4_y[GLYPH_COORD_VECT_SIZE] = {
    0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 2, 1, 1, 1, 2, 2
};

static const int8_t glyph8_x[GLYPH_COORD_VECT_SIZE] = {
    0, 2, 5, 7, 7, 7, 7, 7, 7, 5, 2, 0, 0, 0, 0, 0
};

static const int8_t glyph8_y[GLYPH_COORD_VECT_SIZE] = {
    0, 0, 0, 0, 1, 3, 4, 6, 7, 7, 7, 7, 6, 4, 3, 1
};

static const int8_t motion_vectors[256][2] = {
    {   0,   0 }, {  -1, -43 }, {   6, -43 }, {  -9, -42 }, {  13, -41 },
    { -16, -40 }, {  19, -39 }, { -23, -36 }, {  26, -34 }, {  -2, -33 },
    {   4, -33 }, { -29, -32 }, {  -9, -32 }, {  11, -31 }, { -16, -29 },
    {  32, -29 }, {  18, -28 }, { -34, -26 }, { -22, -25 }, {  -1, -25 },
    {   3, -25 }, {  -7, -24 }, {   8, -24 }, {  24, -23 }, {  36, -23 },
    { -12, -22 }, {  13, -21 }, { -38, -20 }, {   0, -20 }, { -27, -19 },
    {  -4, -19 }, {   4, -19 }, { -17, -18 }, {  -8, -17 }, {   8, -17 },
    {  18, -17 }, {  28, -17 }, {  39, -17 }, { -12, -15 }, {  12, -15 },
    { -21, -14 }, {  -1, -14 }, {   1, -14 }, { -41, -13 }, {  -5, -13 },
    {   5, -13 }, {  21, -13 }, { -31, -12 }, { -15, -11 }, {  -8, -11 },
    {   8, -11 }, {  15, -11 }, {  -2, -10 }, {   1, -10 }, {  31, -10 },
    { -23,  -9 }, { -11,  -9 }, {  -5,  -9 }, {   4,  -9 }, {  11,  -9 },
    {  42,  -9 }, {   6,  -8 }, {  24,  -8 }, { -18,  -7 }, {  -7,  -7 },
    {  -3,  -7 }, {  -1,  -7 }, {   2,  -7 }, {  18,  -7 }, { -43,  -6 },
    { -13,  -6 }, {  -4,  -6 }, {   4,  -6 }, {   8,  -6 }, { -33,  -5 },
    {  -9,  -5 }, {  -2,  -5 }, {   0,  -5 }, {   2,  -5 }, {   5,  -5 },
    {  13,  -5 }, { -25,  -4 }, {  -6,  -4 }, {  -3,  -4 }, {   3,  -4 },
    {   9,  -4 }, { -19,  -3 }, {  -7,  -3 }, {  -4,  -3 }, {  -2,  -3 },
    {  -1,  -3 }, {   0,  -3 }, {   1,  -3 }, {   2,  -3 }, {   4,  -3 },
    {   6,  -3 }, {  33,  -3 }, { -14,  -2 }, { -10,  -2 }, {  -5,  -2 },
    {  -3,  -2 }, {  -2,  -2 }, {  -1,  -2 }, {   0,  -2 }, {   1,  -2 },
    {   2,  -2 }, {   3,  -2 }, {   5,  -2 }, {   7,  -2 }, {  14,  -2 },
    {  19,  -2 }, {  25,  -2 }, {  43,  -2 }, {  -7,  -1 }, {  -3,  -1 },
    {  -2,  -1 }, {  -1,  -1 }, {   0,  -1 }, {   1,  -1 }, {   2,  -1 },
    {   3,  -1 }, {  10,  -1 }, {  -5,   0 }, {  -3,   0 }, {  -2,   0 },
    {  -1,   0 }, {   1,   0 }, {   2,   0 }, {   3,   0 }, {   5,   0 },
    {   7,   0 }, { -10,   1 }, {  -7,   1 }, {  -3,   1 }, {  -2,   1 },
    {  -1,   1 }, {   0,   1 }, {   1,   1 }, {   2,   1 }, {   3,   1 },
    { -43,   2 }, { -25,   2 }, { -19,   2 }, { -14,   2 }, {  -5,   2 },
    {  -3,   2 }, {  -2,   2 }, {  -1,   2 }, {   0,   2 }, {   1,   2 },
    {   2,   2 }, {   3,   2 }, {   5,   2 }, {   7,   2 }, {  10,   2 },
    {  14,   2 }, { -33,   3 }, {  -6,   3 }, {  -4,   3 }, {  -2,   3 },
    {  -1,   3 }, {   0,   3 }, {   1,   3 }, {   2,   3 }, {   4,   3 },
    {  19,   3 }, {  -9,   4 }, {  -3,   4 }, {   3,   4 }, {   7,   4 },
    {  25,   4 }, { -13,   5 }, {  -5,   5 }, {  -2,   5 }, {   0,   5 },
    {   2,   5 }, {   5,   5 }, {   9,   5 }, {  33,   5 }, {  -8,   6 },
    {  -4,   6 }, {   4,   6 }, {  13,   6 }, {  43,   6 }, { -18,   7 },
    {  -2,   7 }, {   0,   7 }, {   2,   7 }, {   7,   7 }, {  18,   7 },
    { -24,   8 }, {  -6,   8 }, { -42,   9 }, { -11,   9 }, {  -4,   9 },
    {   5,   9 }, {  11,   9 }, {  23,   9 }, { -31,  10 }, {  -1,  10 },
    {   2,  10 }, { -15,  11 }, {  -8,  11 }, {   8,  11 }, {  15,  11 },
    {  31,  12 }, { -21,  13 }, {  -5,  13 }, {   5,  13 }, {  41,  13 },
    {  -1,  14 }, {   1,  14 }, {  21,  14 }, { -12,  15 }, {  12,  15 },
    { -39,  17 }, { -28,  17 }, { -18,  17 }, {  -8,  17 }, {   8,  17 },
    {  17,  18 }, {  -4,  19 }, {   0,  19 }, {   4,  19 }, {  27,  19 },
    {  38,  20 }, { -13,  21 }, {  12,  22 }, { -36,  23 }, { -24,  23 },
    {  -8,  24 }, {   7,  24 }, {  -3,  25 }, {   1,  25 }, {  22,  25 },
    {  34,  26 }, { -18,  28 }, { -32,  29 }, {  16,  29 }, { -11,  31 },
    {   9,  32 }, {  29,  32 }, {  -4,  33 }, {   2,  33 }, { -26,  34 },
    {  23,  36 }, { -19,  39 }, {  16,  40 }, { -13,  41 }, {   9,  42 },
    {  -6,  43 }, {   1,  43 }, {   0,   0 }, {   0,   0 }, {   0,   0 },
};

static const int8_t c37_mv[] = {
    0,   0,   1,   0,   2,   0,   3,   0,   5,   0,
    8,   0,  13,   0,  21,   0,  -1,   0,  -2,   0,
   -3,   0,  -5,   0,  -8,   0, -13,   0, -17,   0,
  -21,   0,   0,   1,   1,   1,   2,   1,   3,   1,
    5,   1,   8,   1,  13,   1,  21,   1,  -1,   1,
   -2,   1,  -3,   1,  -5,   1,  -8,   1, -13,   1,
  -17,   1, -21,   1,   0,   2,   1,   2,   2,   2,
    3,   2,   5,   2,   8,   2,  13,   2,  21,   2,
   -1,   2,  -2,   2,  -3,   2,  -5,   2,  -8,   2,
  -13,   2, -17,   2, -21,   2,   0,   3,   1,   3,
    2,   3,   3,   3,   5,   3,   8,   3,  13,   3,
   21,   3,  -1,   3,  -2,   3,  -3,   3,  -5,   3,
   -8,   3, -13,   3, -17,   3, -21,   3,   0,   5,
    1,   5,   2,   5,   3,   5,   5,   5,   8,   5,
   13,   5,  21,   5,  -1,   5,  -2,   5,  -3,   5,
   -5,   5,  -8,   5, -13,   5, -17,   5, -21,   5,
    0,   8,   1,   8,   2,   8,   3,   8,   5,   8,
    8,   8,  13,   8,  21,   8,  -1,   8,  -2,   8,
   -3,   8,  -5,   8,  -8,   8, -13,   8, -17,   8,
  -21,   8,   0,  13,   1,  13,   2,  13,   3,  13,
    5,  13,   8,  13,  13,  13,  21,  13,  -1,  13,
   -2,  13,  -3,  13,  -5,  13,  -8,  13, -13,  13,
  -17,  13, -21,  13,   0,  21,   1,  21,   2,  21,
    3,  21,   5,  21,   8,  21,  13,  21,  21,  21,
   -1,  21,  -2,  21,  -3,  21,  -5,  21,  -8,  21,
  -13,  21, -17,  21, -21,  21,   0,  -1,   1,  -1,
    2,  -1,   3,  -1,   5,  -1,   8,  -1,  13,  -1,
   21,  -1,  -1,  -1,  -2,  -1,  -3,  -1,  -5,  -1,
   -8,  -1, -13,  -1, -17,  -1, -21,  -1,   0,  -2,
    1,  -2,   2,  -2,   3,  -2,   5,  -2,   8,  -2,
   13,  -2,  21,  -2,  -1,  -2,  -2,  -2,  -3,  -2,
   -5,  -2,  -8,  -2, -13,  -2, -17,  -2, -21,  -2,
    0,  -3,   1,  -3,   2,  -3,   3,  -3,   5,  -3,
    8,  -3,  13,  -3,  21,  -3,  -1,  -3,  -2,  -3,
   -3,  -3,  -5,  -3,  -8,  -3, -13,  -3, -17,  -3,
  -21,  -3,   0,  -5,   1,  -5,   2,  -5,   3,  -5,
    5,  -5,   8,  -5,  13,  -5,  21,  -5,  -1,  -5,
   -2,  -5,  -3,  -5,  -5,  -5,  -8,  -5, -13,  -5,
  -17,  -5, -21,  -5,   0,  -8,   1,  -8,   2,  -8,
    3,  -8,   5,  -8,   8,  -8,  13,  -8,  21,  -8,
   -1,  -8,  -2,  -8,  -3,  -8,  -5,  -8,  -8,  -8,
  -13,  -8, -17,  -8, -21,  -8,   0, -13,   1, -13,
    2, -13,   3, -13,   5, -13,   8, -13,  13, -13,
   21, -13,  -1, -13,  -2, -13,  -3, -13,  -5, -13,
   -8, -13, -13, -13, -17, -13, -21, -13,   0, -17,
    1, -17,   2, -17,   3, -17,   5, -17,   8, -17,
   13, -17,  21, -17,  -1, -17,  -2, -17,  -3, -17,
   -5, -17,  -8, -17, -13, -17, -17, -17, -21, -17,
    0, -21,   1, -21,   2, -21,   3, -21,   5, -21,
    8, -21,  13, -21,  21, -21,  -1, -21,  -2, -21,
   -3, -21,  -5, -21,  -8, -21, -13, -21, -17, -21,
    0,   0,  -8, -29,   8, -29, -18, -25,  17, -25,
    0, -23,  -6, -22,   6, -22, -13, -19,  12, -19,
    0, -18,  25, -18, -25, -17,  -5, -17,   5, -17,
  -10, -15,  10, -15,   0, -14,  -4, -13,   4, -13,
   19, -13, -19, -12,  -8, -11,  -2, -11,   0, -11,
    2, -11,   8, -11, -15, -10,  -4, -10,   4, -10,
   15, -10,  -6,  -9,  -1,  -9,   1,  -9,   6,  -9,
  -29,  -8, -11,  -8,  -8,  -8,  -3,  -8,   3,  -8,
    8,  -8,  11,  -8,  29,  -8,  -5,  -7,  -2,  -7,
    0,  -7,   2,  -7,   5,  -7, -22,  -6,  -9,  -6,
   -6,  -6,  -3,  -6,  -1,  -6,   1,  -6,   3,  -6,
    6,  -6,   9,  -6,  22,  -6, -17,  -5,  -7,  -5,
   -4,  -5,  -2,  -5,   0,  -5,   2,  -5,   4,  -5,
    7,  -5,  17,  -5, -13,  -4, -10,  -4,  -5,  -4,
   -3,  -4,  -1,  -4,   0,  -4,   1,  -4,   3,  -4,
    5,  -4,  10,  -4,  13,  -4,  -8,  -3,  -6,  -3,
   -4,  -3,  -3,  -3,  -2,  -3,  -1,  -3,   0,  -3,
    1,  -3,   2,  -3,   4,  -3,   6,  -3,   8,  -3,
  -11,  -2,  -7,  -2,  -5,  -2,  -3,  -2,  -2,  -2,
   -1,  -2,   0,  -2,   1,  -2,   2,  -2,   3,  -2,
    5,  -2,   7,  -2,  11,  -2,  -9,  -1,  -6,  -1,
   -4,  -1,  -3,  -1,  -2,  -1,  -1,  -1,   0,  -1,
    1,  -1,   2,  -1,   3,  -1,   4,  -1,   6,  -1,
    9,  -1, -31,   0, -23,   0, -18,   0, -14,   0,
  -11,   0,  -7,   0,  -5,   0,  -4,   0,  -3,   0,
   -2,   0,  -1,   0,   0, -31,   1,   0,   2,   0,
    3,   0,   4,   0,   5,   0,   7,   0,  11,   0,
   14,   0,  18,   0,  23,   0,  31,   0,  -9,   1,
   -6,   1,  -4,   1,  -3,   1,  -2,   1,  -1,   1,
    0,   1,   1,   1,   2,   1,   3,   1,   4,   1,
    6,   1,   9,   1, -11,   2,  -7,   2,  -5,   2,
   -3,   2,  -2,   2,  -1,   2,   0,   2,   1,   2,
    2,   2,   3,   2,   5,   2,   7,   2,  11,   2,
   -8,   3,  -6,   3,  -4,   3,  -2,   3,  -1,   3,
    0,   3,   1,   3,   2,   3,   3,   3,   4,   3,
    6,   3,   8,   3, -13,   4, -10,   4,  -5,   4,
   -3,   4,  -1,   4,   0,   4,   1,   4,   3,   4,
    5,   4,  10,   4,  13,   4, -17,   5,  -7,   5,
   -4,   5,  -2,   5,   0,   5,   2,   5,   4,   5,
    7,   5,  17,   5, -22,   6,  -9,   6,  -6,   6,
   -3,   6,  -1,   6,   1,   6,   3,   6,   6,   6,
    9,   6,  22,   6,  -5,   7,  -2,   7,   0,   7,
    2,   7,   5,   7, -29,   8, -11,   8,  -8,   8,
   -3,   8,   3,   8,   8,   8,  11,   8,  29,   8,
   -6,   9,  -1,   9,   1,   9,   6,   9, -15,  10,
   -4,  10,   4,  10,  15,  10,  -8,  11,  -2,  11,
    0,  11,   2,  11,   8,  11,  19,  12, -19,  13,
   -4,  13,   4,  13,   0,  14, -10,  15,  10,  15,
   -5,  17,   5,  17,  25,  17, -25,  18,   0,  18,
  -12,  19,  13,  19,  -6,  22,   6,  22,   0,  23,
  -17,  25,  18,  25,  -8,  29,   8,  29,   0,  31,
    0,   0,  -6, -22,   6, -22, -13, -19,  12, -19,
    0, -18,  -5, -17,   5, -17, -10, -15,  10, -15,
    0, -14,  -4, -13,   4, -13,  19, -13, -19, -12,
   -8, -11,  -2, -11,   0, -11,   2, -11,   8, -11,
  -15, -10,  -4, -10,   4, -10,  15, -10,  -6,  -9,
   -1,  -9,   1,  -9,   6,  -9, -11,  -8,  -8,  -8,
   -3,  -8,   0,  -8,   3,  -8,   8,  -8,  11,  -8,
   -5,  -7,  -2,  -7,   0,  -7,   2,  -7,   5,  -7,
  -22,  -6,  -9,  -6,  -6,  -6,  -3,  -6,  -1,  -6,
    1,  -6,   3,  -6,   6,  -6,   9,  -6,  22,  -6,
  -17,  -5,  -7,  -5,  -4,  -5,  -2,  -5,  -1,  -5,
    0,  -5,   1,  -5,   2,  -5,   4,  -5,   7,  -5,
   17,  -5, -13,  -4, -10,  -4,  -5,  -4,  -3,  -4,
   -2,  -4,  -1,  -4,   0,  -4,   1,  -4,   2,  -4,
    3,  -4,   5,  -4,  10,  -4,  13,  -4,  -8,  -3,
   -6,  -3,  -4,  -3,  -3,  -3,  -2,  -3,  -1,  -3,
    0,  -3,   1,  -3,   2,  -3,   3,  -3,   4,  -3,
    6,  -3,   8,  -3, -11,  -2,  -7,  -2,  -5,  -2,
   -4,  -2,  -3,  -2,  -2,  -2,  -1,  -2,   0,  -2,
    1,  -2,   2,  -2,   3,  -2,   4,  -2,   5,  -2,
    7,  -2,  11,  -2,  -9,  -1,  -6,  -1,  -5,  -1,
   -4,  -1,  -3,  -1,  -2,  -1,  -1,  -1,   0,  -1,
    1,  -1,   2,  -1,   3,  -1,   4,  -1,   5,  -1,
    6,  -1,   9,  -1, -23,   0, -18,   0, -14,   0,
  -11,   0,  -7,   0,  -5,   0,  -4,   0,  -3,   0,
   -2,   0,  -1,   0,   0, -23,   1,   0,   2,   0,
    3,   0,   4,   0,   5,   0,   7,   0,  11,   0,
   14,   0,  18,   0,  23,   0,  -9,   1,  -6,   1,
   -5,   1,  -4,   1,  -3,   1,  -2,   1,  -1,   1,
    0,   1,   1,   1,   2,   1,   3,   1,   4,   1,
    5,   1,   6,   1,   9,   1, -11,   2,  -7,   2,
   -5,   2,  -4,   2,  -3,   2,  -2,   2,  -1,   2,
    0,   2,   1,   2,   2,   2,   3,   2,   4,   2,
    5,   2,   7,   2,  11,   2,  -8,   3,  -6,   3,
   -4,   3,  -3,   3,  -2,   3,  -1,   3,   0,   3,
    1,   3,   2,   3,   3,   3,   4,   3,   6,   3,
    8,   3, -13,   4, -10,   4,  -5,   4,  -3,   4,
   -2,   4,  -1,   4,   0,   4,   1,   4,   2,   4,
    3,   4,   5,   4,  10,   4,  13,   4, -17,   5,
   -7,   5,  -4,   5,  -2,   5,  -1,   5,   0,   5,
    1,   5,   2,   5,   4,   5,   7,   5,  17,   5,
  -22,   6,  -9,   6,  -6,   6,  -3,   6,  -1,   6,
    1,   6,   3,   6,   6,   6,   9,   6,  22,   6,
   -5,   7,  -2,   7,   0,   7,   2,   7,   5,   7,
  -11,   8,  -8,   8,  -3,   8,   0,   8,   3,   8,
    8,   8,  11,   8,  -6,   9,  -1,   9,   1,   9,
    6,   9, -15,  10,  -4,  10,   4,  10,  15,  10,
   -8,  11,  -2,  11,   0,  11,   2,  11,   8,  11,
   19,  12, -19,  13,  -4,  13,   4,  13,   0,  14,
  -10,  15,  10,  15,  -5,  17,   5,  17,   0,  18,
  -12,  19,  13,  19,  -6,  22,   6,  22,   0,  23,
};

typedef struct SANMVideoContext {
    AVCodecContext *avctx;
    GetByteContext gb;

    int version, subversion, have_dimensions, first_fob;
    uint32_t pal[PALETTE_SIZE];
    int16_t delta_pal[PALETTE_DELTA];

    ptrdiff_t pitch;
    int width, height;
    int aligned_width, aligned_height;
    int prev_seq;

    AVFrame *frame;
    uint16_t *fbuf, *frm0, *frm1, *frm2;
    uint8_t *stored_frame;
    uint32_t fbuf_size, frm0_size, frm1_size, frm2_size;
    uint32_t stored_frame_size;

    uint8_t *rle_buf;
    unsigned int rle_buf_size;

    int rotate_code;

    long npixels, buf_size;

    uint16_t codebook[256];
    uint16_t small_codebook[4];

    int8_t p4x4glyphs[NGLYPHS][16];
    int8_t p8x8glyphs[NGLYPHS][64];
    uint8_t c47itbl[0x10000];
    uint8_t c23lut[256];
    uint8_t c4tbl[2][256][16];
    uint16_t c4param;
} SANMVideoContext;

typedef struct SANMFrameHeader {
    int seq_num, codec, rotate_code, rle_output_size;

    uint16_t bg_color;
    uint32_t width, height;
} SANMFrameHeader;

enum GlyphEdge {
    LEFT_EDGE,
    TOP_EDGE,
    RIGHT_EDGE,
    BOTTOM_EDGE,
    NO_EDGE
};

enum GlyphDir {
    DIR_LEFT,
    DIR_UP,
    DIR_RIGHT,
    DIR_DOWN,
    NO_DIR
};

/**
 * Return enum GlyphEdge of box where point (x, y) lies.
 *
 * @param x x point coordinate
 * @param y y point coordinate
 * @param edge_size box width/height.
 */
static enum GlyphEdge which_edge(int x, int y, int edge_size)
{
    const int edge_max = edge_size - 1;

    if (!y)
        return BOTTOM_EDGE;
    else if (y == edge_max)
        return TOP_EDGE;
    else if (!x)
        return LEFT_EDGE;
    else if (x == edge_max)
        return RIGHT_EDGE;
    else
        return NO_EDGE;
}

static enum GlyphDir which_direction(enum GlyphEdge edge0, enum GlyphEdge edge1)
{
    if ((edge0 == LEFT_EDGE && edge1 == RIGHT_EDGE) ||
        (edge1 == LEFT_EDGE && edge0 == RIGHT_EDGE) ||
        (edge0 == BOTTOM_EDGE && edge1 != TOP_EDGE) ||
        (edge1 == BOTTOM_EDGE && edge0 != TOP_EDGE))
        return DIR_UP;
    else if ((edge0 == TOP_EDGE && edge1 != BOTTOM_EDGE) ||
             (edge1 == TOP_EDGE && edge0 != BOTTOM_EDGE))
        return DIR_DOWN;
    else if ((edge0 == LEFT_EDGE && edge1 != RIGHT_EDGE) ||
             (edge1 == LEFT_EDGE && edge0 != RIGHT_EDGE))
        return DIR_LEFT;
    else if ((edge0 == TOP_EDGE && edge1 == BOTTOM_EDGE) ||
             (edge1 == TOP_EDGE && edge0 == BOTTOM_EDGE) ||
             (edge0 == RIGHT_EDGE && edge1 != LEFT_EDGE) ||
             (edge1 == RIGHT_EDGE && edge0 != LEFT_EDGE))
        return DIR_RIGHT;

    return NO_DIR;
}

/* Interpolate two points. */
static void interp_point(int8_t *points, int x0, int y0, int x1, int y1,
                         int pos, int npoints)
{
    if (npoints) {
        points[0] = (x0 * pos + x1 * (npoints - pos) + (npoints >> 1)) / npoints;
        points[1] = (y0 * pos + y1 * (npoints - pos) + (npoints >> 1)) / npoints;
    } else {
        points[0] = x0;
        points[1] = y0;
    }
}

/**
 * Construct glyphs by iterating through vector coordinates.
 *
 * @param pglyphs pointer to table where glyphs are stored
 * @param xvec pointer to x component of vector coordinates
 * @param yvec pointer to y component of vector coordinates
 * @param side_length glyph width/height.
 */
static void make_glyphs(int8_t *pglyphs, const int8_t *xvec, const int8_t *yvec,
                        const int side_length)
{
    const int glyph_size = side_length * side_length;
    int8_t *pglyph = pglyphs;

    int i, j;
    for (i = 0; i < GLYPH_COORD_VECT_SIZE; i++) {
        int x0 = xvec[i];
        int y0 = yvec[i];
        enum GlyphEdge edge0 = which_edge(x0, y0, side_length);

        for (j = 0; j < GLYPH_COORD_VECT_SIZE; j++, pglyph += glyph_size) {
            int x1 = xvec[j];
            int y1 = yvec[j];
            enum GlyphEdge edge1 = which_edge(x1, y1, side_length);
            enum GlyphDir dir = which_direction(edge0, edge1);
            int npoints = FFMAX(FFABS(x1 - x0), FFABS(y1 - y0));
            int ipoint;

            for (ipoint = 0; ipoint <= npoints; ipoint++) {
                int8_t point[2];
                int irow, icol;

                interp_point(point, x0, y0, x1, y1, ipoint, npoints);

                switch (dir) {
                case DIR_UP:
                    for (irow = point[1]; irow >= 0; irow--)
                        pglyph[point[0] + irow * side_length] = 1;
                    break;

                case DIR_DOWN:
                    for (irow = point[1]; irow < side_length; irow++)
                        pglyph[point[0] + irow * side_length] = 1;
                    break;

                case DIR_LEFT:
                    for (icol = point[0]; icol >= 0; icol--)
                        pglyph[icol + point[1] * side_length] = 1;
                    break;

                case DIR_RIGHT:
                    for (icol = point[0]; icol < side_length; icol++)
                        pglyph[icol + point[1] * side_length] = 1;
                    break;
                }
            }
        }
    }
}

static void init_sizes(SANMVideoContext *ctx, int width, int height)
{
    ctx->width   = width;
    ctx->height  = height;
    ctx->npixels = width * height;

    ctx->aligned_width  = FFALIGN(width, 8);
    ctx->aligned_height = FFALIGN(height, 8);

    ctx->buf_size = ctx->aligned_width * ctx->aligned_height * sizeof(ctx->frm0[0]);
    ctx->pitch    = width;
}

static void destroy_buffers(SANMVideoContext *ctx)
{
    av_freep(&ctx->fbuf);
    av_freep(&ctx->frm0);
    av_freep(&ctx->frm1);
    av_freep(&ctx->frm2);
    av_freep(&ctx->stored_frame);
    av_freep(&ctx->rle_buf);
    ctx->frm0_size =
    ctx->frm1_size =
    ctx->frm2_size = 0;
    init_sizes(ctx, 0, 0);
}

static av_cold int init_buffers(SANMVideoContext *ctx)
{
    av_fast_padded_mallocz(&ctx->fbuf, &ctx->fbuf_size, ctx->buf_size);
    av_fast_padded_mallocz(&ctx->frm0, &ctx->frm0_size, ctx->buf_size);
    av_fast_padded_mallocz(&ctx->frm1, &ctx->frm1_size, ctx->buf_size);
    av_fast_padded_mallocz(&ctx->frm2, &ctx->frm2_size, ctx->buf_size);
    if (!ctx->version)
        av_fast_padded_mallocz(&ctx->stored_frame,
                              &ctx->stored_frame_size, ctx->buf_size);

    if (!ctx->frm0 || !ctx->frm1 || !ctx->frm2 ||
        (!ctx->stored_frame && !ctx->version)) {
        destroy_buffers(ctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static void codec33_gen_tiles(SANMVideoContext *ctx, int8_t param1)
{
    uint8_t *dst = &(ctx->c4tbl[0][0][0]);
    int i, j, k, l, m, n, o, p;

    for (i = 0; i < 8; i++) {
        for (k = 0; k < 8; k++) {
            j = i + param1;
            l = k + param1;
            p = (j + l) >> 1;
            n = (j + p) >> 1;
            m = (p + l) >> 1;

            *dst++ = p; *dst++ = p; *dst++ = n; *dst++ = j;
            *dst++ = p; *dst++ = p; *dst++ = n; *dst++ = j;
            *dst++ = m; *dst++ = m; *dst++ = p; *dst++ = j;
            *dst++ = l; *dst++ = l; *dst++ = m; *dst++ = p;
        }
    }

    for (i = 0; i < 8; i++) {
        for (k = 0; k < 8; k++) {
            j = i + param1;
            l = k + param1;
            n = (j + l) >> 1;
            m = (l + n) >> 1;

            *dst++ = j; *dst++ = j; *dst++ = j; *dst++ = j;
            *dst++ = n; *dst++ = n; *dst++ = n; *dst++ = n;
            *dst++ = m; *dst++ = m; *dst++ = m; *dst++ = m;
            *dst++ = l; *dst++ = l; *dst++ = l; *dst++ = l;
        }
    }

    for (i = 0; i < 8; i++) {
        for (k = 0; k < 8; k++) {
            j = i + param1;
            l = k + param1;
            m = (j + l) >> 1;
            n = (j + m) >> 1;
            o = (l + m) >> 1;

            *dst++ = j; *dst++ = j; *dst++ = n; *dst++ = m;
            *dst++ = j; *dst++ = j; *dst++ = n; *dst++ = m;
            *dst++ = n; *dst++ = n; *dst++ = m; *dst++ = o;
            *dst++ = m; *dst++ = m; *dst++ = o; *dst++ = l;
        }
    }

    for (i = 0; i < 8; i++) {
        for (k = 0; k < 8; k++) {
            j = i + param1;
            l = k + param1;
            m = (j + l) >> 1;
            n = (l + m) >> 1;

            *dst++ = j; *dst++ = m; *dst++ = n; *dst++ = l;
            *dst++ = j; *dst++ = m; *dst++ = n; *dst++ = l;
            *dst++ = j; *dst++ = m; *dst++ = n; *dst++ = l;
            *dst++ = j; *dst++ = m; *dst++ = n; *dst++ = l;
        }
    }
}

static void codec4_gen_tiles(SANMVideoContext *ctx, uint16_t param1)
{
    uint8_t *dst = &(ctx->c4tbl[0][0][0]);
    int i, j, k, l, m, n, o;

    for (i = 1; i < 16; i += 2) {
        for (k = 0; k < 16; k++) {
            j = i + param1;
            l = k + param1;
            m = (j + l) / 2;
            n = (j + m) / 2;
            o = (l + m) / 2;
            if (j == m || l == m) {
                *dst++ = l; *dst++ = j; *dst++ = l; *dst++ = j;
                *dst++ = j; *dst++ = l; *dst++ = j; *dst++ = j;
                *dst++ = l; *dst++ = j; *dst++ = l; *dst++ = j;
                *dst++ = l; *dst++ = l; *dst++ = j; *dst++ = l;
            } else {
                *dst++ = m; *dst++ = m; *dst++ = n; *dst++ = j;
                *dst++ = m; *dst++ = m; *dst++ = n; *dst++ = j;
                *dst++ = o; *dst++ = o; *dst++ = m; *dst++ = n;
                *dst++ = l; *dst++ = l; *dst++ = o; *dst++ = m;
            }
        }
    }

    for (i = 0; i < 16; i += 2) {
        for (k = 0; k < 16; k++) {
            j = i + param1;
            l = k + param1;
            m = (j + l) / 2;
            n = (j + m) / 2;
            o = (l + m) / 2;
            if (m == j || m == l) {
                *dst++ = j; *dst++ = j; *dst++ = l; *dst++ = j;
                *dst++ = j; *dst++ = j; *dst++ = j; *dst++ = l;
                *dst++ = l; *dst++ = j; *dst++ = l; *dst++ = l;
                *dst++ = j; *dst++ = l; *dst++ = j; *dst++ = l;
            } else {
                *dst++ = j; *dst++ = j; *dst++ = n; *dst++ = m;
                *dst++ = j; *dst++ = j; *dst++ = n; *dst++ = m;
                *dst++ = n; *dst++ = n; *dst++ = m; *dst++ = o;
                *dst++ = m; *dst++ = m; *dst++ = o; *dst++ = l;
            }
        }
    }
}


static int codec4_load_tiles(SANMVideoContext *ctx, GetByteContext *gb,
                             uint16_t param2, uint8_t clr)
{
    uint8_t c, *dst = (uint8_t *)&(ctx->c4tbl[1][0][0]);
    uint32_t loop = param2 * 8;

    if ((param2 > 256) || (bytestream2_get_bytes_left(gb) < loop))
        return AVERROR_INVALIDDATA;

    while (loop--) {
        c = bytestream2_get_byteu(gb);
        *dst++ = (c >> 4) + clr;
        *dst++ = (c & 0xf) + clr;
    }

    return 0;
}

static void rotate_bufs(SANMVideoContext *ctx, int rotate_code)
{
    if (rotate_code == 2)
        FFSWAP(uint16_t*, ctx->frm1, ctx->frm2);
    FFSWAP(uint16_t*, ctx->frm2, ctx->frm0);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    SANMVideoContext *ctx = avctx->priv_data;

    ctx->avctx   = avctx;
    ctx->version = !avctx->extradata_size;
    // early sanity check before allocations to avoid need for deallocation code.
    if (!ctx->version && avctx->extradata_size < 1026) {
        av_log(avctx, AV_LOG_ERROR, "Not enough extradata.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = ctx->version ? AV_PIX_FMT_RGB565 : AV_PIX_FMT_PAL8;

    init_sizes(ctx, avctx->width, avctx->height);
    if (init_buffers(ctx)) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating buffers.\n");
        return AVERROR(ENOMEM);
    }

    make_glyphs(ctx->p4x4glyphs[0], glyph4_x, glyph4_y, 4);
    make_glyphs(ctx->p8x8glyphs[0], glyph8_x, glyph8_y, 8);

    if (!ctx->version) {
        int i;

        ctx->subversion = AV_RL16(avctx->extradata);
        for (i = 0; i < PALETTE_SIZE; i++)
            ctx->pal[i] = 0xFFU << 24 | AV_RL32(avctx->extradata + 2 + i * 4);
        if (ctx->subversion < 2)
            ctx->pal[0] = 0xFFU << 24;
    }
    ctx->c4param = 0xffff;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    SANMVideoContext *ctx = avctx->priv_data;

    destroy_buffers(ctx);

    return 0;
}

static int old_codec4(SANMVideoContext *ctx, GetByteContext *gb, int top, int left,
                      int w, int h, uint8_t param, uint16_t param2, int codec)
{
    const uint16_t p = ctx->pitch;
    const uint32_t maxpxo = ctx->height * p;
    uint8_t mask, bits, idx, *gs, *dst = (uint8_t *)ctx->fbuf;
    int i, j, k, l, bit, ret;
    int32_t pxoff, pxo2;

    if (ctx->c4param != param) {
        if (codec > 32)
            codec33_gen_tiles(ctx, param);
        else
            codec4_gen_tiles(ctx, param);
        ctx->c4param = param;
    }
    if (param2 > 0) {
        ret = codec4_load_tiles(ctx, gb, param2, param);
        if (ret)
            return ret;
    }

    if (codec > 32)
        codec -= 29;

    for (j = 0; j < w; j += 4) {
        mask = bits = 0;
        for (i = 0; i < h; i += 4) {
            pxoff = j + left + ((top + i) * p);
            if (param2 > 0) {
                if (bits == 0) {
                    if (bytestream2_get_bytes_left(gb) < 1)
                        return AVERROR_INVALIDDATA;
                    mask = bytestream2_get_byteu(gb);
                    bits = 8;
                }
                bit = !!(mask & 0x80);
                mask <<= 1;
                bits--;
            } else {
                bit = 0;
            }

            if (bytestream2_get_bytes_left(gb) < 1)
                return AVERROR_INVALIDDATA;
            idx = bytestream2_get_byteu(gb);
            if ((bit == 0) && (idx == 0x80) && (codec != 5))
                continue;

            gs = &(ctx->c4tbl[bit][idx][0]);
            pxo2 = pxoff;
            for (k = 0; k < 4; k++) {
                for (l = 0; l < 4; l++) {
                    if (pxo2 >= 0 && pxo2 < maxpxo) {
                        *(dst + pxo2) = *gs;
                    }
                    gs++;
                    pxo2++;
                }
                pxo2 = pxo2 - 4 + p;
            }
        }
    }
    return 0;
}

static int rle_decode(SANMVideoContext *ctx, GetByteContext *gb, uint8_t *dst, const int out_size)
{
    int opcode, color, run_len, left = out_size;

    while (left > 0) {
        opcode = bytestream2_get_byte(gb);
        run_len = (opcode >> 1) + 1;
        if (run_len > left || bytestream2_get_bytes_left(gb) <= 0)
            return AVERROR_INVALIDDATA;

        if (opcode & 1) {
            color = bytestream2_get_byte(gb);
            memset(dst, color, run_len);
        } else {
            if (bytestream2_get_bytes_left(gb) < run_len)
                return AVERROR_INVALIDDATA;
            bytestream2_get_bufferu(gb, dst, run_len);
        }

        dst  += run_len;
        left -= run_len;
    }

    return 0;
}

static int old_codec23(SANMVideoContext *ctx, GetByteContext *gb, int top, int left,
                       int width, int height, uint8_t param, uint16_t param2)
{
    const uint32_t maxpxo = ctx->height * ctx->pitch;
    uint8_t *dst, lut[256], c;
    int i, j, k, pc, sk;
    int32_t pxoff;

    if (ctx->subversion < 2) {
        /* Rebel Assault 1: constant offset + 0xd0 */
        for (i = 0; i < 256; i++)
            lut[i] = (i + param + 0xd0) & 0xff;
    } else if (param2 == 256) {
        if (bytestream2_get_bytes_left(gb) < 256)
            return AVERROR_INVALIDDATA;
        bytestream2_get_bufferu(gb, ctx->c23lut, 256);
    } else if (param2 < 256) {
        for (i = 0; i < 256; i++)
            lut[i] = (i + param2) & 0xff;
    } else {
        memcpy(lut, ctx->c23lut, 256);
    }
    if (bytestream2_get_bytes_left(gb) < 1)
        return 0;  /* some c23 frames just set up the LUT */

    dst = (uint8_t *)ctx->fbuf;
    for (i = 0; i < height; i++) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return 0;
        pxoff = left + ((top + i) * ctx->pitch);
        k = bytestream2_get_le16u(gb);
        sk = 1;
        pc = 0;
        while (k > 0 && pc <= width) {
            if (bytestream2_get_bytes_left(gb) < 1)
                return AVERROR_INVALIDDATA;
            j = bytestream2_get_byteu(gb);
            if (sk) {
                pxoff += j;
                pc += j;
            } else {
                while (j--) {
                    if (pxoff >=0 && pxoff < maxpxo) {
                        c = *(dst + pxoff);
                        *(dst + pxoff) = lut[c];
                    }
                    pxoff++;
                    pc++;
                }
            }
            sk ^= 1;
        }
    }
    return 0;
}

static int old_codec21(SANMVideoContext *ctx, GetByteContext *gb, int top, int left,
                       int width, int height)
{
    const uint32_t maxpxo = ctx->height * ctx->pitch;
    uint8_t *dst = (uint8_t *)ctx->fbuf, c;
    int i, j, k, pc, sk, pxoff;

    dst = (uint8_t *)ctx->fbuf;
    for (i = 0; i < height; i++) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return 0;
        pxoff = left + ((top + i) * ctx->pitch);
        k = bytestream2_get_le16u(gb);
        sk = 1;
        pc = 0;
        while (k > 0 && pc <= width) {
            if (bytestream2_get_bytes_left(gb) < 2)
                return AVERROR_INVALIDDATA;
            j = bytestream2_get_le16u(gb);
            k -= 2;
            if (sk) {
                pxoff += j;
                pc += j;
            } else {
                if (bytestream2_get_bytes_left(gb) < (j + 1))
                    return AVERROR_INVALIDDATA;
                do {
                    c = bytestream2_get_byteu(gb);
                    if (pxoff >=0 && pxoff < maxpxo) {
                        *(dst + pxoff) = c;
                    }
                    pxoff++;
                    pc++;
                    j--;
                    k--;
                } while (j > -1);
            }
            sk ^= 1;
        }
    }
    return 0;
}

static int old_codec1(SANMVideoContext *ctx, GetByteContext *gb, int top,
                      int left, int width, int height, int opaque)
{
    int i, j, len, flag, code, val, end, pxoff;
    const int maxpxo = ctx->height * ctx->pitch;
    uint8_t *dst = (uint8_t *)ctx->fbuf;

    for (i = 0; i < height; i++) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;

        len = bytestream2_get_le16u(gb);
        end = bytestream2_tell(gb) + len;

        pxoff = left + ((top + i) * ctx->pitch);
        while (bytestream2_tell(gb) < end) {
            if (bytestream2_get_bytes_left(gb) < 2)
                return AVERROR_INVALIDDATA;

            code = bytestream2_get_byteu(gb);
            flag = code & 1;
            code = (code >> 1) + 1;
            if (flag) {
                val = bytestream2_get_byteu(gb);
                if (val || opaque) {
                    for (j = 0; j < code; j++) {
                        if (pxoff >= 0 && pxoff < maxpxo)
                            *(dst + pxoff) = val;
                        pxoff++;
                    }
                } else {
                    pxoff += code;
                }
            } else {
                if (bytestream2_get_bytes_left(gb) < code)
                    return AVERROR_INVALIDDATA;
                for (j = 0; j < code; j++) {
                    val = bytestream2_get_byteu(gb);
                    if ((pxoff >= 0) && (pxoff < maxpxo) && (val || opaque))
                        *(dst + pxoff) = val;
                    pxoff++;
                }
            }
        }
    }
    ctx->rotate_code = 0;

    return 0;
}

static int old_codec31(SANMVideoContext *ctx, GetByteContext *gb, int top,
                       int left, int width, int height, int p1, int opaque)
{
    int i, j, len, flag, code, val, end, pxoff;
    const int maxpxo = ctx->height * ctx->pitch;
    uint8_t *dst = (uint8_t *)ctx->fbuf;

    for (i = 0; i < height; i++) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;

        len = bytestream2_get_le16u(gb);
        end = bytestream2_tell(gb) + len;

        pxoff = left + ((top + i) * ctx->pitch);
        while (bytestream2_tell(gb) < end) {
            if (bytestream2_get_bytes_left(gb) < 2)
                return AVERROR_INVALIDDATA;

            code = bytestream2_get_byteu(gb);
            flag = code & 1;
            code = (code >> 1) + 1;
            if (flag) {
                val = bytestream2_get_byteu(gb);
                for (j = 0; j < code; j++) {
                    if ((0 != (val & 0xf)) || opaque) {
                        if (pxoff >= 0 && pxoff < maxpxo)
                            *(dst + pxoff) = p1 + (val & 0xf);
                    }
                    pxoff++;
                    if ((0 != (val >> 4)) || opaque) {
                        if (pxoff >= 0 && pxoff < maxpxo)
                            *(dst + pxoff) = p1 + (val >> 4);
                    }
                    pxoff++;
                }
            } else {
                if (bytestream2_get_bytes_left(gb) < code)
                    return AVERROR_INVALIDDATA;
                for (j = 0; j < code; j++) {
                    val = bytestream2_get_byteu(gb);
                    if ((pxoff >= 0) && (pxoff < maxpxo) && ((0 != (val & 0xf)) || opaque))
                        *(dst + pxoff) = p1 + (val & 0xf);
                    pxoff++;
                    if ((pxoff >= 0) && (pxoff < maxpxo) && ((0 != (val >> 4)) || opaque))
                        *(dst + pxoff) = p1 + (val >> 4);
                    pxoff++;
                }
            }
        }
    }
    ctx->rotate_code = 0;

    return 0;
}

static int old_codec2(SANMVideoContext *ctx, GetByteContext *gb, int top,
                      int left, int width, int height)
{
    uint8_t *dst = (uint8_t *)ctx->fbuf, col;
    int16_t xpos = left, ypos = top;

    while (bytestream2_get_bytes_left(gb) > 3) {
        xpos += bytestream2_get_le16u(gb);
        ypos += bytestream2_get_byteu(gb);
        col = bytestream2_get_byteu(gb);
        if (xpos >= 0 && ypos >= 0 &&
            xpos < ctx->width && ypos < ctx->height) {
                *(dst + xpos + ypos * ctx->pitch) = col;
        }
    }
    return 0;
}

static int old_codec20(SANMVideoContext *ctx, int w, int h)
{
    uint8_t *dst = (uint8_t *)ctx->fbuf;

    if (bytestream2_get_bytes_left(&ctx->gb) < w * h)
        return AVERROR_INVALIDDATA;

    if (w == ctx->pitch) {
        bytestream2_get_bufferu(&ctx->gb, dst, w * h);
    } else {
        for (int i = 0; i < h; i++) {
            bytestream2_get_bufferu(&ctx->gb, dst, w);
            dst += ctx->pitch;
        }
    }
    return 0;
}

static inline void codec37_mv(uint8_t *dst, const uint8_t *src,
                              int height, int stride, int x, int y)
{
    int pos, i, j;

    pos = x + y * stride;
    for (j = 0; j < 4; j++) {
        for (i = 0; i < 4; i++) {
            if ((pos + i) < 0 || (pos + i) >= height * stride)
                dst[i] = 0;
            else
                dst[i] = src[i];
        }
        dst += stride;
        src += stride;
        pos += stride;
    }
}

static int old_codec37(SANMVideoContext *ctx, int width, int height)
{
    int i, j, k, l, t, run, len, code, skip, mx, my;
    ptrdiff_t stride = ctx->pitch;
    uint8_t *dst, *prev;
    int skip_run = 0;
    int compr = bytestream2_get_byte(&ctx->gb);
    int mvoff = bytestream2_get_byte(&ctx->gb);
    int seq   = bytestream2_get_le16(&ctx->gb);
    uint32_t decoded_size = bytestream2_get_le32(&ctx->gb);
    int flags;

    bytestream2_skip(&ctx->gb, 4);
    flags = bytestream2_get_byte(&ctx->gb);
    bytestream2_skip(&ctx->gb, 3);

    if (decoded_size > ctx->height * stride) {
        decoded_size = ctx->height * stride;
        av_log(ctx->avctx, AV_LOG_WARNING, "Decoded size is too large.\n");
    }

    ctx->rotate_code = 0;

    if (((seq & 1) || !(flags & 1)) && (compr && compr != 2)) {
        FFSWAP(uint16_t*, ctx->frm0, ctx->frm2);
    }

    dst  = ((uint8_t*)ctx->frm0);
    prev = ((uint8_t*)ctx->frm2);

    if (mvoff > 2) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Invalid motion base value %d.\n", mvoff);
        return AVERROR_INVALIDDATA;
    }

    switch (compr) {
    case 0:
        for (i = 0; i < height; i++) {
            bytestream2_get_buffer(&ctx->gb, dst, width);
            dst += stride;
        }
        memset(ctx->frm2, 0, ctx->height * stride);
        break;
    case 1:
        run = 0;
        len = -1;
        code = 0;

        for (j = 0; j < height; j += 4) {
            for (i = 0; i < width; i += 4) {
                if (len < 0) {
                    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                        return AVERROR_INVALIDDATA;
                    code = bytestream2_get_byte(&ctx->gb);
                    len = code >> 1;
                    run = code & 1;
                    skip = 0;
                } else {
                    skip = run;
                }

                if (!skip) {
                    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                        return AVERROR_INVALIDDATA;
                    code = bytestream2_get_byte(&ctx->gb);
                    if (code == 0xff) {
                        len--;
                        for (k = 0; k < 4; k++) {
                            for (l = 0; l < 4; l++) {
                                if (len < 0) {
                                    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                                        return AVERROR_INVALIDDATA;
                                    code = bytestream2_get_byte(&ctx->gb);
                                    len = code >> 1;
                                    run = code & 1;
                                    if (run) {
                                        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                                            return AVERROR_INVALIDDATA;
                                        code = bytestream2_get_byte(&ctx->gb);
                                    }
                                }
                                if (!run) {
                                    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                                            return AVERROR_INVALIDDATA;
                                        code = bytestream2_get_byte(&ctx->gb);
                                }
                                *(dst + i + (k * stride) + l) = code;
                                len--;
                            }
                        }
                        continue;
                    }
                }
                /* 4x4 block copy from prev with MV */
                mx = c37_mv[(mvoff * 255 + code) * 2];
                my = c37_mv[(mvoff * 255 + code) * 2 + 1];
                codec37_mv(dst + i, prev + i + mx + my * stride,
                           ctx->height, stride, i + mx, j + my);
                len--;
            }
            dst += stride * 4;
            prev += stride * 4;
        }
        break;
    case 2:
        if (rle_decode(ctx, &ctx->gb, dst, decoded_size))
            return AVERROR_INVALIDDATA;
        memset(ctx->frm2, 0, ctx->frm2_size);
        break;
    case 3:
    case 4:
        for (j = 0; j < height; j += 4) {
            for (i = 0; i < width; i += 4) {
                int code;
                if (skip_run) {
                    skip_run--;
                    copy_block4(dst + i, prev + i, stride, stride, 4);
                    continue;
                }
                if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                    return AVERROR_INVALIDDATA;
                code = bytestream2_get_byteu(&ctx->gb);
                if (code == 0xFF) {
                    if (bytestream2_get_bytes_left(&ctx->gb) < 16)
                        return AVERROR_INVALIDDATA;
                    for (k = 0; k < 4; k++)
                        bytestream2_get_bufferu(&ctx->gb, dst + i + k * stride, 4);
                } else if ((flags & 4) && (code == 0xFE)) {
                    if (bytestream2_get_bytes_left(&ctx->gb) < 4)
                       return AVERROR_INVALIDDATA;
                   for (k = 0; k < 4; k++)
                       memset(dst + i + k * stride, bytestream2_get_byteu(&ctx->gb), 4);
                } else if ((flags & 4) && (code == 0xFD)) {
                    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                        return AVERROR_INVALIDDATA;
                    t = bytestream2_get_byteu(&ctx->gb);
                    for (k = 0; k < 4; k++)
                        memset(dst + i + k * stride, t, 4);
               } else {
                    mx = c37_mv[(mvoff * 255 + code) * 2];
                    my = c37_mv[(mvoff * 255 + code) * 2 + 1];
                    codec37_mv(dst + i, prev + i + mx + my * stride,
                               ctx->height, stride, i + mx, j + my);

                    if ((compr == 4) && (code == 0)) {
                        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                            return AVERROR_INVALIDDATA;
                        skip_run = bytestream2_get_byteu(&ctx->gb);
                    }
                }
            }
            dst  += stride * 4;
            prev += stride * 4;
        }
        break;
    default:
        avpriv_report_missing_feature(ctx->avctx,
                                      "Subcodec 37 compression %d", compr);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int process_block(SANMVideoContext *ctx, uint8_t *dst, uint8_t *prev1,
                         uint8_t *prev2, int stride, int tbl, int size)
{
    int code, k, t;
    uint8_t colors[2];
    int8_t *pglyph;

    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
        return AVERROR_INVALIDDATA;

    code = bytestream2_get_byteu(&ctx->gb);
    if (code >= 0xF8) {
        switch (code) {
        case 0xFF:
            if (size == 2) {
                if (bytestream2_get_bytes_left(&ctx->gb) < 4)
                    return AVERROR_INVALIDDATA;
                dst[0]          = bytestream2_get_byteu(&ctx->gb);
                dst[1]          = bytestream2_get_byteu(&ctx->gb);
                dst[0 + stride] = bytestream2_get_byteu(&ctx->gb);
                dst[1 + stride] = bytestream2_get_byteu(&ctx->gb);
            } else {
                size >>= 1;
                if (process_block(ctx, dst, prev1, prev2, stride, tbl, size))
                    return AVERROR_INVALIDDATA;
                if (process_block(ctx, dst + size, prev1 + size, prev2 + size,
                                  stride, tbl, size))
                    return AVERROR_INVALIDDATA;
                dst   += size * stride;
                prev1 += size * stride;
                prev2 += size * stride;
                if (process_block(ctx, dst, prev1, prev2, stride, tbl, size))
                    return AVERROR_INVALIDDATA;
                if (process_block(ctx, dst + size, prev1 + size, prev2 + size,
                                  stride, tbl, size))
                    return AVERROR_INVALIDDATA;
            }
            break;
        case 0xFE:
            if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                return AVERROR_INVALIDDATA;

            t = bytestream2_get_byteu(&ctx->gb);
            for (k = 0; k < size; k++)
                memset(dst + k * stride, t, size);
            break;
        case 0xFD:
            if (bytestream2_get_bytes_left(&ctx->gb) < 3)
                return AVERROR_INVALIDDATA;

            code = bytestream2_get_byteu(&ctx->gb);
            pglyph = (size == 8) ? ctx->p8x8glyphs[code] : ctx->p4x4glyphs[code];
            bytestream2_get_bufferu(&ctx->gb, colors, 2);

            for (k = 0; k < size; k++)
                for (t = 0; t < size; t++)
                    dst[t + k * stride] = colors[!*pglyph++];
            break;
        case 0xFC:
            for (k = 0; k < size; k++)
                memcpy(dst + k * stride, prev1 + k * stride, size);
            break;
        default:
            k = bytestream2_tell(&ctx->gb);
            bytestream2_seek(&ctx->gb, tbl + (code & 7), SEEK_SET);
            t = bytestream2_get_byte(&ctx->gb);
            bytestream2_seek(&ctx->gb, k, SEEK_SET);
            for (k = 0; k < size; k++)
                memset(dst + k * stride, t, size);
        }
    } else {
        int mx = motion_vectors[code][0];
        int my = motion_vectors[code][1];
        int index = prev2 - (const uint8_t *)ctx->frm2;

        av_assert2(index >= 0 && index < (ctx->buf_size >> 1));

        if (index < -mx - my * stride ||
            (ctx->buf_size >> 1) - index < mx + size + (my + size - 1) * stride) {
            av_log(ctx->avctx, AV_LOG_ERROR, "MV is invalid.\n");
            return AVERROR_INVALIDDATA;
        }

        for (k = 0; k < size; k++)
            memcpy(dst + k * stride, prev2 + mx + (my + k) * stride, size);
    }

    return 0;
}

static void codec47_read_interptable(SANMVideoContext *ctx)
{
    uint8_t *p1, *p2, *itbl = ctx->c47itbl;
    int i, j;

    for (i = 0; i < 256; i++) {
        p1 = p2 = itbl + i;
        for (j = 256 - i; j; j--) {
            *p1 = *p2 = bytestream2_get_byte(&ctx->gb);
            p1 += 1;
            p2 += 256;
        }
        itbl += 256;
    }
}

static void codec47_comp1(SANMVideoContext *ctx, uint8_t *dst_in, int width,
                          int height, ptrdiff_t stride)
{
    uint8_t p1, *dst, *itbl = ctx->c47itbl;
    uint16_t px;
    int i, j;

    dst = dst_in + stride;
    for (i = 0; i < height; i += 2) {
        p1 = bytestream2_get_byte(&ctx->gb);
        *dst++ = p1;
        *dst++ = p1;
        px = p1;
        for (j = 2; j < width; j += 2) {
            p1 = bytestream2_get_byte(&ctx->gb);
            px = (px << 8) | p1;
            *dst++ = itbl[px];
            *dst++ = p1;
        }
        dst += stride;
    }

    memcpy(dst_in, dst_in + stride, width);
    dst = dst_in + stride + stride;
    for (i = 2; i < height - 1; i += 2) {
        for (j = 0; j < width; j++) {
            px = (*(dst - stride) << 8) | *(dst + stride);
            *dst++ = itbl[px];
        }
        dst += stride;
    }
}

static int old_codec47(SANMVideoContext *ctx, int width, int height)
{
    uint32_t decoded_size;
    int i, j;
    ptrdiff_t stride = ctx->pitch;
    uint8_t *dst   = (uint8_t *)ctx->frm0;
    uint8_t *prev1 = (uint8_t *)ctx->frm1;
    uint8_t *prev2 = (uint8_t *)ctx->frm2;
    uint8_t auxcol[2];
    int tbl_pos = bytestream2_tell(&ctx->gb);
    int seq     = bytestream2_get_le16(&ctx->gb);
    int compr   = bytestream2_get_byte(&ctx->gb);
    int new_rot = bytestream2_get_byte(&ctx->gb);
    int skip    = bytestream2_get_byte(&ctx->gb);

    bytestream2_skip(&ctx->gb, 7);
    auxcol[0] = bytestream2_get_byteu(&ctx->gb);
    auxcol[1] = bytestream2_get_byteu(&ctx->gb);
    decoded_size = bytestream2_get_le32(&ctx->gb);
    bytestream2_skip(&ctx->gb, 8);

    if (decoded_size > ctx->height * stride) {
        decoded_size = ctx->height * stride;
        av_log(ctx->avctx, AV_LOG_WARNING, "Decoded size is too large.\n");
    }

    if (skip & 1) {
        if (bytestream2_get_bytes_left(&ctx->gb) < 0x8080)
            return AVERROR_INVALIDDATA;
        codec47_read_interptable(ctx);
    }
    if (!seq) {
        ctx->prev_seq = -1;
        memset(prev1, auxcol[0], ctx->height * stride);
        memset(prev2, auxcol[1], ctx->height * stride);
    }

    switch (compr) {
    case 0:
        if (bytestream2_get_bytes_left(&ctx->gb) < width * height)
            return AVERROR_INVALIDDATA;
        for (j = 0; j < height; j++) {
            bytestream2_get_bufferu(&ctx->gb, dst, width);
            dst += stride;
        }
        break;
    case 1:
        if (bytestream2_get_bytes_left(&ctx->gb) < ((width + 1) >> 1) * ((height + 1) >> 1))
            return AVERROR_INVALIDDATA;
        codec47_comp1(ctx, dst, width, height, stride);
        break;
    case 2:
        if (seq == ctx->prev_seq + 1) {
            for (j = 0; j < height; j += 8) {
                for (i = 0; i < width; i += 8)
                    if (process_block(ctx, dst + i, prev1 + i, prev2 + i, stride,
                                      tbl_pos + 8, 8))
                        return AVERROR_INVALIDDATA;
                dst   += stride * 8;
                prev1 += stride * 8;
                prev2 += stride * 8;
            }
        }
        break;
    case 3:
        memcpy(ctx->frm0, ctx->frm2, ctx->pitch * ctx->height);
        break;
    case 4:
        memcpy(ctx->frm0, ctx->frm1, ctx->pitch * ctx->height);
        break;
    case 5:
        if (rle_decode(ctx, &ctx->gb, dst, decoded_size))
            return AVERROR_INVALIDDATA;
        break;
    default:
        avpriv_report_missing_feature(ctx->avctx,
                                      "Subcodec 47 compression %d", compr);
        return AVERROR_PATCHWELCOME;
    }
    if (seq == ctx->prev_seq + 1)
        ctx->rotate_code = new_rot;
    else
        ctx->rotate_code = 0;
    ctx->prev_seq = seq;

    return 0;
}

// scale 4x4 input block to an 8x8 output block
static void c48_4to8(uint8_t *dst, const uint8_t *src, const uint16_t w)
{
    uint16_t p;
    // dst is always at least 16bit aligned
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j += 2) {
            p = *src++;
            p = (p << 8) | p;
            *((uint16_t *)(dst + w * 0 + j)) = p;
            *((uint16_t *)(dst + w * 1 + j)) = p;
        }
        dst += w * 2;
    }
}

static int codec48_block(SANMVideoContext *ctx, uint8_t *dst, uint8_t *db,
                         const uint16_t w)
{
    uint8_t opc, sb[16];
    int i, j, k, l;
    int16_t mvofs;
    uint32_t ofs;

    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
        return 1;

    opc = bytestream2_get_byteu(&ctx->gb);
    switch (opc) {
    case 0xFF:    // 1x1 -> 8x8 block scale
        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
            return 1;

        opc = bytestream2_get_byteu(&ctx->gb);
        for (i = 0; i < 16; i++)
            sb[i] = opc;
        c48_4to8(dst, sb, w);
        break;
    case 0xFE:    // 1x 8x8 copy from deltabuf, 16bit mv from source
        if (bytestream2_get_bytes_left(&ctx->gb) < 2)
            return 1;
        mvofs =  bytestream2_get_le16(&ctx->gb);
        for (i = 0; i < 8; i++) {
            ofs = w * i;
            for (k = 0; k < 8; k++)
                *(dst + ofs + k) = *(db + ofs + k + mvofs);
        }
        break;
    case 0xFD:    // 2x2 -> 8x8 block scale
        if (bytestream2_get_bytes_left(&ctx->gb) < 4)
            return 1;
        sb[ 5] =  bytestream2_get_byteu(&ctx->gb);
        sb[ 7] =  bytestream2_get_byteu(&ctx->gb);
        sb[13] =  bytestream2_get_byteu(&ctx->gb);
        sb[15] =  bytestream2_get_byteu(&ctx->gb);

        sb[0] = sb[1] = sb[4] = sb[5];
        sb[2] = sb[3] = sb[6] = sb[7];
        sb[8] = sb[9] = sb[12] = sb[13];
        sb[10] = sb[11] = sb[14] = sb[15];
        c48_4to8(dst, sb, w);
        break;
    case 0xFC:    // 4x copy 4x4 block, per-block c37_mv from source
        if (bytestream2_get_bytes_left(&ctx->gb) < 4)
            return 1;
        for (i = 0; i < 8; i += 4) {
            for (k = 0; k < 8; k += 4) {
                opc =  bytestream2_get_byteu(&ctx->gb);
                mvofs = c37_mv[opc * 2] + (c37_mv[opc * 2 + 1] * w);
                for (j = 0; j < 4; j++) {
                    ofs = (w * (j + i)) + k;
                    for (l = 0; l < 4; l++)
                        *(dst + ofs + l) = *(db + ofs + l + mvofs);
                }
            }
        }
        break;
    case 0xFB:    // Copy 4x 4x4 blocks, per-block mv from source
        if (bytestream2_get_bytes_left(&ctx->gb) < 8)
            return 1;
        for (i = 0; i < 8; i += 4) {
            for (k = 0; k < 8; k += 4) {
                mvofs = bytestream2_get_le16(&ctx->gb);
                for (j = 0; j < 4; j++) {
                    ofs = (w * (j + i)) + k;
                    for (l = 0; l < 4; l++)
                        *(dst + ofs + l) = *(db + ofs + l + mvofs);
                }
            }
        }
        break;
    case 0xFA:    // scale 4x4 input block to 8x8 dest block
        if (bytestream2_get_bytes_left(&ctx->gb) < 16)
            return 1;
        bytestream2_get_bufferu(&ctx->gb, sb, 16);
        c48_4to8(dst, sb, w);
        break;
    case 0xF9:    // 16x 2x2 copy from delta, per-block c37_mv from source
        if (bytestream2_get_bytes_left(&ctx->gb) < 16)
            return 1;
        for (i = 0; i < 8; i += 2) {
            for (j = 0; j < 8; j += 2) {
                ofs = (w * i) + j;
                opc = bytestream2_get_byteu(&ctx->gb);
                mvofs = c37_mv[opc * 2] + (c37_mv[opc * 2 + 1] * w);
                for (l = 0; l < 2; l++) {
                    *(dst + ofs + l + 0) = *(db + ofs + l + 0 + mvofs);
                    *(dst + ofs + l + w) = *(db + ofs + l + w + mvofs);
                }
            }
        }
        break;
    case 0xF8:    // 16x 2x2 blocks copy, 16bit mv from source
        if (bytestream2_get_bytes_left(&ctx->gb) < 32)
            return 1;
        for (i = 0; i < 8; i += 2) {
            for (j = 0; j < 8; j += 2) {
                ofs = w * i + j;
                mvofs = bytestream2_get_le16(&ctx->gb);
                for (l = 0; l < 2; l++) {
                    *(dst + ofs + l + 0) = *(db + ofs + l + 0 + mvofs);
                    *(dst + ofs + l + w) = *(db + ofs + l + w + mvofs);
                }
            }
        }
        break;
    case 0xF7:    // copy 8x8 block from src to dest
        if (bytestream2_get_bytes_left(&ctx->gb) < 64)
            return 1;
        for (i = 0; i < 8; i++) {
            ofs = i * w;
            for (l = 0; l < 8; l++)
                *(dst + ofs + l) = bytestream2_get_byteu(&ctx->gb);
        }
        break;
    default:    // copy 8x8 block from prev, c37_mv from source
        mvofs = c37_mv[opc * 2] + (c37_mv[opc * 2 + 1] * w);
        for (i = 0; i < 8; i++) {
            ofs = i * w;
            for (l = 0; l < 8; l++)
                *(dst + ofs + l) = *(db + ofs + l + mvofs);
        }
        break;
    }
    return 0;
}

static int old_codec48(SANMVideoContext *ctx, int width, int height)
{
    uint8_t *dst, *prev;
    int compr = bytestream2_get_byte(&ctx->gb);
    int mvidx = bytestream2_get_byte(&ctx->gb);
    int seq   = bytestream2_get_le16(&ctx->gb);
    uint32_t decoded_size = bytestream2_get_le32(&ctx->gb);
    int i, j, flags;

    // all codec48 videos use 1, but just to be safe...
    if (mvidx != 1) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Invalid motion base value %d.\n", mvidx);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skip(&ctx->gb, 4);
    flags = bytestream2_get_byte(&ctx->gb);
    bytestream2_skip(&ctx->gb, 3);

    if (flags & 8) {
        if (bytestream2_get_bytes_left(&ctx->gb) < 0x8080)
            return AVERROR_INVALIDDATA;
        codec47_read_interptable(ctx);
    }

    dst  = (uint8_t*)ctx->frm0;
    prev = (uint8_t*)ctx->frm2;

    if (!seq) {
        ctx->prev_seq = -1;
        memset(prev, 0, ctx->aligned_height * width);
    }

    switch (compr) {
    case 0:
        if (bytestream2_get_bytes_left(&ctx->gb) < width * height)
            return AVERROR_INVALIDDATA;
        for (j = 0; j < height; j++) {
            bytestream2_get_bufferu(&ctx->gb, dst, width);
            dst += width;
        }
        break;
    case 2:
        if (rle_decode(ctx, &ctx->gb, dst, decoded_size))
            return AVERROR_INVALIDDATA;
        break;
    case 3:
        if (seq == ctx->prev_seq + 1) {
            for (j = 0; j < height; j += 8) {
                for (i = 0; i < width; i += 8) {
                    if (codec48_block(ctx, dst + i, prev + i, width))
                        return AVERROR_INVALIDDATA;
                }
                dst += width * 8;
                prev += width * 8;
            }
        }
        break;
    case 5:
        if (bytestream2_get_bytes_left(&ctx->gb) < ((width + 1) >> 1) * ((height + 1) >> 1))
            return AVERROR_INVALIDDATA;
        codec47_comp1(ctx, dst, width, height, width);
        break;
    case 6:      // in some videos of "Star Wars - Making Magic", ignored.
        break;
    default:
        avpriv_report_missing_feature(ctx->avctx,
                                      "Subcodec 48 compression %d", compr);
        return AVERROR_PATCHWELCOME;
    }
    ctx->rotate_code = 1;    // swap frm[0] and frm[2]
    ctx->prev_seq = seq;
    return 0;
}

static int process_frame_obj(SANMVideoContext *ctx, GetByteContext *gb)
{
    uint16_t w, h, parm2;
    uint8_t codec, param;
    int16_t left, top;
    int fsc, sote, ret;

    codec = bytestream2_get_byteu(gb);
    param = bytestream2_get_byteu(gb);
    left  = bytestream2_get_le16u(gb);
    top   = bytestream2_get_le16u(gb);
    w     = bytestream2_get_le16u(gb);
    h     = bytestream2_get_le16u(gb);
    bytestream2_skip(gb, 2);
    parm2 = bytestream2_get_le16u(gb);

    if (w < 1 || h < 1 || w > 800 || h > 600 || left > 800 || top > 600) {
        av_log(ctx->avctx, AV_LOG_WARNING,
               "ignoring invalid fobj dimensions: c%d %d %d @ %d %d\n",
               codec, w, h, left, top);
        return 0;
    }

    /* codecs with their own buffers */
    fsc = (codec == 37 || codec == 47 || codec == 48);

    /* special case for "Shadows of the Empire" videos */
    sote = ((w == 640) && (h == 272) && (codec == 47));
    if (sote)
        left = top = 0;

    if (!ctx->have_dimensions) {
        int xres, yres;
        if (ctx->subversion < 2) {
            /* Rebel Assault 1: 384x242 internal size */
            xres = 384;
            yres = 242;
            ctx->have_dimensions = 1;
        } else if (codec == 37 || codec == 47 || codec == 48) {
            /* these codecs work on full frames, trust their dimensions */
            xres = w;
            yres = h;
            ctx->have_dimensions = 1;
        } else {
            /* detect common sizes */
            xres = w + left;
            yres = h + top;
            if (sote) {
                /* SotE: has top=60 at all times to center video
                 * inside the 640x480 game window
                 */
                xres = w;
                yres = h;
                ctx->have_dimensions = 1;
            } else if (((xres == 424) && (yres == 260)) ||  /* RA1 */
                       ((xres == 320) && (yres == 200)) ||  /* ft/dig/... */
                       ((xres == 640) && (yres == 480))) {  /* ol/comi/mots... */
                ctx->have_dimensions = 1;
            }

            xres = FFMAX(xres, ctx->width);
            yres = FFMAX(yres, ctx->height);
        }

        if (ctx->width < xres || ctx->height < yres) {
            int ret = ff_set_dimensions(ctx->avctx, xres, yres);
            if (ret < 0)
                return ret;
            init_sizes(ctx, xres, yres);
            if (init_buffers(ctx)) {
                av_log(ctx->avctx, AV_LOG_ERROR, "Error resizing buffers.\n");
                return AVERROR(ENOMEM);
            }
        }
    } else {
        if (((left + w > ctx->width) || (top + h > ctx->height)) && fsc) {
            /* correct unexpected overly large frames: this happens
             * for instance with The Dig's sq1.san video: it has a few
             * (all black) 640x480 frames halfway in, while the rest is
             * 320x200.
             */
            av_log(ctx->avctx, AV_LOG_WARNING,
                   "resizing too large fobj: c%d  %d %d @ %d %d\n", codec, w, h, left, top);
            w = ctx->width;
            h = ctx->height;
        }
    }

    /* clear the main buffer on the first fob */
    if (ctx->first_fob) {
        ctx->first_fob = 0;
        if (!fsc)
            memset(ctx->fbuf, 0, ctx->frm0_size);
    }

    switch (codec) {
    case 1:
    case 3:
        return old_codec1(ctx, gb, top, left, w, h, codec == 3);
    case 2:
        return old_codec2(ctx, gb, top, left, w, h);
    case 4:
    case 5:
    case 33:
    case 34:
        return old_codec4(ctx, gb, top, left, w, h, param, parm2, codec);
    case 20:
        return old_codec20(ctx, w, h);
    case 21:
        return old_codec21(ctx, gb, top, left, w, h);
    case 23:
        return old_codec23(ctx, gb, top, left, w, h, param, parm2);
    case 31:
    case 32:
        return old_codec31(ctx, gb, top, left, w, h, param, (codec == 32));
    case 37:
        ret = old_codec37(ctx, w, h); break;
    case 45:
        return 0;
    case 47:
        ret = old_codec47(ctx, w, h); break;
    case 48:
        ret = old_codec48(ctx, w, h); break;
    default:
        avpriv_request_sample(ctx->avctx, "Subcodec %d", codec);
        ctx->frame->flags |= AV_FRAME_FLAG_CORRUPT;
        return 0;
    }
    if (ret)
        return ret;

    /* copy the codec37/47/48 result to main buffer */
    if ((w == ctx->width) && (h == ctx->height)) {
        memcpy(ctx->fbuf, ctx->frm0, ctx->fbuf_size);
    } else {
        uint8_t *dst = (uint8_t *)ctx->fbuf + left + top * ctx->pitch;
        const uint8_t *src = (uint8_t *)ctx->frm0;
        const int cw = FFMIN(w, ctx->width - left);
        const int ch = FFMIN(h, ctx->height - top);
        if ((cw > 0) && (ch > 0) && (left >= 0) && (top >= 0)) {
            for (int i = 0; i < ch; i++) {
                memcpy(dst, src, cw);
                dst += ctx->pitch;
                src += w;
            }
        }
    }
    return 0;
}

static int process_ftch(SANMVideoContext *ctx, int size)
{
    uint8_t *sf = ctx->stored_frame;
    int xoff, yoff, left, top, ret;
    GetByteContext gb;
    uint32_t sz;

    /* FTCH defines additional x/y offsets */
    if (size != 12) {
        if (bytestream2_get_bytes_left(&ctx->gb) < 6)
            return AVERROR_INVALIDDATA;
        bytestream2_skip(&ctx->gb, 2);
        xoff = bytestream2_get_le16u(&ctx->gb);
        yoff = bytestream2_get_le16u(&ctx->gb);
    } else {
        if (bytestream2_get_bytes_left(&ctx->gb) < 12)
            return AVERROR_INVALIDDATA;
        bytestream2_skip(&ctx->gb, 4);
        xoff = bytestream2_get_be32u(&ctx->gb);
        yoff = bytestream2_get_be32u(&ctx->gb);
    }

    sz = *(uint32_t *)(sf + 0);
    if ((sz > 0) && (sz <= ctx->stored_frame_size - 4)) {
        /* add the FTCH offsets to the left/top values of the stored FOBJ */
        left = av_le2ne16(*(int16_t *)(sf + 4 + 2));
        top  = av_le2ne16(*(int16_t *)(sf + 4 + 4));
        *(int16_t *)(sf + 4 + 2) = av_le2ne16(left + xoff);
        *(int16_t *)(sf + 4 + 4) = av_le2ne16(top  + yoff);

        /* decode the stored FOBJ */
        bytestream2_init(&gb, sf + 4, sz);
        ret = process_frame_obj(ctx, &gb);

        /* now restore the original left/top values again */
        *(int16_t *)(sf + 4 + 2) = av_le2ne16(left);
        *(int16_t *)(sf + 4 + 4) = av_le2ne16(top);
    } else {
        /* this happens a lot in RA1: The individual files are meant to
         * be played in sequence, with some referencing objects STORed
         * by previous files, e.g. the cockpit codec21 object in RA1 LVL8.
         * But spamming the log with errors is also not helpful, so
         * here we simply ignore this case.
         */
         ret = 0;
    }
    return ret;
}

static int process_xpal(SANMVideoContext *ctx, int size)
{
    int16_t *dp = ctx->delta_pal;
    uint32_t *pal = ctx->pal;
    uint16_t cmd;
    uint8_t c[3];
    int i, j;

    bytestream2_skip(&ctx->gb, 2);
    cmd = bytestream2_get_be16(&ctx->gb);

    if (cmd == 1) {
        for (i = 0; i < PALETTE_DELTA; i += 3) {
            c[0] = (*pal >> 16) & 0xFF;
            c[1] = (*pal >>  8) & 0xFF;
            c[2] = (*pal >>  0) & 0xFF;
            for (j = 0; j < 3; j++) {
                int cl = (c[j] * 129) + *dp++;
                c[j] = av_clip_uint8(cl / 128) & 0xFF;
            }
            *pal++ = 0xFFU << 24 | c[0] << 16 | c[1] << 8 | c[2];
        }
    } else if (cmd == 2) {
        if (size < PALETTE_DELTA * 2 + 4) {
            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Incorrect palette change block size %"PRIu32".\n", size);
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < PALETTE_DELTA; i++)
            dp[i] = bytestream2_get_le16u(&ctx->gb);

        if (size >= PALETTE_DELTA * 2 + 4 + PALETTE_SIZE * 3) {
            for (i = 0; i < PALETTE_SIZE; i++)
                ctx->pal[i] = 0xFFU << 24 | bytestream2_get_be24u(&ctx->gb);
            if (ctx->subversion < 2)
                ctx->pal[0] = 0xFFU << 24;
        }
    }
    return 0;
}

static int decode_0(SANMVideoContext *ctx)
{
    uint16_t *frm = ctx->frm0;
    int x, y;

    if (bytestream2_get_bytes_left(&ctx->gb) < ctx->width * ctx->height * 2) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Insufficient data for raw frame.\n");
        return AVERROR_INVALIDDATA;
    }
    for (y = 0; y < ctx->height; y++) {
        for (x = 0; x < ctx->width; x++)
            frm[x] = bytestream2_get_le16u(&ctx->gb);
        frm += ctx->pitch;
    }
    return 0;
}

static int decode_nop(SANMVideoContext *ctx)
{
    avpriv_request_sample(ctx->avctx, "Unknown/unsupported compression type");
    return AVERROR_PATCHWELCOME;
}

static void copy_block(uint16_t *pdest, uint16_t *psrc, int block_size, ptrdiff_t pitch)
{
    uint8_t *dst = (uint8_t *)pdest;
    uint8_t *src = (uint8_t *)psrc;
    ptrdiff_t stride = pitch * 2;

    switch (block_size) {
    case 2:
        copy_block4(dst, src, stride, stride, 2);
        break;
    case 4:
        copy_block8(dst, src, stride, stride, 4);
        break;
    case 8:
        copy_block16(dst, src, stride, stride, 8);
        break;
    }
}

static void fill_block(uint16_t *pdest, uint16_t color, int block_size, ptrdiff_t pitch)
{
    int x, y;

    pitch -= block_size;
    for (y = 0; y < block_size; y++, pdest += pitch)
        for (x = 0; x < block_size; x++)
            *pdest++ = color;
}

static int draw_glyph(SANMVideoContext *ctx, uint16_t *dst, int index,
                      uint16_t fg_color, uint16_t bg_color, int block_size,
                      ptrdiff_t pitch)
{
    int8_t *pglyph;
    uint16_t colors[2] = { fg_color, bg_color };
    int x, y;

    if (index >= NGLYPHS) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Ignoring nonexistent glyph #%u.\n", index);
        return AVERROR_INVALIDDATA;
    }

    pglyph = block_size == 8 ? ctx->p8x8glyphs[index] : ctx->p4x4glyphs[index];
    pitch -= block_size;

    for (y = 0; y < block_size; y++, dst += pitch)
        for (x = 0; x < block_size; x++)
            *dst++ = colors[*pglyph++];
    return 0;
}

static int opcode_0xf7(SANMVideoContext *ctx, int cx, int cy, int block_size, ptrdiff_t pitch)
{
    uint16_t *dst = ctx->frm0 + cx + cy * ctx->pitch;

    if (block_size == 2) {
        uint32_t indices;

        if (bytestream2_get_bytes_left(&ctx->gb) < 4)
            return AVERROR_INVALIDDATA;

        indices        = bytestream2_get_le32u(&ctx->gb);
        dst[0]         = ctx->codebook[indices & 0xFF];
        indices      >>= 8;
        dst[1]         = ctx->codebook[indices & 0xFF];
        indices      >>= 8;
        dst[pitch]     = ctx->codebook[indices & 0xFF];
        indices      >>= 8;
        dst[pitch + 1] = ctx->codebook[indices & 0xFF];
    } else {
        uint16_t fgcolor, bgcolor;
        int glyph;

        if (bytestream2_get_bytes_left(&ctx->gb) < 3)
            return AVERROR_INVALIDDATA;

        glyph   = bytestream2_get_byteu(&ctx->gb);
        bgcolor = ctx->codebook[bytestream2_get_byteu(&ctx->gb)];
        fgcolor = ctx->codebook[bytestream2_get_byteu(&ctx->gb)];

        draw_glyph(ctx, dst, glyph, fgcolor, bgcolor, block_size, pitch);
    }
    return 0;
}

static int opcode_0xf8(SANMVideoContext *ctx, int cx, int cy, int block_size, ptrdiff_t pitch)
{
    uint16_t *dst = ctx->frm0 + cx + cy * ctx->pitch;

    if (block_size == 2) {
        if (bytestream2_get_bytes_left(&ctx->gb) < 8)
            return AVERROR_INVALIDDATA;

        dst[0]         = bytestream2_get_le16u(&ctx->gb);
        dst[1]         = bytestream2_get_le16u(&ctx->gb);
        dst[pitch]     = bytestream2_get_le16u(&ctx->gb);
        dst[pitch + 1] = bytestream2_get_le16u(&ctx->gb);
    } else {
        uint16_t fgcolor, bgcolor;
        int glyph;

        if (bytestream2_get_bytes_left(&ctx->gb) < 5)
            return AVERROR_INVALIDDATA;

        glyph   = bytestream2_get_byteu(&ctx->gb);
        bgcolor = bytestream2_get_le16u(&ctx->gb);
        fgcolor = bytestream2_get_le16u(&ctx->gb);

        draw_glyph(ctx, dst, glyph, fgcolor, bgcolor, block_size, pitch);
    }
    return 0;
}

static int good_mvec(SANMVideoContext *ctx, int cx, int cy, int mx, int my,
                     int block_size)
{
    int start_pos = cx + mx + (cy + my) * ctx->pitch;
    int end_pos = start_pos + (block_size - 1) * (ctx->pitch + 1);

    int good = start_pos >= 0 && end_pos < (ctx->buf_size >> 1);

    if (!good)
        av_log(ctx->avctx, AV_LOG_ERROR,
               "Ignoring invalid motion vector (%i, %i)->(%u, %u), block size = %u\n",
               cx + mx, cy + my, cx, cy, block_size);

    return good;
}

static int codec2subblock(SANMVideoContext *ctx, int cx, int cy, int blk_size)
{
    int16_t mx, my, index;
    int opcode;

    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
        return AVERROR_INVALIDDATA;

    opcode = bytestream2_get_byteu(&ctx->gb);

    switch (opcode) {
    default:
        mx = motion_vectors[opcode][0];
        my = motion_vectors[opcode][1];

        if (good_mvec(ctx, cx, cy, mx, my, blk_size)) {
            copy_block(ctx->frm0 + cx      + ctx->pitch *  cy,
                       ctx->frm2 + cx + mx + ctx->pitch * (cy + my),
                       blk_size, ctx->pitch);
        }
        break;
    case 0xF5:
        if (bytestream2_get_bytes_left(&ctx->gb) < 2)
            return AVERROR_INVALIDDATA;
        index = bytestream2_get_le16u(&ctx->gb);

        mx = index % ctx->width;
        my = index / ctx->width;

        if (good_mvec(ctx, cx, cy, mx, my, blk_size)) {
            copy_block(ctx->frm0 + cx      + ctx->pitch *  cy,
                       ctx->frm2 + cx + mx + ctx->pitch * (cy + my),
                       blk_size, ctx->pitch);
        }
        break;
    case 0xF6:
        copy_block(ctx->frm0 + cx + ctx->pitch * cy,
                   ctx->frm1 + cx + ctx->pitch * cy,
                   blk_size, ctx->pitch);
        break;
    case 0xF7:
        opcode_0xf7(ctx, cx, cy, blk_size, ctx->pitch);
        break;

    case 0xF8:
        opcode_0xf8(ctx, cx, cy, blk_size, ctx->pitch);
        break;
    case 0xF9:
    case 0xFA:
    case 0xFB:
    case 0xFC:
        fill_block(ctx->frm0 + cx + cy * ctx->pitch,
                   ctx->small_codebook[opcode - 0xf9], blk_size, ctx->pitch);
        break;
    case 0xFD:
        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
            return AVERROR_INVALIDDATA;
        fill_block(ctx->frm0 + cx + cy * ctx->pitch,
                   ctx->codebook[bytestream2_get_byteu(&ctx->gb)], blk_size, ctx->pitch);
        break;
    case 0xFE:
        if (bytestream2_get_bytes_left(&ctx->gb) < 2)
            return AVERROR_INVALIDDATA;
        fill_block(ctx->frm0 + cx + cy * ctx->pitch,
                   bytestream2_get_le16u(&ctx->gb), blk_size, ctx->pitch);
        break;
    case 0xFF:
        if (blk_size == 2) {
            opcode_0xf8(ctx, cx, cy, blk_size, ctx->pitch);
        } else {
            blk_size >>= 1;
            if (codec2subblock(ctx, cx, cy, blk_size))
                return AVERROR_INVALIDDATA;
            if (codec2subblock(ctx, cx + blk_size, cy, blk_size))
                return AVERROR_INVALIDDATA;
            if (codec2subblock(ctx, cx, cy + blk_size, blk_size))
                return AVERROR_INVALIDDATA;
            if (codec2subblock(ctx, cx + blk_size, cy + blk_size, blk_size))
                return AVERROR_INVALIDDATA;
        }
        break;
    }
    return 0;
}

static int decode_2(SANMVideoContext *ctx)
{
    int cx, cy, ret;

    for (cy = 0; cy < ctx->aligned_height; cy += 8)
        for (cx = 0; cx < ctx->aligned_width; cx += 8)
            if (ret = codec2subblock(ctx, cx, cy, 8))
                return ret;

    return 0;
}

static int decode_3(SANMVideoContext *ctx)
{
    memcpy(ctx->frm0, ctx->frm2, ctx->frm2_size);
    return 0;
}

static int decode_4(SANMVideoContext *ctx)
{
    memcpy(ctx->frm0, ctx->frm1, ctx->frm1_size);
    return 0;
}

static int decode_5(SANMVideoContext *ctx)
{
#if HAVE_BIGENDIAN
    uint16_t *frm;
    int npixels;
#endif
    uint8_t *dst = (uint8_t*)ctx->frm0;

    if (rle_decode(ctx, &ctx->gb, dst, ctx->buf_size))
        return AVERROR_INVALIDDATA;

#if HAVE_BIGENDIAN
    npixels = ctx->npixels;
    frm = ctx->frm0;
    while (npixels--) {
        *frm = av_bswap16(*frm);
        frm++;
    }
#endif

    return 0;
}

static int decode_6(SANMVideoContext *ctx)
{
    int npixels = ctx->npixels;
    uint16_t *frm = ctx->frm0;

    if (bytestream2_get_bytes_left(&ctx->gb) < npixels) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Insufficient data for frame.\n");
        return AVERROR_INVALIDDATA;
    }
    while (npixels--)
        *frm++ = ctx->codebook[bytestream2_get_byteu(&ctx->gb)];

    return 0;
}

static int decode_8(SANMVideoContext *ctx)
{
    uint16_t *pdest = ctx->frm0;
    uint8_t *rsrc;
    long npixels = ctx->npixels;

    av_fast_malloc(&ctx->rle_buf, &ctx->rle_buf_size, npixels);
    if (!ctx->rle_buf) {
        av_log(ctx->avctx, AV_LOG_ERROR, "RLE buffer allocation failed.\n");
        return AVERROR(ENOMEM);
    }
    rsrc = ctx->rle_buf;

    if (rle_decode(ctx, &ctx->gb, rsrc, npixels))
        return AVERROR_INVALIDDATA;

    while (npixels--)
        *pdest++ = ctx->codebook[*rsrc++];

    return 0;
}

typedef int (*frm_decoder)(SANMVideoContext *ctx);

static const frm_decoder v1_decoders[] = {
    decode_0, decode_nop, decode_2, decode_3, decode_4, decode_5,
    decode_6, decode_nop, decode_8
};

static int read_frame_header(SANMVideoContext *ctx, SANMFrameHeader *hdr)
{
    int i, ret;

    if ((ret = bytestream2_get_bytes_left(&ctx->gb)) < 560) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Input frame too short (%d bytes).\n",
               ret);
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&ctx->gb, 8); // skip pad

    hdr->width  = bytestream2_get_le32u(&ctx->gb);
    hdr->height = bytestream2_get_le32u(&ctx->gb);

    if (hdr->width != ctx->width || hdr->height != ctx->height) {
        avpriv_report_missing_feature(ctx->avctx, "Variable size frames");
        return AVERROR_PATCHWELCOME;
    }

    hdr->seq_num     = bytestream2_get_le16u(&ctx->gb);
    hdr->codec       = bytestream2_get_byteu(&ctx->gb);
    hdr->rotate_code = bytestream2_get_byteu(&ctx->gb);

    bytestream2_skip(&ctx->gb, 4); // skip pad

    for (i = 0; i < 4; i++)
        ctx->small_codebook[i] = bytestream2_get_le16u(&ctx->gb);
    hdr->bg_color = bytestream2_get_le16u(&ctx->gb);

    bytestream2_skip(&ctx->gb, 2); // skip pad

    hdr->rle_output_size = bytestream2_get_le32u(&ctx->gb);
    for (i = 0; i < 256; i++)
        ctx->codebook[i] = bytestream2_get_le16u(&ctx->gb);

    bytestream2_skip(&ctx->gb, 8); // skip pad

    return 0;
}

static void fill_frame(uint16_t *pbuf, int buf_size, uint16_t color)
{
    if (buf_size--) {
        *pbuf++ = color;
        av_memcpy_backptr((uint8_t*)pbuf, 2, 2*buf_size);
    }
}

static int copy_output(SANMVideoContext *ctx, SANMFrameHeader *hdr)
{
    uint8_t *dst;
    const uint8_t *src = hdr ? (uint8_t *)ctx->frm0 : (uint8_t *)ctx->fbuf;
    int ret, height = ctx->height;
    ptrdiff_t dstpitch, srcpitch = ctx->pitch * (hdr ? sizeof(ctx->frm0[0]) : 1);

    if ((ret = ff_get_buffer(ctx->avctx, ctx->frame, 0)) < 0)
        return ret;

    dst      = ctx->frame->data[0];
    dstpitch = ctx->frame->linesize[0];

    while (height--) {
        memcpy(dst, src, srcpitch);
        src += srcpitch;
        dst += dstpitch;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame_ptr, AVPacket *pkt)
{
    SANMVideoContext *ctx = avctx->priv_data;
    int i, ret;

    ctx->frame = frame;
    bytestream2_init(&ctx->gb, pkt->data, pkt->size);

    if (!ctx->version) {
        int to_store = 0, have_img = 0;

        ctx->first_fob = 1;

        while (bytestream2_get_bytes_left(&ctx->gb) >= 8) {
            uint32_t sig, size;
            int pos;

            sig  = bytestream2_get_be32u(&ctx->gb);
            size = bytestream2_get_be32u(&ctx->gb);
            pos  = bytestream2_tell(&ctx->gb);

            if (bytestream2_get_bytes_left(&ctx->gb) < size) {
                av_log(avctx, AV_LOG_ERROR, "Incorrect chunk size %"PRIu32".\n", size);
                break;
            }
            switch (sig) {
            case MKBETAG('N', 'P', 'A', 'L'):
                if (size != PALETTE_SIZE * 3) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Incorrect palette block size %"PRIu32".\n", size);
                    return AVERROR_INVALIDDATA;
                }
                for (i = 0; i < PALETTE_SIZE; i++)
                    ctx->pal[i] = 0xFFU << 24 | bytestream2_get_be24u(&ctx->gb);
                if (ctx->subversion < 2)
                    ctx->pal[0] = 0xFFU << 24;
                break;
            case MKBETAG('F', 'O', 'B', 'J'):
                if (size < 16)
                    return AVERROR_INVALIDDATA;
                if (ret = process_frame_obj(ctx, &ctx->gb))
                    return ret;
                have_img = 1;

                /* STOR: for ANIMv0/1 store the whole FOBJ datablock, as it
                 * needs to be replayed on FTCH, since none of the codecs
                 * it uses work on the full buffer.
                 * For ANIMv2, it's enough to store the current framebuffer.
                 */
                if (to_store) {
                    to_store = 0;
                    if (ctx->subversion < 2) {
                        if (size + 4 <= ctx->stored_frame_size) {
                            int pos2 = bytestream2_tell(&ctx->gb);
                            bytestream2_seek(&ctx->gb, pos, SEEK_SET);
                            *(uint32_t *)(ctx->stored_frame) = size;
                            bytestream2_get_bufferu(&ctx->gb, ctx->stored_frame + 4, size);
                            bytestream2_seek(&ctx->gb, pos2, SEEK_SET);
                        } else {
                            av_log(avctx, AV_LOG_ERROR, "FOBJ too large for STOR\n");
                            ret = AVERROR(ENOMEM);
                        }
                    } else {
                        memcpy(ctx->stored_frame, ctx->fbuf, ctx->buf_size);
                    }
                }
                break;
            case MKBETAG('X', 'P', 'A', 'L'):
                if (ret = process_xpal(ctx, size))
                    return ret;
                break;
            case MKBETAG('S', 'T', 'O', 'R'):
                to_store = 1;
                break;
            case MKBETAG('F', 'T', 'C', 'H'):
                if (ctx->subversion < 2) {
                    if (ret = process_ftch(ctx, size))
                        return ret;
                } else {
                    memcpy(ctx->fbuf, ctx->stored_frame, ctx->buf_size);
                }
                have_img = 1;
                break;
            default:
                bytestream2_skip(&ctx->gb, size);
                av_log(avctx, AV_LOG_DEBUG,
                       "Unknown/unsupported chunk %"PRIx32".\n", sig);
                break;
            }

            /* the sizes of chunks are usually a multiple of 2. However
             * there are a few unaligned FOBJs in RA1 L2PLAY.ANM only (looks
             * like a game bug) and IACT audio chunks which have odd sizes
             * but are padded with a zero byte.
             */
            bytestream2_seek(&ctx->gb, pos + size, SEEK_SET);
            if ((pos + size) & 1) {
                if (0 != bytestream2_get_byteu(&ctx->gb))
                    bytestream2_seek(&ctx->gb, pos + size, SEEK_SET);
            }
        }

        if (have_img) {
            if ((ret = copy_output(ctx, NULL)))
                return ret;
            memcpy(ctx->frame->data[1], ctx->pal, 1024);
            *got_frame_ptr = 1;
        }
    } else {
        SANMFrameHeader header;

        if ((ret = read_frame_header(ctx, &header)))
            return ret;

        ctx->rotate_code = header.rotate_code;
        if (!header.seq_num) {
            ctx->frame->flags |= AV_FRAME_FLAG_KEY;
            ctx->frame->pict_type = AV_PICTURE_TYPE_I;
            fill_frame(ctx->frm1, ctx->npixels, header.bg_color);
            fill_frame(ctx->frm2, ctx->npixels, header.bg_color);
        } else {
            ctx->frame->flags &= ~AV_FRAME_FLAG_KEY;
            ctx->frame->pict_type = AV_PICTURE_TYPE_P;
        }

        if (header.codec < FF_ARRAY_ELEMS(v1_decoders)) {
            if ((ret = v1_decoders[header.codec](ctx))) {
                av_log(avctx, AV_LOG_ERROR,
                       "Subcodec %d: error decoding frame.\n", header.codec);
                return ret;
            }
        } else {
            avpriv_request_sample(avctx, "Subcodec %d", header.codec);
            return AVERROR_PATCHWELCOME;
        }

        if ((ret = copy_output(ctx, &header)))
            return ret;

        *got_frame_ptr = 1;

    }
    if (ctx->rotate_code)
        rotate_bufs(ctx, ctx->rotate_code);

    return pkt->size;
}

const FFCodec ff_sanm_decoder = {
    .p.name         = "sanm",
    CODEC_LONG_NAME("LucasArts SANM/Smush video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SANM,
    .priv_data_size = sizeof(SANMVideoContext),
    .init           = decode_init,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
