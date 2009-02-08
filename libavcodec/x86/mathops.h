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

#ifndef AVCODEC_X86_MATHOPS_H
#define AVCODEC_X86_MATHOPS_H

#include "config.h"
#include "libavutil/common.h"

#if ARCH_X86_32
#define MULL(ra, rb, shift) \
        ({ int rt, dummy; __asm__ (\
            "imull %3               \n\t"\
            "shrdl %4, %%edx, %%eax \n\t"\
            : "=a"(rt), "=d"(dummy)\
            : "a" ((int)ra), "rm" ((int)rb), "i"(shift));\
         rt; })

#define MULH(ra, rb) \
    ({ int rt, dummy;\
     __asm__ ("imull %3\n\t" : "=d"(rt), "=a"(dummy): "a" ((int)ra), "rm" ((int)rb));\
     rt; })

#define MUL64(ra, rb) \
    ({ int64_t rt;\
     __asm__ ("imull %2\n\t" : "=A"(rt) : "a" ((int)ra), "g" ((int)rb));\
     rt; })
#endif

#if HAVE_CMOV
/* median of 3 */
#define mid_pred mid_pred
static inline av_const int mid_pred(int a, int b, int c)
{
    int i=b;
    __asm__ volatile(
        "cmp    %2, %1 \n\t"
        "cmovg  %1, %0 \n\t"
        "cmovg  %2, %1 \n\t"
        "cmp    %3, %1 \n\t"
        "cmovl  %3, %1 \n\t"
        "cmp    %1, %0 \n\t"
        "cmovg  %1, %0 \n\t"
        :"+&r"(i), "+&r"(a)
        :"r"(b), "r"(c)
    );
    return i;
}
#endif

#endif /* AVCODEC_X86_MATHOPS_H */
