/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/arm/asm.S"

.macro  call_2x_pixels  type, subp
function ff_\type\()_pixels16\subp\()_armv6, export=1
        push            {r0-r3, lr}
        bl              X(ff_\type\()_pixels8\subp\()_armv6)
        pop             {r0-r3, lr}
        add             r0,  r0,  #8
        add             r1,  r1,  #8
        b               X(ff_\type\()_pixels8\subp\()_armv6)
endfunc
.endm

call_2x_pixels          avg
call_2x_pixels          put, _x2
call_2x_pixels          put, _y2
call_2x_pixels          put, _x2_no_rnd
call_2x_pixels          put, _y2_no_rnd

function ff_put_pixels16_armv6, export=1
        push            {r4-r11}
1:
        ldr             r5,  [r1, #4]
        ldr             r6,  [r1, #8]
        ldr             r7,  [r1, #12]
        ldr_post        r4,  r1,  r2
        strd            r6,  r7,  [r0, #8]
        ldr             r9,  [r1, #4]
        strd_post       r4,  r5,  r0,  r2
        ldr             r10, [r1, #8]
        ldr             r11, [r1, #12]
        ldr_post        r8,  r1,  r2
        strd            r10, r11, [r0, #8]
        subs            r3,  r3,  #2
        strd_post       r8,  r9,  r0,  r2
        bne             1b

        pop             {r4-r11}
        bx              lr
endfunc

function ff_put_pixels8_armv6, export=1
        push            {r4-r7}
1:
        ldr             r5,  [r1, #4]
        ldr_post        r4,  r1,  r2
        ldr             r7,  [r1, #4]
        strd_post       r4,  r5,  r0,  r2
        ldr_post        r6,  r1,  r2
        subs            r3,  r3,  #2
        strd_post       r6,  r7,  r0,  r2
        bne             1b

        pop             {r4-r7}
        bx              lr
endfunc

function ff_put_pixels8_x2_armv6, export=1
        push            {r4-r11, lr}
        mov             r12, #1
        orr             r12, r12, r12, lsl #8
        orr             r12, r12, r12, lsl #16
1:
        ldr             r4,  [r1]
        subs            r3,  r3,  #2
        ldr             r5,  [r1, #4]
        ldr             r7,  [r1, #5]
        lsr             r6,  r4,  #8
        ldr_pre         r8,  r1,  r2
        orr             r6,  r6,  r5,  lsl #24
        ldr             r9,  [r1, #4]
        ldr             r11, [r1, #5]
        lsr             r10, r8,  #8
        add             r1,  r1,  r2
        orr             r10, r10, r9,  lsl #24
        eor             r14, r4,  r6
        uhadd8          r4,  r4,  r6
        eor             r6,  r5,  r7
        uhadd8          r5,  r5,  r7
        and             r14, r14, r12
        and             r6,  r6,  r12
        uadd8           r4,  r4,  r14
        eor             r14, r8,  r10
        uadd8           r5,  r5,  r6
        eor             r6,  r9,  r11
        uhadd8          r8,  r8,  r10
        and             r14, r14, r12
        uhadd8          r9,  r9,  r11
        and             r6,  r6,  r12
        uadd8           r8,  r8,  r14
        strd_post       r4,  r5,  r0,  r2
        uadd8           r9,  r9,  r6
        strd_post       r8,  r9,  r0,  r2
        bne             1b

        pop             {r4-r11, pc}
endfunc

function ff_put_pixels8_y2_armv6, export=1
        push            {r4-r11}
        mov             r12, #1
        orr             r12, r12, r12, lsl #8
        orr             r12, r12, r12, lsl #16
        ldr             r4,  [r1]
        ldr             r5,  [r1, #4]
        ldr_pre         r6,  r1,  r2
        ldr             r7,  [r1, #4]
1:
        subs            r3,  r3,  #2
        uhadd8          r8,  r4,  r6
        eor             r10, r4,  r6
        uhadd8          r9,  r5,  r7
        eor             r11, r5,  r7
        and             r10, r10, r12
        ldr_pre         r4,  r1,  r2
        uadd8           r8,  r8,  r10
        and             r11, r11, r12
        uadd8           r9,  r9,  r11
        ldr             r5,  [r1, #4]
        uhadd8          r10, r4,  r6
        eor             r6,  r4,  r6
        uhadd8          r11, r5,  r7
        and             r6,  r6,  r12
        eor             r7,  r5,  r7
        uadd8           r10, r10, r6
        and             r7,  r7,  r12
        ldrc_pre        ne,  r6,  r1,  r2
        uadd8           r11, r11, r7
        strd_post       r8,  r9,  r0,  r2
        it              ne
        ldrne           r7,  [r1, #4]
        strd_post       r10, r11, r0,  r2
        bne             1b

        pop             {r4-r11}
        bx              lr
endfunc

function ff_put_pixels8_x2_no_rnd_armv6, export=1
        push            {r4-r9, lr}
1:
        subs            r3,  r3,  #2
        ldr             r4,  [r1]
        ldr             r5,  [r1, #4]
        ldr             r7,  [r1, #5]
        ldr_pre         r8,  r1,  r2
        ldr             r9,  [r1, #4]
        ldr             r14, [r1, #5]
        add             r1,  r1,  r2
        lsr             r6,  r4,  #8
        orr             r6,  r6,  r5,  lsl #24
        lsr             r12, r8,  #8
        orr             r12, r12, r9,  lsl #24
        uhadd8          r4,  r4,  r6
        uhadd8          r5,  r5,  r7
        uhadd8          r8,  r8,  r12
        uhadd8          r9,  r9,  r14
        stm             r0,  {r4,r5}
        add             r0,  r0,  r2
        stm             r0,  {r8,r9}
        add             r0,  r0,  r2
        bne             1b

        pop             {r4-r9, pc}
endfunc

function ff_put_pixels8_y2_no_rnd_armv6, export=1
        push            {r4-r9, lr}
        ldr             r4,  [r1]
        ldr             r5,  [r1, #4]
        ldr_pre         r6,  r1,  r2
        ldr             r7,  [r1, #4]
1:
        subs            r3,  r3,  #2
        uhadd8          r8,  r4,  r6
        ldr_pre         r4,  r1,  r2
        uhadd8          r9,  r5,  r7
        ldr             r5,  [r1, #4]
        uhadd8          r12, r4,  r6
        ldrc_pre        ne,  r6,  r1,  r2
        uhadd8          r14, r5,  r7
        it              ne
        ldrne           r7,  [r1, #4]
        stm             r0,  {r8,r9}
        add             r0,  r0,  r2
        stm             r0,  {r12,r14}
        add             r0,  r0,  r2
        bne             1b

        pop             {r4-r9, pc}
endfunc

function ff_avg_pixels8_armv6, export=1
        pld             [r1, r2]
        push            {r4-r10, lr}
        mov             lr,  #1
        orr             lr,  lr,  lr,  lsl #8
        orr             lr,  lr,  lr,  lsl #16
        ldrd            r4,  r5,  [r0]
        ldr             r10, [r1, #4]
        ldr_post        r9,  r1,  r2
        subs            r3,  r3,  #2
1:
        pld             [r1, r2]
        eor             r8,  r4,  r9
        uhadd8          r4,  r4,  r9
        eor             r12, r5,  r10
        ldrd_reg        r6,  r7,  r0,  r2
        uhadd8          r5,  r5,  r10
        and             r8,  r8,  lr
        ldr             r10, [r1, #4]
        and             r12, r12, lr
        uadd8           r4,  r4,  r8
        ldr_post        r9,  r1,  r2
        eor             r8,  r6,  r9
        uadd8           r5,  r5,  r12
        pld             [r1, r2,  lsl #1]
        eor             r12, r7,  r10
        uhadd8          r6,  r6,  r9
        strd_post       r4,  r5,  r0,  r2
        uhadd8          r7,  r7,  r10
        beq             2f
        and             r8,  r8,  lr
        ldrd_reg        r4,  r5,  r0,  r2
        uadd8           r6,  r6,  r8
        ldr             r10, [r1, #4]
        and             r12, r12, lr
        subs            r3,  r3,  #2
        uadd8           r7,  r7,  r12
        ldr_post        r9,  r1,  r2
        strd_post       r6,  r7,  r0,  r2
        b               1b
2:
        and             r8,  r8,  lr
        and             r12, r12, lr
        uadd8           r6,  r6,  r8
        uadd8           r7,  r7,  r12
        strd_post       r6,  r7,  r0,  r2

        pop             {r4-r10, pc}
endfunc
