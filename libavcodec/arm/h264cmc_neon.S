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

#include "libavutil/arm/asm.S"

/* chroma_mc8(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
.macro  h264_chroma_mc8 type, codec=h264
function ff_\type\()_\codec\()_chroma_mc8_neon, export=1
        push            {r4-r7, lr}
        ldrd            r4,  r5,  [sp, #20]
  .ifc \type,avg
        mov             lr,  r0
  .endif
        pld             [r1]
        pld             [r1, r2]

  .ifc \codec,rv40
        movrel          r6,  rv40bias
        lsr             r7,  r5,  #1
        add             r6,  r6,  r7,  lsl #3
        lsr             r7,  r4,  #1
        add             r6,  r6,  r7,  lsl #1
        vld1.16         {d22[],d23[]}, [r6,:16]
  .endif

A       muls            r7,  r4,  r5
T       mul             r7,  r4,  r5
T       cmp             r7,  #0
        rsb             r6,  r7,  r5,  lsl #3
        rsb             r12, r7,  r4,  lsl #3
        sub             r4,  r7,  r4,  lsl #3
        sub             r4,  r4,  r5,  lsl #3
        add             r4,  r4,  #64

        beq             2f

        vdup.8          d0,  r4
        vdup.8          d1,  r12
        vld1.8          {d4, d5}, [r1], r2
        vdup.8          d2,  r6
        vdup.8          d3,  r7
        vext.8          d5,  d4,  d5,  #1

1:      vld1.8          {d6, d7}, [r1], r2
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
        vext.8          d7,  d6,  d7,  #1
        vld1.8          {d4, d5}, [r1], r2
        vmlal.u8        q8,  d6,  d2
        pld             [r1]
        vext.8          d5,  d4,  d5,  #1
        vmlal.u8        q8,  d7,  d3
        vmull.u8        q9,  d6,  d0
        subs            r3,  r3,  #2
        vmlal.u8        q9,  d7,  d1
        vmlal.u8        q9,  d4,  d2
        vmlal.u8        q9,  d5,  d3
        pld             [r1, r2]
  .ifc \codec,h264
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
  .else
        vadd.u16        q8,  q8,  q11
        vadd.u16        q9,  q9,  q11
        vshrn.u16       d16, q8,  #6
        vshrn.u16       d17, q9,  #6
  .endif
  .ifc \type,avg
        vld1.8          {d20}, [lr,:64], r2
        vld1.8          {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
  .endif
        vst1.8          {d16}, [r0,:64], r2
        vst1.8          {d17}, [r0,:64], r2
        bgt             1b

        pop             {r4-r7, pc}

2:      tst             r6,  r6
        add             r12, r12, r6
        vdup.8          d0,  r4
        vdup.8          d1,  r12

        beq             4f

        vld1.8          {d4}, [r1], r2

3:      vld1.8          {d6}, [r1], r2
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d6,  d1
        vld1.8          {d4}, [r1], r2
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d4,  d1
        pld             [r1]
  .ifc \codec,h264
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
  .else
        vadd.u16        q8,  q8,  q11
        vadd.u16        q9,  q9,  q11
        vshrn.u16       d16, q8,  #6
        vshrn.u16       d17, q9,  #6
  .endif
        pld             [r1, r2]
  .ifc \type,avg
        vld1.8          {d20}, [lr,:64], r2
        vld1.8          {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
  .endif
        subs            r3,  r3,  #2
        vst1.8          {d16}, [r0,:64], r2
        vst1.8          {d17}, [r0,:64], r2
        bgt             3b

        pop             {r4-r7, pc}

4:      vld1.8          {d4, d5}, [r1], r2
        vld1.8          {d6, d7}, [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1
        pld             [r1]
        subs            r3,  r3,  #2
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d7,  d1
        pld             [r1, r2]
  .ifc \codec,h264
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
  .else
        vadd.u16        q8,  q8,  q11
        vadd.u16        q9,  q9,  q11
        vshrn.u16       d16, q8,  #6
        vshrn.u16       d17, q9,  #6
  .endif
  .ifc \type,avg
        vld1.8          {d20}, [lr,:64], r2
        vld1.8          {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
  .endif
        vst1.8          {d16}, [r0,:64], r2
        vst1.8          {d17}, [r0,:64], r2
        bgt             4b

        pop             {r4-r7, pc}
endfunc
.endm

/* chroma_mc4(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
.macro  h264_chroma_mc4 type, codec=h264
function ff_\type\()_\codec\()_chroma_mc4_neon, export=1
        push            {r4-r7, lr}
        ldrd            r4,  r5,  [sp, #20]
  .ifc \type,avg
        mov             lr,  r0
  .endif
        pld             [r1]
        pld             [r1, r2]

  .ifc \codec,rv40
        movrel          r6,  rv40bias
        lsr             r7,  r5,  #1
        add             r6,  r6,  r7,  lsl #3
        lsr             r7,  r4,  #1
        add             r6,  r6,  r7,  lsl #1
        vld1.16         {d22[],d23[]}, [r6,:16]
  .endif

A       muls            r7,  r4,  r5
T       mul             r7,  r4,  r5
T       cmp             r7,  #0
        rsb             r6,  r7,  r5,  lsl #3
        rsb             r12, r7,  r4,  lsl #3
        sub             r4,  r7,  r4,  lsl #3
        sub             r4,  r4,  r5,  lsl #3
        add             r4,  r4,  #64

        beq             2f

        vdup.8          d0,  r4
        vdup.8          d1,  r12
        vld1.8          {d4},     [r1], r2
        vdup.8          d2,  r6
        vdup.8          d3,  r7

        vext.8          d5,  d4,  d5,  #1
        vtrn.32         d4,  d5

        vtrn.32         d0,  d1
        vtrn.32         d2,  d3

1:      vld1.8          {d6},     [r1], r2
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d6,  d7
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d6,  d2
        vld1.8          {d4},     [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vtrn.32         d4,  d5
        pld             [r1]
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d4,  d2
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
  .ifc \codec,h264
        vrshrn.u16      d16, q8,  #6
  .else
        vadd.u16        q8,  q8,  q11
        vshrn.u16       d16, q8,  #6
  .endif
        subs            r3,  r3,  #2
        pld             [r1, r2]
  .ifc \type,avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
  .endif
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             1b

        pop             {r4-r7, pc}

2:      tst             r6,  r6
        add             r12, r12, r6
        vdup.8          d0,  r4
        vdup.8          d1,  r12
        vtrn.32         d0,  d1

        beq             4f

        vext.32         d1,  d0,  d1,  #1
        vld1.32         {d4[0]},  [r1], r2

3:      vld1.32         {d4[1]},  [r1], r2
        vmull.u8        q8,  d4,  d0
        vld1.32         {d4[0]},  [r1], r2
        vmull.u8        q9,  d4,  d1
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        pld             [r1]
  .ifc \codec,h264
        vrshrn.u16      d16, q8,  #6
  .else
        vadd.u16        q8,  q8,  q11
        vshrn.u16       d16, q8,  #6
  .endif
  .ifc \type,avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
  .endif
        subs            r3,  r3,  #2
        pld             [r1, r2]
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             3b

        pop             {r4-r7, pc}

4:      vld1.8          {d4},     [r1], r2
        vld1.8          {d6},     [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d4,  d5
        vtrn.32         d6,  d7
        vmull.u8        q8,  d4,  d0
        vmull.u8        q9,  d6,  d0
        subs            r3,  r3,  #2
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        pld             [r1]
  .ifc \codec,h264
        vrshrn.u16      d16, q8,  #6
  .else
        vadd.u16        q8,  q8,  q11
        vshrn.u16       d16, q8,  #6
  .endif
  .ifc \type,avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
  .endif
        pld             [r1]
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             4b

        pop             {r4-r7, pc}
endfunc
.endm

.macro  h264_chroma_mc2 type
function ff_\type\()_h264_chroma_mc2_neon, export=1
        push            {r4-r6, lr}
        ldr             r4,  [sp, #16]
        ldr             lr,  [sp, #20]
        pld             [r1]
        pld             [r1, r2]
        orrs            r5,  r4,  lr
        beq             2f

        mul             r5,  r4,  lr
        rsb             r6,  r5,  lr,  lsl #3
        rsb             r12, r5,  r4,  lsl #3
        sub             r4,  r5,  r4,  lsl #3
        sub             r4,  r4,  lr,  lsl #3
        add             r4,  r4,  #64
        vdup.8          d0,  r4
        vdup.8          d2,  r12
        vdup.8          d1,  r6
        vdup.8          d3,  r5
        vtrn.16         q0,  q1
1:
        vld1.32         {d4[0]},  [r1], r2
        vld1.32         {d4[1]},  [r1], r2
        vrev64.32       d5,  d4
        vld1.32         {d5[1]},  [r1]
        vext.8          q3,  q2,  q2,  #1
        vtrn.16         q2,  q3
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
  .ifc \type,avg
        vld1.16         {d18[0]}, [r0,:16], r2
        vld1.16         {d18[1]}, [r0,:16]
        sub             r0,  r0,  r2
  .endif
        vtrn.32         d16, d17
        vadd.i16        d16, d16, d17
        vrshrn.u16      d16, q8,  #6
  .ifc \type,avg
        vrhadd.u8       d16, d16, d18
  .endif
        vst1.16         {d16[0]}, [r0,:16], r2
        vst1.16         {d16[1]}, [r0,:16], r2
        subs            r3,  r3,  #2
        bgt             1b
        pop             {r4-r6, pc}
2:
  .ifc \type,put
        ldrh_post       r5,  r1,  r2
        strh_post       r5,  r0,  r2
        ldrh_post       r6,  r1,  r2
        strh_post       r6,  r0,  r2
  .else
        vld1.16         {d16[0]}, [r1], r2
        vld1.16         {d16[1]}, [r1], r2
        vld1.16         {d18[0]}, [r0,:16], r2
        vld1.16         {d18[1]}, [r0,:16]
        sub             r0,  r0,  r2
        vrhadd.u8       d16, d16, d18
        vst1.16         {d16[0]}, [r0,:16], r2
        vst1.16         {d16[1]}, [r0,:16], r2
  .endif
        subs            r3,  r3,  #2
        bgt             2b
        pop             {r4-r6, pc}
endfunc
.endm

        h264_chroma_mc8 put
        h264_chroma_mc8 avg
        h264_chroma_mc4 put
        h264_chroma_mc4 avg
        h264_chroma_mc2 put
        h264_chroma_mc2 avg

#if CONFIG_RV40_DECODER
const   rv40bias
        .short           0, 16, 32, 16
        .short          32, 28, 32, 28
        .short           0, 32, 16, 32
        .short          32, 28, 32, 28
endconst

        h264_chroma_mc8 put, rv40
        h264_chroma_mc8 avg, rv40
        h264_chroma_mc4 put, rv40
        h264_chroma_mc4 avg, rv40
#endif
