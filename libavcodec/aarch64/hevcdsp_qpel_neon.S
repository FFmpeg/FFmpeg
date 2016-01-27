/*
 * ARM NEON optimised HEVC decode for aarch64
 * Copyright (c) 2015 Junhai ZHANG <243186085@qq.com>
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

#define MAX_PB_SIZE #64
#define MAX_PB_DOUBLESIZE #128

.macro init_put_pixels
        prfm PLDL1STRM,   [x1]
        prfm PLDL1STRM,   [x1, x2]
        mov  x12, MAX_PB_DOUBLESIZE
.endm

function ff_hevc_put_pixels_w2_neon_8, export=1
        init_put_pixels
0:      subs    x3, x3, #2
        ld1         {v0.H}[0], [x1], x2
        ld1         {v0.H}[1], [x1], x2
        ushll       v0.8H, v0.8B, #6
        st1         {v0.S}[0], [x0], x12
        st1         {v0.S}[1], [x0], x12
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w4_neon_8, export=1
        init_put_pixels
0:      subs    x3, x3, #2
        ld1         {v0.S}[0], [x1], x2
        ld1         {v0.S}[1], [x1], x2
        ushll       v0.8H, v0.8B, #6
        st1         {v0.D}[0], [x0], x12
        st1         {v0.D}[1], [x0], x12
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w6_neon_8, export=1
        init_put_pixels
        sub     x10, x2, #4
        sub     x11, x12, #8
0:      subs    x3, x3, #2
        ld1         {v0.S}[0], [x1], #4
        ld1         {v1.H}[0], [x1], x10
        ld1         {v0.S}[1], [x1], #4
        ld1         {v1.H}[1], [x1], x10
        ushll       v0.8H, v0.8B, #6
        ushll       v1.8H, v1.8B, #6
        st1         {v0.1D}, [x0], #8
        st1         {v1.S}[0], [x0], x11
        st1         {v0.D}[1], [x0], #8
        st1         {v1.S}[1], [x0], x11
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w8_neon_8, export=1
        init_put_pixels
0:      subs    x3, x3, #2
        ld1         {v0.8B}, [x1], x2
        ld1         {v1.8B}, [x1], x2
        ushll       v8.8H, v0.8B, #6
        ushll       v9.8H, v1.8B, #6
        st1         {v8.8H}, [x0], x12
        st1         {v9.8H}, [x0], x12
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w12_neon_8, export=1
        init_put_pixels
        sub     x10, x2, #8
        sub     x11, x12, #16
0:      subs    x3, x3, #2
        ld1         {v0.1D}, [x1], #8
        ld1         {v2.S}[0], [x1], x10
        ld1         {v1.1D}, [x1], #8
        ld1         {v2.S}[1], [x1], x10
        ushll       v0.8H, v0.8B, #6
        ushll       v1.8H, v1.8B, #6
        ushll       v2.8H, v2.8B, #6
        st1         {v0.2D}, [x0], #16
        st1         {v2.1D}, [x0], x11
        st1         {v1.2D}, [x0], #16
        st1         {v2.D}[1], [x0], x11
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w16_neon_8, export=1
        init_put_pixels
0:      subs    x3, x3, #2
        ld1         {v0.2D}, [x1], x2
        ld1         {v1.2D}, [x1], x2
        ushll       v8.8H, v0.8B, #6
        ushll2      v9.8H, v0.16B, #6
        ushll       v10.8H, v1.8B, #6
        ushll2      v11.8H, v1.16B, #6
        st1         {v8.2D - v9.2D}, [x0], x12
        st1         {v10.2D - v11.2D}, [x0], x12
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w24_neon_8, export=1
        init_put_pixels
0:      subs    x3, x3, #1
        ld1         {v0.1D - v2.1D}, [x1], x2
        ushll       v8.8H, v0.8B, #6
        ushll       v9.8H, v1.8B, #6
        ushll       v10.8H, v2.8B, #6
        st1         {v8.2D - v10.2D}, [x0], x12
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w32_neon_8, export=1
        init_put_pixels
0:      subs    x3, x3, #1
        ld1         {v0.2D - v1.2D}, [x1], x2
        ushll       v8.8H, v0.8B, #6
        ushll2      v9.8H, v0.16B, #6
        ushll       v10.8H, v1.8B, #6
        ushll2      v11.8H, v1.16B, #6
        st1         {v8.2D - v11.2D}, [x0], x12
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w48_neon_8, export=1
        init_put_pixels
        sub     x11, x12, #64
0:      subs    x3, x3, #1
        ld1         {v0.2D - v2.2D}, [x1], x2
        ushll       v8.8H, v0.8B, #6
        ushll2      v9.8H, v0.16B, #6
        ushll       v10.8H, v1.8B, #6
        ushll2      v11.8H, v1.16B, #6
        ushll       v12.8H, v2.8B, #6
        ushll2      v13.8H, v2.16B, #6
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v12.2D - v13.2D}, [x0], x11
        b.ne    0b
        ret
endfunc

function ff_hevc_put_pixels_w64_neon_8, export=1
        init_put_pixels
        sub     x11, x12, #64
0:      subs    x3, x3, #1
        ld1         {v0.2D - v3.2D}, [x1], x2
        ushll       v8.8H, v0.8B, #6
        ushll2      v9.8H, v0.16B, #6
        ushll       v10.8H, v1.8B, #6
        ushll2      v11.8H, v1.16B, #6
        ushll       v12.8H, v2.8B, #6
        ushll2      v13.8H, v2.16B, #6
        ushll       v14.8H, v3.8B, #6
        ushll2      v15.8H, v3.16B, #6
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v12.2D - v15.2D}, [x0], x11
        b.ne    0b
        ret
endfunc

.macro  regshuffle_d8
        mov         v16.8B, v17.8B
        mov         v17.8B, v18.8B
        mov         v18.8B, v19.8B
        mov         v19.8B, v20.8B
        mov         v20.8B, v21.8B
        mov         v21.8B, v22.8B
        mov         v22.8B, v23.8B
.endm

.macro  regshuffle_v8
        mov         v0.16B, v1.16B
        mov         v1.16B, v2.16B
        mov         v2.16B, v3.16B
        mov         v3.16B, v4.16B
        mov         v4.16B, v5.16B
        mov         v5.16B, v6.16B
        mov         v6.16B, v7.16B
.endm

.macro  vextin8
        prfm PLDL1STRM,   [x2]
        ld1         {v22.1D - v23.1D}, [x2], x3
        ext         v16.8B, v22.8B, v23.8B, #1
        ext         v17.8B, v22.8B, v23.8B, #2
        ext         v18.8B, v22.8B, v23.8B, #3
        ext         v19.8B, v22.8B, v23.8B, #4
        ext         v20.8B, v22.8B, v23.8B, #5
        ext         v21.8B, v22.8B, v23.8B, #6
        ext         v22.8B, v22.8B, v23.8B, #7
.endm

.macro  vextin8_4
        prfm PLDL1STRM,   [x2]
        ld1         {v22.1D - v23.1D}, [x2], x3
        ld1         {v24.1D - v25.1D}, [x2], x3
        ext         v16.8B, v22.8B, v23.8B, #1
        ext         v17.8B, v22.8B, v23.8B, #2
        ext         v18.8B, v22.8B, v23.8B, #3
        ext         v19.8B, v22.8B, v23.8B, #4
        ext         v20.8B, v22.8B, v23.8B, #5
        ext         v21.8B, v22.8B, v23.8B, #6
        ext         v22.8B, v22.8B, v23.8B, #7
        ext         v26.8B, v24.8B, v25.8B, #1
        ext         v27.8B, v24.8B, v25.8B, #2
        ext         v28.8B, v24.8B, v25.8B, #3
        ext         v29.8B, v24.8B, v25.8B, #4
        ext         v30.8B, v24.8B, v25.8B, #5
        ext         v31.8B, v24.8B, v25.8B, #6
        ext         v24.8B, v24.8B, v25.8B, #7
        trn1        v16.4S, v16.4S, v26.4S
        trn1        v17.4S, v17.4S, v27.4S
        trn1        v18.4S, v18.4S, v28.4S
        trn1        v19.4S, v19.4S, v29.4S
        trn1        v20.4S, v20.4S, v30.4S
        trn1        v21.4S, v21.4S, v31.4S
        trn1        v22.4S, v22.4S, v24.4S
        trn1        v23.4S, v23.4S, v25.4S
.endm

.macro  loadin8
        prfm PLDL1STRM,   [x2]
        ld1         {v16.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v17.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v18.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v19.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v20.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v21.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v22.1D}, [x2], x3
        prfm PLDL1STRM,   [x2]
        ld1         {v23.1D}, [x2], x3
.endm

.macro qpel_filter_1_32b
        movi        v16.8H, #58
        movi        v17.8H, #10
        smull       v9.4S, v3.4H, v16.4H
        smull2      v10.4S, v3.8H, v16.8H
        movi        v16.8H, #17
        smull       v11.4S, v2.4H, v17.4H
        smull2      v12.4S, v2.8H, v17.8H
        movi        v17.8H, #5
        smull       v13.4S, v4.4H, v16.4H
        smull2      v14.4S, v4.8H, v16.8H
        smull       v15.4S, v5.4H, v17.4H
        smull2      v8.4S, v5.8H, v17.8H
        sub         v9.4S, v9.4S, v11.4S
        sub         v10.4S, v10.4S, v12.4S
        sshll       v11.4S, v1.4H, #2
        sshll2      v12.4S, v1.8H, #2
        add         v9.4S, v9.4S, v13.4S
        add         v10.4S, v10.4S, v14.4S
        ssubl       v13.4S, v6.4H, v0.4H
        ssubl2      v14.4S, v6.8H, v0.8H
        add         v9.4S, v9.4S, v11.4S
        add         v10.4S, v10.4S, v12.4S
        sub         v13.4S, v13.4S, v15.4S
        sub         v14.4S, v14.4S, v8.4S
        add         v9.4S, v9.4S, v13.4S
        add         v10.4S, v10.4S, v14.4S
        sqshrn      v8.4H, v9.4S, #6
        sqshrn2     v8.8H, v10.4S, #6
.endm

.macro qpel_filter_2_32b
        movi        v8.4S, #11
        saddl       v9.4S, v3.4H, v4.4H
        saddl2      v10.4S, v3.8H, v4.8H
        saddl       v11.4S, v2.4H, v5.4H
        saddl2      v12.4S, v2.8H, v5.8H
        mul         v11.4S, v11.4S, v8.4S
        mul         v12.4S, v12.4S, v8.4S
        movi        v8.4S, #40
        saddl       v15.4S, v1.4H, v6.4H
        mul         v9.4S, v9.4S, v8.4S
        mul         v10.4S, v10.4S, v8.4S
        saddl2      v8.4S, v1.8H, v6.8H
        saddl       v13.4S, v0.4H, v7.4H
        saddl2      v14.4S, v0.8H, v7.8H
        shl         v15.4S, v15.4S, #2
        shl         v8.4S, v8.4S, #2
        add         v11.4S, v11.4S, v13.4S
        add         v12.4S, v12.4S, v14.4S
        add         v9.4S, v9.4S, v15.4S
        add         v10.4S, v10.4S, v8.4S
        sub         v9.4S, v9.4S, v11.4S
        sub         v10.4S, v10.4S, v12.4S
        sqshrn      v8.4H, v9.4S, #6
        sqshrn2     v8.8H, v10.4S, #6
.endm

.macro qpel_filter_3_32b
        movi        v16.8H, #58
        movi        v17.8H, #10
        smull       v9.4S, v4.4H, v16.4H
        smull2      v10.4S, v4.8H, v16.8H
        movi        v16.8H, #17
        smull       v11.4S, v5.4H, v17.4H
        smull2      v12.4S, v5.8H, v17.8H
        movi        v17.8H, #5
        smull       v13.4S, v3.4H, v16.4H
        smull2      v14.4S, v3.8H, v16.8H
        smull       v15.4S, v2.4H, v17.4H
        smull2      v8.4S, v2.8H, v17.8H
        sub         v9.4S, v9.4S, v11.4S
        sub         v10.4S, v10.4S, v12.4S
        sshll       v11.4S, v6.4H, #2
        sshll2      v12.4S, v6.8H, #2
        add         v9.4S, v9.4S, v13.4S
        add         v10.4S, v10.4S, v14.4S
        ssubl       v13.4S, v1.4H, v7.4H
        ssubl2      v14.4S, v1.8H, v7.8H
        add         v9.4S, v9.4S, v11.4S
        add         v10.4S, v10.4S, v12.4S
        sub         v13.4S, v13.4S, v15.4S
        sub         v14.4S, v14.4S, v8.4S
        add         v9.4S, v9.4S, v13.4S
        add         v10.4S, v10.4S, v14.4S
        sqshrn      v8.4H, v9.4S, #6
        sqshrn2     v8.8H, v10.4S, #6
.endm

.macro  qpel_filter_1 out=v7
        movi        v24.8B, #58
        movi        v25.8B, #10
        ushll       v13.8H, v20.8B, #4
        ushll       v14.8H, v21.8B, #2
        umull       \out\().8H, v19.8B, v24.8B
        uaddw       v13.8H, v13.8H, v20.8B
        umull       v15.8H, v18.8B, v25.8B
        uaddw       v14.8H, v14.8H, v21.8B
        usubl       v12.8H, v22.8B, v16.8B
        add         \out\().8H, \out\().8H, v13.8H
        ushll       v13.8H, v17.8B, #2
        add         v15.8H, v15.8H, v14.8H
        add         v13.8H, v13.8H, v12.8H
        sub         \out\().8H, \out\().8H, v15.8H
        add         \out\().8H, \out\().8H, v13.8H
.endm

.macro  qpel_filter_2 out=v7
        movi        v12.8H, #10
        movi        v14.8H, #11
        uaddl       v13.8H, v19.8B, v20.8B
        uaddl       v15.8H, v18.8B, v21.8B
        mul         v13.8H, v13.8H, v12.8H
        mul         v15.8H, v15.8H, v14.8H
        uaddl       \out\().8H, v17.8B, v22.8B
        uaddl       v12.8H, v16.8B, v23.8B
        add         \out\().8H, \out\().8H, v13.8H
        add         v12.8H, v12.8H, v15.8H
        shl         \out\().8H, \out\().8H, #2
        sub         \out\().8H, \out\().8H, v12.8H
.endm

.macro  qpel_filter_3 out=v7
        movi        v24.8B, #58
        movi        v25.8B, #10
        ushll       v13.8H, v19.8B, #4
        ushll       v14.8H, v18.8B, #2
        umull       \out\().8H, v20.8B, v24.8B
        uaddw       v13.8H, v13.8H, v19.8B
        umull       v15.8H, v21.8B, v25.8B
        uaddw       v14.8H, v14.8H, v18.8B
        usubl       v12.8H, v17.8B, v23.8B
        add         \out\().8H, \out\().8H, v13.8H
        ushll       v13.8H, v22.8B, #2  
        add         v15.8H, v15.8H, v14.8H 
        add         v13.8H, v13.8H, v12.8H 
        sub         \out\().8H, \out\().8H, v15.8H
        add         \out\().8H, \out\().8H, v13.8H
.endm

.macro  hevc_put_qpel_vX_neon_8 filter
        sub     x2, x2, x3, lsl #1
        sub     x2, x2, x3
        mov     x12, x4
        mov     x6, x0
        mov     x7, x2
        lsl     x1, x1, #1
0:      loadin8
        cmp     x5, #4
        b.eq    4f
8:      subs    x4, x4, #1
        \filter
        st1         {v7.2D}, [x0], x1
        regshuffle_d8
        ld1         {v23.1D}, [x2], x3
        b.ne    8b
        subs    x5, x5, #8
        b.eq    99f
        mov     x4, x12
        add     x6, x6, #16
        mov     x0, x6
        add     x7, x7, #8
        mov     x2, x7
        b       0b
4:      subs    x4, x4, #1
        \filter
        st1         {v7.1D}, [x0], x1
        regshuffle_d8
        ld1         {v23.S}[0], [x2], x3
        b.ne    4b
99:     ret
.endm

.macro  hevc_put_qpel_uw_vX_neon_8 filter
        sub     x2, x2, x3, lsl #1
        sub     x2, x2, x3
        mov     x12, x5
        mov     x13, x0
        mov     x14, x2
        cmp     x6, #0
        b.ne    .Lbi\@
0:      loadin8
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filter
        sqrshrun    v0.8B, v7.8H, #6
        st1         {v0.1D}, [x0], x1
        regshuffle_d8
        ld1         {v23.1D}, [x2], x3
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x13, x13, #8
        add     x14, x14, #8
        mov     x5, x12
        mov     x0, x13
        mov     x2, x14
        b       0b
4:      subs    x5, x5, #1
        \filter
        sqrshrun    v0.8B, v7.8H, #6
        st1         {v0.S}[0], [x0], x1
        regshuffle_d8
        ld1         {v23.S}[0], [x2], x3
        b.ne    4b
        ret
.Lbi\@:
        lsl     x7, x7, #1
        mov     x15, x6
0:      loadin8
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filter
        ld1         {v0.2D}, [x6], x7
        sqadd       v0.8H, v0.8H, v7.8H
        sqrshrun    v0.8B, v0.8H, #7
        st1         {v0.1D}, [x0], x1
        regshuffle_d8
        ld1         {v23.1D}, [x2], x3
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x13, x13, #8
        add     x15, x15, #16
        add     x14, x14, #8
        mov     x5, x12
        mov     x0, x13
        mov     x6, x15
        mov     x2, x14
        b       0b
4:      subs    x5, x5, #1
        \filter
        ld1         {v0.1D}, [x6], x7
        sqadd       v0.4H, v0.4H, v7.4H
        sqrshrun    v0.8B, v0.8H, #7
        st1         {v0.S}[0], [x0], x1
        regshuffle_d8
        ld1         {v23.S}[0], [x2], x3
        b.ne    4b
99:     ret
.endm

.macro  hevc_put_qpel_uw_weight_vX_neon_8 filter
        ldp     w8, w9, [sp]
        ldp     w10, w11, [sp, #8]
        mov     w12, #7
        sub     w12, w12, w7
        lsl     w8, w8, w12
        lsl     w9, w9, w12
        sub     x2, x2, x3, lsl #1
        sub     x2, x2, x3
        dup         v0.8H, w10
        dup         v1.8H, w8
        mov     x12, x0
        mov     x13, x2
        mov     x14, x5
        cmp     x6, #0
        b.ne    .Lbi\@

0:      loadin8
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filter
        smull       v14.4S, v1.4H, v7.4H
        smull2      v15.4S, v1.8H, v7.8H
        rshrn       v8.4H, v14.4S, #13
        rshrn2      v8.8H, v15.4S, #13
        sqadd       v8.8H, v8.8H, v0.8H
        sqxtun      v8.8B, v8.8H
        st1         {v8.1D}, [x0], x1
        prfm PLDL1STRM,   [x2]
        regshuffle_d8
        ld1         {v23.1D}, [x2], x3
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x12, x12, #8
        add     x13, x13, #8
        mov     x0, x12
        mov     x2, x13
        mov     x5, x14
        b       0b
4:      subs    x5, x5, #1
        \filter
        smull       v14.4S, v1.4H, v7.4H
        rshrn       v8.4H, v14.4S, #13
        sqadd       v8.8H, v8.8H, v0.8H
        sqxtun      v8.8B, v8.8H
        st1         {v8.S}[0], [x0], x1
        prfm PLDL1STRM,   [x2]
        regshuffle_d8
        ld1         {v23.S}[0], [x2], x3
        b.ne    4b
        ret
.Lbi\@:
        add     w10, w10, w11
        add     w10, w10, #1
        lsl     w10, w10, #13
        dup     v0.4S, w10
        dup     v2.8H, w9
        mov     x7, MAX_PB_DOUBLESIZE
        mov     x11, x6
0:      loadin8
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filter
        ld1     {v4.2D}, [x6], x7
        smull       v14.4S, v2.4H, v7.4H
        smull2      v15.4S, v2.8H, v7.8H
        smull       v12.4S, v1.4H, v4.4H
        smull2      v13.4S, v1.8H, v4.8H
        add         v14.4S, v14.4S, v12.4S
        add         v15.4S, v15.4S, v13.4S
        add         v14.4S, v14.4S, v0.4S
        add         v15.4S, v15.4S, v0.4S
        shrn        v8.4H, v14.4S, #14
        shrn2       v8.8H, v14.4S, #14
        sqxtun      v8.8B, v8.8H
        st1         {v8.1D}, [x0], x1
        prfm PLDL1STRM,   [x2]
        regshuffle_d8
        ld1         {v23.1D}, [x2], x3
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x11, x11, #16
        add     x12, x12, #8
        add     x13, x13, #8
        mov     x6, x11
        mov     x0, x12
        mov     x2, x13
        mov     x5, x14
        b       0b
4:      subs    x5, x5, #1
        \filter
        ld1         {v4.1D}, [x6], x7
        smull       v14.4S, v2.4H, v7.4H
        smull       v12.4S, v1.4H, v4.4H
        add         v14.4S, v14.4S, v12.4S
        add         v14.4S, v14.4S, v0.4S
        shrn        v8.4H, v14.4S, #14
        sqxtun      v8.8B, v8.8H
        st1         {v8.S}[0], [x0], x1
        prfm PLDL1STRM,   [x2]
        regshuffle_d8
        ld1         {v23.S}[0], [x2], x3
        b.ne    4b
99:     ret
.endm

function ff_hevc_put_qpel_v1_neon_8, export=1
        hevc_put_qpel_vX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_v2_neon_8, export=1
        hevc_put_qpel_vX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_v3_neon_8, export=1
        hevc_put_qpel_vX_neon_8 qpel_filter_3
endfunc

function ff_hevc_put_qpel_uw_v1_neon_8, export=1
        hevc_put_qpel_uw_vX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_uw_v2_neon_8, export=1
        hevc_put_qpel_uw_vX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_uw_v3_neon_8, export=1
        hevc_put_qpel_uw_vX_neon_8 qpel_filter_3
endfunc

function ff_hevc_put_qpel_uw_weight_v1_neon_8, export=1
	hevc_put_qpel_uw_weight_vX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_uw_weight_v2_neon_8, export=1
	hevc_put_qpel_uw_weight_vX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_uw_weight_v3_neon_8, export=1
	hevc_put_qpel_uw_weight_vX_neon_8 qpel_filter_3
endfunc

.macro hevc_put_qpel_hX_neon_8 filter
        sub     x2, x2, #4
        lsl     x1, x1, #1
        mov     x12, x4
        mov     x6, x0
        mov     x7, x2
        cmp     x5, #4
        b.eq    4f
8:      subs    x4, x4, #1
        vextin8
        \filter
        st1         {v7.2D}, [x0], x1
        b.ne    8b
        subs    x5, x5, #8
        b.eq    99f
        mov     x4, x12
        add     x6, x6, #16
        mov     x0, x6
        add     x7, x7, #8
        mov     x2, x7
        cmp     x5, #4
        b.ne    8b
4:      subs    x4, x4, #2
        vextin8_4
        \filter
        st1         {v7.D}[0], [x0], x1
        st1         {v7.D}[1], [x0], x1
        b.ne    4b
99:     ret
.endm

.macro hevc_put_qpel_uw_hX_neon_8 filter
        sub     x2, x2, #4
        mov     x12, x5
        mov     x13, x0
        mov     x14, x2
        cmp     x6, #0
        b.ne    .Lbi\@
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        vextin8
        \filter
        sqrshrun    v0.8B, v7.8H, #6
        st1         {v0.1D}, [x0], x1
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x13, x13, #8
        add     x14, x14, #8
        mov     x5, x12
        mov     x0, x13
        mov     x2, x14
        cmp     x4, #4
        b.ne    8b
4:      subs    x5, x5, #2
        vextin8_4
        \filter
        sqrshrun    v0.8B, v7.8H, #6
        st1         {v0.S}[0], [x0], x1
        st1         {v0.S}[1], [x0], x1
        b.ne    4b
        ret
.Lbi\@:
        lsl     x7, x7, #1
        cmp     x4, #4
        b.eq    4f
        mov     x15, x6
8:      subs    x5, x5, #1
        vextin8
        \filter
        ld1         {v0.2D}, [x6], x7
        sqadd      v0.8H, v0.8H, v7.8H
        sqrshrun    v0.8B, v0.8H, #7
        st1         {v0.1D}, [x0]
        add     x0, x0, x1
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x15, x15, #16
        add     x13, x13, #8
        add     x14, x14, #8
        mov     x5, x12
        mov     x6, x15
        mov     x2, x14
        mov     x0, x13
        cmp     x4, #4
        b.ne    8b
4:      subs    x5, x5, #2
        vextin8_4
        \filter
        ld1         {v0.1D}, [x6], x7
        ld1         {v0.D}[1], [x6], x7
        sqadd      v0.8H, v0.8H, v7.8H
        sqrshrun    v0.8B, v0.8H, #7
        st1         {v0.S}[0], [x0], x1
        st1         {v0.S}[1], [x0], x1
        b.ne    4b
99:     ret
.endm

.macro  hevc_put_qpel_uw_weight_hX_neon_8 filter
        ldp     w8, w9, [sp]
        ldp     w10, w11, [sp, #8]
        mov     w12, #7
        sub     w12, w12, w7
        sub     x2, x2, #4
        lsl     w8, w8, w12
        lsl     w9, w9, w12
        dup         v0.8H, w10
        dup         v1.8H, w8
        mov     x12, x0
        mov     x13, x2
        mov     x14, x5
        cmp     x6, #0
        b.ne    .Lbi\@
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        vextin8
        \filter                          
        smull       v14.4S, v1.4H, v7.4H
        smull2      v15.4S, v1.8H, v7.8H
        rshrn       v8.4H, v14.4S, #13
        rshrn2      v8.8H, v15.4S, #13
        sqadd       v8.8H, v8.8H, v0.8H
        sqxtun      v8.8B, v8.8H
        st1         {v8.1D}, [x0], x1
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x12, x12, #8
        add     x13, x13, #8
        mov     x0, x12
        mov     x2, x13
        mov     x5, x14
        cmp     x4, #4
        b.ne    8b
4:      subs    x5, x5, #2
        vextin8_4
        \filter
        smull       v14.4S, v1.4H, v7.4H
        smull2      v15.4S, v1.8H, v7.8H
        rshrn       v8.4H, v14.4S, #13
        rshrn2      v8.8H, v15.4S, #13
        sqadd       v8.8H, v8.8H, v0.8H
        sqxtun      v8.8B, v8.8H
        st1         {v8.S}[0], [x0], x1
        st1         {v8.S}[1], [x0], x1
        b.ne    4b
        ret
.Lbi\@:
        add     w10, w10, w11
        add     w10, w10, #1
        lsl     w10, w10, #13
        dup         v0.4S, w10
        dup         v1.8H, w9
        dup         v2.8H, w8
        mov     x7, MAX_PB_DOUBLESIZE
        cmp     x4, #4
        b.eq    4f
        mov     x11, x6
8:      subs    x5, x5, #1
        vextin8
        \filter
        ld1         {v4.2D}, [x6], x7
        smull       v14.4S, v1.4H, v7.4H
        smull2      v15.4S, v1.8H, v7.8H
        smull       v12.4S, v2.4H, v4.4H
        smull2      v13.4S, v2.8H, v4.8H
        add         v14.4S, v14.4S, v12.4S
        add         v15.4S, v15.4S, v13.4S
        add         v14.4S, v14.4S, v0.4S
        add         v15.4S, v15.4S, v0.4S
        shrn        v8.4H, v14.4S, #14
        shrn2       v8.8H, v14.4S, #14
        sqxtun      v8.8B, v8.8H
        st1         {v8.1D}, [x0], x1
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x11, x11, #16
        add     x12, x12, #8
        add     x13, x13, #8
        mov     x6, x11
        mov     x0, x12
        mov     x2, x13
        mov     x5, x14
        cmp     x4, #4
        b.ne    8b
4:      subs    x5, x5, #2
        vextin8_4
        \filter
        ld1         {v4.D}[0], [x6], x7
        ld1         {v4.D}[1], [x6], x7
        smull       v14.4S, v1.4H, v7.4H
        smull2      v15.4S, v1.8H, v7.8H
        smull       v12.4S, v2.4H, v4.4H
        smull2      v13.4S, v2.8H, v4.8H
        add         v14.4S, v14.4S, v12.4S
        add         v15.4S, v15.4S, v13.4S
        shrn        v8.4H, v14.4S, #14
        shrn2       v8.8H, v15.4S, #14
        sqxtun      v8.8B, v8.8H
        st1         {v8.S}[0], [x0], x1
        st1         {v8.S}[1], [x0], x1
        b.ne        4b
99:     ret
.endm

function ff_hevc_put_qpel_h1_neon_8, export=1
        hevc_put_qpel_hX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_h2_neon_8, export=1
        hevc_put_qpel_hX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_h3_neon_8, export=1
        hevc_put_qpel_hX_neon_8 qpel_filter_3
endfunc

function ff_hevc_put_qpel_uw_h1_neon_8, export=1
        hevc_put_qpel_uw_hX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_uw_h2_neon_8, export=1
        hevc_put_qpel_uw_hX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_uw_h3_neon_8, export=1
        hevc_put_qpel_uw_hX_neon_8 qpel_filter_3
endfunc

function ff_hevc_put_qpel_uw_weight_h1_neon_8, export=1
        hevc_put_qpel_uw_weight_hX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_uw_weight_h2_neon_8, export=1        
        hevc_put_qpel_uw_weight_hX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_uw_weight_h3_neon_8, export=1        
        hevc_put_qpel_uw_weight_hX_neon_8 qpel_filter_3
endfunc

.macro hevc_put_qpel_hXvY_neon_8 filterh filterv
        sub     x2, x2, #4 
        sub     x2, x2, x3, lsl #1 
        sub     x2, x2, x3
        lsl     x1, x1, #1
        mov     x12, x4
        mov     x6, x0
        mov     x7, x2
0:      vextin8                                       
        \filterh    v0
        vextin8
        \filterh    v1
        vextin8
        \filterh    v2
        vextin8
        \filterh    v3
        vextin8
        \filterh    v4
        vextin8
        \filterh    v5
        vextin8
        \filterh    v6
        vextin8
        \filterh    v7
        cmp     x5, #4
        b.eq    4f
8:      subs    x4, x4, #1
        \filterv
        st1         {v8.2D}, [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    8b
        subs    x5, x5, #8
        b.eq    99f
        mov     x4, x12
        add     x6, x6, #16
        mov     x0, x6
        add     x7, x7, #8
        mov     x2, x7
        b       0b
4:      subs    x4, x4, #1
        \filterv
        st1         {v8.1D}, [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    4b
99:     ret
.endm

.macro hevc_put_qpel_uw_hXvY_neon_8 filterh filterv
        sub     x2, x2, #4
        sub     x2, x2, x3, lsl #1
        sub     x2, x2, x3
        mov     x12, x5
        mov     x13, x0
        mov     x14, x2
        cmp     x6, #0
        b.ne    .Lbi\@
0:      vextin8
        \filterh    v0
        vextin8
        \filterh    v1
        vextin8
        \filterh    v2
        vextin8
        \filterh    v3
        vextin8
        \filterh    v4
        vextin8
        \filterh    v5
        vextin8
        \filterh    v6
        vextin8
        \filterh    v7
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filterv
        sqrshrun    v0.8B, v8.8H, #6
        st1         {v0.1D}, [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    8b
        subs    x4, x4, #8
        b.eq     99f
        add     x13, x13, #8
        add     x14, x14, #8
        mov     x5, x12
        mov     x0, x13
        mov     x2, x14
        b       0b
4:      subs    x5, x5, #1
        \filterv
        sqrshrun    v0.8B, v8.8H, #6
        st1         {v0.S}[0], [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    4b
        ret
.Lbi\@:
        lsl     x7, x7, #1
        mov     x15, x6
0:      vextin8
        \filterh    v0
        vextin8
        \filterh    v1
        vextin8
        \filterh    v2
        vextin8
        \filterh    v3
        vextin8
        \filterh    v4
        vextin8
        \filterh    v5
        vextin8
        \filterh    v6
        vextin8
        \filterh    v7
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filterv
        ld1         {v0.2D}, [x6], x7
        sqadd       v0.8H, v0.8H, v8.8H
        sqrshrun    v0.8B, v0.8H, #7
        st1         {v0.1D}, [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x13, x13, #8
        add     x14, x14, #8
        add     x15, x15, #16
        mov     x5, x12
        mov     x0, x13
        mov     x2, x14
        mov     x6, x15
        b       0b
4:      subs    x5, x5, #1
        \filterv
        ld1         {v0.1D}, [x6], x7
        sqadd       v0.4H, v0.4H, v8.4H
        sqrshrun    v0.8B, v0.8H, #7
        st1         {v0.S}[0], [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    4b
99:     ret
.endm

.macro hevc_put_qpel_uw_weight_hXvY_neon_8 filterh filterv
        ldp     w8, w9, [sp]
        ldp     w10, w11, [sp, #8]
        mov     w12, #7
        sub     w12, w12, w7
        lsl     w8, w8, w12
        lsl     w9, w9, w12
        dup         v28.8H, w10
        dup         v29.8H, w8
        sub     x2, x2, #4
        sub     x2, x2, x3, lsl #1
        sub     x2, x2, x3
        mov     x12, x0
        mov     x13, x2
        mov     x14, x5
        cmp     x6, #0
        b.ne    .Lbi\@
0:      vextin8
        \filterh    v0
        vextin8
        \filterh    v1
        vextin8
        \filterh    v2
        vextin8
        \filterh    v3
        vextin8
        \filterh    v4
        vextin8
        \filterh    v5
        vextin8
        \filterh    v6
        vextin8
        \filterh    v7
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filterv
        smull       v14.4S, v29.4H, v8.4H
        smull2      v15.4S, v29.8H, v8.8H
        rshrn       v8.4H, v14.4S, #13
        rshrn2      v8.8H, v15.4S, #13
        sqadd       v8.8H, v8.8H, v28.8H
        sqxtun      v8.8B, v8.8H
        st1         {v8.1D}, [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x12, x12, #8
        add     x13, x13, #8
        mov     x0, x12
        mov     x2, x13
        mov     x5, x14
        b       0b
4:      subs    x5, x5, #1
        \filterv
        smull       v14.4S, v29.4H, v8.4H
        rshrn       v8.4H, v14.4S, #13
        sqadd       v8.8H, v8.8H, v28.8H
        sqxtun      v8.8B, v8.8H
        st1         {v8.S}[0], [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    4b
        ret
.Lbi\@:
        add     w10, w10, w11
        add     w10, w10, #1
        lsl     w10, w10, #13
        dup         v28.4S, w10
        dup         v30.8H, w9
        mov     x7, MAX_PB_DOUBLESIZE
        mov     x11, x6
0:      vextin8
        \filterh    v0
        vextin8
        \filterh    v1
        vextin8
        \filterh    v2
        vextin8
        \filterh    v3
        vextin8
        \filterh    v4
        vextin8
        \filterh    v5
        vextin8
        \filterh    v6
        vextin8
        \filterh    v7
        cmp     x4, #4
        b.eq    4f
8:      subs    x5, x5, #1
        \filterv
        ld1         {v0.2D}, [x6], x7
        smull       v14.4S, v30.4H, v8.4H
        smull2      v15.4S, v30.8H, v8.8H
        smull       v12.4S, v29.4H, v0.4H
        smull2      v13.4S, v29.8H, v0.8H
        add         v14.4S, v14.4S, v12.4S
        add         v15.4S, v15.4S, v13.4S
        add         v14.4S, v14.4S, v28.4S
        add         v15.4S, v15.4S, v28.4S
        rshrn       v8.4H, v14.4S, #14
        rshrn2      v8.8H, v15.4S, #14
        sqxtun      v8.8B, v8.8H
        st1         {v8.1D}, [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    8b
        subs    x4, x4, #8
        b.eq    99f
        add     x11, x11, #16
        add     x12, x12, #8
        add     x13, x13, #8
        mov     x6, x11
        mov     x0, x12
        mov     x2, x13
        mov     x5, x14
        b       0b
4:      subs    x5, x5, #1
        ld1         {v0.1D}, [x6], x7
        smull       v14.4S, v30.4H, v8.4H
        smull       v12.4S, v29.4H, v0.4H
        add         v14.4S, v14.4S, v12.4S
        add         v14.4S, v14.4S, v28.4S
        rshrn       v8.4H, v14.4S, #14
        sqxtun      v8.8B, v8.8H
        st1         {v8.S}[0], [x0], x1
        regshuffle_v8
        vextin8
        \filterh    v7
        b.ne    4b
99:     ret
.endm

function ff_hevc_put_qpel_h1v1_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_1 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_h2v1_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_2 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_h3v1_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_3 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_h1v2_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_1 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_h2v2_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_2 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_h3v2_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_3 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_h1v3_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_1 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_h2v3_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_2 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_h3v3_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_3 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_h1v1_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_1 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_h2v1_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_2 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_h3v1_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_3 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_h1v2_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_1 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_h2v2_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_2 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_h3v2_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_3 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_h1v3_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_1 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_h2v3_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_2 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_h3v3_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_3 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h1v1_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_1 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h2v1_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_2 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h3v1_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_3 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h1v2_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_1 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h2v2_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_2 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h3v2_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_3 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h1v3_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_1 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h2v3_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_2 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_weight_h3v3_neon_8, export=1
        hevc_put_qpel_uw_weight_hXvY_neon_8 qpel_filter_3 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uni_w_neon_8, export=1
        ldr     w10, [sp, #16]                              
        mov     w11, #7
        sub     w5, w11, w5                               
        lsl     w6, w6, w5                                 
        dup         v12.16B, w6
        dup         v14.8H, w7
        mov     x12, x4
        mov     x13, x0
        mov     x14, x2
        cmp     w10, #4
        b.eq    4f
8:      subs    x4, x4, #1 
        ld1         {v0.1D}, [x2], x3
        umull       v8.8H, v0.8B, v12.8B
        urshr       v8.8H, v8.8H, #7
        usqadd      v8.8H, v14.8H
        uqxtn       v0.8B, v8.8H
        st1         {v0.1D}, [x0], x1
        b.ne    8b
        subs    w10, w10, #8
        b.eq    99f
        add     x13, x13, #8
        add     x14, x14, #8
        mov     x4, x12
        mov     x0, x13
        mov     x2, x14
        cmp     w10, #4
        b.ne    8b
4:      subs    x4, x4, #2
        ld1         {v0.S}[0], [x2], x3
        ld1         {v0.S}[1], [x2], x3
        umull       v8.8H, v0.8B, v12.8B
        urshr       v8.8H, v8.8H, #7
        usqadd      v8.8H, v14.8H
        uqxtn       v0.8B, v8.8H
        st1         {v0.S}[0], [x0], x1
        st1         {v0.S}[1], [x0], x1
        b.ne    4b
99:     ret
endfunc

function ff_hevc_put_qpel_bi_w_neon_8, export=1
        ldp     w8, w9, [sp]
        ldr     w10, [sp, #8]
        mov     w11, #7
        sub     w11, w11, w6
        lsl     w7, w7, w11
        lsl     w8, w8, w11
        ldr     w6, [sp, #32]
        add     w11, w9, w10
        add     w11, w11, #1
        lsl     w11, w11, #13
        dup         v12.8H, w7
        dup         v13.8H, w8
        dup         v14.4S, w11
        mov     x7, MAX_PB_DOUBLESIZE
        mov     x10, x4
        mov     x11, x0
        mov     x12, x5
        mov     x13, x2
        cmp     w6, #4
        b.eq    4f
8:      subs    x5, x5, #1
        ld1         {v0.1D}, [x2], x3
        ld1         {v1.2D}, [x4], x7
        ushll       v0.8H, v0.8B, #6
        smull       v4.4S, v0.4H, v13.4H
        smull2      v5.4S, v0.8H, v13.8H
        smull       v6.4S, v1.4H, v12.4H
        smull2      v7.4S, v1.8H, v12.8H
        add         v4.4S, v4.4S, v6.4S
        add         v5.4S, v5.4S, v7.4S
        add         v4.4S, v4.4S, v14.4S
        add         v5.4S, v5.4S, v14.4S
        shrn        v0.4H, v4.4S, #14
        shrn2       v0.8H, v5.4S, #14
        sqxtun      v0.8B, v0.8H
        st1         {v0.1D}, [x0], x1
        b.ne    8b
        subs    w6, w6, #8
        b.eq    99f
        add     x11, x11, #8
        add     x10, x10, #16
        add     x13, x13, #8
        mov     x4, x10
        mov     x0, x11
        mov     x5, x12
        mov     x2, x13
        cmp     w6, #4
        b.ne    8b
4:      subs    x5, x5, #2
        ld1         {v0.S}[0], [x2], x3
        ld1         {v2.S}[0], [x2], x3
        ld1         {v1.1D}, [x4], x7
        ld1         {v3.1D}, [x4], x7
        ushll       v0.8H, v0.8B, #6
        ushll       v2.8H, v2.8B, #6
        smull       v4.4S, v0.4H, v13.4H
        smull       v6.4S, v1.4H, v12.4H
        smull       v5.4S, v2.4H, v13.4H
        smull       v7.4S, v3.4H, v12.4H
        add         v4.4S, v4.4S, v6.4S
        add         v4.4S, v4.4S, v14.4S
        add         v5.4S, v5.4S, v7.4S
        add         v5.4S, v5.4S, v14.4S
        shrn        v0.4H, v4.4S, #14
        shrn        v1.4H, v5.4S, #14
        sqxtun      v0.8B, v0.8H
        sqxtun      v1.8B, v1.8H
        st1         {v0.S}[0], [x0], x1
        st1         {v1.S}[0], [x0], x1
        b.ne    4b
99:     ret
endfunc
