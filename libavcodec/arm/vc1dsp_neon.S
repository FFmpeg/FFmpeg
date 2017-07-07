/*
 * VC1 NEON optimisations
 *
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
 */

#include "libavutil/arm/asm.S"
#include "neon.S"

#include "config.h"

@ Transpose rows into columns of a matrix of 16-bit elements. For 4x4, pass
@ double-word registers, for 8x4, pass quad-word registers.
.macro transpose16 r0, r1, r2, r3
        @ At this point:
        @   row[0]  r0
        @   row[1]  r1
        @   row[2]  r2
        @   row[3]  r3

        vtrn.16         \r0,  \r1         @ first and second row
        vtrn.16         \r2,  \r3         @ third and fourth row
        vtrn.32         \r0,  \r2         @ first and third row
        vtrn.32         \r1,  \r3         @ second and fourth row

        @ At this point, if registers are quad-word:
        @   column[0]   d0
        @   column[1]   d2
        @   column[2]   d4
        @   column[3]   d6
        @   column[4]   d1
        @   column[5]   d3
        @   column[6]   d5
        @   column[7]   d7

        @ At this point, if registers are double-word:
        @   column[0]   d0
        @   column[1]   d1
        @   column[2]   d2
        @   column[3]   d3
.endm

@ ff_vc1_inv_trans_{4,8}x{4,8}_neon and overflow: The input values in the file
@ are supposed to be in a specific range as to allow for 16-bit math without
@ causing overflows, but sometimes the input values are just big enough to
@ barely cause overflow in vadd instructions like:
@
@   vadd.i16  q0, q8, q10
@   vshr.s16  q0, q0, #\rshift
@
@ To prevent these borderline cases from overflowing, we just need one more
@ bit of precision, which is accomplished by replacing the sequence above with:
@
@   vhadd.s16 q0, q8, q10
@   vshr.s16  q0, q0, #(\rshift -1)
@
@ This works because vhadd is a single instruction that adds, then shifts to
@ the right once, all before writing the result to the destination register.
@
@ Even with this workaround, there were still some files that caused overflows
@ in ff_vc1_inv_trans_8x8_neon. See the comments in ff_vc1_inv_trans_8x8_neon
@ for the additional workaround.

@ Takes 4 columns of 8 values each and operates on it. Modeled after the first
@ for loop in vc1_inv_trans_4x8_c.
@ Input columns: q0 q1 q2 q3
@ Output columns: q0 q1 q2 q3
@ Trashes: r12 q8 q9 q10 q11 q12 q13
.macro vc1_inv_trans_4x8_helper add rshift
        @ Compute temp1, temp2 and setup scalar #17, #22, #10
        vadd.i16        q12,   q0,  q2              @ temp1 = src[0] + src[2]
        movw            r12,   #17
        vsub.i16        q13,   q0,  q2              @ temp2 = src[0] - src[2]
        movt            r12,   #22
        vmov.32         d0[0], r12
        movw            r12,   #10
        vmov.16         d1[0], r12

        vmov.i16        q8,  #\add                  @ t1 will accumulate here
        vmov.i16        q9,  #\add                  @ t2 will accumulate here

        vmul.i16        q10, q1,  d0[1]             @ t3 = 22 * (src[1])
        vmul.i16        q11, q3,  d0[1]             @ t4 = 22 * (src[3])

        vmla.i16        q8,  q12, d0[0]             @ t1 = 17 * (temp1) + 4
        vmla.i16        q9,  q13, d0[0]             @ t2 = 17 * (temp2) + 4

        vmla.i16        q10, q3,  d1[0]             @ t3 += 10 * src[3]
        vmls.i16        q11, q1,  d1[0]             @ t4 -= 10 * src[1]

        vhadd.s16       q0,  q8,  q10               @ dst[0] = (t1 + t3) >> 1
        vhsub.s16       q3,  q8,  q10               @ dst[3] = (t1 - t3) >> 1
        vhsub.s16       q1,  q9,  q11               @ dst[1] = (t2 - t4) >> 1
        vhadd.s16       q2,  q9,  q11               @ dst[2] = (t2 + t4) >> 1

        @ Halving add/sub above already did one shift
        vshr.s16        q0,  q0,  #(\rshift - 1)    @ dst[0] >>= (rshift - 1)
        vshr.s16        q3,  q3,  #(\rshift - 1)    @ dst[3] >>= (rshift - 1)
        vshr.s16        q1,  q1,  #(\rshift - 1)    @ dst[1] >>= (rshift - 1)
        vshr.s16        q2,  q2,  #(\rshift - 1)    @ dst[2] >>= (rshift - 1)
.endm

