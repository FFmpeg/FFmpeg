/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
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

        /* H.264 qpel MC */

.macro  lowpass_const   r
        movz            \r, #20, lsl #16
        movk            \r, #5
        mov             v6.S[0], \r
.endm

//trashes v0-v5
.macro  lowpass_8       r0,  r1,  r2,  r3,  d0,  d1,  narrow=1
        ext             v2.8B,      \r0\().8B, \r1\().8B, #2
        ext             v3.8B,      \r0\().8B, \r1\().8B, #3
        uaddl           v2.8H,      v2.8B,     v3.8B
        ext             v4.8B,      \r0\().8B, \r1\().8B, #1
        ext             v5.8B,      \r0\().8B, \r1\().8B, #4
        uaddl           v4.8H,      v4.8B,     v5.8B
        ext             v1.8B,      \r0\().8B, \r1\().8B, #5
        uaddl           \d0\().8H,  \r0\().8B, v1.8B
        ext             v0.8B,      \r2\().8B, \r3\().8B, #2
        mla             \d0\().8H,  v2.8H,     v6.H[1]
        ext             v1.8B,      \r2\().8B, \r3\().8B, #3
        uaddl           v0.8H,      v0.8B,     v1.8B
        ext             v1.8B,      \r2\().8B, \r3\().8B, #1
        mls             \d0\().8H,  v4.8H,     v6.H[0]
        ext             v3.8B,      \r2\().8B, \r3\().8B, #4
        uaddl           v1.8H,      v1.8B,     v3.8B
        ext             v2.8B,      \r2\().8B, \r3\().8B, #5
        uaddl           \d1\().8H,  \r2\().8B, v2.8B
        mla             \d1\().8H,  v0.8H,     v6.H[1]
        mls             \d1\().8H,  v1.8H,     v6.H[0]
  .if \narrow
        sqrshrun        \d0\().8B,  \d0\().8H, #5
        sqrshrun        \d1\().8B,  \d1\().8H, #5
  .endif
.endm

//trashes v0-v5, v7, v30-v31
.macro  lowpass_8H      r0,  r1
        ext             v0.16B,     \r0\().16B, \r0\().16B, #2
        ext             v1.16B,     \r0\().16B, \r0\().16B, #3
        uaddl           v0.8H,      v0.8B,      v1.8B
        ext             v2.16B,     \r0\().16B, \r0\().16B, #1
        ext             v3.16B,     \r0\().16B, \r0\().16B, #4
        uaddl           v2.8H,      v2.8B,      v3.8B
        ext             v30.16B,    \r0\().16B, \r0\().16B, #5
        uaddl           \r0\().8H,  \r0\().8B,  v30.8B
        ext             v4.16B,     \r1\().16B, \r1\().16B, #2
        mla             \r0\().8H,  v0.8H,      v6.H[1]
        ext             v5.16B,     \r1\().16B, \r1\().16B, #3
        uaddl           v4.8H,      v4.8B,      v5.8B
        ext             v7.16B,     \r1\().16B, \r1\().16B, #1
        mls             \r0\().8H,  v2.8H,      v6.H[0]
        ext             v0.16B,     \r1\().16B, \r1\().16B, #4
        uaddl           v7.8H,      v7.8B,      v0.8B
        ext             v31.16B,    \r1\().16B, \r1\().16B, #5
        uaddl           \r1\().8H,  \r1\().8B,  v31.8B
        mla             \r1\().8H,  v4.8H,      v6.H[1]
        mls             \r1\().8H,  v7.8H,      v6.H[0]
.endm

