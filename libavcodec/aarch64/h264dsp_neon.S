/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
 * Copyright (c) 2014 Janne Grunau <janne-libav@jannau.net>
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

.macro  h264_loop_filter_start
        cmp             w2,  #0
        ldr             w6,  [x4]
        ccmp            w3,  #0, #0, ne
        mov             v24.S[0], w6
        and             w8,  w6,  w6,  lsl #16
        b.eq            1f
        ands            w8,  w8,  w8,  lsl #8
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
        and             v21.16B, v21.16B, v30.16B      // < beta
        shrn            v30.8b,  v21.8h,  #4
        mov             x7, v30.d[0]
        cmhi            v17.16B, v22.16B, v17.16B       // < beta
        cmhi            v19.16B, v22.16B, v19.16B       // < beta
        cbz             x7,  9f
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
9:
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
9:
        ret
endfunc


.macro h264_loop_filter_start_intra
        orr             w4,  w2,  w3
        cbnz            w4,  1f
        ret
1:
        dup             v30.16b, w2                // alpha
        dup             v31.16b, w3                // beta
.endm

.macro h264_loop_filter_luma_intra
        uabd            v16.16b, v7.16b,  v0.16b        // abs(p0 - q0)
        uabd            v17.16b, v6.16b,  v7.16b        // abs(p1 - p0)
        uabd            v18.16b, v1.16b,  v0.16b        // abs(q1 - q0)
        cmhi            v19.16b, v30.16b, v16.16b       // < alpha
        cmhi            v17.16b, v31.16b, v17.16b       // < beta
        cmhi            v18.16b, v31.16b, v18.16b       // < beta

        movi            v29.16b, #2
        ushr            v30.16b, v30.16b, #2            // alpha >> 2
        add             v30.16b, v30.16b, v29.16b       // (alpha >> 2) + 2
        cmhi            v16.16b, v30.16b, v16.16b       // < (alpha >> 2) + 2

        and             v19.16b, v19.16b, v17.16b
        and             v19.16b, v19.16b, v18.16b
        shrn            v20.8b,  v19.8h,  #4
        mov             x4, v20.d[0]
        cbz             x4, 9f

        ushll           v20.8h,  v6.8b,   #1
        ushll           v22.8h,  v1.8b,   #1
        ushll2          v21.8h,  v6.16b,  #1
        ushll2          v23.8h,  v1.16b,  #1
        uaddw           v20.8h,  v20.8h,  v7.8b
        uaddw           v22.8h,  v22.8h,  v0.8b
        uaddw2          v21.8h,  v21.8h,  v7.16b
        uaddw2          v23.8h,  v23.8h,  v0.16b
        uaddw           v20.8h,  v20.8h,  v1.8b
        uaddw           v22.8h,  v22.8h,  v6.8b
        uaddw2          v21.8h,  v21.8h,  v1.16b
        uaddw2          v23.8h,  v23.8h,  v6.16b

        rshrn           v24.8b,  v20.8h,  #2 // p0'_1
        rshrn           v25.8b,  v22.8h,  #2 // q0'_1
        rshrn2          v24.16b, v21.8h,  #2 // p0'_1
        rshrn2          v25.16b, v23.8h,  #2 // q0'_1

        uabd            v17.16b, v5.16b,  v7.16b        // abs(p2 - p0)
        uabd            v18.16b, v2.16b,  v0.16b        // abs(q2 - q0)
        cmhi            v17.16b, v31.16b, v17.16b       // < beta
        cmhi            v18.16b, v31.16b, v18.16b       // < beta

        and             v17.16b, v16.16b, v17.16b  // if_2 && if_3
        and             v18.16b, v16.16b, v18.16b  // if_2 && if_4

        not             v30.16b, v17.16b
        not             v31.16b, v18.16b

        and             v30.16b, v30.16b, v19.16b  // if_1 && !(if_2 && if_3)
        and             v31.16b, v31.16b, v19.16b  // if_1 && !(if_2 && if_4)

        and             v17.16b, v19.16b, v17.16b  // if_1 && if_2 && if_3
        and             v18.16b, v19.16b, v18.16b  // if_1 && if_2 && if_4

        //calc            p, v7, v6, v5, v4, v17, v7, v6, v5, v4
        uaddl           v26.8h,  v5.8b,   v7.8b
        uaddl2          v27.8h,  v5.16b,  v7.16b
        uaddw           v26.8h,  v26.8h,  v0.8b
        uaddw2          v27.8h,  v27.8h,  v0.16b
        add             v20.8h,  v20.8h,  v26.8h
        add             v21.8h,  v21.8h,  v27.8h
        uaddw           v20.8h,  v20.8h,  v0.8b
        uaddw2          v21.8h,  v21.8h,  v0.16b
        rshrn           v20.8b,  v20.8h,  #3 // p0'_2
        rshrn2          v20.16b, v21.8h,  #3 // p0'_2
        uaddw           v26.8h,  v26.8h,  v6.8b
        uaddw2          v27.8h,  v27.8h,  v6.16b
        rshrn           v21.8b,  v26.8h,  #2 // p1'_2
        rshrn2          v21.16b, v27.8h,  #2 // p1'_2
        uaddl           v28.8h,  v4.8b,   v5.8b
        uaddl2          v29.8h,  v4.16b,  v5.16b
        shl             v28.8h,  v28.8h,  #1
        shl             v29.8h,  v29.8h,  #1
        add             v28.8h,  v28.8h,  v26.8h
        add             v29.8h,  v29.8h,  v27.8h
        rshrn           v19.8b,  v28.8h,  #3 // p2'_2
        rshrn2          v19.16b, v29.8h,  #3 // p2'_2

        //calc            q, v0, v1, v2, v3, v18, v0, v1, v2, v3
        uaddl           v26.8h,  v2.8b,   v0.8b
        uaddl2          v27.8h,  v2.16b,  v0.16b
        uaddw           v26.8h,  v26.8h,  v7.8b
        uaddw2          v27.8h,  v27.8h,  v7.16b
        add             v22.8h,  v22.8h,  v26.8h
        add             v23.8h,  v23.8h,  v27.8h
        uaddw           v22.8h,  v22.8h,  v7.8b
        uaddw2          v23.8h,  v23.8h,  v7.16b
        rshrn           v22.8b,  v22.8h,  #3 // q0'_2
        rshrn2          v22.16b, v23.8h,  #3 // q0'_2
        uaddw           v26.8h,  v26.8h,  v1.8b
        uaddw2          v27.8h,  v27.8h,  v1.16b
        rshrn           v23.8b,  v26.8h,  #2 // q1'_2
        rshrn2          v23.16b, v27.8h,  #2 // q1'_2
        uaddl           v28.8h,  v2.8b,   v3.8b
        uaddl2          v29.8h,  v2.16b,  v3.16b
        shl             v28.8h,  v28.8h,  #1
        shl             v29.8h,  v29.8h,  #1
        add             v28.8h,  v28.8h,  v26.8h
        add             v29.8h,  v29.8h,  v27.8h
        rshrn           v26.8b,  v28.8h,  #3 // q2'_2
        rshrn2          v26.16b, v29.8h,  #3 // q2'_2

        bit             v7.16b,  v24.16b, v30.16b  // p0'_1
        bit             v0.16b,  v25.16b, v31.16b  // q0'_1
        bit             v7.16b,  v20.16b, v17.16b  // p0'_2
        bit             v6.16b,  v21.16b, v17.16b  // p1'_2
        bit             v5.16b,  v19.16b, v17.16b  // p2'_2
        bit             v0.16b,  v22.16b, v18.16b  // q0'_2
        bit             v1.16b,  v23.16b, v18.16b  // q1'_2
        bit             v2.16b,  v26.16b, v18.16b  // q2'_2
