/*
 * Copyright (c) 2016 Google Inc.
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

@ All public functions in this file have the following signature:
@ typedef void (*vp9_mc_func)(uint8_t *dst, ptrdiff_t dst_stride,
@                            const uint8_t *ref, ptrdiff_t ref_stride,
@                            int h, int mx, int my);

function ff_vp9_copy64_neon, export=1
        ldr             r12, [sp]
        sub             r1,  r1,  #32
        sub             r3,  r3,  #32
1:
        vld1.8          {q0,  q1},  [r2]!
        vst1.8          {q0,  q1},  [r0, :128]!
        vld1.8          {q2,  q3},  [r2], r3
        subs            r12, r12, #1
        vst1.8          {q2,  q3},  [r0, :128], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_avg64_neon, export=1
        push            {lr}
        ldr             r12, [sp, #4]
        sub             r1,  r1,  #32
        sub             r3,  r3,  #32
        mov             lr,  r0
1:
        vld1.8          {q8,  q9},  [r2]!
        vld1.8          {q0,  q1},  [r0, :128]!
        vld1.8          {q10, q11}, [r2], r3
        vrhadd.u8       q0,  q0,  q8
        vld1.8          {q2,  q3},  [r0, :128], r1
        vrhadd.u8       q1,  q1,  q9
        vrhadd.u8       q2,  q2,  q10
        vst1.8          {q0,  q1},  [lr, :128]!
        vrhadd.u8       q3,  q3,  q11
        vst1.8          {q2,  q3},  [lr, :128], r1
        subs            r12, r12, #1
        bne             1b
        pop             {pc}
endfunc

function ff_vp9_copy32_neon, export=1
        ldr             r12, [sp]
1:
        vld1.8          {q0,  q1},  [r2], r3
        subs            r12, r12, #1
        vst1.8          {q0,  q1},  [r0, :128], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_avg32_neon, export=1
        ldr             r12, [sp]
1:
        vld1.8          {q2,  q3},  [r2], r3
        vld1.8          {q0,  q1},  [r0, :128]
        vrhadd.u8       q0,  q0,  q2
        vrhadd.u8       q1,  q1,  q3
        subs            r12, r12, #1
        vst1.8          {q0,  q1},  [r0, :128], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_copy16_neon, export=1
        push            {r4,lr}
        ldr             r12, [sp, #8]
        add             r4,  r0,  r1
        add             lr,  r2,  r3
        add             r1,  r1,  r1
        add             r3,  r3,  r3
1:
        vld1.8          {q0},  [r2], r3
        vld1.8          {q1},  [lr], r3
        subs            r12, r12, #2
        vst1.8          {q0},  [r0, :128], r1
        vst1.8          {q1},  [r4, :128], r1
        bne             1b
        pop             {r4,pc}
endfunc

function ff_vp9_avg16_neon, export=1
        push            {lr}
        ldr             r12, [sp, #4]
        mov             lr,  r0
1:
        vld1.8          {q2},  [r2], r3
        vld1.8          {q0},  [r0, :128], r1
        vld1.8          {q3},  [r2], r3
        vrhadd.u8       q0,  q0,  q2
        vld1.8          {q1},  [r0, :128], r1
        vrhadd.u8       q1,  q1,  q3
        subs            r12, r12, #2
        vst1.8          {q0},  [lr, :128], r1
        vst1.8          {q1},  [lr, :128], r1
        bne             1b
        pop             {pc}
endfunc

function ff_vp9_copy8_neon, export=1
        ldr             r12, [sp]
1:
        vld1.8          {d0},  [r2], r3
        vld1.8          {d1},  [r2], r3
        subs            r12, r12, #2
        vst1.8          {d0},  [r0, :64], r1
        vst1.8          {d1},  [r0, :64], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_avg8_neon, export=1
        ldr             r12, [sp]
1:
        vld1.8          {d2},  [r2], r3
        vld1.8          {d0},  [r0, :64], r1
        vld1.8          {d3},  [r2], r3
        vrhadd.u8       d0,  d0,  d2
        vld1.8          {d1},  [r0, :64]
        sub             r0,  r0,  r1
        vrhadd.u8       d1,  d1,  d3
        subs            r12, r12, #2
        vst1.8          {d0},  [r0, :64], r1
        vst1.8          {d1},  [r0, :64], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_copy4_neon, export=1
        ldr             r12, [sp]
1:
        vld1.32         {d0[]},   [r2], r3
        vld1.32         {d1[]},   [r2], r3
        vst1.32         {d0[0]},  [r0, :32], r1
        vld1.32         {d2[]},   [r2], r3
        vst1.32         {d1[0]},  [r0, :32], r1
        vld1.32         {d3[]},   [r2], r3
        subs            r12, r12, #4
        vst1.32         {d2[0]},  [r0, :32], r1
        vst1.32         {d3[0]},  [r0, :32], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_avg4_neon, export=1
        push            {lr}
        ldr             r12, [sp, #4]
        mov             lr,  r0
1:
        vld1.32         {d4[]},   [r2], r3
        vld1.32         {d0[]},   [r0, :32], r1
        vld1.32         {d5[]},   [r2], r3
        vrhadd.u8       d0,  d0,  d4
        vld1.32         {d1[]},   [r0, :32], r1
        vld1.32         {d6[]},   [r2], r3
        vrhadd.u8       d1,  d1,  d5
        vld1.32         {d2[]},   [r0, :32], r1
        vld1.32         {d7[]},   [r2], r3
        vrhadd.u8       d2,  d2,  d6
        vld1.32         {d3[]},   [r0, :32], r1
        subs            r12, r12, #4
        vst1.32         {d0[0]},  [lr, :32], r1
        vrhadd.u8       d3,  d3,  d7
        vst1.32         {d1[0]},  [lr, :32], r1
        vst1.32         {d2[0]},  [lr, :32], r1
        vst1.32         {d3[0]},  [lr, :32], r1
        bne             1b
        pop             {pc}
endfunc

@ Helper macros for vmul/vmla with a constant from either d0 or d1 depending on index
.macro vmul_lane dst, src, idx
.if \idx < 4
       vmul.s16         \dst, \src, d0[\idx]
.else
       vmul.s16         \dst, \src, d1[\idx - 4]
.endif
.endm
.macro vmla_lane dst, src, idx
.if \idx < 4
       vmla.s16         \dst, \src, d0[\idx]
.else
       vmla.s16         \dst, \src, d1[\idx - 4]
.endif
.endm

@ Extract a vector from src1-src2 and src4-src5 (src1-src3 and src4-src6
@ for size >= 16), and multiply-accumulate into dst1 and dst3 (or
@ dst1-dst2 and dst3-dst4 for size >= 16)
.macro extmla dst1, dst2, dst3, dst4, dst1d, dst3d, src1, src2, src3, src4, src5, src6, offset, size
        vext.8          q14, \src1, \src2, #(2*\offset)
        vext.8          q15, \src4, \src5, #(2*\offset)
.if \size >= 16
        vmla_lane       \dst1,  q14, \offset
        vext.8          q5,  \src2, \src3, #(2*\offset)
        vmla_lane       \dst3,  q15, \offset
        vext.8          q6,  \src5, \src6, #(2*\offset)
        vmla_lane       \dst2,  q5,  \offset
        vmla_lane       \dst4,  q6,  \offset
.elseif \size == 8
        vmla_lane       \dst1,  q14, \offset
        vmla_lane       \dst3,  q15, \offset
.else
        vmla_lane       \dst1d, d28, \offset
        vmla_lane       \dst3d, d30, \offset
.endif
.endm
@ The same as above, but don't accumulate straight into the
@ destination, but use a temp register and accumulate with saturation.
.macro extmulqadd dst1, dst2, dst3, dst4, dst1d, dst3d, src1, src2, src3, src4, src5, src6, offset, size
        vext.8          q14, \src1, \src2, #(2*\offset)
        vext.8          q15, \src4, \src5, #(2*\offset)
.if \size >= 16
        vmul_lane       q14, q14, \offset
        vext.8          q5,  \src2, \src3, #(2*\offset)
        vmul_lane       q15, q15, \offset
        vext.8          q6,  \src5, \src6, #(2*\offset)
        vmul_lane       q5,  q5,  \offset
        vmul_lane       q6,  q6,  \offset
.elseif \size == 8
        vmul_lane       q14, q14, \offset
        vmul_lane       q15, q15, \offset
.else
        vmul_lane       d28, d28, \offset
        vmul_lane       d30, d30, \offset
.endif
.if \size == 4
        vqadd.s16       \dst1d, \dst1d, d28
        vqadd.s16       \dst3d, \dst3d, d30
.else
        vqadd.s16       \dst1,  \dst1,  q14
        vqadd.s16       \dst3,  \dst3,  q15
.if \size >= 16
        vqadd.s16       \dst2,  \dst2,  q5
        vqadd.s16       \dst4,  \dst4,  q6
.endif
.endif
.endm


@ Instantiate a horizontal filter function for the given size.
@ This can work on 4, 8 or 16 pixels in parallel; for larger
@ widths it will do 16 pixels at a time and loop horizontally.
@ The actual width is passed in r5, the height in r4 and
@ the filter coefficients in r12. idx2 is the index of the largest
@ filter coefficient (3 or 4) and idx1 is the other one of them.
.macro do_8tap_h type, size, idx1, idx2
function \type\()_8tap_\size\()h_\idx1\idx2
        sub             r2,  r2,  #3
        add             r6,  r0,  r1
        add             r7,  r2,  r3
        add             r1,  r1,  r1
        add             r3,  r3,  r3
        @ Only size >= 16 loops horizontally and needs
        @ reduced dst stride
.if \size >= 16
        sub             r1,  r1,  r5
.endif
        @ size >= 16 loads two qwords and increments r2,
        @ for size 4/8 it's enough with one qword and no
        @ postincrement
.if \size >= 16
        sub             r3,  r3,  r5
        sub             r3,  r3,  #8
.endif
        @ Load the filter vector
        vld1.16         {q0},  [r12,:128]
1:
.if \size >= 16
        mov             r12, r5
.endif
        @ Load src
.if \size >= 16
        vld1.8          {d18, d19, d20}, [r2]!
        vld1.8          {d24, d25, d26}, [r7]!
.else
        vld1.8          {q9},  [r2]
        vld1.8          {q12}, [r7]
.endif
        vmovl.u8        q8,  d18
        vmovl.u8        q9,  d19
        vmovl.u8        q11, d24
        vmovl.u8        q12, d25
.if \size >= 16
        vmovl.u8        q10, d20
        vmovl.u8        q13, d26
.endif
2:

        @ Accumulate, adding idx2 last with a separate
        @ saturating add. The positive filter coefficients
        @ for all indices except idx2 must add up to less
        @ than 127 for this not to overflow.
        vmul.s16        q1,  q8,  d0[0]
        vmul.s16        q3,  q11, d0[0]
.if \size >= 16
        vmul.s16        q2,  q9,  d0[0]
        vmul.s16        q4,  q12, d0[0]
.endif
        extmla          q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, 1,     \size
        extmla          q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, 2,     \size
        extmla          q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, \idx1, \size
        extmla          q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, 5,     \size
        extmla          q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, 6,     \size
        extmla          q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, 7,     \size
        extmulqadd      q1,  q2,  q3,  q4,  d2,  d6,  q8,  q9,  q10, q11, q12, q13, \idx2, \size

        @ Round, shift and saturate
        vqrshrun.s16    d2,  q1,  #7
        vqrshrun.s16    d6,  q3,  #7
.if \size >= 16
        vqrshrun.s16    d3,  q2,  #7
        vqrshrun.s16    d7,  q4,  #7
.endif
        @ Average
.ifc \type,avg
.if \size >= 16
        vld1.8          {q14}, [r0,:128]
        vld1.8          {q15}, [r6,:128]
        vrhadd.u8       q1,  q1,  q14
        vrhadd.u8       q3,  q3,  q15
.elseif \size == 8
        vld1.8          {d28}, [r0,:64]
        vld1.8          {d30}, [r6,:64]
        vrhadd.u8       d2,  d2,  d28
        vrhadd.u8       d6,  d6,  d30
.else
        @ We only need d28[0], but [] is faster on some cores
        vld1.32         {d28[]}, [r0,:32]
        vld1.32         {d30[]}, [r6,:32]
        vrhadd.u8       d2,  d2,  d28
        vrhadd.u8       d6,  d6,  d30
.endif
.endif
        @ Store and loop horizontally (for size >= 16)
.if \size >= 16
        subs            r12, r12, #16
        vst1.8          {q1}, [r0,:128]!
        vst1.8          {q3}, [r6,:128]!
        beq             3f
        vmov            q8,  q10
        vmov            q11, q13
        vld1.8          {q10}, [r2]!
        vld1.8          {q13}, [r7]!
        vmovl.u8        q9,  d20
        vmovl.u8        q10, d21
        vmovl.u8        q12, d26
        vmovl.u8        q13, d27
        b               2b
.elseif \size == 8
        vst1.8          {d2}, [r0,:64]
        vst1.8          {d6}, [r6,:64]
.else @ \size == 4
        vst1.32         {d2[0]}, [r0,:32]
        vst1.32         {d6[0]}, [r6,:32]
.endif
3:
        @ Loop vertically
        add             r0,  r0,  r1
        add             r6,  r6,  r1
        add             r2,  r2,  r3
        add             r7,  r7,  r3
        subs            r4,  r4,  #2
        bne             1b
.if \size >= 16
        vpop            {q4-q6}
.endif
        pop             {r4-r7}
        bx              lr
endfunc
.endm

.macro do_8tap_h_size size
do_8tap_h put, \size, 3, 4
do_8tap_h avg, \size, 3, 4
do_8tap_h put, \size, 4, 3
do_8tap_h avg, \size, 4, 3
.endm

do_8tap_h_size 4
do_8tap_h_size 8
do_8tap_h_size 16

.macro do_8tap_h_func type, filter, offset, size
function ff_vp9_\type\()_\filter\()\size\()_h_neon, export=1
        push            {r4-r7}
.if \size >= 16
        vpush           {q4-q6}
        ldr             r4,  [sp, #64]
        ldr             r5,  [sp, #68]
.else
        ldr             r4,  [sp, #16]
        ldr             r5,  [sp, #20]
.endif
        movrelx         r12, X(ff_vp9_subpel_filters), r6
        add             r12, r12, 256*\offset
        cmp             r5,  #8
        add             r12, r12, r5, lsl #4
        mov             r5,  #\size
.if \size >= 16
        bge             \type\()_8tap_16h_34
        b               \type\()_8tap_16h_43
.else
        bge             \type\()_8tap_\size\()h_34
        b               \type\()_8tap_\size\()h_43
.endif
endfunc
.endm

.macro do_8tap_h_filters size
do_8tap_h_func put, regular, 1, \size
do_8tap_h_func avg, regular, 1, \size
do_8tap_h_func put, sharp,   2, \size
do_8tap_h_func avg, sharp,   2, \size
do_8tap_h_func put, smooth,  0, \size
do_8tap_h_func avg, smooth,  0, \size
.endm

do_8tap_h_filters 64
do_8tap_h_filters 32
do_8tap_h_filters 16
do_8tap_h_filters 8
do_8tap_h_filters 4

.ltorg

@ Vertical filters

@ Round, shift and saturate and store qreg1-2 over 4 lines
.macro do_store4 qreg1, dreg1, qreg2, dreg2, tmp1, tmp2, type
        vqrshrun.s16    \dreg1,  \qreg1, #7
        vqrshrun.s16    \dreg2,  \qreg2, #7
.ifc \type,avg
        vld1.32         {\tmp1[]},   [r0,:32], r1
        vld1.32         {\tmp2[]},   [r0,:32], r1
        vld1.32         {\tmp1[1]},  [r0,:32], r1
        vld1.32         {\tmp2[1]},  [r0,:32], r1
        vrhadd.u8       \dreg1,  \dreg1,  \tmp1
        vrhadd.u8       \dreg2,  \dreg2,  \tmp2
        sub             r0,  r0,  r1, lsl #2
.endif
        vst1.32         {\dreg1[0]}, [r0,:32], r1
        vst1.32         {\dreg2[0]}, [r0,:32], r1
        vst1.32         {\dreg1[1]}, [r0,:32], r1
        vst1.32         {\dreg2[1]}, [r0,:32], r1
.endm

@ Round, shift and saturate and store qreg1-4
.macro do_store qreg1, dreg1, qreg2, dreg2, qreg3, dreg3, qreg4, dreg4, tmp1, tmp2, tmp3, tmp4, type
        vqrshrun.s16    \dreg1,  \qreg1, #7
        vqrshrun.s16    \dreg2,  \qreg2, #7
        vqrshrun.s16    \dreg3,  \qreg3, #7
        vqrshrun.s16    \dreg4,  \qreg4, #7
.ifc \type,avg
        vld1.8          {\tmp1},  [r0,:64], r1
        vld1.8          {\tmp2},  [r0,:64], r1
        vld1.8          {\tmp3},  [r0,:64], r1
        vld1.8          {\tmp4},  [r0,:64], r1
        vrhadd.u8       \dreg1,  \dreg1,  \tmp1
        vrhadd.u8       \dreg2,  \dreg2,  \tmp2
        vrhadd.u8       \dreg3,  \dreg3,  \tmp3
        vrhadd.u8       \dreg4,  \dreg4,  \tmp4
        sub             r0,  r0,  r1, lsl #2
.endif
        vst1.8          {\dreg1}, [r0,:64], r1
        vst1.8          {\dreg2}, [r0,:64], r1
        vst1.8          {\dreg3}, [r0,:64], r1
        vst1.8          {\dreg4}, [r0,:64], r1
.endm

@ Evaluate the filter twice in parallel, from the inputs src1-src9 into dst1-dst2
@ (src1-src8 into dst1, src2-src9 into dst2), adding idx2 separately
@ at the end with saturation. Indices 0 and 7 always have negative or zero
@ coefficients, so they can be accumulated into tmp1-tmp2 together with the
@ largest coefficient.
.macro convolve dst1, dst2, src1, src2, src3, src4, src5, src6, src7, src8, src9, idx1, idx2, tmp1, tmp2
        vmul.s16        \dst1, \src2, d0[1]
        vmul.s16        \dst2, \src3, d0[1]
        vmul.s16        \tmp1, \src1, d0[0]
        vmul.s16        \tmp2, \src2, d0[0]
        vmla.s16        \dst1, \src3, d0[2]
        vmla.s16        \dst2, \src4, d0[2]
.if \idx1 == 3
        vmla.s16        \dst1, \src4, d0[3]
        vmla.s16        \dst2, \src5, d0[3]
.else
        vmla.s16        \dst1, \src5, d1[0]
        vmla.s16        \dst2, \src6, d1[0]
.endif
        vmla.s16        \dst1, \src6, d1[1]
        vmla.s16        \dst2, \src7, d1[1]
        vmla.s16        \tmp1, \src8, d1[3]
        vmla.s16        \tmp2, \src9, d1[3]
        vmla.s16        \dst1, \src7, d1[2]
        vmla.s16        \dst2, \src8, d1[2]
.if \idx2 == 3
        vmla.s16        \tmp1, \src4, d0[3]
        vmla.s16        \tmp2, \src5, d0[3]
.else
        vmla.s16        \tmp1, \src5, d1[0]
        vmla.s16        \tmp2, \src6, d1[0]
.endif
        vqadd.s16       \dst1, \dst1, \tmp1
        vqadd.s16       \dst2, \dst2, \tmp2
.endm

@ Load pixels and extend them to 16 bit
.macro loadl dst1, dst2, dst3, dst4
        vld1.8          {d2}, [r2], r3
        vld1.8          {d3}, [r2], r3
        vld1.8          {d4}, [r2], r3
.ifnb \dst4
        vld1.8          {d5}, [r2], r3
.endif
        vmovl.u8        \dst1, d2
        vmovl.u8        \dst2, d3
        vmovl.u8        \dst3, d4
.ifnb \dst4
        vmovl.u8        \dst4, d5
.endif
.endm

@ Instantiate a vertical filter function for filtering 8 pixels at a time.
@ The height is passed in r4, the width in r5 and the filter coefficients
@ in r12. idx2 is the index of the largest filter coefficient (3 or 4)
@ and idx1 is the other one of them.
.macro do_8tap_8v type, idx1, idx2
function \type\()_8tap_8v_\idx1\idx2
        sub             r2,  r2,  r3, lsl #1
        sub             r2,  r2,  r3
        vld1.16         {q0},  [r12, :128]
1:
        mov             r12, r4

        loadl           q5,  q6,  q7
        loadl           q8,  q9,  q10, q11
2:
        loadl           q12, q13, q14, q15
        convolve        q1,  q2,  q5,  q6,  q7,  q8,  q9,  q10, q11, q12, q13, \idx1, \idx2, q4,  q5
        convolve        q3,  q4,  q7,  q8,  q9,  q10, q11, q12, q13, q14, q15, \idx1, \idx2, q5,  q6
        do_store        q1,  d2,  q2,  d4,  q3,  d6,  q4,  d8,  d3,  d5,  d7,  d9,  \type

        subs            r12, r12, #4
        beq             8f

        loadl           q4,  q5,  q6,  q7
        convolve        q1,  q2,  q9,  q10, q11, q12, q13, q14, q15, q4,  q5,  \idx1, \idx2, q8,  q9
        convolve        q3,  q8,  q11, q12, q13, q14, q15, q4,  q5,  q6,  q7,  \idx1, \idx2, q9,  q10
        do_store        q1,  d2,  q2,  d4,  q3,  d6,  q8,  d16, d3,  d5,  d7,  d17, \type

        subs            r12, r12, #4
        beq             8f

        loadl           q8,  q9,  q10, q11
        convolve        q1,  q2,  q13, q14, q15, q4,  q5,  q6,  q7,  q8,  q9,  \idx1, \idx2, q12, q13
        convolve        q3,  q12, q15, q4,  q5,  q6,  q7,  q8,  q9,  q10, q11, \idx1, \idx2, q13, q14
        do_store        q1,  d2,  q2,  d4,  q3,  d6,  q12, d24, d3,  d5,  d7,  d25, \type

        subs            r12, r12, #4
        bne             2b

8:
        subs            r5,  r5,  #8
        beq             9f
        @ r0 -= h * dst_stride
        mls             r0,  r1,  r4, r0
        @ r2 -= h * src_stride
        mls             r2,  r3,  r4, r2
        @ r2 -= 8 * src_stride
        sub             r2,  r2,  r3, lsl #3
        @ r2 += 1 * src_stride
        add             r2,  r2,  r3
        add             r2,  r2,  #8
        add             r0,  r0,  #8
        b               1b
9:
        vpop            {q4-q7}
        pop             {r4-r5}
        bx              lr
endfunc
.endm

do_8tap_8v put, 3, 4
do_8tap_8v put, 4, 3
do_8tap_8v avg, 3, 4
do_8tap_8v avg, 4, 3

@ Instantiate a vertical filter function for filtering a 4 pixels wide
@ slice. The first half of the registers contain one row, while the second
@ half of a register contains the second-next row (also stored in the first
@ half of the register two steps ahead). The convolution does two outputs
@ at a time; the output of q5-q12 into one, and q4-q13 into another one.
@ The first half of first output is the first output row, the first half
@ of the other output is the second output row. The second halves of the
@ registers are rows 3 and 4.
@ This only is designed to work for 4 or 8 output lines.
.macro do_8tap_4v type, idx1, idx2
function \type\()_8tap_4v_\idx1\idx2
        sub             r2,  r2,  r3, lsl #1
        sub             r2,  r2,  r3
        vld1.16         {q0},  [r12, :128]

        vld1.32         {d2[]},   [r2], r3
        vld1.32         {d3[]},   [r2], r3
        vld1.32         {d4[]},   [r2], r3
        vld1.32         {d5[]},   [r2], r3
        vld1.32         {d6[]},   [r2], r3
        vld1.32         {d7[]},   [r2], r3
        vext.8          d2,  d2,  d4,  #4
        vld1.32         {d8[]},   [r2], r3
        vext.8          d3,  d3,  d5,  #4
        vld1.32         {d9[]},   [r2], r3
        vmovl.u8        q5,  d2
        vext.8          d4,  d4,  d6,  #4
        vld1.32         {d28[]},  [r2], r3
        vmovl.u8        q6,  d3
        vext.8          d5,  d5,  d7,  #4
        vld1.32         {d29[]},  [r2], r3
        vmovl.u8        q7,  d4
        vext.8          d6,  d6,  d8,  #4
        vld1.32         {d30[]},  [r2], r3
        vmovl.u8        q8,  d5
        vext.8          d7,  d7,  d9,  #4
        vmovl.u8        q9,  d6
        vext.8          d8,  d8,  d28, #4
        vmovl.u8        q10, d7
        vext.8          d9,  d9,  d29, #4
        vmovl.u8        q11, d8
        vext.8          d28, d28, d30, #4
        vmovl.u8        q12, d9
        vmovl.u8        q13, d28

        convolve        q1,  q2,  q5,  q6,  q7,  q8,  q9,  q10, q11, q12, q13, \idx1, \idx2, q4, q3
        do_store4       q1,  d2,  q2,  d4,  d3,  d5,  \type
        subs            r4,  r4,  #4
        beq             9f

        vld1.32         {d2[]},   [r2], r3
        vld1.32         {d3[]},   [r2], r3
        vext.8          d29, d29, d2,  #4
        vext.8          d30, d30, d3,  #4
        vld1.32         {d2[1]},  [r2], r3
        vmovl.u8        q14, d29
        vld1.32         {d3[1]},  [r2], r3
        vmovl.u8        q15, d30
        vmovl.u8        q5,  d2
        vmovl.u8        q6,  d3

        convolve        q1,  q2,  q9,  q10, q11, q12, q13, q14, q15, q5,  q6,  \idx1, \idx2, q4, q3
        do_store4       q1,  d2,  q2,  d4,  d3,  d5,  \type

9:
        vpop            {q4-q7}
        pop             {r4-r5}
        bx              lr
endfunc
.endm

do_8tap_4v put, 3, 4
do_8tap_4v put, 4, 3
do_8tap_4v avg, 3, 4
do_8tap_4v avg, 4, 3

.macro do_8tap_v_func type, filter, offset, size
function ff_vp9_\type\()_\filter\()\size\()_v_neon, export=1
        push            {r4-r5}
        vpush           {q4-q7}
        ldr             r4,  [sp, #72]
        movrelx         r12, X(ff_vp9_subpel_filters), r5
        ldr             r5,  [sp, #80]
        add             r12, r12, 256*\offset
        add             r12, r12, r5, lsl #4
        cmp             r5,  #8
        mov             r5,  #\size
.if \size >= 8
        bge             \type\()_8tap_8v_34
        b               \type\()_8tap_8v_43
.else
        bge             \type\()_8tap_4v_34
        b               \type\()_8tap_4v_43
.endif
endfunc
.endm

.macro do_8tap_v_filters size
do_8tap_v_func put, regular, 1, \size
do_8tap_v_func avg, regular, 1, \size
do_8tap_v_func put, sharp,   2, \size
do_8tap_v_func avg, sharp,   2, \size
do_8tap_v_func put, smooth,  0, \size
do_8tap_v_func avg, smooth,  0, \size
.endm

do_8tap_v_filters 64
do_8tap_v_filters 32
do_8tap_v_filters 16
do_8tap_v_filters 8
do_8tap_v_filters 4
