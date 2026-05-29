/*
 * Copyright (c) 2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

// Register aliases for the single-row code. The 32/64-bit view split
// covers prologue-transient roles for x8/x10 (table pointer / scalar
// coefficients are dropped into vector regs and the GPR is reused for
// the loop counter / planar output pointer).
#define width         w0
#define height        w1
#define dst0          x2
#define dstPadding0   w3
#define srcY          x4
#define srcPaddingY   w5
#define srcC          x6
#define srcU          x6
#define srcPaddingC   w7
#define srcPaddingU   w7
#define srcV          x13
#define srcPaddingV   w14
#define srcA          x15
#define srcPaddingA   w16
#define dst1          x10
#define dst2          x15
#define dstPadding1   w12
#define dstPadding2   w16

// Prologue: load table / y_offset / y_coeff from the stack.
#define table_ptr     x8
#define y_offset      w9
#define y_coeff       w10

// Body loop state. Reuses x8/w9 once the prologue has consumed them.
#define cur_width     w8
#define orig_height   w9
#define chroma_rewind w11
#define tmp           w17

.macro load_yoff_ycoeff yoff ycoeff
#if defined(__APPLE__)
        ldp             y_offset, y_coeff, [sp, #\yoff]
#else
        ldr             y_offset, [sp, #\yoff]
        ldr             y_coeff,  [sp, #\ycoeff]
#endif
.endm

.macro load_dst1_dst2 a_dst1 a_linesize1 a_dst2 a_linesize2
#if defined(__APPLE__)
#define DST_OFFSET 8
#else
#define DST_OFFSET 0
#endif
        ldr             dst1,        [sp, #\a_dst1      - DST_OFFSET]
        ldr             dstPadding1, [sp, #\a_linesize1 - DST_OFFSET]
        ldr             dst2,        [sp, #\a_dst2      - DST_OFFSET]
        ldr             dstPadding2, [sp, #\a_linesize2 - DST_OFFSET]
#undef DST_OFFSET
        sub             dstPadding1, dstPadding1, width                 // padding1 = linesize1 - width
        sub             dstPadding2, dstPadding2, width                 // padding2 = linesize2 - width
.endm

.macro load_args_nv12 ofmt
        ldr             table_ptr, [sp]                                 // table
        load_yoff_ycoeff 8, 16                                           // y_offset, y_coeff
        ld1             {v1.1d}, [table_ptr]
        dup             v0.8h, y_coeff
        dup             v3.8h, y_offset
.ifc \ofmt,gbrp
        load_dst1_dst2  24, 32, 40, 48
        sub             dstPadding0, dstPadding0, width                 // dstPadding0 = linesize  - width     (padding)
.else
 .ifc \ofmt,rgb24
        add             tmp, width, width, lsl #1
        sub             dstPadding0, dstPadding0, tmp                   // dstPadding0 = linesize  - width * 3 (padding)
 .else
  .ifc \ofmt,bgr24
        add             tmp, width, width, lsl #1
        sub             dstPadding0, dstPadding0, tmp                   // dstPadding0 = linesize  - width * 3 (padding)
  .else
   .if rgb16
        sub             dstPadding0, dstPadding0, width, lsl #1         // dstPadding0 = linesize  - width * 2 (padding)
   .else
        sub             dstPadding0, dstPadding0, width, lsl #2         // dstPadding0 = linesize  - width * 4 (padding)
   .endif
  .endif
 .endif
.endif
        sub             srcPaddingY, srcPaddingY, width                 // srcPaddingY = linesizeY - width    (paddingY)
        sub             srcPaddingC, srcPaddingC, width                 // srcPaddingC = linesizeC - width    (paddingC)
        neg             chroma_rewind, width
.endm

.macro load_args_nv21 ofmt
        load_args_nv12  \ofmt
.endm

.macro load_args_yuv420p ofmt
        ldr             srcV,        [sp]                               // srcV
        ldr             srcPaddingV, [sp, #8]                           // linesizeV
        ldr             table_ptr,   [sp, #16]                          // table
        load_yoff_ycoeff 24, 32                                          // y_offset, y_coeff
        ld1             {v1.1d}, [table_ptr]
        dup             v0.8h, y_coeff
        dup             v3.8h, y_offset
.ifc \ofmt,gbrp
        load_dst1_dst2  40, 48, 56, 64
        sub             dstPadding0, dstPadding0, width                 // dstPadding0 = linesize  - width     (padding)
.else
 .ifc \ofmt,rgb24
        add             tmp, width, width, lsl #1
        sub             dstPadding0, dstPadding0, tmp                   // dstPadding0 = linesize  - width * 3 (padding)
 .else
  .ifc \ofmt,bgr24
        add             tmp, width, width, lsl #1
        sub             dstPadding0, dstPadding0, tmp                   // dstPadding0 = linesize  - width * 3 (padding)
  .else
   .if rgb16
        sub             dstPadding0, dstPadding0, width, lsl #1         // dstPadding0 = linesize  - width * 2 (padding)
   .else
        sub             dstPadding0, dstPadding0, width, lsl #2         // dstPadding0 = linesize  - width * 4 (padding)
   .endif
  .endif
 .endif
.endif
        sub             srcPaddingY, srcPaddingY, width                 // srcPaddingY = linesizeY - width     (paddingY)
        sub             srcPaddingU, srcPaddingU, width, lsr #1         // srcPaddingU = linesizeU - width / 2 (paddingU)
        sub             srcPaddingV, srcPaddingV, width, lsr #1         // srcPaddingV = linesizeV - width / 2 (paddingV)
        lsr             chroma_rewind, width, #1
        neg             chroma_rewind, chroma_rewind
.endm

.macro load_args_yuva420p ofmt
        load_args_yuv420p \ofmt
#if defined(__APPLE__)
        ldr             srcA,        [sp, #32]                          // srcA
        ldr             srcPaddingA, [sp, #40]                          // linesizeA
#else
        ldr             srcA,        [sp, #40]                          // srcA
        ldr             srcPaddingA, [sp, #48]                          // linesizeA
#endif
        sub             srcPaddingA, srcPaddingA, width                 // srcPaddingA = linesizeA - width    (paddingA)
.endm

.macro load_args_yuv422p ofmt
        ldr             srcV,        [sp]                               // srcV
        ldr             srcPaddingV, [sp, #8]                           // linesizeV
        ldr             table_ptr,   [sp, #16]                          // table
        load_yoff_ycoeff 24, 32                                          // y_offset, y_coeff
        ld1             {v1.1d}, [table_ptr]
        dup             v0.8h, y_coeff
        dup             v3.8h, y_offset
.ifc \ofmt,gbrp
        load_dst1_dst2  40, 48, 56, 64
        sub             dstPadding0, dstPadding0, width                 // dstPadding0 = linesize  - width     (padding)
.else
 .ifc \ofmt,rgb24
        add             tmp, width, width, lsl #1
        sub             dstPadding0, dstPadding0, tmp                   // dstPadding0 = linesize  - width * 3 (padding)
 .else
  .ifc \ofmt,bgr24
        add             tmp, width, width, lsl #1
        sub             dstPadding0, dstPadding0, tmp                   // dstPadding0 = linesize  - width * 3 (padding)
  .else
   .if rgb16
        sub             dstPadding0, dstPadding0, width, lsl #1         // dstPadding0 = linesize  - width * 2 (padding)
   .else
        sub             dstPadding0, dstPadding0, width, lsl #2         // dstPadding0 = linesize  - width * 4 (padding)
   .endif
  .endif
 .endif
.endif
        sub             srcPaddingY, srcPaddingY, width                 // srcPaddingY = linesizeY - width     (paddingY)
        sub             srcPaddingU, srcPaddingU, width, lsr #1         // srcPaddingU = linesizeU - width / 2 (paddingU)
        sub             srcPaddingV, srcPaddingV, width, lsr #1         // srcPaddingV = linesizeV - width / 2 (paddingV)
.endm

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

.macro increment_nv12
        ands            tmp, height, #1
        csel            tmp, srcPaddingC, chroma_rewind, ne             // incC = (h & 1) ? paddingC : -width
        add             srcC, srcC, tmp, sxtw                           // srcC += incC
.endm

.macro increment_nv21
        increment_nv12
.endm

.macro increment_yuv420p
        ands            tmp, height, #1
        csel            tmp, srcPaddingU, chroma_rewind, ne             // incU = (h & 1) ? paddingU : -width/2
        add             srcU, srcU, tmp, sxtw                           // srcU += incU
        csel            tmp, srcPaddingV, chroma_rewind, ne             // incV = (h & 1) ? paddingV : -width/2
        add             srcV, srcV, tmp, sxtw                           // srcV += incV
.endm

.macro increment_yuva420p
        increment_yuv420p
        add             srcA, srcA, srcPaddingA, sxtw                   // srcA += paddingA (every row)
.endm

.macro increment_yuv422p
        add             srcU, srcU, srcPaddingU, sxtw                   // srcU += paddingU
        add             srcV, srcV, srcPaddingV, sxtw                   // srcV += paddingV
.endm

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
.macro save_d8_d9_if_16bpp
.if rgb16
        stp             d8, d9, [sp, #-0x10]!
.endif
.endm

.macro restore_d8_d9_if_16bpp
.if rgb16
        ldp             d8, d9, [sp], #0x10
.endif
.endm

// Pack 8 pixels of 16bpp output. The three channels are extracted via ushr,
// widened to u16, then merged via shift-left-insert:
//   dst0 = (high << high_shl) | (mid << 5) | low
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

.macro declare_func ifmt ofmt
function ff_\ifmt\()_to_\ofmt\()_neon, export=1
        set_rgb16_predicates \ofmt
        load_args_\ifmt \ofmt
        save_d8_d9_if_16bpp

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
        st1             {  v4.8b,  v5.8b }, [dst0],  #16
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
        subs            cur_width, cur_width, #16                       // width -= 16
        b.gt            2b
        add             dst0, dst0, dstPadding0, sxtw                   // dst0 += padding
.ifc \ofmt,gbrp
        add             dst1, dst1, dstPadding1, sxtw                   // dst1 += padding1
        add             dst2, dst2, dstPadding2, sxtw                   // dst2 += padding2
.endif
        add             srcY, srcY, srcPaddingY, sxtw                   // srcY += paddingY
        increment_\ifmt
        subs            height, height, #1                              // height -= 1
        b.gt            1b
        mov             w0, orig_height                                 // return orig_height
        restore_d8_d9_if_16bpp
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

declare_rgb_funcs nv12
declare_rgb_funcs nv21
declare_rgb_funcs yuv420p
declare_rgb_funcs yuv422p

.macro declare_rgb16_funcs ifmt
        declare_func    \ifmt, rgb565le
        declare_func    \ifmt, bgr565le
        declare_func    \ifmt, rgb555le
        declare_func    \ifmt, bgr555le
.endm

declare_rgb16_funcs nv12
declare_rgb16_funcs nv21
declare_rgb16_funcs yuv420p
declare_rgb16_funcs yuv422p

.macro declare_yuva_funcs ifmt
        declare_func    \ifmt, argb
        declare_func    \ifmt, rgba
        declare_func    \ifmt, abgr
        declare_func    \ifmt, bgra
.endm

declare_yuva_funcs yuva420p
