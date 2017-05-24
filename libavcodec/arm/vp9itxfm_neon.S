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

@ Do four 4x4 transposes, using q registers for the subtransposes that don't
@ need to address the individual d registers.
@ r0,r1 == rq1, r2,r3 == rq1, etc
.macro transpose16_q_4x_4x4 rq0, rq1, rq2, rq3, rq4, rq5, rq6, rq7, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15
        vtrn.32          \rq0, \rq1
        vtrn.32          \rq2, \rq3
        vtrn.32          \rq4, \rq5
        vtrn.32          \rq6, \rq7
        vtrn.16          \r0,  \r1
        vtrn.16          \r2,  \r3
        vtrn.16          \r4,  \r5
        vtrn.16          \r6,  \r7
        vtrn.16          \r8,  \r9
        vtrn.16          \r10, \r11
        vtrn.16          \r12, \r13
        vtrn.16          \r14, \r15
.endm

@ out1 = ((in1 + in2) * d0[0] + (1 << 13)) >> 14
@ out2 = ((in1 - in2) * d0[0] + (1 << 13)) >> 14
@ in/out are d registers
.macro mbutterfly0 out1, out2, in1, in2, tmpd1, tmpd2, tmpq3, tmpq4, neg=0
        vadd.s16        \tmpd1, \in1,  \in2
        vsub.s16        \tmpd2, \in1,  \in2
        vmull.s16       \tmpq3, \tmpd1, d0[0]
        vmull.s16       \tmpq4, \tmpd2, d0[0]
.if \neg > 0
        vneg.s32        \tmpq3, \tmpq3
.endif
        vrshrn.s32      \out1, \tmpq3, #14
        vrshrn.s32      \out2, \tmpq4, #14
.endm

@ Same as mbutterfly0 above, but treating the input in in2 as zero,
@ writing the same output into both out1 and out2.
.macro mbutterfly0_h out1, out2, in1, in2, tmpd1, tmpd2, tmpq3, tmpq4
        vmull.s16       \tmpq3, \in1, d0[0]
        vrshrn.s32      \out1,  \tmpq3, #14
        vrshrn.s32      \out2,  \tmpq3, #14
.endm

@ out1,out2 = ((in1 + in2) * d0[0] + (1 << 13)) >> 14
@ out3,out4 = ((in1 - in2) * d0[0] + (1 << 13)) >> 14
@ Same as mbutterfly0, but with input being 2 q registers, output
@ being 4 d registers.
@ This can do with either 4 or 6 temporary q registers.
.macro dmbutterfly0 out1, out2, out3, out4, in1, in2, tmpq1, tmpq2, tmpd11, tmpd12, tmpd21, tmpd22, tmpq3, tmpq4, tmpq5, tmpq6
        vadd.s16        \tmpq1, \in1,  \in2
        vsub.s16        \tmpq2, \in1,  \in2
        vmull.s16       \tmpq3, \tmpd11, d0[0]
        vmull.s16       \tmpq4, \tmpd12, d0[0]
.ifb \tmpq5
        vrshrn.s32      \out1, \tmpq3, #14
        vrshrn.s32      \out2, \tmpq4, #14
        vmull.s16       \tmpq3, \tmpd21, d0[0]
        vmull.s16       \tmpq4, \tmpd22, d0[0]
        vrshrn.s32      \out3, \tmpq3, #14
        vrshrn.s32      \out4, \tmpq4, #14
.else
        vmull.s16       \tmpq5, \tmpd21, d0[0]
        vmull.s16       \tmpq6, \tmpd22, d0[0]
        vrshrn.s32      \out1, \tmpq3, #14
        vrshrn.s32      \out2, \tmpq4, #14
        vrshrn.s32      \out3, \tmpq5, #14
        vrshrn.s32      \out4, \tmpq6, #14
.endif
.endm

@ out1 = in1 * coef1 - in2 * coef2
@ out2 = in1 * coef2 + in2 * coef1
@ out are 2 q registers, in are 2 d registers
.macro mbutterfly_l out1, out2, in1, in2, coef1, coef2
        vmull.s16       \out1, \in1, \coef1
        vmlsl.s16       \out1, \in2, \coef2
        vmull.s16       \out2, \in1, \coef2
        vmlal.s16       \out2, \in2, \coef1
.endm

@ out1,out2 = in1,in2 * coef1 - in3,in4 * coef2
@ out3,out4 = in1,in2 * coef2 + in3,in4 * coef1
@ out are 4 q registers, in are 4 d registers
.macro dmbutterfly_l out1, out2, out3, out4, in1, in2, in3, in4, coef1, coef2
        vmull.s16       \out1, \in1, \coef1
        vmull.s16       \out2, \in2, \coef1
        vmull.s16       \out3, \in1, \coef2
        vmull.s16       \out4, \in2, \coef2
        vmlsl.s16       \out1, \in3, \coef2
        vmlsl.s16       \out2, \in4, \coef2
        vmlal.s16       \out3, \in3, \coef1
        vmlal.s16       \out4, \in4, \coef1
.endm

@ inout1 = (inout1 * coef1 - inout2 * coef2 + (1 << 13)) >> 14
@ inout2 = (inout1 * coef2 + inout2 * coef1 + (1 << 13)) >> 14
@ inout are 2 d registers, tmp are 2 q registers
.macro mbutterfly inout1, inout2, coef1, coef2, tmp1, tmp2, neg=0
        mbutterfly_l    \tmp1, \tmp2, \inout1, \inout2, \coef1, \coef2
.if \neg > 0
        vneg.s32        \tmp2, \tmp2
.endif
        vrshrn.s32      \inout1, \tmp1,  #14
        vrshrn.s32      \inout2, \tmp2,  #14
.endm

@ Same as mbutterfly above, but treating the input in inout2 as zero
.macro mbutterfly_h1 inout1, inout2, coef1, coef2, tmp1, tmp2
        vmull.s16       \tmp1,   \inout1, \coef1
        vmull.s16       \tmp2,   \inout1, \coef2
        vrshrn.s32      \inout1, \tmp1,   #14
        vrshrn.s32      \inout2, \tmp2,   #14
.endm

@ Same as mbutterfly above, but treating the input in inout1 as zero
.macro mbutterfly_h2 inout1, inout2, coef1, coef2, tmp1, tmp2
        vmull.s16       \tmp1,   \inout2, \coef2
        vmull.s16       \tmp2,   \inout2, \coef1
        vneg.s32        \tmp1,   \tmp1
        vrshrn.s32      \inout2, \tmp2,   #14
        vrshrn.s32      \inout1, \tmp1,   #14
.endm

@ inout1,inout2 = (inout1,inout2 * coef1 - inout3,inout4 * coef2 + (1 << 13)) >> 14
@ inout3,inout4 = (inout1,inout2 * coef2 + inout3,inout4 * coef1 + (1 << 13)) >> 14
@ inout are 4 d registers, tmp are 4 q registers
.macro dmbutterfly inout1, inout2, inout3, inout4, coef1, coef2, tmp1, tmp2, tmp3, tmp4
        dmbutterfly_l   \tmp1, \tmp2, \tmp3, \tmp4, \inout1, \inout2, \inout3, \inout4, \coef1, \coef2
        vrshrn.s32      \inout1, \tmp1,  #14
        vrshrn.s32      \inout2, \tmp2,  #14
        vrshrn.s32      \inout3, \tmp3,  #14
        vrshrn.s32      \inout4, \tmp4,  #14
.endm

@ out1 = in1 + in2
@ out2 = in1 - in2
.macro butterfly out1, out2, in1, in2
        vadd.s16        \out1, \in1, \in2
        vsub.s16        \out2, \in1, \in2
.endm

@ out1 = in1 - in2
@ out2 = in1 + in2
.macro butterfly_r out1, out2, in1, in2
        vsub.s16        \out1, \in1, \in2
        vadd.s16        \out2, \in1, \in2
.endm

@ out1 = (in1 + in2 + (1 << 13)) >> 14
@ out2 = (in1 - in2 + (1 << 13)) >> 14
@ out are 2 d registers, in are 2 q registers, tmp are 2 q registers
.macro butterfly_n out1, out2, in1, in2, tmp1, tmp2
        vadd.s32        \tmp1, \in1, \in2
        vsub.s32        \tmp2, \in1, \in2
        vrshrn.s32      \out1, \tmp1,  #14
        vrshrn.s32      \out2, \tmp2,  #14
.endm

