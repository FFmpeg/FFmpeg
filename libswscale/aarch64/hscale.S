/*
 * Copyright (c) 2016 Clément Bœsch <clement stupeflix.com>
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

function ff_hscale_8_to_15_neon, export=1
        add                 x10, x4, w6, UXTW #1        // filter2 = filter + filterSize*2 (x2 because int16)
1:      ldr                 w8, [x5], #4                // filterPos[0]
        ldr                 w9, [x5], #4                // filterPos[1]
        movi                v4.4S, #0                   // val sum part 1 (for dst[0])
        movi                v5.4S, #0                   // val sum part 2 (for dst[1])
        mov                 w7, w6                      // filterSize counter
        mov                 x13, x3                     // srcp = src
2:      add                 x11, x13, w8, UXTW          // srcp + filterPos[0]
        add                 x12, x13, w9, UXTW          // srcp + filterPos[1]
        ld1                 {v0.8B}, [x11]              // srcp[filterPos[0] + {0..7}]
        ld1                 {v1.8B}, [x12]              // srcp[filterPos[1] + {0..7}]
        ld1                 {v2.8H}, [x4],  #16         // load 8x16-bit filter values, part 1
        ld1                 {v3.8H}, [x10], #16         // ditto at filter+filterSize for part 2
        uxtl                v0.8H, v0.8B                // unpack part 1 to 16-bit
        uxtl                v1.8H, v1.8B                // unpack part 2 to 16-bit
        smull               v16.4S, v0.4H, v2.4H        // v16.i32{0..3} = part 1 of: srcp[filterPos[0] + {0..7}] * filter[{0..7}]
        smull               v18.4S, v1.4H, v3.4H        // v18.i32{0..3} = part 1 of: srcp[filterPos[1] + {0..7}] * filter[{0..7}]
        smull2              v17.4S, v0.8H, v2.8H        // v17.i32{0..3} = part 2 of: srcp[filterPos[0] + {0..7}] * filter[{0..7}]
        smull2              v19.4S, v1.8H, v3.8H        // v19.i32{0..3} = part 2 of: srcp[filterPos[1] + {0..7}] * filter[{0..7}]
        addp                v16.4S, v16.4S, v17.4S      // horizontal pair adding of the 8x32-bit multiplied values for part 1 into 4x32-bit
        addp                v18.4S, v18.4S, v19.4S      // horizontal pair adding of the 8x32-bit multiplied values for part 2 into 4x32-bit
        add                 v4.4S, v4.4S, v16.4S        // update val accumulator for part 1
        add                 v5.4S, v5.4S, v18.4S        // update val accumulator for part 2
        add                 x13, x13, #8                // srcp += 8
        subs                w7, w7, #8                  // processed 8/filterSize
        b.gt                2b                          // inner loop if filterSize not consumed completely
        mov                 x4, x10                     // filter = filter2
        add                 x10, x10, w6, UXTW #1       // filter2 += filterSize*2
        addp                v4.4S, v4.4S, v5.4S         // horizontal pair adding of the 8x32-bit sums into 4x32-bit
        addp                v4.4S, v4.4S, v4.4S         // horizontal pair adding of the 4x32-bit sums into 2x32-bit
        sqrshrun            v4.4H, v4.4S, #7            // shift and clip the 2x16-bit final values
        st1                 {v4.S}[0], [x1], #4         // write to destination
        subs                w2, w2, #2                  // dstW -= 2
        b.gt                1b                          // loop until end of line
        ret
endfunc
