/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/aarch64/asm.S"
#include "neon.S"

.macro  h264_loop_filter_start
        cmp             w2,  #0
        ldr             w6,  [x4]
        ccmp            w3,  #0, #0, ne
        mov             v24.S[0], w6
        and             w6,  w6,  w6,  lsl #16
        b.eq            1f
        ands            w6,  w6,  w6,  lsl #8
        b.ge            2f
1:
        ret
2:
.endm

.macro  h264_loop_filter_luma
        dup             v22.16B, w2                     // alpha
        uxtl            v24.8H,  v24.8B
        uabd            v21.16B, v16.16B, v0.16B        // abs(p0 - q0)
        uxtl            v24.4S,  v24.4H
        uabd            v28.16B, v18.16B, v16.16B       // abs(p1 - p0)
        sli             v24.8H,  v24.8H,  #8
        uabd            v30.16B, v2.16B,  v0.16B        // abs(q1 - q0)
        sli             v24.4S,  v24.4S,  #16
        cmhi            v21.16B, v22.16B, v21.16B       // < alpha
        dup             v22.16B, w3                     // beta
        cmlt            v23.16B, v24.16B, #0
        cmhi            v28.16B, v22.16B, v28.16B       // < beta
        cmhi            v30.16B, v22.16B, v30.16B       // < beta
        bic             v21.16B, v21.16B, v23.16B
        uabd            v17.16B, v20.16B, v16.16B       // abs(p2 - p0)
        and             v21.16B, v21.16B, v28.16B
        uabd            v19.16B,  v4.16B,  v0.16B       // abs(q2 - q0)
        cmhi            v17.16B, v22.16B, v17.16B       // < beta
        and             v21.16B, v21.16B, v30.16B
        cmhi            v19.16B, v22.16B, v19.16B       // < beta
        and             v17.16B, v17.16B, v21.16B
        and             v19.16B, v19.16B, v21.16B
        and             v24.16B, v24.16B, v21.16B
        urhadd          v28.16B, v16.16B,  v0.16B
        sub             v21.16B, v24.16B, v17.16B
        uqadd           v23.16B, v18.16B, v24.16B
        uhadd           v20.16B, v20.16B, v28.16B
        sub             v21.16B, v21.16B, v19.16B
        uhadd           v28.16B,  v4.16B, v28.16B
        umin            v23.16B, v23.16B, v20.16B
        uqsub           v22.16B, v18.16B, v24.16B
        uqadd           v4.16B,   v2.16B, v24.16B
        umax            v23.16B, v23.16B, v22.16B
        uqsub           v22.16B,  v2.16B, v24.16B
        umin            v28.16B,  v4.16B, v28.16B
        uxtl            v4.8H,    v0.8B
        umax            v28.16B, v28.16B, v22.16B
        uxtl2           v20.8H,   v0.16B
        usubw           v4.8H,    v4.8H,  v16.8B
        usubw2          v20.8H,  v20.8H,  v16.16B
        shl             v4.8H,    v4.8H,  #2
        shl             v20.8H,  v20.8H,  #2
        uaddw           v4.8H,    v4.8H,  v18.8B
        uaddw2          v20.8H,  v20.8H,  v18.16B
        usubw           v4.8H,    v4.8H,   v2.8B
        usubw2          v20.8H,  v20.8H,   v2.16B
        rshrn           v4.8B,    v4.8H,  #3
        rshrn2          v4.16B,  v20.8H,  #3
        bsl             v17.16B, v23.16B, v18.16B
        bsl             v19.16B, v28.16B,  v2.16B
        neg             v23.16B, v21.16B
        uxtl            v28.8H,  v16.8B
        smin            v4.16B,   v4.16B, v21.16B
        uxtl2           v21.8H,  v16.16B
        smax            v4.16B,   v4.16B, v23.16B
        uxtl            v22.8H,   v0.8B
        uxtl2           v24.8H,   v0.16B
        saddw           v28.8H,  v28.8H,  v4.8B
        saddw2          v21.8H,  v21.8H,  v4.16B
        ssubw           v22.8H,  v22.8H,  v4.8B
        ssubw2          v24.8H,  v24.8H,  v4.16B
        sqxtun          v16.8B,  v28.8H
        sqxtun2         v16.16B, v21.8H
        sqxtun          v0.8B,   v22.8H
        sqxtun2         v0.16B,  v24.8H
