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


// The input to and output from this macro is in the registers v16-v31,
// and v0-v7 are used as scratch registers.
// p7 = v16 .. p3 = v20, p0 = v23, q0 = v24, q3 = v27, q7 = v31
// Depending on the width of the loop filter, we either use v16-v19
// and v28-v31 as temp registers, or v8-v15.
.macro loop_filter wd, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8
        dup             v0.8h,  w2                   // E
        dup             v2.8h,  w3                   // I
        dup             v3.8h,  w4                   // H

        uabd            v4.8h,  v20.8h, v21.8h       // abs(p3 - p2)
        uabd            v5.8h,  v21.8h, v22.8h       // abs(p2 - p1)
        uabd            v6.8h,  v22.8h, v23.8h       // abs(p1 - p0)
        uabd            v7.8h,  v24.8h, v25.8h       // abs(q0 - q1)
        uabd            \tmp1\().8h,  v25.8h, v26.8h // abs(q1 - q2)
        uabd            \tmp2\().8h,  v26.8h, v27.8h // abs(q2 - q3)
        umax            v4.8h,  v4.8h,  v5.8h
        umax            v5.8h,  v6.8h,  v7.8h
        umax            \tmp1\().8h,  \tmp1\().8h, \tmp2\().8h
        uabd            v6.8h,  v23.8h, v24.8h       // abs(p0 - q0)
        umax            v4.8h,  v4.8h,  v5.8h
        add             v6.8h,  v6.8h,  v6.8h        // abs(p0 - q0) * 2
        uabd            v5.8h,  v22.8h, v25.8h       // abs(p1 - q1)
        umax            v4.8h,  v4.8h,  \tmp1\().8h  // max(abs(p3 - p2), ..., abs(q2 - q3))
        ushr            v5.8h,  v5.8h,  #1
        cmhs            v4.8h,  v2.8h,  v4.8h        // max(abs()) <= I
        add             v6.8h,  v6.8h,  v5.8h        // abs(p0 - q0) * 2 + abs(p1 - q1) >> 1
        cmhs            v6.8h,  v0.8h,  v6.8h
        and             v4.16b, v4.16b, v6.16b       // fm

        // If no pixels need filtering, just exit as soon as possible
        mov             x11, v4.d[0]
        mov             x12, v4.d[1]
        adds            x11, x11, x12
        b.ne            1f
        ret             x10
1:

.if \wd >= 8
        dup             v0.8h,  w5

        uabd            v6.8h,  v20.8h, v23.8h       // abs(p3 - p0)
        uabd            v2.8h,  v21.8h, v23.8h       // abs(p2 - p0)
        uabd            v1.8h,  v22.8h, v23.8h       // abs(p1 - p0)
        uabd            \tmp1\().8h,  v25.8h, v24.8h // abs(q1 - q0)
        uabd            \tmp2\().8h,  v26.8h, v24.8h // abs(q2 - q0)
        uabd            \tmp3\().8h,  v27.8h, v24.8h // abs(q3 - q0)
        umax            v6.8h,  v6.8h,  v2.8h
        umax            v1.8h,  v1.8h,  \tmp1\().8h
        umax            \tmp2\().8h,  \tmp2\().8h,  \tmp3\().8h
.if \wd == 16
        uabd            v7.8h,  v16.8h, v23.8h       // abs(p7 - p0)
        umax            v6.8h,  v6.8h,  v1.8h
        uabd            v2.8h,  v17.8h, v23.8h       // abs(p6 - p0)
        umax            v6.8h,  v6.8h,  \tmp2\().8h
        uabd            v1.8h,  v18.8h, v23.8h       // abs(p5 - p0)
        cmhs            v6.8h,  v0.8h,  v6.8h        // flat8in
        uabd            v8.8h,  v19.8h, v23.8h       // abs(p4 - p0)
        and             v6.16b, v6.16b, v4.16b       // flat8in && fm
        uabd            v9.8h,  v28.8h, v24.8h       // abs(q4 - q0)
        bic             v4.16b, v4.16b, v6.16b       // fm && !flat8in
        uabd            v10.8h, v29.8h, v24.8h       // abs(q5 - q0)
        uabd            v11.8h, v30.8h, v24.8h       // abs(q6 - q0)
        uabd            v12.8h, v31.8h, v24.8h       // abs(q7 - q0)

        umax            v7.8h,  v7.8h,  v2.8h
        umax            v1.8h,  v1.8h,  v8.8h
        umax            v9.8h,  v9.8h,  v10.8h
        umax            v11.8h, v11.8h, v12.8h
        // The rest of the calculation of flat8out is interleaved below