.endm

function ff_h264_v_loop_filter_luma_intra_neon, export=1
        h264_loop_filter_start_intra

        ld1             {v0.16b},  [x0], x1 // q0
        ld1             {v1.16b},  [x0], x1 // q1
        ld1             {v2.16b},  [x0], x1 // q2
        ld1             {v3.16b},  [x0], x1 // q3
        sub             x0,  x0,  x1, lsl #3
        ld1             {v4.16b},  [x0], x1 // p3
        ld1             {v5.16b},  [x0], x1 // p2
        ld1             {v6.16b},  [x0], x1 // p1
        ld1             {v7.16b},  [x0]     // p0

        h264_loop_filter_luma_intra

        sub             x0,  x0,  x1, lsl #1
        st1             {v5.16b}, [x0], x1  // p2
        st1             {v6.16b}, [x0], x1  // p1
        st1             {v7.16b}, [x0], x1  // p0
        st1             {v0.16b}, [x0], x1  // q0
        st1             {v1.16b}, [x0], x1  // q1
        st1             {v2.16b}, [x0]      // q2
9:
        ret
endfunc

function ff_h264_h_loop_filter_luma_intra_neon, export=1
        h264_loop_filter_start_intra

        sub             x0,  x0,  #4
        ld1             {v4.8b},  [x0], x1
        ld1             {v5.8b},  [x0], x1
        ld1             {v6.8b},  [x0], x1
        ld1             {v7.8b},  [x0], x1
        ld1             {v0.8b},  [x0], x1
        ld1             {v1.8b},  [x0], x1
        ld1             {v2.8b},  [x0], x1
        ld1             {v3.8b},  [x0], x1
        ld1             {v4.d}[1],  [x0], x1
        ld1             {v5.d}[1],  [x0], x1
        ld1             {v6.d}[1],  [x0], x1
        ld1             {v7.d}[1],  [x0], x1
        ld1             {v0.d}[1],  [x0], x1
        ld1             {v1.d}[1],  [x0], x1
        ld1             {v2.d}[1],  [x0], x1
        ld1             {v3.d}[1],  [x0], x1

        transpose_8x16B v4, v5, v6, v7, v0, v1, v2, v3, v21, v23

        h264_loop_filter_luma_intra

        transpose_8x16B v4, v5, v6, v7, v0, v1, v2, v3, v21, v23

        sub             x0,  x0,  x1, lsl #4
        st1             {v4.8b},  [x0], x1
        st1             {v5.8b},  [x0], x1
        st1             {v6.8b},  [x0], x1
        st1             {v7.8b},  [x0], x1
        st1             {v0.8b},  [x0], x1
        st1             {v1.8b},  [x0], x1
        st1             {v2.8b},  [x0], x1
        st1             {v3.8b},  [x0], x1
        st1             {v4.d}[1],  [x0], x1
        st1             {v5.d}[1],  [x0], x1
        st1             {v6.d}[1],  [x0], x1
        st1             {v7.d}[1],  [x0], x1
        st1             {v0.d}[1],  [x0], x1
        st1             {v1.d}[1],  [x0], x1
        st1             {v2.d}[1],  [x0], x1
        st1             {v3.d}[1],  [x0], x1
