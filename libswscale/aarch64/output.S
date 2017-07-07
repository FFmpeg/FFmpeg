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
        ldr                 w11, [x10], #4                  // read 2x16-bit coeffs (X, Y) at (filter[j], filter[j+1])
        zip1                v16.8H, v5.8H, v6.8H            // A,I,B,J,C,K,D,L
        zip2                v17.8H, v5.8H, v6.8H            // E,M,F,N,F,O,H,P
        dup                 v7.4S, w11                      // X,Y,X,Y,X,Y,X,Y
        smull               v18.4S, v16.4H, v7.4H           // A.X I.Y B.X J.Y
        smull               v20.4S, v17.4H, v7.4H           // E.X M.Y F.X N.Y
        smull2              v19.4S, v16.8H, v7.8H           // C.X K.Y D.X L.Y
        smull2              v21.4S, v17.8H, v7.8H           // G.X O.Y H.X P.Y
        addp                v16.4S, v18.4S, v19.4S          // A.X+I.Y B.X+J.Y C.X+K.Y D.X+L.Y
        addp                v17.4S, v20.4S, v21.4S          // E.X+M.Y F.X+N.Y F.X+O.Y H.X+P.Y
        add                 v3.4S, v3.4S, v16.4S            // update val accumulator for part 1
        add                 v4.4S, v4.4S, v17.4S            // update val accumulator for part 2
        subs                w8, w8, #2                      // tmpfilterSize -= 2
        b.gt                3b                              // loop until filterSize consumed
        sshr                v3.4S, v3.4S, #19               // val>>19 (part 1)
        sshr                v4.4S, v4.4S, #19               // val>>19 (part 2)
        sqxtun              v3.4H, v3.4S                    // clip16(val>>19) (part 1)
        sqxtun              v4.4H, v4.4S                    // clip16(val>>19) (part 2)
        mov                 v3.D[1], v4.D[0]                // merge part 1 and part 2
        uqxtn               v3.8B, v3.8H                    // clip8(val>>19)
        st1                 {v3.1D}, [x3], #8               // write to destination
        add                 x7, x7, #8                      // i += 8
        subs                w4, w4, #8                      // dstW -= 8
        b.gt                2b                              // loop until width consumed
        ret
endfunc