// trashes v2-v5, v30
.macro  lowpass_8_1     r0,  r1,  d0,  narrow=1
        ext             v2.8B,     \r0\().8B, \r1\().8B, #2
        ext             v3.8B,     \r0\().8B, \r1\().8B, #3
        uaddl           v2.8H,     v2.8B,     v3.8B
        ext             v4.8B,     \r0\().8B, \r1\().8B, #1
        ext             v5.8B,     \r0\().8B, \r1\().8B, #4
        uaddl           v4.8H,     v4.8B,     v5.8B
        ext             v30.8B,    \r0\().8B, \r1\().8B, #5
        uaddl           \d0\().8H, \r0\().8B, v30.8B
        mla             \d0\().8H, v2.8H,     v6.H[1]
        mls             \d0\().8H, v4.8H,     v6.H[0]
  .if \narrow
        sqrshrun        \d0\().8B, \d0\().8H, #5
  .endif
.endm

// trashed v0-v7
.macro  lowpass_8.16    r0,  r1,  r2
        ext             v1.16B,     \r0\().16B, \r1\().16B, #4
        ext             v0.16B,     \r0\().16B, \r1\().16B, #6
        saddl           v5.4S,      v1.4H,      v0.4H
        ext             v2.16B,     \r0\().16B, \r1\().16B, #2
        saddl2          v1.4S,      v1.8H,      v0.8H
        ext             v3.16B,     \r0\().16B, \r1\().16B, #8
        saddl           v6.4S,      v2.4H,      v3.4H
        ext             \r1\().16B, \r0\().16B, \r1\().16B, #10
        saddl2          v2.4S,      v2.8H,      v3.8H
        saddl           v0.4S,      \r0\().4H,  \r1\().4H
        saddl2          v4.4S,      \r0\().8H,  \r1\().8H

        shl             v3.4S,  v5.4S,  #4
        shl             v5.4S,  v5.4S,  #2
        shl             v7.4S,  v6.4S,  #2
        add             v5.4S,  v5.4S,  v3.4S
        add             v6.4S,  v6.4S,  v7.4S

        shl             v3.4S,  v1.4S,  #4
        shl             v1.4S,  v1.4S,  #2
        shl             v7.4S,  v2.4S,  #2
        add             v1.4S,  v1.4S,  v3.4S
        add             v2.4S,  v2.4S,  v7.4S

        add             v5.4S,  v5.4S,  v0.4S
        sub             v5.4S,  v5.4S,  v6.4S

        add             v1.4S,  v1.4S,  v4.4S
        sub             v1.4S,  v1.4S,  v2.4S

        rshrn           v5.4H,  v5.4S,  #10
        rshrn2          v5.8H,  v1.4S,  #10

        sqxtun          \r2\().8B,  v5.8H
.endm

function put_h264_qpel16_h_lowpass_neon_packed
        mov             x4,  x30
        mov             x12, #16
        mov             x3,  #8
        bl              put_h264_qpel8_h_lowpass_neon
        sub             x1,  x1,  x2, lsl #4
        add             x1,  x1,  #8
        mov             x12, #16
        mov             x30, x4
        b               put_h264_qpel8_h_lowpass_neon
endfunc

.macro  h264_qpel_h_lowpass type
function \type\()_h264_qpel16_h_lowpass_neon
        mov             x13, x30
        mov             x12, #16
        bl              \type\()_h264_qpel8_h_lowpass_neon
        sub             x0,  x0,  x3, lsl #4
        sub             x1,  x1,  x2, lsl #4
        add             x0,  x0,  #8
        add             x1,  x1,  #8
        mov             x12, #16
        mov             x30, x13
endfunc

function \type\()_h264_qpel8_h_lowpass_neon
1:      ld1             {v28.8B, v29.8B}, [x1], x2
        ld1             {v16.8B, v17.8B}, [x1], x2
        subs            x12, x12, #2
        lowpass_8       v28, v29, v16, v17, v28, v16
  .ifc \type,avg
        ld1             {v2.8B},    [x0], x3
        urhadd          v28.8B, v28.8B,  v2.8B
        ld1             {v3.8B},    [x0]
        urhadd          v16.8B, v16.8B, v3.8B
        sub             x0,  x0,  x3
  .endif
        st1             {v28.8B},    [x0], x3
        st1             {v16.8B},    [x0], x3
        b.ne            1b
        ret
