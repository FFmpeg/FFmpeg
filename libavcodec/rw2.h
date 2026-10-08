/*
 * Panasonic RW2 raw image decoder
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

#ifndef AVCODEC_RW2_H
#define AVCODEC_RW2_H

#include <stdint.h>

#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"

#include "vlc.h"

#define RW2_MAX_STRIPES 5
#define RW2_MAX_IFD0_ENTRIES 512  /* parser cap, sizes exif[] */
#define RW2_JOINT_CATS  13   /* categories with a code per magnitude, 2^k each */
#define RW2_MAX_CODES   ((1 << RW2_JOINT_CATS) + 17)

typedef struct RW2Stripe {
    uint32_t offset;          /* bytes, from the start of the file */
    uint32_t size_bits;
    uint32_t left;
    uint32_t width;
    uint32_t height;
} RW2Stripe;

/* Coding parameters, tags 0x39 - 0x41 */
typedef struct RW2Coding {
    uint32_t gamma_shift[6];  /* 0x39 */
    uint16_t gamma_point[6];  /* 0x3A */
    int datamax;              /* 0x3B */
    int initial[4];           /* 0x3C - 0x3F */
    uint8_t code_len[17];     /* 0x40, Huffman code per magnitude category */
    uint16_t code_val[17];
    uint8_t code_shift[17];   /* 0x41, magnitude bits left out */
} RW2Coding;

/* IFD0 contents, RawFormat 8 */
typedef struct RW2Header {
    int raw_width, raw_height;
    int border[4];            /* top, left, bottom, right */
    int cfa;
    int bpp;
    int raw_format;
    int orientation;
    int black[3];
    int linearity[3];
    int wb[3];                /* as-shot white balance levels */

    RW2Coding c;

    int nb_stripes;
    RW2Stripe stripes[RW2_MAX_STRIPES];

    /* Standard IFD0 tags kept verbatim for EXIF, gathered during the header
     * walk, with the IFD0 offset and where the metadata ends (first preview) */
    uint8_t  exif[RW2_MAX_IFD0_ENTRIES * 12];
    int      exif_count;
    uint32_t exif_off;
    int64_t  exif_end;
} RW2Header;

typedef struct RW2Code {
    uint8_t len;
    uint16_t code;
    int16_t sym;
} RW2Code;

typedef struct RW2DecContext {
    AVFrame *frame;
    void *hwaccel_picture_private;
    enum AVPixelFormat pix_fmt;

    RW2Header h;

    const uint8_t *payload;

    VLC vlc;
    RW2Code codes[RW2_MAX_CODES];
    uint16_t gamma[1 << 16];
    int use_gamma;
} RW2DecContext;

#endif /* AVCODEC_RW2_H */
