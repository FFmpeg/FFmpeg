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

#include "libavutil/float2half.h"

void ff_init_float2half_tables(Float2HalfTables *t)
{
#if !HAVE_FAST_FLOAT16
    for (int i = 0; i < 256; i++) {
        int e = i - 127;

        if (e < -24) { // Very small numbers map to zero
            t->basetable[i|0x000]  = 0x0000;
            t->basetable[i|0x100]  = 0x8000;
            t->shifttable[i|0x000] = 24;
            t->shifttable[i|0x100] = 24;
        } else if (e < -14) { // Small numbers map to denorms
            t->basetable[i|0x000] = (0x0400>>(-e-14));
            t->basetable[i|0x100] = (0x0400>>(-e-14)) | 0x8000;
            t->shifttable[i|0x000] = -e-1;
            t->shifttable[i|0x100] = -e-1;
        } else if (e <= 15) { // Normal numbers just lose precision
            t->basetable[i|0x000] = ((e + 15) << 10);
            t->basetable[i|0x100] = ((e + 15) << 10) | 0x8000;
            t->shifttable[i|0x000] = 13;
            t->shifttable[i|0x100] = 13;
        } else if (e < 128) { // Large numbers map to Infinity
            t->basetable[i|0x000]  = 0x7C00;
            t->basetable[i|0x100]  = 0xFC00;
            t->shifttable[i|0x000] = 24;
            t->shifttable[i|0x100] = 24;
        } else { // Infinity and NaN's stay Infinity and NaN's
            t->basetable[i|0x000]  = 0x7C00;
            t->basetable[i|0x100]  = 0xFC00;
            t->shifttable[i|0x000] = 13;
            t->shifttable[i|0x100] = 13;
        }
    }
#endif
}