@ out1,out2 = (in1,in2 + in3,in4 + (1 << 13)) >> 14
@ out3,out4 = (in1,in2 - in3,in4 + (1 << 13)) >> 14
@ out are 4 d registers, in are 4 q registers, tmp are 4 q registers
.macro dbutterfly_n out1, out2, out3, out4, in1, in2, in3, in4, tmp1, tmp2, tmp3, tmp4
        vadd.s32        \tmp1, \in1, \in3
        vadd.s32        \tmp2, \in2, \in4
        vsub.s32        \tmp3, \in1, \in3
        vsub.s32        \tmp4, \in2, \in4
        vrshrn.s32      \out1, \tmp1,  #14
        vrshrn.s32      \out2, \tmp2,  #14
        vrshrn.s32      \out3, \tmp3,  #14
        vrshrn.s32      \out4, \tmp4,  #14
.endm


.macro iwht4 c0, c1, c2, c3
        vadd.i16        \c0,  \c0,  \c1
        vsub.i16        d17,  \c2,  \c3
        vsub.i16        d16,  \c0,  d17
        vshr.s16        d16,  d16,  #1
        vsub.i16        \c2,  d16,  \c1
        vsub.i16        \c1,  d16,  \c3
        vadd.i16        \c3,  d17,  \c2
        vsub.i16        \c0,  \c0,  \c1
.endm

.macro idct4 c0, c1, c2, c3
        vmull.s16       q13,  \c1,  d0[3]
        vmull.s16       q11,  \c1,  d0[2]
        vadd.i16        d16,  \c0,  \c2
        vsub.i16        d17,  \c0,  \c2
        vmlal.s16       q13,  \c3,  d0[2]
        vmull.s16       q9,   d16,  d0[0]
        vmull.s16       q10,  d17,  d0[0]
        vmlsl.s16       q11,  \c3,  d0[3]
        vrshrn.s32      d26,  q13,  #14
        vrshrn.s32      d18,  q9,   #14
        vrshrn.s32      d20,  q10,  #14
        vrshrn.s32      d22,  q11,  #14
        vadd.i16        \c0,  d18,  d26
        vsub.i16        \c3,  d18,  d26
        vadd.i16        \c1,  d20,  d22
        vsub.i16        \c2,  d20,  d22
.endm

.macro iadst4 c0, c1, c2, c3
        vmull.s16       q10,  \c0,  d1[0]
        vmlal.s16       q10,  \c2,  d1[1]
        vmlal.s16       q10,  \c3,  d1[2]
        vmull.s16       q11,  \c0,  d1[2]
        vmlsl.s16       q11,  \c2,  d1[0]
        vsub.s16        \c0,  \c0,  \c2
        vmlsl.s16       q11,  \c3,  d1[1]
        vadd.s16        \c0,  \c0,  \c3
        vmull.s16       q13,  \c1,  d1[3]
        vmull.s16       q12,  \c0,  d1[3]
        vadd.s32        q14,  q10,  q13
        vadd.s32        q1,   q11,  q13
        vrshrn.s32      \c0,  q14,  #14
        vadd.s32        q10,  q10,  q11
        vrshrn.s32      \c1,  q1,   #14
        vsub.s32        q10,  q10,  q13
        vrshrn.s32      \c2,  q12,  #14
        vrshrn.s32      \c3,  q10,  #14
.endm

@ The public functions in this file have got the following signature:
@ void itxfm_add(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);

.macro itxfm_func4x4 txfm1, txfm2
function ff_vp9_\txfm1\()_\txfm2\()_4x4_add_neon, export=1
.ifc \txfm1,\txfm2
.ifc \txfm1,idct
        movrel          r12, itxfm4_coeffs
        vld1.16         {d0}, [r12,:64]
.endif
.ifc \txfm1,iadst
        movrel          r12, iadst4_coeffs
        vld1.16         {d1}, [r12,:64]
.endif
.else
        movrel          r12, itxfm4_coeffs
        vld1.16         {q0}, [r12,:128]
.endif

        vmov.i16        q15, #0
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #1
        bne             1f
        @ DC-only for idct/idct
        vld1.16         {d4[]},   [r2,:16]
        vmull.s16       q2,  d4,  d0[0]
        vrshrn.s32      d4,  q2,  #14
        vmull.s16       q2,  d4,  d0[0]
        vrshrn.s32      d4,  q2,  #14
        vst1.16         {d30[0]}, [r2,:16]
        vdup.16         q2,  d4[0]
        vmov            q3,  q2
        b               2f
.endif

1:
        vld1.16         {d4-d7},  [r2,:128]
        vst1.16         {q15}, [r2,:128]!

.ifc \txfm1,iwht
        vshr.s16        q2,  q2,  #2
        vshr.s16        q3,  q3,  #2
.endif

        \txfm1\()4      d4,  d5,  d6,  d7

        vst1.16         {q15}, [r2,:128]!
        @ Transpose 4x4 with 16 bit elements
        vtrn.16         d4,  d5
        vtrn.16         d6,  d7
        vtrn.32         q2,  q3

        \txfm2\()4      d4,  d5,  d6,  d7
2:
        vld1.32         {d0[]},   [r0,:32], r1
        vld1.32         {d0[1]},  [r0,:32], r1
.ifnc \txfm1,iwht
        vrshr.s16       q2,  q2,  #4
        vrshr.s16       q3,  q3,  #4
.endif
        vaddw.u8        q2,  q2,  d0
        vld1.32         {d1[]},   [r0,:32], r1
        vld1.32         {d1[1]},  [r0,:32], r1
        vqmovun.s16     d0,  q2
        sub             r0,  r0,  r1, lsl #2

        vaddw.u8        q3,  q3,  d1
        vst1.32         {d0[0]},  [r0,:32], r1
        vqmovun.s16     d1,  q3

        vst1.32         {d0[1]},  [r0,:32], r1
        vst1.32         {d1[0]},  [r0,:32], r1
        vst1.32         {d1[1]},  [r0,:32], r1

        bx              lr
endfunc
.endm

itxfm_func4x4 idct,  idct
itxfm_func4x4 iadst, idct
itxfm_func4x4 idct,  iadst
itxfm_func4x4 iadst, iadst
itxfm_func4x4 iwht,  iwht


.macro idct8
        dmbutterfly0    d16, d17, d24, d25, q8,  q12, q2, q4, d4, d5, d8, d9, q3, q2, q5, q4 @ q8 = t0a, q12 = t1a
        dmbutterfly     d20, d21, d28, d29, d0[2], d0[3], q2,  q3,  q4,  q5 @ q10 = t2a, q14 = t3a
        dmbutterfly     d18, d19, d30, d31, d1[0], d1[1], q2,  q3,  q4,  q5 @ q9  = t4a, q15 = t7a
        dmbutterfly     d26, d27, d22, d23, d1[2], d1[3], q2,  q3,  q4,  q5 @ q13 = t5a, q11 = t6a

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
        dmbutterfly_l   q4,  q5,  q2,  q3,  d30, d31, d16, d17, d2[1], d2[0] @ q4,q5  = t1a, q2,q3 = t0a
        dmbutterfly_l   q8,  q15, q6,  q7,  d22, d23, d24, d25, d3[1], d3[0] @ q8,q15 = t5a, q6,q7 = t4a

        dbutterfly_n    d22, d23, d4,  d5,  q2,  q3,  q6,  q7,  q11, q12, q2,  q3 @ q11 = t0, q2 = t4

        dbutterfly_n    d24, d25, d6,  d7,  q4,  q5,  q8,  q15, q12, q3,  q6,  q7 @ q12 = t1, q3 = t5

        dmbutterfly_l   q6,  q7,  q4,  q5,  d26, d27, d20, d21, d2[3], d2[2] @ q6,q7 = t3a, q4,q5 = t2a
        dmbutterfly_l   q10, q13, q8,  q15, d18, d19, d28, d29, d3[3], d3[2] @ q10,q13 = t7a, q8,q15 = t6a

        dbutterfly_n    d18, d19, d8,  d9,  q4,  q5,  q8,  q15, q9,  q14, q4, q5 @ q9 = t2, q4 = t6
        dbutterfly_n    d16, d17, d12, d13, q6,  q7,  q10, q13, q8,  q15, q6, q7 @ q8 = t3, q6 = t7

        butterfly       q15, q12, q12, q8 @ q15 = -out[7], q12 = t3
        vneg.s16        q15, q15          @ q15 = out[7]
        butterfly       q8,  q9,  q11, q9 @ q8 = out[0], q9 = t2

        dmbutterfly_l   q10, q11, q5,  q7,  d4,  d5,  d6,  d7,  d0[2], d0[3] @ q10,q11 = t5a, q5,q7 = t4a
        dmbutterfly_l   q2,  q3,  q13, q14, d12, d13, d8,  d9,  d0[3], d0[2] @ q2,q3 = t6a, q13,q14 = t7a

        dbutterfly_n    d28, d29, d8,  d9,  q10, q11, q13, q14, q4,  q6,  q10, q11 @ q14 = out[6], q4 = t7

        dmbutterfly0    d22, d23, d24, d25, q9,  q12, q6, q13, d12, d13, d26, d27, q9, q10 @ q11 = -out[3], q12 = out[4]
        vneg.s16        q11, q11      @ q11 = out[3]

        dbutterfly_n    d18, d19, d4,  d5,  q5,  q7,  q2,  q3,  q9, q10, q2,  q3 @ q9 = -out[1], q2 = t6
        vneg.s16        q9,  q9       @ q9 = out[1]

        dmbutterfly0    d20, d21, d26, d27, q2,  q4,  q3, q5,  d6,  d7,  d10, d11, q6,  q7 @ q10 = out[2], q13 = -out[5]
        vneg.s16        q13, q13      @ q13 = out[5]
