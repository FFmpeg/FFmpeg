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
        mov             v6.s[0], \r
.endm

//trashes v0-v5
.macro  lowpass_8       r0,  r1,  r2,  r3,  d0,  d1,  narrow=1
        ext             v2.8b,      \r0\().8b, \r1\().8b, #2
        ext             v3.8b,      \r0\().8b, \r1\().8b, #3
        uaddl           v2.8h,      v2.8b,     v3.8b
        ext             v4.8b,      \r0\().8b, \r1\().8b, #1
        ext             v5.8b,      \r0\().8b, \r1\().8b, #4
        uaddl           v4.8h,      v4.8b,     v5.8b
        ext             v1.8b,      \r0\().8b, \r1\().8b, #5
        uaddl           \d0\().8h,  \r0\().8b, v1.8b
        ext             v0.8b,      \r2\().8b, \r3\().8b, #2
        mla             \d0\().8h,  v2.8h,     v6.h[1]
        ext             v1.8b,      \r2\().8b, \r3\().8b, #3
        uaddl           v0.8h,      v0.8b,     v1.8b
        ext             v1.8b,      \r2\().8b, \r3\().8b, #1
        mls             \d0\().8h,  v4.8h,     v6.h[0]
        ext             v3.8b,      \r2\().8b, \r3\().8b, #4
        uaddl           v1.8h,      v1.8b,     v3.8b
        ext             v2.8b,      \r2\().8b, \r3\().8b, #5
        uaddl           \d1\().8h,  \r2\().8b, v2.8b
        mla             \d1\().8h,  v0.8h,     v6.h[1]
        mls             \d1\().8h,  v1.8h,     v6.h[0]
  .if \narrow
        sqrshrun        \d0\().8b,  \d0\().8h, #5
        sqrshrun        \d1\().8b,  \d1\().8h, #5
  .endif
.endm

//trashes v0-v4
.macro  lowpass_8_v     r0,  r1,  r2,  r3,  r4,  r5,  r6,  d0,  d1,  narrow=1
        uaddl           v2.8h,      \r2\().8b, \r3\().8b
        uaddl           v0.8h,      \r3\().8b, \r4\().8b
        uaddl           v4.8h,      \r1\().8b, \r4\().8b
        uaddl           v1.8h,      \r2\().8b, \r5\().8b
        uaddl           \d0\().8h,  \r0\().8b, \r5\().8b
        uaddl           \d1\().8h,  \r1\().8b, \r6\().8b
        mla             \d0\().8h,  v2.8h,     v6.h[1]
        mls             \d0\().8h,  v4.8h,     v6.h[0]
        mla             \d1\().8h,  v0.8h,     v6.h[1]
        mls             \d1\().8h,  v1.8h,     v6.h[0]
  .if \narrow
        sqrshrun        \d0\().8b,  \d0\().8h, #5
        sqrshrun        \d1\().8b,  \d1\().8h, #5
  .endif
.endm

//trashes v0-v5, v7, v30-v31
.macro  lowpass_8H      r0,  r1
        ext             v0.16b,     \r0\().16b, \r0\().16b, #2
        ext             v1.16b,     \r0\().16b, \r0\().16b, #3
        uaddl           v0.8h,      v0.8b,      v1.8b
        ext             v2.16b,     \r0\().16b, \r0\().16b, #1
        ext             v3.16b,     \r0\().16b, \r0\().16b, #4
        uaddl           v2.8h,      v2.8b,      v3.8b
        ext             v30.16b,    \r0\().16b, \r0\().16b, #5
        uaddl           \r0\().8h,  \r0\().8b,  v30.8b
        ext             v4.16b,     \r1\().16b, \r1\().16b, #2
        mla             \r0\().8h,  v0.8h,      v6.h[1]
        ext             v5.16b,     \r1\().16b, \r1\().16b, #3
        uaddl           v4.8h,      v4.8b,      v5.8b
        ext             v7.16b,     \r1\().16b, \r1\().16b, #1
        mls             \r0\().8h,  v2.8h,      v6.h[0]
        ext             v0.16b,     \r1\().16b, \r1\().16b, #4
        uaddl           v7.8h,      v7.8b,      v0.8b
        ext             v31.16b,    \r1\().16b, \r1\().16b, #5
        uaddl           \r1\().8h,  \r1\().8b,  v31.8b
        mla             \r1\().8h,  v4.8h,      v6.h[1]
        mls             \r1\().8h,  v7.8h,      v6.h[0]
.endm

// trashes v2-v5, v30
.macro  lowpass_8_1     r0,  r1,  d0,  narrow=1
        ext             v2.8b,     \r0\().8b, \r1\().8b, #2
        ext             v3.8b,     \r0\().8b, \r1\().8b, #3
        uaddl           v2.8h,     v2.8b,     v3.8b
        ext             v4.8b,     \r0\().8b, \r1\().8b, #1
        ext             v5.8b,     \r0\().8b, \r1\().8b, #4
        uaddl           v4.8h,     v4.8b,     v5.8b
        ext             v30.8b,    \r0\().8b, \r1\().8b, #5
        uaddl           \d0\().8h, \r0\().8b, v30.8b
        mla             \d0\().8h, v2.8h,     v6.h[1]
        mls             \d0\().8h, v4.8h,     v6.h[0]
  .if \narrow
        sqrshrun        \d0\().8b, \d0\().8h, #5
  .endif