9:
        ret
endfunc

.macro  h264_loop_filter_chroma
        dup             v22.8B, w2              // alpha
        dup             v23.8B, w3              // beta
        uxtl            v24.8H, v24.8B
        uabd            v26.8B, v16.8B, v0.8B   // abs(p0 - q0)
        uabd            v28.8B, v18.8B, v16.8B  // abs(p1 - p0)
        uabd            v30.8B, v2.8B,  v0.8B   // abs(q1 - q0)
        cmhi            v26.8B, v22.8B, v26.8B  // < alpha
        cmhi            v28.8B, v23.8B, v28.8B  // < beta
        cmhi            v30.8B, v23.8B, v30.8B  // < beta
        uxtl            v4.8H,  v0.8B
        and             v26.8B, v26.8B, v28.8B
        usubw           v4.8H,  v4.8H,  v16.8B
        and             v26.8B, v26.8B, v30.8B
        shl             v4.8H,  v4.8H,  #2
        mov             x8,  v26.d[0]
        sli             v24.8H, v24.8H, #8
        uaddw           v4.8H,  v4.8H,  v18.8B
        cbz             x8,  9f
        usubw           v4.8H,  v4.8H,  v2.8B
        rshrn           v4.8B,  v4.8H,  #3
        smin            v4.8B,  v4.8B,  v24.8B
        neg             v25.8B, v24.8B
        smax            v4.8B,  v4.8B,  v25.8B
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
9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             x0,  x0,  #2
h_loop_filter_chroma420:
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
9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma422_neon, export=1
        h264_loop_filter_start
        add             x5,  x0,  x1
        sub             x0,  x0,  #2
        add             x1,  x1,  x1
        mov             x7,  x30
        bl              h_loop_filter_chroma420
        mov             x30, x7
        sub             x0,  x5,  #2
        mov             v24.s[0], w6
        b               h_loop_filter_chroma420
