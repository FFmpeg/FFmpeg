/*
 * Copyright (c) 2014 Seppo Tomperi <seppo.tomperi@vtt.fi>
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

function ff_hevc_idct_4x4_dc_neon_8, export=1
        ldrsh       r1, [r0]
        ldr         r2, =0x20
        add         r1, #1
        asr         r1, #1
        add         r1, r2
        asr         r1, #6
        vdup.16     q0, r1
        vdup.16     q1, r1
        vst1.16     {q0, q1}, [r0]
        bx lr
endfunc

function ff_hevc_idct_8x8_dc_neon_8, export=1
        ldrsh       r1, [r0]
        ldr         r2, =0x20
        add         r1, #1
        asr         r1, #1
        add         r1, r2
        asr         r1, #6
        vdup.16     q8, r1
        vdup.16     q9, r1
        vmov.16     q10, q8
        vmov.16     q11, q8
        vmov.16     q12, q8
        vmov.16     q13, q8
        vmov.16     q14, q8
        vmov.16     q15, q8
        vstm        r0, {q8-q15}
        bx lr
endfunc

function ff_hevc_idct_16x16_dc_neon_8, export=1
        ldrsh       r1, [r0]
        ldr         r2, =0x20
        add         r1, #1
        asr         r1, #1
        add         r1, r2
        asr         r1, #6
        vdup.16     q8, r1
        vdup.16     q9, r1
        vmov.16     q10, q8
        vmov.16     q11, q8
        vmov.16     q12, q8
        vmov.16     q13, q8
        vmov.16     q14, q8
        vmov.16     q15, q8
        vstm        r0!, {q8-q15}
        vstm        r0!, {q8-q15}
        vstm        r0!, {q8-q15}
        vstm        r0, {q8-q15}
        bx lr
endfunc

function ff_hevc_idct_32x32_dc_neon_8, export=1
        ldrsh       r1, [r0]
        ldr         r2, =0x20
        add         r1, #1
        asr         r1, #1
        add         r1, r2
        asr         r1, #6
        mov         r3, #16
        vdup.16     q8, r1
        vdup.16     q9, r1
        vmov.16     q10, q8
        vmov.16     q11, q8
        vmov.16     q12, q8
        vmov.16     q13, q8
        vmov.16     q14, q8
        vmov.16     q15, q8
1:      subs        r3, #1
        vstm        r0!, {q8-q15}
        bne         1b
        bx lr
endfunc

function ff_hevc_add_residual_4x4_neon_8, export=1
        vldm        r1, {q0-q1}
        vld1.32     d4[0], [r0], r2
        vld1.32     d4[1], [r0], r2
        vld1.32     d5[0], [r0], r2
        vld1.32     d5[1], [r0], r2
        sub         r0, r0, r2, lsl #2
        vmovl.u8    q8, d4
        vmovl.u8    q9, d5
        vqadd.s16   q0, q0, q8
        vqadd.s16   q1, q1, q9
        vqmovun.s16 d0, q0
        vqmovun.s16 d1, q1
        vst1.32     d0[0], [r0], r2
        vst1.32     d0[1], [r0], r2
        vst1.32     d1[0], [r0], r2
        vst1.32     d1[1], [r0], r2
        bx          lr
endfunc

function ff_hevc_add_residual_8x8_neon_8, export=1
        mov         r3,   #8
1:      subs        r3,   #1
        vld1.16     {q0}, [r1]!
        vld1.8      d16,  [r0]
        vmovl.u8    q8,   d16
        vqadd.s16   q0,   q8
        vqmovun.s16 d0,   q0
        vst1.32     d0,   [r0], r2
        bne         1b
        bx          lr
endfunc

function ff_hevc_add_residual_16x16_neon_8, export=1
        mov         r3,   #16
1:      subs        r3,   #1
        vld1.16     {q0, q1}, [r1]!
        vld1.8      {q8},  [r0]
        vmovl.u8    q9,  d16
        vmovl.u8    q10, d17
        vqadd.s16   q0,  q9
        vqadd.s16   q1,  q10
        vqmovun.s16 d0,  q0
        vqmovun.s16 d1,  q1
        vst1.8      {q0},   [r0], r2
        bne         1b
        bx          lr
endfunc

function ff_hevc_add_residual_32x32_neon_8, export=1
        mov         r3,   #32
1:      subs        r3,   #1
        vldm        r1!, {q0-q3}
        vld1.8      {q8, q9},  [r0]
        vmovl.u8    q10, d16
        vmovl.u8    q11, d17
        vmovl.u8    q12, d18
        vmovl.u8    q13, d19
        vqadd.s16   q0,  q10
        vqadd.s16   q1,  q11
        vqadd.s16   q2,  q12
        vqadd.s16   q3,  q13
        vqmovun.s16 d0,  q0
        vqmovun.s16 d1,  q1
        vqmovun.s16 d2,  q2
        vqmovun.s16 d3,  q3
        vst1.8     {q0, q1},   [r0], r2
        bne         1b
        bx          lr
endfunc

.macro  transpose_16b_8x8   r0, r1, r2, r3, r4, r5, r6, r7
        vtrn.64         \r0, \r4
        vtrn.64         \r1, \r5
        vtrn.64         \r2, \r6
        vtrn.64         \r3, \r7
        vtrn.32         \r0, \r2
        vtrn.32         \r1, \r3
        vtrn.32         \r4, \r6
        vtrn.32         \r5, \r7
        vtrn.16         \r0, \r1
        vtrn.16         \r2, \r3
        vtrn.16         \r4, \r5
        vtrn.16         \r6, \r7
.endm

// in 4 q regs
// output 8 d regs
.macro transpose_16b_4x4    r0, r1, r2, r3
        vtrn.32         \r0, \r2
        vtrn.32         \r1, \r3
        vtrn.16         \r0, \r1
        vtrn.16         \r2, \r3
.endm

/* uses registers q2 - q9 for temp values */
/* TODO: reorder */
.macro tr4_luma_shift r0, r1, r2, r3, shift
        vaddl.s16   q5, \r0, \r2    // c0 = src0 + src2
        vaddl.s16   q2, \r2, \r3    // c1 = src2 + src3
        vsubl.s16   q4, \r0, \r3    // c2 = src0 - src3
        vmull.s16   q6, \r1, d0[0]  // c3 = 74 * src1

        vaddl.s16   q7, \r0, \r3    // src0 + src3
        vsubw.s16   q7, q7, \r2     // src0 - src2 + src3
        vmul.s32    q7, q7, d0[0]   // dst2 = 74 * (src0 - src2 + src3)

        vmul.s32    q8, q5, d0[1]   // 29 * c0
        vmul.s32    q9, q2, d1[0]   // 55 * c1
        vadd.s32    q8, q9          // 29 * c0 + 55 * c1
        vadd.s32    q8, q6          // dst0 = 29 * c0 + 55 * c1 + c3

        vmul.s32    q2, q2, d0[1]   // 29 * c1
        vmul.s32    q9, q4, d1[0]   // 55 * c2
        vsub.s32    q9, q2          // 55 * c2 - 29 * c1
        vadd.s32    q9, q6          // dst1 = 55 * c2 - 29 * c1 + c3

        vmul.s32    q5, q5, d1[0]   // 55 * c0
        vmul.s32    q4, q4, d0[1]   // 29 * c2
        vadd.s32    q5, q4          // 55 * c0 + 29 * c2
        vsub.s32    q5, q6          // dst3 = 55 * c0 + 29 * c2 - c3

        vqrshrn.s32   \r0, q8, \shift
        vqrshrn.s32   \r1, q9, \shift
        vqrshrn.s32   \r2, q7, \shift
        vqrshrn.s32   \r3, q5, \shift
