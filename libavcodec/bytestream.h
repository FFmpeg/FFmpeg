/*
 * Bytestream functions
 * copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@free.fr>
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

#ifndef FFMPEG_BYTESTREAM_H
#define FFMPEG_BYTESTREAM_H

static always_inline unsigned int bytestream_get_le32(uint8_t **b)
{
    (*b) += 4;
    return LE_32(*b - 4);
}

static always_inline unsigned int bytestream_get_le16(uint8_t **b)
{
    (*b) += 2;
    return LE_16(*b - 2);
}

static always_inline unsigned int bytestream_get_byte(uint8_t **b)
{
    (*b)++;
    return (*b)[-1];
}

static always_inline unsigned int bytestream_get_buffer(uint8_t **b, uint8_t *dst, unsigned int size)
{
    memcpy(dst, *b, size);
    (*b) += size;
    return size;
}

#endif /* FFMPEG_BYTESTREAM_H */
