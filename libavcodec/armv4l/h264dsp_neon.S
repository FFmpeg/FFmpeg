/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

        .fpu neon

/* chroma_mc8(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
        .macro  h264_chroma_mc8 avg=0
        push            {r4-r7, lr}
        ldrd            r4,  [sp, #20]
.if \avg
        mov             lr,  r0
.endif
        pld             [r1]
        pld             [r1, r2]

        muls            r7,  r4,  r5
        rsb             r6,  r7,  r5,  lsl #3
        rsb             ip,  r7,  r4,  lsl #3
        sub             r4,  r7,  r4,  lsl #3
        sub             r4,  r4,  r5,  lsl #3
        add             r4,  r4,  #64

        beq             2f

        add             r5,  r1,  r2

        vdup.8          d0,  r4
        lsl             r4,  r2,  #1
        vdup.8          d1,  ip
        vld1.64         {d4, d5}, [r1], r4
        vdup.8          d2,  r6
        vld1.64         {d6, d7}, [r5], r4
        vdup.8          d3,  r7

        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1

1:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
        vld1.64         {d4, d5}, [r1], r4
        vmlal.u8        q8,  d6,  d2
        vext.8          d5,  d4,  d5,  #1
        vmlal.u8        q8,  d7,  d3
        vmull.u8        q9,  d6,  d0
        subs            r3,  r3,  #2
        vmlal.u8        q9,  d7,  d1
        vmlal.u8        q9,  d4,  d2
        vmlal.u8        q9,  d5,  d3
        vrshrn.u16      d16, q8,  #6
        vld1.64         {d6, d7}, [r5], r4
        pld             [r1]
        vrshrn.u16      d17, q9,  #6
.if \avg
        vld1.64         {d20}, [lr,:64], r2
        vld1.64         {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
.endif
        vext.8          d7,  d6,  d7,  #1
        vst1.64         {d16}, [r0,:64], r2
        vst1.64         {d17}, [r0,:64], r2
        bgt             1b

        pop             {r4-r7, pc}

2:      tst             r6,  r6
        add             ip,  ip,  r6
        vdup.8          d0,  r4
        vdup.8          d1,  ip

        beq             4f

        add             r5,  r1,  r2
        lsl             r4,  r2,  #1
        vld1.64         {d4}, [r1], r4
        vld1.64         {d6}, [r5], r4

3:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d6,  d1
        vld1.64         {d4}, [r1], r4
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d4,  d1
        vld1.64         {d6}, [r5], r4
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
.if \avg
        vld1.64         {d20}, [lr,:64], r2
        vld1.64         {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
.endif
        subs            r3,  r3,  #2
        pld             [r1]
        vst1.64         {d16}, [r0,:64], r2
        vst1.64         {d17}, [r0,:64], r2
        bgt             3b

        pop             {r4-r7, pc}

4:      vld1.64         {d4, d5}, [r1], r2
        vld1.64         {d6, d7}, [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1

5:      pld             [r1]
        subs            r3,  r3,  #2
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
        vld1.64         {d4, d5}, [r1], r2
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d7,  d1
        pld             [r1]
        vext.8          d5,  d4,  d5,  #1
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
.if \avg
        vld1.64         {d20}, [lr,:64], r2
        vld1.64         {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
.endif
        vld1.64         {d6, d7}, [r1], r2
        vext.8          d7,  d6,  d7,  #1
        vst1.64         {d16}, [r0,:64], r2
        vst1.64         {d17}, [r0,:64], r2
        bgt             5b

        pop             {r4-r7, pc}
        .endm

/* chroma_mc4(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
        .macro  h264_chroma_mc4 avg=0
        push            {r4-r7, lr}
        ldrd            r4,  [sp, #20]
.if \avg
        mov             lr,  r0
.endif
        pld             [r1]
        pld             [r1, r2]

        muls            r7,  r4,  r5
        rsb             r6,  r7,  r5,  lsl #3
        rsb             ip,  r7,  r4,  lsl #3
        sub             r4,  r7,  r4,  lsl #3
        sub             r4,  r4,  r5,  lsl #3
        add             r4,  r4,  #64

        beq             2f

        add             r5,  r1,  r2

        vdup.8          d0,  r4
        lsl             r4,  r2,  #1
        vdup.8          d1,  ip
        vld1.64         {d4},     [r1], r4
        vdup.8          d2,  r6
        vld1.64         {d6},     [r5], r4
        vdup.8          d3,  r7

        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d4,  d5
        vtrn.32         d6,  d7

        vtrn.32         d0,  d1
        vtrn.32         d2,  d3

1:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d6,  d2
        vld1.64         {d4},     [r1], r4
        vext.8          d5,  d4,  d5,  #1
        vtrn.32         d4,  d5
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d4,  d2
        vld1.64         {d6},     [r5], r4
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        vrshrn.u16      d16, q8,  #6
        subs            r3,  r3,  #2
        pld             [r1]
.if \avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
.endif
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d6,  d7
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             1b

        pop             {r4-r7, pc}

2:      tst             r6,  r6
        add             ip,  ip,  r6
        vdup.8          d0,  r4
        vdup.8          d1,  ip
        vtrn.32         d0,  d1

        beq             4f

        vext.32         d1,  d0,  d1,  #1
        add             r5,  r1,  r2
        lsl             r4,  r2,  #1
        vld1.32         {d4[0]},  [r1], r4
        vld1.32         {d4[1]},  [r5], r4

3:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vld1.32         {d4[0]},  [r1], r4
        vmull.u8        q9,  d4,  d1
        vld1.32         {d4[1]},  [r5], r4
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        vrshrn.u16      d16, q8,  #6
.if \avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
.endif
        subs            r3,  r3,  #2
        pld             [r1]
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             3b

        pop             {r4-r7, pc}

4:      vld1.64         {d4},     [r1], r2
        vld1.64         {d6},     [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d4,  d5
        vtrn.32         d6,  d7

5:      vmull.u8        q8,  d4,  d0
        vmull.u8        q9,  d6,  d0
        subs            r3,  r3,  #2
        vld1.64         {d4},     [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vtrn.32         d4,  d5
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        pld             [r1]
        vrshrn.u16      d16, q8,  #6
.if \avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
.endif
        vld1.64         {d6},     [r1], r2
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d6,  d7
        pld             [r1]
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             5b

        pop             {r4-r7, pc}
        .endm

        .text
        .align

function ff_put_h264_chroma_mc8_neon, export=1
        h264_chroma_mc8
        .endfunc

function ff_avg_h264_chroma_mc8_neon, export=1
        h264_chroma_mc8 avg=1
        .endfunc

function ff_put_h264_chroma_mc4_neon, export=1
        h264_chroma_mc4
        .endfunc

function ff_avg_h264_chroma_mc4_neon, export=1
        h264_chroma_mc4 avg=1
        .endfunc
