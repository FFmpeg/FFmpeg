/**
 * VP8 ARMv6 optimisations
 *
 * Copyright (c) 2011 The WebM project authors. All Rights Reserved.
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
 *
 * This code was partially ported from libvpx, which uses this license:
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 *
 * (Note that the "LICENSE", "AUTHORS" and "PATENTS" files can be
 *  found in the libvpx source tree.)
 */

#include "asm.S"

@ idct

@ void vp8_luma_dc_wht(DCTELEM block[4][4][16], DCTELEM dc[16])
function ff_vp8_luma_dc_wht_armv6, export=1
        push           {r4 - r10, lr}

        @ load dc[] and zero memory
        mov             r12, #0
        ldr             r2, [r1]                @ dc0[0,1]
        ldr             r3, [r1,  #4]           @ dc0[2,3]
        ldr             r4, [r1,  #8]           @ dc1[0,1]
        ldr             r5, [r1,  #12]          @ dc1[2,3]
        ldr             r6, [r1,  #16]          @ dc2[0,1]
        ldr             r7, [r1,  #20]          @ dc2[2,3]
        ldr             r8, [r1,  #24]          @ dc3[0,1]
        ldr             r9, [r1,  #28]          @ dc3[2,3]
        str             r12,[r1]
        str             r12,[r1,  #4]
        str             r12,[r1,  #8]
        str             r12,[r1,  #12]
        str             r12,[r1,  #16]
        str             r12,[r1,  #20]
        str             r12,[r1,  #24]
        str             r12,[r1,  #28]

        @ loop1
        uadd16          r12, r2,  r8            @ t0[0,1]
        uadd16          r14, r3,  r9            @ t0[2,3]
        usub16          r2,  r2,  r8            @ t3[0,1]
        usub16          r3,  r3,  r9            @ t3[2,3]
        uadd16          r8,  r4,  r6            @ t1[0,1]
        uadd16          r9,  r5,  r7            @ t1[2,3]
        usub16          r4,  r4,  r6            @ t2[0,1]
        usub16          r5,  r5,  r7            @ t2[2,3]

        uadd16          r6,  r12, r8            @ dc0[0,1]
        uadd16          r7,  r14, r9            @ dc0[2,3]
        usub16          r12, r12, r8            @ dc2[0,1]
        usub16          r14, r14, r9            @ dc2[2,3]
        uadd16          r8,  r2,  r4            @ dc1[0,1]
        uadd16          r9,  r3,  r5            @ dc1[2,3]
        usub16          r2,  r2,  r4            @ dc3[0,1]
        usub16          r3,  r3,  r5            @ dc3[2,3]

        mov             r1,  #3
        orr             r1,  r1,  #0x30000      @ 3 | 3 (round)

        @ "transpose"
        pkhbt           r4,  r6,  r8,  lsl #16  @ dc{0,1}[0]
        pkhtb           r6,  r8,  r6,  asr #16  @ dc{0,1}[1]
        pkhbt           r5,  r12, r2,  lsl #16  @ dc{2,3}[0]
        pkhtb           r12, r2,  r12, asr #16  @ dc{2,3}[1]
        pkhbt           r8,  r7,  r9,  lsl #16  @ dc{0,1}[2]
        uadd16          r4,  r4,  r1
        uadd16          r5,  r5,  r1
        pkhtb           r7,  r9,  r7,  asr #16  @ dc{0,1}[3]
        pkhbt           r2,  r14, r3,  lsl #16  @ dc{2,3}[2]
        pkhtb           r14, r3,  r14, asr #16  @ dc{2,3}[3]

        @ loop2
        uadd16          r9,  r4,  r7            @ t0[0,1]
        uadd16          r3,  r5,  r14           @ t0[2,3]
        usub16          r4,  r4,  r7            @ t3[0,1]
        usub16          r5,  r5,  r14           @ t3[2,3]
        uadd16          r7,  r6,  r8            @ t1[0,1]
        uadd16          r14, r12, r2            @ t1[2,3]
        usub16          r6,  r6,  r8            @ t2[0,1]
        usub16          r12, r12, r2            @ t2[2,3]

        uadd16          r8,  r9,  r7            @ block[0,1][0]
        uadd16          r2,  r3,  r14           @ block[2,3][0]
        usub16          r9,  r9,  r7            @ block[0,1][2]
        usub16          r3,  r3,  r14           @ block[2,3][2]
        uadd16          r7,  r4,  r6            @ block[0,1][1]
        uadd16          r14, r5,  r12           @ block[2,3][1]
        usub16          r4,  r4,  r6            @ block[0,1][3]
        usub16          r5,  r5,  r12           @ block[2,3][3]

        @ store
        mov             r6,  r8,  asr #19       @ block[1][0]
        mov             r12, r7,  asr #19       @ block[1][1]
        mov             r1,  r9,  asr #19       @ block[1][2]
        mov             r10, r4,  asr #19       @ block[1][3]
        sxth            r8,  r8
        sxth            r7,  r7
        sxth            r9,  r9
        sxth            r4,  r4
        asr             r8,  #3                 @ block[0][0]
        asr             r7,  #3                 @ block[0][1]
        asr             r9,  #3                 @ block[0][2]
        asr             r4,  #3                 @ block[0][3]

        strh            r8, [r0], #32
        strh            r7, [r0], #32
        strh            r9, [r0], #32
        strh            r4, [r0], #32
        strh            r6, [r0], #32
        strh            r12,[r0], #32
        strh            r1, [r0], #32
        strh            r10,[r0], #32

        mov             r6,  r2,  asr #19       @ block[3][0]
        mov             r12, r14, asr #19       @ block[3][1]
        mov             r1,  r3,  asr #19       @ block[3][2]
        mov             r10, r5,  asr #19       @ block[3][3]
        sxth            r2,  r2
        sxth            r14, r14
        sxth            r3,  r3
        sxth            r5,  r5
        asr             r2,  #3                 @ block[2][0]
        asr             r14, #3                 @ block[2][1]
        asr             r3,  #3                 @ block[2][2]
        asr             r5,  #3                 @ block[2][3]

        strh            r2, [r0], #32
        strh            r14,[r0], #32
        strh            r3, [r0], #32
        strh            r5, [r0], #32
        strh            r6, [r0], #32
        strh            r12,[r0], #32
        strh            r1, [r0], #32
        strh            r10,[r0], #32

        pop            {r4 - r10, pc}
endfunc

@ void vp8_luma_dc_wht_dc(DCTELEM block[4][4][16], DCTELEM dc[16])
function ff_vp8_luma_dc_wht_dc_armv6, export=1
        ldrsh           r2, [r1]
        mov             r3,  #0
        add             r2,  r2,  #3
        strh            r3, [r1]
        asr             r2,  r2,  #3
    .rept 16
        strh            r2, [r0], #32
    .endr
        bx              lr
endfunc

@ void vp8_idct_add(uint8_t *dst, DCTELEM block[16], int stride)
function ff_vp8_idct_add_armv6, export=1
        push           {r4 - r11, lr}
        sub             sp,  sp,  #32

        mov             r3,  #0x00004E00        @ cos
        orr             r3,  r3, #0x0000007B    @ cospi8sqrt2minus1 = 20091
        mov             r4,  #0x00008A00        @ sin
        orr             r4,  r4, #0x0000008C    @ sinpi8sqrt2 = 35468
        mov             r5,  #0x2               @ i=2
1:
        ldr             r6, [r1, #8]            @  i5 | i4  = block1[1] | block1[0]
        ldr             r12,[r1, #24]           @ i13 | i12 = block3[1] | block3[0]
        ldr             r14,[r1, #16]           @  i9 | i8  = block2[1] | block2[0]

        smulwt          r9,  r3,  r6            @ (ip[5] * cospi8sqrt2minus1) >> 16
        smulwb          r7,  r3,  r6            @ (ip[4] * cospi8sqrt2minus1) >> 16
        smulwt          r10, r4,  r6            @ (ip[5] * sinpi8sqrt2) >> 16
        smulwb          r8,  r4,  r6            @ (ip[4] * sinpi8sqrt2) >> 16
        pkhbt           r7,  r7,  r9,  lsl #16  @ 5c | 4c
        smulwt          r11, r3,  r12           @ (ip[13] * cospi8sqrt2minus1) >> 16
        pkhbt           r8,  r8,  r10, lsl #16  @ 5s | 4s         = t2 first half
        uadd16          r6,  r6,  r7            @ 5c+5 | 4c+4     = t3 first half
        smulwt          r7,  r4,  r12           @ (ip[13] * sinpi8sqrt2) >> 16
        smulwb          r9,  r3,  r12           @ (ip[12] * cospi8sqrt2minus1) >> 16
        smulwb          r10, r4,  r12           @ (ip[12] * sinpi8sqrt2) >> 16

        subs            r5,  r5,  #1            @ i--
        pkhbt           r9,  r9,  r11, lsl #16  @ 13c | 12c
        ldr             r11,[r1]                @  i1 | i0
        pkhbt           r10, r10, r7,  lsl #16  @ 13s | 12s       = t3 second half
        uadd16          r7,  r12, r9            @ 13c+13 | 12c+12 = t2 second half
        usub16          r7,  r8,  r7            @ c = t2
        uadd16          r6,  r6,  r10           @ d = t3
        uadd16          r10, r11, r14           @ a = t0
        usub16          r8,  r11, r14           @ b = t1
        uadd16          r9,  r10, r6            @ a+d = tmp{0,1}[0]
        usub16          r10, r10, r6            @ a-d = tmp{0,1}[3]
        uadd16          r6,  r8,  r7            @ b+c = tmp{0,1}[1]
        usub16          r7,  r8,  r7            @ b-c = tmp{0,1}[2]
        mov             r8,  #0
        str             r6, [sp,  #8]           @  o5 | o4
        str             r7, [sp,  #16]          @  o9 | o8
        str             r10,[sp,  #24]          @ o13 | o12
        str             r9, [sp], #4            @  o1 | o0
        str             r8, [r1,  #24]
        str             r8, [r1,  #16]
        str             r8, [r1,  #8]
        str             r8, [r1], #4
        bne             1b

        mov             r5,  #0x2               @ i=2
        sub             sp,  sp, #8
2:
        ldr             r6, [sp,  #8]           @ i5 | i4 = tmp{0,1}[1]
        ldr             r14,[sp,  #4]           @ i3 | i2 = tmp{2,3}[0]
        ldr             r12,[sp,  #12]          @ i7 | i6 = tmp{2,3}[1]
        ldr             r1, [sp], #16           @ i1 | i0 = tmp{0,1}[0]
        smulwt          r9,  r3,  r6            @ (ip[5] * cospi8sqrt2minus1) >> 16
        smulwt          r7,  r3,  r1            @ (ip[1] * cospi8sqrt2minus1) >> 16
        smulwt          r10, r4,  r6            @ (ip[5] * sinpi8sqrt2) >> 16
        smulwt          r8,  r4,  r1            @ (ip[1] * sinpi8sqrt2) >> 16
        pkhbt           r11, r1,  r6,  lsl #16  @ i4 | i0 = t0/t1 first half
        pkhbt           r7,  r7,  r9,  lsl #16  @ 5c | 1c
        pkhbt           r8,  r8,  r10, lsl #16  @ 5s | 1s = temp1 = t2 first half
        pkhtb           r1,  r6,  r1,  asr #16  @ i5 | i1
        uadd16          r1,  r7,  r1            @ 5c+5 | 1c+1 = temp2 (d) = t3 first half
        pkhbt           r9,  r14, r12, lsl #16  @ i6 | i2 = t0/t1 second half
        uadd16          r10, r11, r9            @ a = t0
        usub16          r9,  r11, r9            @ b = t1
        pkhtb           r6,  r12, r14, asr #16  @ i7 | i3
        subs            r5,  r5,  #0x1          @ i--
        smulwt          r7,  r3,  r6            @ (ip[7] * cospi8sqrt2minus1) >> 16
        smulwt          r11, r4,  r6            @ (ip[7] * sinpi8sqrt2) >> 16
        smulwb          r12, r3,  r6            @ (ip[3] * cospi8sqrt2minus1) >> 16
        smulwb          r14, r4,  r6            @ (ip[3] * sinpi8sqrt2) >> 16

        pkhbt           r7,  r12, r7,  lsl #16  @ 7c | 3c
        pkhbt           r11, r14, r11, lsl #16  @ 7s | 3s = temp1 (d) = t3 second half
        mov             r14, #0x4               @ set up 4's
        orr             r14, r14, #0x40000      @ 4|4
        uadd16          r6,  r7,  r6            @ 7c+7 | 3c+3 = temp2 (c) = t2 second half
        usub16          r12, r8,  r6            @ c (o5 | o1) = t2
        uadd16          r6,  r11, r1            @ d (o7 | o3) = t3
        uadd16          r10, r10, r14           @ t0 + 4
        uadd16          r9,  r9,  r14           @ t1 + 4
        uadd16          r7,  r10, r6            @ a+d = dst{0,1}[0]
        usub16          r6,  r10, r6            @ a-d = dst{0,1}[3]
        uadd16          r10, r9,  r12           @ b+c = dst{0,1}[1]
        usub16          r1,  r9,  r12           @ b-c = dst{0,1}[2]

        mov             r9,  r6,  asr #3        @ o[1][3]
        mov             r12, r1,  asr #3        @ o[1][2]
        pkhtb           r8,  r12, r7,  asr #19  @ o[1][0,2]
        pkhtb           r11, r9,  r10, asr #19  @ o[1][1,3]
        ldr             r12,[r0]
        ldr             r9, [r0,  r2]
        sxth            r7,  r7
        sxth            r6,  r6
        sxth            r10, r10
        sxth            r1,  r1
        asr             r7,  #3                 @ o[0][0]
        asr             r10, #3                 @ o[0][1]
        pkhbt           r7,  r7,  r1,  lsl #13  @ o[0][0,2]
        pkhbt           r10, r10, r6,  lsl #13  @ o[0][1,3]

        uxtab16         r7,  r7,  r12
        uxtab16         r10, r10, r12, ror #8
        uxtab16         r8,  r8,  r9
        uxtab16         r11, r11, r9,  ror #8
        usat16          r7,  #8,  r7
        usat16          r10, #8,  r10
        usat16          r8,  #8,  r8
        usat16          r11, #8,  r11
        orr             r7,  r7,  r10, lsl #8
        orr             r8,  r8,  r11, lsl #8
        str             r8, [r0,  r2]
        str_post        r7,  r0,  r2,  lsl #1

        bne             2b

        pop            {r4 - r11, pc}
endfunc

@ void vp8_idct_dc_add(uint8_t *dst, DCTELEM block[16], int stride)
function ff_vp8_idct_dc_add_armv6, export=1
        push           {r4 - r5,  lr}
        ldrsh           r3, [r1]
        mov             r4,  #0
        add             r3,  r3,  #4
        asr             r3,  #3
        strh            r4, [r1], #32
        ldr             r4, [r0,  r2]
        ldr_post        r5,  r0,  r2,  lsl #1
        pkhbt           r3,  r3,  r3,  lsl #16

        uxtab16         lr,  r3,  r5            @ a1+2 | a1+0
        uxtab16         r5,  r3,  r5,  ror #8   @ a1+3 | a1+1
        uxtab16         r12, r3,  r4
        uxtab16         r4,  r3,  r4,  ror #8
        usat16          lr,  #8,  lr
        usat16          r5,  #8,  r5
        usat16          r12, #8,  r12
        usat16          r4,  #8,  r4
        orr             lr,  lr,  r5,  lsl #8
        orr             r12, r12, r4,  lsl #8
        ldr             r5, [r0]
        ldr             r4, [r0,  r2]
        sub             r0,  r0,  r2,  lsl #1
        str             r12,[r0,  r2]
        str_post        lr,  r0,  r2,  lsl #1

        uxtab16         lr,  r3,  r5
        uxtab16         r5,  r3,  r5,  ror #8
        uxtab16         r12, r3,  r4
        uxtab16         r4,  r3,  r4,  ror #8
        usat16          lr,  #8,  lr
        usat16          r5,  #8,  r5
        usat16          r12, #8,  r12
        usat16          r4,  #8,  r4
        orr             lr,  lr,  r5,  lsl #8
        orr             r12, r12, r4,  lsl #8

        str             r12,[r0,  r2]
        str_post        lr,  r0,  r2,  lsl #1

        pop            {r4 - r5,  pc}
endfunc

@ void vp8_idct_dc_add4uv(uint8_t *dst, DCTELEM block[4][16], int stride)
function ff_vp8_idct_dc_add4uv_armv6, export=1
        push           {lr}

        bl              ff_vp8_idct_dc_add_armv6
        sub             r0,  r0,  r2,  lsl #2
        add             r0,  r0,  #4
        bl              ff_vp8_idct_dc_add_armv6
        sub             r0,  r0,  #4
        bl              ff_vp8_idct_dc_add_armv6
        sub             r0,  r0,  r2,  lsl #2
        add             r0,  r0,  #4
        bl              ff_vp8_idct_dc_add_armv6

        pop            {pc}
endfunc

@ void vp8_idct_dc_add4y(uint8_t *dst, DCTELEM block[4][16], int stride)
function ff_vp8_idct_dc_add4y_armv6, export=1
        push           {lr}

        bl              ff_vp8_idct_dc_add_armv6
        sub             r0,  r0,  r2,  lsl #2
        add             r0,  r0,  #4
        bl              ff_vp8_idct_dc_add_armv6
        sub             r0,  r0,  r2,  lsl #2
        add             r0,  r0,  #4
        bl              ff_vp8_idct_dc_add_armv6
        sub             r0,  r0,  r2,  lsl #2
        add             r0,  r0,  #4
        bl              ff_vp8_idct_dc_add_armv6

        pop            {pc}
endfunc

@ loopfilter

@ void vp8_v_loop_filter16_simple(uint8_t *dst, int stride, int flim)
function ff_vp8_v_loop_filter16_simple_armv6, export=1
        push           {r4 - r11, lr}

        ldr_dpren       r3,  r0,  r1,  lsl #1   @ p1
        ldr_dpren       r4,  r0,  r1            @ p0
        ldr             r5, [r0]                @ q0
        ldr             r6, [r0,  r1]           @ q1
        orr             r2,  r2,  r2,  lsl #16
        mov             r9,  #4                 @ count
        mov             lr,  #0                 @ need 0 in a couple places
        orr             r12, r2,  r2,  lsl #8   @ splat int -> byte
        ldr             r2,  c0x80808080

1:
        @ vp8_simple_filter_mask()
        uqsub8          r7,  r3,  r6            @ p1 - q1
        uqsub8          r8,  r6,  r3            @ q1 - p1
        uqsub8          r10, r4,  r5            @ p0 - q0
        uqsub8          r11, r5,  r4            @ q0 - p0
        orr             r8,  r8,  r7            @ abs(p1 - q1)
        orr             r10, r10, r11           @ abs(p0 - q0)
        uqadd8          r10, r10, r10           @ abs(p0 - q0) * 2
        uhadd8          r8,  r8,  lr            @ abs(p1 - q2) >> 1
        uqadd8          r10, r10, r8            @ abs(p0 - q0)*2 + abs(p1 - q1)/2
        mvn             r8,  #0
        usub8           r10, r12, r10           @ compare to flimit. usub8 sets GE flags
        sel             r10, r8,  lr            @ filter mask: F or 0
        cmp             r10, #0
        beq             2f                      @ skip filtering if all masks are 0x00

        @ vp8_simple_filter()
        eor             r3,  r3,  r2            @ p1 offset to convert to a signed value
        eor             r6,  r6,  r2            @ q1 offset to convert to a signed value
        eor             r4,  r4,  r2            @ p0 offset to convert to a signed value
        eor             r5,  r5,  r2            @ q0 offset to convert to a signed value

        qsub8           r3,  r3,  r6            @ vp8_filter = p1 - q1
        qsub8           r6,  r5,  r4            @ q0 - p0
        qadd8           r3,  r3,  r6            @ += q0 - p0
        ldr             r7,  c0x04040404
        qadd8           r3,  r3,  r6            @ += q0 - p0
        ldr             r8,  c0x03030303
        qadd8           r3,  r3,  r6            @ vp8_filter = p1-q1 + 3*(q0-p0))
        @STALL
        and             r3,  r3,  r10           @ vp8_filter &= mask

        qadd8           r7,  r3,  r7            @ Filter1 = vp8_filter + 4
        qadd8           r8,  r3,  r8            @ Filter2 = vp8_filter + 3

        shadd8          r7,  r7,  lr
        shadd8          r8,  r8,  lr
        shadd8          r7,  r7,  lr
        shadd8          r8,  r8,  lr
        shadd8          r7,  r7,  lr            @ Filter1 >>= 3
        shadd8          r8,  r8,  lr            @ Filter2 >>= 3

        qsub8           r5,  r5,  r7            @ u = q0 - Filter1
        qadd8           r4,  r4,  r8            @ u = p0 + Filter2
        eor             r5,  r5,  r2            @ *oq0 = u^0x80
        eor             r4,  r4,  r2            @ *op0 = u^0x80
T       sub             r7,  r0,  r1
        str             r5, [r0]                @ store oq0 result
A       str             r4, [r0, -r1]           @ store op0 result
T       str             r4, [r7]

2:
        subs            r9,  r9,  #1            @ counter--
        add             r0,  r0,  #4            @ next row
T       itttt           ne
A       ldrne           r3, [r0, -r1,  lsl #1]  @ p1
T       subne           r3,  r0,  r1,  lsl #1
T       ldrne           r3, [r3]                @ p1
A       ldrne           r4, [r0, -r1]           @ p0
T       subne           r4,  r0,  r1
T       ldrne           r4, [r4]                @ p0
T       itt             ne
        ldrne           r5, [r0]                @ q0
        ldrne           r6, [r0,  r1]           @ q1

        bne             1b

        pop            {r4 - r11, pc}
endfunc

c0x01010101: .long 0x01010101
c0x03030303: .long 0x03030303
c0x04040404: .long 0x04040404
c0x7F7F7F7F: .long 0x7F7F7F7F
c0x80808080: .long 0x80808080

@ void vp8_v_loop_filter16_inner(uint8_t *dst, int stride,
@                                int fE, int fI, int hev_thresh)
@ and
@ void vp8_v_loop_filter8uv_inner(uint8_t *dstU, uint8_t *dstV, int stride,
@                                 int fE, int fI, int hev_thresh)
@ call:
@ void vp8_v_loop_filter_inner(uint8_t *dst, int stride,
@                              int fE, int fI, int hev_thresh, int count)
function ff_vp8_v_loop_filter_inner_armv6, export=1
        push           {r4 - r11, lr}

        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines
        ldr             r5, [sp,  #40]          @ counter
        ldr             r6, [sp,  #36]          @ load thresh address
        sub             sp,  sp,  #16           @ create temp buffer

        ldr             r10,[r0,  r1]           @ p2
        ldr_post        r9,  r0,  r1,  lsl #1   @ p3
        ldr             r12,[r0,  r1]           @ p0
        ldr_post        r11, r0,  r1,  lsl #1   @ p1

        orr             r2,  r2,  r2,  lsl #16
        orr             r3,  r3,  r3,  lsl #16
        orr             r6,  r6,  r6,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE  splat int -> byte
        orr             r2,  r3,  r3,  lsl #8   @ flimI  splat int -> byte
        orr             r3,  r6,  r6,  lsl #8   @ thresh splat int -> byte

1:
        @ vp8_filter_mask() function
        @ calculate breakout conditions
        uqsub8          r6,  r9,  r10           @ p3 - p2
        uqsub8          r7,  r10, r9            @ p2 - p3
        uqsub8          r8,  r10, r11           @ p2 - p1
        uqsub8          r10, r11, r10           @ p1 - p2

        orr             r6,  r6,  r7            @ abs (p3-p2)
        orr             r8,  r8,  r10           @ abs (p2-p1)
        uqsub8          lr,  r6,  r2            @ compare to limit. lr: vp8_filter_mask
        uqsub8          r8,  r8,  r2            @ compare to limit
        uqsub8          r6,  r11, r12           @ p1 - p0
        orr             lr,  lr,  r8
        uqsub8          r7,  r12, r11           @ p0 - p1
        ldr             r10,[r0,  r1]           @ q1
        ldr_post        r9,  r0,  r1,  lsl #1   @ q0
        orr             r6,  r6,  r7            @ abs (p1-p0)
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r8,  r6,  r3            @ compare to thresh  -- save r8 for later
        orr             lr,  lr,  r7

        uqsub8          r6,  r11, r10           @ p1 - q1
        uqsub8          r7,  r10, r11           @ q1 - p1
        uqsub8          r11, r12, r9            @ p0 - q0
        uqsub8          r12, r9,  r12           @ q0 - p0
        orr             r6,  r6,  r7            @ abs (p1-q1)
        ldr             r7,  c0x7F7F7F7F
        orr             r12, r11, r12           @ abs (p0-q0)
        ldr_post        r11, r0,  r1            @ q2
        uqadd8          r12, r12, r12           @ abs (p0-q0) * 2
        and             r6,  r7,  r6,  lsr #1   @ abs (p1-q1) / 2
        uqsub8          r7,  r9,  r10           @ q0 - q1
        uqadd8          r12, r12, r6            @ abs (p0-q0)*2 + abs (p1-q1)/2
        uqsub8          r6,  r10, r9            @ q1 - q0
        uqsub8          r12, r12, r4            @ compare to flimit
        uqsub8          r9,  r11, r10           @ q2 - q1

        orr             lr, lr, r12

        ldr_post        r12, r0,  r1            @ q3
        uqsub8          r10, r10, r11           @ q1 - q2
        orr             r6,  r7,  r6            @ abs (q1-q0)
        orr             r10, r9,  r10           @ abs (q2-q1)
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r10, r10, r2            @ compare to limit
        uqsub8          r6,  r6,  r3            @ compare to thresh -- save r6 for later
        orr             lr,  lr,  r7
        orr             lr,  lr,  r10

        uqsub8          r10, r12, r11           @ q3 - q2
        uqsub8          r9,  r11, r12           @ q2 - q3

        mvn             r11, #0                 @ r11 == -1

        orr             r10, r10, r9            @ abs (q3-q2)
        uqsub8          r10, r10, r2            @ compare to limit

        mov             r12, #0
        orr             lr,  lr,  r10
        sub             r0,  r0,  r1,  lsl #2

        usub8           lr,  r12, lr            @ use usub8 instead of ssub8
        sel             lr,  r11, r12           @ filter mask: lr

        cmp             lr,  #0
        beq             2f                      @ skip filtering

        sub             r0,  r0,  r1,  lsl #1   @ move r0 pointer down by 6 lines

        @vp8_hevmask() function
        @calculate high edge variance
        orr             r10, r6,  r8            @ calculate vp8_hevmask

        usub8           r10, r12, r10           @ use usub8 instead of ssub8
        sel             r6,  r12, r11           @ obtain vp8_hevmask: r6

        @vp8_filter() function
        ldr             r8, [r0,  r1]           @ p0
        ldr_post        r7,  r0,  r1,  lsl #1   @ p1
        ldr             r12, c0x80808080
        ldr             r10,[r0,  r1]           @ q1
        ldr_post        r9,  r0,  r1,  lsl #1   @ q0

        eor             r7,  r7,  r12           @ p1 offset to convert to a signed value
        eor             r8,  r8,  r12           @ p0 offset to convert to a signed value
        eor             r9,  r9,  r12           @ q0 offset to convert to a signed value
        eor             r10, r10, r12           @ q1 offset to convert to a signed value

        str             r9, [sp]                @ store qs0 temporarily
        str             r8, [sp,  #4]           @ store ps0 temporarily
        str             r10,[sp,  #8]           @ store qs1 temporarily
        str             r7, [sp,  #12]          @ store ps1 temporarily

        qsub8           r7,  r7,  r10           @ vp8_signed_char_clamp(ps1-qs1)
        qsub8           r8,  r9,  r8            @ vp8_signed_char_clamp(vp8_filter + 3 * ( qs0 - ps0))

        and             r7,  r7,  r6            @ vp8_filter (r7) &= hev

        qadd8           r7,  r7,  r8
        ldr             r9,  c0x03030303        @ r9 = 3 --modified for vp8

        qadd8           r7,  r7,  r8
        ldr             r10, c0x04040404

        qadd8           r7,  r7,  r8
        and             r7,  r7,  lr            @ vp8_filter &= mask@

        qadd8           r8,  r7,  r9            @ Filter2 (r8) = vp8_signed_char_clamp(vp8_filter+3)
        qadd8           r7,  r7,  r10           @ vp8_filter = vp8_signed_char_clamp(vp8_filter+4)

        mov             r9,  #0
        shadd8          r8,  r8,  r9            @ Filter2 >>= 3
        shadd8          r7,  r7,  r9            @ vp8_filter >>= 3
        shadd8          r8,  r8,  r9
        shadd8          r7,  r7,  r9
        shadd8          lr,  r8,  r9            @ lr: Filter2
        shadd8          r7,  r7,  r9            @ r7: filter

        @calculate output

        ldr             r8, [sp]                @ load qs0
        ldr             r9, [sp,  #4]           @ load ps0

        ldr             r10, c0x01010101

        qsub8           r8,  r8,  r7            @ u = vp8_signed_char_clamp(qs0 - vp8_filter)
        qadd8           r9,  r9,  lr            @ u = vp8_signed_char_clamp(ps0 + Filter2)

        mov             lr,  #0
        sadd8           r7,  r7,  r10           @ vp8_filter += 1
        shadd8          r7,  r7,  lr            @ vp8_filter >>= 1

        ldr             r11,[sp,  #12]          @ load ps1
        ldr             r10,[sp,  #8]           @ load qs1

        bic             r7,  r7,  r6            @ vp8_filter &= ~hev
        sub             r0,  r0,  r1,  lsl #2

        qadd8           r11, r11, r7            @ u = vp8_signed_char_clamp(ps1 + vp8_filter)
        qsub8           r10, r10, r7            @ u = vp8_signed_char_clamp(qs1 - vp8_filter)

        eor             r11, r11, r12           @ *op1 = u^0x80
        eor             r9,  r9,  r12           @ *op0 = u^0x80
        eor             r8,  r8,  r12           @ *oq0 = u^0x80
        eor             r10, r10, r12           @ *oq1 = u^0x80
        str             r9, [r0,  r1]           @ store op0 result
        str_post        r11, r0,  r1,  lsl #1   @ store op1
        str             r10,[r0,  r1]           @ store oq1
        str_post        r8,  r0,  r1,  lsl #1   @ store oq0 result

        sub             r0,  r0,  r1,  lsl #1

2:
        add             r0,  r0,  #4
        sub             r0,  r0,  r1,  lsl #2

        subs            r5,  r5,  #1
T       ittt            ne
        ldrne           r10,[r0,  r1]           @ p2
A       ldrne           r9, [r0], r1,  lsl #1   @ p3
T       ldrne           r9, [r0]                @ p3
T       addne           r0,  r0,  r1,  lsl #1
T       ittt            ne
        ldrne           r12,[r0,  r1]           @ p0
A       ldrne           r11,[r0], r1,  lsl #1   @ p1
T       ldrne           r11,[r0]                @ p3
T       addne           r0,  r0,  r1,  lsl #1

        bne             1b

        add             sp,  sp,  #16
        pop            {r4 - r11, pc}
endfunc

@ void vp8_v_loop_filter16(uint8_t *dst, int stride,
@                          int fE, int fI, int hev_thresh)
@ and
@ void vp8_v_loop_filter8uv(uint8_t *dstU, uint8_t *dstV, int stride,
@                           int fE, int fI, int hev_thresh)
@ call:
@ void vp8_v_loop_filter(uint8_t *dst, int stride,
@                        int fE, int fI, int hev_thresh, int count)
function ff_vp8_v_loop_filter_armv6, export=1
        push           {r4 - r11, lr}

        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines
        ldr             r5, [sp,  #40]          @ counter
        ldr             r6, [sp,  #36]          @ load thresh address
        sub             sp,  sp,  #16           @ create temp buffer

        ldr             r10,[r0,  r1]           @ p2
        ldr_post        r9,  r0,  r1,  lsl #1   @ p3
        ldr             r12,[r0,  r1]           @ p0
        ldr_post        r11, r0,  r1,  lsl #1   @ p1

        orr             r2,  r2,  r2,  lsl #16
        orr             r3,  r3,  r3,  lsl #16
        orr             r6,  r6,  r6,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE  splat int -> byte
        orr             r2,  r3,  r3,  lsl #8   @ flimI  splat int -> byte
        orr             r3,  r6,  r6,  lsl #8   @ thresh splat int -> byte

1:
        @ vp8_filter_mask() function
        @ calculate breakout conditions
        uqsub8          r6,  r9,  r10           @ p3 - p2
        uqsub8          r7,  r10, r9            @ p2 - p3
        uqsub8          r8,  r10, r11           @ p2 - p1
        uqsub8          r10, r11, r10           @ p1 - p2

        orr             r6,  r6,  r7            @ abs (p3-p2)
        orr             r8,  r8,  r10           @ abs (p2-p1)
        uqsub8          lr,  r6,  r2            @ compare to limit. lr: vp8_filter_mask
        uqsub8          r8,  r8,  r2            @ compare to limit

        uqsub8          r6,  r11, r12           @ p1 - p0
        orr             lr,  lr,  r8
        uqsub8          r7,  r12, r11           @ p0 - p1
        ldr             r10,[r0,  r1]           @ q1
        ldr_post        r9,  r0,  r1,  lsl #1   @ q0
        orr             r6,  r6,  r7            @ abs (p1-p0)
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r8,  r6,  r3            @ compare to thresh  -- save r8 for later
        orr             lr,  lr,  r7

        uqsub8          r6,  r11, r10           @ p1 - q1
        uqsub8          r7,  r10, r11           @ q1 - p1
        uqsub8          r11, r12, r9            @ p0 - q0
        uqsub8          r12, r9,  r12           @ q0 - p0
        orr             r6,  r6,  r7            @ abs (p1-q1)
        ldr             r7,  c0x7F7F7F7F
        orr             r12, r11, r12           @ abs (p0-q0)
        ldr_post        r11, r0,  r1            @ q2
        uqadd8          r12, r12, r12           @ abs (p0-q0) * 2
        and             r6,  r7,  r6,  lsr #1   @ abs (p1-q1) / 2
        uqsub8          r7,  r9,  r10           @ q0 - q1
        uqadd8          r12, r12, r6            @ abs (p0-q0)*2 + abs (p1-q1)/2
        uqsub8          r6,  r10, r9            @ q1 - q0
        uqsub8          r12, r12, r4            @ compare to flimit
        uqsub8          r9,  r11, r10           @ q2 - q1

        orr             lr,  lr,  r12

        ldr_post        r12, r0,  r1            @ q3

        uqsub8          r10, r10, r11           @ q1 - q2
        orr             r6,  r7,  r6            @ abs (q1-q0)
        orr             r10, r9,  r10           @ abs (q2-q1)
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r10, r10, r2            @ compare to limit
        uqsub8          r6,  r6,  r3            @ compare to thresh -- save r6 for later
        orr             lr,  lr,  r7
        orr             lr,  lr,  r10

        uqsub8          r10, r12, r11           @ q3 - q2
        uqsub8          r9,  r11, r12           @ q2 - q3

        mvn             r11, #0                 @ r11 == -1

        orr             r10, r10, r9            @ abs (q3-q2)
        uqsub8          r10, r10, r2            @ compare to limit

        mov             r12, #0

        orr             lr,  lr,  r10

        usub8           lr,  r12, lr            @ use usub8 instead of ssub8
        sel             lr,  r11, r12           @ filter mask: lr

        cmp             lr,  #0
        beq             2f                      @ skip filtering

        @vp8_hevmask() function
        @calculate high edge variance
        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 6 lines
        sub             r0,  r0,  r1,  lsl #1

        orr             r10, r6,  r8

        usub8           r10, r12, r10
        sel             r6,  r12, r11           @ hev mask: r6

        @vp8_mbfilter() function
        @p2, q2 are only needed at the end. Do not need to load them in now.
        ldr             r8, [r0,  r1]           @ p0
        ldr_post        r7,  r0,  r1,  lsl #1   @ p1
        ldr             r12, c0x80808080
        ldr_post        r9,  r0,  r1            @ q0
        ldr             r10,[r0]                @ q1

        eor             r7,  r7,  r12           @ ps1
        eor             r8,  r8,  r12           @ ps0
        eor             r9,  r9,  r12           @ qs0
        eor             r10, r10, r12           @ qs1

        qsub8           r12, r9,  r8            @ vp8_signed_char_clamp(vp8_filter + 3 * ( qs0 - ps0))
        str             r7, [sp,  #12]          @ store ps1 temporarily
        qsub8           r7,  r7,  r10           @ vp8_signed_char_clamp(ps1-qs1)
        str             r10,[sp,  #8]           @ store qs1 temporarily
        qadd8           r7,  r7,  r12
        str             r9, [sp]                @ store qs0 temporarily
        qadd8           r7,  r7,  r12
        str             r8, [sp,  #4]           @ store ps0 temporarily
        qadd8           r7,  r7,  r12           @ vp8_filter: r7

        ldr             r10, c0x03030303        @ r10 = 3 --modified for vp8
        ldr             r9,  c0x04040404

        and             r7,  r7,  lr            @ vp8_filter &= mask (lr is free)

        mov             r12, r7                 @ Filter2: r12
        and             r12, r12, r6            @ Filter2 &= hev

        @save bottom 3 bits so that we round one side +4 and the other +3
        qadd8           r8,  r12, r9            @ Filter1 (r8) = vp8_signed_char_clamp(Filter2+4)
        qadd8           r12, r12, r10           @ Filter2 (r12) = vp8_signed_char_clamp(Filter2+3)

        mov             r10, #0
        shadd8          r8,  r8,  r10           @ Filter1 >>= 3
        shadd8          r12, r12, r10           @ Filter2 >>= 3
        shadd8          r8,  r8,  r10
        shadd8          r12, r12, r10
        shadd8          r8,  r8,  r10           @ r8: Filter1
        shadd8          r12, r12, r10           @ r12: Filter2

        ldr             r9, [sp]                @ load qs0
        ldr             r11,[sp,  #4]           @ load ps0

        qsub8           r9,  r9,  r8            @ qs0 = vp8_signed_char_clamp(qs0 - Filter1)
        qadd8           r11, r11, r12           @ ps0 = vp8_signed_char_clamp(ps0 + Filter2)

        bic             r12, r7,  r6            @ vp8_filter &= ~hev    ( r6 is free)

        @roughly 3/7th difference across boundary
        mov             lr,  #0x1b              @ 27
        mov             r7,  #0x3f              @ 63

        sxtb16          r6,  r12
        sxtb16          r10, r12, ror #8
        smlabb          r8,  r6,  lr,  r7
        smlatb          r6,  r6,  lr,  r7
        smlabb          r7,  r10, lr,  r7
        smultb          r10, r10, lr
        ssat            r8,  #8,  r8,  asr #7
        ssat            r6,  #8,  r6,  asr #7
        add             r10, r10, #63
        ssat            r7,  #8,  r7,  asr #7
        ssat            r10, #8,  r10, asr #7

        ldr             lr,  c0x80808080

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r7,  r10, lsl #16
        uxtb16          r6,  r6
        uxtb16          r10, r10

        sub             r0,  r0,  r1

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 27)>>7)

        qsub8           r8,  r9,  r10           @ s = vp8_signed_char_clamp(qs0 - u)
        qadd8           r10, r11, r10           @ s = vp8_signed_char_clamp(ps0 + u)
        eor             r8,  r8,  lr            @ *oq0 = s^0x80
        str             r8, [r0]                @ store *oq0
        sub             r0,  r0,  r1
        eor             r10, r10, lr            @ *op0 = s^0x80
        str             r10,[r0]                @ store *op0

        @roughly 2/7th difference across boundary
        mov             lr,  #0x12              @ 18
        mov             r7,  #0x3f              @ 63

        sxtb16          r6,  r12
        sxtb16          r10, r12, ror #8
        smlabb          r8,  r6,  lr,  r7
        smlatb          r6,  r6,  lr,  r7
        smlabb          r9,  r10, lr,  r7
        smlatb          r10, r10, lr,  r7
        ssat            r8,  #8,  r8,  asr #7
        ssat            r6,  #8,  r6,  asr #7
        ssat            r9,  #8,  r9,  asr #7
        ssat            r10, #8,  r10, asr #7

        ldr             lr,  c0x80808080

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r9,  r10, lsl #16

        ldr             r9,  [sp,  #8]          @ load qs1
        ldr             r11, [sp,  #12]         @ load ps1

        uxtb16          r6,  r6
        uxtb16          r10, r10

        sub             r0,  r0,  r1

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 18)>>7)

        qadd8           r11, r11, r10           @ s = vp8_signed_char_clamp(ps1 + u)
        qsub8           r8,  r9,  r10           @ s = vp8_signed_char_clamp(qs1 - u)
        eor             r11, r11, lr            @ *op1 = s^0x80
        str_post        r11, r0,  r1            @ store *op1
        eor             r8,  r8,  lr            @ *oq1 = s^0x80
        add             r0,  r0,  r1,  lsl #1

        mov             r7,  #0x3f              @ 63

        str_post        r8,  r0,  r1            @ store *oq1

        @roughly 1/7th difference across boundary
        mov             lr,  #0x9               @ 9
        ldr             r9, [r0]                @ load q2

        sxtb16          r6,  r12
        sxtb16          r10, r12, ror #8
        smlabb          r8,  r6,  lr,  r7
        smlatb          r6,  r6,  lr,  r7
        smlabb          r12, r10, lr,  r7
        smlatb          r10, r10, lr,  r7
        ssat            r8,  #8,  r8,  asr #7
        ssat            r6,  #8,  r6,  asr #7
        ssat            r12, #8,  r12, asr #7
        ssat            r10, #8,  r10, asr #7

        sub             r0,  r0,  r1,  lsl #2

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r12, r10, lsl #16

        sub             r0,  r0,  r1
        ldr             lr,  c0x80808080

        ldr             r11, [r0]               @ load p2

        uxtb16          r6,  r6
        uxtb16          r10, r10

        eor             r9,  r9,  lr
        eor             r11, r11, lr

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 9)>>7)

        qadd8           r8,  r11, r10           @ s = vp8_signed_char_clamp(ps2 + u)
        qsub8           r10, r9,  r10           @ s = vp8_signed_char_clamp(qs2 - u)
        eor             r8,  r8,  lr            @ *op2 = s^0x80
        str_post        r8,  r0,  r1,  lsl #2   @ store *op2
        add             r0,  r0,  r1
        eor             r10, r10, lr            @ *oq2 = s^0x80
        str_post        r10, r0,  r1,  lsl #1   @ store *oq2

2:
        add             r0,  r0,  #4
        sub             r0,  r0,  r1,  lsl #3
        subs            r5,  r5,  #1

T       ittt            ne
        ldrne           r10,[r0,  r1]           @ p2
A       ldrne           r9, [r0], r1,  lsl #1   @ p3
T       ldrne           r9, [r0]                @ p3
T       addne           r0,  r0,  r1,  lsl #1
T       ittt            ne
        ldrne           r12,[r0,  r1]           @ p0
A       ldrne           r11,[r0], r1,  lsl #1   @ p1
T       ldrne           r11,[r0]                @ p3
T       addne           r0,  r0,  r1,  lsl #1

        bne             1b

        add             sp,  sp,  #16
        pop            {r4 - r11, pc}
endfunc

.macro TRANSPOSE_MATRIX i0, i1, i2, i3, o3, o2, o1, o0
        @ input:  $0, $1, $2, $3
        @ output: $4, $5, $6, $7
        @ i0: 03 02 01 00
        @ i1: 13 12 11 10
        @ i2: 23 22 21 20
        @ i3: 33 32 31 30
        @     o3 o2 o1 o0

        uxtb16          \o1, \i1                @ xx 12 xx 10
        uxtb16          \o0, \i0                @ xx 02 xx 00
        uxtb16          \o3, \i3                @ xx 32 xx 30
        uxtb16          \o2, \i2                @ xx 22 xx 20
        orr             \o1, \o0, \o1, lsl #8   @ 12 02 10 00
        orr             \o3, \o2, \o3, lsl #8   @ 32 22 30 20

        uxtb16          \i1, \i1, ror #8        @ xx 13 xx 11
        uxtb16          \i3, \i3, ror #8        @ xx 33 xx 31
        uxtb16          \i0, \i0, ror #8        @ xx 03 xx 01
        uxtb16          \i2, \i2, ror #8        @ xx 23 xx 21
        orr             \i0, \i0, \i1, lsl #8   @ 13 03 11 01
        orr             \i2, \i2, \i3, lsl #8   @ 33 23 31 21

        pkhtb           \o2, \o3, \o1, asr #16  @ 32 22 12 02   -- p1
        pkhbt           \o0, \o1, \o3, lsl #16  @ 30 20 10 00   -- p3

        pkhtb           \o3, \i2, \i0, asr #16  @ 33 23 13 03   -- p0
        pkhbt           \o1, \i0, \i2, lsl #16  @ 31 21 11 01   -- p2
.endm

@ void vp8_h_loop_filter16_simple(uint8_t *dst, int stride, int flim)
function ff_vp8_h_loop_filter16_simple_armv6, export=1
        push           {r4 - r11, lr}
        orr             r12, r2,  r2,  lsl #16
        ldr             r2,  c0x80808080
        orr             r12, r12, r12, lsl #8

        @ load soure data to r7, r8, r9, r10
        sub             r0,  r0,  #2
        ldr             r8, [r0,  r1]
        ldr_post        r7,  r0,  r1,  lsl #1
        ldr             r10,[r0,  r1]
        ldr_post        r9,  r0,  r1,  lsl #1
        add             r0,  r0,  #2

        mov             r11, #4                 @ count (r11) for 4-in-parallel
1:
        @transpose r7, r8, r9, r10 to r3, r4, r5, r6
        TRANSPOSE_MATRIX r7, r8, r9, r10, r6, r5, r4, r3

        @ vp8_simple_filter_mask() function
        uqsub8          r7,  r3,  r6            @ p1 - q1
        uqsub8          r8,  r6,  r3            @ q1 - p1
        uqsub8          r9,  r4,  r5            @ p0 - q0
        uqsub8          r10, r5,  r4            @ q0 - p0
        orr             r7,  r7,  r8            @ abs(p1 - q1)
        orr             r9,  r9,  r10           @ abs(p0 - q0)
        mov             r8,  #0
        uqadd8          r9,  r9,  r9            @ abs(p0 - q0) * 2
        uhadd8          r7,  r7,  r8            @ abs(p1 - q1) / 2
        uqadd8          r7,  r7,  r9            @ abs(p0 - q0)*2 + abs(p1 - q1)/2
        mvn             r10, #0                 @ r10 == -1

        usub8           r7,  r12, r7            @ compare to flimit
        sel             lr,  r10, r8            @ filter mask

        cmp             lr,  #0
        beq             2f                      @ skip filtering

        @vp8_simple_filter() function
        eor             r3,  r3,  r2            @ p1 offset to convert to a signed value
        eor             r6,  r6,  r2            @ q1 offset to convert to a signed value
        eor             r4,  r4,  r2            @ p0 offset to convert to a signed value
        eor             r5,  r5,  r2            @ q0 offset to convert to a signed value

        qsub8           r3,  r3,  r6            @ vp8_filter = p1 - q1
        qsub8           r6,  r5,  r4            @ q0 - p0

        qadd8           r3,  r3,  r6            @ vp8_filter += q0 - p0
        ldr             r9,  c0x03030303        @ r9 = 3

        qadd8           r3,  r3,  r6            @ vp8_filter += q0 - p0
        ldr             r7,  c0x04040404

        qadd8           r3,  r3,  r6            @ vp8_filter = p1-q1 + 3*(q0-p0))
        @STALL
        and             r3,  r3,  lr            @ vp8_filter &= mask

        qadd8           r9,  r3,  r9            @ Filter2 = vp8_filter + 3
        qadd8           r3,  r3,  r7            @ Filter1 = vp8_filter + 4

        shadd8          r9,  r9,  r8
        shadd8          r3,  r3,  r8
        shadd8          r9,  r9,  r8
        shadd8          r3,  r3,  r8
        shadd8          r9,  r9,  r8            @ Filter2 >>= 3
        shadd8          r3,  r3,  r8            @ Filter1 >>= 3

        @calculate output
        sub             r0,  r0,  r1,  lsl #2

        qadd8           r4,  r4,  r9            @ u = p0 + Filter2
        qsub8           r5,  r5,  r3            @ u = q0 - Filter1
        eor             r4,  r4,  r2            @ *op0 = u^0x80
        eor             r5,  r5,  r2            @ *oq0 = u^0x80

        strb            r4, [r0,  #-1]          @ store the result
        mov             r4,  r4,  lsr #8
        strb_post       r5,  r0,  r1
        mov             r5,  r5,  lsr #8

        strb            r4, [r0,  #-1]
        mov             r4,  r4,  lsr #8
        strb_post       r5,  r0,  r1
        mov             r5,  r5,  lsr #8

        strb            r4, [r0,  #-1]
        mov             r4,  r4,  lsr #8
        strb_post       r5,  r0,  r1
        mov             r5,  r5,  lsr #8

        strb            r4, [r0,  #-1]
        strb_post       r5,  r0,  r1

2:
        subs            r11, r11, #1

        @ load soure data to r7, r8, r9, r10
        sub             r0,  r0,  #2
T       ittt            ne
        ldrne           r8, [r0,  r1]
A       ldrne           r7, [r0], r1,  lsl #1
T       ldrne           r7, [r0]
T       addne           r0,  r0,  r1,  lsl #1
T       ittt            ne
        ldrne           r10,[r0,  r1]
A       ldrne           r9, [r0], r1,  lsl #1
T       ldrne           r9, [r0]
T       addne           r0,  r0,  r1,  lsl #1
        add             r0,  r0,  #2

        bne             1b

        pop            {r4 - r11, pc}
endfunc

@ void vp8_h_loop_filter16_inner(uint8_t *dst, int stride,
@                                int fE, int fI, int hev_thresh)
@ and
@ void vp8_h_loop_filter8uv_inner(uint8_t *dstU, uint8_t *dstV, int stride,
@                          int fE, int fI, int hev_thresh)
@ call:
@ void vp8_h_loop_filter_inner(uint8_t *dst, int stride,
@                              int fE, int fI, int hev_thresh, int count)
function ff_vp8_h_loop_filter_inner_armv6, export=1
        push           {r4 - r11, lr}

        sub             r0,  r0,  #4            @ move r0 pointer down by 4
        ldr             r5, [sp,  #40]          @ counter
        ldr             r9, [sp,  #36]          @ load thresh address
        sub             sp,  sp,  #16           @ create temp buffer

        ldr             r7, [r0,  r1]           @ transpose will make it into p3-p0
        ldr_post        r6,  r0,  r1,  lsl #1   @ load source data
        ldr             lr, [r0,  r1]
        ldr_post        r8,  r0,  r1,  lsl #1

        orr             r2,  r2,  r2,  lsl #16
        orr             r3,  r3,  r3,  lsl #16
        orr             r9,  r9,  r9,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE  splat int -> byte
        orr             r2,  r3,  r3,  lsl #8   @ flimI  splat int -> byte
        orr             r3,  r9,  r9,  lsl #8   @ thresh splat int -> byte

1:
        @ vp8_filter_mask() function
        @ calculate breakout conditions
        @ transpose the source data for 4-in-parallel operation
        TRANSPOSE_MATRIX r6, r7, r8, lr, r12, r11, r10, r9

        uqsub8          r7,  r9,  r10           @ p3 - p2
        uqsub8          r8,  r10, r9            @ p2 - p3
        uqsub8          r9,  r10, r11           @ p2 - p1
        uqsub8          r10, r11, r10           @ p1 - p2
        orr             r7,  r7,  r8            @ abs (p3-p2)
        orr             r10, r9,  r10           @ abs (p2-p1)
        uqsub8          lr,  r7,  r2            @ compare to limit. lr: vp8_filter_mask
        uqsub8          r10, r10, r2            @ compare to limit

        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines

        orr             lr,  lr,  r10

        uqsub8          r6,  r11, r12           @ p1 - p0
        uqsub8          r7,  r12, r11           @ p0 - p1
        add             r0,  r0,  #4            @ move r0 pointer up by 4
        orr             r6,  r6,  r7            @ abs (p1-p0)
        str             r11,[sp,  #12]          @ save p1
        uqsub8          r10, r6,  r2            @ compare to limit
        uqsub8          r11, r6,  r3            @ compare to thresh
        orr             lr,  lr,  r10

        @ transpose uses 8 regs(r6 - r12 and lr). Need to save reg value now
        @ transpose the source data for 4-in-parallel operation
        str             r11,[sp]                @ push r11 to stack
        ldr             r7, [r0,  r1]
        ldr_post        r6,  r0,  r1,  lsl #1   @ load source data
        str             r12,[sp,  #4]           @ save current reg before load q0 - q3 data
        str             lr, [sp,  #8]
        ldr             lr, [r0,  r1]
        ldr_post        r8,  r0,  r1,  lsl #1

        TRANSPOSE_MATRIX r6, r7, r8, lr, r12, r11, r10, r9

        ldr             lr, [sp, #8]            @ load back (f)limit accumulator

        uqsub8          r6,  r12, r11           @ q3 - q2
        uqsub8          r7,  r11, r12           @ q2 - q3
        uqsub8          r12, r11, r10           @ q2 - q1
        uqsub8          r11, r10, r11           @ q1 - q2
        orr             r6,  r6,  r7            @ abs (q3-q2)
        orr             r7,  r12, r11           @ abs (q2-q1)
        uqsub8          r6,  r6,  r2            @ compare to limit
        uqsub8          r7,  r7,  r2            @ compare to limit
        ldr             r11,[sp,  #4]           @ load back p0
        ldr             r12,[sp,  #12]          @ load back p1
        orr             lr,  lr,  r6
        orr             lr,  lr,  r7

        uqsub8          r6,  r11, r9            @ p0 - q0
        uqsub8          r7,  r9,  r11           @ q0 - p0
        uqsub8          r8,  r12, r10           @ p1 - q1
        uqsub8          r11, r10, r12           @ q1 - p1
        orr             r6,  r6,  r7            @ abs (p0-q0)
        ldr             r7,  c0x7F7F7F7F
        orr             r8,  r8,  r11           @ abs (p1-q1)
        uqadd8          r6,  r6,  r6            @ abs (p0-q0) * 2
        and             r8,  r7,  r8,  lsr #1   @ abs (p1-q1) / 2
        uqsub8          r11, r10, r9            @ q1 - q0
        uqadd8          r6,  r8,  r6            @ abs (p0-q0)*2 + abs (p1-q1)/2
        uqsub8          r12, r9,  r10           @ q0 - q1
        uqsub8          r6,  r6,  r4            @ compare to flimit

        orr             r9,  r11, r12           @ abs (q1-q0)
        uqsub8          r8,  r9,  r2            @ compare to limit
        uqsub8          r10, r9,  r3            @ compare to thresh
        orr             lr,  lr,  r6
        orr             lr,  lr,  r8

        mvn             r11, #0                 @ r11 == -1
        mov             r12, #0

        usub8           lr,  r12, lr
        ldr             r9, [sp]                @ load the compared result
        sel             lr,  r11, r12           @ filter mask: lr

        cmp             lr,  #0
        beq             2f                      @ skip filtering

        @vp8_hevmask() function
        @calculate high edge variance
        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines

        orr             r9,  r9,  r10

        ldrh            r7, [r0,  #-2]
        ldrh_post       r8,  r0,  r1

        usub8           r9,  r12, r9
        sel             r6,  r12, r11           @ hev mask: r6

        @vp8_filter() function
        @ load soure data to r6, r11, r12, lr
        ldrh            r9, [r0,  #-2]
        ldrh_post       r10, r0,  r1

        pkhbt           r12, r7,  r8,  lsl #16

        ldrh            r7, [r0,  #-2]
        ldrh_post       r8,  r0,  r1

        pkhbt           r11, r9,  r10, lsl #16

        ldrh            r9, [r0,  #-2]
        ldrh_post       r10, r0,  r1

        @ Transpose needs 8 regs(r6 - r12, and lr). Save r6 and lr first
        str             r6, [sp]
        str             lr, [sp,  #4]

        pkhbt           r6,  r7,  r8,  lsl #16
        pkhbt           lr,  r9,  r10, lsl #16

        @transpose r12, r11, r6, lr to r7, r8, r9, r10
        TRANSPOSE_MATRIX r12, r11, r6, lr, r10, r9, r8, r7

        @load back hev_mask r6 and filter_mask lr
        ldr             r12, c0x80808080
        ldr             r6, [sp]
        ldr             lr, [sp,  #4]

        eor             r7,  r7,  r12           @ p1 offset to convert to a signed value
        eor             r8,  r8,  r12           @ p0 offset to convert to a signed value
        eor             r9,  r9,  r12           @ q0 offset to convert to a signed value
        eor             r10, r10, r12           @ q1 offset to convert to a signed value

        str             r9, [sp]                @ store qs0 temporarily
        str             r8, [sp,  #4]           @ store ps0 temporarily
        str             r10,[sp,  #8]           @ store qs1 temporarily
        str             r7, [sp,  #12]          @ store ps1 temporarily

        qsub8           r7,  r7,  r10           @ vp8_signed_char_clamp(ps1-qs1)
        qsub8           r8,  r9,  r8            @ vp8_signed_char_clamp(vp8_filter + 3 * ( qs0 - ps0))

        and             r7,  r7,  r6            @  vp8_filter (r7) &= hev (r7 : filter)

        qadd8           r7,  r7,  r8
        ldr             r9,  c0x03030303        @ r9 = 3 --modified for vp8

        qadd8           r7,  r7,  r8
        ldr             r10, c0x04040404

        qadd8           r7,  r7,  r8

        and             r7,  r7,  lr            @ vp8_filter &= mask

        qadd8           r8,  r7,  r9            @ Filter2 (r8) = vp8_signed_char_clamp(vp8_filter+3)
        qadd8           r7,  r7,  r10           @ vp8_filter = vp8_signed_char_clamp(vp8_filter+4)

        mov             r9,  #0
        shadd8          r8,  r8,  r9            @ Filter2 >>= 3
        shadd8          r7,  r7,  r9            @ vp8_filter >>= 3
        shadd8          r8,  r8,  r9
        shadd8          r7,  r7,  r9
        shadd8          lr,  r8,  r9            @ lr: filter2
        shadd8          r7,  r7,  r9            @ r7: filter

        @calculate output
        ldr             r8, [sp]                @ load qs0
        ldr             r9, [sp,  #4]           @ load ps0

        ldr             r10, c0x01010101

        qsub8           r8,  r8,  r7            @ u = vp8_signed_char_clamp(qs0 - vp8_filter)
        qadd8           r9,  r9,  lr            @ u = vp8_signed_char_clamp(ps0 + Filter2)

        eor             r8,  r8,  r12
        eor             r9,  r9,  r12

        mov             lr,  #0

        sadd8           r7,  r7,  r10
        shadd8          r7,  r7,  lr

        ldr             r10,[sp,  #8]           @ load qs1
        ldr             r11,[sp,  #12]          @ load ps1

        bic             r7,  r7,  r6            @ r7: vp8_filter

        qsub8           r10, r10, r7            @ u = vp8_signed_char_clamp(qs1 - vp8_filter)
        qadd8           r11, r11, r7            @ u = vp8_signed_char_clamp(ps1 + vp8_filter)
        eor             r10, r10, r12
        eor             r11, r11, r12

        sub             r0,  r0,  r1,  lsl #2

        @we can use TRANSPOSE_MATRIX macro to transpose output - input: q1, q0, p0, p1
        TRANSPOSE_MATRIX r11, r9, r8, r10, lr, r12, r7, r6

        strh            r6, [r0,  #-2]          @ store the result
        mov             r6,  r6,  lsr #16
        strh_post       r6,  r0,  r1

        strh            r7, [r0,  #-2]
        mov             r7,  r7,  lsr #16
        strh_post       r7,  r0,  r1

        strh            r12, [r0,  #-2]
        mov             r12,  r12, lsr #16
        strh_post       r12,  r0,  r1

        strh            lr, [r0,  #-2]
        mov             lr,  lr,  lsr #16
        strh_post       lr,  r0,  r1

2:
        sub             r0,  r0,  #4
        subs            r5,  r5,  #1

T       ittt            ne
        ldrne           r7, [r0,  r1]
A       ldrne           r6, [r0], r1,  lsl #1   @ load source data
T       ldrne           r6, [r0]                @ load source data
T       addne           r0,  r0,  r1,  lsl #1
T       ittt            ne
        ldrne           lr, [r0,  r1]
A       ldrne           r8, [r0], r1,  lsl #1
T       ldrne           r8, [r0]
T       addne           r0,  r0,  r1,  lsl #1

        bne             1b

        add             sp, sp, #16
        pop            {r4 - r11, pc}
endfunc

@ void vp8_h_loop_filter16(uint8_t *dst, int stride,
@                          int fE, int fI, int hev_thresh)
@ and
@ void vp8_h_loop_filter8uv(uint8_t *dstU, uint8_t *dstV, int stride,
@                           int fE, int fI, int hev_thresh)
@ call:
@ void vp8_h_loop_filter(uint8_t *dst, int stride,
@                        int fE, int fI, int hev_thresh, int count)
function ff_vp8_h_loop_filter_armv6, export=1
        push           {r4 - r11, lr}

        sub             r0,  r0,  #4            @ move r0 pointer down by 4
        ldr             r5, [sp,  #40]          @ counter
        ldr             r9, [sp,  #36]          @ load thresh address
        sub             sp,  sp,  #16           @ create temp buffer

        ldr             r7, [r0,  r1]           @ transpose will make it into p3-p0
        ldr_post        r6,  r0,  r1,  lsl #1   @ load source data
        ldr             lr, [r0,  r1]
        ldr_post        r8,  r0,  r1,  lsl #1

        orr             r2,  r2,  r2,  lsl #16
        orr             r3,  r3,  r3,  lsl #16
        orr             r9,  r9,  r9,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE  splat int -> byte
        orr             r2,  r3,  r3,  lsl #8   @ flimI  splat int -> byte
        orr             r3,  r9,  r9,  lsl #8   @ thresh splat int -> byte

1:
        @ vp8_filter_mask() function
        @ calculate breakout conditions
        @ transpose the source data for 4-in-parallel operation
        TRANSPOSE_MATRIX r6, r7, r8, lr, r12, r11, r10, r9

        uqsub8          r7,  r9,  r10           @ p3 - p2
        uqsub8          r8,  r10, r9            @ p2 - p3
        uqsub8          r9,  r10, r11           @ p2 - p1
        uqsub8          r10, r11, r10           @ p1 - p2
        orr             r7,  r7,  r8            @ abs (p3-p2)
        orr             r10, r9,  r10           @ abs (p2-p1)
        uqsub8          lr,  r7,  r2            @ compare to limit. lr: vp8_filter_mask
        uqsub8          r10, r10, r2            @ compare to limit

        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines

        orr             lr,  lr,  r10

        uqsub8          r6,  r11, r12           @ p1 - p0
        uqsub8          r7,  r12, r11           @ p0 - p1
        add             r0,  r0,  #4            @ move r0 pointer up by 4
        orr             r6,  r6,  r7            @ abs (p1-p0)
        str             r11,[sp,  #12]          @ save p1
        uqsub8          r10, r6,  r2            @ compare to limit
        uqsub8          r11, r6,  r3            @ compare to thresh
        orr             lr,  lr,  r10

        @ transpose uses 8 regs(r6 - r12 and lr). Need to save reg value now
        @ transpose the source data for 4-in-parallel operation
        str             r11,[sp]                @ push r11 to stack
        ldr             r7, [r0,  r1]
        ldr_post        r6,  r0,  r1,  lsl #1   @ load source data
        str             r12,[sp,  #4]           @ save current reg before load q0 - q3 data
        str             lr, [sp,  #8]
        ldr             lr, [r0,  r1]
        ldr_post        r8,  r0,  r1,  lsl #1

        TRANSPOSE_MATRIX r6, r7, r8, lr, r12, r11, r10, r9

        ldr             lr, [sp,  #8]           @ load back (f)limit accumulator

        uqsub8          r6,  r12, r11           @ q3 - q2
        uqsub8          r7,  r11, r12           @ q2 - q3
        uqsub8          r12, r11, r10           @ q2 - q1
        uqsub8          r11, r10, r11           @ q1 - q2
        orr             r6,  r6,  r7            @ abs (q3-q2)
        orr             r7,  r12, r11           @ abs (q2-q1)
        uqsub8          r6,  r6,  r2            @ compare to limit
        uqsub8          r7,  r7,  r2            @ compare to limit
        ldr             r11,[sp,  #4]           @ load back p0
        ldr             r12,[sp,  #12]          @ load back p1
        orr             lr,  lr,  r6
        orr             lr,  lr,  r7

        uqsub8          r6,  r11, r9            @ p0 - q0
        uqsub8          r7,  r9,  r11           @ q0 - p0
        uqsub8          r8,  r12, r10           @ p1 - q1
        uqsub8          r11, r10, r12           @ q1 - p1
        orr             r6,  r6,  r7            @ abs (p0-q0)
        ldr             r7,  c0x7F7F7F7F
        orr             r8,  r8,  r11           @ abs (p1-q1)
        uqadd8          r6,  r6,  r6            @ abs (p0-q0) * 2
        and             r8,  r7,  r8,  lsr #1   @ abs (p1-q1) / 2
        uqsub8          r11, r10, r9            @ q1 - q0
        uqadd8          r6,  r8,  r6            @ abs (p0-q0)*2 + abs (p1-q1)/2
        uqsub8          r12, r9,  r10           @ q0 - q1
        uqsub8          r6,  r6,  r4            @ compare to flimit

        orr             r9,  r11, r12           @ abs (q1-q0)
        uqsub8          r8,  r9,  r2            @ compare to limit
        uqsub8          r10, r9,  r3            @ compare to thresh
        orr             lr,  lr,  r6
        orr             lr,  lr,  r8

        mvn             r11,  #0                @ r11 == -1
        mov             r12,  #0

        usub8           lr,  r12, lr
        ldr             r9, [sp]                @ load the compared result
        sel             lr,  r11, r12           @ filter mask: lr

        cmp             lr,  #0
        beq             2f                      @ skip filtering


        @vp8_hevmask() function
        @calculate high edge variance
        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines

        orr             r9,  r9,  r10

        ldrh            r7, [r0,  #-2]
        ldrh_post       r8,  r0,  r1

        usub8           r9,  r12, r9
        sel             r6,  r12, r11           @ hev mask: r6


        @ vp8_mbfilter() function
        @ p2, q2 are only needed at the end. do not need to load them in now.
        @ Transpose needs 8 regs(r6 - r12, and lr). Save r6 and lr first
        @ load soure data to r6, r11, r12, lr
        ldrh            r9, [r0,  #-2]
        ldrh_post       r10, r0,  r1

        pkhbt           r12, r7,  r8,  lsl #16

        ldrh            r7, [r0,  #-2]
        ldrh_post       r8,  r0,  r1

        pkhbt           r11, r9,  r10, lsl #16

        ldrh            r9, [r0,  #-2]
        ldrh_post       r10, r0,  r1

        str             r6, [sp]                @ save r6
        str             lr, [sp,  #4]           @ save lr

        pkhbt           r6,  r7,  r8,  lsl #16
        pkhbt           lr,  r9,  r10, lsl #16

        @transpose r12, r11, r6, lr to p1, p0, q0, q1
        TRANSPOSE_MATRIX r12, r11, r6, lr, r10, r9, r8, r7

        @load back hev_mask r6 and filter_mask lr
        ldr             r12, c0x80808080
        ldr             r6, [sp]
        ldr             lr, [sp,  #4]

        eor             r7,  r7,  r12           @ ps1
        eor             r8,  r8,  r12           @ ps0
        eor             r9,  r9,  r12           @ qs0
        eor             r10, r10, r12           @ qs1

        qsub8           r12, r9,  r8            @ vp8_signed_char_clamp(vp8_filter + 3 * ( qs0 - ps0))
        str             r7, [sp,  #12]          @ store ps1 temporarily
        qsub8           r7,  r7,  r10           @ vp8_signed_char_clamp(ps1-qs1)
        str             r10,[sp,  #8]           @ store qs1 temporarily
        qadd8           r7,  r7,  r12
        str             r9, [sp]                @ store qs0 temporarily
        qadd8           r7,  r7,  r12
        str             r8, [sp,  #4]           @ store ps0 temporarily
        qadd8           r7,  r7,  r12           @ vp8_filter: r7

        ldr             r10, c0x03030303        @ r10 = 3 --modified for vp8
        ldr             r9,  c0x04040404

        and             r7,  r7,  lr            @ vp8_filter &= mask (lr is free)

        mov             r12, r7                 @ Filter2: r12
        and             r12, r12, r6            @ Filter2 &= hev

        @save bottom 3 bits so that we round one side +4 and the other +3
        qadd8           r8,  r12, r9            @ Filter1 (r8) = vp8_signed_char_clamp(Filter2+4)
        qadd8           r12, r12, r10           @ Filter2 (r12) = vp8_signed_char_clamp(Filter2+3)

        mov             r10, #0
        shadd8          r8,  r8,  r10           @ Filter1 >>= 3
        shadd8          r12, r12, r10           @ Filter2 >>= 3
        shadd8          r8,  r8,  r10
        shadd8          r12, r12, r10
        shadd8          r8,  r8,  r10           @ r8: Filter1
        shadd8          r12, r12, r10           @ r12: Filter2

        ldr             r9, [sp]                @ load qs0
        ldr             r11,[sp,  #4]           @ load ps0

        qsub8           r9,  r9,  r8            @ qs0 = vp8_signed_char_clamp(qs0 - Filter1)
        qadd8           r11, r11, r12           @ ps0 = vp8_signed_char_clamp(ps0 + Filter2)

        bic             r12, r7,  r6            @vp8_filter &= ~hev    ( r6 is free)

        @roughly 3/7th difference across boundary
        mov             lr,  #0x1b              @ 27
        mov             r7,  #0x3f              @ 63

        sxtb16          r6,  r12
        sxtb16          r10, r12, ror #8
        smlabb          r8,  r6,  lr,  r7
        smlatb          r6,  r6,  lr,  r7
        smlabb          r7,  r10, lr,  r7
        smultb          r10, r10, lr
        ssat            r8,  #8,  r8,  asr #7
        ssat            r6,  #8,  r6,  asr #7
        add             r10, r10, #63
        ssat            r7,  #8,  r7,  asr #7
        ssat            r10, #8,  r10, asr #7

        ldr             lr, c0x80808080

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r7,  r10, lsl #16
        uxtb16          r6,  r6
        uxtb16          r10, r10

        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 27)>>7)

        qsub8           r8,  r9,  r10           @ s = vp8_signed_char_clamp(qs0 - u)
        qadd8           r10, r11, r10           @ s = vp8_signed_char_clamp(ps0 + u)
        eor             r8,  r8,  lr            @ *oq0 = s^0x80
        eor             r10, r10, lr            @ *op0 = s^0x80

        strb            r10,[r0,  #-1]          @ store op0 result
        strb_post       r8,  r0,  r1            @ store oq0 result
        mov             r10, r10, lsr #8
        mov             r8,  r8,  lsr #8
        strb            r10,[r0,  #-1]
        strb_post       r8,  r0,  r1
        mov             r10, r10, lsr #8
        mov             r8,  r8,  lsr #8
        strb            r10,[r0,  #-1]
        strb_post       r8,  r0,  r1
        mov             r10, r10, lsr #8
        mov             r8,  r8,  lsr #8
        strb            r10,[r0,  #-1]
        strb_post       r8,  r0,  r1

        @roughly 2/7th difference across boundary
        mov             lr,  #0x12              @ 18
        mov             r7,  #0x3f              @ 63

        sxtb16          r6,  r12
        sxtb16          r10, r12, ror #8
        smlabb          r8,  r6,  lr,  r7
        smlatb          r6,  r6,  lr,  r7
        smlabb          r9,  r10, lr,  r7
        smlatb          r10, r10, lr,  r7
        ssat            r8,  #8,  r8,  asr #7
        ssat            r6,  #8,  r6,  asr #7
        ssat            r9,  #8,  r9,  asr #7
        ssat            r10, #8,  r10, asr #7

        sub             r0,  r0,  r1,  lsl #2   @ move r0 pointer down by 4 lines

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r9,  r10, lsl #16

        ldr             r9, [sp,  #8]           @ load qs1
        ldr             r11,[sp,  #12]          @ load ps1
        ldr             lr,  c0x80808080

        uxtb16          r6,  r6
        uxtb16          r10, r10

        add             r0,  r0,  #2

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 18)>>7)

        qsub8           r8,  r9,  r10           @ s = vp8_signed_char_clamp(qs1 - u)
        qadd8           r10, r11, r10           @ s = vp8_signed_char_clamp(ps1 + u)
        eor             r8,  r8,  lr            @ *oq1 = s^0x80
        eor             r10, r10, lr            @ *op1 = s^0x80

        ldrb            r11,[r0,  #-5]          @ load p2 for 1/7th difference across boundary
        strb            r10,[r0,  #-4]          @ store op1
        strb            r8, [r0,  #-1]          @ store oq1
        ldrb_post       r9,  r0,  r1            @ load q2 for 1/7th difference across boundary

        mov             r10, r10, lsr #8
        mov             r8,  r8,  lsr #8

        ldrb            r6, [r0,  #-5]
        strb            r10,[r0,  #-4]
        strb            r8, [r0,  #-1]
        ldrb_post       r7,  r0,  r1

        mov             r10, r10, lsr #8
        mov             r8,  r8,  lsr #8
        orr             r11, r11, r6,  lsl #8
        orr             r9,  r9,  r7,  lsl #8

        ldrb            r6, [r0,  #-5]
        strb            r10,[r0,  #-4]
        strb            r8, [r0,  #-1]
        ldrb_post       r7,  r0,  r1

        mov             r10, r10, lsr #8
        mov             r8,  r8,  lsr #8
        orr             r11, r11, r6,  lsl #16
        orr             r9,  r9,  r7,  lsl #16

        ldrb            r6, [r0,  #-5]
        strb            r10,[r0,  #-4]
        strb            r8, [r0,  #-1]
        ldrb_post       r7,  r0,  r1
        orr             r11, r11, r6,  lsl #24
        orr             r9,  r9,  r7,  lsl #24

        @roughly 1/7th difference across boundary
        eor             r9,  r9,  lr
        eor             r11, r11, lr

        mov             lr,  #0x9               @ 9
        mov             r7,  #0x3f              @ 63

        sxtb16          r6,  r12
        sxtb16          r10, r12, ror #8
        smlabb          r8,  r6,  lr,  r7
        smlatb          r6,  r6,  lr,  r7
        smlabb          r12, r10, lr,  r7
        smlatb          r10, r10, lr,  r7
        ssat            r8,  #8,  r8,  asr #7
        ssat            r6,  #8,  r6,  asr #7
        ssat            r12, #8,  r12, asr #7
        ssat            r10, #8,  r10, asr #7

        sub             r0,  r0,  r1,  lsl #2

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r12, r10, lsl #16

        uxtb16          r6,  r6
        uxtb16          r10, r10

        ldr             lr,  c0x80808080

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 9)>>7)

        qadd8           r8,  r11, r10           @ s = vp8_signed_char_clamp(ps2 + u)
        qsub8           r10, r9,  r10           @ s = vp8_signed_char_clamp(qs2 - u)
        eor             r8,  r8,  lr            @ *op2 = s^0x80
        eor             r10, r10, lr            @ *oq2 = s^0x80

        strb            r8, [r0,  #-5]          @ store *op2
        strb_post       r10, r0,  r1            @ store *oq2
        mov             r8,  r8,  lsr #8
        mov             r10, r10, lsr #8
        strb            r8, [r0,  #-5]
        strb_post       r10, r0,  r1
        mov             r8,  r8,  lsr #8
        mov             r10, r10, lsr #8
        strb            r8, [r0,  #-5]
        strb_post       r10, r0,  r1
        mov             r8,  r8,  lsr #8
        mov             r10, r10, lsr #8
        strb            r8, [r0,  #-5]
        strb_post       r10, r0,  r1

        @adjust r0 pointer for next loop
        sub             r0,  r0,  #2

2:
        sub             r0,  r0,  #4
        subs            r5,  r5,  #1

T       ittt            ne
        ldrne           r7, [r0,  r1]
A       ldrne           r6, [r0], r1,  lsl #1   @ load source data
T       ldrne           r6, [r0]
T       addne           r0,  r0,  r1,  lsl #1
T       ittt            ne
        ldrne           lr, [r0,  r1]
A       ldrne           r8, [r0], r1,  lsl #1
T       ldrne           r8, [r0]
T       addne           r0,  r0,  r1,  lsl #1

        bne             1b

        add             sp,  sp,  #16
        pop            {r4 - r11, pc}
endfunc

@ MC

@ void put_vp8_pixels16(uint8_t *dst, int dststride, uint8_t *src,
@                       int srcstride, int h, int mx, int my)
function ff_put_vp8_pixels16_armv6, export=1
        push           {r4 - r11}
        ldr             r12,[sp,  #32]          @ h
1:
        subs            r12, r12, #2
        ldr             r5, [r2,  #4]
        ldr             r6, [r2,  #8]
        ldr             r7, [r2,  #12]
        ldr_post        r4,  r2,  r3
        ldr             r9, [r2,  #4]
        ldr             r10,[r2,  #8]
        ldr             r11,[r2,  #12]
        ldr_post        r8,  r2,  r3
        strd            r6,  r7, [r0,  #8]
        strd_post       r4,  r5,  r0,  r1
        strd            r10, r11,[r0,  #8]
        strd_post       r8,  r9,  r0,  r1
        bgt             1b
        pop            {r4 - r11}
        bx              lr
endfunc

@ void put_vp8_pixels8(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int h, int mx, int my)
function ff_put_vp8_pixels8_armv6, export=1
        push           {r4 - r11}
        ldr             r12,[sp,  #32]          @ h
1:
        subs            r12, r12, #4
        ldr             r5, [r2,  #4]
        ldr_post        r4,  r2,  r3
        ldr             r7, [r2,  #4]
        ldr_post        r6,  r2,  r3
        ldr             r9, [r2,  #4]
        ldr_post        r8,  r2,  r3
        ldr             r11,[r2,  #4]
        ldr_post        r10, r2,  r3
        strd_post       r4,  r5,  r0,  r1
        strd_post       r6,  r7,  r0,  r1
        strd_post       r8,  r9,  r0,  r1
        strd_post       r10, r11, r0,  r1
        bgt             1b
        pop            {r4 - r11}
        bx              lr
endfunc

@ void put_vp8_pixels4(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int h, int mx, int my)
function ff_put_vp8_pixels4_armv6, export=1
        ldr             r12, [sp, #0]           @ h
        push           {r4 - r6, lr}
1:
        subs            r12, r12, #4
        ldr             r5, [r2,  r3]
        ldr_post        r4,  r2,  r3,  lsl #1
        ldr             lr, [r2,  r3]
        ldr_post        r6,  r2,  r3,  lsl #1
        str             r5, [r0,  r1]
        str_post        r4,  r0,  r1,  lsl #1
        str             lr, [r0,  r1]
        str_post        r6,  r0,  r1,  lsl #1
        bgt             1b
        pop            {r4 - r6,  pc}
endfunc

@ note: worst case sum of all 6-tap filter values * 255 is 0x7f80 so 16 bit
@ arithmatic can be used to apply filters
const sixtap_filters_13245600, align=4
        .short     2, 108, -11,  36,  -8, 1, 0, 0
        .short     3,  77, -16,  77, -16, 3, 0, 0
        .short     1,  36,  -8, 108, -11, 2, 0, 0
endconst
const fourtap_filters_1324, align=4
        .short     -6,  12, 123, -1
        .short     -9,  50,  93, -6
        .short     -6,  93,  50, -9
        .short     -1, 123,  12, -6
endconst

@ void put_vp8_epel_h6(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int w, int h, int mx)
function ff_put_vp8_epel_h6_armv6, export=1
        push           {r4 - r11, lr}

        sub             r2,  r2,  #2
        movrel          lr,  sixtap_filters_13245600 - 16
        ldr             r12,[sp,  #44]          @ vp8_filter index
        ldr             r4, [sp,  #36]          @ width
        add             lr,  lr,  r12, lsl #3
        sub             r3,  r3,  r4            @ src_stride - block_width
        sub             r1,  r1,  r4            @ dst_stride - block_width
        lsr             r4, #2

        str             r4, [sp,  #36]          @ "4-in-parallel" loop counter @40
        str             r3, [sp,  #44]          @ src_stride - block_width @48
        push           {r1}                     @ dst_stride - block_width @0
                                                @ height @44

        ldr             r1, [lr], #4            @ coefficients
        ldr             r3, [lr], #4
        ldr             lr, [lr]
1:
        @ 3 loads, 10 shuffles and then mul/acc/add/shr
        @ o0: i0/i1/i2/i3/i4/i5 -> i0/i2 (ld1) | i1/i3 (ld1)   | i4/i5 (ld2)
        @ o1: i1/i2/i3/i4/i5/i6 -> i1/i3 (ld1) | i2/i4 (ld2)   | i5/i6 (ld2/3)
        @ o2: i2/i3/i4/i5/i6/i7 -> i2/i4 (ld2) | i3/i5 (ld2)   | i6/i7 (ld3)
        @ o3: i3/i4/i5/i6/i7/i8 -> i3/i5 (ld2) | i4/i6 (ld2/3) | i7/i8 (ld3)
        ldr             r7, [r2,  #5]           @ ld3 -> src[5-8]
        ldr             r6, [r2,  #2]           @ ld2 -> src[2-5]
        ldr             r5, [r2], #4            @ ld1 -> src[0-3]

        pkhtb           r7,  r7,  r7,  asr #8   @ src[8,7,7,6]
        uxtb16          r9,  r6,  ror #8        @ src[5] | src[3]
        uxtb16          r6,  r6                 @ src[4] | src[2]
        uxtb16          r8,  r5,  ror #8        @ src[3] | src[1]
        uxtb16          r11, r7,  ror #8        @ src[8] | src[7]
        uxtb16          r7,  r7                 @ src[7] | src[6]
        pkhtb           r10, r9,  r6,  asr #16  @ src[5] | src[4]
        uxtb16          r5,  r5                 @ src[2] | src[0]

        smuad           r11, r11, lr            @ filter[3][2] -> r11
        subs            r4,  r4,  #1
        pkhbt           r12, r10, r7,  lsl #16  @ src[6] | src[4]
        smuad           r7,  r7,  lr            @ filter[2][2] -> r7
        smuad           r5,  r5,  r1            @ filter[0][0] -> r5
        smlad           r11, r9,  r1,  r11      @ filter[3][0] -> r11
        smlad           r7,  r9,  r3,  r7       @ filter[2][1] -> r7
        smuad           r9,  r8,  r1            @ filter[1][0] -> r9
        smlad           r5,  r8,  r3,  r5       @ filter[0][1] -> r5
        pkhtb           r8,  r12, r10, asr #16  @ src[6] | src[5]
        smlad           r11, r12, r3,  r11      @ filter[3][1] -> r11
        smlad           r9,  r6,  r3,  r9       @ filter[1][1] -> r9
        smlad           r5,  r10, lr,  r5       @ filter[0][2] -> r5
        smlad           r7,  r6,  r1,  r7       @ filter[2][0] -> r7
        smlad           r9,  r8,  lr,  r9       @ filter[1][2] -> r9

        add             r5,  r5,  #0x40         @ round_shift_and_clamp[0]
        add             r9,  r9,  #0x40         @ round_shift_and_clamp[1]
        add             r7,  r7,  #0x40         @ round_shift_and_clamp[2]
        add             r11, r11, #0x40         @ round_shift_and_clamp[3]

        usat            r5,  #8,  r5,  asr #7
        usat            r9,  #8,  r9,  asr #7
        usat            r7,  #8,  r7,  asr #7
        usat            r11, #8,  r11, asr #7

        strb            r5, [r0], #1            @ store res[0]
        strb            r9, [r0], #1            @ store res[1]
        strb            r7, [r0], #1            @ store res[2]
        strb            r11,[r0], #1            @ store res[3]

        bne             1b

        ldr             r12,[sp,  #44]          @ height = outer-loop counter
        subs            r12, r12, #1
T       itttt           ne
        ldrne           r4, [sp,  #40]          @ 4-in-parallel loop counter
        ldrne           r5, [sp,  #48]
        ldrne           r6, [sp]
        strne           r12,[sp,  #44]
        add             r2,  r2,  r5            @ move to next input/output lines
        add             r0,  r0,  r6

        bne             1b

        add             sp,  sp,  #4            @ restore stack after push{r1} above
        pop            {r4 - r11, pc}
endfunc

@ void put_vp8_epel_v6(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int w, int h, int my)
function ff_put_vp8_epel_v6_armv6, export=1
        push           {r4 - r11, lr}

        movrel          lr,  sixtap_filters_13245600 - 16
        ldr             r12,[sp,  #44]          @ vp8_filter index
        ldr             r4, [sp,  #36]          @ width
        add             lr,  lr,  r12, lsl #3
        sub             r1,  r1,  r4            @ dst_stride - block_width
        lsr             r4,  #2

        str             r4, [sp,  #36]          @ "4-in-parallel" loop counter @40
        str             r3, [sp,  #44]          @ src_stride - block_width @48
        push           {r1}                     @ dst_stride - block_width @0
                                                @ height @44
1:
        add             r1,  r3,  r3,  lsl #1   @ stride * 3
        ldr_dpren       r5,  r2,  r3            @ src[0,1,2,3 + stride * 1]
        ldr             r6, [r2,  r3]           @ src[0,1,2,3 + stride * 3]
        ldr             r7, [r2,  r3,  lsl #1]  @ src[0,1,2,3 + stride * 4]
        ldr             r8, [r2,  r1]           @ src[0,1,2,3 + stride * 5]

        @ byte -> word and "transpose"
        uxtb16          r9,  r5,  ror #8        @ src[3 + stride*1] | src[1 + stride*1]
        uxtb16          r10, r6,  ror #8        @ src[3 + stride*3] | src[1 + stride*3]
        uxtb16          r11, r7,  ror #8        @ src[3 + stride*4] | src[1 + stride*4]
        uxtb16          r12, r8,  ror #8        @ src[3 + stride*5] | src[1 + stride*5]
        uxtb16          r5,  r5                 @ src[2 + stride*1] | src[0 + stride*1]
        uxtb16          r6,  r6                 @ src[2 + stride*3] | src[0 + stride*3]
        uxtb16          r7,  r7                 @ src[2 + stride*4] | src[0 + stride*4]
        uxtb16          r8,  r8                 @ src[2 + stride*5] | src[0 + stride*5]
        pkhbt           r1,  r9,  r10, lsl #16  @ src[1 + stride*3] | src[1 + stride*1]
        pkhtb           r9,  r10, r9,  asr #16  @ src[3 + stride*3] | src[3 + stride*1]
        pkhbt           r10, r11, r12, lsl #16  @ src[1 + stride*5] | src[1 + stride*4]
        pkhtb           r11, r12, r11, asr #16  @ src[3 + stride*5] | src[3 + stride*4]
        pkhbt           r12, r5,  r6,  lsl #16  @ src[0 + stride*3] | src[0 + stride*1]
        pkhtb           r5,  r6,  r5,  asr #16  @ src[2 + stride*3] | src[2 + stride*1]
        pkhbt           r6,  r7,  r8,  lsl #16  @ src[0 + stride*5] | src[0 + stride*4]
        pkhtb           r7,  r8,  r7,  asr #16  @ src[2 + stride*5] | src[2 + stride*4]

        ldr             r8, [lr,  #4]           @ stall - if only I had more registers...
        smuad           r12, r12, r8            @ filter[0][1]
        smuad           r1,  r1,  r8            @ filter[1][1]
        smuad           r5,  r5,  r8            @ filter[2][1]
        smuad           r9,  r9,  r8            @ filter[3][1]
        ldr             r8, [lr,  #8]           @ stall - if only I had more registers...
        smlad           r12, r6,  r8, r12       @ filter[0][2]
        smlad           r1,  r10, r8, r1        @ filter[1][2]
        ldr_dpren       r6,  r2,  r3,  lsl #1   @ src[0,1,2,3 + stride *  0]
        ldr             r10,[r2], #4            @ src[0,1,2,3 + stride *  2]
        smlad           r5,  r7,  r8, r5        @ filter[2][2]
        smlad           r9,  r11, r8, r9        @ filter[3][2]

        uxtb16          r7,  r6,  ror #8        @ src[3 + stride*0] | src[1 + stride*0]
        uxtb16          r11, r10, ror #8        @ src[3 + stride*2] | src[1 + stride*2]
        uxtb16          r6,  r6                 @ src[2 + stride*0] | src[0 + stride*0]
        uxtb16          r10, r10                @ src[2 + stride*2] | src[0 + stride*2]

        pkhbt           r8,  r7,  r11, lsl #16  @ src[1 + stride*2] | src[1 + stride*0]
        pkhtb           r7,  r11, r7,  asr #16  @ src[3 + stride*2] | src[3 + stride*0]
        pkhbt           r11, r6,  r10, lsl #16  @ src[0 + stride*2] | src[0 + stride*0]
        pkhtb           r6,  r10, r6,  asr #16  @ src[2 + stride*2] | src[2 + stride*0]

        ldr             r10,[lr]                @ stall - if only I had more registers...
        subs            r4,  r4,  #1            @ counter--
        smlad           r12, r11, r10, r12      @ filter[0][0]
        smlad           r1,  r8,  r10, r1       @ filter[1][0]
        smlad           r5,  r6,  r10, r5       @ filter[2][0]
        smlad           r9,  r7,  r10, r9       @ filter[3][0]

        add             r12, r12, #0x40         @ round_shift_and_clamp[0]
        add             r1,  r1,  #0x40         @ round_shift_and_clamp[1]
        add             r5,  r5,  #0x40         @ round_shift_and_clamp[2]
        add             r9,  r9,  #0x40         @ round_shift_and_clamp[3]

        usat            r12, #8,  r12, asr #7
        usat            r1,  #8,  r1,  asr #7
        usat            r5,  #8,  r5,  asr #7
        usat            r9,  #8,  r9,  asr #7

        strb            r12,[r0], #1            @ store res[0]
        strb            r1, [r0], #1            @ store res[1]
        strb            r5, [r0], #1            @ store res[2]
        strb            r9, [r0], #1            @ store res[3]

        bne             1b

        ldr             r12,[sp,  #44]          @ height = outer-loop counter
        subs            r12, r12, #1
T       itttt           ne
        ldrne           r4, [sp,  #40]          @ 4-in-parallel loop counter
        ldrne           r6, [sp,  #0]
        subne           r2,  r2,  r4,  lsl #2
        strne           r12,[sp,  #44]
        add             r0,  r0,  r6
        add             r2,  r2,  r3            @ move to next input/output lines

        bne             1b

        add             sp,  sp,  #4            @ restore stack after push{r1} above
        pop            {r4 - r11, pc}
endfunc

@ void put_vp8_epel_h4(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int w, int h, int mx)
function ff_put_vp8_epel_h4_armv6, export=1
        push           {r4 - r11, lr}

        subs            r2,  r2,  #1
        movrel          lr,  fourtap_filters_1324 - 4
        ldr             r4, [sp,  #36]          @ width
        ldr             r12,[sp,  #44]          @ vp8_filter index
        add             lr,  lr,  r12, lsl #2
        sub             r3,  r3,  r4            @ src_stride - block_width
        sub             r1,  r1,  r4            @ dst_stride - block_width
        ldr             r5,  [lr]
        ldr             r6,  [lr,  #4]
        asr             r4,  #2

        ldr             lr, [sp,  #40]          @ height = outer-loop counter
        str             r4, [sp,  #36]          @ "4-in-parallel" inner loop counter
1:
        @ 3 loads, 5 uxtb16s and then mul/acc/add/shr
        @ o0: i0/i1/i2/i3 -> i0/i2(ld1) + i1/i3(ld1)
        @ o1: i1/i2/i3/i4 -> i1/i3(ld1) + i2/i4(ld2)
        @ o2: i2/i3/i4/i5 -> i2/i4(ld2) + i3/i5(ld2)
        @ o3: i3/i4/i5/i6 -> i3/i5(ld2) + i4/i6(ld3)
        ldr             r9, [r2,  #3]           @ load source data
        ldr             r8, [r2,  #2]
        ldr             r7, [r2], #4

        uxtb16          r9,  r9,  ror #8        @ src[6] | src[4]
        uxtb16          r10, r8,  ror #8        @ src[5] | src[3]
        uxtb16          r8,  r8                 @ src[4] | src[2]
        uxtb16          r11, r7,  ror #8        @ src[3] | src[1]
        uxtb16          r7,  r7                 @ src[2] | src[0]

        smuad           r9,  r9,  r6            @ filter[3][1] -> r9
        smuad           r12, r10, r6            @ filter[2][1] -> r12
        smuad           r7,  r7,  r5            @ filter[0][0] -> r7
        smlad           r9,  r10, r5,  r9       @ filter[3][0] -> r9
        smuad           r10, r11, r5            @ filter[1][0] -> r10
        smlad           r12, r8,  r5,  r12      @ filter[2][0] -> r12
        smlad           r7,  r11, r6,  r7       @ filter[0][1] -> r7
        smlad           r10, r8,  r6,  r10      @ filter[1][1] -> r10

        subs            r4,  r4,  #1            @ counter--

        add             r7,  r7,  #0x40         @ round_shift_and_clamp[0]
        add             r10, r10, #0x40         @ round_shift_and_clamp[1]
        add             r12, r12, #0x40         @ round_shift_and_clamp[2]
        add             r9,  r9,  #0x40         @ round_shift_and_clamp[3]

        usat            r7,  #8,  r7,  asr #7
        usat            r10, #8,  r10, asr #7
        usat            r12, #8,  r12, asr #7
        usat            r9,  #8,  r9,  asr #7

        strb            r7, [r0], #1            @ store res[0]
        strb            r10,[r0], #1            @ store res[1]
        strb            r12,[r0], #1            @ store res[2]
        strb            r9, [r0], #1            @ store res[3]

        bne             1b

        subs            lr,  lr,  #1
T       it              ne
        ldrne           r4, [sp,  #36]          @ 4-in-parallel loop counter
        add             r2,  r2,  r3            @ move to next input/output lines
        add             r0,  r0,  r1

        bne             1b

        pop            {r4 - r11, pc}
endfunc

@ void put_vp8_epel_v4(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int w, int h, int my)
function ff_put_vp8_epel_v4_armv6, export=1
        push           {r4 - r11, lr}

        movrel          lr,  fourtap_filters_1324 - 4
        ldr             r12,[sp,  #44]          @ vp8_filter index
        ldr             r4, [sp,  #36]          @ width
        add             lr,  lr,  r12, lsl #2
        sub             r1,  r1,  r4            @ dst_stride - block_width
        asr             r4,  #2
        ldr             r5, [lr]
        ldr             r6, [lr,  #4]

        str             r4, [sp,  #36]          @ "4-in-parallel" loop counter @40
        str             r3, [sp,  #44]          @ src_stride @48
        push           {r1}                     @ dst_stride - block_width @36
                                                @ height @44
1:
        ldr             lr, [r2,  r3, lsl #1]   @ load source pixels
        ldr             r12,[r2,  r3]
        ldr_dpren       r7,  r2,  r3
        ldr             r11,[r2], #4

        @ byte -> word and "transpose"
        uxtb16          r8,  lr,  ror #8        @ src[3 + stride*3] | src[1 + stride*3]
        uxtb16          r9,  r12, ror #8        @ src[3 + stride*2] | src[1 + stride*2]
        uxtb16          r3,  r7,  ror #8        @ src[3 + stride*0] | src[1 + stride*0]
        uxtb16          r1,  r11, ror #8        @ src[3 + stride*1] | src[1 + stride*1]
        uxtb16          lr,  lr                 @ src[2 + stride*3] | src[0 + stride*3]
        uxtb16          r12, r12                @ src[2 + stride*2] | src[0 + stride*2]
        uxtb16          r7,  r7                 @ src[2 + stride*0] | src[0 + stride*0]
        uxtb16          r11, r11                @ src[2 + stride*1] | src[0 + stride*1]
        pkhbt           r10, r1,  r8,  lsl #16  @ src[1 + stride*3] | src[1 + stride*1]
        pkhtb           r1,  r8,  r1,  asr #16  @ src[3 + stride*3] | src[3 + stride*1]
        pkhbt           r8,  r3,  r9,  lsl #16  @ src[1 + stride*2] | src[1 + stride*0]
        pkhtb           r3,  r9,  r3,  asr #16  @ src[3 + stride*2] | src[3 + stride*0]
        pkhbt           r9,  r11, lr,  lsl #16  @ src[0 + stride*3] | src[0 + stride*1]
        pkhtb           r11, lr,  r11, asr #16  @ src[2 + stride*3] | src[2 + stride*1]
        pkhbt           lr,  r7,  r12, lsl #16  @ src[0 + stride*2] | src[0 + stride*0]
        pkhtb           r7,  r12, r7,  asr #16  @ src[2 + stride*2] | src[2 + stride*0]

        smuad           r9,  r9,  r6            @ filter[0][1]
        smuad           r10, r10, r6            @ filter[1][1]
        smuad           r11, r11, r6            @ filter[2][1]
        smuad           r1,  r1,  r6            @ filter[3][1]
        smlad           r9,  lr,  r5, r9        @ filter[0][0]
        smlad           r10, r8,  r5, r10       @ filter[1][0]
        smlad           r11, r7,  r5, r11       @ filter[2][0]
        smlad           r1,  r3,  r5, r1        @ filter[3][0]

        subs            r4,  r4,  #1            @ counter--
        ldr             r3, [sp,  #48]          @ FIXME prevent clobber of r3 above?

        add             r9,  r9,  #0x40         @ round_shift_and_clamp[0]
        add             r10, r10, #0x40         @ round_shift_and_clamp[1]
        add             r11, r11, #0x40         @ round_shift_and_clamp[2]
        add             r1,  r1,  #0x40         @ round_shift_and_clamp[3]

        usat            r9,  #8,  r9,  asr #7
        usat            r10, #8,  r10, asr #7
        usat            r11, #8,  r11, asr #7
        usat            r1,  #8,  r1,  asr #7

        strb            r9, [r0], #1            @ store result
        strb            r10,[r0], #1
        strb            r11,[r0], #1
        strb            r1, [r0], #1

        bne             1b

        ldr             r12,[sp,  #44]          @ height = outer-loop counter
        subs            r12, r12, #1
T       ittt            ne
        ldrne           r4, [sp,  #40]          @ 4-in-parallel loop counter
        ldrne           r9, [sp,  #0]
        strne           r12,[sp,  #44]
        sub             r2,  r2,  r4,  lsl #2
        add             r0,  r0,  r9
        add             r2,  r2,  r3            @ move to next input/output lines

        bne             1b

        add             sp,  sp,  #4            @ restore stack after push{r1} above
        pop            {r4 - r11, pc}
endfunc

@ void put_vp8_bilin_h(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int w, int h, int mx)
function ff_put_vp8_bilin_h_armv6, export=1
        push           {r4 - r9,  lr}

        ldr             r8, [sp,  #36]          @ vp8_filter index
        ldr             r12,[sp,  #32]          @ height = outer-loop counter
        ldr             r4, [sp,  #28]          @ width
        lsl             r5,  r8,  #16           @ mx << 16
        sub             r3,  r3,  r4            @ src_stride - block_width
        sub             r1,  r1,  r4            @ dst_stride - block_width
        asr             r4,  #2
        sub             r5,  r5,  r8            @ (mx << 16) | (-mx)
        str             r4, [sp,  #28]          @ "4-in-parallel" loop counter
        add             r5,  r5,  #8            @ (8 - mx) | (mx << 16) = filter coefficients
1:
        ldrb            r6, [r2], #1            @ load source data
        ldrb            r7, [r2], #1
        ldrb            r8, [r2], #1
        ldrb            r9, [r2], #1
        ldrb            lr, [r2]

        pkhbt           r6,  r6,  r7,  lsl #16  @ src[1] | src[0]
        pkhbt           r7,  r7,  r8,  lsl #16  @ src[2] | src[1]
        pkhbt           r8,  r8,  r9,  lsl #16  @ src[3] | src[2]
        pkhbt           r9,  r9,  lr,  lsl #16  @ src[4] | src[3]

        smuad           r6,  r6,  r5            @ apply the filter
        smuad           r7,  r7,  r5
        smuad           r8,  r8,  r5
        smuad           r9,  r9,  r5

        subs            r4,  r4,  #1            @ counter--

        add             r6,  r6,  #0x4          @ round_shift_and_clamp
        add             r7,  r7,  #0x4
        add             r8,  r8,  #0x4
        add             r9,  r9,  #0x4

        asr             r6,  #3
        asr             r7,  #3
        pkhbt           r6,  r6,  r8,  lsl #13
        pkhbt           r7,  r7,  r9,  lsl #13
        orr             r6,  r6,  r7,  lsl #8
        str             r6, [r0], #4            @ store result

        bne             1b

        ldr             r4, [sp,  #28]          @ 4-in-parallel loop counter
        subs            r12, r12, #1

        add             r2,  r2,  r3            @ move to next input/output lines
        add             r0,  r0,  r1

        bne             1b

        pop            {r4 - r9,  pc}
endfunc

@ void put_vp8_bilin_v(uint8_t *dst, int dststride, uint8_t *src,
@                      int srcstride, int w, int h, int my)
function ff_put_vp8_bilin_v_armv6, export=1
        push           {r4 - r11, lr}

        ldr             r11,[sp,  #44]          @ vp8_filter index
        ldr             r4, [sp,  #36]          @ width
        mov             r5,  r11, lsl #16       @ mx << 16
        ldr             r12,[sp,  #40]          @ height = outer-loop counter
        sub             r1,  r1,  r4
        sub             r5,  r5,  r11           @ (mx << 16) | (-mx)
        asr             r4,  #2
        add             r5,  r5,  #8            @ (8 - mx) | (mx << 16) = filter coefficients
        str             r4, [sp,  #36]          @ "4-in-parallel" loop counter
1:
        ldrb            r10,[r2,  r3]           @ load the data
        ldrb            r6, [r2], #1
        ldrb            r11,[r2,  r3]
        ldrb            r7, [r2], #1
        ldrb            lr, [r2,  r3]
        ldrb            r8, [r2], #1
        ldrb            r9, [r2,  r3]
        pkhbt           r6,  r6,  r10, lsl #16
        ldrb            r10,[r2], #1
        pkhbt           r7,  r7,  r11, lsl #16
        pkhbt           r8,  r8,  lr,  lsl #16
        pkhbt           r9,  r10, r9,  lsl #16

        smuad           r6,  r6,  r5            @ apply the filter
        smuad           r7,  r7,  r5
        smuad           r8,  r8,  r5
        smuad           r9,  r9,  r5

        subs            r4,  r4,  #1            @ counter--

        add             r6,  r6,  #0x4          @ round_shift_and_clamp
        add             r7,  r7,  #0x4
        add             r8,  r8,  #0x4
        add             r9,  r9,  #0x4

        asr             r6,  #3
        asr             r7,  #3
        pkhbt           r6,  r6,  r8,  lsl #13
        pkhbt           r7,  r7,  r9,  lsl #13
        orr             r6,  r6,  r7,  lsl #8
        str             r6, [r0], #4            @ store result

        bne             1b

        ldr             r4, [sp,  #36]          @ 4-in-parallel loop counter
        subs            r12, r12, #1

        add             r2,  r2,  r3            @ move to next input/output lines
        add             r0,  r0,  r1
        sub             r2,  r2,  r4,  lsl #2

        bne             1b
        pop            {r4 - r11, pc}
endfunc
