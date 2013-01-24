/*
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
 * VP5 and VP6 compatible video decoder (common data)
 */

#ifndef AVCODEC_VP56DATA_H
#define AVCODEC_VP56DATA_H

#include "libavutil/common.h"

typedef enum {
    VP56_FRAME_NONE     =-1,
    VP56_FRAME_CURRENT  = 0,
    VP56_FRAME_PREVIOUS = 1,
    VP56_FRAME_GOLDEN   = 2,
    VP56_FRAME_GOLDEN2  = 3,
    VP56_FRAME_UNUSED   = 4,
    VP56_FRAME_UNUSED2  = 5,
} VP56Frame;

typedef enum {
    VP56_MB_INTER_NOVEC_PF = 0,  /**< Inter MB, no vector, from previous frame */
    VP56_MB_INTRA          = 1,  /**< Intra MB */
    VP56_MB_INTER_DELTA_PF = 2,  /**< Inter MB, above/left vector + delta, from previous frame */
    VP56_MB_INTER_V1_PF    = 3,  /**< Inter MB, first vector, from previous frame */
    VP56_MB_INTER_V2_PF    = 4,  /**< Inter MB, second vector, from previous frame */
    VP56_MB_INTER_NOVEC_GF = 5,  /**< Inter MB, no vector, from golden frame */
    VP56_MB_INTER_DELTA_GF = 6,  /**< Inter MB, above/left vector + delta, from golden frame */
    VP56_MB_INTER_4V       = 7,  /**< Inter MB, 4 vectors, from previous frame */
    VP56_MB_INTER_V1_GF    = 8,  /**< Inter MB, first vector, from golden frame */
    VP56_MB_INTER_V2_GF    = 9,  /**< Inter MB, second vector, from golden frame */
} VP56mb;

typedef struct VP56Tree {
  int8_t val;
  int8_t prob_idx;
} VP56Tree;

extern const uint8_t ff_vp56_b2p[];
extern const uint8_t ff_vp56_b6to4[];
extern const uint8_t ff_vp56_coeff_parse_table[6][11];
extern const uint8_t ff_vp56_def_mb_types_stats[3][10][2];
extern const VP56Tree ff_vp56_pva_tree[];
extern const VP56Tree ff_vp56_pc_tree[];
extern const uint8_t ff_vp56_coeff_bias[];
extern const uint8_t ff_vp56_coeff_bit_length[];

static const VP56Frame vp56_reference_frame[] = {
    VP56_FRAME_PREVIOUS,  /* VP56_MB_INTER_NOVEC_PF */
    VP56_FRAME_CURRENT,   /* VP56_MB_INTRA */
    VP56_FRAME_PREVIOUS,  /* VP56_MB_INTER_DELTA_PF */
    VP56_FRAME_PREVIOUS,  /* VP56_MB_INTER_V1_PF */
    VP56_FRAME_PREVIOUS,  /* VP56_MB_INTER_V2_PF */
    VP56_FRAME_GOLDEN,    /* VP56_MB_INTER_NOVEC_GF */
    VP56_FRAME_GOLDEN,    /* VP56_MB_INTER_DELTA_GF */
    VP56_FRAME_PREVIOUS,  /* VP56_MB_INTER_4V */
    VP56_FRAME_GOLDEN,    /* VP56_MB_INTER_V1_GF */
    VP56_FRAME_GOLDEN,    /* VP56_MB_INTER_V2_GF */
};

static const uint8_t vp56_ac_dequant[64] = {
    94, 92, 90, 88, 86, 82, 78, 74,
    70, 66, 62, 58, 54, 53, 52, 51,
    50, 49, 48, 47, 46, 45, 44, 43,
    42, 40, 39, 37, 36, 35, 34, 33,
    32, 31, 30, 29, 28, 27, 26, 25,
    24, 23, 22, 21, 20, 19, 18, 17,
    16, 15, 14, 13, 12, 11, 10,  9,
     8,  7,  6,  5,  4,  3,  2,  1,
};

static const uint8_t vp56_dc_dequant[64] = {
    47, 47, 47, 47, 45, 43, 43, 43,
    43, 43, 42, 41, 41, 40, 40, 40,
    40, 35, 35, 35, 35, 33, 33, 33,
    33, 32, 32, 32, 27, 27, 26, 26,
    25, 25, 24, 24, 23, 23, 19, 19,
    19, 19, 18, 18, 17, 16, 16, 16,
    16, 16, 15, 11, 11, 11, 10, 10,
     9,  8,  7,  5,  3,  3,  2,  2,
};