endfunc

.macro h264_loop_filter_chroma_intra
        uabd            v26.8b,  v16.8b,  v17.8b  // abs(p0 - q0)
        uabd            v27.8b,  v18.8b,  v16.8b  // abs(p1 - p0)
        uabd            v28.8b,  v19.8b,  v17.8b  // abs(q1 - q0)
        cmhi            v26.8b,  v30.8b,  v26.8b  // < alpha
        cmhi            v27.8b,  v31.8b,  v27.8b  // < beta
        cmhi            v28.8b,  v31.8b,  v28.8b  // < beta
        and             v26.8b,  v26.8b,  v27.8b
        and             v26.8b,  v26.8b,  v28.8b
        mov             x2, v26.d[0]

        ushll           v4.8h,   v18.8b,  #1
        ushll           v6.8h,   v19.8b,  #1
        cbz             x2, 9f
        uaddl           v20.8h,  v16.8b,  v19.8b
        uaddl           v22.8h,  v17.8b,  v18.8b
        add             v20.8h,  v20.8h,  v4.8h
        add             v22.8h,  v22.8h,  v6.8h
        uqrshrn         v24.8b,  v20.8h,  #2
        uqrshrn         v25.8b,  v22.8h,  #2
        bit             v16.8b,  v24.8b,  v26.8b
        bit             v17.8b,  v25.8b,  v26.8b
.endm

function ff_h264_v_loop_filter_chroma_intra_neon, export=1
        h264_loop_filter_start_intra

        sub             x0,  x0,  x1, lsl #1
        ld1             {v18.8b}, [x0], x1
        ld1             {v16.8b}, [x0], x1
        ld1             {v17.8b}, [x0], x1
        ld1             {v19.8b}, [x0]

        h264_loop_filter_chroma_intra

        sub             x0,  x0,  x1, lsl #1
        st1             {v16.8b}, [x0], x1
        st1             {v17.8b}, [x0], x1

9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma_mbaff_intra_neon, export=1
        h264_loop_filter_start_intra

        sub             x4,  x0,  #2
        sub             x0,  x0,  #1
        ld1             {v18.8b}, [x4], x1
        ld1             {v16.8b}, [x4], x1
        ld1             {v17.8b}, [x4], x1
        ld1             {v19.8b}, [x4], x1

        transpose_4x8B v18, v16, v17, v19, v26, v27, v28, v29

        h264_loop_filter_chroma_intra

        st2             {v16.b,v17.b}[0], [x0], x1
        st2             {v16.b,v17.b}[1], [x0], x1
        st2             {v16.b,v17.b}[2], [x0], x1
        st2             {v16.b,v17.b}[3], [x0], x1