.else
        // The rest of the calculation of flat8in is interleaved below
.endif
.endif

        // Calculate the normal inner loop filter for 2 or 4 pixels
        uabd            v5.8h,  v22.8h, v23.8h                  // abs(p1 - p0)
.if \wd == 16
        umax            v7.8h,  v7.8h,  v1.8h
        umax            v9.8h,  v9.8h,  v11.8h
.elseif \wd == 8
        umax            v6.8h,  v6.8h,  v1.8h
.endif
        uabd            v1.8h,  v25.8h, v24.8h                  // abs(q1 - q0)
.if \wd == 16
        umax            v7.8h,  v7.8h,  v9.8h
.elseif \wd == 8
        umax            v6.8h,  v6.8h,  \tmp2\().8h
.endif
        dup             \tmp2\().8h,  w6                        // left shift for saturation
        sub             \tmp1\().8h,  v22.8h,  v25.8h           // p1 - q1
        neg             \tmp6\().8h,  \tmp2\().8h               // negative left shift after saturation
        umax            v5.8h,  v5.8h,  v1.8h                   // max(abs(p1 - p0), abs(q1 - q0))
        sub             \tmp3\().8h,  v24.8h,  v23.8h           // q0 - p0
        movi            \tmp5\().8h,  #3
.if \wd == 8
        cmhs            v6.8h,  v0.8h,  v6.8h                   // flat8in
.endif
        cmhs            v5.8h,  v3.8h,  v5.8h                   // !hev
.if \wd == 8
        and             v6.16b, v6.16b, v4.16b                  // flat8in && fm
.endif
        sqshl           \tmp1\().8h,  \tmp1\().8h,  \tmp2\().8h
.if \wd == 16
        cmhs            v7.8h,  v0.8h,  v7.8h                   // flat8out
.elseif \wd == 8
        bic             v4.16b, v4.16b, v6.16b                  // fm && !flat8in
.endif
        and             v5.16b,  v5.16b,  v4.16b                // !hev && fm && !flat8in
.if \wd == 16
        and             v7.16b, v7.16b, v6.16b                  // flat8out && flat8in && fm
.endif
        sshl            \tmp1\().8h,  \tmp1\().8h,  \tmp6\().8h // av_clip_int2p(p1 - q1, BIT_DEPTH - 1)

        mul             \tmp3\().8h,  \tmp3\().8h,  \tmp5\().8h // 3 * (q0 - p0)
        bic             \tmp1\().16b, \tmp1\().16b, v5.16b      // if (!hev) av_clip_int8 = 0
        movi            v2.8h,  #4
        add             \tmp3\().8h,  \tmp3\().8h,  \tmp1\().8h // 3 * (q0 - p0) [+ av_clip_int8(p1 - q1)]
        movi            v3.8h,  #3
        sqshl           \tmp1\().8h,  \tmp3\().8h,  \tmp2\().8h
        movi            \tmp5\().8h,  #0
        sshl            \tmp1\().8h,  \tmp1\().8h,  \tmp6\().8h // av_clip_int2p(3 * (q0 - p0) [+ av_clip_int2p(p1 - q1)], BIT_DEPTH - 1) = f
        dup             \tmp6\().8h,  w7                        // max pixel value
.if \wd == 16
        bic             v6.16b, v6.16b, v7.16b                  // fm && flat8in && !flat8out