.endm

// trashed v0-v7
.macro  lowpass_8.16    r0,  r1,  r2,  r3,  r4,  r5
        saddl           v5.4s,      \r2\().4h,  \r3\().4h
        saddl2          v1.4s,      \r2\().8h,  \r3\().8h
        saddl           v6.4s,      \r1\().4h,  \r4\().4h
        saddl2          v2.4s,      \r1\().8h,  \r4\().8h
        saddl           v0.4s,      \r0\().4h,  \r5\().4h
        saddl2          v4.4s,      \r0\().8h,  \r5\().8h

        shl             v3.4s,  v5.4s,  #4
        shl             v5.4s,  v5.4s,  #2
        shl             v7.4s,  v6.4s,  #2
        add             v5.4s,  v5.4s,  v3.4s
        add             v6.4s,  v6.4s,  v7.4s

        shl             v3.4s,  v1.4s,  #4
        shl             v1.4s,  v1.4s,  #2
        shl             v7.4s,  v2.4s,  #2
        add             v1.4s,  v1.4s,  v3.4s
        add             v2.4s,  v2.4s,  v7.4s

        add             v5.4s,  v5.4s,  v0.4s
        sub             v5.4s,  v5.4s,  v6.4s

        add             v1.4s,  v1.4s,  v4.4s
        sub             v1.4s,  v1.4s,  v2.4s

        rshrn           v5.4h,  v5.4s,  #10
        rshrn2          v5.8h,  v1.4s,  #10

        sqxtun          \r0\().8b,  v5.8h
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
1:      ld1             {v28.8b, v29.8b}, [x1], x2
        ld1             {v16.8b, v17.8b}, [x1], x2
        subs            x12, x12, #2
        lowpass_8       v28, v29, v16, v17, v28, v16
  .ifc \type,avg
        ld1             {v2.8b},    [x0], x3
        ld1             {v3.8b},    [x0]
        urhadd          v28.8b, v28.8b,  v2.8b
        urhadd          v16.8b, v16.8b, v3.8b
        sub             x0,  x0,  x3
  .endif
        st1             {v28.8b},    [x0], x3
        st1             {v16.8b},    [x0], x3
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
1:      ld1             {v26.8b, v27.8b}, [x1], x2
        ld1             {v16.8b, v17.8b}, [x1], x2
        ld1             {v28.8b},     [x3], x2
        ld1             {v29.8b},     [x3], x2
        subs            x12, x12, #2
        lowpass_8       v26, v27, v16, v17, v26, v27
        urhadd          v26.8b, v26.8b, v28.8b
        urhadd          v27.8b, v27.8b, v29.8b
  .ifc \type,avg
        ld1             {v2.8b},      [x0], x2
        ld1             {v3.8b},      [x0]
        urhadd          v26.8b, v26.8b, v2.8b
        urhadd          v27.8b, v27.8b, v3.8b
        sub             x0,  x0,  x2
  .endif
        st1             {v26.8b},     [x0], x2
        st1             {v27.8b},     [x0], x2
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
        ld1             {v16.8b}, [x1], x3
        ld1             {v17.8b}, [x1], x3
        ld1             {v18.8b}, [x1], x3
        ld1             {v19.8b}, [x1], x3
        ld1             {v20.8b}, [x1], x3
        ld1             {v21.8b}, [x1], x3
        ld1             {v22.8b}, [x1], x3
        ld1             {v23.8b}, [x1], x3
        ld1             {v24.8b}, [x1], x3
        ld1             {v25.8b}, [x1], x3
        ld1             {v26.8b}, [x1], x3
        ld1             {v27.8b}, [x1], x3
        ld1             {v28.8b}, [x1]

        lowpass_8_v     v16, v17, v18, v19, v20, v21, v22, v16, v17
        lowpass_8_v     v18, v19, v20, v21, v22, v23, v24, v18, v19
        lowpass_8_v     v20, v21, v22, v23, v24, v25, v26, v20, v21
        lowpass_8_v     v22, v23, v24, v25, v26, v27, v28, v22, v23
  .ifc \type,avg
        ld1             {v24.8b},  [x0], x2
        ld1             {v25.8b}, [x0], x2
        ld1             {v26.8b}, [x0], x2
        urhadd          v16.8b, v16.8b, v24.8b
        ld1             {v27.8b}, [x0], x2
        urhadd          v17.8b, v17.8b, v25.8b
        ld1             {v28.8b}, [x0], x2
        urhadd          v18.8b, v18.8b, v26.8b
        ld1             {v29.8b}, [x0], x2
        urhadd          v19.8b, v19.8b, v27.8b
        ld1             {v30.8b}, [x0], x2
        urhadd          v20.8b, v20.8b, v28.8b
        ld1             {v31.8b}, [x0], x2
        urhadd          v21.8b, v21.8b, v29.8b
        urhadd          v22.8b, v22.8b, v30.8b
        urhadd          v23.8b, v23.8b, v31.8b
        sub             x0,  x0,  x2,  lsl #3
  .endif

        st1             {v16.8b}, [x0], x2
        st1             {v17.8b}, [x0], x2
        st1             {v18.8b}, [x0], x2
        st1             {v19.8b}, [x0], x2
        st1             {v20.8b}, [x0], x2
        st1             {v21.8b}, [x0], x2
        st1             {v22.8b}, [x0], x2
        st1             {v23.8b}, [x0], x2

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
        ld1             {v16.8b}, [x1], x3
        ld1             {v17.8b}, [x1], x3
        ld1             {v18.8b}, [x1], x3
        ld1             {v19.8b}, [x1], x3
        ld1             {v20.8b}, [x1], x3
        ld1             {v21.8b}, [x1], x3
        ld1             {v22.8b}, [x1], x3
        ld1             {v23.8b}, [x1], x3
        ld1             {v24.8b}, [x1], x3
        ld1             {v25.8b}, [x1], x3
        ld1             {v26.8b}, [x1], x3
        ld1             {v27.8b}, [x1], x3
        ld1             {v28.8b}, [x1]

        lowpass_8_v     v16, v17, v18, v19, v20, v21, v22, v16, v17
        lowpass_8_v     v18, v19, v20, v21, v22, v23, v24, v18, v19
        lowpass_8_v     v20, v21, v22, v23, v24, v25, v26, v20, v21
        lowpass_8_v     v22, v23, v24, v25, v26, v27, v28, v22, v23

        ld1             {v24.8b},  [x12], x2
        ld1             {v25.8b},  [x12], x2
        ld1             {v26.8b},  [x12], x2
        ld1             {v27.8b},  [x12], x2
        ld1             {v28.8b},  [x12], x2
        urhadd          v16.8b, v24.8b, v16.8b
        urhadd          v17.8b, v25.8b, v17.8b
        ld1             {v29.8b},  [x12], x2
        urhadd          v18.8b, v26.8b, v18.8b
        urhadd          v19.8b, v27.8b, v19.8b
        ld1             {v30.8b}, [x12], x2
        urhadd          v20.8b, v28.8b, v20.8b
        urhadd          v21.8b, v29.8b, v21.8b
        ld1             {v31.8b}, [x12], x2
        urhadd          v22.8b, v30.8b, v22.8b
        urhadd          v23.8b, v31.8b, v23.8b

  .ifc \type,avg
        ld1             {v24.8b}, [x0], x3
        ld1             {v25.8b}, [x0], x3
        ld1             {v26.8b}, [x0], x3
        urhadd          v16.8b, v16.8b, v24.8b
        ld1             {v27.8b}, [x0], x3
        urhadd          v17.8b, v17.8b, v25.8b
        ld1             {v28.8b}, [x0], x3
        urhadd          v18.8b, v18.8b, v26.8b
        ld1             {v29.8b}, [x0], x3
        urhadd          v19.8b, v19.8b, v27.8b
        ld1             {v30.8b}, [x0], x3
        urhadd          v20.8b, v20.8b, v28.8b
        ld1             {v31.8b}, [x0], x3
        urhadd          v21.8b, v21.8b, v29.8b
        urhadd          v22.8b, v22.8b, v30.8b
        urhadd          v23.8b, v23.8b, v31.8b
        sub             x0,  x0,  x3,  lsl #3
  .endif

        st1             {v16.8b}, [x0], x3
        st1             {v17.8b}, [x0], x3
        st1             {v18.8b}, [x0], x3
        st1             {v19.8b}, [x0], x3
        st1             {v20.8b}, [x0], x3
        st1             {v21.8b}, [x0], x3
        st1             {v22.8b}, [x0], x3
        st1             {v23.8b}, [x0], x3

        ret
