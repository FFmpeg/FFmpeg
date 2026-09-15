/* -*-arm64-*-
 * vim: syntax=arm64asm
 *
 * AArch64 NEON optimised SAO functions for h26x decoding
 *
 * Copyright (c) 2022 J. Dekker <jdek@itanimul.li>
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

#define HEVC_MAX_PB_SIZE 64
#define VVC_MAX_PB_SIZE 128
#define AV_INPUT_BUFFER_PADDING_SIZE 64
#define HEVC_SAO_STRIDE (2 * HEVC_MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE)
#define VVC_SAO_STRIDE (2 * VVC_MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE)

// void sao_band_filter(uint8_t *_dst, uint8_t *_src,
//                      ptrdiff_t stride_dst, ptrdiff_t stride_src,
//                      int16_t *sao_offset_val, int sao_left_class,
//                      int width, int height)
function ff_h26x_sao_band_filter_8x8_8_neon, export=1
        stp             xzr, xzr, [sp, #-32]!
        stp             xzr, xzr, [sp, #16]
        mov             w8,  #4
0:
        ldrsh           x9, [x4, x8, lsl #1]        // sao_offset_val[k+1]
        subs            w8, w8, #1
        add             w10, w8, w5                 // k + sao_left_class
        and             w10, w10, #0x1F
        strb            w9, [sp, x10]
        bne             0b
        ldp             q16, q17, [sp], #32
1:
        ld1             {v2.8b}, [x1], x3
        subs            w7, w7, #1
        uxtl            v0.8h, v2.8b
        ushr            v3.8b, v2.8b, #3            // >> BIT_DEPTH - 3
        tbl             v3.8b, {v16.16b-v17.16b}, v3.8b
        sxtl            v2.8h, v3.8b
        add             v0.8h, v0.8h, v2.8h         // src[x] + table
        sqxtun          v0.8b, v0.8h                // clip + narrow
        st1             {v0.8b}, [x0], x2
        bne             1b
        ret
endfunc

function ff_h26x_sao_band_filter_16x16_8_neon, export=1
        stp             xzr, xzr, [sp, #-32]!
        stp             xzr, xzr, [sp, #16]
        mov             w8,  #4
0:
        ldrsh           x9, [x4, x8, lsl #1]        // sao_offset_val[k+1]
        subs            w8,  w8, #1
        add             w10, w8, w5                 // k + sao_left_class
        and             w10, w10, #0x1F
        strb            w9, [sp, x10]
        bne             0b
        add             w6, w6, #15
        bic             w6, w6, #15
        ldp             q16, q17, [sp], #32
        sub             x2, x2, x6
        sub             x3, x3, x6
1:
        mov             w8, w6                      // beginning of line
2:
        ldr             q2, [x1], #16
        subs            w8, w8, #16
        uxtl            v0.8h, v2.8b
        uxtl2           v1.8h, v2.16b
        ushr            v3.16b, v2.16b, #3          // >> BIT_DEPTH - 3
        tbl             v3.16b, {v16.16b-v17.16b}, v3.16b
        sxtl            v2.8h, v3.8b
        sxtl2           v3.8h, v3.16b
        add             v0.8h, v0.8h, v2.8h         // src[x] + table
        add             v1.8h, v1.8h, v3.8h
        sqxtun          v0.8b, v0.8h                // clip + narrow
        sqxtun2         v0.16b, v1.8h
        str             q0, [x0], #16
        bne             2b
        subs            w7, w7, #1
        add             x0, x0, x2                  // dst += stride_dst
        add             x1, x1, x3                  // src += stride_src
        bne             1b
        ret
endfunc

.Lhevc_sao_edge_pos_8:
.word 1 // horizontal
.word HEVC_SAO_STRIDE // vertical
.word HEVC_SAO_STRIDE + 1 // 135 degree
.word HEVC_SAO_STRIDE - 1 // 45 degree

.Lhevc_sao_edge_pos_16bit:
.word 2 // horizontal
.word HEVC_SAO_STRIDE // vertical
.word HEVC_SAO_STRIDE + 2 // 135 degree
.word HEVC_SAO_STRIDE - 2 // 45 degree

.Lvvc_sao_edge_pos:
.word 1 // horizontal
.word VVC_SAO_STRIDE // vertical
.word VVC_SAO_STRIDE + 1 // 135 degree
.word VVC_SAO_STRIDE - 1 // 45 degree

.Lvvc_sao_edge_pos_16bit:
.word 2 // horizontal
.word VVC_SAO_STRIDE // vertical
.word VVC_SAO_STRIDE + 2 // 135 degree
.word VVC_SAO_STRIDE - 2 // 45 degree

function ff_vvc_sao_edge_filter_16x16_8_neon, export=1
        adr             x7, .Lvvc_sao_edge_pos
        mov             x15, #VVC_SAO_STRIDE
        b               1f
endfunc

// ff_hevc_sao_edge_filter_16x16_8_neon(char *dst, char *src, ptrdiff stride_dst,
//                                      int16 *sao_offset_val, int eo, int width, int height)
function ff_hevc_sao_edge_filter_16x16_8_neon, export=1
        adr             x7, .Lhevc_sao_edge_pos_8
        mov             x15, #HEVC_SAO_STRIDE
1:
        ld1             {v3.8h}, [x3]              // load sao_offset_val
        add             w5,  w5,  #0xF
        bic             w5,  w5,  #0xF
        ldr             w4, [x7, w4, uxtw #2]      // stride_src
        mov             v3.h[7], v3.h[0]           // reorder to [1,2,0,3,4]
        mov             v3.h[0], v3.h[1]
        mov             v3.h[1], v3.h[2]
        mov             v3.h[2], v3.h[7]
        // split 16bit values into two tables
        uzp2            v1.16b, v3.16b, v3.16b     // sao_offset_val -> upper
        uzp1            v0.16b, v3.16b, v3.16b     // sao_offset_val -> lower
        movi            v2.16b, #2
        // strides between end of line and next src/dst
        sub             x15, x15, x5               // stride_src - width
        sub             x16, x2, x5                // stride_dst - width
        mov             x11, x1                    // copy base src
1:      // new line
        mov             x14, x5                    // copy width
        sub             x12, x11, x4               // src_a (prev) = src - sao_edge_pos
        add             x13, x11, x4               // src_b (next) = src + sao_edge_pos
2:      // process 16 bytes
        ld1             {v3.16b}, [x11], #16       // load src
        ld1             {v4.16b}, [x12], #16       // load src_a (prev)
        ld1             {v5.16b}, [x13], #16       // load src_b (next)
        subs            x14, x14, #16
        cmhi            v16.16b, v4.16b, v3.16b    // (prev > cur)
        cmhi            v17.16b, v3.16b, v4.16b    // (cur > prev)
        cmhi            v18.16b, v5.16b, v3.16b    // (next > cur)
        cmhi            v19.16b, v3.16b, v5.16b    // (cur > next)
        sub             v20.16b, v16.16b, v17.16b  // diff0 = CMP(cur, prev) = (cur > prev) - (cur < prev)
        sub             v21.16b, v18.16b, v19.16b  // diff1 = CMP(cur, next) = (cur > next) - (cur < next)
        add             v20.16b, v20.16b, v21.16b  // diff = diff0 + diff1
        add             v20.16b, v20.16b, v2.16b   // offset_val = diff + 2
        tbl             v16.16b, {v0.16b}, v20.16b
        tbl             v17.16b, {v1.16b}, v20.16b
        uxtl            v20.8h, v3.8b              // src[0:7]
        uxtl2           v21.8h, v3.16b             // src[7:15]
        zip1            v18.16b, v16.16b, v17.16b  // sao_offset_val lower ->
        zip2            v19.16b, v16.16b, v17.16b  // sao_offset_val upper ->
        sqadd           v20.8h, v18.8h, v20.8h     // + sao_offset_val
        sqadd           v21.8h, v19.8h, v21.8h
        sqxtun          v3.8b, v20.8h
        sqxtun2         v3.16b, v21.8h
        st1             {v3.16b}, [x0], #16
        // filtered 16 bytes
        b.ne            2b                         // do we have width to filter?
        // no width to filter, setup next line
        subs            w6, w6, #1                 // filtered line
        add             x11, x11, x15              // stride src to next line
        add             x0, x0, x16                // stride dst to next line
        b.ne            1b                         // do we have lines to process?
        // no lines to filter
        ret
endfunc

/* Permutes the low byte of sao_offset_val into the byte table v28 by
 * edge_idx = { 1, 2, 0, 3, 4 }.
 * v29: bias 2
 * v30: 0 for clip lower bound
 * v31: clip higher bound, not set by this macro
 */