.endm

function ff_h264_v_loop_filter_luma_neon, export=1
        h264_loop_filter_start
        sxtw            x1,  w1

        ld1             {v0.16B},  [x0], x1
        ld1             {v2.16B},  [x0], x1
        ld1             {v4.16B},  [x0], x1
        sub             x0,  x0,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #1
        ld1             {v20.16B},  [x0], x1
        ld1             {v18.16B},  [x0], x1
        ld1             {v16.16B},  [x0], x1

        h264_loop_filter_luma

        sub             x0,  x0,  x1, lsl #1
        st1             {v17.16B},  [x0], x1
        st1             {v16.16B}, [x0], x1
        st1             {v0.16B},  [x0], x1
        st1             {v19.16B}, [x0]

        ret
endfunc

function ff_h264_h_loop_filter_luma_neon, export=1
        h264_loop_filter_start

        sub             x0,  x0,  #4
        ld1             {v6.8B},  [x0], x1
        ld1             {v20.8B}, [x0], x1
        ld1             {v18.8B}, [x0], x1
        ld1             {v16.8B}, [x0], x1
        ld1             {v0.8B},  [x0], x1
        ld1             {v2.8B},  [x0], x1
        ld1             {v4.8B},  [x0], x1
        ld1             {v26.8B}, [x0], x1
        ld1             {v6.D}[1],  [x0], x1
        ld1             {v20.D}[1], [x0], x1
        ld1             {v18.D}[1], [x0], x1
        ld1             {v16.D}[1], [x0], x1
        ld1             {v0.D}[1],  [x0], x1
        ld1             {v2.D}[1],  [x0], x1
        ld1             {v4.D}[1],  [x0], x1
        ld1             {v26.D}[1], [x0], x1

        transpose_8x16B v6, v20, v18, v16, v0, v2, v4, v26, v21, v23

        h264_loop_filter_luma

        transpose_4x16B v17, v16, v0, v19, v21, v23, v25, v27

        sub             x0,  x0,  x1, lsl #4
        add             x0,  x0,  #2
        st1             {v17.S}[0],  [x0], x1
        st1             {v16.S}[0], [x0], x1
        st1             {v0.S}[0],  [x0], x1
        st1             {v19.S}[0], [x0], x1
        st1             {v17.S}[1],  [x0], x1
        st1             {v16.S}[1], [x0], x1
        st1             {v0.S}[1],  [x0], x1
        st1             {v19.S}[1], [x0], x1
        st1             {v17.S}[2],  [x0], x1
        st1             {v16.S}[2], [x0], x1
        st1             {v0.S}[2],  [x0], x1
        st1             {v19.S}[2], [x0], x1
        st1             {v17.S}[3],  [x0], x1
        st1             {v16.S}[3], [x0], x1
        st1             {v0.S}[3],  [x0], x1
        st1             {v19.S}[3], [x0], x1

        ret
endfunc

.macro  h264_loop_filter_chroma
        dup             v22.8B, w2              // alpha
        uxtl            v24.8H, v24.8B
        uabd            v26.8B, v16.8B, v0.8B   // abs(p0 - q0)
        uxtl            v4.8H,  v0.8B
        uabd            v28.8B, v18.8B, v16.8B  // abs(p1 - p0)
        usubw           v4.8H,  v4.8H,  v16.8B
        sli             v24.8H, v24.8H, #8
        shl             v4.8H,  v4.8H,  #2
        uabd            v30.8B, v2.8B,  v0.8B   // abs(q1 - q0)
        uaddw           v4.8H,  v4.8H,  v18.8B
        cmhi            v26.8B, v22.8B, v26.8B  // < alpha
        usubw           v4.8H,  v4.8H,  v2.8B
        dup             v22.8B, w3              // beta
        rshrn           v4.8B,  v4.8H,  #3
        cmhi            v28.8B, v22.8B, v28.8B  // < beta
        cmhi            v30.8B, v22.8B, v30.8B  // < beta
        smin            v4.8B,  v4.8B,  v24.8B
        neg             v25.8B, v24.8B
        and             v26.8B, v26.8B, v28.8B
        smax            v4.8B,  v4.8B,  v25.8B
        and             v26.8B, v26.8B, v30.8B
        uxtl            v22.8H, v0.8B
        and             v4.8B,  v4.8B,  v26.8B
        uxtl            v28.8H, v16.8B
        saddw           v28.8H, v28.8H, v4.8B
        ssubw           v22.8H, v22.8H, v4.8B
        sqxtun          v16.8B, v28.8H
        sqxtun          v0.8B,  v22.8H
