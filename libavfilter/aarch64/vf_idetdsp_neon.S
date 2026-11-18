/*
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

/* Loaded at idet_tail_mask + 2 * n, keeps the last n of 8 halfword lanes */
const idet_tail_mask, align=4
        .short          0, 0, 0, 0, 0, 0, 0, 0
        .short          -1, -1, -1, -1, -1, -1, -1, -1
endconst

/* Sum of |a + c - 2 * b| over w pixels.
 * x0: const uint8_t *a
 * x1: const uint8_t *b
 * x2: const uint8_t *c
 * w3: int w
 */
function ff_idet_filter_line_neon, export=1
        cmp             w3, #8
        b.lt            8f
        movi            v16.4s, #0
        movi            v17.4s, #0
        movi            v18.4s, #0
        movi            v19.4s, #0
        subs            w3, w3, #32
        b.lt            2f
1:
        ldp             q0,  q1,  [x0], #32
        ldp             q2,  q3,  [x1], #32
        ldp             q4,  q5,  [x2], #32
        subs            w3, w3, #32
        uaddl           v20.8h, v0.8b,  v4.8b       // a + c
        uaddl2          v21.8h, v0.16b, v4.16b
        uaddl           v22.8h, v1.8b,  v5.8b
        uaddl2          v23.8h, v1.16b, v5.16b
        uaddl           v24.8h, v2.8b,  v2.8b       // 2 * b
        uaddl2          v25.8h, v2.16b, v2.16b
        uaddl           v26.8h, v3.8b,  v3.8b
        uaddl2          v27.8h, v3.16b, v3.16b
        uabd            v20.8h, v20.8h, v24.8h
        uabd            v21.8h, v21.8h, v25.8h
        uabd            v22.8h, v22.8h, v26.8h
        uabd            v23.8h, v23.8h, v27.8h
        uadalp          v16.4s, v20.8h
        uadalp          v17.4s, v21.8h
        uadalp          v18.4s, v22.8h
        uadalp          v19.4s, v23.8h
        b.ge            1b
2:
        adds            w3, w3, #24
        b.lt            4f
3:
        ldr             d0,  [x0], #8
        ldr             d2,  [x1], #8
        ldr             d4,  [x2], #8
        subs            w3, w3, #8
        uaddl           v20.8h, v0.8b,  v4.8b
        uaddl           v24.8h, v2.8b,  v2.8b
        uabd            v20.8h, v20.8h, v24.8h
        uadalp          v16.4s, v20.8h
        b.ge            3b
4:
        // Last 1-7 pixels: reload the final 8 and mask the overlap
        ands            w3, w3, #7
        b.eq            5f
        movrel          x5, idet_tail_mask
        sub             x4, x3, #8
        add             x5, x5, x3, lsl #1
        ldr             d0,  [x0, x4]
        ldr             d2,  [x1, x4]
        ldr             d4,  [x2, x4]
        ldr             q6,  [x5]
        uaddl           v20.8h, v0.8b,  v4.8b
        uaddl           v24.8h, v2.8b,  v2.8b
        uabd            v20.8h, v20.8h, v24.8h
        and             v20.16b, v20.16b, v6.16b
        uadalp          v17.4s, v20.8h
5:
        add             v16.4s, v16.4s, v17.4s
        add             v18.4s, v18.4s, v19.4s
        add             v16.4s, v16.4s, v18.4s
        addv            s0, v16.4s
        fmov            w0, s0
        ret
8:
        b               X(ff_idet_filter_line_c)
endfunc

/* Same as above for samples of up to 15 bits, where a + c and 2 * b
 * still fit in 16-bit lanes. Samples must be within that depth, so
 * nonzero padding bits above it can give a different sum than C.
 * x0: const uint8_t *a
 * x1: const uint8_t *b
 * x2: const uint8_t *c
 * w3: int w, in samples
 */
function ff_idet_filter_line_15bit_neon, export=1
        cmp             w3, #8
        b.lt            8f
        movi            v16.4s, #0
        movi            v17.4s, #0
        subs            w3, w3, #16
        b.lt            2f
1:
        ldp             q0,  q1,  [x0], #32
        ldp             q2,  q3,  [x1], #32
        ldp             q4,  q5,  [x2], #32
        subs            w3, w3, #16
        add             v0.8h, v0.8h, v4.8h         // a + c
        add             v1.8h, v1.8h, v5.8h
        add             v2.8h, v2.8h, v2.8h         // 2 * b
        add             v3.8h, v3.8h, v3.8h
        uabd            v0.8h, v0.8h, v2.8h
        uabd            v1.8h, v1.8h, v3.8h
        uadalp          v16.4s, v0.8h
        uadalp          v17.4s, v1.8h
        b.ge            1b
2:
        // The low 4 bits of w3 are the samples left
        ubfiz           x4, x3, #1, #4
        tbz             w3, #3, 3f
        ldr             q0,  [x0]
        ldr             q2,  [x1]
        ldr             q4,  [x2]
        add             v0.8h, v0.8h, v4.8h
        add             v2.8h, v2.8h, v2.8h
        uabd            v0.8h, v0.8h, v2.8h
        uadalp          v16.4s, v0.8h
3:
        // Last 1-7 samples: reload the final 8 and mask the overlap
        ands            w3, w3, #7
        b.eq            5f
        movrel          x5, idet_tail_mask
        sub             x4, x4, #16
        lsl             x3, x3, #1
        ldr             q0,  [x0, x4]
        ldr             q2,  [x1, x4]
        ldr             q4,  [x2, x4]
        ldr             q6,  [x5, x3]
        add             v0.8h, v0.8h, v4.8h
        add             v2.8h, v2.8h, v2.8h
        uabd            v0.8h, v0.8h, v2.8h
        and             v0.16b, v0.16b, v6.16b
        uadalp          v17.4s, v0.8h
5:
        add             v16.4s, v16.4s, v17.4s
        addv            s0, v16.4s
        fmov            w0, s0
        ret
8:
        b               X(ff_idet_filter_line_c_16bit)
endfunc
