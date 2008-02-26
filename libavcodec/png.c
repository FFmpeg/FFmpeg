/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard.
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
#include "avcodec.h"
#include "bytestream.h"
#include "png.h"

const uint8_t ff_pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
const uint8_t ff_mngsig[8] = {138, 77, 78, 71, 13, 10, 26, 10};

/* Mask to determine which y pixels are valid in a pass */
const uint8_t ff_png_pass_ymask[NB_PASSES] = {
    0x80, 0x80, 0x08, 0x88, 0x22, 0xaa, 0x55,
};

/* minimum x value */
const uint8_t ff_png_pass_xmin[NB_PASSES] = {
    0, 4, 0, 2, 0, 1, 0
};

/* x shift to get row width */
const uint8_t ff_png_pass_xshift[NB_PASSES] = {
    3, 3, 2, 2, 1, 1, 0
};

/* Mask to determine which pixels are valid in a pass */
const uint8_t ff_png_pass_mask[NB_PASSES] = {
    0x80, 0x08, 0x88, 0x22, 0xaa, 0x55, 0xff
};

void *ff_png_zalloc(void *opaque, unsigned int items, unsigned int size)
{
    if(items >= UINT_MAX / size)
        return NULL;
    return av_malloc(items * size);
}

void ff_png_zfree(void *opaque, void *ptr)
{
    av_free(ptr);
}

int ff_png_get_nb_channels(int color_type)
{
    int channels;
    channels = 1;
    if ((color_type & (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE)) ==
        PNG_COLOR_MASK_COLOR)
        channels = 3;
    if (color_type & PNG_COLOR_MASK_ALPHA)
        channels++;
    return channels;
}

/* compute the row size of an interleaved pass */
int ff_png_pass_row_size(int pass, int bits_per_pixel, int width)
{
    int shift, xmin, pass_width;

    xmin = ff_png_pass_xmin[pass];
    if (width <= xmin)
        return 0;
    shift = ff_png_pass_xshift[pass];
    pass_width = (width - xmin + (1 << shift) - 1) >> shift;
    return (pass_width * bits_per_pixel + 7) >> 3;
}
