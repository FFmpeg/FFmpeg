/*
 * Common bit i/o utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2010 Loren Merritt
 *
 * alternative bitstream reader & writer by Michael Niedermayer <michaelni@gmx.at>
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
 * bitstream api.
 */

#include <stdint.h>
#include <string.h>

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "put_bits.h"

void ff_put_string(PutBitContext *pb, const char *string, int terminate_string)
{
    while (*string) {
        put_bits(pb, 8, *string);
        string++;
    }
    if (terminate_string)
        put_bits(pb, 8, 0);
}

void ff_copy_bits(PutBitContext *pb, const uint8_t *src, int length)
{
    int words = length >> 4;
    int bits  = length & 15;
    int i;

    if (length == 0)
        return;

    av_assert0(length <= put_bits_left(pb));

    if (CONFIG_SMALL || words < 16 || put_bits_count(pb) & 7) {
        for (i = 0; i < words; i++)
            put_bits(pb, 16, AV_RB16(src + 2 * i));
    } else {
        for (i = 0; put_bits_count(pb) & 31; i++)
            put_bits(pb, 8, src[i]);
        flush_put_bits(pb);
        memcpy(put_bits_ptr(pb), src + i, 2 * words - i);
        skip_put_bytes(pb, 2 * words - i);
    }

    put_bits(pb, bits, AV_RB16(src + 2 * words) >> (16 - bits));
}
