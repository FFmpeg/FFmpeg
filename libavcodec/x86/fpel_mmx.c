/*
 * MMX-optimized avg/put pixel routines
 *
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "dsputil_x86.h"

#if HAVE_MMX_INLINE

// in case more speed is needed - unrolling would certainly help
void ff_avg_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
             "movq  %0, %%mm0           \n\t"
             "movq  %1, %%mm1           \n\t"
             PAVGB_MMX(%%mm0, %%mm1, %%mm2, %%mm6)
             "movq  %%mm2, %0           \n\t"
             :"+m"(*block)
             :"m"(*pixels)
             :"memory");
        pixels += line_size;
        block += line_size;
    }
    while (--h);
}

void ff_avg_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
             "movq  %0, %%mm0           \n\t"
             "movq  %1, %%mm1           \n\t"
             PAVGB_MMX(%%mm0, %%mm1, %%mm2, %%mm6)
             "movq  %%mm2, %0           \n\t"
             "movq  8%0, %%mm0          \n\t"
             "movq  8%1, %%mm1          \n\t"
             PAVGB_MMX(%%mm0, %%mm1, %%mm2, %%mm6)
             "movq  %%mm2, 8%0          \n\t"
             :"+m"(*block)
             :"m"(*pixels)
             :"memory");
        pixels += line_size;
        block += line_size;
    }
    while (--h);
}

void ff_put_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "lea   (%3, %3), %%"REG_a"      \n\t"
        ".p2align     3                 \n\t"
        "1:                             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq     %%mm0, (%2)           \n\t"
        "movq     %%mm1, (%2, %3)       \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq     %%mm0, (%2)           \n\t"
        "movq     %%mm1, (%2, %3)       \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "subl        $4, %0             \n\t"
        "jnz         1b                 \n\t"
        : "+g"(h), "+r"(pixels),  "+r"(block)
        : "r"((x86_reg)line_size)
        : "%"REG_a, "memory"
        );
}

void ff_put_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "lea   (%3, %3), %%"REG_a"      \n\t"
        ".p2align     3                 \n\t"
        "1:                             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq 8(%1    ), %%mm4          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq 8(%1, %3), %%mm5          \n\t"
        "movq     %%mm0,  (%2)          \n\t"
        "movq     %%mm4, 8(%2)          \n\t"
        "movq     %%mm1,  (%2, %3)      \n\t"
        "movq     %%mm5, 8(%2, %3)      \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq 8(%1    ), %%mm4          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq 8(%1, %3), %%mm5          \n\t"
        "movq     %%mm0,  (%2)          \n\t"
        "movq     %%mm4, 8(%2)          \n\t"
        "movq     %%mm1,  (%2, %3)      \n\t"
        "movq     %%mm5, 8(%2, %3)      \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "subl        $4, %0             \n\t"
        "jnz         1b                 \n\t"
        : "+g"(h), "+r"(pixels),  "+r"(block)
        : "r"((x86_reg)line_size)
        : "%"REG_a, "memory"
        );
}

#endif /* HAVE_MMX_INLINE */
