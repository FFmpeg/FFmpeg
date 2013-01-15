/*
 * Copyright (c) 2011-2012 Derek Buitenhuis
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Known FOURCCs:
 *     'ULY0' (YCbCr 4:2:0), 'ULY2' (YCbCr 4:2:2), 'ULRG' (RGB), 'ULRA' (RGBA)
 */

#ifndef AVCODEC_LIBUTVIDEO_H
#define AVCODEC_LIBUTVIDEO_H

#include <stdlib.h>
#include <utvideo/utvideo.h>
#include <utvideo/Codec.h>

/* Ut Video version 12.0.0 removed the _WIN names, so if those are
 * absent, redefine them to maintain compatibility with pre-v12 versions.*/
#if !defined(UTVF_RGB24_WIN)
#define UTVF_RGB24_WIN UTVF_NFCC_BGR_BU
#endif

#if !defined(UTVF_RGB32_WIN)
#define UTVF_RGB32_WIN UTVF_NFCC_BGRA_BU
#endif

typedef struct {
    uint32_t version;
    uint32_t original_format;
    uint32_t frameinfo_size;
    uint32_t flags;
} UtVideoExtra;

typedef struct {
    CCodec *codec;
    unsigned int buf_size;
    uint8_t *buffer;
} UtVideoContext;

#endif /* AVCODEC_LIBUTVIDEO_H */
