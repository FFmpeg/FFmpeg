/*
 * Copyright (c) 2002-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used
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
 * huffyuv codec for libavcodec.
 */

#ifndef AVCODEC_HUFFYUV_H
#define AVCODEC_HUFFYUV_H

#include <stdint.h>

#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"
#include "put_bits.h"

#define VLC_BITS 11

#if HAVE_BIGENDIAN
#define B 3
#define G 2
#define R 1
#define A 0
#else
#define B 0
#define G 1
#define R 2
#define A 3
#endif

typedef enum Predictor {
    LEFT = 0,
    PLANE,
    MEDIAN,
} Predictor;

typedef struct HYuvContext {
    AVCodecContext *avctx;
    Predictor predictor;
    GetBitContext gb;
    PutBitContext pb;
    int interlaced;
    int decorrelate;
    int bitstream_bpp;
    int version;
    int yuy2;                               //use yuy2 instead of 422P
    int bgr32;                              //use bgr32 instead of bgr24
    int width, height;
    int flags;
    int context;
    int picture_number;
    int last_slice_end;
    uint8_t *temp[3];
    uint64_t stats[3][256];
    uint8_t len[3][256];
    uint32_t bits[3][256];
    uint32_t pix_bgr_map[1<<VLC_BITS];
    VLC vlc[6];                             //Y,U,V,YY,YU,YV
    AVFrame picture;
    uint8_t *bitstream_buffer;
    unsigned int bitstream_buffer_size;
    DSPContext dsp;
} HYuvContext;

void ff_huffyuv_common_init(AVCodecContext *s);
void ff_huffyuv_common_end(HYuvContext *s);
int  ff_huffyuv_alloc_temp(HYuvContext *s);
int ff_huffyuv_generate_bits_table(uint32_t *dst, const uint8_t *len_table);

#endif /* AVCODEC_HUFFYUV_H */
