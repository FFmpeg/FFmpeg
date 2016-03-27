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


.macro compute_premult half_u1, half_u2, half_v1, half_v2
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

.macro compute_color dst_comp1 dst_comp2 pre1 pre2
    vadd.s16            q1, q14, \pre1
    vadd.s16            q2, q15, \pre2
    vqrshrun.s16        \dst_comp1, q1, #6
    vqrshrun.s16        \dst_comp2, q2, #6
.endm

.macro compute_rgba r1 r2 g1 g2 b1 b2 a1 a2
    compute_color       \r1, \r2, q8,  q9
    compute_color       \g1, \g2, q10, q11
    compute_color       \b1, \b2, q12, q13
    vmov.u8             \a1, #255
    vmov.u8             \a2, #255
.endm

.macro compute_16px dst y0 y1 ofmt
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
    compute_rgba        d7, d11, d8, d12, d9, d13, d6, d10
.endif

.ifc \ofmt,rgba
    compute_rgba        d6, d10, d7, d11, d8, d12, d9, d13
.endif

.ifc \ofmt,abgr
    compute_rgba        d9, d13, d8, d12, d7, d11, d6, d10
.endif

.ifc \ofmt,bgra
    compute_rgba        d8, d12, d7, d11, d6, d10, d9, d13
.endif
    vst4.8              {q3, q4}, [\dst,:128]!
    vst4.8              {q5, q6}, [\dst,:128]!

.endm

.macro process_1l_16px_internal dst src ofmt
    vld1.8              {q7}, [\src]!
    compute_16px        \dst, d14, d15, \ofmt
.endm

.macro process_1l_16px ofmt
    compute_premult     d28, d29, d30, d31
    process_1l_16px_internal r2, r4, \ofmt
.endm

.macro process_2l_16px ofmt
    compute_premult     d28, d29, d30, d31
    process_1l_16px_internal r2, r4, \ofmt
    process_1l_16px_internal r11,r12,\ofmt
.endm

.macro load_args_nv12
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
    sub                 r3, r3, r0, lsl #2                             @ r3 = linesize  * 2 - width * 4 (padding)
    sub                 r5, r5, r0                                     @ r5 = linesizeY * 2 - width     (paddingY)
    sub                 r7, r7, r0                                     @ r7 = linesizeC     - width     (paddingC)
.endm

.macro load_args_nv21
    load_args_nv12
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
    sub                 r3, r3, r0, lsl #2                             @ r3 = linesize  * 2 - width * 4 (padding)
    sub                 r5, r5, r0                                     @ r5 = linesizeY * 2 - width     (paddingY)
    ldr                 r10,[sp, #120]                                 @ r10 = srcV
.endm

.macro load_args_yuv422p
    push                {r4-r12, lr}
    vpush               {q4-q7}
    ldr                 r4, [sp, #104]                                 @ r4  = srcY
    ldr                 r5, [sp, #108]                                 @ r5  = linesizeY
    ldr                 r6, [sp, #112]                                 @ r6  = srcU
    ldr                 r7, [sp, #116]                                 @ r7  = linesizeU
    ldr                 r12,[sp, #124]                                 @ r12 = linesizeV
    ldr                 r8, [sp, #128]                                 @ r8  = table
    ldr                 r9, [sp, #132]                                 @ r9  = y_offset
    ldr                 r10,[sp, #136]                                 @ r10 = y_coeff
    vdup.16             d0, r10                                        @ d0  = y_coeff
    vld1.16             {d1}, [r8]                                     @ d1  = *table
    sub                 r3, r3, r0, lsl #2                             @ r3  = linesize  - width * 4 (padding)
    sub                 r5, r5, r0                                     @ r5  = linesizeY - width     (paddingY)
    sub                 r7, r7, r0, lsr #1                             @ r7  = linesizeU - width / 2 (paddingU)
    sub                 r12,r12,r0, lsr #1                             @ r12 = linesizeV - width / 2 (paddingV)
    ldr                 r10,[sp, #120]                                 @ r10 = srcV
.endm

.macro load_chroma_nv12
    pld [r12, #64*3]

    vld2.8              {d2, d3}, [r6]!                                @ q1: interleaved chroma line
    vsubl.u8            q14, d2, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d3, d10                                   @ q15 = V - 128
.endm

.macro load_chroma_nv21
    pld [r12, #64*3]

    vld2.8              {d2, d3}, [r6]!                                @ q1: interleaved chroma line
    vsubl.u8            q14, d3, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d2, d10                                   @ q15 = V - 128
.endm

.macro load_chroma_yuv420p
    pld [r10, #64*3]
    pld [r12, #64*3]

    vld1.8              d2, [r6]!                                      @ d2: chroma red line
    vld1.8              d3, [r10]!                                     @ d3: chroma blue line
    vsubl.u8            q14, d2, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d3, d10                                   @ q15 = V - 128
.endm

.macro load_chroma_yuv422p
    pld [r10, #64*3]

    vld1.8              d2, [r6]!                                      @ d2: chroma red line
    vld1.8              d3, [r10]!                                     @ d3: chroma blue line
    vsubl.u8            q14, d2, d10                                   @ q14 = U - 128
    vsubl.u8            q15, d3, d10                                   @ q15 = V - 128
.endm

.macro increment_and_test_nv12
    add                 r11, r11, r3                                   @ dst2  += padding
    add                 r12, r12, r5                                   @ srcY2 += paddingY
    add                 r6, r6, r7                                     @ srcC  += paddingC
    subs                r1, r1, #2                                     @ height -= 2
.endm

.macro increment_and_test_nv21
    increment_and_test_nv12
.endm

.macro increment_and_test_yuv420p
    add                 r11, r11, r3                                   @ dst2  += padding
    add                 r12, r12, r5                                   @ srcY2 += paddingY
    ldr                 r7, [sp, #116]                                 @ r7     = linesizeU
    sub                 r7, r7, r0, lsr #1                             @ r7     = linesizeU - width / 2 (paddingU)
    add                 r6, r6, r7                                     @ srcU  += paddingU
    ldr                 r7, [sp, #124]                                 @ r7     = linesizeV
    sub                 r7, r7, r0, lsr #1                             @ r7     = linesizeV - width / 2 (paddingV)
    add                 r10, r10, r7                                   @ srcV  += paddingV
    subs                r1, r1, #2                                     @ height -= 2
.endm

.macro increment_and_test_yuv422p
    add                 r6, r6, r7                                     @ srcU  += paddingU
    add                 r10,r10,r12                                    @ srcV  += paddingV
    subs                r1, r1, #1                                     @ height -= 1
.endm

.macro process_nv12 ofmt
    process_2l_16px \ofmt
.endm

.macro process_nv21 ofmt
    process_2l_16px \ofmt
.endm

.macro process_yuv420p ofmt
    process_2l_16px \ofmt
.endm

.macro process_yuv422p ofmt
    process_1l_16px \ofmt
.endm

.macro declare_func ifmt ofmt
function ff_\ifmt\()_to_\ofmt\()_neon, export=1
    load_args_\ifmt
1:
    mov                 r8, r0                                         @ r8 = width
2:
    pld [r6, #64*3]
    pld [r4, #64*3]
    vmov.i8             d10, #128
    load_chroma_\ifmt
    process_\ifmt \ofmt
    subs                r8, r8, #16                                    @ width -= 16
    bgt                 2b
    add                 r2, r2, r3                                     @ dst   += padding
    add                 r4, r4, r5                                     @ srcY  += paddingY
    increment_and_test_\ifmt
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
declare_rgb_funcs yuv420p
declare_rgb_funcs yuv422p