.macro  sao_edge_offsets_init
        ld1             {v6.8h}, [x3]
        mov             x11, #0x0402
        movi            v29.8b, #2
        movk            x11, #0x0600, lsl #16
        movi            v30.8h, #0
        movk            x11, #0x0008, lsl #32      // 0x0000000806000402
        fmov            d7, x11
        tbl             v28.8b, {v6.16b}, v7.8b
.endm

/* Filters one 8-pixel chunk
 * v0: cur, and hold clipped result
 * v1: a
 * v2: b
 */
.macro  sao_edge_filter8
        cmhi            v16.8h, v1.8h, v0.8h
        cmhi            v17.8h, v0.8h, v1.8h
        cmhi            v18.8h, v2.8h, v0.8h
        cmhi            v19.8h, v0.8h, v2.8h
        add             v20.8h, v16.8h, v18.8h
        add             v21.8h, v17.8h, v19.8h
        sub             v20.8h, v20.8h, v21.8h      // CMP(cur, a) + CMP(cur, b)
        xtn             v20.8b, v20.8h
        add             v20.8b, v20.8b, v29.8b      // offset table index
        tbl             v16.8b, {v28.16b}, v20.8b
        saddw           v0.8h, v0.8h, v16.8b
        smax            v0.8h, v0.8h, v30.8h
        smin            v0.8h, v0.8h, v31.8h
