/*
 * Common functions for the frame{crc,md5} muxers
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

#include "internal.h"

int ff_framehash_write_header(AVFormatContext *s)
{
    int i;

    if (s->nb_streams && !(s->flags & AVFMT_FLAG_BITEXACT))
        avio_printf(s->pb, "#software: %s\n", LIBAVFORMAT_IDENT);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        avio_printf(s->pb, "#tb %d: %d/%d\n", i, st->time_base.num, st->time_base.den);
        avio_flush(s->pb);
    }
    return 0;
}
