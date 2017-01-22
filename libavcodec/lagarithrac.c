/*
 * Lagarith range decoder
 * Copyright (c) 2009 Nathan Caldwell <saintdev (at) gmail.com>
 * Copyright (c) 2009 David Conrad
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
 * Lagarith range decoder
 * @author Nathan Caldwell
 * @author David Conrad
 */

#include "get_bits.h"
#include "lagarithrac.h"

void ff_lag_rac_init(lag_rac *l, GetBitContext *gb, int length)
{
    int i, j, left;

    /* According to reference decoder "1st byte is garbage",
     * however, it gets skipped by the call to align_get_bits()
     */
    align_get_bits(gb);
    left                = get_bits_left(gb) >> 3;
    l->bytestream_start =
    l->bytestream       = gb->buffer + get_bits_count(gb) / 8;
    l->bytestream_end   = l->bytestream_start + left;

    l->range        = 0x80;
    l->low          = *l->bytestream >> 1;
    l->hash_shift   = FFMAX(l->scale, 10) - 10;

    for (i = j = 0; i < 1024; i++) {
        unsigned r = i << l->hash_shift;
        while (l->prob[j + 1] <= r)
            j++;
        l->range_hash[i] = j;
    }
}
