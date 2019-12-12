/*
 * Copyright (c) 2016 Google Inc.
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
#include "neon.S"

@ Do an 8x8 transpose, using q registers for the subtransposes that don't
@ need to address the indiviudal d registers.
@ r0,r1 == rq0, r2,r3 == rq1, etc
.macro transpose_q_8x8 rq0, rq1, rq2, rq3, r0, r1, r2, r3, r4, r5, r6, r7
        vtrn.32         \rq0, \rq2
        vtrn.32         \rq1, \rq3
        vtrn.16         \rq0, \rq1
        vtrn.16         \rq2, \rq3
        vtrn.8          \r0,  \r1
        vtrn.8          \r2,  \r3
        vtrn.8          \r4,  \r5
        vtrn.8          \r6,  \r7
.endm

@ Do a 4x4 transpose, using q registers for the subtransposes that don't
@ need to address the indiviudal d registers.
@ r0,r1 == rq0, r2,r3 == rq1
.macro transpose_q_4x4 rq0, rq1, r0, r1, r2, r3
        vtrn.16         \rq0, \rq1
        vtrn.8          \r0,  \r1
        vtrn.8          \r2,  \r3
.endm

@ The input to and output from this macro is in the registers q8-q15,
@ and q0-q7 are used as scratch registers.
@ p3 = q8, p0 = q11, q0 = q12, q3 = q15
.macro loop_filter_q
        vdup.u8         d0,  r2          @ E
        lsr             r2,  r2,  #8
        vdup.u8         d2,  r3          @ I
        lsr             r3,  r3,  #8
        vdup.u8         d1,  r2          @ E
        vdup.u8         d3,  r3          @ I

        vabd.u8         q2,  q8,  q9     @ abs(p3 - p2)
        vabd.u8         q3,  q9,  q10    @ abs(p2 - p1)
        vabd.u8         q4,  q10, q11    @ abs(p1 - p0)
        vabd.u8         q5,  q12, q13    @ abs(q0 - q1)
        vabd.u8         q6,  q13, q14    @ abs(q1 - q2)
        vabd.u8         q7,  q14, q15    @ abs(q2 - q3)
        vmax.u8         q2,  q2,  q3
        vmax.u8         q3,  q4,  q5
        vmax.u8         q4,  q6,  q7
        vabd.u8         q5,  q11, q12    @ abs(p0 - q0)
        vmax.u8         q2,  q2,  q3
        vqadd.u8        q5,  q5,  q5     @ abs(p0 - q0) * 2
        vabd.u8         q7,  q10, q13    @ abs(p1 - q1)
        vmax.u8         q2,  q2,  q4     @ max(abs(p3 - p2), ..., abs(q2 - q3))
        vshr.u8         q7,  q7,  #1
        vcle.u8         q2,  q2,  q1     @ max(abs()) <= I
        vqadd.u8        q5,  q5,  q7     @ abs(p0 - q0) * 2 + abs(p1 - q1) >> 1
        vcle.u8         q5,  q5,  q0
        vand            q2,  q2,  q5     @ fm

        vshrn.u16       d10, q2,  #4
        vmov            r2,  r3,  d10
        orrs            r2,  r2,  r3
        @ If no pixels need filtering, just exit as soon as possible
        beq             9f

        @ Calculate the normal inner loop filter for 2 or 4 pixels
        ldr             r3,  [sp, #64]
        vabd.u8         q3,  q10, q11    @ abs(p1 - p0)
        vabd.u8         q4,  q13, q12    @ abs(q1 - q0)

        vsubl.u8        q5,  d20, d26    @ p1 - q1
        vsubl.u8        q6,  d21, d27    @ p1 - q1
        vmax.u8         q3,  q3,  q4     @ max(abs(p1 - p0), abs(q1 - q0))
        vqmovn.s16      d10, q5          @ av_clip_int8p(p1 - q1)
        vqmovn.s16      d11, q6          @ av_clip_int8p(p1 - q1)
        vdup.u8         d8,  r3          @ H
        lsr             r3,  r3,  #8
        vdup.u8         d9,  r3          @ H
        vsubl.u8        q6,  d24, d22    @ q0 - p0
        vsubl.u8        q7,  d25, d23    @ q0 - p0
        vcle.u8         q3,  q3,  q4     @ hev
        vmov.s16        q0,  #3
        vand            q3,  q3,  q2     @ !hev && fm && !flat8in

        vmul.s16        q6,  q6,  q0     @ 3 * (q0 - p0)
        vmul.s16        q7,  q7,  q0     @ 3 * (q0 - p0)
        vbic            q5,  q5,  q3     @ if (!hev) av_clip_int8 = 0
        vaddw.s8        q6,  q6,  d10    @ 3 * (q0 - p0) [+ av_clip_int8(p1 - q1)]
        vaddw.s8        q7,  q7,  d11    @ 3 * (q0 - p0) [+ av_clip_int8(p1 - q1)]
        vmov.s8         q5,  #4
        vqmovn.s16      d12, q6
        vqmovn.s16      d13, q7          @ av_clip_int8(3 * (q0 - p0) [+ av_clip_int8(p1 - q1)], BIT_DEPTH - 1) = f
        vmov.s8         q0,  #3

        vqadd.s8        q5,  q6,  q5     @ FFMIN(f + 4, 127)
        vqadd.s8        q0,  q6,  q0     @ FFMIN(f + 3, 127)
        vmovl.u8        q6,  d22         @ p0
        vmovl.u8        q7,  d23         @ p0
        vshr.s8         q5,  q5,  #3     @ f1
        vshr.s8         q0,  q0,  #3     @ f2

        vaddw.s8        q6,  q6,  d0     @ p0 + f2
        vaddw.s8        q7,  q7,  d1     @ p0 + f2
        vqmovun.s16     d0,  q6          @ out p0
        vmovl.u8        q6,  d24         @ q0
        vqmovun.s16     d1,  q7          @ out p0
        vmovl.u8        q7,  d25         @ q0
        vsubw.s8        q6,  q6,  d10    @ q0 - f1
        vsubw.s8        q7,  q7,  d11    @ q0 - f1
        vqmovun.s16     d12, q6          @ out q0
        vqmovun.s16     d13, q7          @ out q0
        vrshr.s8        q5,  q5,  #1     @ f = (f1 + 1) >> 1
        vbit            q11, q0,  q2     @ if (fm && !flat8in)
        vbit            q12, q6,  q2

        vmovl.u8        q0,  d20         @ p1
        vmovl.u8        q2,  d21         @ p1
        vmovl.u8        q6,  d26         @ q1
        vmovl.u8        q7,  d27         @ q1
        vaddw.s8        q0,  q0,  d10    @ p1 + f
        vaddw.s8        q2,  q2,  d11    @ p1 + f
        vsubw.s8        q6,  q6,  d10    @ q1 - f
        vsubw.s8        q7,  q7,  d11    @ q1 - f
        vqmovun.s16     d0,  q0          @ out p1
        vqmovun.s16     d1,  q2          @ out p1
        vqmovun.s16     d12, q6          @ out q1
        vqmovun.s16     d13, q7          @ out q1
        vbit            q10, q0,  q3     @ if (!hev && fm && !flat8in)
        vbit            q13, q6,  q3
.endm

@ The input to and output from this macro is in the registers d16-d31,
@ and d0-d7 are used as scratch registers.
@ p7 = d16 .. p3 = d20, p0 = d23, q0 = d24, q3 = d27, q7 = d31
@ Depending on the width of the loop filter, we either use d16-d19
@ and d28-d31 as temp registers, or d8-d15.
@ tmp1,tmp2 = tmpq1, tmp3,tmp4 = tmpq2, tmp5,tmp6 = tmpq3, tmp7,tmp8 = tmpq4
.macro loop_filter wd, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmpq1, tmpq2, tmpq3, tmpq4
        vdup.u8         d0,  r2 @ E
        vdup.u8         d2,  r3 @ I
        ldr             r3,  [sp]

        vabd.u8         d4,  d20, d21    @ abs(p3 - p2)
        vabd.u8         d5,  d21, d22    @ abs(p2 - p1)
        vabd.u8         d6,  d22, d23    @ abs(p1 - p0)
        vabd.u8         d7,  d24, d25    @ abs(q0 - q1)
        vabd.u8         \tmp1,  d25, d26 @ abs(q1 - q2)
        vabd.u8         \tmp2,  d26, d27 @ abs(q2 - q3)
        vmax.u8         d4,  d4,  d5
        vmax.u8         d5,  d6,  d7
        vmax.u8         \tmp1,  \tmp1,  \tmp2
        vabd.u8         d6,  d23, d24    @ abs(p0 - q0)
        vmax.u8         d4,  d4,  d5
        vqadd.u8        d6,  d6,  d6     @ abs(p0 - q0) * 2
        vabd.u8         d5,  d22, d25    @ abs(p1 - q1)
        vmax.u8         d4,  d4,  \tmp1  @ max(abs(p3 - p2), ..., abs(q2 - q3))
        vshr.u8         d5,  d5,  #1
        vcle.u8         d4,  d4,  d2     @ max(abs()) <= I
        vqadd.u8        d6,  d6,  d5     @ abs(p0 - q0) * 2 + abs(p1 - q1) >> 1
        vcle.u8         d5,  d6,  d0
        vand            d4,  d4,  d5     @ fm

        vdup.u8         d3,  r3          @ H
        vmov            r2,  r3,  d4
        orrs            r2,  r2,  r3
        @ If no pixels need filtering, just exit as soon as possible
        beq             9f

.if \wd >= 8
        vmov.u8         d0,  #1

        vabd.u8         d6,  d20, d23    @ abs(p3 - p0)
        vabd.u8         d2,  d21, d23    @ abs(p2 - p0)
        vabd.u8         d1,  d22, d23    @ abs(p1 - p0)
        vabd.u8         \tmp1,  d25, d24 @ abs(q1 - q0)
        vabd.u8         \tmp2,  d26, d24 @ abs(q2 - q0)
        vabd.u8         \tmp3,  d27, d24 @ abs(q3 - q0)
        vmax.u8         d6,  d6,  d2
        vmax.u8         d1,  d1,  \tmp1
        vmax.u8         \tmp2,  \tmp2,  \tmp3
.if \wd == 16
        vabd.u8         d7,  d16, d23    @ abs(p7 - p0)
        vmax.u8         d6,  d6,  d1
        vabd.u8         d2,  d17, d23    @ abs(p6 - p0)
        vmax.u8         d6,  d6,  \tmp2
        vabd.u8         d1,  d18, d23    @ abs(p5 - p0)
        vcle.u8         d6,  d6,  d0     @ flat8in
        vabd.u8         d8,  d19, d23    @ abs(p4 - p0)
        vand            d6,  d6,  d4     @ flat8in && fm
        vabd.u8         d9,  d28, d24    @ abs(q4 - q0)
        vbic            d4,  d4,  d6     @ fm && !flat8in
        vabd.u8         d10, d29, d24    @ abs(q5 - q0)
        vabd.u8         d11, d30, d24    @ abs(q6 - q0)
        vabd.u8         d12, d31, d24    @ abs(q7 - q0)

        vmax.u8         d7,  d7,  d2
        vmax.u8         d1,  d1,  d8
        vmax.u8         d9,  d9,  d10
        vmax.u8         d11, d11, d12
        @ The rest of the calculation of flat8out is interleaved below
.else
        @ The rest of the calculation of flat8in is interleaved below
.endif
.endif

        @ Calculate the normal inner loop filter for 2 or 4 pixels
        vabd.u8         d5,  d22, d23           @ abs(p1 - p0)
.if \wd == 16
        vmax.u8         d7,  d7,  d1
        vmax.u8         d9,  d9,  d11
.elseif \wd == 8
        vmax.u8         d6,  d6,  d1
.endif
        vabd.u8         d1,  d25, d24           @ abs(q1 - q0)
.if \wd == 16
        vmax.u8         d7,  d7,  d9
.elseif \wd == 8
        vmax.u8         d6,  d6,  \tmp2
.endif
        vsubl.u8        \tmpq1,  d22, d25       @ p1 - q1
        vmax.u8         d5,  d5,  d1            @ max(abs(p1 - p0), abs(q1 - q0))
        vsubl.u8        \tmpq2,  d24, d23       @ q0 - p0
        vmov.s16        \tmpq3,  #3
.if \wd == 8
        vcle.u8         d6,  d6,  d0            @ flat8in
.endif
        vcle.u8         d5,  d5,  d3            @ !hev
.if \wd == 8
        vand            d6,  d6,  d4            @ flat8in && fm
.endif
        vqmovn.s16      \tmp1,   \tmpq1         @ av_clip_int8(p1 - q1)
.if \wd == 16
        vcle.u8         d7,  d7,  d0            @ flat8out
.elseif \wd == 8
        vbic            d4,  d4,  d6            @ fm && !flat8in
.endif
        vand            d5,  d5,  d4            @ !hev && fm && !flat8in
.if \wd == 16
        vand            d7,  d7,  d6            @ flat8out && flat8in && fm
.endif

        vmul.s16        \tmpq2,  \tmpq2, \tmpq3 @ 3 * (q0 - p0)
        vbic            \tmp1,   \tmp1,   d5    @ if (!hev) av_clip_int8 = 0
        vmov.s8         d2,  #4
        vaddw.s8        \tmpq2,  \tmpq2,  \tmp1 @ 3 * (q0 - p0) [+ av_clip_int8(p1 - q1)]
        vmov.s8         d3,  #3
        vqmovn.s16      \tmp1,   \tmpq2         @ f
.if \wd == 16
        vbic            d6,  d6,  d7            @ fm && flat8in && !flat8out
.endif

        vqadd.s8        \tmp3, \tmp1,  d2       @ FFMIN(f + 4, 127)
        vqadd.s8        \tmp4, \tmp1,  d3       @ FFMIN(f + 3, 127)
        vmovl.u8        q0,  d23                @ p0
        vshr.s8         \tmp3, \tmp3,  #3       @ f1
        vshr.s8         \tmp4, \tmp4,  #3       @ f2

        vmovl.u8        q1,  d24                @ q0
        vaddw.s8        q0,  q0,  \tmp4         @ p0 + f2
        vsubw.s8        q1,  q1,  \tmp3         @ q0 - f1
        vqmovun.s16     d0,  q0                 @ out p0
        vqmovun.s16     d1,  q1                 @ out q0
        vrshr.s8        \tmp3, \tmp3, #1        @ f = (f1 + 1) >> 1
        vbit            d23, d0,  d4            @ if (fm && !flat8in)
        vbit            d24, d1,  d4

        vmovl.u8        q0,  d22                @ p1
        vmovl.u8        q1,  d25                @ q1
.if \wd >= 8
        vmov            r2,  r3,  d6
.endif
        vaddw.s8        q0,  q0,  \tmp3         @ p1 + f
        vsubw.s8        q1,  q1,  \tmp3         @ q1 - f
.if \wd >= 8
        orrs            r2,  r2,  r3
.endif
        vqmovun.s16     d0,  q0                 @ out p1
        vqmovun.s16     d2,  q1                 @ out q1
        vbit            d22, d0,  d5            @ if (!hev && fm && !flat8in)
        vbit            d25, d2,  d5

.if \wd >= 8
        @ If no pixels need flat8in, jump to flat8out
        @ (or to a writeout of the inner 4 pixels, for wd=8)
        beq             6f

        @ flat8in
        vaddl.u8        \tmpq1, d20, d21
        vaddl.u8        \tmpq2, d22, d25
        vaddl.u8        \tmpq3, d20, d22
        vaddl.u8        \tmpq4, d23, d26
        vadd.u16        q0,  \tmpq1, \tmpq1
        vaddw.u8        q0,  q0,  d23
        vaddw.u8        q0,  q0,  d24
        vadd.u16        q0,  q0,  \tmpq3
        vsub.s16        \tmpq2, \tmpq2, \tmpq1
        vsub.s16        \tmpq4, \tmpq4, \tmpq3
        vrshrn.u16      d2,  q0,  #3            @ out p2

        vadd.u16        q0,  q0,  \tmpq2
        vaddl.u8        \tmpq1, d20, d23
        vaddl.u8        \tmpq2, d24, d27
        vrshrn.u16      d3,  q0,  #3            @ out p1

        vadd.u16        q0,  q0,  \tmpq4
        vsub.s16        \tmpq2, \tmpq2, \tmpq1
        vaddl.u8        \tmpq3, d21, d24
        vaddl.u8        \tmpq4, d25, d27
        vrshrn.u16      d4,  q0,  #3            @ out p0

        vadd.u16        q0,  q0,  \tmpq2
        vsub.s16        \tmpq4, \tmpq4, \tmpq3
        vaddl.u8        \tmpq1, d22, d25
        vaddl.u8        \tmpq2, d26, d27
        vrshrn.u16      d5,  q0,  #3            @ out q0

        vadd.u16        q0,  q0,  \tmpq4
        vsub.s16        \tmpq2, \tmpq2, \tmpq1
        vrshrn.u16      \tmp5,  q0,  #3         @ out q1

        vadd.u16        q0,  q0,  \tmpq2
        @ The output here is written back into the input registers. This doesn't
        @ matter for the flat8out part below, since we only update those pixels
        @ which won't be touched below.
        vbit            d21, d2,  d6
        vbit            d22, d3,  d6
        vbit            d23, d4,  d6
        vrshrn.u16      \tmp6,  q0,  #3         @ out q2
        vbit            d24, d5,  d6
        vbit            d25, \tmp5,  d6
        vbit            d26, \tmp6,  d6
.endif
.if \wd == 16
6:
        vorr            d2,  d6,  d7
        vmov            r2,  r3,  d2
        orrs            r2,  r2,  r3
        @ If no pixels needed flat8in nor flat8out, jump to a
        @ writeout of the inner 4 pixels
        beq             7f
        vmov            r2,  r3,  d7
        orrs            r2,  r2,  r3
        @ If no pixels need flat8out, jump to a writeout of the inner 6 pixels
        beq             8f

        @ flat8out
        @ This writes all outputs into d2-d17 (skipping d6 and d16).
        @ If this part is skipped, the output is read from d21-d26 (which is the input
        @ to this section).
        vshll.u8        q0,  d16, #3  @ 8 * d16
        vsubw.u8        q0,  q0,  d16 @ 7 * d16
        vaddw.u8        q0,  q0,  d17
        vaddl.u8        q4,  d17, d18
        vaddl.u8        q5,  d19, d20
        vadd.s16        q0,  q0,  q4
        vaddl.u8        q4,  d16, d17
        vaddl.u8        q6,  d21, d22
        vadd.s16        q0,  q0,  q5
        vaddl.u8        q5,  d18, d25
        vaddl.u8        q7,  d23, d24
        vsub.s16        q5,  q5,  q4
        vadd.s16        q0,  q0,  q6
        vadd.s16        q0,  q0,  q7
        vaddl.u8        q6,  d16, d18
        vaddl.u8        q7,  d19, d26
        vrshrn.u16      d2,  q0,  #4

        vadd.s16        q0,  q0,  q5
        vaddl.u8        q4,  d16, d19
        vaddl.u8        q5,  d20, d27
        vsub.s16        q7,  q7,  q6
        vbif            d2,  d17, d7
        vrshrn.u16      d3,  q0,  #4

        vadd.s16        q0,  q0,  q7
        vaddl.u8        q6,  d16, d20
        vaddl.u8        q7,  d21, d28
        vsub.s16        q5,  q5,  q4
        vbif            d3,  d18, d7
        vrshrn.u16      d4,  q0,  #4

        vadd.s16        q0,  q0,  q5
        vaddl.u8        q4,  d16, d21
        vaddl.u8        q5,  d22, d29
        vsub.s16        q7,  q7,  q6
        vbif            d4,  d19, d7
        vrshrn.u16      d5,  q0,  #4

        vadd.s16        q0,  q0,  q7
        vaddl.u8        q6,  d16, d22
        vaddl.u8        q7,  d23, d30
        vsub.s16        q5,  q5,  q4
        vbif            d5,  d20, d7
        vrshrn.u16      d6,  q0,  #4

        vadd.s16        q0,  q0,  q5
        vaddl.u8        q5,  d16, d23
        vsub.s16        q7,  q7,  q6
        vaddl.u8        q6,  d24, d31
        vbif            d6,  d21, d7
        vrshrn.u16      d8,  q0,  #4

        vadd.s16        q0,  q0,  q7
        vsub.s16        q5,  q6,  q5
        vaddl.u8        q6,  d17, d24
        vaddl.u8        q7,  d25, d31
        vbif            d8,  d22, d7
        vrshrn.u16      d9,  q0,  #4

        vadd.s16        q0,  q0,  q5
        vsub.s16        q7,  q7,  q6
        vaddl.u8        q6,  d26, d31
        vbif            d9,  d23, d7
        vrshrn.u16      d10, q0,  #4

        vadd.s16        q0,  q0,  q7
        vaddl.u8        q7,  d18, d25
        vaddl.u8        q9,  d19, d26
        vsub.s16        q6,  q6,  q7
        vaddl.u8        q7,  d27, d31
        vbif            d10, d24, d7
        vrshrn.u16      d11, q0,  #4

        vadd.s16        q0,  q0,  q6
        vaddl.u8        q6,  d20, d27
        vsub.s16        q7,  q7,  q9
        vaddl.u8        q9,  d28, d31
        vbif            d11, d25, d7
        vsub.s16        q9,  q9,  q6
        vrshrn.u16      d12, q0,  #4

        vadd.s16        q0,  q0,  q7
        vaddl.u8        q7,  d21, d28
        vaddl.u8        q10, d29, d31
        vbif            d12, d26, d7
        vrshrn.u16      d13, q0,  #4

        vadd.s16        q0,  q0,  q9
        vsub.s16        q10, q10, q7
        vaddl.u8        q9,  d22, d29
        vaddl.u8        q11, d30, d31
        vbif            d13, d27, d7
        vrshrn.u16      d14, q0,  #4

        vadd.s16        q0,  q0,  q10
        vsub.s16        q11, q11, q9
        vbif            d14, d28, d7
        vrshrn.u16      d15, q0,  #4

        vadd.s16        q0,  q0,  q11
        vbif            d15, d29, d7
        vrshrn.u16      d17, q0,  #4
        vbif            d17, d30, d7
.endif
.endm

@ For wd <= 8, we use d16-d19 and d28-d31 for temp registers,
@ while we need those for inputs/outputs in wd=16 and use d8-d15
@ for temp registers there instead.
.macro loop_filter_4
        loop_filter     4,  d16, d17, d18, d19, d28, d29, d30, d31, q8,  q9,  q14, q15
.endm

.macro loop_filter_8
        loop_filter     8,  d16, d17, d18, d19, d28, d29, d30, d31, q8,  q9,  q14, q15
.endm

.macro loop_filter_16
        loop_filter     16, d8,  d9,  d10, d11, d12, d13, d14, d15, q4,  q5,  q6,  q7
.endm


@ The public functions in this file have got the following signature:
@ void loop_filter(uint8_t *dst, ptrdiff_t stride, int mb_lim, int lim, int hev_thr);

function ff_vp9_loop_filter_v_4_8_neon, export=1
        sub             r12, r0,  r1, lsl #2
        vld1.8          {d20}, [r12,:64], r1 @ p3
        vld1.8          {d24}, [r0, :64], r1 @ q0
        vld1.8          {d21}, [r12,:64], r1 @ p2
        vld1.8          {d25}, [r0, :64], r1 @ q1
        vld1.8          {d22}, [r12,:64], r1 @ p1
        vld1.8          {d26}, [r0, :64], r1 @ q2
        vld1.8          {d23}, [r12,:64], r1 @ p0
        vld1.8          {d27}, [r0, :64], r1 @ q3
        sub             r0,  r0,  r1, lsl #2
        sub             r12, r12, r1, lsl #1

        loop_filter_4

        vst1.8          {d22}, [r12,:64], r1
        vst1.8          {d24}, [r0, :64], r1
        vst1.8          {d23}, [r12,:64], r1
        vst1.8          {d25}, [r0, :64], r1
9:
        bx              lr
endfunc

function ff_vp9_loop_filter_h_4_8_neon, export=1
        sub             r12, r0,  #4
        add             r0,  r12, r1, lsl #2
        vld1.8          {d20}, [r12], r1
        vld1.8          {d24}, [r0],  r1
        vld1.8          {d21}, [r12], r1
        vld1.8          {d25}, [r0],  r1
        vld1.8          {d22}, [r12], r1
        vld1.8          {d26}, [r0],  r1
        vld1.8          {d23}, [r12], r1
        vld1.8          {d27}, [r0],  r1

        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2
        @ Move r0/r12 forward by 2 pixels; we don't need to rewrite the
        @ outermost 2 pixels since they aren't changed.
        add             r12, r12, #2
        add             r0,  r0,  #2

        @ Transpose the 8x8 pixels, taking advantage of q registers, to get
        @ one register per column.
        transpose_q_8x8 q10, q11, q12, q13, d20, d21, d22, d23, d24, d25, d26, d27

        loop_filter_4

        @ We only will write the mid 4 pixels back; after the loop filter,
        @ these are in d22, d23, d24, d25 (q11, q12), ordered as rows
        @ (8x4 pixels). We need to transpose them to columns, done with a
        @ 4x4 transpose (which in practice is two 4x4 transposes of the two
        @ 4x4 halves of the 8x4 pixels; into 4x8 pixels).
        transpose_q_4x4 q11, q12, d22, d23, d24, d25

        vst1.32         {d22[0]}, [r12], r1
        vst1.32         {d22[1]}, [r0],  r1
        vst1.32         {d23[0]}, [r12], r1
        vst1.32         {d23[1]}, [r0],  r1
        vst1.32         {d24[0]}, [r12], r1
        vst1.32         {d24[1]}, [r0],  r1
        vst1.32         {d25[0]}, [r12], r1
        vst1.32         {d25[1]}, [r0],  r1
9:
        bx              lr
endfunc

function ff_vp9_loop_filter_v_44_16_neon, export=1
        vpush           {q4-q7}
        sub             r12, r0,  r1, lsl #2
        vld1.8          {q8},  [r12,:128], r1 @ p3
        vld1.8          {q12}, [r0, :128], r1 @ q0
        vld1.8          {q9},  [r12,:128], r1 @ p2
        vld1.8          {q13}, [r0, :128], r1 @ q1
        vld1.8          {q10}, [r12,:128], r1 @ p1
        vld1.8          {q14}, [r0, :128], r1 @ q2
        vld1.8          {q11}, [r12,:128], r1 @ p0
        vld1.8          {q15}, [r0, :128], r1 @ q3
        sub             r0,  r0,  r1, lsl #2
        sub             r12, r12, r1, lsl #1

        loop_filter_q

        vst1.8          {q10}, [r12,:128], r1
        vst1.8          {q12}, [r0, :128], r1
        vst1.8          {q11}, [r12,:128], r1
        vst1.8          {q13}, [r0, :128], r1
9:
        vpop            {q4-q7}
        bx              lr
endfunc

function ff_vp9_loop_filter_h_44_16_neon, export=1
        vpush           {q4-q7}
        sub             r12, r0,  #4
        add             r0,  r12, r1, lsl #2
        vld1.8          {d16}, [r12], r1
        vld1.8          {d24}, [r0],  r1
        vld1.8          {d18}, [r12], r1
        vld1.8          {d26}, [r0],  r1
        vld1.8          {d20}, [r12], r1
        vld1.8          {d28}, [r0],  r1
        vld1.8          {d22}, [r12], r1
        vld1.8          {d30}, [r0],  r1
        mov             r12, r0
        add             r0,  r0,  r1, lsl #2
        vld1.8          {d17}, [r12], r1
        vld1.8          {d25}, [r0],  r1
        vld1.8          {d19}, [r12], r1
        vld1.8          {d27}, [r0],  r1
        vld1.8          {d21}, [r12], r1
        vld1.8          {d29}, [r0],  r1
        vld1.8          {d23}, [r12], r1
        vld1.8          {d31}, [r0],  r1

        @ Transpose the 16x8 pixels, as two 8x8 parts
        transpose_8x8   q8,  q9,  q10, q11, q12, q13, q14, q15

        loop_filter_q

        sub             r12, r0,  r1, lsl #4
        add             r0,  r12, r1, lsl #3
        @ Move r0/r12 forward by 2 pixels; we don't need to rewrite the
        @ outermost 2 pixels since they aren't changed.
        add             r12, r12, #2
        add             r0,  r0,  #2

        @ We only will write the mid 4 pixels back; after the loop filter,
        @ these are in q10, q11, q12, q13, ordered as rows (16x4 pixels).
        @ We need to transpose them to columns, done with a 4x4 transpose
        @ (which in practice is four 4x4 transposes of the 4x4 blocks of
        @ the 16x4 pixels; into 4x16 pixels).
        transpose_4x4   q10, q11, q12, q13

        vst1.32         {d20[0]}, [r12], r1
        vst1.32         {d21[0]}, [r0],  r1
        vst1.32         {d22[0]}, [r12], r1
        vst1.32         {d23[0]}, [r0],  r1
        vst1.32         {d24[0]}, [r12], r1
        vst1.32         {d25[0]}, [r0],  r1
        vst1.32         {d26[0]}, [r12], r1
        vst1.32         {d27[0]}, [r0],  r1
        vst1.32         {d20[1]}, [r12], r1
        vst1.32         {d21[1]}, [r0],  r1
        vst1.32         {d22[1]}, [r12], r1
        vst1.32         {d23[1]}, [r0],  r1
        vst1.32         {d24[1]}, [r12], r1
        vst1.32         {d25[1]}, [r0],  r1
        vst1.32         {d26[1]}, [r12], r1
        vst1.32         {d27[1]}, [r0],  r1
9:
        vpop            {q4-q7}
        bx              lr
endfunc

function ff_vp9_loop_filter_v_8_8_neon, export=1
        sub             r12, r0,  r1, lsl #2
        vld1.8          {d20}, [r12,:64], r1 @ p3
        vld1.8          {d24}, [r0, :64], r1 @ q0
        vld1.8          {d21}, [r12,:64], r1 @ p2
        vld1.8          {d25}, [r0, :64], r1 @ q1
        vld1.8          {d22}, [r12,:64], r1 @ p1
        vld1.8          {d26}, [r0, :64], r1 @ q2
        vld1.8          {d23}, [r12,:64], r1 @ p0
        vld1.8          {d27}, [r0, :64], r1 @ q3
        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2
        add             r12, r12, r1

        loop_filter_8

        vst1.8          {d21}, [r12,:64], r1
        vst1.8          {d24}, [r0, :64], r1
        vst1.8          {d22}, [r12,:64], r1
        vst1.8          {d25}, [r0, :64], r1
        vst1.8          {d23}, [r12,:64], r1
        vst1.8          {d26}, [r0, :64], r1
9:
        bx              lr
6:
        sub             r12, r0,  r1, lsl #1
        vst1.8          {d22}, [r12,:64], r1
        vst1.8          {d24}, [r0, :64], r1
        vst1.8          {d23}, [r12,:64], r1
        vst1.8          {d25}, [r0, :64], r1
        bx              lr
endfunc

function ff_vp9_loop_filter_h_8_8_neon, export=1
        sub             r12, r0,  #4
        add             r0,  r12, r1, lsl #2
        vld1.8          {d20}, [r12], r1
        vld1.8          {d24}, [r0],  r1
        vld1.8          {d21}, [r12], r1
        vld1.8          {d25}, [r0],  r1
        vld1.8          {d22}, [r12], r1
        vld1.8          {d26}, [r0],  r1
        vld1.8          {d23}, [r12], r1
        vld1.8          {d27}, [r0],  r1

        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2

        transpose_q_8x8 q10, q11, q12, q13, d20, d21, d22, d23, d24, d25, d26, d27

        loop_filter_8

        @ Even though only 6 pixels per row have been changed, we write the
        @ full 8 pixel registers.
        transpose_q_8x8 q10, q11, q12, q13, d20, d21, d22, d23, d24, d25, d26, d27

        vst1.8          {d20}, [r12], r1
        vst1.8          {d24}, [r0],  r1
        vst1.8          {d21}, [r12], r1
        vst1.8          {d25}, [r0],  r1
        vst1.8          {d22}, [r12], r1
        vst1.8          {d26}, [r0],  r1
        vst1.8          {d23}, [r12], r1
        vst1.8          {d27}, [r0],  r1
9:
        bx              lr
6:
        @ If we didn't need to do the flat8in part, we use the same writeback
        @ as in loop_filter_h_4_8.
        add             r12, r12, #2
        add             r0,  r0,  #2
        transpose_q_4x4 q11, q12, d22, d23, d24, d25
        vst1.32         {d22[0]}, [r12], r1
        vst1.32         {d22[1]}, [r0],  r1
        vst1.32         {d23[0]}, [r12], r1
        vst1.32         {d23[1]}, [r0],  r1
        vst1.32         {d24[0]}, [r12], r1
        vst1.32         {d24[1]}, [r0],  r1
        vst1.32         {d25[0]}, [r12], r1
        vst1.32         {d25[1]}, [r0],  r1
        bx              lr
endfunc

function vp9_loop_filter_v_16_neon
        sub             r12, r0,  r1, lsl #3
        @ Read p7-p0 using r12 and q0-q7 using r0
        vld1.8          {d16}, [r12,:64], r1 @ p7
        vld1.8          {d24}, [r0, :64], r1 @ q0
        vld1.8          {d17}, [r12,:64], r1 @ p6
        vld1.8          {d25}, [r0, :64], r1 @ q1
        vld1.8          {d18}, [r12,:64], r1 @ p5
        vld1.8          {d26}, [r0, :64], r1 @ q2
        vld1.8          {d19}, [r12,:64], r1 @ p4
        vld1.8          {d27}, [r0, :64], r1 @ q3
        vld1.8          {d20}, [r12,:64], r1 @ p3
        vld1.8          {d28}, [r0, :64], r1 @ q4
        vld1.8          {d21}, [r12,:64], r1 @ p2
        vld1.8          {d29}, [r0, :64], r1 @ q5
        vld1.8          {d22}, [r12,:64], r1 @ p1
        vld1.8          {d30}, [r0, :64], r1 @ q6
        vld1.8          {d23}, [r12,:64], r1 @ p0
        vld1.8          {d31}, [r0, :64], r1 @ q7
        sub             r12, r12, r1, lsl #3
        sub             r0,  r0,  r1, lsl #3
        add             r12, r12, r1

        loop_filter_16

        @ If we did the flat8out part, we get the output in
        @ d2-d17 (skipping d7 and d16). r12 points to r0 - 7 * stride,
        @ store d2-d9 there, and d10-d17 into r0.
        vst1.8          {d2},  [r12,:64], r1
        vst1.8          {d10}, [r0, :64], r1
        vst1.8          {d3},  [r12,:64], r1
        vst1.8          {d11}, [r0, :64], r1
        vst1.8          {d4},  [r12,:64], r1
        vst1.8          {d12}, [r0, :64], r1
        vst1.8          {d5},  [r12,:64], r1
        vst1.8          {d13}, [r0, :64], r1
        vst1.8          {d6},  [r12,:64], r1
        vst1.8          {d14}, [r0, :64], r1
        vst1.8          {d8},  [r12,:64], r1
        vst1.8          {d15}, [r0, :64], r1
        vst1.8          {d9},  [r12,:64], r1
        vst1.8          {d17}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #3
        add             r0,  r0,  r1

9:
        bx              lr

8:
        add             r12, r12, r1, lsl #2
        @ If we didn't do the flat8out part, the output is left in the
        @ input registers.
        vst1.8          {d21}, [r12,:64], r1
        vst1.8          {d24}, [r0, :64], r1
        vst1.8          {d22}, [r12,:64], r1
        vst1.8          {d25}, [r0, :64], r1
        vst1.8          {d23}, [r12,:64], r1
        vst1.8          {d26}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #1
        sub             r0,  r0,  r1
        bx              lr
7:
        sub             r12, r0,  r1, lsl #1
        vst1.8          {d22}, [r12,:64], r1
        vst1.8          {d24}, [r0, :64], r1
        vst1.8          {d23}, [r12,:64], r1
        vst1.8          {d25}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #1
        bx              lr
endfunc

function ff_vp9_loop_filter_v_16_8_neon, export=1
        ldr             r12, [sp]
        push            {lr}
        vpush           {q4-q7}
        push            {r12}
        bl              vp9_loop_filter_v_16_neon
        add             sp,  sp,  #4
        vpop            {q4-q7}
        pop             {pc}
endfunc

function ff_vp9_loop_filter_v_16_16_neon, export=1
        ldr             r12, [sp]
        // The filter clobbers r2 and r3, but we need to keep them for the second round
        push            {r2, r3, lr}
        vpush           {q4-q7}
        push            {r12}
        bl              vp9_loop_filter_v_16_neon
        add             r0,  #8
        ldr             r2,  [sp, #68]
        ldr             r3,  [sp, #72]
        bl              vp9_loop_filter_v_16_neon
        add             sp,  sp,  #4
        vpop            {q4-q7}
        pop             {r2, r3, pc}
endfunc

function vp9_loop_filter_h_16_neon
        sub             r12, r0,  #8
        vld1.8          {d16}, [r12,:64], r1
        vld1.8          {d24}, [r0, :64], r1
        vld1.8          {d17}, [r12,:64], r1
        vld1.8          {d25}, [r0, :64], r1
        vld1.8          {d18}, [r12,:64], r1
        vld1.8          {d26}, [r0, :64], r1
        vld1.8          {d19}, [r12,:64], r1
        vld1.8          {d27}, [r0, :64], r1
        vld1.8          {d20}, [r12,:64], r1
        vld1.8          {d28}, [r0, :64], r1
        vld1.8          {d21}, [r12,:64], r1
        vld1.8          {d29}, [r0, :64], r1
        vld1.8          {d22}, [r12,:64], r1
        vld1.8          {d30}, [r0, :64], r1
        vld1.8          {d23}, [r12,:64], r1
        vld1.8          {d31}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #3
        sub             r12, r12, r1, lsl #3

        @ The 16x8 pixels read above is in two 8x8 blocks; the left
        @ half in d16-d23, and the right half in d24-d31. Do two 8x8 transposes
        @ of this, to get one column per register. This could be done with two
        @ transpose_8x8 as below, but this takes advantage of the q registers.
        transpose16_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15
        vtrn.8          d16, d17
        vtrn.8          d18, d19
        vtrn.8          d20, d21
        vtrn.8          d22, d23
        vtrn.8          d24, d25
        vtrn.8          d26, d27
        vtrn.8          d28, d29
        vtrn.8          d30, d31

        loop_filter_16

        @ Transpose back; this is the same transpose as above, but
        @ we can't take advantage of q registers for the transpose, since
        @ all d registers in the transpose aren't consecutive.
        transpose_8x8   d16, d2,  d3,  d4,  d5,  d6,  d8,  d9
        transpose_8x8   d10, d11, d12, d13, d14, d15, d17, d31

        vst1.8          {d16}, [r12,:64], r1
        vst1.8          {d10}, [r0, :64], r1

        vst1.8          {d2},  [r12,:64], r1
        vst1.8          {d11}, [r0, :64], r1

        vst1.8          {d3},  [r12,:64], r1
        vst1.8          {d12}, [r0, :64], r1

        vst1.8          {d4},  [r12,:64], r1
        vst1.8          {d13}, [r0, :64], r1

        vst1.8          {d5},  [r12,:64], r1
        vst1.8          {d14}, [r0, :64], r1

        vst1.8          {d6},  [r12,:64], r1
        vst1.8          {d15}, [r0, :64], r1

        vst1.8          {d8},  [r12,:64], r1
        vst1.8          {d17}, [r0, :64], r1

        vst1.8          {d9},  [r12,:64], r1
        vst1.8          {d31}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #3
9:
        bx              lr
8:
        @ The same writeback as in loop_filter_h_8_8
        sub             r12, r0,  #4
        add             r0,  r12, r1, lsl #2
        transpose_q_8x8 q10, q11, q12, q13, d20, d21, d22, d23, d24, d25, d26, d27

        vst1.8          {d20}, [r12], r1
        vst1.8          {d24}, [r0],  r1
        vst1.8          {d21}, [r12], r1
        vst1.8          {d25}, [r0],  r1
        vst1.8          {d22}, [r12], r1
        vst1.8          {d26}, [r0],  r1
        vst1.8          {d23}, [r12], r1
        vst1.8          {d27}, [r0],  r1
        sub             r0,  r0,  r1, lsl #3
        add             r0,  r0,  #4
        bx              lr
7:
        @ The same writeback as in loop_filter_h_4_8
        sub             r12, r0,  #2
        add             r0,  r12, r1, lsl #2
        transpose_q_4x4 q11, q12, d22, d23, d24, d25
        vst1.32         {d22[0]}, [r12], r1
        vst1.32         {d22[1]}, [r0],  r1
        vst1.32         {d23[0]}, [r12], r1
        vst1.32         {d23[1]}, [r0],  r1
        vst1.32         {d24[0]}, [r12], r1
        vst1.32         {d24[1]}, [r0],  r1
        vst1.32         {d25[0]}, [r12], r1
        vst1.32         {d25[1]}, [r0],  r1
        sub             r0,  r0,  r1, lsl #3
        add             r0,  r0,  #2
        bx              lr
endfunc

function ff_vp9_loop_filter_h_16_8_neon, export=1
        ldr             r12, [sp]
        push            {lr}
        vpush           {q4-q7}
        push            {r12}
        bl              vp9_loop_filter_h_16_neon
        add             sp,  sp,  #4
        vpop            {q4-q7}
        pop             {pc}
endfunc

function ff_vp9_loop_filter_h_16_16_neon, export=1
        ldr             r12, [sp]
        // The filter clobbers r2 and r3, but we need to keep them for the second round
        push            {r2, r3, lr}
        vpush           {q4-q7}
        push            {r12}
        bl              vp9_loop_filter_h_16_neon
        add             r0,  r0,  r1, lsl #3
        ldr             r2,  [sp, #68]
        ldr             r3,  [sp, #72]
        bl              vp9_loop_filter_h_16_neon
        add             sp,  sp,  #4
        vpop            {q4-q7}
        pop             {r2, r3, pc}
endfunc
