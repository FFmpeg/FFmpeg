/*
 * MPEG1/2 decoder tables
 * copyright (c) 2000,2001 Fabrice Bellard
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * @file libavcodec/mpeg12decdata.h
 * MPEG1/2 decoder tables.
 */

#ifndef AVCODEC_MPEG12DECDATA_H
#define AVCODEC_MPEG12DECDATA_H

#include <stdint.h>
#include "mpegvideo.h"


#define MB_TYPE_ZERO_MV   0x20000000
#define IS_ZERO_MV(a)   ((a)&MB_TYPE_ZERO_MV)

static const uint8_t table_mb_ptype[7][2] = {
    { 3, 5 }, // 0x01 MB_INTRA
    { 1, 2 }, // 0x02 MB_PAT
    { 1, 3 }, // 0x08 MB_FOR
    { 1, 1 }, // 0x0A MB_FOR|MB_PAT
    { 1, 6 }, // 0x11 MB_QUANT|MB_INTRA
    { 1, 5 }, // 0x12 MB_QUANT|MB_PAT
    { 2, 5 }, // 0x1A MB_QUANT|MB_FOR|MB_PAT
};

static const uint32_t ptype2mb_type[7] = {
                    MB_TYPE_INTRA,
                    MB_TYPE_L0 | MB_TYPE_CBP | MB_TYPE_ZERO_MV | MB_TYPE_16x16,
                    MB_TYPE_L0,
                    MB_TYPE_L0 | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_INTRA,
    MB_TYPE_QUANT | MB_TYPE_L0 | MB_TYPE_CBP | MB_TYPE_ZERO_MV | MB_TYPE_16x16,
    MB_TYPE_QUANT | MB_TYPE_L0 | MB_TYPE_CBP,
};

static const uint8_t table_mb_btype[11][2] = {
    { 3, 5 }, // 0x01 MB_INTRA
    { 2, 3 }, // 0x04 MB_BACK
    { 3, 3 }, // 0x06 MB_BACK|MB_PAT
    { 2, 4 }, // 0x08 MB_FOR
    { 3, 4 }, // 0x0A MB_FOR|MB_PAT
    { 2, 2 }, // 0x0C MB_FOR|MB_BACK
    { 3, 2 }, // 0x0E MB_FOR|MB_BACK|MB_PAT
    { 1, 6 }, // 0x11 MB_QUANT|MB_INTRA
    { 2, 6 }, // 0x16 MB_QUANT|MB_BACK|MB_PAT
    { 3, 6 }, // 0x1A MB_QUANT|MB_FOR|MB_PAT
    { 2, 5 }, // 0x1E MB_QUANT|MB_FOR|MB_BACK|MB_PAT
};

static const uint32_t btype2mb_type[11] = {
                    MB_TYPE_INTRA,
                    MB_TYPE_L1,
                    MB_TYPE_L1   | MB_TYPE_CBP,
                    MB_TYPE_L0,
                    MB_TYPE_L0   | MB_TYPE_CBP,
                    MB_TYPE_L0L1,
                    MB_TYPE_L0L1 | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_INTRA,
    MB_TYPE_QUANT | MB_TYPE_L1   | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_L0   | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_L0L1 | MB_TYPE_CBP,
};

static const uint8_t non_linear_qscale[32] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8,10,12,14,16,18,20,22,
    24,28,32,36,40,44,48,52,
    56,64,72,80,88,96,104,112,
};

static const uint8_t mpeg2_dc_scale_table1[128]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

static const uint8_t mpeg2_dc_scale_table2[128]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
};

static const uint8_t mpeg2_dc_scale_table3[128]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const uint8_t * const mpeg2_dc_scale_table[4]={
    ff_mpeg1_dc_scale_table,
    mpeg2_dc_scale_table1,
    mpeg2_dc_scale_table2,
    mpeg2_dc_scale_table3,
};

#endif /* AVCODEC_MPEG12DECDATA_H */
