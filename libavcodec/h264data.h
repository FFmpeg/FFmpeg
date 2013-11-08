/*
 * H26L/H264/AVC/JVT/14496-10/... encoder/decoder
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
 *     H264 / AVC / MPEG4 part10 codec data table
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_H264DATA_H
#define AVCODEC_H264DATA_H

#include <stdint.h>

#include "libavutil/rational.h"
#include "mpegvideo.h"
#include "h264.h"

static const uint8_t golomb_to_pict_type[5] = {
    AV_PICTURE_TYPE_P, AV_PICTURE_TYPE_B, AV_PICTURE_TYPE_I,
    AV_PICTURE_TYPE_SP, AV_PICTURE_TYPE_SI
};

static const uint8_t golomb_to_intra4x4_cbp[48] = {
    47, 31, 15, 0,  23, 27, 29, 30, 7,  11, 13, 14, 39, 43, 45, 46,
    16, 3,  5,  10, 12, 19, 21, 26, 28, 35, 37, 42, 44, 1,  2,  4,
    8,  17, 18, 20, 24, 6,  9,  22, 25, 32, 33, 34, 36, 40, 38, 41
};

static const uint8_t golomb_to_inter_cbp[48] = {
    0,  16, 1,  2,  4,  8,  32, 3,  5,  10, 12, 15, 47, 7,  11, 13,
    14, 6,  9,  31, 35, 37, 42, 44, 33, 34, 36, 40, 39, 43, 45, 46,
    17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41
};

static const uint8_t zigzag_scan[16+1] = {
    0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4,
    1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
    1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
    3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

static const uint8_t chroma_dc_scan[4] = {
    (0 + 0 * 2) * 16, (1 + 0 * 2) * 16,
    (0 + 1 * 2) * 16, (1 + 1 * 2) * 16,
};

static const uint8_t chroma422_dc_scan[8] = {
    (0 + 0 * 2) * 16, (0 + 1 * 2) * 16,
    (1 + 0 * 2) * 16, (0 + 2 * 2) * 16,
    (0 + 3 * 2) * 16, (1 + 1 * 2) * 16,
    (1 + 2 * 2) * 16, (1 + 3 * 2) * 16,
};

typedef struct IMbInfo {
    uint16_t type;
    uint8_t pred_mode;
    uint8_t cbp;
} IMbInfo;

static const IMbInfo i_mb_type_info[26] = {
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

typedef struct PMbInfo {
    uint16_t type;
    uint8_t partition_count;
} PMbInfo;

static const PMbInfo p_mb_type_info[5] = {
    { MB_TYPE_16x16 | MB_TYPE_P0L0,                               1 },
    { MB_TYPE_16x8  | MB_TYPE_P0L0 | MB_TYPE_P1L0,                2 },
    { MB_TYPE_8x16  | MB_TYPE_P0L0 | MB_TYPE_P1L0,                2 },
    { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0,                4 },
    { MB_TYPE_8x8   | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_REF0, 4 },
};

static const PMbInfo p_sub_mb_type_info[4] = {
    { MB_TYPE_16x16 | MB_TYPE_P0L0, 1 },
    { MB_TYPE_16x8  | MB_TYPE_P0L0, 2 },
    { MB_TYPE_8x16  | MB_TYPE_P0L0, 2 },
    { MB_TYPE_8x8   | MB_TYPE_P0L0, 4 },
};

static const PMbInfo b_mb_type_info[23] = {
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

static const PMbInfo b_sub_mb_type_info[13] = {
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

#endif /* AVCODEC_H264DATA_H */
