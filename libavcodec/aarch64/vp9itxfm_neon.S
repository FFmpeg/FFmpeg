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

// out1 = ((in1 + in2) * v0[0] + (1 << 13)) >> 14
// out2 = ((in1 - in2) * v0[0] + (1 << 13)) >> 14
// in/out are .8h registers; this can do with 4 temp registers, but is
// more efficient if 6 temp registers are available.
.macro dmbutterfly0 out1, out2, in1, in2, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, neg=0
.if \neg > 0
        neg             \tmp4\().4h, v0.4h
.endif
        add             \tmp1\().8h, \in1\().8h,  \in2\().8h
        sub             \tmp2\().8h, \in1\().8h,  \in2\().8h
.if \neg > 0
        smull           \tmp3\().4s, \tmp1\().4h, \tmp4\().h[0]
        smull2          \tmp4\().4s, \tmp1\().8h, \tmp4\().h[0]
.else
        smull           \tmp3\().4s, \tmp1\().4h, v0.h[0]
        smull2          \tmp4\().4s, \tmp1\().8h, v0.h[0]
.endif
.ifb \tmp5
        rshrn           \out1\().4h, \tmp3\().4s, #14
        rshrn2          \out1\().8h, \tmp4\().4s, #14
        smull           \tmp3\().4s, \tmp2\().4h, v0.h[0]
        smull2          \tmp4\().4s, \tmp2\().8h, v0.h[0]
        rshrn           \out2\().4h, \tmp3\().4s, #14
        rshrn2          \out2\().8h, \tmp4\().4s, #14
.else
        smull           \tmp5\().4s, \tmp2\().4h, v0.h[0]
        smull2          \tmp6\().4s, \tmp2\().8h, v0.h[0]
        rshrn           \out1\().4h, \tmp3\().4s, #14
        rshrn2          \out1\().8h, \tmp4\().4s, #14
        rshrn           \out2\().4h, \tmp5\().4s, #14
        rshrn2          \out2\().8h, \tmp6\().4s, #14
.endif
.endm

// Same as dmbutterfly0 above, but treating the input in in2 as zero,
// writing the same output into both out1 and out2.
.macro dmbutterfly0_h out1, out2, in1, in2, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6
        smull           \tmp1\().4s,  \in1\().4h,  v0.h[0]
        smull2          \tmp2\().4s,  \in1\().8h,  v0.h[0]
        rshrn           \out1\().4h,  \tmp1\().4s, #14
        rshrn2          \out1\().8h,  \tmp2\().4s, #14
        rshrn           \out2\().4h,  \tmp1\().4s, #14
        rshrn2          \out2\().8h,  \tmp2\().4s, #14
.endm

// out1,out2 = in1 * coef1 - in2 * coef2
// out3,out4 = in1 * coef2 + in2 * coef1
// out are 4 x .4s registers, in are 2 x .8h registers
.macro dmbutterfly_l out1, out2, out3, out4, in1, in2, coef1, coef2
        smull           \out1\().4s, \in1\().4h, \coef1
        smull2          \out2\().4s, \in1\().8h, \coef1
        smull           \out3\().4s, \in1\().4h, \coef2
        smull2          \out4\().4s, \in1\().8h, \coef2
        smlsl           \out1\().4s, \in2\().4h, \coef2
        smlsl2          \out2\().4s, \in2\().8h, \coef2
        smlal           \out3\().4s, \in2\().4h, \coef1
        smlal2          \out4\().4s, \in2\().8h, \coef1
.endm

// inout1 = (inout1 * coef1 - inout2 * coef2 + (1 << 13)) >> 14
// inout2 = (inout1 * coef2 + inout2 * coef1 + (1 << 13)) >> 14
// inout are 2 x .8h registers
.macro dmbutterfly inout1, inout2, coef1, coef2, tmp1, tmp2, tmp3, tmp4, neg=0
        dmbutterfly_l   \tmp1, \tmp2, \tmp3, \tmp4, \inout1, \inout2, \coef1, \coef2
.if \neg > 0
        neg             \tmp3\().4s, \tmp3\().4s
        neg             \tmp4\().4s, \tmp4\().4s
.endif
        rshrn           \inout1\().4h, \tmp1\().4s,  #14
        rshrn2          \inout1\().8h, \tmp2\().4s,  #14
        rshrn           \inout2\().4h, \tmp3\().4s,  #14
        rshrn2          \inout2\().8h, \tmp4\().4s,  #14
.endm

// Same as dmbutterfly above, but treating the input in inout2 as zero
.macro dmbutterfly_h1 inout1, inout2, coef1, coef2, tmp1, tmp2, tmp3, tmp4
        smull           \tmp1\().4s, \inout1\().4h, \coef1
        smull2          \tmp2\().4s, \inout1\().8h, \coef1
        smull           \tmp3\().4s, \inout1\().4h, \coef2
        smull2          \tmp4\().4s, \inout1\().8h, \coef2
        rshrn           \inout1\().4h, \tmp1\().4s, #14
        rshrn2          \inout1\().8h, \tmp2\().4s, #14
        rshrn           \inout2\().4h, \tmp3\().4s, #14
        rshrn2          \inout2\().8h, \tmp4\().4s, #14
.endm

// Same as dmbutterfly above, but treating the input in inout1 as zero
.macro dmbutterfly_h2 inout1, inout2, coef1, coef2, tmp1, tmp2, tmp3, tmp4
        smull           \tmp1\().4s, \inout2\().4h, \coef2
        smull2          \tmp2\().4s, \inout2\().8h, \coef2
        smull           \tmp3\().4s, \inout2\().4h, \coef1
        smull2          \tmp4\().4s, \inout2\().8h, \coef1
        neg             \tmp1\().4s, \tmp1\().4s
        neg             \tmp2\().4s, \tmp2\().4s
        rshrn           \inout2\().4h, \tmp3\().4s, #14
        rshrn2          \inout2\().8h, \tmp4\().4s, #14
        rshrn           \inout1\().4h, \tmp1\().4s, #14
        rshrn2          \inout1\().8h, \tmp2\().4s, #14
.endm

.macro dsmull_h out1, out2, in, coef
        smull           \out1\().4s, \in\().4h, \coef
        smull2          \out2\().4s, \in\().8h, \coef
.endm

.macro drshrn_h out, in1, in2, shift
        rshrn           \out\().4h, \in1\().4s, \shift
        rshrn2          \out\().8h, \in2\().4s, \shift
.endm


// out1 = in1 + in2
// out2 = in1 - in2
.macro butterfly_8h out1, out2, in1, in2
        add             \out1\().8h, \in1\().8h, \in2\().8h
        sub             \out2\().8h, \in1\().8h, \in2\().8h
.endm

// out1 = in1 - in2
// out2 = in1 + in2
.macro butterfly_8h_r out1, out2, in1, in2
        sub             \out1\().8h, \in1\().8h, \in2\().8h
        add             \out2\().8h, \in1\().8h, \in2\().8h
.endm

// out1 = (in1,in2 + in3,in4 + (1 << 13)) >> 14
// out2 = (in1,in2 - in3,in4 + (1 << 13)) >> 14
// out are 2 x .8h registers, in are 4 x .4s registers
.macro dbutterfly_n out1, out2, in1, in2, in3, in4, tmp1, tmp2, tmp3, tmp4
        add             \tmp1\().4s, \in1\().4s, \in3\().4s
        add             \tmp2\().4s, \in2\().4s, \in4\().4s
        sub             \tmp3\().4s, \in1\().4s, \in3\().4s
        sub             \tmp4\().4s, \in2\().4s, \in4\().4s
        rshrn           \out1\().4h, \tmp1\().4s,  #14
        rshrn2          \out1\().8h, \tmp2\().4s,  #14
        rshrn           \out2\().4h, \tmp3\().4s,  #14
        rshrn2          \out2\().8h, \tmp4\().4s,  #14
.endm

.macro iwht4 c0, c1, c2, c3
        add             \c0\().4h, \c0\().4h, \c1\().4h
        sub             v17.4h,    \c2\().4h, \c3\().4h
        sub             v16.4h,    \c0\().4h, v17.4h
        sshr            v16.4h,    v16.4h,    #1
        sub             \c2\().4h, v16.4h,    \c1\().4h
        sub             \c1\().4h, v16.4h,    \c3\().4h
        add             \c3\().4h, v17.4h,    \c2\().4h
        sub             \c0\().4h, \c0\().4h, \c1\().4h
.endm

.macro idct4 c0, c1, c2, c3
        smull           v22.4s,    \c1\().4h, v0.h[3]
        smull           v20.4s,    \c1\().4h, v0.h[2]
        add             v16.4h,    \c0\().4h, \c2\().4h
        sub             v17.4h,    \c0\().4h, \c2\().4h
        smlal           v22.4s,    \c3\().4h, v0.h[2]
        smull           v18.4s,    v16.4h,    v0.h[0]
        smull           v19.4s,    v17.4h,    v0.h[0]
        smlsl           v20.4s,    \c3\().4h, v0.h[3]
        rshrn           v22.4h,    v22.4s,    #14
        rshrn           v18.4h,    v18.4s,    #14
        rshrn           v19.4h,    v19.4s,    #14
        rshrn           v20.4h,    v20.4s,    #14
        add             \c0\().4h, v18.4h,    v22.4h
        sub             \c3\().4h, v18.4h,    v22.4h
        add             \c1\().4h, v19.4h,    v20.4h
        sub             \c2\().4h, v19.4h,    v20.4h
