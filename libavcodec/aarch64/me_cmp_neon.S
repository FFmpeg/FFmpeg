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
        movi            v18.4S, #0                  // clear result accumulator
        b.lt            2f
1:
        ld1             {v0.16b}, [x1], x3          // load pix1
        ld1             {v4.16b}, [x2], x3          // load pix2
        ld1             {v1.16b}, [x1], x3          // load pix1
        ld1             {v5.16b}, [x2], x3          // load pix2
        uabdl           v16.8h, v0.8b, v4.8b        // absolute difference accumulate
        uabdl2          v17.8h, v0.16b, v4.16b
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
        add             v16.8h, v16.8h, v17.8h
        uaddlv          s16, v16.8h                 // add up everything in v16 accumulator
        add             d18, d16, d18               // add to the end result register

        b.ge            1b
        cbnz            w4, 2f                      // if iterations remain, jump to completion section

        fmov            w0, s18                     // copy result to general purpose register
        ret

2:
        ld1             {v0.16b}, [x1], x3          // load pix1
        ld1             {v4.16b}, [x2], x3          // load pix2
        uabdl           v16.8h, v0.8b, v4.8b        // absolute difference accumulate
        uabal2          v16.8h, v0.16b, v4.16b
        subs            w4, w4, #1                  // h -= 1
        addv            h16, v16.8h                 // add up v16
        add             d18, d16, d18               // add to result
        b.ne            2b

        fmov            w0, s18                     // copy result to general purpose register
        ret
endfunc

function ff_pix_abs16_xy2_neon, export=1
        // x0           unused
        // x1           uint8_t *pix1
        // x2           uint8_t *pix2
        // x3           ptrdiff_t stride
        // w4           int h

        add             x5, x2, x3                  // use x5 to hold uint8_t *pix3
        movi            v0.2d, #0                   // initialize the result register

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

        ldur            q19, [x5, #1]               // load pix3+1
        ld1             {v18.16b}, [x5], x3         // load pix3
        ld1             {v17.16b}, [x1], x3         // load pix1

        ldur            q22, [x5, #1]               // load pix3+1
        ld1             {v21.16b}, [x5], x3         // load pix3
        ld1             {v20.16b}, [x1], x3         // load pix1

        // These blocks compute the average: avg(pix2[n], pix2[n+1], pix3[n], pix3[n+1])
        uaddl           v30.8h, v4.8b, v5.8b        // pix3 + pix3+1 0..7
        uaddl2          v31.8h, v4.16b, v5.16b      // pix3 + pix3+1 8..15
        add             v23.8h, v2.8h, v30.8h       // add up 0..7, using pix2 + pix2+1 values from previous iteration
        add             v24.8h, v3.8h, v31.8h       // add up 8..15, using pix2 + pix2+1 values from previous iteration
        rshrn           v23.8b, v23.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v23.16b, v24.8h, #2         // shift right 2 8..15

        uaddl           v2.8h, v6.8b, v7.8b         // pix3 + pix3+1 0..7
        uaddl2          v3.8h, v6.16b, v7.16b       // pix3 + pix3+1 8..15
        add             v26.8h, v30.8h, v2.8h       // add up 0..7, using pix2 + pix2+1 values from pix3 above
        add             v27.8h, v31.8h, v3.8h       // add up 8..15, using pix2 + pix2+1 values from pix3 above
        rshrn           v26.8b, v26.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v26.16b, v27.8h, #2         // shift right 2 8..15

        uaddl           v4.8h, v18.8b, v19.8b       // pix3 + pix3+1 0..7
        uaddl2          v5.8h, v18.16b, v19.16b     // pix3 + pix3+1 8..15
        add             v28.8h, v2.8h, v4.8h        // add up 0..7, using pix2 + pix2+1 values from pix3 above
        add             v29.8h, v3.8h, v5.8h        // add up 8..15, using pix2 + pix2+1 values from pix3 above
        rshrn           v28.8b, v28.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v28.16b, v29.8h, #2         // shift right 2 8..15

        uaddl           v2.8h, v21.8b, v22.8b       // pix3 + pix3+1 0..7
        uaddl2          v3.8h, v21.16b, v22.16b     // pix3 + pix3+1 8..15
        add             v30.8h, v4.8h, v2.8h        // add up 0..7, using pix2 + pix2+1 values from pix3 above
        add             v31.8h, v5.8h, v3.8h        // add up 8..15, using pix2 + pix2+1 values from pix3 above
        rshrn           v30.8b, v30.8h, #2          // shift right 2 0..7 (rounding shift right)
        rshrn2          v30.16b, v31.8h, #2         // shift right 2 8..15

        // Averages are now stored in these registers:
        // v23, v16, v28, v30
        // pix1 values in these registers:
        // v1, v16, v17, v20
        // available:
        // v4, v5, v7, v18, v19, v24, v25, v27, v29, v31

        sub             w4, w4, #4                  // h -= 4

        // Using absolute-difference instructions instead of absolute-difference-accumulate allows
        // us to keep the results in 16b vectors instead of widening values with twice the instructions.
        // This approach also has fewer data dependencies, allowing better instruction level parallelism.
        uabd            v4.16b, v1.16b, v23.16b     // absolute difference 0..15, i=0
        uabd            v5.16b, v16.16b, v26.16b    // absolute difference 0..15, i=1
        uabd            v6.16b, v17.16b, v28.16b    // absolute difference 0..15, i=2
        uabd            v7.16b, v20.16b, v30.16b    // absolute difference 0..15, i=3

        cmp             w4, #4                      // loop if h >= 4

        // Now add up all the values in each vector, v4-v7 with widening adds
        uaddl           v19.8h, v4.8b, v5.8b
        uaddl2          v18.8h, v4.16b, v5.16b
        uaddl           v4.8h, v6.8b, v7.8b
        uaddl2          v5.8h, v6.16b, v7.16b
        add             v4.8h, v4.8h, v5.8h
        add             v4.8h, v4.8h, v18.8h
        add             v4.8h, v4.8h, v19.8h
        uaddlv          s4, v4.8h                   // finish adding up accumulated values
        add             d0, d0, d4                  // add the value to the top level accumulator

        b.ge            1b
        cbnz            w4, 2f                      // if iterations remain jump to completion section

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
        urshr           v16.8h, v16.8h, #2          // shift right by 2 0..7 (rounding shift right)
        urshr           v17.8h, v17.8h, #2          // shift right by 2 8..15

        uxtl2           v8.8h, v1.16b               // 8->16 bits pix1 8..15
        uxtl            v1.8h, v1.8b                // 8->16 bits pix1 0..7

        uabd            v6.8h, v1.8h, v16.8h        // absolute difference 0..7
        uaba            v6.8h, v8.8h, v17.8h        // absolute difference accumulate 8..15
        mov             v2.16b, v18.16b             // pix3 -> pix2
        mov             v3.16b, v19.16b             // pix3+1 -> pix2+1
        uaddlv          s6, v6.8h                   // add up accumulator in v6
        add             d0, d0, d6                  // add to the final result

        b.ne            2b                          // loop if h > 0
        fmov            w0, s0                      // copy result to general purpose register
        ret
endfunc