.endm

/* uses registers q2 - q6 for temp values */
.macro tr4 r0, r1, r2, r3
        vmull.s16  q4, \r1, d0[0]   // 83 * src1
        vmull.s16  q6, \r1, d0[1]   // 36 * src1
        vshll.s16  q2, \r0, #6   // 64 * src0
        vshll.s16  q3, \r2, #6   // 64 * src2
        vadd.s32   q5, q2, q3    // 64 * (src0 + src2)     e0
        vsub.s32   q2, q2, q3    // 64 * (src0 - src2)     e1
        vmlal.s16  q4, \r3, d0[1]   // 83 * src1 + 36 * src3  o0
        vmlsl.s16  q6, \r3, d0[0]   // 36 * src1 - 83 * src3  o1

        vsub.s32   q3, q5, q4    // e0 - o0
        vadd.s32   q4, q5, q4    // e0 + o0
        vadd.s32   q5, q2, q6    // e1 + o1
        vsub.s32   q6, q2, q6    // e1 - o1
.endm

.macro tr4_shift r0, r1, r2, r3, shift
        vmull.s16  q4, \r1, d0[0]   // 83 * src1
        vmull.s16  q6, \r1, d0[1]   // 36 * src1
        vshll.s16  q2, \r0, #6   // 64 * src0
        vshll.s16  q3, \r2, #6   // 64 * src2
        vadd.s32   q5, q2, q3    // 64 * (src0 + src2)     e0
        vsub.s32   q2, q2, q3    // 64 * (src0 - src2)     e1
        vmlal.s16  q4, \r3, d0[1]   // 83 * src1 + 36 * src3  o0
        vmlsl.s16  q6, \r3, d0[0]   // 36 * src1 - 83 * src3  o1

        vsub.s32   q3, q5, q4    // e0 - o0
        vadd.s32   q4, q5, q4    // e0 + o0
        vadd.s32   q5, q2, q6    // e1 + o1
        vsub.s32   q6, q2, q6    // e1 - o1

        vqrshrn.s32   \r0, q4, \shift
        vqrshrn.s32   \r1, q5, \shift
        vqrshrn.s32   \r2, q6, \shift
        vqrshrn.s32   \r3, q3, \shift
