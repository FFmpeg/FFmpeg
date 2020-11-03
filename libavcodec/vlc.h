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

/**
 * Build VLC decoding tables suitable for use with get_vlc2()
 *
 * This function takes lengths and symbols and calculates the codes from them.
 * For this the input lengths and symbols have to be sorted according to "left
 * nodes in the corresponding tree first".
 *
 * @param[in,out] vlc      The VLC to be initialized; table and table_allocated
 *                         must have been set when initializing a static VLC,
 *                         otherwise this will be treated as uninitialized.
 * @param[in] nb_bits      The number of bits to use for the VLC table;
 *                         higher values take up more memory and cache, but
 *                         allow to read codes with fewer reads.
 * @param[in] nb_codes     The number of provided length and (if supplied) symbol
 *                         entries.
 * @param[in] lens         The lengths of the codes. Entries > 0 correspond to
 *                         valid codes; entries == 0 will be skipped and entries
 *                         with len < 0 indicate that the tree is incomplete and
 *                         has an open end of length -len at this position.
 * @param[in] lens_wrap    Stride (in bytes) of the lengths.
 * @param[in] symbols      The symbols, i.e. what is returned from get_vlc2()
 *                         when the corresponding code is encountered.
 *                         May be NULL, then 0, 1, 2, 3, 4,... will be used.
 * @param[in] symbols_wrap Stride (in bytes) of the symbols.
 * @param[in] symbols_size Size of the symbols. 1 and 2 are supported.
 * @param[in] offset       An offset to apply to all the valid symbols.
 * @param[in] flags        A combination of the INIT_VLC_* flags; notice that
 *                         INIT_VLC_INPUT_LE is pointless and ignored.
 */
int ff_init_vlc_from_lengths(VLC *vlc, int nb_bits, int nb_codes,
                             const int8_t *lens, int lens_wrap,
                             const void *symbols, int symbols_wrap, int symbols_size,
                             int offset, int flags, void *logctx);

void ff_free_vlc(VLC *vlc);

/* If INIT_VLC_INPUT_LE is set, the LSB bit of the codes used to
 * initialize the VLC table is the first bit to be read. */
#define INIT_VLC_INPUT_LE       2
/* If set the VLC is intended for a little endian bitstream reader. */
#define INIT_VLC_OUTPUT_LE      8
#define INIT_VLC_LE             (INIT_VLC_INPUT_LE | INIT_VLC_OUTPUT_LE)
#define INIT_VLC_USE_NEW_STATIC 4
#define INIT_VLC_STATIC_OVERLONG (1 | INIT_VLC_USE_NEW_STATIC)

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

#define INIT_VLC_STATIC_FROM_LENGTHS(vlc, bits, nb_codes, lens, len_wrap,  \
                                     symbols, symbols_wrap, symbols_size,  \
                                     offset, flags, static_size)           \
    do {                                                                   \
        static VLC_TYPE table[static_size][2];                             \
        (vlc)->table           = table;                                    \
        (vlc)->table_allocated = static_size;                              \
        ff_init_vlc_from_lengths(vlc, bits, nb_codes, lens, len_wrap,      \
                                 symbols, symbols_wrap, symbols_size,      \
                                 offset, flags | INIT_VLC_USE_NEW_STATIC,  \
                                 NULL);                                    \
    } while (0)

#endif /* AVCODEC_VLC_H */