.endm

.macro iadst4 c0, c1, c2, c3
        smull           v16.4s,    \c0\().4h, v0.h[4]
        smlal           v16.4s,    \c2\().4h, v0.h[5]
        smlal           v16.4s,    \c3\().4h, v0.h[6]
        smull           v17.4s,    \c0\().4h, v0.h[6]
        smlsl           v17.4s,    \c2\().4h, v0.h[4]
        sub             \c0\().4h, \c0\().4h, \c2\().4h
        smlsl           v17.4s,    \c3\().4h, v0.h[5]
        add             \c0\().4h, \c0\().4h, \c3\().4h
        smull           v19.4s,    \c1\().4h, v0.h[7]
        smull           v18.4s,    \c0\().4h, v0.h[7]
        add             v20.4s,    v16.4s,    v19.4s
        add             v21.4s,    v17.4s,    v19.4s
        rshrn           \c0\().4h, v20.4s,    #14
        add             v16.4s,    v16.4s,    v17.4s
        rshrn           \c1\().4h, v21.4s,    #14
        sub             v16.4s,    v16.4s,    v19.4s
        rshrn           \c2\().4h, v18.4s,    #14
        rshrn           \c3\().4h, v16.4s,    #14
.endm

// The public functions in this file have got the following signature:
// void itxfm_add(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);

.macro itxfm_func4x4 txfm1, txfm2
function ff_vp9_\txfm1\()_\txfm2\()_4x4_add_neon, export=1
.ifc \txfm1,\txfm2
.ifc \txfm1,idct
        movrel          x4,  itxfm4_coeffs
        ld1             {v0.4h}, [x4]
.endif
.ifc \txfm1,iadst
        movrel          x4,  iadst4_coeffs
        ld1             {v0.d}[1], [x4]
.endif
.else
        movrel          x4,  itxfm4_coeffs
        ld1             {v0.8h}, [x4]
.endif

        movi            v31.8h, #0
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #1
        b.ne            1f
        // DC-only for idct/idct
        ld1             {v2.h}[0], [x2]
        smull           v2.4s,  v2.4h, v0.h[0]
        rshrn           v2.4h,  v2.4s, #14
        smull           v2.4s,  v2.4h, v0.h[0]
        rshrn           v2.4h,  v2.4s, #14
        st1             {v31.h}[0], [x2]
        dup             v4.4h,  v2.h[0]
        mov             v5.16b, v4.16b
        mov             v6.16b, v4.16b
        mov             v7.16b, v4.16b
        b               2f
.endif

1:
        ld1             {v4.4h,v5.4h,v6.4h,v7.4h},  [x2]
        st1             {v31.8h}, [x2], #16

.ifc \txfm1,iwht
        sshr            v4.4h,  v4.4h,  #2
        sshr            v5.4h,  v5.4h,  #2
        sshr            v6.4h,  v6.4h,  #2
        sshr            v7.4h,  v7.4h,  #2
.endif

        \txfm1\()4      v4,  v5,  v6,  v7

        st1             {v31.8h}, [x2], #16
        // Transpose 4x4 with 16 bit elements
        transpose_4x4H  v4,  v5,  v6,  v7,  v16, v17, v18, v19

        \txfm2\()4      v4,  v5,  v6,  v7
2:
        ld1             {v0.s}[0],   [x0], x1
        ld1             {v1.s}[0],   [x0], x1
.ifnc \txfm1,iwht
        srshr           v4.4h,  v4.4h,  #4
        srshr           v5.4h,  v5.4h,  #4
        srshr           v6.4h,  v6.4h,  #4
        srshr           v7.4h,  v7.4h,  #4
.endif
        uaddw           v4.8h,  v4.8h,  v0.8b
        uaddw           v5.8h,  v5.8h,  v1.8b
        ld1             {v2.s}[0],   [x0], x1
        ld1             {v3.s}[0],   [x0], x1
        sqxtun          v0.8b,  v4.8h
        sqxtun          v1.8b,  v5.8h
        sub             x0,  x0,  x1, lsl #2

        uaddw           v6.8h,  v6.8h,  v2.8b
        uaddw           v7.8h,  v7.8h,  v3.8b
        st1             {v0.s}[0],  [x0], x1
        sqxtun          v2.8b,  v6.8h
        sqxtun          v3.8b,  v7.8h

        st1             {v1.s}[0],  [x0], x1
        st1             {v2.s}[0],  [x0], x1
        st1             {v3.s}[0],  [x0], x1

        ret
endfunc
.endm

itxfm_func4x4 idct,  idct
itxfm_func4x4 iadst, idct
itxfm_func4x4 idct,  iadst
itxfm_func4x4 iadst, iadst
itxfm_func4x4 iwht,  iwht


.macro idct8
        dmbutterfly0    v16, v20, v16, v20, v2, v3, v4, v5, v6, v7 // v16 = t0a, v20 = t1a
        dmbutterfly     v18, v22, v0.h[2], v0.h[3], v2, v3, v4, v5 // v18 = t2a, v22 = t3a
        dmbutterfly     v17, v23, v0.h[4], v0.h[5], v2, v3, v4, v5 // v17 = t4a, v23 = t7a
        dmbutterfly     v21, v19, v0.h[6], v0.h[7], v2, v3, v4, v5 // v21 = t5a, v19 = t6a

        butterfly_8h    v24, v25, v16, v22 // v24 = t0, v25 = t3
        butterfly_8h    v28, v29, v17, v21 // v28 = t4, v29 = t5a
        butterfly_8h    v30, v31, v23, v19 // v30 = t7, v31 = t6a
        butterfly_8h    v26, v27, v20, v18 // v26 = t1, v27 = t2

        dmbutterfly0    v31, v29, v31, v29, v2, v3, v4, v5, v6, v7 // v31 = t6, v29 = t5

        butterfly_8h    v16, v23, v24, v30 // v16 = out[0], v23 = out[7]
        butterfly_8h    v17, v22, v26, v31 // v17 = out[1], v22 = out[6]
        butterfly_8h    v18, v21, v27, v29 // q13 = out[2], q10 = out[5]
        butterfly_8h    v19, v20, v25, v28 // v17 = out[3], q12 = out[4]
.endm

.macro iadst8
        dmbutterfly_l   v24, v25, v26, v27, v23, v16, v1.h[1], v1.h[0]   // v24,v25 = t1a, v26,v27 = t0a
        dmbutterfly_l   v28, v29, v30, v31, v21, v18, v1.h[3], v1.h[2]   // v28,v29 = t3a, v30,v31 = t2a
        dmbutterfly_l   v2,  v3,  v4,  v5,  v19, v20, v1.h[5], v1.h[4]   // v2,v3   = t5a, v4,v5   = t4a
        dmbutterfly_l   v16, v18, v21, v23, v17, v22, v1.h[7], v1.h[6]   // v16,v18 = t7a, v21,v23 = t6a

        dbutterfly_n    v4,  v5,  v26, v27, v4,  v5,  v6,  v7, v26, v27  // v4  = t0, v5  = t4
        dbutterfly_n    v2,  v3,  v24, v25, v2,  v3,  v6,  v7, v26, v27  // v2  = t1, v3  = t5
        dbutterfly_n    v24, v25, v30, v31, v21, v23, v6,  v7, v26, v27  // v24 = t2, v25 = t6
        dbutterfly_n    v30, v31, v28, v29, v16, v18, v6,  v7, v26, v27  // v30 = t3, v31 = t7

        butterfly_8h    v16, v6,  v4, v24 // v16 = out[0],  v6 = t2
        butterfly_8h    v23, v7,  v2, v30 // v23 = -out[7], v7 = t3
        neg             v23.8h,   v23.8h  // v23 = out[7]

        dmbutterfly0    v19, v20, v6, v7, v24, v26, v27, v28, v29, v30   // v19 = -out[3], v20 = out[4]
        neg             v19.8h,   v19.8h  // v19 = out[3]

        dmbutterfly_l   v26, v27, v28, v29, v5,  v3,  v0.h[2], v0.h[3]   // v26,v27 = t5a, v28,v29 = t4a
        dmbutterfly_l   v2,  v3,  v4,  v5,  v31, v25, v0.h[3], v0.h[2]   // v2,v3   = t6a, v4,v5   = t7a

        dbutterfly_n    v17, v30, v28, v29, v2,  v3,  v6,  v7,  v24, v25 // v17 = -out[1], v30 = t6
        dbutterfly_n    v22, v31, v26, v27, v4,  v5,  v6,  v7,  v24, v25 // v22 = out[6],  v31 = t7
        neg             v17.8h,   v17.8h  // v17 = out[1]

        dmbutterfly0    v18, v21, v30, v31, v2,  v3,  v4,  v5,  v6,  v7  // v18 = out[2], v21 = -out[5]
        neg             v21.8h,   v21.8h  // v21 = out[5]