.endm


.macro itxfm_func8x8 txfm1, txfm2
function ff_vp9_\txfm1\()_\txfm2\()_8x8_add_neon, export=1
        @ Push q4-q7 if iadst is used, idct requires
        @ a few scratch registers less, so only push q4-q5
        @ if only idct is involved.
        @ The iadst also uses a few coefficients from
        @ idct, so those always need to be loaded.
.ifc \txfm1\()_\txfm2,idct_idct
        movrel          r12, idct_coeffs
        vpush           {q4-q5}
.else
        movrel          r12, iadst8_coeffs
        vld1.16         {q1}, [r12,:128]!
        vpush           {q4-q7}
.endif
        vld1.16         {q0}, [r12,:128]

        vmov.i16        q2, #0
        vmov.i16        q3, #0

.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #1
        bne             1f
        @ DC-only for idct/idct
        vld1.16         {d16[]}, [r2,:16]
        vmull.s16       q8,  d16, d0[0]
        vrshrn.s32      d16, q8,  #14
        vmull.s16       q8,  d16, d0[0]
        vrshrn.s32      d16, q8,  #14
        vdup.16         q8,  d16[0]
        vmov            q9,  q8
        vmov            q10, q8
        vmov            q11, q8
        vmov            q12, q8
        vmov            q13, q8
        vmov            q14, q8
        vmov            q15, q8
        vst1.16         {d4[0]}, [r2,:16]
        b               2f
.endif
1:
        vld1.16         {q8-q9},    [r2,:128]!
        vld1.16         {q10-q11},  [r2,:128]!
        vld1.16         {q12-q13},  [r2,:128]!
        vld1.16         {q14-q15},  [r2,:128]!
        sub             r2,  r2,  #128
        vst1.16         {q2-q3}, [r2,:128]!
        vst1.16         {q2-q3}, [r2,:128]!
        vst1.16         {q2-q3}, [r2,:128]!
        vst1.16         {q2-q3}, [r2,:128]!

        \txfm1\()8

        @ Transpose 8x8 with 16 bit elements
        vswp            d17, d24
        vswp            d19, d26
        vswp            d21, d28
        vswp            d23, d30
        transpose16_4x4 q8, q9, q10, q11, q12, q13, q14, q15

        \txfm2\()8
2:
        mov             r3,  r0
        @ Add into the destination
        vld1.8          {d4},  [r0,:64], r1
        vrshr.s16       q8,  q8,  #5
        vld1.8          {d5},  [r0,:64], r1
        vrshr.s16       q9,  q9,  #5
        vld1.8          {d6},  [r0,:64], r1
        vrshr.s16       q10, q10, #5
        vaddw.u8        q8,  q8,  d4
        vld1.8          {d7},  [r0,:64], r1
        vrshr.s16       q11, q11, #5
        vaddw.u8        q9,  q9,  d5
        vld1.8          {d8},  [r0,:64], r1
        vrshr.s16       q12, q12, #5
        vaddw.u8        q10, q10, d6
        vqmovun.s16     d4,  q8
        vld1.8          {d9},  [r0,:64], r1
        vrshr.s16       q13, q13, #5
        vaddw.u8        q11, q11, d7
        vqmovun.s16     d5,  q9
        vld1.8          {d10}, [r0,:64], r1
        vrshr.s16       q14, q14, #5
        vaddw.u8        q12, q12, d8
        vqmovun.s16     d6,  q10
        vld1.8          {d11}, [r0,:64], r1
        vrshr.s16       q15, q15, #5
        vaddw.u8        q13, q13, d9
        vqmovun.s16     d7,  q11


        vst1.8          {d4},  [r3,:64], r1
        vaddw.u8        q14, q14, d10
        vst1.8          {d5},  [r3,:64], r1
        vqmovun.s16     d8,  q12
        vst1.8          {d6},  [r3,:64], r1
        vaddw.u8        q15, q15, d11
        vst1.8          {d7},  [r3,:64], r1
        vqmovun.s16     d9,  q13
        vst1.8          {d8},  [r3,:64], r1
        vqmovun.s16     d10, q14
        vst1.8          {d9},  [r3,:64], r1
        vqmovun.s16     d11, q15

        vst1.8          {d10}, [r3,:64], r1
        vst1.8          {d11}, [r3,:64], r1

.ifc \txfm1\()_\txfm2,idct_idct
        vpop            {q4-q5}
.else
        vpop            {q4-q7}
.endif
        bx              lr
endfunc
.endm

itxfm_func8x8 idct,  idct
itxfm_func8x8 iadst, idct
.ltorg
itxfm_func8x8 idct,  iadst
itxfm_func8x8 iadst, iadst


function idct16x16_dc_add_neon
        movrel          r12, idct_coeffs
        vld1.16         {d0}, [r12,:64]

        vmov.i16        q2,  #0

        vld1.16         {d16[]}, [r2,:16]
        vmull.s16       q8,  d16, d0[0]
        vrshrn.s32      d16, q8,  #14
        vmull.s16       q8,  d16, d0[0]
        vrshrn.s32      d16, q8,  #14
        vdup.16         q8,  d16[0]
        vst1.16         {d4[0]}, [r2,:16]

        vrshr.s16       q8,  q8,  #6

        mov             r3,  r0
        mov             r12, #16
1:
        @ Loop to add the constant from q8 into all 16x16 outputs
        subs            r12, r12, #2
        vld1.8          {q2},  [r0,:128], r1
        vaddw.u8        q10, q8,  d4
        vld1.8          {q3},  [r0,:128], r1
        vaddw.u8        q11, q8,  d5
        vaddw.u8        q12, q8,  d6
        vaddw.u8        q13, q8,  d7
        vqmovun.s16     d4,  q10
        vqmovun.s16     d5,  q11
        vqmovun.s16     d6,  q12
        vst1.8          {q2},  [r3,:128], r1
        vqmovun.s16     d7,  q13
        vst1.8          {q3},  [r3,:128], r1
        bne             1b

        bx              lr
endfunc
.ltorg

.macro idct16_end
        butterfly       d18, d7,  d4,  d7                @ d18 = t0a,  d7  = t7a
        butterfly       d19, d22, d5,  d22               @ d19 = t1a,  d22 = t6
        butterfly       d4,  d26, d20, d26               @ d4  = t2a,  d26 = t5
        butterfly       d5,  d6,  d28, d6                @ d5  = t3a,  d6  = t4
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
        butterfly       d23, d24, d7,  d20               @ d23 = out[7], d24 = out[8]
        butterfly       d18, d29, d4,  d29               @ d18 = out[2], d29 = out[13]
        butterfly       d19, d28, d5,  d28               @ d19 = out[3], d28 = out[12]
        vmov            d4,  d21                         @ d4  = t10a
        butterfly       d20, d27, d6,  d27               @ d20 = out[4], d27 = out[11]
        butterfly       d21, d26, d26, d4                @ d21 = out[5], d26 = out[10]
        bx              lr
.endm