.endm

function ff_hevc_transform_4x4_neon_8, export=1
        vpush       {d8-d15}
        vld1.16     {q14, q15}, [r0]  // coeffs
        ldr         r3, =0x00240053 // 36 and 83
        vmov.32     d0[0], r3

        tr4_shift d28, d29, d30, d31, #7

        vtrn.16     d28, d29
        vtrn.16     d30, d31
        vtrn.32     q14, q15

        tr4_shift d28, d29, d30, d31, #12

        vtrn.16     d28, d29
        vtrn.16     d30, d31
        vtrn.32     q14, q15

        vst1.16     {q14, q15}, [r0]
        vpop        {d8-d15}
        bx lr
endfunc

function ff_hevc_transform_luma_4x4_neon_8, export=1
        vpush       {d8-d15}
        vld1.16     {q14, q15}, [r0]  // coeffs
        ldr         r3, =0x4a  // 74
        vmov.32     d0[0], r3
        ldr         r3, =0x1d  // 29
        vmov.32     d0[1], r3
        ldr         r3, =0x37  // 55
        vmov.32     d1[0], r3

        tr4_luma_shift d28, d29, d30, d31, #7

        vtrn.16     d28, d29
        vtrn.16     d30, d31
        vtrn.32     q14, q15

        tr4_luma_shift d28, d29, d30, d31, #12

        vtrn.16     d28, d29
        vtrn.16     d30, d31
        vtrn.32     q14, q15
        vst1.16     {q14, q15}, [r0]
        vpop        {d8-d15}
        bx lr
endfunc

.macro tr8_begin in0, in1, in2, in3
        vmull.s16  q7, \in0, d1[1]   // 89 * src1
        vmull.s16  q8, \in0, d1[0]   // 75 * src1
        vmull.s16  q9, \in0, d1[3]   // 50 * src1
        vmull.s16  q10, \in0, d1[2]  // 18 * src1

        vmlal.s16  q7, \in1, d1[0]   // 75 * src3
        vmlsl.s16  q8, \in1, d1[2]   //-18 * src3
        vmlsl.s16  q9, \in1, d1[1]   //-89 * src3
        vmlsl.s16  q10, \in1, d1[3]  //-50 * src3

        vmlal.s16  q7, \in2, d1[3]   // 50 * src5
        vmlsl.s16  q8, \in2, d1[1]   //-89 * src5
        vmlal.s16  q9, \in2, d1[2]   // 18 * src5
        vmlal.s16  q10, \in2, d1[0]  // 75 * src5

        vmlal.s16  q7, \in3, d1[2]   // 18 * src7
        vmlsl.s16  q8, \in3, d1[3]   //-50 * src7
        vmlal.s16  q9, \in3, d1[0]   // 75 * src7
        vmlsl.s16  q10, \in3, d1[1]  //-89 * src7
.endm

.macro tr8_end shift
        vadd.s32   q1, q4, q7   //  e_8[0] + o_8[0], dst[0]
        vsub.s32   q4, q4, q7   //  e_8[0] - o_8[0], dst[7]

        vadd.s32   q2, q5, q8   // e_8[1] + o_8[1], dst[1]
        vsub.s32   q5, q5, q8   // e_8[1] - o_8[1], dst[6]

        vadd.s32   q11, q6, q9  // e_8[2] + o_8[2], dst[2]
        vsub.s32    q6, q6, q9  // e_8[2] - o_8[2], dst[5]

        vadd.s32   q12, q3, q10 // e_8[3] + o_8[3], dst[3]
        vsub.s32   q3, q3, q10  // e_8[3] - o_8[3], dst[4]
        vqrshrn.s32   d2, q1, \shift
        vqrshrn.s32   d3, q2, \shift
        vqrshrn.s32   d4, q11, \shift
        vqrshrn.s32   d5, q12, \shift
        vqrshrn.s32   d6, q3, \shift
        vqrshrn.s32   d7, q6, \shift
        vqrshrn.s32   d9, q4, \shift
        vqrshrn.s32   d8, q5, \shift