.endm

function ff_h264_v_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             x0,  x0,  x1, lsl #1
        ld1             {v18.8B}, [x0], x1
        ld1             {v16.8B}, [x0], x1
        ld1             {v0.8B},  [x0], x1
        ld1             {v2.8B},  [x0]

        h264_loop_filter_chroma

        sub             x0,  x0,  x1, lsl #1
        st1             {v16.8B}, [x0], x1
        st1             {v0.8B},  [x0], x1

        ret
endfunc

function ff_h264_h_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             x0,  x0,  #2
        ld1             {v18.S}[0], [x0], x1
        ld1             {v16.S}[0], [x0], x1
        ld1             {v0.S}[0],  [x0], x1
        ld1             {v2.S}[0],  [x0], x1
        ld1             {v18.S}[1], [x0], x1
        ld1             {v16.S}[1], [x0], x1
        ld1             {v0.S}[1],  [x0], x1
        ld1             {v2.S}[1],  [x0], x1

        transpose_4x8B  v18, v16, v0, v2, v28, v29, v30, v31

        h264_loop_filter_chroma

        transpose_4x8B  v18, v16, v0, v2, v28, v29, v30, v31

        sub             x0,  x0,  x1, lsl #3
        st1             {v18.S}[0], [x0], x1
        st1             {v16.S}[0], [x0], x1
        st1             {v0.S}[0],  [x0], x1
        st1             {v2.S}[0],  [x0], x1
        st1             {v18.S}[1], [x0], x1
        st1             {v16.S}[1], [x0], x1
        st1             {v0.S}[1],  [x0], x1
        st1             {v2.S}[1],  [x0], x1

        ret
endfunc

.macro  biweight_16     macs, macd
        dup             v0.16B,  w5
        dup             v1.16B,  w6
        mov             v4.16B,  v16.16B
        mov             v6.16B,  v16.16B
1:      subs            w3,  w3,  #2
        ld1             {v20.16B}, [x0], x2
        \macd           v4.8H,   v0.8B,  v20.8B
        \macd\()2       v6.8H,   v0.16B, v20.16B
        ld1             {v22.16B}, [x1], x2
        \macs           v4.8H,   v1.8B,  v22.8B
        \macs\()2       v6.8H,   v1.16B, v22.16B
        mov             v24.16B, v16.16B
        ld1             {v28.16B}, [x0], x2
        mov             v26.16B, v16.16B
        \macd           v24.8H,  v0.8B,  v28.8B
        \macd\()2       v26.8H,  v0.16B, v28.16B
        ld1             {v30.16B}, [x1], x2
        \macs           v24.8H,  v1.8B,  v30.8B
        \macs\()2       v26.8H,  v1.16B, v30.16B
        sshl            v4.8H,   v4.8H,  v18.8H
        sshl            v6.8H,   v6.8H,  v18.8H
        sqxtun          v4.8B,   v4.8H
        sqxtun2         v4.16B,  v6.8H
        sshl            v24.8H,  v24.8H, v18.8H
        sshl            v26.8H,  v26.8H, v18.8H
        sqxtun          v24.8B,  v24.8H
        sqxtun2         v24.16B, v26.8H
        mov             v6.16B,  v16.16B
        st1             {v4.16B},  [x7], x2
        mov             v4.16B,  v16.16B
        st1             {v24.16B}, [x7], x2
        b.ne            1b
        ret