.endif

        ushr            \tmp2\().8h,  \tmp6\().8h,  #1          // (1 << (BIT_DEPTH - 1)) - 1

        add             \tmp3\().8h,  \tmp1\().8h,  v2.8h       // f + 4
        add             \tmp4\().8h,  \tmp1\().8h,  v3.8h       // f + 3
        smin            \tmp3\().8h,  \tmp3\().8h,  \tmp2\().8h // FFMIN(f + 4, (1 << (BIT_DEPTH - 1)) - 1)
        smin            \tmp4\().8h,  \tmp4\().8h,  \tmp2\().8h // FFMIN(f + 3, (1 << (BIT_DEPTH - 1)) - 1)
        sshr            \tmp3\().8h,  \tmp3\().8h,  #3          // f1
        sshr            \tmp4\().8h,  \tmp4\().8h,  #3          // f2

        add             v0.8h,   v23.8h,  \tmp4\().8h           // p0 + f2
        sub             v2.8h,   v24.8h,  \tmp3\().8h           // q0 - f1
        smin            v0.8h,   v0.8h,   \tmp6\().8h
        smin            v2.8h,   v2.8h,   \tmp6\().8h
        srshr           \tmp3\().8h, \tmp3\().8h, #1            // f = (f1 + 1) >> 1
        smax            v0.8h,   v0.8h,   \tmp5\().8h           // out p0
        smax            v2.8h,   v2.8h,   \tmp5\().8h           // out q0
        bit             v23.16b, v0.16b,  v4.16b                // if (fm && !flat8in)
        bit             v24.16b, v2.16b,  v4.16b

        add             v0.8h,  v22.8h,  \tmp3\().8h            // p1 + f
        sub             v2.8h,  v25.8h,  \tmp3\().8h            // q1 - f
.if \wd >= 8
        mov             x11, v6.d[0]
.endif
        smin            v0.8h,  v0.8h,  \tmp6\().8h
        smin            v2.8h,  v2.8h,  \tmp6\().8h
.if \wd >= 8
        mov             x12, v6.d[1]
.endif
        smax            v0.8h,  v0.8h,  \tmp5\().8h             // out p1
        smax            v2.8h,  v2.8h,  \tmp5\().8h             // out q1
.if \wd >= 8
        adds            x11, x11, x12
.endif
        bit             v22.16b, v0.16b,  v5.16b                // if (!hev && fm && !flat8in)
        bit             v25.16b, v2.16b,  v5.16b

        // If no pixels need flat8in, jump to flat8out
        // (or to a writeout of the inner 4 pixels, for wd=8)
.if \wd >= 8
.if \wd == 16
        b.eq            6f
.else
        b.ne            1f
        ret             x13
1:
.endif

        // flat8in
        add             \tmp1\().8h, v20.8h, v21.8h
        add             \tmp3\().8h, v22.8h, v25.8h
        add             \tmp5\().8h, v20.8h, v22.8h
        add             \tmp7\().8h, v23.8h, v26.8h
        add             v0.8h,  \tmp1\().8h, \tmp1\().8h
        add             v0.8h,  v0.8h,  v23.8h
        add             v0.8h,  v0.8h,  v24.8h
        add             v0.8h,  v0.8h,  \tmp5\().8h
        sub             \tmp3\().8h, \tmp3\().8h, \tmp1\().8h
        sub             \tmp7\().8h, \tmp7\().8h, \tmp5\().8h
        urshr           v2.8h,  v0.8h,  #3                      // out p2

        add             v0.8h,  v0.8h,  \tmp3\().8h
        add             \tmp1\().8h, v20.8h,  v23.8h
        add             \tmp3\().8h, v24.8h,  v27.8h
        urshr           v3.8h,  v0.8h,  #3                      // out p1

        add             v0.8h,  v0.8h,  \tmp7\().8h
        sub             \tmp3\().8h, \tmp3\().8h, \tmp1\().8h
        add             \tmp5\().8h, v21.8h,  v24.8h
        add             \tmp7\().8h, v25.8h,  v27.8h
        urshr           v4.8h,  v0.8h,  #3                      // out p0

        add             v0.8h,  v0.8h,  \tmp3\().8h
        sub             \tmp7\().8h, \tmp7\().8h, \tmp5\().8h
        add             \tmp1\().8h, v22.8h,  v25.8h
        add             \tmp3\().8h, v26.8h,  v27.8h
        urshr           v5.8h,  v0.8h,  #3                      // out q0

        add             v0.8h,  v0.8h,  \tmp7\().8h
        sub             \tmp3\().8h, \tmp3\().8h, \tmp1\().8h
        urshr           \tmp5\().8h, v0.8h,  #3                 // out q1

        add             v0.8h,  v0.8h,  \tmp3\().8h
        // The output here is written back into the input registers. This doesn't
        // matter for the flat8part below, since we only update those pixels
        // which won't be touched below.
        bit             v21.16b, v2.16b,  v6.16b
        bit             v22.16b, v3.16b,  v6.16b
        bit             v23.16b, v4.16b,  v6.16b
        urshr           \tmp6\().8h,  v0.8h,  #3                // out q2
        bit             v24.16b, v5.16b,  v6.16b
        bit             v25.16b, \tmp5\().16b,  v6.16b
        bit             v26.16b, \tmp6\().16b,  v6.16b
