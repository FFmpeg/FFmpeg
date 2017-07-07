/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * @brief
 *     H.264 / AVC / MPEG-4 part10 codec data table
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include <stdint.h>

#include "libavutil/avutil.h"

#include "avcodec.h"
#include "h264dec.h"
#include "h264data.h"

const uint8_t ff_h264_golomb_to_pict_type[5] = {
    AV_PICTURE_TYPE_P, AV_PICTURE_TYPE_B, AV_PICTURE_TYPE_I,
    AV_PICTURE_TYPE_SP, AV_PICTURE_TYPE_SI
};

const uint8_t ff_h264_golomb_to_intra4x4_cbp[48] = {
    47, 31, 15, 0,  23, 27, 29, 30, 7,  11, 13, 14, 39, 43, 45, 46,
    16, 3,  5,  10, 12, 19, 21, 26, 28, 35, 37, 42, 44, 1,  2,  4,
    8,  17, 18, 20, 24, 6,  9,  22, 25, 32, 33, 34, 36, 40, 38, 41
};

const uint8_t ff_h264_golomb_to_inter_cbp[48] = {
    0,  16, 1,  2,  4,  8,  32, 3,  5,  10, 12, 15, 47, 7,  11, 13,
    14, 6,  9,  31, 35, 37, 42, 44, 33, 34, 36, 40, 39, 43, 45, 46,
    17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41
};

const uint8_t ff_h264_chroma_dc_scan[4] = {
    (0 + 0 * 2) * 16, (1 + 0 * 2) * 16,
    (0 + 1 * 2) * 16, (1 + 1 * 2) * 16,
};

const uint8_t ff_h264_chroma422_dc_scan[8] = {
    (0 + 0 * 2) * 16, (0 + 1 * 2) * 16,
    (1 + 0 * 2) * 16, (0 + 2 * 2) * 16,
    (0 + 3 * 2) * 16, (1 + 1 * 2) * 16,
    (1 + 2 * 2) * 16, (1 + 3 * 2) * 16,
};

const IMbInfo ff_h264_i_mb_type_info[26] = {
    { MB_TYPE_INTRA4x4,  -1,  -1 },
    { MB_TYPE_INTRA16x16, 2,   0 },
    { MB_TYPE_INTRA16x16, 1,   0 },
    { MB_TYPE_INTRA16x16, 0,   0 },
    { MB_TYPE_INTRA16x16, 3,   0 },
    { MB_TYPE_INTRA16x16, 2,  16 },
    { MB_TYPE_INTRA16x16, 1,  16 },
    { MB_TYPE_INTRA16x16, 0,  16 },
    { MB_TYPE_INTRA16x16, 3,  16 },
    { MB_TYPE_INTRA16x16, 2,  32 },
    { MB_TYPE_INTRA16x16, 1,  32 },
    { MB_TYPE_INTRA16x16, 0,  32 },
    { MB_TYPE_INTRA16x16, 3,  32 },
    { MB_TYPE_INTRA16x16, 2,  15 +  0 },
    { MB_TYPE_INTRA16x16, 1,  15 +  0 },
    { MB_TYPE_INTRA16x16, 0,  15 +  0 },
    { MB_TYPE_INTRA16x16, 3,  15 +  0 },
    { MB_TYPE_INTRA16x16, 2,  15 + 16 },
    { MB_TYPE_INTRA16x16, 1,  15 + 16 },
    { MB_TYPE_INTRA16x16, 0,  15 + 16 },
    { MB_TYPE_INTRA16x16, 3,  15 + 16 },
    { MB_TYPE_INTRA16x16, 2,  15 + 32 },
    { MB_TYPE_INTRA16x16, 1,  15 + 32 },
    { MB_TYPE_INTRA16x16, 0,  15 + 32 },
    { MB_TYPE_INTRA16x16, 3,  15 + 32 },
    { MB_TYPE_INTRA_PCM,  -1, -1 },
};

const PMbInfo ff_h264_p_mb_type_info[5] = {
    { MB_TYPE_16x16 | MB_TYPE_P0L0,                               1 },
    { MB_TYPE_16x8  | MB_TYPE_P0L0 | MB_TYPE_P1L0,                2 },
    { MB_TYPE_8x16  | MB_TYPE_P0L0 | MB_TYPE_P1L0,                2 },
    { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0,                4 },
    { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_REF0, 4 },
};

const PMbInfo ff_h264_p_sub_mb_type_info[4] = {
    { MB_TYPE_16x16 | MB_TYPE_P0L0, 1 },
    { MB_TYPE_16x8  | MB_TYPE_P0L0, 2 },
    { MB_TYPE_8x16  | MB_TYPE_P0L0, 2 },
    { MB_TYPE_8x8   | MB_TYPE_P0L0, 4 },
};

const PMbInfo ff_h264_b_mb_type_info[23] = {
    { MB_TYPE_DIRECT2 | MB_TYPE_L0L1,                                              1, },
    { MB_TYPE_16x16   | MB_TYPE_P0L0,                                              1, },
    { MB_TYPE_16x16   | MB_TYPE_P0L1,                                              1, },
    { MB_TYPE_16x16   | MB_TYPE_P0L0 | MB_TYPE_P0L1,                               1, },
    { MB_TYPE_16x8    | MB_TYPE_P0L0 | MB_TYPE_P1L0,                               2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L0 | MB_TYPE_P1L0,                               2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L1 | MB_TYPE_P1L1,                               2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L1 | MB_TYPE_P1L1,                               2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L0 | MB_TYPE_P1L1,                               2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L0 | MB_TYPE_P1L1,                               2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L1 | MB_TYPE_P1L0,                               2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L1 | MB_TYPE_P1L0,                               2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_P1L1,                2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_P1L1,                2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1,                2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1,                2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0,                2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0,                2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L1,                2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L1,                2, },
    { MB_TYPE_16x8    | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2, },
    { MB_TYPE_8x16    | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2, },
    { MB_TYPE_8x8     | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 4, },
};

