/*
 * MJPEG decoder VLC code
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
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

#include <stdint.h>
#include "libavutil/avassert.h"
#include "mjpegdec.h"
#include "vlc.h"

static int build_huffman_codes(uint8_t *huff_size, const uint8_t *bits_table)
{
    int nb_codes = 0;
    for (int i = 1, j = 0; i <= 16; i++) {
        nb_codes += bits_table[i];
        av_assert1(nb_codes <= 256);
        for (; j < nb_codes; j++)
            huff_size[j] = i;
    }
    return nb_codes;
}

int ff_mjpeg_build_vlc(VLC *vlc, const uint8_t *bits_table,
                       const uint8_t *val_table, int is_ac, void *logctx)
{
    uint8_t huff_size[256];
    uint16_t huff_sym[256];
    int nb_codes = build_huffman_codes(huff_size, bits_table);

    for (int i = 0; i < nb_codes; i++) {
        huff_sym[i] = val_table[i] + 16 * is_ac;

        if (is_ac && !val_table[i])
            huff_sym[i] = 16 * 256;
    }

    return ff_init_vlc_from_lengths(vlc, 9, nb_codes, huff_size, 1,
                                    huff_sym, 2, 2, 0, 0, logctx);
}