endfunc
.endm

        h264_qpel_v_lowpass_l2 put
        h264_qpel_v_lowpass_l2 avg

function put_h264_qpel8_hv_lowpass_neon_top
        lowpass_const   w12
        ld1             {v16.8h}, [x1], x3
        ld1             {v17.8h}, [x1], x3
        ld1             {v18.8h}, [x1], x3
        ld1             {v19.8h}, [x1], x3
        ld1             {v20.8h}, [x1], x3
        ld1             {v21.8h}, [x1], x3
        ld1             {v22.8h}, [x1], x3
        ld1             {v23.8h}, [x1], x3
        ld1             {v24.8h}, [x1], x3
        ld1             {v25.8h}, [x1], x3
        ld1             {v26.8h}, [x1], x3
        ld1             {v27.8h}, [x1], x3
        ld1             {v28.8h}, [x1]
        lowpass_8H      v16, v17
        lowpass_8H      v18, v19
        lowpass_8H      v20, v21
        lowpass_8H      v22, v23
        lowpass_8H      v24, v25
        lowpass_8H      v26, v27
        lowpass_8H      v28, v29

        lowpass_8.16    v16, v17, v18, v19, v20, v21
        lowpass_8.16    v17, v18, v19, v20, v21, v22

        lowpass_8.16    v18, v19, v20, v21, v22, v23
        lowpass_8.16    v19, v20, v21, v22, v23, v24

        lowpass_8.16    v20, v21, v22, v23, v24, v25
        lowpass_8.16    v21, v22, v23, v24, v25, v26

        lowpass_8.16    v22, v23, v24, v25, v26, v27
        lowpass_8.16    v23, v24, v25, v26, v27, v28

        ret
