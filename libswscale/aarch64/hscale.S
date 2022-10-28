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
        sbfiz               x7, x6, #1, #32             // filterSize*2 (*2 because int16)
1:      ldr                 w8, [x5], #4                // filterPos[idx]
        ldr                 w0, [x5], #4                // filterPos[idx + 1]
        ldr                 w11, [x5], #4               // filterPos[idx + 2]
        ldr                 w9, [x5], #4                // filterPos[idx + 3]
        mov                 x16, x4                     // filter0 = filter
        add                 x12, x16, x7                // filter1 = filter0 + filterSize*2
        add                 x13, x12, x7                // filter2 = filter1 + filterSize*2
        add                 x4, x13, x7                 // filter3 = filter2 + filterSize*2
        movi                v0.2D, #0                   // val sum part 1 (for dst[0])
        movi                v1.2D, #0                   // val sum part 2 (for dst[1])
        movi                v2.2D, #0                   // val sum part 3 (for dst[2])
        movi                v3.2D, #0                   // val sum part 4 (for dst[3])
        add                 x17, x3, w8, UXTW           // srcp + filterPos[0]
        add                 x8,  x3, w0, UXTW           // srcp + filterPos[1]
        add                 x0, x3, w11, UXTW           // srcp + filterPos[2]
        add                 x11, x3, w9, UXTW           // srcp + filterPos[3]
        mov                 w15, w6                     // filterSize counter
2:      ld1                 {v4.8B}, [x17], #8          // srcp[filterPos[0] + {0..7}]
        ld1                 {v5.8H}, [x16], #16         // load 8x16-bit filter values, part 1
        ld1                 {v6.8B}, [x8], #8           // srcp[filterPos[1] + {0..7}]
        ld1                 {v7.8H}, [x12], #16         // load 8x16-bit at filter+filterSize
        uxtl                v4.8H, v4.8B                // unpack part 1 to 16-bit
        smlal               v0.4S, v4.4H, v5.4H         // v0 accumulates srcp[filterPos[0] + {0..3}] * filter[{0..3}]
        smlal2              v0.4S, v4.8H, v5.8H         // v0 accumulates srcp[filterPos[0] + {4..7}] * filter[{4..7}]
        ld1                 {v16.8B}, [x0], #8          // srcp[filterPos[2] + {0..7}]
        ld1                 {v17.8H}, [x13], #16        // load 8x16-bit at filter+2*filterSize
        uxtl                v6.8H, v6.8B                // unpack part 2 to 16-bit
        smlal               v1.4S, v6.4H, v7.4H         // v1 accumulates srcp[filterPos[1] + {0..3}] * filter[{0..3}]
        uxtl                v16.8H, v16.8B              // unpack part 3 to 16-bit
        smlal               v2.4S, v16.4H, v17.4H       // v2 accumulates srcp[filterPos[2] + {0..3}] * filter[{0..3}]
        smlal2              v2.4S, v16.8H, v17.8H       // v2 accumulates srcp[filterPos[2] + {4..7}] * filter[{4..7}]
        ld1                 {v18.8B}, [x11], #8         // srcp[filterPos[3] + {0..7}]
        smlal2              v1.4S, v6.8H, v7.8H         // v1 accumulates srcp[filterPos[1] + {4..7}] * filter[{4..7}]
        ld1                 {v19.8H}, [x4], #16         // load 8x16-bit at filter+3*filterSize
        subs                w15, w15, #8                // j -= 8: processed 8/filterSize
        uxtl                v18.8H, v18.8B              // unpack part 4 to 16-bit
        smlal               v3.4S, v18.4H, v19.4H       // v3 accumulates srcp[filterPos[3] + {0..3}] * filter[{0..3}]
        smlal2              v3.4S, v18.8H, v19.8H       // v3 accumulates srcp[filterPos[3] + {4..7}] * filter[{4..7}]
        b.gt                2b                          // inner loop if filterSize not consumed completely
        addp                v0.4S, v0.4S, v1.4S         // part01 horizontal pair adding
        addp                v2.4S, v2.4S, v3.4S         // part23 horizontal pair adding
        addp                v0.4S, v0.4S, v2.4S         // part0123 horizontal pair adding
        subs                w2, w2, #4                  // dstW -= 4
        sqshrn              v0.4H, v0.4S, #7            // shift and clip the 2x16-bit final values
        st1                 {v0.4H}, [x1], #8           // write to destination part0123
        b.gt                1b                          // loop until end of line
        ret
endfunc

function ff_hscale8to15_X4_neon, export=1
// x0  SwsContext *c (not used)
// x1  int16_t *dst
// w2  int dstW
// x3  const uint8_t *src
// x4  const int16_t *filter
// x5  const int32_t *filterPos
// w6  int filterSize

// This function for filter sizes that are 4 mod 8. In other words, anything that's 0 mod 4 but not
// 0 mod 8. It also assumes that dstW is 0 mod 4.

        lsl                 w7, w6, #1                  // w7 = filterSize * 2
