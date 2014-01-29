@
@ ARMv4-optimized halfpel functions
@ Copyright (c) 2004 AGAWA Koji <i (AT) atty (DOT) jp>
@
@ This file is part of Libav.
@
@ Libav is free software; you can redistribute it and/or
@ modify it under the terms of the GNU Lesser General Public
@ License as published by the Free Software Foundation; either
@ version 2.1 of the License, or (at your option) any later version.
@
@ Libav is distributed in the hope that it will be useful,
@ but WITHOUT ANY WARRANTY; without even the implied warranty of
@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
@ Lesser General Public License for more details.
@
@ You should have received a copy of the GNU Lesser General Public
@ License along with Libav; if not, write to the Free Software
@ Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
@

#include "config.h"
#include "libavutil/arm/asm.S"

#if !HAVE_ARMV5TE_EXTERNAL
#define pld @
#endif

.macro  ALIGN_QWORD_D shift, Rd0, Rd1, Rd2, Rd3, Rn0, Rn1, Rn2, Rn3, Rn4
        mov             \Rd0, \Rn0, lsr #(\shift * 8)
        mov             \Rd1, \Rn1, lsr #(\shift * 8)
        mov             \Rd2, \Rn2, lsr #(\shift * 8)
        mov             \Rd3, \Rn3, lsr #(\shift * 8)
        orr             \Rd0, \Rd0, \Rn1, lsl #(32 - \shift * 8)
        orr             \Rd1, \Rd1, \Rn2, lsl #(32 - \shift * 8)
        orr             \Rd2, \Rd2, \Rn3, lsl #(32 - \shift * 8)
        orr             \Rd3, \Rd3, \Rn4, lsl #(32 - \shift * 8)
.endm
.macro  ALIGN_DWORD shift, R0, R1, R2
        mov             \R0, \R0, lsr #(\shift * 8)
        orr             \R0, \R0, \R1, lsl #(32 - \shift * 8)
        mov             \R1, \R1, lsr #(\shift * 8)
        orr             \R1, \R1, \R2, lsl #(32 - \shift * 8)
.endm
.macro  ALIGN_DWORD_D shift, Rdst0, Rdst1, Rsrc0, Rsrc1, Rsrc2
        mov             \Rdst0, \Rsrc0, lsr #(\shift * 8)
        mov             \Rdst1, \Rsrc1, lsr #(\shift * 8)
        orr             \Rdst0, \Rdst0, \Rsrc1, lsl #(32 - (\shift * 8))
        orr             \Rdst1, \Rdst1, \Rsrc2, lsl #(32 - (\shift * 8))
.endm

.macro  RND_AVG32 Rd0, Rd1, Rn0, Rn1, Rm0, Rm1, Rmask
        @ Rd = (Rn | Rm) - (((Rn ^ Rm) & ~0x01010101) >> 1)
        @ Rmask = 0xFEFEFEFE
        @ Rn = destroy
        eor             \Rd0, \Rn0, \Rm0
        eor             \Rd1, \Rn1, \Rm1
        orr             \Rn0, \Rn0, \Rm0
        orr             \Rn1, \Rn1, \Rm1
        and             \Rd0, \Rd0, \Rmask
        and             \Rd1, \Rd1, \Rmask
        sub             \Rd0, \Rn0, \Rd0, lsr #1
        sub             \Rd1, \Rn1, \Rd1, lsr #1
.endm

.macro  NO_RND_AVG32 Rd0, Rd1, Rn0, Rn1, Rm0, Rm1, Rmask
        @ Rd = (Rn & Rm) - (((Rn ^ Rm) & ~0x01010101) >> 1)
        @ Rmask = 0xFEFEFEFE
        @ Rn = destroy
        eor             \Rd0, \Rn0, \Rm0
        eor             \Rd1, \Rn1, \Rm1
        and             \Rn0, \Rn0, \Rm0
        and             \Rn1, \Rn1, \Rm1
        and             \Rd0, \Rd0, \Rmask
        and             \Rd1, \Rd1, \Rmask
        add             \Rd0, \Rn0, \Rd0, lsr #1
        add             \Rd1, \Rn1, \Rd1, lsr #1
.endm

.macro  JMP_ALIGN tmp, reg
        ands            \tmp, \reg, #3
        bic             \reg, \reg, #3
        beq             1f
        subs            \tmp, \tmp, #1
        beq             2f
        subs            \tmp, \tmp, #1
        beq             3f
        b    4f