endfunc

.macro  h264_qpel8_hv_lowpass type
function \type\()_h264_qpel8_hv_lowpass_neon
        mov             x10, x30
        bl              put_h264_qpel8_hv_lowpass_neon_top
  .ifc \type,avg
        ld1             {v0.8b},      [x0], x2
        ld1             {v1.8b},      [x0], x2
        ld1             {v2.8b},      [x0], x2
        urhadd          v16.8b, v16.8b, v0.8b
        ld1             {v3.8b},      [x0], x2
        urhadd          v17.8b, v17.8b, v1.8b
        ld1             {v4.8b},      [x0], x2
        urhadd          v18.8b, v18.8b, v2.8b
        ld1             {v5.8b},      [x0], x2
        urhadd          v19.8b, v19.8b, v3.8b
        ld1             {v6.8b},      [x0], x2
        urhadd          v20.8b, v20.8b, v4.8b
        ld1             {v7.8b},      [x0], x2
        urhadd          v21.8b, v21.8b, v5.8b
        urhadd          v22.8b, v22.8b, v6.8b
        urhadd          v23.8b, v23.8b, v7.8b
        sub             x0,  x0,  x2,  lsl #3
  .endif

        st1             {v16.8b},     [x0], x2
        st1             {v17.8b},     [x0], x2
        st1             {v18.8b},     [x0], x2
        st1             {v19.8b},     [x0], x2
        st1             {v20.8b},     [x0], x2
        st1             {v21.8b},     [x0], x2
        st1             {v22.8b},     [x0], x2
        st1             {v23.8b},     [x0], x2

        ret             x10
endfunc
.endm

        h264_qpel8_hv_lowpass put
        h264_qpel8_hv_lowpass avg

.macro  h264_qpel8_hv_lowpass_l2 type
function \type\()_h264_qpel8_hv_lowpass_l2_neon
        mov             x10, x30
        bl              put_h264_qpel8_hv_lowpass_neon_top

        ld1             {v0.8b, v1.8b},  [x2], #16
        ld1             {v2.8b, v3.8b},  [x2], #16
        urhadd          v0.8b,  v0.8b,  v16.8b
        urhadd          v1.8b,  v1.8b,  v17.8b
        ld1             {v4.8b, v5.8b},  [x2], #16
        urhadd          v2.8b,  v2.8b,  v18.8b
        urhadd          v3.8b,  v3.8b,  v19.8b
        ld1             {v6.8b, v7.8b},  [x2], #16
        urhadd          v4.8b,  v4.8b,  v20.8b
        urhadd          v5.8b,  v5.8b,  v21.8b
        urhadd          v6.8b,  v6.8b,  v22.8b
        urhadd          v7.8b,  v7.8b,  v23.8b
  .ifc \type,avg
        ld1             {v16.8b},     [x0], x3
        ld1             {v17.8b},     [x0], x3
        ld1             {v18.8b},     [x0], x3
        urhadd          v0.8b,  v0.8b,  v16.8b
        ld1             {v19.8b},     [x0], x3
        urhadd          v1.8b,  v1.8b,  v17.8b
        ld1             {v20.8b},     [x0], x3
        urhadd          v2.8b,  v2.8b,  v18.8b
        ld1             {v21.8b},     [x0], x3
        urhadd          v3.8b,  v3.8b,  v19.8b
        ld1             {v22.8b},     [x0], x3
        urhadd          v4.8b,  v4.8b,  v20.8b
        ld1             {v23.8b},     [x0], x3
        urhadd          v5.8b,  v5.8b,  v21.8b
        urhadd          v6.8b,  v6.8b,  v22.8b
        urhadd          v7.8b,  v7.8b,  v23.8b
        sub             x0,  x0,  x3,  lsl #3
  .endif
        st1             {v0.8b},      [x0], x3
        st1             {v1.8b},      [x0], x3
        st1             {v2.8b},      [x0], x3
        st1             {v3.8b},      [x0], x3
        st1             {v4.8b},      [x0], x3
        st1             {v5.8b},      [x0], x3
        st1             {v6.8b},      [x0], x3
        st1             {v7.8b},      [x0], x3

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

        h264_qpel16_hv  put
        h264_qpel16_hv  avg

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

        h264_qpel8      put
        h264_qpel8      avg

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

        h264_qpel16     put
        h264_qpel16     avg

