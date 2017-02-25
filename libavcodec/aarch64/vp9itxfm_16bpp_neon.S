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

#include "libavutil/aarch64/asm.S"
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

.macro transpose_4x4s r0, r1, r2, r3, r4, r5, r6, r7
        trn1            \r4\().4s,  \r0\().4s,  \r1\().4s
        trn2            \r5\().4s,  \r0\().4s,  \r1\().4s
        trn1            \r6\().4s,  \r2\().4s,  \r3\().4s
        trn2            \r7\().4s,  \r2\().4s,  \r3\().4s
        trn1            \r0\().2d,  \r4\().2d,  \r6\().2d
        trn2            \r2\().2d,  \r4\().2d,  \r6\().2d
        trn1            \r1\().2d,  \r5\().2d,  \r7\().2d
        trn2            \r3\().2d,  \r5\().2d,  \r7\().2d
.endm

// Transpose a 8x8 matrix of 32 bit elements, where each row is spread out
// over two registers.
.macro transpose_8x8s r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, t0, t1, t2, t3
        transpose_4x4s  \r0,  \r2,  \r4,  \r6,  \t0, \t1, \t2, \t3
        transpose_4x4s  \r9,  \r11, \r13, \r15, \t0, \t1, \t2, \t3

        // Do 4x4 transposes of r1,r3,r5,r7 and r8,r10,r12,r14
        // while swapping the two 4x4 matrices between each other

        // First step of the 4x4 transpose of r1-r7, into t0-t3
        trn1            \t0\().4s,  \r1\().4s,  \r3\().4s
        trn2            \t1\().4s,  \r1\().4s,  \r3\().4s
        trn1            \t2\().4s,  \r5\().4s,  \r7\().4s
        trn2            \t3\().4s,  \r5\().4s,  \r7\().4s

        // First step of the 4x4 transpose of r8-r12, into r1-r7
        trn1            \r1\().4s,  \r8\().4s,  \r10\().4s
        trn2            \r3\().4s,  \r8\().4s,  \r10\().4s
        trn1            \r5\().4s,  \r12\().4s, \r14\().4s
        trn2            \r7\().4s,  \r12\().4s, \r14\().4s

        // Second step of the 4x4 transpose of r1-r7 (now in t0-r3), into r8-r12
        trn1            \r8\().2d,  \t0\().2d,  \t2\().2d
        trn2            \r12\().2d, \t0\().2d,  \t2\().2d
        trn1            \r10\().2d, \t1\().2d,  \t3\().2d
        trn2            \r14\().2d, \t1\().2d,  \t3\().2d

        // Second step of the 4x4 transpose of r8-r12 (now in r1-r7), in place as far as possible
        trn1            \t0\().2d,  \r1\().2d,  \r5\().2d
        trn2            \r5\().2d,  \r1\().2d,  \r5\().2d
        trn1            \t1\().2d,  \r3\().2d,  \r7\().2d
        trn2            \r7\().2d,  \r3\().2d,  \r7\().2d

        // Move the outputs of trn1 back in place
        mov             \r1\().16b,  \t0\().16b
        mov             \r3\().16b,  \t1\().16b
.endm

// out1 = ((in1 + in2) * d0[0] + (1 << 13)) >> 14
// out2 = ((in1 - in2) * d0[0] + (1 << 13)) >> 14
// in/out are .4s registers; this can do with 4 temp registers, but is
// more efficient if 6 temp registers are available.
.macro dmbutterfly0 out1, out2, in1, in2, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, neg=0
.if \neg > 0
        neg             \tmp4\().4s, v0.4s
.endif
        add             \tmp1\().4s, \in1\().4s,  \in2\().4s
        sub             \tmp2\().4s, \in1\().4s,  \in2\().4s
.if \neg > 0
        smull           \tmp3\().2d, \tmp1\().2s, \tmp4\().s[0]
        smull2          \tmp4\().2d, \tmp1\().4s, \tmp4\().s[0]
.else
        smull           \tmp3\().2d, \tmp1\().2s, v0.s[0]
        smull2          \tmp4\().2d, \tmp1\().4s, v0.s[0]
.endif
.ifb \tmp5
        rshrn           \out1\().2s, \tmp3\().2d, #14
        rshrn2          \out1\().4s, \tmp4\().2d, #14
        smull           \tmp3\().2d, \tmp2\().2s, v0.s[0]
        smull2          \tmp4\().2d, \tmp2\().4s, v0.s[0]
        rshrn           \out2\().2s, \tmp3\().2d, #14
        rshrn2          \out2\().4s, \tmp4\().2d, #14
.else
        smull           \tmp5\().2d, \tmp2\().2s, v0.s[0]
        smull2          \tmp6\().2d, \tmp2\().4s, v0.s[0]
        rshrn           \out1\().2s, \tmp3\().2d, #14
        rshrn2          \out1\().4s, \tmp4\().2d, #14
        rshrn           \out2\().2s, \tmp5\().2d, #14
        rshrn2          \out2\().4s, \tmp6\().2d, #14
.endif
.endm

// Same as dmbutterfly0 above, but treating the input in in2 as zero,
// writing the same output into both out1 and out2.
.macro dmbutterfly0_h out1, out2, in1, in2, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6
        smull           \tmp1\().2d, \in1\().2s,  v0.s[0]
        smull2          \tmp2\().2d, \in1\().4s,  v0.s[0]
        rshrn           \out1\().2s, \tmp1\().2d, #14
        rshrn2          \out1\().4s, \tmp2\().2d, #14
        rshrn           \out2\().2s, \tmp1\().2d, #14
        rshrn2          \out2\().4s, \tmp2\().2d, #14
.endm

// out1,out2 = in1 * coef1 - in2 * coef2
// out3,out4 = in1 * coef2 + in2 * coef1
// out are 4 x .2d registers, in are 2 x .4s registers
.macro dmbutterfly_l out1, out2, out3, out4, in1, in2, coef1, coef2
        smull           \out1\().2d, \in1\().2s, \coef1
        smull2          \out2\().2d, \in1\().4s, \coef1
        smull           \out3\().2d, \in1\().2s, \coef2
        smull2          \out4\().2d, \in1\().4s, \coef2
        smlsl           \out1\().2d, \in2\().2s, \coef2
        smlsl2          \out2\().2d, \in2\().4s, \coef2
        smlal           \out3\().2d, \in2\().2s, \coef1
        smlal2          \out4\().2d, \in2\().4s, \coef1
.endm

// inout1 = (inout1 * coef1 - inout2 * coef2 + (1 << 13)) >> 14
// inout2 = (inout1 * coef2 + inout2 * coef1 + (1 << 13)) >> 14
// inout are 2 x .4s registers
.macro dmbutterfly inout1, inout2, coef1, coef2, tmp1, tmp2, tmp3, tmp4, neg=0
        dmbutterfly_l   \tmp1, \tmp2, \tmp3, \tmp4, \inout1, \inout2, \coef1, \coef2
.if \neg > 0
        neg             \tmp3\().2d, \tmp3\().2d
        neg             \tmp4\().2d, \tmp4\().2d
.endif
        rshrn           \inout1\().2s, \tmp1\().2d,  #14
        rshrn2          \inout1\().4s, \tmp2\().2d,  #14
        rshrn           \inout2\().2s, \tmp3\().2d,  #14
        rshrn2          \inout2\().4s, \tmp4\().2d,  #14
.endm

// Same as dmbutterfly above, but treating the input in inout2 as zero
.macro dmbutterfly_h1 inout1, inout2, coef1, coef2, tmp1, tmp2, tmp3, tmp4
        smull           \tmp1\().2d, \inout1\().2s, \coef1
        smull2          \tmp2\().2d, \inout1\().4s, \coef1
        smull           \tmp3\().2d, \inout1\().2s, \coef2
        smull2          \tmp4\().2d, \inout1\().4s, \coef2
        rshrn           \inout1\().2s, \tmp1\().2d, #14
        rshrn2          \inout1\().4s, \tmp2\().2d, #14
        rshrn           \inout2\().2s, \tmp3\().2d, #14
        rshrn2          \inout2\().4s, \tmp4\().2d, #14
.endm

// Same as dmbutterfly above, but treating the input in inout1 as zero
.macro dmbutterfly_h2 inout1, inout2, coef1, coef2, tmp1, tmp2, tmp3, tmp4
        smull           \tmp1\().2d, \inout2\().2s, \coef2
        smull2          \tmp2\().2d, \inout2\().4s, \coef2
        smull           \tmp3\().2d, \inout2\().2s, \coef1
        smull2          \tmp4\().2d, \inout2\().4s, \coef1
        neg             \tmp1\().2d, \tmp1\().2d
        neg             \tmp2\().2d, \tmp2\().2d
        rshrn           \inout2\().2s, \tmp3\().2d, #14
        rshrn2          \inout2\().4s, \tmp4\().2d, #14
        rshrn           \inout1\().2s, \tmp1\().2d, #14
        rshrn2          \inout1\().4s, \tmp2\().2d, #14
.endm

.macro dsmull_h out1, out2, in, coef
        smull           \out1\().2d, \in\().2s, \coef
        smull2          \out2\().2d, \in\().4s, \coef
.endm

.macro drshrn_h out, in1, in2, shift
        rshrn           \out\().2s, \in1\().2d, \shift
        rshrn2          \out\().4s, \in2\().2d, \shift
.endm


// out1 = in1 + in2
// out2 = in1 - in2
.macro butterfly_4s out1, out2, in1, in2
        add             \out1\().4s, \in1\().4s, \in2\().4s
        sub             \out2\().4s, \in1\().4s, \in2\().4s
.endm

// out1 = in1 - in2
// out2 = in1 + in2
.macro butterfly_4s_r out1, out2, in1, in2
        sub             \out1\().4s, \in1\().4s, \in2\().4s
        add             \out2\().4s, \in1\().4s, \in2\().4s
.endm

// out1 = (in1,in2 + in3,in4 + (1 << 13)) >> 14
// out2 = (in1,in2 - in3,in4 + (1 << 13)) >> 14
// out are 2 x .4s registers, in are 4 x .2d registers
.macro dbutterfly_n out1, out2, in1, in2, in3, in4, tmp1, tmp2, tmp3, tmp4
        add             \tmp1\().2d, \in1\().2d, \in3\().2d
        add             \tmp2\().2d, \in2\().2d, \in4\().2d
        sub             \tmp3\().2d, \in1\().2d, \in3\().2d
        sub             \tmp4\().2d, \in2\().2d, \in4\().2d
        rshrn           \out1\().2s, \tmp1\().2d,  #14
        rshrn2          \out1\().4s, \tmp2\().2d,  #14
        rshrn           \out2\().2s, \tmp3\().2d,  #14
        rshrn2          \out2\().4s, \tmp4\().2d,  #14
.endm

.macro iwht4_10 c0, c1, c2, c3
        add             \c0\().4s, \c0\().4s, \c1\().4s
        sub             v17.4s,    \c2\().4s, \c3\().4s
        sub             v16.4s,    \c0\().4s, v17.4s
        sshr            v16.4s,    v16.4s,    #1
        sub             \c2\().4s, v16.4s,    \c1\().4s
        sub             \c1\().4s, v16.4s,    \c3\().4s
        add             \c3\().4s, v17.4s,    \c2\().4s
        sub             \c0\().4s, \c0\().4s, \c1\().4s
.endm

.macro iwht4_12 c0, c1, c2, c3
        iwht4_10        \c0, \c1, \c2, \c3
.endm