endfunc
.endm

        h264_qpel_h_lowpass put
        h264_qpel_h_lowpass avg

.macro  h264_qpel_h_lowpass_l2 type
function \type\()_h264_qpel16_h_lowpass_l2_neon
        mov             x13, x30
        mov             x12, #16
        bl              \type\()_h264_qpel8_h_lowpass_l2_neon
        sub             x0,  x0,  x2, lsl #4
        sub             x1,  x1,  x2, lsl #4
        sub             x3,  x3,  x2, lsl #4
        add             x0,  x0,  #8
        add             x1,  x1,  #8
        add             x3,  x3,  #8
        mov             x12, #16
        mov             x30, x13
endfunc

function \type\()_h264_qpel8_h_lowpass_l2_neon
1:      ld1             {v26.8B, v27.8B}, [x1], x2
        ld1             {v16.8B, v17.8B}, [x1], x2
        ld1             {v28.8B},     [x3], x2
        ld1             {v29.8B},     [x3], x2
        subs            x12, x12, #2
        lowpass_8       v26, v27, v16, v17, v26, v27
        urhadd          v26.8B, v26.8B, v28.8B
        urhadd          v27.8B, v27.8B, v29.8B
  .ifc \type,avg
        ld1             {v2.8B},      [x0], x2
        urhadd          v26.8B, v26.8B, v2.8B
        ld1             {v3.8B},      [x0]
        urhadd          v27.8B, v27.8B, v3.8B
        sub             x0,  x0,  x2
  .endif
        st1             {v26.8B},     [x0], x2
        st1             {v27.8B},     [x0], x2
        b.ne            1b
        ret
endfunc
.endm

        h264_qpel_h_lowpass_l2 put
        h264_qpel_h_lowpass_l2 avg

function put_h264_qpel16_v_lowpass_neon_packed
        mov             x4,  x30
        mov             x2,  #8
        bl              put_h264_qpel8_v_lowpass_neon
        sub             x1,  x1,  x3, lsl #2
        bl              put_h264_qpel8_v_lowpass_neon
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #8
        bl              put_h264_qpel8_v_lowpass_neon
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x4
        b               put_h264_qpel8_v_lowpass_neon
endfunc

.macro  h264_qpel_v_lowpass type
function \type\()_h264_qpel16_v_lowpass_neon
        mov             x4,  x30
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             x1,  x1,  x3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             x0,  x0,  x2, lsl #4
        add             x0,  x0,  #8
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #8
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x4
endfunc

function \type\()_h264_qpel8_v_lowpass_neon
        ld1             {v16.8B}, [x1], x3
        ld1             {v18.8B}, [x1], x3
        ld1             {v20.8B}, [x1], x3
        ld1             {v22.8B}, [x1], x3
        ld1             {v24.8B}, [x1], x3
        ld1             {v26.8B}, [x1], x3
        ld1             {v28.8B}, [x1], x3
        ld1             {v30.8B}, [x1], x3
        ld1             {v17.8B}, [x1], x3
        ld1             {v19.8B}, [x1], x3
        ld1             {v21.8B}, [x1], x3
        ld1             {v23.8B}, [x1], x3
        ld1             {v25.8B}, [x1]

        transpose_8x8B  v16, v18, v20, v22, v24, v26, v28, v30, v0,  v1
        transpose_8x8B  v17, v19, v21, v23, v25, v27, v29, v31, v0,  v1
        lowpass_8       v16, v17, v18, v19, v16, v17
        lowpass_8       v20, v21, v22, v23, v18, v19
        lowpass_8       v24, v25, v26, v27, v20, v21
        lowpass_8       v28, v29, v30, v31, v22, v23
        transpose_8x8B  v16, v17, v18, v19, v20, v21, v22, v23, v0,  v1

  .ifc \type,avg
        ld1             {v24.8B},  [x0], x2
        urhadd          v16.8B, v16.8B, v24.8B
        ld1             {v25.8B}, [x0], x2
        urhadd          v17.8B, v17.8B, v25.8B
        ld1             {v26.8B}, [x0], x2
        urhadd          v18.8B, v18.8B, v26.8B
        ld1             {v27.8B}, [x0], x2
        urhadd          v19.8B, v19.8B, v27.8B
        ld1             {v28.8B}, [x0], x2
        urhadd          v20.8B, v20.8B, v28.8B
        ld1             {v29.8B}, [x0], x2
        urhadd          v21.8B, v21.8B, v29.8B
        ld1             {v30.8B}, [x0], x2
        urhadd          v22.8B, v22.8B, v30.8B
        ld1             {v31.8B}, [x0], x2
        urhadd          v23.8B, v23.8B, v31.8B
        sub             x0,  x0,  x2,  lsl #3
  .endif

        st1             {v16.8B}, [x0], x2
        st1             {v17.8B}, [x0], x2
        st1             {v18.8B}, [x0], x2
        st1             {v19.8B}, [x0], x2
        st1             {v20.8B}, [x0], x2
        st1             {v21.8B}, [x0], x2
        st1             {v22.8B}, [x0], x2
        st1             {v23.8B}, [x0], x2

        ret
