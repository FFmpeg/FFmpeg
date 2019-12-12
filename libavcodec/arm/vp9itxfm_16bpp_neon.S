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
#include "neon.S"

const itxfm4_coeffs, align=4
        .short  11585, 0, 6270, 15137
iadst4_coeffs:
        .short  5283, 15212, 9929, 13377
endconst

const iadst8_coeffs, align=4
        .short  16305, 1606, 14449, 7723, 10394, 12665, 4756, 15679
idct_coeffs:
        .short  11585, 0, 6270, 15137, 3196, 16069, 13623, 9102
        .short  1606, 16305, 12665, 10394, 7723, 14449, 15679, 4756
        .short  804, 16364, 12140, 11003, 7005, 14811, 15426, 5520
        .short  3981, 15893, 14053, 8423, 9760, 13160, 16207, 2404
endconst

const iadst16_coeffs, align=4
        .short  16364, 804, 15893, 3981, 11003, 12140, 8423, 14053
        .short  14811, 7005, 13160, 9760, 5520, 15426, 2404, 16207
endconst

@ Do two 4x4 transposes, using q registers for the subtransposes that don't
@ need to address the individual d registers.
@ r0,r1 == rq1, r2,r3 == rq1, etc
.macro transpose32_q_2x_4x4 rq0, rq1, rq2, rq3, rq4, rq5, rq6, rq7, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15
        vswp             \r1,  \r4  @ vtrn.64 \rq0, \rq2
        vswp             \r3,  \r6  @ vtrn.64 \rq1, \rq3
        vswp             \r9,  \r12 @ vtrn.64 \rq4, \rq6
        vswp             \r11, \r14 @ vtrn.64 \rq5, \rq7
        vtrn.32          \rq0, \rq1
        vtrn.32          \rq2, \rq3
        vtrn.32          \rq4, \rq5
        vtrn.32          \rq6, \rq7
.endm

@ Do eight 2x2 transposes.
.macro transpose32_8x_2x2 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15
        vtrn.32          \r0,  \r1
        vtrn.32          \r2,  \r3
        vtrn.32          \r4,  \r5
        vtrn.32          \r6,  \r7
        vtrn.32          \r8,  \r9
        vtrn.32          \r10, \r11
        vtrn.32          \r12, \r13
        vtrn.32          \r14, \r15
.endm

@ out1 = ((in1 + in2) * d0[0] + (1 << 13)) >> 14
@ out2 = ((in1 - in2) * d0[0] + (1 << 13)) >> 14
@ in/out are d registers
.macro mbutterfly0 out1, out2, in1, in2, tmpd1, tmpd2, tmpq3, tmpq4, neg=0
        vadd.s32        \tmpd1, \in1,  \in2
        vsub.s32        \tmpd2, \in1,  \in2
.if \neg > 0
        vneg.s32        \tmpd1, \tmpd1
.endif
        vmull.s32       \tmpq3, \tmpd1, d0[0]
        vmull.s32       \tmpq4, \tmpd2, d0[0]
        vrshrn.s64      \out1, \tmpq3, #14
        vrshrn.s64      \out2, \tmpq4, #14
.endm

@ Same as mbutterfly0 above, but treating the input in in2 as zero,
@ writing the same output into both out1 and out2.
.macro mbutterfly0_h out1, out2, in1, in2, tmpd1, tmpd2, tmpq3, tmpq4
        vmull.s32       \tmpq3, \in1, d0[0]
        vrshrn.s64      \out1, \tmpq3, #14
        vrshrn.s64      \out2, \tmpq3, #14
.endm

@ out1,out2 = ((in1 + in2) * d0[0] + (1 << 13)) >> 14
@ out3,out4 = ((in1 - in2) * d0[0] + (1 << 13)) >> 14
@ Same as mbutterfly0, but with input being 2 q registers, output
@ being 4 d registers.
@ This can do with either 4 or 6 temporary q registers.
.macro dmbutterfly0 out1, out2, out3, out4, in1, in2, tmpq1, tmpq2, tmpd11, tmpd12, tmpd21, tmpd22, tmpq3, tmpq4, tmpq5, tmpq6
        vadd.s32        \tmpq1, \in1,  \in2
        vsub.s32        \tmpq2, \in1,  \in2
        vmull.s32       \tmpq3, \tmpd11, d0[0]
        vmull.s32       \tmpq4, \tmpd12, d0[0]
.ifb \tmpq5
        vrshrn.s64      \out1, \tmpq3, #14
        vrshrn.s64      \out2, \tmpq4, #14
        vmull.s32       \tmpq3, \tmpd21, d0[0]
        vmull.s32       \tmpq4, \tmpd22, d0[0]
        vrshrn.s64      \out3, \tmpq3, #14
        vrshrn.s64      \out4, \tmpq4, #14
.else
        vmull.s32       \tmpq5, \tmpd21, d0[0]
        vmull.s32       \tmpq6, \tmpd22, d0[0]
        vrshrn.s64      \out1, \tmpq3, #14
        vrshrn.s64      \out2, \tmpq4, #14
        vrshrn.s64      \out3, \tmpq5, #14
        vrshrn.s64      \out4, \tmpq6, #14
.endif
.endm

@ out1 = in1 * coef1 - in2 * coef2
@ out2 = in1 * coef2 + in2 * coef1
@ out are 2 q registers, in are 2 d registers
.macro mbutterfly_l out1, out2, in1, in2, coef1, coef2, neg=0
        vmull.s32       \out1, \in1, \coef1
        vmlsl.s32       \out1, \in2, \coef2
.if \neg
        vmov.s64        \out2, #0
        vmlsl.s32       \out2, \in1, \coef2
        vmlsl.s32       \out2, \in2, \coef1
.else
        vmull.s32       \out2, \in1, \coef2
        vmlal.s32       \out2, \in2, \coef1
.endif
.endm

@ out1,out2 = in1,in2 * coef1 - in3,in4 * coef2
@ out3,out4 = in1,in2 * coef2 + in3,in4 * coef1
@ out are 4 q registers, in are 4 d registers
.macro dmbutterfly_l out1, out2, out3, out4, in1, in2, in3, in4, coef1, coef2
        vmull.s32       \out1, \in1, \coef1
        vmull.s32       \out2, \in2, \coef1
        vmull.s32       \out3, \in1, \coef2
        vmull.s32       \out4, \in2, \coef2
        vmlsl.s32       \out1, \in3, \coef2
        vmlsl.s32       \out2, \in4, \coef2
        vmlal.s32       \out3, \in3, \coef1
        vmlal.s32       \out4, \in4, \coef1
.endm

@ inout1 = (inout1 * coef1 - inout2 * coef2 + (1 << 13)) >> 14
@ inout2 = (inout1 * coef2 + inout2 * coef1 + (1 << 13)) >> 14
@ inout are 2 d registers, tmp are 2 q registers
.macro mbutterfly inout1, inout2, coef1, coef2, tmp1, tmp2, neg=0
        mbutterfly_l    \tmp1, \tmp2, \inout1, \inout2, \coef1, \coef2, \neg
        vrshrn.s64      \inout1, \tmp1,  #14
        vrshrn.s64      \inout2, \tmp2,  #14
.endm

@ Same as mbutterfly above, but treating the input in inout2 as zero
.macro mbutterfly_h1 inout1, inout2, coef1, coef2, tmp1, tmp2
        vmull.s32       \tmp1,   \inout1, \coef1
        vmull.s32       \tmp2,   \inout1, \coef2
        vrshrn.s64      \inout1, \tmp1,   #14
        vrshrn.s64      \inout2, \tmp2,   #14
.endm

@ Same as mbutterfly above, but treating the input in inout1 as zero
.macro mbutterfly_h2 inout1, inout2, coef1, coef2, tmp1, tmp2
        vmov.s64        \tmp1,   #0
        vmull.s32       \tmp2,   \inout2, \coef1
        vmlsl.s32       \tmp1,   \inout2, \coef2
        vrshrn.s64      \inout2, \tmp2,   #14
        vrshrn.s64      \inout1, \tmp1,   #14
.endm

@ inout1,inout2 = (inout1,inout2 * coef1 - inout3,inout4 * coef2 + (1 << 13)) >> 14
@ inout3,inout4 = (inout1,inout2 * coef2 + inout3,inout4 * coef1 + (1 << 13)) >> 14
@ inout are 4 d registers, tmp are 4 q registers
.macro dmbutterfly inout1, inout2, inout3, inout4, coef1, coef2, tmp1, tmp2, tmp3, tmp4
        dmbutterfly_l   \tmp1, \tmp2, \tmp3, \tmp4, \inout1, \inout2, \inout3, \inout4, \coef1, \coef2
        vrshrn.s64      \inout1, \tmp1,  #14
        vrshrn.s64      \inout2, \tmp2,  #14
        vrshrn.s64      \inout3, \tmp3,  #14
        vrshrn.s64      \inout4, \tmp4,  #14
.endm

@ out1 = in1 + in2
@ out2 = in1 - in2
.macro butterfly out1, out2, in1, in2
        vadd.s32        \out1, \in1, \in2
        vsub.s32        \out2, \in1, \in2
.endm

@ out1 = in1 - in2
@ out2 = in1 + in2
.macro butterfly_r out1, out2, in1, in2
        vsub.s32        \out1, \in1, \in2
        vadd.s32        \out2, \in1, \in2
.endm

@ out1 = (in1 + in2 + (1 << 13)) >> 14
@ out2 = (in1 - in2 + (1 << 13)) >> 14
@ out are 2 d registers, in are 2 q registers, tmp are 2 q registers
.macro butterfly_n out1, out2, in1, in2, tmp1, tmp2
        vadd.s64        \tmp1, \in1, \in2
        vsub.s64        \tmp2, \in1, \in2
        vrshrn.s64      \out1, \tmp1,  #14
        vrshrn.s64      \out2, \tmp2,  #14
.endm

@ out1,out2 = (in1,in2 + in3,in4 + (1 << 13)) >> 14
@ out3,out4 = (in1,in2 - in3,in4 + (1 << 13)) >> 14
@ out are 4 d registers, in are 4 q registers, tmp are 4 q registers
.macro dbutterfly_n out1, out2, out3, out4, in1, in2, in3, in4, tmp1, tmp2, tmp3, tmp4
        vadd.s64        \tmp1, \in1, \in3
        vadd.s64        \tmp2, \in2, \in4
        vsub.s64        \tmp3, \in1, \in3
        vsub.s64        \tmp4, \in2, \in4
        vrshrn.s64      \out1, \tmp1,  #14
        vrshrn.s64      \out2, \tmp2,  #14
        vrshrn.s64      \out3, \tmp3,  #14
        vrshrn.s64      \out4, \tmp4,  #14
