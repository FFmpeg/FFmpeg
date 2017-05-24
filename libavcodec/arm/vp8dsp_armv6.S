/*
 * VP8 ARMv6 optimisations
 *
 * Copyright (c) 2010 Google Inc.
 * Copyright (c) 2010 Rob Clark <rob@ti.com>
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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
 *
 * This code was partially ported from libvpx, which uses this license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *   * Neither the name of Google nor the names of its contributors may
 *     be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "libavutil/arm/asm.S"

@ idct

@ void vp8_luma_dc_wht(int16_t block[4][4][16], int16_t dc[16])
function ff_vp8_luma_dc_wht_armv6, export=1
        push            {r4-r10, lr}

        ldm             r1,  {r2-r9}
        mov             r10, #0
        mov             lr,  #0
        uadd16          r12, r2,  r8            @ t0[0,1]
        usub16          r2,  r2,  r8            @ t3[0,1]
        stm             r1!, {r10, lr}
        uadd16          r8,  r4,  r6            @ t1[0,1]
        usub16          r4,  r4,  r6            @ t2[0,1]
        stm             r1!, {r10, lr}
        uadd16          r6,  r12, r8            @ dc0[0,1]
        usub16          r12, r12, r8            @ dc2[0,1]
        stm             r1!, {r10, lr}
        uadd16          r8,  r2,  r4            @ dc1[0,1]
        usub16          r2,  r2,  r4            @ dc3[0,1]
        stm             r1!, {r10, lr}

        uadd16          lr,  r3,  r9            @ t0[2,3]
        usub16          r3,  r3,  r9            @ t3[2,3]
        uadd16          r9,  r5,  r7            @ t1[2,3]
        usub16          r5,  r5,  r7            @ t2[2,3]

        uadd16          r7,  lr,  r9            @ dc0[2,3]
        usub16          lr,  lr,  r9            @ dc2[2,3]
        uadd16          r9,  r3,  r5            @ dc1[2,3]
        usub16          r3,  r3,  r5            @ dc3[2,3]

        mov             r1,  #3
        orr             r1,  r1,  #0x30000      @ 3 | 3 (round)

        pkhbt           r4,  r6,  r8,  lsl #16  @ dc{0,1}[0]
        pkhtb           r6,  r8,  r6,  asr #16  @ dc{0,1}[1]
        pkhbt           r5,  r12, r2,  lsl #16  @ dc{2,3}[0]
        pkhtb           r12, r2,  r12, asr #16  @ dc{2,3}[1]
        pkhbt           r8,  r7,  r9,  lsl #16  @ dc{0,1}[2]
        uadd16          r4,  r4,  r1
        uadd16          r5,  r5,  r1
        pkhtb           r7,  r9,  r7,  asr #16  @ dc{0,1}[3]
        pkhbt           r2,  lr,  r3,  lsl #16  @ dc{2,3}[2]
        pkhtb           lr,  r3,  lr,  asr #16  @ dc{2,3}[3]

        uadd16          r9,  r4,  r7            @ t0[0,1]
        uadd16          r3,  r5,  lr            @ t0[2,3]
        usub16          r4,  r4,  r7            @ t3[0,1]
        usub16          r5,  r5,  lr            @ t3[2,3]
        uadd16          r7,  r6,  r8            @ t1[0,1]
        uadd16          lr,  r12, r2            @ t1[2,3]
        usub16          r6,  r6,  r8            @ t2[0,1]
        usub16          r12, r12, r2            @ t2[2,3]

        uadd16          r8,  r9,  r7            @ block[0,1][0]
        uadd16          r2,  r3,  lr            @ block[2,3][0]
        usub16          r9,  r9,  r7            @ block[0,1][2]
        usub16          r3,  r3,  lr            @ block[2,3][2]
        uadd16          r7,  r4,  r6            @ block[0,1][1]
        uadd16          lr,  r5,  r12           @ block[2,3][1]
        usub16          r4,  r4,  r6            @ block[0,1][3]
        usub16          r5,  r5,  r12           @ block[2,3][3]

#if HAVE_ARMV6T2_EXTERNAL
        sbfx            r6,  r8,  #3,  #13
        sbfx            r12, r7,  #3,  #13
        sbfx            r1,  r9,  #3,  #13
        sbfx            r10, r4,  #3,  #13
#else
        sxth            r6,  r8
        sxth            r12, r7
        sxth            r1,  r9
        sxth            r10, r4
        asr             r6,  #3                 @ block[0][0]
        asr             r12, #3                 @ block[0][1]
        asr             r1,  #3                 @ block[0][2]
        asr             r10, #3                 @ block[0][3]
#endif

        strh            r6,  [r0], #32
        asr             r8,  r8,  #19           @ block[1][0]
        strh            r12, [r0], #32
        asr             r7,  r7,  #19           @ block[1][1]
        strh            r1,  [r0], #32
        asr             r9,  r9,  #19           @ block[1][2]
        strh            r10, [r0], #32
        asr             r4,  r4,  #19           @ block[1][3]
        strh            r8,  [r0], #32
        asr             r6,  r2,  #19           @ block[3][0]
        strh            r7,  [r0], #32
        asr             r12, lr,  #19           @ block[3][1]
        strh            r9,  [r0], #32
        asr             r1,  r3,  #19           @ block[3][2]
        strh            r4,  [r0], #32
        asr             r10, r5,  #19           @ block[3][3]

#if HAVE_ARMV6T2_EXTERNAL
        sbfx            r2,  r2,  #3,  #13
        sbfx            lr,  lr,  #3,  #13
        sbfx            r3,  r3,  #3,  #13
        sbfx            r5,  r5,  #3,  #13
#else
        sxth            r2,  r2
        sxth            lr,  lr
        sxth            r3,  r3
        sxth            r5,  r5
        asr             r2,  #3                 @ block[2][0]
        asr             lr,  #3                 @ block[2][1]
        asr             r3,  #3                 @ block[2][2]
        asr             r5,  #3                 @ block[2][3]
#endif

        strh            r2,  [r0], #32
        strh            lr,  [r0], #32
        strh            r3,  [r0], #32
        strh            r5,  [r0], #32
        strh            r6,  [r0], #32
        strh            r12, [r0], #32
        strh            r1,  [r0], #32
        strh            r10, [r0], #32

        pop             {r4-r10, pc}
endfunc

@ void vp8_luma_dc_wht_dc(int16_t block[4][4][16], int16_t dc[16])
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

@ void vp8_idct_add(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
function ff_vp8_idct_add_armv6, export=1
        push            {r4-r12, lr}
        sub             sp,  sp,  #32

        movw            r3,  #20091             @ cospi8sqrt2minus1
        movw            r4,  #35468             @ sinpi8sqrt2
        mov             r5,  sp
1:
        ldr             r6,  [r1, #8]       @  i5 | i4  = block1[1] | block1[0]
        ldr             lr,  [r1, #16]      @  i9 | i8  = block2[1] | block2[0]
        ldr             r12, [r1, #24]      @ i13 | i12 = block3[1] | block3[0]

        smulwt          r9,  r3,  r6            @ ip[5] * cospi8sqrt2minus1
        smulwb          r7,  r3,  r6            @ ip[4] * cospi8sqrt2minus1
        smulwt          r10, r4,  r6            @ ip[5] * sinpi8sqrt2
        smulwb          r8,  r4,  r6            @ ip[4] * sinpi8sqrt2
        pkhbt           r7,  r7,  r9,  lsl #16  @ 5c | 4c
        smulwt          r11, r3,  r12           @ ip[13] * cospi8sqrt2minus1
        pkhbt           r8,  r8,  r10, lsl #16  @ 5s   | 4s   = t2 first half
        uadd16          r6,  r6,  r7            @ 5c+5 | 4c+4 = t3 first half
        smulwb          r9,  r3,  r12           @ ip[12] * cospi8sqrt2minus1
        smulwt          r7,  r4,  r12           @ ip[13] * sinpi8sqrt2
        smulwb          r10, r4,  r12           @ ip[12] * sinpi8sqrt2

        pkhbt           r9,  r9,  r11, lsl #16  @ 13c | 12c
        ldr             r11, [r1]               @  i1 | i0
        pkhbt           r10, r10,  r7, lsl #16  @ 13s | 12s    = t3 second half
        uadd16          r7,  r12, r9            @ 13c+13  | 12c+12 = t2 2nd half
        uadd16          r6,  r6,  r10           @ d = t3
        uadd16          r10, r11, lr            @ a = t0
        usub16          r7,  r8,  r7            @ c = t2
        usub16          r8,  r11, lr            @ b = t1
        uadd16          r9,  r10, r6            @ a+d = tmp{0,1}[0]
        usub16          r10, r10, r6            @ a-d = tmp{0,1}[3]
        uadd16          r6,  r8,  r7            @ b+c = tmp{0,1}[1]
        usub16          r7,  r8,  r7            @ b-c = tmp{0,1}[2]
        mov             r8,  #0
        cmp             sp,  r5
        str             r6,  [r5, #8]           @  o5 | o4
        str             r7,  [r5, #16]          @  o9 | o8
        str             r10, [r5, #24]          @ o13 | o12
        str             r9,  [r5], #4           @  o1 | o0
        str             r8,  [r1, #8]
        str             r8,  [r1, #16]
        str             r8,  [r1, #24]
        str             r8,  [r1], #4
        beq             1b

        mov             r5,  #2
2:
        pop             {r1, r6, r12, lr}
        smulwt          r9,  r3,  r12           @ ip[5] * cospi8sqrt2minus1
        smulwt          r7,  r3,  r1            @ ip[1] * cospi8sqrt2minus1
        smulwt          r10, r4,  r12           @ ip[5] * sinpi8sqrt2
        smulwt          r8,  r4,  r1            @ ip[1] * sinpi8sqrt2
        pkhbt           r11, r1,  r12, lsl #16  @ i4 | i0 = t0/t1 first half
        pkhtb           r1,  r12, r1,  asr #16  @ i5 | i1
        pkhbt           r7,  r7,  r9,  lsl #16  @ 5c | 1c
        pkhbt           r8,  r8,  r10, lsl #16  @ 5s | 1s = t2 first half
        pkhbt           r9,  r6,  lr,  lsl #16  @ i6 | i2 = t0/t1 second half
        pkhtb           r12, lr,  r6,  asr #16  @ i7 | i3
        uadd16          r1,  r7,  r1            @ 5c+5 | 1c+1 = t3 first half
        uadd16          r10, r11, r9            @ a = t0
        usub16          r9,  r11, r9            @ b = t1
        smulwt          r7,  r3,  r12           @ ip[7] * cospi8sqrt2minus1
        smulwb          lr,  r3,  r12           @ ip[3] * cospi8sqrt2minus1
        smulwt          r11, r4,  r12           @ ip[7] * sinpi8sqrt2
        smulwb          r6,  r4,  r12           @ ip[3] * sinpi8sqrt2
        subs            r5,  r5,  #1
        pkhbt           r7,  lr,  r7,  lsl #16  @ 7c | 3c
        pkhbt           r11, r6,  r11, lsl #16  @ 7s | 3s = t3 second half
        mov             r6,  #0x4
        orr             r6,  r6,  #0x40000
        uadd16          r12, r7,  r12           @ 7c+7 | 3c+3 = t2 second half
        uadd16          r10, r10, r6            @ t0 + 4
        uadd16          r9,  r9,  r6            @ t1 + 4
        usub16          lr,  r8,  r12           @ c (o5 | o1) = t2
        uadd16          r12, r11, r1            @ d (o7 | o3) = t3
        usub16          r1,  r9,  lr            @ b-c = dst{0,1}[2]
        uadd16          r7,  r10, r12           @ a+d = dst{0,1}[0]
        usub16          r12, r10, r12           @ a-d = dst{0,1}[3]
        uadd16          r10, r9,  lr            @ b+c = dst{0,1}[1]

        asr             lr,  r1,  #3            @ o[1][2]
        asr             r9,  r12, #3            @ o[1][3]
        pkhtb           r8,  lr,  r7,  asr #19  @ o[1][0,2]
        pkhtb           r11, r9,  r10, asr #19  @ o[1][1,3]
        ldr             lr,  [r0]
        sxth            r12, r12
        ldr             r9,  [r0, r2]
        sxth            r1,  r1
#if HAVE_ARMV6T2_EXTERNAL
        sbfx            r7,  r7,  #3,  #13
        sbfx            r10, r10, #3,  #13
#else
        sxth            r7,  r7
        sxth            r10, r10
        asr             r7,  #3                 @ o[0][0]
        asr             r10, #3                 @ o[0][1]
#endif
        pkhbt           r7,  r7,  r1,  lsl #13  @ o[0][0,2]
        pkhbt           r10, r10, r12, lsl #13  @ o[0][1,3]

        uxtab16         r7,  r7,  lr
        uxtab16         r10, r10, lr,  ror #8
        uxtab16         r8,  r8,  r9
        uxtab16         r11, r11, r9,  ror #8
        usat16          r7,  #8,  r7
        usat16          r10, #8,  r10
        usat16          r8,  #8,  r8
        usat16          r11, #8,  r11
        orr             r7,  r7,  r10, lsl #8
        orr             r8,  r8,  r11, lsl #8
        str             r8,  [r0, r2]
        str_post        r7,  r0,  r2,  lsl #1

        bne             2b

        pop             {r4-r12, pc}
endfunc

@ void vp8_idct_dc_add(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
function ff_vp8_idct_dc_add_armv6, export=1
        push            {r4-r6, lr}
        add             r6,  r0,  r2,  lsl #1
        ldrsh           r3,  [r1]
        mov             r4,  #0
        add             r3,  r3,  #4
        strh            r4,  [r1], #32
        asr             r3,  #3
        ldr             r5,  [r0]
        ldr             r4,  [r0, r2]
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
        ldr             r5,  [r6]
        orr             r12, r12, r4,  lsl #8
        ldr             r4,  [r6, r2]
        str             lr,  [r0]
        uxtab16         lr,  r3,  r5
        str             r12, [r0, r2]
        uxtab16         r5,  r3,  r5,  ror #8
        uxtab16         r12, r3,  r4
        uxtab16         r4,  r3,  r4,  ror #8
        usat16          lr,  #8,  lr
        usat16          r5,  #8,  r5
        usat16          r12, #8,  r12
        usat16          r4,  #8,  r4
        orr             lr,  lr,  r5,  lsl #8
        orr             r12, r12, r4,  lsl #8
        str             lr,  [r6]
        str             r12, [r6, r2]
        pop             {r4-r6, pc}
endfunc

@ void vp8_idct_dc_add4uv(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride)
function ff_vp8_idct_dc_add4uv_armv6, export=1
        push            {r4, lr}

        bl              X(ff_vp8_idct_dc_add_armv6)
        add             r0,  r0,  #4
        bl              X(ff_vp8_idct_dc_add_armv6)
        add             r0,  r0,  r2,  lsl #2
        sub             r0,  r0,  #4
        bl              X(ff_vp8_idct_dc_add_armv6)
        add             r0,  r0,  #4
        bl              X(ff_vp8_idct_dc_add_armv6)

        pop             {r4, pc}
endfunc

@ void vp8_idct_dc_add4y(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride)
function ff_vp8_idct_dc_add4y_armv6, export=1
        push            {r4, lr}

        bl              X(ff_vp8_idct_dc_add_armv6)
        add             r0,  r0,  #4
        bl              X(ff_vp8_idct_dc_add_armv6)
        add             r0,  r0,  #4
        bl              X(ff_vp8_idct_dc_add_armv6)
        add             r0,  r0,  #4
        bl              X(ff_vp8_idct_dc_add_armv6)

        pop             {r4, pc}
endfunc

@ loopfilter

.macro  transpose       o3,  o2,  o1,  o0,  i0,  i1,  i2,  i3
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

        pkhtb           \o2, \o3, \o1, asr #16  @ 32 22 12 02
        pkhbt           \o0, \o1, \o3, lsl #16  @ 30 20 10 00
        pkhtb           \o3, \i2, \i0, asr #16  @ 33 23 13 03
        pkhbt           \o1, \i0, \i2, lsl #16  @ 31 21 11 01
.endm

.macro  simple_filter
        uqsub8          r7,  r3,  r6            @ p1 - q1
        uqsub8          r8,  r6,  r3            @ q1 - p1
        uqsub8          r10, r4,  r5            @ p0 - q0
        uqsub8          r9,  r5,  r4            @ q0 - p0
        orr             r7,  r7,  r8            @ abs(p1 - q1)
        orr             r9,  r9,  r10           @ abs(p0 - q0)
        uhadd8          r7,  r7,  lr            @ abs(p1 - q2) >> 1
        uqadd8          r9,  r9,  r9            @ abs(p0 - q0) * 2
        uqadd8          r7,  r7,  r9            @ abs(p0 - q0)*2 + abs(p1-q1)/2
        mvn             r8,  #0
        usub8           r10, r12, r7            @ compare to flimit
        sel             r10, r8,  lr            @ filter mask: F or 0
        cmp             r10, #0
        beq             2f

        eor             r3,  r3,  r2            @ ps1
        eor             r6,  r6,  r2            @ qs1
        eor             r4,  r4,  r2            @ ps0
        eor             r5,  r5,  r2            @ qs0

        qsub8           r3,  r3,  r6            @ vp8_filter = p1 - q1
        qsub8           r6,  r5,  r4            @ q0 - p0
        qadd8           r3,  r3,  r6            @ += q0 - p0
        lsr             r7,  r2,  #5            @ 0x04040404
        qadd8           r3,  r3,  r6            @ += q0 - p0
        sub             r9,  r7,  r2,  lsr #7   @ 0x03030303
        qadd8           r3,  r3,  r6            @ vp8_filter = p1-q1 + 3*(q0-p0)
        and             r3,  r3,  r10           @ vp8_filter &= mask

        qadd8           r9,  r3,  r9            @ Filter2 = vp8_filter + 3
        qadd8           r3,  r3,  r7            @ Filter1 = vp8_filter + 4

        shadd8          r9,  r9,  lr
        shadd8          r3,  r3,  lr
        shadd8          r9,  r9,  lr
        shadd8          r3,  r3,  lr
        shadd8          r9,  r9,  lr            @ Filter2 >>= 3
        shadd8          r3,  r3,  lr            @ Filter1 >>= 3

        qadd8           r4,  r4,  r9            @ u = p0 + Filter2
        qsub8           r5,  r5,  r3            @ u = q0 - Filter1
        eor             r4,  r4,  r2            @ *op0 = u ^ 0x80
        eor             r5,  r5,  r2            @ *oq0 = u ^ 0x80
.endm

@ void vp8_v_loop_filter16_simple(uint8_t *dst, ptrdiff_t stride, int flim)
function ff_vp8_v_loop_filter16_simple_armv6, export=1
        push            {r4-r11, lr}

        orr             r2,  r2,  r2,  lsl #16
        mov             r11, #4
        mov             lr,  #0
        orr             r12, r2,  r2,  lsl #8
        mov32           r2,  0x80808080
1:
        ldr_nreg        r3,  r0,  r1,  lsl #1   @ p1
        ldr_nreg        r4,  r0,  r1            @ p0
        ldr             r5,  [r0]               @ q0
        ldr             r6,  [r0, r1]           @ q1
        simple_filter
T       sub             r7,  r0,  r1
        str             r5,  [r0]               @ oq0
A       str             r4,  [r0, -r1]          @ op0
T       str             r4,  [r7]
2:
        subs            r11, r11, #1
        add             r0,  r0,  #4
        bne             1b

        pop             {r4-r11, pc}
endfunc

.macro  filter_mask_p
        uqsub8          r6,  r9,  r10           @ p3 - p2
        uqsub8          r7,  r10, r9            @ p2 - p3
        uqsub8          r8,  r10, r11           @ p2 - p1
        uqsub8          r10, r11, r10           @ p1 - p2
        orr             r6,  r6,  r7            @ abs(p3-p2)
        orr             r8,  r8,  r10           @ abs(p2-p1)
        uqsub8          lr,  r6,  r2            @ compare to limit
        uqsub8          r8,  r8,  r2            @ compare to limit
        uqsub8          r6,  r11, r12           @ p1 - p0
        orr             lr,  lr,  r8
        uqsub8          r7,  r12, r11           @ p0 - p1
        orr             r6,  r6,  r7            @ abs(p1-p0)
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r8,  r6,  r3            @ compare to thresh
        orr             lr,  lr,  r7
.endm

.macro filter_mask_pq
        uqsub8          r6,  r11, r10           @ p1 - q1
        uqsub8          r7,  r10, r11           @ q1 - p1
        uqsub8          r11, r12, r9            @ p0 - q0
        uqsub8          r12, r9,  r12           @ q0 - p0
        orr             r6,  r6,  r7            @ abs(p1-q1)
        orr             r12, r11, r12           @ abs(p0-q0)
        mov32           r7,  0x7f7f7f7f
        uqadd8          r12, r12, r12           @ abs(p0-q0) * 2
        and             r6,  r7,  r6,  lsr #1   @ abs(p1-q1) / 2
        uqadd8          r12, r12, r6            @ abs(p0-q0) * 2 + abs(p1-q1)/2
.endm

.macro  filter_mask_v
        filter_mask_p

        ldr             r10, [r0, r1]           @ q1
        ldr_post        r9,  r0,  r1,  lsl #1   @ q0

        filter_mask_pq

        ldr             r11, [r0]               @ q2

        uqsub8          r7,  r9,  r10           @ q0 - q1
        uqsub8          r6,  r10, r9            @ q1 - q0
        uqsub8          r12, r12, r4            @ compare to flimit
        uqsub8          r9,  r11, r10           @ q2 - q1
        uqsub8          r10, r10, r11           @ q1 - q2
        orr             lr,  lr,  r12
        ldr             r12, [r0, r1]           @ q3
        orr             r6,  r7,  r6            @ abs(q1-q0)
        orr             r10, r9,  r10           @ abs(q2-q1)
        uqsub8          r9,  r12, r11           @ q3 - q2
        uqsub8          r11, r11, r12           @ q2 - q3
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r10, r10, r2            @ compare to limit
        uqsub8          r6,  r6,  r3            @ compare to thresh
        orr             r9,  r9,  r11           @ abs(q3-q2)
        orr             lr,  lr,  r7
        orr             lr,  lr,  r10
        uqsub8          r9,  r9,  r2            @ compare to limit
        orr             lr,  lr,  r9

        mov             r12, #0
        usub8           lr,  r12, lr
        mvn             r11, #0
        sel             lr,  r11, r12           @ filter mask
        sub             r0,  r0,  r1,  lsl #1
.endm

.macro  filter_mask_h
        transpose       r12, r11, r10, r9,  r6,  r7,  r8,  lr

        filter_mask_p

        stm             sp,  {r8, r11, r12, lr}
        sub             r0,  r0,  r1,  lsl #2
        add             r0,  r0,  #4

        ldr             r7,  [r0, r1]
        ldr_post        r6,  r0,  r1,  lsl #1
        ldr             lr,  [r0, r1]
        ldr             r8,  [r0]

        transpose       r12, r11, r10, r9,  r6,  r7,  r8,  lr

        uqsub8          r8,  r12, r11           @ q3 - q2
        uqsub8          lr,  r11, r12           @ q2 - q3
        uqsub8          r7,  r9,  r10           @ q0 - q1
        uqsub8          r6,  r10, r9            @ q1 - q0
        uqsub8          r12, r11, r10           @ q2 - q1
        uqsub8          r11, r10, r11           @ q1 - q2
        orr             r8,  r8,  lr            @ abs(q3-q2)
        orr             r6,  r7,  r6            @ abs(q1-q0)
        orr             r11, r12, r11           @ abs(q2-q1)
        ldr             lr,  [sp, #12]          @ load back (f)limit accumulator
        uqsub8          r8,  r8,  r2            @ compare to limit
        uqsub8          r7,  r6,  r2            @ compare to limit
        uqsub8          r11, r11, r2            @ compare to limit
        orr             lr,  lr,  r8
        uqsub8          r8,  r6,  r3            @ compare to thresh
        orr             lr,  lr,  r7
        ldr             r12, [sp, #8]           @ p1
        orr             lr,  lr,  r11

        ldr             r11, [sp, #4]           @ p0

        filter_mask_pq

        mov             r10, #0
        uqsub8          r12, r12, r4            @ compare to flimit
        mvn             r11, #0
        orr             lr,  lr,  r12
        usub8           lr,  r10, lr
        sel             lr,  r11, r10           @ filter mask
.endm

.macro  filter          inner
        mov32           r12, 0x80808080
        eor             r11, r7,  r12           @ ps1
        eor             r8,  r8,  r12           @ ps0
        eor             r9,  r9,  r12           @ qs0
        eor             r10, r10, r12           @ qs1

        stm             sp,  {r8-r11}

        qsub8           r7,  r11, r10           @ vp8_signed_char_clamp(ps1-qs1)
        qsub8           r8,  r9,  r8            @ vp8_signed_char_clamp(vp8_filter + 3 * ( qs0 - ps0))
    .if \inner
        and             r7,  r7,  r6            @ vp8_filter &= hev
    .endif
        qadd8           r7,  r7,  r8
        lsr             r10, r12, #5            @ 0x04040404
        qadd8           r7,  r7,  r8
        sub             r9,  r10, r12, lsr #7   @ 0x03030303
        qadd8           r7,  r7,  r8

        and             r7,  r7,  lr            @ vp8_filter &= mask
    .if !\inner
        mov             r12, r7                 @ Filter2
        and             r7,  r7,  r6            @ Filter2 &= hev
    .endif
        qadd8           lr,  r7,  r9            @ Filter2 = vp8_signed_char_clamp(vp8_filter+3)
        qadd8           r7,  r7,  r10           @ Filter1 = vp8_signed_char_clamp(vp8_filter+4)

        mov             r9,  #0
        shadd8          lr,  lr,  r9            @ Filter2 >>= 3
        shadd8          r7,  r7,  r9            @ Filter1 >>= 3
        shadd8          lr,  lr,  r9
        shadd8          r7,  r7,  r9
        shadd8          lr,  lr,  r9            @ Filter2
        shadd8          r7,  r7,  r9            @ Filter1
.endm

.macro  filter_v        inner
        orr             r10, r6,  r8            @ calculate vp8_hevmask
        ldr_nreg        r7,  r0,  r1,  lsl #1   @ p1
        usub8           r10, r12, r10
        ldr_nreg        r8,  r0,  r1            @ p0
        sel             r6,  r12, r11           @ obtain vp8_hevmask
        ldr             r9,  [r0]               @ q0
        ldr             r10, [r0, r1]           @ q1
        filter          \inner
.endm

.macro  filter_h        inner
        orr             r9,  r6,  r8
        usub8           r9,  r12, r9
        sel             r6,  r12, r11           @ hev mask

        stm             sp,  {r6, lr}

        ldr_nreg        r12, r0,  r1,  lsl #1
        ldr_nreg        r11, r0,  r1
        ldr             r6,  [r0]
        ldr             lr,  [r0, r1]

        transpose       r10, r9,  r8,  r7,  r12, r11, r6,  lr

        ldm             sp,  {r6, lr}
        filter          \inner
.endm

.macro  filter_inner
        ldm             sp,  {r8, r9}
        lsr             r10, r10, #2            @ 0x01010101
        qadd8           r8,  r8,  lr            @ u = vp8_signed_char_clamp(ps0 + Filter2)
        mov             lr,  #0
        qsub8           r9,  r9,  r7            @ u = vp8_signed_char_clamp(qs0 - Filter1)
        sadd8           r7,  r7,  r10           @ vp8_filter += 1
        ldr             r10, [sp, #8]           @ qs1
        shadd8          r7,  r7,  lr            @ vp8_filter >>= 1
        eor             r8,  r8,  r12           @ *op0 = u ^ 0x80
        bic             r7,  r7,  r6            @ vp8_filter &= ~hev
        qadd8           r11, r11, r7            @ u = vp8_signed_char_clamp(ps1 + vp8_filter)
        eor             r9,  r9,  r12           @ *oq0 = u ^ 0x80
        qsub8           r10, r10, r7            @ u = vp8_signed_char_clamp(qs1 - vp8_filter)
        eor             r11, r11, r12           @ *op1 = u ^ 0x80
        eor             r10, r10, r12           @ *oq1 = u ^ 0x80
.endm

.macro  filter_x        c0
        mov             lr,  \c0
        mov             r7,  #63

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

        pkhbt           r6,  r8,  r6,  lsl #16
        pkhbt           r10, r7,  r10, lsl #16
        uxtb16          r6,  r6
        uxtb16          r10, r10

        mov32           lr,  0x80808080

        orr             r10, r6,  r10, lsl #8   @ u = vp8_signed_char_clamp((63 + Filter2 * 27)>>7)
        qsub8           r8,  r9,  r10           @ s = vp8_signed_char_clamp(qs0 - u)
        qadd8           r10, r11, r10           @ s = vp8_signed_char_clamp(ps0 + u)
        eor             r8,  r8,  lr            @ *oq0 = s ^ 0x80
        eor             r10, r10, lr            @ *op0 = s ^ 0x80
.endm

.macro  filter_1
        ldm             sp,  {r8, r9}
        qadd8           r11, r8,  lr
        qsub8           r9,  r9,  r7
        bic             r12, r12, r6            @ vp8_filter &= ~hev
        filter_x        #27
.endm

.macro  filter_2
        ldr             r9,   [sp, #8]          @ qs1
        ldr             r11,  [sp, #12]         @ ps1
        filter_x        #18
.endm

.macro  filter_3
        eor             r9,  r9,  lr
        eor             r11, r11, lr
        filter_x        #9
.endm

function vp8_v_loop_filter_inner_armv6
        mov             r5,  #4
        sub             sp,  sp,  #16

        orr             r2,  r2,  r2,  lsl #16
        orr             r3,  r3,  r3,  lsl #16
        orr             r6,  r6,  r6,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE
        orr             r2,  r3,  r3,  lsl #8   @ flimI
        orr             r3,  r6,  r6,  lsl #8   @ thresh
1:
        sub             r0,  r0,  r1,  lsl #2
        ldr             r10, [r0, r1]           @ p2
        ldr_post        r9,  r0,  r1,  lsl #1   @ p3
        ldr             r12, [r0, r1]           @ p0
        ldr_post        r11, r0,  r1,  lsl #1   @ p1

        filter_mask_v
        cmp             lr,  #0
        beq             2f
        filter_v        inner=1
        filter_inner

A       str             r11, [r0, -r1, lsl #1]  @ op1
A       str             r8,  [r0, -r1]          @ op0
T       sub             r0,  r0,  r1,  lsl #1
T       str             r8,  [r0, r1]
T       str_post        r11, r0,  r1,  lsl #1
        str             r9,  [r0]               @ oq0
        str             r10, [r0, r1]           @ oq1
2:
        add             r0,  r0,  #4
        cmp             r5,  #3
        it              eq
        ldreq           r0,  [sp, #16]
        subs            r5,  r5,  #1
        bne             1b

        add             sp,  sp,  #16
        pop             {r0, r4-r11, pc}
endfunc

function ff_vp8_v_loop_filter16_inner_armv6, export=1
        push            {r4-r11, lr}
        add             r12, r0,  #8
        push            {r12}
        ldr             r6,  [sp, #40]
        orr             r2,  r2,  r2,  lsl #16
        b               vp8_v_loop_filter_inner_armv6
endfunc

function ff_vp8_v_loop_filter8uv_inner_armv6, export=1
        push            {r1, r4-r11, lr}
        mov             r1,  r2
        orr             r2,  r3,  r3,  lsl #16
        ldr             r3,  [sp, #40]
        ldr             r6,  [sp, #44]
        b               vp8_v_loop_filter_inner_armv6
endfunc

function vp8_v_loop_filter_armv6
        mov             r5,  #4
        sub             sp,  sp,  #16

        orr             r3,  r3,  r3,  lsl #16
        orr             r6,  r6,  r6,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE
        orr             r2,  r3,  r3,  lsl #8   @ flimI
        orr             r3,  r6,  r6,  lsl #8   @ thresh
1:
        sub             r0,  r0,  r1,  lsl #2
        ldr             r10, [r0, r1]           @ p2
        ldr_post        r9,  r0,  r1,  lsl #1   @ p3
        ldr             r12, [r0, r1]           @ p0
        ldr_post        r11, r0,  r1,  lsl #1   @ p1

        filter_mask_v
        cmp             lr,  #0
        beq             2f

        filter_v        inner=0
        filter_1

        str             r8,  [r0]               @ *oq0
A       str             r10, [r0, -r1]          @ *op0
T       sub             r0,  r0,  r1,  lsl #1
T       str             r10, [r0, r1]

        filter_2

A       str             r10, [r0, -r1, lsl #1]  @ *op1
T       str_post        r10, r0,  r1,  lsl #1
        str             r8,  [r0, r1]           @ *oq1

        ldr             r9,  [r0, r1,  lsl #1]  @ q2
        add             r0,  r0,  r1
A       ldr             r11, [r0, -r1, lsl #2]  @ p2
T       ldr_dpre        r11, r0,  r1,  lsl #2

        filter_3

A       str             r10, [r0, -r1, lsl #2]  @ *op2
T       str_post        r10, r0,  r1,  lsl #2
        str             r8,  [r0, r1]           @ *oq2
        sub             r0,  r0,  r1
2:
        add             r0,  r0,  #4
        cmp             r5,  #3
        it              eq
        ldreq           r0,  [sp, #16]
        subs            r5,  r5,  #1
        bne             1b

        add             sp,  sp,  #16
        pop             {r0, r4-r11, pc}
endfunc

function ff_vp8_v_loop_filter16_armv6, export=1
        push            {r4-r11, lr}
        add             r12, r0,  #8
        push            {r12}
        ldr             r6,  [sp, #40]
        orr             r2,  r2,  r2,  lsl #16
        b               vp8_v_loop_filter_armv6
endfunc

function ff_vp8_v_loop_filter8uv_armv6, export=1
        push            {r1, r4-r11, lr}
        mov             r1,  r2
        orr             r2,  r3,  r3,  lsl #16
        ldr             r3,  [sp, #40]
        ldr             r6,  [sp, #44]
        b               vp8_v_loop_filter_armv6
endfunc

@ void vp8_h_loop_filter16_simple(uint8_t *dst, ptrdiff_t stride, int flim)
function ff_vp8_h_loop_filter16_simple_armv6, export=1
        push            {r4-r11, lr}
        orr             r12, r2,  r2,  lsl #16
        mov32           r2,  0x80808080
        orr             r12, r12, r12, lsl #8

        mov             lr,  #0
        mov             r11, #4
1:
        sub             r0,  r0,  #2
        ldr             r8,  [r0, r1]
        ldr_post        r7,  r0,  r1,  lsl #1
        ldr             r10, [r0, r1]
        ldr_post        r9,  r0,  r1,  lsl #1
        add             r0,  r0,  #2
        transpose       r6,  r5,  r4,  r3,  r7,  r8,  r9,  r10
        simple_filter
        sub             r0,  r0,  r1,  lsl #2
        sub             r0,  r0,  #1

        uxtb16          r6,  r4
        uxtb16          r8,  r5
        uxtb16          r7,  r4,  ror #8
        uxtb16          r9,  r5,  ror #8
        orr             r6,  r6,  r8,  lsl #8
        orr             r7,  r7,  r9,  lsl #8
        lsr             r4,  r6,  #16
        lsr             r5,  r7,  #16

        strh_post       r6,  r0,  r1
        strh_post       r7,  r0,  r1
        strh_post       r4,  r0,  r1
        strh_post       r5,  r0,  r1
        add             r0,  r0,  #1
2:
        subs            r11, r11, #1
        bne             1b

        pop             {r4-r11, pc}
endfunc

function vp8_h_loop_filter_inner_armv6
        mov             r5,  #4
        sub             sp,  sp,  #16

        orr             r3,  r3,  r3,  lsl #16
        orr             r9,  r9,  r9,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE
        orr             r2,  r3,  r3,  lsl #8   @ flimI
        orr             r3,  r9,  r9,  lsl #8   @ thresh
        sub             r0,  r0,  #4
1:
        ldr             r7,  [r0, r1]
        ldr_post        r6,  r0,  r1,  lsl #1
        ldr             lr,  [r0, r1]
        ldr_post        r8,  r0,  r1,  lsl #1

        filter_mask_h

        cmp             lr,  #0
        sub             r0,  r0,  #2
        beq             2f

        ldr             r6,  [sp]

        filter_h        inner=1
        filter_inner

        transpose       lr,  r12, r7,  r6,  r11, r8,  r9,  r10

A       str             r6,  [r0, -r1, lsl #1]
A       str             r7,  [r0, -r1]
T       sub             r0,  r0,  r1,  lsl #1
T       str             r7,  [r0, r1]
T       str_post        r6,  r0,  r1,  lsl #1
        str             r12, [r0]
        str             lr,  [r0, r1]
2:
        sub             r0,  r0,  #2
        add             r0,  r0,  r1,  lsl #1
        cmp             r5,  #3
        it              eq
        ldreq           r0,  [sp, #16]
        subs            r5,  r5,  #1
        bne             1b

        add             sp, sp, #16
        pop             {r0, r4-r11, pc}
endfunc

function ff_vp8_h_loop_filter16_inner_armv6, export=1
        push            {r4-r11, lr}
        add             r12, r0,  r1,  lsl #3
        sub             r12, r12, #4
        push            {r12}
        ldr             r9,  [sp, #40]
        orr             r2,  r2,  r2,  lsl #16
        b               vp8_h_loop_filter_inner_armv6
endfunc

function ff_vp8_h_loop_filter8uv_inner_armv6, export=1
        sub             r1,  r1,  #4
        push            {r1, r4-r11, lr}
        mov             r1,  r2
        orr             r2,  r3,  r3,  lsl #16
        ldr             r3,  [sp, #40]
        ldr             r9,  [sp, #44]
        b               vp8_h_loop_filter_inner_armv6
endfunc

function vp8_h_loop_filter_armv6
        mov             r5,  #4
        sub             sp,  sp,  #16

        orr             r3,  r3,  r3,  lsl #16
        orr             r9,  r9,  r9,  lsl #16
        orr             r4,  r2,  r2,  lsl #8   @ flimE
        orr             r2,  r3,  r3,  lsl #8   @ flimI
        orr             r3,  r9,  r9,  lsl #8   @ thresh
1:
        sub             r0,  r0,  #4
        ldr             r7,  [r0, r1]
        ldr_post        r6,  r0,  r1,  lsl #1
        ldr             lr,  [r0, r1]
        ldr_post        r8,  r0,  r1,  lsl #1

        filter_mask_h
        cmp             lr,  #0
        it              eq
        addeq           r0,  r0,  r1,  lsl #1
        beq             2f

        ldr             r6,  [sp]
        sub             r0,  r0,  #2

        filter_h        inner=0
        filter_1

        sub             r0,  r0,  r1,  lsl #1
        uxtb16          r6,  r10
        uxtb16          r7,  r8
        uxtb16          r10, r10, ror #8
        uxtb16          r8,  r8,  ror #8
        orr             r6,  r6,  r7,  lsl #8
        orr             r10, r10, r8,  lsl #8
        lsr             r7,  r6,  #16
        lsr             r8,  r10, #16

        add             r0,  r0,  #1
        strh_post       r6,  r0,  r1
        strh_post       r10, r0,  r1
        strh_post       r7,  r0,  r1
        strh_post       r8,  r0,  r1

        filter_2

        sub             r0,  r0,  r1,  lsl #2
        add             r0,  r0,  #3

        ldrb            r11, [r0, #-5]          @ p2 for 1/7th difference
        strb            r10, [r0, #-4]          @ op1
        strb            r8,  [r0, #-1]          @ oq1
        ldrb_post       r9,  r0,  r1            @ q2 for 1/7th difference

        lsr             r10, r10, #8
        lsr             r8,  r8,  #8

        ldrb            r6,  [r0, #-5]
        strb            r10, [r0, #-4]
        strb            r8,  [r0, #-1]
        ldrb_post       r7,  r0,  r1

        lsr             r10, r10, #8
        lsr             r8,  r8,  #8
        orr             r11, r11, r6,  lsl #8
        orr             r9,  r9,  r7,  lsl #8

        ldrb            r6,  [r0, #-5]
        strb            r10, [r0, #-4]
        strb            r8,  [r0, #-1]
        ldrb_post       r7,  r0,  r1

        lsr             r10, r10, #8
        lsr             r8,  r8,  #8
        orr             r11, r11, r6,  lsl #16
        orr             r9,  r9,  r7,  lsl #16

        ldrb            r6,  [r0, #-5]
        strb            r10, [r0, #-4]
        strb            r8,  [r0, #-1]
        ldrb_post       r7,  r0,  r1
        orr             r11, r11, r6,  lsl #24
        orr             r9,  r9,  r7,  lsl #24

        filter_3

        sub             r0,  r0,  r1,  lsl #2
        strb            r10, [r0, #-5]
        strb_post       r8,  r0,  r1
        lsr             r10, r10, #8
        lsr             r8,  r8,  #8
        strb            r10, [r0, #-5]
        strb_post       r8,  r0,  r1
        lsr             r10, r10, #8
        lsr             r8,  r8,  #8
        strb            r10, [r0, #-5]
        strb_post       r8,  r0,  r1
        lsr             r10, r10, #8
        lsr             r8,  r8,  #8
        strb            r10, [r0, #-5]
        strb_post       r8,  r0,  r1

        sub             r0,  r0,  #2
2:
        cmp             r5,  #3
        it              eq
        ldreq           r0,  [sp, #16]
        subs            r5,  r5,  #1
        bne             1b

        add             sp,  sp,  #16
        pop             {r0, r4-r11, pc}
endfunc

function ff_vp8_h_loop_filter16_armv6, export=1
        push            {r4-r11, lr}
        add             r12, r0,  r1,  lsl #3
        push            {r12}
        ldr             r9,  [sp, #40]
        orr             r2,  r2,  r2,  lsl #16
        b               vp8_h_loop_filter_armv6
endfunc

function ff_vp8_h_loop_filter8uv_armv6, export=1
        push            {r1, r4-r11, lr}
        mov             r1,  r2
        orr             r2,  r3,  r3,  lsl #16
        ldr             r3,  [sp, #40]
        ldr             r9,  [sp, #44]
        b               vp8_h_loop_filter_armv6
endfunc

.ltorg

@ MC

@ void put_vp8_pixels16(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
@                       ptrdiff_t srcstride, int h, int mx, int my)
function ff_put_vp8_pixels16_armv6, export=1
        push            {r4-r11}
        ldr             r12, [sp, #32]          @ h
1:
        subs            r12, r12, #2
        ldr             r5,  [r2, #4]
        ldr             r6,  [r2, #8]
        ldr             r7,  [r2, #12]
        ldr_post        r4,  r2,  r3
        ldr             r9,  [r2, #4]
        ldr             r10, [r2, #8]
        ldr             r11, [r2, #12]
        ldr_post        r8,  r2,  r3
        strd            r6,  r7,  [r0, #8]
        strd_post       r4,  r5,  r0,  r1
        strd            r10, r11, [r0, #8]
        strd_post       r8,  r9,  r0,  r1
        bgt             1b
        pop             {r4-r11}
        bx              lr
endfunc

@ void put_vp8_pixels8(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
@                      ptrdiff_t srcstride, int h, int mx, int my)
function ff_put_vp8_pixels8_armv6, export=1
        push            {r4-r11}
        ldr             r12, [sp, #32]          @ h
1:
        subs            r12, r12, #4
        ldr             r5,  [r2, #4]
        ldr_post        r4,  r2,  r3
        ldr             r7,  [r2, #4]
        ldr_post        r6,  r2,  r3
        ldr             r9,  [r2, #4]
        ldr_post        r8,  r2,  r3
        ldr             r11, [r2, #4]
        ldr_post        r10, r2,  r3
        strd_post       r4,  r5,  r0,  r1
        strd_post       r6,  r7,  r0,  r1
        strd_post       r8,  r9,  r0,  r1
        strd_post       r10, r11, r0,  r1
        bgt             1b
        pop             {r4-r11}
        bx              lr
endfunc

@ void put_vp8_pixels4(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
@                      ptrdiff_t srcstride, int h, int mx, int my)
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

@ note: worst case sum of all 6-tap filter values * 255 is 0x7f80 so 16 bit
@ arithmetic can be used to apply filters
const   sixtap_filters_13245600, align=4
        .short     2, 108, -11,  36,  -8, 1, 0, 0
        .short     3,  77, -16,  77, -16, 3, 0, 0
        .short     1,  36,  -8, 108, -11, 2, 0, 0
endconst

const   fourtap_filters_1324, align=4
        .short     -6,  12, 123, -1
        .short     -9,  50,  93, -6
        .short     -6,  93,  50, -9
        .short     -1, 123,  12, -6
endconst

.macro  vp8_mc_1        name, size, hv
function ff_put_vp8_\name\size\()_\hv\()_armv6, export=1
        sub             r1,  r1,  #\size
        mov             r12, sp
        push            {r1, r4-r11, lr}
        ldm             r12, {r5-r7}
        mov             r4,  #\size
        stm             r12, {r4, r5}
        orr             r12, r6,  r7
        b               bl_put_\name\()_\hv\()_armv6
endfunc
.endm

vp8_mc_1                epel,  16, h6
vp8_mc_1                epel,  16, v6
vp8_mc_1                epel,   8, h6
vp8_mc_1                epel,   8, v6
vp8_mc_1                epel,   8, h4
vp8_mc_1                epel,   8, v4
vp8_mc_1                epel,   4, h6
vp8_mc_1                epel,   4, v6
vp8_mc_1                epel,   4, h4
vp8_mc_1                epel,   4, v4

vp8_mc_1                bilin, 16, h
vp8_mc_1                bilin, 16, v
vp8_mc_1                bilin,  8, h
vp8_mc_1                bilin,  8, v
vp8_mc_1                bilin,  4, h
vp8_mc_1                bilin,  4, v

@ 4 and 8 pixel wide mc blocks might have height of 8 or 16 lines
#define TMPSIZE \size * (16 / ((16 / \size + 1) / 2) + \ytaps - 1)

.macro  vp8_mc_hv       name, size, h, v, ytaps
function ff_put_vp8_\name\size\()_\h\v\()_armv6, export=1
        push            {r0, r1, r4, lr}
        add             r0,  sp,  #16
        sub             sp,  sp,  #TMPSIZE+16
        ldm             r0,  {r0, r12}
        mov             r4,  #\size
        add             lr,  r0,  #\ytaps-1
    .if \ytaps > 2
        sub             r2,  r2,  r3,  lsl #\ytaps >> 1 & 1
    .endif
        stm             sp,  {r4, lr}
        add             r0,  sp,  #16
        mov             r1,  #0
        bl              vp8_put_\name\()_\h\()_armv6
        add             r0,  sp,  #TMPSIZE+16
        ldr             lr,  [sp, #TMPSIZE+16+16]
        ldm             r0,  {r0, r1}
        mov             r3,  #\size
        ldr             r12, [sp, #TMPSIZE+16+16+8]
        str             lr,  [sp, #4]
        add             r2,  sp,  #16 + \size * (\ytaps / 2 - 1)
        sub             r1,  r1,  #\size
        bl              vp8_put_\name\()_\v\()_armv6
        add             sp,  sp,  #TMPSIZE+16+8
        pop             {r4, pc}
endfunc
.endm

vp8_mc_hv               epel,  16, h6, v6, 6
vp8_mc_hv               epel,   8, h6, v6, 6
vp8_mc_hv               epel,   8, h4, v6, 6
vp8_mc_hv               epel,   8, h6, v4, 4
vp8_mc_hv               epel,   8, h4, v4, 4
vp8_mc_hv               epel,   4, h6, v6, 6
vp8_mc_hv               epel,   4, h4, v6, 6
vp8_mc_hv               epel,   4, h6, v4, 4
vp8_mc_hv               epel,   4, h4, v4, 4

vp8_mc_hv               bilin, 16, h,  v,  2
vp8_mc_hv               bilin,  8, h,  v,  2
vp8_mc_hv               bilin,  4, h,  v,  2

.macro  sat4            r0,  r1,  r2,  r3
        asr             \r0, \r0, #7
        asr             \r1, \r1, #7
        pkhbt           \r0, \r0, \r2, lsl #9
        pkhbt           \r1, \r1, \r3, lsl #9
        usat16          \r0, #8,  \r0
        usat16          \r1, #8,  \r1
        orr             \r0, \r0, \r1, lsl #8
.endm

@ Calling convention for the inner MC functions:
@       r0      dst
@       r1      dst_stride - block_width
@       r2      src
@       r3      src_stride
@       r4      block_width
@       r12     filter_index
@       [sp]    block_width
@       [sp+4]  height
@       [sp+8]  scratch

function vp8_put_epel_h6_armv6
        push            {r1, r4-r11, lr}
bl_put_epel_h6_armv6:
        sub             r2,  r2,  #2
        movrel          lr,  sixtap_filters_13245600 - 16
        add             lr,  lr,  r12, lsl #3
        sub             r3,  r3,  r4
        str             r3,  [sp, #48]
        ldm             lr,  {r1, r3, lr}
1:
        ldr             r7,  [r2, #5]           @ src[5-8]
        ldr             r6,  [r2, #2]           @ src[2-5]
        ldr             r5,  [r2], #4           @ src[0-3]

        pkhtb           r7,  r7,  r7,  asr #8   @ src[8,7,7,6]
        uxtb16          r9,  r6,  ror #8        @ src[5] | src[3]
        uxtb16          r6,  r6                 @ src[4] | src[2]
        uxtb16          r8,  r5,  ror #8        @ src[3] | src[1]
        uxtb16          r11, r7,  ror #8        @ src[8] | src[7]
        uxtb16          r7,  r7                 @ src[7] | src[6]
        uxtb16          r5,  r5                 @ src[2] | src[0]

        mov             r10, #0x40
        smlad           r5,  r5,  r1,  r10      @ filter[0][0]
        smlad           r11, r11, lr,  r10      @ filter[3][2]
        smlad           r12, r7,  lr,  r10      @ filter[2][2]
        smlad           r10, r8,  r1,  r10      @ filter[1][0]
        smlad           r5,  r8,  r3,  r5       @ filter[0][1]
        smlad           r11, r9,  r1,  r11      @ filter[3][0]
        smlad           r12, r9,  r3,  r12      @ filter[2][1]
        pkhtb           r9,  r9,  r6,  asr #16  @ src[5] | src[4]
        smlad           r10, r6,  r3,  r10      @ filter[1][1]
        pkhbt           r7,  r9,  r7,  lsl #16  @ src[6] | src[4]
        smlad           r5,  r9,  lr,  r5       @ filter[0][2]
        pkhtb           r8,  r7,  r9,  asr #16  @ src[6] | src[5]
        smlad           r11, r7,  r3,  r11      @ filter[3][1]
        smlad           r9,  r8,  lr,  r10      @ filter[1][2]
        smlad           r7,  r6,  r1,  r12      @ filter[2][0]

        subs            r4,  r4,  #4

        sat4            r5,  r9,  r7,  r11
        str             r5,  [r0], #4

        bne             1b

        add             r4,  sp,  #40
        ldm             r4,  {r4, r5, r12}
        ldr             r6,  [sp]
        subs            r5,  r5,  #1
        add             r2,  r2,  r12
        str             r5,  [sp, #44]
        add             r0,  r0,  r6

        bne             1b

        pop             {r1, r4-r11, pc}
endfunc

function vp8_put_epel_v6_armv6
        push            {r1, r4-r11, lr}
bl_put_epel_v6_armv6:
        movrel          lr,  sixtap_filters_13245600 - 16
        add             lr,  lr,  r12, lsl #3
        str             r3,  [sp, #48]
1:
        add             r1,  r3,  r3,  lsl #1   @ stride * 3
        ldr_nreg        r5,  r2,  r3            @ src[0,1,2,3 + stride * 1]
        ldr             r6,  [r2, r3]           @ src[0,1,2,3 + stride * 3]
        ldr             r7,  [r2, r3,  lsl #1]  @ src[0,1,2,3 + stride * 4]
        ldr             r8,  [r2, r1]           @ src[0,1,2,3 + stride * 5]

        uxtb16          r9,  r5,  ror #8        @ src[3 + s*1] | src[1 + s*1]
        uxtb16          r10, r6,  ror #8        @ src[3 + s*3] | src[1 + s*3]
        uxtb16          r11, r7,  ror #8        @ src[3 + s*4] | src[1 + s*4]
        uxtb16          r12, r8,  ror #8        @ src[3 + s*5] | src[1 + s*5]
        uxtb16          r5,  r5                 @ src[2 + s*1] | src[0 + s*1]
        uxtb16          r6,  r6                 @ src[2 + s*3] | src[0 + s*3]
        uxtb16          r7,  r7                 @ src[2 + s*4] | src[0 + s*4]
        uxtb16          r8,  r8                 @ src[2 + s*5] | src[0 + s*5]
        pkhbt           r1,  r9,  r10, lsl #16  @ src[1 + s*3] | src[1 + s*1]
        pkhtb           r9,  r10, r9,  asr #16  @ src[3 + s*3] | src[3 + s*1]
        pkhbt           r10, r11, r12, lsl #16  @ src[1 + s*5] | src[1 + s*4]
        pkhtb           r11, r12, r11, asr #16  @ src[3 + s*5] | src[3 + s*4]
        pkhbt           r12, r5,  r6,  lsl #16  @ src[0 + s*3] | src[0 + s*1]
        pkhtb           r5,  r6,  r5,  asr #16  @ src[2 + s*3] | src[2 + s*1]
        pkhbt           r6,  r7,  r8,  lsl #16  @ src[0 + s*5] | src[0 + s*4]
        pkhtb           r7,  r8,  r7,  asr #16  @ src[2 + s*5] | src[2 + s*4]

        ldr             r8,  [lr, #4]
        mov             r3,  #0x40
        smlad           r12, r12, r8,  r3       @ filter[0][1]
        smlad           r1,  r1,  r8,  r3       @ filter[1][1]
        smlad           r5,  r5,  r8,  r3       @ filter[2][1]
        smlad           r9,  r9,  r8,  r3       @ filter[3][1]
        ldr             r8,  [lr, #8]
        ldr             r3,  [sp, #48]
        smlad           r12, r6,  r8,  r12      @ filter[0][2]
        smlad           r1,  r10, r8,  r1       @ filter[1][2]
        ldr_nreg        r6,  r2,  r3,  lsl #1   @ src[0,1,2,3 + stride * 0]
        ldr             r10, [r2], #4           @ src[0,1,2,3 + stride * 2]
        smlad           r5,  r7,  r8,  r5       @ filter[2][2]
        smlad           r9,  r11, r8,  r9       @ filter[3][2]

        uxtb16          r7,  r6,  ror #8        @ src[3 + s*0] | src[1 + s*0]
        uxtb16          r11, r10, ror #8        @ src[3 + s*2] | src[1 + s*2]
        uxtb16          r6,  r6                 @ src[2 + s*0] | src[0 + s*0]
        uxtb16          r10, r10                @ src[2 + s*2] | src[0 + s*2]

        pkhbt           r8,  r7,  r11, lsl #16  @ src[1 + s*2] | src[1 + s*0]
        pkhtb           r7,  r11, r7,  asr #16  @ src[3 + s*2] | src[3 + s*0]
        pkhbt           r11, r6,  r10, lsl #16  @ src[0 + s*2] | src[0 + s*0]
        pkhtb           r6,  r10, r6,  asr #16  @ src[2 + s*2] | src[2 + s*0]

        ldr             r10, [lr]
        subs            r4,  r4,  #4
        smlad           r12, r11, r10, r12      @ filter[0][0]
        smlad           r1,  r8,  r10, r1       @ filter[1][0]
        smlad           r5,  r6,  r10, r5       @ filter[2][0]
        smlad           r9,  r7,  r10, r9       @ filter[3][0]

        sat4            r12, r1,  r5,  r9
        str             r12, [r0], #4

        bne             1b

        ldrd            r4,  r5,  [sp, #40]
        ldr             r6,  [sp]
        subs            r5,  r5,  #1
        sub             r2,  r2,  r4
        str             r5,  [sp, #44]
        add             r0,  r0,  r6
        add             r2,  r2,  r3

        bne             1b

        pop             {r1, r4-r11, pc}
endfunc

function vp8_put_epel_h4_armv6
        push            {r1, r4-r11, lr}
bl_put_epel_h4_armv6:
        subs            r2,  r2,  #1
        movrel          lr,  fourtap_filters_1324 - 4
        add             lr,  lr,  r12, lsl #2
        sub             r3,  r3,  r4
        ldm             lr,  {r5, r6}
        ldr             lr,  [sp, #44]
1:
        ldr             r9,  [r2, #3]
        ldr             r8,  [r2, #2]
        ldr             r7,  [r2], #4

        uxtb16          r9,  r9,  ror #8        @ src[6] | src[4]
        uxtb16          r10, r8,  ror #8        @ src[5] | src[3]
        uxtb16          r8,  r8                 @ src[4] | src[2]
        uxtb16          r11, r7,  ror #8        @ src[3] | src[1]
        uxtb16          r7,  r7                 @ src[2] | src[0]

        mov             r12, #0x40
        smlad           r9,  r9,  r6,  r12      @ filter[3][1]
        smlad           r7,  r7,  r5,  r12      @ filter[0][0]
        smlad           r9,  r10, r5,  r9       @ filter[3][0]
        smlad           r10, r10, r6,  r12      @ filter[2][1]
        smlad           r12, r11, r5,  r12      @ filter[1][0]
        smlad           r7,  r11, r6,  r7       @ filter[0][1]
        smlad           r10, r8,  r5,  r10      @ filter[2][0]
        smlad           r12, r8,  r6,  r12      @ filter[1][1]

        subs            r4,  r4,  #4

        sat4            r7,  r12, r10, r9
        str             r7,  [r0], #4

        bne             1b

        subs            lr,  lr,  #1
        ldr             r4,  [sp, #40]
        add             r2,  r2,  r3
        add             r0,  r0,  r1

        bne             1b

        pop             {r1, r4-r11, pc}
endfunc

function vp8_put_epel_v4_armv6
        push            {r1, r4-r11, lr}
bl_put_epel_v4_armv6:
        movrel          lr,  fourtap_filters_1324 - 4
        add             lr,  lr,  r12, lsl #2
        ldm             lr,  {r5, r6}
        str             r3,  [sp, #48]
1:
        ldr             lr,  [r2, r3, lsl #1]
        ldr             r12, [r2, r3]
        ldr_nreg        r7,  r2,  r3
        ldr             r11, [r2], #4

        uxtb16          r8,  lr,  ror #8        @ src[3 + s*3] | src[1 + s*3]
        uxtb16          r9,  r12, ror #8        @ src[3 + s*2] | src[1 + s*2]
        uxtb16          r3,  r7,  ror #8        @ src[3 + s*0] | src[1 + s*0]
        uxtb16          r1,  r11, ror #8        @ src[3 + s*1] | src[1 + s*1]
        uxtb16          lr,  lr                 @ src[2 + s*3] | src[0 + s*3]
        uxtb16          r12, r12                @ src[2 + s*2] | src[0 + s*2]
        uxtb16          r7,  r7                 @ src[2 + s*0] | src[0 + s*0]
        uxtb16          r11, r11                @ src[2 + s*1] | src[0 + s*1]
        pkhbt           r10, r1,  r8,  lsl #16  @ src[1 + s*3] | src[1 + s*1]
        pkhtb           r1,  r8,  r1,  asr #16  @ src[3 + s*3] | src[3 + s*1]
        pkhbt           r8,  r3,  r9,  lsl #16  @ src[1 + s*2] | src[1 + s*0]
        pkhtb           r3,  r9,  r3,  asr #16  @ src[3 + s*2] | src[3 + s*0]
        pkhbt           r9,  r11, lr,  lsl #16  @ src[0 + s*3] | src[0 + s*1]
        pkhtb           r11, lr,  r11, asr #16  @ src[2 + s*3] | src[2 + s*1]
        pkhbt           lr,  r7,  r12, lsl #16  @ src[0 + s*2] | src[0 + s*0]
        pkhtb           r7,  r12, r7,  asr #16  @ src[2 + s*2] | src[2 + s*0]

        mov             r12, #0x40
        smlad           r9,  r9,  r6,  r12      @ filter[0][1]
        smlad           r10, r10, r6,  r12      @ filter[1][1]
        smlad           r11, r11, r6,  r12      @ filter[2][1]
        smlad           r1,  r1,  r6,  r12      @ filter[3][1]
        smlad           r9,  lr,  r5,  r9       @ filter[0][0]
        smlad           r10, r8,  r5,  r10      @ filter[1][0]
        smlad           r11, r7,  r5,  r11      @ filter[2][0]
        smlad           r1,  r3,  r5,  r1       @ filter[3][0]

        subs            r4,  r4,  #4
        ldr             r3,  [sp, #48]

        sat4            r9,  r10, r11, r1
        str             r9,  [r0], #4

        bne             1b

        ldr             r4,  [sp, #40]
        ldr             r12, [sp, #44]
        add             r2,  r2,  r3
        ldr             r9,  [sp, #0]
        subs            r12, r12, #1
        sub             r2,  r2,  r4
        str             r12, [sp, #44]
        add             r0,  r0,  r9

        bne             1b

        pop             {r1, r4-r11, pc}
endfunc

function vp8_put_bilin_h_armv6
        push            {r1, r4-r11, lr}
bl_put_bilin_h_armv6:
        rsb             r5,  r12, r12, lsl #16
        ldr             r12, [sp, #44]
        sub             r3,  r3,  r4
        add             r5,  r5,  #8
1:
        ldrb            r6,  [r2], #1
        ldrb            r7,  [r2], #1
        ldrb            r8,  [r2], #1
        ldrb            r9,  [r2], #1
        ldrb            lr,  [r2]

        pkhbt           r6,  r6,  r7,  lsl #16  @ src[1] | src[0]
        pkhbt           r7,  r7,  r8,  lsl #16  @ src[2] | src[1]
        pkhbt           r8,  r8,  r9,  lsl #16  @ src[3] | src[2]
        pkhbt           r9,  r9,  lr,  lsl #16  @ src[4] | src[3]

        mov             r10, #4
        smlad           r6,  r6,  r5,  r10
        smlad           r7,  r7,  r5,  r10
        smlad           r8,  r8,  r5,  r10
        smlad           r9,  r9,  r5,  r10

        subs            r4,  r4,  #4

        asr             r6,  #3
        asr             r7,  #3
        pkhbt           r6,  r6,  r8,  lsl #13
        pkhbt           r7,  r7,  r9,  lsl #13
        orr             r6,  r6,  r7,  lsl #8
        str             r6,  [r0], #4

        bne             1b

        ldr             r4,  [sp, #40]
        subs            r12, r12, #1
        add             r2,  r2,  r3
        add             r0,  r0,  r1

        bne             1b

        pop             {r1, r4-r11, pc}
endfunc

function vp8_put_bilin_v_armv6
        push            {r1, r4-r11, lr}
bl_put_bilin_v_armv6:
        rsb             r5,  r12, r12, lsl #16
        ldr             r12, [sp, #44]
        add             r5,  r5,  #8
1:
        ldrb            r10, [r2, r3]
        ldrb            r6,  [r2], #1
        ldrb            r11, [r2, r3]
        ldrb            r7,  [r2], #1
        ldrb            lr,  [r2, r3]
        ldrb            r8,  [r2], #1
        ldrb            r9,  [r2, r3]
        pkhbt           r6,  r6,  r10, lsl #16
        ldrb            r10, [r2], #1
        pkhbt           r7,  r7,  r11, lsl #16
        pkhbt           r8,  r8,  lr,  lsl #16
        pkhbt           r9,  r10, r9,  lsl #16

        mov             r10, #4
        smlad           r6,  r6,  r5,  r10
        smlad           r7,  r7,  r5,  r10
        smlad           r8,  r8,  r5,  r10
        smlad           r9,  r9,  r5,  r10

        subs            r4,  r4,  #4

        asr             r6,  #3
        asr             r7,  #3
        pkhbt           r6,  r6,  r8,  lsl #13
        pkhbt           r7,  r7,  r9,  lsl #13
        orr             r6,  r6,  r7,  lsl #8
        str             r6,  [r0], #4

        bne             1b

        ldr             r4,  [sp, #40]
        subs            r12, r12, #1
        add             r2,  r2,  r3
        add             r0,  r0,  r1
        sub             r2,  r2,  r4

        bne             1b
        pop             {r1, r4-r11, pc}
endfunc
