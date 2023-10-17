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

.macro load_yoff_ycoeff yoff ycoeff
#if defined(__APPLE__)
        ldp             w9, w10, [sp, #\yoff]
#else
        ldr             w9,  [sp, #\yoff]
        ldr             w10, [sp, #\ycoeff]
#endif
.endm

.macro load_args_nv12
        ldr             x8,  [sp]                                       // table
        load_yoff_ycoeff 8, 16                                           // y_offset, y_coeff
        ld1             {v1.1d}, [x8]
        dup             v0.8h, w10
        dup             v3.8h, w9
        sub             w3, w3, w0, lsl #2                              // w3 = linesize  - width * 4 (padding)
        sub             w5, w5, w0                                      // w5 = linesizeY - width     (paddingY)
        sub             w7, w7, w0                                      // w7 = linesizeC - width     (paddingC)
        neg             w11, w0
.endm

.macro load_args_nv21
    load_args_nv12
.endm

.macro load_args_yuv420p
        ldr             x13, [sp]                                       // srcV
        ldr             w14, [sp, #8]                                   // linesizeV
        ldr             x8,  [sp, #16]                                  // table
        load_yoff_ycoeff 24, 32                                          // y_offset, y_coeff
        ld1             {v1.1d}, [x8]
        dup             v0.8h, w10
        dup             v3.8h, w9
        sub             w3, w3, w0, lsl #2                              // w3 = linesize  - width * 4 (padding)
        sub             w5, w5, w0                                      // w5 = linesizeY - width     (paddingY)
        sub             w7,  w7,  w0, lsr #1                            // w7  = linesizeU - width / 2 (paddingU)
        sub             w14, w14, w0, lsr #1                            // w14 = linesizeV - width / 2 (paddingV)
        lsr             w11, w0, #1
        neg             w11, w11
.endm

.macro load_args_yuv422p
        ldr             x13, [sp]                                       // srcV
        ldr             w14, [sp, #8]                                   // linesizeV
        ldr             x8,  [sp, #16]                                  // table
        load_yoff_ycoeff 24, 32                                          // y_offset, y_coeff
        ld1             {v1.1d}, [x8]
        dup             v0.8h, w10
        dup             v3.8h, w9
        sub             w3, w3, w0, lsl #2                              // w3 = linesize  - width * 4 (padding)
        sub             w5, w5, w0                                      // w5 = linesizeY - width     (paddingY)
        sub             w7,  w7,  w0, lsr #1                            // w7  = linesizeU - width / 2 (paddingU)
        sub             w14, w14, w0, lsr #1                            // w14 = linesizeV - width / 2 (paddingV)
.endm

.macro load_chroma_nv12
        ld2             {v16.8b, v17.8b}, [x6], #16
        ushll           v18.8h, v16.8b, #3
        ushll           v19.8h, v17.8b, #3
.endm

.macro load_chroma_nv21
        ld2             {v16.8b, v17.8b}, [x6], #16
        ushll           v19.8h, v16.8b, #3
        ushll           v18.8h, v17.8b, #3
.endm

.macro load_chroma_yuv420p
        ld1             {v16.8b}, [ x6], #8
        ld1             {v17.8b}, [x13], #8
        ushll           v18.8h, v16.8b, #3
        ushll           v19.8h, v17.8b, #3
.endm

.macro load_chroma_yuv422p
    load_chroma_yuv420p
.endm

.macro increment_nv12
        ands            w15, w1, #1
        csel            w16, w7, w11, ne                                // incC = (h & 1) ? paddincC : -width
        add             x6,  x6, w16, sxtw                              // srcC += incC
.endm

.macro increment_nv21
    increment_nv12
.endm

.macro increment_yuv420p
        ands            w15, w1, #1
        csel            w16,  w7, w11, ne                               // incU = (h & 1) ? paddincU : -width/2
        csel            w17, w14, w11, ne                               // incV = (h & 1) ? paddincV : -width/2
        add             x6,  x6,  w16, sxtw                             // srcU += incU
        add             x13, x13, w17, sxtw                             // srcV += incV
.endm

.macro increment_yuv422p
        add             x6,  x6,  w7, sxtw                              // srcU += incU
        add             x13, x13, w14, sxtw                             // srcV += incV
.endm

.macro compute_rgba r1 g1 b1 a1 r2 g2 b2 a2
        add             v20.8h, v26.8h, v20.8h                          // Y1 + R1
        add             v21.8h, v27.8h, v21.8h                          // Y2 + R2
        add             v22.8h, v26.8h, v22.8h                          // Y1 + G1
        add             v23.8h, v27.8h, v23.8h                          // Y2 + G2
        add             v24.8h, v26.8h, v24.8h                          // Y1 + B1
        add             v25.8h, v27.8h, v25.8h                          // Y2 + B2
        sqrshrun        \r1, v20.8h, #1                                 // clip_u8((Y1 + R1) >> 1)
        sqrshrun        \r2, v21.8h, #1                                 // clip_u8((Y2 + R1) >> 1)
        sqrshrun        \g1, v22.8h, #1                                 // clip_u8((Y1 + G1) >> 1)
        sqrshrun        \g2, v23.8h, #1                                 // clip_u8((Y2 + G1) >> 1)
        sqrshrun        \b1, v24.8h, #1                                 // clip_u8((Y1 + B1) >> 1)
        sqrshrun        \b2, v25.8h, #1                                 // clip_u8((Y2 + B1) >> 1)
        movi            \a1, #255
        movi            \a2, #255
.endm

.macro declare_func ifmt ofmt
function ff_\ifmt\()_to_\ofmt\()_neon, export=1
    load_args_\ifmt
        mov             w9, w1
1:
        mov             w8, w0                                          // w8 = width
2:
        movi            v5.8h, #4, lsl #8                               // 128 * (1<<3)
    load_chroma_\ifmt
        sub             v18.8h, v18.8h, v5.8h                           // U*(1<<3) - 128*(1<<3)
        sub             v19.8h, v19.8h, v5.8h                           // V*(1<<3) - 128*(1<<3)
        sqdmulh         v20.8h, v19.8h, v1.h[0]                         // V * v2r            (R)
        sqdmulh         v22.8h, v18.8h, v1.h[1]                         // U * u2g
        sqdmulh         v19.8h, v19.8h, v1.h[2]                         //           V * v2g
        add             v22.8h, v22.8h, v19.8h                          // U * u2g + V * v2g  (G)
        sqdmulh         v24.8h, v18.8h, v1.h[3]                         // U * u2b            (B)
        zip2            v21.8h, v20.8h, v20.8h                          // R2
        zip1            v20.8h, v20.8h, v20.8h                          // R1
        zip2            v23.8h, v22.8h, v22.8h                          // G2
        zip1            v22.8h, v22.8h, v22.8h                          // G1
        zip2            v25.8h, v24.8h, v24.8h                          // B2
        zip1            v24.8h, v24.8h, v24.8h                          // B1
        ld1             {v2.16b}, [x4], #16                             // load luma
        ushll           v26.8h, v2.8b,  #3                              // Y1*(1<<3)
        ushll2          v27.8h, v2.16b, #3                              // Y2*(1<<3)
        sub             v26.8h, v26.8h, v3.8h                           // Y1*(1<<3) - y_offset
        sub             v27.8h, v27.8h, v3.8h                           // Y2*(1<<3) - y_offset
        sqdmulh         v26.8h, v26.8h, v0.8h                           // ((Y1*(1<<3) - y_offset) * y_coeff) >> 15
        sqdmulh         v27.8h, v27.8h, v0.8h                           // ((Y2*(1<<3) - y_offset) * y_coeff) >> 15

.ifc \ofmt,argb // 1 2 3 0
        compute_rgba    v5.8b,v6.8b,v7.8b,v4.8b, v17.8b,v18.8b,v19.8b,v16.8b
.endif

.ifc \ofmt,rgba // 0 1 2 3
        compute_rgba    v4.8b,v5.8b,v6.8b,v7.8b, v16.8b,v17.8b,v18.8b,v19.8b
.endif

.ifc \ofmt,abgr // 3 2 1 0
        compute_rgba    v7.8b,v6.8b,v5.8b,v4.8b, v19.8b,v18.8b,v17.8b,v16.8b
.endif

.ifc \ofmt,bgra // 2 1 0 3
        compute_rgba    v6.8b,v5.8b,v4.8b,v7.8b, v18.8b,v17.8b,v16.8b,v19.8b
.endif

        st4             { v4.8b, v5.8b, v6.8b, v7.8b}, [x2], #32
        st4             {v16.8b,v17.8b,v18.8b,v19.8b}, [x2], #32
        subs            w8, w8, #16                                     // width -= 16
        b.gt            2b
        add             x2, x2, w3, sxtw                                // dst  += padding
        add             x4, x4, w5, sxtw                                // srcY += paddingY
    increment_\ifmt
        subs            w1, w1, #1                                      // height -= 1
        b.gt            1b
        mov             w0, w9
    ret
endfunc
.endm

.macro declare_rgb_funcs ifmt
        declare_func    \ifmt, argb
        declare_func    \ifmt, rgba
        declare_func    \ifmt, abgr
        declare_func    \ifmt, bgra
.endm

declare_rgb_funcs nv12
declare_rgb_funcs nv21
declare_rgb_funcs yuv420p
declare_rgb_funcs yuv422p