.endm


.macro iwht4_10 c0, c1, c2, c3, cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
        vadd.i32        \c0,  \c0,  \c1
        vsub.i32        q11,  \c2,  \c3
        vsub.i32        q10,  \c0,  q11
        vshr.s32        q10,  q10,  #1
        vsub.i32        \c2,  q10,  \c1
        vsub.i32        \c1,  q10,  \c3
        vadd.i32        \c3,  q11,  \c2
        vsub.i32        \c0,  \c0,  \c1
.endm

.macro iwht4_12 c0, c1, c2, c3, cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
        iwht4_10        \c0, \c1, \c2, \c3, \cd0, \cd1, \cd2, \cd3, \cd4, \cd5, \cd6, \cd7
.endm

@ c0 == cd0,cd1, c1 == cd2,cd3
.macro idct4_10 c0, c1, c2, c3, cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
        vmul.s32        q13,  \c1,  d1[1]
        vmul.s32        q11,  \c1,  d1[0]
        vadd.i32        q14,  \c0,  \c2
        vsub.i32        q15,  \c0,  \c2
        vmla.s32        q13,  \c3,  d1[0]
        vmul.s32        q12,  q14,  d0[0]
        vmul.s32        q10,  q15,  d0[0]
        vmls.s32        q11,  \c3,  d1[1]
        vrshr.s32       q13,  q13,  #14
        vrshr.s32       q12,  q12,  #14
        vrshr.s32       q10,  q10,  #14
        vrshr.s32       q11,  q11,  #14
        vadd.i32        \c0,  q12,  q13
        vsub.i32        \c3,  q12,  q13
        vadd.i32        \c1,  q10,  q11
        vsub.i32        \c2,  q10,  q11
.endm

.macro idct4_12 c0, c1, c2, c3, cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
        vmull.s32       q13,  \cd2, d1[1]
        vmull.s32       q15,  \cd3, d1[1]
        vmull.s32       q11,  \cd2, d1[0]
        vmull.s32       q3,   \cd3, d1[0]
        vadd.i32        q14,  \c0,  \c2
        vsub.i32        q2,   \c0,  \c2
        vmlal.s32       q13,  \cd6, d1[0]
        vmlal.s32       q15,  \cd7, d1[0]
        vmull.s32       q12,  d28,  d0[0]
        vmull.s32       q14,  d29,  d0[0]
        vmull.s32       q10,  d4,   d0[0]
        vmull.s32       q8,   d5,   d0[0]
        vmlsl.s32       q11,  \cd6, d1[1]
        vmlsl.s32       q3,   \cd7, d1[1]
        vrshrn.s64      d26,  q13,  #14
        vrshrn.s64      d27,  q15,  #14
        vrshrn.s64      d24,  q12,  #14
        vrshrn.s64      d25,  q14,  #14
        vrshrn.s64      d20,  q10,  #14
        vrshrn.s64      d21,  q8,   #14
        vrshrn.s64      d22,  q11,  #14
        vrshrn.s64      d23,  q3,   #14
        vadd.i32        \c0,  q12,  q13
        vsub.i32        \c3,  q12,  q13
        vadd.i32        \c1,  q10,  q11
        vsub.i32        \c2,  q10,  q11
.endm

.macro iadst4_10 c0, c1, c2, c3, cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
        vmul.s32        q10,  \c0,  d2[0]
        vmla.s32        q10,  \c2,  d2[1]
        vmla.s32        q10,  \c3,  d3[0]
        vmul.s32        q11,  \c0,  d3[0]
        vmls.s32        q11,  \c2,  d2[0]
        vsub.s32        \c0,  \c0,  \c2
        vmls.s32        q11,  \c3,  d2[1]
        vadd.s32        \c0,  \c0,  \c3
        vmul.s32        q13,  \c1,  d3[1]
        vmul.s32        q12,  \c0,  d3[1]
        vadd.s32        q14,  q10,  q13
        vadd.s32        q15,  q11,  q13
        vrshr.s32       \c0,  q14,  #14
        vadd.s32        q10,  q10,  q11
        vrshr.s32       \c1,  q15,  #14
        vsub.s32        q10,  q10,  q13
        vrshr.s32       \c2,  q12,  #14
        vrshr.s32       \c3,  q10,  #14
.endm

.macro iadst4_12 c0, c1, c2, c3, cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
        vmull.s32       q10,  \cd0, d2[0]
        vmull.s32       q4,   \cd1, d2[0]
        vmlal.s32       q10,  \cd4, d2[1]
        vmlal.s32       q4,   \cd5, d2[1]
        vmlal.s32       q10,  \cd6, d3[0]
        vmlal.s32       q4,   \cd7, d3[0]
        vmull.s32       q11,  \cd0, d3[0]
        vmull.s32       q5,   \cd1, d3[0]
        vmlsl.s32       q11,  \cd4, d2[0]
        vmlsl.s32       q5,   \cd5, d2[0]
        vsub.s32        \c0,  \c0,  \c2
        vmlsl.s32       q11,  \cd6, d2[1]
        vmlsl.s32       q5,   \cd7, d2[1]
        vadd.s32        \c0,  \c0,  \c3
        vmull.s32       q13,  \cd2, d3[1]
        vmull.s32       q6,   \cd3, d3[1]
        vmull.s32       q12,  \cd0, d3[1]
        vmull.s32       q7,   \cd1, d3[1]
        vadd.s64        q14,  q10,  q13
        vadd.s64        q2,   q4,   q6
        vadd.s64        q15,  q11,  q13
        vadd.s64        q3,   q5,   q6
        vrshrn.s64      \cd1, q2,   #14
        vrshrn.s64      \cd0, q14,  #14
        vadd.s64        q10,  q10,  q11
        vadd.s64        q4,   q4,   q5
        vrshrn.s64      \cd3, q3,   #14
        vrshrn.s64      \cd2, q15,  #14
        vsub.s64        q10,  q10,  q13
        vsub.s64        q4,   q4,   q6
        vrshrn.s64      \cd4, q12,  #14
        vrshrn.s64      \cd5, q7,   #14
        vrshrn.s64      \cd6, q10,  #14
        vrshrn.s64      \cd7, q4,   #14
.endm

@ The public functions in this file have got the following signature:
@ void itxfm_add(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);

.macro itxfm_func4x4 txfm1, txfm2, bpp
function ff_vp9_\txfm1\()_\txfm2\()_4x4_add_\bpp\()_neon, export=1
.ifc \txfm1,\txfm2
.ifc \txfm1,idct
        movrel          r12, itxfm4_coeffs
        vld1.16         {d0}, [r12,:64]
        vmovl.s16       q0,  d0
.endif
.ifc \txfm1,iadst
        movrel          r12, iadst4_coeffs
        vld1.16         {d1}, [r12,:64]
        vmovl.s16       q1,  d1
.endif
.else
        movrel          r12, itxfm4_coeffs
        vld1.16         {q0}, [r12,:128]
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
.endif
.if \bpp > 10
.ifnc \txfm1\()_\txfm2,idct_idct
        @ iadst4_12 needs q4-q7
        vpush           {q4-q7}
.endif
.endif

        vmov.i32        q14, #0
        vmov.i32        q15, #0
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #1
        bne             1f
        @ DC-only for idct/idct
        vld1.32         {d4[]},   [r2,:32]
        vmull.s32       q2,  d4,  d0[0]
        vrshrn.s64      d4,  q2,  #14
        vmull.s32       q2,  d4,  d0[0]
        vrshrn.s64      d4,  q2,  #14
        vst1.32         {d30[0]}, [r2,:32]
        vdup.32         q2,  d4[0]
        vmov            q3,  q2
        vmov            q8,  q2
        vmov            q9,  q2
        b               2f
.endif

1:
        vld1.32         {q2-q3},   [r2,:128]
        vst1.32         {q14-q15}, [r2,:128]!
        vld1.32         {q8-q9},   [r2,:128]

.ifc \txfm1,iwht
        vshr.s32        q2,  q2,  #2
        vshr.s32        q3,  q3,  #2
        vshr.s32        q8,  q8,  #2
        vshr.s32        q9,  q9,  #2
.endif

        vst1.16         {q14-q15}, [r2,:128]!
        \txfm1\()4_\bpp q2,  q3,  q8,  q9,  d4,  d5,  d6,  d7,  d16, d17, d18, d19

        @ Transpose 4x4 with 32 bit elements
        vtrn.32         q2,  q3
        vtrn.32         q8,  q9
        vswp            d5,  d16
        vswp            d7,  d18

        \txfm2\()4_\bpp q2,  q3,  q8,  q9,  d4,  d5,  d6,  d7,  d16, d17, d18, d19
2:
        vmvn.u16        q15, #((0xffff << \bpp) & 0xffff)
        vld1.16         {d0},  [r0,:64], r1
        vld1.16         {d1},  [r0,:64], r1
.ifnc \txfm1,iwht
        vrshr.s32       q2,  q2,  #4
        vrshr.s32       q3,  q3,  #4
        vrshr.s32       q8,  q8,  #4
        vrshr.s32       q9,  q9,  #4
.endif
        vaddw.u16       q2,  q2,  d0
        vaddw.u16       q3,  q3,  d1
        vld1.16         {d2},  [r0,:64], r1
        vld1.16         {d3},  [r0,:64], r1
        vqmovun.s32     d0,  q2
        vqmovun.s32     d1,  q3
        sub             r0,  r0,  r1, lsl #2

        vaddw.u16       q8,  q8,  d2
        vmin.u16        q0,  q0,  q15
        vaddw.u16       q9,  q9,  d3
        vst1.16         {d0},  [r0,:64], r1
        vqmovun.s32     d2,  q8
        vqmovun.s32     d3,  q9
        vmin.u16        q1,  q1,  q15

        vst1.16         {d1},  [r0,:64], r1
        vst1.16         {d2},  [r0,:64], r1
        vst1.16         {d3},  [r0,:64], r1