//trashes v0-v5
.macro  lowpass_8_10    r0,  r1,  r2,  r3,  d0,  d1
        ext             v2.16b,     \r0\().16b,  \r1\().16b, #4
        ext             v3.16b,     \r0\().16b,  \r1\().16b, #6
        add             v2.8h,      v2.8h,       v3.8h
        ext             v4.16b,     \r0\().16b,  \r1\().16b, #2
        ext             v5.16b,     \r0\().16b,  \r1\().16b, #8
        add             v4.8h,      v4.8h,       v5.8h
        ext             v1.16b,     \r0\().16b,  \r1\().16b, #10

        add             \d0\().8h,  \r0\().8h,   v1.8h
        ext             v0.16b,     \r2\().16b,  \r3\().16b, #4
        mla             \d0\().8h,  v2.8h,       v6.h[1]
        ext             v1.16b,     \r2\().16b,  \r3\().16b, #6
        add             v0.8h,      v0.8h,       v1.8h
        ext             v1.16b,     \r2\().16b,  \r3\().16b, #2
        mul             v5.8h,      v4.8h,       v6.h[0]
        uqsub           \d0\().8h,  \d0\().8h,   v5.8h
        urshr           \d0\().8h,  \d0\().8h,   #5

        ext             v3.16b,     \r2\().16b,  \r3\().16b, #8
        add             v1.8h,      v1.8h,       v3.8h
        ext             v2.16b,     \r2\().16b,  \r3\().16b, #10

        add             \d1\().8h,  \r2\().8h,   v2.8h
        mla             \d1\().8h,  v0.8h,       v6.h[1]
        mul             v5.8h,      v1.8h,       v6.h[0]
        uqsub           \d1\().8h,  \d1\().8h,   v5.8h
        mvni            v5.8h,      #0xFC,       lsl #8 // 1023 for clipping
        urshr           \d1\().8h,  \d1\().8h,   #5

        umin            \d0\().8h,  \d0\().8h,   v5.8h
        umin            \d1\().8h,  \d1\().8h,   v5.8h
.endm

//trashes v0-v4
.macro lowpass_8_10_v   r0,  r1,  r2,  r3,  r4,  r5,  r6,  d0,  d1
        add             v2.8h,      \r2\().8h,   \r3\().8h
        add             v0.8h,      \r3\().8h,   \r4\().8h
        add             v4.8h,      \r1\().8h,   \r4\().8h
        add             v1.8h,      \r2\().8h,   \r5\().8h

        add             \d0\().8h,  \r0\().8h,   \r5\().8h
        add             \d1\().8h,  \r1\().8h,   \r6\().8h
        mla             \d0\().8h,  v2.8h,       v6.h[1]
        mla             \d1\().8h,  v0.8h,       v6.h[1]
        mul             v2.8h,      v4.8h,       v6.h[0]
        mul             v0.8h,      v1.8h,       v6.h[0]
        uqsub           \d0\().8h,  \d0\().8h,   v2.8h
        uqsub           \d1\().8h,  \d1\().8h,   v0.8h

        mvni            v0.8h,      #0xFC,       lsl #8 // 1023 for clipping

        urshr           \d0\().8h,  \d0\().8h,   #5
        urshr           \d1\().8h,  \d1\().8h,   #5

        umin            \d0\().8h,  \d0\().8h,   v0.8h
        umin            \d1\().8h,  \d1\().8h,   v0.8h
.endm

function put_h264_qpel16_h_lowpass_neon_packed_10
        mov             x4,  x30
        mov             x12, #32
        mov             x3,  #16
        bl              put_h264_qpel8_h_lowpass_neon_10
        sub             x1,  x1,  x2, lsl #4
        add             x1,  x1,  #16
        mov             x12, #32
        mov             x30, x4
        b               put_h264_qpel8_h_lowpass_neon_10
endfunc

.macro  h264_qpel_h_lowpass_10 type
function \type\()_h264_qpel16_h_lowpass_neon_10
        mov             x13, x30
        mov             x12, #32
        bl              \type\()_h264_qpel8_h_lowpass_neon_10
        sub             x0,  x0,  x3, lsl #4
        sub             x1,  x1,  x2, lsl #4
        add             x0,  x0,  #16
        add             x1,  x1,  #16
        mov             x12, #32
        mov             x30, x13
endfunc

function \type\()_h264_qpel8_h_lowpass_neon_10
1:      ld1             {v28.8h, v29.8h}, [x1], x2
        ld1             {v16.8h, v17.8h}, [x1], x2
        subs            x12, x12, #4
        lowpass_8_10    v28, v29, v16, v17, v28, v20
  .ifc \type,avg
        ld1             {v2.8h},    [x0], x3
        ld1             {v3.8h},    [x0]
        urhadd          v28.8h, v28.8h, v2.8h
        urhadd          v20.8h, v20.8h, v3.8h
        sub             x0,  x0,  x3
  .endif
        st1             {v28.8h},    [x0], x3
        st1             {v20.8h},    [x0], x3
        b.ne            1b
        ret