endfunc
.endm

        h264_qpel_v_lowpass put
        h264_qpel_v_lowpass avg

.macro  h264_qpel_v_lowpass_l2 type
function \type\()_h264_qpel16_v_lowpass_l2_neon
        mov             x4,  x30
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             x1,  x1,  x3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             x0,  x0,  x3, lsl #4
        sub             x12, x12, x2, lsl #4
        add             x0,  x0,  #8
        add             x12, x12, #8
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #8
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x4
endfunc

function \type\()_h264_qpel8_v_lowpass_l2_neon
        ld1             {v16.8B}, [x1], x3
        ld1             {v18.8B}, [x1], x3
        ld1             {v20.8B}, [x1], x3
        ld1             {v22.8B}, [x1], x3
        ld1             {v24.8B}, [x1], x3
        ld1             {v26.8B}, [x1], x3
        ld1             {v28.8B}, [x1], x3
        ld1             {v30.8B}, [x1], x3
        ld1             {v17.8B}, [x1], x3
        ld1             {v19.8B}, [x1], x3
        ld1             {v21.8B}, [x1], x3
        ld1             {v23.8B}, [x1], x3
        ld1             {v25.8B}, [x1]

        transpose_8x8B  v16, v18, v20, v22, v24, v26, v28, v30, v0,  v1
        transpose_8x8B  v17, v19, v21, v23, v25, v27, v29, v31, v0,  v1
        lowpass_8       v16, v17, v18, v19, v16, v17
        lowpass_8       v20, v21, v22, v23, v18, v19
        lowpass_8       v24, v25, v26, v27, v20, v21
        lowpass_8       v28, v29, v30, v31, v22, v23
        transpose_8x8B  v16, v17, v18, v19, v20, v21, v22, v23, v0,  v1

        ld1             {v24.8B},  [x12], x2
        ld1             {v25.8B},  [x12], x2
        ld1             {v26.8B},  [x12], x2
        ld1             {v27.8B},  [x12], x2
        ld1             {v28.8B},  [x12], x2
        urhadd          v16.8B, v24.8B, v16.8B
        urhadd          v17.8B, v25.8B, v17.8B
        ld1             {v29.8B},  [x12], x2
        urhadd          v18.8B, v26.8B, v18.8B
        urhadd          v19.8B, v27.8B, v19.8B
        ld1             {v30.8B}, [x12], x2
        urhadd          v20.8B, v28.8B, v20.8B
        urhadd          v21.8B, v29.8B, v21.8B
        ld1             {v31.8B}, [x12], x2
        urhadd          v22.8B, v30.8B, v22.8B
        urhadd          v23.8B, v31.8B, v23.8B

  .ifc \type,avg
        ld1             {v24.8B}, [x0], x3
        urhadd          v16.8B, v16.8B, v24.8B
        ld1             {v25.8B}, [x0], x3
        urhadd          v17.8B, v17.8B, v25.8B
        ld1             {v26.8B}, [x0], x3
        urhadd          v18.8B, v18.8B, v26.8B
        ld1             {v27.8B}, [x0], x3
        urhadd          v19.8B, v19.8B, v27.8B
        ld1             {v28.8B}, [x0], x3
        urhadd          v20.8B, v20.8B, v28.8B
        ld1             {v29.8B}, [x0], x3
        urhadd          v21.8B, v21.8B, v29.8B
        ld1             {v30.8B}, [x0], x3
        urhadd          v22.8B, v22.8B, v30.8B
        ld1             {v31.8B}, [x0], x3
        urhadd          v23.8B, v23.8B, v31.8B
        sub             x0,  x0,  x3,  lsl #3
  .endif

        st1             {v16.8B}, [x0], x3
        st1             {v17.8B}, [x0], x3
        st1             {v18.8B}, [x0], x3
        st1             {v19.8B}, [x0], x3
        st1             {v20.8B}, [x0], x3
        st1             {v21.8B}, [x0], x3
        st1             {v22.8B}, [x0], x3
        st1             {v23.8B}, [x0], x3

        ret
