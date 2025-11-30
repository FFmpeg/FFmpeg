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
        sbfiz           x7, x6, #1, #32             // filterSize*2 (*2 because int16)
1:      ldr             w8, [x5], #4                // filterPos[idx]
        ldr             w0, [x5], #4                // filterPos[idx + 1]
        ldr             w11, [x5], #4               // filterPos[idx + 2]
        ldr             w9, [x5], #4                // filterPos[idx + 3]
        mov             x16, x4                     // filter0 = filter
        add             x12, x16, x7                // filter1 = filter0 + filterSize*2
        add             x13, x12, x7                // filter2 = filter1 + filterSize*2
        add             x4, x13, x7                 // filter3 = filter2 + filterSize*2
        movi            v0.2d, #0                   // val sum part 1 (for dst[0])
        movi            v1.2d, #0                   // val sum part 2 (for dst[1])
        movi            v2.2d, #0                   // val sum part 3 (for dst[2])
        movi            v3.2d, #0                   // val sum part 4 (for dst[3])
        add             x17, x3, w8, uxtw           // srcp + filterPos[0]
        add             x8,  x3, w0, uxtw           // srcp + filterPos[1]
        add             x0, x3, w11, uxtw           // srcp + filterPos[2]
        add             x11, x3, w9, uxtw           // srcp + filterPos[3]
        mov             w15, w6                     // filterSize counter
2:      ld1             {v4.8b}, [x17], #8          // srcp[filterPos[0] + {0..7}]
        ld1             {v5.8h}, [x16], #16         // load 8x16-bit filter values, part 1
        ld1             {v6.8b}, [x8], #8           // srcp[filterPos[1] + {0..7}]
        ld1             {v7.8h}, [x12], #16         // load 8x16-bit at filter+filterSize
        uxtl            v4.8h, v4.8b                // unpack part 1 to 16-bit
        smlal           v0.4s, v4.4h, v5.4h         // v0 accumulates srcp[filterPos[0] + {0..3}] * filter[{0..3}]
        smlal2          v0.4s, v4.8h, v5.8h         // v0 accumulates srcp[filterPos[0] + {4..7}] * filter[{4..7}]
        ld1             {v16.8b}, [x0], #8          // srcp[filterPos[2] + {0..7}]
        ld1             {v17.8h}, [x13], #16        // load 8x16-bit at filter+2*filterSize
        uxtl            v6.8h, v6.8b                // unpack part 2 to 16-bit
        smlal           v1.4s, v6.4h, v7.4h         // v1 accumulates srcp[filterPos[1] + {0..3}] * filter[{0..3}]
        uxtl            v16.8h, v16.8b              // unpack part 3 to 16-bit
        smlal           v2.4s, v16.4h, v17.4h       // v2 accumulates srcp[filterPos[2] + {0..3}] * filter[{0..3}]
        smlal2          v2.4s, v16.8h, v17.8h       // v2 accumulates srcp[filterPos[2] + {4..7}] * filter[{4..7}]
        ld1             {v18.8b}, [x11], #8         // srcp[filterPos[3] + {0..7}]
        smlal2          v1.4s, v6.8h, v7.8h         // v1 accumulates srcp[filterPos[1] + {4..7}] * filter[{4..7}]
        ld1             {v19.8h}, [x4], #16         // load 8x16-bit at filter+3*filterSize
        subs            w15, w15, #8                // j -= 8: processed 8/filterSize
        uxtl            v18.8h, v18.8b              // unpack part 4 to 16-bit
        smlal           v3.4s, v18.4h, v19.4h       // v3 accumulates srcp[filterPos[3] + {0..3}] * filter[{0..3}]
        smlal2          v3.4s, v18.8h, v19.8h       // v3 accumulates srcp[filterPos[3] + {4..7}] * filter[{4..7}]
        b.gt            2b                          // inner loop if filterSize not consumed completely
        addp            v0.4s, v0.4s, v0.4s         // part0 horizontal pair adding
        addp            v1.4s, v1.4s, v1.4s         // part1 horizontal pair adding
        addp            v2.4s, v2.4s, v2.4s         // part2 horizontal pair adding
        addp            v3.4s, v3.4s, v3.4s         // part3 horizontal pair adding
        addp            v0.4s, v0.4s, v0.4s         // part0 horizontal pair adding
        addp            v1.4s, v1.4s, v1.4s         // part1 horizontal pair adding
        addp            v2.4s, v2.4s, v2.4s         // part2 horizontal pair adding
        addp            v3.4s, v3.4s, v3.4s         // part3 horizontal pair adding
        zip1            v0.4s, v0.4s, v1.4s         // part01 = zip values from part0 and part1
        zip1            v2.4s, v2.4s, v3.4s         // part23 = zip values from part2 and part3
        mov             v0.d[1], v2.d[0]            // part0123 = zip values from part01 and part23
        subs            w2, w2, #4                  // dstW -= 4
        sqshrn          v0.4h, v0.4s, #7            // shift and clip the 2x16-bit final values
        st1             {v0.4h}, [x1], #8           // write to destination part0123
        b.gt            1b                          // loop until end of line
        ret
endfunc