9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma_intra_neon, export=1
        h264_loop_filter_start_intra

        sub             x4,  x0,  #2
        sub             x0,  x0,  #1
h_loop_filter_chroma420_intra:
        ld1             {v18.8b}, [x4], x1
        ld1             {v16.8b}, [x4], x1
        ld1             {v17.8b}, [x4], x1
        ld1             {v19.8b}, [x4], x1
        ld1             {v18.s}[1], [x4], x1
        ld1             {v16.s}[1], [x4], x1
        ld1             {v17.s}[1], [x4], x1
        ld1             {v19.s}[1], [x4], x1

        transpose_4x8B v18, v16, v17, v19, v26, v27, v28, v29

        h264_loop_filter_chroma_intra

        st2             {v16.b,v17.b}[0], [x0], x1
        st2             {v16.b,v17.b}[1], [x0], x1
        st2             {v16.b,v17.b}[2], [x0], x1
        st2             {v16.b,v17.b}[3], [x0], x1
        st2             {v16.b,v17.b}[4], [x0], x1
        st2             {v16.b,v17.b}[5], [x0], x1
        st2             {v16.b,v17.b}[6], [x0], x1
        st2             {v16.b,v17.b}[7], [x0], x1

9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma422_intra_neon, export=1
        h264_loop_filter_start_intra
        sub             x4,  x0,  #2
        add             x5,  x0,  x1, lsl #3
        sub             x0,  x0,  #1
        mov             x7,  x30
        bl              h_loop_filter_chroma420_intra
        sub             x0,  x5,  #1
        mov             x30, x7
        b               h_loop_filter_chroma420_intra
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

.macro  h264_loop_filter_start_10
        cmp             w2,  #0
        ldr             w6,  [x4]
        ccmp            w3,  #0,  #0,  ne
        lsl             w2,  w2,  #2
        mov             v24.S[0], w6
        lsl             w3,  w3,  #2
        and             w8,  w6,  w6,  lsl #16
        b.eq            1f
        ands            w8,  w8,  w8,  lsl #8
        b.ge            2f
1:
        ret
2:
.endm

.macro h264_loop_filter_start_intra_10
        orr             w4,  w2,  w3
        cbnz            w4,  1f
        ret
1:
        lsl             w2,  w2,  #2
        lsl             w3,  w3,  #2
        dup             v30.8h,   w2              // alpha
        dup             v31.8h,   w3              // beta
.endm

.macro  h264_loop_filter_chroma_10
        dup             v22.8h,  w2               // alpha
        dup             v23.8h,  w3               // beta
        uxtl            v24.8h,  v24.8b           // tc0

        uabd            v26.8h,  v16.8h,  v0.8h   // abs(p0 - q0)
        uabd            v28.8h,  v18.8h,  v16.8h  // abs(p1 - p0)
        uabd            v30.8h,  v2.8h,   v0.8h   // abs(q1 - q0)
        cmhi            v26.8h,  v22.8h,  v26.8h  // < alpha
        cmhi            v28.8h,  v23.8h,  v28.8h  // < beta
        cmhi            v30.8h,  v23.8h,  v30.8h  // < beta

        and             v26.16b, v26.16b, v28.16b
        mov             v4.16b,  v0.16b
        sub             v4.8h,   v4.8h,   v16.8h
        and             v26.16b, v26.16b, v30.16b
        shl             v4.8h,   v4.8h,   #2
        mov             x8, v26.d[0]
        mov             x9, v26.d[1]
        sli             v24.8h,  v24.8h,  #8
        uxtl            v24.8h,  v24.8b
        add             v4.8h,   v4.8h,   v18.8h
        adds            x8,  x8,  x9
        shl             v24.8h,  v24.8h,  #2

        b.eq            9f

        movi            v31.8h, #3                // (tc0 - 1) << (BIT_DEPTH - 8)) + 1
        uqsub           v24.8h,  v24.8h,  v31.8h
        sub             v4.8h,   v4.8h,   v2.8h
        srshr           v4.8h,   v4.8h,   #3
        smin            v4.8h,   v4.8h,   v24.8h
        neg             v25.8h,  v24.8h
        smax            v4.8h,   v4.8h,   v25.8h
        and             v4.16b,  v4.16b,  v26.16b
        add             v16.8h,  v16.8h,  v4.8h
        sub             v0.8h,   v0.8h,   v4.8h

        mvni            v4.8h,   #0xFC, lsl #8    // 1023 for clipping
        movi            v5.8h,   #0
        smin            v0.8h,   v0.8h,   v4.8h
        smin            v16.8h,  v16.8h,  v4.8h
        smax            v0.8h,   v0.8h,   v5.8h
        smax            v16.8h,  v16.8h,  v5.8h
