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
// x0 - const int16_t *filter,
// x1 - int filterSize,
// x2 - const int16_t **src,
// x3 - uint8_t *dest,
// w4 - int dstW,
// x5 - const uint8_t *dither,
// w6 - int offset

        ld1             {v0.8b}, [x5]                   // load 8x8-bit dither
        and             w6, w6, #7
        cbz             w6, 1f                          // check if offsetting present
        ext             v0.8b, v0.8b, v0.8b, #3         // honor offsetting which can be 0 or 3 only
1:      uxtl            v0.8h, v0.8b                    // extend dither to 16-bit
        ushll           v1.4s, v0.4h, #12               // extend dither to 32-bit with left shift by 12 (part 1)
        ushll2          v2.4s, v0.8h, #12               // extend dither to 32-bit with left shift by 12 (part 2)
        cmp             w1, #8                          // if filterSize == 8, branch to specialized version
        b.eq            6f
        cmp             w1, #4                          // if filterSize == 4, branch to specialized version
        b.eq            8f
        cmp             w1, #2                          // if filterSize == 2, branch to specialized version
        b.eq            10f

// The filter size does not match of the of specialized implementations. It is either even or odd. If it is even
// then use the first section below.
        mov             x7, #0                          // i = 0
        tbnz            w1, #0, 4f                      // if filterSize % 2 != 0 branch to specialized version
// fs % 2 == 0
2:      mov             v3.16b, v1.16b                  // initialize accumulator part 1 with dithering value
        mov             v4.16b, v2.16b                  // initialize accumulator part 2 with dithering value
        mov             w8, w1                          // tmpfilterSize = filterSize
        mov             x9, x2                          // srcp    = src
        mov             x10, x0                         // filterp = filter
3:      ldp             x11, x12, [x9], #16             // get 2 pointers: src[j] and src[j+1]
        ldr             s7, [x10], #4                   // read 2x16-bit coeff X and Y at filter[j] and filter[j+1]
        add             x11, x11, x7, lsl #1            // &src[j  ][i]
        add             x12, x12, x7, lsl #1            // &src[j+1][i]
        ld1             {v5.8h}, [x11]                  // read 8x16-bit @ src[j  ][i + {0..7}]: A,B,C,D,E,F,G,H
        ld1             {v6.8h}, [x12]                  // read 8x16-bit @ src[j+1][i + {0..7}]: I,J,K,L,M,N,O,P
        smlal           v3.4s, v5.4h, v7.h[0]           // val0 += {A,B,C,D} * X
        smlal2          v4.4s, v5.8h, v7.h[0]           // val1 += {E,F,G,H} * X
        smlal           v3.4s, v6.4h, v7.h[1]           // val0 += {I,J,K,L} * Y
        smlal2          v4.4s, v6.8h, v7.h[1]           // val1 += {M,N,O,P} * Y
        subs            w8, w8, #2                      // tmpfilterSize -= 2
        b.gt            3b                              // loop until filterSize consumed

        sqshrun         v3.4h, v3.4s, #16               // clip16(val0>>16)
        sqshrun2        v3.8h, v4.4s, #16               // clip16(val1>>16)
        uqshrn          v3.8b, v3.8h, #3                // clip8(val>>19)
        st1             {v3.8b}, [x3], #8               // write to destination
        subs            w4, w4, #8                      // dstW -= 8
        add             x7, x7, #8                      // i += 8
        b.gt            2b                              // loop until width consumed
        ret

// If filter size is odd (most likely == 1), then use this section.
// fs % 2 != 0
4:      mov             v3.16b, v1.16b                  // initialize accumulator part 1 with dithering value
        mov             v4.16b, v2.16b                  // initialize accumulator part 2 with dithering value
        mov             w8, w1                          // tmpfilterSize = filterSize
        mov             x9, x2                          // srcp    = src
        mov             x10, x0                         // filterp = filter
5:      ldr             x11, [x9], #8                   // get 1 pointer: src[j]
        ldr             h6, [x10], #2                   // read 1 16 bit coeff X at filter[j]
        add             x11, x11, x7, lsl #1            // &src[j  ][i]
        ld1             {v5.8h}, [x11]                  // read 8x16-bit @ src[j  ][i + {0..7}]: A,B,C,D,E,F,G,H
        smlal           v3.4s, v5.4h, v6.h[0]           // val0 += {A,B,C,D} * X
        smlal2          v4.4s, v5.8h, v6.h[0]           // val1 += {E,F,G,H} * X
        subs            w8, w8, #1                      // tmpfilterSize -= 2
        b.gt            5b                              // loop until filterSize consumed

        sqshrun         v3.4h, v3.4s, #16               // clip16(val0>>16)
        sqshrun2        v3.8h, v4.4s, #16               // clip16(val1>>16)
        uqshrn          v3.8b, v3.8h, #3                // clip8(val>>19)
        st1             {v3.8b}, [x3], #8               // write to destination
        subs            w4, w4, #8                      // dstW -= 8
        add             x7, x7, #8                      // i += 8
        b.gt            4b                              // loop until width consumed
        ret

