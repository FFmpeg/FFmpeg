/*
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/arm/asm.S"

#include "asm-offsets.h"

.macro resample_one     fmt, es=2
function ff_resample_one_\fmt\()_neon, export=1
        push            {r4, r5}
        add             r1, r1, r2, lsl #\es

        ldr             r2, [r0, #PHASE_SHIFT+4] /* phase_mask */
        ldr             ip, [sp, #8] /* index */
        ldr             r5, [r0, #FILTER_LENGTH]
        and             r2, ip, r2 /* (index & phase_mask) */
        ldr             r4, [r0, #PHASE_SHIFT]
        lsr             r4, ip, r4 /* compute sample_index */
        mul             r2, r2, r5

        ldr             ip, [r0, #FILTER_BANK]
        add             r3, r3, r4, lsl #\es /* &src[sample_index] */

        cmp             r5, #8
        add             r0, ip, r2, lsl #\es /* filter = &filter_bank[...] */

        blt             5f
8:
        subs            r5, r5, #8
        LOAD4
        MUL4
7:
        LOAD4
        beq             6f
        cmp             r5, #8
        MLA4
        blt             4f
        subs            r5, r5, #8
        LOAD4
        MLA4
        b               7b
6:
        MLA4
        STORE
        pop             {r4, r5}
        bx              lr
5:
        INIT4
4:      /* remaining filter_length 1 to 7 */
        cmp             r5, #4
        blt             2f
        subs            r5, r5, #4
        LOAD4
        MLA4
        beq             0f
2:      /* remaining filter_length 1 to 3 */
        cmp             r5, #2
        blt             1f
        subs            r5, r5, #2
        LOAD2
        MLA2
        beq             0f
1:      /* remaining filter_length 1 */
        LOAD1
        MLA1
0:
        STORE
        pop             {r4, r5}
        bx              lr
endfunc

.purgem LOAD1
.purgem LOAD2
.purgem LOAD4
.purgem MLA1
.purgem MLA2
.purgem MLA4
.purgem MUL4
.purgem INIT4
.purgem STORE
.endm


/* float32 */
.macro  LOAD1
        veor.32         d0, d0
        vld1.32         {d0[0]}, [r0]! /* load filter */
        vld1.32         {d4[0]}, [r3]! /* load src */
.endm
.macro  LOAD2
        vld1.32         {d0}, [r0]! /* load filter */
        vld1.32         {d4}, [r3]! /* load src */
.endm
.macro  LOAD4
        vld1.32         {d0,d1}, [r0]! /* load filter */
        vld1.32         {d4,d5}, [r3]! /* load src */
.endm
.macro  MLA1
        vmla.f32        d16, d0, d4[0]
.endm
.macro  MLA2
        vmla.f32        d16, d0, d4
.endm
.macro  MLA4
        vmla.f32        d16, d0, d4
        vmla.f32        d17, d1, d5
.endm
.macro  MUL4
        vmul.f32        d16, d0, d4
        vmul.f32        d17, d1, d5
.endm
.macro  INIT4
        veor.f32        q8, q8
.endm
.macro  STORE
        vpadd.f32       d16, d16, d17
        vpadd.f32       d16, d16, d16
        vst1.32         d16[0], [r1]
.endm

resample_one flt, 2


/* s32 */
.macro  LOAD1
        veor.32         d0, d0
        vld1.32         {d0[0]}, [r0]! /* load filter */
        vld1.32         {d4[0]}, [r3]! /* load src */
.endm
.macro  LOAD2
        vld1.32         {d0}, [r0]! /* load filter */
        vld1.32         {d4}, [r3]! /* load src */
.endm
.macro  LOAD4
        vld1.32         {d0,d1}, [r0]! /* load filter */
        vld1.32         {d4,d5}, [r3]! /* load src */
.endm
.macro  MLA1
        vmlal.s32       q8, d0, d4[0]
.endm
.macro  MLA2
        vmlal.s32       q8, d0, d4
.endm
.macro  MLA4
        vmlal.s32       q8, d0, d4
        vmlal.s32       q9, d1, d5
.endm
.macro  MUL4
        vmull.s32       q8, d0, d4
        vmull.s32       q9, d1, d5
.endm
.macro  INIT4
        veor.s64        q8, q8
        veor.s64        q9, q9
.endm
.macro  STORE
        vadd.s64        q8, q8, q9
        vadd.s64        d16, d16, d17
        vqrshrn.s64     d16, q8, #30
        vst1.32         d16[0], [r1]
.endm

resample_one s32, 2


/* s16 */
.macro  LOAD1
        veor.16         d0, d0
        vld1.16         {d0[0]}, [r0]! /* load filter */
        vld1.16         {d4[0]}, [r3]! /* load src */
.endm
.macro  LOAD2
        veor.16         d0, d0
        vld1.32         {d0[0]}, [r0]! /* load filter */
        veor.16         d4, d4
        vld1.32         {d4[0]}, [r3]! /* load src */
.endm
.macro  LOAD4
        vld1.16         {d0}, [r0]! /* load filter */
        vld1.16         {d4}, [r3]! /* load src */
.endm
.macro  MLA1
        vmlal.s16       q8, d0, d4[0]
.endm
.macro  MLA2
        vmlal.s16       q8, d0, d4
.endm
.macro  MLA4
        vmlal.s16       q8, d0, d4
.endm
.macro  MUL4
        vmull.s16       q8, d0, d4
.endm
.macro  INIT4
        veor.s32        q8, q8
.endm
.macro  STORE
        vpadd.s32       d16, d16, d17
        vpadd.s32       d16, d16, d16
        vqrshrn.s32     d16, q8, #15
        vst1.16         d16[0], [r1]
.endm

resample_one s16, 1


.macro resample_linear  fmt, es=2
function ff_resample_linear_\fmt\()_neon, export=1
        push            {r4, r5}
        add             r1, r1, r2, lsl #\es

        ldr             r2, [r0, #PHASE_SHIFT+4] /* phase_mask */
        ldr             ip, [sp, #8] /* index */
        ldr             r5, [r0, #FILTER_LENGTH]
        and             r2, ip, r2 /* (index & phase_mask) */
        ldr             r4, [r0, #PHASE_SHIFT]
        lsr             r4, ip, r4 /* compute sample_index */
        mul             r2, r2, r5

        ldr             ip, [r0, #FILTER_BANK]
        add             r3, r3, r4, lsl #\es /* &src[sample_index] */

        cmp             r5, #8
        ldr             r4, [r0, #SRC_INCR]
        add             r0, ip, r2, lsl #\es /* filter = &filter_bank[...] */
        add             r2, r0, r5, lsl #\es /* filter[... + c->filter_length] */

        blt             5f
8:
        subs            r5, r5, #8
        LOAD4
        MUL4
7:
        LOAD4
        beq             6f
        cmp             r5, #8
        MLA4
        blt             4f
        subs            r5, r5, #8
        LOAD4
        MLA4
        b               7b
6:
        MLA4
        STORE
        pop             {r4, r5}
        bx              lr
5:
        INIT4
4:      /* remaining filter_length 1 to 7 */
        cmp             r5, #4
        blt             2f
        subs            r5, r5, #4
        LOAD4
        MLA4
        beq             0f
2:      /* remaining filter_length 1 to 3 */
        cmp             r5, #2
        blt             1f
        subs            r5, r5, #2
        LOAD2
        MLA2
        beq             0f
1:      /* remaining filter_length 1 */
        LOAD1
        MLA1
0:
        STORE
        pop             {r4, r5}
        bx              lr
endfunc

.purgem LOAD1
.purgem LOAD2
.purgem LOAD4
.purgem MLA1
.purgem MLA2
.purgem MLA4
.purgem MUL4
.purgem INIT4
.purgem STORE
.endm


/* float32 linear */
.macro  LOAD1
        veor.32         d0, d0
        veor.32         d2, d2
        vld1.32         {d0[0]}, [r0]! /* load filter */
        vld1.32         {d2[0]}, [r2]! /* load filter */
        vld1.32         {d4[0]}, [r3]! /* load src */
.endm
.macro  LOAD2
        vld1.32         {d0}, [r0]! /* load filter */
        vld1.32         {d2}, [r2]! /* load filter */
        vld1.32         {d4}, [r3]! /* load src */
.endm
.macro  LOAD4
        vld1.32         {d0,d1}, [r0]! /* load filter */
        vld1.32         {d2,d3}, [r2]! /* load filter */
        vld1.32         {d4,d5}, [r3]! /* load src */
.endm
.macro  MLA1
        vmla.f32        d18, d0, d4[0]
        vmla.f32        d16, d2, d4[0]
.endm
.macro  MLA2
        vmla.f32        d18, d0, d4
        vmla.f32        d16, d2, d4
.endm
.macro  MLA4
        vmla.f32        q9, q0, q2
        vmla.f32        q8, q1, q2
.endm
.macro  MUL4
        vmul.f32        q9, q0, q2
        vmul.f32        q8, q1, q2
.endm
.macro  INIT4
        veor.f32        q9, q9
        veor.f32        q8, q8
.endm
.macro  STORE
        vldr            s0, [sp, #12] /* frac */
        vmov            s1, r4
        vcvt.f32.s32    d0, d0

        vsub.f32        q8, q8, q9 /* v2 - val */
        vpadd.f32       d18, d18, d19
        vpadd.f32       d16, d16, d17
        vpadd.f32       d2, d18, d18
        vpadd.f32       d1, d16, d16

        vmul.f32        s2, s2, s0 /* (v2 - val) * frac */
        vdiv.f32        s2, s2, s1 /* / c->src_incr */
        vadd.f32        s4, s4, s2

        vstr            s4, [r1]
.endm

resample_linear flt, 2
