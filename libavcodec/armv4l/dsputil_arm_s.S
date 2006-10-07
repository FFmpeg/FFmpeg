@
@ ARMv4L optimized DSP utils
@ Copyright (c) 2004 AGAWA Koji <i (AT) atty (DOT) jp>
@
@ This file is part of FFmpeg.
@
@ FFmpeg is free software; you can redistribute it and/or
@ modify it under the terms of the GNU Lesser General Public
@ License as published by the Free Software Foundation; either
@ version 2.1 of the License, or (at your option) any later version.
@
@ FFmpeg is distributed in the hope that it will be useful,
@ but WITHOUT ANY WARRANTY; without even the implied warranty of
@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
@ Lesser General Public License for more details.
@
@ You should have received a copy of the GNU Lesser General Public
@ License along with FFmpeg; if not, write to the Free Software
@ Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
@

.macro  ADJ_ALIGN_QUADWORD_D shift, Rd0, Rd1, Rd2, Rd3, Rn0, Rn1, Rn2, Rn3, Rn4
        mov \Rd0, \Rn0, lsr #(\shift * 8)
        mov \Rd1, \Rn1, lsr #(\shift * 8)
        mov \Rd2, \Rn2, lsr #(\shift * 8)
        mov \Rd3, \Rn3, lsr #(\shift * 8)
        orr \Rd0, \Rd0, \Rn1, lsl #(32 - \shift * 8)
        orr \Rd1, \Rd1, \Rn2, lsl #(32 - \shift * 8)
        orr \Rd2, \Rd2, \Rn3, lsl #(32 - \shift * 8)
        orr \Rd3, \Rd3, \Rn4, lsl #(32 - \shift * 8)
.endm
.macro  ADJ_ALIGN_DOUBLEWORD shift, R0, R1, R2
        mov \R0, \R0, lsr #(\shift * 8)
        orr \R0, \R0, \R1, lsl #(32 - \shift * 8)
        mov \R1, \R1, lsr #(\shift * 8)
        orr \R1, \R1, \R2, lsl #(32 - \shift * 8)
.endm
.macro  ADJ_ALIGN_DOUBLEWORD_D shift, Rdst0, Rdst1, Rsrc0, Rsrc1, Rsrc2
        mov \Rdst0, \Rsrc0, lsr #(\shift * 8)
        mov \Rdst1, \Rsrc1, lsr #(\shift * 8)
        orr \Rdst0, \Rdst0, \Rsrc1, lsl #(32 - (\shift * 8))
        orr \Rdst1, \Rdst1, \Rsrc2, lsl #(32 - (\shift * 8))
.endm

.macro  RND_AVG32 Rd0, Rd1, Rn0, Rn1, Rm0, Rm1, Rmask
        @ Rd = (Rn | Rm) - (((Rn ^ Rm) & ~0x01010101) >> 1)
        @ Rmask = 0xFEFEFEFE
        @ Rn = destroy
        eor \Rd0, \Rn0, \Rm0
        eor \Rd1, \Rn1, \Rm1
        orr \Rn0, \Rn0, \Rm0
        orr \Rn1, \Rn1, \Rm1
        and \Rd0, \Rd0, \Rmask
        and \Rd1, \Rd1, \Rmask
        sub \Rd0, \Rn0, \Rd0, lsr #1
        sub \Rd1, \Rn1, \Rd1, lsr #1
.endm

.macro  NO_RND_AVG32 Rd0, Rd1, Rn0, Rn1, Rm0, Rm1, Rmask
        @ Rd = (Rn & Rm) - (((Rn ^ Rm) & ~0x01010101) >> 1)
        @ Rmask = 0xFEFEFEFE
        @ Rn = destroy
        eor \Rd0, \Rn0, \Rm0
        eor \Rd1, \Rn1, \Rm1
        and \Rn0, \Rn0, \Rm0
        and \Rn1, \Rn1, \Rm1
        and \Rd0, \Rd0, \Rmask
        and \Rd1, \Rd1, \Rmask
        add \Rd0, \Rn0, \Rd0, lsr #1
        add \Rd1, \Rn1, \Rd1, lsr #1