.endif
.if \wd == 16
6:
        orr             v2.16b,  v6.16b,  v7.16b
        mov             x11, v2.d[0]
        mov             x12, v2.d[1]
        adds            x11, x11, x12
        b.ne            1f
        // If no pixels needed flat8in nor flat8out, jump to a
        // writeout of the inner 4 pixels
        ret             x14
1:

        mov             x11, v7.d[0]
        mov             x12, v7.d[1]
        adds            x11, x11, x12
        b.ne            1f
        // If no pixels need flat8out, jump to a writeout of the inner 6 pixels
        ret             x15

1:
        // flat8out
        // This writes all outputs into v2-v17 (skipping v6 and v16).
        // If this part is skipped, the output is read from v21-v26 (which is the input
        // to this section).
        shl             v0.8h,   v16.8h,  #3     // 8 * v16
        sub             v0.8h,   v0.8h,   v16.8h // 7 * v16
        add             v0.8h,   v0.8h,   v17.8h
        add             v8.8h,   v17.8h,  v18.8h
        add             v10.8h,  v19.8h,  v20.8h
        add             v0.8h,   v0.8h,   v8.8h
        add             v8.8h,   v16.8h,  v17.8h
        add             v12.8h,  v21.8h,  v22.8h
        add             v0.8h,   v0.8h,   v10.8h
        add             v10.8h,  v18.8h,  v25.8h
        add             v14.8h,  v23.8h,  v24.8h
        sub             v10.8h,  v10.8h,  v8.8h
        add             v0.8h,   v0.8h,   v12.8h
        add             v0.8h,   v0.8h,   v14.8h
        add             v12.8h,  v16.8h,  v18.8h
        add             v14.8h,  v19.8h,  v26.8h
        urshr           v2.8h,   v0.8h,   #4

        add             v0.8h,   v0.8h,   v10.8h
        add             v8.8h,   v16.8h,  v19.8h
        add             v10.8h,  v20.8h,  v27.8h
        sub             v14.8h,  v14.8h,  v12.8h
        bif             v2.16b,  v17.16b, v7.16b
        urshr           v3.8h ,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v14.8h
        add             v12.8h,  v16.8h,  v20.8h
        add             v14.8h,  v21.8h,  v28.8h
        sub             v10.8h,  v10.8h,  v8.8h
        bif             v3.16b,  v18.16b, v7.16b
        urshr           v4.8h,   v0.8h,   #4

        add             v0.8h,   v0.8h,   v10.8h
        add             v8.8h,   v16.8h,  v21.8h
        add             v10.8h,  v22.8h,  v29.8h
        sub             v14.8h,  v14.8h,  v12.8h
        bif             v4.16b,  v19.16b, v7.16b
        urshr           v5.8h,   v0.8h,   #4

        add             v0.8h,   v0.8h,   v14.8h
        add             v12.8h,  v16.8h,  v22.8h
        add             v14.8h,  v23.8h,  v30.8h
        sub             v10.8h,  v10.8h,  v8.8h
        bif             v5.16b,  v20.16b, v7.16b
        urshr           v6.8h,   v0.8h,   #4

        add             v0.8h,   v0.8h,   v10.8h
        add             v10.8h,  v16.8h,  v23.8h
        sub             v14.8h,  v14.8h,  v12.8h
        add             v12.8h,  v24.8h,  v31.8h
        bif             v6.16b,  v21.16b, v7.16b
        urshr           v8.8h,   v0.8h,   #4

        add             v0.8h,   v0.8h,   v14.8h
        sub             v10.8h,  v12.8h,  v10.8h
        add             v12.8h,  v17.8h,  v24.8h
        add             v14.8h,  v25.8h,  v31.8h
        bif             v8.16b,  v22.16b, v7.16b
        urshr           v9.8h,   v0.8h,   #4

        add             v0.8h,   v0.8h,   v10.8h
        sub             v14.8h,  v14.8h,  v12.8h
        add             v12.8h,  v26.8h,  v31.8h
        bif             v9.16b,  v23.16b, v7.16b
        urshr           v10.8h,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v14.8h
        add             v14.8h,  v18.8h,  v25.8h
        add             v18.8h,  v19.8h,  v26.8h
        sub             v12.8h,  v12.8h,  v14.8h
        add             v14.8h,  v27.8h,  v31.8h
        bif             v10.16b, v24.16b, v7.16b
        urshr           v11.8h,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v12.8h
        add             v12.8h,  v20.8h,  v27.8h
        sub             v14.8h,  v14.8h,  v18.8h
        add             v18.8h,  v28.8h,  v31.8h
        bif             v11.16b, v25.16b, v7.16b
        sub             v18.8h,  v18.8h,  v12.8h
        urshr           v12.8h,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v14.8h
        add             v14.8h,  v21.8h,  v28.8h
        add             v20.8h,  v29.8h,  v31.8h
        bif             v12.16b, v26.16b, v7.16b
        urshr           v13.8h,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v18.8h
        sub             v20.8h,  v20.8h,  v14.8h
        add             v18.8h,  v22.8h,  v29.8h
        add             v22.8h,  v30.8h,  v31.8h
        bif             v13.16b, v27.16b, v7.16b
        urshr           v14.8h,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v20.8h
        sub             v22.8h,  v22.8h,  v18.8h
        bif             v14.16b, v28.16b, v7.16b
        urshr           v15.8h,  v0.8h,   #4

        add             v0.8h,   v0.8h,   v22.8h
        bif             v15.16b, v29.16b, v7.16b
        urshr           v17.8h,  v0.8h,   #4
        bif             v17.16b, v30.16b, v7.16b
