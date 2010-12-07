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
 * Default magic bytes for ID3v2 header: "ID3"
 */
#define ID3v2_DEFAULT_MAGIC "ID3"

#define ID3v2_FLAG_DATALEN     0x0001
#define ID3v2_FLAG_UNSYNCH     0x0002
#define ID3v2_FLAG_ENCRYPTION  0x0004
#define ID3v2_FLAG_COMPRESSION 0x0008

/**
 * Detect ID3v2 Header.
 * @param buf   must be ID3v2_HEADER_SIZE byte long
 * @param magic magic bytes to identify the header, machine byte order.
 * If in doubt, use ID3v2_DEFAULT_MAGIC.
 */
int ff_id3v2_match(const uint8_t *buf, const char *magic);

/**
 * Get the length of an ID3v2 tag.
 * @param buf must be ID3v2_HEADER_SIZE bytes long and point to the start of an
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
void ff_id3v2_read(AVFormatContext *s, const char *magic);

extern const AVMetadataConv ff_id3v2_metadata_conv[];

/**
 * A list of ID3v2.4 text information frames.
 * http://www.id3.org/id3v2.4.0-frames
 */
extern const char ff_id3v2_tags[][4];

#endif /* AVFORMAT_ID3V2_H */
