/*
 * Copyright (c) 2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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
* @file
* data structures common to libdiracenc.c and libdiracdec.c
*/

#ifndef AVCODEC_LIBDIRAC_H
#define AVCODEC_LIBDIRAC_H

#include "avcodec.h"
#include <libdirac_common/dirac_types.h>

/**
* Table providing a Dirac chroma format to FFmpeg pixel format mapping.
*/
static const struct {
    enum PixelFormat ff_pix_fmt;
    dirac_chroma_t dirac_pix_fmt;
} ffmpeg_dirac_pixel_format_map[] = {
    { PIX_FMT_YUV420P, format420 },
    { PIX_FMT_YUV422P, format422 },
    { PIX_FMT_YUV444P, format444 },
};

#endif /* AVCODEC_LIBDIRAC_H */