static const uint8_t vp56_pre_def_mb_type_stats[16][3][10][2] = {
  { { {   9, 15 }, {  32, 25 }, {  7,  19 }, {   9, 21 }, {  1, 12 },
      {  14, 12 }, {   3, 18 }, { 14,  23 }, {   3, 10 }, {  0,  4 }, },
    { {  41, 22 }, {   1,  0 }, {  1,  31 }, {   0,  0 }, {  0,  0 },
      {   0,  1 }, {   1,  7 }, {  0,   1 }, {  98, 25 }, {  4, 10 }, },
    { {   2,  3 }, {   2,  3 }, {  0,   2 }, {   0,  2 }, {  0,  0 },
      {  11,  4 }, {   1,  4 }, {  0,   2 }, {   3,  2 }, {  0,  4 }, }, },
  { { {  48, 39 }, {   1,  2 }, { 11,  27 }, {  29, 44 }, {  7, 27 },
      {   1,  4 }, {   0,  3 }, {  1,   6 }, {   1,  2 }, {  0,  0 }, },
    { { 123, 37 }, {   6,  4 }, {  1,  27 }, {   0,  0 }, {  0,  0 },
      {   5,  8 }, {   1,  7 }, {  0,   1 }, {  12, 10 }, {  0,  2 }, },
    { {  49, 46 }, {   3,  4 }, {  7,  31 }, {  42, 41 }, {  0,  0 },
      {   2,  6 }, {   1,  7 }, {  1,   4 }, {   2,  4 }, {  0,  1 }, }, },
  { { {  21, 32 }, {   1,  2 }, {  4,  10 }, {  32, 43 }, {  6, 23 },
      {   2,  3 }, {   1, 19 }, {  1,   6 }, {  12, 21 }, {  0,  7 }, },
    { {  26, 14 }, {  14, 12 }, {  0,  24 }, {   0,  0 }, {  0,  0 },
      {  55, 17 }, {   1,  9 }, {  0,  36 }, {   5,  7 }, {  1,  3 }, },
    { {  26, 25 }, {   1,  1 }, {  2,  10 }, {  67, 39 }, {  0,  0 },
      {   1,  1 }, {   0, 14 }, {  0,   2 }, {  31, 26 }, {  1,  6 }, }, },
  { { {  69, 83 }, {   0,  0 }, {  0,   2 }, {  10, 29 }, {  3, 12 },
      {   0,  1 }, {   0,  3 }, {  0,   3 }, {   2,  2 }, {  0,  0 }, },
    { { 209,  5 }, {   0,  0 }, {  0,  27 }, {   0,  0 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   1 }, {   0,  0 }, {  0,  0 }, },
    { { 103, 46 }, {   1,  2 }, {  2,  10 }, {  33, 42 }, {  0,  0 },
      {   1,  4 }, {   0,  3 }, {  0,   1 }, {   1,  3 }, {  0,  0 }, }, },
  { { {  11, 20 }, {   1,  4 }, { 18,  36 }, {  43, 48 }, { 13, 35 },
      {   0,  2 }, {   0,  5 }, {  3,  12 }, {   1,  2 }, {  0,  0 }, },
    { {   2,  5 }, {   4,  5 }, {  0, 121 }, {   0,  0 }, {  0,  0 },
      {   0,  3 }, {   2,  4 }, {  1,   4 }, {   2,  2 }, {  0,  1 }, },
    { {  14, 31 }, {   9, 13 }, { 14,  54 }, {  22, 29 }, {  0,  0 },
      {   2,  6 }, {   4, 18 }, {  6,  13 }, {   1,  5 }, {  0,  1 }, }, },
  { { {  70, 44 }, {   0,  1 }, {  2,  10 }, {  37, 46 }, {  8, 26 },
      {   0,  2 }, {   0,  2 }, {  0,   2 }, {   0,  1 }, {  0,  0 }, },
    { { 175,  5 }, {   0,  1 }, {  0,  48 }, {   0,  0 }, {  0,  0 },
      {   0,  2 }, {   0,  1 }, {  0,   2 }, {   0,  1 }, {  0,  0 }, },
    { {  85, 39 }, {   0,  0 }, {  1,   9 }, {  69, 40 }, {  0,  0 },
      {   0,  1 }, {   0,  3 }, {  0,   1 }, {   2,  3 }, {  0,  0 }, }, },
  { { {   8, 15 }, {   0,  1 }, {  8,  21 }, {  74, 53 }, { 22, 42 },
      {   0,  1 }, {   0,  2 }, {  0,   3 }, {   1,  2 }, {  0,  0 }, },
    { {  83,  5 }, {   2,  3 }, {  0, 102 }, {   0,  0 }, {  0,  0 },
      {   1,  3 }, {   0,  2 }, {  0,   1 }, {   0,  0 }, {  0,  0 }, },
    { {  31, 28 }, {   0,  0 }, {  3,  14 }, { 130, 34 }, {  0,  0 },
      {   0,  1 }, {   0,  3 }, {  0,   1 }, {   3,  3 }, {  0,  1 }, }, },
  { { { 141, 42 }, {   0,  0 }, {  1,   4 }, {  11, 24 }, {  1, 11 },
      {   0,  1 }, {   0,  1 }, {  0,   2 }, {   0,  0 }, {  0,  0 }, },
    { { 233,  6 }, {   0,  0 }, {  0,   8 }, {   0,  0 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   0 }, {   0,  1 }, {  0,  0 }, },
    { { 171, 25 }, {   0,  0 }, {  1,   5 }, {  25, 21 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   0 }, {   0,  0 }, {  0,  0 }, }, },
  { { {   8, 19 }, {   4, 10 }, { 24,  45 }, {  21, 37 }, {  9, 29 },
      {   0,  3 }, {   1,  7 }, { 11,  25 }, {   0,  2 }, {  0,  1 }, },
    { {  34, 16 }, { 112, 21 }, {  1,  28 }, {   0,  0 }, {  0,  0 },
      {   6,  8 }, {   1,  7 }, {  0,   3 }, {   2,  5 }, {  0,  2 }, },
    { {  17, 21 }, {  68, 29 }, {  6,  15 }, {  13, 22 }, {  0,  0 },
      {   6, 12 }, {   3, 14 }, {  4,  10 }, {   1,  7 }, {  0,  3 }, }, },
  { { {  46, 42 }, {   0,  1 }, {  2,  10 }, {  54, 51 }, { 10, 30 },
      {   0,  2 }, {   0,  2 }, {  0,   1 }, {   0,  1 }, {  0,  0 }, },
    { { 159, 35 }, {   2,  2 }, {  0,  25 }, {   0,  0 }, {  0,  0 },
      {   3,  6 }, {   0,  5 }, {  0,   1 }, {   4,  4 }, {  0,  1 }, },
    { {  51, 39 }, {   0,  1 }, {  2,  12 }, {  91, 44 }, {  0,  0 },
      {   0,  2 }, {   0,  3 }, {  0,   1 }, {   2,  3 }, {  0,  1 }, }, },
  { { {  28, 32 }, {   0,  0 }, {  3,  10 }, {  75, 51 }, { 14, 33 },
      {   0,  1 }, {   0,  2 }, {  0,   1 }, {   1,  2 }, {  0,  0 }, },
    { {  75, 39 }, {   5,  7 }, {  2,  48 }, {   0,  0 }, {  0,  0 },
      {   3, 11 }, {   2, 16 }, {  1,   4 }, {   7, 10 }, {  0,  2 }, },
    { {  81, 25 }, {   0,  0 }, {  2,   9 }, { 106, 26 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   1 }, {   1,  1 }, {  0,  0 }, }, },
  { { { 100, 46 }, {   0,  1 }, {  3,   9 }, {  21, 37 }, {  5, 20 },
      {   0,  1 }, {   0,  2 }, {  1,   2 }, {   0,  1 }, {  0,  0 }, },
    { { 212, 21 }, {   0,  1 }, {  0,   9 }, {   0,  0 }, {  0,  0 },
      {   1,  2 }, {   0,  2 }, {  0,   0 }, {   2,  2 }, {  0,  0 }, },
    { { 140, 37 }, {   0,  1 }, {  1,   8 }, {  24, 33 }, {  0,  0 },
      {   1,  2 }, {   0,  2 }, {  0,   1 }, {   1,  2 }, {  0,  0 }, }, },
  { { {  27, 29 }, {   0,  1 }, {  9,  25 }, {  53, 51 }, { 12, 34 },
      {   0,  1 }, {   0,  3 }, {  1,   5 }, {   0,  2 }, {  0,  0 }, },
    { {   4,  2 }, {   0,  0 }, {  0, 172 }, {   0,  0 }, {  0,  0 },
      {   0,  1 }, {   0,  2 }, {  0,   0 }, {   2,  0 }, {  0,  0 }, },
    { {  14, 23 }, {   1,  3 }, { 11,  53 }, {  90, 31 }, {  0,  0 },
      {   0,  3 }, {   1,  5 }, {  2,   6 }, {   1,  2 }, {  0,  0 }, }, },
  { { {  80, 38 }, {   0,  0 }, {  1,   4 }, {  69, 33 }, {  5, 16 },
      {   0,  1 }, {   0,  1 }, {  0,   0 }, {   0,  1 }, {  0,  0 }, },
    { { 187, 22 }, {   1,  1 }, {  0,  17 }, {   0,  0 }, {  0,  0 },
      {   3,  6 }, {   0,  4 }, {  0,   1 }, {   4,  4 }, {  0,  1 }, },
    { { 123, 29 }, {   0,  0 }, {  1,   7 }, {  57, 30 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   1 }, {   0,  1 }, {  0,  0 }, }, },
  { { {  16, 20 }, {   0,  0 }, {  2,   8 }, { 104, 49 }, { 15, 33 },
      {   0,  1 }, {   0,  1 }, {  0,   1 }, {   1,  1 }, {  0,  0 }, },
    { { 133,  6 }, {   1,  2 }, {  1,  70 }, {   0,  0 }, {  0,  0 },
      {   0,  2 }, {   0,  4 }, {  0,   3 }, {   1,  1 }, {  0,  0 }, },
    { {  13, 14 }, {   0,  0 }, {  4,  20 }, { 175, 20 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   1 }, {   1,  1 }, {  0,  0 }, }, },
  { { { 194, 16 }, {   0,  0 }, {  1,   1 }, {   1,  9 }, {  1,  3 },
      {   0,  0 }, {   0,  1 }, {  0,   1 }, {   0,  0 }, {  0,  0 }, },
    { { 251,  1 }, {   0,  0 }, {  0,   2 }, {   0,  0 }, {  0,  0 },
      {   0,  0 }, {   0,  0 }, {  0,   0 }, {   0,  0 }, {  0,  0 }, },
    { { 202, 23 }, {   0,  0 }, {  1,   3 }, {   2,  9 }, {  0,  0 },
      {   0,  1 }, {   0,  1 }, {  0,   1 }, {   0,  0 }, {  0,  0 }, }, },
};