endfunc
.endm

        h264_qpel_v_lowpass_l2 put
        h264_qpel_v_lowpass_l2 avg

function put_h264_qpel8_hv_lowpass_neon_top
        lowpass_const   w12
        ld1             {v16.8H}, [x1], x3
        ld1             {v17.8H}, [x1], x3
        ld1             {v18.8H}, [x1], x3
        ld1             {v19.8H}, [x1], x3
        ld1             {v20.8H}, [x1], x3
        ld1             {v21.8H}, [x1], x3
        ld1             {v22.8H}, [x1], x3
        ld1             {v23.8H}, [x1], x3
        ld1             {v24.8H}, [x1], x3
        ld1             {v25.8H}, [x1], x3
        ld1             {v26.8H}, [x1], x3
        ld1             {v27.8H}, [x1], x3
        ld1             {v28.8H}, [x1]
        lowpass_8H      v16, v17
        lowpass_8H      v18, v19
        lowpass_8H      v20, v21
        lowpass_8H      v22, v23
        lowpass_8H      v24, v25
        lowpass_8H      v26, v27
        lowpass_8H      v28, v29

        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v0,  v1
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v30, v31, v0,  v1

        lowpass_8.16    v16, v24, v16
        lowpass_8.16    v17, v25, v17

        lowpass_8.16    v18, v26, v18
        lowpass_8.16    v19, v27, v19

        lowpass_8.16    v20, v28, v20
        lowpass_8.16    v21, v29, v21

        lowpass_8.16    v22, v30, v22
        lowpass_8.16    v23, v31, v23

        transpose_8x8B v16, v17, v18, v19, v20, v21, v22, v23, v0,  v1

        ret
endfunc