.endm

@ ----------------------------------------------------------------
function ff_put_pixels16_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r11, lr}
        JMP_ALIGN       r5,  r1
1:
        ldm             r1,  {r4-r7}
        add             r1,  r1,  r2
        stm             r0,  {r4-r7}
        pld             [r1]
        subs            r3,  r3,  #1
        add             r0,  r0,  r2
        bne             1b
        pop             {r4-r11, pc}
        .align 5
2:
        ldm             r1,  {r4-r8}
        add             r1,  r1,  r2
        ALIGN_QWORD_D   1,   r9,  r10, r11, r12, r4,  r5,  r6,  r7,  r8
        pld             [r1]
        subs            r3,  r3,  #1
        stm             r0,  {r9-r12}
        add             r0,  r0,  r2
        bne             2b
        pop             {r4-r11, pc}
        .align 5
3:
        ldm             r1,  {r4-r8}
        add             r1,  r1,  r2
        ALIGN_QWORD_D   2,   r9,  r10, r11, r12, r4,  r5,  r6,  r7,  r8
        pld             [r1]
        subs            r3,  r3,  #1
        stm             r0,  {r9-r12}
        add             r0,  r0,  r2
        bne             3b
        pop             {r4-r11, pc}
        .align 5
4:
        ldm             r1,  {r4-r8}
        add             r1,  r1,  r2
        ALIGN_QWORD_D   3,   r9,  r10, r11, r12, r4,  r5,  r6,  r7,  r8
        pld             [r1]
        subs            r3,  r3,  #1
        stm             r0,  {r9-r12}
        add             r0,  r0,  r2
        bne             4b
        pop             {r4-r11,pc}
endfunc

@ ----------------------------------------------------------------
function ff_put_pixels8_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r5,lr}
        JMP_ALIGN       r5,  r1
1:
        ldm             r1,  {r4-r5}
        add             r1,  r1,  r2
        subs            r3,  r3,  #1
        pld             [r1]
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             1b
        pop             {r4-r5,pc}
        .align 5
2:
        ldm             r1,  {r4-r5, r12}
        add             r1,  r1,  r2
        ALIGN_DWORD     1,   r4,  r5,  r12
        pld             [r1]
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             2b
        pop             {r4-r5,pc}
        .align 5
3:
        ldm             r1,  {r4-r5, r12}
        add             r1,  r1,  r2
        ALIGN_DWORD     2,   r4,  r5,  r12
        pld             [r1]
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             3b
        pop             {r4-r5,pc}
        .align 5
4:
        ldm             r1,  {r4-r5, r12}
        add             r1,  r1,  r2
        ALIGN_DWORD     3,   r4,  r5,  r12
        pld             [r1]
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             4b
        pop             {r4-r5,pc}
endfunc

@ ----------------------------------------------------------------
function ff_put_pixels8_x2_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r10,lr}
        ldr             r12, =0xfefefefe
        JMP_ALIGN       r5,  r1
1:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   1,   r6,  r7,  r4,  r5,  r10
        pld             [r1]
        RND_AVG32       r8,  r9,  r4,  r5,  r6,  r7,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        bne             1b
        pop             {r4-r10,pc}
        .align 5
2:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   1,   r6,  r7,  r4,  r5,  r10
        ALIGN_DWORD_D   2,   r8,  r9,  r4,  r5,  r10
        pld             [r1]
        RND_AVG32       r4,  r5,  r6,  r7,  r8,  r9,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             2b
        pop             {r4-r10,pc}
        .align 5
3:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   2,   r6,  r7,  r4,  r5,  r10
        ALIGN_DWORD_D   3,   r8,  r9,  r4,  r5,  r10
        pld             [r1]
        RND_AVG32       r4,  r5,  r6,  r7,  r8,  r9,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             3b
        pop             {r4-r10,pc}
        .align 5
4:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   3,   r6,  r7,  r4,  r5,  r10
        pld             [r1]
        RND_AVG32       r8,  r9,  r6,  r7,  r5,  r10, r12
        subs            r3,  r3,  #1
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        bne             4b
        pop             {r4-r10,pc}
endfunc

function ff_put_no_rnd_pixels8_x2_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r10,lr}
        ldr             r12, =0xfefefefe
        JMP_ALIGN       r5,  r1
1:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   1,   r6,  r7,  r4,  r5,  r10
        pld             [r1]
        NO_RND_AVG32    r8,  r9,  r4,  r5,  r6,  r7,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        bne             1b
        pop             {r4-r10,pc}
        .align 5
2:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   1,   r6,  r7,  r4,  r5,  r10
        ALIGN_DWORD_D   2,   r8,  r9,  r4,  r5,  r10
        pld             [r1]
        NO_RND_AVG32    r4,  r5,  r6,  r7,  r8,  r9,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             2b
        pop             {r4-r10,pc}
        .align 5
3:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   2,   r6,  r7,  r4,  r5,  r10
        ALIGN_DWORD_D   3,   r8,  r9,  r4,  r5,  r10
        pld             [r1]
        NO_RND_AVG32    r4,  r5,  r6,  r7,  r8,  r9,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bne             3b
        pop             {r4-r10,pc}
        .align 5
4:
        ldm             r1,  {r4-r5, r10}
        add             r1,  r1,  r2
        ALIGN_DWORD_D   3,   r6,  r7,  r4,  r5,  r10
        pld             [r1]
        NO_RND_AVG32    r8,  r9,  r6,  r7,  r5,  r10, r12
        subs            r3,  r3,  #1
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        bne             4b
        pop             {r4-r10,pc}
endfunc


@ ----------------------------------------------------------------
function ff_put_pixels8_y2_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r11,lr}
        mov             r3,  r3,  lsr #1
        ldr             r12, =0xfefefefe
        JMP_ALIGN       r5,  r1
1:
        ldm             r1,  {r4-r5}
        add             r1,  r1,  r2
6:      ldm             r1,  {r6-r7}
        add             r1,  r1,  r2
        pld             [r1]
        RND_AVG32       r8,  r9,  r4,  r5,  r6,  r7,  r12
        ldm             r1,  {r4-r5}
        add             r1,  r1,  r2
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        pld             [r1]
        RND_AVG32       r8,  r9,  r6,  r7,  r4,  r5,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
        .align 5
2:
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     1,   r4,  r5,  r6
6:      ldm             r1,  {r7-r9}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     1,   r7,  r8,  r9
        RND_AVG32       r10, r11, r4,  r5,  r7,  r8,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     1,   r4,  r5,  r6
        subs            r3,  r3,  #1
        RND_AVG32       r10, r11, r7,  r8,  r4,  r5,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
        .align 5
3:
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     2,   r4,  r5,  r6
6:      ldm             r1,  {r7-r9}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     2,   r7,  r8,  r9
        RND_AVG32       r10, r11, r4,  r5,  r7,  r8,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     2,   r4,  r5,  r6
        subs            r3,  r3,  #1
        RND_AVG32       r10, r11, r7,  r8,  r4,  r5,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
        .align 5
4:
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     3,   r4,  r5,  r6
6:      ldm             r1,  {r7-r9}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     3,   r7,  r8,  r9
        RND_AVG32       r10, r11, r4,  r5,  r7,  r8,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     3,   r4,  r5,  r6
        subs            r3,  r3,  #1
        RND_AVG32       r10, r11, r7,  r8,  r4,  r5,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
endfunc

function ff_put_no_rnd_pixels8_y2_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r11,lr}
        mov             r3,  r3,  lsr #1
        ldr             r12, =0xfefefefe
        JMP_ALIGN       r5,  r1
1:
        ldm             r1,  {r4-r5}
        add             r1,  r1,  r2
6:      ldm             r1,  {r6-r7}
        add             r1,  r1,  r2
        pld             [r1]
        NO_RND_AVG32    r8,  r9,  r4,  r5,  r6,  r7,  r12
        ldm             r1,  {r4-r5}
        add             r1,  r1,  r2
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        pld             [r1]
        NO_RND_AVG32    r8,  r9,  r6,  r7,  r4,  r5,  r12
        subs            r3,  r3,  #1
        stm             r0,  {r8-r9}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
        .align 5
2:
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     1,   r4,  r5,  r6
6:      ldm             r1,  {r7-r9}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     1,   r7,  r8,  r9
        NO_RND_AVG32    r10, r11, r4,  r5,  r7,  r8,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     1,   r4,  r5,  r6
        subs            r3,  r3,  #1
        NO_RND_AVG32    r10, r11, r7,  r8,  r4,  r5,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
        .align 5