.endm

@ ----------------------------------------------------------------
        .align 8
        .global put_pixels16_arm
put_pixels16_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r11, lr} @ R14 is also called LR
        adr r5, 5f
        ands r4, r1, #3
        bic r1, r1, #3
        add r5, r5, r4, lsl #2
        ldrne pc, [r5]
1:
        ldmia r1, {r4-r7}
        add r1, r1, r2
        stmia r0, {r4-r7}
        pld [r1]
        subs r3, r3, #1
        add r0, r0, r2
        bne 1b
        ldmfd sp!, {r4-r11, pc}
        .align 8
2:
        ldmia r1, {r4-r8}
        add r1, r1, r2
        ADJ_ALIGN_QUADWORD_D 1, r9, r10, r11, r12, r4, r5, r6, r7, r8
        pld [r1]
        subs r3, r3, #1
        stmia r0, {r9-r12}
        add r0, r0, r2
        bne 2b
        ldmfd sp!, {r4-r11, pc}
        .align 8
3:
        ldmia r1, {r4-r8}
        add r1, r1, r2
        ADJ_ALIGN_QUADWORD_D 2, r9, r10, r11, r12, r4, r5, r6, r7, r8
        pld [r1]
        subs r3, r3, #1
        stmia r0, {r9-r12}
        add r0, r0, r2
        bne 3b
        ldmfd sp!, {r4-r11, pc}
        .align 8
4:
        ldmia r1, {r4-r8}
        add r1, r1, r2
        ADJ_ALIGN_QUADWORD_D 3, r9, r10, r11, r12, r4, r5, r6, r7, r8
        pld [r1]
        subs r3, r3, #1
        stmia r0, {r9-r12}
        add r0, r0, r2
        bne 4b
        ldmfd sp!, {r4-r11,pc}
        .align 8
5:
        .word 1b
        .word 2b
        .word 3b
        .word 4b

@ ----------------------------------------------------------------
        .align 8
        .global put_pixels8_arm
put_pixels8_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r5,lr} @ R14 is also called LR
        adr r5, 5f
        ands r4, r1, #3
        bic r1, r1, #3
        add r5, r5, r4, lsl #2
        ldrne pc, [r5]
1:
        ldmia r1, {r4-r5}
        add r1, r1, r2
        subs r3, r3, #1
        pld [r1]
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 1b
        ldmfd sp!, {r4-r5,pc}
        .align 8
2:
        ldmia r1, {r4-r5, r12}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD 1, r4, r5, r12
        pld [r1]
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 2b
        ldmfd sp!, {r4-r5,pc}
        .align 8
3:
        ldmia r1, {r4-r5, r12}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD 2, r4, r5, r12
        pld [r1]
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 3b
        ldmfd sp!, {r4-r5,pc}
        .align 8
4:
        ldmia r1, {r4-r5, r12}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD 3, r4, r5, r12
        pld [r1]
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 4b
        ldmfd sp!, {r4-r5,pc}
        .align 8
5:
        .word 1b
        .word 2b
        .word 3b
        .word 4b

@ ----------------------------------------------------------------
        .align 8
        .global put_pixels8_x2_arm
put_pixels8_x2_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r10,lr} @ R14 is also called LR
        adr r5, 5f
        ands r4, r1, #3
        ldr r12, [r5]
        add r5, r5, r4, lsl #2
        bic r1, r1, #3
        ldrne pc, [r5]
1:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 1, r6, r7, r4, r5, r10
        pld [r1]
        RND_AVG32 r8, r9, r4, r5, r6, r7, r12
        subs r3, r3, #1
        stmia r0, {r8-r9}
        add r0, r0, r2
        bne 1b
        ldmfd sp!, {r4-r10,pc}
        .align 8
2:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 1, r6, r7, r4, r5, r10
        ADJ_ALIGN_DOUBLEWORD_D 2, r8, r9, r4, r5, r10
        pld [r1]
        RND_AVG32 r4, r5, r6, r7, r8, r9, r12
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 2b
        ldmfd sp!, {r4-r10,pc}
        .align 8
3:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 2, r6, r7, r4, r5, r10
        ADJ_ALIGN_DOUBLEWORD_D 3, r8, r9, r4, r5, r10
        pld [r1]
        RND_AVG32 r4, r5, r6, r7, r8, r9, r12
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 3b
        ldmfd sp!, {r4-r10,pc}
        .align 8
4:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 3, r6, r7, r4, r5, r10
        pld [r1]
        RND_AVG32 r8, r9, r6, r7, r5, r10, r12
        subs r3, r3, #1
        stmia r0, {r8-r9}
        add r0, r0, r2
        bne 4b
        ldmfd sp!, {r4-r10,pc} @@ update PC with LR content.
        .align 8
5:
        .word 0xFEFEFEFE
        .word 2b
        .word 3b
        .word 4b

        .align 8
        .global put_no_rnd_pixels8_x2_arm
put_no_rnd_pixels8_x2_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r10,lr} @ R14 is also called LR
        adr r5, 5f
        ands r4, r1, #3
        ldr r12, [r5]
        add r5, r5, r4, lsl #2
        bic r1, r1, #3
        ldrne pc, [r5]
1:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 1, r6, r7, r4, r5, r10
        pld [r1]
        NO_RND_AVG32 r8, r9, r4, r5, r6, r7, r12
        subs r3, r3, #1
        stmia r0, {r8-r9}
        add r0, r0, r2
        bne 1b
        ldmfd sp!, {r4-r10,pc}
        .align 8
2:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 1, r6, r7, r4, r5, r10
        ADJ_ALIGN_DOUBLEWORD_D 2, r8, r9, r4, r5, r10
        pld [r1]
        NO_RND_AVG32 r4, r5, r6, r7, r8, r9, r12
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 2b
        ldmfd sp!, {r4-r10,pc}
        .align 8
3:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 2, r6, r7, r4, r5, r10
        ADJ_ALIGN_DOUBLEWORD_D 3, r8, r9, r4, r5, r10
        pld [r1]
        NO_RND_AVG32 r4, r5, r6, r7, r8, r9, r12
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 3b
        ldmfd sp!, {r4-r10,pc}
        .align 8
4:
        ldmia r1, {r4-r5, r10}
        add r1, r1, r2
        ADJ_ALIGN_DOUBLEWORD_D 3, r6, r7, r4, r5, r10
        pld [r1]
        NO_RND_AVG32 r8, r9, r6, r7, r5, r10, r12
        subs r3, r3, #1
        stmia r0, {r8-r9}
        add r0, r0, r2
        bne 4b
        ldmfd sp!, {r4-r10,pc} @@ update PC with LR content.
        .align 8
5:
        .word 0xFEFEFEFE
        .word 2b
        .word 3b
        .word 4b


@ ----------------------------------------------------------------
        .align 8
        .global put_pixels8_y2_arm
put_pixels8_y2_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r11,lr} @ R14 is also called LR
        adr r5, 5f
        ands r4, r1, #3
        mov r3, r3, lsr #1
        ldr r12, [r5]
        add r5, r5, r4, lsl #2
        bic r1, r1, #3
        ldrne pc, [r5]
1:
        ldmia r1, {r4-r5}
        add r1, r1, r2
6:      ldmia r1, {r6-r7}
        add r1, r1, r2
        pld [r1]
        RND_AVG32 r8, r9, r4, r5, r6, r7, r12
        ldmia r1, {r4-r5}
        add r1, r1, r2
        stmia r0, {r8-r9}
        add r0, r0, r2
        pld [r1]
        RND_AVG32 r8, r9, r6, r7, r4, r5, r12
        subs r3, r3, #1
        stmia r0, {r8-r9}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
