/*
 * PNM image format
 * Copyright (c) 2002, 2003 Fabrice Bellard
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

#ifndef AVCODEC_PNM_H
#define AVCODEC_PNM_H

#include "avcodec.h"

typedef struct PNMContext {
    uint8_t *bytestream;
    uint8_t *bytestream_start;
    uint8_t *bytestream_end;
    AVFrame picture;
    int maxval;                 ///< maximum value of a pixel
    int type;
} PNMContext;

int ff_pnm_decode_header(AVCodecContext *avctx, PNMContext * const s);
av_cold int ff_pnm_end(AVCodecContext *avctx);
av_cold int ff_pnm_init(AVCodecContext *avctx);

#endif /* AVCODEC_PNM_H */