.endif
.endm

// For wd <= 8, we use v16-v19 and v28-v31 for temp registers,
// while we need those for inputs/outputs in wd=16 and use v8-v15
// for temp registers there instead.
function vp9_loop_filter_4
        loop_filter     4,  v16, v17, v18, v19, v28, v29, v30, v31
        ret
endfunc

function vp9_loop_filter_8
        loop_filter     8,  v16, v17, v18, v19, v28, v29, v30, v31
        ret
endfunc

function vp9_loop_filter_16
        loop_filter     16, v8,  v9,  v10, v11, v12, v13, v14, v15
        ret
endfunc

.macro loop_filter_4
        bl              vp9_loop_filter_4
.endm

.macro loop_filter_8
        // calculate alternative 'return' targets
        adr             x13, 6f
        bl              vp9_loop_filter_8
.endm

.macro loop_filter_16
        // calculate alternative 'return' targets
        adr             x14, 7f
        adr             x15, 8f
        bl              vp9_loop_filter_16
.endm


// The public functions in this file have got the following signature:
// void loop_filter(uint8_t *dst, ptrdiff_t stride, int mb_lim, int lim, int hev_thr);

.macro bpp_frontend func, bpp, push
function ff_\func\()_\bpp\()_neon, export=1
.if \push
        mov             x16, x30
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
.endif
        lsl             w2,  w2,  #\bpp - 8
        lsl             w3,  w3,  #\bpp - 8
        lsl             w4,  w4,  #\bpp - 8
        mov             x5,  #1 << (\bpp - 8)
        mov             x6,  #16 - \bpp
        mov             x7,  #((1 << \bpp) - 1)
.if \push
        bl              \func\()_16_neon
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x16
.else
        b               \func\()_16_neon
.endif
endfunc
.endm

.macro bpp_frontends func, push=0
        bpp_frontend    \func, 10, \push
        bpp_frontend    \func, 12, \push
.endm

.macro bpp_frontend_rep func, suffix, int_suffix, dir, bpp, push
function ff_\func\()_\suffix\()_\bpp\()_neon, export=1
        mov             x16, x30
.if \push
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
.endif
        lsl             w2,  w2,  #\bpp - 8
        lsl             w3,  w3,  #\bpp - 8
        lsl             w4,  w4,  #\bpp - 8
        mov             x5,  #1 << (\bpp - 8)
        mov             x6,  #16 - \bpp
        mov             x7,  #((1 << \bpp) - 1)
        bl              \func\()_\int_suffix\()_16_neon
.ifc \dir,h
        add             x0,  x0,  x1, lsl #3
.else
        add             x0,  x0,  #16
.endif
        bl              \func\()_\int_suffix\()_16_neon
.if \push
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
.endif
        ret             x16
endfunc
.endm

