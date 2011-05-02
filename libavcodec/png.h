/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard
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

#ifndef AVCODEC_PNG_H
#define AVCODEC_PNG_H

#include <stdint.h>
#include <zlib.h>

#include "avcodec.h"

#define PNG_COLOR_MASK_PALETTE    1
#define PNG_COLOR_MASK_COLOR      2
#define PNG_COLOR_MASK_ALPHA      4

#define PNG_COLOR_TYPE_GRAY 0
#define PNG_COLOR_TYPE_PALETTE  (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE)
#define PNG_COLOR_TYPE_RGB        (PNG_COLOR_MASK_COLOR)
#define PNG_COLOR_TYPE_RGB_ALPHA  (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_ALPHA)
#define PNG_COLOR_TYPE_GRAY_ALPHA (PNG_COLOR_MASK_ALPHA)

#define PNG_FILTER_TYPE_LOCO   64
#define PNG_FILTER_VALUE_NONE  0
#define PNG_FILTER_VALUE_SUB   1
#define PNG_FILTER_VALUE_UP    2
#define PNG_FILTER_VALUE_AVG   3
#define PNG_FILTER_VALUE_PAETH 4
#define PNG_FILTER_VALUE_MIXED 5

#define PNG_IHDR      0x0001
#define PNG_IDAT      0x0002
#define PNG_ALLIMAGE  0x0004
#define PNG_PLTE      0x0008

#define NB_PASSES 7

extern const uint8_t ff_pngsig[8];
extern const uint8_t ff_mngsig[8];

/* Mask to determine which y pixels are valid in a pass */
extern const uint8_t ff_png_pass_ymask[NB_PASSES];

/* Mask to determine which pixels are valid in a pass */
extern const uint8_t ff_png_pass_mask[NB_PASSES];

void *ff_png_zalloc(void *opaque, unsigned int items, unsigned int size);

void ff_png_zfree(void *opaque, void *ptr);

int ff_png_get_nb_channels(int color_type);

/* compute the row size of an interleaved pass */
int ff_png_pass_row_size(int pass, int bits_per_pixel, int width);

void ff_add_png_paeth_prediction(uint8_t *dst, uint8_t *src, uint8_t *top, int w, int bpp);

typedef struct PNGDecContext {
    const uint8_t *bytestream;
    const uint8_t *bytestream_start;
    const uint8_t *bytestream_end;
    AVFrame picture1, picture2;
    AVFrame *current_picture, *last_picture;

    int state;
    int width, height;
    int bit_depth;
    int color_type;
    int compression_type;
    int interlace_type;
    int filter_type;
    int channels;
    int bits_per_pixel;
    int bpp;

    uint8_t *image_buf;
    int image_linesize;
    uint32_t palette[256];
    uint8_t *crow_buf;
    uint8_t *last_row;
    uint8_t *tmp_row;
    int pass;
    int crow_size; /* compressed row size (include filter type) */
    int row_size; /* decompressed row size */
    int pass_row_size; /* decompress row size of the current pass */
    int y;
    z_stream zstream;

    void (*add_bytes_l2)(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w);
    void (*add_paeth_prediction)(uint8_t *dst, uint8_t *src, uint8_t *top, int w, int bpp);
} PNGDecContext;

void ff_png_init_mmx(PNGDecContext *s);

#endif /* AVCODEC_PNG_H */