static const uint8_t vp56_filter_threshold[] = {
    14, 14, 13, 13, 12, 12, 10, 10,
    10, 10,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  7,  7,  7,  7,
     7,  7,  6,  6,  6,  6,  6,  6,
     5,  5,  5,  5,  4,  4,  4,  4,
     4,  4,  4,  3,  3,  3,  3,  2,
};

static const uint8_t vp56_mb_type_model_model[] = {
    171, 83, 199, 140, 125, 104,
};

static const VP56Tree vp56_pmbtm_tree[] = {
    { 4, 0},
    { 2, 1}, {-8}, {-4},
    { 8, 2},
    { 6, 3},
    { 4, 4},
    { 2, 5}, {-24}, {-20}, {-16}, {-12}, {-0},
};

static const VP56Tree vp56_pmbt_tree[] = {
    { 8, 1},
    { 4, 2},
    { 2, 4}, {-VP56_MB_INTER_NOVEC_PF}, {-VP56_MB_INTER_DELTA_PF},
    { 2, 5}, {-VP56_MB_INTER_V1_PF},    {-VP56_MB_INTER_V2_PF},
    { 4, 3},
    { 2, 6}, {-VP56_MB_INTRA},          {-VP56_MB_INTER_4V},
    { 4, 7},
    { 2, 8}, {-VP56_MB_INTER_NOVEC_GF}, {-VP56_MB_INTER_DELTA_GF},
    { 2, 9}, {-VP56_MB_INTER_V1_GF},    {-VP56_MB_INTER_V2_GF},
};

/* relative pos of surrounding blocks, from closest to farthest */
static const int8_t vp56_candidate_predictor_pos[12][2] = {
    {  0, -1 },
    { -1,  0 },
    { -1, -1 },
    {  1, -1 },
    {  0, -2 },
    { -2,  0 },
    { -2, -1 },
    { -1, -2 },
    {  1, -2 },
    {  2, -1 },
    { -2, -2 },
    {  2, -2 },
};

#endif /* AVCODEC_VP56DATA_H */