function idct16
        mbutterfly0     d16, d24, d16, d24, d4, d6,  q2,  q3 @ d16 = t0a,  d24 = t1a
        mbutterfly      d20, d28, d0[2], d0[3], q2,  q3  @ d20 = t2a,  d28 = t3a
        mbutterfly      d18, d30, d1[0], d1[1], q2,  q3  @ d18 = t4a,  d30 = t7a
        mbutterfly      d26, d22, d1[2], d1[3], q2,  q3  @ d26 = t5a,  d22 = t6a
        mbutterfly      d17, d31, d2[0], d2[1], q2,  q3  @ d17 = t8a,  d31 = t15a
        mbutterfly      d25, d23, d2[2], d2[3], q2,  q3  @ d25 = t9a,  d23 = t14a
        mbutterfly      d21, d27, d3[0], d3[1], q2,  q3  @ d21 = t10a, d27 = t13a
        mbutterfly      d29, d19, d3[2], d3[3], q2,  q3  @ d29 = t11a, d19 = t12a

        butterfly       d4,  d28, d16, d28               @ d4  = t0,   d28 = t3
        butterfly       d5,  d20, d24, d20               @ d5  = t1,   d20 = t2
        butterfly       d6,  d26, d18, d26               @ d6  = t4,   d26 = t5
        butterfly       d7,  d22, d30, d22               @ d7  = t7,   d22 = t6
        butterfly       d16, d25, d17, d25               @ d16 = t8,   d25 = t9
        butterfly       d24, d21, d29, d21               @ d24 = t11,  d21 = t10
        butterfly       d17, d27, d19, d27               @ d17 = t12,  d27 = t13
        butterfly       d29, d23, d31, d23               @ d29 = t15,  d23 = t14

        mbutterfly0     d22, d26, d22, d26, d18, d30, q9,  q15  @ d22 = t6a, d26 = t5a
        mbutterfly      d23, d25, d0[2], d0[3], q9,  q15        @ d23 = t9a,  d25 = t14a
        mbutterfly      d27, d21, d0[2], d0[3], q9,  q15, neg=1 @ d27 = t13a, d21 = t10a
        idct16_end
endfunc

function idct16_half
        mbutterfly0_h   d16, d24, d16, d24, d4, d6,  q2,  q3 @ d16 = t0a,  d24 = t1a
        mbutterfly_h1   d20, d28, d0[2], d0[3], q2,  q3  @ d20 = t2a,  d28 = t3a
        mbutterfly_h1   d18, d30, d1[0], d1[1], q2,  q3  @ d18 = t4a,  d30 = t7a
        mbutterfly_h2   d26, d22, d1[2], d1[3], q2,  q3  @ d26 = t5a,  d22 = t6a
        mbutterfly_h1   d17, d31, d2[0], d2[1], q2,  q3  @ d17 = t8a,  d31 = t15a
        mbutterfly_h2   d25, d23, d2[2], d2[3], q2,  q3  @ d25 = t9a,  d23 = t14a
        mbutterfly_h1   d21, d27, d3[0], d3[1], q2,  q3  @ d21 = t10a, d27 = t13a
        mbutterfly_h2   d29, d19, d3[2], d3[3], q2,  q3  @ d29 = t11a, d19 = t12a

        butterfly       d4,  d28, d16, d28               @ d4  = t0,   d28 = t3
        butterfly       d5,  d20, d24, d20               @ d5  = t1,   d20 = t2
        butterfly       d6,  d26, d18, d26               @ d6  = t4,   d26 = t5
        butterfly       d7,  d22, d30, d22               @ d7  = t7,   d22 = t6
        butterfly       d16, d25, d17, d25               @ d16 = t8,   d25 = t9
        butterfly       d24, d21, d29, d21               @ d24 = t11,  d21 = t10
        butterfly       d17, d27, d19, d27               @ d17 = t12,  d27 = t13
        butterfly       d29, d23, d31, d23               @ d29 = t15,  d23 = t14

        mbutterfly0     d22, d26, d22, d26, d18, d30, q9,  q15  @ d22 = t6a, d26 = t5a
        mbutterfly      d23, d25, d0[2], d0[3], q9,  q15        @ d23 = t9a,  d25 = t14a
        mbutterfly      d27, d21, d0[2], d0[3], q9,  q15, neg=1 @ d27 = t13a, d21 = t10a
        idct16_end
endfunc

function idct16_quarter
        vmull.s16       q12, d19, d3[3]
        vmull.s16       q2,  d17, d2[0]
        vmull.s16       q3,  d18, d1[1]
        vmull.s16       q15, d18, d1[0]
        vneg.s32        q12, q12
        vmull.s16       q14, d17, d2[1]
        vmull.s16       q13, d19, d3[2]
        vmull.s16       q11, d16, d0[0]
        vrshrn.s32      d24, q12, #14
        vrshrn.s32      d16, q2,  #14
        vrshrn.s32      d7,  q3,  #14
        vrshrn.s32      d6,  q15, #14
        vrshrn.s32      d29, q14, #14
        vrshrn.s32      d17, q13, #14
        vrshrn.s32      d28, q11, #14

        mbutterfly_l    q10, q11, d17, d24, d0[2], d0[3]
        mbutterfly_l    q9,  q15, d29, d16, d0[2], d0[3]
        vneg.s32        q11, q11
        vrshrn.s32      d27, q10, #14
        vrshrn.s32      d21, q11, #14
        vrshrn.s32      d23, q9,  #14
        vrshrn.s32      d25, q15, #14
        vmov            d4,  d28
        vmov            d5,  d28
        mbutterfly0     d22, d26, d7,  d6,  d18, d30, q9,  q15
        vmov            d20, d28
        idct16_end
endfunc