.endm

.macro  biweight_8      macs, macd
        dup             v0.8B,  w5
        dup             v1.8B,  w6
        mov             v2.16B,  v16.16B
        mov             v20.16B, v16.16B
1:      subs            w3,  w3,  #2
        ld1             {v4.8B}, [x0], x2
        \macd           v2.8H,  v0.8B,  v4.8B
        ld1             {v5.8B}, [x1], x2
        \macs           v2.8H,  v1.8B,  v5.8B
        ld1             {v6.8B}, [x0], x2
        \macd           v20.8H, v0.8B,  v6.8B
        ld1             {v7.8B}, [x1], x2
        \macs           v20.8H, v1.8B,  v7.8B
        sshl            v2.8H,  v2.8H,  v18.8H
        sqxtun          v2.8B,  v2.8H
        sshl            v20.8H, v20.8H, v18.8H
        sqxtun          v4.8B,  v20.8H
        mov             v20.16B, v16.16B
        st1             {v2.8B}, [x7], x2
        mov             v2.16B,  v16.16B
        st1             {v4.8B}, [x7], x2
        b.ne            1b
        ret
.endm

.macro  biweight_4      macs, macd
        dup             v0.8B,  w5
        dup             v1.8B,  w6
        mov             v2.16B, v16.16B
        mov             v20.16B,v16.16B
1:      subs            w3,  w3,  #4
        ld1             {v4.S}[0], [x0], x2
        ld1             {v4.S}[1], [x0], x2
        \macd           v2.8H,  v0.8B,  v4.8B
        ld1             {v5.S}[0], [x1], x2
        ld1             {v5.S}[1], [x1], x2
        \macs           v2.8H,  v1.8B,  v5.8B
        b.lt            2f
        ld1             {v6.S}[0], [x0], x2
        ld1             {v6.S}[1], [x0], x2
        \macd           v20.8H, v0.8B,  v6.8B
        ld1             {v7.S}[0], [x1], x2
        ld1             {v7.S}[1], [x1], x2
        \macs           v20.8H, v1.8B,  v7.8B
        sshl            v2.8H,  v2.8H,  v18.8H
        sqxtun          v2.8B,  v2.8H
        sshl            v20.8H, v20.8H, v18.8H
        sqxtun          v4.8B,  v20.8H
        mov             v20.16B, v16.16B
        st1             {v2.S}[0], [x7], x2
        st1             {v2.S}[1], [x7], x2
        mov             v2.16B,  v16.16B
        st1             {v4.S}[0], [x7], x2
        st1             {v4.S}[1], [x7], x2
        b.ne            1b
        ret
2:      sshl            v2.8H,  v2.8H,  v18.8H
        sqxtun          v2.8B,  v2.8H
        st1             {v2.S}[0], [x7], x2
        st1             {v2.S}[1], [x7], x2
        ret
.endm

.macro  biweight_func   w
function ff_biweight_h264_pixels_\w\()_neon, export=1
        sxtw            x2,  w2
        lsr             w8,  w5,  #31
        add             w7,  w7,  #1
        eor             w8,  w8,  w6,  lsr #30
        orr             w7,  w7,  #1
        dup             v18.8H,   w4
        lsl             w7,  w7,  w4
        not             v18.16B,  v18.16B
        dup             v16.8H,   w7
        mov             x7,  x0
        cbz             w8,  10f
        subs            w8,  w8,  #1
        b.eq            20f
        subs            w8,  w8,  #1
        b.eq            30f
        b               40f
10:     biweight_\w     umlal, umlal
20:     neg             w5, w5
        biweight_\w     umlal, umlsl
30:     neg             w5, w5
        neg             w6, w6
        biweight_\w     umlsl, umlsl
40:     neg             w6, w6
        biweight_\w     umlsl, umlal
endfunc
.endm

        biweight_func   16
        biweight_func   8
        biweight_func   4

.macro  weight_16       add
        dup             v0.16B,  w4