.endm


.macro itxfm_func8x8 txfm1, txfm2
function ff_vp9_\txfm1\()_\txfm2\()_8x8_add_neon, export=1
        // The iadst also uses a few coefficients from
        // idct, so those always need to be loaded.
.ifc \txfm1\()_\txfm2,idct_idct
        movrel          x4,  idct_coeffs
.else
        movrel          x4,  iadst8_coeffs
        ld1             {v1.8h}, [x4], #16
.endif
        ld1             {v0.8h}, [x4]

        movi            v2.8h, #0
        movi            v3.8h, #0
        movi            v4.8h, #0
        movi            v5.8h, #0

.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #1
        b.ne            1f
        // DC-only for idct/idct
        ld1             {v2.h}[0],  [x2]
        smull           v2.4s,  v2.4h, v0.h[0]
        rshrn           v2.4h,  v2.4s, #14
        smull           v2.4s,  v2.4h, v0.h[0]
        rshrn           v2.4h,  v2.4s, #14
        st1             {v3.h}[0],  [x2]
        dup             v16.8h,  v2.h[0]
        mov             v17.16b, v16.16b
        mov             v18.16b, v16.16b
        mov             v19.16b, v16.16b
        mov             v20.16b, v16.16b
        mov             v21.16b, v16.16b
        mov             v22.16b, v16.16b
        mov             v23.16b, v16.16b
        b               2f
.endif
1:
        ld1             {v16.8h,v17.8h,v18.8h,v19.8h},  [x2], #64
        ld1             {v20.8h,v21.8h,v22.8h,v23.8h},  [x2], #64
        sub             x2,  x2,  #128
        st1             {v2.8h,v3.8h,v4.8h,v5.8h},      [x2], #64
        st1             {v2.8h,v3.8h,v4.8h,v5.8h},      [x2], #64

        \txfm1\()8

        // Transpose 8x8 with 16 bit elements
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v24, v25

        \txfm2\()8
2:
        mov             x3,  x0
        // Add into the destination
        ld1             {v0.8b},  [x0], x1
        srshr           v16.8h, v16.8h, #5
        ld1             {v1.8b},  [x0], x1
        srshr           v17.8h, v17.8h, #5
        ld1             {v2.8b},  [x0], x1
        srshr           v18.8h, v18.8h, #5
        uaddw           v16.8h, v16.8h, v0.8b
        ld1             {v3.8b},  [x0], x1
        srshr           v19.8h, v19.8h, #5
        uaddw           v17.8h, v17.8h, v1.8b
        ld1             {v4.8b},  [x0], x1
        srshr           v20.8h, v20.8h, #5
        uaddw           v18.8h, v18.8h, v2.8b
        sqxtun          v0.8b,  v16.8h
        ld1             {v5.8b},  [x0], x1
        srshr           v21.8h, v21.8h, #5
        uaddw           v19.8h, v19.8h, v3.8b
        sqxtun          v1.8b,  v17.8h
        ld1             {v6.8b},  [x0], x1
        srshr           v22.8h, v22.8h, #5
        uaddw           v20.8h, v20.8h, v4.8b
        sqxtun          v2.8b,  v18.8h
        ld1             {v7.8b},  [x0], x1
        srshr           v23.8h, v23.8h, #5
        uaddw           v21.8h, v21.8h, v5.8b
        sqxtun          v3.8b,  v19.8h

        st1             {v0.8b},  [x3], x1
        uaddw           v22.8h, v22.8h, v6.8b
        st1             {v1.8b},  [x3], x1
        sqxtun          v4.8b,  v20.8h
        st1             {v2.8b},  [x3], x1
        uaddw           v23.8h, v23.8h, v7.8b
        st1             {v3.8b},  [x3], x1
        sqxtun          v5.8b,  v21.8h
        st1             {v4.8b},  [x3], x1
        sqxtun          v6.8b,  v22.8h
        st1             {v5.8b},  [x3], x1
        sqxtun          v7.8b,  v23.8h

        st1             {v6.8b},  [x3], x1
        st1             {v7.8b},  [x3], x1

        ret
endfunc
.endm

itxfm_func8x8 idct,  idct
itxfm_func8x8 iadst, idct
itxfm_func8x8 idct,  iadst
itxfm_func8x8 iadst, iadst


function idct16x16_dc_add_neon
        movrel          x4,  idct_coeffs
        ld1             {v0.4h}, [x4]

        movi            v1.4h,  #0

        ld1             {v2.h}[0], [x2]
        smull           v2.4s,  v2.4h,  v0.h[0]
        rshrn           v2.4h,  v2.4s,  #14
        smull           v2.4s,  v2.4h,  v0.h[0]
        rshrn           v2.4h,  v2.4s,  #14
        dup             v2.8h,  v2.h[0]
        st1             {v1.h}[0], [x2]

        srshr           v2.8h,  v2.8h,  #6

        mov             x3,  x0
        mov             x4,  #16
1:
        // Loop to add the constant from v2 into all 16x16 outputs
        subs            x4,  x4,  #2
        ld1             {v3.16b},  [x0], x1
        ld1             {v4.16b},  [x0], x1
        uaddw           v16.8h, v2.8h,  v3.8b
        uaddw2          v17.8h, v2.8h,  v3.16b
        uaddw           v18.8h, v2.8h,  v4.8b
        uaddw2          v19.8h, v2.8h,  v4.16b
        sqxtun          v3.8b,  v16.8h
        sqxtun2         v3.16b, v17.8h
        sqxtun          v4.8b,  v18.8h
        sqxtun2         v4.16b, v19.8h
        st1             {v3.16b},  [x3], x1
        st1             {v4.16b},  [x3], x1
        b.ne            1b

        ret
endfunc

.macro idct16_end
        butterfly_8h    v18, v7,  v4,  v7                // v18 = t0a,  v7  = t7a
        butterfly_8h    v19, v22, v5,  v22               // v19 = t1a,  v22 = t6
        butterfly_8h    v4,  v26, v20, v26               // v4  = t2a,  v26 = t5
        butterfly_8h    v5,  v6,  v28, v6                // v5  = t3a,  v6  = t4
        butterfly_8h    v20, v28, v16, v24               // v20 = t8a,  v28 = t11a
        butterfly_8h    v24, v21, v23, v21               // v24 = t9,   v21 = t10
        butterfly_8h    v23, v27, v25, v27               // v23 = t14,  v27 = t13
        butterfly_8h    v25, v29, v29, v17               // v25 = t15a, v29 = t12a

        dmbutterfly0    v2,  v3,  v27, v21, v2,  v3,  v16, v17, v30, v31 // v2  = t13a, v3  = t10a
        dmbutterfly0    v28, v27, v29, v28, v21, v29, v16, v17, v30, v31 // v28 = t12,  v27 = t11

        butterfly_8h    v16, v31, v18, v25               // v16 = out[0], v31 = out[15]
        butterfly_8h    v17, v30, v19, v23               // v17 = out[1], v30 = out[14]
        butterfly_8h_r  v25, v22, v22, v24               // v25 = out[9], v22 = out[6]
        butterfly_8h    v23, v24, v7,  v20               // v23 = out[7], v24 = out[8]
        butterfly_8h    v18, v29, v4,  v2                // v18 = out[2], v29 = out[13]
        butterfly_8h    v19, v28, v5,  v28               // v19 = out[3], v28 = out[12]
        butterfly_8h    v20, v27, v6,  v27               // v20 = out[4], v27 = out[11]
        butterfly_8h    v21, v26, v26, v3                // v21 = out[5], v26 = out[10]
        ret
.endm