3:
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     2,   r4,  r5,  r6
6:      ldm             r1,  {r7-r9}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     2,   r7,  r8,  r9
        NO_RND_AVG32    r10, r11, r4,  r5,  r7,  r8,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     2,   r4,  r5,  r6
        subs            r3,  r3,  #1
        NO_RND_AVG32    r10, r11, r7,  r8,  r4,  r5,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
        .align 5
4:
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     3,   r4,  r5,  r6
6:      ldm             r1,  {r7-r9}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     3,   r7,  r8,  r9
        NO_RND_AVG32    r10, r11, r4,  r5,  r7,  r8,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        ldm             r1,  {r4-r6}
        add             r1,  r1,  r2
        pld             [r1]
        ALIGN_DWORD     3,   r4,  r5,  r6
        subs            r3,  r3,  #1
        NO_RND_AVG32    r10, r11, r7,  r8,  r4,  r5,  r12
        stm             r0,  {r10-r11}
        add             r0,  r0,  r2
        bne             6b
        pop             {r4-r11,pc}
endfunc

        .ltorg

@ ----------------------------------------------------------------
.macro  RND_XY2_IT align, rnd
        @ l1=  (a & 0x03030303) + (b & 0x03030303) ?(+ 0x02020202)
        @ h1= ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2)
.if \align == 0
        ldm             r1,  {r6-r8}
.elseif \align == 3
        ldm             r1,  {r5-r7}
.else
        ldm             r1,  {r8-r10}
.endif
        add             r1,  r1,  r2
        pld             [r1]
.if \align == 0
        ALIGN_DWORD_D   1,   r4,  r5,  r6,  r7,  r8
.elseif \align == 1
        ALIGN_DWORD_D   1,   r4,  r5,  r8,  r9,  r10
        ALIGN_DWORD_D   2,   r6,  r7,  r8,  r9,  r10
.elseif \align == 2
        ALIGN_DWORD_D   2,   r4,  r5,  r8,  r9,  r10
        ALIGN_DWORD_D   3,   r6,  r7,  r8,  r9,  r10
.elseif \align == 3
        ALIGN_DWORD_D   3,   r4,  r5,  r5,  r6,  r7
.endif
        ldr             r14, =0x03030303
        tst             r3,  #1
        and             r8,  r4,  r14
        and             r9,  r5,  r14
        and             r10, r6,  r14
        and             r11, r7,  r14
        it              eq
        andeq           r14, r14, r14, \rnd #1
        add             r8,  r8,  r10
        add             r9,  r9,  r11
        ldr             r12, =0xfcfcfcfc >> 2
        itt             eq
        addeq           r8,  r8,  r14
        addeq           r9,  r9,  r14
        and             r4,  r12, r4,  lsr #2
        and             r5,  r12, r5,  lsr #2
        and             r6,  r12, r6,  lsr #2
        and             r7,  r12, r7,  lsr #2
        add             r10, r4,  r6
        add             r11, r5,  r7
        subs            r3,  r3,  #1
.endm

.macro RND_XY2_EXPAND align, rnd
        RND_XY2_IT      \align, \rnd
6:      push            {r8-r11}
        RND_XY2_IT      \align, \rnd
        pop             {r4-r7}
        add             r4,  r4,  r8
        add             r5,  r5,  r9
        ldr             r14, =0x0f0f0f0f
        add             r6,  r6,  r10
        add             r7,  r7,  r11
        and             r4,  r14, r4,  lsr #2
        and             r5,  r14, r5,  lsr #2
        add             r4,  r4,  r6
        add             r5,  r5,  r7
        stm             r0,  {r4-r5}
        add             r0,  r0,  r2
        bge             6b
        pop             {r4-r11,pc}
.endm

function ff_put_pixels8_xy2_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r11,lr} @ R14 is also called LR
        JMP_ALIGN       r5,  r1
1:      RND_XY2_EXPAND  0, lsl
        .align 5
2:      RND_XY2_EXPAND  1, lsl
        .align 5
3:      RND_XY2_EXPAND  2, lsl
        .align 5
4:      RND_XY2_EXPAND  3, lsl
endfunc

function ff_put_no_rnd_pixels8_xy2_arm, export=1, align=5
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld             [r1]
        push            {r4-r11,lr}
        JMP_ALIGN       r5,  r1
1:      RND_XY2_EXPAND  0, lsr
        .align 5
2:      RND_XY2_EXPAND  1, lsr
        .align 5
3:      RND_XY2_EXPAND  2, lsr
        .align 5
4:      RND_XY2_EXPAND  3, lsr
endfunc