1:
        ldp                 w8, w9,  [x5]               // filterPos[idx + 0], [idx + 1]
        ldp                 w10, w11, [x5, #8]          // filterPos[idx + 2], [idx + 3]

        movi                v16.2d, #0                  // initialize accumulator for idx + 0
        movi                v17.2d, #0                  // initialize accumulator for idx + 1
        movi                v18.2d, #0                  // initialize accumulator for idx + 2
        movi                v19.2d, #0                  // initialize accumulator for idx + 3

        mov                 x12, x4                     // filter pointer for idx + 0
        add                 x13, x4, x7                 // filter pointer for idx + 1
        add                 x8, x3, w8, uxtw            // srcp + filterPos[idx + 0]
        add                 x9, x3, w9, uxtw            // srcp + filterPos[idx + 1]

        add                 x14, x13, x7                // filter pointer for idx + 2
        add                 x10, x3, w10, uxtw          // srcp + filterPos[idx + 2]
        add                 x11, x3, w11, uxtw          // srcp + filterPos[idx + 3]

        mov                 w0, w6                      // copy filterSize to a temp register, w0
        add                 x5, x5, #16                 // advance the filterPos pointer
        add                 x15, x14, x7                // filter pointer for idx + 3
        mov                 x16, xzr                    // temp register for offsetting filter pointers

2:
        // This section loops over 8-wide chunks of filter size
        ldr                 d4, [x8], #8                // load 8 bytes from srcp for idx + 0
        ldr                 q0, [x12, x16]              // load 8 values, 16 bytes from filter for idx + 0

        ldr                 d5, [x9], #8                // load 8 bytes from srcp for idx + 1
        ldr                 q1, [x13, x16]              // load 8 values, 16 bytes from filter for idx + 1

        uxtl                v4.8h, v4.8b                // unsigned extend long for idx + 0
        uxtl                v5.8h, v5.8b                // unsigned extend long for idx + 1

        ldr                 d6, [x10], #8               // load 8 bytes from srcp for idx + 2
        ldr                 q2, [x14, x16]              // load 8 values, 16 bytes from filter for idx + 2

        smlal               v16.4s, v0.4h, v4.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 0
        smlal               v17.4s, v1.4h, v5.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 1

        ldr                 d7, [x11], #8               // load 8 bytes from srcp for idx + 3
        ldr                 q3, [x15, x16]              // load 8 values, 16 bytes from filter for idx + 3

        sub                 w0, w0, #8                  // decrement the remaining filterSize counter
        smlal2              v16.4s, v0.8h, v4.8h        // val += src[srcPos + j + 4..7] * filter[fs * i + j + 4..7], idx + 0
        smlal2              v17.4s, v1.8h, v5.8h        // val += src[srcPos + j + 4..7] * filter[fs * i + j + 4..7], idx + 1
        uxtl                v6.8h, v6.8b                // unsigned extend long for idx + 2
        uxtl                v7.8h, v7.8b                // unsigned extend long for idx + 3
        smlal               v18.4s, v2.4h, v6.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 2
        smlal               v19.4s, v3.4h, v7.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 3

        cmp                 w0, #8                      // are there at least 8 more elements in filter to consume?
        add                 x16, x16, #16               // advance the offsetting register for filter values

        smlal2              v18.4s, v2.8h, v6.8h        // val += src[srcPos + j + 4..7] * filter[fs * i + j + 4..7], idx + 2
        smlal2              v19.4s, v3.8h, v7.8h        // val += src[srcPos + j + 4..7] * filter[fs * i + j + 4..7], idx + 3

        b.ge                2b                          // branch back to inner loop

        // complete the remaining 4 filter elements
        sub                 x17, x7, #8                 // calculate the offset of the filter pointer for the remaining 4 elements

        ldr                 s4, [x8]                    // load 4 bytes from srcp for idx + 0
        ldr                 d0, [x12, x17]              // load 4 values, 8 bytes from filter for idx + 0
        ldr                 s5, [x9]                    // load 4 bytes from srcp for idx + 1
        ldr                 d1, [x13, x17]              // load 4 values, 8 bytes from filter for idx + 1

        uxtl                v4.8h, v4.8b                // unsigned extend long for idx + 0
        uxtl                v5.8h, v5.8b                // unsigned extend long for idx + 1

        ldr                 s6, [x10]                   // load 4 bytes from srcp for idx + 2
        ldr                 d2, [x14, x17]              // load 4 values, 8 bytes from filter for idx + 2
        smlal               v16.4s, v0.4h, v4.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 0
        smlal               v17.4s, v1.4h, v5.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 1
        ldr                 s7, [x11]                   // load 4 bytes from srcp for idx + 3
        ldr                 d3, [x15, x17]              // load 4 values, 8 bytes from filter for idx + 3

        uxtl                v6.8h, v6.8b                // unsigned extend long for idx + 2
        uxtl                v7.8h, v7.8b                // unsigned extend long for idx + 3
        addp                v16.4s, v16.4s, v17.4s      // horizontal pair adding for idx 0,1
        smlal               v18.4s, v2.4h, v6.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 2
        smlal               v19.4s, v3.4h, v7.4h        // val += src[srcPos + j + 0..3] * filter[fs * i + j + 0..3], idx + 3

        addp                v18.4s, v18.4s, v19.4s      // horizontal pair adding for idx 2,3
        addp                v16.4s, v16.4s, v18.4s      // final horizontal pair adding producing one vector with results for idx = 0..3

        subs                w2, w2, #4                  // dstW -= 4
        sqshrn              v0.4h, v16.4s, #7           // shift and clip the 2x16-bit final values
        st1                 {v0.4h}, [x1], #8           // write to destination idx 0..3
        add                 x4, x4, x7, lsl #2          // filter += (filterSize*2) * 4
        b.gt                1b                          // loop until end of line
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

        sub                 sp, sp, #32                 // allocate 32 bytes on the stack
        cmp                 w2, #16                     // if dstW <16, skip to the last block used for wrapping up
        b.lt                2f

        // load 8 values from filterPos to be used as offsets into src
        ldp                 w8, w9,  [x5]               // filterPos[idx + 0], [idx + 1]
        ldp                 w10, w11, [x5, #8]          // filterPos[idx + 2], [idx + 3]
        ldp                 w12, w13, [x5, #16]         // filterPos[idx + 4], [idx + 5]
        ldp                 w14, w15, [x5, #24]         // filterPos[idx + 6], [idx + 7]
        add                 x5, x5, #32                 // advance filterPos

        // gather random access data from src into contiguous memory
        ldr                 w8, [x3, w8, UXTW]          // src[filterPos[idx + 0]][0..3]
        ldr                 w9, [x3, w9, UXTW]          // src[filterPos[idx + 1]][0..3]
        ldr                 w10, [x3, w10, UXTW]        // src[filterPos[idx + 2]][0..3]
        ldr                 w11, [x3, w11, UXTW]        // src[filterPos[idx + 3]][0..3]
        ldr                 w12, [x3, w12, UXTW]        // src[filterPos[idx + 4]][0..3]
        ldr                 w13, [x3, w13, UXTW]        // src[filterPos[idx + 5]][0..3]
        ldr                 w14, [x3, w14, UXTW]        // src[filterPos[idx + 6]][0..3]
        ldr                 w15, [x3, w15, UXTW]        // src[filterPos[idx + 7]][0..3]
        stp                 w8, w9, [sp]                // *scratch_mem = { src[filterPos[idx + 0]][0..3], src[filterPos[idx + 1]][0..3] }
        stp                 w10, w11, [sp, #8]          // *scratch_mem = { src[filterPos[idx + 2]][0..3], src[filterPos[idx + 3]][0..3] }
        stp                 w12, w13, [sp, #16]         // *scratch_mem = { src[filterPos[idx + 4]][0..3], src[filterPos[idx + 5]][0..3] }
        stp                 w14, w15, [sp, #24]         // *scratch_mem = { src[filterPos[idx + 6]][0..3], src[filterPos[idx + 7]][0..3] }

1:
        ld4                 {v16.8B, v17.8B, v18.8B, v19.8B}, [sp] // transpose 8 bytes each from src into 4 registers

        // load 8 values from filterPos to be used as offsets into src
        ldp                 w8, w9,  [x5]               // filterPos[idx + 0][0..3], [idx + 1][0..3], next iteration
        ldp                 w10, w11, [x5, #8]          // filterPos[idx + 2][0..3], [idx + 3][0..3], next iteration
        ldp                 w12, w13, [x5, #16]         // filterPos[idx + 4][0..3], [idx + 5][0..3], next iteration
        ldp                 w14, w15, [x5, #24]         // filterPos[idx + 6][0..3], [idx + 7][0..3], next iteration

        movi                v0.2D, #0                   // Clear madd accumulator for idx 0..3
        movi                v5.2D, #0                   // Clear madd accumulator for idx 4..7

        ld4                 {v1.8H, v2.8H, v3.8H, v4.8H}, [x4], #64 // load filter idx + 0..7

        add                 x5, x5, #32                 // advance filterPos

        // interleaved SIMD and prefetching intended to keep ld/st and vector pipelines busy
        uxtl                v16.8H, v16.8B              // unsigned extend long, covert src data to 16-bit
        uxtl                v17.8H, v17.8B              // unsigned extend long, covert src data to 16-bit
        ldr                 w8, [x3, w8, UXTW]          // src[filterPos[idx + 0]], next iteration
        ldr                 w9, [x3, w9, UXTW]          // src[filterPos[idx + 1]], next iteration
        uxtl                v18.8H, v18.8B              // unsigned extend long, covert src data to 16-bit
        uxtl                v19.8H, v19.8B              // unsigned extend long, covert src data to 16-bit
        ldr                 w10, [x3, w10, UXTW]        // src[filterPos[idx + 2]], next iteration
        ldr                 w11, [x3, w11, UXTW]        // src[filterPos[idx + 3]], next iteration

        smlal               v0.4S, v1.4H, v16.4H        // multiply accumulate inner loop j = 0, idx = 0..3
        smlal               v0.4S, v2.4H, v17.4H        // multiply accumulate inner loop j = 1, idx = 0..3
        ldr                 w12, [x3, w12, UXTW]        // src[filterPos[idx + 4]], next iteration
        ldr                 w13, [x3, w13, UXTW]        // src[filterPos[idx + 5]], next iteration
        smlal               v0.4S, v3.4H, v18.4H        // multiply accumulate inner loop j = 2, idx = 0..3
        smlal               v0.4S, v4.4H, v19.4H        // multiply accumulate inner loop j = 3, idx = 0..3
        ldr                 w14, [x3, w14, UXTW]        // src[filterPos[idx + 6]], next iteration
        ldr                 w15, [x3, w15, UXTW]        // src[filterPos[idx + 7]], next iteration

        smlal2              v5.4S, v1.8H, v16.8H        // multiply accumulate inner loop j = 0, idx = 4..7
        smlal2              v5.4S, v2.8H, v17.8H        // multiply accumulate inner loop j = 1, idx = 4..7
        stp                 w8, w9, [sp]                // *scratch_mem = { src[filterPos[idx + 0]][0..3], src[filterPos[idx + 1]][0..3] }
        stp                 w10, w11, [sp, #8]          // *scratch_mem = { src[filterPos[idx + 2]][0..3], src[filterPos[idx + 3]][0..3] }
        smlal2              v5.4S, v3.8H, v18.8H        // multiply accumulate inner loop j = 2, idx = 4..7
        smlal2              v5.4S, v4.8H, v19.8H        // multiply accumulate inner loop j = 3, idx = 4..7
        stp                 w12, w13, [sp, #16]         // *scratch_mem = { src[filterPos[idx + 4]][0..3], src[filterPos[idx + 5]][0..3] }
        stp                 w14, w15, [sp, #24]         // *scratch_mem = { src[filterPos[idx + 6]][0..3], src[filterPos[idx + 7]][0..3] }

        sub                 w2, w2, #8                  // dstW -= 8
        sqshrn              v0.4H, v0.4S, #7            // shift and clip the 2x16-bit final values
        sqshrn              v1.4H, v5.4S, #7            // shift and clip the 2x16-bit final values
        st1                 {v0.4H, v1.4H}, [x1], #16   // write to dst[idx + 0..7]
        cmp                 w2, #16                     // continue on main loop if there are at least 16 iterations left
        b.ge                1b

        // last full iteration
        ld4                 {v16.8B, v17.8B, v18.8B, v19.8B}, [sp]
        ld4                 {v1.8H, v2.8H, v3.8H, v4.8H}, [x4], #64 // load filter idx + 0..7

        movi                v0.2D, #0                   // Clear madd accumulator for idx 0..3
        movi                v5.2D, #0                   // Clear madd accumulator for idx 4..7

        uxtl                v16.8H, v16.8B              // unsigned extend long, covert src data to 16-bit
        uxtl                v17.8H, v17.8B              // unsigned extend long, covert src data to 16-bit
        uxtl                v18.8H, v18.8B              // unsigned extend long, covert src data to 16-bit
        uxtl                v19.8H, v19.8B              // unsigned extend long, covert src data to 16-bit

        smlal               v0.4S, v1.4H, v16.4H        // multiply accumulate inner loop j = 0, idx = 0..3
        smlal               v0.4S, v2.4H, v17.4H        // multiply accumulate inner loop j = 1, idx = 0..3
        smlal               v0.4S, v3.4H, v18.4H        // multiply accumulate inner loop j = 2, idx = 0..3
        smlal               v0.4S, v4.4H, v19.4H        // multiply accumulate inner loop j = 3, idx = 0..3

        smlal2              v5.4S, v1.8H, v16.8H        // multiply accumulate inner loop j = 0, idx = 4..7
        smlal2              v5.4S, v2.8H, v17.8H        // multiply accumulate inner loop j = 1, idx = 4..7
        smlal2              v5.4S, v3.8H, v18.8H        // multiply accumulate inner loop j = 2, idx = 4..7
        smlal2              v5.4S, v4.8H, v19.8H        // multiply accumulate inner loop j = 3, idx = 4..7

        subs                w2, w2, #8                  // dstW -= 8
        sqshrn              v0.4H, v0.4S, #7            // shift and clip the 2x16-bit final values
        sqshrn              v1.4H, v5.4S, #7            // shift and clip the 2x16-bit final values
        st1                 {v0.4H, v1.4H}, [x1], #16   // write to dst[idx + 0..7]

        cbnz                w2, 2f                      // if >0 iterations remain, jump to the wrap up section

        add                 sp, sp, #32                 // clean up stack
        ret

        // finish up when dstW % 8 != 0 or dstW < 16
2:
        // load src
        ldr                 w8, [x5], #4                // filterPos[i]
        add                 x9, x3, w8, UXTW            // calculate the address for src load
        ld1                 {v5.S}[0], [x9]             // src[filterPos[i] + 0..3]
        // load filter
        ld1                 {v6.4H}, [x4], #8           // filter[filterSize * i + 0..3]

        uxtl                v5.8H, v5.8B                // unsigned exten long, convert src data to 16-bit
        smull               v0.4S, v5.4H, v6.4H         // 4 iterations of src[...] * filter[...]
        addv                s0, v0.4S                   // add up products of src and filter values
        sqshrn              h0, s0, #7                  // shift and clip the 2x16-bit final value
        st1                 {v0.H}[0], [x1], #2         // dst[i] = ...
        sub                 w2, w2, #1                  // dstW--
        cbnz                w2, 2b

        add                 sp, sp, #32                 // clean up stack
        ret
endfunc

function ff_hscale8to19_4_neon, export=1
        // x0               SwsContext *c (unused)
        // x1               int32_t *dst
        // w2               int dstW
        // x3               const uint8_t *src // treat it as uint16_t *src
        // x4               const uint16_t *filter
        // x5               const int32_t *filterPos
        // w6               int filterSize

        movi                v18.4s, #1
        movi                v17.4s, #1
        shl                 v18.4s, v18.4s, #19
        sub                 v18.4s, v18.4s, v17.4s      // max allowed value

        cmp                 w2, #16
        b.lt                2f // move to last block

        ldp                 w8, w9, [x5]                // filterPos[0], filterPos[1]
        ldp                 w10, w11, [x5, #8]          // filterPos[2], filterPos[3]
        ldp                 w12, w13, [x5, #16]         // filterPos[4], filterPos[5]
        ldp                 w14, w15, [x5, #24]         // filterPos[6], filterPos[7]
        add                 x5, x5, #32

        // load data from
        ldr                 w8, [x3, w8, UXTW]
        ldr                 w9, [x3, w9, UXTW]
        ldr                 w10, [x3, w10, UXTW]
        ldr                 w11, [x3, w11, UXTW]
        ldr                 w12, [x3, w12, UXTW]
        ldr                 w13, [x3, w13, UXTW]
        ldr                 w14, [x3, w14, UXTW]
        ldr                 w15, [x3, w15, UXTW]

        sub                 sp, sp, #32

        stp                 w8, w9, [sp]
        stp                 w10, w11, [sp, #8]
        stp                 w12, w13, [sp, #16]
        stp                 w14, w15, [sp, #24]

1:
        ld4                 {v0.8b, v1.8b, v2.8b, v3.8b}, [sp]
        ld4                 {v28.8h, v29.8h, v30.8h, v31.8h}, [x4], #64 // filter[0..7]
        // load filterPositions into registers for next iteration

        ldp                 w8, w9, [x5]                // filterPos[0], filterPos[1]
        ldp                 w10, w11, [x5, #8]          // filterPos[2], filterPos[3]
        ldp                 w12, w13, [x5, #16]         // filterPos[4], filterPos[5]
        ldp                 w14, w15, [x5, #24]         // filterPos[6], filterPos[7]
        add                 x5, x5, #32
        uxtl                v0.8h, v0.8b
        ldr                 w8, [x3, w8, UXTW]
        smull               v5.4s, v0.4h, v28.4h        // multiply first column of src
        ldr                 w9, [x3, w9, UXTW]
        smull2              v6.4s, v0.8h, v28.8h
        stp                 w8, w9, [sp]

        uxtl                v1.8h, v1.8b
        ldr                 w10, [x3, w10, UXTW]
        smlal               v5.4s, v1.4h, v29.4h        // multiply second column of src
        ldr                 w11, [x3, w11, UXTW]
        smlal2              v6.4s, v1.8h, v29.8h
        stp                 w10, w11, [sp, #8]

        uxtl                v2.8h, v2.8b
        ldr                 w12, [x3, w12, UXTW]
        smlal               v5.4s, v2.4h, v30.4h        // multiply third column of src
        ldr                 w13, [x3, w13, UXTW]
        smlal2              v6.4s, v2.8h, v30.8h
        stp                 w12, w13, [sp, #16]

        uxtl                v3.8h, v3.8b
        ldr                 w14, [x3, w14, UXTW]
        smlal               v5.4s, v3.4h, v31.4h        // multiply fourth column of src
        ldr                 w15, [x3, w15, UXTW]
        smlal2              v6.4s, v3.8h, v31.8h
        stp                 w14, w15, [sp, #24]

        sub                 w2, w2, #8
        sshr                v5.4s, v5.4s, #3
        sshr                v6.4s, v6.4s, #3
        smin                v5.4s, v5.4s, v18.4s
        smin                v6.4s, v6.4s, v18.4s

        st1                 {v5.4s, v6.4s}, [x1], #32
        cmp                 w2, #16
        b.ge                1b

        // here we make last iteration, without updating the registers
        ld4                 {v0.8b, v1.8b, v2.8b, v3.8b}, [sp]
        ld4                 {v28.8h, v29.8h, v30.8h, v31.8h}, [x4], #64 // filter[0..7]

        uxtl                v0.8h, v0.8b
        uxtl                v1.8h, v1.8b
        smull               v5.4s, v0.4h, v28.4h
        smull2              v6.4s, v0.8h, v28.8h
        uxtl                v2.8h, v2.8b
        smlal               v5.4s, v1.4h, v29.4H
        smlal2              v6.4s, v1.8h, v29.8H
        uxtl                v3.8h, v3.8b
        smlal               v5.4s, v2.4h, v30.4H
        smlal2              v6.4s, v2.8h, v30.8H
        smlal               v5.4s, v3.4h, v31.4H
        smlal2              v6.4s, v3.8h, v31.8h

        sshr                v5.4s, v5.4s, #3
        sshr                v6.4s, v6.4s, #3

        smin                v5.4s, v5.4s, v18.4s
        smin                v6.4s, v6.4s, v18.4s

        sub                 w2, w2, #8
        st1                 {v5.4s, v6.4s}, [x1], #32
        add                 sp, sp, #32 // restore stack
        cbnz                w2, 2f

        ret

2:
        ldr                 w8, [x5], #4 // load filterPos
        add                 x9, x3, w8, UXTW // src + filterPos
        ld1                 {v0.s}[0], [x9] // load 4 * uint8_t* into one single
        ld1                 {v31.4h}, [x4], #8
        uxtl                v0.8h, v0.8b
        smull               v5.4s, v0.4h, v31.4H
        saddlv              d0, v5.4S
        sqshrn              s0, d0, #3
        smin                v0.4s, v0.4s, v18.4s
        st1                 {v0.s}[0], [x1], #4
        sub                 w2, w2, #1
        cbnz                w2, 2b // if iterations remain jump to beginning

        ret
endfunc

function ff_hscale8to19_X8_neon, export=1
        movi                v20.4s, #1
        movi                v17.4s, #1
        shl                 v20.4s, v20.4s, #19
        sub                 v20.4s, v20.4s, v17.4s

        sbfiz               x7, x6, #1, #32             // filterSize*2 (*2 because int16)
1:
        mov                 x16, x4                     // filter0 = filter
        ldr                 w8, [x5], #4                // filterPos[idx]
        add                 x12, x16, x7                // filter1 = filter0 + filterSize*2
        ldr                 w0, [x5], #4                // filterPos[idx + 1]
        add                 x13, x12, x7                // filter2 = filter1 + filterSize*2
        ldr                 w11, [x5], #4               // filterPos[idx + 2]
        add                 x4, x13, x7                 // filter3 = filter2 + filterSize*2
        ldr                 w9, [x5], #4                // filterPos[idx + 3]
        movi                v0.2D, #0                   // val sum part 1 (for dst[0])
        movi                v1.2D, #0                   // val sum part 2 (for dst[1])
        movi                v2.2D, #0                   // val sum part 3 (for dst[2])
        movi                v3.2D, #0                   // val sum part 4 (for dst[3])
        add                 x17, x3, w8, UXTW           // srcp + filterPos[0]
        add                 x8,  x3, w0, UXTW           // srcp + filterPos[1]
        add                 x0, x3, w11, UXTW           // srcp + filterPos[2]
        add                 x11, x3, w9, UXTW           // srcp + filterPos[3]
        mov                 w15, w6                     // filterSize counter
2:      ld1                 {v4.8B}, [x17], #8          // srcp[filterPos[0] + {0..7}]
        ld1                 {v5.8H}, [x16], #16         // load 8x16-bit filter values, part 1
        uxtl                v4.8H, v4.8B                // unpack part 1 to 16-bit
        smlal               v0.4S, v4.4H, v5.4H         // v0 accumulates srcp[filterPos[0] + {0..3}] * filter[{0..3}]
        ld1                 {v6.8B}, [x8], #8           // srcp[filterPos[1] + {0..7}]
        smlal2              v0.4S, v4.8H, v5.8H         // v0 accumulates srcp[filterPos[0] + {4..7}] * filter[{4..7}]
        ld1                 {v7.8H}, [x12], #16         // load 8x16-bit at filter+filterSize
        ld1                 {v16.8B}, [x0], #8          // srcp[filterPos[2] + {0..7}]
        uxtl                v6.8H, v6.8B                // unpack part 2 to 16-bit
        ld1                 {v17.8H}, [x13], #16        // load 8x16-bit at filter+2*filterSize
        uxtl                v16.8H, v16.8B              // unpack part 3 to 16-bit
        smlal               v1.4S, v6.4H, v7.4H         // v1 accumulates srcp[filterPos[1] + {0..3}] * filter[{0..3}]
        ld1                 {v18.8B}, [x11], #8         // srcp[filterPos[3] + {0..7}]
        smlal               v2.4S, v16.4H, v17.4H       // v2 accumulates srcp[filterPos[2] + {0..3}] * filter[{0..3}]
        ld1                 {v19.8H}, [x4], #16         // load 8x16-bit at filter+3*filterSize
        smlal2              v2.4S, v16.8H, v17.8H       // v2 accumulates srcp[filterPos[2] + {4..7}] * filter[{4..7}]
        uxtl                v18.8H, v18.8B              // unpack part 4 to 16-bit
        smlal2              v1.4S, v6.8H, v7.8H         // v1 accumulates srcp[filterPos[1] + {4..7}] * filter[{4..7}]
        smlal               v3.4S, v18.4H, v19.4H       // v3 accumulates srcp[filterPos[3] + {0..3}] * filter[{0..3}]
        subs                w15, w15, #8                // j -= 8: processed 8/filterSize
        smlal2              v3.4S, v18.8H, v19.8H       // v3 accumulates srcp[filterPos[3] + {4..7}] * filter[{4..7}]
        b.gt                2b                          // inner loop if filterSize not consumed completely
        addp                v0.4S, v0.4S, v1.4S         // part01 horizontal pair adding
        addp                v2.4S, v2.4S, v3.4S         // part23 horizontal pair adding
        addp                v0.4S, v0.4S, v2.4S         // part0123 horizontal pair adding
        subs                w2, w2, #4                  // dstW -= 4
        sshr                v0.4s, v0.4S, #3            // shift and clip the 2x16-bit final values
        smin                v0.4s, v0.4s, v20.4s
        st1                 {v0.4s}, [x1], #16           // write to destination part0123
        b.gt                1b                          // loop until end of line
        ret
endfunc

function ff_hscale8to19_X4_neon, export=1
        // x0  SwsContext *c (not used)
        // x1  int16_t *dst
        // w2  int dstW
        // x3  const uint8_t *src
        // x4  const int16_t *filter
        // x5  const int32_t *filterPos
        // w6  int filterSize

        movi                v20.4s, #1
        movi                v17.4s, #1
        shl                 v20.4s, v20.4s, #19
        sub                 v20.4s, v20.4s, v17.4s

        lsl                 w7, w6, #1
1:
        ldp                 w8, w9, [x5]
        ldp                 w10, w11, [x5, #8]

        movi                v16.2d, #0                  // initialize accumulator for idx + 0
        movi                v17.2d, #0                  // initialize accumulator for idx + 1
        movi                v18.2d, #0                  // initialize accumulator for idx + 2
        movi                v19.2d, #0                  // initialize accumulator for idx + 3

        mov                 x12, x4                     // filter + 0
        add                 x13, x4, x7                 // filter + 1
        add                 x8, x3, w8, UXTW            // srcp + filterPos 0
        add                 x14, x13, x7                // filter + 2
        add                 x9, x3, w9, UXTW            // srcp + filterPos 1
        add                 x15, x14, x7                // filter + 3
        add                 x10, x3, w10, UXTW          // srcp + filterPos 2
        mov                 w0, w6                      // save the filterSize to temporary variable
        add                 x11, x3, w11, UXTW          // srcp + filterPos 3
        add                 x5, x5, #16                 // advance filter position
        mov                 x16, xzr                    // clear the register x16 used for offsetting the filter values

2:
        ldr                 d4, [x8], #8                // load src values for idx 0
        ldr                 q31, [x12, x16]             // load filter values for idx 0
        uxtl                v4.8h, v4.8b                // extend type to match the filter' size
        ldr                 d5, [x9], #8                // load src values for idx 1
        smlal               v16.4s, v4.4h, v31.4h       // multiplication of lower half for idx 0
        uxtl                v5.8h, v5.8b                // extend type to match the filter' size
        ldr                 q30, [x13, x16]             // load filter values for idx 1
        smlal2              v16.4s, v4.8h, v31.8h       // multiplication of upper half for idx 0
        ldr                 d6, [x10], #8               // load src values for idx 2
        ldr                 q29, [x14, x16]             // load filter values for idx 2
        smlal               v17.4s, v5.4h, v30.4H       // multiplication of lower half for idx 1
        ldr                 d7, [x11], #8               // load src values for idx 3
        smlal2              v17.4s, v5.8h, v30.8H       // multiplication of upper half for idx 1
        uxtl                v6.8h, v6.8B                // extend tpye to matchi the filter's size
        ldr                 q28, [x15, x16]             // load filter values for idx 3
        smlal               v18.4s, v6.4h, v29.4h       // multiplication of lower half for idx 2
        uxtl                v7.8h, v7.8B
        smlal2              v18.4s, v6.8h, v29.8H       // multiplication of upper half for idx 2
        sub                 w0, w0, #8
        smlal               v19.4s, v7.4h, v28.4H       // multiplication of lower half for idx 3
        cmp                 w0, #8
        smlal2              v19.4s, v7.8h, v28.8h       // multiplication of upper half for idx 3
        add                 x16, x16, #16                // advance filter values indexing

        b.ge                2b


        // 4 iterations left

        sub                 x17, x7, #8                 // step back to wrap up the filter pos for last 4 elements

        ldr                 s4, [x8]                    // load src values for idx 0
        ldr                 d31, [x12, x17]             // load filter values for idx 0
        uxtl                v4.8h, v4.8b                // extend type to match the filter' size
        ldr                 s5, [x9]                    // load src values for idx 1
        smlal               v16.4s, v4.4h, v31.4h
        ldr                 d30, [x13, x17]             // load filter values for idx 1
        uxtl                v5.8h, v5.8b                // extend type to match the filter' size
        ldr                 s6, [x10]                   // load src values for idx 2
        smlal               v17.4s, v5.4h, v30.4h
        uxtl                v6.8h, v6.8B                // extend type to match the filter's size
        ldr                 d29, [x14, x17]             // load filter values for idx 2
        ldr                 s7, [x11]                   // load src values for idx 3
        addp                v16.4s, v16.4s, v17.4s
        uxtl                v7.8h, v7.8B
        ldr                 d28, [x15, x17]             // load filter values for idx 3
        smlal               v18.4s, v6.4h, v29.4h
        smlal               v19.4s, v7.4h, v28.4h
        subs                w2, w2, #4
        addp                v18.4s, v18.4s, v19.4s
        addp                v16.4s, v16.4s, v18.4s
        sshr                v16.4s, v16.4s, #3
        smin                v16.4s, v16.4s, v20.4s

        st1                 {v16.4s}, [x1], #16
        add                 x4, x4, x7, lsl #2
        b.gt                1b
        ret
endfunc

function ff_hscale16to15_4_neon_asm, export=1
        // w0               int shift
        // x1               int32_t *dst
        // w2               int dstW
        // x3               const uint8_t *src // treat it as uint16_t *src
        // x4               const uint16_t *filter
        // x5               const int32_t *filterPos
        // w6               int filterSize

        movi                v18.4s, #1
        movi                v17.4s, #1
        shl                 v18.4s, v18.4s, #15
        sub                 v18.4s, v18.4s, v17.4s      // max allowed value
        dup                 v17.4s, w0                  // read shift
        neg                 v17.4s, v17.4s              // negate it, so it can be used in sshl (effectively shift right)

        cmp                 w2, #16
        b.lt                2f // move to last block

        ldp                 w8, w9, [x5]                // filterPos[0], filterPos[1]
        ldp                 w10, w11, [x5, #8]          // filterPos[2], filterPos[3]
        ldp                 w12, w13, [x5, #16]         // filterPos[4], filterPos[5]
        ldp                 w14, w15, [x5, #24]         // filterPos[6], filterPos[7]
        add                 x5, x5, #32

        // shift all filterPos left by one, as uint16_t will be read
        lsl                 x8, x8, #1
        lsl                 x9, x9, #1
        lsl                 x10, x10, #1
        lsl                 x11, x11, #1
        lsl                 x12, x12, #1
        lsl                 x13, x13, #1
        lsl                 x14, x14, #1
        lsl                 x15, x15, #1

        // load src with given offset
        ldr                 x8,  [x3, w8,  UXTW]
        ldr                 x9,  [x3, w9,  UXTW]
        ldr                 x10, [x3, w10, UXTW]
        ldr                 x11, [x3, w11, UXTW]
        ldr                 x12, [x3, w12, UXTW]
        ldr                 x13, [x3, w13, UXTW]
        ldr                 x14, [x3, w14, UXTW]
        ldr                 x15, [x3, w15, UXTW]

        sub                 sp, sp, #64
        // push src on stack so it can be loaded into vectors later
        stp                 x8, x9, [sp]
        stp                 x10, x11, [sp, #16]
        stp                 x12, x13, [sp, #32]
        stp                 x14, x15, [sp, #48]

1:
        ld4                 {v0.8h, v1.8h, v2.8h, v3.8h}, [sp]
        ld4                 {v28.8h, v29.8h, v30.8h, v31.8h}, [x4], #64 // filter[0..7]

        // Each of blocks does the following:
        // Extend src and filter to 32 bits with uxtl and sxtl
        // multiply or multiply and accumulate results
        // Extending to 32 bits is necessary, as unit16_t values can't
        // be represented as int16_t without type promotion.
        uxtl                v26.4s, v0.4h
        sxtl                v27.4s, v28.4H
        uxtl2               v0.4s, v0.8h
        mul                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v28.8H
        uxtl                v26.4s, v1.4h
        mul                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v29.4H
        uxtl2               v0.4s, v1.8h
        mla                 v5.4s, v27.4s, v26.4s
        sxtl2               v28.4s, v29.8H
        uxtl                v26.4s, v2.4h
        mla                 v6.4s, v28.4s, v0.4s

        sxtl                v27.4s, v30.4H
        uxtl2               v0.4s, v2.8h
        mla                 v5.4s, v27.4s, v26.4s
        sxtl2               v28.4s, v30.8H
        uxtl                v26.4s, v3.4h
        mla                 v6.4s, v28.4s, v0.4s

        sxtl                v27.4s, v31.4H
        uxtl2               v0.4s, v3.8h
        mla                 v5.4s, v27.4s, v26.4s
        sxtl2               v28.4s, v31.8H
        sub                 w2, w2, #8
        mla                 v6.4s, v28.4s, v0.4s

        sshl                v5.4s, v5.4s, v17.4s
        sshl                v6.4s, v6.4s, v17.4s
        smin                v5.4s, v5.4s, v18.4s
        smin                v6.4s, v6.4s, v18.4s
        xtn                 v5.4h, v5.4s
        xtn2                v5.8h, v6.4s

        st1                 {v5.8h}, [x1], #16
        cmp                 w2, #16

        // load filterPositions into registers for next iteration
        ldp                 w8, w9, [x5]                // filterPos[0], filterPos[1]
        ldp                 w10, w11, [x5, #8]          // filterPos[2], filterPos[3]
        ldp                 w12, w13, [x5, #16]         // filterPos[4], filterPos[5]
        ldp                 w14, w15, [x5, #24]         // filterPos[6], filterPos[7]
        add                 x5, x5, #32

        lsl                 x8, x8, #1
        lsl                 x9, x9, #1
        lsl                 x10, x10, #1
        lsl                 x11, x11, #1
        lsl                 x12, x12, #1
        lsl                 x13, x13, #1
        lsl                 x14, x14, #1
        lsl                 x15, x15, #1

        ldr                 x8,  [x3, w8,  UXTW]
        ldr                 x9,  [x3, w9,  UXTW]
        ldr                 x10, [x3, w10, UXTW]
        ldr                 x11, [x3, w11, UXTW]
        ldr                 x12, [x3, w12, UXTW]
        ldr                 x13, [x3, w13, UXTW]
        ldr                 x14, [x3, w14, UXTW]
        ldr                 x15, [x3, w15, UXTW]

        stp                 x8, x9, [sp]
        stp                 x10, x11, [sp, #16]
        stp                 x12, x13, [sp, #32]
        stp                 x14, x15, [sp, #48]

        b.ge                1b

        // here we make last iteration, without updating the registers
        ld4                 {v0.8h, v1.8h, v2.8h, v3.8h}, [sp]
        ld4                 {v28.8h, v29.8h, v30.8h, v31.8h}, [x4], #64

        uxtl                v26.4s, v0.4h
        sxtl                v27.4s, v28.4H
        uxtl2               v0.4s, v0.8h
        mul                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v28.8H
        uxtl                v26.4s, v1.4h
        mul                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v29.4H
        uxtl2               v0.4s, v1.8h
        mla                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v29.8H
        uxtl                v26.4s, v2.4h
        mla                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v30.4H
        uxtl2               v0.4s, v2.8h
        mla                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v30.8H
        uxtl                v26.4s, v3.4h
        mla                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v31.4H
        uxtl2               v0.4s, v3.8h
        mla                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v31.8H
        subs                w2, w2, #8
        mla                 v6.4s, v0.4s, v28.4s

        sshl                v5.4s, v5.4s, v17.4s
        sshl                v6.4s, v6.4s, v17.4s
        smin                v5.4s, v5.4s, v18.4s
        smin                v6.4s, v6.4s, v18.4s
        xtn                 v5.4h, v5.4S
        xtn2                v5.8h, v6.4s

        st1                 {v5.8h}, [x1], #16
        add                 sp, sp, #64                 // restore stack
        cbnz                w2, 2f

        ret

2:
        ldr                 w8, [x5], #4                // load filterPos
        lsl                 w8, w8, #1
        add                 x9, x3, w8, UXTW            // src + filterPos
        ld1                 {v0.4h}, [x9]               // load 4 * uint16_t
        ld1                 {v31.4h}, [x4], #8

        uxtl                v0.4s, v0.4h
        sxtl                v31.4s, v31.4h
        mul                 v5.4s, v0.4s, v31.4s
        addv                s0, v5.4S
        sshl                v0.4s, v0.4s, v17.4s
        smin                v0.4s, v0.4s, v18.4s
        st1                 {v0.h}[0], [x1], #2
        sub                 w2, w2, #1
        cbnz                w2, 2b                      // if iterations remain jump to beginning

        ret
endfunc

function ff_hscale16to15_X8_neon_asm, export=1
        // w0               int shift
        // x1               int32_t *dst
        // w2               int dstW
        // x3               const uint8_t *src // treat it as uint16_t *src
        // x4               const uint16_t *filter
        // x5               const int32_t *filterPos
        // w6               int filterSize

        movi                v20.4s, #1
        movi                v21.4s, #1
        shl                 v20.4s, v20.4s, #15
        sub                 v20.4s, v20.4s, v21.4s
        dup                 v21.4s, w0
        neg                 v21.4s, v21.4s

        sbfiz               x7, x6, #1, #32             // filterSize*2 (*2 because int16)
1:      ldr                 w8, [x5], #4                // filterPos[idx]
        lsl                 w8, w8, #1
        ldr                 w10, [x5], #4               // filterPos[idx + 1]
        lsl                 w10, w10, #1
        ldr                 w11, [x5], #4               // filterPos[idx + 2]
        lsl                 w11, w11, #1
        ldr                 w9, [x5], #4                // filterPos[idx + 3]
        lsl                 w9, w9, #1
        mov                 x16, x4                     // filter0 = filter
        add                 x12, x16, x7                // filter1 = filter0 + filterSize*2
        add                 x13, x12, x7                // filter2 = filter1 + filterSize*2
        add                 x4, x13, x7                 // filter3 = filter2 + filterSize*2
        movi                v0.2D, #0                   // val sum part 1 (for dst[0])
        movi                v1.2D, #0                   // val sum part 2 (for dst[1])
        movi                v2.2D, #0                   // val sum part 3 (for dst[2])
        movi                v3.2D, #0                   // val sum part 4 (for dst[3])
        add                 x17, x3, w8, UXTW           // srcp + filterPos[0]
        add                 x8,  x3, w10, UXTW          // srcp + filterPos[1]
        add                 x10, x3, w11, UXTW          // srcp + filterPos[2]
        add                 x11, x3, w9, UXTW           // srcp + filterPos[3]
        mov                 w15, w6                     // filterSize counter
2:      ld1                 {v4.8H}, [x17], #16         // srcp[filterPos[0] + {0..7}]
        ld1                 {v5.8H}, [x16], #16         // load 8x16-bit filter values, part 1
        ld1                 {v6.8H}, [x8], #16          // srcp[filterPos[1] + {0..7}]
        ld1                 {v7.8H}, [x12], #16         // load 8x16-bit at filter+filterSize
        uxtl                v24.4s, v4.4H               // extend srcp lower half to 32 bits to preserve sign
        sxtl                v25.4s, v5.4H               // extend filter lower half to 32 bits to match srcp size
        uxtl2               v4.4s, v4.8h                // extend srcp upper half to 32 bits
        mla                 v0.4s, v24.4s, v25.4s       // multiply accumulate lower half of v4 * v5
        sxtl2               v5.4s, v5.8h                // extend filter upper half to 32 bits
        uxtl                v26.4s, v6.4h               // extend srcp lower half to 32 bits
        mla                 v0.4S, v4.4s, v5.4s         // multiply accumulate upper half of v4 * v5
        sxtl                v27.4s, v7.4H               // exted filter lower half
        uxtl2               v6.4s, v6.8H                // extend srcp upper half
        sxtl2               v7.4s, v7.8h                // extend filter upper half
        ld1                 {v16.8H}, [x10], #16        // srcp[filterPos[2] + {0..7}]
        mla                 v1.4S, v26.4s, v27.4s       // v1 accumulates srcp[filterPos[1] + {0..3}] * filter[{0..3}]
        ld1                 {v17.8H}, [x13], #16        // load 8x16-bit at filter+2*filterSize
        uxtl                v22.4s, v16.4H              // extend srcp lower half
        sxtl                v23.4s, v17.4H              // extend filter lower half
        uxtl2               v16.4s, v16.8H              // extend srcp upper half
        sxtl2               v17.4s, v17.8h              // extend filter upper half
        mla                 v2.4S, v22.4s, v23.4s       // v2 accumulates srcp[filterPos[2] + {0..3}] * filter[{0..3}]
        mla                 v2.4S, v16.4s, v17.4s       // v2 accumulates srcp[filterPos[2] + {4..7}] * filter[{4..7}]
        ld1                 {v18.8H}, [x11], #16        // srcp[filterPos[3] + {0..7}]
        mla                 v1.4S, v6.4s, v7.4s         // v1 accumulates srcp[filterPos[1] + {4..7}] * filter[{4..7}]
        ld1                 {v19.8H}, [x4], #16         // load 8x16-bit at filter+3*filterSize
        subs                w15, w15, #8                // j -= 8: processed 8/filterSize
        uxtl                v28.4s, v18.4H              // extend srcp lower half
        sxtl                v29.4s, v19.4H              // extend filter lower half
        uxtl2               v18.4s, v18.8H              // extend srcp upper half
        sxtl2               v19.4s, v19.8h              // extend filter upper half
        mla                 v3.4S, v28.4s, v29.4s       // v3 accumulates srcp[filterPos[3] + {0..3}] * filter[{0..3}]
        mla                 v3.4S, v18.4s, v19.4s       // v3 accumulates srcp[filterPos[3] + {4..7}] * filter[{4..7}]
        b.gt                2b                          // inner loop if filterSize not consumed completely
        addp                v0.4S, v0.4S, v1.4S         // part01 horizontal pair adding
        addp                v2.4S, v2.4S, v3.4S         // part23 horizontal pair adding
        addp                v0.4S, v0.4S, v2.4S         // part0123 horizontal pair adding
        subs                w2, w2, #4                  // dstW -= 4
        sshl                v0.4s, v0.4s, v21.4s        // shift right (effectively rigth, as shift is negative); overflow expected
        smin                v0.4s, v0.4s, v20.4s        // apply min (do not use sqshl)
        xtn                 v0.4h, v0.4s                // narrow down to 16 bits

        st1                 {v0.4H}, [x1], #8           // write to destination part0123
        b.gt                1b                          // loop until end of line
        ret
endfunc

function ff_hscale16to15_X4_neon_asm, export=1
        // w0  int shift
        // x1  int16_t *dst
        // w2  int dstW
        // x3  const uint8_t *src
        // x4  const int16_t *filter
        // x5  const int32_t *filterPos
        // w6  int filterSize

        stp                 d8, d9, [sp, #-0x20]!
        stp                 d10, d11, [sp, #0x10]

        movi                v18.4s, #1
        movi                v17.4s, #1
        shl                 v18.4s, v18.4s, #15
        sub                 v21.4s, v18.4s, v17.4s      // max allowed value
        dup                 v17.4s, w0                  // read shift
        neg                 v20.4s, v17.4s              // negate it, so it can be used in sshl (effectively shift right)

        lsl                 w7, w6, #1
1:
        ldp                 w8, w9, [x5]
        ldp                 w10, w11, [x5, #8]

        movi                v16.2d, #0                  // initialize accumulator for idx + 0
        movi                v17.2d, #0                  // initialize accumulator for idx + 1
        movi                v18.2d, #0                  // initialize accumulator for idx + 2
        movi                v19.2d, #0                  // initialize accumulator for idx + 3

        mov                 x12, x4                     // filter + 0
        add                 x13, x4, x7                 // filter + 1
        add                 x8, x3, x8, lsl #1          // srcp + filterPos 0
        add                 x14, x13, x7                // filter + 2
        add                 x9, x3, x9, lsl #1          // srcp + filterPos 1
        add                 x15, x14, x7                // filter + 3
        add                 x10, x3, x10, lsl #1        // srcp + filterPos 2
        mov                 w0, w6                      // save the filterSize to temporary variable
        add                 x11, x3, x11, lsl #1        // srcp + filterPos 3
        add                 x5, x5, #16                 // advance filter position
        mov                 x16, xzr                    // clear the register x16 used for offsetting the filter values

2:
        ldr                 q4, [x8], #16               // load src values for idx 0
        ldr                 q5, [x9], #16               // load src values for idx 1
        uxtl                v26.4s, v4.4h
        uxtl2               v4.4s, v4.8h
        ldr                 q31, [x12, x16]             // load filter values for idx 0
        ldr                 q6, [x10], #16              // load src values for idx 2
        sxtl                v22.4s, v31.4h
        sxtl2               v31.4s, v31.8h
        mla                 v16.4s, v26.4s, v22.4s      // multiplication of lower half for idx 0
        uxtl                v25.4s, v5.4h
        uxtl2               v5.4s, v5.8h
        ldr                 q30, [x13, x16]             // load filter values for idx 1
        ldr                 q7, [x11], #16              // load src values for idx 3
        mla                 v16.4s, v4.4s, v31.4s       // multiplication of upper half for idx 0
        uxtl                v24.4s, v6.4h
        sxtl                v8.4s, v30.4h
        sxtl2               v30.4s, v30.8h
        mla                 v17.4s, v25.4s, v8.4s       // multiplication of lower half for idx 1
        ldr                 q29, [x14, x16]             // load filter values for idx 2
        uxtl2               v6.4s, v6.8h
        sxtl                v9.4s, v29.4h
        sxtl2               v29.4s, v29.8h
        mla                 v17.4s, v5.4s, v30.4s       // multiplication of upper half for idx 1
        mla                 v18.4s, v24.4s, v9.4s       // multiplication of lower half for idx 2
        ldr                 q28, [x15, x16]             // load filter values for idx 3
        uxtl                v23.4s, v7.4h
        sxtl                v10.4s, v28.4h
        mla                 v18.4s, v6.4s, v29.4s       // multiplication of upper half for idx 2
        uxtl2               v7.4s, v7.8h
        sxtl2               v28.4s, v28.8h
        mla                 v19.4s, v23.4s, v10.4s      // multiplication of lower half for idx 3
        sub                 w0, w0, #8
        cmp                 w0, #8
        mla                 v19.4s, v7.4s, v28.4s       // multiplication of upper half for idx 3

        add                 x16, x16, #16               // advance filter values indexing

        b.ge                2b

        // 4 iterations left

        sub                 x17, x7, #8                 // step back to wrap up the filter pos for last 4 elements

        ldr                 d4, [x8]                    // load src values for idx 0
        ldr                 d31, [x12, x17]             // load filter values for idx 0
        uxtl                v4.4s, v4.4h
        sxtl                v31.4s, v31.4h
        ldr                 d5, [x9]                    // load src values for idx 1
        mla                 v16.4s, v4.4s, v31.4s       // multiplication of upper half for idx 0
        ldr                 d30, [x13, x17]             // load filter values for idx 1
        uxtl                v5.4s, v5.4h
        sxtl                v30.4s, v30.4h
        ldr                 d6, [x10]                   // load src values for idx 2
        mla                 v17.4s, v5.4s, v30.4s       // multiplication of upper half for idx 1
        ldr                 d29, [x14, x17]             // load filter values for idx 2
        uxtl                v6.4s, v6.4h
        sxtl                v29.4s, v29.4h
        ldr                 d7, [x11]                   // load src values for idx 3
        ldr                 d28, [x15, x17]             // load filter values for idx 3
        mla                 v18.4s, v6.4s, v29.4s       // multiplication of upper half for idx 2
        uxtl                v7.4s, v7.4h
        sxtl                v28.4s, v28.4h
        addp                v16.4s, v16.4s, v17.4s
        mla                 v19.4s, v7.4s, v28.4s       // multiplication of upper half for idx 3
        subs                w2, w2, #4
        addp                v18.4s, v18.4s, v19.4s
        addp                v16.4s, v16.4s, v18.4s
        sshl                v16.4s, v16.4s, v20.4s
        smin                v16.4s, v16.4s, v21.4s
        xtn                 v16.4h, v16.4s

        st1                 {v16.4h}, [x1], #8
        add                 x4, x4, x7, lsl #2
        b.gt                1b

        ldp                 d8, d9, [sp]
        ldp                 d10, d11, [sp, #0x10]

        add                 sp, sp, #0x20

        ret
endfunc

function ff_hscale16to19_4_neon_asm, export=1
        // w0               int shift
        // x1               int32_t *dst
        // w2               int dstW
        // x3               const uint8_t *src // treat it as uint16_t *src
        // x4               const uint16_t *filter
        // x5               const int32_t *filterPos
        // w6               int filterSize

        movi                v18.4s, #1
        movi                v17.4s, #1
        shl                 v18.4s, v18.4s, #19
        sub                 v18.4s, v18.4s, v17.4s      // max allowed value
        dup                 v17.4s, w0                  // read shift
        neg                 v17.4s, v17.4s              // negate it, so it can be used in sshl (effectively shift right)

        cmp                 w2, #16
        b.lt                2f // move to last block

        ldp                 w8, w9, [x5]                // filterPos[0], filterPos[1]
        ldp                 w10, w11, [x5, #8]          // filterPos[2], filterPos[3]
        ldp                 w12, w13, [x5, #16]         // filterPos[4], filterPos[5]
        ldp                 w14, w15, [x5, #24]         // filterPos[6], filterPos[7]
        add                 x5, x5, #32

        // shift all filterPos left by one, as uint16_t will be read
        lsl                 x8, x8, #1
        lsl                 x9, x9, #1
        lsl                 x10, x10, #1
        lsl                 x11, x11, #1
        lsl                 x12, x12, #1
        lsl                 x13, x13, #1
        lsl                 x14, x14, #1
        lsl                 x15, x15, #1

        // load src with given offset
        ldr                 x8,  [x3, w8,  UXTW]
        ldr                 x9,  [x3, w9,  UXTW]
        ldr                 x10, [x3, w10, UXTW]
        ldr                 x11, [x3, w11, UXTW]
        ldr                 x12, [x3, w12, UXTW]
        ldr                 x13, [x3, w13, UXTW]
        ldr                 x14, [x3, w14, UXTW]
        ldr                 x15, [x3, w15, UXTW]

        sub                 sp, sp, #64
        // push src on stack so it can be loaded into vectors later
        stp                 x8, x9, [sp]
        stp                 x10, x11, [sp, #16]
        stp                 x12, x13, [sp, #32]
        stp                 x14, x15, [sp, #48]

1:
        ld4                 {v0.8h, v1.8h, v2.8h, v3.8h}, [sp]
        ld4                 {v28.8h, v29.8h, v30.8h, v31.8h}, [x4], #64 // filter[0..7]

        // Each of blocks does the following:
        // Extend src and filter to 32 bits with uxtl and sxtl
        // multiply or multiply and accumulate results
        // Extending to 32 bits is necessary, as unit16_t values can't
        // be represented as int16_t without type promotion.
        uxtl                v26.4s, v0.4h
        sxtl                v27.4s, v28.4H
        uxtl2               v0.4s, v0.8h
        mul                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v28.8H
        uxtl                v26.4s, v1.4h
        mul                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v29.4H
        uxtl2               v0.4s, v1.8h
        mla                 v5.4s, v27.4s, v26.4s
        sxtl2               v28.4s, v29.8H
        uxtl                v26.4s, v2.4h
        mla                 v6.4s, v28.4s, v0.4s

        sxtl                v27.4s, v30.4H
        uxtl2               v0.4s, v2.8h
        mla                 v5.4s, v27.4s, v26.4s
        sxtl2               v28.4s, v30.8H
        uxtl                v26.4s, v3.4h
        mla                 v6.4s, v28.4s, v0.4s

        sxtl                v27.4s, v31.4H
        uxtl2               v0.4s, v3.8h
        mla                 v5.4s, v27.4s, v26.4s
        sxtl2               v28.4s, v31.8H
        sub                 w2, w2, #8
        mla                 v6.4s, v28.4s, v0.4s

        sshl                v5.4s, v5.4s, v17.4s
        sshl                v6.4s, v6.4s, v17.4s
        smin                v5.4s, v5.4s, v18.4s
        smin                v6.4s, v6.4s, v18.4s

        st1                 {v5.4s, v6.4s}, [x1], #32
        cmp                 w2, #16

        // load filterPositions into registers for next iteration
        ldp                 w8, w9, [x5]                // filterPos[0], filterPos[1]
        ldp                 w10, w11, [x5, #8]          // filterPos[2], filterPos[3]
        ldp                 w12, w13, [x5, #16]         // filterPos[4], filterPos[5]
        ldp                 w14, w15, [x5, #24]         // filterPos[6], filterPos[7]
        add                 x5, x5, #32

        lsl                 x8, x8, #1
        lsl                 x9, x9, #1
        lsl                 x10, x10, #1
        lsl                 x11, x11, #1
        lsl                 x12, x12, #1
        lsl                 x13, x13, #1
        lsl                 x14, x14, #1
        lsl                 x15, x15, #1

        ldr                 x8,  [x3, w8,  UXTW]
        ldr                 x9,  [x3, w9,  UXTW]
        ldr                 x10, [x3, w10, UXTW]
        ldr                 x11, [x3, w11, UXTW]
        ldr                 x12, [x3, w12, UXTW]
        ldr                 x13, [x3, w13, UXTW]
        ldr                 x14, [x3, w14, UXTW]
        ldr                 x15, [x3, w15, UXTW]

        stp                 x8, x9, [sp]
        stp                 x10, x11, [sp, #16]
        stp                 x12, x13, [sp, #32]
        stp                 x14, x15, [sp, #48]

        b.ge                1b

        // here we make last iteration, without updating the registers
        ld4                 {v0.8h, v1.8h, v2.8h, v3.8h}, [sp]
        ld4                 {v28.8h, v29.8h, v30.8h, v31.8h}, [x4], #64

        uxtl                v26.4s, v0.4h
        sxtl                v27.4s, v28.4H
        uxtl2               v0.4s, v0.8h
        mul                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v28.8H
        uxtl                v26.4s, v1.4h
        mul                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v29.4H
        uxtl2               v0.4s, v1.8h
        mla                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v29.8H
        uxtl                v26.4s, v2.4h
        mla                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v30.4H
        uxtl2               v0.4s, v2.8h
        mla                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v30.8H
        uxtl                v26.4s, v3.4h
        mla                 v6.4s, v0.4s, v28.4s

        sxtl                v27.4s, v31.4H
        uxtl2               v0.4s, v3.8h
        mla                 v5.4s, v26.4s, v27.4s
        sxtl2               v28.4s, v31.8H
        subs                w2, w2, #8
        mla                 v6.4s, v0.4s, v28.4s

        sshl                v5.4s, v5.4s, v17.4s
        sshl                v6.4s, v6.4s, v17.4s

        smin                v5.4s, v5.4s, v18.4s
        smin                v6.4s, v6.4s, v18.4s

        st1                 {v5.4s, v6.4s}, [x1], #32
        add                 sp, sp, #64                 // restore stack
        cbnz                w2, 2f

        ret

2:
        ldr                 w8, [x5], #4                // load filterPos
        lsl                 w8, w8, #1
        add                 x9, x3, w8, UXTW            // src + filterPos
        ld1                 {v0.4h}, [x9]               // load 4 * uint16_t
        ld1                 {v31.4h}, [x4], #8

        uxtl                v0.4s, v0.4h
        sxtl                v31.4s, v31.4h
        subs                w2, w2, #1
        mul                 v5.4s, v0.4s, v31.4s
        addv                s0, v5.4S
        sshl                v0.4s, v0.4s, v17.4s
        smin                v0.4s, v0.4s, v18.4s
        st1                 {v0.s}[0], [x1], #4
        cbnz                w2, 2b                      // if iterations remain jump to beginning

        ret
endfunc

function ff_hscale16to19_X8_neon_asm, export=1
        // w0               int shift
        // x1               int32_t *dst
        // w2               int dstW
        // x3               const uint8_t *src // treat it as uint16_t *src
        // x4               const uint16_t *filter
        // x5               const int32_t *filterPos
        // w6               int filterSize

        movi                v20.4s, #1
        movi                v21.4s, #1
        shl                 v20.4s, v20.4s, #19
        sub                 v20.4s, v20.4s, v21.4s
        dup                 v21.4s, w0
        neg                 v21.4s, v21.4s

        sbfiz               x7, x6, #1, #32             // filterSize*2 (*2 because int16)
1:      ldr                 w8, [x5], #4                // filterPos[idx]
        ldr                 w10, [x5], #4               // filterPos[idx + 1]
        lsl                 w8, w8, #1
        ldr                 w11, [x5], #4               // filterPos[idx + 2]
        ldr                 w9, [x5], #4                // filterPos[idx + 3]
        mov                 x16, x4                     // filter0 = filter
        lsl                 w11, w11, #1
        add                 x12, x16, x7                // filter1 = filter0 + filterSize*2
        lsl                 w9, w9, #1
        add                 x13, x12, x7                // filter2 = filter1 + filterSize*2
        lsl                 w10, w10, #1
        add                 x4, x13, x7                 // filter3 = filter2 + filterSize*2
        movi                v0.2D, #0                   // val sum part 1 (for dst[0])
        movi                v1.2D, #0                   // val sum part 2 (for dst[1])
        movi                v2.2D, #0                   // val sum part 3 (for dst[2])
        movi                v3.2D, #0                   // val sum part 4 (for dst[3])
        add                 x17, x3, w8, UXTW           // srcp + filterPos[0]
        add                 x8,  x3, w10, UXTW          // srcp + filterPos[1]
        add                 x10, x3, w11, UXTW          // srcp + filterPos[2]
        add                 x11, x3, w9, UXTW           // srcp + filterPos[3]
        mov                 w15, w6                     // filterSize counter
2:      ld1                 {v4.8H}, [x17], #16         // srcp[filterPos[0] + {0..7}]
        ld1                 {v5.8H}, [x16], #16         // load 8x16-bit filter values, part 1
        ld1                 {v6.8H}, [x8], #16          // srcp[filterPos[1] + {0..7}]
        ld1                 {v7.8H}, [x12], #16         // load 8x16-bit at filter+filterSize
        uxtl                v24.4s, v4.4H               // extend srcp lower half to 32 bits to preserve sign
        sxtl                v25.4s, v5.4H               // extend filter lower half to 32 bits to match srcp size
        uxtl2               v4.4s, v4.8h                // extend srcp upper half to 32 bits
        mla                 v0.4s, v24.4s, v25.4s       // multiply accumulate lower half of v4 * v5
        sxtl2               v5.4s, v5.8h                // extend filter upper half to 32 bits
        uxtl                v26.4s, v6.4h               // extend srcp lower half to 32 bits
        mla                 v0.4S, v4.4s, v5.4s         // multiply accumulate upper half of v4 * v5
        sxtl                v27.4s, v7.4H               // exted filter lower half
        uxtl2               v6.4s, v6.8H                // extend srcp upper half
        sxtl2               v7.4s, v7.8h                // extend filter upper half
        ld1                 {v16.8H}, [x10], #16        // srcp[filterPos[2] + {0..7}]
        mla                 v1.4S, v26.4s, v27.4s       // v1 accumulates srcp[filterPos[1] + {0..3}] * filter[{0..3}]
        ld1                 {v17.8H}, [x13], #16        // load 8x16-bit at filter+2*filterSize
        uxtl                v22.4s, v16.4H              // extend srcp lower half
        sxtl                v23.4s, v17.4H              // extend filter lower half
        uxtl2               v16.4s, v16.8H              // extend srcp upper half
        sxtl2               v17.4s, v17.8h              // extend filter upper half
        mla                 v2.4S, v22.4s, v23.4s       // v2 accumulates srcp[filterPos[2] + {0..3}] * filter[{0..3}]
        mla                 v2.4S, v16.4s, v17.4s       // v2 accumulates srcp[filterPos[2] + {4..7}] * filter[{4..7}]
        ld1                 {v18.8H}, [x11], #16        // srcp[filterPos[3] + {0..7}]
        mla                 v1.4S, v6.4s, v7.4s         // v1 accumulates srcp[filterPos[1] + {4..7}] * filter[{4..7}]
        ld1                 {v19.8H}, [x4], #16         // load 8x16-bit at filter+3*filterSize
        subs                w15, w15, #8                // j -= 8: processed 8/filterSize
        uxtl                v28.4s, v18.4H              // extend srcp lower half
        sxtl                v29.4s, v19.4H              // extend filter lower half
        uxtl2               v18.4s, v18.8H              // extend srcp upper half
        sxtl2               v19.4s, v19.8h              // extend filter upper half
        mla                 v3.4S, v28.4s, v29.4s       // v3 accumulates srcp[filterPos[3] + {0..3}] * filter[{0..3}]
        mla                 v3.4S, v18.4s, v19.4s       // v3 accumulates srcp[filterPos[3] + {4..7}] * filter[{4..7}]
        b.gt                2b                          // inner loop if filterSize not consumed completely
        addp                v0.4S, v0.4S, v1.4S         // part01 horizontal pair adding
        addp                v2.4S, v2.4S, v3.4S         // part23 horizontal pair adding
        addp                v0.4S, v0.4S, v2.4S         // part0123 horizontal pair adding
        subs                w2, w2, #4                  // dstW -= 4
        sshl                v0.4s, v0.4s, v21.4s        // shift right (effectively rigth, as shift is negative); overflow expected
        smin                v0.4s, v0.4s, v20.4s        // apply min (do not use sqshl)
        st1                 {v0.4s}, [x1], #16          // write to destination part0123
        b.gt                1b                          // loop until end of line
        ret
endfunc

function ff_hscale16to19_X4_neon_asm, export=1
        // w0  int shift
        // x1  int16_t *dst
        // w2  int dstW
        // x3  const uint8_t *src
        // x4  const int16_t *filter
        // x5  const int32_t *filterPos
        // w6  int filterSize

        stp                 d8, d9, [sp, #-0x20]!
        stp                 d10, d11, [sp, #0x10]

        movi                v18.4s, #1
        movi                v17.4s, #1
        shl                 v18.4s, v18.4s, #19
        sub                 v21.4s, v18.4s, v17.4s      // max allowed value
        dup                 v17.4s, w0                  // read shift
        neg                 v20.4s, v17.4s              // negate it, so it can be used in sshl (effectively shift right)

        lsl                 w7, w6, #1
1:
        ldp                 w8, w9, [x5]
        ldp                 w10, w11, [x5, #8]

        movi                v16.2d, #0                  // initialize accumulator for idx + 0
        movi                v17.2d, #0                  // initialize accumulator for idx + 1
        movi                v18.2d, #0                  // initialize accumulator for idx + 2
        movi                v19.2d, #0                  // initialize accumulator for idx + 3

        mov                 x12, x4                     // filter + 0
        add                 x13, x4, x7                 // filter + 1
        add                 x8, x3, x8, lsl #1          // srcp + filterPos 0
        add                 x14, x13, x7                // filter + 2
        add                 x9, x3, x9, lsl #1          // srcp + filterPos 1
        add                 x15, x14, x7                // filter + 3
        add                 x10, x3, x10, lsl #1        // srcp + filterPos 2
        mov                 w0, w6                      // save the filterSize to temporary variable
        add                 x11, x3, x11, lsl #1        // srcp + filterPos 3
        add                 x5, x5, #16                 // advance filter position
        mov                 x16, xzr                    // clear the register x16 used for offsetting the filter values

2:
        ldr                 q4, [x8], #16               // load src values for idx 0
        ldr                 q5, [x9], #16               // load src values for idx 1
        uxtl                v26.4s, v4.4h
        uxtl2               v4.4s, v4.8h
        ldr                 q31, [x12, x16]             // load filter values for idx 0
        ldr                 q6, [x10], #16              // load src values for idx 2
        sxtl                v22.4s, v31.4h
        sxtl2               v31.4s, v31.8h
        mla                 v16.4s, v26.4s, v22.4s      // multiplication of lower half for idx 0
        uxtl                v25.4s, v5.4h
        uxtl2               v5.4s, v5.8h
        ldr                 q30, [x13, x16]             // load filter values for idx 1
        ldr                 q7, [x11], #16              // load src values for idx 3
        mla                 v16.4s, v4.4s, v31.4s       // multiplication of upper half for idx 0
        uxtl                v24.4s, v6.4h
        sxtl                v8.4s, v30.4h
        sxtl2               v30.4s, v30.8h
        mla                 v17.4s, v25.4s, v8.4s       // multiplication of lower half for idx 1
        ldr                 q29, [x14, x16]             // load filter values for idx 2
        uxtl2               v6.4s, v6.8h
        sxtl                v9.4s, v29.4h
        sxtl2               v29.4s, v29.8h
        mla                 v17.4s, v5.4s, v30.4s       // multiplication of upper half for idx 1
        ldr                 q28, [x15, x16]             // load filter values for idx 3
        mla                 v18.4s, v24.4s, v9.4s       // multiplication of lower half for idx 2
        uxtl                v23.4s, v7.4h
        sxtl                v10.4s, v28.4h
        mla                 v18.4s, v6.4s, v29.4s       // multiplication of upper half for idx 2
        uxtl2               v7.4s, v7.8h
        sxtl2               v28.4s, v28.8h
        mla                 v19.4s, v23.4s, v10.4s      // multiplication of lower half for idx 3
        sub                 w0, w0, #8
        cmp                 w0, #8
        mla                 v19.4s, v7.4s, v28.4s       // multiplication of upper half for idx 3

        add                 x16, x16, #16               // advance filter values indexing

        b.ge                2b

        // 4 iterations left

        sub                 x17, x7, #8                 // step back to wrap up the filter pos for last 4 elements

        ldr                 d4, [x8]                    // load src values for idx 0
        ldr                 d31, [x12, x17]             // load filter values for idx 0
        uxtl                v4.4s, v4.4h
        sxtl                v31.4s, v31.4h
        ldr                 d5, [x9]                    // load src values for idx 1
        mla                 v16.4s, v4.4s, v31.4s       // multiplication of upper half for idx 0
        ldr                 d30, [x13, x17]             // load filter values for idx 1
        uxtl                v5.4s, v5.4h
        sxtl                v30.4s, v30.4h
        ldr                 d6, [x10]                   // load src values for idx 2
        mla                 v17.4s, v5.4s, v30.4s       // multiplication of upper half for idx 1
        ldr                 d29, [x14, x17]             // load filter values for idx 2
        uxtl                v6.4s, v6.4h
        sxtl                v29.4s, v29.4h
        ldr                 d7, [x11]                   // load src values for idx 3
        ldr                 d28, [x15, x17]             // load filter values for idx 3
        mla                 v18.4s, v6.4s, v29.4s       // multiplication of upper half for idx 2
        uxtl                v7.4s, v7.4h
        sxtl                v28.4s, v28.4h
        addp                v16.4s, v16.4s, v17.4s
        mla                 v19.4s, v7.4s, v28.4s       // multiplication of upper half for idx 3
        subs                w2, w2, #4
        addp                v18.4s, v18.4s, v19.4s
        addp                v16.4s, v16.4s, v18.4s
        sshl                v16.4s, v16.4s, v20.4s
        smin                v16.4s, v16.4s, v21.4s

        st1                 {v16.4s}, [x1], #16
        add                 x4, x4, x7, lsl #2
        b.gt                1b

        ldp                 d8, d9, [sp]
        ldp                 d10, d11, [sp, #0x10]

        add                 sp, sp, #0x20

        ret
endfunc