.if \bpp > 10
.ifnc \txfm1\()_\txfm2,idct_idct
        vpop            {q4-q7}
.endif
.endif
        bx              lr
endfunc
.endm

.macro itxfm_funcs4x4 bpp
itxfm_func4x4 idct,  idct,  \bpp
itxfm_func4x4 iadst, idct,  \bpp
itxfm_func4x4 idct,  iadst, \bpp
itxfm_func4x4 iadst, iadst, \bpp
itxfm_func4x4 iwht,  iwht,  \bpp
.endm

itxfm_funcs4x4 10
itxfm_funcs4x4 12

.macro idct8
        dmbutterfly0    d16, d17, d24, d25, q8,  q12, q2, q4, d4, d5, d8, d9, q3, q2, q5, q4 @ q8 = t0a, q12 = t1a
        dmbutterfly     d20, d21, d28, d29, d1[0], d1[1], q2,  q3,  q4,  q5 @ q10 = t2a, q14 = t3a
        dmbutterfly     d18, d19, d30, d31, d2[0], d2[1], q2,  q3,  q4,  q5 @ q9  = t4a, q15 = t7a
        dmbutterfly     d26, d27, d22, d23, d3[0], d3[1], q2,  q3,  q4,  q5 @ q13 = t5a, q11 = t6a

        butterfly       q2,  q14, q8,  q14 @ q2 = t0, q14 = t3
        butterfly       q3,  q10, q12, q10 @ q3 = t1, q10 = t2
        butterfly       q4,  q13, q9,  q13 @ q4 = t4, q13 = t5a
        butterfly       q5,  q11, q15, q11 @ q5 = t7, q11 = t6a

        butterfly       q8,  q15, q2,  q5  @ q8 = out[0], q15 = out[7]

        dmbutterfly0    d4,  d5,  d10, d11, q11, q13, q9,  q13, d18, d19, d26, d27, q2,  q5, q11, q12 @ q2 = t6, q5 = t5

        butterfly       q11, q12, q14, q4  @ q11 = out[3], q12 = out[4]
        butterfly       q9,  q14, q3,  q2  @ q9 = out[1],  q14 = out[6]
        butterfly_r     q13, q10, q10, q5  @ q13 = out[5], q10 = out[2]
.endm

.macro iadst8
        movrel          r12, iadst8_coeffs
        vld1.16         {q1}, [r12,:128]!
        vmovl.s16       q0,  d2
        vmovl.s16       q1,  d3

        dmbutterfly_l   q4,  q5,  q2,  q3,  d30, d31, d16, d17, d0[1], d0[0] @ q4,q5  = t1a, q2,q3 = t0a
        dmbutterfly_l   q8,  q15, q6,  q7,  d22, d23, d24, d25, d2[1], d2[0] @ q8,q15 = t5a, q6,q7 = t4a

        dbutterfly_n    d22, d23, d4,  d5,  q2,  q3,  q6,  q7,  q11, q12, q2,  q3 @ q11 = t0, q2 = t4

        dbutterfly_n    d24, d25, d6,  d7,  q4,  q5,  q8,  q15, q12, q3,  q6,  q7 @ q12 = t1, q3 = t5

        dmbutterfly_l   q6,  q7,  q4,  q5,  d26, d27, d20, d21, d1[1], d1[0] @ q6,q7 = t3a, q4,q5 = t2a
        dmbutterfly_l   q10, q13, q8,  q15, d18, d19, d28, d29, d3[1], d3[0] @ q10,q13 = t7a, q8,q15 = t6a

        dbutterfly_n    d18, d19, d8,  d9,  q4,  q5,  q8,  q15, q9,  q14, q4, q5 @ q9 = t2, q4 = t6
        dbutterfly_n    d16, d17, d12, d13, q6,  q7,  q10, q13, q8,  q15, q6, q7 @ q8 = t3, q6 = t7

        movrel          r12, idct_coeffs
        vld1.16         {q0}, [r12,:128]
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0

        butterfly       q15, q12, q12, q8 @ q15 = -out[7], q12 = t3
        vneg.s32        q15, q15          @ q15 = out[7]
        butterfly       q8,  q9,  q11, q9 @ q8 = out[0], q9 = t2

        dmbutterfly_l   q10, q11, q5,  q7,  d4,  d5,  d6,  d7,  d1[0], d1[1] @ q10,q11 = t5a, q5,q7 = t4a
        dmbutterfly_l   q2,  q3,  q13, q14, d12, d13, d8,  d9,  d1[1], d1[0] @ q2,q3 = t6a, q13,q14 = t7a

        dbutterfly_n    d28, d29, d8,  d9,  q10, q11, q13, q14, q4,  q6,  q10, q11 @ q14 = out[6], q4 = t7

        dmbutterfly0    d22, d23, d24, d25, q9,  q12, q6, q13, d12, d13, d26, d27, q9, q10 @ q11 = -out[3], q12 = out[4]
        vneg.s32        q11, q11      @ q11 = out[3]

        dbutterfly_n    d18, d19, d4,  d5,  q5,  q7,  q2,  q3,  q9, q10, q2,  q3 @ q9 = -out[1], q2 = t6
        vneg.s32        q9,  q9       @ q9 = out[1]

        dmbutterfly0    d20, d21, d26, d27, q2,  q4,  q3, q5,  d6,  d7,  d10, d11, q6,  q7 @ q10 = out[2], q13 = -out[5]
        vneg.s32        q13, q13      @ q13 = out[5]
.endm

function idct8x8_dc_add_neon
        movrel          r12, idct_coeffs
        vld1.16         {d0}, [r12,:64]

        vmov.i32        q2,  #0
        vmovl.s16       q0,  d0

        vld1.32         {d16[]}, [r2,:32]
        vmull.s32       q8,  d16, d0[0]
        vrshrn.s64      d16, q8,  #14
        vmull.s32       q8,  d16, d0[0]
        vrshrn.s64      d16, q8,  #14
        vdup.32         q8,  d16[0]
        vst1.32         {d4[0]}, [r2,:32]

        vrshr.s32       q8,  q8,  #5
        vdup.s16        q15, r8

        mov             r3,  r0
        mov             r12, #8
1:
        @ Loop to add the constant from q8 into all 8x8 outputs
        subs            r12, r12, #2
        vld1.16         {q2},  [r0,:128], r1
        vaddw.u16       q10, q8,  d4
        vld1.16         {q3},  [r0,:128], r1
        vaddw.u16       q11, q8,  d5
        vaddw.u16       q12, q8,  d6
        vaddw.u16       q13, q8,  d7
        vqmovun.s32     d4,  q10
        vqmovun.s32     d5,  q11
        vqmovun.s32     d6,  q12
        vqmovun.s32     d7,  q13
        vmin.u16        q2,  q2,  q15
        vst1.16         {q2},  [r3,:128], r1
        vmin.u16        q3,  q3,  q15
        vst1.16         {q3},  [r3,:128], r1
        bne             1b

        pop             {r4-r8,pc}
endfunc
.ltorg

.macro itxfm8_1d_funcs txfm
@ Read a vertical 4x8 slice out of a 8x8 matrix, do a transform on it,
@ transpose into a horizontal 8x4 slice and store.
@ r0 = dst (temp buffer)
@ r1 = slice offset
@ r2 = src
function \txfm\()8_1d_4x8_pass1_neon
        mov             r12, #32
        vmov.s32        q2,  #0
.irp i, 8, 9, 10, 11, 12, 13, 14, 15
        vld1.32         {q\i}, [r2,:128]
        vst1.32         {q2},  [r2,:128], r12
.endr

        \txfm\()8

        @ Do two 4x4 transposes. Originally, q8-q15 contain the
        @ 8 rows. Afterwards, q8-q11, q12-q15 contain the transposed
        @ 4x4 blocks.
        transpose32_q_2x_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        @ Store the transposed 4x4 blocks horizontally.
        cmp             r1,  #4
        beq             1f
.irp i, 8, 12, 9, 13, 10, 14, 11, 15
        vst1.32         {q\i}, [r0,:128]!
.endr
        bx              lr
1:
        @ Special case: For the last input column (r1 == 4),
        @ which would be stored as the last row in the temp buffer,
        @ don't store the first 4x4 block, but keep it in registers
        @ for the first slice of the second pass (where it is the
        @ last 4x4 block).
.irp i, 12, 13, 14, 15
        add             r0,  r0,  #16
        vst1.32         {q\i}, [r0,:128]!
.endr
        vmov            q12, q8
        vmov            q13, q9
        vmov            q14, q10
        vmov            q15, q11
        bx              lr
endfunc

@ Read a vertical 4x8 slice out of a 8x8 matrix, do a transform on it,
@ load the destination pixels (from a similar 4x8 slice), add and store back.
@ r0 = dst
@ r1 = dst stride
@ r2 = src (temp buffer)
@ r3 = slice offset
function \txfm\()8_1d_4x8_pass2_neon
        mov             r12, #32
.irp i, 8, 9, 10, 11
        vld1.32         {q\i}, [r2,:128], r12
.endr
        cmp             r3,  #0
        beq             1f
.irp i, 12, 13, 14, 15
        vld1.32         {q\i}, [r2,:128], r12
.endr
1:

        add             r3,  r0,  r1
        lsl             r1,  r1,  #1
        \txfm\()8

        vdup.s16        q4,  r8
.macro load_add_store coef0, coef1, coef2, coef3
        vld1.16         {d4},   [r0,:64], r1
        vld1.16         {d5},   [r3,:64], r1
        vld1.16         {d6},   [r0,:64], r1
        vld1.16         {d7},   [r3,:64], r1

        vrshr.s32       \coef0, \coef0, #5
        vrshr.s32       \coef1, \coef1, #5
        vrshr.s32       \coef2, \coef2, #5
        vrshr.s32       \coef3, \coef3, #5

        vaddw.u16       \coef0, \coef0, d4
        vaddw.u16       \coef1, \coef1, d5
        vaddw.u16       \coef2, \coef2, d6
        vaddw.u16       \coef3, \coef3, d7

        sub             r0,  r0,  r1, lsl #1
        sub             r3,  r3,  r1, lsl #1

        vqmovun.s32     d4,  \coef0
        vqmovun.s32     d5,  \coef1
        vqmovun.s32     d6,  \coef2
        vqmovun.s32     d7,  \coef3

        vmin.u16        q2,  q2,  q4
        vmin.u16        q3,  q3,  q4

        vst1.16         {d4},  [r0,:64], r1
        vst1.16         {d5},  [r3,:64], r1
        vst1.16         {d6},  [r0,:64], r1
        vst1.16         {d7},  [r3,:64], r1
