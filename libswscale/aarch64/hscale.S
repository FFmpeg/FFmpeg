/*
 * Copyright (c) 2016 Clément Bœsch <clement stupeflix.com>
 * Copyright (c) 2019-2021 Sebastian Pop <spop@amazon.com>
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

/*
;-----------------------------------------------------------------------------
; horizontal line scaling
;
; void hscale<source_width>to<intermediate_nbits>_<filterSize>_<opt>
;                               (SwsContext *c, int{16,32}_t *dst,
;                                int dstW, const uint{8,16}_t *src,
;                                const int16_t *filter,
;                                const int32_t *filterPos, int filterSize);
;
; Scale one horizontal line. Input is either 8-bit width or 16-bit width
; ($source_width can be either 8, 9, 10 or 16, difference is whether we have to
; downscale before multiplying). Filter is 14 bits. Output is either 15 bits
; (in int16_t) or 19 bits (in int32_t), as given in $intermediate_nbits. Each
; output pixel is generated from $filterSize input pixels, the position of
; the first pixel is given in filterPos[nOutputPixel].
;----------------------------------------------------------------------------- */

function ff_hscale8to15_X8_neon, export=1
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
        addp            v0.4s, v0.4s, v1.4s         // part01 horizontal pair adding
        addp            v2.4s, v2.4s, v3.4s         // part23 horizontal pair adding
        addp            v0.4s, v0.4s, v2.4s         // part0123 horizontal pair adding
        subs            w2, w2, #4                  // dstW -= 4
        sqshrn          v0.4h, v0.4s, #7            // shift and clip the 2x16-bit final values
        st1             {v0.4h}, [x1], #8           // write to destination part0123
        b.gt            1b                          // loop until end of line
        ret
endfunc

function ff_hscale8to15_4_neon, export=1
// x0  SwsContext *c (not used)
// x1  int16_t *dst
// x2  int dstW
// x3  const uint8_t *src
// x4  const int16_t *filter
// x5  const int32_t *filterPos
// x6  int filterSize
// x8-x15 registers for gathering src data

// v0      madd accumulator 4S
// v1-v4   filter values (16 bit) 8H
// v5      madd accumulator 4S
// v16-v19 src values (8 bit) 8B

