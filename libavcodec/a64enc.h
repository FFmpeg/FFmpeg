/*
 * a64 video encoder - basic headers
 * Copyright (c) 2009 Tobias Bindhammer
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
 * a64 video encoder - basic headers
 */

#ifndef AVCODEC_A64ENC_H
#define AVCODEC_A64ENC_H

#include "libavutil/lfg.h"
#include "avcodec.h"

#define C64XRES 320
#define C64YRES 200

typedef struct A64Context {
    /* general variables */
    AVFrame picture;

    /* variables for multicolor modes */
    AVLFG randctx;
    int mc_lifetime;
    int mc_use_5col;
    unsigned mc_frame_counter;
    int *mc_meta_charset;
    int *mc_charmap;
    int *mc_best_cb;
    int mc_luma_vals[5];
    uint8_t *mc_charset;
    uint8_t *mc_colram;
    uint8_t *mc_palette;
    int mc_pal_size;

    /* pts of the next packet that will be output */
    int64_t next_pts;
} A64Context;

#endif /* AVCODEC_A64ENC_H */
