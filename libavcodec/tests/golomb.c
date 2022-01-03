/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
#include <stdio.h>

#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "libavcodec/get_bits.h"
#include "libavcodec/golomb.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/put_golomb.h"

#define COUNT 8191
#define SIZE (COUNT * 4)

int main(void)
{
    int i, ret = 0;
    uint8_t *temp;
    PutBitContext pb;
    GetBitContext gb;

    temp = av_malloc(SIZE);
    if (!temp)
        return 2;

    init_put_bits(&pb, temp, SIZE);
    for (i = 0; i < COUNT; i++)
        set_ue_golomb(&pb, i);
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8 * SIZE);
    for (i = 0; i < COUNT; i++) {
        int j, s = show_bits(&gb, 25);

        j = get_ue_golomb(&gb);
        if (j != i) {
            fprintf(stderr, "get_ue_golomb: expected %d, got %d. bits: %7x\n",
                    i, j, s);
            ret = 1;
        }
    }

#define EXTEND(i) ((i) << 3 | (i) & 7)
    init_put_bits(&pb, temp, SIZE);
    for (i = 0; i < COUNT; i++)
        set_ue_golomb(&pb, EXTEND(i));
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8 * SIZE);
    for (i = 0; i < COUNT; i++) {
        int j, s = show_bits_long(&gb, 32);

        j = get_ue_golomb_long(&gb);
        if (j != EXTEND(i)) {
            fprintf(stderr, "get_ue_golomb_long: expected %d, got %d. "
                    "bits: %8x\n", EXTEND(i), j, s);
            ret = 1;
        }
    }

#define EXTEND_L(i) ((i) << 4 | (i) & 15)
    init_put_bits(&pb, temp, SIZE);
    for (i = 0; i < COUNT; i++)
        set_ue_golomb_long(&pb, EXTEND_L(i));
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8 * SIZE);
    for (i = 0; i < COUNT; i++) {
        int j, s = show_bits_long(&gb, 32);

        j = get_ue_golomb_long(&gb);
        if (j != EXTEND_L(i)) {
            fprintf(stderr, "get_ue_golomb_long: expected %d, got %d. "
                    "bits: %8x\n", EXTEND_L(i), j, s);
            ret = 1;
        }
    }

    init_put_bits(&pb, temp, SIZE);
    for (i = 0; i < COUNT; i++)
        set_se_golomb(&pb, i - COUNT / 2);
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8 * SIZE);
    for (i = 0; i < COUNT; i++) {
        int j, s = show_bits(&gb, 25);

        j = get_se_golomb(&gb);
        if (j != i - COUNT / 2) {
            fprintf(stderr, "get_se_golomb: expected %d, got %d. bits: %7x\n",
                    i - COUNT / 2, j, s);
            ret = 1;
        }
    }

    av_free(temp);

    return ret;
}