6:      // fs=8
        ldp             x5, x6, [x2]                    // load 2 pointers: src[j  ] and src[j+1]
        ldp             x7, x9, [x2, #16]               // load 2 pointers: src[j+2] and src[j+3]
        ldp             x10, x11, [x2, #32]             // load 2 pointers: src[j+4] and src[j+5]
        ldp             x12, x13, [x2, #48]             // load 2 pointers: src[j+6] and src[j+7]

        // load 8x16-bit values for filter[j], where j=0..7
        ld1             {v6.8h}, [x0]
7:
        mov             v3.16b, v1.16b                  // initialize accumulator part 1 with dithering value
        mov             v4.16b, v2.16b                  // initialize accumulator part 2 with dithering value

        ld1             {v24.8h}, [x5], #16             // load 8x16-bit values for src[j + 0][i + {0..7}]
        ld1             {v25.8h}, [x6], #16             // load 8x16-bit values for src[j + 1][i + {0..7}]
        ld1             {v26.8h}, [x7], #16             // load 8x16-bit values for src[j + 2][i + {0..7}]
        ld1             {v27.8h}, [x9], #16             // load 8x16-bit values for src[j + 3][i + {0..7}]
        ld1             {v28.8h}, [x10], #16            // load 8x16-bit values for src[j + 4][i + {0..7}]
        ld1             {v29.8h}, [x11], #16            // load 8x16-bit values for src[j + 5][i + {0..7}]
        ld1             {v30.8h}, [x12], #16            // load 8x16-bit values for src[j + 6][i + {0..7}]
        ld1             {v31.8h}, [x13], #16            // load 8x16-bit values for src[j + 7][i + {0..7}]

        smlal           v3.4s, v24.4h, v6.h[0]          // val0 += src[0][i + {0..3}] * filter[0]
        smlal2          v4.4s, v24.8h, v6.h[0]          // val1 += src[0][i + {4..7}] * filter[0]
        smlal           v3.4s, v25.4h, v6.h[1]          // val0 += src[1][i + {0..3}] * filter[1]
        smlal2          v4.4s, v25.8h, v6.h[1]          // val1 += src[1][i + {4..7}] * filter[1]
        smlal           v3.4s, v26.4h, v6.h[2]          // val0 += src[2][i + {0..3}] * filter[2]
        smlal2          v4.4s, v26.8h, v6.h[2]          // val1 += src[2][i + {4..7}] * filter[2]
        smlal           v3.4s, v27.4h, v6.h[3]          // val0 += src[3][i + {0..3}] * filter[3]
        smlal2          v4.4s, v27.8h, v6.h[3]          // val1 += src[3][i + {4..7}] * filter[3]
        smlal           v3.4s, v28.4h, v6.h[4]          // val0 += src[4][i + {0..3}] * filter[4]
        smlal2          v4.4s, v28.8h, v6.h[4]          // val1 += src[4][i + {4..7}] * filter[4]
        smlal           v3.4s, v29.4h, v6.h[5]          // val0 += src[5][i + {0..3}] * filter[5]
        smlal2          v4.4s, v29.8h, v6.h[5]          // val1 += src[5][i + {4..7}] * filter[5]
        smlal           v3.4s, v30.4h, v6.h[6]          // val0 += src[6][i + {0..3}] * filter[6]
        smlal2          v4.4s, v30.8h, v6.h[6]          // val1 += src[6][i + {4..7}] * filter[6]
        smlal           v3.4s, v31.4h, v6.h[7]          // val0 += src[7][i + {0..3}] * filter[7]
        smlal2          v4.4s, v31.8h, v6.h[7]          // val1 += src[7][i + {4..7}] * filter[7]

        sqshrun         v3.4h, v3.4s, #16               // clip16(val0>>16)
        sqshrun2        v3.8h, v4.4s, #16               // clip16(val1>>16)
        uqshrn          v3.8b, v3.8h, #3                // clip8(val>>19)
        subs            w4, w4, #8                      // dstW -= 8
        st1             {v3.8b}, [x3], #8               // write to destination
        b.gt            7b                              // loop until width consumed
        ret

8:      // fs=4
        ldp             x5, x6, [x2]                    // load 2 pointers: src[j  ] and src[j+1]
        ldp             x7, x9, [x2, #16]               // load 2 pointers: src[j+2] and src[j+3]

        // load 4x16-bit values for filter[j], where j=0..3 and replicated across lanes
        ld1             {v6.4h}, [x0]
9:
        mov             v3.16b, v1.16b                  // initialize accumulator part 1 with dithering value
        mov             v4.16b, v2.16b                  // initialize accumulator part 2 with dithering value

        ld1             {v24.8h}, [x5], #16             // load 8x16-bit values for src[j + 0][i + {0..7}]
        ld1             {v25.8h}, [x6], #16             // load 8x16-bit values for src[j + 1][i + {0..7}]
        ld1             {v26.8h}, [x7], #16             // load 8x16-bit values for src[j + 2][i + {0..7}]
        ld1             {v27.8h}, [x9], #16             // load 8x16-bit values for src[j + 3][i + {0..7}]

        smlal           v3.4s, v24.4h, v6.h[0]          // val0 += src[0][i + {0..3}] * filter[0]
        smlal2          v4.4s, v24.8h, v6.h[0]          // val1 += src[0][i + {4..7}] * filter[0]
        smlal           v3.4s, v25.4h, v6.h[1]          // val0 += src[1][i + {0..3}] * filter[1]
        smlal2          v4.4s, v25.8h, v6.h[1]          // val1 += src[1][i + {4..7}] * filter[1]
        smlal           v3.4s, v26.4h, v6.h[2]          // val0 += src[2][i + {0..3}] * filter[2]
        smlal2          v4.4s, v26.8h, v6.h[2]          // val1 += src[2][i + {4..7}] * filter[2]
        smlal           v3.4s, v27.4h, v6.h[3]          // val0 += src[3][i + {0..3}] * filter[3]
        smlal2          v4.4s, v27.8h, v6.h[3]          // val1 += src[3][i + {4..7}] * filter[3]

        sqshrun         v3.4h, v3.4s, #16               // clip16(val0>>16)
        sqshrun2        v3.8h, v4.4s, #16               // clip16(val1>>16)
        uqshrn          v3.8b, v3.8h, #3                // clip8(val>>19)
        st1             {v3.8b}, [x3], #8               // write to destination
        subs            w4, w4, #8                      // dstW -= 8
        b.gt            9b                              // loop until width consumed
        ret

10:     // fs=2
        ldp             x5, x6, [x2]                    // load 2 pointers: src[j  ] and src[j+1]

        // load 2x16-bit values for filter[j], where j=0..1 and replicated across lanes
        ldr             s6, [x0]
11:
        mov             v3.16b, v1.16b                  // initialize accumulator part 1 with dithering value
        mov             v4.16b, v2.16b                  // initialize accumulator part 2 with dithering value

        ld1             {v24.8h}, [x5], #16             // load 8x16-bit values for src[j + 0][i + {0..7}]
        ld1             {v25.8h}, [x6], #16             // load 8x16-bit values for src[j + 1][i + {0..7}]

        smlal           v3.4s, v24.4h, v6.h[0]          // val0 += src[0][i + {0..3}] * filter[0]
        smlal2          v4.4s, v24.8h, v6.h[0]          // val1 += src[0][i + {4..7}] * filter[0]
        smlal           v3.4s, v25.4h, v6.h[1]          // val0 += src[1][i + {0..3}] * filter[1]
        smlal2          v4.4s, v25.8h, v6.h[1]          // val1 += src[1][i + {4..7}] * filter[1]

        sqshrun         v3.4h, v3.4s, #16               // clip16(val0>>16)
        sqshrun2        v3.8h, v4.4s, #16               // clip16(val1>>16)
        uqshrn          v3.8b, v3.8h, #3                // clip8(val>>19)
        st1             {v3.8b}, [x3], #8               // write to destination
        subs            w4, w4, #8                      // dstW -= 8
        b.gt            11b                             // loop until width consumed
        ret
endfunc

function ff_yuv2plane1_8_neon, export=1
// x0 - const int16_t *src,
// x1 - uint8_t *dest,
// w2 - int dstW,
// x3 - const uint8_t *dither,
// w4 - int offset
        ld1             {v0.8b}, [x3]                   // load 8x8-bit dither
        and             w4, w4, #7
        cbz             w4, 1f                          // check if offsetting present
        ext             v0.8b, v0.8b, v0.8b, #3         // honor offsetting which can be 0 or 3 only
1:
        uxtl            v0.8h, v0.8b                    // extend dither to 32-bit
2:
        ld1             {v3.8h}, [x0], #16              // read 8x16-bit @ src[j  ][i + {0..7}]: A,B,C,D,E,F,G,H
        subs            w2, w2, #8                      // dstW -= 8
        shadd           v1.8h, v0.8h, v3.8h             // v1 = (v0 + v3) >> 1
        sqshrun         v2.8b, v1.8h, #6                // clip_uint8(v1 >> 6)

        st1             {v2.8b}, [x1], #8               // write to destination
        b.gt            2b                              // loop until width consumed
        ret
endfunc