function idct16
        dmbutterfly0    v16, v24, v16, v24, v2, v3, v4, v5, v6, v7 // v16 = t0a,  v24 = t1a
        dmbutterfly     v20, v28, v0.h[2], v0.h[3], v2, v3, v4, v5 // v20 = t2a,  v28 = t3a
        dmbutterfly     v18, v30, v0.h[4], v0.h[5], v2, v3, v4, v5 // v18 = t4a,  v30 = t7a
        dmbutterfly     v26, v22, v0.h[6], v0.h[7], v2, v3, v4, v5 // v26 = t5a,  v22 = t6a
        dmbutterfly     v17, v31, v1.h[0], v1.h[1], v2, v3, v4, v5 // v17 = t8a,  v31 = t15a
        dmbutterfly     v25, v23, v1.h[2], v1.h[3], v2, v3, v4, v5 // v25 = t9a,  v23 = t14a
        dmbutterfly     v21, v27, v1.h[4], v1.h[5], v2, v3, v4, v5 // v21 = t10a, v27 = t13a
        dmbutterfly     v29, v19, v1.h[6], v1.h[7], v2, v3, v4, v5 // v29 = t11a, v19 = t12a

        butterfly_8h    v4,  v28, v16, v28               // v4  = t0,   v28 = t3
        butterfly_8h    v5,  v20, v24, v20               // v5  = t1,   v20 = t2
        butterfly_8h    v6,  v26, v18, v26               // v6  = t4,   v26 = t5
        butterfly_8h    v7,  v22, v30, v22               // v7  = t7,   v22 = t6
        butterfly_8h    v16, v25, v17, v25               // v16 = t8,   v25 = t9
        butterfly_8h    v24, v21, v29, v21               // v24 = t11,  v21 = t10
        butterfly_8h    v17, v27, v19, v27               // v17 = t12,  v27 = t13
        butterfly_8h    v29, v23, v31, v23               // v29 = t15,  v23 = t14

        dmbutterfly0    v22, v26, v22, v26, v2, v3, v18, v19, v30, v31        // v22 = t6a,  v26 = t5a
        dmbutterfly     v23, v25, v0.h[2], v0.h[3], v18, v19, v30, v31        // v23 = t9a,  v25 = t14a
        dmbutterfly     v27, v21, v0.h[2], v0.h[3], v18, v19, v30, v31, neg=1 // v27 = t13a, v21 = t10a
        idct16_end
endfunc

function idct16_half
        dmbutterfly0_h  v16, v24, v16, v24, v2, v3, v4, v5, v6, v7 // v16 = t0a,  v24 = t1a
        dmbutterfly_h1  v20, v28, v0.h[2], v0.h[3], v2, v3, v4, v5 // v20 = t2a,  v28 = t3a
        dmbutterfly_h1  v18, v30, v0.h[4], v0.h[5], v2, v3, v4, v5 // v18 = t4a,  v30 = t7a
        dmbutterfly_h2  v26, v22, v0.h[6], v0.h[7], v2, v3, v4, v5 // v26 = t5a,  v22 = t6a
        dmbutterfly_h1  v17, v31, v1.h[0], v1.h[1], v2, v3, v4, v5 // v17 = t8a,  v31 = t15a
        dmbutterfly_h2  v25, v23, v1.h[2], v1.h[3], v2, v3, v4, v5 // v25 = t9a,  v23 = t14a
        dmbutterfly_h1  v21, v27, v1.h[4], v1.h[5], v2, v3, v4, v5 // v21 = t10a, v27 = t13a
        dmbutterfly_h2  v29, v19, v1.h[6], v1.h[7], v2, v3, v4, v5 // v29 = t11a, v19 = t12a

        butterfly_8h    v4,  v28, v16, v28               // v4  = t0,   v28 = t3
        butterfly_8h    v5,  v20, v24, v20               // v5  = t1,   v20 = t2
        butterfly_8h    v6,  v26, v18, v26               // v6  = t4,   v26 = t5
        butterfly_8h    v7,  v22, v30, v22               // v7  = t7,   v22 = t6
        butterfly_8h    v16, v25, v17, v25               // v16 = t8,   v25 = t9
        butterfly_8h    v24, v21, v29, v21               // v24 = t11,  v21 = t10
        butterfly_8h    v17, v27, v19, v27               // v17 = t12,  v27 = t13
        butterfly_8h    v29, v23, v31, v23               // v29 = t15,  v23 = t14

        dmbutterfly0    v22, v26, v22, v26, v2, v3, v18, v19, v30, v31        // v22 = t6a,  v26 = t5a
        dmbutterfly     v23, v25, v0.h[2], v0.h[3], v18, v19, v30, v31        // v23 = t9a,  v25 = t14a
        dmbutterfly     v27, v21, v0.h[2], v0.h[3], v18, v19, v30, v31, neg=1 // v27 = t13a, v21 = t10a
        idct16_end
endfunc

function idct16_quarter
        dsmull_h        v24, v25, v19, v1.h[7]
        dsmull_h        v4,  v5,  v17, v1.h[0]
        dsmull_h        v7,  v6,  v18, v0.h[5]
        dsmull_h        v30, v31, v18, v0.h[4]
        neg             v24.4s,  v24.4s
        neg             v25.4s,  v25.4s
        dsmull_h        v29, v28, v17, v1.h[1]
        dsmull_h        v26, v27, v19, v1.h[6]
        dsmull_h        v22, v23, v16, v0.h[0]
        drshrn_h        v24, v24, v25, #14
        drshrn_h        v16, v4,  v5,  #14
        drshrn_h        v7,  v7,  v6,  #14
        drshrn_h        v6,  v30, v31, #14
        drshrn_h        v29, v29, v28, #14
        drshrn_h        v17, v26, v27, #14
        drshrn_h        v28, v22, v23, #14

        dmbutterfly_l   v20, v21, v22, v23, v17, v24, v0.h[2], v0.h[3]
        dmbutterfly_l   v18, v19, v30, v31, v29, v16, v0.h[2], v0.h[3]
        neg             v22.4s,  v22.4s
        neg             v23.4s,  v23.4s
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

        dmbutterfly_l   v6,  v7,  v4,  v5,  v31, v16, v0.h[1], v0.h[0]   // v6,v7   = t1,   v4,v5   = t0
        dmbutterfly_l   v10, v11, v8,  v9,  v23, v24, v0.h[5], v0.h[4]   // v10,v11 = t9,   v8,v9   = t8
        dbutterfly_n    v31, v24, v6,  v7,  v10, v11, v12, v13, v10, v11 // v31     = t1a,  v24     = t9a
        dmbutterfly_l   v14, v15, v12, v13, v29, v18, v0.h[3], v0.h[2]   // v14,v15 = t3,   v12,v13 = t2
        dbutterfly_n    v16, v23, v4,  v5,  v8,  v9,  v6,  v7,  v8,  v9  // v16     = t0a,  v23     = t8a

        dmbutterfly_l   v6,  v7,  v4,  v5,  v21, v26, v0.h[7], v0.h[6]   // v6,v7   = t11,  v4,v5   = t10
        dbutterfly_n    v29, v26, v14, v15, v6,  v7,  v8,  v9,  v6,  v7  // v29     = t3a,  v26     = t11a
        dmbutterfly_l   v10, v11, v8,  v9,  v27, v20, v1.h[1], v1.h[0]   // v10,v11 = t5,   v8,v9   = t4
        dbutterfly_n    v18, v21, v12, v13, v4,  v5,  v6,  v7,  v4,  v5  // v18     = t2a,  v21     = t10a

        dmbutterfly_l   v14, v15, v12, v13, v19, v28, v1.h[5], v1.h[4]   // v14,v15 = t13,  v12,v13 = t12
        dbutterfly_n    v20, v28, v10, v11, v14, v15, v4,  v5,  v14, v15 // v20     = t5a,  v28     = t13a
        dmbutterfly_l   v6,  v7,  v4,  v5,  v25, v22, v1.h[3], v1.h[2]   // v6,v7   = t7,   v4,v5   = t6
        dbutterfly_n    v27, v19, v8,  v9,  v12, v13, v10, v11, v12, v13 // v27     = t4a,  v19     = t12a

        dmbutterfly_l   v10, v11, v8,  v9,  v17, v30, v1.h[7], v1.h[6]   // v10,v11 = t15,  v8,v9   = t14
        ld1             {v0.8h}, [x10]
        dbutterfly_n    v22, v30, v6,  v7,  v10, v11, v12, v13, v10, v11 // v22     = t7a,  v30     = t15a
        dmbutterfly_l   v14, v15, v12, v13, v23, v24, v0.h[4], v0.h[5]   // v14,v15 = t9,   v12,v13 = t8
        dbutterfly_n    v25, v17, v4,  v5,  v8,  v9,  v6,  v7,  v8,  v9  // v25     = t6a,  v17     = t14a

        dmbutterfly_l   v4,  v5,  v6,  v7,  v28, v19, v0.h[5], v0.h[4]   // v4,v5   = t12,  v6,v7   = t13
        dbutterfly_n    v23, v19, v12, v13, v4,  v5,  v8,  v9,  v4,  v5  // v23     = t8a,  v19     = t12a
        dmbutterfly_l   v10, v11, v8,  v9,  v21, v26, v0.h[6], v0.h[7]   // v10,v11 = t11,  v8,v9   = t10
        butterfly_8h_r  v4,  v27, v16, v27               // v4  = t4,   v27 = t0
        dbutterfly_n    v24, v28, v14, v15, v6,  v7,  v12, v13, v6,  v7  // v24     = t9a,  v28     = t13a

        dmbutterfly_l   v12, v13, v14, v15, v30, v17, v0.h[7], v0.h[6]   // v12,v13 = t14,  v14,v15 = t15
        butterfly_8h_r  v5,  v20, v31, v20               // v5  = t5, v20 = t1
        dbutterfly_n    v21, v17, v8,  v9,  v12, v13, v6,  v7,  v12, v13 // v21     = t10a, v17     = t14a
        dbutterfly_n    v26, v30, v10, v11, v14, v15, v8,  v9,  v14, v15 // v26     = t11a, v30     = t15a

        butterfly_8h_r  v6,  v25, v18, v25               // v6  = t6, v25 = t2
        butterfly_8h_r  v7,  v22, v29, v22               // v7  = t7, v22 = t3

        dmbutterfly_l   v10, v11, v8,  v9,  v19, v28, v0.h[2], v0.h[3]   // v10,v11 = t13,  v8,v9   = t12
        dmbutterfly_l   v12, v13, v14, v15, v30, v17, v0.h[3], v0.h[2]   // v12,v13 = t14,  v14,v15 = t15

        dbutterfly_n    v18, v30, v8,  v9,  v12, v13, v16, v17, v12, v13 // v18   = out[2], v30     = t14a
        dbutterfly_n    v29, v17, v10, v11, v14, v15, v12, v13, v14, v15 // v29 = -out[13], v17     = t15a
        neg             v29.8h, v29.8h                   // v29 = out[13]

        dmbutterfly_l   v10, v11, v8,  v9,  v4,  v5,  v0.h[2], v0.h[3]   // v10,v11 = t5a,  v8,v9   = t4a
        dmbutterfly_l   v12, v13, v14, v15, v7,  v6,  v0.h[3], v0.h[2]   // v12,v13 = t6a,  v14,v15 = t7a

        butterfly_8h    v2,  v6,  v27, v25               // v2 = out[0], v6 = t2a
        butterfly_8h    v3,  v7,  v23, v21               // v3 =-out[1], v7 = t10

        dbutterfly_n    v19, v31, v8,  v9,  v12, v13, v4,  v5,  v8,  v9  // v19 = -out[3],  v31 = t6
        neg             v19.8h, v19.8h                   // v19 = out[3]
        dbutterfly_n    v28, v16, v10, v11, v14, v15, v4,  v5,  v10, v11 // v28 = out[12],  v16 = t7

        butterfly_8h    v5,  v8,  v20, v22               // v5 =-out[15],v8 = t3a
        butterfly_8h    v4,  v9,  v24, v26               // v4 = out[14],v9 = t11

        dmbutterfly0    v23, v24, v6,  v8,  v10, v11, v12, v13, v14, v15, 1 // v23 = out[7], v24 = out[8]
        dmbutterfly0    v21, v26, v30, v17, v10, v11, v12, v13, v14, v15, 1 // v21 = out[5], v26 = out[10]
        dmbutterfly0    v20, v27, v16, v31, v10, v11, v12, v13, v14, v15    // v20 = out[4], v27 = out[11]
        dmbutterfly0    v22, v25, v9,  v7,  v10, v11, v12, v13, v14, v15    // v22 = out[6], v25 = out[9]

        neg             v31.8h,  v5.8h                    // v31 = out[15]
        neg             v17.8h,  v3.8h                    // v17 = out[1]

        mov             v16.16b, v2.16b
        mov             v30.16b, v4.16b
        ret
