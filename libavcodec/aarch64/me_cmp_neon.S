/*
 * Copyright (c) 2022 Jonathan Swinney <jswinney@amazon.com>
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

function ff_pix_abs16_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h
        cmp             w4, #4                      // if h < 4, jump to completion section
        movi            v16.8h, #0                  // clear result accumulator
        movi            v17.8h, #0                  // clear result accumulator
        b.lt            2f
1:
        ld1             {v0.16b}, [x1], x3          // load pix1
        ld1             {v4.16b}, [x2], x3          // load pix2
        ld1             {v1.16b}, [x1], x3          // load pix1
        ld1             {v5.16b}, [x2], x3          // load pix2
        uabal           v16.8h, v0.8b, v4.8b        // absolute difference accumulate
        uabal2          v17.8h, v0.16b, v4.16b
        ld1             {v2.16b}, [x1], x3          // load pix1
        ld1             {v6.16b}, [x2], x3          // load pix2
        uabal           v16.8h, v1.8b, v5.8b        // absolute difference accumulate
        uabal2          v17.8h, v1.16b, v5.16b
        ld1             {v3.16b}, [x1], x3
        ld1             {v7.16b}, [x2], x3
        uabal           v16.8h, v2.8b, v6.8b
        uabal2          v17.8h, v2.16b, v6.16b
        sub             w4, w4, #4                  // h -= 4
        uabal           v16.8h, v3.8b, v7.8b
        uabal2          v17.8h, v3.16b, v7.16b
        cmp             w4, #4                      // if h >= 4, loop

        b.ge            1b
        cbnz            w4, 2f                      // if iterations remain, jump to completion section

        add             v16.8h, v16.8h, v17.8h
        uaddlv          s16, v16.8h                 // add up everything in v16 accumulator
        fmov            w0, s16                     // copy result to general purpose register
        ret

2:
        ld1             {v0.16b}, [x1], x3          // load pix1
        ld1             {v4.16b}, [x2], x3          // load pix2
        subs            w4, w4, #1                  // h -= 1
        uabal           v16.8h, v0.8b, v4.8b        // absolute difference accumulate
        uabal2          v17.8h, v0.16b, v4.16b
        b.ne            2b

        add             v16.8h, v16.8h, v17.8h
        uaddlv          s16, v16.8h                 // add up everything in v16 accumulator
        fmov            w0, s16                     // copy result to general purpose register
        ret
endfunc

function ff_pix_abs8_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        movi            v30.8h, #0
        cmp             w4, #4
        b.lt            2f

// make 4 iterations at once
1:
        ld1             {v0.8b}, [x1], x3               // Load pix1 for first iteration
        ld1             {v1.8b}, [x2], x3               // Load pix2 for first iteration
        ld1             {v2.8b}, [x1], x3               // Load pix1 for second iteration
        uabal           v30.8h, v0.8b, v1.8b            // Absolute difference, first iteration
        ld1             {v3.8b}, [x2], x3               // Load pix2 for second iteration
        ld1             {v4.8b}, [x1], x3               // Load pix1 for third iteration
        uabal           v30.8h, v2.8b, v3.8b            // Absolute difference, second iteration
        ld1             {v5.8b}, [x2], x3               // Load pix2 for third iteration
        sub             w4, w4, #4                      // h -= 4
        ld1             {v6.8b}, [x1], x3               // Load pix1 for foruth iteration
        ld1             {v7.8b}, [x2], x3               // Load pix2 for fourth iteration
        uabal           v30.8h, v4.8b, v5.8b            // Absolute difference, third iteration
        cmp             w4, #4
        uabal           v30.8h, v6.8b, v7.8b            // Absolute difference, foruth iteration
        b.ge            1b

        cbz             w4, 3f

// iterate by one
2:
        ld1             {v0.8b}, [x1], x3               // Load pix1
        ld1             {v1.8b}, [x2], x3               // Load pix2

        subs            w4, w4, #1
        uabal           v30.8h, v0.8b, v1.8b
        b.ne            2b

3:
        uaddlv          s20, v30.8h                     // Add up vector
        fmov            w0, s20

        ret
endfunc

function ff_pix_abs8_x2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        cmp             w4, #4
        movi            v26.8h, #0
        add             x5, x2, #1 // pix2 + 1
        b.lt            2f

// make 4 iterations at once
1:
        ld1             {v1.8b}, [x2], x3
        ld1             {v2.8b}, [x5], x3
        ld1             {v0.8b}, [x1], x3
        ld1             {v4.8b}, [x2], x3
        urhadd          v30.8b, v1.8b, v2.8b
        ld1             {v5.8b}, [x5], x3
        uabal           v26.8h, v0.8b, v30.8b
        ld1             {v6.8b}, [x1], x3
        urhadd          v29.8b, v4.8b, v5.8b
        ld1             {v7.8b}, [x2], x3
        ld1             {v20.8b}, [x5], x3
        uabal           v26.8h, v6.8b, v29.8b
        ld1             {v21.8b}, [x1], x3
        urhadd          v28.8b, v7.8b, v20.8b
        ld1             {v22.8b}, [x2], x3
        ld1             {v23.8b}, [x5], x3
        uabal           v26.8h, v21.8b, v28.8b
        sub             w4, w4, #4
        ld1             {v24.8b}, [x1], x3
        urhadd          v27.8b, v22.8b, v23.8b
        cmp             w4, #4
        uabal           v26.8h, v24.8b, v27.8b

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v1.8b}, [x2], x3
        ld1             {v2.8b}, [x5], x3
        ld1             {v0.8b}, [x1], x3
        urhadd          v30.8b, v1.8b, v2.8b
        subs            w4, w4, #1
        uabal           v26.8h, v0.8b, v30.8b

        b.ne            2b
3:
        uaddlv          s20, v26.8h
        fmov            w0, s20

        ret

endfunc

function ff_pix_abs8_y2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        cmp             w4, #4
        movi            v26.8h, #0
        ld1             {v1.8b}, [x2], x3
        b.lt            2f

// make 4 iterations at once
1:
        ld1             {v2.8b}, [x2], x3
        ld1             {v0.8b}, [x1], x3
        urhadd          v30.8b, v1.8b, v2.8b
        ld1             {v5.8b}, [x2], x3
        ld1             {v6.8b}, [x1], x3
        uabal           v26.8h, v0.8b, v30.8b
        urhadd          v29.8b, v2.8b, v5.8b
        ld1             {v20.8b}, [x2], x3
        ld1             {v21.8b}, [x1], x3
        uabal           v26.8h, v6.8b, v29.8b
        urhadd          v28.8b, v5.8b, v20.8b
        ld1             {v1.8b},  [x2], x3
        ld1             {v24.8b}, [x1], x3
        urhadd          v27.8b, v20.8b, v1.8b
        sub             w4, w4, #4
        uabal           v26.8h, v21.8b, v28.8b
        cmp             w4, #4
        uabal           v26.8h, v24.8b, v27.8b

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v0.8b}, [x1], x3
        ld1             {v2.8b}, [x2], x3
        urhadd          v30.8b, v1.8b, v2.8b
        subs            w4, w4, #1
        uabal           v26.8h, v0.8b, v30.8b
        mov             v1.8b, v2.8b

        b.ne            2b
3:
        uaddlv          s20, v26.8h
        fmov            w0, s20

        ret

endfunc

function ff_pix_abs8_xy2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        movi            v31.8h, #0
        add             x0, x2, 1  // pix2 + 1

        add             x5, x2, x3 // pix2 + stride = pix3
        cmp             w4, #4
        add             x6, x5, 1 // pix3 + stride + 1

        ld1             {v0.8b}, [x2], x3
        ld1             {v1.8b}, [x0], x3
        uaddl           v2.8h, v0.8b, v1.8b

        b.lt            2f

// make 4 iterations at once
1:
        ld1             {v4.8b}, [x5], x3
        ld1             {v5.8b}, [x6], x3
        ld1             {v7.8b}, [x5], x3
        uaddl           v0.8h, v4.8b, v5.8b
        ld1             {v16.8b}, [x6], x3
        add             v4.8h, v0.8h, v2.8h
        ld1             {v5.8b}, [x1], x3
        rshrn           v4.8b, v4.8h, #2
        uaddl           v7.8h, v7.8b, v16.8b
        uabal           v31.8h, v5.8b, v4.8b
        add             v2.8h, v0.8h, v7.8h
        ld1             {v17.8b}, [x1], x3
        rshrn           v2.8b, v2.8h, #2
        ld1             {v20.8b}, [x5], x3
        uabal           v31.8h, v17.8b, v2.8b
        ld1             {v21.8b}, [x6], x3
        ld1             {v25.8b}, [x5], x3
        uaddl           v20.8h, v20.8b, v21.8b
        ld1             {v26.8b}, [x6], x3
        add             v7.8h, v7.8h, v20.8h
        uaddl           v25.8h, v25.8b, v26.8b
        rshrn           v7.8b, v7.8h, #2
        ld1             {v22.8b}, [x1], x3
        mov             v2.16b, v25.16b
        uabal           v31.8h, v22.8b, v7.8b
        add             v20.8h, v20.8h, v25.8h
        ld1             {v27.8b}, [x1], x3
        sub             w4, w4, #4
        rshrn           v20.8b, v20.8h, #2
        cmp             w4, #4
        uabal           v31.8h, v27.8b, v20.8b

        b.ge            1b

        cbz             w4, 3f

// iterate by one
2:
        ld1             {v0.8b}, [x5], x3
        ld1             {v1.8b}, [x6], x3
        ld1             {v4.8b}, [x1], x3
        uaddl           v21.8h, v0.8b, v1.8b
        subs            w4, w4, #1
        add             v3.8h, v2.8h, v21.8h
        mov             v2.16b, v21.16b
        rshrn           v3.8b, v3.8h, #2
        uabal           v31.8h, v4.8b, v3.8b
        b.ne            2b

3:
        uaddlv          s18, v31.8h
        fmov            w0, s18

        ret

endfunc


function ff_pix_abs16_xy2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        add             x5, x2, x3                  // use x5 to hold uint8_t *pix3
        movi            v21.8h, #0                  // initialize the result register
        movi            v22.8h, #0                  // initialize the result register

        // Load initial pix2 values for either the unrolled version or completion version.
        ldur            q4, [x2, #1]                // load pix2+1
        ldr             q3, [x2]                    // load pix2
        uaddl           v2.8h, v4.8b, v3.8b         // pix2 + pix2+1 0..7
        uaddl2          v3.8h, v4.16b, v3.16b       // pix2 + pix2+1 8..15
        cmp             w4, #4                      // if h < 4 jump to the completion version
        b.lt            2f
1:
        // This is an unrolled implementation. It completes 4 iterations of the C for each branch.
        // In each iteration, pix2[i+1] == pix3[i]. This means we need only three loads per iteration,
        // plus two at the beginning to start.
        ldur            q5, [x5, #1]                // load pix3+1
        ld1             {v4.16b}, [x5], x3          // load pix3
        ld1             {v1.16b}, [x1], x3          // load pix1

        ldur            q7, [x5, #1]                // load pix3+1
        ld1             {v6.16b}, [x5], x3          // load pix3
        ld1             {v16.16b}, [x1], x3         // load pix1

        // These blocks compute the average: avg(pix2[n], pix2[n+1], pix3[n], pix3[n+1])
        uaddl           v30.8h, v4.8b, v5.8b        // pix3 + pix3+1 0..7
        uaddl2          v31.8h, v4.16b, v5.16b      // pix3 + pix3+1 8..15

        ldur            q19, [x5, #1]               // load pix3+1

        add             v23.8h, v2.8h, v30.8h       // add up 0..7, using pix2 + pix2+1 values from previous iteration
        add             v24.8h, v3.8h, v31.8h       // add up 8..15, using pix2 + pix2+1 values from previous iteration

        ld1             {v18.16b}, [x5], x3         // load pix3
        ld1             {v17.16b}, [x1], x3         // load pix1

        rshrn           v23.8b, v23.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v23.16b, v24.8h, #2         // shift right 2 8..15

        uaddl           v2.8h, v6.8b, v7.8b         // pix3 + pix3+1 0..7
        uaddl2          v3.8h, v6.16b, v7.16b       // pix3 + pix3+1 8..15

        ldur            q7, [x5, #1]                // load pix3+1

        add             v26.8h, v30.8h, v2.8h       // add up 0..7, using pix2 + pix2+1 values from pix3 above
        add             v27.8h, v31.8h, v3.8h       // add up 8..15, using pix2 + pix2+1 values from pix3 above

        uabal           v21.8h, v1.8b,  v23.8b      // absolute difference 0..7, i=0
        uabal2          v22.8h, v1.16b, v23.16b     // absolute difference 8..15, i=0

        ld1             {v6.16b}, [x5], x3          // load pix3
        ld1             {v20.16b}, [x1], x3         // load pix1

        rshrn           v26.8b, v26.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v26.16b, v27.8h, #2         // shift right 2 8..15

        uaddl           v4.8h, v18.8b, v19.8b       // pix3 + pix3+1 0..7
        uaddl2          v5.8h, v18.16b, v19.16b     // pix3 + pix3+1 8..15
        add             v28.8h, v2.8h, v4.8h        // add up 0..7, using pix2 + pix2+1 values from pix3 above
        add             v29.8h, v3.8h, v5.8h        // add up 8..15, using pix2 + pix2+1 values from pix3 above
        rshrn           v28.8b, v28.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v28.16b, v29.8h, #2         // shift right 2 8..15

        uabal           v21.8h, v16.8b,  v26.8b     // absolute difference 0..7, i=1
        uabal2          v22.8h, v16.16b, v26.16b    // absolute difference 8..15, i=1

        uaddl           v2.8h, v6.8b,  v7.8b        // pix3 + pix3+1 0..7
        uaddl2          v3.8h, v6.16b, v7.16b       // pix3 + pix3+1 8..15
        add             v30.8h, v4.8h, v2.8h        // add up 0..7, using pix2 + pix2+1 values from pix3 above
        add             v31.8h, v5.8h, v3.8h        // add up 8..15, using pix2 + pix2+1 values from pix3 above
        rshrn           v30.8b, v30.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v30.16b, v31.8h, #2         // shift right 2 8..15

        sub             w4, w4, #4                  // h -= 4

        uabal           v21.8h, v17.8b,  v28.8b     // absolute difference 0..7, i=2
        uabal2          v22.8h, v17.16b, v28.16b    // absolute difference 8..15, i=2

        cmp             w4, #4                      // loop if h >= 4


        uabal           v21.8h, v20.8b,  v30.8b     // absolute difference 0..7, i=3
        uabal2          v22.8h, v20.16b, v30.16b    // absolute difference 8..15, i=3

        b.ge            1b
        cbnz            w4, 2f                      // if iterations remain jump to completion section

        add             v4.8h, v21.8h, v22.8h
        uaddlv          s0, v4.8h                   // finish adding up accumulated values

        fmov            w0, s0                      // copy result to general purpose register
        ret
2:
        // v2 and v3 are set either at the end of this loop or at from the unrolled version
        // which branches here to complete iterations when h % 4 != 0.
        ldur            q5, [x5, #1]                // load pix3+1
        ld1             {v4.16b}, [x5], x3          // load pix3
        ld1             {v1.16b}, [x1], x3          // load pix1
        subs            w4, w4, #1                  // decrement h

        uaddl           v18.8h, v4.8b, v5.8b        // pix3 + pix3+1 0..7
        uaddl2          v19.8h, v4.16b, v5.16b      // pix3 + pix3+1 8..15
        add             v16.8h, v2.8h, v18.8h       // add up 0..7, using pix2 + pix2+1 values from previous iteration
        add             v17.8h, v3.8h, v19.8h       // add up 8..15, using pix2 + pix2+1 values from previous iteration
        // divide by 4 to compute the average of values summed above
        rshrn           v16.8b,  v16.8h, #2         // shift right by 2 0..7 (rounding shift right)
        rshrn2          v16.16b, v17.8h, #2         // shift right by 2 8..15

        uabal           v21.8h, v1.8b,  v16.8b      // absolute difference 0..7
        uabal2          v22.8h, v1.16b, v16.16b     // absolute difference accumulate 8..15
        mov             v2.16b, v18.16b             // pix3 -> pix2
        mov             v3.16b, v19.16b             // pix3+1 -> pix2+1

        b.ne            2b                          // loop if h > 0

        add             v4.8h, v21.8h, v22.8h
        uaddlv          s0, v4.8h                   // finish adding up accumulated values
        fmov            w0, s0                      // copy result to general purpose register
        ret
endfunc

function ff_pix_abs16_x2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        cmp             w4, #4
        // initialize buffers
        movi            v16.8h, #0
        movi            v17.8h, #0
        add             x5, x2, #1 // pix2 + 1
        b.lt            2f

// make 4 iterations at once
1:

        // abs(pix1[0] - avg2(pix2[0], pix2[1]))
        // avg2(a,b) = (((a) + (b) + 1) >> 1)
        // abs(x) = (x < 0 ? -x : x)

        ld1             {v1.16b}, [x2], x3
        ld1             {v2.16b}, [x5], x3
        urhadd          v30.16b, v1.16b, v2.16b
        ld1             {v0.16b}, [x1], x3
        uabal           v16.8h, v0.8b, v30.8b
        ld1             {v4.16b}, [x2], x3
        uabal2          v17.8h, v0.16b, v30.16b
        ld1             {v5.16b}, [x5], x3
        urhadd          v29.16b, v4.16b, v5.16b
        ld1             {v3.16b}, [x1], x3
        uabal           v16.8h, v3.8b, v29.8b
        ld1             {v7.16b}, [x2], x3
        uabal2          v17.8h, v3.16b, v29.16b
        ld1             {v22.16b}, [x5], x3
        urhadd          v28.16b, v7.16b, v22.16b
        ld1             {v6.16b}, [x1], x3
        uabal           v16.8h, v6.8b, v28.8b
        ld1             {v24.16b}, [x2], x3
        sub             w4, w4, #4
        uabal2          v17.8h, v6.16b, v28.16b
        ld1             {v25.16b}, [x5], x3
        urhadd          v27.16b, v24.16b, v25.16b
        ld1             {v23.16b}, [x1], x3
        cmp             w4, #4
        uabal           v16.8h, v23.8b, v27.8b
        uabal2          v17.8h, v23.16b, v27.16b

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v1.16b}, [x2], x3
        ld1             {v2.16b}, [x5], x3
        subs            w4, w4, #1
        urhadd          v29.16b, v1.16b, v2.16b
        ld1             {v0.16b}, [x1], x3
        uabal           v16.8h, v0.8b,  v29.8b
        uabal2          v17.8h, v0.16b, v29.16b


        b.ne            2b

3:
        add             v16.8h, v16.8h, v17.8h
        uaddlv          s16, v16.8h
        fmov            w0, s16

        ret
endfunc

function ff_pix_abs16_y2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        // initialize buffers
        ld1             {v1.16b}, [x2], x3              // Load pix2
        movi            v29.8h, #0                      // clear the accumulator
        movi            v28.8h, #0                      // clear the accumulator
        cmp             w4, #4
        b.lt            2f

// make 4 iterations at once
1:

        // abs(pix1[0], avg2(pix2[0], pix2[0 + stride]))
        // avg2(a, b) = (((a) + (b) + 1) >> 1)
        // abs(x) = (x < 0 ? (-x) : (x))

        ld1             {v2.16b}, [x2], x3              // Load pix3 for first iteration
        ld1             {v0.16b}, [x1], x3              // Load pix1 for first iteration
        urhadd          v30.16b, v1.16b, v2.16b         // Rounding halving add, first iteration
        ld1             {v5.16b}, [x2], x3              // Load pix3 for second iteartion
        uabal           v29.8h, v0.8b, v30.8b           // Absolute difference of lower half, first iteration
        uabal2          v28.8h, v0.16b, v30.16b         // Absolute difference of upper half, first iteration
        ld1             {v3.16b}, [x1], x3              // Load pix1 for second iteration
        urhadd          v27.16b, v2.16b, v5.16b         // Rounding halving add, second iteration
        ld1             {v20.16b}, [x2], x3             // Load pix3 for third iteration
        uabal           v29.8h, v3.8b, v27.8b           // Absolute difference of lower half for second iteration
        uabal2          v28.8h, v3.16b, v27.16b         // Absolute difference of upper half for second iteration
        ld1             {v6.16b}, [x1], x3              // Load pix1 for third iteration
        urhadd          v26.16b, v5.16b, v20.16b        // Rounding halving add, third iteration
        ld1             {v1.16b}, [x2], x3              // Load pix3 for fourth iteration
        uabal           v29.8h, v6.8b, v26.8b           // Absolute difference of lower half for third iteration
        uabal2          v28.8h, v6.16b, v26.16b         // Absolute difference of upper half for third iteration
        ld1             {v21.16b}, [x1], x3             // Load pix1 for fourth iteration
        sub             w4, w4, #4                      // h-= 4
        urhadd          v25.16b, v20.16b, v1.16b        // Rounding halving add
        cmp             w4, #4
        uabal           v29.8h, v21.8b, v25.8b          // Absolute difference of lower half for fourth iteration
        uabal2          v28.8h, v21.16b, v25.16b        // Absolute difference of upper half for fourth iteration

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:

        ld1             {v2.16b}, [x2], x3              // Load pix3
        subs            w4, w4, #1
        ld1             {v0.16b}, [x1], x3              // Load pix1
        urhadd          v30.16b, v1.16b, v2.16b         // Rounding halving add
        mov             v1.16b, v2.16b                  // Shift pix3->pix2
        uabal           v29.8h, v30.8b, v0.8b
        uabal2          v28.8h, v30.16b, v0.16b

        b.ne            2b

3:
        add             v29.8h, v29.8h, v28.8h          // Add vectors together
        uaddlv          s16, v29.8h                     // Add up vector values

        fmov            w0, s16

        ret
endfunc

function sse16_neon, export=1
        // x0 - unused
        // x1 - pix1
        // x2 - pix2
        // x3 - stride
        // w4 - h

        cmp             w4, #4
        movi            v17.4s, #0
        b.lt            2f

// Make 4 iterations at once
1:

        // res = abs(pix1[0] - pix2[0])
        // res * res

        ld1             {v0.16b}, [x1], x3              // Load pix1 vector for first iteration
        ld1             {v1.16b}, [x2], x3              // Load pix2 vector for first iteration
        ld1             {v2.16b}, [x1], x3              // Load pix1 vector for second iteration
        uabd            v30.16b, v0.16b, v1.16b         // Absolute difference, first iteration
        ld1             {v3.16b}, [x2], x3              // Load pix2 vector for second iteration
        umull           v29.8h, v30.8b, v30.8b          // Multiply lower half of vectors, first iteration
        umull2          v28.8h, v30.16b, v30.16b        // Multiply upper half of vectors, first iteration
        uabd            v27.16b, v2.16b, v3.16b         // Absolute difference, second iteration
        uadalp          v17.4s, v29.8h                  // Pairwise add, first iteration
        ld1             {v4.16b}, [x1], x3              // Load pix1 for third iteration
        umull           v26.8h, v27.8b, v27.8b          // Mulitply lower half, second iteration
        umull2          v25.8h, v27.16b, v27.16b        // Multiply upper half, second iteration
        ld1             {v5.16b}, [x2], x3              // Load pix2 for third iteration
        uadalp          v17.4s, v26.8h                  // Pairwise add and accumulate, second iteration
        uabd            v24.16b, v4.16b, v5.16b         // Absolute difference, third iteration
        ld1             {v6.16b}, [x1], x3              // Load pix1 for fourth iteration
        uadalp          v17.4s, v25.8h                  // Pairwise add and accumulate, second iteration
        umull           v23.8h, v24.8b, v24.8b          // Multiply lower half, third iteration
        umull2          v22.8h, v24.16b, v24.16b        // Multiply upper half, third iteration
        uadalp          v17.4s, v23.8h                  // Pairwise add and accumulate, third iteration
        ld1             {v7.16b}, [x2], x3              // Load pix2 for fouth iteration
        uadalp          v17.4s, v22.8h                  // Pairwise add and accumulate, third iteration
        uabd            v21.16b, v6.16b, v7.16b         // Absolute difference, fourth iteration
        uadalp          v17.4s, v28.8h                  // Pairwise add and accumulate, first iteration
        umull           v20.8h, v21.8b, v21.8b          // Multiply lower half, fourth iteration
        sub             w4, w4, #4                      // h -= 4
        umull2          v19.8h, v21.16b, v21.16b        // Multiply upper half, fourth iteration
        uadalp          v17.4s, v20.8h                  // Pairwise add and accumulate, fourth iteration
        cmp             w4, #4
        uadalp          v17.4s, v19.8h                  // Pairwise add and accumulate, fourth iteration
        b.ge            1b

        cbz             w4, 3f

// iterate by one
2:

        ld1             {v0.16b}, [x1], x3              // Load pix1
        ld1             {v1.16b}, [x2], x3              // Load pix2

        uabd            v30.16b, v0.16b, v1.16b
        umull           v29.8h, v30.8b, v30.8b
        umull2          v28.8h, v30.16b, v30.16b
        uadalp          v17.4s, v29.8h
        subs            w4, w4, #1
        uadalp          v17.4s, v28.8h

        b.ne            2b

3:
        uaddlv          d16, v17.4s                     // add up accumulator vector

        fmov            w0, s16

        ret
endfunc

function sse8_neon, export=1
        // x0 - unused
        // x1 - pix1
        // x2 - pix2
        // x3 - stride
        // w4 - h

        movi            v21.4s, #0
        movi            v20.4s, #0
        cmp             w4, #4
        b.lt            2f

// make 4 iterations at once
1:

        // res = abs(pix1[0] - pix2[0])
        // res * res

        ld1             {v0.8b}, [x1], x3               // Load pix1 for first iteration
        ld1             {v1.8b}, [x2], x3               // Load pix2 for second iteration
        ld1             {v2.8b}, [x1], x3               // Load pix1 for second iteration
        ld1             {v3.8b}, [x2], x3               // Load pix2 for second iteration
        uabdl           v30.8h, v0.8b, v1.8b            // Absolute difference, first iteration
        ld1             {v4.8b}, [x1], x3               // Load pix1 for third iteration
        ld1             {v5.8b}, [x2], x3               // Load pix2 for third iteration
        uabdl           v29.8h, v2.8b, v3.8b            // Absolute difference, second iteration
        umlal           v21.4s, v30.4h, v30.4h          // Multiply lower half, first iteration
        ld1             {v6.8b}, [x1], x3               // Load pix1 for fourth iteration
        ld1             {v7.8b}, [x2], x3               // Load pix2 for fourth iteration
        uabdl           v28.8h, v4.8b, v5.8b            // Absolute difference, third iteration
        umlal           v21.4s, v29.4h, v29.4h          // Multiply lower half, second iteration
        umlal2          v20.4s, v30.8h, v30.8h          // Multiply upper half, first iteration
        uabdl           v27.8h, v6.8b, v7.8b            // Absolute difference, fourth iteration
        umlal           v21.4s, v28.4h, v28.4h          // Multiply lower half, third iteration
        umlal2          v20.4s, v29.8h, v29.8h          // Multiply upper half, second iteration
        sub             w4, w4, #4                      // h -= 4
        umlal2          v20.4s, v28.8h, v28.8h          // Multiply upper half, third iteration
        umlal           v21.4s, v27.4h, v27.4h          // Multiply lower half, fourth iteration
        cmp             w4, #4
        umlal2          v20.4s, v27.8h, v27.8h          // Multiply upper half, fourth iteration
        b.ge            1b

        cbz             w4, 3f

// iterate by one
2:
        ld1             {v0.8b}, [x1], x3               // Load pix1
        ld1             {v1.8b}, [x2], x3               // Load pix2
        subs            w4, w4, #1
        uabdl           v30.8h, v0.8b, v1.8b
        umlal           v21.4s, v30.4h, v30.4h
        umlal2          v20.4s, v30.8h, v30.8h

        b.ne            2b

3:
        add             v21.4s, v21.4s, v20.4s          // Add accumulator vectors together
        uaddlv          d17, v21.4s                     // Add up vector

        fmov            w0, s17
        ret

endfunc

function sse4_neon, export=1
        // x0 - unused
        // x1 - pix1
        // x2 - pix2
        // x3 - stride
        // w4 - h

        movi            v16.4s, #0                      // clear the result accumulator
        cmp             w4, #4
        b.lt            2f

// make 4 iterations at once
1:

        // res = abs(pix1[0] - pix2[0])
        // res * res

        ld1             {v0.s}[0], [x1], x3             // Load pix1, first iteration
        ld1             {v1.s}[0], [x2], x3             // Load pix2, first iteration
        ld1             {v2.s}[0], [x1], x3             // Load pix1, second iteration
        ld1             {v3.s}[0], [x2], x3             // Load pix2, second iteration
        uabdl           v30.8h, v0.8b, v1.8b            // Absolute difference, first iteration
        ld1             {v4.s}[0], [x1], x3             // Load pix1, third iteration
        ld1             {v5.s}[0], [x2], x3             // Load pix2, third iteration
        uabdl           v29.8h, v2.8b, v3.8b            // Absolute difference, second iteration
        umlal           v16.4s, v30.4h, v30.4h          // Multiply vectors, first iteration
        ld1             {v6.s}[0], [x1], x3             // Load pix1, fourth iteration
        ld1             {v7.s}[0], [x2], x3             // Load pix2, fourth iteration
        uabdl           v28.8h, v4.8b, v5.8b            // Absolute difference, third iteration
        umlal           v16.4s, v29.4h, v29.4h          // Multiply and accumulate, second iteration
        sub             w4, w4, #4
        uabdl           v27.8h, v6.8b, v7.8b            // Absolue difference, fourth iteration
        umlal           v16.4s, v28.4h, v28.4h          // Multiply and accumulate, third iteration
        cmp             w4, #4
        umlal           v16.4s, v27.4h, v27.4h          // Multiply and accumulate, fourth iteration
        b.ge            1b

        cbz             w4, 3f

// iterate by one
2:
        ld1             {v0.s}[0], [x1], x3               // Load pix1
        ld1             {v1.s}[0], [x2], x3               // Load pix2
        uabdl           v30.8h, v0.8b, v1.8b
        subs            w4, w4, #1
        umlal           v16.4s, v30.4h, v30.4h

        b.ne            2b

3:
        uaddlv          d17, v16.4s                     // Add vector
        fmov            w0, s17

        ret
endfunc

function vsad16_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v0.16b}, [x1], x3              // Load pix1[0], first iteration
        ld1             {v1.16b}, [x2], x3              // Load pix2[0], first iteration

        sub             w4, w4, #1                      // we need to make h-1 iterations
        movi            v16.8h, #0

        cmp             w4, #3                          // check if we can make 3 iterations at once
        usubl           v31.8h, v0.8b, v1.8b            // Signed difference pix1[0] - pix2[0], first iteration
        usubl2          v30.8h, v0.16b, v1.16b          // Signed difference pix1[0] - pix2[0], first iteration

        b.lt            2f

1:
        // abs(pix1[0] - pix2[0] - pix1[0 + stride] + pix2[0 + stride])
        ld1             {v0.16b}, [x1], x3              // Load pix1[0 + stride], first iteration
        ld1             {v1.16b}, [x2], x3              // Load pix2[0 + stride], first iteration
        ld1             {v2.16b}, [x1], x3              // Load pix1[0 + stride], second iteration
        ld1             {v3.16b}, [x2], x3              // Load pix2[0 + stride], second iteration
        usubl           v29.8h, v0.8b,  v1.8b           // Signed difference pix1[0 + stride] - pix2[0 + stride], first iteration
        usubl2          v28.8h, v0.16b, v1.16b          // Signed difference pix1[0 + stride] - pix2[0 + stride], first iteration
        ld1             {v4.16b}, [x1], x3              // Load pix1[0 + stride], third iteration
        ld1             {v5.16b}, [x2], x3              // Load pix2[0 + stride], third iteration
        usubl           v27.8h, v2.8b,  v3.8b           // Signed difference pix1[0 + stride] - pix2[0 + stride], second iteration
        saba            v16.8h, v31.8h, v29.8h          // Signed absolute difference and accumulate the result. first iteration
        usubl2          v26.8h, v2.16b, v3.16b          // Signed difference pix1[0 + stride] - pix2[0 + stride], second iteration
        saba            v16.8h, v30.8h, v28.8h          // Signed absolute difference and accumulate the result. first iteration
        usubl           v25.8h, v4.8b,  v5.8b           // Signed difference pix1[0 + stride] - pix2[0 + stride], third iteration
        usubl2          v24.8h, v4.16b, v5.16b          // Signed difference pix1[0 + stride] - pix2[0 + stride], third iteration
        saba            v16.8h, v29.8h, v27.8h          // Signed absolute difference and accumulate the result. second iteration
        mov             v31.16b, v25.16b
        saba            v16.8h, v28.8h, v26.8h          // Signed absolute difference and accumulate the result. second iteration
        sub             w4, w4, #3                      // h -= 3
        mov             v30.16b, v24.16b
        saba            v16.8h, v27.8h, v25.8h          // Signed absolute difference and accumulate the result. third iteration
        cmp             w4, #3
        saba            v16.8h, v26.8h, v24.8h          // Signed absolute difference and accumulate the result. third iteration

        b.ge            1b
        cbz             w4, 3f
2:
        ld1             {v0.16b}, [x1], x3
        ld1             {v1.16b}, [x2], x3
        subs            w4, w4, #1
        usubl           v29.8h,  v0.8b,   v1.8b
        usubl2          v28.8h,  v0.16b,  v1.16b
        saba            v16.8h,  v31.8h,  v29.8h
        mov             v31.16b, v29.16b
        saba            v16.8h,  v30.8h,  v28.8h
        mov             v30.16b, v28.16b

        b.ne            2b
3:
        uaddlv          s17, v16.8h
        fmov            w0, s17

        ret
endfunc

function vsse8_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v0.8b}, [x1], x3              // Load pix1[0], first iteration
        ld1             {v1.8b}, [x2], x3              // Load pix2[0], first iteration

        sub             w4, w4, #1                      // we need to make h-1 iterations
        movi            v16.4s, #0
        movi            v17.4s, #0

        cmp             w4, #3                          // check if we can make 3 iterations at once
        usubl           v31.8h, v0.8b, v1.8b            // Signed difference of pix1[0] - pix2[0], first iteration
        b.lt            2f


1:
        // x = abs(pix1[0] - pix2[0] - pix1[0 + stride] + pix2[0 + stride])
        // res = (x) * (x)
        ld1             {v0.8b}, [x1], x3              // Load pix1[0 + stride], first iteration
        ld1             {v1.8b}, [x2], x3              // Load pix2[0 + stride], first iteration
        ld1             {v2.8b}, [x1], x3              // Load pix1[0 + stride], second iteration
        ld1             {v3.8b}, [x2], x3              // Load pix2[0 + stride], second iteration
        usubl           v29.8h, v0.8b, v1.8b
        usubl2          v28.8h, v0.16b, v1.16b
        ld1             {v4.8b}, [x1], x3              // Load pix1[0 + stride], third iteration
        ld1             {v5.8b}, [x2], x3              // Load pix1[0 + stride], third iteration
        sabd            v31.8h, v31.8h, v29.8h
        usubl           v27.8h, v2.8b, v3.8b
        usubl           v25.8h, v4.8b, v5.8b
        sabd            v29.8h, v29.8h, v27.8h
        sabd            v27.8h, v27.8h, v25.8h
        umlal           v16.4s, v31.4h, v31.4h
        umlal2          v17.4s, v31.8h, v31.8h
        mov             v31.16b, v25.16b
        umlal           v16.4s, v29.4h, v29.4h
        umlal2          v17.4s, v29.8h, v29.8h
        sub             w4, w4, #3
        umlal           v16.4s, v27.4h, v27.4h
        umlal2          v17.4s, v27.8h, v27.8h
        cmp             w4, #3

        b.ge            1b

        cbz             w4, 3f

// iterate by once
2:
        ld1             {v0.8b}, [x1], x3
        ld1             {v1.8b}, [x2], x3
        subs            w4, w4, #1
        usubl           v29.8h, v0.8b, v1.8b
        sabd            v31.8h, v31.8h, v29.8h
        umlal           v16.4s, v31.4h, v31.4h
        umlal2          v17.4s, v31.8h, v31.8h
        mov             v31.16b, v29.16b
        b.ne            2b

3:
        add             v16.4s, v16.4s, v17.4s
        uaddlv          d17, v16.4s
        fmov            w0, s17

        ret
endfunc


function vsse16_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v0.16b}, [x1], x3              // Load pix1[0], first iteration
        ld1             {v1.16b}, [x2], x3              // Load pix2[0], first iteration

        sub             w4, w4, #1                      // we need to make h-1 iterations
        movi            v16.4s, #0
        movi            v17.4s, #0

        cmp             w4, #3                          // check if we can make 3 iterations at once
        usubl           v31.8h, v0.8b, v1.8b            // Signed difference of pix1[0] - pix2[0], first iteration
        usubl2          v30.8h, v0.16b, v1.16b          // Signed difference of pix1[0] - pix2[0], first iteration
        b.lt            2f


1:
        // x = abs(pix1[0] - pix2[0] - pix1[0 + stride] + pix2[0 + stride])
        // res = (x) * (x)
        ld1             {v0.16b}, [x1], x3              // Load pix1[0 + stride], first iteration
        ld1             {v1.16b}, [x2], x3              // Load pix2[0 + stride], first iteration
        ld1             {v2.16b}, [x1], x3              // Load pix1[0 + stride], second iteration
        ld1             {v3.16b}, [x2], x3              // Load pix2[0 + stride], second iteration
        usubl           v29.8h, v0.8b, v1.8b
        usubl2          v28.8h, v0.16b, v1.16b
        ld1             {v4.16b}, [x1], x3              // Load pix1[0 + stride], third iteration
        ld1             {v5.16b}, [x2], x3              // Load pix1[0 + stride], third iteration
        sabd            v31.8h, v31.8h, v29.8h
        sabd            v30.8h, v30.8h, v28.8h
        usubl           v27.8h, v2.8b, v3.8b
        usubl2          v26.8h, v2.16b, v3.16b
        usubl           v25.8h, v4.8b, v5.8b
        usubl2          v24.8h, v4.16b, v5.16b
        sabd            v29.8h, v29.8h, v27.8h
        sabd            v27.8h, v27.8h, v25.8h
        umlal           v16.4s, v31.4h, v31.4h
        umlal2          v17.4s, v31.8h, v31.8h
        sabd            v28.8h, v28.8h, v26.8h
        sabd            v26.8h, v26.8h, v24.8h
        umlal           v16.4s, v30.4h, v30.4h
        umlal2          v17.4s, v30.8h, v30.8h
        mov             v31.16b, v25.16b
        umlal           v16.4s, v29.4h, v29.4h
        umlal2          v17.4s, v29.8h, v29.8h
        mov             v30.16b, v24.16b
        umlal           v16.4s, v28.4h, v28.4h
        umlal2          v17.4s, v28.8h, v28.8h
        sub             w4, w4, #3
        umlal           v16.4s, v27.4h, v27.4h
        umlal2          v17.4s, v27.8h, v27.8h
        cmp             w4, #3
        umlal           v16.4s, v26.4h, v26.4h
        umlal2          v17.4s, v26.8h, v26.8h

        b.ge            1b

        cbz             w4, 3f

// iterate by once
2:
        ld1             {v0.16b}, [x1], x3
        ld1             {v1.16b}, [x2], x3
        subs            w4, w4, #1
        usubl           v29.8h, v0.8b, v1.8b
        usubl2          v28.8h, v0.16b, v1.16b
        sabd            v31.8h, v31.8h, v29.8h
        sabd            v30.8h, v30.8h, v28.8h
        umlal           v16.4s, v31.4h, v31.4h
        umlal2          v17.4s, v31.8h, v31.8h
        mov             v31.16b, v29.16b
        umlal           v16.4s, v30.4h, v30.4h
        umlal2          v17.4s, v30.8h, v30.8h
        mov             v30.16b, v28.16b
        b.ne            2b

3:
        add             v16.4s, v16.4s, v17.4s
        uaddlv          d17, v16.4s
        fmov            w0, s17

        ret
endfunc

function vsad_intra16_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *dummy
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v0.16b}, [x1], x3
        sub             w4, w4, #1 // we need to make h-1 iterations
        cmp             w4, #3
        movi            v16.8h, #0
        b.lt            2f

// make 4 iterations at once
1:
        // v = abs( pix1[0] - pix1[0 + stride] )
        // score = sum(v)
        ld1             {v1.16b}, [x1], x3
        ld1             {v2.16b}, [x1], x3
        uabal           v16.8h, v0.8b, v1.8b
        ld1             {v3.16b}, [x1], x3
        uabal2          v16.8h, v0.16b, v1.16b
        sub             w4, w4, #3
        uabal           v16.8h, v1.8b, v2.8b
        cmp             w4, #3
        uabal2          v16.8h, v1.16b, v2.16b
        mov             v0.16b, v3.16b
        uabal           v16.8h, v2.8b, v3.8b
        uabal2          v16.8h, v2.16b, v3.16b
        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v1.16b}, [x1], x3
        subs            w4, w4, #1
        uabal           v16.8h, v0.8b, v1.8b
        uabal2          v16.8h, v0.16b, v1.16b
        mov             v0.16b, v1.16b
        cbnz            w4, 2b

3:
        uaddlv          s17, v16.8h
        fmov            w0, s17

        ret
endfunc

function vsse_intra16_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *dummy
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v0.16b}, [x1], x3
        movi            v16.4s, #0
        movi            v17.4s, #0

        sub             w4, w4, #1 // we need to make h-1 iterations
        cmp             w4, #3
        b.lt            2f

1:
        // v = abs( pix1[0] - pix1[0 + stride] )
        // score = sum( v * v )
        ld1             {v1.16b}, [x1], x3
        ld1             {v2.16b}, [x1], x3
        uabd            v30.16b, v0.16b, v1.16b
        ld1             {v3.16b}, [x1], x3
        umull           v29.8h, v30.8b, v30.8b
        umull2          v28.8h, v30.16b, v30.16b
        uabd            v27.16b, v1.16b, v2.16b
        uadalp          v16.4s, v29.8h
        umull           v26.8h, v27.8b, v27.8b
        umull2          v27.8h, v27.16b, v27.16b
        uadalp          v17.4s, v28.8h
        uabd            v25.16b, v2.16b, v3.16b
        uadalp          v16.4s, v26.8h
        umull           v24.8h, v25.8b, v25.8b
        umull2          v25.8h, v25.16b, v25.16b
        uadalp          v17.4s, v27.8h
        sub             w4, w4, #3
        uadalp          v16.4s, v24.8h
        cmp             w4, #3
        uadalp          v17.4s, v25.8h
        mov             v0.16b, v3.16b

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v1.16b}, [x1], x3
        subs            w4, w4, #1
        uabd            v30.16b, v0.16b, v1.16b
        mov             v0.16b, v1.16b
        umull           v29.8h, v30.8b, v30.8b
        umull2          v30.8h, v30.16b, v30.16b
        uadalp          v16.4s, v29.8h
        uadalp          v17.4s, v30.8h
        cbnz            w4, 2b

3:
        add             v16.4s, v16.4s, v17.4S
        uaddlv          d17, v16.4s
        fmov            w0, s17

        ret
endfunc

function vsse_intra8_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *dummy
        // x3           ptrdiff_t stride
        // w4           int h

        sub             w4, w4, #1 // we need to make h-1 iterations
        ld1             {v0.8b}, [x1], x3
        cmp             w4, #3
        movi            v16.4s, #0

        b.lt            2f

1:
        // v = abs( pix1[0] - pix1[0 + stride] )
        // score = sum( v * v )
        ld1             {v1.8b}, [x1], x3
        ld1             {v2.8b}, [x1], x3
        uabd            v30.8b, v0.8b, v1.8b
        ld1             {v3.8b}, [x1], x3
        uabd            v27.8b, v1.8b, v2.8b
        umull           v29.8h, v30.8b, v30.8b
        uabd            v25.8b, v2.8b, v3.8b
        umull           v26.8h, v27.8b, v27.8b
        uadalp          v16.4s, v29.8h
        umull           v24.8h, v25.8b, v25.8b
        uadalp          v16.4s, v26.8h
        sub             w4, w4, #3
        uadalp          v16.4s, v24.8h
        cmp             w4, #3
        mov             v0.8b, v3.8b

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v1.8b}, [x1], x3
        subs            w4, w4, #1
        uabd            v30.8b, v0.8b, v1.8b
        mov             v0.8b, v1.8b
        umull           v29.8h, v30.8b, v30.8b
        uadalp          v16.4s, v29.8h
        cbnz            w4, 2b

3:
        uaddlv          d17, v16.4s
        fmov            w0, s17

        ret
endfunc

function nsse16_neon, export=1
        // x0           multiplier
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        str             x0, [sp, #-0x40]!
        stp             x1, x2, [sp, #0x10]
        stp             x3, x4, [sp, #0x20]
        str             x30, [sp, #0x30]
        bl              X(sse16_neon)
        ldr             x30, [sp, #0x30]
        mov             w9, w0                                  // here we store score1
        ldp             x1, x2, [sp, #0x10]
        ldp             x3, x4, [sp, #0x20]
        ldr             x5, [sp], #0x40

        movi            v16.8h, #0
        movi            v17.8h, #0
        movi            v18.8h, #0
        movi            v19.8h, #0

        ld1             {v0.16b}, [x1], x3
        subs            w4, w4, #1                              // we need to make h-1 iterations
        ld1             {v2.16b}, [x2], x3
        ext             v1.16b, v0.16b, v0.16b, #1              // x1 + 1
        cmp             w4, #2
        ext             v3.16b, v2.16b, v2.16b, #1              // x2 + 1

        b.lt            2f

// make 2 iterations at once
1:
        ld1             {v4.16b}, [x1], x3
        ld1             {v6.16b}, [x2], x3
        ld1             {v20.16b}, [x1], x3
        ext             v5.16b, v4.16b, v4.16b, #1              // x1 + stride + 1
        usubl           v31.8h, v0.8b, v4.8b
        usubl2          v30.8h, v0.16b, v4.16b
        ld1             {v22.16b}, [x2], x3
        usubl           v29.8h, v1.8b, v5.8b
        usubl2          v28.8h, v1.16b, v5.16b
        ext             v7.16b, v6.16b, v6.16b, #1              // x2 + stride + 1
        saba            v16.8h, v31.8h, v29.8h
        ext             v21.16b, v20.16b, v20.16b, #1
        saba            v17.8h, v30.8h, v28.8h
        usubl           v27.8h, v2.8b, v6.8b
        usubl2          v26.8h, v2.16b, v6.16b
        ext             v23.16b, v22.16b, v22.16b, #1
        usubl           v25.8h, v3.8b, v7.8b
        usubl2          v24.8h, v3.16b, v7.16b
        saba            v18.8h, v27.8h, v25.8h
        saba            v19.8h, v26.8h, v24.8h

        usubl           v31.8h, v4.8b, v20.8b
        usubl2          v30.8h, v4.16b, v20.16b
        usubl           v29.8h, v5.8b, v21.8b
        usubl2          v28.8h, v5.16b, v21.16b
        saba            v16.8h, v31.8h, v29.8h
        saba            v17.8h, v30.8h, v28.8h
        usubl           v27.8h, v6.8b, v22.8b
        usubl2          v26.8h, v6.16b, v22.16b
        usubl           v25.8h, v7.8b, v23.8b
        usubl2          v24.8h, v7.16b, v23.16b
        saba            v18.8h, v27.8h, v25.8h
        saba            v19.8h, v26.8h, v24.8h
        sub             w4, w4, #2

        mov             v0.16b, v20.16b
        mov             v1.16b, v21.16b
        cmp             w4, #2
        mov             v2.16b, v22.16b
        mov             v3.16b, v23.16b

        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v4.16b}, [x1], x3
        subs            w4, w4, #1
        ld1             {v6.16b}, [x2], x3
        ext             v5.16b, v4.16b, v4.16b, #1              // x1 + stride + 1
        usubl           v31.8h, v0.8b, v4.8b
        ext             v7.16b, v6.16b, v6.16b, #1              // x2 + stride + 1

        usubl2          v30.8h, v0.16b, v4.16b
        usubl           v29.8h, v1.8b, v5.8b
        usubl2          v28.8h, v1.16b, v5.16b
        saba            v16.8h, v31.8h, v29.8h
        saba            v17.8h, v30.8h, v28.8h
        usubl           v27.8h, v2.8b, v6.8b
        usubl2          v26.8h, v2.16b, v6.16b
        usubl           v25.8h, v3.8b, v7.8b
        usubl2          v24.8h, v3.16b, v7.16b
        saba            v18.8h, v27.8h, v25.8h
        saba            v19.8h, v26.8h, v24.8h

        mov             v0.16b, v4.16b
        mov             v1.16b, v5.16b
        mov             v2.16b, v6.16b
        mov             v3.16b, v7.16b

        cbnz            w4, 2b

3:
        sqsub           v17.8h, v17.8h, v19.8h
        sqsub           v16.8h, v16.8h, v18.8h
        ins             v17.h[7], wzr
        sqadd           v16.8h, v16.8h, v17.8h
        saddlv          s16, v16.8h
        sqabs           s16, s16
        fmov            w0, s16

        mul             w0, w0, w5
        add             w0, w0, w9

        ret
endfunc

function nsse8_neon, export=1
        // x0           multiplier
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        str             x0, [sp, #-0x40]!
        stp             x1, x2, [sp, #0x10]
        stp             x3, x4, [sp, #0x20]
        str             x30, [sp, #0x30]
        bl              X(sse8_neon)
        ldr             x30, [sp, #0x30]
        mov             w9, w0                                  // here we store score1
        ldp             x1, x2, [sp, #0x10]
        ldp             x3, x4, [sp, #0x20]
        ldr             x5, [sp], #0x40

        movi            v16.8h, #0
        movi            v17.8h, #0
        movi            v18.8h, #0
        movi            v19.8h, #0

        ld1             {v0.8b}, [x1], x3
        subs            w4, w4, #1                              // we need to make h-1 iterations
        ext             v1.8b, v0.8b, v0.8b, #1                 // x1 + 1
        ld1             {v2.8b}, [x2], x3
        cmp             w4, #2
        ext             v3.8b, v2.8b, v2.8b, #1                 // x2 + 1

        b.lt            2f

// make 2 iterations at once
1:
        ld1             {v4.8b}, [x1], x3
        ld1             {v20.8b}, [x1], x3
        ld1             {v6.8b}, [x2], x3
        ext             v5.8b, v4.8b, v4.8b, #1                 // x1 + stride + 1
        ext             v21.8b, v20.8b, v20.8b, #1
        ld1             {v22.8b}, [x2], x3
        ext             v7.8b, v6.8b, v6.8b, #1                 // x2 + stride + 1
        usubl           v31.8h, v0.8b, v4.8b
        ext             v23.8b, v22.8b, v22.8b, #1
        usubl           v29.8h, v1.8b, v5.8b
        usubl           v27.8h, v2.8b, v6.8b
        usubl           v25.8h, v3.8b, v7.8b
        saba            v16.8h, v31.8h, v29.8h
        usubl           v31.8h, v4.8b, v20.8b
        saba            v18.8h, v27.8h, v25.8h
        sub             w4, w4, #2
        usubl           v29.8h, v5.8b, v21.8b
        mov             v0.16b, v20.16b
        mov             v1.16b, v21.16b
        saba            v16.8h, v31.8h, v29.8h
        usubl           v27.8h, v6.8b, v22.8b
        usubl           v25.8h, v7.8b, v23.8b
        mov             v2.16b, v22.16b
        mov             v3.16b, v23.16b
        cmp             w4, #2
        saba            v18.8h, v27.8h, v25.8h
        b.ge            1b
        cbz             w4, 3f

// iterate by one
2:
        ld1             {v4.8b}, [x1], x3
        subs            w4, w4, #1
        ext             v5.8b, v4.8b, v4.8b, #1                 // x1 + stride + 1
        ld1             {v6.8b}, [x2], x3
        usubl           v31.8h, v0.8b, v4.8b
        ext             v7.8b, v6.8b, v6.8b, #1                 // x2 + stride + 1

        usubl           v29.8h, v1.8b, v5.8b
        saba            v16.8h, v31.8h, v29.8h
        usubl           v27.8h, v2.8b, v6.8b
        usubl           v25.8h, v3.8b, v7.8b
        saba            v18.8h, v27.8h, v25.8h

        mov             v0.16b, v4.16b
        mov             v1.16b, v5.16b
        mov             v2.16b, v6.16b
        mov             v3.16b, v7.16b

        cbnz            w4, 2b

3:
        sqsub           v16.8h, v16.8h, v18.8h
        ins             v16.h[7], wzr
        saddlv          s16, v16.8h
        sqabs           s16, s16
        fmov            w0, s16

        mul             w0, w0, w5
        add             w0, w0, w9

        ret
endfunc

function pix_median_abs16_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v2.16b}, [x1], x3
        ld1             {v3.16b}, [x2], x3
        movi            v31.8h, #0
        movi            v16.8h, #0
        ext             v0.16b, v2.16b, v2.16b, #1
        ext             v1.16b, v3.16b, v3.16b, #1
        usubl           v28.8h, v2.8b, v3.8b
        usubl2          v27.8h, v2.16b, v3.16b
        usubl           v26.8h, v0.8b, v1.8b
        usubl2          v25.8h, v0.16b, v1.16b
        sub             w4, w4, #1                              // we need to make h-1 iterations
        saba            v31.8h, v26.8h, v28.8h
        saba            v16.8h, v25.8h, v27.8h
        mov             h18, v28.h[0]
        cmp             w4, #1
        sqabs           h18, h18
        movi            v0.8h, #0

        b.lt            2f
1:

        ld1             {v6.16b}, [x1], x3                      // pix1 vector for V(j-1)
        ld1             {v7.16b}, [x2], x3                      // pix2 vector for V(j-1)
        subs            w4, w4, #1
        ext             v4.16b, v6.16b, v6.16b, #1              // pix1 vector for V(j)
        ext             v5.16b, v7.16b, v7.16b, #1              // pix2 vector for V(j)

        // protected registers: v30, v29, v28, v27, v26, v25, v24, v23
        // scratch registers: v22, v21, v20, v19, v17

        // To find median of three values, calculate sum of them
        // and subtract max and min value from it.
        usubl           v30.8h, v6.8b, v7.8b                    // V(j-1)
        usubl2          v29.8h, v6.16b, v7.16b                  // V(j-1)
        usubl           v24.8h, v4.8b, v5.8b                    // V(j)
        usubl2          v23.8h, v4.16b, v5.16b                  // V(j)
        saba            v0.8h, v30.8h, v28.8h
        add             v22.8h, v26.8h, v30.8h
        smin            v20.8h, v26.8h, v30.8h
        add             v21.8h, v25.8h, v29.8h
        smax            v19.8h, v26.8h, v30.8h
        sub             v22.8h, v22.8h, v28.8h
        sub             v21.8h, v21.8h, v27.8h
        smin            v17.8h, v19.8h, v22.8h
        smin            v22.8h, v25.8h, v29.8h
        mov             v28.16b, v30.16b
        smax            v20.8h, v20.8h, v17.8h                  // median values lower half
        smax            v19.8h, v25.8h, v29.8h
        saba            v31.8h, v24.8h, v20.8h
        mov             v27.16b, v29.16b
        smin            v19.8h, v19.8h, v21.8h
        mov             v26.16b, v24.16b
        smax            v17.8h, v22.8h, v19.8h                  // median values upper half
        mov             v25.16b, v23.16b
        saba            v16.8h, v23.8h, v17.8h

        b.ne            1b

2:
        mov             h17, v0.h[0]
        ins             v16.h[7], wzr
        add             d18, d18, d17
        add             v31.8h, v31.8h, v16.8h
        uaddlv          s17, v31.8h
        add             d18, d18, d17
        fmov            w0, s18

        ret

endfunc

function vsad_intra8_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *dummy
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v0.8b}, [x1], x3
        sub             w4, w4, #1 // we need to make h-1 iterations
        cmp             w4, #3
        movi            v16.8h, #0
        b.lt            2f

1:
        // v = abs( pix1[0] - pix1[0 + stride] )
        // score = sum(v)
        ld1             {v1.8b}, [x1], x3
        sub             w4, w4, #3
        ld1             {v2.8b}, [x1], x3
        uabal           v16.8h, v0.8b, v1.8b
        ld1             {v3.8b}, [x1], x3
        uabal           v16.8h, v1.8b, v2.8b
        cmp             w4, #3
        mov             v0.8b, v3.8b
        uabal           v16.8h, v2.8b, v3.8b
        b.ge            1b
        cbz             w4, 3f

2:
        ld1             {v1.8b}, [x1], x3
        subs            w4, w4, #1
        uabal           v16.8h, v0.8b, v1.8b
        mov             v0.8b, v1.8b
        cbnz            w4, 2b

3:
        uaddlv          s17, v16.8h
        fmov            w0, s17

        ret
endfunc

function pix_median_abs8_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        ld1             {v2.8b}, [x1], x3
        ld1             {v3.8b}, [x2], x3
        movi            v31.8h, #0
        ext             v0.8b, v2.8b, v2.8b, #1
        ext             v1.8b, v3.8b, v3.8b, #1
        usubl           v28.8h, v2.8b, v3.8b
        usubl           v26.8h, v0.8b, v1.8b
        sub             w4, w4, #1                              // we need to make h-1 iterations
        saba            v31.8h, v26.8h, v28.8h
        mov             h18, v28.h[0]
        cmp             w4, #1
        sqabs           h18, h18
        movi            v0.8h, #0

        b.lt            2f
1:
        ld1             {v6.8b}, [x1], x3                       // pix1 vector for V(j-1)
        ld1             {v7.8b}, [x2], x3                       // pix2 vector for V(j-1)
        subs            w4, w4, #1
        ext             v4.8b, v6.8b, v6.8b, #1                 // pix1 vector for V(j)
        ext             v5.8b, v7.8b, v7.8b, #1                 // pix2 vector for V(j)

        // protected registers: v30, v29, v28, v27, v26, v25, v24, v23
        // scratch registers: v22, v21, v20, v19, v17

        // To find median of three values, calculate sum of them
        // and subtract max and min value from it.
        usubl           v30.8h, v6.8b, v7.8b                    // V(j-1)
        usubl           v24.8h, v4.8b, v5.8b                    // V(j)
        saba            v0.8h, v30.8h, v28.8h
        add             v22.8h, v26.8h, v30.8h
        smin            v20.8h, v26.8h, v30.8h
        smax            v19.8h, v26.8h, v30.8h
        sub             v22.8h, v22.8h, v28.8h
        smin            v17.8h, v19.8h, v22.8h
        mov             v28.16b, v30.16b
        smax            v20.8h, v20.8h, v17.8h                  // median values lower half
        smax            v19.8h, v25.8h, v29.8h
        saba            v31.8h, v24.8h, v20.8h
        mov             v26.16b, v24.16b
        smax            v17.8h, v22.8h, v19.8h                  // median values upper half

        b.ne            1b
2:
        mov             h17, v0.h[0]
        ins             v31.h[7], wzr
        add             d18, d18, d17
        uaddlv          s17, v31.8h
        add             d18, d18, d17
        fmov            w0, s18

        ret

endfunc