.endm
        load_add_store  q8,  q9,  q10, q11
        load_add_store  q12, q13, q14, q15
.purgem load_add_store

        bx              lr
endfunc
.endm

itxfm8_1d_funcs idct
itxfm8_1d_funcs iadst

.macro itxfm_func8x8 txfm1, txfm2
function vp9_\txfm1\()_\txfm2\()_8x8_add_16_neon
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #1
        beq             idct8x8_dc_add_neon
.endif
.ifnc \txfm1\()_\txfm2,idct_idct
        vpush           {q4-q7}
.else
        vpush           {q4-q5}
.endif

        @ Align the stack, allocate a temp buffer
T       mov             r7,  sp
T       and             r7,  r7,  #15
A       and             r7,  sp,  #15
        add             r7,  r7,  #256
        sub             sp,  sp,  r7

        mov             r4,  r0
        mov             r5,  r1
        mov             r6,  r2

.ifc \txfm1,idct
        movrel          r12, idct_coeffs
        vld1.16         {q0}, [r12,:128]
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
.endif

.irp i, 0, 4
        add             r0,  sp,  #(\i*32)
.ifc \txfm1\()_\txfm2,idct_idct
.if \i == 4
        cmp             r3,  #12
        ble             1f
.endif
.endif
        mov             r1,  #\i
        add             r2,  r6,  #(\i*4)
        bl              \txfm1\()8_1d_4x8_pass1_neon
.endr
.ifc \txfm1\()_\txfm2,idct_idct
        b               3f
1:
        @ For all-zero slices in pass 1, set q12-q15 to zero, for the in-register
        @ passthrough of coefficients to pass 2 and clear the end of the temp buffer
        vmov.i32        q12, #0
        vmov.i32        q13, #0
        vmov.i32        q14, #0
        vmov.i32        q15, #0
.rept 4
        vst1.32         {q12-q13}, [r0,:128]!
.endr
3:
.endif
.ifc \txfm1\()_\txfm2,iadst_idct
        movrel          r12, idct_coeffs
        vld1.16         {q0}, [r12,:128]
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
.endif
.irp i, 0, 4
        add             r0,  r4,  #(\i*2)
        mov             r1,  r5
        add             r2,  sp,  #(\i*4)
        mov             r3,  #\i
        bl              \txfm2\()8_1d_4x8_pass2_neon
.endr

        add             sp,  sp,  r7
.ifnc \txfm1\()_\txfm2,idct_idct
        vpop            {q4-q7}
.else
        vpop            {q4-q5}
.endif
        pop             {r4-r8,pc}
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_8x8_add_10_neon, export=1
        push            {r4-r8,lr}
        movw            r8,  #0x03ff
        b               vp9_\txfm1\()_\txfm2\()_8x8_add_16_neon
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_8x8_add_12_neon, export=1
        push            {r4-r8,lr}
        movw            r8,  #0x0fff
        b               vp9_\txfm1\()_\txfm2\()_8x8_add_16_neon
endfunc
.endm

itxfm_func8x8 idct,  idct
itxfm_func8x8 iadst, idct
itxfm_func8x8 idct,  iadst
itxfm_func8x8 iadst, iadst

function idct16x16_dc_add_neon
        movrel          r12, idct_coeffs
        vld1.16         {d0}, [r12,:64]

        vmov.i32        q2,  #0
        vmovl.s16       q0,  d0

        vld1.32         {d16[]}, [r2,:32]
        vmull.s32       q8,  d16, d0[0]
        vrshrn.s64      d16, q8,  #14
        vmull.s32       q8,  d16, d0[0]
        vrshrn.s64      d16, q8,  #14
        vdup.32         q8,  d16[0]
        vst1.32         {d4[0]}, [r2,:32]

        vrshr.s32       q8,  q8,  #6
        vdup.s16        q15, r9

        mov             r3,  r0
        mov             r12, #16
1:
        @ Loop to add the constant from q8 into all 16x16 outputs
        subs            r12, r12, #2
        vld1.16         {q0-q1},  [r0,:128], r1
        vaddw.u16       q9,  q8,  d0
        vaddw.u16       q10, q8,  d1
        vld1.16         {q2-q3},  [r0,:128], r1
        vaddw.u16       q11, q8,  d2
        vaddw.u16       q12, q8,  d3
        vaddw.u16       q13, q8,  d4
        vaddw.u16       q14, q8,  d5
        vqmovun.s32     d0,  q9
        vaddw.u16       q9,  q8,  d6
        vqmovun.s32     d1,  q10
        vaddw.u16       q10, q8,  d7
        vqmovun.s32     d2,  q11
        vqmovun.s32     d3,  q12
        vqmovun.s32     d4,  q13
        vqmovun.s32     d5,  q14
        vmin.u16        q0,  q0,  q15
        vmin.u16        q1,  q1,  q15
        vqmovun.s32     d6,  q9
        vqmovun.s32     d7,  q10
        vst1.16         {q0-q1},  [r3,:128], r1
        vmin.u16        q2,  q2,  q15
        vmin.u16        q3,  q3,  q15
        vst1.16         {q2-q3},  [r3,:128], r1
        bne             1b

        pop             {r4-r9,pc}
endfunc
.ltorg

.macro idct16_end
        butterfly       d18, d11, d8,  d11               @ d18 = t0a,  d11 = t7a
        butterfly       d19, d22, d9,  d22               @ d19 = t1a,  d22 = t6
        butterfly       d8,  d26, d20, d26               @ d8  = t2a,  d26 = t5
        butterfly       d9,  d10, d28, d10               @ d9  = t3a,  d10 = t4
        butterfly       d20, d28, d16, d24               @ d20 = t8a,  d28 = t11a
        butterfly       d24, d21, d23, d21               @ d24 = t9,   d21 = t10
        butterfly       d23, d27, d25, d27               @ d23 = t14,  d27 = t13
        butterfly       d25, d29, d29, d17               @ d25 = t15a, d29 = t12a

        mbutterfly0     d27, d21, d27, d21, d16, d30, q8, q15 @ d27 = t13a, d21 = t10a
        mbutterfly0     d29, d28, d29, d28, d16, d30, q8, q15 @ d29 = t12,  d28 = t11

        vswp            d27, d29                         @ d27 = t12, d29 = t13a
        vswp            d28, d27                         @ d28 = t12, d27 = t11
        butterfly       d16, d31, d18, d25               @ d16 = out[0], d31 = out[15]
        butterfly       d17, d30, d19, d23               @ d17 = out[1], d30 = out[14]
        butterfly_r     d25, d22, d22, d24               @ d25 = out[9], d22 = out[6]
        butterfly       d23, d24, d11, d20               @ d23 = out[7], d24 = out[8]
        butterfly       d18, d29, d8,  d29               @ d18 = out[2], d29 = out[13]
        butterfly       d19, d28, d9,  d28               @ d19 = out[3], d28 = out[12]
        vmov            d8,  d21                         @ d8  = t10a
        butterfly       d20, d27, d10, d27               @ d20 = out[4], d27 = out[11]
        butterfly       d21, d26, d26, d8                @ d21 = out[5], d26 = out[10]
        bx              lr
.endm

function idct16
        mbutterfly0     d16, d24, d16, d24, d8, d10, q4,  q5 @ d16 = t0a,  d24 = t1a
        mbutterfly      d20, d28, d1[0], d1[1], q4,  q5  @ d20 = t2a,  d28 = t3a
        mbutterfly      d18, d30, d2[0], d2[1], q4,  q5  @ d18 = t4a,  d30 = t7a
        mbutterfly      d26, d22, d3[0], d3[1], q4,  q5  @ d26 = t5a,  d22 = t6a
        mbutterfly      d17, d31, d4[0], d4[1], q4,  q5  @ d17 = t8a,  d31 = t15a
        mbutterfly      d25, d23, d5[0], d5[1], q4,  q5  @ d25 = t9a,  d23 = t14a
        mbutterfly      d21, d27, d6[0], d6[1], q4,  q5  @ d21 = t10a, d27 = t13a
        mbutterfly      d29, d19, d7[0], d7[1], q4,  q5  @ d29 = t11a, d19 = t12a

        butterfly       d8,  d28, d16, d28               @ d8  = t0,   d28 = t3
        butterfly       d9,  d20, d24, d20               @ d9  = t1,   d20 = t2
        butterfly       d10, d26, d18, d26               @ d10 = t4,   d26 = t5
        butterfly       d11, d22, d30, d22               @ d11 = t7,   d22 = t6
        butterfly       d16, d25, d17, d25               @ d16 = t8,   d25 = t9
        butterfly       d24, d21, d29, d21               @ d24 = t11,  d21 = t10
        butterfly       d17, d27, d19, d27               @ d17 = t12,  d27 = t13
        butterfly       d29, d23, d31, d23               @ d29 = t15,  d23 = t14

        mbutterfly0     d22, d26, d22, d26, d18, d30, q9,  q15  @ d22 = t6a, d26 = t5a
        mbutterfly      d23, d25, d1[0], d1[1], q9,  q15        @ d23 = t9a,  d25 = t14a
        mbutterfly      d27, d21, d1[0], d1[1], q9,  q15, neg=1 @ d27 = t13a, d21 = t10a
        idct16_end
endfunc

