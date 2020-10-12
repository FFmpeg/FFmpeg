/*
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

#ifndef AVCODEC_VLC_H
#define AVCODEC_VLC_H

#include <stdint.h>

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

/* If INIT_VLC_INPUT_LE is set, the LSB bit of the codes used to
 * initialize the VLC table is the first bit to be read. */
#define INIT_VLC_INPUT_LE       2
/* If set the VLC is intended for a little endian bitstream reader. */
#define INIT_VLC_OUTPUT_LE      8
#define INIT_VLC_LE             (INIT_VLC_INPUT_LE | INIT_VLC_OUTPUT_LE)
#define INIT_VLC_USE_NEW_STATIC 4

#define INIT_CUSTOM_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g,      \
                                      h, i, j, flags, static_size)         \
    do {                                                                   \
        static VLC_TYPE table[static_size][2];                             \
        (vlc)->table           = table;                                    \
        (vlc)->table_allocated = static_size;                              \
        ff_init_vlc_sparse(vlc, bits, a, b, c, d, e, f, g, h, i, j,        \
                           flags | INIT_VLC_USE_NEW_STATIC);               \
    } while (0)

#define INIT_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g, h, i, j, static_size) \
    INIT_CUSTOM_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g,          \
                                  h, i, j, 0, static_size)

#define INIT_LE_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g, h, i, j, static_size) \
    INIT_CUSTOM_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g,          \
                                  h, i, j, INIT_VLC_LE, static_size)

#define INIT_CUSTOM_VLC_STATIC(vlc, bits, a, b, c, d, e, f, g, flags, static_size) \
    INIT_CUSTOM_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g,          \
                                  NULL, 0, 0, flags, static_size)

#define INIT_VLC_STATIC(vlc, bits, a, b, c, d, e, f, g, static_size)       \
    INIT_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g, NULL, 0, 0, static_size)

#define INIT_LE_VLC_STATIC(vlc, bits, a, b, c, d, e, f, g, static_size) \
    INIT_LE_VLC_SPARSE_STATIC(vlc, bits, a, b, c, d, e, f, g, NULL, 0, 0, static_size)

#endif /* AVCODEC_VLC_H */
