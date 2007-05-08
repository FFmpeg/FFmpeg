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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef CAVS_H
#define CAVS_H

#include "dsputil.h"

#define SLICE_MIN_START_CODE    0x00000101
#define SLICE_MAX_START_CODE    0x000001af
#define EXT_START_CODE          0x000001b5
#define USER_START_CODE         0x000001b2
#define CAVS_START_CODE         0x000001b0
#define PIC_I_START_CODE        0x000001b3
#define PIC_PB_START_CODE       0x000001b6

#define A_AVAIL                          1
#define B_AVAIL                          2
#define C_AVAIL                          4
#define D_AVAIL                          8
#define NOT_AVAIL                       -1
#define REF_INTRA                       -2
#define REF_DIR                         -3

#define ESCAPE_CODE                     59

#define FWD0                          0x01
#define FWD1                          0x02
#define BWD0                          0x04
#define BWD1                          0x08
#define SYM0                          0x10
#define SYM1                          0x20
#define SPLITH                        0x40
#define SPLITV                        0x80

#define MV_BWD_OFFS                     12
#define MV_STRIDE                        4

enum mb_t {
  I_8X8 = 0,
  P_SKIP,
  P_16X16,
  P_16X8,
  P_8X16,
  P_8X8,
  B_SKIP,
  B_DIRECT,
  B_FWD_16X16,
  B_BWD_16X16,
  B_SYM_16X16,
  B_8X8 = 29
};

enum sub_mb_t {
  B_SUB_DIRECT,
  B_SUB_FWD,
  B_SUB_BWD,
  B_SUB_SYM
};

enum intra_luma_t {
  INTRA_L_VERT,
  INTRA_L_HORIZ,
  INTRA_L_LP,
  INTRA_L_DOWN_LEFT,
  INTRA_L_DOWN_RIGHT,
  INTRA_L_LP_LEFT,
  INTRA_L_LP_TOP,
  INTRA_L_DC_128
};

enum intra_chroma_t {
  INTRA_C_LP,
  INTRA_C_HORIZ,
  INTRA_C_VERT,
  INTRA_C_PLANE,
  INTRA_C_LP_LEFT,
  INTRA_C_LP_TOP,
  INTRA_C_DC_128,
};

enum mv_pred_t {
  MV_PRED_MEDIAN,
  MV_PRED_LEFT,
  MV_PRED_TOP,
  MV_PRED_TOPRIGHT,
  MV_PRED_PSKIP,
  MV_PRED_BSKIP
};

enum block_t {
  BLK_16X16,
  BLK_16X8,
  BLK_8X16,
  BLK_8X8
};

enum mv_loc_t {
  MV_FWD_D3 = 0,
  MV_FWD_B2,
  MV_FWD_B3,
  MV_FWD_C2,
  MV_FWD_A1,
  MV_FWD_X0,
  MV_FWD_X1,
  MV_FWD_A3 = 8,
  MV_FWD_X2,
  MV_FWD_X3,
  MV_BWD_D3 = MV_BWD_OFFS,
  MV_BWD_B2,
  MV_BWD_B3,
  MV_BWD_C2,
  MV_BWD_A1,
  MV_BWD_X0,
  MV_BWD_X1,
  MV_BWD_A3 = MV_BWD_OFFS+8,
  MV_BWD_X2,
  MV_BWD_X3
};

DECLARE_ALIGNED_8(typedef, struct) {
    int16_t x;
    int16_t y;
    int16_t dist;
    int16_t ref;
} vector_t;

typedef struct residual_vlc_t {
  int8_t rltab[59][3];
  int8_t level_add[27];
  int8_t golomb_order;
  int inc_limit;
  int8_t max_run;
} residual_vlc_t;

#endif /* CAVS_H */