.macro  h264_qpel8_hv_lowpass type
function \type\()_h264_qpel8_hv_lowpass_neon
        mov             x10, x30
        bl              put_h264_qpel8_hv_lowpass_neon_top
  .ifc \type,avg
        ld1             {v0.8B},      [x0], x2
        urhadd          v16.8B, v16.8B, v0.8B
        ld1             {v1.8B},      [x0], x2
        urhadd          v17.8B, v17.8B, v1.8B
        ld1             {v2.8B},      [x0], x2
        urhadd          v18.8B, v18.8B, v2.8B
        ld1             {v3.8B},      [x0], x2
        urhadd          v19.8B, v19.8B, v3.8B
        ld1             {v4.8B},      [x0], x2
        urhadd          v20.8B, v20.8B, v4.8B
        ld1             {v5.8B},      [x0], x2
        urhadd          v21.8B, v21.8B, v5.8B
        ld1             {v6.8B},      [x0], x2
        urhadd          v22.8B, v22.8B, v6.8B
        ld1             {v7.8B},      [x0], x2
        urhadd          v23.8B, v23.8B, v7.8B
        sub             x0,  x0,  x2,  lsl #3
  .endif

        st1             {v16.8B},     [x0], x2
        st1             {v17.8B},     [x0], x2
        st1             {v18.8B},     [x0], x2
        st1             {v19.8B},     [x0], x2
        st1             {v20.8B},     [x0], x2
        st1             {v21.8B},     [x0], x2
        st1             {v22.8B},     [x0], x2
        st1             {v23.8B},     [x0], x2

        ret             x10
endfunc
.endm

        h264_qpel8_hv_lowpass put
        h264_qpel8_hv_lowpass avg

.macro  h264_qpel8_hv_lowpass_l2 type
function \type\()_h264_qpel8_hv_lowpass_l2_neon
        mov             x10, x30
        bl              put_h264_qpel8_hv_lowpass_neon_top

        ld1             {v0.8B, v1.8B},  [x2], #16
        ld1             {v2.8B, v3.8B},  [x2], #16
        urhadd          v0.8B,  v0.8B,  v16.8B
        urhadd          v1.8B,  v1.8B,  v17.8B
        ld1             {v4.8B, v5.8B},  [x2], #16
        urhadd          v2.8B,  v2.8B,  v18.8B
        urhadd          v3.8B,  v3.8B,  v19.8B
        ld1             {v6.8B, v7.8B},  [x2], #16
        urhadd          v4.8B,  v4.8B,  v20.8B
        urhadd          v5.8B,  v5.8B,  v21.8B
        urhadd          v6.8B,  v6.8B,  v22.8B
        urhadd          v7.8B,  v7.8B,  v23.8B
  .ifc \type,avg
        ld1             {v16.8B},     [x0], x3
        urhadd          v0.8B,  v0.8B,  v16.8B
        ld1             {v17.8B},     [x0], x3
        urhadd          v1.8B,  v1.8B,  v17.8B
        ld1             {v18.8B},     [x0], x3
        urhadd          v2.8B,  v2.8B,  v18.8B
        ld1             {v19.8B},     [x0], x3
        urhadd          v3.8B,  v3.8B,  v19.8B
        ld1             {v20.8B},     [x0], x3
        urhadd          v4.8B,  v4.8B,  v20.8B
        ld1             {v21.8B},     [x0], x3
        urhadd          v5.8B,  v5.8B,  v21.8B
        ld1             {v22.8B},     [x0], x3
        urhadd          v6.8B,  v6.8B,  v22.8B
        ld1             {v23.8B},     [x0], x3
        urhadd          v7.8B,  v7.8B,  v23.8B
        sub             x0,  x0,  x3,  lsl #3
  .endif
        st1             {v0.8B},      [x0], x3
        st1             {v1.8B},      [x0], x3
        st1             {v2.8B},      [x0], x3
        st1             {v3.8B},      [x0], x3
        st1             {v4.8B},      [x0], x3
        st1             {v5.8B},      [x0], x3
        st1             {v6.8B},      [x0], x3
        st1             {v7.8B},      [x0], x3

        ret             x10
endfunc
.endm

        h264_qpel8_hv_lowpass_l2 put
        h264_qpel8_hv_lowpass_l2 avg

.macro  h264_qpel16_hv  type
function \type\()_h264_qpel16_hv_lowpass_neon
        mov             x13, x30
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             x1,  x1,  x3, lsl #2
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #8
        sub             x0,  x0,  x2, lsl #4
        add             x0,  x0,  #8
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x13
        b               \type\()_h264_qpel8_hv_lowpass_neon
endfunc

