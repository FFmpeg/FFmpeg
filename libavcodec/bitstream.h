/*
 * Copyright (c) 2016 Alexandra Hájková
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
 * bitstream reader API header.
 */

#ifndef AVCODEC_BITSTREAM_H
#define AVCODEC_BITSTREAM_H

#include <stdint.h>

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"

#include "mathops.h"
#include "vlc.h"

#ifndef UNCHECKED_BITSTREAM_READER
#define UNCHECKED_BITSTREAM_READER !CONFIG_SAFE_BITSTREAM_READER
#endif

typedef struct BitstreamContext {
    uint64_t bits;       // stores bits read from the buffer
    const uint8_t *buffer, *buffer_end;
    const uint8_t *ptr;  // pointer to the position inside a buffer
    unsigned bits_valid; // number of bits left in bits field
    unsigned size_in_bits;
} BitstreamContext;

/**
 * @return
 * - 0 on successful refill
 * - a negative number when bitstream end is hit
 *
 * Always succeeds when UNCHECKED_BITSTREAM_READER is enabled.
 */
static inline int bits_priv_refill_64(BitstreamContext *bc)
{
#if !UNCHECKED_BITSTREAM_READER
    if (bc->ptr >= bc->buffer_end)
        return -1;
#endif

#ifdef BITSTREAM_READER_LE
    bc->bits       = AV_RL64(bc->ptr);
#else
    bc->bits       = AV_RB64(bc->ptr);
#endif
    bc->ptr       += 8;
    bc->bits_valid = 64;

    return 0;
}

/**
 * @return
 * - 0 on successful refill
 * - a negative number when bitstream end is hit
 *
 * Always succeeds when UNCHECKED_BITSTREAM_READER is enabled.
 */
static inline int bits_priv_refill_32(BitstreamContext *bc)
{
#if !UNCHECKED_BITSTREAM_READER
    if (bc->ptr >= bc->buffer_end)
        return -1;
#endif

#ifdef BITSTREAM_READER_LE
    bc->bits      |= (uint64_t)AV_RL32(bc->ptr) << bc->bits_valid;
#else
    bc->bits      |= (uint64_t)AV_RB32(bc->ptr) << (32 - bc->bits_valid);
#endif
    bc->ptr        += 4;
    bc->bits_valid += 32;

    return 0;
}

/**
 * Initialize BitstreamContext.
 * @param buffer bitstream buffer, must be AV_INPUT_BUFFER_PADDING_SIZE bytes
 *        larger than the actual read bits because some optimized bitstream
 *        readers read 32 or 64 bits at once and could read over the end
 * @param bit_size the size of the buffer in bits
 * @return 0 on success, AVERROR_INVALIDDATA if the buffer_size would overflow.
 */
static inline int bits_init(BitstreamContext *bc, const uint8_t *buffer,
                            unsigned int bit_size)
{
    unsigned int buffer_size;

    if (bit_size > INT_MAX - 7 || !buffer) {
        bc->buffer     = NULL;
        bc->ptr        = NULL;
        bc->bits_valid = 0;
        return AVERROR_INVALIDDATA;
    }

    buffer_size = (bit_size + 7) >> 3;

    bc->buffer       = buffer;
    bc->buffer_end   = buffer + buffer_size;
    bc->ptr          = bc->buffer;
    bc->size_in_bits = bit_size;
    bc->bits_valid   = 0;
    bc->bits         = 0;

    bits_priv_refill_64(bc);

    return 0;
}

/**
 * Initialize BitstreamContext.
 * @param buffer bitstream buffer, must be AV_INPUT_BUFFER_PADDING_SIZE bytes
 *        larger than the actual read bits because some optimized bitstream
 *        readers read 32 or 64 bits at once and could read over the end
 * @param byte_size the size of the buffer in bytes
 * @return 0 on success, AVERROR_INVALIDDATA if the buffer_size would overflow
 */
static inline int bits_init8(BitstreamContext *bc, const uint8_t *buffer,
                             unsigned int byte_size)
{
    if (byte_size > INT_MAX / 8)
        return AVERROR_INVALIDDATA;
    return bits_init(bc, buffer, byte_size * 8);
}

/**
 * Return number of bits already read.
 */
static inline int bits_tell(const BitstreamContext *bc)
{
    return (bc->ptr - bc->buffer) * 8 - bc->bits_valid;
}

/**
 * Return buffer size in bits.
 */
static inline int bits_size(const BitstreamContext *bc)
{
    return bc->size_in_bits;
}

/**
 * Return the number of the bits left in a buffer.
 */
static inline int bits_left(const BitstreamContext *bc)
{
    return (bc->buffer - bc->ptr) * 8 + bc->size_in_bits + bc->bits_valid;
}