.endm

// ff_vvc_sao_edge_filter_16x16_12_neon(char *dst, char *src, ptrdiff stride_dst,
//                                      int16 *sao_offset_val, int eo, int width, int height)
function ff_vvc_sao_edge_filter_16x16_12_neon, export=1
        mvni            v31.8h, #0xf0, lsl #8       // 4095
        b               .Lvvc_sao_edge_16x16_16bit
endfunc

// ff_vvc_sao_edge_filter_16x16_10_neon(char *dst, char *src, ptrdiff stride_dst,
//                                      int16 *sao_offset_val, int eo, int width, int height)
function ff_vvc_sao_edge_filter_16x16_10_neon, export=1
        mvni            v31.8h, #0xfc, lsl #8       // 1023
.Lvvc_sao_edge_16x16_16bit:
        adr             x7, .Lvvc_sao_edge_pos_16bit
        mov             x15, #VVC_SAO_STRIDE
        b               .Lh26x_sao_edge_16x16_16bit
endfunc

// ff_hevc_sao_edge_filter_16x16_12_neon(char *dst, char *src, ptrdiff stride_dst,
//                                       int16 *sao_offset_val, int eo, int width, int height)
function ff_hevc_sao_edge_filter_16x16_12_neon, export=1
        mvni            v31.8h, #0xf0, lsl #8       // 4095
        b               .Lhevc_sao_edge_16bit
endfunc

// ff_hevc_sao_edge_filter_16x16_10_neon(char *dst, char *src, ptrdiff stride_dst,
//                                       int16 *sao_offset_val, int eo, int width, int height)
function ff_hevc_sao_edge_filter_16x16_10_neon, export=1
        mvni            v31.8h, #0xfc, lsl #8       // 1023
.Lhevc_sao_edge_16bit:
        adr             x7, .Lhevc_sao_edge_pos_16bit
        mov             x15, #HEVC_SAO_STRIDE
