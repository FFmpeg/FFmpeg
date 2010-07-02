/*
 * Chomp bitstream filter
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#include "avcodec.h"
#include "internal.h"

static int chomp_filter(AVBitStreamFilterContext *bsfc,
                        AVCodecContext *avctx, const char *args,
                        uint8_t  **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int      buf_size,
                        int keyframe)
{
    while (buf_size > 0 && !buf[buf_size-1])
        buf_size--;

    *poutbuf = (uint8_t*) buf;
    *poutbuf_size = buf_size;

    return 0;
}

/**
 * This filter removes a string of NULL bytes from the end of a packet.
 */
AVBitStreamFilter chomp_bsf = {
    "chomp",
    0,
    chomp_filter,
};
