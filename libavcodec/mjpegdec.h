/*
 * MJPEG decoder
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
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
 * @file mjpegdec.h
 * MJPEG decoder.
 */

#ifndef MJPEGDEC_H
#define MJPEGDEC_H

#include "avcodec.h"

int ff_mjpeg_decode_init(AVCodecContext *avctx);
int ff_mjpeg_decode_end(AVCodecContext *avctx);
int ff_mjpeg_decode_frame(AVCodecContext *avctx,
                          void *data, int *data_size,
                          uint8_t *buf, int buf_size);

#endif /* MJPEGDEC_H */