function idct16_half
        mbutterfly0_h   d16, d24, d16, d24, d8, d10, q4,  q5 @ d16 = t0a,  d24 = t1a
        mbutterfly_h1   d20, d28, d1[0], d1[1], q4,  q5  @ d20 = t2a,  d28 = t3a
        mbutterfly_h1   d18, d30, d2[0], d2[1], q4,  q5  @ d18 = t4a,  d30 = t7a
        mbutterfly_h2   d26, d22, d3[0], d3[1], q4,  q5  @ d26 = t5a,  d22 = t6a
        mbutterfly_h1   d17, d31, d4[0], d4[1], q4,  q5  @ d17 = t8a,  d31 = t15a
        mbutterfly_h2   d25, d23, d5[0], d5[1], q4,  q5  @ d25 = t9a,  d23 = t14a
        mbutterfly_h1   d21, d27, d6[0], d6[1], q4,  q5  @ d21 = t10a, d27 = t13a
        mbutterfly_h2   d29, d19, d7[0], d7[1], q4,  q5  @ d29 = t11a, d19 = t12a

        butterfly       d8,  d28, d16, d28               @ d8  = t0,   d28 = t3
        butterfly       d9,  d20, d24, d20               @ d9  = t1,   d20 = t2
        butterfly       d10, d26, d18, d26               @ d10 = t4,   d26 = t5
        butterfly       d11, d22, d30, d22               @ d11 = t7,   d22 = t6
        butterfly       d16, d25, d17, d25               @ d16 = t8,   d25 = t9
        butterfly       d24, d21, d29, d21               @ d24 = t11,  d21 = t10
        butterfly       d17, d27, d19, d27               @ d17 = t12,  d27 = t13
        butterfly       d29, d23, d31, d23               @ d29 = t15,  d23 = t14

        mbutterfly0     d22, d26, d22, d26, d18, d30, q9,  q15  @ d22 = t6a, d26 = t5a
        mbutterfly      d23, d25, d1[0], d1[1], q9,  q15        @ d23 = t9a,  d25 = t14a
        mbutterfly      d27, d21, d1[0], d1[1], q9,  q15, neg=1 @ d27 = t13a, d21 = t10a
        idct16_end
endfunc

function idct16_quarter
        vmov.s64        q12, #0
        vmull.s32       q4,  d17, d4[0]
        vmull.s32       q5,  d18, d2[1]
        vmull.s32       q15, d18, d2[0]
        vmlsl.s32       q12, d19, d7[1]
        vmull.s32       q14, d17, d4[1]
        vmull.s32       q13, d19, d7[0]
        vmull.s32       q11, d16, d0[0]
        vrshrn.s64      d16, q4,  #14
        vrshrn.s64      d11, q5,  #14
        vrshrn.s64      d10, q15, #14
        vrshrn.s64      d24, q12, #14
        vrshrn.s64      d29, q14, #14
        vrshrn.s64      d17, q13, #14
        vrshrn.s64      d28, q11, #14

        mbutterfly_l    q10, q11, d17, d24, d1[0], d1[1], neg=1
        mbutterfly_l    q9,  q15, d29, d16, d1[0], d1[1]
        vrshrn.s64      d27, q10, #14
        vrshrn.s64      d21, q11, #14
        vrshrn.s64      d23, q9,  #14
        vrshrn.s64      d25, q15, #14
        vmov            d8,  d28
        vmov            d9,  d28
        mbutterfly0     d22, d26, d11, d10, d18, d30, q9,  q15
        vmov            d20, d28
        idct16_end
endfunc

function iadst16
        movrel          r12, iadst16_coeffs
        vld1.16         {q0},  [r12,:128]!
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0

        mbutterfly_l    q3,  q2,  d31, d16, d0[1], d0[0] @ q3  = t1,   q2  = t0
        mbutterfly_l    q5,  q4,  d23, d24, d2[1], d2[0] @ q5  = t9,   q4  = t8
        butterfly_n     d31, d24, q3,  q5,  q6,  q5      @ d31 = t1a,  d24 = t9a
        mbutterfly_l    q7,  q6,  d29, d18, d1[1], d1[0] @ q7  = t3,   q6  = t2
        butterfly_n     d16, d23, q2,  q4,  q3,  q4      @ d16 = t0a,  d23 = t8a
        mbutterfly_l    q3,  q2,  d21, d26, d3[1], d3[0] @ q3  = t11,  q2  = t10

        vld1.16         {q0},  [r12,:128]!
        butterfly_n     d29, d26, q7,  q3,  q4,  q3      @ d29 = t3a,  d26 = t11a
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
        mbutterfly_l    q5,  q4,  d27, d20, d0[1], d0[0] @ q5  = t5,   q4  = t4
        butterfly_n     d18, d21, q6,  q2,  q3,  q2      @ d18 = t2a,  d21 = t10a

        mbutterfly_l    q7,  q6,  d19, d28, d2[1], d2[0] @ q7  = t13,  q6  = t12
        butterfly_n     d20, d28, q5,  q7,  q2,  q7      @ d20 = t5a,  d28 = t13a
        mbutterfly_l    q3,  q2,  d25, d22, d1[1], d1[0] @ q3  = t7,   q2  = t6
        butterfly_n     d27, d19, q4,  q6,  q5,  q6      @ d27 = t4a,  d19 = t12a

        mbutterfly_l    q5,  q4,  d17, d30, d3[1], d3[0] @ q5  = t15,  q4  = t14
        movrel          r12, idct_coeffs
        vld1.16         {q0}, [r12,:128]
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
        butterfly_n     d22, d30, q3,  q5,  q6,  q5      @ d22 = t7a,  d30 = t15a
        mbutterfly_l    q7,  q6,  d23, d24, d2[0], d2[1] @ q7  = t9,   q6  = t8
        butterfly_n     d25, d17, q2,  q4,  q3,  q4      @ d25 = t6a,  d17 = t14a

        mbutterfly_l    q2,  q3,  d28, d19, d2[1], d2[0] @ q2  = t12,  q3  = t13
        butterfly_n     d23, d19, q6,  q2,  q4,  q2      @ d23 = t8a,  d19 = t12a
        mbutterfly_l    q5,  q4,  d21, d26, d3[0], d3[1] @ q5  = t11,  q4  = t10
        butterfly_r     d4,  d27, d16, d27               @ d4  = t4,   d27 = t0
        butterfly_n     d24, d28, q7,  q3,  q6,  q3      @ d24 = t9a,  d28 = t13a

        mbutterfly_l    q6,  q7,  d30, d17, d3[1], d3[0] @ q6  = t14,  q7  = t15
        butterfly_r     d5,  d20, d31, d20               @ d5  = t5,   d20 = t1
        butterfly_n     d21, d17, q4,  q6,  q3,  q6      @ d21 = t10a, d17 = t14a
        butterfly_n     d26, d30, q5,  q7,  q4,  q7      @ d26 = t11a, d30 = t15a

        butterfly_r     d6,  d25, d18, d25               @ d6  = t6,   d25 = t2
        butterfly_r     d7,  d22, d29, d22               @ d7  = t7,   d22 = t3

        mbutterfly_l    q5,  q4,  d19, d28, d1[0], d1[1] @ q5  = t13,  q4  = t12
        mbutterfly_l    q6,  q7,  d30, d17, d1[1], d1[0] @ q6  = t14,  q7  = t15

        butterfly_n     d18, d30, q4,  q6,  q8,  q6      @ d18 = out[2],   d30 = t14a
        butterfly_n     d29, d17, q5,  q7,  q6,  q7      @ d29 = -out[13], d17 = t15a
        vneg.s32        d29, d29                         @ d29 = out[13]

        mbutterfly_l    q5,  q4,  d4,  d5,  d1[0], d1[1] @ q5  = t5a,  q4  = t4a
        mbutterfly_l    q6,  q7,  d7,  d6,  d1[1], d1[0] @ q6  = t6a,  q7  = t7a

        butterfly       d2,  d6,  d27, d25               @ d2 = out[0], d6 = t2a
        butterfly       d3,  d7,  d23, d21               @ d3 =-out[1], d7 = t10

        butterfly_n     d19, d31, q4,  q6,  q2,  q4      @ d19 = -out[3],  d31 = t6
        vneg.s32        d19, d19                         @ d19 = out[3]
        butterfly_n     d28, d16, q5,  q7,  q2,  q5      @ d28 = out[12],  d16 = t7

        butterfly       d5,  d8,  d20, d22               @ d5 =-out[15],d8 = t3a
        butterfly       d4,  d9,  d24, d26               @ d4 = out[14],d9 = t11

        mbutterfly0     d23, d24, d6,  d8,  d10, d11, q6,  q7, 1 @ d23 = out[7], d24 = out[8]
        mbutterfly0     d20, d27, d16, d31, d10, d11, q6,  q7    @ d20 = out[4], d27 = out[11]
        mbutterfly0     d22, d25, d9,  d7,  d10, d11, q6,  q7    @ d22 = out[6], d25 = out[9]
        mbutterfly0     d21, d26, d30, d17, d10, d11, q6,  q7, 1 @ d21 = out[5], d26 = out[10]

        vneg.s32        d31, d5                          @ d31 = out[15]
        vneg.s32        d17, d3                          @ d17 = out[1]

        vmov            d16, d2
        vmov            d30, d4
        bx              lr
endfunc

.macro itxfm16_1d_funcs txfm, suffix
@ Read a vertical 2x16 slice out of a 16x16 matrix, do a transform on it,
@ transpose into a horizontal 16x2 slice and store.
@ r0 = dst (temp buffer)
@ r2 = src
function \txfm\()16_1d_2x16_pass1\suffix\()_neon
        push            {lr}

        mov             r12, #64
        vmov.s32        q4,  #0
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.32         {d\i}, [r2,:64]
        vst1.32         {d8},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.32         {d\i}, [r2,:64]
        vst1.32         {d8},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.32         {d\i}, [r2,:64]
        vst1.32         {d8},  [r2,:64], r12
.endr
.endif

        bl              \txfm\()16\suffix

        @ Do eight 2x2 transposes. Originally, d16-d31 contain the
        @ 16 rows. Afterwards, d16-d17, d18-d19 etc contain the eight
        @ transposed 2x2 blocks.
        transpose32_8x_2x2 d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        @ Store the transposed 2x2 blocks horizontally.
.irp i, 16, 18, 20, 22, 24, 26, 28, 30, 17, 19, 21, 23, 25, 27, 29, 31
        vst1.32         {d\i}, [r0,:64]!
.endr
        pop             {pc}
endfunc

@ Read a vertical 2x16 slice out of a 16x16 matrix, do a transform on it,
@ load the destination pixels (from a similar 2x16 slice), add and store back.
@ r0 = dst
@ r1 = dst stride
@ r2 = src (temp buffer)
function \txfm\()16_1d_2x16_pass2\suffix\()_neon
        push            {lr}

        mov             r12, #64
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64], r12
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19, 20
        vld1.16         {d\i}, [r2,:64], r12
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64], r12
.endr
.endif

        add             r3,  r0,  r1
        lsl             r1,  r1,  #1
        bl              \txfm\()16\suffix

