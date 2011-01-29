/*
 * Copyright (c) 2010 Mans Rullgard
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

#include "asm.S"
#include "asm-offsets.h"

function ff_dct_unquantize_h263_inter_neon, export=1
        add             r12, r0,  #BLOCK_LAST_INDEX
        ldr             r12, [r12, r2, lsl #2]
        add             r0,  r0,  #INTER_SCANTAB_RASTER_END
        ldrb            r12, [r0, r12]
        sub             r2,  r3,  #1
        lsl             r0,  r3,  #1
        orr             r2,  r2,  #1
        add             r3,  r12, #1
endfunc

function ff_dct_unquantize_h263_neon, export=1
        vdup.16         q15, r0                 @ qmul
        vdup.16         q14, r2                 @ qadd
        vneg.s16        q13, q14
        cmp             r3,  #4
        mov             r0,  r1
        ble             2f
1:
        vld1.16         {q0},     [r0,:128]!
        vclt.s16        q3,  q0,  #0
        vld1.16         {q8},     [r0,:128]!
        vceq.s16        q1,  q0,  #0
        vmul.s16        q2,  q0,  q15
        vclt.s16        q11, q8,  #0
        vmul.s16        q10, q8,  q15
        vbsl            q3,  q13, q14
        vbsl            q11, q13, q14
        vadd.s16        q2,  q2,  q3
        vceq.s16        q9,  q8,  #0
        vadd.s16        q10, q10, q11
        vbif            q0,  q2,  q1
        vbif            q8,  q10, q9
        subs            r3,  r3,  #16
        vst1.16         {q0},     [r1,:128]!
        vst1.16         {q8},     [r1,:128]!
        bxle            lr
        cmp             r3,  #8
        bgt             1b
2:
        vld1.16         {d0},     [r0,:64]
        vclt.s16        d3,  d0,  #0
        vceq.s16        d1,  d0,  #0
        vmul.s16        d2,  d0,  d30
        vbsl            d3,  d26, d28
        vadd.s16        d2,  d2,  d3
        vbif            d0,  d2,  d1
        vst1.16         {d0},     [r1,:64]
        bx              lr
endfunc

function ff_dct_unquantize_h263_intra_neon, export=1
        push            {r4-r6,lr}
        add             r12, r0,  #BLOCK_LAST_INDEX
        ldr             r6,  [r0, #AC_PRED]
        add             lr,  r0,  #INTER_SCANTAB_RASTER_END
        cmp             r6,  #0
        movne           r12, #63
        bne             1f
        ldr             r12, [r12, r2, lsl #2]
        ldrb            r12, [lr, r12]
1:      ldr             r5,  [r0, #H263_AIC]
        ldrsh           r4,  [r1]
        cmp             r5,  #0
        mov             r5,  r1
        movne           r2,  #0
        bne             2f
        cmp             r2,  #4
        addge           r0,  r0,  #4
        sub             r2,  r3,  #1
        ldr             r6,  [r0, #Y_DC_SCALE]
        orr             r2,  r2,  #1
        smulbb          r4,  r4,  r6
2:      lsl             r0,  r3,  #1
        add             r3,  r12, #1
        bl              ff_dct_unquantize_h263_neon
        vmov.16         d0[0], r4
        vst1.16         {d0[0]},  [r5]
        pop             {r4-r6,pc}
endfunc
