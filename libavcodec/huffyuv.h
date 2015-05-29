/*
 * Copyright (c) 2002-2014 Michael Niedermayer <michaelni@gmx.at>
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
#include "bswapdsp.h"
#include "get_bits.h"
#include "huffyuvdsp.h"
#include "huffyuvencdsp.h"
#include "put_bits.h"
#include "lossless_videodsp.h"

#define VLC_BITS 12

#define MAX_BITS 16
#define MAX_N (1<<MAX_BITS)
#define MAX_VLC_N 16384

typedef enum Predictor {
    LEFT = 0,
    PLANE,
    MEDIAN,
} Predictor;

typedef struct HYuvContext {
    AVClass *class;
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
    int bps;
    int n;                                  // 1<<bps
    int vlc_n;                              // number of vlc codes (FFMIN(1<<bps, MAX_VLC_N))
    int alpha;
    int chroma;
    int yuv;
    int chroma_h_shift;
    int chroma_v_shift;
    int width, height;
    int flags;
    int context;
    int picture_number;
    int last_slice_end;
    uint8_t *temp[3];
    uint16_t *temp16[3];                    ///< identical to temp but 16bit type
    uint64_t stats[4][MAX_VLC_N];
    uint8_t len[4][MAX_VLC_N];
    uint32_t bits[4][MAX_VLC_N];
    uint32_t pix_bgr_map[1<<VLC_BITS];
    VLC vlc[8];                             //Y,U,V,A,YY,YU,YV,AA
    uint8_t *bitstream_buffer;
    unsigned int bitstream_buffer_size;
    BswapDSPContext bdsp;
    HuffYUVDSPContext hdsp;
    HuffYUVEncDSPContext hencdsp;
    LLVidDSPContext llviddsp;
    int non_determ; // non-deterministic, multi-threaded encoder allowed
} HYuvContext;

void ff_huffyuv_common_init(AVCodecContext *s);
void ff_huffyuv_common_end(HYuvContext *s);
int  ff_huffyuv_alloc_temp(HYuvContext *s);
int ff_huffyuv_generate_bits_table(uint32_t *dst, const uint8_t *len_table, int n);

#endif /* AVCODEC_HUFFYUV_H */
