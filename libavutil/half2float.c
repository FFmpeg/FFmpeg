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

#include "libavutil/half2float.h"

#if !HAVE_FAST_FLOAT16
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
#endif

void ff_init_half2float_tables(Half2FloatTables *t)
{
#if !HAVE_FAST_FLOAT16
    t->mantissatable[0] = 0;
    for (int i = 1; i < 1024; i++)
        t->mantissatable[i] = convertmantissa(i);
    for (int i = 1024; i < 2048; i++)
        t->mantissatable[i] = 0x38000000UL + ((i - 1024) << 13UL);
    for (int i = 2048; i < 3072; i++)
        t->mantissatable[i] = t->mantissatable[i - 1024] | 0x400000UL;
    t->mantissatable[2048] = t->mantissatable[1024];

    t->exponenttable[0] = 0;
    for (int i = 1; i < 31; i++)
        t->exponenttable[i] = i << 23;
    for (int i = 33; i < 63; i++)
        t->exponenttable[i] = 0x80000000UL + ((i - 32) << 23UL);
    t->exponenttable[31]= 0x47800000UL;
    t->exponenttable[32]= 0x80000000UL;
    t->exponenttable[63]= 0xC7800000UL;

    t->offsettable[0] = 0;
    for (int i = 1; i < 64; i++)
        t->offsettable[i] = 1024;
    t->offsettable[31] = 2048;
    t->offsettable[32] = 0;
    t->offsettable[63] = 2048;
#endif
}
