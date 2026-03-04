/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#include "asm.S"

function ff_pixelutils_sad16_armv6, export=1
        push            {r4-r10, lr}
        mov             r12, #0
        mov             r10, #16
        mov             lr,  #0
        ldm             r0,  {r4-r7}
        ldr             r8,  [r2]
1:
        ldr             r9,  [r2, #4]
        pld             [r0, r1]
        usada8          r12, r4,  r8,  r12
        ldr             r8,  [r2, #8]
        pld             [r2, r3]
        usada8          lr,  r5,  r9,  lr
        ldr             r9,  [r2, #12]
        usada8          r12, r6,  r8,  r12
        subs            r10,  r10,  #1
        usada8          lr,  r7,  r9,  lr
        beq             2f
        add             r0,  r0,  r1
        ldm             r0,  {r4-r7}
        add             r2,  r2,  r3
        ldr             r8,  [r2]
        b               1b
2:
        add             r0,  r12, lr
        pop             {r4-r10, pc}
endfunc

function ff_pixelutils_sad8_armv6, export=1
        pld             [r2, r3]
        push            {r4-r10, lr}
        mov             r10, #8
        mov             r12,  #0
        mov             lr,  #0
        ldrd_post       r4,  r5,  r0,  r1
1:
        subs            r10, r10, #2
        ldr             r7,  [r2, #4]
        ldr_post        r6,  r2,  r3
        ldrd_post       r8,  r9,  r0,  r1
        usada8          r12,  r4,  r6,  r12
        pld             [r2, r3]
        usada8          lr,  r5,  r7,  lr
        ldr             r7,  [r2, #4]
        ldr_post        r6,  r2,  r3
        beq             2f
        ldrd_post       r4,  r5,  r0,  r1
        usada8          r12,  r8,  r6,  r12
        pld             [r2, r3]
        usada8          lr,  r9,  r7,  lr
        b               1b
2:
        usada8          r12,  r8,  r6,  r12
        usada8          lr,  r9,  r7,  lr
        add             r0,  r12,  lr
        pop             {r4-r10, pc}
endfunc
