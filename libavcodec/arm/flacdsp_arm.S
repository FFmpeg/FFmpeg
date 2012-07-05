/*
 * Copyright (c) 2012 Mans Rullgard <mans@mansr.com>
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

function flac_lpc_16_1_arm
        ldr             r12, [sp]
        push            {r4, lr}
        ldr             r1,  [r1]
        subs            r12, r12, #2
        ldr             lr,  [r0], #4
        beq             2f
        it              lt
        poplt           {r4, pc}
1:
        mul             r4,  lr,  r1
        ldm             r0,  {r2, lr}
        add_sh          r2,  r2,  r4,  asr r3
        mul             r4,  r2,  r1
        subs            r12, r12, #2
        add_sh          lr,  lr,  r4,  asr r3
        stm             r0!, {r2, lr}
        bgt             1b
        it              lt
        poplt           {r4, pc}
2:
        mul             r4,  lr,  r1
        ldr             r2,  [r0]
        add_sh          r2,  r2,  r4,  asr r3
        str             r2,  [r0]
        pop             {r4, pc}
endfunc

function flac_lpc_16_2_arm
        ldr             r12, [sp]
        subs            r12, r12, r2
        it              le
        bxle            lr

        push            {r4-r9, lr}
        ldm             r0!, {r6, r7}
        ldm             r1,  {r8, r9}
        subs            r12, r12, #1
        beq             2f
1:
        mul             r4,  r6,  r8
        mul             r5,  r7,  r8
        mla             r4,  r7,  r9,  r4
        ldm             r0,  {r6, r7}
        add_sh          r6,  r6,  r4,  asr r3
        mla             r5,  r6,  r9,  r5
        add_sh          r7,  r7,  r5,  asr r3
        stm             r0!, {r6, r7}
        subs            r12, r12, #2
        bgt             1b
        it              lt
        poplt           {r4-r9, pc}
2:
        mul             r4,  r6,  r8
        mla             r4,  r7,  r9,  r4
        ldr             r5,  [r0]
        add_sh          r5,  r5,  r4,  asr r3
        str             r5,  [r0]
        pop             {r4-r9, pc}
endfunc

function ff_flac_lpc_16_arm, export=1
        cmp             r2,  #2
        blt             flac_lpc_16_1_arm
        beq             flac_lpc_16_2_arm

        ldr             r12, [sp]
        subs            r12, r12, r2
        it              le
        bxle            lr

        push            {r4-r9, lr}

        subs            r12, r12, #1
        beq             3f
1:
        sub             lr,  r2,  #2
        mov             r4,  #0
        mov             r5,  #0

        ldr             r7,  [r0], #4
        ldr             r9,  [r1], #4
2:
        mla             r4,  r7,  r9,  r4
        ldm             r0!, {r6, r7}
        mla             r5,  r6,  r9,  r5
        ldm             r1!, {r8, r9}
        mla             r4,  r6,  r8,  r4
        subs            lr,  lr,  #2
        mla             r5,  r7,  r8,  r5
        bgt             2b
        blt             6f

        mla             r4,  r7,  r9,  r4
        ldr             r7,  [r0], #4
        mla             r5,  r7,  r9,  r5
        ldr             r9,  [r1], #4
6:
        mla             r4,  r7,  r9,  r4
        ldm             r0,  {r6, r7}
        add_sh          r6,  r6,  r4,  asr r3
        mla             r5,  r6,  r9,  r5
        add_sh          r7,  r7,  r5,  asr r3
        stm             r0!, {r6, r7}
        sub             r0,  r0,  r2,  lsl #2
        sub             r1,  r1,  r2,  lsl #2

        subs            r12, r12, #2
        bgt             1b
        it              lt
        poplt           {r4-r9, pc}
3:
        mov             r4,  #0
4:
        ldr             r5,  [r1], #4
        ldr             r6,  [r0], #4
        mla             r4,  r5,  r6,  r4
        subs            r2,  r2,  #1
        bgt             4b
        ldr             r5,  [r0]
        add_sh          r5,  r5,  r4,  asr r3
        str             r5,  [r0]
        pop             {r4-r9, pc}
endfunc
