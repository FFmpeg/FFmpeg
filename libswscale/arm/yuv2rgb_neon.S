/*
 * Copyright (c) 2015 Matthieu Bouron <matthieu.bouron stupeflix.com>
 * Copyright (c) 2015 Clément Bœsch <clement stupeflix.com>
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

#include "libavutil/arm/asm.S"


.macro compute_premult_16 half_u1, half_u2, half_v1, half_v2
    vmov                d2, \half_u1                                   @ copy left q14 to left q1
    vmov                d3, \half_u1                                   @ copy left q14 to right q1
    vmov                d4, \half_u2                                   @ copy right q14 to left q2
    vmov                d5, \half_u2                                   @ copy right q14 to right q2

    vmov                d6, \half_v1                                   @ copy left q15 to left q3
    vmov                d7, \half_v1                                   @ copy left q15 to right q3
    vmov                d8, \half_v2                                   @ copy right q15 to left q4
    vmov                d9, \half_v2                                   @ copy right q15 to right q4

    vzip.16             d2, d3                                         @ U1U1U2U2U3U3U4U4
    vzip.16             d4, d5                                         @ U5U5U6U6U7U7U8U8

    vzip.16             d6, d7                                         @ V1V1V2V2V3V3V4V4
    vzip.16             d8, d9                                         @ V5V5V6V6V7V7V8V8

    vmul.s16            q8,  q3, d1[0]                                 @  V * v2r             (left,  red)
    vmul.s16            q9,  q4, d1[0]                                 @  V * v2r             (right, red)
    vmul.s16            q10, q1, d1[1]                                 @  U * u2g
    vmul.s16            q11, q2, d1[1]                                 @  U * u2g
    vmla.s16            q10, q3, d1[2]                                 @  U * u2g + V * v2g   (left,  green)
    vmla.s16            q11, q4, d1[2]                                 @  U * u2g + V * v2g   (right, green)
    vmul.s16            q12, q1, d1[3]                                 @  U * u2b             (left,  blue)
    vmul.s16            q13, q2, d1[3]                                 @  U * u2b             (right, blue)
.endm

.macro compute_premult_32 half_u half_v
    vmov                d2, \half_u                                    @ copy left q14 to left q1
    vmov                d3, \half_u                                    @ copy left q14 to right q1
    vmov                d4, \half_v                                    @ copy left q15 to left q2
    vmov                d5, \half_v                                    @ copy left q15 to right q2

    vzip.16             d2, d3                                         @ U1U1U2U2U3U3U4U4
    vzip.16             d4, d5                                         @ V1V1V2V2V3V3V4V4

    vmull.s16           q8,  d4, d1[0]                                 @  V * v2r             (left,  red)
    vmull.s16           q9,  d5, d1[0]                                 @  V * v2r             (right, red)
    vmull.s16           q10, d2, d1[1]                                 @  U * u2g
    vmull.s16           q11, d3, d1[1]                                 @  U * u2g
    vmlal.s16           q10, d4, d1[2]                                 @  U * u2g + V * v2g   (left,  green)
    vmlal.s16           q11, d5, d1[2]                                 @  U * u2g + V * v2g   (right, green)
    vmull.s16           q12, d2, d1[3]                                 @  U * u2b             (left,  blue)
    vmull.s16           q13, d3, d1[3]                                 @  U * u2b             (right, blue)
.endm

.macro compute_color_16 dst_comp1 dst_comp2 pre1 pre2
    vadd.s16            q1, q14, \pre1
    vadd.s16            q2, q15, \pre2
    vqrshrun.s16        \dst_comp1, q1, #6
    vqrshrun.s16        \dst_comp2, q2, #6
.endm

.macro compute_color_32 dst_comp pre1 pre2
    vadd.s32            q3, q1, \pre1
    vadd.s32            q4, q2, \pre2
    vqrshrun.s32        d10, q3, #13
    vqrshrun.s32        d11, q4, #13                                   @ q5 = ({q3,q4} + (1<<12)) >> 13
    vqmovn.u16          \dst_comp, q5                                  @ saturate 16bit -> 8bit
.endm

.macro compute_rgba_16 r1 r2 g1 g2 b1 b2 a1 a2
    compute_color_16    \r1, \r2, q8,  q9
    compute_color_16    \g1, \g2, q10, q11
    compute_color_16    \b1, \b2, q12, q13
    vmov.u8             \a1, #255
    vmov.u8             \a2, #255
.endm

.macro compute_rgba_32 r g b a
    compute_color_32    \r, q8,  q9
    compute_color_32    \g, q10, q11
    compute_color_32    \b, q12, q13
    vmov.u8             \a, #255
.endm

.macro compute_16px_16 dst y0 y1 ofmt
    vmovl.u8            q14, \y0                                       @ 8px of y
    vmovl.u8            q15, \y1                                       @ 8px of y

    vdup.16             q5, r9                                         @ q5  = y_offset
    vmov                d14, d0                                        @ q7  = y_coeff
    vmov                d15, d0                                        @ q7  = y_coeff

    vsub.s16            q14, q5
    vsub.s16            q15, q5

    vmul.s16            q14, q7                                        @ q14 = (srcY - y_offset) * y_coeff (left)
    vmul.s16            q15, q7                                        @ q15 = (srcY - y_offset) * y_coeff (right)


.ifc \ofmt,argb
    compute_rgba_16     d7, d11, d8, d12, d9, d13, d6, d10
.endif

.ifc \ofmt,rgba
    compute_rgba_16     d6, d10, d7, d11, d8, d12, d9, d13
.endif

.ifc \ofmt,abgr
    compute_rgba_16     d9, d13, d8, d12, d7, d11, d6, d10
.endif

.ifc \ofmt,bgra
    compute_rgba_16     d8, d12, d7, d11, d6, d10, d9, d13
.endif
    vst4.8              {q3, q4}, [\dst,:128]!
    vst4.8              {q5, q6}, [\dst,:128]!

.endm

.macro compute_8px_32 dst half_y ofmt
    vmovl.u8            q7, \half_y                                    @ 8px of Y
    vdup.16             q5, r9
    vsub.s16            q7, q5
    vmull.s16           q1, d14, d0                                    @ q1 = (srcY - y_offset) * y_coeff (left)
    vmull.s16           q2, d15, d0                                    @ q2 = (srcY - y_offset) * y_coeff (right)

.ifc \ofmt,argb
    compute_rgba_32     d13, d14, d15, d12
.endif

.ifc \ofmt,rgba
    compute_rgba_32     d12, d13, d14, d15
.endif

.ifc \ofmt,abgr
    compute_rgba_32     d15, d14, d13, d12
.endif

.ifc \ofmt,bgra
    compute_rgba_32     d14, d13, d12, d15
.endif

    vst4.8              {q6, q7}, [\dst,:128]!
.endm

.macro process_16px_16 ofmt
    compute_premult_16  d28, d29, d30, d31

    vld1.8              {q7}, [r4]!                                    @ first line of luma
    compute_16px_16     r2, d14, d15, \ofmt

    vld1.8              {q7}, [r12]!                                   @ second line of luma
    compute_16px_16     r11, d14, d15, \ofmt
.endm

.macro process_16px_32 ofmt
    compute_premult_32  d28, d30

    vld1.8              {q7}, [r4]!                                    @ first line of luma
    vmov                d28, d15                                       @ save right of the first line of luma for later use
    compute_8px_32      r2, d14, \ofmt

    vld1.8              {q7}, [r12]!                                   @ second line of luma
    vmov                d30, d15                                       @ save right of the second line of luma for later use
    compute_8px_32      r11, d14, \ofmt

    compute_premult_32  d29, d31
    compute_8px_32      r2,  d28, \ofmt
    compute_8px_32      r11, d30, \ofmt
.endm

.macro load_args_nvx
    push                {r4-r12, lr}
    vpush               {q4-q7}
    ldr                 r4, [sp, #104]                                 @ r4  = srcY
    ldr                 r5, [sp, #108]                                 @ r5  = linesizeY
    ldr                 r6, [sp, #112]                                 @ r6  = srcC
    ldr                 r7, [sp, #116]                                 @ r7  = linesizeC
    ldr                 r8, [sp, #120]                                 @ r8  = table
    ldr                 r9, [sp, #124]                                 @ r9  = y_offset
    ldr                 r10,[sp, #128]                                 @ r10 = y_coeff
    vdup.16             d0, r10                                        @ d0  = y_coeff
    vld1.16             {d1}, [r8]                                     @ d1  = *table
    add                 r11, r2, r3                                    @ r11 = dst + linesize (dst2)
    add                 r12, r4, r5                                    @ r12 = srcY + linesizeY (srcY2)
    lsl                 r3, r3, #1
    lsl                 r5, r5, #1
    lsl                 r8, r0, #2
    sub                 r3, r3, r8                                     @ r3 = linesize  * 2 - width * 4 (padding)
    sub                 r5, r5, r0                                     @ r5 = linesizeY * 2 - width     (paddingY)
    sub                 r7, r7, r0                                     @ r7 = linesizeC     - width     (paddingC)
.endm

.macro load_args_yuv420p
    push                {r4-r12, lr}
    vpush               {q4-q7}
    ldr                 r4, [sp, #104]                                 @ r4  = srcY
    ldr                 r5, [sp, #108]                                 @ r5  = linesizeY
    ldr                 r6, [sp, #112]                                 @ r6  = srcU
    ldr                 r8, [sp, #128]                                 @ r8  = table
    ldr                 r9, [sp, #132]                                 @ r9  = y_offset
    ldr                 r10,[sp, #136]                                 @ r10 = y_coeff
    vdup.16             d0, r10                                        @ d0  = y_coeff
    vld1.16             {d1}, [r8]                                     @ d1  = *table
    add                 r11, r2, r3                                    @ r11 = dst + linesize (dst2)
    add                 r12, r4, r5                                    @ r12 = srcY + linesizeY (srcY2)
    lsl                 r3, r3, #1
    lsl                 r5, r5, #1
    lsl                 r8, r0, #2
    sub                 r3, r3, r8                                     @ r3 = linesize  * 2 - width * 4 (padding)
    sub                 r5, r5, r0                                     @ r5 = linesizeY * 2 - width     (paddingY)
    ldr                 r10,[sp, #120]                                 @ r10 = srcV
.endm

.macro declare_func ifmt ofmt precision
function ff_\ifmt\()_to_\ofmt\()_neon_\precision\(), export=1

.ifc \ifmt,nv12
    load_args_nvx
.endif

.ifc \ifmt,nv21
    load_args_nvx
.endif

.ifc \ifmt,yuv420p
    load_args_yuv420p
.endif

1:
    mov                 r8, r0                                         @ r8 = width
2:
    pld [r6, #64*3]
    pld [r4, #64*3]
    pld [r12, #64*3]

    vmov.i8             d10, #128

.ifc \ifmt,nv12
    vld2.8              {d2, d3}, [r6]!                                @ q1: interleaved chroma line
    vsubl.u8            q14, d2, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d3, d10                                   @ q15 = V - 128
.endif

.ifc \ifmt,nv21
    vld2.8              {d2, d3}, [r6]!                                @ q1: interleaved chroma line
    vsubl.u8            q14, d3, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d2, d10                                   @ q15 = V - 128
.endif

.ifc \ifmt,yuv420p
    pld [r10, #64*3]

    vld1.8              d2, [r6]!                                      @ d2: chroma red line
    vld1.8              d3, [r10]!                                     @ d3: chroma blue line
    vsubl.u8            q14, d2, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d3, d10                                   @ q15 = V - 128
.endif


    process_16px_\precision \ofmt

    subs                r8, r8, #16                                    @ width -= 16
    bgt                 2b

    add                 r2, r2, r3                                     @ dst   += padding
    add                 r4, r4, r5                                     @ srcY  += paddingY
    add                 r11, r11, r3                                   @ dst2  += padding
    add                 r12, r12, r5                                   @ srcY2 += paddingY

.ifc \ifmt,nv12
    add                 r6, r6, r7                                     @ srcC  += paddingC
.endif

.ifc \ifmt,nv21
    add                 r6, r6, r7                                     @ srcC  += paddingC
.endif

.ifc \ifmt,yuv420p
    ldr                 r7, [sp, #116]                                 @ r7     = linesizeU
    sub                 r7, r7, r0, lsr #1                             @ r7     = linesizeU - width / 2 (paddingU)
    add                 r6, r6, r7                                     @ srcU  += paddingU

    ldr                 r7, [sp, #124]                                 @ r7     = linesizeV
    sub                 r7, r7, r0, lsr #1                             @ r7     = linesizeV - width / 2 (paddingV)
    add                 r10, r10, r7                                   @ srcU  += paddingV
.endif

    subs                r1, r1, #2                                     @ height -= 2
    bgt                 1b

    vpop                {q4-q7}
    pop                 {r4-r12, lr}
    mov                 pc, lr
endfunc
.endm

.macro declare_rgb_funcs ifmt precision
    declare_func \ifmt, argb, \precision
    declare_func \ifmt, rgba, \precision
    declare_func \ifmt, abgr, \precision
    declare_func \ifmt, bgra, \precision
.endm

declare_rgb_funcs nv12, 16
declare_rgb_funcs nv21, 16
declare_rgb_funcs nv12, 32
declare_rgb_funcs nv21, 32
declare_rgb_funcs yuv420p, 16
declare_rgb_funcs yuv420p, 32
