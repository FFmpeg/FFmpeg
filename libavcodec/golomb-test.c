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

#include <stdint.h>
#include <stdio.h>

#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"
#include "golomb.h"
#include "put_bits.h"

#undef printf
#define COUNT 8000
#define SIZE (COUNT * 40)

int main(void)
{
    int i;
    uint8_t temp[SIZE];
    PutBitContext pb;
    GetBitContext gb;

    init_put_bits(&pb, temp, SIZE);
    printf("testing unsigned exp golomb\n");
    for (i = 0; i < COUNT; i++)
        set_ue_golomb(&pb, i);
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8 * SIZE);
    for (i = 0; i < COUNT; i++) {
        int j, s = show_bits(&gb, 24);

        j = get_ue_golomb(&gb);
        if (j != i)
            printf("mismatch at %d (%d should be %d) bits: %6X\n", i, j, i, s);
    }

    init_put_bits(&pb, temp, SIZE);
    printf("testing signed exp golomb\n");
    for (i = 0; i < COUNT; i++)
        set_se_golomb(&pb, i - COUNT / 2);
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8 * SIZE);
    for (i = 0; i < COUNT; i++) {
        int j, s = show_bits(&gb, 24);

        j = get_se_golomb(&gb);
        if (j != i - COUNT / 2)
            printf("mismatch at %d (%d should be %d) bits: %6X\n", i, j, i, s);
    }

    return 0;
}