function iadst16
        movrel          r12, iadst16_coeffs
        vld1.16         {q0-q1}, [r12,:128]

        mbutterfly_l    q3,  q2,  d31, d16, d0[1], d0[0] @ q3  = t1,   q2  = t0
        mbutterfly_l    q5,  q4,  d23, d24, d1[1], d1[0] @ q5  = t9,   q4  = t8
        butterfly_n     d31, d24, q3,  q5,  q6,  q5      @ d31 = t1a,  d24 = t9a
        mbutterfly_l    q7,  q6,  d29, d18, d0[3], d0[2] @ q7  = t3,   q6  = t2
        butterfly_n     d16, d23, q2,  q4,  q3,  q4      @ d16 = t0a,  d23 = t8a

        mbutterfly_l    q3,  q2,  d21, d26, d1[3], d1[2] @ q3  = t11,  q2  = t10
        butterfly_n     d29, d26, q7,  q3,  q4,  q3      @ d29 = t3a,  d26 = t11a
        mbutterfly_l    q5,  q4,  d27, d20, d2[1], d2[0] @ q5  = t5,   q4  = t4
        butterfly_n     d18, d21, q6,  q2,  q3,  q2      @ d18 = t2a,  d21 = t10a

        mbutterfly_l    q7,  q6,  d19, d28, d3[1], d3[0] @ q7  = t13,  q6  = t12
        butterfly_n     d20, d28, q5,  q7,  q2,  q7      @ d20 = t5a,  d28 = t13a
        mbutterfly_l    q3,  q2,  d25, d22, d2[3], d2[2] @ q3  = t7,   q2  = t6
        butterfly_n     d27, d19, q4,  q6,  q5,  q6      @ d27 = t4a,  d19 = t12a

        mbutterfly_l    q5,  q4,  d17, d30, d3[3], d3[2] @ q5  = t15,  q4  = t14
        movrel          r12, idct_coeffs
        vld1.16         {q0}, [r12,:128]
        butterfly_n     d22, d30, q3,  q5,  q6,  q5      @ d22 = t7a,  d30 = t15a
        mbutterfly_l    q7,  q6,  d23, d24, d1[0], d1[1] @ q7  = t9,   q6  = t8
        butterfly_n     d25, d17, q2,  q4,  q3,  q4      @ d25 = t6a,  d17 = t14a

        mbutterfly_l    q2,  q3,  d28, d19, d1[1], d1[0] @ q2  = t12,  q3  = t13
        butterfly_n     d23, d19, q6,  q2,  q4,  q2      @ d23 = t8a,  d19 = t12a
        mbutterfly_l    q5,  q4,  d21, d26, d1[2], d1[3] @ q5  = t11,  q4  = t10
        butterfly_r     d4,  d27, d16, d27               @ d4  = t4,   d27 = t0
        butterfly_n     d24, d28, q7,  q3,  q6,  q3      @ d24 = t9a,  d28 = t13a

        mbutterfly_l    q6,  q7,  d30, d17, d1[3], d1[2] @ q6  = t14,  q7  = t15
        butterfly_r     d5,  d20, d31, d20               @ d5  = t5,   d20 = t1
        butterfly_n     d21, d17, q4,  q6,  q3,  q6      @ d21 = t10a, d17 = t14a
        butterfly_n     d26, d30, q5,  q7,  q4,  q7      @ d26 = t11a, d30 = t15a

        butterfly_r     d6,  d25, d18, d25               @ d6  = t6,   d25 = t2
        butterfly_r     d7,  d22, d29, d22               @ d7  = t7,   d22 = t3

        mbutterfly_l    q5,  q4,  d19, d28, d0[2], d0[3] @ q5  = t13,  q4  = t12
        mbutterfly_l    q6,  q7,  d30, d17, d0[3], d0[2] @ q6  = t14,  q7  = t15

        butterfly_n     d18, d30, q4,  q6,  q8,  q6      @ d18 = out[2],   d30 = t14a
        butterfly_n     d29, d17, q5,  q7,  q6,  q7      @ d29 = -out[13], d17 = t15a
        vneg.s16        d29, d29                         @ d29 = out[13]

        mbutterfly_l    q5,  q4,  d4,  d5,  d0[2], d0[3] @ q5  = t5a,  q4  = t4a
        mbutterfly_l    q6,  q7,  d7,  d6,  d0[3], d0[2] @ q6  = t6a,  q7  = t7a

        butterfly       d2,  d6,  d27, d25               @ d2 = out[0], d6 = t2a
        butterfly       d3,  d7,  d23, d21               @ d3 =-out[1], d7 = t10

        butterfly_n     d19, d31, q4,  q6,  q2,  q4      @ d19 = -out[3],  d31 = t6
        vneg.s16        d19, d19                         @ d19 = out[3]
        butterfly_n     d28, d16, q5,  q7,  q2,  q5      @ d28 = out[12],  d16 = t7

        butterfly       d5,  d8,  d20, d22               @ d5 =-out[15],d8 = t3a
        butterfly       d4,  d9,  d24, d26               @ d4 = out[14],d9 = t11

        mbutterfly0     d23, d24, d6,  d8,  d10, d11, q6,  q7, 1 @ d23 = out[7], d24 = out[8]
        mbutterfly0     d20, d27, d16, d31, d10, d11, q6,  q7    @ d20 = out[4], d27 = out[11]
        mbutterfly0     d22, d25, d9,  d7,  d10, d11, q6,  q7    @ d22 = out[6], d25 = out[9]
        mbutterfly0     d21, d26, d30, d17, d10, d11, q6,  q7, 1 @ d21 = out[5], d26 = out[10]

        vneg.s16        d31, d5                          @ d31 = out[15]
        vneg.s16        d17, d3                          @ d17 = out[1]

        vmov            d16, d2
        vmov            d30, d4
        bx              lr
endfunc

.macro load_add_store coef0, coef1, coef2, coef3
        vrshr.s16       \coef0, \coef0, #6
        vrshr.s16       \coef1, \coef1, #6

        vld1.32         {d4[]},   [r0,:32], r1
        vld1.32         {d4[1]},  [r3,:32], r1
        vrshr.s16       \coef2, \coef2, #6
        vrshr.s16       \coef3, \coef3, #6
        vld1.32         {d5[]},   [r0,:32], r1
        vld1.32         {d5[1]},  [r3,:32], r1
        vaddw.u8        \coef0, \coef0, d4
        vld1.32         {d6[]},   [r0,:32], r1
        vld1.32         {d6[1]},  [r3,:32], r1
        vaddw.u8        \coef1, \coef1, d5
        vld1.32         {d7[]},   [r0,:32], r1
        vld1.32         {d7[1]},  [r3,:32], r1

        vqmovun.s16     d4,  \coef0
        vqmovun.s16     d5,  \coef1
        sub             r0,  r0,  r1, lsl #2
        sub             r3,  r3,  r1, lsl #2
        vaddw.u8        \coef2, \coef2, d6
        vaddw.u8        \coef3, \coef3, d7
        vst1.32         {d4[0]},  [r0,:32], r1
        vst1.32         {d4[1]},  [r3,:32], r1
        vqmovun.s16     d6,  \coef2
        vst1.32         {d5[0]},  [r0,:32], r1
        vst1.32         {d5[1]},  [r3,:32], r1
        vqmovun.s16     d7,  \coef3

        vst1.32         {d6[0]},  [r0,:32], r1
        vst1.32         {d6[1]},  [r3,:32], r1
        vst1.32         {d7[0]},  [r0,:32], r1
        vst1.32         {d7[1]},  [r3,:32], r1
.endm

.macro itxfm16_1d_funcs txfm
@ Read a vertical 4x16 slice out of a 16x16 matrix, do a transform on it,
@ transpose into a horizontal 16x4 slice and store.
@ r0 = dst (temp buffer)
@ r1 = slice offset
@ r2 = src
function \txfm\()16_1d_4x16_pass1_neon
        push            {lr}

        mov             r12, #32
        vmov.s16        q2,  #0
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d4},  [r2,:64], r12
.endr

        bl              \txfm\()16

        @ Do four 4x4 transposes. Originally, d16-d31 contain the
        @ 16 rows. Afterwards, d16-d19, d20-d23, d24-d27, d28-d31
        @ contain the transposed 4x4 blocks.
        transpose16_q_4x_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        @ Store the transposed 4x4 blocks horizontally.
        cmp             r1,  #12
        beq             1f
.irp i, 16, 20, 24, 28, 17, 21, 25, 29, 18, 22, 26, 30, 19, 23, 27, 31
        vst1.16         {d\i}, [r0,:64]!
.endr
        pop             {pc}
1:
        @ Special case: For the last input column (r1 == 12),
        @ which would be stored as the last row in the temp buffer,
        @ don't store the first 4x4 block, but keep it in registers
        @ for the first slice of the second pass (where it is the
        @ last 4x4 block).
        add             r0,  r0,  #8
        vst1.16         {d20}, [r0,:64]!
        vst1.16         {d24}, [r0,:64]!
        vst1.16         {d28}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d21}, [r0,:64]!
        vst1.16         {d25}, [r0,:64]!
        vst1.16         {d29}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d22}, [r0,:64]!
        vst1.16         {d26}, [r0,:64]!
        vst1.16         {d30}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d23}, [r0,:64]!
        vst1.16         {d27}, [r0,:64]!
        vst1.16         {d31}, [r0,:64]!
        vmov            d28, d16
        vmov            d29, d17
        vmov            d30, d18
        vmov            d31, d19
        pop             {pc}
endfunc

@ Read a vertical 4x16 slice out of a 16x16 matrix, do a transform on it,
@ load the destination pixels (from a similar 4x16 slice), add and store back.
@ r0 = dst
@ r1 = dst stride
@ r2 = src (temp buffer)
@ r3 = slice offset
function \txfm\()16_1d_4x16_pass2_neon
        push            {lr}
        mov             r12, #32
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
        vld1.16         {d\i}, [r2,:64], r12
.endr
        cmp             r3,  #0
        beq             1f
.irp i, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64], r12
.endr
1:

        add             r3,  r0,  r1
        lsl             r1,  r1,  #1
        bl              \txfm\()16

        load_add_store  q8,  q9,  q10, q11
        load_add_store  q12, q13, q14, q15

        pop             {pc}
endfunc
.endm

itxfm16_1d_funcs idct
itxfm16_1d_funcs iadst

@ This is the minimum eob value for each subpartition, in increments of 4
const min_eob_idct_idct_16, align=4
        .short  0, 10, 38, 89
endconst

.macro itxfm_func16x16 txfm1, txfm2
function ff_vp9_\txfm1\()_\txfm2\()_16x16_add_neon, export=1
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #1
        beq             idct16x16_dc_add_neon
.endif
        push            {r4-r8,lr}
.ifnc \txfm1\()_\txfm2,idct_idct
        vpush           {q4-q7}
.endif

        @ Align the stack, allocate a temp buffer
T       mov             r7,  sp
T       and             r7,  r7,  #15
A       and             r7,  sp,  #15
        add             r7,  r7,  #512
        sub             sp,  sp,  r7

        mov             r4,  r0
        mov             r5,  r1
        mov             r6,  r2