@ Takes 8 columns of 4 values each and operates on it. Modeled after the second
@ for loop in vc1_inv_trans_4x8_c.
@ Input columns: d0 d2 d4 d6 d1 d3 d5 d7
@ Output columns: d16 d17 d18 d19 d21 d20 d23 d22
@ Trashes all NEON registers (and r12) except for: q4 q5 q6 q7
.macro vc1_inv_trans_8x4_helper add add1beforeshift rshift
        @ At this point:
        @   src[0]      d0 overwritten later
        @   src[8]      d2
        @   src[16]     d4 overwritten later
        @   src[24]     d6
        @   src[32]     d1 overwritten later
        @   src[40]     d3
        @   src[48]     d5 overwritten later
        @   src[56]     d7

        movw            r12,   #12
        vmov.i16        q14,   #\add            @ t1|t2 will accumulate here
        movt            r12,   #6

        vadd.i16        d20,   d0,  d1          @ temp1 = src[0] + src[32]
        vsub.i16        d21,   d0,  d1          @ temp2 = src[0] - src[32]
        vmov.i32        d0[0], r12              @ 16-bit: d0[0] = #12, d0[1] = #6

        vshl.i16        q15,   q2,  #4          @ t3|t4 = 16 * (src[16]|src[48])
        vswp            d4,    d5               @ q2 = src[48]|src[16]
        vmla.i16        q14,   q10, d0[0]       @ t1|t2 = 12 * (temp1|temp2) + 64
        movw            r12,   #15
        movt            r12,   #9
        vmov.i32        d0[1], r12              @ 16-bit: d0[2] = #15, d0[3] = #9
        vneg.s16        d31,   d31              @ t4 = -t4
        vmla.i16        q15,   q2,  d0[1]       @ t3|t4 += 6 * (src[48]|src[16])

        @ At this point:
        @   d0[2]   #15
        @   d0[3]   #9
        @   q1      src[8]|src[40]
        @   q3      src[24]|src[56]
        @   q14     old t1|t2
        @   q15     old t3|t4

        vshl.i16        q8,  q1,  #4            @ t1|t2 = 16 * (src[8]|src[40])
        vswp            d2,  d3                 @ q1 = src[40]|src[8]
        vshl.i16        q12, q3,  #4            @ temp3a|temp4a = 16 * src[24]|src[56]
        vswp            d6,  d7                 @ q3 = src[56]|src[24]
        vshl.i16        q13, q1,  #2            @ temp3b|temp4b = 4 * (src[40]|src[8])
        vshl.i16        q2,  q3,  #2            @ temp1|temp2 = 4 * (src[56]|src[24])
        vswp            d3,  d6                 @ q1 = src[40]|src[56], q3 = src[8]|src[24]
        vsub.i16        q9,  q13, q12           @ t3|t4 = - (temp3a|temp4a) + (temp3b|temp4b)
        vadd.i16        q8,  q8,  q2            @ t1|t2 += temp1|temp2
        vmul.i16        q12, q3,  d0[3]         @ temp3|temp4 = 9 * src[8]|src[24]
        vmla.i16        q8,  q1,  d0[3]         @ t1|t2 += 9 * (src[40]|src[56])
        vswp            d6,  d7                 @ q3 = src[24]|src[8]
        vswp            d2,  d3                 @ q1 = src[56]|src[40]

        vsub.i16        q11, q14, q15           @ t8|t7 = old t1|t2 - old t3|t4
        vadd.i16        q10, q14, q15           @ t5|t6 = old t1|t2 + old t3|t4
  .if \add1beforeshift
        vmov.i16        q15, #1
  .endif

        vadd.i16        d18, d18, d24           @ t3 += temp3
        vsub.i16        d19, d19, d25           @ t4 -= temp4

        vswp            d22, d23                @ q11 = t7|t8

        vneg.s16        d17, d17                @ t2 = -t2
        vmla.i16        q9,  q1,  d0[2]         @ t3|t4 += 15 * src[56]|src[40]
        vmla.i16        q8,  q3,  d0[2]         @ t1|t2 += 15 * src[24]|src[8]

        @ At this point:
        @   t1  d16
        @   t2  d17
        @   t3  d18
        @   t4  d19
        @   t5  d20
        @   t6  d21
        @   t7  d22
        @   t8  d23
        @   #1  q15

  .if \add1beforeshift
        vadd.i16        q3,  q15, q10           @ line[7,6] = t5|t6 + 1
        vadd.i16        q2,  q15, q11           @ line[5,4] = t7|t8 + 1
  .endif

        @ Sometimes this overflows, so to get one additional bit of precision, use
        @ a single instruction that both adds and shifts right (halving).
        vhadd.s16       q1,  q9,  q11           @ line[2,3] = (t3|t4 + t7|t8) >> 1
        vhadd.s16       q0,  q8,  q10           @ line[0,1] = (t1|t2 + t5|t6) >> 1
  .if \add1beforeshift
        vhsub.s16       q2,  q2,  q9            @ line[5,4] = (t7|t8 - t3|t4 + 1) >> 1
        vhsub.s16       q3,  q3,  q8            @ line[7,6] = (t5|t6 - t1|t2 + 1) >> 1
  .else
        vhsub.s16       q2,  q11, q9            @ line[5,4] = (t7|t8 - t3|t4) >> 1
        vhsub.s16       q3,  q10, q8            @ line[7,6] = (t5|t6 - t1|t2) >> 1
  .endif

        vshr.s16        q9,  q1,  #(\rshift - 1)    @ one shift is already done by vhadd/vhsub above
        vshr.s16        q8,  q0,  #(\rshift - 1)
        vshr.s16        q10, q2,  #(\rshift - 1)
        vshr.s16        q11, q3,  #(\rshift - 1)

        @ At this point:
        @   dst[0]   d16
        @   dst[1]   d17
        @   dst[2]   d18
        @   dst[3]   d19
        @   dst[4]   d21
        @   dst[5]   d20
        @   dst[6]   d23
        @   dst[7]   d22
.endm

