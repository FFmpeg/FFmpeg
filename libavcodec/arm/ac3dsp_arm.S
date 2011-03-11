/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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

#include "asm.S"

function ff_ac3_compute_mantissa_size_arm, export=1
        push            {r4-r8,lr}
        ldm             r0,  {r4-r8}
        mov             r3,  r0
        mov             r0,  #0
1:
        ldrb            lr,  [r1], #1
        subs            r2,  r2,  #1
        blt             2f
        cmp             lr,  #4
        bgt             3f
        subs            lr,  lr,  #1
        addlt           r4,  r4,  #1
        addeq           r5,  r5,  #1
        ble             1b
        subs            lr,  lr,  #2
        addlt           r6,  r6,  #1
        addeq           r7,  r7,  #1
        addgt           r8,  r8,  #1
        b               1b
3:
        cmp             lr,  #14
        sublt           lr,  lr,  #1
        addgt           r0,  r0,  #16
        addle           r0,  r0,  lr
        b               1b
2:
        stm             r3,  {r4-r8}
        pop             {r4-r8,pc}
endfunc