.ifc \txfm1,idct
        movrel          r12, idct_coeffs
        vld1.16         {q0-q1}, [r12,:128]
.endif

.ifc \txfm1\()_\txfm2,idct_idct
        cmp             r3,  #10
        ble             idct16x16_quarter_add_neon
        cmp             r3,  #38
        ble             idct16x16_half_add_neon

        movrel          r8,  min_eob_idct_idct_16 + 2
.endif

.irp i, 0, 4, 8, 12
        add             r0,  sp,  #(\i*32)
.ifc \txfm1\()_\txfm2,idct_idct
.if \i > 0
        ldrh_post       r1,  r8,  #2
        cmp             r3,  r1
        it              le
        movle           r1,  #(16 - \i)/4
        ble             1f
.endif
.endif
        mov             r1,  #\i
        add             r2,  r6,  #(\i*2)
        bl              \txfm1\()16_1d_4x16_pass1_neon
.endr

.ifc \txfm1\()_\txfm2,idct_idct
        b               3f
1:
        @ For all-zero slices in pass 1, set d28-d31 to zero, for the in-register
        @ passthrough of coefficients to pass 2 and clear the end of the temp buffer
        vmov.i16        q14, #0
        vmov.i16        q15, #0
2:
        subs            r1,  r1,  #1
.rept 4
        vst1.16         {q14-q15}, [r0,:128]!
.endr
        bne             2b
3:
.endif

.ifc \txfm1\()_\txfm2,iadst_idct
        movrel          r12, idct_coeffs
        vld1.16         {q0-q1}, [r12,:128]
.endif
.irp i, 0, 4, 8, 12
        add             r0,  r4,  #(\i)
        mov             r1,  r5
        add             r2,  sp,  #(\i*2)
        mov             r3,  #\i
        bl              \txfm2\()16_1d_4x16_pass2_neon
.endr

        add             sp,  sp,  r7
.ifnc \txfm1\()_\txfm2,idct_idct
        vpop            {q4-q7}
.endif
        pop             {r4-r8,pc}
endfunc
.endm

itxfm_func16x16 idct,  idct
itxfm_func16x16 iadst, idct
itxfm_func16x16 idct,  iadst
itxfm_func16x16 iadst, iadst
.ltorg

function idct16_1d_4x16_pass1_quarter_neon
        push            {lr}
        mov             r12, #32
        vmov.s16        q2, #0
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d4},  [r2,:64], r12
.endr

        bl              idct16_quarter

        @ Do four 4x4 transposes. Originally, d16-d31 contain the
        @ 16 rows. Afterwards, d16-d19, d20-d23, d24-d27, d28-d31
        @ contain the transposed 4x4 blocks.
        transpose16_q_4x_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        @ Store the transposed 4x4 blocks horizontally.
        @ The first 4x4 block is kept in registers for the second pass,
        @ store the rest in the temp buffer.
        add             r0,  r0,  #8
        vst1.16         {d20}, [r0,:64]!
        vst1.16         {d24}, [r0,:64]!
        vst1.16         {d28}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d21}, [r0,:64]!
        vst1.16         {d25}, [r0,:64]!
        vst1.16         {d29}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d22}, [r0,:64]!
        vst1.16         {d26}, [r0,:64]!
        vst1.16         {d30}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d23}, [r0,:64]!
        vst1.16         {d27}, [r0,:64]!
        vst1.16         {d31}, [r0,:64]!
        pop             {pc}
endfunc

function idct16_1d_4x16_pass2_quarter_neon
        push            {lr}
        @ Only load the top 4 lines, and only do it for the later slices.
        @ For the first slice, d16-d19 is kept in registers from the first pass.
        cmp             r3,  #0
        beq             1f
        mov             r12, #32
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64], r12
.endr
1:

        add             r3,  r0,  r1
        lsl             r1,  r1,  #1
        bl              idct16_quarter

        load_add_store  q8,  q9,  q10, q11
        load_add_store  q12, q13, q14, q15

        pop             {pc}
endfunc

function idct16_1d_4x16_pass1_half_neon
        push            {lr}
        mov             r12, #32
        vmov.s16        q2, #0
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d4},  [r2,:64], r12
.endr

        bl              idct16_half

        @ Do four 4x4 transposes. Originally, d16-d31 contain the
        @ 16 rows. Afterwards, d16-d19, d20-d23, d24-d27, d28-d31
        @ contain the transposed 4x4 blocks.
        transpose16_q_4x_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

        @ Store the transposed 4x4 blocks horizontally.
        cmp             r1,  #4
        beq             1f
.irp i, 16, 20, 24, 28, 17, 21, 25, 29, 18, 22, 26, 30, 19, 23, 27, 31
        vst1.16         {d\i}, [r0,:64]!
.endr
        pop             {pc}
1:
        @ Special case: For the second input column (r1 == 4),
        @ which would be stored as the second row in the temp buffer,
        @ don't store the first 4x4 block, but keep it in registers
        @ for the first slice of the second pass (where it is the
        @ second 4x4 block).
        add             r0,  r0,  #8
        vst1.16         {d20}, [r0,:64]!
        vst1.16         {d24}, [r0,:64]!
        vst1.16         {d28}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d21}, [r0,:64]!
        vst1.16         {d25}, [r0,:64]!
        vst1.16         {d29}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d22}, [r0,:64]!
        vst1.16         {d26}, [r0,:64]!
        vst1.16         {d30}, [r0,:64]!
        add             r0,  r0,  #8
        vst1.16         {d23}, [r0,:64]!
        vst1.16         {d27}, [r0,:64]!
        vst1.16         {d31}, [r0,:64]!
        vmov            d20, d16
        vmov            d21, d17
        vmov            d22, d18
        vmov            d23, d19
        pop             {pc}
endfunc

function idct16_1d_4x16_pass2_half_neon
        push            {lr}
        mov             r12, #32
        cmp             r3,  #0
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64], r12
.endr
        beq             1f
.irp i, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64], r12
.endr
1:

        add             r3,  r0,  r1
        lsl             r1,  r1,  #1
        bl              idct16_half

        load_add_store  q8,  q9,  q10, q11
        load_add_store  q12, q13, q14, q15

        pop             {pc}
endfunc
.purgem load_add_store

.macro idct16_partial size
function idct16x16_\size\()_add_neon
        add             r0,  sp,  #(0*32)
        mov             r1,  #0
        add             r2,  r6,  #(0*2)
        bl              idct16_1d_4x16_pass1_\size\()_neon
.ifc \size,half
        add             r0,  sp,  #(4*32)
        mov             r1,  #4
        add             r2,  r6,  #(4*2)
        bl              idct16_1d_4x16_pass1_\size\()_neon
.endif
.irp i, 0, 4, 8, 12
        add             r0,  r4,  #(\i)
        mov             r1,  r5
        add             r2,  sp,  #(\i*2)
        mov             r3,  #\i
        bl              idct16_1d_4x16_pass2_\size\()_neon
.endr

        add             sp,  sp,  r7
        pop             {r4-r8,pc}
endfunc
.endm

idct16_partial quarter
idct16_partial half

function idct32x32_dc_add_neon
        movrel          r12, idct_coeffs
        vld1.16         {d0}, [r12,:64]

        vmov.i16        q2,  #0

        vld1.16         {d16[]}, [r2,:16]
        vmull.s16       q8,  d16, d0[0]
        vrshrn.s32      d16, q8,  #14
        vmull.s16       q8,  d16, d0[0]
        vrshrn.s32      d16, q8,  #14
        vdup.16         q8,  d16[0]
        vst1.16         {d4[0]}, [r2,:16]

        vrshr.s16       q8,  q8,  #6

        mov             r3,  r0
        mov             r12, #32