2:
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 1, r4, r5, r6
6:      ldmia r1, {r7-r9}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 1, r7, r8, r9
        RND_AVG32 r10, r11, r4, r5, r7, r8, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 1, r4, r5, r6
        subs r3, r3, #1
        RND_AVG32 r10, r11, r7, r8, r4, r5, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
3:
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 2, r4, r5, r6
6:      ldmia r1, {r7-r9}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 2, r7, r8, r9
        RND_AVG32 r10, r11, r4, r5, r7, r8, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 2, r4, r5, r6
        subs r3, r3, #1
        RND_AVG32 r10, r11, r7, r8, r4, r5, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
4:
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 3, r4, r5, r6
6:      ldmia r1, {r7-r9}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 3, r7, r8, r9
        RND_AVG32 r10, r11, r4, r5, r7, r8, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 3, r4, r5, r6
        subs r3, r3, #1
        RND_AVG32 r10, r11, r7, r8, r4, r5, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}

        .align 8
5:
        .word 0xFEFEFEFE
        .word 2b
        .word 3b
        .word 4b

        .align 8
        .global put_no_rnd_pixels8_y2_arm
put_no_rnd_pixels8_y2_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r11,lr} @ R14 is also called LR
        adr r5, 5f
        ands r4, r1, #3
        mov r3, r3, lsr #1
        ldr r12, [r5]
        add r5, r5, r4, lsl #2
        bic r1, r1, #3
        ldrne pc, [r5]
1:
        ldmia r1, {r4-r5}
        add r1, r1, r2
6:      ldmia r1, {r6-r7}
        add r1, r1, r2
        pld [r1]
        NO_RND_AVG32 r8, r9, r4, r5, r6, r7, r12
        ldmia r1, {r4-r5}
        add r1, r1, r2
        stmia r0, {r8-r9}
        add r0, r0, r2
        pld [r1]
        NO_RND_AVG32 r8, r9, r6, r7, r4, r5, r12
        subs r3, r3, #1
        stmia r0, {r8-r9}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
2:
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 1, r4, r5, r6
6:      ldmia r1, {r7-r9}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 1, r7, r8, r9
        NO_RND_AVG32 r10, r11, r4, r5, r7, r8, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 1, r4, r5, r6
        subs r3, r3, #1
        NO_RND_AVG32 r10, r11, r7, r8, r4, r5, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
3:
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 2, r4, r5, r6
6:      ldmia r1, {r7-r9}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 2, r7, r8, r9
        NO_RND_AVG32 r10, r11, r4, r5, r7, r8, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 2, r4, r5, r6
        subs r3, r3, #1
        NO_RND_AVG32 r10, r11, r7, r8, r4, r5, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
4:
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 3, r4, r5, r6
6:      ldmia r1, {r7-r9}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 3, r7, r8, r9
        NO_RND_AVG32 r10, r11, r4, r5, r7, r8, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        ldmia r1, {r4-r6}
        add r1, r1, r2
        pld [r1]
        ADJ_ALIGN_DOUBLEWORD 3, r4, r5, r6
        subs r3, r3, #1
        NO_RND_AVG32 r10, r11, r7, r8, r4, r5, r12
        stmia r0, {r10-r11}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
        .align 8
5:
        .word 0xFEFEFEFE
        .word 2b
        .word 3b
        .word 4b

@ ----------------------------------------------------------------
.macro  RND_XY2_IT align, rnd
        @ l1=  (a & 0x03030303) + (b & 0x03030303) ?(+ 0x02020202)
        @ h1= ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2)
.if \align == 0
        ldmia r1, {r6-r8}
.elseif \align == 3
        ldmia r1, {r5-r7}
.else
        ldmia r1, {r8-r10}
.endif
        add r1, r1, r2
        pld [r1]
.if \align == 0
        ADJ_ALIGN_DOUBLEWORD_D 1, r4, r5, r6, r7, r8
