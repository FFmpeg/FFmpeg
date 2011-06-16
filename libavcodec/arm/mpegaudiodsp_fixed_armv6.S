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

.macro  skip            args:vararg
.endm

.macro  sum8            lo,  hi,  w, p, t1, t2, t3, t4, rsb=skip, offs=0
        ldr             \t1, [\w, #4*\offs]
        ldr             \t2, [\p, #4]!
        \rsb            \t1, \t1, #0
  .irpc i, 135
        ldr             \t3, [\w, #4*64*\i+4*\offs]
        ldr             \t4, [\p, #4*64*\i]
        smlal           \lo, \hi, \t1, \t2
        \rsb            \t3, \t3, #0
        ldr             \t1, [\w, #4*64*(\i+1)+4*\offs]
        ldr             \t2, [\p, #4*64*(\i+1)]
        smlal           \lo, \hi, \t3, \t4
        \rsb            \t1, \t1, #0
  .endr
        ldr             \t3, [\w, #4*64*7+4*\offs]
        ldr             \t4, [\p, #4*64*7]
        smlal           \lo, \hi, \t1, \t2
        \rsb            \t3, \t3, #0
        smlal           \lo, \hi, \t3, \t4
.endm

.macro  round           rd,  lo,  hi
        lsr             \rd, \lo, #24
        bic             \lo, \lo, #0xff000000
        orr             \rd, \rd, \hi, lsl #8
        mov             \hi, #0
        ssat            \rd, #16, \rd
.endm

function ff_mpadsp_apply_window_fixed_armv6, export=1
        push            {r2,r4-r11,lr}

        add             r4,  r0,  #4*512        @ synth_buf + 512
    .rept 4
        ldm             r0!, {r5-r12}
        stm             r4!, {r5-r12}
    .endr

        ldr             r4,  [sp, #40]          @ incr
        sub             r0,  r0,  #4*17         @ synth_buf + 16
        ldr             r8,  [r2]               @ sum:low
        add             r2,  r0,  #4*32         @ synth_buf + 48
        rsb             r5,  r4,  r4,  lsl #5   @ 31 * incr
        lsl             r4,  r4,  #1
        asr             r9,  r8,  #31           @ sum:high
        add             r5,  r3,  r5,  lsl #1   @ samples2
        add             r6,  r1,  #4*32         @ w2
        str             r4,  [sp, #40]

        sum8            r8,  r9,  r1,  r0,  r10, r11, r12, lr
        sum8            r8,  r9,  r1,  r2,  r10, r11, r12, lr, rsb, 32
        round           r10, r8,  r9
        strh            r10, [r3], r4

        mov             lr,  #15
1:
        ldr             r12, [r0, #4]!
        ldr             r11, [r6, #-4]!
        ldr             r10, [r1, #4]!
  .irpc i, 0246
    .if \i
        ldr             r11, [r6, #4*64*\i]
        ldr             r10, [r1, #4*64*\i]
    .endif
        rsb             r11, r11, #0
        smlal           r8,  r9,  r10, r12
        ldr             r10, [r0, #4*64*(\i+1)]
    .ifeq \i
        smull           r4, r7, r11, r12
    .else
        smlal           r4, r7, r11, r12
    .endif
        ldr             r11, [r6, #4*64*(\i+1)]
        ldr             r12, [r1, #4*64*(\i+1)]
        rsb             r11, r11, #0
        smlal           r8,  r9,  r12, r10
    .iflt \i-6
        ldr             r12, [r0, #4*64*(\i+2)]
    .else
        ldr             r12, [r2, #-4]!
    .endif
        smlal           r4,  r7,  r11, r10
  .endr
  .irpc i, 0246
        ldr             r10, [r1, #4*64*\i+4*32]
        rsb             r12, r12, #0
        ldr             r11, [r6, #4*64*\i+4*32]
        smlal           r8,  r9,  r10, r12
        ldr             r10, [r2, #4*64*(\i+1)]
        smlal           r4,  r7,  r11, r12
        ldr             r12, [r1, #4*64*(\i+1)+4*32]
        rsb             r10, r10, #0
        ldr             r11, [r6, #4*64*(\i+1)+4*32]
        smlal           r8,  r9,  r12, r10
    .iflt \i-6
        ldr             r12, [r2, #4*64*(\i+2)]
    .else
        ldr             r12, [sp, #40]
    .endif
        smlal           r4,  r7,  r11, r10
  .endr
        round           r10, r8,  r9
        adds            r8,  r8,  r4
        adc             r9,  r9,  r7
        strh            r10, [r3], r12
        round           r11, r8,  r9
        subs            lr,  lr,  #1
        strh            r11, [r5], -r12
        bgt             1b

        sum8            r8,  r9,  r1,  r0,  r10, r11, r12, lr, rsb, 33
        pop             {r4}
        round           r10, r8,  r9
        str             r8,  [r4]
        strh            r10, [r3]

        pop             {r4-r11,pc}
endfunc
