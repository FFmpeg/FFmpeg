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

#ifndef AVCODEC_FLOAT2HALF_H
#define AVCODEC_FLOAT2HALF_H

#include <stdint.h>

static void float2half_tables(uint16_t *basetable, uint8_t *shifttable)
{
    for (int i = 0; i < 256; i++) {
        int e = i - 127;

        if (e < -24) { // Very small numbers map to zero
            basetable[i|0x000]  = 0x0000;
            basetable[i|0x100]  = 0x8000;
            shifttable[i|0x000] = 24;
            shifttable[i|0x100] = 24;
        } else if (e < -14) { // Small numbers map to denorms
            basetable[i|0x000] = (0x0400>>(-e-14));
            basetable[i|0x100] = (0x0400>>(-e-14)) | 0x8000;
            shifttable[i|0x000] = -e-1;
            shifttable[i|0x100] = -e-1;
        } else if (e <= 15) { // Normal numbers just lose precision
            basetable[i|0x000] = ((e + 15) << 10);
            basetable[i|0x100] = ((e + 15) << 10) | 0x8000;
            shifttable[i|0x000] = 13;
            shifttable[i|0x100] = 13;
        } else if (e < 128) { // Large numbers map to Infinity
            basetable[i|0x000]  = 0x7C00;
            basetable[i|0x100]  = 0xFC00;
            shifttable[i|0x000] = 24;
            shifttable[i|0x100] = 24;
        } else { // Infinity and NaN's stay Infinity and NaN's
            basetable[i|0x000]  = 0x7C00;
            basetable[i|0x100]  = 0xFC00;
            shifttable[i|0x000] = 13;
            shifttable[i|0x100] = 13;
        }
    }
}

static uint16_t float2half(uint32_t f, uint16_t *basetable, uint8_t *shifttable)
{
    uint16_t h;

    h = basetable[(f >> 23) & 0x1ff] + ((f & 0x007fffff) >> shifttable[(f >> 23) & 0x1ff]);

    return h;
}

#endif /* AVCODEC_FLOAT2HALF_H */