const PMbInfo ff_h264_b_sub_mb_type_info[13] = {
    { MB_TYPE_DIRECT2,                                                           1, },
    { MB_TYPE_16x16 | MB_TYPE_P0L0,                                              1, },
    { MB_TYPE_16x16 | MB_TYPE_P0L1,                                              1, },
    { MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1,                               1, },
    { MB_TYPE_16x8  | MB_TYPE_P0L0 | MB_TYPE_P1L0,                               2, },
    { MB_TYPE_8x16  | MB_TYPE_P0L0 | MB_TYPE_P1L0,                               2, },
    { MB_TYPE_16x8  | MB_TYPE_P0L1 | MB_TYPE_P1L1,                               2, },
    { MB_TYPE_8x16  | MB_TYPE_P0L1 | MB_TYPE_P1L1,                               2, },
    { MB_TYPE_16x8  | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2, },
    { MB_TYPE_8x16  | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 2, },
    { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0,                               4, },
    { MB_TYPE_8x8   | MB_TYPE_P0L1 | MB_TYPE_P1L1,                               4, },
    { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P0L1 | MB_TYPE_P1L0 | MB_TYPE_P1L1, 4, },
};

const uint8_t ff_h264_dequant4_coeff_init[6][3] = {
    { 10, 13, 16 },
    { 11, 14, 18 },
    { 13, 16, 20 },
    { 14, 18, 23 },
    { 16, 20, 25 },
    { 18, 23, 29 },
};

const uint8_t ff_h264_dequant8_coeff_init_scan[16] = {
    0, 3, 4, 3, 3, 1, 5, 1, 4, 5, 2, 5, 3, 1, 5, 1
};

const uint8_t ff_h264_dequant8_coeff_init[6][6] = {
    { 20, 18, 32, 19, 25, 24 },
    { 22, 19, 35, 21, 28, 26 },
    { 26, 23, 42, 24, 33, 31 },
    { 28, 25, 45, 26, 35, 33 },
    { 32, 28, 51, 30, 40, 38 },
    { 36, 32, 58, 34, 46, 43 },
};

const uint8_t ff_h264_quant_rem6[QP_MAX_NUM + 1] = {
    0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
    3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
    3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
    0, 1, 2, 3,
};

const uint8_t ff_h264_quant_div6[QP_MAX_NUM + 1] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,  3,  3,
    3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,  6,  6,
    7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10,
   10,10,10,11,11,11,11,11,11,12,12,12,12,12,12,13,13,13, 13, 13, 13,
   14,14,14,14,
};

#define QP(qP, depth) ((qP) + 6 * ((depth) - 8))

#define CHROMA_QP_TABLE_END(d)                                          \
    QP(0,  d), QP(1,  d), QP(2,  d), QP(3,  d), QP(4,  d), QP(5,  d),   \
    QP(6,  d), QP(7,  d), QP(8,  d), QP(9,  d), QP(10, d), QP(11, d),   \
    QP(12, d), QP(13, d), QP(14, d), QP(15, d), QP(16, d), QP(17, d),   \
    QP(18, d), QP(19, d), QP(20, d), QP(21, d), QP(22, d), QP(23, d),   \
    QP(24, d), QP(25, d), QP(26, d), QP(27, d), QP(28, d), QP(29, d),   \
    QP(29, d), QP(30, d), QP(31, d), QP(32, d), QP(32, d), QP(33, d),   \
    QP(34, d), QP(34, d), QP(35, d), QP(35, d), QP(36, d), QP(36, d),   \
    QP(37, d), QP(37, d), QP(37, d), QP(38, d), QP(38, d), QP(38, d),   \
    QP(39, d), QP(39, d), QP(39, d), QP(39, d)

const uint8_t ff_h264_chroma_qp[7][QP_MAX_NUM + 1] = {
    { CHROMA_QP_TABLE_END(8) },
    { 0, 1, 2, 3, 4, 5,
      CHROMA_QP_TABLE_END(9) },
    { 0, 1, 2, 3,  4,  5,
      6, 7, 8, 9, 10, 11,
      CHROMA_QP_TABLE_END(10) },
    { 0,  1, 2, 3,  4,  5,
      6,  7, 8, 9, 10, 11,
      12,13,14,15, 16, 17,
      CHROMA_QP_TABLE_END(11) },
    { 0,  1, 2, 3,  4,  5,
      6,  7, 8, 9, 10, 11,
      12,13,14,15, 16, 17,
      18,19,20,21, 22, 23,
      CHROMA_QP_TABLE_END(12) },
    { 0,  1, 2, 3,  4,  5,
      6,  7, 8, 9, 10, 11,
      12,13,14,15, 16, 17,
      18,19,20,21, 22, 23,
      24,25,26,27, 28, 29,
      CHROMA_QP_TABLE_END(13) },
    { 0,  1, 2, 3,  4,  5,
      6,  7, 8, 9, 10, 11,
      12,13,14,15, 16, 17,
      18,19,20,21, 22, 23,
      24,25,26,27, 28, 29,
      30,31,32,33, 34, 35,
      CHROMA_QP_TABLE_END(14) },
};