1:      subs            w2,  w2,  #2
        ld1             {v20.16B}, [x0], x1
        umull           v4.8H,   v0.8B,  v20.8B
        umull2          v6.8H,   v0.16B, v20.16B
        ld1             {v28.16B}, [x0], x1
        umull           v24.8H,  v0.8B,  v28.8B
        umull2          v26.8H,  v0.16B, v28.16B
        \add            v4.8H,   v16.8H, v4.8H
        srshl           v4.8H,   v4.8H,  v18.8H
        \add            v6.8H,   v16.8H, v6.8H
        srshl           v6.8H,   v6.8H,  v18.8H
        sqxtun          v4.8B,   v4.8H
        sqxtun2         v4.16B,  v6.8H
        \add            v24.8H,  v16.8H, v24.8H
        srshl           v24.8H,  v24.8H, v18.8H
        \add            v26.8H,  v16.8H, v26.8H
        srshl           v26.8H,  v26.8H, v18.8H
        sqxtun          v24.8B,  v24.8H
        sqxtun2         v24.16B, v26.8H
        st1             {v4.16B},  [x5], x1
        st1             {v24.16B}, [x5], x1
        b.ne            1b
        ret
.endm

.macro  weight_8        add
        dup             v0.8B,  w4
1:      subs            w2,  w2,  #2
        ld1             {v4.8B}, [x0], x1
        umull           v2.8H,  v0.8B,  v4.8B
        ld1             {v6.8B}, [x0], x1
        umull           v20.8H, v0.8B,  v6.8B
        \add            v2.8H,  v16.8H,  v2.8H
        srshl           v2.8H,  v2.8H,  v18.8H
        sqxtun          v2.8B,  v2.8H
        \add            v20.8H, v16.8H,  v20.8H
        srshl           v20.8H, v20.8H, v18.8H
        sqxtun          v4.8B,  v20.8H
        st1             {v2.8B}, [x5], x1
        st1             {v4.8B}, [x5], x1
        b.ne            1b
        ret
.endm

.macro  weight_4        add
        dup             v0.8B,  w4
1:      subs            w2,  w2,  #4
        ld1             {v4.S}[0], [x0], x1
        ld1             {v4.S}[1], [x0], x1
        umull           v2.8H,  v0.8B,  v4.8B
        b.lt            2f
        ld1             {v6.S}[0], [x0], x1
        ld1             {v6.S}[1], [x0], x1
        umull           v20.8H, v0.8B,  v6.8B
        \add            v2.8H,  v16.8H,  v2.8H
        srshl           v2.8H,  v2.8H,  v18.8H
        sqxtun          v2.8B,  v2.8H
        \add            v20.8H, v16.8H,  v20.8H
        srshl           v20.8H, v20.8h, v18.8H
        sqxtun          v4.8B,  v20.8H
        st1             {v2.S}[0], [x5], x1
        st1             {v2.S}[1], [x5], x1
        st1             {v4.S}[0], [x5], x1
        st1             {v4.S}[1], [x5], x1
        b.ne            1b
        ret
2:      \add            v2.8H,  v16.8H,  v2.8H
        srshl           v2.8H,  v2.8H,  v18.8H
        sqxtun          v2.8B,  v2.8H
        st1             {v2.S}[0], [x5], x1
        st1             {v2.S}[1], [x5], x1
        ret
.endm

.macro  weight_func     w
function ff_weight_h264_pixels_\w\()_neon, export=1
        sxtw            x1,  w1
        cmp             w3,  #1
        mov             w6,  #1
        lsl             w5,  w5,  w3
        dup             v16.8H,  w5
        mov             x5,  x0
        b.le            20f
        sub             w6,  w6,  w3
        dup             v18.8H,  w6
        cmp             w4, #0
        b.lt            10f
        weight_\w       shadd
10:     neg             w4,  w4
        weight_\w       shsub
20:     neg             w6,  w3
        dup             v18.8H,  w6
        cmp             w4,  #0
        b.lt            10f
        weight_\w       add
10:     neg             w4,  w4
        weight_\w       sub
endfunc
.endm

        weight_func     16
        weight_func     8
        weight_func     4