1:
        @ Loop to add the constant from q8 into all 32x32 outputs
        subs            r12, r12, #2
        vld1.8          {q0-q1},  [r0,:128], r1
        vaddw.u8        q9,  q8,  d0
        vaddw.u8        q10, q8,  d1
        vld1.8          {q2-q3},  [r0,:128], r1
        vaddw.u8        q11, q8,  d2
        vaddw.u8        q12, q8,  d3
        vaddw.u8        q13, q8,  d4
        vaddw.u8        q14, q8,  d5
        vaddw.u8        q15, q8,  d6
        vqmovun.s16     d0,  q9
        vaddw.u8        q9,  q8,  d7
        vqmovun.s16     d1,  q10
        vqmovun.s16     d2,  q11
        vqmovun.s16     d3,  q12
        vqmovun.s16     d4,  q13
        vqmovun.s16     d5,  q14
        vst1.8          {q0-q1},  [r3,:128], r1
        vqmovun.s16     d6,  q15
        vqmovun.s16     d7,  q9
        vst1.8          {q2-q3},  [r3,:128], r1
        bne             1b

        bx              lr
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

        mbutterfly      d27, d20, d0[2], d0[3], q12, q15        @ d27 = t18a, d20 = t29a
        mbutterfly      d29, d9,  d0[2], d0[3], q12, q15        @ d29 = t19,  d5  = t28
        mbutterfly      d28, d10, d0[2], d0[3], q12, q15, neg=1 @ d28 = t27,  d6  = t20
        mbutterfly      d26, d21, d0[2], d0[3], q12, q15, neg=1 @ d26 = t26a, d21 = t21a

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
        mbutterfly      d16, d31, d4[0], d4[1], q4, q5 @ d16 = t16a, d31 = t31a
        mbutterfly      d24, d23, d4[2], d4[3], q4, q5 @ d24 = t17a, d23 = t30a
        mbutterfly      d20, d27, d5[0], d5[1], q4, q5 @ d20 = t18a, d27 = t29a
        mbutterfly      d28, d19, d5[2], d5[3], q4, q5 @ d28 = t19a, d19 = t28a
        mbutterfly      d18, d29, d6[0], d6[1], q4, q5 @ d18 = t20a, d29 = t27a
        mbutterfly      d26, d21, d6[2], d6[3], q4, q5 @ d26 = t21a, d21 = t26a
        mbutterfly      d22, d25, d7[0], d7[1], q4, q5 @ d22 = t22a, d25 = t25a
        mbutterfly      d30, d17, d7[2], d7[3], q4, q5 @ d30 = t23a, d17 = t24a

        butterfly       d8,  d24, d16, d24 @ d8  = t16, d24 = t17
        butterfly       d9,  d20, d28, d20 @ d9  = t19, d20 = t18
        butterfly       d10, d26, d18, d26 @ d10 = t20, d26 = t21
        butterfly       d11, d22, d30, d22 @ d11 = t23, d22 = t22
        butterfly       d28, d25, d17, d25 @ d28 = t24, d25 = t25
        butterfly       d30, d21, d29, d21 @ d30 = t27, d21 = t26
        butterfly       d29, d23, d31, d23 @ d29 = t31, d23 = t30
        butterfly       d31, d27, d19, d27 @ d31 = t28, d27 = t29

        mbutterfly      d23, d24, d1[0], d1[1], q8, q9        @ d23 = t17a, d24 = t30a
        mbutterfly      d27, d20, d1[0], d1[1], q8, q9, neg=1 @ d27 = t29a, d20 = t18a
        mbutterfly      d21, d26, d1[2], d1[3], q8, q9        @ d21 = t21a, d26 = t26a
        mbutterfly      d25, d22, d1[2], d1[3], q8, q9, neg=1 @ d25 = t25a, d22 = t22a
        idct32_end
endfunc

function idct32_odd_half
        mbutterfly_h1   d16, d31, d4[0], d4[1], q4, q5 @ d16 = t16a, d31 = t31a
        mbutterfly_h2   d24, d23, d4[2], d4[3], q4, q5 @ d24 = t17a, d23 = t30a
        mbutterfly_h1   d20, d27, d5[0], d5[1], q4, q5 @ d20 = t18a, d27 = t29a
        mbutterfly_h2   d28, d19, d5[2], d5[3], q4, q5 @ d28 = t19a, d19 = t28a
        mbutterfly_h1   d18, d29, d6[0], d6[1], q4, q5 @ d18 = t20a, d29 = t27a
        mbutterfly_h2   d26, d21, d6[2], d6[3], q4, q5 @ d26 = t21a, d21 = t26a
        mbutterfly_h1   d22, d25, d7[0], d7[1], q4, q5 @ d22 = t22a, d25 = t25a
        mbutterfly_h2   d30, d17, d7[2], d7[3], q4, q5 @ d30 = t23a, d17 = t24a

        butterfly       d8,  d24, d16, d24 @ d8  = t16, d24 = t17
        butterfly       d9,  d20, d28, d20 @ d9  = t19, d20 = t18
        butterfly       d10, d26, d18, d26 @ d10 = t20, d26 = t21
        butterfly       d11, d22, d30, d22 @ d11 = t23, d22 = t22
        butterfly       d28, d25, d17, d25 @ d28 = t24, d25 = t25
        butterfly       d30, d21, d29, d21 @ d30 = t27, d21 = t26
        butterfly       d29, d23, d31, d23 @ d29 = t31, d23 = t30
        butterfly       d31, d27, d19, d27 @ d31 = t28, d27 = t29

        mbutterfly      d23, d24, d1[0], d1[1], q8, q9        @ d23 = t17a, d24 = t30a
        mbutterfly      d27, d20, d1[0], d1[1], q8, q9, neg=1 @ d27 = t29a, d20 = t18a
        mbutterfly      d21, d26, d1[2], d1[3], q8, q9        @ d21 = t21a, d26 = t26a
        mbutterfly      d25, d22, d1[2], d1[3], q8, q9, neg=1 @ d25 = t25a, d22 = t22a

        idct32_end
endfunc

function idct32_odd_quarter
        vmull.s16       q4,  d16, d4[0]
        vmull.s16       q14, d19, d5[3]
        vmull.s16       q15, d16, d4[1]
        vmull.s16       q11, d17, d7[2]
        vmull.s16       q5,  d17, d7[3]
        vmull.s16       q13, d19, d5[2]
        vmull.s16       q10, d18, d6[0]
        vmull.s16       q12, d18, d6[1]

        vneg.s32        q14, q14
        vneg.s32        q5,  q5

        vrshrn.s32      d8,  q4,  #14
        vrshrn.s32      d9,  q14, #14
        vrshrn.s32      d29, q15, #14
        vrshrn.s32      d28, q11, #14
        vrshrn.s32      d11, q5,  #14
        vrshrn.s32      d31, q13, #14
        vrshrn.s32      d10, q10, #14
        vrshrn.s32      d30, q12, #14

        mbutterfly_l    q8,  q9,  d29, d8,  d1[0], d1[1]
        mbutterfly_l    q13, q10, d31, d9,  d1[0], d1[1]
        vrshrn.s32      d23, q8,  #14
        vrshrn.s32      d24, q9,  #14
        vneg.s32        q10, q10
        vrshrn.s32      d27, q13, #14
        vrshrn.s32      d20, q10, #14
        mbutterfly_l    q8,  q9,  d30, d10, d1[2], d1[3]
        vrshrn.s32      d21, q8,  #14
        vrshrn.s32      d26, q9,  #14
        mbutterfly_l    q8,  q9,  d28, d11, d1[2], d1[3]
        vrshrn.s32      d25, q8,  #14
        vneg.s32        q9,  q9
        vrshrn.s32      d22, q9,  #14

        idct32_end
endfunc

.macro idct32_funcs suffix
@ Do an 32-point IDCT of a 4x32 slice out of a 32x32 matrix.
@ We don't have register space to do a single pass IDCT of 4x32 though,
@ but the 32-point IDCT can be decomposed into two 16-point IDCTs;
@ a normal IDCT16 with every other input component (the even ones, with
@ each output written twice), followed by a separate 16-point IDCT
@ of the odd inputs, added/subtracted onto the outputs of the first idct16.
@ r0 = dst (temp buffer)
@ r1 = unused
@ r2 = src
function idct32_1d_4x32_pass1\suffix\()_neon
        push            {lr}

        @ idct16 clobbers q2-q3 (since it doesn't clobber q4-q7 at all
        @ when doing the normal 16x16 idct), so move the idct32_odd coeffs
        @ to q4-q5
        vmov            q4,  q2
        vmov            q5,  q3

        @ Double stride of the input, since we only read every other line
        mov             r12, #128
        vmov.s16        d4,  #0

        @ d16 = IN(0), d17 = IN(2) ... d31 = IN(30)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d4},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d4},  [r2,:64], r12
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64]
        vst1.16         {d4},  [r2,:64], r12