.macro bpp_frontends_rep func, suffix, int_suffix, dir, push=0
        bpp_frontend_rep \func, \suffix, \int_suffix, \dir, 10, \push
        bpp_frontend_rep \func, \suffix, \int_suffix, \dir, 12, \push
.endm

.macro bpp_frontend_mix2 wd1, wd2, dir, bpp
function ff_vp9_loop_filter_\dir\()_\wd1\()\wd2\()_16_\bpp\()_neon, export=1
        mov             x16, x30
        lsr             w8,  w2,  #8
        lsr             w14, w3,  #8
        lsr             w15, w4,  #8
        and             w2,  w2,  #0xff
        and             w3,  w3,  #0xff
        and             w4,  w4,  #0xff
        lsl             w2,  w2,  #\bpp - 8
        lsl             w3,  w3,  #\bpp - 8
        lsl             w4,  w4,  #\bpp - 8
        mov             x5,  #1 << (\bpp - 8)
        mov             x6,  #16 - \bpp
        mov             x7,  #((1 << \bpp) - 1)
        bl              vp9_loop_filter_\dir\()_\wd1\()_8_16_neon
.ifc \dir,h
        add             x0,  x0,  x1, lsl #3
.else
        add             x0,  x0,  #16
.endif
        lsl             w2,  w8,  #\bpp - 8
        lsl             w3,  w14, #\bpp - 8
        lsl             w4,  w15, #\bpp - 8
        bl              vp9_loop_filter_\dir\()_\wd2\()_8_16_neon
        ret             x16
endfunc
.endm

.macro bpp_frontends_mix2 wd1, wd2
        bpp_frontend_mix2 \wd1, \wd2, v, 10
        bpp_frontend_mix2 \wd1, \wd2, v, 12
        bpp_frontend_mix2 \wd1, \wd2, h, 10
        bpp_frontend_mix2 \wd1, \wd2, h, 12
.endm

function vp9_loop_filter_v_4_8_16_neon
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #2
        ld1             {v20.8h}, [x9], x1 // p3
        ld1             {v24.8h}, [x0], x1 // q0
        ld1             {v21.8h}, [x9], x1 // p2
        ld1             {v25.8h}, [x0], x1 // q1
        ld1             {v22.8h}, [x9], x1 // p1
        ld1             {v26.8h}, [x0], x1 // q2
        ld1             {v23.8h}, [x9], x1 // p0
        ld1             {v27.8h}, [x0], x1 // q3
        sub             x0,  x0,  x1, lsl #2
        sub             x9,  x9,  x1, lsl #1

        loop_filter_4

        st1             {v22.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #1

        ret             x10
endfunc

bpp_frontends vp9_loop_filter_v_4_8

function vp9_loop_filter_h_4_8_16_neon
        mov             x10, x30
        sub             x9,  x0,  #8
        add             x0,  x9,  x1, lsl #2
        ld1             {v20.8h}, [x9], x1
        ld1             {v24.8h}, [x0], x1
        ld1             {v21.8h}, [x9], x1
        ld1             {v25.8h}, [x0], x1
        ld1             {v22.8h}, [x9], x1
        ld1             {v26.8h}, [x0], x1
        ld1             {v23.8h}, [x9], x1
        ld1             {v27.8h}, [x0], x1

        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #8

        transpose_8x8H  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        loop_filter_4

        // Move x9 forward by 2 pixels; we don't need to rewrite the
        // outermost 2 pixels since they aren't changed.
        add             x9,  x9,  #4
        add             x0,  x9,  x1, lsl #2

        // We only will write the mid 4 pixels back; after the loop filter,
        // these are in v22, v23, v24, v25, ordered as rows (8x4 pixels).
        // We need to transpose them to columns, done with a 4x8 transpose
        // (which in practice is two 4x4 transposes of the two 4x4 halves
        // of the 8x4 pixels; into 4x8 pixels).
        transpose_4x8H  v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.d}[0], [x9], x1
        st1             {v22.d}[1], [x0], x1
        st1             {v23.d}[0], [x9], x1
        st1             {v23.d}[1], [x0], x1
        st1             {v24.d}[0], [x9], x1
        st1             {v24.d}[1], [x0], x1
        st1             {v25.d}[0], [x9], x1
        st1             {v25.d}[1], [x0], x1
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #4

        ret             x10
endfunc

bpp_frontends vp9_loop_filter_h_4_8