.Lh26x_sao_edge_16x16_16bit:
        add             w5, w5, #7
        ldr             w4, [x7, w4, uxtw #2]       // a/b offsets in bytes
        bic             w5, w5, #7
        sao_edge_offsets_init
        lsl             w5, w5, #1                  // width in bytes, multiple of 16
        sub             x15, x15, x5                // src step to the next line
        sub             x16, x2, x5                 // dst step to the next line
1:
        lsr             x14, x5, #4                 // 8-pixel groups
        sub             x12, x1, x4
        add             x13, x1, x4
2:
        ldr             q0, [x1], #16
        ldr             q1, [x12], #16
        ldr             q2, [x13], #16
        subs            x14, x14, #1
        sao_edge_filter8
        str             q0, [x0], #16
        b.ne            2b
        subs            w6, w6, #1
        add             x1, x1, x15
        add             x0, x0, x16
        b.ne            1b
        ret
endfunc

function ff_vvc_sao_edge_filter_8x8_8_neon, export=1
        adr             x7, .Lvvc_sao_edge_pos
        mov             x15, #VVC_SAO_STRIDE
        b               1f
endfunc

// ff_hevc_sao_edge_filter_8x8_8_neon(char *dst, char *src, ptrdiff stride_dst,
//                                    int16 *sao_offset_val, int eo, int width, int height)
function ff_hevc_sao_edge_filter_8x8_8_neon, export=1
        adr             x7, .Lhevc_sao_edge_pos_8
        mov             x15, #HEVC_SAO_STRIDE
1:
        ldr             w4, [x7, w4, uxtw #2]
        ld1             {v3.8h}, [x3]
        mov             v3.h[7], v3.h[0]
        mov             v3.h[0], v3.h[1]
        mov             v3.h[1], v3.h[2]
        mov             v3.h[2], v3.h[7]
        uzp2            v1.16b, v3.16b, v3.16b
        uzp1            v0.16b, v3.16b, v3.16b
        movi            v2.16b, #2
        add             x16, x0, x2
        lsl             x2,  x2, #1
        mov             x8,  x1
        sub             x9,  x1, x4
        add             x10, x1, x4
1:      ld1             {v3.d}[0], [ x8], x15
        ld1             {v4.d}[0], [ x9], x15
        ld1             {v5.d}[0], [x10], x15
        ld1             {v3.d}[1], [ x8], x15
        ld1             {v4.d}[1], [ x9], x15
        ld1             {v5.d}[1], [x10], x15
        subs            w6, w6, #2
        cmhi            v16.16b, v4.16b, v3.16b
        cmhi            v17.16b, v3.16b, v4.16b
        cmhi            v18.16b, v5.16b, v3.16b
        cmhi            v19.16b, v3.16b, v5.16b
        sub             v20.16b, v16.16b, v17.16b
        sub             v21.16b, v18.16b, v19.16b
        add             v20.16b, v20.16b, v21.16b
        add             v20.16b, v20.16b, v2.16b
        tbl             v16.16b, {v0.16b}, v20.16b
        tbl             v17.16b, {v1.16b}, v20.16b
        uxtl            v20.8h, v3.8b
        uxtl2           v21.8h, v3.16b
        zip1            v18.16b, v16.16b, v17.16b
        zip2            v19.16b, v16.16b, v17.16b
        sqadd           v20.8h, v18.8h, v20.8h
        sqadd           v21.8h, v19.8h, v21.8h
        sqxtun          v6.8b, v20.8h
        sqxtun          v7.8b, v21.8h
        st1             {v6.8b}, [ x0], x2
        st1             {v7.8b}, [x16], x2
        b.ne            1b
        ret
endfunc

// ff_vvc_sao_edge_filter_8x8_12_neon(char *dst, char *src, ptrdiff stride_dst,
//                                    int16 *sao_offset_val, int eo, int width, int height)
function ff_vvc_sao_edge_filter_8x8_12_neon, export=1
        mvni            v31.8h, #0xf0, lsl #8       // 4095
        b               .Lvvc_sao_edge_8x8_16bit
endfunc

// ff_vvc_sao_edge_filter_8x8_10_neon(char *dst, char *src, ptrdiff stride_dst,
//                                    int16 *sao_offset_val, int eo, int width, int height)
function ff_vvc_sao_edge_filter_8x8_10_neon, export=1
        mvni            v31.8h, #0xfc, lsl #8       // 1023
.Lvvc_sao_edge_8x8_16bit:
        adr             x7, .Lvvc_sao_edge_pos_16bit
        mov             x15, #VVC_SAO_STRIDE
        b               .Lh26x_sao_edge_8x8_16bit
endfunc

// ff_hevc_sao_edge_filter_8x8_12_neon(char *dst, char *src, ptrdiff stride_dst,
//                                     int16 *sao_offset_val, int eo, int width, int height)
function ff_hevc_sao_edge_filter_8x8_12_neon, export=1
        mvni            v31.8h, #0xf0, lsl #8     // 4095
        b               .Lhevc_sao_edge_8x8_16bit
endfunc

// ff_hevc_sao_edge_filter_8x8_10_neon(char *dst, char *src, ptrdiff stride_dst,
//                                     int16 *sao_offset_val, int eo, int width, int height)
function ff_hevc_sao_edge_filter_8x8_10_neon, export=1
        mvni            v31.8h, #0xfc, lsl #8     // 1023
.Lhevc_sao_edge_8x8_16bit:
        adr             x7, .Lhevc_sao_edge_pos_16bit
        mov             x15, #HEVC_SAO_STRIDE
.Lh26x_sao_edge_8x8_16bit:
        ldr             w4, [x7, w4, uxtw #2]      // a/b offsets in bytes
        sao_edge_offsets_init
        sub             x9,  x1, x4                // a neighbours
        add             x10, x1, x4                // b neighbours
1:
        ld1             {v0.8h}, [x1], x15
        ld1             {v1.8h}, [x9], x15
        ld1             {v2.8h}, [x10], x15
        subs            w6, w6, #1
        sao_edge_filter8
        st1             {v0.8h}, [x0], x2
        b.ne            1b
        ret
endfunc