.endr
.endif

        bl              idct16\suffix

        @ Move the idct32_odd coeffs back into q2-q3 for idct32_odd;
        @ the constants for a vmul with a lane must be in q0-q3.
        vmov            q2,  q4
        vmov            q3,  q5

        @ Do four 4x4 transposes. Originally, d16-d31 contain the
        @ 16 rows. Afterwards, d16-d19, d20-d23, d24-d27, d28-d31
        @ contain the transposed 4x4 blocks.
        transpose16_q_4x_4x4 q8,  q9,  q10, q11, q12, q13, q14, q15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31
        @ Store the registers a, b, c, d horizontally, followed
        @ by the same registers d, c, b, a mirrored.
.macro store_rev a, b, c, d
.irp i, \a, \b, \c, \d
        vst1.16         {d\i}, [r0,:64]!
        vrev64.16       d\i, d\i
.endr
.irp i, \d, \c, \b, \a
        vst1.16         {d\i}, [r0,:64]!
.endr
.endm
        store_rev       16, 20, 24, 28
        store_rev       17, 21, 25, 29
        store_rev       18, 22, 26, 30
        store_rev       19, 23, 27, 31
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
        add             r2,  r2,  #64

        vmov.s16        d8,  #0
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

        transpose16_q_4x_4x4 q15, q14, q13, q12, q11, q10, q9,  q8,  d31, d30, d29, d28, d27, d26, d25, d24, d23, d22, d21, d20, d19, d18, d17, d16

        @ Store the registers a, b, c, d horizontally,
        @ adding into the output first, and then mirrored, subtracted
        @ from the output.
.macro store_rev a, b, c, d
.irp i, \a, \b, \c, \d
        vld1.16         {d8},  [r0,:64]
        vadd.s16        d8,  d8,  d\i
        vst1.16         {d8},  [r0,:64]!
        vrev64.16       d\i, d\i
.endr
.irp i, \d, \c, \b, \a
        vld1.16         {d8},  [r0,:64]
        vsub.s16        d8,  d8,  d\i
        vst1.16         {d8},  [r0,:64]!
.endr
.endm

        store_rev       31, 27, 23, 19
        store_rev       30, 26, 22, 18
        store_rev       29, 25, 21, 17
        store_rev       28, 24, 20, 16
.purgem store_rev
        pop             {pc}
endfunc
.ltorg

@ This is mostly the same as 4x32_pass1, but without the transpose,
@ and use the source as temp buffer between the two idct passes, and
@ add into the destination.
@ r0 = dst
@ r1 = dst stride
@ r2 = src (temp buffer)
function idct32_1d_4x32_pass2\suffix\()_neon
        push            {lr}
        vmov            q4,  q2
        vmov            q5,  q3

        mov             r12, #128
        @ d16 = IN(0), d17 = IN(2) ... d31 = IN(30)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #4
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #2
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #3
.endif

        bl              idct16\suffix

        vmov            q2,  q4
        vmov            q3,  q5

.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vst1.16         {d\i}, [r2,:64], r12
.endr

        sub             r2,  r2,  r12, lsl #4
        add             r2,  r2,  #64

        @ d16 = IN(1), d17 = IN(3) ... d31 = IN(31)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        vld1.16         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #4
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        vld1.16         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #2
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        vld1.16         {d\i}, [r2,:64], r12
.endr
        sub             r2,  r2,  r12, lsl #3
.endif
        sub             r2,  r2,  #64

        bl              idct32_odd\suffix

        mov             r12, #128
.macro load_acc_store a, b, c, d, neg=0
        vld1.16         {d8},  [r2,:64], r12
        vld1.16         {d9},  [r2,:64], r12
.if \neg == 0
        vadd.s16        d8,  d8,  d\a
        vld1.16         {d10}, [r2,:64], r12
        vadd.s16        d9,  d9,  d\b
        vld1.16         {d11}, [r2,:64], r12
        vadd.s16        d10, d10, d\c
        vadd.s16        d11, d11, d\d
.else
        vsub.s16        d8,  d8,  d\a
        vld1.16         {d10}, [r2,:64], r12
        vsub.s16        d9,  d9,  d\b
        vld1.16         {d11}, [r2,:64], r12
        vsub.s16        d10, d10, d\c
        vsub.s16        d11, d11, d\d
.endif
        vld1.32         {d12[]},  [r0,:32], r1
        vld1.32         {d12[1]}, [r0,:32], r1
        vrshr.s16       q4,  q4,  #6
        vld1.32         {d13[]},  [r0,:32], r1
        vrshr.s16       q5,  q5,  #6
        vld1.32         {d13[1]}, [r0,:32], r1
        sub             r0,  r0,  r1, lsl #2
        vaddw.u8        q4,  q4,  d12
        vaddw.u8        q5,  q5,  d13
        vqmovun.s16     d8,  q4
        vqmovun.s16     d9,  q5
        vst1.32         {d8[0]},  [r0,:32], r1
        vst1.32         {d8[1]},  [r0,:32], r1
        vst1.32         {d9[0]},  [r0,:32], r1
        vst1.32         {d9[1]},  [r0,:32], r1
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
        pop             {pc}
endfunc
.endm

idct32_funcs
idct32_funcs _quarter
idct32_funcs _half

const min_eob_idct_idct_32, align=4
        .short  0, 9, 34, 70, 135, 240, 336, 448
endconst

function ff_vp9_idct_idct_32x32_add_neon, export=1
        cmp             r3,  #1
        beq             idct32x32_dc_add_neon
        push            {r4-r8,lr}
        vpush           {q4-q6}

        @ Align the stack, allocate a temp buffer
T       mov             r7,  sp
T       and             r7,  r7,  #15
A       and             r7,  sp,  #15
        add             r7,  r7,  #2048
        sub             sp,  sp,  r7

        mov             r4,  r0
        mov             r5,  r1
        mov             r6,  r2

        movrel          r12, idct_coeffs
        vld1.16         {q0-q1}, [r12,:128]!
        vld1.16         {q2-q3}, [r12,:128]

        cmp             r3,  #34
        ble             idct32x32_quarter_add_neon
        cmp             r3,  #135
        ble             idct32x32_half_add_neon

        movrel          r8,  min_eob_idct_idct_32 + 2

.irp i, 0, 4, 8, 12, 16, 20, 24, 28
        add             r0,  sp,  #(\i*64)
.if \i > 0
        ldrh_post       r1,  r8,  #2
        cmp             r3,  r1
        it              le
        movle           r1,  #(32 - \i)/2
        ble             1f
.endif
        add             r2,  r6,  #(\i*2)
        bl              idct32_1d_4x32_pass1_neon
.endr
        b               3f

1:
        @ Write zeros to the temp buffer for pass 2
        vmov.i16        q14, #0
        vmov.i16        q15, #0
2:
        subs            r1,  r1,  #1
.rept 4
        vst1.16         {q14-q15}, [r0,:128]!
.endr
        bne             2b
3:
.irp i, 0, 4, 8, 12, 16, 20, 24, 28
        add             r0,  r4,  #(\i)
        mov             r1,  r5
        add             r2,  sp,  #(\i*2)
        bl              idct32_1d_4x32_pass2_neon
.endr

        add             sp,  sp,  r7
        vpop            {q4-q6}
        pop             {r4-r8,pc}
endfunc

.macro idct32_partial size
function idct32x32_\size\()_add_neon
.irp i, 0, 4
        add             r0,  sp,  #(\i*64)
.ifc \size,quarter
.if \i == 4
        cmp             r3,  #9
        ble             1f
.endif
.endif
        add             r2,  r6,  #(\i*2)
        bl              idct32_1d_4x32_pass1_\size\()_neon
.endr

.ifc \size,half
.irp i, 8, 12
        add             r0,  sp,  #(\i*64)
.if \i == 12
        cmp             r3,  #70
        ble             1f
.endif
        add             r2,  r6,  #(\i*2)
        bl              idct32_1d_4x32_pass1_\size\()_neon
.endr
.endif
        b               3f

1:
        @ Write zeros to the temp buffer for pass 2
        vmov.i16        q14, #0
        vmov.i16        q15, #0
.rept 8
        vst1.16         {q14-q15}, [r0,:128]!
.endr

3:
.irp i, 0, 4, 8, 12, 16, 20, 24, 28
        add             r0,  r4,  #(\i)
        mov             r1,  r5
        add             r2,  sp,  #(\i*2)
        bl              idct32_1d_4x32_pass2_\size\()_neon
.endr

        add             sp,  sp,  r7
        vpop            {q4-q6}
        pop             {r4-r8,pc}
endfunc
.endm

idct32_partial quarter
idct32_partial half
