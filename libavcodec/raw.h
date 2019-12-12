/*
 * Raw Video Codec
 * Copyright (c) 2001 Fabrice Bellard
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
 * Raw Video Codec
 */

#ifndef AVCODEC_RAW_H
#define AVCODEC_RAW_H

#include "avcodec.h"
#include "internal.h"
#include "libavutil/internal.h"

typedef struct PixelFormatTag {
    enum AVPixelFormat pix_fmt;
    unsigned int fourcc;
} PixelFormatTag;

extern const PixelFormatTag ff_raw_pix_fmt_tags[]; // exposed through avpriv_get_raw_pix_fmt_tags()

const struct PixelFormatTag *avpriv_get_raw_pix_fmt_tags(void);

enum AVPixelFormat avpriv_find_pix_fmt(const PixelFormatTag *tags, unsigned int fourcc);

extern av_export_avcodec const PixelFormatTag avpriv_pix_fmt_bps_avi[];
extern av_export_avcodec const PixelFormatTag avpriv_pix_fmt_bps_mov[];

#endif /* AVCODEC_RAW_H */
