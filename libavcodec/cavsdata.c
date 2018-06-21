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

#include "cavs.h"

const uint8_t ff_cavs_partition_flags[30] = {
  0,                                 //I_8X8
  0,                                 //P_SKIP
  0,                                 //P_16X16
                      SPLITH,        //P_16X8
                             SPLITV, //P_8X16
                      SPLITH|SPLITV, //P_8X8
                      SPLITH|SPLITV, //B_SKIP
                      SPLITH|SPLITV, //B_DIRECT
  0,                                 //B_FWD_16X16
  0,                                 //B_BWD_16X16
  0,                                 //B_SYM_16X16
  FWD0|FWD1          |SPLITH,
  FWD0|FWD1                 |SPLITV,
  BWD0|BWD1          |SPLITH,
  BWD0|BWD1                 |SPLITV,
  FWD0|BWD1          |SPLITH,
  FWD0|BWD1                 |SPLITV,
  BWD0|FWD1          |SPLITH,
  BWD0|FWD1                 |SPLITV,
  FWD0|FWD1     |SYM1|SPLITH,
  FWD0|FWD1     |SYM1       |SPLITV,
  BWD0|FWD1     |SYM1|SPLITH,
  BWD0|FWD1     |SYM1       |SPLITV,
  FWD0|FWD1|SYM0     |SPLITH,
  FWD0|FWD1|SYM0            |SPLITV,
  FWD0|BWD1|SYM0     |SPLITH,
  FWD0|BWD1|SYM0            |SPLITV,
  FWD0|FWD1|SYM0|SYM1|SPLITH,
  FWD0|FWD1|SYM0|SYM1       |SPLITV,
                      SPLITH|SPLITV, //B_8X8 = 29
};

const uint8_t ff_cavs_chroma_qp[64] = {
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 42, 43, 43, 44, 44,
  45, 45, 46, 46, 47, 47, 48, 48, 48, 49, 49, 49, 50, 50, 50, 51
};

/** mark block as "no prediction from this direction"
    e.g. forward motion vector in BWD partition */
const cavs_vector ff_cavs_dir_mv   = {0,0,1,REF_DIR};

/** mark block as using intra prediction */
const cavs_vector ff_cavs_intra_mv = {0,0,1,REF_INTRA};