function \type\()_h264_qpel16_hv_lowpass_l2_neon
        mov             x13, x30
        sub             x2,  x4,  #256
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             x1,  x1,  x3, lsl #2
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #8
        sub             x0,  x0,  x3, lsl #4
        add             x0,  x0,  #8
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x13
        b               \type\()_h264_qpel8_hv_lowpass_l2_neon
endfunc
.endm

        h264_qpel16_hv put
        h264_qpel16_hv avg

.macro  h264_qpel8      type
function ff_\type\()_h264_qpel8_mc10_neon, export=1
        lowpass_const   w3
        mov             x3,  x1
        sub             x1,  x1,  #2
        mov             x12, #8
        b               \type\()_h264_qpel8_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel8_mc20_neon, export=1
        lowpass_const   w3
        sub             x1,  x1,  #2
        mov             x3,  x2
        mov             x12, #8
        b               \type\()_h264_qpel8_h_lowpass_neon
endfunc

function ff_\type\()_h264_qpel8_mc30_neon, export=1
        lowpass_const   w3
        add             x3,  x1,  #1
        sub             x1,  x1,  #2
        mov             x12, #8
        b               \type\()_h264_qpel8_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel8_mc01_neon, export=1
        mov             x14, x30
        mov             x12, x1
\type\()_h264_qpel8_mc01:
        lowpass_const   w3
        mov             x3,  x2
        sub             x1,  x1,  x2, lsl #1
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc11_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel8_mc11:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #64
        mov             x0,  sp
        sub             x1,  x1,  #2
        mov             x3,  #8
        mov             x12, #8
        bl              put_h264_qpel8_h_lowpass_neon
        mov             x0,  x8
        mov             x3,  x2
        mov             x12, sp
        sub             x1,  x9,  x2, lsl #1
        mov             x2,  #8
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc21_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel8_mc21:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #(8*8+16*12)
        sub             x1,  x1,  #2
        mov             x3,  #8
        mov             x0,  sp
        mov             x12, #8
        bl              put_h264_qpel8_h_lowpass_neon
        mov             x4,  x0
        mov             x0,  x8
        sub             x1,  x9,  x2, lsl #1
        sub             x1,  x1,  #2
        mov             x3,  x2
        sub             x2,  x4,  #64
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc31_neon, export=1
        add             x1,  x1,  #1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        sub             x1,  x1,  #1
        b               \type\()_h264_qpel8_mc11
endfunc

function ff_\type\()_h264_qpel8_mc02_neon, export=1
        mov             x14, x30
        lowpass_const   w3
        sub             x1,  x1,  x2, lsl #1
        mov             x3,  x2
        bl              \type\()_h264_qpel8_v_lowpass_neon
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc12_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel8_mc12:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #(8*8+16*12)
        sub             x1,  x1,  x2, lsl #1
        mov             x3,  x2
        mov             x2,  #8
        mov             x0,  sp
        bl              put_h264_qpel8_v_lowpass_neon
        mov             x4,  x0
        mov             x0,  x8
        sub             x1,  x9,  x3, lsl #1
        sub             x1,  x1,  #2
        sub             x2,  x4,  #64
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc22_neon, export=1
        mov             x14, x30
        mov             x11, sp
        sub             x1,  x1,  x2, lsl #1
        sub             x1,  x1,  #2
        mov             x3,  x2
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc32_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  #1
        b               \type\()_h264_qpel8_mc12
endfunc

function ff_\type\()_h264_qpel8_mc03_neon, export=1
        mov             x14, x30
        add             x12, x1,  x2
        b               \type\()_h264_qpel8_mc01
endfunc

function ff_\type\()_h264_qpel8_mc13_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        b               \type\()_h264_qpel8_mc11
endfunc

function ff_\type\()_h264_qpel8_mc23_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        b               \type\()_h264_qpel8_mc21
endfunc

function ff_\type\()_h264_qpel8_mc33_neon, export=1
        add             x1,  x1,  #1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        sub             x1,  x1,  #1
        b               \type\()_h264_qpel8_mc11
