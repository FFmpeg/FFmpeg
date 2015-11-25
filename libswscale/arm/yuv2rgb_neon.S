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

.macro compute_premult half_u half_v
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

.macro compute_color dst_comp pre1 pre2
    vadd.s32            q3, q1, \pre1
    vadd.s32            q4, q2, \pre2
    vqrshrun.s32        d10, q3, #13
    vqrshrun.s32        d11, q4, #13                                   @ q5 = ({q3,q4} + (1<<12)) >> 13
    vqmovn.u16          \dst_comp, q5                                  @ saturate 16bit -> 8bit
.endm

.macro compute_rgba r g b a
    compute_color       \r, q8,  q9
    compute_color       \g, q10, q11
    compute_color       \b, q12, q13
    vmov.u8             \a, #255
.endm

.macro compute_half_line dst half_y ofmt
    vmovl.u8            q7, \half_y                                    @ 8px of Y
    vdup.16             q5, r9
    vsub.s16            q7, q5
    vmull.s16           q1, d14, d0                                    @ q1 = (srcY - y_offset) * y_coeff (left)
    vmull.s16           q2, d15, d0                                    @ q2 = (srcY - y_offset) * y_coeff (right)

.ifc \ofmt,argb
    compute_rgba        d13, d14, d15, d12
.endif

.ifc \ofmt,rgba
    compute_rgba        d12, d13, d14, d15
.endif

.ifc \ofmt,abgr
    compute_rgba        d15, d14, d13, d12
.endif

.ifc \ofmt,bgra
    compute_rgba        d14, d13, d12, d15
.endif

    vst4.8              {q6, q7}, [\dst,:128]!
.endm

.macro declare_func ifmt ofmt
function ff_\ifmt\()_to_\ofmt\()_neon, export=1
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
1:
    mov                 r8, r0                                         @ r8 = width
2:
    pld [r6, #64*3]
    pld [r4, #64*3]
    pld [r12, #64*3]

    vld2.8              {d2, d3}, [r6]!                                @ q1: interleaved chroma line
    vmov.i8             d10, #128
.ifc \ifmt,nv12
    vsubl.u8            q14, d2, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d3, d10                                   @ q15 = V - 128
.else
    vsubl.u8            q14, d3, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d2, d10                                   @ q15 = V - 128
.endif

    compute_premult     d28, d30

    vld1.8              {q7}, [r4]!                                    @ first line of luma
    vmov                d28, d15                                       @ save right of the first line of luma for later use
    compute_half_line   r2, d14, \ofmt

    vld1.8              {q7}, [r12]!                                   @ second line of luma
    vmov                d30, d15                                       @ save right of the second line of luma for later use
    compute_half_line   r11, d14, \ofmt

    compute_premult     d29, d31
    compute_half_line   r2,  d28, \ofmt
    compute_half_line   r11, d30, \ofmt

    subs                r8, r8, #16                                    @ width -= 16
    bgt                 2b

    add                 r2, r2, r3                                     @ dst   += padding
    add                 r4, r4, r5                                     @ srcY  += paddingY
    add                 r11, r11, r3                                   @ dst2  += padding
    add                 r12, r12, r5                                   @ srcY2 += paddingY
    add                 r6, r6, r7                                     @ srcC  += paddingC

    subs                r1, r1, #2                                     @ height -= 2
    bgt                 1b

    vpop                {q4-q7}
    pop                 {r4-r12, lr}
    mov                 pc, lr
endfunc
.endm

.macro declare_rgb_funcs ifmt
    declare_func \ifmt, argb
    declare_func \ifmt, rgba
    declare_func \ifmt, abgr
    declare_func \ifmt, bgra
.endm

declare_rgb_funcs nv12
declare_rgb_funcs nv21