.macro load_add_store coef0, coef1, coef2, coef3
        vrshr.s32       \coef0, \coef0, #6
        vrshr.s32       \coef1, \coef1, #6

        vld1.32         {d8[]},   [r0,:32], r1
        vld1.32         {d8[1]},  [r3,:32], r1
        vrshr.s32       \coef2, \coef2, #6
        vrshr.s32       \coef3, \coef3, #6
        vld1.32         {d9[]},   [r0,:32], r1
        vld1.32         {d9[1]},  [r3,:32], r1
        vaddw.u16       \coef0, \coef0, d8
        vld1.32         {d10[]},  [r0,:32], r1
        vld1.32         {d10[1]}, [r3,:32], r1
        vaddw.u16       \coef1, \coef1, d9
        vld1.32         {d11[]},  [r0,:32], r1
        vld1.32         {d11[1]}, [r3,:32], r1

        vqmovun.s32     d8,  \coef0
        vdup.s16        q8,  r9
        vqmovun.s32     d9,  \coef1
        sub             r0,  r0,  r1, lsl #2
        sub             r3,  r3,  r1, lsl #2
        vaddw.u16       \coef2, \coef2, d10
        vaddw.u16       \coef3, \coef3, d11
        vmin.u16        q4,  q4,  q8
        vst1.32         {d8[0]},  [r0,:32], r1
        vst1.32         {d8[1]},  [r3,:32], r1
        vqmovun.s32     d10, \coef2
        vst1.32         {d9[0]},  [r0,:32], r1
        vst1.32         {d9[1]},  [r3,:32], r1
        vqmovun.s32     d11, \coef3
        vmin.u16        q5,  q5,  q8

        vst1.32         {d10[0]}, [r0,:32], r1
        vst1.32         {d10[1]}, [r3,:32], r1
        vst1.32         {d11[0]}, [r0,:32], r1
        vst1.32         {d11[1]}, [r3,:32], r1
.endm
        load_add_store  q8,  q9,  q10, q11
        load_add_store  q12, q13, q14, q15
.purgem load_add_store

        pop             {pc}
endfunc
.endm

itxfm16_1d_funcs idct
itxfm16_1d_funcs iadst
itxfm16_1d_funcs idct, _quarter
itxfm16_1d_funcs idct, _half
.ltorg

@ This is the minimum eob value for each subpartition, in increments of 2
const min_eob_idct_idct_16, align=4
        .short  0, 3, 10, 22, 38, 62, 89, 121
endconst

.macro itxfm_func16x16 txfm1, txfm2
function vp9_\txfm1\()_\txfm2\()_16x16_add_16_neon
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #1
        beq             idct16x16_dc_add_neon
.endif
.ifnc \txfm1\()_\txfm2,idct_idct
        vpush           {q4-q7}
.else
        vpush           {q4-q5}
.endif

        @ Align the stack, allocate a temp buffer
T       mov             r7,  sp
T       and             r7,  r7,  #15
A       and             r7,  sp,  #15
        add             r7,  r7,  #1024
        sub             sp,  sp,  r7

        mov             r4,  r0
        mov             r5,  r1
        mov             r6,  r2

.ifc \txfm1,idct
        movrel          r12, idct_coeffs
        vld1.16         {q0-q1}, [r12,:128]
        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
.endif

.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #10
        ble             idct16x16_quarter_add_16_neon
        cmp             r3,  #38
        ble             idct16x16_half_add_16_neon

        movrel          r8,  min_eob_idct_idct_16 + 2
.endif

.irp i, 0, 2, 4, 6, 8, 10, 12, 14
        add             r0,  sp,  #(\i*64)
.ifc \txfm1\()_\txfm2,idct_idct
.if \i > 0
        ldrh_post       r1,  r8,  #2
        cmp             r3,  r1
        it              le
        movle           r1,  #(16 - \i)/2
        ble             1f
.endif
.endif
        add             r2,  r6,  #(\i*4)
        bl              \txfm1\()16_1d_2x16_pass1_neon
.endr

.ifc \txfm1\()_\txfm2,idct_idct
        b               3f
1:
        vmov.i32        q14, #0
        vmov.i32        q15, #0
2:
        subs            r1,  r1,  #1
        @ Unroll for 2 lines
.rept 2
        @ Fill one line with zeros
        vst1.32         {q14-q15}, [r0,:128]!
        vst1.32         {q14-q15}, [r0,:128]!
.endr
        bne             2b
3:
.endif

.ifc \txfm1\()_\txfm2,iadst_idct
        movrel          r12, idct_coeffs
        vld1.16         {q0-q1}, [r12,:128]
        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
.endif
.irp i, 0, 2, 4, 6, 8, 10, 12, 14
        add             r0,  r4,  #(\i*2)
        mov             r1,  r5
        add             r2,  sp,  #(\i*4)
        bl              \txfm2\()16_1d_2x16_pass2_neon
.endr

        add             sp,  sp,  r7
.ifnc \txfm1\()_\txfm2,idct_idct
        vpop            {q4-q7}
.else
        vpop            {q4-q5}
.endif
        pop             {r4-r9,pc}
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_16x16_add_10_neon, export=1
        push            {r4-r9,lr}
        movw            r9,  #0x03ff
        b               vp9_\txfm1\()_\txfm2\()_16x16_add_16_neon
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_16x16_add_12_neon, export=1
        push            {r4-r9,lr}
        movw            r9,  #0x0fff
        b               vp9_\txfm1\()_\txfm2\()_16x16_add_16_neon
endfunc
.endm

itxfm_func16x16 idct,  idct
itxfm_func16x16 iadst, idct
itxfm_func16x16 idct,  iadst
itxfm_func16x16 iadst, iadst
.ltorg

.macro idct16_partial size
function idct16x16_\size\()_add_16_neon
.irp i, 0, 2
        add             r0,  sp,  #(\i*64)
.ifc \size,quarter
.if \i == 2
        cmp             r3,  #3
        ble             1f
.endif
.endif
        add             r2,  r6,  #(\i*4)
        bl              idct16_1d_2x16_pass1_\size\()_neon
.endr

.ifc \size,half
.irp i, 4, 6
        add             r0,  sp,  #(\i*64)
.if \i == 6
        cmp             r3,  #22
        ble             1f
.endif
        add             r2,  r6,  #(\i*4)
        bl              idct16_1d_2x16_pass1_\size\()_neon
.endr
.endif

        b               3f
1:
        vmov.i32        q14, #0
        vmov.i32        q15, #0

        @ Unroll for 2 lines
.rept 2
        @ Fill one line with zeros
        vst1.32         {q14-q15}, [r0,:128]!
        vst1.32         {q14-q15}, [r0,:128]!
.endr

3:

.irp i, 0, 2, 4, 6, 8, 10, 12, 14
        add             r0,  r4,  #(\i*2)
        mov             r1,  r5
        add             r2,  sp,  #(\i*4)
        bl              idct16_1d_2x16_pass2_\size\()_neon
.endr

        add             sp,  sp,  r7
        vpop            {q4-q5}
        pop             {r4-r9,pc}
endfunc
.endm

idct16_partial quarter
idct16_partial half

function idct32x32_dc_add_neon
        movrel          r12, idct_coeffs
        vld1.16         {d0}, [r12,:64]

        vmov.i32        q2,  #0
        vmovl.s16       q0,  d0

        vld1.32         {d16[]}, [r2,:32]
        vmull.s32       q8,  d16, d0[0]
        vrshrn.s64      d16, q8,  #14
        vmull.s32       q8,  d16, d0[0]
        vrshrn.s64      d16, q8,  #14
        vdup.32         q8,  d16[0]
        vst1.32         {d4[0]}, [r2,:32]

        vrshr.s32       q8,  q8,  #6
        vdup.s16        q15, r9

        mov             r3,  r0
        mov             r12, #32
        sub             r1,  r1,  #32
1:
        @ Loop to add the constant from q8 into all 32x32 outputs
        subs            r12, r12, #1
        vld1.16         {q0-q1},  [r0,:128]!
        vaddw.u16       q9,  q8,  d0
        vaddw.u16       q10, q8,  d1
        vld1.16         {q2-q3},  [r0,:128], r1
        vaddw.u16       q11, q8,  d2
        vaddw.u16       q12, q8,  d3
        vaddw.u16       q13, q8,  d4
        vaddw.u16       q14, q8,  d5
        vqmovun.s32     d0,  q9
        vaddw.u16       q9,  q8,  d6
        vqmovun.s32     d1,  q10
        vaddw.u16       q10, q8,  d7
        vqmovun.s32     d2,  q11
        vqmovun.s32     d3,  q12
        vqmovun.s32     d4,  q13
        vqmovun.s32     d5,  q14
        vmin.u16        q0,  q0,  q15
        vmin.u16        q1,  q1,  q15
        vqmovun.s32     d6,  q9
        vqmovun.s32     d7,  q10
        vst1.16         {q0-q1},  [r3,:128]!
        vmin.u16        q2,  q2,  q15
        vmin.u16        q3,  q3,  q15
        vst1.16         {q2-q3},  [r3,:128], r1
        bne             1b

        pop             {r4-r9,pc}
endfunc

