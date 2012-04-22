/*
 * VP8 ARMv6 optimisations
 *
 * Copyright (c) 2010 Rob Clark <rob@ti.com>
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

function ff_vp8_luma_dc_wht_dc_armv6, export=1
        ldrsh           r2,  [r1]
        mov             r3,  #0
        add             r2,  r2,  #3
        strh            r3,  [r1]
        asr             r2,  r2,  #3
    .rept 16
        strh            r2,  [r0], #32
    .endr
        bx              lr
endfunc

function ff_put_vp8_pixels4_armv6, export=1
        ldr             r12, [sp, #0]           @ h
        push            {r4-r6,lr}
1:
        subs            r12, r12, #4
        ldr_post        r4,  r2,  r3
        ldr_post        r5,  r2,  r3
        ldr_post        r6,  r2,  r3
        ldr_post        lr,  r2,  r3
        str_post        r4,  r0,  r1
        str_post        r5,  r0,  r1
        str_post        r6,  r0,  r1
        str_post        lr,  r0,  r1
        bgt             1b
        pop             {r4-r6,pc}
endfunc