endfunc
.endm

        h264_qpel8 put
        h264_qpel8 avg

.macro  h264_qpel16     type
function ff_\type\()_h264_qpel16_mc10_neon, export=1
        lowpass_const   w3
        mov             x3,  x1
        sub             x1,  x1,  #2
        b               \type\()_h264_qpel16_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel16_mc20_neon, export=1
        lowpass_const   w3
        sub             x1,  x1,  #2
        mov             x3,  x2
        b               \type\()_h264_qpel16_h_lowpass_neon
endfunc

function ff_\type\()_h264_qpel16_mc30_neon, export=1
        lowpass_const   w3
        add             x3,  x1,  #1
        sub             x1,  x1,  #2
        b               \type\()_h264_qpel16_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel16_mc01_neon, export=1
        mov             x14, x30
        mov             x12, x1
\type\()_h264_qpel16_mc01:
        lowpass_const   w3
        mov             x3,  x2
        sub             x1,  x1,  x2, lsl #1
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc11_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel16_mc11:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #256
        mov             x0,  sp
        sub             x1,  x1,  #2
        mov             x3,  #16
        bl              put_h264_qpel16_h_lowpass_neon
        mov             x0,  x8
        mov             x3,  x2
        mov             x12, sp
        sub             x1,  x9,  x2, lsl #1
        mov             x2,  #16
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc21_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel16_mc21:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #(16*16+16*12)
        sub             x1,  x1,  #2
        mov             x0,  sp
        bl              put_h264_qpel16_h_lowpass_neon_packed
        mov             x4,  x0
        mov             x0,  x8
        sub             x1,  x9,  x2, lsl #1
        sub             x1,  x1,  #2
        mov             x3,  x2
        bl              \type\()_h264_qpel16_hv_lowpass_l2_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc31_neon, export=1
        add             x1,  x1,  #1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        sub             x1,  x1,  #1
        b               \type\()_h264_qpel16_mc11
endfunc

function ff_\type\()_h264_qpel16_mc02_neon, export=1
        mov             x14, x30
        lowpass_const   w3
        sub             x1,  x1,  x2, lsl #1
        mov             x3,  x2
        bl              \type\()_h264_qpel16_v_lowpass_neon
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc12_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel16_mc12:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #(16*16+16*12)
        sub             x1,  x1,  x2, lsl #1
        mov             x0,  sp
        mov             x3,  x2
        bl              put_h264_qpel16_v_lowpass_neon_packed
        mov             x4,  x0
        mov             x0,  x8
        sub             x1,  x9,  x3, lsl #1
        sub             x1,  x1,  #2
        mov             x2,  x3
        bl              \type\()_h264_qpel16_hv_lowpass_l2_neon
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc22_neon, export=1
        mov             x14, x30
        lowpass_const   w3
        mov             x11, sp
        sub             x1,  x1,  x2, lsl #1
        sub             x1,  x1,  #2
        mov             x3,  x2
        bl              \type\()_h264_qpel16_hv_lowpass_neon
        mov             sp,  x11 // restore stack
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc32_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  #1
        b               \type\()_h264_qpel16_mc12
endfunc

function ff_\type\()_h264_qpel16_mc03_neon, export=1
        mov             x14, x30
        add             x12, x1,  x2
        b               \type\()_h264_qpel16_mc01
endfunc

function ff_\type\()_h264_qpel16_mc13_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        b               \type\()_h264_qpel16_mc11
endfunc

function ff_\type\()_h264_qpel16_mc23_neon, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        b               \type\()_h264_qpel16_mc21
endfunc

function ff_\type\()_h264_qpel16_mc33_neon, export=1
        add             x1,  x1,  #1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        sub             x1,  x1,  #1
        b               \type\()_h264_qpel16_mc11
endfunc
.endm

        h264_qpel16 put
        h264_qpel16 avg