endfunc

// Helper macros; we can't use these expressions directly within
// e.g. .irp due to the extra concatenation \(). Therefore wrap
// them in macros to allow using .irp below.
.macro load i, src, inc
        ld1             {v\i\().8h},  [\src], \inc
.endm
.macro store i, dst, inc
        st1             {v\i\().8h},  [\dst], \inc
.endm
.macro movi_v i, size, imm
        movi            v\i\()\size,  \imm
.endm
.macro load_clear i, src, inc
        ld1             {v\i\().8h}, [\src]
        st1             {v2.8h},  [\src], \inc
.endm

.macro load_add_store coef0, coef1, coef2, coef3, coef4, coef5, coef6, coef7, tmp1, tmp2
        srshr           \coef0, \coef0, #6
        ld1             {v2.8b},  [x0], x1
        srshr           \coef1, \coef1, #6
        ld1             {v3.8b},  [x3], x1
        srshr           \coef2, \coef2, #6
        ld1             {v4.8b},  [x0], x1
        srshr           \coef3, \coef3, #6
        uaddw           \coef0, \coef0, v2.8b
        ld1             {v5.8b},  [x3], x1
        uaddw           \coef1, \coef1, v3.8b
        srshr           \coef4, \coef4, #6
        ld1             {v6.8b},  [x0], x1
        srshr           \coef5, \coef5, #6
        ld1             {v7.8b},  [x3], x1
        sqxtun          v2.8b,  \coef0
        srshr           \coef6, \coef6, #6
        sqxtun          v3.8b,  \coef1
        srshr           \coef7, \coef7, #6
        uaddw           \coef2, \coef2, v4.8b
        ld1             {\tmp1},  [x0], x1
        uaddw           \coef3, \coef3, v5.8b
        ld1             {\tmp2},  [x3], x1
        sqxtun          v4.8b,  \coef2
        sub             x0,  x0,  x1, lsl #2
        sub             x3,  x3,  x1, lsl #2
        sqxtun          v5.8b,  \coef3
        uaddw           \coef4, \coef4, v6.8b
        st1             {v2.8b},  [x0], x1
        uaddw           \coef5, \coef5, v7.8b
        st1             {v3.8b},  [x3], x1
        sqxtun          v6.8b,  \coef4
        st1             {v4.8b},  [x0], x1
        sqxtun          v7.8b,  \coef5
        st1             {v5.8b},  [x3], x1
        uaddw           \coef6, \coef6, \tmp1
        st1             {v6.8b},  [x0], x1
        uaddw           \coef7, \coef7, \tmp2
        st1             {v7.8b},  [x3], x1
        sqxtun          \tmp1,  \coef6
        sqxtun          \tmp2,  \coef7
        st1             {\tmp1},  [x0], x1
        st1             {\tmp2},  [x3], x1
.endm

// Read a vertical 8x16 slice out of a 16x16 matrix, do a transform on it,
// transpose into a horizontal 16x8 slice and store.
// x0 = dst (temp buffer)
// x1 = slice offset
// x2 = src
// x9 = input stride
.macro itxfm16_1d_funcs txfm
function \txfm\()16_1d_8x16_pass1_neon
        mov             x14, x30

        movi            v2.8h, #0
.irp i, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        load_clear      \i,  x2,  x9
.endr

        bl              \txfm\()16

        // Do two 8x8 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v23 and v24-v31 contain the two
        // transposed 8x8 blocks.
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v2, v3
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v30, v31, v2, v3

        // Store the transposed 8x8 blocks horizontally.
        cmp             x1,  #8
        b.eq            1f
.irp i, 16, 24, 17, 25, 18, 26, 19, 27, 20, 28, 21, 29, 22, 30, 23, 31
        store           \i,  x0,  #16
.endr
        ret             x14
1:
        // Special case: For the last input column (x1 == 8),
        // which would be stored as the last row in the temp buffer,
        // don't store the first 8x8 block, but keep it in registers
        // for the first slice of the second pass (where it is the
        // last 8x8 block).
.irp i, 24, 25, 26, 27, 28, 29, 30, 31
        add             x0,  x0,  #16
        store           \i,  x0,  #16
.endr
        mov             v24.16b, v16.16b
        mov             v25.16b, v17.16b
        mov             v26.16b, v18.16b
        mov             v27.16b, v19.16b
        mov             v28.16b, v20.16b
        mov             v29.16b, v21.16b
        mov             v30.16b, v22.16b
        mov             v31.16b, v23.16b
        ret             x14
endfunc

// Read a vertical 8x16 slice out of a 16x16 matrix, do a transform on it,
// load the destination pixels (from a similar 8x16 slice), add and store back.
// x0 = dst
// x1 = dst stride
// x2 = src (temp buffer)
// x3 = slice offset
// x9 = temp buffer stride
function \txfm\()16_1d_8x16_pass2_neon
        mov             x14, x30
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load            \i,  x2,  x9
.endr
        cbz             x3,  1f
.irp i, 24, 25, 26, 27, 28, 29, 30, 31
        load            \i,  x2,  x9
.endr
1:

        add             x3,  x0,  x1
        lsl             x1,  x1,  #1
        bl              \txfm\()16

        load_add_store  v16.8h, v17.8h, v18.8h, v19.8h, v20.8h, v21.8h, v22.8h, v23.8h, v16.8b, v17.8b
        load_add_store  v24.8h, v25.8h, v26.8h, v27.8h, v28.8h, v29.8h, v30.8h, v31.8h, v16.8b, v17.8b

        ret             x14
endfunc
.endm

itxfm16_1d_funcs idct
itxfm16_1d_funcs iadst

.macro itxfm_func16x16 txfm1, txfm2
function ff_vp9_\txfm1\()_\txfm2\()_16x16_add_neon, export=1
.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #1
        b.eq            idct16x16_dc_add_neon
.endif
        mov             x15, x30
        // iadst16 requires clobbering v8-v15, but idct16 doesn't need to.
.ifnc \txfm1\()_\txfm2,idct_idct
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
.endif

        sub             sp,  sp,  #512

        mov             x4,  x0
        mov             x5,  x1
        mov             x6,  x2

        movrel          x10, idct_coeffs