// This implementation has 4 sections:
//  1. Prefetch src data
//  2. Interleaved prefetching src data and madd
//  3. Complete madd
//  4. Complete remaining iterations when dstW % 8 != 0

        sub             sp, sp, #32                 // allocate 32 bytes on the stack
        cmp             w2, #16                     // if dstW <16, skip to the last block used for wrapping up
        b.lt            2f

        // load 8 values from filterPos to be used as offsets into src
        ldp             w8, w9,  [x5]               // filterPos[idx + 0], [idx + 1]
        ldp             w10, w11, [x5, #8]          // filterPos[idx + 2], [idx + 3]
        ldp             w12, w13, [x5, #16]         // filterPos[idx + 4], [idx + 5]
        ldp             w14, w15, [x5, #24]         // filterPos[idx + 6], [idx + 7]
        add             x5, x5, #32                 // advance filterPos

        // gather random access data from src into contiguous memory
        ldr             w8, [x3, w8, uxtw]          // src[filterPos[idx + 0]][0..3]
        ldr             w9, [x3, w9, uxtw]          // src[filterPos[idx + 1]][0..3]
        ldr             w10, [x3, w10, uxtw]        // src[filterPos[idx + 2]][0..3]
        ldr             w11, [x3, w11, uxtw]        // src[filterPos[idx + 3]][0..3]
        ldr             w12, [x3, w12, uxtw]        // src[filterPos[idx + 4]][0..3]
        ldr             w13, [x3, w13, uxtw]        // src[filterPos[idx + 5]][0..3]
        ldr             w14, [x3, w14, uxtw]        // src[filterPos[idx + 6]][0..3]
        ldr             w15, [x3, w15, uxtw]        // src[filterPos[idx + 7]][0..3]
        stp             w8, w9, [sp]                // *scratch_mem = { src[filterPos[idx + 0]][0..3], src[filterPos[idx + 1]][0..3] }
        stp             w10, w11, [sp, #8]          // *scratch_mem = { src[filterPos[idx + 2]][0..3], src[filterPos[idx + 3]][0..3] }
        stp             w12, w13, [sp, #16]         // *scratch_mem = { src[filterPos[idx + 4]][0..3], src[filterPos[idx + 5]][0..3] }
        stp             w14, w15, [sp, #24]         // *scratch_mem = { src[filterPos[idx + 6]][0..3], src[filterPos[idx + 7]][0..3] }

1:
        ld4             {v16.8b, v17.8b, v18.8b, v19.8b}, [sp] // transpose 8 bytes each from src into 4 registers

        // load 8 values from filterPos to be used as offsets into src
        ldp             w8, w9,  [x5]               // filterPos[idx + 0][0..3], [idx + 1][0..3], next iteration
        ldp             w10, w11, [x5, #8]          // filterPos[idx + 2][0..3], [idx + 3][0..3], next iteration
        ldp             w12, w13, [x5, #16]         // filterPos[idx + 4][0..3], [idx + 5][0..3], next iteration
        ldp             w14, w15, [x5, #24]         // filterPos[idx + 6][0..3], [idx + 7][0..3], next iteration

        movi            v0.2d, #0                   // Clear madd accumulator for idx 0..3
        movi            v5.2d, #0                   // Clear madd accumulator for idx 4..7

        ld4             {v1.8h, v2.8h, v3.8h, v4.8h}, [x4], #64 // load filter idx + 0..7

        add             x5, x5, #32                 // advance filterPos

        // interleaved SIMD and prefetching intended to keep ld/st and vector pipelines busy
        uxtl            v16.8h, v16.8b              // unsigned extend long, covert src data to 16-bit
        uxtl            v17.8h, v17.8b              // unsigned extend long, covert src data to 16-bit
        ldr             w8, [x3, w8, uxtw]          // src[filterPos[idx + 0]], next iteration
        ldr             w9, [x3, w9, uxtw]          // src[filterPos[idx + 1]], next iteration
        uxtl            v18.8h, v18.8b              // unsigned extend long, covert src data to 16-bit
        uxtl            v19.8h, v19.8b              // unsigned extend long, covert src data to 16-bit
        ldr             w10, [x3, w10, uxtw]        // src[filterPos[idx + 2]], next iteration
        ldr             w11, [x3, w11, uxtw]        // src[filterPos[idx + 3]], next iteration

        smlal           v0.4s, v1.4h, v16.4h        // multiply accumulate inner loop j = 0, idx = 0..3
        smlal           v0.4s, v2.4h, v17.4h        // multiply accumulate inner loop j = 1, idx = 0..3
        ldr             w12, [x3, w12, uxtw]        // src[filterPos[idx + 4]], next iteration
        ldr             w13, [x3, w13, uxtw]        // src[filterPos[idx + 5]], next iteration
        smlal           v0.4s, v3.4h, v18.4h        // multiply accumulate inner loop j = 2, idx = 0..3
        smlal           v0.4s, v4.4h, v19.4h        // multiply accumulate inner loop j = 3, idx = 0..3
        ldr             w14, [x3, w14, uxtw]        // src[filterPos[idx + 6]], next iteration
        ldr             w15, [x3, w15, uxtw]        // src[filterPos[idx + 7]], next iteration

        smlal2          v5.4s, v1.8h, v16.8h        // multiply accumulate inner loop j = 0, idx = 4..7
        smlal2          v5.4s, v2.8h, v17.8h        // multiply accumulate inner loop j = 1, idx = 4..7
        stp             w8, w9, [sp]                // *scratch_mem = { src[filterPos[idx + 0]][0..3], src[filterPos[idx + 1]][0..3] }
        stp             w10, w11, [sp, #8]          // *scratch_mem = { src[filterPos[idx + 2]][0..3], src[filterPos[idx + 3]][0..3] }
        smlal2          v5.4s, v3.8h, v18.8h        // multiply accumulate inner loop j = 2, idx = 4..7
        smlal2          v5.4s, v4.8h, v19.8h        // multiply accumulate inner loop j = 3, idx = 4..7
        stp             w12, w13, [sp, #16]         // *scratch_mem = { src[filterPos[idx + 4]][0..3], src[filterPos[idx + 5]][0..3] }
        stp             w14, w15, [sp, #24]         // *scratch_mem = { src[filterPos[idx + 6]][0..3], src[filterPos[idx + 7]][0..3] }

        sub             w2, w2, #8                  // dstW -= 8
        sqshrn          v0.4h, v0.4s, #7            // shift and clip the 2x16-bit final values
        sqshrn          v1.4h, v5.4s, #7            // shift and clip the 2x16-bit final values
        st1             {v0.4h, v1.4h}, [x1], #16   // write to dst[idx + 0..7]
        cmp             w2, #16                     // continue on main loop if there are at least 16 iterations left
        b.ge            1b

        // last full iteration
        ld4             {v16.8b, v17.8b, v18.8b, v19.8b}, [sp]
        ld4             {v1.8h, v2.8h, v3.8h, v4.8h}, [x4], #64 // load filter idx + 0..7

        movi            v0.2d, #0                   // Clear madd accumulator for idx 0..3
        movi            v5.2d, #0                   // Clear madd accumulator for idx 4..7

        uxtl            v16.8h, v16.8b              // unsigned extend long, covert src data to 16-bit
        uxtl            v17.8h, v17.8b              // unsigned extend long, covert src data to 16-bit
        uxtl            v18.8h, v18.8b              // unsigned extend long, covert src data to 16-bit
        uxtl            v19.8h, v19.8b              // unsigned extend long, covert src data to 16-bit

        smlal           v0.4s, v1.4h, v16.4h        // multiply accumulate inner loop j = 0, idx = 0..3
        smlal           v0.4s, v2.4h, v17.4h        // multiply accumulate inner loop j = 1, idx = 0..3
        smlal           v0.4s, v3.4h, v18.4h        // multiply accumulate inner loop j = 2, idx = 0..3
        smlal           v0.4s, v4.4h, v19.4h        // multiply accumulate inner loop j = 3, idx = 0..3

        smlal2          v5.4s, v1.8h, v16.8h        // multiply accumulate inner loop j = 0, idx = 4..7
        smlal2          v5.4s, v2.8h, v17.8h        // multiply accumulate inner loop j = 1, idx = 4..7
        smlal2          v5.4s, v3.8h, v18.8h        // multiply accumulate inner loop j = 2, idx = 4..7
        smlal2          v5.4s, v4.8h, v19.8h        // multiply accumulate inner loop j = 3, idx = 4..7

        subs            w2, w2, #8                  // dstW -= 8
        sqshrn          v0.4h, v0.4s, #7            // shift and clip the 2x16-bit final values
        sqshrn          v1.4h, v5.4s, #7            // shift and clip the 2x16-bit final values
        st1             {v0.4h, v1.4h}, [x1], #16   // write to dst[idx + 0..7]

        cbnz            w2, 2f                      // if >0 iterations remain, jump to the wrap up section

        add             sp, sp, #32                 // clean up stack
        ret

        // finish up when dstW % 8 != 0 or dstW < 16
2:
        // load src
        ldr             w8, [x5], #4                // filterPos[i]
        add             x9, x3, w8, uxtw            // calculate the address for src load
        ld1             {v5.s}[0], [x9]             // src[filterPos[i] + 0..3]
        // load filter
        ld1             {v6.4h}, [x4], #8           // filter[filterSize * i + 0..3]

        uxtl            v5.8h, v5.8b                // unsigned exten long, convert src data to 16-bit
        smull           v0.4s, v5.4h, v6.4h         // 4 iterations of src[...] * filter[...]
        addv            s0, v0.4s                   // add up products of src and filter values
        sqshrn          h0, s0, #7                  // shift and clip the 2x16-bit final value
        st1             {v0.h}[0], [x1], #2         // dst[i] = ...
        sub             w2, w2, #1                  // dstW--
        cbnz            w2, 2b

        add             sp, sp, #32                 // clean up stack
        ret
endfunc
