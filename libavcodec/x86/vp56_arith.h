/**
 * VP5 and VP6 compatible video decoder (arith decoder)
 *
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2010  Eli Friedman
 *
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

#ifndef AVCODEC_X86_VP56_ARITH_H
#define AVCODEC_X86_VP56_ARITH_H

#if HAVE_FAST_CMOV
#define vp56_rac_get_prob vp56_rac_get_prob
static av_always_inline int vp56_rac_get_prob(VP56RangeCoder *c, uint8_t prob)
{
    unsigned int code_word = vp56_rac_renorm(c);
    unsigned int high = c->high;
    unsigned int low = 1 + (((high - 1) * prob) >> 8);
    unsigned int low_shift = low << 16;
    int bit = 0;

    __asm__(
        "subl  %4, %1      \n\t"
        "subl  %3, %2      \n\t"
        "leal (%2, %3), %3 \n\t"
        "setae %b0         \n\t"
        "cmovb %4, %1      \n\t"
        "cmovb %3, %2      \n\t"
        : "+q"(bit), "+r"(high), "+r"(code_word), "+r"(low_shift)
        : "r"(low)
    );

    c->high      = high;
    c->code_word = code_word;
    return bit;
}
#endif

#endif /* AVCODEC_X86_VP56_ARITH_H */
