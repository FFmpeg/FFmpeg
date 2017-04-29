/*
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

#ifndef AVCODEC_VLC_H
#define AVCODEC_VLC_H

#include <stdint.h>

#include "bitstream.h"

#define VLC_TYPE int16_t

typedef struct VLC {
    int bits;
    VLC_TYPE (*table)[2]; ///< code, bits
    int table_size, table_allocated;
} VLC;

typedef struct RL_VLC_ELEM {
    int16_t level;
    int8_t len;
    uint8_t run;
} RL_VLC_ELEM;

#define init_vlc(vlc, nb_bits, nb_codes,                \
                 bits, bits_wrap, bits_size,            \
                 codes, codes_wrap, codes_size,         \
                 flags)                                 \
    ff_init_vlc_sparse(vlc, nb_bits, nb_codes,          \
                       bits, bits_wrap, bits_size,      \
                       codes, codes_wrap, codes_size,   \
                       NULL, 0, 0, flags)

int ff_init_vlc_sparse(VLC *vlc, int nb_bits, int nb_codes,
                       const void *bits, int bits_wrap, int bits_size,
                       const void *codes, int codes_wrap, int codes_size,
                       const void *symbols, int symbols_wrap, int symbols_size,
                       int flags);
void ff_free_vlc(VLC *vlc);

#define INIT_VLC_LE             2
#define INIT_VLC_USE_NEW_STATIC 4

#define INIT_VLC_STATIC(vlc, bits, a, b, c, d, e, f, g, static_size)       \
    do {                                                                   \
        static VLC_TYPE table[static_size][2];                             \
        (vlc)->table           = table;                                    \
        (vlc)->table_allocated = static_size;                              \
        init_vlc(vlc, bits, a, b, c, d, e, f, g, INIT_VLC_USE_NEW_STATIC); \
    } while (0)

/* Return the LUT element for the given bitstream configuration. */
static inline int set_idx(BitstreamContext *bc, int code, int *n, int *nb_bits,
                          VLC_TYPE (*table)[2])
{
    unsigned idx;

    *nb_bits = -*n;
    idx = bitstream_peek(bc, *nb_bits) + code;
    *n = table[idx][1];

    return table[idx][0];
}

/**
 * Parse a VLC code.
 * @param bits      is the number of bits which will be read at once, must be
 *                  identical to nb_bits in init_vlc()
 * @param max_depth is the number of times bits bits must be read to completely
 *                  read the longest VLC code
 *                  = (max_vlc_length + bits - 1) / bits
 * If the VLC code is invalid and max_depth = 1, then no bits will be removed.
 * If the VLC code is invalid and max_depth > 1, then the number of bits removed
 * is undefined. */
static inline int bitstream_read_vlc(BitstreamContext *bc, VLC_TYPE (*table)[2],
                                     int bits, int max_depth)
{
    int nb_bits;
    unsigned idx = bitstream_peek(bc, bits);
    int code = table[idx][0];
    int n    = table[idx][1];

    if (max_depth > 1 && n < 0) {
        skip_remaining(bc, bits);
        code = set_idx(bc, code, &n, &nb_bits, table);
        if (max_depth > 2 && n < 0) {
            skip_remaining(bc, nb_bits);
            code = set_idx(bc, code, &n, &nb_bits, table);
        }
    }
    skip_remaining(bc, n);

    return code;
}

#define BITSTREAM_RL_VLC(level, run, bc, table, bits, max_depth) \
    do {                                                         \
        int n, nb_bits;                                          \
        unsigned index = bitstream_peek(bc, bits);               \
        level = table[index].level;                              \
        n     = table[index].len;                                \
                                                                 \
        if (max_depth > 1 && n < 0) {                            \
            bitstream_skip(bc, bits);                            \
                                                                 \
            nb_bits = -n;                                        \
                                                                 \
            index = bitstream_peek(bc, nb_bits) + level;         \
            level = table[index].level;                          \
            n     = table[index].len;                            \
            if (max_depth > 2 && n < 0) {                        \
                bitstream_skip(bc, nb_bits);                     \
                nb_bits = -n;                                    \
                                                                 \
                index = bitstream_peek(bc, nb_bits) + level;     \
                level = table[index].level;                      \
                n     = table[index].len;                        \
            }                                                    \
        }                                                        \
        run = table[index].run;                                  \
        bitstream_skip(bc, n);                                   \
    } while (0)

#endif /* AVCODEC_VLC_H */
