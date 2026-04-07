/**
 * Copyright (C) 2025 Niklas Haas
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

#ifndef SWSCALE_UOPS_H
#define SWSCALE_UOPS_H

#include <stdbool.h>
#include <stdint.h>

/***************************************************************************
 * Note: This header must be usable at build time, to generate asm sources *
 ***************************************************************************/

#include "libavutil/attributes.h"

typedef enum SwsPixelType {
    SWS_PIXEL_NONE = 0,
    SWS_PIXEL_U8,
    SWS_PIXEL_U16,
    SWS_PIXEL_U32,
    SWS_PIXEL_F32,
    SWS_PIXEL_TYPE_NB
} SwsPixelType;

const char *ff_sws_pixel_type_name(SwsPixelType type);
int ff_sws_pixel_type_size(SwsPixelType type) av_const;
bool ff_sws_pixel_type_is_int(SwsPixelType type) av_const;

/**
 * Bit-mask of components. Exact meaning depends on the usage context.
 */
typedef uint8_t SwsCompMask;
enum {
    SWS_COMP_NONE = 0,
    SWS_COMP_ALL  = 0xF,
#define SWS_COMP(X) (1 << (X))
#define SWS_COMP_TEST(mask, X) (!!((mask) & SWS_COMP(X)))
#define SWS_COMP_INV(mask) ((mask) ^ SWS_COMP_ALL)
#define SWS_COMP_ELEMS(N) ((1 << (N)) - 1)
#define SWS_COMP_MASK(X, Y, Z, W)   \
    (((X) ? SWS_COMP(0) : 0) |      \
     ((Y) ? SWS_COMP(1) : 0) |      \
     ((Z) ? SWS_COMP(2) : 0) |      \
     ((W) ? SWS_COMP(3) : 0))
};

#endif