@ This is modeled after the first and second for loop in vc1_inv_trans_8x8_c.
@ Input columns:  q8, q9, q10, q11, q12, q13, q14, q15
@ Output columns: q8, q9, q10, q11, q12, q13, q14, q15
@ Trashes all NEON registers (and r12) except for: q4 q5 q6 q7
.macro vc1_inv_trans_8x8_helper add add1beforeshift rshift
        @ This actually computes half of t1, t2, t3, t4, as explained below
        @ near `tNhalf`.
        vmov.i16        q0,    #(6 / 2)         @ q0 = #6/2
        vshl.i16        q1,    q10, #3          @ t3 = 16/2 * src[16]
        vshl.i16        q3,    q14, #3          @ temp4 = 16/2 * src[48]
        vmul.i16        q2,    q10, q0          @ t4 = 6/2 * src[16]
        vmla.i16        q1,    q14, q0          @ t3 += 6/2 * src[48]
        @ unused: q0, q10, q14
        vmov.i16        q0,    #(12 / 2)        @ q0 = #12/2
        vadd.i16        q10,   q8,  q12         @ temp1 = src[0] + src[32]
        vsub.i16        q14,   q8,  q12         @ temp2 = src[0] - src[32]
        @ unused: q8, q12
        vmov.i16        q8,    #(\add / 2)      @ t1 will accumulate here
        vmov.i16        q12,   #(\add / 2)      @ t2 will accumulate here
        movw            r12,   #15
        vsub.i16        q2,    q2,  q3          @ t4 = 6/2 * src[16] - 16/2 * src[48]
        movt            r12,   #9
        @ unused: q3
        vmla.i16        q8,    q10, q0          @ t1 = 12/2 * temp1 + add
        vmla.i16        q12,   q14, q0          @ t2 = 12/2 * temp2 + add
        vmov.i32        d0[0], r12
        @ unused: q3, q10, q14

        @ At this point:
        @   q0          d0=#15|#9
        @   q1  old t3
        @   q2  old t4
        @   q3
        @   q8  old t1
        @   q9          src[8]
        @   q10
        @   q11         src[24]
        @   q12 old t2
        @   q13         src[40]
        @   q14
        @   q15         src[56]

        @ unused: q3, q10, q14
        movw            r12,   #16
        vshl.i16        q3,    q9,  #4          @ t1 = 16 * src[8]
        movt            r12,   #4
        vshl.i16        q10,   q9,  #2          @ t4 = 4 * src[8]
        vmov.i32        d1[0], r12
        vmul.i16        q14,   q9,  d0[0]       @ t2 = 15 * src[8]
        vmul.i16        q9,    q9,  d0[1]       @ t3 = 9 * src[8]
        @ unused: none
        vmla.i16        q3,    q11, d0[0]       @ t1 += 15 * src[24]
        vmls.i16        q10,   q11, d0[1]       @ t4 -= 9 * src[24]
        vmls.i16        q14,   q11, d1[1]       @ t2 -= 4 * src[24]
        vmls.i16        q9,    q11, d1[0]       @ t3 -= 16 * src[24]
        @ unused: q11
        vmla.i16        q3,    q13, d0[1]       @ t1 += 9 * src[40]
        vmla.i16        q10,   q13, d0[0]       @ t4 += 15 * src[40]
        vmls.i16        q14,   q13, d1[0]       @ t2 -= 16 * src[40]
        vmla.i16        q9,    q13, d1[1]       @ t3 += 4 * src[40]
        @ unused: q11, q13

        @ Compute t5, t6, t7, t8 from old t1, t2, t3, t4. Actually, it computes
        @ half of t5, t6, t7, t8 since t1, t2, t3, t4 are halved.
        vadd.i16        q11,   q8,  q1          @ t5 = t1 + t3
        vsub.i16        q1,    q8,  q1          @ t8 = t1 - t3
        vadd.i16        q13,   q12, q2          @ t6 = t2 + t4
        vsub.i16        q2,    q12, q2          @ t7 = t2 - t4
        @ unused: q8, q12

  .if \add1beforeshift
        vmov.i16        q12,   #1
  .endif

        @ unused: q8
        vmla.i16        q3,    q15, d1[1]       @ t1 += 4 * src[56]
        vmls.i16        q14,   q15, d0[1]       @ t2 -= 9 * src[56]
        vmla.i16        q9,    q15, d0[0]       @ t3 += 15 * src[56]
        vmls.i16        q10,   q15, d1[0]       @ t4 -= 16 * src[56]
        @ unused: q0, q8, q15

        @ At this point:
        @   t1      q3
        @   t2      q14
        @   t3      q9
        @   t4      q10
        @   t5half  q11
        @   t6half  q13
        @   t7half  q2
        @   t8half  q1
        @   #1      q12
        @
        @ tNhalf is half of the value of tN (as described in vc1_inv_trans_8x8_c).
        @ This is done because sometimes files have input that causes tN + tM to
        @ overflow. To avoid this overflow, we compute tNhalf, then compute
        @ tNhalf + tM (which doesn't overflow), and then we use vhadd to compute
        @ (tNhalf + (tNhalf + tM)) >> 1 which does not overflow because it is
        @ one instruction.

        @ For each pair of tN and tM, do:
        @   lineA = t5half + t1
        @   if add1beforeshift:  t1 -= 1
        @   lineA = (t5half + lineA) >> 1
        @   lineB = t5half - t1
        @   lineB = (t5half + lineB) >> 1
        @   lineA >>= rshift - 1
        @   lineB >>= rshift - 1

        vadd.i16        q8,  q11, q3                @ q8 = t5half + t1
  .if \add1beforeshift
        vsub.i16        q3,  q3,  q12               @ q3 = t1 - 1
  .endif

        vadd.i16        q0,  q13, q14               @ q0  = t6half + t2
  .if \add1beforeshift
        vsub.i16        q14, q14, q12               @ q14 = t2 - 1
  .endif

        vadd.i16        q15, q2,  q9                @ q15 = t7half + t3
  .if \add1beforeshift
        vsub.i16        q9,  q9,  q12               @ q9  = t3 - 1
  .endif
        @ unused: none

        vhadd.s16       q8,  q11, q8                @ q8  = (t5half + t5half + t1) >> 1
        vsub.i16        q3,  q11, q3                @ q3  = t5half - t1 + 1

        vhadd.s16       q0,  q13, q0                @ q0  = (t6half + t6half + t2) >> 1
        vsub.i16        q14, q13, q14               @ q14 = t6half - t2 + 1

        vhadd.s16       q15, q2,  q15               @ q15 = (t7half + t7half + t3) >> 1
        vsub.i16        q9,  q2,  q9                @ q9  = t7half - t3 + 1

        vhadd.s16       q3,  q11, q3                @ q3  = (t5half + t5half - t1 + 1) >> 1
        @ unused: q11

        vadd.i16        q11, q1,  q10               @ q11 = t8half + t4
  .if \add1beforeshift
        vsub.i16        q10, q10, q12               @ q10 = t4 - 1
  .endif
        @ unused: q12

        vhadd.s16       q14, q13, q14               @ q14 = (t6half + t6half - t2 + 1) >> 1
        @ unused: q12, q13
        vhadd.s16       q13, q2,  q9                @ q9  = (t7half + t7half - t3 + 1) >> 1
        @ unused: q12, q2, q9

        vsub.i16        q10, q1,  q10               @ q10 = t8half - t4 + 1
        vhadd.s16       q11, q1,  q11               @ q11 = (t8half + t8half + t4) >> 1

        vshr.s16        q8,  q8,  #(\rshift - 1)    @ q8  = line[0]
        vhadd.s16       q12, q1,  q10               @ q12 = (t8half + t8half - t4 + 1) >> 1
        vshr.s16        q9,  q0,  #(\rshift - 1)    @ q9  = line[1]
        vshr.s16        q10, q15, #(\rshift - 1)    @ q10 = line[2]
        vshr.s16        q11, q11, #(\rshift - 1)    @ q11 = line[3]
        vshr.s16        q12, q12, #(\rshift - 1)    @ q12 = line[4]
        vshr.s16        q13, q13, #(\rshift - 1)    @ q13 = line[5]
        vshr.s16        q14, q14, #(\rshift - 1)    @ q14 = line[6]
        vshr.s16        q15, q3,  #(\rshift - 1)    @ q15 = line[7]
.endm

@ (int16_t *block [r0])
function ff_vc1_inv_trans_8x8_neon, export=1
        vld1.64         {q8-q9},   [r0,:128]!
        vld1.64         {q10-q11}, [r0,:128]!
        vld1.64         {q12-q13}, [r0,:128]!
        vld1.64         {q14-q15}, [r0,:128]
        sub             r0, r0, #(16 * 2 * 3)   @ restore r0

        @ At this point:
        @   src[0]  q8
        @   src[8]  q9
        @   src[16] q10
        @   src[24] q11
        @   src[32] q12
        @   src[40] q13
        @   src[48] q14
        @   src[56] q15

        vc1_inv_trans_8x8_helper add=4 add1beforeshift=0 rshift=3

        @ Transpose result matrix of 8x8
        swap4           d17, d19, d21, d23, d24, d26, d28, d30
        transpose16_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15

        vc1_inv_trans_8x8_helper add=64 add1beforeshift=1 rshift=7

        vst1.64         {q8-q9},   [r0,:128]!
        vst1.64         {q10-q11}, [r0,:128]!
        vst1.64         {q12-q13}, [r0,:128]!
        vst1.64         {q14-q15}, [r0,:128]

        bx              lr
endfunc

@ (uint8_t *dest [r0], ptrdiff_t stride [r1], int16_t *block [r2])
function ff_vc1_inv_trans_8x4_neon, export=1
        vld1.64         {q0-q1}, [r2,:128]!     @ load 8 * 4 * 2 = 64 bytes / 16 bytes per quad = 4 quad registers
        vld1.64         {q2-q3}, [r2,:128]

        transpose16     q0 q1 q2 q3             @ transpose rows to columns

        @ At this point:
        @   src[0]   d0
        @   src[1]   d2
        @   src[2]   d4
        @   src[3]   d6
        @   src[4]   d1
        @   src[5]   d3
        @   src[6]   d5
        @   src[7]   d7

        vc1_inv_trans_8x4_helper    add=4 add1beforeshift=0 rshift=3

        @ Move output to more standardized registers
        vmov        d0, d16
        vmov        d2, d17
        vmov        d4, d18
        vmov        d6, d19
        vmov        d1, d21
        vmov        d3, d20
        vmov        d5, d23
        vmov        d7, d22

        @ At this point:
        @   dst[0]   d0
        @   dst[1]   d2
        @   dst[2]   d4
        @   dst[3]   d6
        @   dst[4]   d1
        @   dst[5]   d3
        @   dst[6]   d5
        @   dst[7]   d7

        transpose16     q0 q1 q2 q3   @ turn columns into rows

        @ At this point:
        @   row[0] q0
        @   row[1] q1
        @   row[2] q2
        @   row[3] q3

        vc1_inv_trans_4x8_helper    add=64 rshift=7

        @ At this point:
        @   line[0].l   d0
        @   line[0].h   d1
        @   line[1].l   d2
        @   line[1].h   d3
        @   line[2].l   d4
        @   line[2].h   d5
        @   line[3].l   d6
        @   line[3].h   d7

        @ unused registers: q12, q13, q14, q15

        vld1.64         {d28}, [r0,:64], r1     @ read dest
        vld1.64         {d29}, [r0,:64], r1
        vld1.64         {d30}, [r0,:64], r1
        vld1.64         {d31}, [r0,:64], r1
        sub             r0,  r0,  r1, lsl #2    @ restore original r0 value

        vaddw.u8        q0,  q0,  d28           @ line[0] += dest[0]
        vaddw.u8        q1,  q1,  d29           @ line[1] += dest[1]
        vaddw.u8        q2,  q2,  d30           @ line[2] += dest[2]
        vaddw.u8        q3,  q3,  d31           @ line[3] += dest[3]

        vqmovun.s16     d0,  q0                 @ line[0]
        vqmovun.s16     d1,  q1                 @ line[1]
        vqmovun.s16     d2,  q2                 @ line[2]
        vqmovun.s16     d3,  q3                 @ line[3]

        vst1.64         {d0},  [r0,:64], r1     @ write dest
        vst1.64         {d1},  [r0,:64], r1
        vst1.64         {d2},  [r0,:64], r1
        vst1.64         {d3},  [r0,:64]

        bx              lr
endfunc

@ (uint8_t *dest [r0], ptrdiff_t stride [r1], int16_t *block [r2])
function ff_vc1_inv_trans_4x8_neon, export=1
        mov             r12, #(8 * 2)  @ 8 elements per line, each element 2 bytes
        vld4.16         {d0[],  d2[],  d4[],  d6[]},  [r2,:64], r12     @ read each column into a q register
        vld4.16         {d0[1], d2[1], d4[1], d6[1]}, [r2,:64], r12
        vld4.16         {d0[2], d2[2], d4[2], d6[2]}, [r2,:64], r12
        vld4.16         {d0[3], d2[3], d4[3], d6[3]}, [r2,:64], r12
        vld4.16         {d1[],  d3[],  d5[],  d7[]},  [r2,:64], r12
        vld4.16         {d1[1], d3[1], d5[1], d7[1]}, [r2,:64], r12
        vld4.16         {d1[2], d3[2], d5[2], d7[2]}, [r2,:64], r12
        vld4.16         {d1[3], d3[3], d5[3], d7[3]}, [r2,:64]

        vc1_inv_trans_4x8_helper    add=4 rshift=3

        @ At this point:
        @   dst[0] = q0
        @   dst[1] = q1
        @   dst[2] = q2
        @   dst[3] = q3

        transpose16     q0 q1 q2 q3     @ Transpose rows (registers) into columns

        vc1_inv_trans_8x4_helper    add=64 add1beforeshift=1 rshift=7

        vld1.32         {d28[]},  [r0,:32], r1  @ read dest
        vld1.32         {d28[1]}, [r0,:32], r1
        vld1.32         {d29[]},  [r0,:32], r1
        vld1.32         {d29[1]}, [r0,:32], r1

        vld1.32         {d30[]},  [r0,:32], r1
        vld1.32         {d30[0]}, [r0,:32], r1
        vld1.32         {d31[]},  [r0,:32], r1
        vld1.32         {d31[0]}, [r0,:32], r1
        sub             r0,  r0,  r1, lsl #3    @ restore original r0 value

        vaddw.u8        q8,  q8,  d28           @ line[0,1] += dest[0,1]
        vaddw.u8        q9,  q9,  d29           @ line[2,3] += dest[2,3]
        vaddw.u8        q10, q10, d30           @ line[5,4] += dest[5,4]
        vaddw.u8        q11, q11, d31           @ line[7,6] += dest[7,6]

        vqmovun.s16     d16, q8                 @ clip(line[0,1])
        vqmovun.s16     d18, q9                 @ clip(line[2,3])
        vqmovun.s16     d20, q10                @ clip(line[5,4])
        vqmovun.s16     d22, q11                @ clip(line[7,6])

        vst1.32         {d16[0]}, [r0,:32], r1  @ write dest
        vst1.32         {d16[1]}, [r0,:32], r1
        vst1.32         {d18[0]}, [r0,:32], r1
        vst1.32         {d18[1]}, [r0,:32], r1

        vst1.32         {d20[1]}, [r0,:32], r1
        vst1.32         {d20[0]}, [r0,:32], r1
        vst1.32         {d22[1]}, [r0,:32], r1
        vst1.32         {d22[0]}, [r0,:32]

        bx              lr
endfunc

@ Setup constants in registers which are used by vc1_inv_trans_4x4_helper
.macro vc1_inv_trans_4x4_helper_setup
        vmov.i16        q13, #17
        vmov.i16        q14, #22
        vmov.i16        d30, #10                @ only need double-word, not quad-word
.endm

@ This is modeled after the first for loop in vc1_inv_trans_4x4_c.
.macro vc1_inv_trans_4x4_helper add rshift
        vmov.i16        q2,  #\add              @ t1|t2 will accumulate here

        vadd.i16        d16, d0,  d1            @ temp1 = src[0] + src[2]
        vsub.i16        d17, d0,  d1            @ temp2 = src[0] - src[2]
        vmul.i16        q3,  q14, q1            @ t3|t4 = 22 * (src[1]|src[3])
        vmla.i16        q2,  q13, q8            @ t1|t2 = 17 * (temp1|temp2) + add
        vmla.i16        d6,  d30, d3            @ t3 += 10 * src[3]
        vmls.i16        d7,  d30, d2            @ t4 -= 10 * src[1]

        vadd.i16        q0,  q2,  q3            @ dst[0,2] = (t1|t2 + t3|t4)
        vsub.i16        q1,  q2,  q3            @ dst[3,1] = (t1|t2 - t3|t4)
        vshr.s16        q0,  q0,  #\rshift      @ dst[0,2] >>= rshift
        vshr.s16        q1,  q1,  #\rshift      @ dst[3,1] >>= rshift
.endm

@ (uint8_t *dest [r0], ptrdiff_t stride [r1], int16_t *block [r2])
function ff_vc1_inv_trans_4x4_neon, export=1
        mov             r12, #(8 * 2)  @ 8 elements per line, each element 2 bytes
        vld4.16         {d0[],  d1[],  d2[],  d3[]},  [r2,:64], r12     @ read each column into a register
        vld4.16         {d0[1], d1[1], d2[1], d3[1]}, [r2,:64], r12
        vld4.16         {d0[2], d1[2], d2[2], d3[2]}, [r2,:64], r12
        vld4.16         {d0[3], d1[3], d2[3], d3[3]}, [r2,:64]

        vswp            d1,  d2         @ so that we can later access column 1 and column 3 as a single q1 register

        vc1_inv_trans_4x4_helper_setup

        @ At this point:
        @   src[0] = d0
        @   src[1] = d2
        @   src[2] = d1
        @   src[3] = d3

        vc1_inv_trans_4x4_helper add=4 rshift=3      @ compute t1, t2, t3, t4 and combine them into dst[0-3]

        @ At this point:
        @   dst[0] = d0
        @   dst[1] = d3
        @   dst[2] = d1
        @   dst[3] = d2

        transpose16     d0 d3 d1 d2     @ Transpose rows (registers) into columns

        @ At this point:
        @   src[0]  = d0
        @   src[8]  = d3
        @   src[16] = d1
        @   src[24] = d2

        vswp            d2,  d3         @ so that we can later access column 1 and column 3 in order as a single q1 register

        @ At this point:
        @   src[0]  = d0
        @   src[8]  = d2
        @   src[16] = d1
        @   src[24] = d3

        vc1_inv_trans_4x4_helper add=64 rshift=7              @ compute t1, t2, t3, t4 and combine them into dst[0-3]

        @ At this point:
        @   line[0] = d0
        @   line[1] = d3
        @   line[2] = d1
        @   line[3] = d2

        vld1.32         {d18[]},  [r0,:32], r1  @ read dest
        vld1.32         {d19[]},  [r0,:32], r1
        vld1.32         {d18[1]}, [r0,:32], r1
        vld1.32         {d19[0]}, [r0,:32], r1
        sub             r0,  r0,  r1, lsl #2    @ restore original r0 value

        vaddw.u8        q0,  q0,  d18           @ line[0,2] += dest[0,2]
        vaddw.u8        q1,  q1,  d19           @ line[3,1] += dest[3,1]

        vqmovun.s16     d0,  q0                 @ clip(line[0,2])
        vqmovun.s16     d1,  q1                 @ clip(line[3,1])

        vst1.32         {d0[0]},  [r0,:32], r1  @ write dest
        vst1.32         {d1[1]},  [r0,:32], r1
        vst1.32         {d0[1]},  [r0,:32], r1
        vst1.32         {d1[0]},  [r0,:32]

        bx              lr
endfunc

#if HAVE_AS_DN_DIRECTIVE
@ The absolute value of multiplication constants from vc1_mspel_filter and vc1_mspel_{ver,hor}_filter_16bits.
@ The sign is embedded in the code below that carries out the multiplication (mspel_filter{,.16}).
#define MSPEL_MODE_1_MUL_CONSTANTS  4 53 18 3
#define MSPEL_MODE_2_MUL_CONSTANTS  1 9  9  1
#define MSPEL_MODE_3_MUL_CONSTANTS  3 18 53 4

@ These constants are from reading the source code of vc1_mspel_mc and determining the value that
@ is added to `rnd` to result in the variable `r`, and the value of the variable `shift`.
#define MSPEL_MODES_11_ADDSHIFT_CONSTANTS   15 5
#define MSPEL_MODES_12_ADDSHIFT_CONSTANTS   3  3
#define MSPEL_MODES_13_ADDSHIFT_CONSTANTS   15 5
#define MSPEL_MODES_21_ADDSHIFT_CONSTANTS   MSPEL_MODES_12_ADDSHIFT_CONSTANTS
#define MSPEL_MODES_22_ADDSHIFT_CONSTANTS   0  1
#define MSPEL_MODES_23_ADDSHIFT_CONSTANTS   3  3
#define MSPEL_MODES_31_ADDSHIFT_CONSTANTS   MSPEL_MODES_13_ADDSHIFT_CONSTANTS
#define MSPEL_MODES_32_ADDSHIFT_CONSTANTS   MSPEL_MODES_23_ADDSHIFT_CONSTANTS
#define MSPEL_MODES_33_ADDSHIFT_CONSTANTS   15 5

@ The addition and shift constants from vc1_mspel_filter.
#define MSPEL_MODE_1_ADDSHIFT_CONSTANTS     32 6
#define MSPEL_MODE_2_ADDSHIFT_CONSTANTS     8  4
#define MSPEL_MODE_3_ADDSHIFT_CONSTANTS     32 6

@ Setup constants in registers for a subsequent use of mspel_filter{,.16}.
.macro mspel_constants typesize reg_a reg_b reg_c reg_d filter_a filter_b filter_c filter_d reg_add filter_add_register
  @ Define double-word register aliases. Typesize should be i8 or i16.
  ra .dn \reg_a\().\typesize
  rb .dn \reg_b\().\typesize
  rc .dn \reg_c\().\typesize
  rd .dn \reg_d\().\typesize

  @ Only set the register if the value is not 1 and unique
  .if \filter_a != 1
        vmov            ra,  #\filter_a              @ ra = filter_a
  .endif
        vmov            rb,  #\filter_b              @ rb = filter_b
  .if \filter_b != \filter_c
        vmov            rc,  #\filter_c              @ rc = filter_c
  .endif
  .if \filter_d != 1
        vmov            rd,  #\filter_d              @ rd = filter_d
  .endif
  @ vdup to double the size of typesize
  .ifc \typesize,i8
        vdup.16         \reg_add,  \filter_add_register     @ reg_add = filter_add_register
  .else
        vdup.32         \reg_add,  \filter_add_register     @ reg_add = filter_add_register
  .endif

  .unreq ra
  .unreq rb
  .unreq rc
  .unreq rd
.endm

@ After mspel_constants has been used, do the filtering.
.macro mspel_filter acc dest src0 src1 src2 src3 filter_a filter_b filter_c filter_d reg_a reg_b reg_c reg_d reg_add filter_shift narrow=1
  .if \filter_a != 1
        @ If filter_a != 1, then we need a move and subtract instruction
        vmov            \acc,  \reg_add                     @ acc = reg_add
        vmlsl.u8        \acc,  \reg_a,  \src0               @ acc -= filter_a * src[-stride]
  .else
        @ If filter_a is 1, then just subtract without an extra move
        vsubw.u8        \acc,  \reg_add,  \src0             @ acc = reg_add - src[-stride]      @ since filter_a == 1
  .endif
        vmlal.u8        \acc,  \reg_b,  \src1               @ acc += filter_b * src[0]
  .if \filter_b != \filter_c
        vmlal.u8        \acc,  \reg_c,  \src2               @ acc += filter_c * src[stride]
  .else
        @ If filter_b is the same as filter_c, use the same reg_b register
        vmlal.u8        \acc,  \reg_b,  \src2               @ acc += filter_c * src[stride]     @ where filter_c == filter_b
  .endif
  .if \filter_d != 1
        @ If filter_d != 1, then do a multiply accumulate
        vmlsl.u8        \acc,  \reg_d,  \src3               @ acc -= filter_d * src[stride * 2]
  .else
        @ If filter_d is 1, then just do a subtract
        vsubw.u8        \acc,  \acc,    \src3               @ acc -= src[stride * 2]            @ since filter_d == 1
  .endif
  .if \narrow
        vqshrun.s16     \dest, \acc,    #\filter_shift      @ dest = clip_uint8(acc >> filter_shift)
  .else
        vshr.s16        \dest, \acc,    #\filter_shift      @ dest = acc >> filter_shift
  .endif
.endm

@ This is similar to mspel_filter, but the input is 16-bit instead of 8-bit and narrow=0 is not supported.
.macro mspel_filter.16 acc0 acc1 acc0_0 acc0_1 dest src0 src1 src2 src3 src4 src5 src6 src7 filter_a filter_b filter_c filter_d reg_a reg_b reg_c reg_d reg_add filter_shift
  .if \filter_a != 1
        vmov            \acc0,  \reg_add
        vmov            \acc1,  \reg_add
        vmlsl.s16       \acc0,  \reg_a,  \src0
        vmlsl.s16       \acc1,  \reg_a,  \src1
  .else
        vsubw.s16       \acc0,  \reg_add,  \src0
        vsubw.s16       \acc1,  \reg_add,  \src1
  .endif
        vmlal.s16       \acc0,  \reg_b,  \src2
        vmlal.s16       \acc1,  \reg_b,  \src3
  .if \filter_b != \filter_c
        vmlal.s16       \acc0,  \reg_c,  \src4
        vmlal.s16       \acc1,  \reg_c,  \src5
  .else
        vmlal.s16       \acc0,  \reg_b,  \src4
        vmlal.s16       \acc1,  \reg_b,  \src5
  .endif
  .if \filter_d != 1
        vmlsl.s16       \acc0,  \reg_d,  \src6
        vmlsl.s16       \acc1,  \reg_d,  \src7
  .else
        vsubw.s16       \acc0,  \acc0,   \src6
        vsubw.s16       \acc1,  \acc1,   \src7
  .endif
        @ Use acc0_0 and acc0_1 as temp space
        vqshrun.s32     \acc0_0, \acc0,  #\filter_shift     @ Shift and narrow with saturation from s32 to u16
        vqshrun.s32     \acc0_1, \acc1,  #\filter_shift
        vqmovn.u16      \dest,  \acc0                       @ Narrow with saturation from u16 to u8
.endm

@ Register usage for put_vc1_mspel_mc functions. Registers marked 'hv' are only used in put_vc1_mspel_mc_hv.
@
@   r0        adjusted dst
@   r1        adjusted src
@   r2        stride
@   r3        adjusted rnd
@   r4 [hv]   tmp
@   r11 [hv]  sp saved
@   r12       loop counter
@   d0        src[-stride]
@   d1        src[0]
@   d2        src[stride]
@   d3        src[stride * 2]
@   q0 [hv]   src[-stride]
@   q1 [hv]   src[0]
@   q2 [hv]   src[stride]
@   q3 [hv]   src[stride * 2]
@   d21       often result from mspel_filter
@   q11       accumulator 0
@   q12 [hv]  accumulator 1
@   q13       accumulator initial value
@   d28       filter_a
@   d29       filter_b
@   d30       filter_c
@   d31       filter_d

@ (uint8_t *dst [r0], const uint8_t *src [r1], ptrdiff_t stride [r2], int rnd [r3])
.macro put_vc1_mspel_mc_hv hmode vmode filter_h_a filter_h_b filter_h_c filter_h_d filter_v_a filter_v_b filter_v_c filter_v_d filter_add filter_shift
function ff_put_vc1_mspel_mc\hmode\()\vmode\()_neon, export=1
        push            {r4, r11, lr}
        mov             r11, sp                 @ r11 = stack pointer before realignmnet
A       bic             sp,  sp,  #15           @ sp = round down to multiple of 16 bytes
T       bic             r4,  r11, #15
T       mov             sp,  r4
        sub             sp,  sp,  #(8*2*16)     @ make space for 8 rows * 2 byte per element * 16 elements per row (to fit 11 actual elements per row)
        mov             r4,  sp                 @ r4 = int16_t tmp[8 * 16]

        sub             r1,  r1,  #1            @ src -= 1
  .if \filter_add != 0
        add             r3,  r3,  #\filter_add  @ r3 = filter_add + rnd
  .endif
        mov             r12, #8                 @ loop counter
        sub             r1,  r1,  r2            @ r1 = &src[-stride]      @ slide back

        @ Do vertical filtering from src into tmp
        mspel_constants i8 d28 d29 d30 d31 \filter_v_a \filter_v_b \filter_v_c \filter_v_d q13 r3

        vld1.64         {d0,d1}, [r1], r2
        vld1.64         {d2,d3}, [r1], r2
        vld1.64         {d4,d5}, [r1], r2

1:
        subs            r12,  r12,  #4

        vld1.64         {d6,d7}, [r1], r2
        mspel_filter    q11 q11 d0 d2 d4 d6 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        mspel_filter    q12 q12 d1 d3 d5 d7 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        vst1.64         {q11,q12}, [r4,:128]!   @ store and increment

        vld1.64         {d0,d1}, [r1], r2
        mspel_filter    q11 q11 d2 d4 d6 d0 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        mspel_filter    q12 q12 d3 d5 d7 d1 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        vst1.64         {q11,q12}, [r4,:128]!   @ store and increment

        vld1.64         {d2,d3}, [r1], r2
        mspel_filter    q11 q11 d4 d6 d0 d2 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        mspel_filter    q12 q12 d5 d7 d1 d3 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        vst1.64         {q11,q12}, [r4,:128]!   @ store and increment

        vld1.64         {d4,d5}, [r1], r2
        mspel_filter    q11 q11 d6 d0 d2 d4 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        mspel_filter    q12 q12 d7 d1 d3 d5 \filter_v_a \filter_v_b \filter_v_c \filter_v_d d28 d29 d30 d31 q13 \filter_shift narrow=0
        vst1.64         {q11,q12}, [r4,:128]!   @ store and increment

        bne             1b

        rsb             r3,   r3,  #(64 + \filter_add)      @ r3 = (64 + filter_add) - r3
        mov             r12,  #8                @ loop counter
        mov             r4,   sp                @ r4 = tmp

        @ Do horizontal filtering from temp to dst
        mspel_constants i16 d28 d29 d30 d31 \filter_h_a \filter_h_b \filter_h_c \filter_h_d q13 r3

2:
        subs            r12,  r12,  #1

        vld1.64         {q0,q1}, [r4,:128]!     @ read one line of tmp
        vext.16         q2,   q0,   q1,  #2
        vext.16         q3,   q0,   q1,  #3
        vext.16         q1,   q0,   q1,  #1     @ do last because it writes to q1 which is read by the other vext instructions

        mspel_filter.16 q11 q12 d22 d23 d21 d0 d1 d2 d3 d4 d5 d6 d7 \filter_h_a \filter_h_b \filter_h_c \filter_h_d d28 d29 d30 d31 q13 7

        vst1.64         {d21}, [r0,:64], r2     @ store and increment dst

        bne             2b

        mov             sp,  r11
        pop             {r4, r11, pc}
endfunc
.endm

@ Use C preprocessor and assembler macros to expand to functions for horizontal and vertical filtering.
#define PUT_VC1_MSPEL_MC_HV(hmode, vmode)   \
    put_vc1_mspel_mc_hv hmode vmode \
        MSPEL_MODE_ ## hmode ## _MUL_CONSTANTS \
        MSPEL_MODE_ ## vmode ## _MUL_CONSTANTS \
        MSPEL_MODES_ ## hmode ## vmode ## _ADDSHIFT_CONSTANTS

PUT_VC1_MSPEL_MC_HV(1, 1)
PUT_VC1_MSPEL_MC_HV(1, 2)
PUT_VC1_MSPEL_MC_HV(1, 3)
PUT_VC1_MSPEL_MC_HV(2, 1)
PUT_VC1_MSPEL_MC_HV(2, 2)
PUT_VC1_MSPEL_MC_HV(2, 3)
PUT_VC1_MSPEL_MC_HV(3, 1)
PUT_VC1_MSPEL_MC_HV(3, 2)
PUT_VC1_MSPEL_MC_HV(3, 3)

#undef PUT_VC1_MSPEL_MC_HV

.macro  put_vc1_mspel_mc_h_only hmode filter_a filter_b filter_c filter_d filter_add filter_shift
function ff_put_vc1_mspel_mc\hmode\()0_neon, export=1
        rsb             r3,   r3,   #\filter_add        @ r3 = filter_add - r = filter_add - rnd
        mov             r12,  #8                        @ loop counter
        sub             r1,   r1,   #1                  @ slide back, using immediate

        mspel_constants i8 d28 d29 d30 d31 \filter_a \filter_b \filter_c \filter_d q13 r3

1:
        subs            r12,  r12,  #1

        vld1.64         {d0,d1}, [r1], r2               @ read 16 bytes even though we only need 11, also src += stride
        vext.8          d2,   d0,   d1,  #2
        vext.8          d3,   d0,   d1,  #3
        vext.8          d1,   d0,   d1,  #1             @ do last because it writes to d1 which is read by the other vext instructions

        mspel_filter    q11 d21 d0 d1 d2 d3 \filter_a \filter_b \filter_c \filter_d d28 d29 d30 d31 q13 \filter_shift

        vst1.64         {d21}, [r0,:64], r2             @ store and increment dst

        bne             1b

        bx              lr
endfunc
.endm

@ Use C preprocessor and assembler macros to expand to functions for horizontal only filtering.
#define PUT_VC1_MSPEL_MC_H_ONLY(hmode) \
        put_vc1_mspel_mc_h_only hmode MSPEL_MODE_ ## hmode ## _MUL_CONSTANTS MSPEL_MODE_ ## hmode ## _ADDSHIFT_CONSTANTS

PUT_VC1_MSPEL_MC_H_ONLY(1)
PUT_VC1_MSPEL_MC_H_ONLY(2)
PUT_VC1_MSPEL_MC_H_ONLY(3)

#undef PUT_VC1_MSPEL_MC_H_ONLY

@ (uint8_t *dst [r0], const uint8_t *src [r1], ptrdiff_t stride [r2], int rnd [r3])
.macro put_vc1_mspel_mc_v_only vmode filter_a filter_b filter_c filter_d filter_add filter_shift
function ff_put_vc1_mspel_mc0\vmode\()_neon, export=1
        add             r3,   r3,   #\filter_add - 1    @ r3 = filter_add - r = filter_add - (1 - rnd) = filter_add - 1 + rnd
        mov             r12,  #8                        @ loop counter
        sub             r1,   r1,   r2                  @ r1 = &src[-stride]      @ slide back

        mspel_constants i8 d28 d29 d30 d31 \filter_a \filter_b \filter_c \filter_d q13 r3

        vld1.64         {d0},  [r1], r2                 @ d0 = src[-stride]
        vld1.64         {d1},  [r1], r2                 @ d1 = src[0]
        vld1.64         {d2},  [r1], r2                 @ d2 = src[stride]

1:
        subs            r12,  r12,  #4

        vld1.64         {d3},  [r1], r2                 @ d3 = src[stride * 2]
        mspel_filter    q11 d21 d0 d1 d2 d3 \filter_a \filter_b \filter_c \filter_d d28 d29 d30 d31 q13 \filter_shift
        vst1.64         {d21}, [r0,:64], r2             @ store and increment dst

        vld1.64         {d0},  [r1], r2                 @ d0 = next line
        mspel_filter    q11 d21 d1 d2 d3 d0 \filter_a \filter_b \filter_c \filter_d d28 d29 d30 d31 q13 \filter_shift
        vst1.64         {d21}, [r0,:64], r2             @ store and increment dst

        vld1.64         {d1},  [r1], r2                 @ d1 = next line
        mspel_filter    q11 d21 d2 d3 d0 d1 \filter_a \filter_b \filter_c \filter_d d28 d29 d30 d31 q13 \filter_shift
        vst1.64         {d21}, [r0,:64], r2             @ store and increment dst

        vld1.64         {d2},  [r1], r2                 @ d2 = next line
        mspel_filter    q11 d21 d3 d0 d1 d2 \filter_a \filter_b \filter_c \filter_d d28 d29 d30 d31 q13 \filter_shift
        vst1.64         {d21}, [r0,:64], r2             @ store and increment dst

        bne             1b

        bx              lr
endfunc
.endm

@ Use C preprocessor and assembler macros to expand to functions for vertical only filtering.
#define PUT_VC1_MSPEL_MC_V_ONLY(vmode) \
        put_vc1_mspel_mc_v_only vmode MSPEL_MODE_ ## vmode ## _MUL_CONSTANTS MSPEL_MODE_ ## vmode ## _ADDSHIFT_CONSTANTS

PUT_VC1_MSPEL_MC_V_ONLY(1)
PUT_VC1_MSPEL_MC_V_ONLY(2)
PUT_VC1_MSPEL_MC_V_ONLY(3)

#undef PUT_VC1_MSPEL_MC_V_ONLY
#endif

function ff_put_pixels8x8_neon, export=1
        vld1.64         {d0}, [r1], r2
        vld1.64         {d1}, [r1], r2
        vld1.64         {d2}, [r1], r2
        vld1.64         {d3}, [r1], r2
        vld1.64         {d4}, [r1], r2
        vld1.64         {d5}, [r1], r2
        vld1.64         {d6}, [r1], r2
        vld1.64         {d7}, [r1]
        vst1.64         {d0}, [r0,:64], r2
        vst1.64         {d1}, [r0,:64], r2
        vst1.64         {d2}, [r0,:64], r2
        vst1.64         {d3}, [r0,:64], r2
        vst1.64         {d4}, [r0,:64], r2
        vst1.64         {d5}, [r0,:64], r2
        vst1.64         {d6}, [r0,:64], r2
        vst1.64         {d7}, [r0,:64]
        bx              lr
endfunc

function ff_vc1_inv_trans_8x8_dc_neon, export=1
        ldrsh           r2, [r2]              @ int dc = block[0];

        vld1.64         {d0},  [r0,:64], r1
        vld1.64         {d1},  [r0,:64], r1
        vld1.64         {d4},  [r0,:64], r1
        vld1.64         {d5},  [r0,:64], r1

        add             r2, r2, r2, lsl #1    @ dc = (3 * dc +  1) >> 1;
        vld1.64         {d6},  [r0,:64], r1
        add             r2, r2, #1
        vld1.64         {d7},  [r0,:64], r1
        vld1.64         {d16}, [r0,:64], r1
        vld1.64         {d17}, [r0,:64], r1
        asr             r2, r2, #1

        sub             r0,  r0,  r1, lsl #3  @ restore r0 to original value

        add             r2, r2, r2, lsl #1    @ dc = (3 * dc + 16) >> 5;
        add             r2, r2, #16
        asr             r2, r2, #5

        vdup.16         q1,  r2               @ dc

        vaddw.u8        q9,   q1,  d0
        vaddw.u8        q10,  q1,  d1
        vaddw.u8        q11,  q1,  d4
        vaddw.u8        q12,  q1,  d5
        vqmovun.s16     d0,  q9
        vqmovun.s16     d1,  q10
        vqmovun.s16     d4,  q11
        vst1.64         {d0},  [r0,:64], r1
        vqmovun.s16     d5,  q12
        vst1.64         {d1},  [r0,:64], r1
        vaddw.u8        q13,  q1,  d6
        vst1.64         {d4},  [r0,:64], r1
        vaddw.u8        q14,  q1,  d7
        vst1.64         {d5},  [r0,:64], r1
        vaddw.u8        q15,  q1,  d16
        vaddw.u8        q1,   q1,  d17        @ this destroys q1
        vqmovun.s16     d6,  q13
        vqmovun.s16     d7,  q14
        vqmovun.s16     d16, q15
        vqmovun.s16     d17, q1
        vst1.64         {d6},  [r0,:64], r1
        vst1.64         {d7},  [r0,:64], r1
        vst1.64         {d16}, [r0,:64], r1
        vst1.64         {d17}, [r0,:64]
        bx              lr
endfunc

function ff_vc1_inv_trans_8x4_dc_neon, export=1
        ldrsh           r2, [r2]              @ int dc = block[0];

        vld1.64         {d0},  [r0,:64], r1
        vld1.64         {d1},  [r0,:64], r1
        vld1.64         {d4},  [r0,:64], r1
        vld1.64         {d5},  [r0,:64], r1

        add             r2, r2, r2, lsl #1    @ dc = ( 3 * dc +  1) >> 1;

        sub             r0,  r0,  r1, lsl #2  @ restore r0 to original value

        add             r2, r2, #1
        asr             r2, r2, #1

        add             r2, r2, r2, lsl #4    @ dc = (17 * dc + 64) >> 7;
        add             r2, r2, #64
        asr             r2, r2, #7

        vdup.16         q1,  r2               @ dc

        vaddw.u8        q3,  q1,  d0
        vaddw.u8        q8,  q1,  d1
        vaddw.u8        q9,  q1,  d4
        vaddw.u8        q10, q1,  d5
        vqmovun.s16     d0,  q3
        vqmovun.s16     d1,  q8
        vqmovun.s16     d4,  q9
        vst1.64         {d0},  [r0,:64], r1
        vqmovun.s16     d5,  q10
        vst1.64         {d1},  [r0,:64], r1
        vst1.64         {d4},  [r0,:64], r1
        vst1.64         {d5},  [r0,:64]
        bx              lr
endfunc

function ff_vc1_inv_trans_4x8_dc_neon, export=1
        ldrsh           r2, [r2]              @ int dc = block[0];

        vld1.32         {d0[]},   [r0,:32], r1
        vld1.32         {d1[]},   [r0,:32], r1
        vld1.32         {d0[1]},  [r0,:32], r1
        vld1.32         {d1[1]},  [r0,:32], r1

        add             r2, r2, r2, lsl #4    @ dc = (17 * dc +  4) >> 3;
        vld1.32         {d4[]},   [r0,:32], r1
        add             r2, r2, #4
        vld1.32         {d5[]},   [r0,:32], r1
        vld1.32         {d4[1]},  [r0,:32], r1
        asr             r2, r2, #3
        vld1.32         {d5[1]},  [r0,:32], r1

        add             r2, r2, r2, lsl #1    @ dc = (12 * dc + 64) >> 7;

        sub             r0,  r0,  r1, lsl #3  @ restore r0 to original value

        lsl             r2, r2, #2
        add             r2, r2, #64
        asr             r2, r2, #7

        vdup.16         q1,  r2               @ dc

        vaddw.u8        q3,  q1,  d0
        vaddw.u8        q8,  q1,  d1
        vaddw.u8        q9,  q1,  d4
        vaddw.u8        q10, q1,  d5
        vqmovun.s16     d0,  q3
        vst1.32         {d0[0]},  [r0,:32], r1
        vqmovun.s16     d1,  q8
        vst1.32         {d1[0]},  [r0,:32], r1
        vqmovun.s16     d4,  q9
        vst1.32         {d0[1]},  [r0,:32], r1
        vqmovun.s16     d5,  q10
        vst1.32         {d1[1]},  [r0,:32], r1
        vst1.32         {d4[0]},  [r0,:32], r1
        vst1.32         {d5[0]},  [r0,:32], r1
        vst1.32         {d4[1]},  [r0,:32], r1
        vst1.32         {d5[1]},  [r0,:32]
        bx              lr
endfunc

function ff_vc1_inv_trans_4x4_dc_neon, export=1
        ldrsh           r2, [r2]              @ int dc = block[0];

        vld1.32         {d0[]},   [r0,:32], r1
        vld1.32         {d1[]},   [r0,:32], r1
        vld1.32         {d0[1]},  [r0,:32], r1
        vld1.32         {d1[1]},  [r0,:32], r1

        add             r2, r2, r2, lsl #4    @ dc = (17 * dc +  4) >> 3;

        sub             r0,  r0,  r1, lsl #2  @ restore r0 to original value

        add             r2, r2, #4
        asr             r2, r2, #3

        add             r2, r2, r2, lsl #4    @ dc = (17 * dc + 64) >> 7;
        add             r2, r2, #64
        asr             r2, r2, #7

        vdup.16         q1,  r2               @ dc

        vaddw.u8        q2,  q1,  d0
        vaddw.u8        q3,  q1,  d1
        vqmovun.s16     d0,  q2
        vst1.32         {d0[0]},  [r0,:32], r1
        vqmovun.s16     d1,  q3
        vst1.32         {d1[0]},  [r0,:32], r1
        vst1.32         {d0[1]},  [r0,:32], r1
        vst1.32         {d1[1]},  [r0,:32]
        bx              lr
endfunc