static inline uint64_t bits_priv_val_show(BitstreamContext *bc, unsigned int n)
{
    av_assert2(n > 0 && n <= 64);

#ifdef BITSTREAM_READER_LE
    return bc->bits & (UINT64_MAX >> (64 - n));
#else
    return bc->bits >> (64 - n);
#endif
}

static inline void bits_priv_skip_remaining(BitstreamContext *bc, unsigned int n)
{
#ifdef BITSTREAM_READER_LE
    bc->bits >>= n;
#else
    bc->bits <<= n;
#endif
    bc->bits_valid -= n;
}

static inline uint64_t bits_priv_val_get(BitstreamContext *bc, unsigned int n)
{
    uint64_t ret;

    av_assert2(n > 0 && n < 64);

    ret = bits_priv_val_show(bc, n);
    bits_priv_skip_remaining(bc, n);

    return ret;
}

/**
 * Return one bit from the buffer.
 */
static inline unsigned int bits_read_bit(BitstreamContext *bc)
{
    if (!bc->bits_valid && bits_priv_refill_64(bc) < 0)
        return 0;

    return bits_priv_val_get(bc, 1);
}

/**
 * Return n bits from the buffer, n has to be in the 1-32 range.
 * May be faster than bits_read() when n is not a compile-time constant and is
 * known to be non-zero;
 */
static inline uint32_t bits_read_nz(BitstreamContext *bc, unsigned int n)
{
    av_assert2(n > 0 && n <= 32);

    if (n > bc->bits_valid) {
        if (bits_priv_refill_32(bc) < 0)
            bc->bits_valid = n;
    }

    return bits_priv_val_get(bc, n);
}

/**
 * Return n bits from the buffer, n has to be in the 0-32  range.
 */
static inline uint32_t bits_read(BitstreamContext *bc, unsigned int n)
{
    av_assert2(n <= 32);

    if (!n)
        return 0;

    return bits_read_nz(bc, n);
}

/**
 * Return n bits from the buffer, n has to be in the 0-63 range.
 */
static inline uint64_t bits_read_63(BitstreamContext *bc, unsigned int n)
{
    uint64_t ret = 0;
    unsigned left = 0;

    av_assert2(n <= 63);

    if (!n)
        return 0;

    if (n > bc->bits_valid) {
        left = bc->bits_valid;
        n   -= left;

        if (left)
            ret = bits_priv_val_get(bc, left);

        if (bits_priv_refill_64(bc) < 0)
            bc->bits_valid = n;

    }

#ifdef BITSTREAM_READER_LE
    ret = bits_priv_val_get(bc, n) << left | ret;
#else
    ret = bits_priv_val_get(bc, n) | ret << n;
#endif

    return ret;
}

/**
 * Return n bits from the buffer, n has to be in the 0-64 range.
 */
static inline uint64_t bits_read_64(BitstreamContext *bc, unsigned int n)
{
    av_assert2(n <= 64);

    if (n == 64) {
        uint64_t ret = bits_read_63(bc, 63);
#ifdef BITSTREAM_READER_LE
        return ret | ((uint64_t)bits_read_bit(bc) << 63);
#else
        return (ret << 1) | (uint64_t)bits_read_bit(bc);
#endif
    }
    return bits_read_63(bc, n);
}

/**
 * Return n bits from the buffer as a signed integer.
 * n has to be in the 0-32 range.
 */
static inline int32_t bits_read_signed(BitstreamContext *bc, unsigned int n)
{
    return sign_extend(bits_read(bc, n), n);
}

/**
 * Return n bits from the buffer but do not change the buffer state.
 * n has to be in the 1-32 range. May
 */
static inline uint32_t bits_peek_nz(BitstreamContext *bc, unsigned int n)
{
    av_assert2(n > 0 && n <= 32);

    if (n > bc->bits_valid)
        bits_priv_refill_32(bc);

    return bits_priv_val_show(bc, n);
}

/**
 * Return n bits from the buffer but do not change the buffer state.
 * n has to be in the 0-32 range.
 */
static inline uint32_t bits_peek(BitstreamContext *bc, unsigned int n)
{
    av_assert2(n <= 32);

    if (!n)
        return 0;

    return bits_peek_nz(bc, n);
}

/**
 * Return n bits from the buffer as a signed integer,
 * do not change the buffer state.
 * n has to be in the 0-32 range.
 */
static inline int bits_peek_signed(BitstreamContext *bc, unsigned int n)
{
    return sign_extend(bits_peek(bc, n), n);
}

/**
 * Skip n bits in the buffer.
 */
static inline void bits_skip(BitstreamContext *bc, unsigned int n)
{
    if (n < bc->bits_valid)
        bits_priv_skip_remaining(bc, n);
    else {
        n -= bc->bits_valid;
        bc->bits       = 0;
        bc->bits_valid = 0;

        if (n >= 64) {
            unsigned int skip = n / 8;

            n -= skip * 8;
            bc->ptr += skip;
        }
        bits_priv_refill_64(bc);
        if (n)
            bits_priv_skip_remaining(bc, n);
    }
}