.macro idct4_10 c0, c1, c2, c3
        mul             v22.4s,    \c1\().4s, v0.s[3]
        mul             v20.4s,    \c1\().4s, v0.s[2]
        add             v16.4s,    \c0\().4s, \c2\().4s
        sub             v17.4s,    \c0\().4s, \c2\().4s
        mla             v22.4s,    \c3\().4s, v0.s[2]
        mul             v18.4s,    v16.4s,    v0.s[0]
        mul             v24.4s,    v17.4s,    v0.s[0]
        mls             v20.4s,    \c3\().4s, v0.s[3]
        srshr           v22.4s,    v22.4s,    #14
        srshr           v18.4s,    v18.4s,    #14
        srshr           v24.4s,    v24.4s,    #14
        srshr           v20.4s,    v20.4s,    #14
        add             \c0\().4s, v18.4s,    v22.4s
        sub             \c3\().4s, v18.4s,    v22.4s
        add             \c1\().4s, v24.4s,    v20.4s
        sub             \c2\().4s, v24.4s,    v20.4s
.endm

.macro idct4_12 c0, c1, c2, c3
        smull           v22.2d,    \c1\().2s, v0.s[3]
        smull2          v23.2d,    \c1\().4s, v0.s[3]
        smull           v20.2d,    \c1\().2s, v0.s[2]
        smull2          v21.2d,    \c1\().4s, v0.s[2]
        add             v16.4s,    \c0\().4s, \c2\().4s
        sub             v17.4s,    \c0\().4s, \c2\().4s
        smlal           v22.2d,    \c3\().2s, v0.s[2]
        smlal2          v23.2d,    \c3\().4s, v0.s[2]
        smull           v18.2d,    v16.2s,    v0.s[0]
        smull2          v19.2d,    v16.4s,    v0.s[0]
        smull           v24.2d,    v17.2s,    v0.s[0]
        smull2          v25.2d,    v17.4s,    v0.s[0]
        smlsl           v20.2d,    \c3\().2s, v0.s[3]
        smlsl2          v21.2d,    \c3\().4s, v0.s[3]
        rshrn           v22.2s,    v22.2d,    #14
        rshrn2          v22.4s,    v23.2d,    #14
        rshrn           v18.2s,    v18.2d,    #14
        rshrn2          v18.4s,    v19.2d,    #14
        rshrn           v24.2s,    v24.2d,    #14
        rshrn2          v24.4s,    v25.2d,    #14
        rshrn           v20.2s,    v20.2d,    #14
        rshrn2          v20.4s,    v21.2d,    #14
        add             \c0\().4s, v18.4s,    v22.4s
        sub             \c3\().4s, v18.4s,    v22.4s
        add             \c1\().4s, v24.4s,    v20.4s
        sub             \c2\().4s, v24.4s,    v20.4s
.endm

.macro iadst4_10 c0, c1, c2, c3
        mul             v16.4s,    \c0\().4s, v1.s[0]
        mla             v16.4s,    \c2\().4s, v1.s[1]
        mla             v16.4s,    \c3\().4s, v1.s[2]
        mul             v18.4s,    \c0\().4s, v1.s[2]
        mls             v18.4s,    \c2\().4s, v1.s[0]
        sub             \c0\().4s, \c0\().4s, \c2\().4s
        mls             v18.4s,    \c3\().4s, v1.s[1]
        add             \c0\().4s, \c0\().4s, \c3\().4s
        mul             v22.4s,    \c1\().4s, v1.s[3]
        mul             v20.4s,    \c0\().4s, v1.s[3]
        add             v24.4s,    v16.4s,    v22.4s
        add             v26.4s,    v18.4s,    v22.4s
        srshr           \c0\().4s, v24.4s,    #14
        add             v16.4s,    v16.4s,    v18.4s
        srshr           \c1\().4s, v26.4s,    #14
        sub             v16.4s,    v16.4s,    v22.4s
        srshr           \c2\().4s, v20.4s,    #14
        srshr           \c3\().4s, v16.4s,    #14
.endm

.macro iadst4_12 c0, c1, c2, c3
        smull           v16.2d,    \c0\().2s, v1.s[0]
        smull2          v17.2d,    \c0\().4s, v1.s[0]
        smlal           v16.2d,    \c2\().2s, v1.s[1]
        smlal2          v17.2d,    \c2\().4s, v1.s[1]
        smlal           v16.2d,    \c3\().2s, v1.s[2]
        smlal2          v17.2d,    \c3\().4s, v1.s[2]
        smull           v18.2d,    \c0\().2s, v1.s[2]
        smull2          v19.2d,    \c0\().4s, v1.s[2]
        smlsl           v18.2d,    \c2\().2s, v1.s[0]
        smlsl2          v19.2d,    \c2\().4s, v1.s[0]
        sub             \c0\().4s, \c0\().4s, \c2\().4s
        smlsl           v18.2d,    \c3\().2s, v1.s[1]
        smlsl2          v19.2d,    \c3\().4s, v1.s[1]
        add             \c0\().4s, \c0\().4s, \c3\().4s
        smull           v22.2d,    \c1\().2s, v1.s[3]
        smull2          v23.2d,    \c1\().4s, v1.s[3]
        smull           v20.2d,    \c0\().2s, v1.s[3]
        smull2          v21.2d,    \c0\().4s, v1.s[3]
        add             v24.2d,    v16.2d,    v22.2d
        add             v25.2d,    v17.2d,    v23.2d
        add             v26.2d,    v18.2d,    v22.2d
        add             v27.2d,    v19.2d,    v23.2d
        rshrn           \c0\().2s, v24.2d,    #14
        rshrn2          \c0\().4s, v25.2d,    #14
        add             v16.2d,    v16.2d,    v18.2d
        add             v17.2d,    v17.2d,    v19.2d
        rshrn           \c1\().2s, v26.2d,    #14
        rshrn2          \c1\().4s, v27.2d,    #14
        sub             v16.2d,    v16.2d,    v22.2d
        sub             v17.2d,    v17.2d,    v23.2d
        rshrn           \c2\().2s, v20.2d,    #14
        rshrn2          \c2\().4s, v21.2d,    #14
        rshrn           \c3\().2s, v16.2d,    #14
        rshrn2          \c3\().4s, v17.2d,    #14
.endm

// The public functions in this file have got the following signature:
// void itxfm_add(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);

.macro itxfm_func4x4 txfm1, txfm2, bpp
function ff_vp9_\txfm1\()_\txfm2\()_4x4_add_\bpp\()_neon, export=1
.ifc \txfm1,\txfm2
.ifc \txfm1,idct
        movrel          x4,  itxfm4_coeffs
        ld1             {v0.4h}, [x4]
        sxtl            v0.4s,  v0.4h
.endif
.ifc \txfm1,iadst
        movrel          x4,  iadst4_coeffs
        ld1             {v0.d}[1], [x4]
        sxtl2           v1.4s,  v0.8h
.endif
.else
        movrel          x4,  itxfm4_coeffs
        ld1             {v0.8h}, [x4]
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h
.endif

        movi            v30.4s, #0
        movi            v31.4s, #0
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #1
        b.ne            1f
        // DC-only for idct/idct
        ld1             {v2.s}[0],  [x2]
        smull           v2.2d,  v2.2s, v0.s[0]
        rshrn           v2.2s,  v2.2d, #14
        smull           v2.2d,  v2.2s, v0.s[0]
        rshrn           v2.2s,  v2.2d, #14
        st1             {v31.s}[0], [x2]
        dup             v4.4s,  v2.s[0]
        mov             v5.16b, v4.16b
        mov             v6.16b, v4.16b
        mov             v7.16b, v4.16b
        b               2f
.endif

1:
        ld1             {v4.4s,v5.4s,v6.4s,v7.4s},  [x2]
        st1             {v30.4s,v31.4s}, [x2], #32

.ifc \txfm1,iwht
        sshr            v4.4s,  v4.4s,  #2
        sshr            v5.4s,  v5.4s,  #2
        sshr            v6.4s,  v6.4s,  #2
        sshr            v7.4s,  v7.4s,  #2
.endif

        \txfm1\()4_\bpp v4,  v5,  v6,  v7

        st1             {v30.4s,v31.4s}, [x2], #32
        // Transpose 4x4 with 32 bit elements
        transpose_4x4s  v4,  v5,  v6,  v7,  v16, v17, v18, v19

        \txfm2\()4_\bpp v4,  v5,  v6,  v7
2:
        mvni            v31.8h, #((0xff << (\bpp - 8)) & 0xff), lsl #8
        ld1             {v0.4h},   [x0], x1
        ld1             {v1.4h},   [x0], x1
.ifnc \txfm1,iwht
        srshr           v4.4s,  v4.4s,  #4
        srshr           v5.4s,  v5.4s,  #4
        srshr           v6.4s,  v6.4s,  #4
        srshr           v7.4s,  v7.4s,  #4
.endif
        uaddw           v4.4s,  v4.4s,  v0.4h
        uaddw           v5.4s,  v5.4s,  v1.4h
        ld1             {v2.4h},   [x0], x1
        ld1             {v3.4h},   [x0], x1
        sqxtun          v0.4h,  v4.4s
        sqxtun2         v0.8h,  v5.4s
        sub             x0,  x0,  x1, lsl #2

        uaddw           v6.4s,  v6.4s,  v2.4h
        umin            v0.8h,  v0.8h,  v31.8h
        uaddw           v7.4s,  v7.4s,  v3.4h
        st1             {v0.4h},   [x0], x1
        sqxtun          v2.4h,  v6.4s
        sqxtun2         v2.8h,  v7.4s
        umin            v2.8h,  v2.8h,  v31.8h

        st1             {v0.d}[1], [x0], x1
        st1             {v2.4h},   [x0], x1
        st1             {v2.d}[1], [x0], x1

        ret
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

function idct8x8_dc_add_neon
        movrel          x4,  idct_coeffs
        ld1             {v0.4h}, [x4]

        movi            v1.4h,  #0
        sxtl            v0.4s,  v0.4h

        ld1             {v2.s}[0],  [x2]
        smull           v2.2d,  v2.2s,  v0.s[0]
        rshrn           v2.2s,  v2.2d,  #14
        smull           v2.2d,  v2.2s,  v0.s[0]
        rshrn           v2.2s,  v2.2d,  #14
        st1             {v1.s}[0],  [x2]
        dup             v2.4s,  v2.s[0]

        srshr           v2.4s,  v2.4s,  #5

        mov             x4,  #8
        mov             x3,  x0
        dup             v31.8h, w5
1:
        // Loop to add the constant from v2 into all 8x8 outputs
        subs            x4,  x4,  #2
        ld1             {v3.8h},  [x0], x1
        ld1             {v4.8h},  [x0], x1
        uaddw           v16.4s, v2.4s,  v3.4h
        uaddw2          v17.4s, v2.4s,  v3.8h
        uaddw           v18.4s, v2.4s,  v4.4h
        uaddw2          v19.4s, v2.4s,  v4.8h
        sqxtun          v3.4h,  v16.4s
        sqxtun2         v3.8h,  v17.4s
        sqxtun          v4.4h,  v18.4s
        sqxtun2         v4.8h,  v19.4s
        umin            v3.8h,  v3.8h,  v31.8h
        umin            v4.8h,  v4.8h,  v31.8h
        st1             {v3.8h},  [x3], x1
        st1             {v4.8h},  [x3], x1
        b.ne            1b

        ret
endfunc

.macro idct8 r0, r1, r2, r3, r4, r5, r6, r7, t0, t1, t2, t3, t4, t5
        dmbutterfly0    \r0, \r4, \r0, \r4, \t0, \t1, \t2, \t3, \t4, \t5 // r0 = t0a, r4 = t1a
        dmbutterfly     \r2, \r6, v0.s[2], v0.s[3], \t0, \t1, \t2, \t3   // r2 = t2a, r6 = t3a
        dmbutterfly     \r1, \r7, v1.s[0], v1.s[1], \t0, \t1, \t2, \t3   // r1 = t4a, r7 = t7a
        dmbutterfly     \r5, \r3, v1.s[2], v1.s[3], \t0, \t1, \t2, \t3   // r5 = t5a, r3 = t6a

        butterfly_4s    \t0, \t1, \r0, \r6 // t0 = t0, t1 = t3
        butterfly_4s    \t2, \r5, \r1, \r5 // t2 = t4, r5 = t5a
        butterfly_4s    \t3, \r6, \r7, \r3 // t3 = t7, r6 = t6a
        butterfly_4s    \r7, \r4, \r4, \r2 // r7 = t1, r4 = t2

        dmbutterfly0    \r6, \r5, \r6, \r5, \r0, \r1, \r2, \r3, \t4, \t5 // r6 = t6, r5 = t5

        butterfly_4s    \r1, \r6, \r7, \r6 // r1 = out[1], r6 = out[6]
        butterfly_4s    \r0, \r7, \t0, \t3 // r0 = out[0], r7 = out[7]
        butterfly_4s    \r2, \r5, \r4, \r5 // r2 = out[2], r5 = out[5]
        butterfly_4s    \r3, \r4, \t1, \t2 // r3 = out[3], r4 = out[4]
