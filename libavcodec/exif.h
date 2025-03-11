/*
 * EXIF metadata parser
 * Copyright (c) 2013 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * EXIF metadata parser
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#ifndef AVCODEC_EXIF_H
#define AVCODEC_EXIF_H

#include <stdint.h>
#include "libavutil/dict.h"
#include "bytestream.h"

/** Data type identifiers for TIFF tags */
enum AVTiffDataType {
    AV_TIFF_BYTE = 1,
    AV_TIFF_STRING,
    AV_TIFF_SHORT,
    AV_TIFF_LONG,
    AV_TIFF_RATIONAL,
    AV_TIFF_SBYTE,
    AV_TIFF_UNDEFINED,
    AV_TIFF_SSHORT,
    AV_TIFF_SLONG,
    AV_TIFF_SRATIONAL,
    AV_TIFF_FLOAT,
    AV_TIFF_DOUBLE,
    AV_TIFF_IFD,
};

/** Recursively decodes all IFD's and
 *  adds included TAGS into the metadata dictionary. */
int avpriv_exif_decode_ifd(void *logctx, const uint8_t *buf, int size,
                           int le, int depth, AVDictionary **metadata);

int ff_exif_decode_ifd(void *logctx, GetByteContext *gbytes, int le,
                       int depth, AVDictionary **metadata);

#endif /* AVCODEC_EXIF_H */
