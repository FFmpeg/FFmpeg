/*
 * Vidvox Hap
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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

#ifndef AVCODEC_HAP_H
#define AVCODEC_HAP_H

#include <stdint.h>

#include "libavutil/opt.h"

#include "bytestream.h"
#include "texturedsp.h"

typedef struct HapContext {
    AVClass *class;

    TextureDSPContext dxtc;
    GetByteContext gbc;
    PutByteContext pbc;

    int section_type;        /* Header type */

    int tex_rat;             /* Compression ratio */
    const uint8_t *tex_data; /* Compressed texture */
    uint8_t *tex_buf;        /* Uncompressed texture */
    size_t tex_size;         /* Size of the compressed texture */

    uint8_t *snappied;       /* Buffer interacting with snappy */
    size_t max_snappy;       /* Maximum compressed size for snappy buffer */

    /* Pointer to the selected compress or decompress function */
    int (*tex_fun)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
} HapContext;

enum {
    HAP_FMT_RGBDXT1   = 0x0B,
    HAP_FMT_RGBADXT5  = 0x0E,
    HAP_FMT_YCOCGDXT5 = 0x0F,
};

enum {
    HAP_COMP_NONE    = 0xA0,
    HAP_COMP_SNAPPY  = 0xB0,
    HAP_COMP_COMPLEX = 0xC0,
};

#endif /* AVCODEC_HAP_H */
