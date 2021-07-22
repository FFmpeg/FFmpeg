/*
 * Snappy decompression algorithm
 * Copyright (c) 2015 Luca Barbato
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

#include "bytestream.h"
#include "snappy.h"

enum {
    SNAPPY_LITERAL,
    SNAPPY_COPY_1,
    SNAPPY_COPY_2,
    SNAPPY_COPY_4,
};

static int64_t bytestream2_get_levarint(GetByteContext *gb)
{
    uint64_t val = 0;
    int shift = 0;
    int tmp;

    do {
        tmp = bytestream2_get_byte(gb);
        if (shift > 31 || ((tmp & 127LL) << shift) > INT_MAX)
            return AVERROR_INVALIDDATA;
        val |= (tmp & 127) << shift;
        shift += 7;
    } while (tmp & 128);

    return val;
}

static int snappy_literal(GetByteContext *gb, uint8_t *p, int size, int val)
{
    unsigned int len = 1;

    switch (val) {
    case 63:
        len += bytestream2_get_le32(gb);
        break;
    case 62:
        len += bytestream2_get_le24(gb);
        break;
    case 61:
        len += bytestream2_get_le16(gb);
        break;
    case 60:
        len += bytestream2_get_byte(gb);
        break;
    default: // val < 60
        len += val;
    }

    if (size < len)
        return AVERROR_INVALIDDATA;

    bytestream2_get_buffer(gb, p, len);

    return len;
}

static int snappy_copy(uint8_t *start, uint8_t *p, int size,
                       unsigned int off, int len)
{
    uint8_t *q;
    int i;
    if (off > p - start || size < len)
        return AVERROR_INVALIDDATA;

    q = p - off;

    for (i = 0; i < len; i++)
        p[i] = q[i];

    return len;
}

static int snappy_copy1(GetByteContext *gb, uint8_t *start, uint8_t *p,
                        int size, int val)
{
    int len          = 4 + (val & 0x7);
    unsigned int off = bytestream2_get_byte(gb) | (val & 0x38) << 5;

    return snappy_copy(start, p, size, off, len);
}

static int snappy_copy2(GetByteContext *gb, uint8_t *start, uint8_t *p,
                        int size, int val)
{
    int len          = 1 + val;
    unsigned int off = bytestream2_get_le16(gb);

    return snappy_copy(start, p, size, off, len);
}

static int snappy_copy4(GetByteContext *gb, uint8_t *start, uint8_t *p,
                        int size, int val)
{
    int len          = 1 + val;
    unsigned int off = bytestream2_get_le32(gb);

    return snappy_copy(start, p, size, off, len);
}

static int64_t decode_len(GetByteContext *gb)
{
    int64_t len = bytestream2_get_levarint(gb);

    if (len < 0 || len > UINT_MAX)
        return AVERROR_INVALIDDATA;

    return len;
}

int64_t ff_snappy_peek_uncompressed_length(GetByteContext *gb)
{
    int pos = bytestream2_get_bytes_left(gb);
    int64_t len = decode_len(gb);

    bytestream2_seek(gb, -pos, SEEK_END);

    return len;
}

int ff_snappy_uncompress(GetByteContext *gb, uint8_t *buf, int64_t *size)
{
    int64_t len = decode_len(gb);
    int ret     = 0;
    uint8_t *p;

    if (len < 0)
        return len;

    if (len > *size)
        return AVERROR_BUFFER_TOO_SMALL;

    *size = len;
    p     = buf;

    while (bytestream2_get_bytes_left(gb) > 0) {
        uint8_t s = bytestream2_get_byte(gb);
        int val   = s >> 2;

        switch (s & 0x03) {
        case SNAPPY_LITERAL:
            ret = snappy_literal(gb, p, len, val);
            break;
        case SNAPPY_COPY_1:
            ret = snappy_copy1(gb, buf, p, len, val);
            break;
        case SNAPPY_COPY_2:
            ret = snappy_copy2(gb, buf, p, len, val);
            break;
        case SNAPPY_COPY_4:
            ret = snappy_copy4(gb, buf, p, len, val);
            break;
        }

        if (ret < 0)
            return ret;

        p   += ret;
        len -= ret;
    }

    return 0;
}
