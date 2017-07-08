/*
 * Copyright (c) 2016 Alexandra Hájková
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
 * functions for reading bits from a buffer
 */

#ifndef AVCODEC_BITSTREAM_H
#define AVCODEC_BITSTREAM_H

#include <stdint.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"

#include "mathops.h"

typedef struct BitstreamContext {
    uint64_t bits;      // stores bits read from the buffer
    const uint8_t *buffer, *buffer_end;
    const uint8_t *ptr; // position inside a buffer
    unsigned bits_left; // number of bits left in bits field
    unsigned size_in_bits;
} BitstreamContext;

static inline void refill_64(BitstreamContext *bc)
{
    if (bc->ptr >= bc->buffer_end)
        return;

#ifdef BITSTREAM_READER_LE
    bc->bits       = AV_RL64(bc->ptr);
#else
    bc->bits       = AV_RB64(bc->ptr);
#endif
    bc->ptr       += 8;
    bc->bits_left  = 64;
}

static inline void refill_32(BitstreamContext *bc)
{
    if (bc->ptr >= bc->buffer_end)
        return;

#ifdef BITSTREAM_READER_LE
    bc->bits       = (uint64_t)AV_RL32(bc->ptr) << bc->bits_left | bc->bits;
#else
    bc->bits       = bc->bits | (uint64_t)AV_RB32(bc->ptr) << (32 - bc->bits_left);
#endif
    bc->ptr       += 4;
    bc->bits_left += 32;
}

/* Initialize BitstreamContext. Input buffer must have an additional zero
 * padding of AV_INPUT_BUFFER_PADDING_SIZE bytes at the end. */
static inline int bitstream_init(BitstreamContext *bc, const uint8_t *buffer,
                                 unsigned bit_size)
{
    unsigned buffer_size;

    if (bit_size > INT_MAX - 7 || !buffer) {
        buffer        =
        bc->buffer    =
        bc->ptr       = NULL;
        bc->bits_left = 0;
        return AVERROR_INVALIDDATA;
    }

    buffer_size = (bit_size + 7) >> 3;

    bc->buffer       = buffer;
    bc->buffer_end   = buffer + buffer_size;
    bc->ptr          = bc->buffer;
    bc->size_in_bits = bit_size;
    bc->bits_left    = 0;
    bc->bits         = 0;

    refill_64(bc);

    return 0;
}

/* Initialize BitstreamContext with buffer size in bytes instead of bits. */
static inline int bitstream_init8(BitstreamContext *bc, const uint8_t *buffer,
                                  unsigned byte_size)
{
    if (byte_size > INT_MAX / 8)
        return AVERROR_INVALIDDATA;
    return bitstream_init(bc, buffer, byte_size * 8);
}

/* Return number of bits already read. */
static inline int bitstream_tell(const BitstreamContext *bc)
{
    return (bc->ptr - bc->buffer) * 8 - bc->bits_left;
}

/* Return buffer size in bits. */
static inline int bitstream_tell_size(const BitstreamContext *bc)
{
    return bc->size_in_bits;
}

/* Return the number of the bits left in a buffer. */
static inline int bitstream_bits_left(const BitstreamContext *bc)
{
    return (bc->buffer - bc->ptr) * 8 + bc->size_in_bits + bc->bits_left;
}

static inline uint64_t get_val(BitstreamContext *bc, unsigned n)
{
#ifdef BITSTREAM_READER_LE
    uint64_t ret = bc->bits & ((UINT64_C(1) << n) - 1);
    bc->bits >>= n;
#else
    uint64_t ret = bc->bits >> (64 - n);
    bc->bits <<= n;
#endif
    bc->bits_left -= n;

    return ret;
}

/* Return one bit from the buffer. */
static inline unsigned bitstream_read_bit(BitstreamContext *bc)
{
    if (!bc->bits_left)
        refill_64(bc);

    return get_val(bc, 1);
}

/* Return n bits from the buffer. n has to be in the 0-63 range. */
static inline uint64_t bitstream_read_63(BitstreamContext *bc, unsigned n)
{
    uint64_t ret = 0;
#ifdef BITSTREAM_READER_LE
    uint64_t left = 0;
#endif

    if (!n)
        return 0;

    if (n > bc->bits_left) {
        n -= bc->bits_left;
#ifdef BITSTREAM_READER_LE
        left = bc->bits_left;
#endif
        ret = get_val(bc, bc->bits_left);
        refill_64(bc);
    }

#ifdef BITSTREAM_READER_LE
    ret = get_val(bc, n) << left | ret;
#else
    ret = get_val(bc, n) | ret << n;
#endif

    return ret;
}

