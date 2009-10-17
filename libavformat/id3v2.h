/*
 * ID3v2 header parser
 * Copyright (c) 2003 Fabrice Bellard
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

#ifndef AVFORMAT_ID3V2_H
#define AVFORMAT_ID3V2_H

#include <stdint.h>
#include "avformat.h"
#include "metadata.h"

#define ID3v2_HEADER_SIZE 10

/**
 * Detects ID3v2 Header.
 * @buf must be ID3v2_HEADER_SIZE byte long
 */
int ff_id3v2_match(const uint8_t *buf);

/**
 * Gets the length of an ID3v2 tag.
 * @buf must be ID3v2_HEADER_SIZE bytes long and point to the start of an
 * already detected ID3v2 tag
 */
int ff_id3v2_tag_len(const uint8_t *buf);

/**
 * ID3v2 parser
 * Handles ID3v2.2, 2.3 and 2.4.
 */
void ff_id3v2_parse(AVFormatContext *s, int len, uint8_t version, uint8_t flags);

/**
 * Read an ID3v2 tag
 */
void ff_id3v2_read(AVFormatContext *s);

extern const AVMetadataConv ff_id3v2_metadata_conv[];

/**
 * A list of ID3v2.4 text information frames.
 * http://www.id3.org/id3v2.4.0-frames
 */
extern const char ff_id3v2_tags[][4];

#endif /* AVFORMAT_ID3V2_H */