.ifnc \txfm1\()_\txfm2,idct_idct
        movrel          x11, iadst16_coeffs
.endif
.ifc \txfm1,idct
        ld1             {v0.8h,v1.8h}, [x10]
.endif
        mov             x9,  #32

.ifc \txfm1\()_\txfm2,idct_idct
        cmp             w3,  #10
        b.le            idct16x16_quarter_add_neon
        cmp             w3,  #38
        b.le            idct16x16_half_add_neon
.endif

.irp i, 0, 8
        add             x0,  sp,  #(\i*32)
.ifc \txfm1\()_\txfm2,idct_idct
.if \i == 8
        cmp             w3,  #38
        b.le            1f
.endif
.endif
        mov             x1,  #\i
        add             x2,  x6,  #(\i*2)
        bl              \txfm1\()16_1d_8x16_pass1_neon
.endr
.ifc \txfm1\()_\txfm2,iadst_idct
        ld1             {v0.8h,v1.8h}, [x10]
.endif

.ifc \txfm1\()_\txfm2,idct_idct
        b               3f
1:
        // Set v24-v31 to zero, for the in-register passthrough of
        // coefficients to pass 2. Since we only do two slices, this can
        // only ever happen for the second slice. So we only need to store
        // zeros to the temp buffer for the second half of the buffer.
        // Move x0 to the second half, and use x9 == 32 as increment.
        add             x0,  x0,  #16
.irp i, 24, 25, 26, 27, 28, 29, 30, 31
        movi_v          \i,  .16b, #0
        st1             {v24.8h},  [x0], x9
.endr
3:
.endif

.irp i, 0, 8
        add             x0,  x4,  #(\i)
        mov             x1,  x5
        add             x2,  sp,  #(\i*2)
        mov             x3,  #\i
        bl              \txfm2\()16_1d_8x16_pass2_neon
.endr

        add             sp,  sp,  #512
.ifnc \txfm1\()_\txfm2,idct_idct
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
.endif
        ret             x15
endfunc
.endm

itxfm_func16x16 idct,  idct
itxfm_func16x16 iadst, idct
itxfm_func16x16 idct,  iadst
itxfm_func16x16 iadst, iadst

function idct16_1d_8x16_pass1_quarter_neon
        mov             x14, x30
        movi            v2.8h, #0
.irp i, 16, 17, 18, 19
        load_clear      \i,  x2,  x9
.endr

        bl              idct16_quarter

        // Do two 8x8 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v23 and v24-v31 contain the two
        // transposed 8x8 blocks.
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v2, v3
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v30, v31, v2, v3

        // Store the transposed 8x8 blocks horizontally.
        // The first 8x8 block is kept in registers for the second pass,
        // store the rest in the temp buffer.
        // Since only a 4x4 part of the input was nonzero, this means that
        // only 4 rows are nonzero after transposing, and the second pass
        // only reads the topmost 4 rows. Therefore only store the topmost
        // 4 rows.
        add             x0,  x0,  #16
.irp i, 24, 25, 26, 27
        store           \i,  x0,  x9
.endr
        ret             x14
endfunc

function idct16_1d_8x16_pass2_quarter_neon
        mov             x14, x30
        cbz             x3,  1f
.irp i, 16, 17, 18, 19
        load            \i,  x2,  x9
.endr
1:

        add             x3,  x0,  x1
        lsl             x1,  x1,  #1
        bl              idct16_quarter

        load_add_store  v16.8h, v17.8h, v18.8h, v19.8h, v20.8h, v21.8h, v22.8h, v23.8h, v16.8b, v17.8b
        load_add_store  v24.8h, v25.8h, v26.8h, v27.8h, v28.8h, v29.8h, v30.8h, v31.8h, v16.8b, v17.8b

        ret             x14
endfunc

function idct16_1d_8x16_pass1_half_neon
        mov             x14, x30
        movi            v2.8h, #0
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load_clear      \i,  x2,  x9
.endr

        bl              idct16_half

        // Do two 8x8 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v23 and v24-v31 contain the two
        // transposed 8x8 blocks.
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v2, v3
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v30, v31, v2, v3

        // Store the transposed 8x8 blocks horizontally.
        // The first 8x8 block is kept in registers for the second pass,
        // store the rest in the temp buffer.
        add             x0,  x0,  #16
.irp i, 24, 25, 26, 27, 28, 29, 30, 31
        store           \i,  x0,  x9
.endr
        ret             x14
endfunc

function idct16_1d_8x16_pass2_half_neon
        mov             x14, x30
        cbz             x3,  1f
.irp i, 16, 17, 18, 19, 20, 21, 22, 23
        load            \i,  x2,  x9
.endr
1:

        add             x3,  x0,  x1
        lsl             x1,  x1,  #1
        bl              idct16_half

        load_add_store  v16.8h, v17.8h, v18.8h, v19.8h, v20.8h, v21.8h, v22.8h, v23.8h, v16.8b, v17.8b
        load_add_store  v24.8h, v25.8h, v26.8h, v27.8h, v28.8h, v29.8h, v30.8h, v31.8h, v16.8b, v17.8b

        ret             x14
endfunc

.macro idct16_partial size
function idct16x16_\size\()_add_neon
        add             x0,  sp,  #(0*32)
        add             x2,  x6,  #(0*2)
        bl              idct16_1d_8x16_pass1_\size\()_neon
.irp i, 0, 8
        add             x0,  x4,  #(\i)
        mov             x1,  x5
        add             x2,  sp,  #(\i*2)
        mov             x3,  #\i
        bl              idct16_1d_8x16_pass2_\size\()_neon
.endr

        add             sp,  sp,  #512
        ret             x15
endfunc
.endm

idct16_partial quarter
idct16_partial half

function idct32x32_dc_add_neon
        movrel          x4,  idct_coeffs
        ld1             {v0.4h}, [x4]

        movi            v1.4h,  #0

        ld1             {v2.h}[0], [x2]
        smull           v2.4s,  v2.4h,  v0.h[0]
        rshrn           v2.4h,  v2.4s,  #14
        smull           v2.4s,  v2.4h,  v0.h[0]
        rshrn           v2.4h,  v2.4s,  #14
        dup             v2.8h,  v2.h[0]
        st1             {v1.h}[0], [x2]

        srshr           v0.8h,  v2.8h,  #6

        mov             x3,  x0
        mov             x4,  #32
1:
        // Loop to add the constant v0 into all 32x32 outputs
        subs            x4,  x4,  #2
        ld1             {v1.16b,v2.16b},  [x0], x1
        uaddw           v16.8h, v0.8h,  v1.8b
        uaddw2          v17.8h, v0.8h,  v1.16b
        ld1             {v3.16b,v4.16b},  [x0], x1
        uaddw           v18.8h, v0.8h,  v2.8b
        uaddw2          v19.8h, v0.8h,  v2.16b
        uaddw           v20.8h, v0.8h,  v3.8b
        uaddw2          v21.8h, v0.8h,  v3.16b
        uaddw           v22.8h, v0.8h,  v4.8b
        uaddw2          v23.8h, v0.8h,  v4.16b
        sqxtun          v1.8b,  v16.8h
        sqxtun2         v1.16b, v17.8h
        sqxtun          v2.8b,  v18.8h
        sqxtun2         v2.16b, v19.8h
        sqxtun          v3.8b,  v20.8h
        sqxtun2         v3.16b, v21.8h
        st1             {v1.16b,v2.16b},  [x3], x1
        sqxtun          v4.8b,  v22.8h
        sqxtun2         v4.16b, v23.8h
        st1             {v3.16b,v4.16b},  [x3], x1
        b.ne            1b

        ret
endfunc

