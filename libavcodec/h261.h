/*
 * H261 decoder
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
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
 * h261codec.
 */

#ifndef AVCODEC_H261_H
#define AVCODEC_H261_H

#include "mpegvideo.h"
#include "rl.h"

/**
 * H261Context
 */
typedef struct H261Context {
    MpegEncContext s;

    int current_mba;
    int mba_diff;
    int mtype;
    int current_mv_x;
    int current_mv_y;
    int gob_number;
    int gob_start_code_skipped; // 1 if gob start code is already read before gob header is read
} H261Context;

#define MB_TYPE_H261_FIL 0x800000

extern uint8_t ff_h261_rl_table_store[2][2 * MAX_RUN + MAX_LEVEL + 3];

extern const uint8_t ff_h261_mba_code[35];
extern const uint8_t ff_h261_mba_bits[35];
extern const uint8_t ff_h261_mtype_code[10];
extern const uint8_t ff_h261_mtype_bits[10];
extern const int     ff_h261_mtype_map[10];
extern const uint8_t ff_h261_mv_tab[17][2];
extern const uint8_t ff_h261_cbp_tab[63][2];
extern RLTable ff_h261_rl_tcoeff;

void ff_h261_loop_filter(MpegEncContext *s);
void ff_h261_common_init(void);

int ff_h261_get_picture_format(int width, int height);
void ff_h261_reorder_mb_index(MpegEncContext *s);
void ff_h261_encode_mb(MpegEncContext *s, int16_t block[6][64],
                       int motion_x, int motion_y);
void ff_h261_encode_picture_header(MpegEncContext *s, int picture_number);
void ff_h261_encode_init(MpegEncContext *s);

#endif /* AVCODEC_H261_H */
