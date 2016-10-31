/*
 * Copyright (c) 2014 - 2015 Seppo Tomperi <seppo.tomperi@vtt.fi>
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
#include "neon.S"

#define MAX_PB_SIZE #64

.macro regshuffle_d8
    vmov d16, d17
    vmov d17, d18
    vmov d18, d19
    vmov d19, d20
    vmov d20, d21
    vmov d21, d22
    vmov d22, d23
.endm

.macro regshuffle_q8
    vmov q0, q1
    vmov q1, q2
    vmov q2, q3
    vmov q3, q4
    vmov q4, q5
    vmov q5, q6
    vmov q6, q7
.endm

.macro vextin8
        pld       [r2]
        vld1.8    {q11}, [r2], r3
        vext.8    d16, d22, d23, #1
        vext.8    d17, d22, d23, #2
        vext.8    d18, d22, d23, #3
        vext.8    d19, d22, d23, #4
        vext.8    d20, d22, d23, #5
        vext.8    d21, d22, d23, #6
        vext.8    d22, d22, d23, #7
.endm

.macro loadin8
        pld       [r2]
        vld1.8    {d16}, [r2], r3
        pld       [r2]
        vld1.8    {d17}, [r2], r3
        pld       [r2]
        vld1.8    {d18}, [r2], r3
        pld       [r2]
        vld1.8    {d19}, [r2], r3
        pld       [r2]
        vld1.8    {d20}, [r2], r3
        pld       [r2]
        vld1.8    {d21}, [r2], r3
        pld       [r2]
        vld1.8    {d22}, [r2], r3
        pld       [r2]
        vld1.8    {d23}, [r2], r3
.endm

.macro qpel_filter_1_32b
        vmov.i16   d16, #58
        vmov.i16   d17, #10
        vmull.s16   q9, d6, d16   // 58 * d0
        vmull.s16  q10, d7, d16   // 58 * d1
        vmov.i16   d16, #17
        vmull.s16  q11, d4, d17   // 10 * c0
        vmull.s16  q12, d5, d17   // 10 * c1
        vmov.i16   d17, #5
        vmull.s16  q13, d8, d16   // 17 * e0
        vmull.s16  q14, d9, d16   // 17 * e1
        vmull.s16  q15, d10, d17  //  5 * f0
        vmull.s16   q8, d11, d17  //  5 * f1
        vsub.s32    q9, q11       // 58 * d0 - 10 * c0
        vsub.s32   q10, q12       // 58 * d1 - 10 * c1
        vshll.s16  q11, d2, #2    // 4 * b0
        vshll.s16  q12, d3, #2    // 4 * b1
        vadd.s32    q9, q13       // 58 * d0 - 10 * c0 + 17 * e0
        vadd.s32   q10, q14       // 58 * d1 - 10 * c1 + 17 * e1
        vsubl.s16  q13, d12, d0   // g0 - a0
        vsubl.s16  q14, d13, d1   // g1 - a1
        vadd.s32    q9, q11       // 58 * d0 - 10 * c0 + 17 * e0 + 4 * b0
        vadd.s32   q10, q12       // 58 * d1 - 10 * c1 + 17 * e1 + 4 * b1
        vsub.s32   q13, q15       // g0 - a0 - 5 * f0
        vsub.s32   q14, q8        // g1 - a1 - 5 * f1
        vadd.s32    q9, q13       // 58 * d0 - 10 * c0 + 17 * e0 + 4 * b0 + g0 - a0 - 5 * f0
        vadd.s32   q10, q14       // 58 * d1 - 10 * c1 + 17 * e1 + 4 * b1 + g1 - a1 - 5 * f1
        vqshrn.s32  d16, q9, #6
        vqshrn.s32  d17, q10, #6
.endm

// input  q0 - q7
// output q8
.macro qpel_filter_2_32b
        vmov.i32   q8, #11
        vaddl.s16   q9, d6, d8   // d0 + e0
        vaddl.s16  q10, d7, d9   // d1 + e1
        vaddl.s16  q11, d4, d10  // c0 + f0
        vaddl.s16  q12, d5, d11  // c1 + f1
        vmul.s32   q11, q8       // 11 * (c0 + f0)
        vmul.s32   q12, q8       // 11 * (c1 + f1)
        vmov.i32   q8, #40
        vaddl.s16  q15, d2, d12  // b0 + g0
        vmul.s32    q9, q8       // 40 * (d0 + e0)
        vmul.s32   q10, q8       // 40 * (d1 + e1)
        vaddl.s16   q8, d3, d13  // b1 + g1
        vaddl.s16  q13, d0, d14  // a0 + h0
        vaddl.s16  q14, d1, d15  // a1 + h1
        vshl.s32   q15, #2       // 4*(b0+g0)
        vshl.s32    q8, #2       // 4*(b1+g1)
        vadd.s32   q11, q13      // 11 * (c0 + f0) + a0 + h0
        vadd.s32   q12, q14      // 11 * (c1 + f1) + a1 + h1
        vadd.s32   q9, q15       // 40 * (d0 + e0) + 4*(b0+g0)
        vadd.s32   q10, q8       // 40 * (d1 + e1) + 4*(b1+g1)
        vsub.s32   q9, q11       // 40 * (d0 + e0) + 4*(b0+g0) - (11 * (c0 + f0) + a0 + h0)
        vsub.s32   q10, q12      // 40 * (d1 + e1) + 4*(b1+g1) - (11 * (c1 + f1) + a1 + h1)
        vqshrn.s32  d16, q9, #6
        vqshrn.s32  d17, q10, #6
.endm

.macro qpel_filter_3_32b
        vmov.i16   d16, #58
        vmov.i16   d17, #10
        vmull.s16   q9, d8, d16   // 58 * d0
        vmull.s16  q10, d9, d16   // 58 * d1
        vmov.i16   d16, #17
        vmull.s16  q11, d10, d17  // 10 * c0
        vmull.s16  q12, d11, d17  // 10 * c1
        vmov.i16   d17, #5
        vmull.s16  q13, d6, d16   // 17 * e0
        vmull.s16  q14, d7, d16   // 17 * e1
        vmull.s16  q15, d4, d17   //  5 * f0
        vmull.s16   q8, d5, d17   //  5 * f1
        vsub.s32    q9, q11       // 58 * d0 - 10 * c0
        vsub.s32   q10, q12       // 58 * d1 - 10 * c1
        vshll.s16  q11, d12, #2   // 4 * b0
        vshll.s16  q12, d13, #2   // 4 * b1
        vadd.s32    q9, q13       // 58 * d0 - 10 * c0 + 17 * e0
        vadd.s32   q10, q14       // 58 * d1 - 10 * c1 + 17 * e1
        vsubl.s16  q13, d2, d14   // g0 - a0
        vsubl.s16  q14, d3, d15   // g1 - a1
        vadd.s32    q9, q11       // 58 * d0 - 10 * c0 + 17 * e0 + 4 * b0
        vadd.s32   q10, q12       // 58 * d1 - 10 * c1 + 17 * e1 + 4 * b1
        vsub.s32   q13, q15       // g0 - a0 - 5 * f0
        vsub.s32   q14, q8        // g1 - a1 - 5 * f1
        vadd.s32    q9, q13       // 58 * d0 - 10 * c0 + 17 * e0 + 4 * b0 + g0 - a0 - 5 * f0
        vadd.s32   q10, q14       // 58 * d1 - 10 * c1 + 17 * e1 + 4 * b1 + g1 - a1 - 5 * f1
        vqshrn.s32  d16, q9, #6
        vqshrn.s32  d17, q10, #6
.endm

.macro qpel_filter_1 out=q7
        vmov.u8    d24, #58
        vmov.u8    d25, #10
        vshll.u8   q13, d20, #4   // 16*e
        vshll.u8   q14, d21, #2   // 4*f
        vmull.u8  \out, d19, d24  // 58*d
        vaddw.u8   q13, q13, d20  // 17*e
        vmull.u8   q15, d18, d25  // 10*c
        vaddw.u8   q14, q14, d21  // 5*f
        vsubl.u8   q12, d22, d16  // g - a
        vadd.u16  \out, q13       // 58d + 17e
        vshll.u8   q13, d17, #2   // 4*b
        vadd.u16   q15, q14       // 10*c + 5*f
        vadd.s16   q13, q12       // - a + 4*b + g
        vsub.s16  \out, q15       // -10*c + 58*d + 17*e -5*f
        vadd.s16  \out, q13       // -a + 4*b -10*c + 58*d + 17*e -5*f
.endm

.macro qpel_filter_2 out=q7
        vmov.i16   q12, #10
        vmov.i16   q14, #11
        vaddl.u8   q13, d19, d20   // d + e
        vaddl.u8   q15, d18, d21   // c + f
        vmul.u16   q13, q12        // 10 * (d+e)
        vmul.u16   q15, q14        // 11 * ( c + f)
        vaddl.u8  \out, d17, d22   // b + g
        vaddl.u8   q12, d16, d23   // a + h
        vadd.u16  \out, q13        // b + 10 * (d + e) + g
        vadd.s16   q12, q15
        vshl.u16  \out, #2         // 4 * (b + 10 * (d + e) + g)
        vsub.s16  \out, q12
.endm

.macro qpel_filter_3 out=q7
        vmov.u8    d24, #58
        vmov.u8    d25, #10
        vshll.u8   q13, d19, #4     // 16*e
        vshll.u8   q14, d18, #2     // 4*f
        vmull.u8  \out, d20, d24    // 58*d
        vaddw.u8   q13, q13, d19    // 17*e
        vmull.u8   q15, d21, d25    // 10*c
        vaddw.u8   q14, q14, d18    // 5*f
        vsubl.u8   q12, d17, d23    // g - a
        vadd.u16  \out, q13         // 58d + 17e
        vshll.u8   q13, d22, #2     // 4*b
        vadd.u16   q15, q14         // 10*c + 5*f
        vadd.s16   q13, q12         // - a + 4*b + g
        vsub.s16  \out, q15         // -10*c + 58*d + 17*e -5*f
        vadd.s16  \out, q13         // -a + 4*b -10*c + 58*d + 17*e -5*f
.endm

.macro  hevc_put_qpel_vX_neon_8 filter
        push   {r4, r5, r6, r7}
        ldr    r4, [sp, #16] // height
        ldr    r5, [sp, #20] // width
        vpush {d8-d15}
        sub       r2, r2, r3, lsl #1
        sub       r2, r3
        mov       r12, r4
        mov       r6, r0
        mov       r7, r2
        lsl       r1, #1
0:      loadin8
        cmp       r5, #4
        beq       4f
8:      subs r4, #1
        \filter
        vst1.16    {q7}, [r0], r1
        regshuffle_d8
        vld1.8    {d23}, [r2], r3
        bne 8b
        subs  r5, #8
        beq       99f
        mov r4, r12
        add r6, #16
        mov r0, r6
        add r7, #8
        mov r2, r7
        b     0b
4:      subs r4, #1
        \filter
        vst1.16    d14, [r0], r1
        regshuffle_d8
        vld1.32    {d23[0]}, [r2], r3
        bne 4b
99:     vpop {d8-d15}
        pop {r4, r5, r6, r7}
        bx lr
.endm

.macro  hevc_put_qpel_uw_vX_neon_8 filter
        push   {r4-r10}
        ldr    r5, [sp, #28] // width
        ldr    r4, [sp, #32] // height
        ldr    r8, [sp, #36] // src2
        ldr    r9, [sp, #40] // src2stride
        vpush {d8-d15}
        sub       r2, r2, r3, lsl #1
        sub       r2, r3
        mov       r12, r4
        mov       r6, r0
        mov       r7, r2
        cmp       r8, #0
        bne       .Lbi\@
0:      loadin8
        cmp       r5, #4
        beq       4f
8:      subs r4, #1
        \filter
        vqrshrun.s16   d0, q7, #6
        vst1.8    d0, [r0], r1
        regshuffle_d8
        vld1.8    {d23}, [r2], r3
        bne 8b
        subs  r5, #8
        beq       99f
        mov r4, r12
        add r6, #8
        mov r0, r6
        add r7, #8
        mov r2, r7
        b     0b
4:      subs r4, #1
        \filter
        vqrshrun.s16   d0, q7, #6
        vst1.32    d0[0], [r0], r1
        regshuffle_d8
        vld1.32    {d23[0]}, [r2], r3
        bne 4b
        b   99f
.Lbi\@: lsl       r9, #1
        mov       r10, r8
0:      loadin8
        cmp       r5, #4
        beq       4f
8:      subs r4, #1
        \filter
        vld1.16        {q0}, [r8], r9
        vqadd.s16      q0, q7
        vqrshrun.s16   d0, q0, #7
        vst1.8         d0, [r0], r1
        regshuffle_d8
        vld1.8    {d23}, [r2], r3
        bne 8b
        subs  r5, #8
        beq       99f
        mov r4, r12
        add r6, #8
        mov r0, r6
        add r10, #16
        mov r8, r10
        add r7, #8
        mov r2, r7
        b     0b
4:      subs r4, #1
        \filter
        vld1.16      d0, [r8], r9
        vqadd.s16    d0, d14
        vqrshrun.s16 d0, q0, #7
        vst1.32      d0[0], [r0], r1
        regshuffle_d8
        vld1.32    {d23[0]}, [r2], r3
        bne 4b
99:     vpop {d8-d15}
        pop {r4-r10}
        bx lr
.endm

function ff_hevc_put_qpel_v1_neon_8, export=1
        hevc_put_qpel_vX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_v2_neon_8, export=1
        hevc_put_qpel_vX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_v3_neon_8, export=1
        hevc_put_qpel_vX_neon_8 qpel_filter_3
endfunc


function ff_hevc_put_qpel_uw_v1_neon_8, export=1
        hevc_put_qpel_uw_vX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_uw_v2_neon_8, export=1
        hevc_put_qpel_uw_vX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_uw_v3_neon_8, export=1
        hevc_put_qpel_uw_vX_neon_8 qpel_filter_3
endfunc

.macro hevc_put_qpel_hX_neon_8 filter
        push     {r4, r5, r6, r7}
        ldr    r4, [sp, #16] // height
        ldr    r5, [sp, #20] // width

        vpush    {d8-d15}
        sub       r2, #4
        lsl       r1, #1
        mov      r12, r4
        mov       r6, r0
        mov       r7, r2
        cmp       r5, #4
        beq       4f
8:      subs      r4, #1
        vextin8
        \filter
        vst1.16   {q7}, [r0], r1
        bne       8b
        subs      r5, #8
        beq      99f
        mov       r4, r12
        add       r6, #16
        mov       r0, r6
        add       r7, #8
        mov       r2, r7
        cmp       r5, #4
        bne       8b
4:      subs      r4, #1
        vextin8
        \filter
        vst1.16  d14, [r0], r1
        bne       4b
99:     vpop     {d8-d15}
        pop      {r4, r5, r6, r7}
        bx lr
.endm

.macro hevc_put_qpel_uw_hX_neon_8 filter
        push     {r4-r10}
        ldr       r5, [sp, #28] // width
        ldr       r4, [sp, #32] // height
        ldr       r8, [sp, #36] // src2
        ldr       r9, [sp, #40] // src2stride
        vpush    {d8-d15}
        sub       r2, #4
        mov      r12, r4
        mov       r6, r0
        mov       r7, r2
        cmp       r8, #0
        bne       .Lbi\@
        cmp       r5, #4
        beq       4f
8:      subs      r4, #1
        vextin8
        \filter
        vqrshrun.s16   d0, q7, #6
        vst1.8    d0, [r0], r1
        bne       8b
        subs      r5, #8
        beq      99f
        mov       r4, r12
        add       r6, #8
        mov       r0, r6
        add       r7, #8
        mov       r2, r7
        cmp       r5, #4
        bne       8b
4:      subs      r4, #1
        vextin8
        \filter
        vqrshrun.s16   d0, q7, #6
        vst1.32  d0[0], [r0], r1
        bne       4b
        b         99f
.Lbi\@:
        lsl       r9, #1
        cmp       r5, #4
        beq       4f
        mov       r10, r8
8:      subs      r4, #1
        vextin8
        \filter
        vld1.16        {q0}, [r8], r9
        vqadd.s16      q0, q7
        vqrshrun.s16   d0, q0, #7
        vst1.8         d0, [r0], r1
        bne       8b
        subs      r5, #8
        beq      99f
        mov       r4, r12
        add       r6, #8
        add       r10, #16
        mov       r8, r10
        mov       r0, r6
        add       r7, #8
        mov       r2, r7
        cmp       r5, #4
        bne       8b
4:      subs      r4, #1
        vextin8
        \filter
        vld1.16      d0, [r8], r9
        vqadd.s16    d0, d14
        vqrshrun.s16 d0, q0, #7
        vst1.32      d0[0], [r0], r1
        bne       4b
99:     vpop     {d8-d15}
        pop      {r4-r10}
        bx lr
.endm

function ff_hevc_put_qpel_h1_neon_8, export=1
        hevc_put_qpel_hX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_h2_neon_8, export=1
        hevc_put_qpel_hX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_h3_neon_8, export=1
        hevc_put_qpel_hX_neon_8 qpel_filter_3
endfunc


function ff_hevc_put_qpel_uw_h1_neon_8, export=1
        hevc_put_qpel_uw_hX_neon_8 qpel_filter_1
endfunc

function ff_hevc_put_qpel_uw_h2_neon_8, export=1
        hevc_put_qpel_uw_hX_neon_8 qpel_filter_2
endfunc

function ff_hevc_put_qpel_uw_h3_neon_8, export=1
        hevc_put_qpel_uw_hX_neon_8 qpel_filter_3
endfunc

.macro hevc_put_qpel_hXvY_neon_8 filterh filterv
        push   {r4, r5, r6, r7}
        ldr    r4, [sp, #16] // height
        ldr    r5, [sp, #20] // width

        vpush {d8-d15}
        sub       r2, #4
        sub       r2, r2, r3, lsl #1
        sub       r2, r3  // extra_before 3
        lsl       r1, #1
        mov       r12, r4
        mov       r6, r0
        mov       r7, r2
0:      vextin8
        \filterh q0
        vextin8
        \filterh q1
        vextin8
        \filterh q2
        vextin8
        \filterh q3
        vextin8
        \filterh q4
        vextin8
        \filterh q5
        vextin8
        \filterh q6
        vextin8
        \filterh q7
        cmp r5, #4
        beq 4f
8:      subs  r4, #1
        \filterv
        vst1.16    {q8}, [r0], r1
        regshuffle_q8
        vextin8
        \filterh q7
        bne 8b
        subs  r5, #8
        beq 99f
        mov r4, r12
        add r6, #16
        mov r0, r6
        add r7, #8
        mov r2, r7
        b 0b
4:      subs  r4, #1
        \filterv
        vst1.16    d16, [r0], r1
        regshuffle_q8
        vextin8
        \filterh q7
        bne 4b
99:     vpop {d8-d15}
        pop {r4, r5, r6, r7}
        bx lr
.endm

.macro hevc_put_qpel_uw_hXvY_neon_8 filterh filterv
        push     {r4-r10}
        ldr       r5, [sp, #28] // width
        ldr       r4, [sp, #32] // height
        ldr       r8, [sp, #36] // src2
        ldr       r9, [sp, #40] // src2stride
        vpush {d8-d15}
        sub       r2, #4
        sub       r2, r2, r3, lsl #1
        sub       r2, r3  // extra_before 3
        mov       r12, r4
        mov       r6, r0
        mov       r7, r2
        cmp       r8, #0
        bne       .Lbi\@
0:      vextin8
        \filterh q0
        vextin8
        \filterh q1
        vextin8
        \filterh q2
        vextin8
        \filterh q3
        vextin8
        \filterh q4
        vextin8
        \filterh q5
        vextin8
        \filterh q6
        vextin8
        \filterh q7
        cmp r5, #4
        beq 4f
8:      subs  r4, #1
        \filterv
        vqrshrun.s16   d0, q8, #6
        vst1.8    d0, [r0], r1
        regshuffle_q8
        vextin8
        \filterh q7
        bne 8b
        subs  r5, #8
        beq 99f
        mov r4, r12
        add r6, #8
        mov r0, r6
        add r7, #8
        mov r2, r7
        b 0b
4:      subs  r4, #1
        \filterv
        vqrshrun.s16   d0, q8, #6
        vst1.32        d0[0], [r0], r1
        regshuffle_q8
        vextin8
        \filterh q7
        bne 4b
        b   99f
.Lbi\@: lsl      r9, #1
        mov      r10, r8
0:      vextin8
        \filterh q0
        vextin8
        \filterh q1
        vextin8
        \filterh q2
        vextin8
        \filterh q3
        vextin8
        \filterh q4
        vextin8
        \filterh q5
        vextin8
        \filterh q6
        vextin8
        \filterh q7
        cmp r5, #4
        beq 4f
8:      subs  r4, #1
        \filterv
        vld1.16        {q0}, [r8], r9
        vqadd.s16      q0, q8
        vqrshrun.s16   d0, q0, #7
        vst1.8         d0, [r0], r1
        regshuffle_q8
        vextin8
        \filterh q7
        bne 8b
        subs  r5, #8
        beq 99f
        mov r4, r12
        add r6, #8
        mov r0, r6
        add r10, #16
        mov r8, r10
        add r7, #8
        mov r2, r7
        b 0b
4:      subs  r4, #1
        \filterv
        vld1.16      d0, [r8], r9
        vqadd.s16    d0, d16
        vqrshrun.s16 d0, q0, #7
        vst1.32      d0[0], [r0], r1
        regshuffle_q8
        vextin8
        \filterh q7
        bne 4b
99:     vpop {d8-d15}
        pop {r4-r10}
        bx lr
.endm


function ff_hevc_put_qpel_h1v1_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_1 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_h2v1_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_2 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_h3v1_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_3 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_h1v2_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_1 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_h2v2_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_2 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_h3v2_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_3 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_h1v3_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_1 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_h2v3_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_2 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_h3v3_neon_8, export=1
        hevc_put_qpel_hXvY_neon_8 qpel_filter_3 qpel_filter_3_32b
endfunc


function ff_hevc_put_qpel_uw_h1v1_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_1 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_h2v1_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_2 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_h3v1_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_3 qpel_filter_1_32b
endfunc

function ff_hevc_put_qpel_uw_h1v2_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_1 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_h2v2_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_2 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_h3v2_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_3 qpel_filter_2_32b
endfunc

function ff_hevc_put_qpel_uw_h1v3_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_1 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_h2v3_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_2 qpel_filter_3_32b
endfunc

function ff_hevc_put_qpel_uw_h3v3_neon_8, export=1
        hevc_put_qpel_uw_hXvY_neon_8 qpel_filter_3 qpel_filter_3_32b
endfunc

.macro init_put_pixels
        pld    [r1]
        pld    [r1, r2]
        mov    r12, MAX_PB_SIZE
        lsl    r12, #1
.endm

function ff_hevc_put_pixels_w2_neon_8, export=1
        init_put_pixels
        vmov.u8      d5, #255
        vshr.u64     d5, #32
0:      subs r3, #1
        vld1.32     {d0[0]}, [r1], r2
        pld [r1]
        vld1.32     d6, [r0]
        vshll.u8    q0, d0, #6
        vbit        d6, d0, d5
        vst1.32     d6, [r0], r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w4_neon_8, export=1
        init_put_pixels
0:      subs r3, #2
        vld1.32   {d0[0]}, [r1], r2
        vld1.32   {d0[1]}, [r1], r2
        pld       [r1]
        pld       [r1, r2]
        vshll.u8   q0, d0, #6
        vst1.64   {d0}, [r0], r12
        vst1.64   {d1}, [r0], r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w6_neon_8, export=1
        init_put_pixels
        vmov.u8      q10, #255
        vshr.u64     d21, #32
0:      subs r3, #1
        vld1.16     {d0}, [r1], r2
        pld [r1]
        vshll.u8    q0, d0, #6
        vld1.8      {q12}, [r0]
        vbit        q12, q0, q10
        vst1.8      {q12}, [r0], r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w8_neon_8, export=1
        init_put_pixels
0:      subs r3, #2
        vld1.8   {d0}, [r1], r2
        vld1.8   {d2}, [r1], r2
        pld        [r1]
        pld        [r1, r2]
        vshll.u8   q0, d0, #6
        vshll.u8   q1, d2, #6
        vst1.16   {q0}, [r0], r12
        vst1.16   {q1}, [r0], r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w12_neon_8, export=1
        init_put_pixels
0:      subs r3, #2
        vld1.64    {d0}, [r1]
        add       r1, #8
        vld1.32   {d1[0]}, [r1], r2
        sub       r1, #8
        vld1.64    {d2}, [r1]
        add       r1, #8
        vld1.32   {d1[1]}, [r1], r2
        sub       r1, #8
        pld       [r1]
        pld       [r1, r2]
        vshll.u8  q8, d0, #6
        vshll.u8  q9, d1, #6
        vshll.u8  q10, d2, #6
        vmov      d22, d19
        vst1.64   {d16, d17, d18}, [r0], r12
        vst1.64   {d20, d21, d22}, [r0], r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w16_neon_8, export=1
        init_put_pixels
0:      subs r3, #2
        vld1.8   {q0}, [r1], r2
        vld1.8   {q1}, [r1], r2
        pld       [r1]
        pld       [r1, r2]
        vshll.u8  q8, d0, #6
        vshll.u8  q9, d1, #6
        vshll.u8  q10, d2, #6
        vshll.u8  q11, d3, #6
        vst1.8    {q8, q9}, [r0], r12
        vst1.8    {q10, q11}, [r0], r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w24_neon_8, export=1
        init_put_pixels
0:      subs r3, #1
        vld1.8   {d0, d1, d2}, [r1], r2
        pld       [r1]
        vshll.u8  q10, d0, #6
        vshll.u8  q11, d1, #6
        vshll.u8  q12, d2, #6
        vstm     r0, {q10, q11, q12}
        add      r0, r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w32_neon_8, export=1
        init_put_pixels
0:      subs r3, #1
        vld1.8 {q0, q1}, [r1], r2
        pld       [r1]
        vshll.u8  q8, d0, #6
        vshll.u8  q9, d1, #6
        vshll.u8  q10, d2, #6
        vshll.u8  q11, d3, #6
        vstm    r0, {q8, q9, q10, q11}
        add     r0, r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w48_neon_8, export=1
        init_put_pixels
0:      subs r3, #1
        vld1.8    {q0, q1}, [r1]
        add r1, #32
        vld1.8    {q2}, [r1], r2
        sub r1, #32
        pld       [r1]
        vshll.u8  q8, d0, #6
        vshll.u8  q9, d1, #6
        vshll.u8  q10, d2, #6
        vshll.u8  q11, d3, #6
        vshll.u8  q12, d4, #6
        vshll.u8  q13, d5, #6
        vstm r0, {q8, q9, q10, q11, q12, q13}
        add  r0, r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_pixels_w64_neon_8, export=1
        init_put_pixels
0:      subs r3, #1
        vld1.8    {q0, q1}, [r1]
        add      r1, #32
        vld1.8    {q2, q3}, [r1], r2
        sub      r1, #32
        pld       [r1]
        vshll.u8  q8, d0, #6
        vshll.u8  q9, d1, #6
        vshll.u8  q10, d2, #6
        vshll.u8  q11, d3, #6
        vshll.u8  q12, d4, #6
        vshll.u8  q13, d5, #6
        vshll.u8  q14, d6, #6
        vshll.u8  q15, d7, #6
        vstm    r0, {q8, q9, q10, q11, q12, q13, q14, q15}
        add r0, r12
        bne 0b
        bx lr
endfunc

function ff_hevc_put_qpel_uw_pixels_neon_8, export=1
        push   {r4-r9}
        ldr    r5, [sp, #24] // width
        ldr    r4, [sp, #28] // height
        ldr    r8, [sp, #32] // src2
        ldr    r9, [sp, #36] // src2stride
        vpush {d8-d15}
        cmp    r8, #0
        bne    2f
1:      subs r4, #1
        vld1.8     {d0}, [r2], r3
        vst1.8      d0, [r0], r1
        bne 1b
        vpop {d8-d15}
        pop   {r4-r9}
        bx lr
2:      subs  r4, #1
        vld1.8         {d0}, [r2], r3
        vld1.16        {q1}, [r8], r9
        vshll.u8       q0, d0, #6
        vqadd.s16      q0, q1
        vqrshrun.s16   d0, q0, #7
        vst1.8      d0, [r0], r1
        bne 2b
        vpop {d8-d15}
        pop   {r4-r9}
        bx lr
endfunc

.macro put_qpel_uw_pixels width, regs, regs2, regs3, regs4
function ff_hevc_put_qpel_uw_pixels_w\width\()_neon_8, export=1
        ldr    r12, [sp] // height
1:      subs   r12, #4
        vld1.32     {\regs}  , [r2], r3
        vld1.32     {\regs2} , [r2], r3
        vld1.32     {\regs3} , [r2], r3
        vld1.32     {\regs4} , [r2], r3
        vst1.32     {\regs}  , [r0], r1
        vst1.32     {\regs2} , [r0], r1
        vst1.32     {\regs3} , [r0], r1
        vst1.32     {\regs4} , [r0], r1
        bne 1b
        bx lr
endfunc
.endm

.macro put_qpel_uw_pixels_m width, regs, regs2, regs3, regs4
function ff_hevc_put_qpel_uw_pixels_w\width\()_neon_8, export=1
        push   {r4-r5}
        ldr    r12, [sp, #8] // height
1:      subs r12, #2
        mov      r4, r2
        vld1.32   {\regs} , [r2]!
        vld1.32   {\regs2} , [r2]
        add      r2, r4, r3
        mov      r4, r2
        vld1.32   {\regs3} , [r2]!
        vld1.32   {\regs4} , [r2]
        add      r2, r4, r3
        mov      r5, r0
        vst1.32   {\regs} , [r0]!
        vst1.32   {\regs2} , [r0]
        add      r0, r5, r1
        mov      r5, r0
        vst1.32   {\regs3} , [r0]!
        vst1.32   {\regs4} , [r0]
        add      r0, r5, r1
        bne 1b
        pop   {r4-r5}
        bx lr
endfunc
.endm

put_qpel_uw_pixels    4, d0[0], d0[1], d1[0], d1[1]
put_qpel_uw_pixels    8, d0,    d1,    d2,    d3
put_qpel_uw_pixels_m 12, d0,    d1[0], d2,    d3[0]
put_qpel_uw_pixels   16, q0,    q1,    q2,    q3
put_qpel_uw_pixels   24, d0-d2, d3-d5, d16-d18, d19-d21
put_qpel_uw_pixels   32, q0-q1, q2-q3, q8-q9, q10-q11
put_qpel_uw_pixels_m 48, q0-q1, q2,    q8-q9, q10
put_qpel_uw_pixels_m 64, q0-q1, q2-q3, q8-q9, q10-q11