.macro idct32_end
        butterfly_8h    v16, v5,  v4,  v5  // v16 = t16a, v5  = t19a
        butterfly_8h    v17, v20, v23, v20 // v17 = t17,  v20 = t18
        butterfly_8h    v18, v6,  v7,  v6  // v18 = t23a, v6  = t20a
        butterfly_8h    v19, v21, v22, v21 // v19 = t22,  v21 = t21
        butterfly_8h    v4,  v28, v28, v30 // v4  = t24a, v28 = t27a
        butterfly_8h    v23, v26, v25, v26 // v23 = t25,  v26 = t26
        butterfly_8h    v7,  v3,  v29, v31 // v7  = t31a, v3  = t28a
        butterfly_8h    v22, v27, v24, v27 // v22 = t30,  v27 = t29

        dmbutterfly     v27, v20, v0.h[2], v0.h[3], v24, v25, v30, v31        // v27 = t18a, v20 = t29a
        dmbutterfly     v3,  v5,  v0.h[2], v0.h[3], v24, v25, v30, v31        // v3  = t19,  v5  = t28
        dmbutterfly     v28, v6,  v0.h[2], v0.h[3], v24, v25, v30, v31, neg=1 // v28 = t27,  v6  = t20
        dmbutterfly     v26, v21, v0.h[2], v0.h[3], v24, v25, v30, v31, neg=1 // v26 = t26a, v21 = t21a

        butterfly_8h    v31, v24, v7,  v4  // v31 = t31,  v24 = t24
        butterfly_8h    v30, v25, v22, v23 // v30 = t30a, v25 = t25a
        butterfly_8h_r  v23, v16, v16, v18 // v23 = t23,  v16 = t16
        butterfly_8h_r  v22, v17, v17, v19 // v22 = t22a, v17 = t17a
        butterfly_8h    v18, v21, v27, v21 // v18 = t18,  v21 = t21
        butterfly_8h_r  v27, v28, v5,  v28 // v27 = t27a, v28 = t28a
        butterfly_8h    v29, v26, v20, v26 // v29 = t29,  v26 = t26
        butterfly_8h    v19, v20, v3,  v6  // v19 = t19a, v20 = t20

        dmbutterfly0    v27, v20, v27, v20, v2, v3, v4, v5, v6, v7 // v27 = t27,  v20 = t20
        dmbutterfly0    v26, v21, v26, v21, v2, v3, v4, v5, v6, v7 // v26 = t26a, v21 = t21a
        dmbutterfly0    v25, v22, v25, v22, v2, v3, v4, v5, v6, v7 // v25 = t25,  v22 = t22
        dmbutterfly0    v24, v23, v24, v23, v2, v3, v4, v5, v6, v7 // v24 = t24a, v23 = t23a
        ret
.endm

function idct32_odd
        dmbutterfly     v16, v31, v8.h[0], v8.h[1], v4, v5, v6, v7 // v16 = t16a, v31 = t31a
        dmbutterfly     v24, v23, v8.h[2], v8.h[3], v4, v5, v6, v7 // v24 = t17a, v23 = t30a
        dmbutterfly     v20, v27, v8.h[4], v8.h[5], v4, v5, v6, v7 // v20 = t18a, v27 = t29a
        dmbutterfly     v28, v19, v8.h[6], v8.h[7], v4, v5, v6, v7 // v28 = t19a, v19 = t28a
        dmbutterfly     v18, v29, v9.h[0], v9.h[1], v4, v5, v6, v7 // v18 = t20a, v29 = t27a
        dmbutterfly     v26, v21, v9.h[2], v9.h[3], v4, v5, v6, v7 // v26 = t21a, v21 = t26a
        dmbutterfly     v22, v25, v9.h[4], v9.h[5], v4, v5, v6, v7 // v22 = t22a, v25 = t25a
        dmbutterfly     v30, v17, v9.h[6], v9.h[7], v4, v5, v6, v7 // v30 = t23a, v17 = t24a

        butterfly_8h    v4,  v24, v16, v24 // v4  = t16, v24 = t17
        butterfly_8h    v5,  v20, v28, v20 // v5  = t19, v20 = t18
        butterfly_8h    v6,  v26, v18, v26 // v6  = t20, v26 = t21
        butterfly_8h    v7,  v22, v30, v22 // v7  = t23, v22 = t22
        butterfly_8h    v28, v25, v17, v25 // v28 = t24, v25 = t25
        butterfly_8h    v30, v21, v29, v21 // v30 = t27, v21 = t26
        butterfly_8h    v29, v23, v31, v23 // v29 = t31, v23 = t30
        butterfly_8h    v31, v27, v19, v27 // v31 = t28, v27 = t29

        dmbutterfly     v23, v24, v0.h[4], v0.h[5], v16, v17, v18, v19        // v23 = t17a, v24 = t30a
        dmbutterfly     v27, v20, v0.h[4], v0.h[5], v16, v17, v18, v19, neg=1 // v27 = t29a, v20 = t18a
        dmbutterfly     v21, v26, v0.h[6], v0.h[7], v16, v17, v18, v19        // v21 = t21a, v26 = t26a
        dmbutterfly     v25, v22, v0.h[6], v0.h[7], v16, v17, v18, v19, neg=1 // v25 = t25a, v22 = t22a
        idct32_end
endfunc

function idct32_odd_half
        dmbutterfly_h1  v16, v31, v8.h[0], v8.h[1], v4, v5, v6, v7 // v16 = t16a, v31 = t31a
        dmbutterfly_h2  v24, v23, v8.h[2], v8.h[3], v4, v5, v6, v7 // v24 = t17a, v23 = t30a
        dmbutterfly_h1  v20, v27, v8.h[4], v8.h[5], v4, v5, v6, v7 // v20 = t18a, v27 = t29a
        dmbutterfly_h2  v28, v19, v8.h[6], v8.h[7], v4, v5, v6, v7 // v28 = t19a, v19 = t28a
        dmbutterfly_h1  v18, v29, v9.h[0], v9.h[1], v4, v5, v6, v7 // v18 = t20a, v29 = t27a
        dmbutterfly_h2  v26, v21, v9.h[2], v9.h[3], v4, v5, v6, v7 // v26 = t21a, v21 = t26a
        dmbutterfly_h1  v22, v25, v9.h[4], v9.h[5], v4, v5, v6, v7 // v22 = t22a, v25 = t25a
        dmbutterfly_h2  v30, v17, v9.h[6], v9.h[7], v4, v5, v6, v7 // v30 = t23a, v17 = t24a

        butterfly_8h    v4,  v24, v16, v24 // v4  = t16, v24 = t17
        butterfly_8h    v5,  v20, v28, v20 // v5  = t19, v20 = t18
        butterfly_8h    v6,  v26, v18, v26 // v6  = t20, v26 = t21
        butterfly_8h    v7,  v22, v30, v22 // v7  = t23, v22 = t22
        butterfly_8h    v28, v25, v17, v25 // v28 = t24, v25 = t25
        butterfly_8h    v30, v21, v29, v21 // v30 = t27, v21 = t26
        butterfly_8h    v29, v23, v31, v23 // v29 = t31, v23 = t30
        butterfly_8h    v31, v27, v19, v27 // v31 = t28, v27 = t29

        dmbutterfly     v23, v24, v0.h[4], v0.h[5], v16, v17, v18, v19        // v23 = t17a, v24 = t30a
        dmbutterfly     v27, v20, v0.h[4], v0.h[5], v16, v17, v18, v19, neg=1 // v27 = t29a, v20 = t18a
        dmbutterfly     v21, v26, v0.h[6], v0.h[7], v16, v17, v18, v19        // v21 = t21a, v26 = t26a
        dmbutterfly     v25, v22, v0.h[6], v0.h[7], v16, v17, v18, v19, neg=1 // v25 = t25a, v22 = t22a
        idct32_end
endfunc

function idct32_odd_quarter
        dsmull_h        v4,  v5,  v16, v8.h[0]
        dsmull_h        v28, v29, v19, v8.h[7]
        dsmull_h        v30, v31, v16, v8.h[1]
        dsmull_h        v22, v23, v17, v9.h[6]
        dsmull_h        v7,  v6,  v17, v9.h[7]
        dsmull_h        v26, v27, v19, v8.h[6]
        dsmull_h        v20, v21, v18, v9.h[0]
        dsmull_h        v24, v25, v18, v9.h[1]

        neg             v28.4s, v28.4s
        neg             v29.4s, v29.4s
        neg             v7.4s,  v7.4s
        neg             v6.4s,  v6.4s

        drshrn_h        v4,  v4,  v5,  #14
        drshrn_h        v5,  v28, v29, #14
        drshrn_h        v29, v30, v31, #14
        drshrn_h        v28, v22, v23, #14
        drshrn_h        v7,  v7,  v6,  #14
        drshrn_h        v31, v26, v27, #14
        drshrn_h        v6,  v20, v21, #14
        drshrn_h        v30, v24, v25, #14

        dmbutterfly_l   v16, v17, v18, v19, v29, v4,  v0.h[4], v0.h[5]
        dmbutterfly_l   v27, v26, v20, v21, v31, v5,  v0.h[4], v0.h[5]
        drshrn_h        v23, v16, v17, #14
        drshrn_h        v24, v18, v19, #14
        neg             v20.4s, v20.4s
        neg             v21.4s, v21.4s
        drshrn_h        v27, v27, v26, #14
        drshrn_h        v20, v20, v21, #14
        dmbutterfly_l   v16, v17, v18, v19, v30, v6,  v0.h[6], v0.h[7]
        drshrn_h        v21, v16, v17, #14
        drshrn_h        v26, v18, v19, #14
        dmbutterfly_l   v16, v17, v18, v19, v28, v7,  v0.h[6], v0.h[7]
        drshrn_h        v25, v16, v17, #14
        neg             v18.4s, v18.4s
        neg             v19.4s, v19.4s
        drshrn_h        v22, v18, v19, #14

        idct32_end
endfunc

