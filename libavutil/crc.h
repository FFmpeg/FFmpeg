/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef CRC_H
#define CRC_H

typedef uint32_t AVCRC;

#if LIBAVUTIL_VERSION_INT  < (50<<16)
extern AVCRC *av_crcEDB88320;
extern AVCRC *av_crc04C11DB7;
extern AVCRC *av_crc8005    ;
extern AVCRC *av_crc07      ;
#else
extern AVCRC av_crcEDB88320[];
extern AVCRC av_crc04C11DB7[];
extern AVCRC av_crc8005    [];
extern AVCRC av_crc07      [];
#endif

int av_crc_init(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size);
uint32_t av_crc(const AVCRC *ctx, uint32_t start_crc, const uint8_t *buffer, size_t length);

#endif /* CRC_H */