.elseif \align == 1
        ADJ_ALIGN_DOUBLEWORD_D 1, r4, r5, r8, r9, r10
        ADJ_ALIGN_DOUBLEWORD_D 2, r6, r7, r8, r9, r10
.elseif \align == 2
        ADJ_ALIGN_DOUBLEWORD_D 2, r4, r5, r8, r9, r10
        ADJ_ALIGN_DOUBLEWORD_D 3, r6, r7, r8, r9, r10
.elseif \align == 3
        ADJ_ALIGN_DOUBLEWORD_D 3, r4, r5, r5, r6, r7
.endif
        ldr r14, [r12, #0]      @ 0x03030303
        tst r3, #1
        and r8, r4, r14
        and r9, r5, r14
        and r10, r6, r14
        and r11, r7, r14
.if \rnd == 1
        ldreq r14, [r12, #16]   @ 0x02020202
.else
        ldreq r14, [r12, #28]   @ 0x01010101
.endif
        add r8, r8, r10
        add r9, r9, r11
        addeq r8, r8, r14
        addeq r9, r9, r14
        ldr r14, [r12, #20]     @ 0xFCFCFCFC >> 2
        and r4, r14, r4, lsr #2
        and r5, r14, r5, lsr #2
        and r6, r14, r6, lsr #2
        and r7, r14, r7, lsr #2
        add r10, r4, r6
        add r11, r5, r7
.endm

.macro RND_XY2_EXPAND align, rnd
        RND_XY2_IT \align, \rnd
6:      stmfd sp!, {r8-r11}
        RND_XY2_IT \align, \rnd
        ldmfd sp!, {r4-r7}
        add r4, r4, r8
        add r5, r5, r9
        add r6, r6, r10
        add r7, r7, r11
        ldr r14, [r12, #24]     @ 0x0F0F0F0F
        and r4, r14, r4, lsr #2
        and r5, r14, r5, lsr #2
        add r4, r4, r6
        add r5, r5, r7
        subs r3, r3, #1
        stmia r0, {r4-r5}
        add r0, r0, r2
        bne 6b
        ldmfd sp!, {r4-r11,pc}
.endm

        .align 8
        .global put_pixels8_xy2_arm
put_pixels8_xy2_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r11,lr} @ R14 is also called LR
        adrl r12, 5f
        ands r4, r1, #3
        add r5, r12, r4, lsl #2
        bic r1, r1, #3
        ldrne pc, [r5]
1:
        RND_XY2_EXPAND 0, 1

        .align 8
2:
        RND_XY2_EXPAND 1, 1

        .align 8
3:
        RND_XY2_EXPAND 2, 1

        .align 8
4:
        RND_XY2_EXPAND 3, 1

5:
        .word 0x03030303
        .word 2b
        .word 3b
        .word 4b
        .word 0x02020202
        .word 0xFCFCFCFC >> 2
        .word 0x0F0F0F0F
        .word 0x01010101

        .align 8
        .global put_no_rnd_pixels8_xy2_arm
put_no_rnd_pixels8_xy2_arm:
        @ void func(uint8_t *block, const uint8_t *pixels, int line_size, int h)
        @ block = word aligned, pixles = unaligned
        pld [r1]
        stmfd sp!, {r4-r11,lr} @ R14 is also called LR
        adrl r12, 5f
        ands r4, r1, #3
        add r5, r12, r4, lsl #2
        bic r1, r1, #3
        ldrne pc, [r5]
1:
        RND_XY2_EXPAND 0, 0

        .align 8
2:
        RND_XY2_EXPAND 1, 0

        .align 8
3:
        RND_XY2_EXPAND 2, 0

        .align 8
4:
        RND_XY2_EXPAND 3, 0

5:
        .word 0x03030303
        .word 2b
        .word 3b
        .word 4b
        .word 0x02020202
        .word 0xFCFCFCFC >> 2
        .word 0x0F0F0F0F
        .word 0x01010101
