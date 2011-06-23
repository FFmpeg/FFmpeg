/*
 * Lagarith range decoder
 * Copyright (c) 2009 Nathan Caldwell <saintdev (at) gmail.com>
 * Copyright (c) 2009 David Conrad
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Lagarith range decoder
 * @author Nathan Caldwell
 * @author David Conrad
 */

#ifndef AVCODEC_LAGARITHRAC_H
#define AVCODEC_LAGARITHRAC_H

#include <stdint.h>
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "get_bits.h"

typedef struct lag_rac {
    AVCodecContext *avctx;
    unsigned low;
    unsigned range;
    unsigned scale;             /**< Number of bits of precision in range. */
    unsigned hash_shift;        /**< Number of bits to shift to calculate hash for radix search. */

    const uint8_t *bytestream_start;  /**< Start of input bytestream. */
    const uint8_t *bytestream;        /**< Current position in input bytestream. */
    const uint8_t *bytestream_end;    /**< End position of input bytestream. */

    uint32_t prob[258];         /**< Table of cumulative probability for each symbol. */
    uint8_t  range_hash[256];   /**< Hash table mapping upper byte to approximate symbol. */
} lag_rac;

void lag_rac_init(lag_rac *l, GetBitContext *gb, int length);

/* TODO: Optimize */
static inline void lag_rac_refill(lag_rac *l)
{
    while (l->range <= 0x800000) {
        l->low   <<= 8;
        l->range <<= 8;
        l->low |= 0xff & (AV_RB16(l->bytestream) >> 1);
        if (l->bytestream < l->bytestream_end)
            l->bytestream++;
    }
}

/**
 * Decode a single byte from the compressed plane described by *l.
 * @param l pointer to lag_rac for the current plane
 * @return next byte of decoded data
 */
static inline uint8_t lag_get_rac(lag_rac *l)
{
    unsigned range_scaled, low_scaled, div;
    int val;
    uint8_t shift;

    lag_rac_refill(l);

    range_scaled = l->range >> l->scale;

    if (l->low < range_scaled * l->prob[255]) {
        /* val = 0 is frequent enough to deserve a shortcut */
        if (l->low < range_scaled * l->prob[1]) {
            val = 0;
        } else {
            /* FIXME __builtin_clz is ~20% faster here, but not allowed in generic code. */
            shift = 30 - av_log2(range_scaled);
            div = ((range_scaled << shift) + (1 << 23) - 1) >> 23;
            /* low>>24 ensures that any cases too big for exact FASTDIV are
             * under- rather than over-estimated
             */
            low_scaled = FASTDIV(l->low - (l->low >> 24), div);
            shift -= l->hash_shift;
            shift &= 31;
            low_scaled = (low_scaled << shift) | (low_scaled >> (32 - shift));
            /* low_scaled is now a lower bound of low/range_scaled */
            val = l->range_hash[(uint8_t) low_scaled];
            while (l->low >= range_scaled * l->prob[val + 1])
                val++;
        }

        l->range = range_scaled * (l->prob[val + 1] - l->prob[val]);
    } else {
        val = 255;
        l->range -= range_scaled * l->prob[255];
    }

    l->low -= range_scaled * l->prob[val];

    return val;
}


#endif /* AVCODEC_LAGARITHRAC_H */
