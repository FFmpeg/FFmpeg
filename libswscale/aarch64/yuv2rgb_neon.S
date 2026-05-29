/*
 * Copyright (c) 2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
 * Copyright (c) 2016 Clément Bœsch <clement stupeflix.com>
 * Copyright (c) 2026 Ramiro Polla
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

// Calling convention for ff_<ifmt>_to_<ofmt>_neon:
//   w0  int  w           (width, multiple of 16)
//   w1  int  h
//   w2  int  y_offset
//   w3  int  y_coeff
//   x4  const int16_t        *yuv2rgb_table
//   x5  const uint8_t        *const src[]      (Y, U/C, V, A as needed)
//   x6  const int            *const srcStride[]
//   x7  uint8_t              *dst0
//   [sp +  0] int             linesize0
//   [sp +  8] uint8_t        *dst1             (planar only)
//   [sp + 16] int             linesize1        (planar only)
//   [sp + 24] uint8_t        *dst2             (planar only)
//   [sp + 32] int             linesize2        (planar only)
// Passing src/srcStride as arrays keeps every scalar arg in a register and
// leaves only pointer-followed-by-int on the stack, so Apple's natural
// packing and AAPCS64's 8-byte slotting coincide and no per-ABI offset
// branching is needed.

#define width         w0
#define widthx        x0
#define height        w1
#define y_offset      w2
#define y_coeff       w3
#define table_ptr     x4

// Source plane pointers (loaded from src[] in the prologue; the slots are
// reused for srcY/srcC/srcV/srcA once y_offset/y_coeff/table_ptr are
// consumed by dup/ld1).
#define srcY          x2
#define srcC          x3
#define srcU          x3
#define srcV          x4
#define srcA          x5

// Source plane padding (sign-extended in the prologue so the row-end
// increment is a single 64-bit add).
#define srcPaddingY   x10
#define srcPaddingC   x11
#define srcPaddingU   x11
#define srcPaddingV   x12
#define srcPaddingA   x6
#define srcPaddingYw  w10
#define srcPaddingCw  w11
#define srcPaddingUw  w11
#define srcPaddingVw  w12
#define srcPaddingAw  w6

// Destination plane pointers. dst0/dst1/dst2 share x5/x6 with srcA/
// srcPaddingA, but those aliases never coexist in the same function (yuva
// is packed-only; gbrp is yuv420p/yuv422p/nv12/nv21).
#define dst0          x7
#define dst1          x6
#define dst2          x5

#define dstPadding0   x13
#define dstPadding1   x14
#define dstPadding2   x15
#define dstPadding0w  w13
#define dstPadding1w  w14
#define dstPadding2w  w15

// Loop state.
#define cur_width     w8
#define orig_height   w9
#define chroma_rewind x16
#define tmp           w17
#define tmpx          x17

// Second-row scratch for the 2-lines-at-a-time paths. chroma_rewind and
// tmp are unused there (the chroma row is consumed by both output rows
// in the same iteration, so the rewind csel/add is gone), so x16/x17
// double as the line-2 luma and dst pointers.
#define l2_srcY       x16
#define l2_dst0       x17
// Planar 2-line variant needs three line-2 dst pointers. x16/x17 are
// already taken by l2_srcY/l2_dst0, so l2_dst1/l2_dst2 land in the
// AAPCS callee-saved range and the 2-line gbrp prologue spills them.
#define l2_dst1       x19
#define l2_dst2       x20
// yuva420p 2-line carries a per-row alpha pointer (alpha is full
// resolution -- each output row reads its own 16 bytes). x14 is free
// for the yuva packed variants (no planar gbrp dst there).
#define l2_srcA       x14

// --------------------------------------------------------------------
// Source-side argument unpacking.

.macro src_load_args_nv12
        ldp             srcPaddingYw, srcPaddingCw, [x6]                // srcStride[0], srcStride[1]
        ldp             srcY,         srcC,         [x5]                // src[0], src[1]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingC,  srcPaddingCw
        sub             srcPaddingY,  srcPaddingY,  widthx              // = srcStride[0] - width
        sub             srcPaddingC,  srcPaddingC,  widthx              // = srcStride[1] - width   (UV interleaved)
        neg             chroma_rewind, widthx                           // chroma_rewind = -width
.endm

.macro src_load_args_nv21
        src_load_args_nv12
.endm

.macro src_load_args_yuv420p
        ldp             srcPaddingYw, srcPaddingUw, [x6]                // srcStride[0], srcStride[1]
        ldr             srcPaddingVw, [x6, #8]                          // srcStride[2]
        ldp             srcY,         srcU,         [x5]                // src[0], src[1]
        ldr             srcV,         [x5, #16]                         // src[2]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingU,  srcPaddingUw
        sxtw            srcPaddingV,  srcPaddingVw
        sub             srcPaddingY,  srcPaddingY,  widthx              // = srcStride[0] - width
        sub             srcPaddingU,  srcPaddingU,  widthx, lsr #1      // = srcStride[1] - width/2
        sub             srcPaddingV,  srcPaddingV,  widthx, lsr #1      // = srcStride[2] - width/2
        neg             chroma_rewind, widthx
        asr             chroma_rewind, chroma_rewind, #1                // chroma_rewind = -width/2
.endm

.macro src_load_args_yuv422p
        ldp             srcPaddingYw, srcPaddingUw, [x6]
        ldr             srcPaddingVw, [x6, #8]
        ldp             srcY,         srcU,         [x5]
        ldr             srcV,         [x5, #16]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingU,  srcPaddingUw
        sxtw            srcPaddingV,  srcPaddingVw
        sub             srcPaddingY,  srcPaddingY,  widthx
        sub             srcPaddingU,  srcPaddingU,  widthx, lsr #1
        sub             srcPaddingV,  srcPaddingV,  widthx, lsr #1
.endm

.macro src_load_args_yuva420p
        ldp             srcPaddingYw, srcPaddingUw, [x6]
        ldr             srcPaddingVw, [x6, #8]                          // srcStride[2]
        ldr             srcPaddingAw, [x6, #12]                         // srcStride[3]
        ldp             srcY,         srcU,         [x5]
        ldr             srcV,         [x5, #16]
        ldr             srcA,         [x5, #24]                         // src[3]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingU,  srcPaddingUw
        sxtw            srcPaddingV,  srcPaddingVw
        sxtw            srcPaddingA,  srcPaddingAw
        sub             srcPaddingY,  srcPaddingY,  widthx
        sub             srcPaddingU,  srcPaddingU,  widthx, lsr #1
        sub             srcPaddingV,  srcPaddingV,  widthx, lsr #1
        sub             srcPaddingA,  srcPaddingA,  widthx              // alpha is full resolution
        neg             chroma_rewind, widthx
        asr             chroma_rewind, chroma_rewind, #1
.endm

// 2-lines-at-a-time variants: compute l2_srcY = srcY + srcStride[0]
// up front and pre-double srcPaddingY so the row-end increment advances
// both luma pointers by a full pair-stride. Chroma advances once per
// pair, so srcPaddingC/U/V are computed the same way as the single-row
// case. No chroma_rewind is needed (a chroma row is consumed by both
// output rows in the same inner iteration).

.macro src_load_args_nv12_2l
        ldp             srcPaddingYw, srcPaddingCw, [x6]
        ldp             srcY,         srcC,         [x5]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingC,  srcPaddingCw
        add             l2_srcY,      srcY,         srcPaddingY         // l2_srcY = srcY + linesizeY
        lsl             srcPaddingY,  srcPaddingY,  #1
        sub             srcPaddingY,  srcPaddingY,  widthx              // = 2*linesizeY - width
        sub             srcPaddingC,  srcPaddingC,  widthx
.endm

.macro src_load_args_nv21_2l
        src_load_args_nv12_2l
.endm

.macro src_load_args_yuv420p_2l
        ldp             srcPaddingYw, srcPaddingUw, [x6]
        ldr             srcPaddingVw, [x6, #8]
        ldp             srcY,         srcU,         [x5]
        ldr             srcV,         [x5, #16]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingU,  srcPaddingUw
        sxtw            srcPaddingV,  srcPaddingVw
        add             l2_srcY,      srcY,         srcPaddingY         // l2_srcY = srcY + linesizeY
        lsl             srcPaddingY,  srcPaddingY,  #1
        sub             srcPaddingY,  srcPaddingY,  widthx              // = 2*linesizeY - width
        sub             srcPaddingU,  srcPaddingU,  widthx, lsr #1
        sub             srcPaddingV,  srcPaddingV,  widthx, lsr #1
.endm

.macro src_load_args_yuva420p_2l
        ldp             srcPaddingYw, srcPaddingUw, [x6]
        ldr             srcPaddingVw, [x6, #8]
        ldr             srcPaddingAw, [x6, #12]                         // srcStride[3]
        ldp             srcY,         srcU,         [x5]
        ldr             srcV,         [x5, #16]
        ldr             srcA,         [x5, #24]                         // src[3]
        sxtw            srcPaddingY,  srcPaddingYw
        sxtw            srcPaddingU,  srcPaddingUw
        sxtw            srcPaddingV,  srcPaddingVw
        sxtw            srcPaddingA,  srcPaddingAw
        add             l2_srcY,      srcY,         srcPaddingY         // l2_srcY = srcY + linesizeY
        add             l2_srcA,      srcA,         srcPaddingA         // l2_srcA = srcA + linesizeA
        lsl             srcPaddingY,  srcPaddingY,  #1
        lsl             srcPaddingA,  srcPaddingA,  #1
        sub             srcPaddingY,  srcPaddingY,  widthx              // = 2*linesizeY - width
        sub             srcPaddingU,  srcPaddingU,  widthx, lsr #1
        sub             srcPaddingV,  srcPaddingV,  widthx, lsr #1
        sub             srcPaddingA,  srcPaddingA,  widthx              // = 2*linesizeA - width
.endm

// --------------------------------------------------------------------
// Destination-side argument unpacking.

.macro dst_load_args_packed bpp
        ldr             dstPadding0w, [sp]                              // linesize0
        sxtw            dstPadding0,  dstPadding0w
.ifc \bpp,2
        sub             dstPadding0,  dstPadding0,  widthx, lsl #1      // = linesize0 - width*2
.endif
.ifc \bpp,3
        sub             dstPadding0,  dstPadding0,  widthx, lsl #1
        sub             dstPadding0,  dstPadding0,  widthx              // = linesize0 - width*3
.endif
.ifc \bpp,4
        sub             dstPadding0,  dstPadding0,  widthx, lsl #2      // = linesize0 - width*4
.endif
.endm

.macro dst_load_args_planar
        ldr             dstPadding0w, [sp]                              // linesize0
        ldr             dst1,         [sp, #8]                          // dst1
        ldr             dstPadding1w, [sp, #16]                         // linesize1
        ldr             dst2,         [sp, #24]                         // dst2
        ldr             dstPadding2w, [sp, #32]                         // linesize2
        sxtw            dstPadding0,  dstPadding0w
        sxtw            dstPadding1,  dstPadding1w
        sxtw            dstPadding2,  dstPadding2w
        sub             dstPadding0,  dstPadding0,  widthx
        sub             dstPadding1,  dstPadding1,  widthx
        sub             dstPadding2,  dstPadding2,  widthx
.endm

.macro dst_load_args_argb
        dst_load_args_packed 4
.endm

.macro dst_load_args_rgba
        dst_load_args_packed 4
.endm

.macro dst_load_args_abgr
        dst_load_args_packed 4
.endm

.macro dst_load_args_bgra
        dst_load_args_packed 4
.endm

.macro dst_load_args_rgb24
        dst_load_args_packed 3
.endm

.macro dst_load_args_bgr24
        dst_load_args_packed 3
.endm

.macro dst_load_args_rgb565le
        dst_load_args_packed 2
.endm

.macro dst_load_args_bgr565le
        dst_load_args_packed 2
.endm

.macro dst_load_args_rgb555le
        dst_load_args_packed 2
.endm

.macro dst_load_args_bgr555le
        dst_load_args_packed 2
.endm

.macro dst_load_args_gbrp
        dst_load_args_planar
.endm

// 2-lines-at-a-time dst loader. Pre-compute l2_dst = dst + linesize
// and pre-double dstPadding so the row-end advance covers both rows.

.macro dst_load_args_packed_2l bpp
        ldr             dstPadding0w, [sp]                              // linesize0
        sxtw            dstPadding0,  dstPadding0w
        add             l2_dst0,      dst0,         dstPadding0         // l2_dst0 = dst0 + linesize0
        lsl             dstPadding0,  dstPadding0,  #1
.ifc \bpp,2
        sub             dstPadding0,  dstPadding0,  widthx, lsl #1      // = 2*linesize0 - width*2
.endif
.ifc \bpp,3
        sub             dstPadding0,  dstPadding0,  widthx, lsl #1
        sub             dstPadding0,  dstPadding0,  widthx              // = 2*linesize0 - width*3
.endif
.ifc \bpp,4
        sub             dstPadding0,  dstPadding0,  widthx, lsl #2      // = 2*linesize0 - width*4
.endif
.endm

.macro dst_load_args_argb_2l
        dst_load_args_packed_2l 4
.endm

.macro dst_load_args_rgba_2l
        dst_load_args_packed_2l 4
.endm

.macro dst_load_args_abgr_2l
        dst_load_args_packed_2l 4
.endm

.macro dst_load_args_bgra_2l
        dst_load_args_packed_2l 4
.endm

.macro dst_load_args_rgb24_2l
        dst_load_args_packed_2l 3
.endm

.macro dst_load_args_bgr24_2l
        dst_load_args_packed_2l 3
.endm

.macro dst_load_args_rgb565le_2l
        dst_load_args_packed_2l 2
.endm

.macro dst_load_args_bgr565le_2l
        dst_load_args_packed_2l 2
.endm

.macro dst_load_args_rgb555le_2l
        dst_load_args_packed_2l 2
.endm

.macro dst_load_args_bgr555le_2l
        dst_load_args_packed_2l 2
.endm

// 2-lines-at-a-time planar dst loader. \sp_off is the byte offset at
// which the caller's [sp+0] arg now lives (i.e., however many bytes the
// caller pushed before invoking this macro). declare_2l_gbrp spills
// x19/x20 (16 bytes) and passes 16; the on-stack args end up at:
//   [sp + sp_off +  0] int     linesize0
//   [sp + sp_off +  8] uint8_t *dst1
//   [sp + sp_off + 16] int     linesize1
//   [sp + sp_off + 24] uint8_t *dst2
//   [sp + sp_off + 32] int     linesize2
.macro dst_load_args_planar_2l sp_off
        ldr             dstPadding0w, [sp, #(\sp_off +  0)]
        ldr             dst1,         [sp, #(\sp_off +  8)]
        ldr             dstPadding1w, [sp, #(\sp_off + 16)]
        ldr             dst2,         [sp, #(\sp_off + 24)]
        ldr             dstPadding2w, [sp, #(\sp_off + 32)]
        sxtw            dstPadding0,  dstPadding0w
        sxtw            dstPadding1,  dstPadding1w
        sxtw            dstPadding2,  dstPadding2w
        add             l2_dst0,      dst0,         dstPadding0          // l2_dst0 = dst0 + linesize0
        add             l2_dst1,      dst1,         dstPadding1          // l2_dst1 = dst1 + linesize1
        add             l2_dst2,      dst2,         dstPadding2          // l2_dst2 = dst2 + linesize2
        lsl             dstPadding0,  dstPadding0,  #1
        lsl             dstPadding1,  dstPadding1,  #1
        lsl             dstPadding2,  dstPadding2,  #1
        sub             dstPadding0,  dstPadding0,  widthx               // = 2*linesize0 - width
        sub             dstPadding1,  dstPadding1,  widthx
        sub             dstPadding2,  dstPadding2,  widthx
.endm

// --------------------------------------------------------------------
// Per-input chroma load (run inside the inner loop).

.macro load_chroma_nv12
        ld2             {v16.8b, v17.8b}, [srcC], #16
        ushll           v18.8h, v16.8b, #3
        ushll           v19.8h, v17.8b, #3
.endm

.macro load_chroma_nv21
        ld2             {v16.8b, v17.8b}, [srcC], #16
        ushll           v19.8h, v16.8b, #3
        ushll           v18.8h, v17.8b, #3
.endm

.macro load_chroma_yuv420p
        ld1             {v16.8b}, [srcU], #8
        ld1             {v17.8b}, [srcV], #8
        ushll           v18.8h, v16.8b, #3
        ushll           v19.8h, v17.8b, #3
.endm

.macro load_chroma_yuva420p
        load_chroma_yuv420p
.endm

.macro load_chroma_yuv422p
        load_chroma_yuv420p
.endm

// --------------------------------------------------------------------
// Row-end chroma increments (single-row code shares one chroma row
// between two consecutive output rows by rewinding on even rows).

.macro increment_nv12
        ands            tmp, height, #1
        csel            tmpx, srcPaddingC, chroma_rewind, ne            // incC = (h & 1) ? srcPaddingC : -width
        add             srcC, srcC, tmpx                                // srcC += incC
.endm

.macro increment_nv21
        increment_nv12
.endm

.macro increment_yuv420p
        ands            tmp, height, #1
        csel            tmpx, srcPaddingU, chroma_rewind, ne            // incU = (h & 1) ? srcPaddingU : -width/2
        add             srcU, srcU, tmpx                                // srcU += incU
        csel            tmpx, srcPaddingV, chroma_rewind, ne            // incV = (h & 1) ? srcPaddingV : -width/2
        add             srcV, srcV, tmpx                                // srcV += incV
.endm

.macro increment_yuva420p
        increment_yuv420p
        add             srcA, srcA, srcPaddingA                         // srcA += srcPaddingA (every row)
.endm

.macro increment_yuv422p
        add             srcU, srcU, srcPaddingU                         // srcU += srcPaddingU
        add             srcV, srcV, srcPaddingV                         // srcV += srcPaddingV
.endm

// 2-lines-at-a-time row-end increments. srcPaddingY already covers two
// luma rows; chroma advances by a single chroma row per pair.

.macro src_increment_nv12_2l
        add             srcY,    srcY,    srcPaddingY
        add             l2_srcY, l2_srcY, srcPaddingY
        add             srcC,    srcC,    srcPaddingC
.endm

.macro src_increment_nv21_2l
        src_increment_nv12_2l
.endm

.macro src_increment_yuv420p_2l
        add             srcY,    srcY,    srcPaddingY
        add             l2_srcY, l2_srcY, srcPaddingY
        add             srcU,    srcU,    srcPaddingU
        add             srcV,    srcV,    srcPaddingV
.endm

.macro src_increment_yuva420p_2l
        add             srcY,    srcY,    srcPaddingY
        add             l2_srcY, l2_srcY, srcPaddingY
        add             srcU,    srcU,    srcPaddingU
        add             srcV,    srcV,    srcPaddingV
        add             srcA,    srcA,    srcPaddingA
        add             l2_srcA, l2_srcA, srcPaddingA
.endm

.macro dst_increment_packed_2l
        add             dst0,    dst0,    dstPadding0
        add             l2_dst0, l2_dst0, dstPadding0
.endm

.macro dst_increment_planar_2l
        add             dst0,    dst0,    dstPadding0
        add             l2_dst0, l2_dst0, dstPadding0
        add             dst1,    dst1,    dstPadding1
        add             l2_dst1, l2_dst1, dstPadding1
        add             dst2,    dst2,    dstPadding2
        add             l2_dst2, l2_dst2, dstPadding2
.endm

// --------------------------------------------------------------------
// Shared compute / pack helpers.

.macro compute_rgb r1 g1 b1 r2 g2 b2
        add             \r1\().8h, v26.8h, v20.8h                       // Y1 + R1
        add             \r2\().8h, v27.8h, v21.8h                       // Y2 + R2
        add             \g1\().8h, v26.8h, v22.8h                       // Y1 + G1
        add             \g2\().8h, v27.8h, v23.8h                       // Y2 + G2
        add             \b1\().8h, v26.8h, v24.8h                       // Y1 + B1
        add             \b2\().8h, v27.8h, v25.8h                       // Y2 + B2
        sqrshrun        \r1\().8b, \r1\().8h, #1                        // clip_u8((Y1 + R1) >> 1)
        sqrshrun        \r2\().8b, \r2\().8h, #1                        // clip_u8((Y2 + R2) >> 1)
        sqrshrun        \g1\().8b, \g1\().8h, #1                        // clip_u8((Y1 + G1) >> 1)
        sqrshrun        \g2\().8b, \g2\().8h, #1                        // clip_u8((Y2 + G2) >> 1)
        sqrshrun        \b1\().8b, \b1\().8h, #1                        // clip_u8((Y1 + B1) >> 1)
        sqrshrun        \b2\().8b, \b2\().8h, #1                        // clip_u8((Y2 + B2) >> 1)
.endm

.macro compute_rgba r1 g1 b1 a1 r2 g2 b2 a2
        compute_rgb     \r1, \g1, \b1, \r2, \g2, \b2
        mov             \a1\().8b, v30.8b
        mov             \a2\().8b, v30.8b
.endm

.macro compute_rgba_alpha r1 g1 b1 a1 r2 g2 b2 a2
        compute_rgb     \r1, \g1, \b1, \r2, \g2, \b2
        mov             \a1\().8b, v28.8b                               // real alpha (first 8 pixels)
        mov             \a2\().8b, v29.8b                               // real alpha (next 8 pixels)
.endm

// Chroma -> RGB offsets, computed once per pixel column for both luma rows.
// In:  v18/v19 (widened chroma from load_chroma_<ifmt>).
// Out: v20-v25 (R1, R2, G1, G2, B1, B2).
.macro chroma_to_rgb_offsets
        sub             v18.8h, v18.8h, v31.8h                          // U*(1<<3) - 128*(1<<3)
        sub             v19.8h, v19.8h, v31.8h                          // V*(1<<3) - 128*(1<<3)
        sqdmulh         v20.8h, v19.8h, v1.h[0]                         // V * v2r            (R)
        sqdmulh         v22.8h, v18.8h, v1.h[1]                         // U * u2g
        sqdmulh         v19.8h, v19.8h, v1.h[2]                         //           V * v2g
        sqdmulh         v24.8h, v18.8h, v1.h[3]                         // U * u2b            (B)
        add             v22.8h, v22.8h, v19.8h                          // U * u2g + V * v2g  (G)
        zip2            v21.8h, v20.8h, v20.8h                          // R2
        zip1            v20.8h, v20.8h, v20.8h                          // R1
        zip2            v23.8h, v22.8h, v22.8h                          // G2
        zip1            v22.8h, v22.8h, v22.8h                          // G1
        zip2            v25.8h, v24.8h, v24.8h                          // B2
        zip1            v24.8h, v24.8h, v24.8h                          // B1
.endm

// Load and scale 16 luma samples from \rsrcY into v26 (Y1) / v27 (Y2).
// v0 = y_coeff, v3 = y_offset (loop-invariant).
.macro load_luma rsrcY
        ld1             {v2.16b}, [\rsrcY], #16                         // load luma
        ushll           v26.8h, v2.8b,  #3                              // Y1*(1<<3)
        ushll2          v27.8h, v2.16b, #3                              // Y2*(1<<3)
        sub             v26.8h, v26.8h, v3.8h                           // Y1*(1<<3) - y_offset
        sub             v27.8h, v27.8h, v3.8h                           // Y2*(1<<3) - y_offset
        sqdmulh         v26.8h, v26.8h, v0.8h                           // (Y1 * y_coeff) >> 15
        sqdmulh         v27.8h, v27.8h, v0.8h                           // (Y2 * y_coeff) >> 15
.endm

// Process one output row: load 16 luma px from \rsrcY, combine with the
// shared chroma offsets in v20-v25, and store 16 px in format \ofmt.
// Packed callers pass the same dst three times.
.macro process_row ifmt, ofmt, rsrcY, rsrcA, rdst0, rdst1, rdst2
        set_rgb16_predicates \ofmt
        load_luma       \rsrcY
.ifc \ifmt,yuva420p
        ld1             {v28.8b, v29.8b}, [\rsrcA], #16                 // 16 alpha bytes
.endif
.ifc \ofmt,argb // a r g b
        compute_rgb     v5, v6, v7, v17, v18, v19
 .ifc \ifmt,yuva420p
        mov             v4.8b,  v28.8b
        mov             v16.8b, v29.8b
 .else
        mov             v4.8b,  v30.8b
        mov             v16.8b, v30.8b
 .endif
        st4             { v4.8b, v5.8b, v6.8b, v7.8b}, [\rdst0], #32
        st4             {v16.8b,v17.8b,v18.8b,v19.8b}, [\rdst0], #32
.endif
.ifc \ofmt,rgba // r g b a
        compute_rgb     v4, v5, v6, v16, v17, v18
 .ifc \ifmt,yuva420p
        mov             v7.8b,  v28.8b
        mov             v19.8b, v29.8b
 .else
        mov             v7.8b,  v30.8b
        mov             v19.8b, v30.8b
 .endif
        st4             { v4.8b, v5.8b, v6.8b, v7.8b}, [\rdst0], #32
        st4             {v16.8b,v17.8b,v18.8b,v19.8b}, [\rdst0], #32
.endif
.ifc \ofmt,abgr // a b g r
        compute_rgb     v7, v6, v5, v19, v18, v17
 .ifc \ifmt,yuva420p
        mov             v4.8b,  v28.8b
        mov             v16.8b, v29.8b
 .else
        mov             v4.8b,  v30.8b
        mov             v16.8b, v30.8b
 .endif
        st4             { v4.8b, v5.8b, v6.8b, v7.8b}, [\rdst0], #32
        st4             {v16.8b,v17.8b,v18.8b,v19.8b}, [\rdst0], #32
.endif
.ifc \ofmt,bgra // b g r a
        compute_rgb     v6, v5, v4, v18, v17, v16
 .ifc \ifmt,yuva420p
        mov             v7.8b,  v28.8b
        mov             v19.8b, v29.8b
 .else
        mov             v7.8b,  v30.8b
        mov             v19.8b, v30.8b
 .endif
        st4             { v4.8b, v5.8b, v6.8b, v7.8b}, [\rdst0], #32
        st4             {v16.8b,v17.8b,v18.8b,v19.8b}, [\rdst0], #32
.endif
.ifc \ofmt,rgb24
        compute_rgb     v4, v5, v6, v16, v17, v18
        st3             { v4.8b, v5.8b, v6.8b}, [\rdst0], #24
        st3             {v16.8b,v17.8b,v18.8b}, [\rdst0], #24
.endif
.ifc \ofmt,bgr24
        compute_rgb     v6, v5, v4, v18, v17, v16
        st3             { v4.8b, v5.8b, v6.8b}, [\rdst0], #24
        st3             {v16.8b,v17.8b,v18.8b}, [\rdst0], #24
.endif
.ifc \ofmt,gbrp
        compute_rgb     v18, v4, v6, v19, v5, v7
        st1             {  v4.8b,  v5.8b }, [\rdst0], #16
        st1             {  v6.8b,  v7.8b }, [\rdst1], #16
        st1             { v18.8b, v19.8b }, [\rdst2], #16
.endif
.if rgb16
 .ifc \ifmt,yuva420p
        .error "yuva420p->rgb16 is dispatched through the yuv420p path (rgb16 has no alpha channel)"
 .endif
        compute_rgb     v4, v5, v6, v16, v17, v18
 .if r_first
        // rgb*le: (R << hshift) | (G << 5) | B
        pack_rgb16_2l   v8,  v6,  v5,  v4,  gshift, hshift
        pack_rgb16_2l   v9,  v18, v17, v16, gshift, hshift
 .else
        // bgr*le: (B << hshift) | (G << 5) | R
        pack_rgb16_2l   v8,  v4,  v5,  v6,  gshift, hshift
        pack_rgb16_2l   v9,  v16, v17, v18, gshift, hshift
 .endif
        st1             { v8.8h, v9.8h}, [\rdst0], #32
.endif
.endm

// Map ofmt to .set predicates: rgb16=1 for the four 16bpp LE ofmts
// (r_first=1 for rgb*, 0 for bgr*; gshift/hshift = 2/11 for 565,
// 3/10 for 555), letting sibling macros branch on .if rgb16 instead of
// repeating a four-way .ifc cascade.
.macro set_rgb16_predicates ofmt
        .set rgb16,     0
        .set r_first,   0
        .set gshift,    0
        .set hshift,    0
.ifc \ofmt,rgb565le
        .set rgb16,     1
        .set r_first,   1
        .set gshift,    2
        .set hshift,   11
.endif
.ifc \ofmt,bgr565le
        .set rgb16,     1
        .set gshift,    2
        .set hshift,   11
.endif
.ifc \ofmt,rgb555le
        .set rgb16,     1
        .set r_first,   1
        .set gshift,    3
        .set hshift,   10
.endif
.ifc \ofmt,bgr555le
        .set rgb16,     1
        .set gshift,    3
        .set hshift,   10
.endif
.endm

// 16bpp packing uses v8/v9 as the accumulator. AAPCS-64 requires d8/d9
// callee-saved (low 64 bits of v8/v9); other ofmts don't touch v8-v15,
// so the spill is gated on rgb16.
.macro save_d8_d9_if_16bpp ofmt
        set_rgb16_predicates \ofmt
.if rgb16
        stp             d8, d9, [sp, #-0x10]!
.endif
.endm

.macro restore_d8_d9_if_16bpp ofmt
        set_rgb16_predicates \ofmt
.if rgb16
        ldp             d8, d9, [sp], #0x10
.endif
.endm

// Pack 8 pixels of 16bpp output. The three channels are extracted via ushr,
// widened to u16, then merged via shift-left-insert:
//   dst = (high << high_shl) | (mid << 5) | low
// For RGB565LE  pass (B, G, R) as (low, mid, high), g_shr=2, high_shl=11.
// For BGR565LE  pass (R, G, B), g_shr=2, high_shl=11.
// For RGB555LE  pass (B, G, R), g_shr=3, high_shl=10.
// For BGR555LE  pass (R, G, B), g_shr=3, high_shl=10.
// Clobbers v20-v23.
.macro pack_rgb16 dst, low_ch, mid_ch, high_ch, g_shr, high_shl
        ushr            v20.8b, \high_ch\().8b, #3
        ushr            v21.8b, \mid_ch\().8b,  #\g_shr
        ushr            v22.8b, \low_ch\().8b,  #3
        uxtl            \dst\().8h, v22.8b
        uxtl            v23.8h, v21.8b
        sli             \dst\().8h, v23.8h, #5
        uxtl            v23.8h, v20.8b
        sli             \dst\().8h, v23.8h, #\high_shl
.endm

// As pack_rgb16 but uses v26-v29 as scratch (luma temps, dead after
// compute_rgb), so v20-v25 chroma contributions survive for the
// second luma row. yuva420p->rgb16 is dispatched through the yuv420p
// path, so v28/v29 aliasing alpha is not a concern here.
.macro pack_rgb16_2l dst, low_ch, mid_ch, high_ch, g_shr, high_shl
        ushr            v26.8b, \high_ch\().8b, #3
        ushr            v27.8b, \mid_ch\().8b,  #\g_shr
        ushr            v28.8b, \low_ch\().8b,  #3
        uxtl            \dst\().8h, v28.8b
        uxtl            v29.8h, v27.8b
        sli             \dst\().8h, v29.8h, #5
        uxtl            v29.8h, v26.8b
        sli             \dst\().8h, v29.8h, #\high_shl
.endm

.macro declare_func ifmt ofmt
function ff_\ifmt\()_to_\ofmt\()_neon, export=1
        uxtw            widthx, width                                   // ensure upper 32 bits of widthx are zero
        dup             v3.8h, y_offset                                 // broadcast y_offset before w2 is reused
        dup             v0.8h, y_coeff                                  // broadcast y_coeff  before w3 is reused
        ld1             {v1.1d}, [table_ptr]                            // load yuv2rgb_table before x4 is reused
        src_load_args_\ifmt
        dst_load_args_\ofmt
        save_d8_d9_if_16bpp \ofmt

        movi            v31.8h, #4, lsl #8                              // 128 * (1<<3) (loop-invariant)
        movi            v30.8b, #255                                    // alpha = 255  (loop-invariant)
        mov             orig_height, height
1:
        mov             cur_width, width
2:
        load_chroma_\ifmt
        sub             v18.8h, v18.8h, v31.8h                          // U*(1<<3) - 128*(1<<3)
        sub             v19.8h, v19.8h, v31.8h                          // V*(1<<3) - 128*(1<<3)
        sqdmulh         v20.8h, v19.8h, v1.h[0]                         // V * v2r            (R)
        sqdmulh         v22.8h, v18.8h, v1.h[1]                         // U * u2g
        ld1             {v2.16b}, [srcY], #16                           // load luma (interleaved)
.ifc \ifmt,yuva420p
        ld1             {v28.8b, v29.8b}, [srcA], #16                   // load 16 alpha bytes
.endif
        sqdmulh         v19.8h, v19.8h, v1.h[2]                         //           V * v2g
        sqdmulh         v24.8h, v18.8h, v1.h[3]                         // U * u2b            (B)
        ushll           v26.8h, v2.8b,  #3                              // Y1*(1<<3)
        ushll2          v27.8h, v2.16b, #3                              // Y2*(1<<3)
        add             v22.8h, v22.8h, v19.8h                          // U * u2g + V * v2g  (G)
        sub             v26.8h, v26.8h, v3.8h                           // Y1*(1<<3) - y_offset
        sub             v27.8h, v27.8h, v3.8h                           // Y2*(1<<3) - y_offset
        zip2            v21.8h, v20.8h, v20.8h                          // R2
        zip1            v20.8h, v20.8h, v20.8h                          // R1
        sqdmulh         v26.8h, v26.8h, v0.8h                           // ((Y1*(1<<3) - y_offset) * y_coeff) >> 15
        sqdmulh         v27.8h, v27.8h, v0.8h                           // ((Y2*(1<<3) - y_offset) * y_coeff) >> 15
        zip2            v23.8h, v22.8h, v22.8h                          // G2
        zip1            v22.8h, v22.8h, v22.8h                          // G1
        zip2            v25.8h, v24.8h, v24.8h                          // B2
        zip1            v24.8h, v24.8h, v24.8h                          // B1

.ifc \ofmt,argb // 1 2 3 0
 .ifc \ifmt,yuva420p
        compute_rgba_alpha v5,v6,v7,v4, v17,v18,v19,v16
 .else
        compute_rgba    v5,v6,v7,v4, v17,v18,v19,v16
 .endif
.endif

.ifc \ofmt,rgba // 0 1 2 3
 .ifc \ifmt,yuva420p
        compute_rgba_alpha v4,v5,v6,v7, v16,v17,v18,v19
 .else
        compute_rgba    v4,v5,v6,v7, v16,v17,v18,v19
 .endif
.endif

.ifc \ofmt,abgr // 3 2 1 0
 .ifc \ifmt,yuva420p
        compute_rgba_alpha v7,v6,v5,v4, v19,v18,v17,v16
 .else
        compute_rgba    v7,v6,v5,v4, v19,v18,v17,v16
 .endif
.endif

.ifc \ofmt,bgra // 2 1 0 3
 .ifc \ifmt,yuva420p
        compute_rgba_alpha v6,v5,v4,v7, v18,v17,v16,v19
 .else
        compute_rgba    v6,v5,v4,v7, v18,v17,v16,v19
 .endif
.endif

.ifc \ofmt,rgb24
        compute_rgb     v4,v5,v6, v16,v17,v18
        st3             { v4.8b, v5.8b, v6.8b}, [dst0], #24
        st3             {v16.8b,v17.8b,v18.8b}, [dst0], #24
.else
 .ifc \ofmt,bgr24
        compute_rgb     v6,v5,v4, v18,v17,v16
        st3             { v4.8b, v5.8b, v6.8b}, [dst0], #24
        st3             {v16.8b,v17.8b,v18.8b}, [dst0], #24
 .else
  .ifc \ofmt,gbrp
        compute_rgb     v18,v4,v6, v19,v5,v7
        st1             {  v4.8b,  v5.8b }, [dst0], #16
        st1             {  v6.8b,  v7.8b }, [dst1], #16
        st1             { v18.8b, v19.8b }, [dst2], #16
  .else
   .if rgb16
        compute_rgb     v4,v5,v6, v16,v17,v18
    .if r_first
        // rgb*le: (R << hshift) | (G << 5) | B
        pack_rgb16      v8,  v6,  v5,  v4,  gshift, hshift
        pack_rgb16      v9,  v18, v17, v16, gshift, hshift
    .else
        // bgr*le: (B << hshift) | (G << 5) | R
        pack_rgb16      v8,  v4,  v5,  v6,  gshift, hshift
        pack_rgb16      v9,  v16, v17, v18, gshift, hshift
    .endif
        st1             { v8.8h, v9.8h}, [dst0], #32
   .else
        st4             { v4.8b, v5.8b, v6.8b, v7.8b}, [dst0], #32
        st4             {v16.8b,v17.8b,v18.8b,v19.8b}, [dst0], #32
   .endif
  .endif
 .endif
.endif
        subs            cur_width, cur_width, #16                       // cur_width -= 16
        b.gt            2b
        add             dst0, dst0, dstPadding0                         // dst0 += padding
.ifc \ofmt,gbrp
        add             dst1, dst1, dstPadding1                         // dst1 += padding1
        add             dst2, dst2, dstPadding2                         // dst2 += padding2
.endif
        add             srcY, srcY, srcPaddingY                         // srcY += paddingY
        increment_\ifmt
        subs            height, height, #1                              // height -= 1
        b.gt            1b
        mov             w0, orig_height                                 // return orig_height
        restore_d8_d9_if_16bpp \ofmt
        ret
endfunc
.endm

.macro declare_rgb_funcs ifmt
        declare_func    \ifmt, argb
        declare_func    \ifmt, rgba
        declare_func    \ifmt, abgr
        declare_func    \ifmt, bgra
        declare_func    \ifmt, gbrp
        declare_func    \ifmt, rgb24
        declare_func    \ifmt, bgr24
.endm

// 2-lines-at-a-time variant of declare_func for the single-dst-pointer
// packed outputs (argb/rgba/abgr/bgra/rgb24/bgr24) with vertically-
// subsampled inputs (nv12/nv21/yuv420p). Two consecutive output rows
// share one chroma row, so the chroma -> RGB offsets (v20-v25) are
// computed once and applied to both luma rows.
//
// Precondition: slice height is even. SET_FF_YUVX_TO_RGBX_FUNC gates
// on !(src_h & 1); scale_internal()'s macro_height_src check in
// libswscale/swscale.c rejects any odd srcSliceH for vertically-
// subsampled sources (chrSrcVSubSample > 0).
.macro declare_2l_packed ifmt ofmt
function ff_\ifmt\()_to_\ofmt\()_neon, export=1
        uxtw            widthx, width
        dup             v3.8h, y_offset
        dup             v0.8h, y_coeff
        ld1             {v1.1d}, [table_ptr]
        src_load_args_\ifmt\()_2l
        dst_load_args_\ofmt\()_2l
        save_d8_d9_if_16bpp \ofmt

        movi            v31.8h, #4, lsl #8                              // 128 * (1<<3) (loop-invariant)
        movi            v30.8b, #255                                    // alpha = 255  (loop-invariant)
        mov             orig_height, height
1:
        mov             cur_width, width
2:
        load_chroma_\ifmt
        chroma_to_rgb_offsets
        process_row     \ifmt, \ofmt, srcY,    srcY,    dst0,    dst0,    dst0
        process_row     \ifmt, \ofmt, l2_srcY, l2_srcY, l2_dst0, l2_dst0, l2_dst0
        subs            cur_width, cur_width, #16
        b.gt            2b
        dst_increment_packed_2l
        src_increment_\ifmt\()_2l
        subs            height, height, #2
        b.gt            1b
        mov             w0, orig_height
        restore_d8_d9_if_16bpp \ofmt
        ret
endfunc
.endm

.macro declare_rgb_funcs_2l_packed ifmt
        declare_2l_packed \ifmt, argb
        declare_2l_packed \ifmt, rgba
        declare_2l_packed \ifmt, abgr
        declare_2l_packed \ifmt, bgra
        declare_2l_packed \ifmt, rgb24
        declare_2l_packed \ifmt, bgr24
.endm

// 2-lines-at-a-time variant for the gbrp planar output. Six dst pointers
// (three per row) exhaust the caller-saved registers, so x19/x20 are
// spilled AAPCS-style. Stack args for the line-1 dst1/dst2/linesize are
// read after the spill, so dst_load_args_planar_2l uses the shifted
// offsets.
.macro declare_2l_gbrp ifmt
function ff_\ifmt\()_to_gbrp_neon, export=1
        uxtw            widthx, width
        dup             v3.8h, y_offset
        dup             v0.8h, y_coeff
        ld1             {v1.1d}, [table_ptr]

        stp             x19, x20, [sp, #-0x10]!                         // callee-saved (line2 planar ptrs)

        src_load_args_\ifmt\()_2l
        dst_load_args_planar_2l 16                                      // 16 = bytes pushed above

        movi            v31.8h, #4, lsl #8                              // 128 * (1<<3) (loop-invariant)
        mov             orig_height, height
1:
        mov             cur_width, width
2:
        load_chroma_\ifmt
        chroma_to_rgb_offsets
        process_row     \ifmt, gbrp, srcY,    srcY,    dst0,    dst1,    dst2
        process_row     \ifmt, gbrp, l2_srcY, l2_srcY, l2_dst0, l2_dst1, l2_dst2
        subs            cur_width, cur_width, #16
        b.gt            2b
        dst_increment_planar_2l
        src_increment_\ifmt\()_2l
        subs            height, height, #2
        b.gt            1b
        mov             w0, orig_height
        ldp             x19, x20, [sp], #0x10                           // restore callee-saved
        ret
endfunc
.endm

// Vertically-subsampled inputs: both packed RGB and gbrp go through the
// 2-lines path. yuv422p has full-height chroma -- no sharing, so it
// keeps the single-row path for every ofmt.
declare_rgb_funcs_2l_packed nv12
declare_2l_gbrp             nv12
declare_rgb_funcs_2l_packed nv21
declare_2l_gbrp             nv21
declare_rgb_funcs_2l_packed yuv420p
declare_2l_gbrp             yuv420p
declare_rgb_funcs           yuv422p

.macro declare_rgb16_funcs ifmt
        declare_func    \ifmt, rgb565le
        declare_func    \ifmt, bgr565le
        declare_func    \ifmt, rgb555le
        declare_func    \ifmt, bgr555le
.endm

.macro declare_rgb16_funcs_2l ifmt
        declare_2l_packed \ifmt, rgb565le
        declare_2l_packed \ifmt, bgr565le
        declare_2l_packed \ifmt, rgb555le
        declare_2l_packed \ifmt, bgr555le
.endm

// Subsampled inputs take the 2-line rgb16 path; yuv422p stays single-row.
declare_rgb16_funcs_2l nv12
declare_rgb16_funcs_2l nv21
declare_rgb16_funcs_2l yuv420p
declare_rgb16_funcs    yuv422p

.macro declare_yuva_funcs ifmt
        declare_func    \ifmt, argb
        declare_func    \ifmt, rgba
        declare_func    \ifmt, abgr
        declare_func    \ifmt, bgra
.endm

// 2-lines-at-a-time path for yuva420p -> {argb,rgba,abgr,bgra}. Chroma
// is vertically subsampled and shared between the two output rows; the
// alpha plane is full resolution, so each row loads its own 16 alpha
// bytes via process_row's \rsrcA arg (srcA / l2_srcA). The constant
// alpha (v30) is never read in this path, so its prologue movi is
// omitted.
.macro declare_2l_yuva ofmt
.ifc \ofmt,gbrp
        .error "yuva420p->gbrp is dispatched through the yuv420p path (gbrp has no alpha channel)"
.endif
function ff_yuva420p_to_\ofmt\()_neon, export=1
        uxtw            widthx, width
        dup             v3.8h, y_offset
        dup             v0.8h, y_coeff
        ld1             {v1.1d}, [table_ptr]
        src_load_args_yuva420p_2l
        dst_load_args_\ofmt\()_2l

        movi            v31.8h, #4, lsl #8                              // 128 * (1<<3) (loop-invariant)
        mov             orig_height, height
1:
        mov             cur_width, width
2:
        load_chroma_yuv420p
        chroma_to_rgb_offsets
        process_row     yuva420p, \ofmt, srcY,    srcA,    dst0,    dst0,    dst0
        process_row     yuva420p, \ofmt, l2_srcY, l2_srcA, l2_dst0, l2_dst0, l2_dst0
        subs            cur_width, cur_width, #16
        b.gt            2b
        dst_increment_packed_2l
        src_increment_yuva420p_2l
        subs            height, height, #2
        b.gt            1b
        mov             w0, orig_height
        ret
endfunc
.endm

.macro declare_yuva_funcs_2l
        declare_2l_yuva argb
        declare_2l_yuva rgba
        declare_2l_yuva abgr
        declare_2l_yuva bgra
.endm

declare_yuva_funcs_2l