.endm

.macro iadst8 r0, r1, r2, r3, r4, r5, r6, r7, t0, t1, t2, t3, t4, t5
        dmbutterfly_l   \t2, \t3, \t0, \t1, \r7, \r0, v2.s[1], v2.s[0]   // t2,t3 = t1a, t0,t1 = t0a
        dmbutterfly_l   \r0, \r7, \t4, \t5, \r3, \r4, v3.s[1], v3.s[0]   // r0,r7 = t5a, t4,t5 = t4a

        dbutterfly_n    \r3, \t0, \t0, \t1, \t4, \t5, \r3, \r4, \t0, \t1 // r3 = t0, t0 = t4
        dbutterfly_n    \r4, \t1, \t2, \t3, \r0, \r7, \r4, \t1, \t4, \t5 // r4 = t1, t1 = t5

        dmbutterfly_l   \t4, \t5, \t2, \t3, \r5, \r2, v2.s[3], v2.s[2]   // t4,t5 = t3a, t2,t3 = t2a
        dmbutterfly_l   \r2, \r5, \r0, \r7, \r1, \r6, v3.s[3], v3.s[2]   // r2,r5 = t7a, r0,r7 = t6a

        dbutterfly_n    \r1, \t2, \t2, \t3, \r0, \r7, \r1, \r6, \t2, \t3 // r1 = t2, t2 = t6
        dbutterfly_n    \r0, \t4, \t4, \t5, \r2, \r5, \r0, \r7, \t4, \t5 // r0 = t3, t4 = t7

        butterfly_4s    \r7, \r4, \r4, \r0   // r7 = -out[7], r4 = t3
        neg             \r7\().4s, \r7\().4s // r7 = out[7]
        butterfly_4s    \r0, \r1, \r3, \r1   // r0 = out[0],  r1 = t2

        dmbutterfly_l   \r2, \r3, \t3, \t5, \t0, \t1, v0.s[2], v0.s[3]   // r2,r3 = t5a, t3,t5 = t4a
        dmbutterfly_l   \t0, \t1, \r5, \r6, \t4, \t2, v0.s[3], v0.s[2]   // t0,t1 = t6a, r5,r6 = t7a

        dbutterfly_n    \r6, \t2, \r2, \r3, \r5, \r6, \t2, \t4, \r2, \r3 // r6 = out[6],  t2 = t7

        dmbutterfly0    \r3, \r4, \r1, \r4, \t4, \r5, \r1, \r2           // r3 = -out[3], r4 = out[4]
        neg             \r3\().4s, \r3\().4s  // r3 = out[3]

        dbutterfly_n    \r1, \t0, \t3, \t5, \t0, \t1, \r1, \r2, \t0, \t1 // r1 = -out[1], t0 = t6
        neg             \r1\().4s, \r1\().4s  // r1 = out[1]

        dmbutterfly0    \r2, \r5, \t0, \t2, \t1, \t3, \t4, \t5           // r2 = out[2],  r5 = -out[5]
        neg             \r5\().4s, \r5\().4s  // r5 = out[5]
.endm


.macro itxfm_func8x8 txfm1, txfm2
function vp9_\txfm1\()_\txfm2\()_8x8_add_16_neon
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #1
        b.eq            idct8x8_dc_add_neon
.endif
        // The iadst also uses a few coefficients from
        // idct, so those always need to be loaded.
.ifc \txfm1\()_\txfm2,idct_idct
        movrel          x4,  idct_coeffs