endfunc
.endm

        h264_qpel_h_lowpass_10 put
        h264_qpel_h_lowpass_10 avg

.macro h264_qpel_h_lowpass_l2_10 type
function \type\()_h264_qpel16_h_lowpass_l2_neon_10
        mov             x13, x30
        mov             x12, #32
        bl              \type\()_h264_qpel8_h_lowpass_l2_neon_10
        sub             x0,  x0,  x2, lsl #4
        sub             x1,  x1,  x2, lsl #4
        sub             x3,  x3,  x2, lsl #4
        add             x0,  x0,  #16
        add             x1,  x1,  #16
        add             x3,  x3,  #16
        mov             x12, #32
        mov             x30, x13
endfunc

function \type\()_h264_qpel8_h_lowpass_l2_neon_10
1:      ld1             {v26.8h, v27.8h}, [x1], x2
        ld1             {v16.8h, v17.8h}, [x1], x2
        ld1             {v28.8h},     [x3], x2
        ld1             {v29.8h},     [x3], x2
        subs            x12, x12, #4
        lowpass_8_10    v26, v27, v16, v17, v26, v27
        urhadd          v26.8h, v26.8h, v28.8h
        urhadd          v27.8h, v27.8h, v29.8h
  .ifc \type,avg
        ld1             {v2.8h},      [x0], x2
        ld1             {v3.8h},      [x0]
        urhadd          v26.8h, v26.8h, v2.8h
        urhadd          v27.8h, v27.8h, v3.8h
        sub             x0,  x0,  x2
  .endif
        st1             {v26.8h},     [x0], x2
        st1             {v27.8h},     [x0], x2
        b.ne            1b
        ret
endfunc
.endm

        h264_qpel_h_lowpass_l2_10 put
        h264_qpel_h_lowpass_l2_10 avg

function put_h264_qpel16_v_lowpass_neon_packed_10
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

.macro  h264_qpel_v_lowpass_10 type
function \type\()_h264_qpel16_v_lowpass_neon_10
        mov             x4,  x30
        bl              \type\()_h264_qpel8_v_lowpass_neon_10
        sub             x1,  x1,  x3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_neon_10
        sub             x0,  x0,  x2, lsl #4
        add             x0,  x0,  #16
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #16
        bl              \type\()_h264_qpel8_v_lowpass_neon_10
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x4
endfunc

function \type\()_h264_qpel8_v_lowpass_neon_10
        ld1             {v16.8h}, [x1], x3
        ld1             {v17.8h}, [x1], x3
        ld1             {v18.8h}, [x1], x3
        ld1             {v19.8h}, [x1], x3
        ld1             {v20.8h}, [x1], x3
        ld1             {v21.8h}, [x1], x3
        ld1             {v22.8h}, [x1], x3
        ld1             {v23.8h}, [x1], x3
        ld1             {v24.8h}, [x1], x3
        ld1             {v25.8h}, [x1], x3
        ld1             {v26.8h}, [x1], x3
        ld1             {v27.8h}, [x1], x3
        ld1             {v28.8h}, [x1]

        lowpass_8_10_v  v16, v17, v18, v19, v20, v21, v22, v16, v17
        lowpass_8_10_v  v18, v19, v20, v21, v22, v23, v24, v18, v19
        lowpass_8_10_v  v20, v21, v22, v23, v24, v25, v26, v20, v21
        lowpass_8_10_v  v22, v23, v24, v25, v26, v27, v28, v22, v23

  .ifc \type,avg
        ld1             {v24.8h},  [x0], x2
        ld1             {v25.8h}, [x0], x2
        ld1             {v26.8h}, [x0], x2
        urhadd          v16.8h, v16.8h, v24.8h
        ld1             {v27.8h}, [x0], x2
        urhadd          v17.8h, v17.8h, v25.8h
        ld1             {v28.8h}, [x0], x2
        urhadd          v18.8h, v18.8h, v26.8h
        ld1             {v29.8h}, [x0], x2
        urhadd          v19.8h, v19.8h, v27.8h
        ld1             {v30.8h}, [x0], x2
        urhadd          v20.8h, v20.8h, v28.8h
        ld1             {v31.8h}, [x0], x2
        urhadd          v21.8h, v21.8h, v29.8h
        urhadd          v22.8h, v22.8h, v30.8h
        urhadd          v23.8h, v23.8h, v31.8h
        sub             x0,  x0,  x2,  lsl #3
  .endif

        st1             {v16.8h}, [x0], x2
        st1             {v17.8h}, [x0], x2
        st1             {v18.8h}, [x0], x2
        st1             {v19.8h}, [x0], x2
        st1             {v20.8h}, [x0], x2
        st1             {v21.8h}, [x0], x2
        st1             {v22.8h}, [x0], x2
        st1             {v23.8h}, [x0], x2

        ret