.macro idct32_funcs suffix
// Do an 32-point IDCT of a 8x32 slice out of a 32x32 matrix.
// The 32-point IDCT can be decomposed into two 16-point IDCTs;
// a normal IDCT16 with every other input component (the even ones, with
// each output written twice), followed by a separate 16-point IDCT
// of the odd inputs, added/subtracted onto the outputs of the first idct16.
// x0 = dst (temp buffer)
// x1 = unused
// x2 = src
// x9 = double input stride
function idct32_1d_8x32_pass1\suffix\()_neon
        mov             x14, x30
        movi            v2.8h,  #0

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

        // Do two 8x8 transposes. Originally, v16-v31 contain the
        // 16 rows. Afterwards, v16-v23 and v24-v31 contain the
        // two transposed 8x8 blocks.
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v2, v3
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v30, v31, v2, v3

        // Store the registers a, b horizontally, followed by the
        // same registers b, a mirrored.
.macro store_rev a, b
        // There's no rev128 instruction, but we reverse each 64 bit
        // half, and then flip them using an ext with 8 bytes offset.
        rev64           v3.8h, \b
        st1             {\a},  [x0], #16
        rev64           v2.8h, \a
        ext             v3.16b, v3.16b, v3.16b, #8
        st1             {\b},  [x0], #16
        ext             v2.16b, v2.16b, v2.16b, #8
        st1             {v3.8h},  [x0], #16
        st1             {v2.8h},  [x0], #16
.endm
        store_rev       v16.8h, v24.8h
        store_rev       v17.8h, v25.8h
        store_rev       v18.8h, v26.8h
        store_rev       v19.8h, v27.8h
        store_rev       v20.8h, v28.8h
        store_rev       v21.8h, v29.8h
        store_rev       v22.8h, v30.8h
        store_rev       v23.8h, v31.8h
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
        add             x2,  x2,  #64

        movi            v2.8h,  #0
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

        transpose_8x8H  v31, v30, v29, v28, v27, v26, v25, v24, v2, v3
        transpose_8x8H  v23, v22, v21, v20, v19, v18, v17, v16, v2, v3

        // Store the registers a, b horizontally,
        // adding into the output first, and the mirrored,
        // subtracted from the output.
.macro store_rev a, b
        ld1             {v4.8h},  [x0]
        rev64           v3.8h, \b
        add             v4.8h, v4.8h, \a
        rev64           v2.8h, \a
        st1             {v4.8h},  [x0], #16
        ext             v3.16b, v3.16b, v3.16b, #8
        ld1             {v5.8h},  [x0]
        ext             v2.16b, v2.16b, v2.16b, #8
        add             v5.8h, v5.8h, \b
        st1             {v5.8h},  [x0], #16
        ld1             {v6.8h},  [x0]
        sub             v6.8h, v6.8h, v3.8h
        st1             {v6.8h},  [x0], #16
        ld1             {v7.8h},  [x0]
        sub             v7.8h, v7.8h, v2.8h
        st1             {v7.8h},  [x0], #16
.endm

        store_rev       v31.8h, v23.8h
        store_rev       v30.8h, v22.8h
        store_rev       v29.8h, v21.8h
        store_rev       v28.8h, v20.8h
        store_rev       v27.8h, v19.8h
        store_rev       v26.8h, v18.8h
        store_rev       v25.8h, v17.8h
        store_rev       v24.8h, v16.8h
.purgem store_rev
        ret             x14
endfunc

// This is mostly the same as 8x32_pass1, but without the transpose,
// and use the source as temp buffer between the two idct passes, and
// add into the destination.
// x0 = dst
// x1 = dst stride
// x2 = src (temp buffer)
// x7 = negative double temp buffer stride
// x9 = double temp buffer stride
function idct32_1d_8x32_pass2\suffix\()_neon
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
        add             x2,  x2,  #64

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
        sub             x2,  x2,  #64

        bl              idct32_odd\suffix

.macro load_acc_store a, b, c, d, neg=0
.if \neg == 0
        ld1             {v4.8h},  [x2], x9
        ld1             {v5.8h},  [x2], x9
        add             v4.8h, v4.8h, \a
        ld1             {v6.8h},  [x2], x9
        add             v5.8h, v5.8h, \b
        ld1             {v7.8h},  [x2], x9
        add             v6.8h, v6.8h, \c
        add             v7.8h, v7.8h, \d
.else
        ld1             {v4.8h},  [x2], x7
        ld1             {v5.8h},  [x2], x7
        sub             v4.8h, v4.8h, \a
        ld1             {v6.8h},  [x2], x7
        sub             v5.8h, v5.8h, \b
        ld1             {v7.8h},  [x2], x7
        sub             v6.8h, v6.8h, \c
        sub             v7.8h, v7.8h, \d
.endif
        ld1             {v10.8b}, [x0], x1
        ld1             {v11.8b}, [x0], x1
        srshr           v4.8h, v4.8h, #6
        ld1             {v2.8b}, [x0], x1
        srshr           v5.8h, v5.8h, #6
        uaddw           v4.8h, v4.8h, v10.8b
        ld1             {v3.8b}, [x0], x1
        srshr           v6.8h, v6.8h, #6
        uaddw           v5.8h, v5.8h, v11.8b
        srshr           v7.8h, v7.8h, #6
        sub             x0,  x0,  x1, lsl #2
        uaddw           v6.8h, v6.8h, v2.8b
        sqxtun          v4.8b, v4.8h
        uaddw           v7.8h, v7.8h, v3.8b
        sqxtun          v5.8b, v5.8h
        st1             {v4.8b}, [x0], x1
        sqxtun          v6.8b, v6.8h
        st1             {v5.8b}, [x0], x1
        sqxtun          v7.8b, v7.8h
        st1             {v6.8b}, [x0], x1
        st1             {v7.8b}, [x0], x1
.endm
        load_acc_store  v31.8h, v30.8h, v29.8h, v28.8h
        load_acc_store  v27.8h, v26.8h, v25.8h, v24.8h
        load_acc_store  v23.8h, v22.8h, v21.8h, v20.8h
        load_acc_store  v19.8h, v18.8h, v17.8h, v16.8h
        sub             x2,  x2,  x9
        load_acc_store  v16.8h, v17.8h, v18.8h, v19.8h, 1
        load_acc_store  v20.8h, v21.8h, v22.8h, v23.8h, 1
        load_acc_store  v24.8h, v25.8h, v26.8h, v27.8h, 1
        load_acc_store  v28.8h, v29.8h, v30.8h, v31.8h, 1
.purgem load_acc_store
        ret             x14
endfunc
.endm

idct32_funcs
idct32_funcs _quarter
idct32_funcs _half

const min_eob_idct_idct_32, align=4
        .short  0, 34, 135, 336
endconst

function ff_vp9_idct_idct_32x32_add_neon, export=1
        cmp             w3,  #1
        b.eq            idct32x32_dc_add_neon

        movrel          x10, idct_coeffs

        mov             x15, x30

        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!

        sub             sp,  sp,  #2048

        mov             x4,  x0
        mov             x5,  x1
        mov             x6,  x2

        // Double stride of the input, since we only read every other line
        mov             x9,  #128
        neg             x7,  x9

        ld1             {v0.8h,v1.8h}, [x10], #32
        ld1             {v8.8h,v9.8h}, [x10]

        cmp             w3,  #34
        b.le            idct32x32_quarter_add_neon
        cmp             w3,  #135
        b.le            idct32x32_half_add_neon

        movrel          x12, min_eob_idct_idct_32, 2

.irp i, 0, 8, 16, 24
        add             x0,  sp,  #(\i*64)
.if \i > 0
        ldrh            w1,  [x12], #2
        cmp             w3,  w1
        mov             x1,  #(32 - \i)/4
        b.le            1f
.endif
        add             x2,  x6,  #(\i*2)
        bl              idct32_1d_8x32_pass1_neon
.endr
        b               3f

1:
        // Write zeros to the temp buffer for pass 2
        movi            v16.8h,  #0
        movi            v17.8h,  #0
        movi            v18.8h,  #0
        movi            v19.8h,  #0
2:
        subs            x1,  x1,  #1
.rept 4
        st1             {v16.8h,v17.8h,v18.8h,v19.8h},  [x0], #64
.endr
        b.ne            2b
3:
.irp i, 0, 8, 16, 24
        add             x0,  x4,  #(\i)
        mov             x1,  x5
        add             x2,  sp,  #(\i*2)
        bl              idct32_1d_8x32_pass2_neon
.endr

        add             sp,  sp,  #2048

        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10

        ret             x15
endfunc

.macro idct32_partial size
function idct32x32_\size\()_add_neon
        add             x0,  sp,  #(0*64)
        add             x2,  x6,  #(0*2)
        bl              idct32_1d_8x32_pass1_\size\()_neon
.ifc \size,half
        add             x0,  sp,  #(8*64)
        add             x2,  x6,  #(8*2)
        bl              idct32_1d_8x32_pass1_\size\()_neon
.endif
.irp i, 0, 8, 16, 24
        add             x0,  x4,  #(\i)
        mov             x1,  x5
        add             x2,  sp,  #(\i*2)
        bl              idct32_1d_8x32_pass2_\size\()_neon
.endr

        add             sp,  sp,  #2048

        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10

        ret             x15
endfunc
.endm

idct32_partial quarter
idct32_partial half
