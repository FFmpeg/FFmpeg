/*
 * Copyright (c) 2016 Clément Bœsch <clement stupeflix.com>
 * Copyright (c) 2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

function ff_hscale_8_to_15_neon, export=1
    push                {r4-r12, lr}
    vpush               {q4-q7}
    ldr                 r4, [sp, #104]                                 @ filter
    ldr                 r5, [sp, #108]                                 @ filterPos
    ldr                 r6, [sp, #112]                                 @ filterSize
    add                 r10, r4, r6, lsl #1                            @ filter2 = filter + filterSize * 2
1:  ldr                 r8, [r5], #4                                   @ filterPos[0]
    ldr                 r9, [r5], #4                                   @ filterPos[1]
    vmov.s32            q4, #0                                         @ val accumulator
    vmov.s32            q5, #0                                         @ val accumulator
    mov                 r7, r6                                         @ tmpfilterSize = filterSize
    mov                 r0, r3                                         @ srcp
2:  add                 r11, r0, r8                                    @ srcp + filterPos[0]
    add                 r12, r0, r9                                    @ srcp + filterPos[1]
    vld1.8              d0, [r11]                                      @ srcp[filterPos[0] + {0..7}]
    vld1.8              d2, [r12]                                      @ srcp[filterPos[1] + {0..7}]
    vld1.16             {q2}, [r4]!                                    @ load 8x16-bit filter values
    vld1.16             {q3}, [r10]!                                   @ load 8x16-bit filter values
    vmovl.u8            q0, d0                                         @ unpack src values to 16-bit
    vmovl.u8            q1, d2                                         @ unpack src values to 16-bit
    vmull.s16           q8, d0, d4                                     @ srcp[filterPos[0] + {0..7}] * filter[{0..7}] (part 1)
    vmull.s16           q9, d1, d5                                     @ srcp[filterPos[0] + {0..7}] * filter[{0..7}] (part 2)
    vmull.s16           q10, d2, d6                                    @ srcp[filterPos[1] + {0..7}] * filter[{0..7}] (part 1)
    vmull.s16           q11, d3, d7                                    @ srcp[filterPos[1] + {0..7}] * filter[{0..7}] (part 2)
    vpadd.s32           d16, d16, d17                                  @ horizontal pair adding of the 8x32-bit multiplied values into 4x32-bit (part 1)
    vpadd.s32           d17, d18, d19                                  @ horizontal pair adding of the 8x32-bit multiplied values into 4x32-bit (part 2)
    vpadd.s32           d20, d20, d21                                  @ horizontal pair adding of the 8x32-bit multiplied values into 4x32-bit (part 1)
    vpadd.s32           d21, d22, d23                                  @ horizontal pair adding of the 8x32-bit multiplied values into 4x32-bit (part 2)
    vadd.s32            q4, q8                                         @ update val accumulator
    vadd.s32            q5, q10                                        @ update val accumulator
    add                 r0, #8                                         @ srcp += 8
    subs                r7, #8                                         @ tmpfilterSize -= 8
    bgt                 2b                                             @ loop until tmpfilterSize is consumed
    mov                 r4, r10                                        @ filter = filter2
    add                 r10, r10, r6, lsl #1                           @ filter2 += filterSize * 2
    vpadd.s32           d8, d8, d9                                     @ horizontal pair adding of the 8x32-bit sums into 4x32-bit (part 1)
    vpadd.s32           d9, d10, d11                                   @ horizontal pair adding of the 8x32-bit sums into 4x32-bit (part 2)
    vpadd.s32           d8, d8, d9                                     @ horizontal pair adding of the 4x32-bit sums into 2x32-bit
    vqshrn.s32          d8, q4, #7                                     @ shift and clip the 2x16-bit final values
    vst1.32             {d8[0]},[r1]!                                  @ write destination
    subs                r2, #2                                         @ dstW -= 2
    bgt                 1b                                             @ loop until end of line
    vpop                {q4-q7}
    pop                 {r4-r12, pc}
endfunc