/* Return n bits from the buffer. n has to be in the 0-32 range. */
static inline uint32_t bitstream_read(BitstreamContext *bc, unsigned n)
{
    if (!n)
        return 0;

    if (n > bc->bits_left) {
        refill_32(bc);
        if (bc->bits_left < 32)
            bc->bits_left = n;
    }

    return get_val(bc, n);
}

/* Return n bits from the buffer as a signed integer.
 * n has to be in the 0-32 range. */
static inline int32_t bitstream_read_signed(BitstreamContext *bc, unsigned n)
{
    return sign_extend(bitstream_read(bc, n), n);
}

static inline unsigned show_val(const BitstreamContext *bc, unsigned n)
{
#ifdef BITSTREAM_READER_LE
    return bc->bits & ((UINT64_C(1) << n) - 1);
#else
    return bc->bits >> (64 - n);
#endif
}

/* Return n bits from the buffer, but do not change the buffer state.
 * n has to be in the 0-32 range. */
static inline unsigned bitstream_peek(BitstreamContext *bc, unsigned n)
{
    if (n > bc->bits_left)
        refill_32(bc);

    return show_val(bc, n);
}

/* Return n bits from the buffer as a signed integer, but do not change the
 * buffer state. n has to be in the 0-32 range. */
static inline int bitstream_peek_signed(BitstreamContext *bc, unsigned n)
{
    return sign_extend(bitstream_peek(bc, n), n);
}

static inline void skip_remaining(BitstreamContext *bc, unsigned n)
{
#ifdef BITSTREAM_READER_LE
    bc->bits >>= n;
#else
    bc->bits <<= n;
#endif
    bc->bits_left -= n;
}

/* Skip n bits in the buffer. */
static inline void bitstream_skip(BitstreamContext *bc, unsigned n)
{
    if (n < bc->bits_left)
        skip_remaining(bc, n);
    else {
        n -= bc->bits_left;
        bc->bits      = 0;
        bc->bits_left = 0;

        if (n >= 64) {
            unsigned skip = n / 8;

            n -= skip * 8;
            bc->ptr += skip;
        }
        refill_64(bc);
        if (n)
            skip_remaining(bc, n);
    }
}

/* Seek to the given bit position. */
static inline void bitstream_seek(BitstreamContext *bc, unsigned pos)
{
    bc->ptr       = bc->buffer;
    bc->bits      = 0;
    bc->bits_left = 0;

    bitstream_skip(bc, pos);
}

/* Skip bits to a byte boundary. */
static inline const uint8_t *bitstream_align(BitstreamContext *bc)
{
    unsigned n = -bitstream_tell(bc) & 7;
    if (n)
        bitstream_skip(bc, n);
    return bc->buffer + (bitstream_tell(bc) >> 3);
}

/* Read MPEG-1 dc-style VLC (sign bit + mantissa with no MSB).
 * If MSB not set it is negative. */
static inline int bitstream_read_xbits(BitstreamContext *bc, unsigned length)
{
    int32_t cache = bitstream_peek(bc, 32);
    int sign = ~cache >> 31;
    skip_remaining(bc, length);

    return ((((uint32_t)(sign ^ cache)) >> (32 - length)) ^ sign) - sign;
}

/* Return decoded truncated unary code for the values 0, 1, 2. */
static inline int bitstream_decode012(BitstreamContext *bc)
{
    if (!bitstream_read_bit(bc))
        return 0;
    else
        return bitstream_read_bit(bc) + 1;
}

/* Return decoded truncated unary code for the values 2, 1, 0. */
static inline int bitstream_decode210(BitstreamContext *bc)
{
    if (bitstream_read_bit(bc))
        return 0;
    else
        return 2 - bitstream_read_bit(bc);
}

/* Read sign bit and flip the sign of the provided value accordingly. */
static inline int bitstream_apply_sign(BitstreamContext *bc, int val)
{
    int sign = bitstream_read_signed(bc, 1);
    return (val ^ sign) - sign;
}

/* Unwind the cache so a refill_32 can fill it again. */
static inline void bitstream_unwind(BitstreamContext *bc)
{
    int unwind = 4;
    int unwind_bits = unwind * 8;

    if (bc->bits_left < unwind_bits)
        return;

    bc->bits      >>= unwind_bits;
    bc->bits      <<= unwind_bits;
    bc->bits_left  -= unwind_bits;
    bc->ptr        -= unwind;
}

/* Unget up to 32 bits. */
static inline void bitstream_unget(BitstreamContext *bc, uint64_t value,
                                   size_t amount)
{
    size_t cache_size = sizeof(bc->bits) * 8;

    if (bc->bits_left + amount > cache_size)
        bitstream_unwind(bc);

    bc->bits       = (bc->bits >> amount) | (value << (cache_size - amount));
    bc->bits_left += amount;
}

#endif /* AVCODEC_BITSTREAM_H */
