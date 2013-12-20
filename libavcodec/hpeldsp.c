/*
 * Half-pel DSP functions.
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * Half-pel DSP functions.
 */

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "hpeldsp.h"

#define BIT_DEPTH 8
#include "hpeldsp_template.c"

av_cold void ff_hpeldsp_init(HpelDSPContext *c, int flags)
{
#define hpel_funcs(prefix, idx, num) \
    c->prefix ## _pixels_tab idx [0] = prefix ## _pixels ## num ## _8_c; \
    c->prefix ## _pixels_tab idx [1] = prefix ## _pixels ## num ## _x2_8_c; \
    c->prefix ## _pixels_tab idx [2] = prefix ## _pixels ## num ## _y2_8_c; \
    c->prefix ## _pixels_tab idx [3] = prefix ## _pixels ## num ## _xy2_8_c

    hpel_funcs(put, [0], 16);
    hpel_funcs(put, [1],  8);
    hpel_funcs(put, [2],  4);
    hpel_funcs(put, [3],  2);
    hpel_funcs(put_no_rnd, [0], 16);
    hpel_funcs(put_no_rnd, [1],  8);
    hpel_funcs(avg, [0], 16);
    hpel_funcs(avg, [1],  8);
    hpel_funcs(avg, [2],  4);
    hpel_funcs(avg, [3],  2);
    hpel_funcs(avg_no_rnd,, 16);

    if (ARCH_AARCH64)
        ff_hpeldsp_init_aarch64(c, flags);
    if (ARCH_ARM)
        ff_hpeldsp_init_arm(c, flags);
    if (ARCH_BFIN)
        ff_hpeldsp_init_bfin(c, flags);
    if (ARCH_PPC)
        ff_hpeldsp_init_ppc(c, flags);
    if (HAVE_VIS)
        ff_hpeldsp_init_vis(c, flags);
    if (ARCH_X86)
        ff_hpeldsp_init_x86(c, flags);
}