.else
        movrel          x4,  iadst8_coeffs
        ld1             {v1.8h}, [x4], #16
        stp             d8,  d9,  [sp, #-0x10]!
        sxtl2           v3.4s,  v1.8h
        sxtl            v2.4s,  v1.4h
.endif
        ld1             {v0.8h}, [x4]
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h

        movi            v4.4s, #0
        movi            v5.4s, #0
        movi            v6.4s, #0
        movi            v7.4s, #0

1:
        ld1             {v16.4s,v17.4s,v18.4s,v19.4s},  [x2], #64
        ld1             {v20.4s,v21.4s,v22.4s,v23.4s},  [x2], #64
        ld1             {v24.4s,v25.4s,v26.4s,v27.4s},  [x2], #64
        ld1             {v28.4s,v29.4s,v30.4s,v31.4s},  [x2], #64
        sub             x2,  x2,  #256
        st1             {v4.4s,v5.4s,v6.4s,v7.4s},      [x2], #64
        st1             {v4.4s,v5.4s,v6.4s,v7.4s},      [x2], #64
        st1             {v4.4s,v5.4s,v6.4s,v7.4s},      [x2], #64
        st1             {v4.4s,v5.4s,v6.4s,v7.4s},      [x2], #64

.ifc \txfm1\()_\txfm2,idct_idct
        idct8           v16, v18, v20, v22, v24, v26, v28, v30, v2,  v3,  v4,  v5,  v6,  v7
        idct8           v17, v19, v21, v23, v25, v27, v29, v31, v2,  v3,  v4,  v5,  v6,  v7
.else
        \txfm1\()8      v16, v18, v20, v22, v24, v26, v28, v30, v4,  v5,  v6,  v7,  v8,  v9
        \txfm1\()8      v17, v19, v21, v23, v25, v27, v29, v31, v4,  v5,  v6,  v7,  v8,  v9
.endif

        // Transpose 8x8 with 16 bit elements
        transpose_8x8s  v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31, v4, v5, v6, v7

.ifc \txfm1\()_\txfm2,idct_idct
        idct8           v16, v18, v20, v22, v24, v26, v28, v30, v2,  v3,  v4,  v5,  v6,  v7
        idct8           v17, v19, v21, v23, v25, v27, v29, v31, v2,  v3,  v4,  v5,  v6,  v7
.else
        \txfm2\()8      v16, v18, v20, v22, v24, v26, v28, v30, v4,  v5,  v6,  v7,  v8,  v9
        \txfm2\()8      v17, v19, v21, v23, v25, v27, v29, v31, v4,  v5,  v6,  v7,  v8,  v9
.endif
2:
        mov             x3,  x0
        // Add into the destination
        ld1             {v0.8h},  [x0], x1
        srshr           v16.4s, v16.4s, #5
        srshr           v17.4s, v17.4s, #5
        ld1             {v1.8h},  [x0], x1
        srshr           v18.4s, v18.4s, #5
        srshr           v19.4s, v19.4s, #5
        ld1             {v2.8h},  [x0], x1
        srshr           v20.4s, v20.4s, #5
        srshr           v21.4s, v21.4s, #5
        uaddw           v16.4s, v16.4s, v0.4h
        uaddw2          v17.4s, v17.4s, v0.8h
        ld1             {v3.8h},  [x0], x1
        srshr           v22.4s, v22.4s, #5
        srshr           v23.4s, v23.4s, #5
        uaddw           v18.4s, v18.4s, v1.4h
        uaddw2          v19.4s, v19.4s, v1.8h
        ld1             {v4.8h},  [x0], x1
        srshr           v24.4s, v24.4s, #5
        srshr           v25.4s, v25.4s, #5
        uaddw           v20.4s, v20.4s, v2.4h
        uaddw2          v21.4s, v21.4s, v2.8h
        sqxtun          v0.4h,  v16.4s
        sqxtun2         v0.8h,  v17.4s
        dup             v16.8h, w5
        ld1             {v5.8h},  [x0], x1
        srshr           v26.4s, v26.4s, #5
        srshr           v27.4s, v27.4s, #5
        uaddw           v22.4s, v22.4s, v3.4h
        uaddw2          v23.4s, v23.4s, v3.8h
        sqxtun          v1.4h,  v18.4s
        sqxtun2         v1.8h,  v19.4s
        umin            v0.8h,  v0.8h,  v16.8h
        ld1             {v6.8h},  [x0], x1
        srshr           v28.4s, v28.4s, #5
        srshr           v29.4s, v29.4s, #5
        uaddw           v24.4s, v24.4s, v4.4h
        uaddw2          v25.4s, v25.4s, v4.8h
        sqxtun          v2.4h,  v20.4s
        sqxtun2         v2.8h,  v21.4s
        umin            v1.8h,  v1.8h,  v16.8h
        ld1             {v7.8h},  [x0], x1
        srshr           v30.4s, v30.4s, #5
        srshr           v31.4s, v31.4s, #5
        uaddw           v26.4s, v26.4s, v5.4h
        uaddw2          v27.4s, v27.4s, v5.8h
        sqxtun          v3.4h,  v22.4s
        sqxtun2         v3.8h,  v23.4s
        umin            v2.8h,  v2.8h,  v16.8h

        st1             {v0.8h},  [x3], x1
        uaddw           v28.4s, v28.4s, v6.4h
        uaddw2          v29.4s, v29.4s, v6.8h
        st1             {v1.8h},  [x3], x1
        sqxtun          v4.4h,  v24.4s
        sqxtun2         v4.8h,  v25.4s
        umin            v3.8h,  v3.8h,  v16.8h
        st1             {v2.8h},  [x3], x1
        uaddw           v30.4s, v30.4s, v7.4h
        uaddw2          v31.4s, v31.4s, v7.8h
        st1             {v3.8h},  [x3], x1
        sqxtun          v5.4h,  v26.4s
        sqxtun2         v5.8h,  v27.4s
        umin            v4.8h,  v4.8h,  v16.8h
        st1             {v4.8h},  [x3], x1
        sqxtun          v6.4h,  v28.4s
        sqxtun2         v6.8h,  v29.4s
        umin            v5.8h,  v5.8h,  v16.8h
        st1             {v5.8h},  [x3], x1
        sqxtun          v7.4h,  v30.4s
        sqxtun2         v7.8h,  v31.4s
        umin            v6.8h,  v6.8h,  v16.8h

        st1             {v6.8h},  [x3], x1
        umin            v7.8h,  v7.8h,  v16.8h
        st1             {v7.8h},  [x3], x1

.ifnc \txfm1\()_\txfm2,idct_idct
        ldp             d8,  d9,  [sp], 0x10
.endif
        ret
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_8x8_add_10_neon, export=1
        mov             x5,  #0x03ff
        b               vp9_\txfm1\()_\txfm2\()_8x8_add_16_neon
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_8x8_add_12_neon, export=1
        mov             x5,  #0x0fff
        b               vp9_\txfm1\()_\txfm2\()_8x8_add_16_neon
endfunc
.endm

itxfm_func8x8 idct,  idct
itxfm_func8x8 iadst, idct
itxfm_func8x8 idct,  iadst
itxfm_func8x8 iadst, iadst


function idct16x16_dc_add_neon
        movrel          x4,  idct_coeffs
        ld1             {v0.4h}, [x4]
        sxtl            v0.4s,  v0.4h

        movi            v1.4h,  #0

        ld1             {v2.s}[0],  [x2]
        smull           v2.2d,  v2.2s,  v0.s[0]
        rshrn           v2.2s,  v2.2d,  #14
        smull           v2.2d,  v2.2s,  v0.s[0]
        rshrn           v2.2s,  v2.2d,  #14
        st1             {v1.s}[0],  [x2]
        dup             v2.4s,  v2.s[0]

        srshr           v0.4s,  v2.4s,  #6

        mov             x3, x0
        mov             x4, #16
        dup             v31.8h, w13
1:
        // Loop to add the constant from v2 into all 16x16 outputs
        subs            x4,  x4,  #2
        ld1             {v1.8h,v2.8h},  [x0], x1
        uaddw           v16.4s, v0.4s,  v1.4h
        uaddw2          v17.4s, v0.4s,  v1.8h
        ld1             {v3.8h,v4.8h},  [x0], x1
        uaddw           v18.4s, v0.4s,  v2.4h
        uaddw2          v19.4s, v0.4s,  v2.8h
        uaddw           v20.4s, v0.4s,  v3.4h
        uaddw2          v21.4s, v0.4s,  v3.8h
        uaddw           v22.4s, v0.4s,  v4.4h
        uaddw2          v23.4s, v0.4s,  v4.8h
        sqxtun          v1.4h,  v16.4s
        sqxtun2         v1.8h,  v17.4s
        sqxtun          v2.4h,  v18.4s
        sqxtun2         v2.8h,  v19.4s
        sqxtun          v3.4h,  v20.4s
        sqxtun2         v3.8h,  v21.4s
        sqxtun          v4.4h,  v22.4s
        sqxtun2         v4.8h,  v23.4s
        umin            v1.8h,  v1.8h,  v31.8h
        umin            v2.8h,  v2.8h,  v31.8h
        st1             {v1.8h,v2.8h},  [x3], x1
        umin            v3.8h,  v3.8h,  v31.8h
        umin            v4.8h,  v4.8h,  v31.8h
        st1             {v3.8h,v4.8h},  [x3], x1
        b.ne            1b

        ret
endfunc

.macro idct16_end
        butterfly_4s    v18, v7,  v4,  v7                // v18 = t0a,  v7  = t7a
        butterfly_4s    v19, v22, v5,  v22               // v19 = t1a,  v22 = t6
        butterfly_4s    v4,  v26, v20, v26               // v4  = t2a,  v26 = t5
        butterfly_4s    v5,  v6,  v28, v6                // v5  = t3a,  v6  = t4
        butterfly_4s    v20, v28, v16, v24               // v20 = t8a,  v28 = t11a
        butterfly_4s    v24, v21, v23, v21               // v24 = t9,   v21 = t10
        butterfly_4s    v23, v27, v25, v27               // v23 = t14,  v27 = t13
        butterfly_4s    v25, v29, v29, v17               // v25 = t15a, v29 = t12a

        dmbutterfly0    v8,  v9,  v27, v21, v8,  v9,  v16, v17, v30, v31 // v8  = t13a, v9  = t10a
        dmbutterfly0    v28, v27, v29, v28, v21, v29, v16, v17, v30, v31 // v28 = t12,  v27 = t11

        butterfly_4s    v16, v31, v18, v25               // v16 = out[0], v31 = out[15]
        butterfly_4s    v17, v30, v19, v23               // v17 = out[1], v30 = out[14]
        butterfly_4s_r  v25, v22, v22, v24               // v25 = out[9], v22 = out[6]
        butterfly_4s    v23, v24, v7,  v20               // v23 = out[7], v24 = out[8]
        butterfly_4s    v18, v29, v4,  v8                // v18 = out[2], v29 = out[13]
        butterfly_4s    v19, v28, v5,  v28               // v19 = out[3], v28 = out[12]
        butterfly_4s    v20, v27, v6,  v27               // v20 = out[4], v27 = out[11]
        butterfly_4s    v21, v26, v26, v9                // v21 = out[5], v26 = out[10]
        ret
.endm

function idct16
        dmbutterfly0    v16, v24, v16, v24, v4, v5, v6, v7, v8, v9 // v16 = t0a,  v24 = t1a
        dmbutterfly     v20, v28, v0.s[2], v0.s[3], v4, v5, v6, v7 // v20 = t2a,  v28 = t3a
        dmbutterfly     v18, v30, v1.s[0], v1.s[1], v4, v5, v6, v7 // v18 = t4a,  v30 = t7a
        dmbutterfly     v26, v22, v1.s[2], v1.s[3], v4, v5, v6, v7 // v26 = t5a,  v22 = t6a
        dmbutterfly     v17, v31, v2.s[0], v2.s[1], v4, v5, v6, v7 // v17 = t8a,  v31 = t15a
        dmbutterfly     v25, v23, v2.s[2], v2.s[3], v4, v5, v6, v7 // v25 = t9a,  v23 = t14a
        dmbutterfly     v21, v27, v3.s[0], v3.s[1], v4, v5, v6, v7 // v21 = t10a, v27 = t13a
        dmbutterfly     v29, v19, v3.s[2], v3.s[3], v4, v5, v6, v7 // v29 = t11a, v19 = t12a

        butterfly_4s    v4,  v28, v16, v28               // v4  = t0,   v28 = t3
        butterfly_4s    v5,  v20, v24, v20               // v5  = t1,   v20 = t2
        butterfly_4s    v6,  v26, v18, v26               // v6  = t4,   v26 = t5
        butterfly_4s    v7,  v22, v30, v22               // v7  = t7,   v22 = t6
        butterfly_4s    v16, v25, v17, v25               // v16 = t8,   v25 = t9
        butterfly_4s    v24, v21, v29, v21               // v24 = t11,  v21 = t10
        butterfly_4s    v17, v27, v19, v27               // v17 = t12,  v27 = t13
        butterfly_4s    v29, v23, v31, v23               // v29 = t15,  v23 = t14

        dmbutterfly0    v22, v26, v22, v26, v8, v9, v18, v19, v30, v31        // v22 = t6a,  v26 = t5a
        dmbutterfly     v23, v25, v0.s[2], v0.s[3], v18, v19, v30, v31        // v23 = t9a,  v25 = t14a
        dmbutterfly     v27, v21, v0.s[2], v0.s[3], v18, v19, v30, v31, neg=1 // v27 = t13a, v21 = t10a
        idct16_end
endfunc

function idct16_half
        dmbutterfly0_h  v16, v24, v16, v24, v4, v5, v6, v7, v8, v9 // v16 = t0a,  v24 = t1a
        dmbutterfly_h1  v20, v28, v0.s[2], v0.s[3], v4, v5, v6, v7 // v20 = t2a,  v28 = t3a
        dmbutterfly_h1  v18, v30, v1.s[0], v1.s[1], v4, v5, v6, v7 // v18 = t4a,  v30 = t7a
        dmbutterfly_h2  v26, v22, v1.s[2], v1.s[3], v4, v5, v6, v7 // v26 = t5a,  v22 = t6a
        dmbutterfly_h1  v17, v31, v2.s[0], v2.s[1], v4, v5, v6, v7 // v17 = t8a,  v31 = t15a
        dmbutterfly_h2  v25, v23, v2.s[2], v2.s[3], v4, v5, v6, v7 // v25 = t9a,  v23 = t14a
        dmbutterfly_h1  v21, v27, v3.s[0], v3.s[1], v4, v5, v6, v7 // v21 = t10a, v27 = t13a
        dmbutterfly_h2  v29, v19, v3.s[2], v3.s[3], v4, v5, v6, v7 // v29 = t11a, v19 = t12a

        butterfly_4s    v4,  v28, v16, v28               // v4  = t0,   v28 = t3
        butterfly_4s    v5,  v20, v24, v20               // v5  = t1,   v20 = t2
        butterfly_4s    v6,  v26, v18, v26               // v6  = t4,   v26 = t5
        butterfly_4s    v7,  v22, v30, v22               // v7  = t7,   v22 = t6
        butterfly_4s    v16, v25, v17, v25               // v16 = t8,   v25 = t9
        butterfly_4s    v24, v21, v29, v21               // v24 = t11,  v21 = t10
        butterfly_4s    v17, v27, v19, v27               // v17 = t12,  v27 = t13
        butterfly_4s    v29, v23, v31, v23               // v29 = t15,  v23 = t14

        dmbutterfly0    v22, v26, v22, v26, v8, v9, v18, v19, v30, v31        // v22 = t6a,  v26 = t5a
        dmbutterfly     v23, v25, v0.s[2], v0.s[3], v18, v19, v30, v31        // v23 = t9a,  v25 = t14a
        dmbutterfly     v27, v21, v0.s[2], v0.s[3], v18, v19, v30, v31, neg=1 // v27 = t13a, v21 = t10a
        idct16_end
endfunc

function idct16_quarter
        dsmull_h        v24, v25, v19, v3.s[3]
        dsmull_h        v4,  v5,  v17, v2.s[0]
        dsmull_h        v7,  v6,  v18, v1.s[1]
        dsmull_h        v30, v31, v18, v1.s[0]
        neg             v24.2d,  v24.2d
        neg             v25.2d,  v25.2d
        dsmull_h        v29, v28, v17, v2.s[1]
        dsmull_h        v26, v27, v19, v3.s[2]
        dsmull_h        v22, v23, v16, v0.s[0]
        drshrn_h        v24, v24, v25, #14
        drshrn_h        v16, v4,  v5,  #14
        drshrn_h        v7,  v7,  v6,  #14
        drshrn_h        v6,  v30, v31, #14
        drshrn_h        v29, v29, v28, #14
        drshrn_h        v17, v26, v27, #14
        drshrn_h        v28, v22, v23, #14

        dmbutterfly_l   v20, v21, v22, v23, v17, v24, v0.s[2], v0.s[3]
        dmbutterfly_l   v18, v19, v30, v31, v29, v16, v0.s[2], v0.s[3]
        neg             v22.2d,  v22.2d
        neg             v23.2d,  v23.2d
        drshrn_h        v27, v20, v21, #14
        drshrn_h        v21, v22, v23, #14
        drshrn_h        v23, v18, v19, #14
        drshrn_h        v25, v30, v31, #14
        mov             v4.16b,  v28.16b
        mov             v5.16b,  v28.16b
        dmbutterfly0    v22, v26, v7,  v6,  v18, v19, v30, v31
        mov             v20.16b, v28.16b
        idct16_end
endfunc

function iadst16
        ld1             {v0.8h,v1.8h}, [x11]
        sxtl            v2.4s,  v1.4h
        sxtl2           v3.4s,  v1.8h
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h

        dmbutterfly_l   v6,  v7,  v4,  v5,  v31, v16, v0.s[1], v0.s[0]   // v6,v7   = t1,   v4,v5   = t0
        dmbutterfly_l   v10, v11, v8,  v9,  v23, v24, v1.s[1], v1.s[0]   // v10,v11 = t9,   v8,v9   = t8
        dbutterfly_n    v31, v24, v6,  v7,  v10, v11, v12, v13, v10, v11 // v31     = t1a,  v24     = t9a
        dmbutterfly_l   v14, v15, v12, v13, v29, v18, v0.s[3], v0.s[2]   // v14,v15 = t3,   v12,v13 = t2
        dbutterfly_n    v16, v23, v4,  v5,  v8,  v9,  v6,  v7,  v8,  v9  // v16     = t0a,  v23     = t8a

        dmbutterfly_l   v6,  v7,  v4,  v5,  v21, v26, v1.s[3], v1.s[2]   // v6,v7   = t11,  v4,v5   = t10
        dbutterfly_n    v29, v26, v14, v15, v6,  v7,  v8,  v9,  v6,  v7  // v29     = t3a,  v26     = t11a
        dmbutterfly_l   v10, v11, v8,  v9,  v27, v20, v2.s[1], v2.s[0]   // v10,v11 = t5,   v8,v9   = t4
        dbutterfly_n    v18, v21, v12, v13, v4,  v5,  v6,  v7,  v4,  v5  // v18     = t2a,  v21     = t10a

        dmbutterfly_l   v14, v15, v12, v13, v19, v28, v3.s[1], v3.s[0]   // v14,v15 = t13,  v12,v13 = t12
        dbutterfly_n    v20, v28, v10, v11, v14, v15, v4,  v5,  v14, v15 // v20     = t5a,  v28     = t13a
        dmbutterfly_l   v6,  v7,  v4,  v5,  v25, v22, v2.s[3], v2.s[2]   // v6,v7   = t7,   v4,v5   = t6
        dbutterfly_n    v27, v19, v8,  v9,  v12, v13, v10, v11, v12, v13 // v27     = t4a,  v19     = t12a

        dmbutterfly_l   v10, v11, v8,  v9,  v17, v30, v3.s[3], v3.s[2]   // v10,v11 = t15,  v8,v9   = t14
        ld1             {v0.8h}, [x10]
        dbutterfly_n    v22, v30, v6,  v7,  v10, v11, v12, v13, v10, v11 // v22     = t7a,  v30     = t15a
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h
        dmbutterfly_l   v14, v15, v12, v13, v23, v24, v1.s[0], v1.s[1]   // v14,v15 = t9,   v12,v13 = t8
        dbutterfly_n    v25, v17, v4,  v5,  v8,  v9,  v6,  v7,  v8,  v9  // v25     = t6a,  v17     = t14a

        dmbutterfly_l   v4,  v5,  v6,  v7,  v28, v19, v1.s[1], v1.s[0]   // v4,v5   = t12,  v6,v7   = t13
        dbutterfly_n    v23, v19, v12, v13, v4,  v5,  v8,  v9,  v4,  v5  // v23     = t8a,  v19     = t12a
        dmbutterfly_l   v10, v11, v8,  v9,  v21, v26, v1.s[2], v1.s[3]   // v10,v11 = t11,  v8,v9   = t10
        butterfly_4s_r  v4,  v27, v16, v27               // v4  = t4,   v27 = t0
        dbutterfly_n    v24, v28, v14, v15, v6,  v7,  v12, v13, v6,  v7  // v24     = t9a,  v28     = t13a

        dmbutterfly_l   v12, v13, v14, v15, v30, v17, v1.s[3], v1.s[2]   // v12,v13 = t14,  v14,v15 = t15
        butterfly_4s_r  v5,  v20, v31, v20               // v5  = t5, v20 = t1
        dbutterfly_n    v21, v17, v8,  v9,  v12, v13, v6,  v7,  v12, v13 // v21     = t10a, v17     = t14a
        dbutterfly_n    v26, v30, v10, v11, v14, v15, v8,  v9,  v14, v15 // v26     = t11a, v30     = t15a

        butterfly_4s_r  v6,  v25, v18, v25               // v6  = t6, v25 = t2
        butterfly_4s_r  v7,  v22, v29, v22               // v7  = t7, v22 = t3

        dmbutterfly_l   v10, v11, v8,  v9,  v19, v28, v0.s[2], v0.s[3]   // v10,v11 = t13,  v8,v9   = t12
        dmbutterfly_l   v12, v13, v14, v15, v30, v17, v0.s[3], v0.s[2]   // v12,v13 = t14,  v14,v15 = t15

        dbutterfly_n    v18, v30, v8,  v9,  v12, v13, v16, v17, v12, v13 // v18   = out[2], v30     = t14a
        dbutterfly_n    v29, v17, v10, v11, v14, v15, v12, v13, v14, v15 // v29 = -out[13], v17     = t15a
        neg             v29.4s, v29.4s                   // v29 = out[13]

        dmbutterfly_l   v10, v11, v8,  v9,  v4,  v5,  v0.s[2], v0.s[3]   // v10,v11 = t5a,  v8,v9   = t4a
        dmbutterfly_l   v12, v13, v14, v15, v7,  v6,  v0.s[3], v0.s[2]   // v12,v13 = t6a,  v14,v15 = t7a

        butterfly_4s    v2,  v6,  v27, v25               // v2 = out[0], v6 = t2a
        butterfly_4s    v3,  v7,  v23, v21               // v3 =-out[1], v7 = t10

        dbutterfly_n    v19, v31, v8,  v9,  v12, v13, v4,  v5,  v8,  v9  // v19 = -out[3],  v31 = t6
        neg             v19.4s, v19.4s                   // v19 = out[3]
        dbutterfly_n    v28, v16, v10, v11, v14, v15, v4,  v5,  v10, v11 // v28 = out[12],  v16 = t7

        butterfly_4s    v5,  v8,  v20, v22               // v5 =-out[15],v8 = t3a
        butterfly_4s    v4,  v9,  v24, v26               // v4 = out[14],v9 = t11

        dmbutterfly0    v23, v24, v6,  v8,  v10, v11, v12, v13, v14, v15, 1 // v23 = out[7], v24 = out[8]
        dmbutterfly0    v21, v26, v30, v17, v10, v11, v12, v13, v14, v15, 1 // v21 = out[5], v26 = out[10]
        dmbutterfly0    v20, v27, v16, v31, v10, v11, v12, v13, v14, v15    // v20 = out[4], v27 = out[11]
        dmbutterfly0    v22, v25, v9,  v7,  v10, v11, v12, v13, v14, v15    // v22 = out[6], v25 = out[9]

        neg             v31.4s,  v5.4s                    // v31 = out[15]
        neg             v17.4s,  v3.4s                    // v17 = out[1]

        mov             v16.16b, v2.16b
        mov             v30.16b, v4.16b
        ret
endfunc

// Helper macros; we can't use these expressions directly within
// e.g. .irp due to the extra concatenation \(). Therefore wrap
// them in macros to allow using .irp below.
.macro load i, src, inc
        ld1             {v\i\().4s},  [\src], \inc
.endm
.macro store i, dst, inc
        st1             {v\i\().4s},  [\dst], \inc
.endm
.macro movi_v i, size, imm
        movi            v\i\()\size,  \imm
.endm
.macro load_clear i, src, inc
        ld1             {v\i\().4s}, [\src]
        st1             {v4.4s},  [\src], \inc
.endm

.macro load_add_store coef0, coef1, coef2, coef3, coef4, coef5, coef6, coef7
        srshr           \coef0, \coef0, #6
        ld1             {v4.4h},   [x0], x1
        srshr           \coef1, \coef1, #6
        ld1             {v4.d}[1], [x3], x1
        srshr           \coef2, \coef2, #6
        ld1             {v5.4h},   [x0], x1
        srshr           \coef3, \coef3, #6
        uaddw           \coef0, \coef0, v4.4h
        ld1             {v5.d}[1], [x3], x1
        srshr           \coef4, \coef4, #6
        uaddw2          \coef1, \coef1, v4.8h
        ld1             {v6.4h},   [x0], x1
        srshr           \coef5, \coef5, #6
        uaddw           \coef2, \coef2, v5.4h
        ld1             {v6.d}[1], [x3], x1
        sqxtun          v4.4h,  \coef0
        srshr           \coef6, \coef6, #6
        uaddw2          \coef3, \coef3, v5.8h
        ld1             {v7.4h},   [x0], x1
        sqxtun2         v4.8h,  \coef1
        srshr           \coef7, \coef7, #6
        uaddw           \coef4, \coef4, v6.4h
        ld1             {v7.d}[1], [x3], x1
        umin            v4.8h,  v4.8h,  v8.8h
        sub             x0,  x0,  x1, lsl #2
        sub             x3,  x3,  x1, lsl #2
        sqxtun          v5.4h,  \coef2
        uaddw2          \coef5, \coef5, v6.8h
        st1             {v4.4h},   [x0], x1
        sqxtun2         v5.8h,  \coef3
        uaddw           \coef6, \coef6, v7.4h
        st1             {v4.d}[1], [x3], x1
        umin            v5.8h,  v5.8h,  v8.8h
        sqxtun          v6.4h,  \coef4
        uaddw2          \coef7, \coef7, v7.8h
        st1             {v5.4h},   [x0], x1
        sqxtun2         v6.8h,  \coef5
        st1             {v5.d}[1], [x3], x1
        umin            v6.8h,  v6.8h,  v8.8h
        sqxtun          v7.4h,  \coef6
        st1             {v6.4h},   [x0], x1
        sqxtun2         v7.8h,  \coef7
        st1             {v6.d}[1], [x3], x1
        umin            v7.8h,  v7.8h,  v8.8h
        st1             {v7.4h},   [x0], x1
        st1             {v7.d}[1], [x3], x1
.endm

// Read a vertical 4x16 slice out of a 16x16 matrix, do a transform on it,
// transpose into a horizontal 16x4 slice and store.
// x0 = dst (temp buffer)
// x1 = slice offset
// x2 = src
// x9 = input stride
.macro itxfm16_1d_funcs txfm
function \txfm\()16_1d_4x16_pass1_neon
        mov             x14, x30

        movi            v4.4s, #0
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        load_clear      \i,  x2,  x9
.endr

        bl              \txfm\()16

        // Do four 4x4 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v19, v20-v23, v24-v27 and v28-v31
        // contain the four transposed 4x4 blocks.
        transpose_4x4s  v16, v17, v18, v19, v4, v5, v6, v7
        transpose_4x4s  v20, v21, v22, v23, v4, v5, v6, v7
        transpose_4x4s  v24, v25, v26, v27, v4, v5, v6, v7
        transpose_4x4s  v28, v29, v30, v31, v4, v5, v6, v7

        // Store the transposed 4x4 blocks horizontally.
        cmp             x1,  #12
        b.eq            1f
.irp i, 16, 20, 24, 28, 17, 21, 25, 29, 18, 22, 26, 30, 19, 23, 27, 31
        store           \i,  x0,  #16
.endr
        br              x14
1:
        // Special case: For the last input column (x1 == 12),
        // which would be stored as the last row in the temp buffer,
        // don't store the first 4x4 block, but keep it in registers
        // for the first slice of the second pass (where it is the
        // last 4x4 block).
        add             x0,  x0,  #16
        st1             {v20.4s},  [x0], #16
        st1             {v24.4s},  [x0], #16
        st1             {v28.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v21.4s},  [x0], #16
        st1             {v25.4s},  [x0], #16
        st1             {v29.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v22.4s},  [x0], #16
        st1             {v26.4s},  [x0], #16
        st1             {v30.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v23.4s},  [x0], #16
        st1             {v27.4s},  [x0], #16
        st1             {v31.4s},  [x0], #16

        mov             v28.16b, v16.16b
        mov             v29.16b, v17.16b
        mov             v30.16b, v18.16b
        mov             v31.16b, v19.16b
        br              x14
endfunc

// Read a vertical 4x16 slice out of a 16x16 matrix, do a transform on it,
// load the destination pixels (from a similar 4x16 slice), add and store back.
// x0 = dst
// x1 = dst stride
// x2 = src (temp buffer)
// x3 = slice offset
// x9 = temp buffer stride
function \txfm\()16_1d_4x16_pass2_neon
        mov             x14, x30

.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
        load            \i,  x2,  x9
.endr
        cbz             x3,  1f
.irp i, 28, 29, 30, 31
        load            \i,  x2,  x9
.endr
1:

        add             x3,  x0,  x1
        lsl             x1,  x1,  #1
        bl              \txfm\()16

        dup             v8.8h, w13
        load_add_store  v16.4s, v17.4s, v18.4s, v19.4s, v20.4s, v21.4s, v22.4s, v23.4s
        load_add_store  v24.4s, v25.4s, v26.4s, v27.4s, v28.4s, v29.4s, v30.4s, v31.4s

        br              x14
endfunc
.endm

itxfm16_1d_funcs idct
itxfm16_1d_funcs iadst

// This is the minimum eob value for each subpartition, in increments of 4
const min_eob_idct_idct_16, align=4
        .short  0, 10, 38, 89
endconst

.macro itxfm_func16x16 txfm1, txfm2
function vp9_\txfm1\()_\txfm2\()_16x16_add_16_neon
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #1
        b.eq            idct16x16_dc_add_neon
.endif
        mov             x15, x30
        // iadst16 requires clobbering v8-v15, idct16 only clobbers v8-v9.
.ifnc \txfm1\()_\txfm2,idct_idct
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
.endif
        stp             d8,  d9,  [sp, #-0x10]!

        sub             sp,  sp,  #1024

        mov             x4,  x0
        mov             x5,  x1
        mov             x6,  x2

        movrel          x10, idct_coeffs
.ifnc \txfm1\()_\txfm2,idct_idct
        movrel          x11, iadst16_coeffs
.endif
.ifc \txfm1,idct
        ld1             {v0.8h,v1.8h}, [x10]
        sxtl            v2.4s,  v1.4h
        sxtl2           v3.4s,  v1.8h
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h
.endif
        mov             x9,  #64

.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #10
        b.le            idct16x16_quarter_add_16_neon
        cmp             w3,  #38
        b.le            idct16x16_half_add_16_neon

        movrel          x12, min_eob_idct_idct_16, 2
.endif

.irp i, 0, 4, 8, 12
        add             x0,  sp,  #(\i*64)
.ifc \txfm1\()_\txfm2,idct_idct
.if \i > 0
        ldrh            w1,  [x12], #2
        cmp             w3,  w1
        mov             x1,  #(16 - \i)/4
        b.le            1f
.endif
.endif
        mov             x1,  #\i
        add             x2,  x6,  #(\i*4)
        bl              \txfm1\()16_1d_4x16_pass1_neon
.endr
.ifc \txfm1\()_\txfm2,iadst_idct
        ld1             {v0.8h,v1.8h}, [x10]
        sxtl            v2.4s,  v1.4h
        sxtl2           v3.4s,  v1.8h
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h
.endif

.ifc \txfm1\()_\txfm2,idct_idct
        b               3f
1:
        // Set v28-v31 to zero, for the in-register passthrough of
        // coefficients to pass 2.
        movi            v28.4s,  #0
        movi            v29.4s,  #0
        movi            v30.4s,  #0
        movi            v31.4s,  #0
2:
        subs            x1,  x1,  #1
.rept 4
        st1             {v28.4s,v29.4s,v30.4s,v31.4s}, [x0], x9
.endr
        b.ne            2b
3:
.endif

.irp i, 0, 4, 8, 12
        add             x0,  x4,  #(\i*2)
        mov             x1,  x5
        add             x2,  sp,  #(\i*4)
        mov             x3,  #\i
        bl              \txfm2\()16_1d_4x16_pass2_neon
.endr

        add             sp,  sp,  #1024
        ldp             d8,  d9,  [sp], 0x10
.ifnc \txfm1\()_\txfm2,idct_idct
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
.endif
        br              x15
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_16x16_add_10_neon, export=1
        mov             x13, #0x03ff
        b               vp9_\txfm1\()_\txfm2\()_16x16_add_16_neon
endfunc

function ff_vp9_\txfm1\()_\txfm2\()_16x16_add_12_neon, export=1
        mov             x13, #0x0fff
        b               vp9_\txfm1\()_\txfm2\()_16x16_add_16_neon
endfunc
.endm

itxfm_func16x16 idct,  idct
itxfm_func16x16 iadst, idct
itxfm_func16x16 idct,  iadst
itxfm_func16x16 iadst, iadst

function idct16_1d_4x16_pass1_quarter_neon
        mov             x14, x30

        movi            v4.4s, #0
.irp i, 16, 17, 18, 19
        load_clear      \i,  x2,  x9
.endr

        bl              idct16_quarter

        // Do four 4x4 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v19, v20-v23, v24-v27 and v28-v31
        // contain the four transposed 4x4 blocks.
        transpose_4x4s  v16, v17, v18, v19, v4, v5, v6, v7
        transpose_4x4s  v20, v21, v22, v23, v4, v5, v6, v7
        transpose_4x4s  v24, v25, v26, v27, v4, v5, v6, v7
        transpose_4x4s  v28, v29, v30, v31, v4, v5, v6, v7

        // Store the transposed 4x4 blocks horizontally.
        // The first 4x4 block is kept in registers for the second pass,
        // store the rest in the temp buffer.
        add             x0,  x0,  #16
        st1             {v20.4s},  [x0], #16
        st1             {v24.4s},  [x0], #16
        st1             {v28.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v21.4s},  [x0], #16
        st1             {v25.4s},  [x0], #16
        st1             {v29.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v22.4s},  [x0], #16
        st1             {v26.4s},  [x0], #16
        st1             {v30.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v23.4s},  [x0], #16
        st1             {v27.4s},  [x0], #16
        st1             {v31.4s},  [x0], #16
        br              x14
endfunc

function idct16_1d_4x16_pass2_quarter_neon
        mov             x14, x30

        // Only load the top 4 lines, and only do it for the later slices.
        // For the first slice, d16-d19 is kept in registers from the first pass.
        cbz             x3,  1f
.irp i, 16, 17, 18, 19
        load            \i,  x2,  x9
.endr
1:

        add             x3,  x0,  x1
        lsl             x1,  x1,  #1
        bl              idct16_quarter

        dup             v8.8h, w13
        load_add_store  v16.4s, v17.4s, v18.4s, v19.4s, v20.4s, v21.4s, v22.4s, v23.4s
        load_add_store  v24.4s, v25.4s, v26.4s, v27.4s, v28.4s, v29.4s, v30.4s, v31.4s

        br              x14
endfunc

function idct16_1d_4x16_pass1_half_neon
        mov             x14, x30

        movi            v4.4s, #0
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load_clear      \i,  x2,  x9
.endr

        bl              idct16_half

        // Do four 4x4 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v19, v20-v23, v24-v27 and v28-v31
        // contain the four transposed 4x4 blocks.
        transpose_4x4s  v16, v17, v18, v19, v4, v5, v6, v7
        transpose_4x4s  v20, v21, v22, v23, v4, v5, v6, v7
        transpose_4x4s  v24, v25, v26, v27, v4, v5, v6, v7
        transpose_4x4s  v28, v29, v30, v31, v4, v5, v6, v7

        // Store the transposed 4x4 blocks horizontally.
        cmp             x1,  #4
        b.eq            1f
.irp i, 16, 20, 24, 28, 17, 21, 25, 29, 18, 22, 26, 30, 19, 23, 27, 31
        store           \i,  x0,  #16
.endr
        br              x14
1:
        // Special case: For the second input column (r1 == 4),
        // which would be stored as the second row in the temp buffer,
        // don't store the first 4x4 block, but keep it in registers
        // for the first slice of the second pass (where it is the
        // second 4x4 block).
        add             x0,  x0,  #16
        st1             {v20.4s},  [x0], #16
        st1             {v24.4s},  [x0], #16
        st1             {v28.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v21.4s},  [x0], #16
        st1             {v25.4s},  [x0], #16
        st1             {v29.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v22.4s},  [x0], #16
        st1             {v26.4s},  [x0], #16
        st1             {v30.4s},  [x0], #16
        add             x0,  x0,  #16
        st1             {v23.4s},  [x0], #16
        st1             {v27.4s},  [x0], #16
        st1             {v31.4s},  [x0], #16

        mov             v20.16b, v16.16b
        mov             v21.16b, v17.16b
        mov             v22.16b, v18.16b
        mov             v23.16b, v19.16b
        br              x14
endfunc

function idct16_1d_4x16_pass2_half_neon
        mov             x14, x30

.irp i, 16, 17, 18, 19
        load            \i,  x2,  x9
.endr
        cbz             x3,  1f
.irp i, 20, 21, 22, 23
        load            \i,  x2,  x9
.endr
1:

        add             x3,  x0,  x1
        lsl             x1,  x1,  #1
        bl              idct16_half

        dup             v8.8h, w13
        load_add_store  v16.4s, v17.4s, v18.4s, v19.4s, v20.4s, v21.4s, v22.4s, v23.4s
        load_add_store  v24.4s, v25.4s, v26.4s, v27.4s, v28.4s, v29.4s, v30.4s, v31.4s

        br              x14
endfunc

.macro idct16_partial size
function idct16x16_\size\()_add_16_neon
        add             x0,  sp,  #(0*64)
        mov             x1,  #0
        add             x2,  x6,  #(0*4)
        bl              idct16_1d_4x16_pass1_\size\()_neon
.ifc \size,half
        add             x0,  sp,  #(4*64)
        mov             x1,  #4
        add             x2,  x6,  #(4*4)
        bl              idct16_1d_4x16_pass1_\size\()_neon
.endif

.irp i, 0, 4, 8, 12
        add             x0,  x4,  #(\i*2)
        mov             x1,  x5
        add             x2,  sp,  #(\i*4)
        mov             x3,  #\i
        bl              idct16_1d_4x16_pass2_\size\()_neon
.endr

        add             sp,  sp,  #1024
        ldp             d8,  d9,  [sp], 0x10
        br              x15
endfunc
.endm

idct16_partial quarter
idct16_partial half

function idct32x32_dc_add_neon
        movrel          x4,  idct_coeffs
        ld1             {v0.4h}, [x4]
        sxtl            v0.4s,  v0.4h

        movi            v1.4h,  #0

        ld1             {v2.s}[0],  [x2]
        smull           v2.2d,  v2.2s,  v0.s[0]
        rshrn           v2.2s,  v2.2d,  #14
        smull           v2.2d,  v2.2s,  v0.s[0]
        rshrn           v2.2s,  v2.2d,  #14
        st1             {v1.s}[0],  [x2]
        dup             v2.4s,  v2.s[0]

        srshr           v0.4s,  v2.4s,  #6

        mov             x3,  x0
        mov             x4,  #32
        sub             x1,  x1,  #32
        dup             v31.8h, w13
1:
        // Loop to add the constant v0 into all 32x32 outputs
        subs            x4,  x4,  #1
        ld1             {v1.8h,v2.8h},  [x0], #32
        uaddw           v16.4s, v0.4s,  v1.4h
        uaddw2          v17.4s, v0.4s,  v1.8h
        ld1             {v3.8h,v4.8h},  [x0], x1
        uaddw           v18.4s, v0.4s,  v2.4h
        uaddw2          v19.4s, v0.4s,  v2.8h
        uaddw           v20.4s, v0.4s,  v3.4h
        uaddw2          v21.4s, v0.4s,  v3.8h
        uaddw           v22.4s, v0.4s,  v4.4h
        uaddw2          v23.4s, v0.4s,  v4.8h
        sqxtun          v1.4h,  v16.4s
        sqxtun2         v1.8h,  v17.4s
        sqxtun          v2.4h,  v18.4s
        sqxtun2         v2.8h,  v19.4s
        sqxtun          v3.4h,  v20.4s
        sqxtun2         v3.8h,  v21.4s
        sqxtun          v4.4h,  v22.4s
        sqxtun2         v4.8h,  v23.4s
        umin            v1.8h,  v1.8h,  v31.8h
        umin            v2.8h,  v2.8h,  v31.8h
        st1             {v1.8h,v2.8h},  [x3], #32
        umin            v3.8h,  v3.8h,  v31.8h
        umin            v4.8h,  v4.8h,  v31.8h
        st1             {v3.8h,v4.8h},  [x3], x1
        b.ne            1b

        ret
endfunc

.macro idct32_end
        butterfly_4s    v16, v5,  v4,  v5  // v16 = t16a, v5  = t19a
        butterfly_4s    v17, v20, v23, v20 // v17 = t17,  v20 = t18
        butterfly_4s    v18, v6,  v7,  v6  // v18 = t23a, v6  = t20a
        butterfly_4s    v19, v21, v22, v21 // v19 = t22,  v21 = t21
        butterfly_4s    v4,  v28, v28, v30 // v4  = t24a, v28 = t27a
        butterfly_4s    v23, v26, v25, v26 // v23 = t25,  v26 = t26
        butterfly_4s    v7,  v8,  v29, v31 // v7  = t31a, v3  = t28a
        butterfly_4s    v22, v27, v24, v27 // v22 = t30,  v27 = t29

        dmbutterfly     v27, v20, v0.s[2], v0.s[3], v24, v25, v30, v31        // v27 = t18a, v20 = t29a
        dmbutterfly     v8,  v5,  v0.s[2], v0.s[3], v24, v25, v30, v31        // v3  = t19,  v5  = t28
        dmbutterfly     v28, v6,  v0.s[2], v0.s[3], v24, v25, v30, v31, neg=1 // v28 = t27,  v6  = t20
        dmbutterfly     v26, v21, v0.s[2], v0.s[3], v24, v25, v30, v31, neg=1 // v26 = t26a, v21 = t21a

        butterfly_4s    v31, v24, v7,  v4  // v31 = t31,  v24 = t24
        butterfly_4s    v30, v25, v22, v23 // v30 = t30a, v25 = t25a
        butterfly_4s_r  v23, v16, v16, v18 // v23 = t23,  v16 = t16
        butterfly_4s_r  v22, v17, v17, v19 // v22 = t22a, v17 = t17a
        butterfly_4s    v18, v21, v27, v21 // v18 = t18,  v21 = t21
        butterfly_4s_r  v27, v28, v5,  v28 // v27 = t27a, v28 = t28a
        butterfly_4s    v29, v26, v20, v26 // v29 = t29,  v26 = t26
        butterfly_4s    v19, v20, v8,  v6  // v19 = t19a, v20 = t20

        dmbutterfly0    v27, v20, v27, v20, v4, v5, v6, v7, v8, v9 // v27 = t27,  v20 = t20
        dmbutterfly0    v26, v21, v26, v21, v4, v5, v6, v7, v8, v9 // v26 = t26a, v21 = t21a
        dmbutterfly0    v25, v22, v25, v22, v4, v5, v6, v7, v8, v9 // v25 = t25,  v22 = t22
        dmbutterfly0    v24, v23, v24, v23, v4, v5, v6, v7, v8, v9 // v24 = t24a, v23 = t23a
        ret
.endm

function idct32_odd
        dmbutterfly     v16, v31, v10.s[0], v10.s[1], v4, v5, v6, v7 // v16 = t16a, v31 = t31a
        dmbutterfly     v24, v23, v10.s[2], v10.s[3], v4, v5, v6, v7 // v24 = t17a, v23 = t30a
        dmbutterfly     v20, v27, v11.s[0], v11.s[1], v4, v5, v6, v7 // v20 = t18a, v27 = t29a
        dmbutterfly     v28, v19, v11.s[2], v11.s[3], v4, v5, v6, v7 // v28 = t19a, v19 = t28a
        dmbutterfly     v18, v29, v12.s[0], v12.s[1], v4, v5, v6, v7 // v18 = t20a, v29 = t27a
        dmbutterfly     v26, v21, v12.s[2], v12.s[3], v4, v5, v6, v7 // v26 = t21a, v21 = t26a
        dmbutterfly     v22, v25, v13.s[0], v13.s[1], v4, v5, v6, v7 // v22 = t22a, v25 = t25a
        dmbutterfly     v30, v17, v13.s[2], v13.s[3], v4, v5, v6, v7 // v30 = t23a, v17 = t24a

        butterfly_4s    v4,  v24, v16, v24 // v4  = t16, v24 = t17
        butterfly_4s    v5,  v20, v28, v20 // v5  = t19, v20 = t18
        butterfly_4s    v6,  v26, v18, v26 // v6  = t20, v26 = t21
        butterfly_4s    v7,  v22, v30, v22 // v7  = t23, v22 = t22
        butterfly_4s    v28, v25, v17, v25 // v28 = t24, v25 = t25
        butterfly_4s    v30, v21, v29, v21 // v30 = t27, v21 = t26
        butterfly_4s    v29, v23, v31, v23 // v29 = t31, v23 = t30
        butterfly_4s    v31, v27, v19, v27 // v31 = t28, v27 = t29

        dmbutterfly     v23, v24, v1.s[0], v1.s[1], v16, v17, v18, v19        // v23 = t17a, v24 = t30a
        dmbutterfly     v27, v20, v1.s[0], v1.s[1], v16, v17, v18, v19, neg=1 // v27 = t29a, v20 = t18a
        dmbutterfly     v21, v26, v1.s[2], v1.s[3], v16, v17, v18, v19        // v21 = t21a, v26 = t26a
        dmbutterfly     v25, v22, v1.s[2], v1.s[3], v16, v17, v18, v19, neg=1 // v25 = t25a, v22 = t22a
        idct32_end
endfunc

function idct32_odd_half
        dmbutterfly_h1  v16, v31, v10.s[0], v10.s[1], v4, v5, v6, v7 // v16 = t16a, v31 = t31a
        dmbutterfly_h2  v24, v23, v10.s[2], v10.s[3], v4, v5, v6, v7 // v24 = t17a, v23 = t30a
        dmbutterfly_h1  v20, v27, v11.s[0], v11.s[1], v4, v5, v6, v7 // v20 = t18a, v27 = t29a
        dmbutterfly_h2  v28, v19, v11.s[2], v11.s[3], v4, v5, v6, v7 // v28 = t19a, v19 = t28a
        dmbutterfly_h1  v18, v29, v12.s[0], v12.s[1], v4, v5, v6, v7 // v18 = t20a, v29 = t27a
        dmbutterfly_h2  v26, v21, v12.s[2], v12.s[3], v4, v5, v6, v7 // v26 = t21a, v21 = t26a
        dmbutterfly_h1  v22, v25, v13.s[0], v13.s[1], v4, v5, v6, v7 // v22 = t22a, v25 = t25a
        dmbutterfly_h2  v30, v17, v13.s[2], v13.s[3], v4, v5, v6, v7 // v30 = t23a, v17 = t24a

        butterfly_4s    v4,  v24, v16, v24 // v4  = t16, v24 = t17
        butterfly_4s    v5,  v20, v28, v20 // v5  = t19, v20 = t18
        butterfly_4s    v6,  v26, v18, v26 // v6  = t20, v26 = t21
        butterfly_4s    v7,  v22, v30, v22 // v7  = t23, v22 = t22
        butterfly_4s    v28, v25, v17, v25 // v28 = t24, v25 = t25
        butterfly_4s    v30, v21, v29, v21 // v30 = t27, v21 = t26
        butterfly_4s    v29, v23, v31, v23 // v29 = t31, v23 = t30
        butterfly_4s    v31, v27, v19, v27 // v31 = t28, v27 = t29

        dmbutterfly     v23, v24, v1.s[0], v1.s[1], v16, v17, v18, v19        // v23 = t17a, v24 = t30a
        dmbutterfly     v27, v20, v1.s[0], v1.s[1], v16, v17, v18, v19, neg=1 // v27 = t29a, v20 = t18a
        dmbutterfly     v21, v26, v1.s[2], v1.s[3], v16, v17, v18, v19        // v21 = t21a, v26 = t26a
        dmbutterfly     v25, v22, v1.s[2], v1.s[3], v16, v17, v18, v19, neg=1 // v25 = t25a, v22 = t22a
        idct32_end
endfunc

function idct32_odd_quarter
        dsmull_h        v4,  v5,  v16, v10.s[0]
        dsmull_h        v28, v29, v19, v11.s[3]
        dsmull_h        v30, v31, v16, v10.s[1]
        dsmull_h        v22, v23, v17, v13.s[2]
        dsmull_h        v7,  v6,  v17, v13.s[3]
        dsmull_h        v26, v27, v19, v11.s[2]
        dsmull_h        v20, v21, v18, v12.s[0]
        dsmull_h        v24, v25, v18, v12.s[1]

        neg             v28.2d, v28.2d
        neg             v29.2d, v29.2d
        neg             v7.2d,  v7.2d
        neg             v6.2d,  v6.2d

        drshrn_h        v4,  v4,  v5,  #14
        drshrn_h        v5,  v28, v29, #14
        drshrn_h        v29, v30, v31, #14
        drshrn_h        v28, v22, v23, #14
        drshrn_h        v7,  v7,  v6,  #14
        drshrn_h        v31, v26, v27, #14
        drshrn_h        v6,  v20, v21, #14
        drshrn_h        v30, v24, v25, #14

        dmbutterfly_l   v16, v17, v18, v19, v29, v4,  v1.s[0], v1.s[1]
        dmbutterfly_l   v27, v26, v20, v21, v31, v5,  v1.s[0], v1.s[1]
        drshrn_h        v23, v16, v17, #14
        drshrn_h        v24, v18, v19, #14
        neg             v20.2d, v20.2d
        neg             v21.2d, v21.2d
        drshrn_h        v27, v27, v26, #14
        drshrn_h        v20, v20, v21, #14
        dmbutterfly_l   v16, v17, v18, v19, v30, v6,  v1.s[2], v1.s[3]
        drshrn_h        v21, v16, v17, #14
        drshrn_h        v26, v18, v19, #14
        dmbutterfly_l   v16, v17, v18, v19, v28, v7,  v1.s[2], v1.s[3]
        drshrn_h        v25, v16, v17, #14
        neg             v18.2d, v18.2d
        neg             v19.2d, v19.2d
        drshrn_h        v22, v18, v19, #14

        idct32_end
endfunc

.macro idct32_funcs suffix
// Do an 32-point IDCT of a 4x32 slice out of a 32x32 matrix.
// The 32-point IDCT can be decomposed into two 16-point IDCTs;
// a normal IDCT16 with every other input component (the even ones, with
// each output written twice), followed by a separate 16-point IDCT
// of the odd inputs, added/subtracted onto the outputs of the first idct16.
// x0 = dst (temp buffer)
// x1 = unused
// x2 = src
// x9 = double input stride
function idct32_1d_4x32_pass1\suffix\()_neon
        mov             x14, x30

        movi            v4.4s,  #0

        // v16 = IN(0), v17 = IN(2) ... v31 = IN(30)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        load_clear      \i, x2, x9
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        load_clear      \i, x2, x9
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load_clear      \i, x2, x9
.endr
.endif

        bl              idct16\suffix

        // Do four 4x4 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v19, v20-v23, v24-v27 and v28-v31
        // contain the four transposed 4x4 blocks.
        transpose_4x4s  v16, v17, v18, v19, v4, v5, v6, v7
        transpose_4x4s  v20, v21, v22, v23, v4, v5, v6, v7
        transpose_4x4s  v24, v25, v26, v27, v4, v5, v6, v7
        transpose_4x4s  v28, v29, v30, v31, v4, v5, v6, v7

        // Store the registers a, b, c, d horizontally, followed by the
        // same registers d, c, b, a mirrored.
.macro store_rev a, b, c, d
        // There's no rev128 instruction, but we reverse each 64 bit
        // half, and then flip them using an ext with 8 bytes offset.
        rev64           v7.4s, \d
        st1             {\a},  [x0], #16
        ext             v7.16b, v7.16b, v7.16b, #8
        st1             {\b},  [x0], #16
        rev64           v6.4s, \c
        st1             {\c},  [x0], #16
        ext             v6.16b, v6.16b, v6.16b, #8
        st1             {\d},  [x0], #16
        rev64           v5.4s, \b
        st1             {v7.4s},  [x0], #16
        ext             v5.16b, v5.16b, v5.16b, #8
        st1             {v6.4s},  [x0], #16
        rev64           v4.4s, \a
        st1             {v5.4s},  [x0], #16
        ext             v4.16b, v4.16b, v4.16b, #8
        st1             {v4.4s},  [x0], #16
.endm
        store_rev       v16.4s, v20.4s, v24.4s, v28.4s
        store_rev       v17.4s, v21.4s, v25.4s, v29.4s
        store_rev       v18.4s, v22.4s, v26.4s, v30.4s
        store_rev       v19.4s, v23.4s, v27.4s, v31.4s
        sub             x0,  x0,  #512
.purgem store_rev

        // Move x2 back to the start of the input, and move
        // to the first odd row
.ifb \suffix
        sub             x2,  x2,  x9, lsl #4
.endif
.ifc \suffix,_quarter
        sub             x2,  x2,  x9, lsl #2
.endif
.ifc \suffix,_half
        sub             x2,  x2,  x9, lsl #3
.endif
        add             x2,  x2,  #128

        movi            v4.4s,  #0
        // v16 = IN(1), v17 = IN(3) ... v31 = IN(31)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        load_clear      \i, x2, x9
.endr
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        load_clear      \i, x2, x9
.endr
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load_clear      \i, x2, x9
.endr
.endif

        bl              idct32_odd\suffix

        transpose_4x4s  v31, v30, v29, v28, v4, v5, v6, v7
        transpose_4x4s  v27, v26, v25, v24, v4, v5, v6, v7
        transpose_4x4s  v23, v22, v21, v20, v4, v5, v6, v7
        transpose_4x4s  v19, v18, v17, v16, v4, v5, v6, v7

        // Store the registers a, b, c, d horizontally,
        // adding into the output first, and the mirrored,
        // subtracted from the output.
.macro store_rev a, b, c, d, a16b, b16b
        ld1             {v4.4s},  [x0]
        rev64           v9.4s, \d
        add             v4.4s, v4.4s, \a
        st1             {v4.4s},  [x0], #16
        rev64           v8.4s, \c
        ld1             {v4.4s},  [x0]
        ext             v9.16b, v9.16b, v9.16b, #8
        add             v4.4s, v4.4s, \b
        st1             {v4.4s},  [x0], #16
        ext             v8.16b, v8.16b, v8.16b, #8
        ld1             {v4.4s},  [x0]
        rev64           \b, \b
        add             v4.4s, v4.4s, \c
        st1             {v4.4s},  [x0], #16
        rev64           \a, \a
        ld1             {v4.4s},  [x0]
        ext             \b16b, \b16b, \b16b, #8
        add             v4.4s, v4.4s, \d
        st1             {v4.4s},  [x0], #16
        ext             \a16b, \a16b, \a16b, #8
        ld1             {v4.4s},  [x0]
        sub             v4.4s, v4.4s, v9.4s
        st1             {v4.4s},  [x0], #16
        ld1             {v4.4s},  [x0]
        sub             v4.4s, v4.4s, v8.4s
        st1             {v4.4s},  [x0], #16
        ld1             {v4.4s},  [x0]
        sub             v4.4s, v4.4s, \b
        st1             {v4.4s},  [x0], #16
        ld1             {v4.4s},  [x0]
        sub             v4.4s, v4.4s, \a
        st1             {v4.4s},  [x0], #16
.endm

        store_rev       v31.4s, v27.4s, v23.4s, v19.4s, v31.16b, v27.16b
        store_rev       v30.4s, v26.4s, v22.4s, v18.4s, v30.16b, v26.16b
        store_rev       v29.4s, v25.4s, v21.4s, v17.4s, v29.16b, v25.16b
        store_rev       v28.4s, v24.4s, v20.4s, v16.4s, v28.16b, v24.16b
.purgem store_rev
        br              x14
endfunc

// This is mostly the same as 4x32_pass1, but without the transpose,
// and use the source as temp buffer between the two idct passes, and
// add into the destination.
// x0 = dst
// x1 = dst stride
// x2 = src (temp buffer)
// x7 = negative double temp buffer stride
// x9 = double temp buffer stride
function idct32_1d_4x32_pass2\suffix\()_neon
        mov             x14, x30

        // v16 = IN(0), v17 = IN(2) ... v31 = IN(30)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        load            \i, x2, x9
.endr
        sub             x2,  x2,  x9, lsl #4
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        load            \i, x2, x9
.endr
        sub             x2,  x2,  x9, lsl #2
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load            \i, x2, x9
.endr
        sub             x2,  x2,  x9, lsl #3
.endif

        bl              idct16\suffix

.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        store           \i, x2, x9
.endr

        sub             x2,  x2,  x9, lsl #4
        add             x2,  x2,  #128

        // v16 = IN(1), v17 = IN(3) ... v31 = IN(31)
.ifb \suffix
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        load            \i, x2, x9
.endr
        sub             x2,  x2,  x9, lsl #4
.endif
.ifc \suffix,_quarter
.irp i, 16, 17, 18, 19
        load            \i, x2, x9
.endr
        sub             x2,  x2,  x9, lsl #2
.endif
.ifc \suffix,_half
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load            \i, x2, x9
.endr
        sub             x2,  x2,  x9, lsl #3
.endif
        sub             x2,  x2,  #128

        bl              idct32_odd\suffix

.macro load_acc_store a, b, c, d, neg=0
.if \neg == 0
        ld1             {v4.4s},  [x2], x9
        ld1             {v5.4s},  [x2], x9
        add             v4.4s, v4.4s, \a
        ld1             {v6.4s},  [x2], x9
        add             v5.4s, v5.4s, \b
        ld1             {v7.4s},  [x2], x9
        add             v6.4s, v6.4s, \c
        add             v7.4s, v7.4s, \d
.else
        ld1             {v4.4s},  [x2], x7
        ld1             {v5.4s},  [x2], x7
        sub             v4.4s, v4.4s, \a
        ld1             {v6.4s},  [x2], x7
        sub             v5.4s, v5.4s, \b
        ld1             {v7.4s},  [x2], x7
        sub             v6.4s, v6.4s, \c
        sub             v7.4s, v7.4s, \d
.endif
        ld1             {v8.4h},   [x0], x1
        ld1             {v8.d}[1], [x0], x1
        srshr           v4.4s, v4.4s, #6
        ld1             {v9.4h},   [x0], x1
        srshr           v5.4s, v5.4s, #6
        uaddw           v4.4s, v4.4s, v8.4h
        ld1             {v9.d}[1], [x0], x1
        srshr           v6.4s, v6.4s, #6
        uaddw2          v5.4s, v5.4s, v8.8h
        srshr           v7.4s, v7.4s, #6
        sub             x0,  x0,  x1, lsl #2
        uaddw           v6.4s, v6.4s, v9.4h
        sqxtun          v4.4h, v4.4s
        uaddw2          v7.4s, v7.4s, v9.8h
        sqxtun2         v4.8h, v5.4s
        umin            v4.8h, v4.8h, v15.8h
        st1             {v4.4h},   [x0], x1
        sqxtun          v5.4h, v6.4s
        st1             {v4.d}[1], [x0], x1
        sqxtun2         v5.8h, v7.4s
        umin            v5.8h, v5.8h, v15.8h
        st1             {v5.4h},   [x0], x1
        st1             {v5.d}[1], [x0], x1
.endm
        load_acc_store  v31.4s, v30.4s, v29.4s, v28.4s
        load_acc_store  v27.4s, v26.4s, v25.4s, v24.4s
        load_acc_store  v23.4s, v22.4s, v21.4s, v20.4s
        load_acc_store  v19.4s, v18.4s, v17.4s, v16.4s
        sub             x2,  x2,  x9
        load_acc_store  v16.4s, v17.4s, v18.4s, v19.4s, 1
        load_acc_store  v20.4s, v21.4s, v22.4s, v23.4s, 1
        load_acc_store  v24.4s, v25.4s, v26.4s, v27.4s, 1
        load_acc_store  v28.4s, v29.4s, v30.4s, v31.4s, 1
.purgem load_acc_store
        br              x14
endfunc
.endm

idct32_funcs
idct32_funcs _quarter
idct32_funcs _half

const min_eob_idct_idct_32, align=4
        .short  0, 9, 34, 70, 135, 240, 336, 448
endconst

function vp9_idct_idct_32x32_add_16_neon
        cmp             w3,  #1
        b.eq            idct32x32_dc_add_neon

        movrel          x10, idct_coeffs

        mov             x15, x30
        stp             d8,  d9,  [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d14, d15, [sp, #-0x10]!

        sub             sp,  sp,  #4096

        mov             x4,  x0
        mov             x5,  x1
        mov             x6,  x2

        // Double stride of the input, since we only read every other line
        mov             x9,  #256
        neg             x7,  x9

        ld1             {v0.8h,v1.8h},   [x10], #32
        sxtl            v2.4s,  v1.4h
        sxtl2           v3.4s,  v1.8h
        sxtl2           v1.4s,  v0.8h
        sxtl            v0.4s,  v0.4h
        ld1             {v10.8h,v11.8h}, [x10]
        sxtl            v12.4s, v11.4h
        sxtl2           v13.4s, v11.8h
        sxtl2           v11.4s, v10.8h
        sxtl            v10.4s, v10.4h

        dup             v15.8h, w13

        cmp             w3,  #34
        b.le            idct32x32_quarter_add_16_neon
        cmp             w3,  #135
        b.le            idct32x32_half_add_16_neon

        movrel          x12, min_eob_idct_idct_32, 2

.irp i, 0, 4, 8, 12, 16, 20, 24, 28
        add             x0,  sp,  #(\i*128)
.if \i > 0
        ldrh            w1,  [x12], #2
        cmp             w3,  w1
        mov             x1,  #(32 - \i)/4
        b.le            1f
.endif
        add             x2,  x6,  #(\i*4)
        bl              idct32_1d_4x32_pass1_neon
.endr
        b               3f

1:
        // Write zeros to the temp buffer for pass 2
        movi            v16.4s,  #0
        movi            v17.4s,  #0
        movi            v18.4s,  #0
        movi            v19.4s,  #0
2:
        subs            x1,  x1,  #1
.rept 4
        st1             {v16.4s-v19.4s},  [x0], #64
        st1             {v16.4s-v19.4s},  [x0], #64
.endr
        b.ne            2b
3:
.irp i, 0, 4, 8, 12, 16, 20, 24, 28
        add             x0,  x4,  #(\i*2)
        mov             x1,  x5
        add             x2,  sp,  #(\i*4)
        bl              idct32_1d_4x32_pass2_neon
.endr

        add             sp,  sp,  #4096
        ldp             d14, d15, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d8,  d9,  [sp], 0x10

        br              x15
endfunc

function ff_vp9_idct_idct_32x32_add_10_neon, export=1
        mov             x13, #0x03ff
        b               vp9_idct_idct_32x32_add_16_neon
endfunc

function ff_vp9_idct_idct_32x32_add_12_neon, export=1
        mov             x13, #0x0fff
        b               vp9_idct_idct_32x32_add_16_neon
endfunc

.macro idct32_partial size
function idct32x32_\size\()_add_16_neon
.irp i, 0, 4
        add             x0,  sp,  #(\i*128)
.ifc \size,quarter
.if \i == 4
        cmp             w3,  #9
        b.le            1f
.endif
.endif
        add             x2,  x6,  #(\i*4)
        bl              idct32_1d_4x32_pass1_\size\()_neon
.endr

.ifc \size,half
.irp i, 8, 12
        add             x0,  sp,  #(\i*128)
.if \i == 12
        cmp             w3,  #70
        b.le            1f
.endif
        add             x2,  x6,  #(\i*4)
        bl              idct32_1d_4x32_pass1_\size\()_neon
.endr
.endif
        b               3f

1:
        // Write zeros to the temp buffer for pass 2
        movi            v16.4s,  #0
        movi            v17.4s,  #0
        movi            v18.4s,  #0
        movi            v19.4s,  #0

.rept 4
        st1             {v16.4s-v19.4s},  [x0], #64
        st1             {v16.4s-v19.4s},  [x0], #64
.endr

3:
.irp i, 0, 4, 8, 12, 16, 20, 24, 28
        add             x0,  x4,  #(\i*2)
        mov             x1,  x5
        add             x2,  sp,  #(\i*4)
        bl              idct32_1d_4x32_pass2_\size\()_neon
.endr

        add             sp,  sp,  #4096
        ldp             d14, d15, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d8,  d9,  [sp], 0x10

        br              x15
endfunc
.endm

idct32_partial quarter
idct32_partial half
