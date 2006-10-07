/*
 * simple math operations
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at> et al
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

#ifdef FRAC_BITS
#   define MULL(ra, rb) \
        ({ int rt, dummy; asm (\
            "imull %3               \n\t"\
            "shrdl %4, %%edx, %%eax \n\t"\
            : "=a"(rt), "=d"(dummy)\
            : "a" (ra), "rm" (rb), "i"(FRAC_BITS));\
         rt; })
#endif

#define MULH(ra, rb) \
    ({ int rt, dummy;\
     asm ("imull %3\n\t" : "=d"(rt), "=a"(dummy): "a" (ra), "rm" (rb));\
     rt; })

#define MUL64(ra, rb) \
    ({ int64_t rt;\
     asm ("imull %2\n\t" : "=A"(rt) : "a" (ra), "g" (rb));\
     rt; })

