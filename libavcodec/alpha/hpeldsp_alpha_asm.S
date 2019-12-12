/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

/*
 * These functions are scheduled for pca56. They should work
 * reasonably on ev6, though.
 */

#include "regdef.h"


        .set noat
        .set noreorder
        .arch pca56
        .text

/************************************************************************
 * void put_pixels_axp_asm(uint8_t *block, const uint8_t *pixels,
 *                         int line_size, int h)
 */
        .align 6
        .globl put_pixels_axp_asm
        .ent put_pixels_axp_asm
put_pixels_axp_asm:
        .frame sp, 0, ra
        .prologue 0

        and     a1, 7, t0
        beq     t0, $aligned

        .align 4
$unaligned:
        ldq_u   t0, 0(a1)
        ldq_u   t1, 8(a1)
        addq    a1, a2, a1
        nop

        ldq_u   t2, 0(a1)
        ldq_u   t3, 8(a1)
        addq    a1, a2, a1
        nop

        ldq_u   t4, 0(a1)
        ldq_u   t5, 8(a1)
        addq    a1, a2, a1
        nop

        ldq_u   t6, 0(a1)
        ldq_u   t7, 8(a1)
        extql   t0, a1, t0
        addq    a1, a2, a1

        extqh   t1, a1, t1
        addq    a0, a2, t8
        extql   t2, a1, t2
        addq    t8, a2, t9

        extqh   t3, a1, t3
        addq    t9, a2, ta
        extql   t4, a1, t4
        or      t0, t1, t0

        extqh   t5, a1, t5
        or      t2, t3, t2
        extql   t6, a1, t6
        or      t4, t5, t4

        extqh   t7, a1, t7
        or      t6, t7, t6
        stq     t0, 0(a0)
        stq     t2, 0(t8)

        stq     t4, 0(t9)
        subq    a3, 4, a3
        stq     t6, 0(ta)
        addq    ta, a2, a0

        bne     a3, $unaligned
        ret

        .align 4
$aligned:
        ldq     t0, 0(a1)
        addq    a1, a2, a1
        ldq     t1, 0(a1)
        addq    a1, a2, a1

        ldq     t2, 0(a1)
        addq    a1, a2, a1
        ldq     t3, 0(a1)

        addq    a0, a2, t4
        addq    a1, a2, a1
        addq    t4, a2, t5
        subq    a3, 4, a3

        stq     t0, 0(a0)
        addq    t5, a2, t6
        stq     t1, 0(t4)
        addq    t6, a2, a0

        stq     t2, 0(t5)
        stq     t3, 0(t6)

        bne     a3, $aligned
        ret
        .end put_pixels_axp_asm
