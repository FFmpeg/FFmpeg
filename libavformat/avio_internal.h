/*
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

#ifndef AVFORMAT_AVIO_INTERNAL_H
#define AVFORMAT_AVIO_INTERNAL_H

#include "avio.h"

int ffio_init_context(AVIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence));


/**
 * Read size bytes from AVIOContext into buf.
 * This reads at most 1 packet. If that is not enough fewer bytes will be
 * returned.
 * @return number of bytes read or AVERROR
 */
int ffio_read_partial(AVIOContext *s, unsigned char *buf, int size);

void ffio_fill(AVIOContext *s, int b, int count);

#define ffio_wfourcc(pb, str) avio_wl32(pb, MKTAG((str)[0], (str)[1], (str)[2], (str)[3]))

#endif // AVFORMAT_AVIO_INTERNAL_H