.endm

function ff_hevc_transform_8x8_neon_8, export=1
        push   {r4-r8}
        vpush {d8-d15}
        mov    r5, #16

        adr       r3, tr4f
        vld1.16   {d0, d1}, [r3]

        // left half
        vld1.16 {d24}, [r0], r5
        vld1.16 {d25}, [r0], r5
        vld1.16 {d26}, [r0], r5
        vld1.16 {d27}, [r0], r5
        vld1.16 {d28}, [r0], r5
        vld1.16 {d29}, [r0], r5
        vld1.16 {d30}, [r0], r5
        vld1.16 {d31}, [r0], r5
        sub      r0, #128
        tr8_begin d25, d27, d29, d31
        tr4       d24, d26, d28, d30
        tr8_end   #7
        vst1.16 {d2}, [r0], r5
        vst1.16 {d3}, [r0], r5
        vst1.16 {d4}, [r0], r5
        vst1.16 {d5}, [r0], r5
        vst1.16 {d6}, [r0], r5
        vst1.16 {d7}, [r0], r5
        vst1.16 {d8}, [r0], r5
        vst1.16 {d9}, [r0], r5
        sub      r0, #128
        //skip right half if col_limit in r1 is less than 4
        cmp      r1, #4
        blt      1f
        //right half
        add      r0, #8
        vld1.16 {d24}, [r0], r5
        vld1.16 {d25}, [r0], r5
        vld1.16 {d26}, [r0], r5
        vld1.16 {d27}, [r0], r5
        vld1.16 {d28}, [r0], r5
        vld1.16 {d29}, [r0], r5
        vld1.16 {d30}, [r0], r5
        vld1.16 {d31}, [r0], r5
        sub      r0, #128
        tr8_begin d25, d27, d29, d31
        tr4       d24, d26, d28, d30
        tr8_end   #7
        vst1.16 {d2}, [r0], r5
        vst1.16 {d3}, [r0], r5
        vst1.16 {d4}, [r0], r5
        vst1.16 {d5}, [r0], r5
        vst1.16 {d6}, [r0], r5
        vst1.16 {d7}, [r0], r5
        vst1.16 {d8}, [r0], r5
        vst1.16 {d9}, [r0], r5
        sub      r0, #136
1:
        // top half
        vldm r0, {q12-q15} // coeffs
        transpose_16b_4x4 d24, d26, d28, d30
        transpose_16b_4x4 d25, d27, d29, d31
        tr8_begin d26, d30, d27, d31
        tr4 d24, d28, d25, d29
        tr8_end #12
        transpose_16b_4x4 d2, d3, d4, d5
        transpose_16b_4x4 d6, d7, d8, d9
        vswp     d7, d5
        vswp     d7, d8
        vswp     d3, d6
        vswp     d6, d4
        vstm r0!, {q1-q4}

        // bottom half
        vldm r0, {q12-q15} // coeffs
        transpose_16b_4x4 d24, d26, d28, d30
        transpose_16b_4x4 d25, d27, d29, d31
        tr8_begin d26, d30, d27, d31
        tr4 d24, d28, d25, d29
        tr8_end #12
        transpose_16b_4x4 d2, d3, d4, d5
        transpose_16b_4x4 d6, d7, d8, d9
        vswp     d7, d5
        vswp     d7, d8
        vswp     d3, d6
        vswp     d6, d4
        //vstm     r0, {q1-q4}
        vst1.16 {q1-q2}, [r0]
        add     r0, #32
        vst1.16 {q3-q4}, [r0]
        sub     r0, #32
        vpop {d8-d15}
        pop {r4-r8}
        bx lr
endfunc

.align 4
tr4f:
.word 0x00240053  // 36 and d1[0] = 83
.word 0x00000000
tr8f:
.word 0x0059004b  // 89, d0[0] = 75
.word 0x00320012  // 50, d0[2] = 18
tr16:
.word 0x005a0057  // 90, d2[0] = 87
.word 0x00500046  // 80, d2[2] = 70
.word 0x0039002b  // 57, d2[0] = 43
.word 0x00190009  // 25, d2[2] = 9
