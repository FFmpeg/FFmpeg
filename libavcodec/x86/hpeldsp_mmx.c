/*
 * MMX-optimized avg/put pixel routines
 *
 * Copyright (c) 2001 Fabrice Bellard
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

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "dsputil_mmx.h"

#if HAVE_MMX_INLINE

void ff_avg_pixels8_x2_mmx(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    JUMPALIGN();
    do {
        __asm__ volatile(
            "movq  %1, %%mm0            \n\t"
            "movq  1%1, %%mm1           \n\t"
            "movq  %0, %%mm3            \n\t"
            PAVGB_MMX(%%mm0, %%mm1, %%mm2, %%mm6)
            PAVGB_MMX(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, %0            \n\t"
            :"+m"(*block)
            :"m"(*pixels)
            :"memory");
        pixels += line_size;
        block += line_size;
    } while (--h);
}

#endif /* HAVE_MMX_INLINE */
