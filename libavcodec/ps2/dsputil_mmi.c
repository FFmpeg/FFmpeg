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

void ff_mmi_idct(DCTELEM * block);

#include "mmi.h"


static void clear_blocks_mmi(DCTELEM * blocks)
{
    /* $4 = blocks */
    int i;
    for (i = 0; i < 6; i++) {
        sq($0, 0, $4);
        sq($0, 16, $4);
        sq($0, 32, $4);
        sq($0, 48, $4);
        sq($0, 64, $4);
        sq($0, 80, $4);
        sq($0, 96, $4);
        sq($0, 112, $4);
        __asm__ __volatile__("addi $4, $4, 128");
    }
}


static void put_pixels_clamped_mmi(const DCTELEM * block, UINT8 * pixels,
				   int line_size)
{
    /* $4 = block, $5 = pixels, $6 = line_size */
    __asm__ __volatile__("li $11, 255":::"$11");
    lq($4, 0, $12);
    pcpyld($11, $11, $11);
    pcpyh($11, $11);

#define PUT(rs) \
    ppacb($0, $##rs, $##rs); \
    sd3(rs, 0, 5); \
    __asm__ __volatile__ ("add $5, $5, $6");

    pminh($12, $11, $12);
    pmaxh($12, $0, $12);
    lq($4, 16, $13);
    PUT(12);

    pminh($13, $11, $13);
    pmaxh($13, $0, $13);
    lq($4, 32, $12);
    PUT(13);

    pminh($12, $11, $12);
    pmaxh($12, $0, $12);
    lq($4, 48, $13);
    PUT(12);

    pminh($13, $11, $13);
    pmaxh($13, $0, $13);
    lq($4, 64, $12);
    PUT(13);

    pminh($12, $11, $12);
    pmaxh($12, $0, $12);
    lq($4, 80, $13);
    PUT(12);

    pminh($13, $11, $13);
    pmaxh($13, $0, $13);
    lq($4, 96, $12);
    PUT(13);

    pminh($12, $11, $12);
    pmaxh($12, $0, $12);
    lq($4, 112, $13);
    PUT(12);

    pminh($13, $11, $13);
    pmaxh($13, $0, $13);
    PUT(13);
}

/* todo
static void add_pixels_clamped_mmi(const DCTELEM * block, UINT8 * pixels,
				   int line_size)
{
}
*/


void dsputil_init_mmi(void)
{
    put_pixels_clamped = put_pixels_clamped_mmi;
    //add_pixels_clamped = add_pixels_clamped_mmi;
    clear_blocks = clear_blocks_mmi;
    ff_idct = ff_mmi_idct;
}
