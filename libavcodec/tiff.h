/*
 * TIFF tables
 * Copyright (c) 2006 Konstantin Shishkov
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
 * TIFF tables
 * @file libavcodec/tiff.h
 * @author Konstantin Shishkov
 */
#ifndef AVCODEC_TIFF_H
#define AVCODEC_TIFF_H

#include <stdint.h>

/** abridged list of TIFF tags */
enum TiffTags{
    TIFF_SUBFILE = 0xfe,
    TIFF_WIDTH = 0x100,
    TIFF_HEIGHT,
    TIFF_BPP,
    TIFF_COMPR,
    TIFF_INVERT = 0x106,
    TIFF_FILL_ORDER = 0x10A,
    TIFF_STRIP_OFFS = 0x111,
    TIFF_SAMPLES_PER_PIXEL = 0x115,
    TIFF_ROWSPERSTRIP = 0x116,
    TIFF_STRIP_SIZE,
    TIFF_XRES = 0x11A,
    TIFF_YRES = 0x11B,
    TIFF_PLANAR = 0x11C,
    TIFF_XPOS = 0x11E,
    TIFF_YPOS = 0x11F,
    TIFF_T4OPTIONS = 0x124,
    TIFF_T6OPTIONS,
    TIFF_RES_UNIT = 0x128,
    TIFF_SOFTWARE_NAME = 0x131,
    TIFF_PREDICTOR = 0x13D,
    TIFF_PAL = 0x140,
    TIFF_YCBCR_COEFFICIENTS = 0x211,
    TIFF_YCBCR_SUBSAMPLING = 0x212,
    TIFF_YCBCR_POSITIONING = 0x213,
    TIFF_REFERENCE_BW = 0x214,
};

/** list of TIFF compression types */
enum TiffCompr{
    TIFF_RAW = 1,
    TIFF_CCITT_RLE,
    TIFF_G3,
    TIFF_G4,
    TIFF_LZW,
    TIFF_JPEG,
    TIFF_NEWJPEG,
    TIFF_ADOBE_DEFLATE,
    TIFF_PACKBITS = 0x8005,
    TIFF_DEFLATE = 0x80B2
};

enum TiffTypes{
    TIFF_BYTE = 1,
    TIFF_STRING,
    TIFF_SHORT,
    TIFF_LONG,
    TIFF_RATIONAL,
};

/** sizes of various TIFF field types (string size = 100)*/
static const uint8_t type_sizes[6] = {
    0, 1, 100, 2, 4, 8
};

#endif /* AVCODEC_TIFF_H */