.macro idct32_end
        butterfly       d16, d9,  d8,  d9  @ d16 = t16a, d9  = t19a
        butterfly       d17, d20, d23, d20 @ d17 = t17,  d20 = t18
        butterfly       d18, d10, d11, d10 @ d18 = t23a, d10 = t20a
        butterfly       d19, d21, d22, d21 @ d19 = t22,  d21 = t21
        butterfly       d8,  d28, d28, d30 @ d8  = t24a, d28 = t27a
        butterfly       d23, d26, d25, d26 @ d23 = t25,  d26 = t26
        butterfly       d11, d29, d29, d31 @ d11 = t31a, d29 = t28a
        butterfly       d22, d27, d24, d27 @ d22 = t30,  d27 = t29

        mbutterfly      d27, d20, d1[0], d1[1], q12, q15        @ d27 = t18a, d20 = t29a
        mbutterfly      d29, d9,  d1[0], d1[1], q12, q15        @ d29 = t19,  d9  = t28
        mbutterfly      d28, d10, d1[0], d1[1], q12, q15, neg=1 @ d28 = t27,  d10 = t20
        mbutterfly      d26, d21, d1[0], d1[1], q12, q15, neg=1 @ d26 = t26a, d21 = t21a

        butterfly       d31, d24, d11, d8  @ d31 = t31,  d24 = t24
        butterfly       d30, d25, d22, d23 @ d30 = t30a, d25 = t25a
        butterfly_r     d23, d16, d16, d18 @ d23 = t23,  d16 = t16
        butterfly_r     d22, d17, d17, d19 @ d22 = t22a, d17 = t17a
        butterfly       d18, d21, d27, d21 @ d18 = t18,  d21 = t21
        butterfly_r     d27, d28, d9,  d28 @ d27 = t27a, d28 = t28a
        butterfly       d8,  d26, d20, d26 @ d8  = t29,  d26 = t26
        butterfly       d19, d20, d29, d10 @ d19 = t19a, d20 = t20
        vmov            d29, d8            @ d29 = t29

        mbutterfly0     d27, d20, d27, d20, d8, d10, q4, q5 @ d27 = t27,  d20 = t20
        mbutterfly0     d26, d21, d26, d21, d8, d10, q4, q5 @ d26 = t26a, d21 = t21a
        mbutterfly0     d25, d22, d25, d22, d8, d10, q4, q5 @ d25 = t25,  d22 = t22
        mbutterfly0     d24, d23, d24, d23, d8, d10, q4, q5 @ d24 = t24a, d23 = t23a
        bx              lr
.endm

function idct32_odd
        movrel          r12, idct_coeffs

        @ Overwrite the idct16 coeffs with the stored ones for idct32
        vmovl.s16       q0,  d12
        vmovl.s16       q1,  d13
        vmovl.s16       q2,  d14
        vmovl.s16       q3,  d15

        mbutterfly      d16, d31, d0[0], d0[1], q4, q5 @ d16 = t16a, d31 = t31a
        mbutterfly      d24, d23, d1[0], d1[1], q4, q5 @ d24 = t17a, d23 = t30a
        mbutterfly      d20, d27, d2[0], d2[1], q4, q5 @ d20 = t18a, d27 = t29a
        mbutterfly      d28, d19, d3[0], d3[1], q4, q5 @ d28 = t19a, d19 = t28a
        mbutterfly      d18, d29, d4[0], d4[1], q4, q5 @ d18 = t20a, d29 = t27a
        mbutterfly      d26, d21, d5[0], d5[1], q4, q5 @ d26 = t21a, d21 = t26a
        mbutterfly      d22, d25, d6[0], d6[1], q4, q5 @ d22 = t22a, d25 = t25a
        mbutterfly      d30, d17, d7[0], d7[1], q4, q5 @ d30 = t23a, d17 = t24a

        @ Reload the idct16 coefficients. We could swap the coefficients between
        @ q0-q3 and q6-q7 by narrowing/lengthening, but that's slower than just
        @ loading and lengthening.
        vld1.16         {q0-q1}, [r12,:128]

        butterfly       d8,  d24, d16, d24 @ d8  = t16, d24 = t17
        butterfly       d9,  d20, d28, d20 @ d9  = t19, d20 = t18
        butterfly       d10, d26, d18, d26 @ d10 = t20, d26 = t21
        butterfly       d11, d22, d30, d22 @ d11 = t23, d22 = t22
        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
        butterfly       d28, d25, d17, d25 @ d28 = t24, d25 = t25
        butterfly       d30, d21, d29, d21 @ d30 = t27, d21 = t26
        butterfly       d29, d23, d31, d23 @ d29 = t31, d23 = t30
        butterfly       d31, d27, d19, d27 @ d31 = t28, d27 = t29

        mbutterfly      d23, d24, d2[0], d2[1], q8, q9        @ d23 = t17a, d24 = t30a
        mbutterfly      d27, d20, d2[0], d2[1], q8, q9, neg=1 @ d27 = t29a, d20 = t18a
        mbutterfly      d21, d26, d3[0], d3[1], q8, q9        @ d21 = t21a, d26 = t26a
        mbutterfly      d25, d22, d3[0], d3[1], q8, q9, neg=1 @ d25 = t25a, d22 = t22a
        idct32_end
endfunc

function idct32_odd_half
        movrel          r12, idct_coeffs

        vmovl.s16       q0,  d12
        vmovl.s16       q1,  d13
        vmovl.s16       q2,  d14
        vmovl.s16       q3,  d15

        mbutterfly_h1   d16, d31, d0[0], d0[1], q4, q5 @ d16 = t16a, d31 = t31a
        mbutterfly_h2   d24, d23, d1[0], d1[1], q4, q5 @ d24 = t17a, d23 = t30a
        mbutterfly_h1   d20, d27, d2[0], d2[1], q4, q5 @ d20 = t18a, d27 = t29a
        mbutterfly_h2   d28, d19, d3[0], d3[1], q4, q5 @ d28 = t19a, d19 = t28a
        mbutterfly_h1   d18, d29, d4[0], d4[1], q4, q5 @ d18 = t20a, d29 = t27a
        mbutterfly_h2   d26, d21, d5[0], d5[1], q4, q5 @ d26 = t21a, d21 = t26a
        mbutterfly_h1   d22, d25, d6[0], d6[1], q4, q5 @ d22 = t22a, d25 = t25a
        mbutterfly_h2   d30, d17, d7[0], d7[1], q4, q5 @ d30 = t23a, d17 = t24a

        vld1.16         {q0-q1}, [r12,:128]

        butterfly       d8,  d24, d16, d24 @ d8  = t16, d24 = t17
        butterfly       d9,  d20, d28, d20 @ d9  = t19, d20 = t18
        butterfly       d10, d26, d18, d26 @ d10 = t20, d26 = t21
        butterfly       d11, d22, d30, d22 @ d11 = t23, d22 = t22
        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
        butterfly       d28, d25, d17, d25 @ d28 = t24, d25 = t25
        butterfly       d30, d21, d29, d21 @ d30 = t27, d21 = t26
        butterfly       d29, d23, d31, d23 @ d29 = t31, d23 = t30
        butterfly       d31, d27, d19, d27 @ d31 = t28, d27 = t29

        mbutterfly      d23, d24, d2[0], d2[1], q8, q9        @ d23 = t17a, d24 = t30a
        mbutterfly      d27, d20, d2[0], d2[1], q8, q9, neg=1 @ d27 = t29a, d20 = t18a
        mbutterfly      d21, d26, d3[0], d3[1], q8, q9        @ d21 = t21a, d26 = t26a
        mbutterfly      d25, d22, d3[0], d3[1], q8, q9, neg=1 @ d25 = t25a, d22 = t22a
        idct32_end
endfunc

function idct32_odd_quarter
        movrel          r12, idct_coeffs

        vmovl.s16       q0,  d12
        vmovl.s16       q1,  d13
        vmovl.s16       q2,  d14
        vmovl.s16       q3,  d15

        vmov.s64        q14, #0
        vmov.s64        q5,  #0

        vmull.s32       q4,  d16, d0[0]
        vmlsl.s32       q14, d19, d3[1]
        vmull.s32       q15, d16, d0[1]
        vmull.s32       q11, d17, d7[0]
        vmlsl.s32       q5,  d17, d7[1]
        vmull.s32       q13, d19, d3[0]
        vmull.s32       q10, d18, d4[0]
        vmull.s32       q12, d18, d4[1]

        vld1.16         {q0-q1}, [r12,:128]

        vrshrn.s64      d8,  q4,  #14
        vrshrn.s64      d9,  q14, #14
        vrshrn.s64      d29, q15, #14
        vrshrn.s64      d28, q11, #14

        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0

        vrshrn.s64      d11, q5,  #14
        vrshrn.s64      d31, q13, #14
        vrshrn.s64      d10, q10, #14
        vrshrn.s64      d30, q12, #14

        mbutterfly_l    q8,  q9,  d29, d8,  d2[0], d2[1]
        mbutterfly_l    q13, q10, d31, d9,  d2[0], d2[1], neg=1
        vrshrn.s64      d23, q8,  #14
        vrshrn.s64      d24, q9,  #14
        vrshrn.s64      d27, q13, #14
        vrshrn.s64      d20, q10, #14
        mbutterfly_l    q8,  q9,  d30, d10, d3[0], d3[1]
        vrshrn.s64      d21, q8,  #14
        vrshrn.s64      d26, q9,  #14
        mbutterfly_l    q8,  q9,  d28, d11, d3[0], d3[1], neg=1
        vrshrn.s64      d25, q8,  #14
        vrshrn.s64      d22, q9,  #14

        idct32_end
endfunc

.macro idct32_funcs suffix
@ Do an 32-point IDCT of a 2x32 slice out of a 32x32 matrix.
@ We don't have register space to do a single pass IDCT of 2x32 though,
@ but the 32-point IDCT can be decomposed into two 16-point IDCTs;
@ a normal IDCT16 with every other input component (the even ones, with
@ each output written twice), followed by a separate 16-point IDCT
@ of the odd inputs, added/subtracted onto the outputs of the first idct16.
@ r0 = dst (temp buffer)
@ r1 = unused
@ r2 = src
function idct32_1d_2x32_pass1\suffix\()_neon
        push            {lr}

        @ Double stride of the input, since we only read every other line
        mov             r12, #256
        vmov.s32        d8,  #0

        @ d16 = IN(0), d17 = IN(2) ... d31 = IN(30)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.32         {d\i}, [r2,:64]
        vst1.32         {d8},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.32         {d\i}, [r2,:64]
        vst1.32         {d8},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.32         {d\i}, [r2,:64]
        vst1.32         {d8},  [r2,:64], r12
.endr
.endif

        bl              idct16\suffix

        @ Do eight 2x2 transposes. Originally, d16-d31 contain the
        @ 16 rows. Afterwards, d16-d17, d18-d19 etc contain the eight
        @ transposed 2x2 blocks.
        transpose32_8x_2x2 d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        @ Store the registers a, b, c, d, e, f, g, h horizontally, followed
        @ by the same registers h, g, f, e, d, c, b, a mirrored.
.macro store_rev a, b, c, d, e, f, g, h
.irp i, \a, \b, \c, \d, \e, \f, \g, \h
        vst1.32         {d\i}, [r0,:64]!
        vrev64.32       d\i, d\i