function vp9_loop_filter_v_8_8_16_neon
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #2
        ld1             {v20.8h}, [x9], x1 // p3
        ld1             {v24.8h}, [x0], x1 // q0
        ld1             {v21.8h}, [x9], x1 // p2
        ld1             {v25.8h}, [x0], x1 // q1
        ld1             {v22.8h}, [x9], x1 // p1
        ld1             {v26.8h}, [x0], x1 // q2
        ld1             {v23.8h}, [x9], x1 // p0
        ld1             {v27.8h}, [x0], x1 // q3
        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #2
        add             x9,  x9,  x1

        loop_filter_8

        st1             {v21.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v22.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v26.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #1
        sub             x0,  x0,  x1

        ret             x10
6:
        sub             x9,  x0,  x1, lsl #1
        st1             {v22.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #1
        ret             x10
endfunc

bpp_frontends vp9_loop_filter_v_8_8

function vp9_loop_filter_h_8_8_16_neon
        mov             x10, x30
        sub             x9,  x0,  #8
        add             x0,  x9,  x1, lsl #2
        ld1             {v20.8h}, [x9], x1
        ld1             {v24.8h}, [x0], x1
        ld1             {v21.8h}, [x9], x1
        ld1             {v25.8h}, [x0], x1
        ld1             {v22.8h}, [x9], x1
        ld1             {v26.8h}, [x0], x1
        ld1             {v23.8h}, [x9], x1
        ld1             {v27.8h}, [x0], x1

        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #8

        transpose_8x8H  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        loop_filter_8

        add             x0,  x9,  x1, lsl #2

        // Even though only 6 pixels per row have been changed, we write the
        // full 8 pixel registers.
        transpose_8x8H  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v20.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v21.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        st1             {v22.8h}, [x9], x1
        st1             {v26.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v27.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #8

        ret             x10
6:
        // If we didn't need to do the flat8in part, we use the same writeback
        // as in loop_filter_h_4_8.
        add             x9,  x9,  #4
        add             x0,  x9,  x1, lsl #2
        transpose_4x8H  v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.d}[0], [x9], x1
        st1             {v22.d}[1], [x0], x1
        st1             {v23.d}[0], [x9], x1
        st1             {v23.d}[1], [x0], x1
        st1             {v24.d}[0], [x9], x1
        st1             {v24.d}[1], [x0], x1
        st1             {v25.d}[0], [x9], x1
        st1             {v25.d}[1], [x0], x1
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #4
        ret             x10
endfunc

bpp_frontends vp9_loop_filter_h_8_8

bpp_frontends_mix2 4, 4
bpp_frontends_mix2 4, 8
bpp_frontends_mix2 8, 4
bpp_frontends_mix2 8, 8

function vp9_loop_filter_v_16_8_16_neon
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #3
        ld1             {v16.8h}, [x9], x1 // p7
        ld1             {v24.8h}, [x0], x1 // q0
        ld1             {v17.8h}, [x9], x1 // p6
        ld1             {v25.8h}, [x0], x1 // q1
        ld1             {v18.8h}, [x9], x1 // p5
        ld1             {v26.8h}, [x0], x1 // q2
        ld1             {v19.8h}, [x9], x1 // p4
        ld1             {v27.8h}, [x0], x1 // q3
        ld1             {v20.8h}, [x9], x1 // p3
        ld1             {v28.8h}, [x0], x1 // q4
        ld1             {v21.8h}, [x9], x1 // p2
        ld1             {v29.8h}, [x0], x1 // q5
        ld1             {v22.8h}, [x9], x1 // p1
        ld1             {v30.8h}, [x0], x1 // q6
        ld1             {v23.8h}, [x9], x1 // p0
        ld1             {v31.8h}, [x0], x1 // q7
        sub             x9,  x9,  x1, lsl #3
        sub             x0,  x0,  x1, lsl #3
        add             x9,  x9,  x1

        loop_filter_16

        // If we did the flat8out part, we get the output in
        // v2-v17 (skipping v7 and v16). x9 points to x0 - 7 * stride,
        // store v2-v9 there, and v10-v17 into x0.
        st1             {v2.8h},  [x9], x1
        st1             {v10.8h}, [x0], x1
        st1             {v3.8h},  [x9], x1
        st1             {v11.8h}, [x0], x1
        st1             {v4.8h},  [x9], x1
        st1             {v12.8h}, [x0], x1
        st1             {v5.8h},  [x9], x1
        st1             {v13.8h}, [x0], x1
        st1             {v6.8h},  [x9], x1
        st1             {v14.8h}, [x0], x1
        st1             {v8.8h},  [x9], x1
        st1             {v15.8h}, [x0], x1
        st1             {v9.8h},  [x9], x1
        st1             {v17.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  x1

        ret             x10
8:
        add             x9,  x9,  x1, lsl #2
        // If we didn't do the flat8out part, the output is left in the
        // input registers.
        st1             {v21.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v22.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v26.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #1
        sub             x0,  x0,  x1
        ret             x10
7:
        sub             x9,  x0,  x1, lsl #1
        st1             {v22.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #1
        ret             x10
endfunc

bpp_frontends vp9_loop_filter_v_16_8, push=1
bpp_frontends_rep vp9_loop_filter_v_16, 16, 8, v, push=1

function vp9_loop_filter_h_16_8_16_neon
        mov             x10, x30
        sub             x9,  x0,  #16
        ld1             {v16.8h}, [x9], x1
        ld1             {v24.8h}, [x0], x1
        ld1             {v17.8h}, [x9], x1
        ld1             {v25.8h}, [x0], x1
        ld1             {v18.8h}, [x9], x1
        ld1             {v26.8h}, [x0], x1
        ld1             {v19.8h}, [x9], x1
        ld1             {v27.8h}, [x0], x1
        ld1             {v20.8h}, [x9], x1
        ld1             {v28.8h}, [x0], x1
        ld1             {v21.8h}, [x9], x1
        ld1             {v29.8h}, [x0], x1
        ld1             {v22.8h}, [x9], x1
        ld1             {v30.8h}, [x0], x1
        ld1             {v23.8h}, [x9], x1
        ld1             {v31.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #3
        sub             x9,  x9,  x1, lsl #3

        // The 16x8 pixels read above is in two 8x8 blocks; the left
        // half in v16-v23, and the right half in v24-v31. Do two 8x8 transposes
        // of this, to get one column per register.
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v0, v1
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v30, v31, v0, v1

        loop_filter_16

        transpose_8x8H  v16, v2,  v3,  v4,  v5,  v6,  v8,  v9,  v0, v1
        transpose_8x8H  v10, v11, v12, v13, v14, v15, v17, v31, v0, v1

        st1             {v16.8h}, [x9], x1
        st1             {v10.8h}, [x0], x1
        st1             {v2.8h},  [x9], x1
        st1             {v11.8h}, [x0], x1
        st1             {v3.8h},  [x9], x1
        st1             {v12.8h}, [x0], x1
        st1             {v4.8h},  [x9], x1
        st1             {v13.8h}, [x0], x1
        st1             {v5.8h},  [x9], x1
        st1             {v14.8h}, [x0], x1
        st1             {v6.8h},  [x9], x1
        st1             {v15.8h}, [x0], x1
        st1             {v8.8h},  [x9], x1
        st1             {v17.8h}, [x0], x1
        st1             {v9.8h},  [x9], x1
        st1             {v31.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #3

        ret             x10
8:
        // The same writeback as in loop_filter_h_8_8
        sub             x9,  x0,  #8
        add             x0,  x9,  x1, lsl #2
        transpose_8x8H  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v20.8h}, [x9], x1
        st1             {v24.8h}, [x0], x1
        st1             {v21.8h}, [x9], x1
        st1             {v25.8h}, [x0], x1
        st1             {v22.8h}, [x9], x1
        st1             {v26.8h}, [x0], x1
        st1             {v23.8h}, [x9], x1
        st1             {v27.8h}, [x0], x1
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #8
        ret             x10
7:
        // The same writeback as in loop_filter_h_4_8
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #2
        transpose_4x8H  v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.d}[0], [x9], x1
        st1             {v22.d}[1], [x0], x1
        st1             {v23.d}[0], [x9], x1
        st1             {v23.d}[1], [x0], x1
        st1             {v24.d}[0], [x9], x1
        st1             {v24.d}[1], [x0], x1
        st1             {v25.d}[0], [x9], x1
        st1             {v25.d}[1], [x0], x1
        sub             x0,  x0,  x1, lsl #3
        add             x0,  x0,  #4
        ret             x10
endfunc

bpp_frontends vp9_loop_filter_h_16_8, push=1
bpp_frontends_rep vp9_loop_filter_h_16, 16, 8, h, push=1