endfunc
.endm

        h264_qpel_v_lowpass_10 put
        h264_qpel_v_lowpass_10 avg

.macro  h264_qpel_v_lowpass_l2_10 type
function \type\()_h264_qpel16_v_lowpass_l2_neon_10
        mov             x4,  x30
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon_10
        sub             x1,  x1,  x3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon_10
        sub             x0,  x0,  x3, lsl #4
        sub             x12, x12, x2, lsl #4
        add             x0,  x0,  #16
        add             x12, x12, #16
        sub             x1,  x1,  x3, lsl #4
        sub             x1,  x1,  x3, lsl #2
        add             x1,  x1,  #16
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon_10
        sub             x1,  x1,  x3, lsl #2
        mov             x30, x4
endfunc

function \type\()_h264_qpel8_v_lowpass_l2_neon_10
        ld1             {v16.8h}, [x1], x3
        ld1             {v17.8h}, [x1], x3
        ld1             {v18.8h}, [x1], x3
        ld1             {v19.8h}, [x1], x3
        ld1             {v20.8h}, [x1], x3
        ld1             {v21.8h}, [x1], x3
        ld1             {v22.8h}, [x1], x3
        ld1             {v23.8h}, [x1], x3
        ld1             {v24.8h}, [x1], x3
        ld1             {v25.8h}, [x1], x3
        ld1             {v26.8h}, [x1], x3
        ld1             {v27.8h}, [x1], x3
        ld1             {v28.8h}, [x1]

        lowpass_8_10_v  v16, v17, v18, v19, v20, v21, v22, v16, v17
        lowpass_8_10_v  v18, v19, v20, v21, v22, v23, v24, v18, v19
        lowpass_8_10_v  v20, v21, v22, v23, v24, v25, v26, v20, v21
        lowpass_8_10_v  v22, v23, v24, v25, v26, v27, v28, v22, v23

        ld1             {v24.8h},  [x12], x2
        ld1             {v25.8h},  [x12], x2
        ld1             {v26.8h},  [x12], x2
        ld1             {v27.8h},  [x12], x2
        ld1             {v28.8h},  [x12], x2
        urhadd          v16.8h, v24.8h, v16.8h
        urhadd          v17.8h, v25.8h, v17.8h
        ld1             {v29.8h},  [x12], x2
        urhadd          v18.8h, v26.8h, v18.8h
        urhadd          v19.8h, v27.8h, v19.8h
        ld1             {v30.8h}, [x12], x2
        urhadd          v20.8h, v28.8h, v20.8h
        urhadd          v21.8h, v29.8h, v21.8h
        ld1             {v31.8h}, [x12], x2
        urhadd          v22.8h, v30.8h, v22.8h
        urhadd          v23.8h, v31.8h, v23.8h

  .ifc \type,avg
        ld1             {v24.8h}, [x0], x3
        ld1             {v25.8h}, [x0], x3
        ld1             {v26.8h}, [x0], x3
        urhadd          v16.8h, v16.8h, v24.8h
        ld1             {v27.8h}, [x0], x3
        urhadd          v17.8h, v17.8h, v25.8h
        ld1             {v28.8h}, [x0], x3
        urhadd          v18.8h, v18.8h, v26.8h
        ld1             {v29.8h}, [x0], x3
        urhadd          v19.8h, v19.8h, v27.8h
        ld1             {v30.8h}, [x0], x3
        urhadd          v20.8h, v20.8h, v28.8h
        ld1             {v31.8h}, [x0], x3
        urhadd          v21.8h, v21.8h, v29.8h
        urhadd          v22.8h, v22.8h, v30.8h
        urhadd          v23.8h, v23.8h, v31.8h
        sub             x0,  x0,  x3,  lsl #3
  .endif

        st1             {v16.8h}, [x0], x3
        st1             {v17.8h}, [x0], x3
        st1             {v18.8h}, [x0], x3
        st1             {v19.8h}, [x0], x3
        st1             {v20.8h}, [x0], x3
        st1             {v21.8h}, [x0], x3
        st1             {v22.8h}, [x0], x3
        st1             {v23.8h}, [x0], x3

        ret
endfunc
.endm

        h264_qpel_v_lowpass_l2_10 put
        h264_qpel_v_lowpass_l2_10 avg

.macro  h264_qpel8_10   type
function ff_\type\()_h264_qpel8_mc10_neon_10, export=1
        lowpass_const   w3
        mov             x3,  x1
        sub             x1,  x1,  #4
        mov             x12, #16
        b               \type\()_h264_qpel8_h_lowpass_l2_neon_10
endfunc

function ff_\type\()_h264_qpel8_mc20_neon_10, export=1
        lowpass_const   w3
        sub             x1,  x1,  #4
        mov             x3,  x2
        mov             x12, #16
        b               \type\()_h264_qpel8_h_lowpass_neon_10
endfunc

