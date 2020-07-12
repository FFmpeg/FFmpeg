/*
 * FFV1 codec
 *
 * Copyright (c) 2003-2013 Michael Niedermayer <michaelni@gmx.at>
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

static inline int RENAME(predict)(TYPE *src, TYPE *last)
{
    const int LT = last[-1];
    const int T  = last[0];
    const int L  = src[-1];

    return mid_pred(L, L + T - LT, T);
}

static inline int RENAME(get_context)(PlaneContext *p, TYPE *src,
                                      TYPE *last, TYPE *last2)
{
    const int LT = last[-1];
    const int T  = last[0];
    const int RT = last[1];
    const int L  = src[-1];

    if (p->quant_table[3][127] || p->quant_table[4][127]) {
        const int TT = last2[0];
        const int LL = src[-2];
        return p->quant_table[0][(L - LT) & 0xFF] +
               p->quant_table[1][(LT - T) & 0xFF] +
               p->quant_table[2][(T - RT) & 0xFF] +
               p->quant_table[3][(LL - L) & 0xFF] +
               p->quant_table[4][(TT - T) & 0xFF];
    } else
        return p->quant_table[0][(L - LT) & 0xFF] +
               p->quant_table[1][(LT - T) & 0xFF] +
               p->quant_table[2][(T - RT) & 0xFF];
}