.endm

function ff_h264_v_loop_filter_chroma_neon_10, export=1
        h264_loop_filter_start_10

        mov             x10,  x0
        sub             x0,  x0,  x1, lsl #1
        ld1             {v18.8h}, [x0 ], x1
        ld1             {v0.8h},  [x10], x1
        ld1             {v16.8h}, [x0 ], x1
        ld1             {v2.8h},  [x10]

        h264_loop_filter_chroma_10

        sub             x0,  x10,  x1, lsl #1
        st1             {v16.8h}, [x0], x1
        st1             {v0.8h},  [x0], x1
9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma_neon_10, export=1
        h264_loop_filter_start_10

        sub             x0,  x0,  #4 // access the 2nd left pixel
h_loop_filter_chroma420_10:
        add             x10,  x0,  x1,  lsl #2
        ld1             {v18.d}[0], [x0 ], x1
        ld1             {v18.d}[1], [x10], x1
        ld1             {v16.d}[0], [x0 ], x1
        ld1             {v16.d}[1], [x10], x1
        ld1             {v0.d}[0],  [x0 ], x1
        ld1             {v0.d}[1],  [x10], x1
        ld1             {v2.d}[0],  [x0 ], x1
        ld1             {v2.d}[1],  [x10], x1

        transpose_4x8H  v18, v16, v0, v2, v28, v29, v30, v31

        h264_loop_filter_chroma_10

        transpose_4x8H  v18, v16, v0, v2, v28, v29, v30, v31

        sub             x0,  x10,  x1, lsl #3
        st1             {v18.d}[0], [x0], x1
        st1             {v16.d}[0], [x0], x1
        st1             {v0.d}[0],  [x0], x1
        st1             {v2.d}[0],  [x0], x1
        st1             {v18.d}[1], [x0], x1
        st1             {v16.d}[1], [x0], x1
        st1             {v0.d}[1],  [x0], x1
        st1             {v2.d}[1],  [x0], x1
9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma422_neon_10, export=1
        h264_loop_filter_start_10
        add             x5,  x0,  x1
        sub             x0,  x0,  #4
        add             x1,  x1,  x1
        mov             x7,  x30
        bl              h_loop_filter_chroma420_10
        mov             x30, x7
        sub             x0,  x5,  #4
        mov             v24.s[0], w6
        b               h_loop_filter_chroma420_10
endfunc

.macro h264_loop_filter_chroma_intra_10
        uabd            v26.8h,  v16.8h,  v17.8h  // abs(p0 - q0)
        uabd            v27.8h,  v18.8h,  v16.8h  // abs(p1 - p0)
        uabd            v28.8h,  v19.8h,  v17.8h  // abs(q1 - q0)
        cmhi            v26.8h,  v30.8h,  v26.8h  // < alpha
        cmhi            v27.8h,  v31.8h,  v27.8h  // < beta
        cmhi            v28.8h,  v31.8h,  v28.8h  // < beta
        and             v26.16b, v26.16b, v27.16b
        and             v26.16b, v26.16b, v28.16b
        mov             x2, v26.d[0]
        mov             x3, v26.d[1]

        shl             v4.8h,  v18.8h,  #1
        shl             v6.8h,  v19.8h,  #1

        adds            x2,  x2,  x3
        b.eq            9f

        add             v20.8h,  v16.8h,  v19.8h
        add             v22.8h,  v17.8h,  v18.8h
        add             v20.8h,  v20.8h,  v4.8h
        add             v22.8h,  v22.8h,  v6.8h
        urshr           v24.8h,  v20.8h,  #2
        urshr           v25.8h,  v22.8h,  #2
        bit             v16.16b, v24.16b, v26.16b
        bit             v17.16b, v25.16b, v26.16b