function ff_\type\()_h264_qpel8_mc30_neon_10, export=1
        lowpass_const   w3
        add             x3,  x1,  #2
        sub             x1,  x1,  #4
        mov             x12, #16
        b               \type\()_h264_qpel8_h_lowpass_l2_neon_10
endfunc

function ff_\type\()_h264_qpel8_mc01_neon_10, export=1
        mov             x14, x30
        mov             x12, x1
\type\()_h264_qpel8_mc01_10:
        lowpass_const   w3
        mov             x3,  x2
        sub             x1,  x1,  x2, lsl #1
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon_10
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc11_neon_10, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel8_mc11_10:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #128
        mov             x0,  sp
        sub             x1,  x1,  #4
        mov             x3,  #16
        mov             x12, #16
        bl              put_h264_qpel8_h_lowpass_neon_10
        mov             x0,  x8
        mov             x3,  x2
        mov             x12, sp
        sub             x1,  x9,  x2, lsl #1
        mov             x2,  #16
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon_10
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc31_neon_10, export=1
        add             x1,  x1,  #2
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        sub             x1,  x1,  #2
        b               \type\()_h264_qpel8_mc11_10
endfunc

function ff_\type\()_h264_qpel8_mc02_neon_10, export=1
        mov             x14, x30
        lowpass_const   w3
        sub             x1,  x1,  x2, lsl #1
        mov             x3,  x2
        bl              \type\()_h264_qpel8_v_lowpass_neon_10
        ret             x14
endfunc

function ff_\type\()_h264_qpel8_mc03_neon_10, export=1
        mov             x14, x30
        add             x12, x1,  x2
        b               \type\()_h264_qpel8_mc01_10
endfunc

function ff_\type\()_h264_qpel8_mc13_neon_10, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        b               \type\()_h264_qpel8_mc11_10
endfunc

function ff_\type\()_h264_qpel8_mc33_neon_10, export=1
        add             x1,  x1,  #2
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        sub             x1,  x1,  #2
        b               \type\()_h264_qpel8_mc11_10
endfunc
.endm

        h264_qpel8_10   put
        h264_qpel8_10   avg

.macro  h264_qpel16_10     type
function ff_\type\()_h264_qpel16_mc10_neon_10, export=1
        lowpass_const   w3
        mov             x3,  x1
        sub             x1,  x1,  #4
        b               \type\()_h264_qpel16_h_lowpass_l2_neon_10
endfunc

function ff_\type\()_h264_qpel16_mc20_neon_10, export=1
        lowpass_const   w3
        sub             x1,  x1,  #4
        mov             x3,  x2
        b               \type\()_h264_qpel16_h_lowpass_neon_10
endfunc

function ff_\type\()_h264_qpel16_mc30_neon_10, export=1
        lowpass_const   w3
        add             x3,  x1,  #2
        sub             x1,  x1,  #4
        b               \type\()_h264_qpel16_h_lowpass_l2_neon_10
endfunc

function ff_\type\()_h264_qpel16_mc01_neon_10, export=1
        mov             x14, x30
        mov             x12, x1
\type\()_h264_qpel16_mc01_10:
        lowpass_const   w3
        mov             x3,  x2
        sub             x1,  x1,  x2, lsl #1
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon_10
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc11_neon_10, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
\type\()_h264_qpel16_mc11_10:
        lowpass_const   w3
        mov             x11, sp
        sub             sp,  sp,  #512
        mov             x0,  sp
        sub             x1,  x1,  #4
        mov             x3,  #32
        bl              put_h264_qpel16_h_lowpass_neon_10
        mov             x0,  x8
        mov             x3,  x2
        mov             x12, sp
        sub             x1,  x9,  x2, lsl #1
        mov             x2,  #32
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon_10
        mov             sp,  x11
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc31_neon_10, export=1
        add             x1,  x1,  #2
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        sub             x1,  x1,  #2
        b               \type\()_h264_qpel16_mc11_10
endfunc

function ff_\type\()_h264_qpel16_mc02_neon_10, export=1
        mov             x14, x30
        lowpass_const   w3
        sub             x1,  x1,  x2, lsl #1
        mov             x3,  x2
        bl              \type\()_h264_qpel16_v_lowpass_neon_10
        ret             x14
endfunc

function ff_\type\()_h264_qpel16_mc03_neon_10, export=1
        mov             x14, x30
        add             x12, x1,  x2
        b               \type\()_h264_qpel16_mc01_10
endfunc

function ff_\type\()_h264_qpel16_mc13_neon_10, export=1
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        b               \type\()_h264_qpel16_mc11_10
endfunc

function ff_\type\()_h264_qpel16_mc33_neon_10, export=1
        add             x1,  x1,  #2
        mov             x14, x30
        mov             x8,  x0
        mov             x9,  x1
        add             x1,  x1,  x2
        sub             x1,  x1,  #2
        b               \type\()_h264_qpel16_mc11_10
endfunc
.endm

        h264_qpel16_10  put
        h264_qpel16_10  avg
