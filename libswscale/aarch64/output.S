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

function ff_yuv2planeX_8_neon, export=1
        ld1                 {v0.8B}, [x5]                   // load 8x8-bit dither
        cbz                 w6, 1f                          // check if offsetting present
        ext                 v0.8B, v0.8B, v0.8B, #3         // honor offsetting which can be 0 or 3 only
1:      uxtl                v0.8H, v0.8B                    // extend dither to 16-bit
        ushll               v1.4S, v0.4H, #12               // extend dither to 32-bit with left shift by 12 (part 1)
        ushll2              v2.4S, v0.8H, #12               // extend dither to 32-bit with left shift by 12 (part 2)
        mov                 x7, #0                          // i = 0
2:      mov                 v3.16B, v1.16B                  // initialize accumulator part 1 with dithering value
        mov                 v4.16B, v2.16B                  // initialize accumulator part 2 with dithering value
        mov                 w8, w1                          // tmpfilterSize = filterSize
        mov                 x9, x2                          // srcp    = src
        mov                 x10, x0                         // filterp = filter
3:      ldp                 x11, x12, [x9], #16             // get 2 pointers: src[j] and src[j+1]
        add                 x11, x11, x7, lsl #1            // &src[j  ][i]
        add                 x12, x12, x7, lsl #1            // &src[j+1][i]
        ld1                 {v5.8H}, [x11]                  // read 8x16-bit @ src[j  ][i + {0..7}]: A,B,C,D,E,F,G,H
        ld1                 {v6.8H}, [x12]                  // read 8x16-bit @ src[j+1][i + {0..7}]: I,J,K,L,M,N,O,P
        ld1r                {v7.8H}, [x10], #2              // read 1x16-bit coeff X at filter[j  ] and duplicate across lanes
        ld1r                {v16.8H}, [x10], #2             // read 1x16-bit coeff Y at filter[j+1] and duplicate across lanes
        smlal               v3.4S, v5.4H, v7.4H             // val0 += {A,B,C,D} * X
        smlal2              v4.4S, v5.8H, v7.8H             // val1 += {E,F,G,H} * X
        smlal               v3.4S, v6.4H, v16.4H            // val0 += {I,J,K,L} * Y
        smlal2              v4.4S, v6.8H, v16.8H            // val1 += {M,N,O,P} * Y
        subs                w8, w8, #2                      // tmpfilterSize -= 2
        b.gt                3b                              // loop until filterSize consumed

        sqshrun             v3.4h, v3.4s, #16               // clip16(val0>>16)
        sqshrun2            v3.8h, v4.4s, #16               // clip16(val1>>16)
        uqshrn              v3.8b, v3.8h, #3                // clip8(val>>19)
        st1                 {v3.8b}, [x3], #8               // write to destination
        subs                w4, w4, #8                      // dstW -= 8
        add                 x7, x7, #8                      // i += 8
        b.gt                2b                              // loop until width consumed
        ret
endfunc
