/*
 * TIFF tables
 * Copyright (c) 2006 Konstantin Shishkov
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

/**
 * @file
 * TIFF tables
 * @author Konstantin Shishkov
 */

#ifndef AVCODEC_TIFF_H
#define AVCODEC_TIFF_H

#include <stdint.h>

/** abridged list of TIFF tags */
enum TiffTags {
    TIFF_SUBFILE            = 0xfe,
    TIFF_WIDTH              = 0x100,
    TIFF_HEIGHT,
    TIFF_BPP,
    TIFF_COMPR,
    TIFF_PHOTOMETRIC        = 0x106,
    TIFF_FILL_ORDER         = 0x10A,
    TIFF_STRIP_OFFS         = 0x111,
    TIFF_SAMPLES_PER_PIXEL  = 0x115,
    TIFF_ROWSPERSTRIP       = 0x116,
    TIFF_STRIP_SIZE,
    TIFF_XRES               = 0x11A,
    TIFF_YRES               = 0x11B,
    TIFF_PLANAR             = 0x11C,
    TIFF_XPOS               = 0x11E,
    TIFF_YPOS               = 0x11F,
    TIFF_T4OPTIONS          = 0x124,
    TIFF_T6OPTIONS,
    TIFF_RES_UNIT           = 0x128,
    TIFF_SOFTWARE_NAME      = 0x131,
    TIFF_PREDICTOR          = 0x13D,
    TIFF_PAL                = 0x140,
    TIFF_YCBCR_COEFFICIENTS = 0x211,
    TIFF_YCBCR_SUBSAMPLING  = 0x212,
    TIFF_YCBCR_POSITIONING  = 0x213,
    TIFF_REFERENCE_BW       = 0x214,
};

/** list of TIFF compression types */
enum TiffCompr {
    TIFF_RAW = 1,
    TIFF_CCITT_RLE,
    TIFF_G3,
    TIFF_G4,
    TIFF_LZW,
    TIFF_JPEG,
    TIFF_NEWJPEG,
    TIFF_ADOBE_DEFLATE,
    TIFF_PACKBITS = 0x8005,
    TIFF_DEFLATE  = 0x80B2,
    TIFF_LZMA     = 0x886D,
};

enum TiffTypes {
    TIFF_BYTE = 1,
    TIFF_STRING,
    TIFF_SHORT,
    TIFF_LONG,
    TIFF_RATIONAL,
};

enum TiffPhotometric {
    TIFF_PHOTOMETRIC_NONE       = -1,
    TIFF_PHOTOMETRIC_WHITE_IS_ZERO,      /* mono or grayscale, 0 is white */
    TIFF_PHOTOMETRIC_BLACK_IS_ZERO,      /* mono or grayscale, 0 is black */
    TIFF_PHOTOMETRIC_RGB,                /* RGB or RGBA*/
    TIFF_PHOTOMETRIC_PALETTE,            /* Uses a palette */
    TIFF_PHOTOMETRIC_ALPHA_MASK,         /* Transparency mask */
    TIFF_PHOTOMETRIC_SEPARATED,          /* CMYK or some other ink set */
    TIFF_PHOTOMETRIC_YCBCR,              /* YCbCr */
    TIFF_PHOTOMETRIC_CIE_LAB    = 8,     /* 1976 CIE L*a*b* */
    TIFF_PHOTOMETRIC_ICC_LAB,            /* ICC L*a*b* */
    TIFF_PHOTOMETRIC_ITU_LAB,            /* ITU L*a*b* */
    TIFF_PHOTOMETRIC_CFA        = 32803, /* Color Filter Array (DNG) */
    TIFF_PHOTOMETRIC_LOG_L      = 32844, /* CIE Log2(L) */
    TIFF_PHOTOMETRIC_LOG_LUV,            /* CIE Log L*u*v* */
    TIFF_PHOTOMETRIC_LINEAR_RAW = 34892, /* Linear Raw (DNG) */
};

/** sizes of various TIFF field types (string size = 100)*/
static const uint8_t type_sizes[6] = {
    0, 1, 100, 2, 4, 8
};

#endif /* AVCODEC_TIFF_H */
