/**
 * Copyright (C) 2010 Mans Rullgard
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

.macro rac_get_prob     h, bs, buf, cw, pr, t0, t1
        adds            \bs, \bs, \t0
        lsl             \cw, \cw, \t0
        lsl             \t0, \h,  \t0
        rsb             \h,  \pr, #256
        it              cs
        ldrhcs          \t1, [\buf], #2
        smlabb          \h,  \t0, \pr, \h
T       itttt           cs
        rev16cs         \t1, \t1
A       orrcs           \cw, \cw, \t1, lsl \bs
T       lslcs           \t1, \t1, \bs
T       orrcs           \cw, \cw, \t1
        subcs           \bs, \bs, #16
        lsr             \h,  \h,  #8
        cmp             \cw, \h,  lsl #16
        itt             ge
        subge           \cw, \cw, \h,  lsl #16
        subge           \h,  \t0, \h
.endm

.macro rac_get_128      h, bs, buf, cw, t0, t1
        adds            \bs, \bs, \t0
        lsl             \cw, \cw, \t0
        lsl             \t0, \h,  \t0
        it              cs
        ldrhcs          \t1, [\buf], #2
        mov             \h,  #128
        it              cs
        rev16cs         \t1, \t1
        add             \h,  \h,  \t0, lsl #7
A       orrcs           \cw, \cw, \t1, lsl \bs
T       ittt            cs
T       lslcs           \t1, \t1, \bs
T       orrcs           \cw, \cw, \t1
        subcs           \bs, \bs, #16
        lsr             \h,  \h,  #8
        cmp             \cw, \h,  lsl #16
        itt             ge
        subge           \cw, \cw, \h,  lsl #16
        subge           \h,  \t0, \h
.endm

function ff_decode_block_coeffs_armv6, export=1
        push            {r0,r1,r4-r11,lr}
        movrel          lr,  X(ff_vp56_norm_shift)
        ldrd            r4,  r5,  [sp, #44]             @ token_prob, qmul
        cmp             r3,  #0
        ldr             r11, [r5]
        ldm             r0,  {r5-r7}                    @ high, bits, buf
        it              ne
        pkhtbne         r11, r11, r11, asr #16
        ldr             r8,  [r0, #16]                  @ code_word
0:
        ldrb            r9,  [lr, r5]
        add             r3,  r3,  #1
        ldrb            r0,  [r4, #1]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        blt             2f

        ldrb            r9,  [lr, r5]
        ldrb            r0,  [r4, #2]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        ldrb            r9,  [lr, r5]
        bge             3f

        add             r4,  r3,  r3,  lsl #5
        sxth            r12, r11
        add             r4,  r2,  r4
        adds            r6,  r6,  r9
        add             r4,  r4,  #11
        lsl             r8,  r8,  r9
        it              cs
        ldrhcs          r10, [r7], #2
        lsl             r9,  r5,  r9
        mov             r5,  #128
        it              cs
        rev16cs         r10, r10
        add             r5,  r5,  r9,  lsl #7
T       ittt            cs
T       lslcs           r10, r10, r6
T       orrcs           r8,  r8,  r10
A       orrcs           r8,  r8,  r10, lsl r6
        subcs           r6,  r6,  #16
        lsr             r5,  r5,  #8
        cmp             r8,  r5,  lsl #16
        movrel          r10, zigzag_scan-1
        itt             ge
        subge           r8,  r8,  r5,  lsl #16
        subge           r5,  r9,  r5
        ldrb            r10, [r10, r3]
        it              ge
        rsbge           r12, r12, #0
        cmp             r3,  #16
        strh            r12, [r1, r10]
        bge             6f
5:
        ldrb            r9,  [lr, r5]
        ldrb            r0,  [r4]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        pkhtb           r11, r11, r11, asr #16
        bge             0b

6:
        ldr             r0,  [sp]
        ldr             r9,  [r0, #12]
        cmp             r7,  r9
        it              hi
        movhi           r7,  r9
        stm             r0,  {r5-r7}                    @ high, bits, buf
        str             r8,  [r0, #16]                  @ code_word

        add             sp,  sp,  #8
        mov             r0,  r3
        pop             {r4-r11,pc}
2:
        add             r4,  r3,  r3,  lsl #5
        cmp             r3,  #16
        add             r4,  r2,  r4
        pkhtb           r11, r11, r11, asr #16
        bne             0b
        b               6b
3:
        ldrb            r0,  [r4, #3]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        ldrb            r9,  [lr, r5]
        bge             1f

        mov             r12, #2
        ldrb            r0,  [r4, #4]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        it              ge
        addge           r12, #1
        ldrb            r9,  [lr, r5]
        blt             4f
        ldrb            r0,  [r4, #5]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        it              ge
        addge           r12, #1
        ldrb            r9,  [lr, r5]
        b               4f
1:
        ldrb            r0,  [r4, #6]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        ldrb            r9,  [lr, r5]
        bge             3f

        ldrb            r0,  [r4, #7]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        ldrb            r9,  [lr, r5]
        bge             2f

        mov             r12, #5
        mov             r0,  #159
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        it              ge
        addge           r12, r12, #1
        ldrb            r9,  [lr, r5]
        b               4f
2:
        mov             r12, #7
        mov             r0,  #165
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        it              ge
        addge           r12, r12, #2
        ldrb            r9,  [lr, r5]
        mov             r0,  #145
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        it              ge
        addge           r12, r12, #1
        ldrb            r9,  [lr, r5]
        b               4f
3:
        ldrb            r0,  [r4, #8]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        it              ge
        addge           r4,  r4,  #1
        ldrb            r9,  [lr, r5]
        ite             ge
        movge           r12, #2
        movlt           r12, #0
        ldrb            r0,  [r4, #9]
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        mov             r9,  #8
        it              ge
        addge           r12, r12, #1
        movrel          r4,  X(ff_vp8_dct_cat_prob)
        lsl             r9,  r9,  r12
        ldr             r4,  [r4, r12, lsl #2]
        add             r12, r9,  #3
        mov             r1,  #0
        ldrb            r0,  [r4], #1
1:
        ldrb            r9,  [lr, r5]
        lsl             r1,  r1,  #1
        rac_get_prob    r5,  r6,  r7,  r8,  r0,  r9,  r10
        ldrb            r0,  [r4], #1
        it              ge
        addge           r1,  r1,  #1
        cmp             r0,  #0
        bne             1b
        ldrb            r9,  [lr, r5]
        add             r12, r12, r1
        ldr             r1,  [sp, #4]
4:
        add             r4,  r3,  r3,  lsl #5
        add             r4,  r2,  r4
        add             r4,  r4,  #22
        rac_get_128     r5,  r6,  r7,  r8,  r9,  r10
        it              ge
        rsbge           r12, r12, #0
        smulbb          r12, r12, r11
        movrel          r9,  zigzag_scan-1
        ldrb            r9,  [r9, r3]
        cmp             r3,  #16
        strh            r12, [r1, r9]
        bge             6b
        b               5b
endfunc

const zigzag_scan
        .byte            0,  2,  8, 16
        .byte           10,  4,  6, 12
        .byte           18, 24, 26, 20
        .byte           14, 22, 28, 30
endconst