.endr
.irp i, \h, \g, \f, \e, \d, \c, \b, \a
        vst1.32         {d\i}, [r0,:64]!
.endr
.endm
        store_rev       16, 18, 20, 22, 24, 26, 28, 30
        store_rev       17, 19, 21, 23, 25, 27, 29, 31
        sub             r0,  r0,  #256
.purgem store_rev

        @ Move r2 back to the start of the input, and move
        @ to the first odd row
.ifb \suffix
        sub             r2,  r2,  r12, lsl #4
.endif
.ifc \suffix,_quarter
        sub             r2,  r2,  r12, lsl #2
.endif
.ifc \suffix,_half
        sub             r2,  r2,  r12, lsl #3
.endif
        add             r2,  r2,  #128

        vmov.s32        d8,  #0
        @ d16 = IN(1), d17 = IN(3) ... d31 = IN(31)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d8},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d8},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d8},  [r2,:64], r12
.endr
.endif

        bl              idct32_odd\suffix

        transpose32_8x_2x2 d31, d30, d29, d28, d27, d26, d25, d24, d23, d22, d21, d20, d19, d18, d17, d16

        @ Store the registers a, b, c, d, e, f, g, h horizontally,
        @ adding into the output first, and then mirrored, subtracted
        @ from the output.
.macro store_rev a, b, c, d, e, f, g, h
.irp i, \a, \b, \c, \d, \e, \f, \g, \h
        vld1.32         {d8},  [r0,:64]
        vadd.s32        d8, d8, d\i
        vst1.32         {d8},  [r0,:64]!
        vrev64.32       d\i, d\i
.endr
.irp i, \h, \g, \f, \e, \d, \c, \b, \a
        vld1.32         {d8},  [r0,:64]
        vsub.s32        d8, d8, d\i
        vst1.32         {d8},  [r0,:64]!
.endr
.endm

        store_rev       31, 29, 27, 25, 23, 21, 19, 17
        store_rev       30, 28, 26, 24, 22, 20, 18, 16
.purgem store_rev
        pop             {pc}
endfunc
.ltorg

@ This is mostly the same as 2x32_pass1, but without the transpose,
@ and use the source as temp buffer between the two idct passes, and
@ add into the destination.
@ r0 = dst
@ r1 = dst stride
@ r2 = src (temp buffer)
function idct32_1d_2x32_pass2\suffix\()_neon
        push            {lr}

        mov             r12, #256
        @ d16 = IN(0), d17 = IN(2) ... d31 = IN(30)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.32         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #4
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.32         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #2
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.32         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #3
.endif

        bl              idct16\suffix

.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vst1.32         {d\i}, [r2,:64], r12
.endr

        sub             r2,  r2,  r12, lsl #4
        add             r2,  r2,  #128

        @ d16 = IN(1), d17 = IN(3) ... d31 = IN(31)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.32         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #4
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.32         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #2
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.32         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #3
.endif
        sub             r2,  r2,  #128

        bl              idct32_odd\suffix

        @ Narrow the ict16 coefficients in q0-q3 into q0-q1, to
        @ allow clobbering q2-q3 below.
        vmovn.s32       d0,  q0
        vmovn.s32       d1,  q1
        vmovn.s32       d2,  q2
        vmovn.s32       d3,  q3

        mov             r12, #256
        vdup.s16        q4,  r9
.macro load_acc_store a, b, c, d, neg=0
        vld1.32         {d4},  [r2,:64], r12
        vld1.32         {d5},  [r2,:64], r12
.if \neg == 0
        vadd.s32        d4,  d4,  d\a
        vld1.32         {d6},  [r2,:64], r12
        vadd.s32        d5,  d5,  d\b
        vld1.32         {d7},  [r2,:64], r12
        vadd.s32        d6,  d6,  d\c
        vadd.s32        d7,  d7,  d\d
.else
        vsub.s32        d4,  d4,  d\a
        vld1.32         {d6},  [r2,:64], r12
        vsub.s32        d5,  d5,  d\b
        vld1.32         {d7},  [r2,:64], r12
        vsub.s32        d6,  d6,  d\c
        vsub.s32        d7,  d7,  d\d
.endif
        vld1.32         {d10[]},  [r0,:32], r1
        vld1.32         {d10[1]}, [r0,:32], r1
        vrshr.s32       q2,  q2,  #6
        vld1.32         {d11[]},  [r0,:32], r1
        vrshr.s32       q3,  q3,  #6
        vld1.32         {d11[1]}, [r0,:32], r1
        sub             r0,  r0,  r1, lsl #2
        vaddw.u16       q2,  q2,  d10
        vaddw.u16       q3,  q3,  d11
        vqmovun.s32     d4,  q2
        vqmovun.s32     d5,  q3
        vmin.u16        q2,  q2,  q4
        vst1.32         {d4[0]},  [r0,:32], r1
        vst1.32         {d4[1]},  [r0,:32], r1
        vst1.32         {d5[0]},  [r0,:32], r1
        vst1.32         {d5[1]},  [r0,:32], r1
.endm
        load_acc_store  31, 30, 29, 28
        load_acc_store  27, 26, 25, 24
        load_acc_store  23, 22, 21, 20
        load_acc_store  19, 18, 17, 16
        sub             r2,  r2,  r12
        neg             r12, r12
        load_acc_store  16, 17, 18, 19, 1
        load_acc_store  20, 21, 22, 23, 1
        load_acc_store  24, 25, 26, 27, 1
        load_acc_store  28, 29, 30, 31, 1
.purgem load_acc_store
        @ Lengthen the idct16 coeffs back into 32 bit form
        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0
        pop             {pc}
endfunc
.endm

idct32_funcs
idct32_funcs _quarter
idct32_funcs _half

const min_eob_idct_idct_32, align=4
        .short  0, 3, 9, 21, 34, 51, 70, 98, 135, 176, 240, 258, 336, 357, 448, 472
endconst

function vp9_idct_idct_32x32_add_16_neon
        cmp             r3,  #1
        beq             idct32x32_dc_add_neon
        vpush           {q4-q7}
        movrel          r8,  min_eob_idct_idct_32 + 2

        @ Align the stack, allocate a temp buffer
T       mov             r7,  sp
T       and             r7,  r7,  #15
A       and             r7,  sp,  #15
        add             r7,  r7,  #4096
        sub             sp,  sp,  r7

        mov             r4,  r0
        mov             r5,  r1
        mov             r6,  r2

        movrel          r12, idct_coeffs
        vld1.16         {q0-q1}, [r12,:128]!
        vld1.16         {q6-q7}, [r12,:128]
        vmovl.s16       q2,  d2
        vmovl.s16       q3,  d3
        vmovl.s16       q1,  d1
        vmovl.s16       q0,  d0

        cmp             r3,  #34
        ble             idct32x32_quarter_add_16_neon
        cmp             r3,  #135
        ble             idct32x32_half_add_16_neon

.irp i, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
        add             r0,  sp,  #(\i*128)
.if \i > 0
        ldrh_post       r1,  r8,  #2
        cmp             r3,  r1
        it              le
        movle           r1,  #(32 - \i)/2
        ble             1f
.endif
        add             r2,  r6,  #(\i*4)
        bl              idct32_1d_2x32_pass1_neon
.endr
        b               3f

1:
        @ Write zeros to the temp buffer for pass 2
        vmov.i16        q14, #0
        vmov.i16        q15, #0
2:
        subs            r1,  r1,  #1
.rept 2
        @ Fill one line with zeros
        vst1.16         {q14-q15}, [r0,:128]!
        vst1.16         {q14-q15}, [r0,:128]!
        vst1.16         {q14-q15}, [r0,:128]!
        vst1.16         {q14-q15}, [r0,:128]!
.endr
        bne             2b
3:
.irp i, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
        add             r0,  r4,  #(\i*2)
        mov             r1,  r5
        add             r2,  sp,  #(\i*4)
        bl              idct32_1d_2x32_pass2_neon
.endr

        add             sp,  sp,  r7
        vpop            {q4-q7}
        pop             {r4-r9,pc}
endfunc

function ff_vp9_idct_idct_32x32_add_10_neon, export=1
        push            {r4-r9,lr}
        movw            r9,  #0x03ff
        b               vp9_idct_idct_32x32_add_16_neon
endfunc

function ff_vp9_idct_idct_32x32_add_12_neon, export=1
        push            {r4-r9,lr}
        movw            r9,  #0x0fff
        b               vp9_idct_idct_32x32_add_16_neon
endfunc

.macro idct32_partial size, rows
function idct32x32_\size\()_add_16_neon
.irp i, 0, 2, 4, 6
        add             r0,  sp,  #(\i*128)
.ifc \size,quarter
.if \i > 0
        ldrh_post       r1,  r8,  #2
        cmp             r3,  r1
        it              le
        movle           r1,  #(\rows - \i)/2
        ble             1f
.endif
.endif
        add             r2,  r6,  #(\i*4)
        bl              idct32_1d_2x32_pass1_\size\()_neon
.endr
.ifc \size,half
        add             r8,  r8,  #8
.irp i, 8, 10, 12, 14
        add             r0,  sp,  #(\i*128)
.if \i > 8
        ldrh_post       r1,  r8,  #2
        cmp             r3,  r1
        it              le
        movle           r1,  #(\rows - \i)/2
        ble             1f
.endif
        add             r2,  r6,  #(\i*4)
        bl              idct32_1d_2x32_pass1_\size\()_neon
.endr
.endif
        b               3f

1:
        @ Write zeros to the temp buffer for pass 2
        vmov.i16        q14, #0
        vmov.i16        q15, #0
2:
        subs            r1,  r1,  #1
.rept 2
        @ Fill one line with zeros
        vst1.16         {q14-q15}, [r0,:128]!
        vst1.16         {q14-q15}, [r0,:128]!
        vst1.16         {q14-q15}, [r0,:128]!
        vst1.16         {q14-q15}, [r0,:128]!
.endr
        bne             2b
3:
.irp i, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
        add             r0,  r4,  #(\i*2)
        mov             r1,  r5
        add             r2,  sp,  #(\i*4)
        bl              idct32_1d_2x32_pass2_\size\()_neon
.endr

        add             sp,  sp,  r7
        vpop            {q4-q7}
        pop             {r4-r9,pc}
endfunc
.endm

idct32_partial quarter, 8
idct32_partial half, 16
