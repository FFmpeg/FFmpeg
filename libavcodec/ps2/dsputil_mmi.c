/*
 * MMI optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * MMI optimization by Leon van Stuivenberg <leonvs@iae.nl>
 */

#include "../dsputil.h"
#include "mmi.h"

/* the provided 'as' in binutils 2.9EE doesn't support
the EE's mips3 instructions properly */
#define AS_BUGGY


static void clear_blocks_mmi(DCTELEM * blocks)
{
    int i;
    for (i = 0; i < 6; i++) {
        asm volatile(
        "sq     $0, 0(%0)       \n\t"
        "sq     $0, 16(%0)      \n\t"
        "sq     $0, 32(%0)      \n\t"
        "sq     $0, 48(%0)      \n\t"
        "sq     $0, 64(%0)      \n\t"
        "sq     $0, 80(%0)      \n\t"
        "sq     $0, 96(%0)      \n\t"
        "sq     $0, 112(%0)     \n\t" :: "r" (blocks) : "memory" );
        blocks += 64;
    }
}


static void get_pixels_mmi(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    int i;
    for(i=0;i<8;i++) {
#ifdef AS_BUGGY
        ld3(5, 0, 8);
        asm volatile(
        "add    %1, %1, %2      \n\t"
        "pextlb $8, $0, $8      \n\t"
        "sq     $8, 0(%0)       \n\t" :: "r" (block), "r" (pixels), "r" (line_size) : "$8", "memory" );
#else
        asm volatile(
        "ld     $8, 0(%1)       \n\t"
        "add    %1, %1, %2      \n\t"
        "pextlb $8, $0, $8      \n\t"
        "sq     $8, 0(%0)       \n\t" :: "r" (block), "r" (pixels), "r" (line_size) : "$8", "memory" );
#endif
        block += 8;
    }
}


static void put_pixels8_mmi(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    int i;
    for(i=0; i<h; i++) {
#ifdef AS_BUGGY
        ldr3(5, 0, 8);
        ldl3(5, 7, 8);
        asm volatile ( "add $5, $5, $6 \n\t" );
        sd3(8, 0, 4);
        asm volatile ( "add $4, $4, $6 \n\t" );
#else
        asm volatile(
        "ldr    $8, 0(%1)       \n\t"
        "ldl    $8, 7(%1)       \n\t"
        "add    %1, %1, %2      \n\t"
        "sd     $8, 0(%0)       \n\t"
        "add    %0, %0, %2      \n\t" :: "r" (block), "r" (pixels), "r" (line_size) : "$8", "memory" );
#endif
    }
}


static void put_pixels16_mmi(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    int i;
    for(i=0; i<h; i++) {
#ifdef AS_BUGGY
        ldr3(5, 0, 8);
        ldl3(5, 7, 8);
        ldr3(5, 8, 9);
        ldl3(5, 15, 9);
        asm volatile ( "add $5, $5, $6 \n\t" );
        pcpyld($9, $8, $8);
        sq($8, 0, $4);
        asm volatile ( "add $4, $4, $6 \n\t" );
#else
        asm volatile (
        "ldr    $8, 0(%1)       \n\t"
        "ldl    $8, 7(%1)       \n\t"
        "ldr    $9, 8(%1)       \n\t"
        "ldl    $9, 15(%1)      \n\t"
        "add    %1, %1, %2      \n\t"
        "pcpyld $8, $9, $8      \n\t"
        "sq     $8, 0(%0)       \n\t"
        "add    %0, %0, %2      \n\t" :: "r" (block), "r" (pixels), "r" (line_size) : "$8", "$9", "memory" );
#endif
    }
}


void dsputil_init_mmi(void)
{
    clear_blocks = clear_blocks_mmi;
    
    put_pixels_tab[1][0] = put_pixels8_mmi;
    put_no_rnd_pixels_tab[1][0] = put_pixels8_mmi;
    
    put_pixels_tab[0][0] = put_pixels16_mmi;
    put_no_rnd_pixels_tab[0][0] = put_pixels16_mmi;
    
    get_pixels = get_pixels_mmi;
}