/**
 * Seek to the given bit position.
 */
static inline void bits_seek(BitstreamContext *bc, unsigned pos)
{
    bc->ptr        = bc->buffer;
    bc->bits       = 0;
    bc->bits_valid = 0;

    bits_skip(bc, pos);
}

/**
 * Skip bits to a byte boundary.
 */
static inline const uint8_t *bits_align(BitstreamContext *bc)
{
    unsigned int n = -bits_tell(bc) & 7;
    if (n)
        bits_skip(bc, n);
    return bc->buffer + (bits_tell(bc) >> 3);
}

/**
 * Read MPEG-1 dc-style VLC (sign bit + mantissa with no MSB).
 * If MSB not set it is negative.
 * @param n length in bits
 */
static inline int bits_read_xbits(BitstreamContext *bc, unsigned int n)
{
    int32_t cache = bits_peek(bc, 32);
    int sign = ~cache >> 31;
    bits_priv_skip_remaining(bc, n);

    return ((((uint32_t)(sign ^ cache)) >> (32 - n)) ^ sign) - sign;
}

/**
 * Return decoded truncated unary code for the values 0, 1, 2.
 */
static inline int bits_decode012(BitstreamContext *bc)
{
    if (!bits_read_bit(bc))
        return 0;
    else
        return bits_read_bit(bc) + 1;
}

/**
 * Return decoded truncated unary code for the values 2, 1, 0.
 */
static inline int bits_decode210(BitstreamContext *bc)
{
    if (bits_read_bit(bc))
        return 0;
    else
        return 2 - bits_read_bit(bc);
}

/* Read sign bit and flip the sign of the provided value accordingly. */
static inline int bits_apply_sign(BitstreamContext *bc, int val)
{
    int sign = bits_read_signed(bc, 1);
    return (val ^ sign) - sign;
}

static inline int bits_skip_1stop_8data(BitstreamContext *s)
{
    if (bits_left(s) <= 0)
        return AVERROR_INVALIDDATA;

    while (bits_read_bit(s)) {
        bits_skip(s, 8);
        if (bits_left(s) <= 0)
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 * Return the LUT element for the given bitstream configuration.
 */
static inline int bits_priv_set_idx(BitstreamContext *bc, int code, int *n, int *nb_bits,
                                    const VLCElem *table)
{
    unsigned idx;

    *nb_bits = -*n;
    idx = bits_peek(bc, *nb_bits) + code;
    *n = table[idx].len;

    return table[idx].sym;
}

/**
 * Parse a vlc code.
 * @param bits is the number of bits which will be read at once, must be
 *             identical to nb_bits in init_vlc()
 * @param max_depth is the number of times bits bits must be read to completely
 *                  read the longest vlc code
 *                  = (max_vlc_length + bits - 1) / bits
 * If the vlc code is invalid and max_depth=1, then no bits will be removed.
 * If the vlc code is invalid and max_depth>1, then the number of bits removed
 * is undefined.
 */
static inline int bits_read_vlc(BitstreamContext *bc, const VLCElem *table,
                                int bits, int max_depth)
{
    int nb_bits;
    unsigned idx = bits_peek(bc, bits);
    int code     = table[idx].sym;
    int n        = table[idx].len;

    if (max_depth > 1 && n < 0) {
        bits_priv_skip_remaining(bc, bits);
        code = bits_priv_set_idx(bc, code, &n, &nb_bits, table);
        if (max_depth > 2 && n < 0) {
            bits_priv_skip_remaining(bc, nb_bits);
            code = bits_priv_set_idx(bc, code, &n, &nb_bits, table);
        }
    }
    bits_priv_skip_remaining(bc, n);

    return code;
}

#define BITS_RL_VLC(level, run, bc, table, bits, max_depth) \
    do {                                                    \
        int n, nb_bits;                                     \
        unsigned int index = bits_peek(bc, bits);           \
        level = table[index].level;                         \
        n     = table[index].len;                           \
                                                            \
        if (max_depth > 1 && n < 0) {                       \
            bits_skip(bc, bits);                            \
                                                            \
            nb_bits = -n;                                   \
                                                            \
            index = bits_peek(bc, nb_bits) + level;         \
            level = table[index].level;                     \
            n     = table[index].len;                       \
            if (max_depth > 2 && n < 0) {                   \
                bits_skip(bc, nb_bits);                     \
                nb_bits = -n;                               \
                                                            \
                index = bits_peek(bc, nb_bits) + level;     \
                level = table[index].level;                 \
                n     = table[index].len;                   \
            }                                               \
        }                                                   \
        run = table[index].run;                             \
        bits_skip(bc, n);                                   \
    } while (0)

#endif /* AVCODEC_BITSTREAM_H */
