/*
 * Common Ut Video header
 * Copyright (c) 2011 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_UTVIDEO_H
#define AVCODEC_UTVIDEO_H

/**
 * @file
 * Common Ut Video header
 */

#include "libavutil/common.h"
#include "avcodec.h"
#include "dsputil.h"

enum {
    PRED_NONE = 0,
    PRED_LEFT,
    PRED_GRADIENT,
    PRED_MEDIAN,
};

enum {
    COMP_NONE = 0,
    COMP_HUFF,
};

/*
 * "Original format" markers.
 * Based on values gotten from the official VFW encoder.
 * They are not used during decoding, but they do have
 * an informative role on seeing what was input
 * to the encoder.
 */
enum {
    UTVIDEO_RGB  = MKTAG(0x00, 0x00, 0x01, 0x18),
    UTVIDEO_RGBA = MKTAG(0x00, 0x00, 0x02, 0x18),
    UTVIDEO_420  = MKTAG('Y', 'V', '1', '2'),
    UTVIDEO_422  = MKTAG('Y', 'U', 'Y', '2'),
};

/* Mapping of libavcodec prediction modes to Ut Video's */
extern const int ff_ut_pred_order[5];

/* Order of RGB(A) planes in Ut Video */
extern const int ff_ut_rgb_order[4];

typedef struct UtvideoContext {
    AVCodecContext *avctx;
    DSPContext     dsp;

    uint32_t frame_info_size, flags, frame_info;
    int      planes;
    int      slices;
    int      compression;
    int      interlaced;
    int      frame_pred;

    int      slice_stride;
    uint8_t *slice_bits, *slice_buffer[4];
    int      slice_bits_size;
} UtvideoContext;

typedef struct HuffEntry {
    uint8_t  sym;
    uint8_t  len;
    uint32_t code;
} HuffEntry;

/* Compare huffman tree nodes */
int ff_ut_huff_cmp_len(const void *a, const void *b);

#endif /* AVCODEC_UTVIDEO_H */