.endm

function ff_h264_v_loop_filter_chroma_intra_neon_10, export=1
        h264_loop_filter_start_intra_10
        mov             x9,  x0
        sub             x0,  x0,  x1, lsl #1
        ld1             {v18.8h}, [x0], x1
        ld1             {v17.8h}, [x9], x1
        ld1             {v16.8h}, [x0], x1
        ld1             {v19.8h}, [x9]

        h264_loop_filter_chroma_intra_10

        sub             x0,  x9,  x1, lsl #1
        st1             {v16.8h}, [x0], x1
        st1             {v17.8h}, [x0], x1

9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma_mbaff_intra_neon_10, export=1
        h264_loop_filter_start_intra_10

        sub             x4,  x0,  #4
        sub             x0,  x0,  #2
        add             x9,  x4,  x1, lsl #1
        ld1             {v18.8h}, [x4], x1
        ld1             {v17.8h}, [x9], x1
        ld1             {v16.8h}, [x4], x1
        ld1             {v19.8h}, [x9], x1

        transpose_4x8H v18, v16, v17, v19, v26, v27, v28, v29

        h264_loop_filter_chroma_intra_10

        st2             {v16.h,v17.h}[0], [x0], x1
        st2             {v16.h,v17.h}[1], [x0], x1
        st2             {v16.h,v17.h}[2], [x0], x1
        st2             {v16.h,v17.h}[3], [x0], x1

9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma_intra_neon_10, export=1
        h264_loop_filter_start_intra_10
        sub             x4,  x0,  #4
        sub             x0,  x0,  #2
h_loop_filter_chroma420_intra_10:
        add             x9,  x4,  x1, lsl #2
        ld1             {v18.4h},   [x4], x1
        ld1             {v18.d}[1], [x9], x1
        ld1             {v16.4h},   [x4], x1
        ld1             {v16.d}[1], [x9], x1
        ld1             {v17.4h},   [x4], x1
        ld1             {v17.d}[1], [x9], x1
        ld1             {v19.4h},   [x4], x1
        ld1             {v19.d}[1], [x9], x1

        transpose_4x8H v18, v16, v17, v19, v26, v27, v28, v29

        h264_loop_filter_chroma_intra_10

        st2             {v16.h,v17.h}[0], [x0], x1
        st2             {v16.h,v17.h}[1], [x0], x1
        st2             {v16.h,v17.h}[2], [x0], x1
        st2             {v16.h,v17.h}[3], [x0], x1
        st2             {v16.h,v17.h}[4], [x0], x1
        st2             {v16.h,v17.h}[5], [x0], x1
        st2             {v16.h,v17.h}[6], [x0], x1
        st2             {v16.h,v17.h}[7], [x0], x1

9:
        ret
endfunc

function ff_h264_h_loop_filter_chroma422_intra_neon_10, export=1
        h264_loop_filter_start_intra_10
        sub             x4,  x0,  #4
        add             x5,  x0,  x1, lsl #3
        sub             x0,  x0,  #2
        mov             x7,  x30
        bl              h_loop_filter_chroma420_intra_10
        mov             x4,  x9
        sub             x0,  x5,  #2
        mov             x30, x7
        b               h_loop_filter_chroma420_intra_10
endfunc
