/*
 * Copyright (c) 2010-2011 Maxim Poliakovski
 * Copyright (c) 2010-2011 Elvis Presley
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_PRORESDEC_H
#define AVCODEC_PRORESDEC_H

#include "dsputil.h"
#include "proresdsp.h"

typedef struct {
    const uint8_t *data;
    unsigned mb_x;
    unsigned mb_y;
    unsigned mb_count;
    unsigned data_size;
} SliceContext;

typedef struct {
    DSPContext dsp;
    ProresDSPContext prodsp;
    AVFrame frame;
    int frame_type;              ///< 0 = progressive, 1 = tff, 2 = bff
    uint8_t qmat_luma[64];
    uint8_t qmat_chroma[64];
    SliceContext *slices;
    int slice_count;             ///< number of slices in the current picture
    unsigned mb_width;           ///< width of the current picture in mb
    unsigned mb_height;          ///< height of the current picture in mb
    uint8_t progressive_scan[64];
    uint8_t interlaced_scan[64];
    const uint8_t *scan;
    int first_field;
} ProresContext;

#endif /* AVCODEC_PRORESDEC_H */
