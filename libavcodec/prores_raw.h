/*
 * ProRes RAW decoder
 * Copyright (c) 2025 Lynne
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

#ifndef AVCODEC_PRORES_RAW_H
#define AVCODEC_PRORES_RAW_H

#include "libavutil/frame.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixfmt.h"

#include "bytestream.h"
#include "blockdsp.h"
#include "proresdsp.h"

typedef struct TileContext {
    GetByteContext gb;
    unsigned x, y;
    int log2_nb_blocks;
} TileContext;

typedef struct ProResRAWContext {
    ProresDSPContext prodsp;
    BlockDSPContext  bdsp;

    TileContext *tiles;
    unsigned int tiles_size;
    int nb_tiles;
    int th;
    int nb_tw, nb_th;

    enum AVPixelFormat pix_fmt;
    AVFrame *frame;
    void *hwaccel_picture_private;

    int version;

    DECLARE_ALIGNED(32, uint8_t, qmat)[64];

    /* 8-point combined linearization curve
     * (inv. transfer fn + encoder-defined shaping) from the frame header,
     * applied after iDCT */
    uint16_t lin_curve[8];
} ProResRAWContext;

extern const uint8_t ff_prores_raw_dc_cb[13];
extern const int16_t ff_prores_raw_ac_cb[95];
extern const int16_t ff_prores_raw_rn_cb[28];
extern const int16_t ff_prores_raw_ln_cb[15];

#endif /* AVCODEC_PRORES_RAW_H */
