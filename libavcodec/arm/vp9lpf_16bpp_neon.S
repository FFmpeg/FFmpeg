/*
 * Copyright (c) 2017 Google Inc.
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

.macro transpose16_q_8x8 rq0, rq1, rq2, rq3, rq4, rq5, rq6, rq7, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15
        vswp             \r1,  \r8  @ vtrn.64 \rq0, \rq4
        vswp             \r3,  \r10 @ vtrn.64 \rq1, \rq5
        vswp             \r5,  \r12 @ vtrn.64 \rq2, \rq6
        vswp             \r7,  \r14 @ vtrn.64 \rq3, \rq7
        vtrn.32          \rq0, \rq2
        vtrn.32          \rq1, \rq3
        vtrn.32          \rq4, \rq6
        vtrn.32          \rq5, \rq7
        vtrn.16          \rq0, \rq1
        vtrn.16          \rq2, \rq3
        vtrn.16          \rq4, \rq5
        vtrn.16          \rq6, \rq7
.endm

.macro transpose16_4x4 r0, r1, r2, r3
        vtrn.32          \r0, \r2
        vtrn.32          \r1, \r3
        vtrn.16          \r0, \r1
        vtrn.16          \r2, \r3
.endm

@ Do a 4x4 transpose, using q registers for the subtransposes that don't
@ need to address the indiviudal d registers.
@ r0,r1 == rq0, r2,r3 == rq1
.macro transpose16_q_4x4 rq0, rq1, r0, r1, r2, r3
        vtrn.32         \rq0, \rq1
        vtrn.16         \r0,  \r1
        vtrn.16         \r2,  \r3
.endm

@ The input to and output from this macro is in the registers q8-q15,
@ and q0-q7 are used as scratch registers.
@ p3 = q8, p0 = q11, q0 = q12, q3 = q15
.macro loop_filter_q wd
        vdup.u16        q0,  r2          @ E
        vdup.u16        q1,  r3          @ I

        vabd.u16        q2,  q8,  q9     @ abs(p3 - p2)
        vabd.u16        q3,  q9,  q10    @ abs(p2 - p1)
        vabd.u16        q4,  q10, q11    @ abs(p1 - p0)
        vabd.u16        q5,  q12, q13    @ abs(q0 - q1)
        vabd.u16        q6,  q13, q14    @ abs(q1 - q2)
        vabd.u16        q7,  q14, q15    @ abs(q2 - q3)
        vmax.u16        q2,  q2,  q3
        vmax.u16        q3,  q4,  q5
        vmax.u16        q4,  q6,  q7
        vabd.u16        q5,  q11, q12    @ abs(p0 - q0)
        vmax.u16        q2,  q2,  q3
        vadd.u16        q5,  q5,  q5     @ abs(p0 - q0) * 2
        vabd.u16        q6,  q10, q13    @ abs(p1 - q1)
        vmax.u16        q2,  q2,  q4     @ max(abs(p3 - p2), ..., abs(q2 - q3))
        vshr.u16        q6,  q6,  #1
        vcle.u16        q2,  q2,  q1     @ max(abs()) <= I
        vadd.u16        q5,  q5,  q6     @ abs(p0 - q0) * 2 + abs(p1 - q1) >> 1
        vcle.u16        q5,  q5,  q0
        vand            q2,  q2,  q5     @ fm

        vmovn.u16       d10, q2
        vmov            r8,  r9,  d10
        orrs            r8,  r8,  r9
        @ If no pixels need filtering, just exit as soon as possible
        beq             9f

.if \wd >= 8
        vdup.u16        q0,  r5

        vabd.u16        q1,  q8,  q11    @ abs(p3 - p0)
        vabd.u16        q3,  q9,  q11    @ abs(p2 - p0)
        vabd.u16        q4,  q10, q11    @ abs(p1 - p0)
        vabd.u16        q5,  q13, q12    @ abs(q1 - q0)
        vabd.u16        q6,  q14, q12    @ abs(q2 - q0)
        vabd.u16        q7,  q15, q12    @ abs(q3 - q0)
        vmax.u16        q1,  q1,  q3
        vmax.u16        q4,  q4,  q5
        vmax.u16        q6,  q6,  q7
        @ The rest of the calculation of flat8in is interleaved below
.endif

        @ Calculate the normal inner loop filter for 2 or 4 pixels
        vabd.u16        q3,  q10, q11    @ abs(p1 - p0)
.if \wd == 8
        vmax.u16        q1,  q1,  q4
.endif
        vabd.u16        q4,  q13, q12    @ abs(q1 - q0)
.if \wd == 8
        vmax.u16        q1,  q1,  q6
.endif

        vsub.u16        q5,  q10, q13    @ p1 - q1
        vmax.u16        q3,  q3,  q4     @ max(abs(p1 - p0), abs(q1 - q0))
        vdup.u16        q4,  r4          @ H
        vsub.u16        q6,  q12, q11    @ q0 - p0
.if \wd == 8
        vcle.u16        q1,  q1,  q0     @ flat8in
.endif
        vdup.u16        q0,  r6          @ left shift for saturation
        vcle.u16        q3,  q3,  q4     @ !hev
.if \wd == 8
        vand            q1,  q1,  q2     @ flat8in && fm
.endif
        vneg.s16        q4,  q0          @ negative left shift after saturation
        vqshl.s16       q5,  q5,  q0
.if \wd == 8
        vbic            q2,  q2,  q1     @ fm && !flat8in
.endif
        vmov.s16        q7,  #3
        vand            q3,  q3,  q2     @ !hev && fm && !flat8in
        vshl.s16        q5,  q5,  q4     @ av_clip_int2p(p1 - q1, BIT_DEPTH - 1)

        vmul.s16        q6,  q6,  q7     @ 3 * (q0 - p0)
        vbic            q5,  q5,  q3     @ if (!hev) av_clip_int2p = 0
        vadd.s16        q6,  q6,  q5     @ 3 * (q0 - p0) [+ av_clip_int2p(p1 - q1)]
        vmov.s16        q5,  #4
        vqshl.s16       q6,  q6,  q0
        vmov.s16        q0,  #3
        vshl.s16        q6,  q6,  q4     @ av_clip_int2p(3 * (q0 - p0) [+ av_clip_int2p(p1 - q1)], BIT_DEPTH - 1) = f
        vdup.u16        q4,  r7          @ max pixel value

        vshr.u16        q4,  q4,  #1     @ (1 << (BIT_DEPTH - 1)) - 1)

        vadd.s16        q5,  q6,  q5     @ f + 4
        vadd.s16        q0,  q6,  q0     @ f + 3
        vmov.s16        q6,  #0
        vmin.s16        q5,  q5,  q4     @ FFMIN(f + 4, (1 << (BIT_DEPTH - 1)) - 1)
        vmin.s16        q0,  q0,  q4     @ FFMIN(f + 3, (1 << (BIT_DEPTH - 1)) - 1)
        vdup.u16        q4,  r7          @ max pixel value
        vshr.s16        q5,  q5,  #3     @ f1
        vshr.s16        q0,  q0,  #3     @ f2

        vadd.s16        q0,  q11, q0     @ p0 + f2
        vsub.s16        q7,  q12, q5     @ q0 - f1
        vmin.s16        q0,  q0,  q4
        vmin.s16        q7,  q7,  q4
        vrshr.s16       q5,  q5,  #1     @ f = (f1 + 1) >> 1
        vmax.s16        q0,  q0,  q6     @ out p0
        vmax.s16        q7,  q7,  q6     @ out q0
        vbit            q11, q0,  q2     @ if (fm && !flat8in)
        vbit            q12, q7,  q2
.if \wd >= 8
        vmovn.u16       d4,  q1
.endif

        vadd.s16        q0,  q10, q5     @ p1 + f
        vsub.s16        q7,  q13, q5     @ q1 - f
.if \wd >= 8
        vmov            r8,  r9,  d4
.endif
        vmin.s16        q0,  q0,  q4
        vmin.s16        q7,  q7,  q4
.if \wd >= 8
        orrs            r8,  r8,  r9
.endif
        vmax.s16        q0,  q0,  q6     @ out p1
        vmax.s16        q7,  q7,  q6     @ out q1
        vbit            q10, q0,  q3     @ if (!hev && fm && !flat8in)
        vbit            q13, q7,  q3

.if \wd >= 8
        @ If no pixels need flat8in, jump to a writeout of the inner 4 pixels
        beq             6f

        @ flat8in
        vadd.u16        q2,  q8,  q9
        vadd.u16        q3,  q10, q13
        vadd.u16        q4,  q8,  q10
        vadd.u16        q5,  q11, q14
        vadd.u16        q0,  q2,  q2
        vadd.u16        q0,  q0,  q11
        vadd.u16        q0,  q0,  q12
        vadd.u16        q0,  q0,  q4
        vsub.s16        q3,  q3,  q2
        vsub.s16        q5,  q5,  q4
        vrshr.u16       q6,  q0,  #3     @ out p2

        vadd.u16        q0,  q0,  q3
        vadd.u16        q2,  q8,  q11
        vadd.u16        q3,  q12, q15
        vrshr.u16       q7,  q0,  #3     @ out p1

        vadd.u16        q0,  q0,  q5
        vsub.s16        q3,  q3,  q2
        vadd.u16        q4,  q9,  q12
        vbit            q9,  q6,  q1
        vadd.u16        q5,  q13, q15
        vrshr.u16       q6,  q0,  #3     @ out p0

        vadd.u16        q0,  q0,  q3
        vsub.s16        q5,  q5,  q4
        vadd.u16        q2,  q10, q13
        vbit            q10, q7,  q1
        vadd.u16        q3,  q14, q15
        vrshr.u16       q7,  q0,  #3     @ out q0

        vadd.u16        q0,  q0,  q5
        vsub.s16        q3,  q3,  q2
        vbit            q11, q6,  q1
        vrshr.u16       q6,  q0,  #3     @ out q1

        vadd.u16        q0,  q0,  q3
        vbit            q12, q7,  q1
        vrshr.u16       q7,  q0,  #3     @ out q2
        vbit            q13, q6,  q1
        vbit            q14, q7,  q1
.endif
.endm

@ The input to and output from this macro is in the registers d16-d31,
@ and d0-d7 are used as scratch registers.
@ p7 = d16 .. p3 = d20, p0 = d23, q0 = d24, q3 = d27, q7 = d31
@ Depending on the width of the loop filter, we either use d16-d19
@ and d28-d31 as temp registers, or d8-d15.
@ In practice, this is only ever instantiated once, so the macro parameters
@ could be hardcoded, but keeping them as is, to keep similarities to the
@ 8 bpp and aarch64 versions.
.macro loop_filter wd, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8
        vdup.u16        d0,  r2          @ E
        vdup.u16        d2,  r3          @ I

        vabd.u16        d4,  d20, d21    @ abs(p3 - p2)
        vabd.u16        d5,  d21, d22    @ abs(p2 - p1)
        vabd.u16        d6,  d22, d23    @ abs(p1 - p0)
        vabd.u16        d7,  d24, d25    @ abs(q0 - q1)
        vabd.u16        \tmp1,  d25, d26 @ abs(q1 - q2)
        vabd.u16        \tmp2,  d26, d27 @ abs(q2 - q3)
        vmax.u16        d4,  d4,  d5
        vmax.u16        d5,  d6,  d7
        vmax.u16        \tmp1,  \tmp1,  \tmp2
        vabd.u16        d6,  d23, d24    @ abs(p0 - q0)
        vmax.u16        d4,  d4,  d5
        vadd.u16        d6,  d6,  d6     @ abs(p0 - q0) * 2
        vabd.u16        d5,  d22, d25    @ abs(p1 - q1)
        vmax.u16        d4,  d4,  \tmp1  @ max(abs(p3 - p2), ..., abs(q2 - q3))
        vshr.u16        d5,  d5,  #1
        vcle.u16        d4,  d4,  d2     @ max(abs()) <= I
        vadd.u16        d6,  d6,  d5     @ abs(p0 - q0) * 2 + abs(p1 - q1) >> 1
        vcle.u16        d6,  d6,  d0
        vand            d4,  d4,  d6     @ fm

        vdup.u16        d3,  r4          @ H
        vmov            r8,  r9,  d4
        orrs            r8,  r8,  r9
        @ If no pixels need filtering, just exit as soon as possible
        beq             9f

.if \wd >= 8
        vdup.u16        d0,  r5

        vabd.u16        d6,  d20, d23    @ abs(p3 - p0)
        vabd.u16        d2,  d21, d23    @ abs(p2 - p0)
        vabd.u16        d1,  d22, d23    @ abs(p1 - p0)
        vabd.u16        \tmp1,  d25, d24 @ abs(q1 - q0)
        vabd.u16        \tmp2,  d26, d24 @ abs(q2 - q0)
        vabd.u16        \tmp3,  d27, d24 @ abs(q3 - q0)
        vmax.u16        d6,  d6,  d2
        vmax.u16        d1,  d1,  \tmp1
        vmax.u16        \tmp2,  \tmp2,  \tmp3
.if \wd == 16
        vabd.u16        d7,  d16, d23    @ abs(p7 - p0)
        vmax.u16        d6,  d6,  d1
        vabd.u16        d2,  d17, d23    @ abs(p6 - p0)
        vmax.u16        d6,  d6,  \tmp2
        vabd.u16        d1,  d18, d23    @ abs(p5 - p0)
        vcle.u16        d6,  d6,  d0     @ flat8in
        vabd.u16        d8,  d19, d23    @ abs(p4 - p0)
        vand            d6,  d6,  d4     @ flat8in && fm
        vabd.u16        d9,  d28, d24    @ abs(q4 - q0)
        vbic            d4,  d4,  d6     @ fm && !flat8in
        vabd.u16        d10, d29, d24    @ abs(q5 - q0)
        vabd.u16        d11, d30, d24    @ abs(q6 - q0)
        vabd.u16        d12, d31, d24    @ abs(q7 - q0)

        vmax.u16        d7,  d7,  d2
        vmax.u16        d1,  d1,  d8
        vmax.u16        d9,  d9,  d10
        vmax.u16        d11, d11, d12
        @ The rest of the calculation of flat8out is interleaved below
.else
        @ The rest of the calculation of flat8in is interleaved below
.endif
.endif

        @ Calculate the normal inner loop filter for 2 or 4 pixels
        vabd.u16        d5,  d22, d23           @ abs(p1 - p0)
.if \wd == 16
        vmax.u16        d7,  d7,  d1
        vmax.u16        d9,  d9,  d11
.elseif \wd == 8
        vmax.u16        d6,  d6,  d1
.endif
        vabd.u16        d1,  d25, d24           @ abs(q1 - q0)
.if \wd == 16
        vmax.u16        d7,  d7,  d9
.elseif \wd == 8
        vmax.u16        d6,  d6,  \tmp2
.endif
        vdup.u16        \tmp2,  r6              @ left shift for saturation
        vsub.u16        \tmp1,  d22, d25        @ p1 - q1
        vneg.s16        \tmp6,  \tmp2           @ negative left shift after saturation
        vmax.u16        d5,  d5,  d1            @ max(abs(p1 - p0), abs(q1 - q0))
        vsub.u16        \tmp3,   d24, d23       @ q0 - p0
        vmov.s16        \tmp5,  #3
.if \wd == 8
        vcle.u16        d6,  d6,  d0            @ flat8in
.endif
        vcle.u16        d5,  d5,  d3            @ !hev
.if \wd == 8
        vand            d6,  d6,  d4            @ flat8in && fm
.endif
        vqshl.s16       \tmp1,  \tmp1,  \tmp2
.if \wd == 16
        vcle.u16        d7,  d7,  d0            @ flat8out
.elseif \wd == 8
        vbic            d4,  d4,  d6            @ fm && !flat8in
.endif
        vand            d5,  d5,  d4            @ !hev && fm && !flat8in
.if \wd == 16
        vand            d7,  d7,  d6            @ flat8out && flat8in && fm
.endif
        vshl.s16        \tmp1,  \tmp1,  \tmp6   @ av_clip_int2p(p1 - q1, BIT_DEPTH - 1)

        vmul.s16        \tmp3,  \tmp3,  \tmp5   @ 3 * (q0 - p0)
        vbic            \tmp1,  \tmp1,   d5     @ if (!hev) av_clip_int2p = 0
        vmov.s16        d2,  #4
        vadd.s16        \tmp3,  \tmp3,  \tmp1   @ 3 * (q0 - p0) [+ av_clip_int2p(p1 - q1)]
        vmov.s16        d3,  #3
        vqshl.s16       \tmp1,  \tmp3,  \tmp2
        vmov.s16        \tmp5,  #0
        vshl.s16        \tmp1,  \tmp1,  \tmp6   @ av_clip_int2p(3 * (q0 - p0) [+ av_clip_int2p(p1 - q1)], BIT_DEPTH - 1) = f
        vdup.u16        \tmp6,  r7              @ max pixel value
.if \wd == 16
        vbic            d6,  d6,  d7            @ fm && flat8in && !flat8out
.endif

        vshr.u16        \tmp2,  \tmp6,  #1      @ (1 << (BIT_DEPTH - 1)) - 1

        vadd.s16        \tmp3,  \tmp1,  d2      @ f + 4
        vadd.s16        \tmp4,  \tmp1,  d3      @ f + 3
        vmin.s16        \tmp3,  \tmp3,  \tmp2   @ FFMIN(f + 4, (1 << (BIT_DEPTH - 1)) - 1)
        vmin.s16        \tmp4,  \tmp4,  \tmp2   @ FFMIN(f + 3, (1 << (BIT_DEPTH - 1)) - 1)
        vshr.s16        \tmp3,  \tmp3,  #3      @ f1
        vshr.s16        \tmp4,  \tmp4,  #3      @ f2

        vadd.s16        d0,  d23, \tmp4         @ p0 + f2
        vsub.s16        d2,  d24, \tmp3         @ q0 - f1
        vmin.s16        d0,  d0,  \tmp6
        vmin.s16        d2,  d2,  \tmp6
        vrshr.s16       \tmp3,  \tmp3,  #1      @ f = (f1 + 1) >> 1
        vmax.s16        d0,  d0,  \tmp5         @ out p0
        vmax.s16        d2,  d2,  \tmp5         @ out q0
        vbit            d23, d0,  d4            @ if (fm && !flat8in)
        vbit            d24, d2,  d4

        vadd.s16        d0,  d22, \tmp3         @ p1 + f
        vsub.s16        d2,  d25, \tmp3         @ q1 - f
.if \wd >= 8
        vmov            r8,  r9,  d6
.endif
        vmin.s16        d0,  d0,  \tmp6
        vmin.s16        d2,  d2,  \tmp6
.if \wd >= 8
        orrs            r8,  r8,  r9
.endif
        vmax.s16        d0,  d0,  \tmp5         @ out p1
        vmax.s16        d2,  d2,  \tmp5         @ out q1
        vbit            d22, d0,  d5            @ if (!hev && fm && !flat8in)
        vbit            d25, d2,  d5

.if \wd >= 8
        @ If no pixels need flat8in, jump to flat8out
        @ (or to a writeout of the inner 4 pixels, for wd=8)
        beq             6f

        @ flat8in
        vadd.u16        \tmp1,  d20, d21
        vadd.u16        \tmp3,  d22, d25
        vadd.u16        \tmp5,  d20, d22
        vadd.u16        \tmp7,  d23, d26
        vadd.u16        d0,  \tmp1,  \tmp1
        vadd.u16        d0,  d0,  d23
        vadd.u16        d0,  d0,  d24
        vadd.u16        d0,  d0,  \tmp5
        vsub.s16        \tmp3,  \tmp3,  \tmp1
        vsub.s16        \tmp7,  \tmp7,  \tmp5
        vrshr.u16       d2,  d0,  #3            @ out p2

        vadd.u16        d0,  d0,  \tmp3
        vadd.u16        \tmp1,  d20, d23
        vadd.u16        \tmp3,  d24, d27
        vrshr.u16       d3,  d0,  #3            @ out p1

        vadd.u16        d0,  d0,  \tmp7
        vsub.s16        \tmp3,  \tmp3,  \tmp1
        vadd.u16        \tmp5,  d21, d24
        vadd.u16        \tmp7,  d25, d27
        vrshr.u16       d4,  d0,  #3            @ out p0

        vadd.u16        d0,  d0,  \tmp3
        vsub.s16        \tmp7,  \tmp7,  \tmp5
        vadd.u16        \tmp1,  d22, d25
        vadd.u16        \tmp3,  d26, d27
        vrshr.u16       d5,  d0,  #3            @ out d0

        vadd.u16        d0,  d0,  \tmp7
        vsub.s16        \tmp3,  \tmp3,  \tmp1
        vrshr.u16       \tmp5,  d0,  #3         @ out q1

        vadd.u16        d0,  d0,  \tmp3
        @ The output here is written back into the input registers. This doesn't
        @ matter for the flat8out part below, since we only update those pixels
        @ which won't be touched below.
        vbit            d21, d2,  d6
        vbit            d22, d3,  d6
        vbit            d23, d4,  d6
        vrshr.u16       \tmp6,  d0,  #3         @ out q2
        vbit            d24, d5,  d6
        vbit            d25, \tmp5,  d6
        vbit            d26, \tmp6,  d6
.endif
.if \wd == 16
6:
        vorr            d2,  d6,  d7
        vmov            r8,  r9,  d2
        orrs            r8,  r8,  r9
        @ If no pixels needed flat8in nor flat8out, jump to a
        @ writeout of the inner 4 pixels
        beq             7f
        vmov            r8,  r9,  d7
        orrs            r8,  r8,  r9
        @ If no pixels need flat8out, jump to a writeout of the inner 6 pixels
        beq             8f

        @ flat8out
        @ This writes all outputs into d2-d17 (skipping d6 and d16).
        @ If this part is skipped, the output is read from d21-d26 (which is the input
        @ to this section).
        vshl.u16        d0,  d16, #3  @ 8 * d16
        vsub.u16        d0,  d0,  d16 @ 7 * d16
        vadd.u16        d0,  d0,  d17
        vadd.u16        d8,  d17, d18
        vadd.u16        d10, d19, d20
        vadd.s16        d0,  d0,  d8
        vadd.u16        d8,  d16, d17
        vadd.u16        d12, d21, d22
        vadd.s16        d0,  d0,  d10
        vadd.u16        d10, d18, d25
        vadd.u16        d14, d23, d24
        vsub.s16        d10, d10, d8
        vadd.s16        d0,  d0,  d12
        vadd.s16        d0,  d0,  d14
        vadd.u16        d12, d16, d18
        vadd.u16        d14, d19, d26
        vrshr.u16       d2,  d0,  #4

        vadd.s16        d0,  d0,  d10
        vadd.u16        d8,  d16, d19
        vadd.u16        d10, d20, d27
        vsub.s16        d14, d14, d12
        vbif            d2,  d17, d7
        vrshr.u16       d3,  d0,  #4

        vadd.s16        d0,  d0,  d14
        vadd.u16        d12, d16, d20
        vadd.u16        d14, d21, d28
        vsub.s16        d10, d10, d8
        vbif            d3,  d18, d7
        vrshr.u16       d4,  d0,  #4

        vadd.s16        d0,  d0,  d10
        vadd.u16        d8,  d16, d21
        vadd.u16        d10, d22, d29
        vsub.s16        d14, d14, d12
        vbif            d4,  d19, d7
        vrshr.u16       d5,  d0,  #4

        vadd.s16        d0,  d0,  d14
        vadd.u16        d12, d16, d22
        vadd.u16        d14, d23, d30
        vsub.s16        d10, d10, d8
        vbif            d5,  d20, d7
        vrshr.u16       d6,  d0,  #4

        vadd.s16        d0,  d0,  d10
        vadd.u16        d10, d16, d23
        vsub.s16        d14, d14, d12
        vadd.u16        d12, d24, d31
        vbif            d6,  d21, d7
        vrshr.u16       d8,  d0,  #4

        vadd.s16        d0,  d0,  d14
        vsub.s16        d10, d12, d10
        vadd.u16        d12, d17, d24
        vadd.u16        d14, d25, d31
        vbif            d8,  d22, d7
        vrshr.u16       d9,  d0,  #4

        vadd.s16        d0,  d0,  d10
        vsub.s16        d14, d14, d12
        vadd.u16        d12, d26, d31
        vbif            d9,  d23, d7
        vrshr.u16       d10, d0,  #4

        vadd.s16        d0,  d0,  d14
        vadd.u16        d14, d18, d25
        vadd.u16        d18, d19, d26
        vsub.s16        d12, d12, d14
        vadd.u16        d14, d27, d31
        vbif            d10, d24, d7
        vrshr.u16       d11, d0,  #4

        vadd.s16        d0,  d0,  d12
        vadd.u16        d12, d20, d27
        vsub.s16        d14, d14, d18
        vadd.u16        d18, d28, d31
        vbif            d11, d25, d7
        vsub.s16        d18, d18, d12
        vrshr.u16       d12, d0,  #4

        vadd.s16        d0,  d0,  d14
        vadd.u16        d14, d21, d28
        vadd.u16        d20, d29, d31
        vbif            d12, d26, d7
        vrshr.u16       d13, d0,  #4

        vadd.s16        d0,  d0,  d18
        vsub.s16        d20, d20, d14
        vadd.u16        d18, d22, d29
        vadd.u16        d22, d30, d31
        vbif            d13, d27, d7
        vrshr.u16       d14, d0,  #4

        vadd.s16        d0,  d0,  d20
        vsub.s16        d22, d22, d18
        vbif            d14, d28, d7
        vrshr.u16       d15, d0,  #4

        vadd.s16        d0,  d0,  d22
        vbif            d15, d29, d7
        vrshr.u16       d17, d0,  #4
        vbif            d17, d30, d7
.endif
.endm

.macro loop_filter_q_4
        loop_filter_q   4
.endm

.macro loop_filter_q_8
        loop_filter_q   8
.endm

.macro loop_filter_16
        loop_filter     16, d8,  d9,  d10, d11, d12, d13, d14, d15
.endm


@ The public functions in this file have got the following signature:
@ void loop_filter(uint8_t *dst, ptrdiff_t stride, int mb_lim, int lim, int hev_thr);

.macro bpp_frontend func, bpp
function ff_\func\()_\bpp\()_neon, export=1
        push            {r4-r9,lr}
        ldr             r4,  [sp, #28]
        vpush           {q4-q7}
        lsl             r2,  r2,  #\bpp - 8
        lsl             r3,  r3,  #\bpp - 8
        lsl             r4,  r4,  #\bpp - 8
        mov             r5,  #1 << (\bpp - 8)
        mov             r6,  #16 - \bpp
        movw            r7,  #((1 << \bpp) - 1)
        bl              \func\()_16_neon
        vpop            {q4-q7}
        pop             {r4-r9,pc}
endfunc
.endm

.macro bpp_frontends func
        bpp_frontend    \func, 10
        bpp_frontend    \func, 12
.endm

.macro bpp_frontend_rep func, suffix, int_suffix, rep, dir, bpp
function ff_\func\()_\suffix\()_\bpp\()_neon, export=1
        push            {r4-r9,lr}
        ldr             r4,  [sp, #28]
        vpush           {q4-q7}
        lsl             r2,  r2,  #\bpp - 8
        lsl             r3,  r3,  #\bpp - 8
        lsl             r4,  r4,  #\bpp - 8
        mov             r5,  #1 << (\bpp - 8)
        mov             r6,  #16 - \bpp
        movw            r7,  #((1 << \bpp) - 1)
        bl              \func\()_\int_suffix\()_16_neon
.ifc \dir,h
        add             r0,  r0,  r1, lsl #2
.else
        add             r0,  r0,  #8
.endif
        bl              \func\()_\int_suffix\()_16_neon
.if \rep >= 4
.ifc \dir,h
        add             r0,  r0,  r1, lsl #2
        bl              \func\()_\int_suffix\()_16_neon
        add             r0,  r0,  r1, lsl #2
        bl              \func\()_\int_suffix\()_16_neon
.else
        add             r0,  r0,  #8
        bl              \func\()_\int_suffix\()_16_neon
        add             r0,  r0,  #8
        bl              \func\()_\int_suffix\()_16_neon
.endif
.endif
        vpop            {q4-q7}
        pop             {r4-r9,pc}
endfunc
.endm

.macro bpp_frontends_rep func, suffix, int_suffix, rep, dir
        bpp_frontend_rep \func, \suffix, \int_suffix, \rep, \dir, 10
        bpp_frontend_rep \func, \suffix, \int_suffix, \rep, \dir, 12
.endm

.macro bpp_frontend_mix2 wd1, wd2, dir, bpp
function ff_vp9_loop_filter_\dir\()_\wd1\()\wd2\()_16_\bpp\()_neon, export=1
        push            {r4-r9,lr}
        ldr             r4,  [sp, #28]
        vpush           {q4-q7}
        push            {r2, r3, r4}
        and             r2,  r2,  #0xff
        and             r3,  r3,  #0xff
        and             r4,  r4,  #0xff
        lsl             r2,  r2,  #\bpp - 8
        lsl             r3,  r3,  #\bpp - 8
        lsl             r4,  r4,  #\bpp - 8
        mov             r5,  #1 << (\bpp - 8)
        mov             r6,  #16 - \bpp
        movw            r7,  #((1 << \bpp) - 1)
        bl              vp9_loop_filter_\dir\()_\wd1\()_8_16_neon
.ifc \dir,h
        add             r0,  r0,  r1, lsl #3
.else
        add             r0,  r0,  #16
.endif
        pop             {r2, r3, r4}
        lsr             r2,  r2,  #8
        lsr             r3,  r3,  #8
        lsr             r4,  r4,  #8
        lsl             r2,  r2,  #\bpp - 8
        lsl             r3,  r3,  #\bpp - 8
        lsl             r4,  r4,  #\bpp - 8
        bl              vp9_loop_filter_\dir\()_\wd2\()_8_16_neon
        vpop            {q4-q7}
        pop             {r4-r9,pc}
endfunc
.endm

.macro bpp_frontends_mix2 wd1, wd2
        bpp_frontend_mix2 \wd1, \wd2, v, 10
        bpp_frontend_mix2 \wd1, \wd2, v, 12
        bpp_frontend_mix2 \wd1, \wd2, h, 10
        bpp_frontend_mix2 \wd1, \wd2, h, 12
.endm

function vp9_loop_filter_v_4_8_16_neon
        sub             r12, r0,  r1, lsl #2
        vld1.16         {q8},  [r12,:128], r1 @ p3
        vld1.16         {q12}, [r0, :128], r1 @ q0
        vld1.16         {q9},  [r12,:128], r1 @ p2
        vld1.16         {q13}, [r0, :128], r1 @ q1
        vld1.16         {q10}, [r12,:128], r1 @ p1
        vld1.16         {q14}, [r0, :128], r1 @ q2
        vld1.16         {q11}, [r12,:128], r1 @ p0
        vld1.16         {q15}, [r0, :128], r1 @ q3
        sub             r0,  r0,  r1, lsl #2
        sub             r12, r12, r1, lsl #1

        loop_filter_q_4

        vst1.16         {q10}, [r12,:128], r1
        vst1.16         {q12}, [r0, :128], r1
        vst1.16         {q11}, [r12,:128], r1
        vst1.16         {q13}, [r0, :128], r1
        sub             r0,  r0,  r1, lsl #1
9:
        bx              lr
endfunc

bpp_frontends vp9_loop_filter_v_4_8


function vp9_loop_filter_h_4_8_16_neon
        sub             r12, r0,  #8
        add             r0,  r12, r1, lsl #2
        vld1.16         {q8},  [r12,:64], r1
        vld1.16         {q12}, [r0, :64], r1
        vld1.16         {q9},  [r12,:64], r1
        vld1.16         {q13}, [r0, :64], r1
        vld1.16         {q10}, [r12,:64], r1
        vld1.16         {q14}, [r0, :64], r1
        vld1.16         {q11}, [r12,:64], r1
        vld1.16         {q15}, [r0, :64], r1

        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2
        @ Move r0/r12 forward by 2 pixels; we don't need to rewrite the
        @ outermost 2 pixels since they aren't changed.
        add             r12, r12, #4
        add             r0,  r0,  #4

        transpose16_q_8x8 q8, q9, q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        loop_filter_q_4

        @ We only will write the mid 4 pixels back; after the loop filter,
        @ these are in q10, q11, q12, q13, ordered as rows (8x4 pixels).
        @ We need to transpose them to columns, done with a
        @ 4x4 transpose (which in practice is two 4x4 transposes of the two
        @ 4x4 halves of the 8x4 pixels; into 4x8 pixels).
        transpose16_4x4 q10, q11, q12, q13

        vst1.16         {d20}, [r12], r1
        vst1.16         {d21}, [r0],  r1
        vst1.16         {d22}, [r12], r1
        vst1.16         {d23}, [r0],  r1
        vst1.16         {d24}, [r12], r1
        vst1.16         {d25}, [r0],  r1
        vst1.16         {d26}, [r12], r1
        vst1.16         {d27}, [r0],  r1
        sub             r12, r12, r1, lsl #2
9:
        add             r0,  r12, #4
        bx              lr
endfunc

bpp_frontends vp9_loop_filter_h_4_8


function vp9_loop_filter_v_8_8_16_neon
        sub             r12, r0,  r1, lsl #2
        vld1.16         {q8},  [r12,:128], r1 @ p3
        vld1.16         {q12}, [r0, :128], r1 @ q0
        vld1.16         {q9},  [r12,:128], r1 @ p2
        vld1.16         {q13}, [r0, :128], r1 @ q1
        vld1.16         {q10}, [r12,:128], r1 @ p1
        vld1.16         {q14}, [r0, :128], r1 @ q2
        vld1.16         {q11}, [r12,:128], r1 @ p0
        vld1.16         {q15}, [r0, :128], r1 @ q3
        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2
        add             r12, r12, r1

        loop_filter_q_8

        vst1.16         {q9},  [r12,:128], r1
        vst1.16         {q12}, [r0, :128], r1
        vst1.16         {q10}, [r12,:128], r1
        vst1.16         {q13}, [r0, :128], r1
        vst1.16         {q11}, [r12,:128], r1
        vst1.16         {q14}, [r0, :128], r1
        sub             r0,  r0,  r1, lsl #1
        sub             r0,  r0,  r1
9:
        bx              lr
6:
        sub             r12, r0,  r1, lsl #1
        vst1.16         {q10}, [r12,:128], r1
        vst1.16         {q12}, [r0, :128], r1
        vst1.16         {q11}, [r12,:128], r1
        vst1.16         {q13}, [r0, :128], r1
        sub             r0,  r0,  r1, lsl #1
        bx              lr
endfunc

bpp_frontends vp9_loop_filter_v_8_8


function vp9_loop_filter_h_8_8_16_neon
        sub             r12, r0,  #8
        add             r0,  r12, r1, lsl #2
        vld1.16         {q8},  [r12,:64], r1
        vld1.16         {q12}, [r0, :64], r1
        vld1.16         {q9},  [r12,:64], r1
        vld1.16         {q13}, [r0, :64], r1
        vld1.16         {q10}, [r12,:64], r1
        vld1.16         {q14}, [r0, :64], r1
        vld1.16         {q11}, [r12,:64], r1
        vld1.16         {q15}, [r0, :64], r1

        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2

        transpose16_q_8x8 q8, q9, q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        loop_filter_q_8

        @ Even though only 6 pixels per row have been changed, we write the
        @ full 8 pixel registers.
        transpose16_q_8x8 q8, q9, q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        vst1.16         {q8},  [r12,:64], r1
        vst1.16         {q12}, [r0, :64], r1
        vst1.16         {q9},  [r12,:64], r1
        vst1.16         {q13}, [r0, :64], r1
        vst1.16         {q10}, [r12,:64], r1
        vst1.16         {q14}, [r0, :64], r1
        vst1.16         {q11}, [r12,:64], r1
        vst1.16         {q15}, [r0, :64], r1
        sub             r12, r12, r1, lsl #2
9:
        add             r0,  r12, #8
        bx              lr
6:
        @ If we didn't need to do the flat8in part, we use the same writeback
        @ as in loop_filter_h_4_8.
        add             r12, r12, #4
        add             r0,  r0,  #4
        transpose16_4x4 q10, q11, q12, q13

        vst1.16         {d20}, [r12], r1
        vst1.16         {d21}, [r0],  r1
        vst1.16         {d22}, [r12], r1
        vst1.16         {d23}, [r0],  r1
        vst1.16         {d24}, [r12], r1
        vst1.16         {d25}, [r0],  r1
        vst1.16         {d26}, [r12], r1
        vst1.16         {d27}, [r0],  r1
        sub             r12, r12, r1, lsl #2
        add             r0,  r12, #4
        bx              lr
endfunc

bpp_frontends vp9_loop_filter_h_8_8

bpp_frontends_mix2 4, 4
bpp_frontends_mix2 4, 8
bpp_frontends_mix2 8, 4
bpp_frontends_mix2 8, 8

function vp9_loop_filter_v_16_4_16_neon
        sub             r12, r0,  r1, lsl #3
        @ Read p7-p0 using r12 and q0-q7 using r0
        vld1.16         {d16}, [r12,:64], r1 @ p7
        vld1.16         {d24}, [r0, :64], r1 @ q0
        vld1.16         {d17}, [r12,:64], r1 @ p6
        vld1.16         {d25}, [r0, :64], r1 @ q1
        vld1.16         {d18}, [r12,:64], r1 @ p5
        vld1.16         {d26}, [r0, :64], r1 @ q2
        vld1.16         {d19}, [r12,:64], r1 @ p4
        vld1.16         {d27}, [r0, :64], r1 @ q3
        vld1.16         {d20}, [r12,:64], r1 @ p3
        vld1.16         {d28}, [r0, :64], r1 @ q4
        vld1.16         {d21}, [r12,:64], r1 @ p2
        vld1.16         {d29}, [r0, :64], r1 @ q5
        vld1.16         {d22}, [r12,:64], r1 @ p1
        vld1.16         {d30}, [r0, :64], r1 @ q6
        vld1.16         {d23}, [r12,:64], r1 @ p0
        vld1.16         {d31}, [r0, :64], r1 @ q7
        sub             r12, r12, r1, lsl #3
        sub             r0,  r0,  r1, lsl #3
        add             r12, r12, r1

        loop_filter_16

        @ If we did the flat8out part, we get the output in
        @ d2-d17 (skipping d7 and d16). r12 points to r0 - 7 * stride,
        @ store d2-d9 there, and d10-d17 into r0.
        vst1.16         {d2},  [r12,:64], r1
        vst1.16         {d10}, [r0, :64], r1
        vst1.16         {d3},  [r12,:64], r1
        vst1.16         {d11}, [r0, :64], r1
        vst1.16         {d4},  [r12,:64], r1
        vst1.16         {d12}, [r0, :64], r1
        vst1.16         {d5},  [r12,:64], r1
        vst1.16         {d13}, [r0, :64], r1
        vst1.16         {d6},  [r12,:64], r1
        vst1.16         {d14}, [r0, :64], r1
        vst1.16         {d8},  [r12,:64], r1
        vst1.16         {d15}, [r0, :64], r1
        vst1.16         {d9},  [r12,:64], r1
        vst1.16         {d17}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #3
        add             r0,  r0,  r1

9:
        bx              lr

8:
        add             r12, r12, r1, lsl #2
        @ If we didn't do the flat8out part, the output is left in the
        @ input registers.
        vst1.16         {d21}, [r12,:64], r1
        vst1.16         {d24}, [r0, :64], r1
        vst1.16         {d22}, [r12,:64], r1
        vst1.16         {d25}, [r0, :64], r1
        vst1.16         {d23}, [r12,:64], r1
        vst1.16         {d26}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #1
        sub             r0,  r0,  r1
        bx              lr
7:
        sub             r12, r0,  r1, lsl #1
        vst1.16         {d22}, [r12,:64], r1
        vst1.16         {d24}, [r0, :64], r1
        vst1.16         {d23}, [r12,:64], r1
        vst1.16         {d25}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #1
        bx              lr
endfunc

bpp_frontends_rep vp9_loop_filter_v_16, 8,  4, 2, v
bpp_frontends_rep vp9_loop_filter_v_16, 16, 4, 4, v

function vp9_loop_filter_h_16_4_16_neon
        sub             r12, r0,  #16
        sub             r0,  r0,  #8
        vld1.16         {d16}, [r12,:64], r1
        vld1.16         {d20}, [r0, :64], r1
        vld1.16         {d17}, [r12,:64], r1
        vld1.16         {d21}, [r0, :64], r1
        vld1.16         {d18}, [r12,:64], r1
        vld1.16         {d22}, [r0, :64], r1
        vld1.16         {d19}, [r12,:64], r1
        vld1.16         {d23}, [r0, :64], r1
        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2
        add             r12, r12, #16
        add             r0,  r0,  #16
        vld1.16         {d24}, [r12,:64], r1
        vld1.16         {d28}, [r0, :64], r1
        vld1.16         {d25}, [r12,:64], r1
        vld1.16         {d29}, [r0, :64], r1
        vld1.16         {d26}, [r12,:64], r1
        vld1.16         {d30}, [r0, :64], r1
        vld1.16         {d27}, [r12,:64], r1
        vld1.16         {d31}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #2
        sub             r12, r12, r1, lsl #2
        sub             r12, r12, #16
        sub             r0,  r0,  #16

        @ The 16x4 pixels read above is in four 4x4 blocks
        transpose16_q_4x4 q8,  q9,  d16, d17, d18, d19
        transpose16_q_4x4 q10, q11, d20, d21, d22, d23
        transpose16_q_4x4 q12, q13, d24, d25, d26, d27
        transpose16_q_4x4 q14, q15, d28, d29, d30, d31

        loop_filter_16

        @ Transpose back; this is the same transpose as above, but
        @ we can't take advantage of q registers for the transpose, since
        @ all d registers in the transpose aren't consecutive.
        transpose16_4x4 d16, d2,  d3,  d4
        transpose16_4x4 d5,  d6,  d8,  d9
        transpose16_4x4 d10, d11, d12, d13
        transpose16_4x4 d14, d15, d17, d31

        vst1.16         {d16}, [r12,:64], r1
        vst1.16         {d5},  [r0, :64], r1

        vst1.16         {d2},  [r12,:64], r1
        vst1.16         {d6},  [r0, :64], r1

        vst1.16         {d3},  [r12,:64], r1
        vst1.16         {d8},  [r0, :64], r1

        vst1.16         {d4},  [r12,:64], r1
        vst1.16         {d9},  [r0, :64], r1

        sub             r12, r12, r1, lsl #2
        sub             r0,  r0,  r1, lsl #2
        add             r12, r12, #16
        add             r0,  r0,  #16

        vst1.16         {d10}, [r12,:64], r1
        vst1.16         {d14}, [r0, :64], r1

        vst1.16         {d11}, [r12,:64], r1
        vst1.16         {d15}, [r0, :64], r1

        vst1.16         {d12}, [r12,:64], r1
        vst1.16         {d17}, [r0, :64], r1

        vst1.16         {d13}, [r12,:64], r1
        vst1.16         {d31}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #2
        sub             r0,  r0,  #8
        bx              lr
9:
        add             r0,  r0,  #8
        bx              lr
8:
        add             r12, r12, #8
        add             r0,  r0,  #8
        transpose16_q_4x4 q10, q11, d20, d21, d22, d23
        transpose16_q_4x4 q12, q13, d24, d25, d26, d27

        vst1.16         {d20}, [r12,:64], r1
        vst1.16         {d24}, [r0, :64], r1
        vst1.16         {d21}, [r12,:64], r1
        vst1.16         {d25}, [r0, :64], r1
        vst1.16         {d22}, [r12,:64], r1
        vst1.16         {d26}, [r0, :64], r1
        vst1.16         {d23}, [r12,:64], r1
        vst1.16         {d27}, [r0, :64], r1
        sub             r0,  r0,  r1, lsl #2
        bx              lr
7:
        add             r12, r12, #12
        add             r0,  r12, r1, lsl #1
        transpose16_q_4x4 q11, q12, d22, d23, d24, d25

        vst1.16         {d22}, [r12], r1
        vst1.16         {d24}, [r0],  r1
        vst1.16         {d23}, [r12], r1
        vst1.16         {d25}, [r0],  r1
        sub             r0,  r0,  r1, lsl #2
        add             r0,  r0,  #4
        bx              lr
endfunc

bpp_frontends_rep vp9_loop_filter_h_16, 8,  4, 2, h
bpp_frontends_rep vp9_loop_filter_h_16, 16, 4, 4, h
