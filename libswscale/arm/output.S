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

function ff_yuv2planeX_8_neon, export=1
    push {r4-r12, lr}
    vpush {q4-q7}
    ldr                 r4, [sp, #104]                                 @ dstW
    ldr                 r5, [sp, #108]                                 @ dither
    ldr                 r6, [sp, #112]                                 @ offset
    vld1.8              {d0}, [r5]                                     @ load 8x8-bit dither values
    cmp                 r6, #0                                         @ check offsetting which can be 0 or 3 only
    beq                 1f
    vext.u8             d0, d0, d0, #3                                 @ honor offseting which can be 3 only
1:  vmovl.u8            q0, d0                                         @ extend dither to 16-bit
    vshll.u16           q1, d0, #12                                    @ extend dither to 32-bit with left shift by 12 (part 1)
    vshll.u16           q2, d1, #12                                    @ extend dither to 32-bit with left shift by 12 (part 2)
    mov                 r7, #0                                         @ i = 0
2:  vmov.u8             q3, q1                                         @ initialize accumulator with dithering values (part 1)
    vmov.u8             q4, q2                                         @ initialize accumulator with dithering values (part 2)
    mov                 r8, r1                                         @ tmpFilterSize = filterSize
    mov                 r9, r2                                         @ srcp
    mov                 r10, r0                                        @ filterp
3:  ldr                 r11, [r9], #4                                  @ get pointer @ src[j]
    ldr                 r12, [r9], #4                                  @ get pointer @ src[j+1]
    add                 r11, r11, r7, lsl #1                           @ &src[j][i]
    add                 r12, r12, r7, lsl #1                           @ &src[j+1][i]
    vld1.16             {q5}, [r11]                                    @ read 8x16-bit @ src[j  ][i + {0..7}]: A,B,C,D,E,F,G,H
    vld1.16             {q6}, [r12]                                    @ read 8x16-bit @ src[j+1][i + {0..7}]: I,J,K,L,M,N,O,P
    ldr                 r11, [r10], #4                                 @ read 2x16-bit coeffs (X, Y) at (filter[j], filter[j+1])
    vmov.16             q7, q5                                         @ copy 8x16-bit @ src[j  ][i + {0..7}] for following inplace zip instruction
    vmov.16             q8, q6                                         @ copy 8x16-bit @ src[j+1][i + {0..7}] for following inplace zip instruction
    vzip.16             q7, q8                                         @ A,I,B,J,C,K,D,L,E,M,F,N,G,O,H,P
    vdup.32             q15, r11                                       @ X,Y,X,Y,X,Y,X,Y
    vmull.s16           q9, d14, d30                                   @ A*X,I*Y,B*X,J*Y
    vmull.s16           q10, d15, d31                                  @ C*X,K*Y,D*X,L*Y
    vmull.s16           q11, d16, d30                                  @ E*X,M*Y,F*X,N*Y
    vmull.s16           q12, d17, d31                                  @ G*X,O*Y,H*X,P*Y
    vpadd.s32           d10, d18, d19                                  @ A*X+I*Y,B*X+J*Y
    vpadd.s32           d11, d20, d21                                  @ C*X+K*Y,D*X+L*Y
    vpadd.s32           d12, d22, d23                                  @ E*X+M*Y,F*X+N*Y
    vpadd.s32           d13, d24, d25                                  @ G*X+O*Y,H*X+P*Y
    vadd.s32            q3, q5                                         @ update val accumulator (part 1)
    vadd.s32            q4, q6                                         @ update val accumulator (part 2)
    subs                r8, #2                                         @ tmpFilterSize -= 2
    bgt                 3b                                             @ loop until filterSize is consumed
    vshr.s32            q3, q3, #19                                    @ val>>19 (part 1)
    vshr.s32            q4, q4, #19                                    @ val>>19 (part 2)
    vqmovun.s32         d6, q3                                         @ clip16(val>>19) (part 1)
    vqmovun.s32         d7, q4                                         @ clip16(val>>19) (part 2)
    vqmovn.u16          d6, q3                                         @ merge part 1 and part 2
    vst1.8              {d6}, [r3]!                                    @ write destination
    add                 r7, #8                                         @ i += 8
    subs                r4, r4, #8                                     @ dstW -= 8
    bgt                 2b                                             @ loop until width is consumed
    vpop                {q4-q7}
    pop                 {r4-r12, lr}
    mov                 pc, lr
endfunc
