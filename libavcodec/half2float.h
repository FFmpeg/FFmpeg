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

#ifndef AVCODEC_HALF2FLOAT_H
#define AVCODEC_HALF2FLOAT_H

#include <stdint.h>

static uint32_t convertmantissa(uint32_t i)
{
    int32_t m = i << 13; // Zero pad mantissa bits
    int32_t e = 0; // Zero exponent

    while (!(m & 0x00800000)) { // While not normalized
        e -= 0x00800000; // Decrement exponent (1<<23)
        m <<= 1; // Shift mantissa
    }

    m &= ~0x00800000; // Clear leading 1 bit
    e +=  0x38800000; // Adjust bias ((127-14)<<23)

    return m | e; // Return combined number
}

static void half2float_table(uint32_t *mantissatable, uint32_t *exponenttable,
                             uint16_t *offsettable)
{
    mantissatable[0] = 0;
    for (int i = 1; i < 1024; i++)
        mantissatable[i] = convertmantissa(i);
    for (int i = 1024; i < 2048; i++)
        mantissatable[i] = 0x38000000UL + ((i - 1024) << 13UL);

    exponenttable[0] = 0;
    for (int i = 1; i < 31; i++)
        exponenttable[i] = i << 23;
    for (int i = 33; i < 63; i++)
        exponenttable[i] = 0x80000000UL + ((i - 32) << 23UL);
    exponenttable[31]= 0x47800000UL;
    exponenttable[32]= 0x80000000UL;
    exponenttable[63]= 0xC7800000UL;

    offsettable[0] = 0;
    for (int i = 1; i < 64; i++)
        offsettable[i] = 1024;
    offsettable[32] = 0;
}

static uint32_t half2float(uint16_t h, uint32_t *mantissatable, uint32_t *exponenttable,
                           uint16_t *offsettable)
{
    uint32_t f;

    f = mantissatable[offsettable[h >> 10] + (h & 0x3ff)] + exponenttable[h >> 10];

    return f;
}

#endif /* AVCODEC_HALF2FLOAT_H */
