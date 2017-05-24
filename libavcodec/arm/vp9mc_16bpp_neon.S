/*
 * Copyright (c) 2017 Google Inc.
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

function ff_vp9_copy128_neon, export=1
        ldr             r12, [sp]
        sub             r1,  r1,  #96
        sub             r3,  r3,  #96
1:
        subs            r12, r12, #1
        vld1.16         {q0,  q1},  [r2]!
        vst1.16         {q0,  q1},  [r0, :128]!
        vld1.16         {q2,  q3},  [r2]!
        vst1.16         {q2,  q3},  [r0, :128]!
        vld1.16         {q8,  q9},  [r2]!
        vst1.16         {q8,  q9},  [r0, :128]!
        vld1.16         {q10, q11}, [r2], r3
        vst1.16         {q10, q11}, [r0, :128], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_avg64_16_neon, export=1
        push            {lr}
        ldr             r12, [sp, #4]
        sub             r1,  r1,  #96
        sub             r3,  r3,  #96
        mov             lr,  r0
1:
        subs            r12, r12, #1
        vld1.16         {q8,  q9},  [r2]!
        vld1.16         {q0,  q1},  [r0, :128]!
        vld1.16         {q10, q11}, [r2]!
        vrhadd.u16      q0,  q0,  q8
        vld1.16         {q2,  q3},  [r0, :128]!
        vrhadd.u16      q1,  q1,  q9
        vld1.16         {q12, q13}, [r2]!
        vrhadd.u16      q2,  q2,  q10
        vst1.16         {q0,  q1},  [lr, :128]!
        vrhadd.u16      q3,  q3,  q11
        vld1.16         {q8,  q9},  [r0, :128]!
        vst1.16         {q2,  q3},  [lr, :128]!
        vrhadd.u16      q8,  q8,  q12
        vld1.16         {q14, q15}, [r2], r3
        vrhadd.u16      q9,  q9,  q13
        vld1.16         {q10, q11}, [r0, :128], r1
        vrhadd.u16      q10, q10, q14
        vst1.16         {q8,  q9},  [lr, :128]!
        vrhadd.u16      q11, q11, q15
        vst1.16         {q10, q11}, [lr, :128], r1
        bne             1b
        pop             {pc}
endfunc

function ff_vp9_avg32_16_neon, export=1
        push            {lr}
        ldr             r12, [sp, #4]
        sub             r1,  r1,  #32
        sub             r3,  r3,  #32
        mov             lr,  r0
1:
        subs            r12, r12, #1
        vld1.16         {q8,  q9},  [r2]!
        vld1.16         {q0,  q1},  [r0, :128]!
        vld1.16         {q10, q11}, [r2], r3
        vrhadd.u16      q0,  q0,  q8
        vld1.16         {q2,  q3},  [r0, :128], r1
        vrhadd.u16      q1,  q1,  q9
        vrhadd.u16      q2,  q2,  q10
        vst1.16         {q0, q1},  [lr, :128]!
        vrhadd.u16      q3,  q3,  q11
        vst1.16         {q2, q3},  [lr, :128], r1
        bne             1b
        pop             {pc}
endfunc

function ff_vp9_avg16_16_neon, export=1
        ldr             r12, [sp]
1:
        subs            r12, r12, #1
        vld1.16         {q2,  q3},  [r2], r3
        vld1.16         {q0,  q1},  [r0, :128]
        vrhadd.u16      q0,  q0,  q2
        vrhadd.u16      q1,  q1,  q3
        vst1.16         {q0,  q1},  [r0, :128], r1
        bne             1b
        bx              lr
endfunc

function ff_vp9_avg8_16_neon, export=1
        push            {lr}
        ldr             r12, [sp, #4]
        mov             lr,  r0
1:
        subs            r12, r12, #2
        vld1.16         {q2},  [r2], r3
        vld1.16         {q0},  [r0, :128], r1
        vld1.16         {q3},  [r2], r3
        vrhadd.u16      q0,  q0,  q2
        vld1.16         {q1},  [r0, :128], r1
        vrhadd.u16      q1,  q1,  q3
        vst1.16         {q0},  [lr, :128], r1
        vst1.16         {q1},  [lr, :128], r1
        bne             1b
        pop             {pc}
endfunc

function ff_vp9_avg4_16_neon, export=1
        ldr             r12, [sp]
1:
        subs            r12, r12, #2
        vld1.16         {d2},  [r2], r3
        vld1.16         {d0},  [r0, :64], r1
        vld1.16         {d3},  [r2], r3
        vrhadd.u16      d0,  d0,  d2
        vld1.16         {d1},  [r0, :64]
        sub             r0,  r0,  r1
        vrhadd.u16      d1,  d1,  d3
        vst1.16         {d0},  [r0, :64], r1
        vst1.16         {d1},  [r0, :64], r1
        bne             1b
        bx              lr
endfunc

@ Helper macros for vmull/vmlal with a constant from either d0 or d1 depending on index
.macro vmull_lane dst, src, idx
.if \idx < 4
       vmull.s16        \dst, \src, d0[\idx]
.else
       vmull.s16        \dst, \src, d1[\idx - 4]
.endif
.endm
.macro vmlal_lane dst, src, idx
.if \idx < 4
       vmlal.s16        \dst, \src, d0[\idx]
.else
       vmlal.s16        \dst, \src, d1[\idx - 4]
.endif
.endm

@ Extract a vector from src1-src2 and src3-src4, andmultiply-accumulate
@ into dst1 and dst3 (or dst1-dst2 and dst3-dst4 for size >= 8)
.macro extmlal dst1, dst2, dst3, dst4, src1, src2, src3, src4, offset, size
        vext.8          q14, \src1, \src2, #(2*\offset)
        vext.8          q15, \src3, \src4, #(2*\offset)
        vmlal_lane      \dst1,  d28, \offset
        vmlal_lane      \dst3,  d30, \offset
.if \size >= 8
        vmlal_lane      \dst2,  d29, \offset
        vmlal_lane      \dst4,  d31, \offset
.endif
.endm


@ Instantiate a horizontal filter function for the given size.
@ This can work on 4 or 8 pixels in parallel; for larger
@ widths it will do 8 pixels at a time and loop horizontally.
@ The actual width (in bytes) is passed in r5, the height in r4 and
@ the filter coefficients in r12.
.macro do_8tap_h type, size
function \type\()_8tap_\size\()h
        sub             r2,  r2,  #6
        add             r6,  r0,  r1
        add             r7,  r2,  r3
        add             r1,  r1,  r1
        add             r3,  r3,  r3
        @ Only size >= 8 loops horizontally and needs
        @ reduced dst stride
.if \size >= 8
        sub             r1,  r1,  r5
.endif
        @ size >= 8 loads two qwords and increments r2,
        @ for size 4 it's enough with three dwords and no
        @ postincrement
.if \size >= 8
        sub             r3,  r3,  r5
        sub             r3,  r3,  #16
.endif
        @ Load the filter vector
        vld1.16         {q0},  [r12,:128]
1:
.if \size >= 8
        mov             r12, r5
.endif
        @ Load src
.if \size >= 8
        vld1.16         {q8,  q9},  [r2]!
        vld1.16         {q10, q11}, [r7]!
.else
        vld1.16         {d16, d17, d18}, [r2]
        vld1.16         {d20, d21, d22}, [r7]
.endif
2:

        vmull.s16       q1,  d16, d0[0]
        vmull.s16       q12, d20, d0[0]
.if \size >= 8
        vmull.s16       q2,  d17, d0[0]
        vmull.s16       q13, d21, d0[0]
.endif
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 1, \size
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 2, \size
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 3, \size
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 4, \size
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 5, \size
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 6, \size
        extmlal         q1,  q2,  q12, q13, q8,  q9,  q10, q11, 7, \size

        @ Round, shift and saturate.
        @ The vqrshrun takes care of clamping negative values to zero, but
        @ we manually need to do vmin with the max pixel value.
        vqrshrun.s32    d2,  q1,  #7
        vqrshrun.s32    d24, q12, #7
.if \size >= 8
        vqrshrun.s32    d3,  q2,  #7
        vqrshrun.s32    d25, q13, #7
        vmin.u16        q1,  q1,  q3
        vmin.u16        q12, q12, q3
.else
        vmin.u16        d2,  d2,  d6
        vmin.u16        d24, d24, d6
.endif
        @ Average
.ifc \type,avg
.if \size >= 8
        vld1.16         {q14}, [r0,:128]
        vld1.16         {q15}, [r6,:128]
        vrhadd.u16      q1,  q1,  q14
        vrhadd.u16      q12, q12, q15
.else
        vld1.16         {d28}, [r0,:64]
        vld1.16         {d30}, [r6,:64]
        vrhadd.u16      d2,  d2,  d28
        vrhadd.u16      d24, d24, d30
.endif
.endif
        @ Store and loop horizontally (for size >= 8)
.if \size >= 8
        subs            r12, r12, #16
        vst1.16         {q1},  [r0,:128]!
        vst1.16         {q12}, [r6,:128]!
        beq             3f
        vmov            q8,  q9
        vmov            q10, q11
        vld1.16         {q9},  [r2]!
        vld1.16         {q11}, [r7]!
        b               2b
.else @ \size == 4
        vst1.16         {d2},  [r0,:64]
        vst1.16         {d24}, [r6,:64]
.endif
3:
        @ Loop vertically
        add             r0,  r0,  r1
        add             r6,  r6,  r1
        add             r2,  r2,  r3
        add             r7,  r7,  r3
        subs            r4,  r4,  #2
        bne             1b
        pop             {r4-r7}
        bx              lr
endfunc
.endm

.macro do_8tap_h_size size
do_8tap_h put, \size
do_8tap_h avg, \size
.endm

do_8tap_h_size 4
do_8tap_h_size 8

.macro do_8tap_h_func type, filter, offset, size, bpp
function ff_vp9_\type\()_\filter\()\size\()_h_\bpp\()_neon, export=1
        push            {r4-r7}
        ldr             r4,  [sp, #16]
        ldr             r5,  [sp, #20]
        vmvn.u16        q3,  #((0xffff << \bpp) & 0xffff)
        movrelx         r12, X(ff_vp9_subpel_filters), r6
        add             r12, r12, 256*\offset
        add             r12, r12, r5, lsl #4
        mov             r5,  #2*\size
.if \size >= 8
        b               \type\()_8tap_8h
.else
        b               \type\()_8tap_4h
.endif
endfunc
.endm

.macro do_8tap_h_filters size, bpp
do_8tap_h_func put, regular, 1, \size, \bpp
do_8tap_h_func avg, regular, 1, \size, \bpp
do_8tap_h_func put, sharp,   2, \size, \bpp
do_8tap_h_func avg, sharp,   2, \size, \bpp
do_8tap_h_func put, smooth,  0, \size, \bpp
do_8tap_h_func avg, smooth,  0, \size, \bpp
.endm

.macro do_8tap_h_filters_bpp bpp
do_8tap_h_filters 64, \bpp
do_8tap_h_filters 32, \bpp
do_8tap_h_filters 16, \bpp
do_8tap_h_filters 8,  \bpp
do_8tap_h_filters 4,  \bpp
.endm

do_8tap_h_filters_bpp 10
do_8tap_h_filters_bpp 12

.ltorg

@ Vertical filters

@ Round, shift and saturate and store qreg1-4
.macro do_store4 qreg1, dreg1, qreg2, dreg2, qreg3, dreg3, qreg4, dreg4, tmp1, tmp2, tmp3, tmp4, minreg, type
        vqrshrun.s32    \dreg1,  \qreg1, #7
        vqrshrun.s32    \dreg2,  \qreg2, #7
        vqrshrun.s32    \dreg3,  \qreg3, #7
        vqrshrun.s32    \dreg4,  \qreg4, #7
.ifc \type,avg
        vld1.16         {\tmp1},  [r6,:64], r1
        vld1.16         {\tmp2},  [r6,:64], r1
        vld1.16         {\tmp3},  [r6,:64], r1
        vld1.16         {\tmp4},  [r6,:64], r1
.endif
        vmin.u16        \dreg1,  \dreg1,  \minreg
        vmin.u16        \dreg2,  \dreg2,  \minreg
        vmin.u16        \dreg3,  \dreg3,  \minreg
        vmin.u16        \dreg4,  \dreg4,  \minreg
.ifc \type,avg
        vrhadd.u16      \dreg1,  \dreg1,  \tmp1
        vrhadd.u16      \dreg2,  \dreg2,  \tmp2
        vrhadd.u16      \dreg3,  \dreg3,  \tmp3
        vrhadd.u16      \dreg4,  \dreg4,  \tmp4
.endif
        vst1.16         {\dreg1}, [r0,:64], r1
        vst1.16         {\dreg2}, [r0,:64], r1
        vst1.16         {\dreg3}, [r0,:64], r1
        vst1.16         {\dreg4}, [r0,:64], r1
.endm

@ Round, shift and saturate and store qreg1-4
@ qreg1-2 belong to one line and qreg3-4 to the second line.
@ dreg1-2 == qreg1, dreg3-4 == qreg2.
.macro do_store8 qreg1, qreg2, qreg3, qreg4, dreg1, dreg2, dreg3, dreg4, minreg, type
        vqrshrun.s32    \dreg1,  \qreg1, #7
        vqrshrun.s32    \dreg2,  \qreg2, #7
        vqrshrun.s32    \dreg3,  \qreg3, #7
        vqrshrun.s32    \dreg4,  \qreg4, #7
.ifc \type,avg
        vld1.16         {\qreg3},  [r6,:128], r1
        vld1.16         {\qreg4},  [r6,:128], r1
.endif
        vmin.u16        \qreg1,  \qreg1,  \minreg
        vmin.u16        \qreg2,  \qreg2,  \minreg
.ifc \type,avg
        vrhadd.u16      \qreg1,  \qreg1,  \qreg3
        vrhadd.u16      \qreg2,  \qreg2,  \qreg4
.endif
        vst1.16         {\qreg1}, [r0,:128], r1
        vst1.16         {\qreg2}, [r0,:128], r1
.endm

@ Evaluate the filter twice in parallel, from the inputs src1-src9 into dst1-dst2
@ (src1-src8 into dst1, src2-src9 into dst2).
.macro convolve4 dst1, dst2, src1, src2, src3, src4, src5, src6, src7, src8, src9, tmp1, tmp2
        vmull.s16       \dst1, \src1, d0[0]
        vmull.s16       \dst2, \src2, d0[0]
        vmull.s16       \tmp1, \src2, d0[1]
        vmull.s16       \tmp2, \src3, d0[1]
        vmlal.s16       \dst1, \src3, d0[2]
        vmlal.s16       \dst2, \src4, d0[2]
        vmlal.s16       \tmp1, \src4, d0[3]
        vmlal.s16       \tmp2, \src5, d0[3]
        vmlal.s16       \dst1, \src5, d1[0]
        vmlal.s16       \dst2, \src6, d1[0]
        vmlal.s16       \tmp1, \src6, d1[1]
        vmlal.s16       \tmp2, \src7, d1[1]
        vmlal.s16       \dst1, \src7, d1[2]
        vmlal.s16       \dst2, \src8, d1[2]
        vmlal.s16       \tmp1, \src8, d1[3]
        vmlal.s16       \tmp2, \src9, d1[3]
        vadd.s32        \dst1, \dst1, \tmp1
        vadd.s32        \dst2, \dst2, \tmp2
.endm

@ Evaluate the filter twice in parallel. This does the same as convolve4 above,
@ but with double width (two input/output registers per row).
.macro convolve8 dst1, dst2, dst3, dst4, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15, src16, src17, src18
        vmull.s16       \dst1, \src1,  d0[0]
        vmull.s16       \dst2, \src2,  d0[0]
        vmull.s16       \dst3, \src3,  d0[0]
        vmull.s16       \dst4, \src4,  d0[0]
        vmlal.s16       \dst1, \src3,  d0[1]
        vmlal.s16       \dst2, \src4,  d0[1]
        vmlal.s16       \dst3, \src5,  d0[1]
        vmlal.s16       \dst4, \src6,  d0[1]
        vmlal.s16       \dst1, \src5,  d0[2]
        vmlal.s16       \dst2, \src6,  d0[2]
        vmlal.s16       \dst3, \src7,  d0[2]
        vmlal.s16       \dst4, \src8,  d0[2]
        vmlal.s16       \dst1, \src7,  d0[3]
        vmlal.s16       \dst2, \src8,  d0[3]
        vmlal.s16       \dst3, \src9,  d0[3]
        vmlal.s16       \dst4, \src10, d0[3]
        vmlal.s16       \dst1, \src9,  d1[0]
        vmlal.s16       \dst2, \src10, d1[0]
        vmlal.s16       \dst3, \src11, d1[0]
        vmlal.s16       \dst4, \src12, d1[0]
        vmlal.s16       \dst1, \src11, d1[1]
        vmlal.s16       \dst2, \src12, d1[1]
        vmlal.s16       \dst3, \src13, d1[1]
        vmlal.s16       \dst4, \src14, d1[1]
        vmlal.s16       \dst1, \src13, d1[2]
        vmlal.s16       \dst2, \src14, d1[2]
        vmlal.s16       \dst3, \src15, d1[2]
        vmlal.s16       \dst4, \src16, d1[2]
        vmlal.s16       \dst1, \src15, d1[3]
        vmlal.s16       \dst2, \src16, d1[3]
        vmlal.s16       \dst3, \src17, d1[3]
        vmlal.s16       \dst4, \src18, d1[3]
.endm

@ Instantiate a vertical filter function for filtering 8 pixels at a time.
@ The height is passed in r4, the width in r5 and the filter coefficients
@ in r12.
.macro do_8tap_8v type
function \type\()_8tap_8v
        sub             r2,  r2,  r3, lsl #1
        sub             r2,  r2,  r3
        vld1.16         {q0},  [r12, :128]
1:
.ifc \type,avg
        mov             r6,  r0
.endif
        mov             r12, r4

        vld1.16         {q5},  [r2], r3
        vld1.16         {q6},  [r2], r3
        vld1.16         {q7},  [r2], r3
        vld1.16         {q8},  [r2], r3
        vld1.16         {q9},  [r2], r3
        vld1.16         {q10}, [r2], r3
        vld1.16         {q11}, [r2], r3
2:
        vld1.16         {q12}, [r2], r3
        vld1.16         {q13}, [r2], r3
        vld1.16         {q14}, [r2], r3
        vld1.16         {q15}, [r2], r3
        convolve8       q2,  q3,  q4,  q5,  d10, d11, d12, d13, d14, d15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27
        do_store8       q2,  q3,  q4,  q5,  d4,  d5,  d6,  d7,  q1,  \type
        convolve8       q2,  q3,  q4,  q5,  d14, d15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31
        do_store8       q2,  q3,  q4,  q5,  d4,  d5,  d6,  d7,  q1,  \type

        subs            r12, r12, #4
        beq             8f

        vld1.16         {q4},  [r2], r3
        vld1.16         {q5},  [r2], r3
        vld1.16         {q6},  [r2], r3
        vld1.16         {q7},  [r2], r3
        convolve8       q2,  q3,  q8,  q9,  d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31, d8,  d9,  d10, d11
        do_store8       q2,  q3,  q8,  q9,  d4,  d5,  d6,  d7,  q1,  \type
        convolve8       q2,  q3,  q8,  q9,  d22, d23, d24, d25, d26, d27, d28, d29, d30, d31, d8,  d9,  d10, d11, d12, d13, d14, d15
        do_store8       q2,  q3,  q8,  q9,  d4,  d5,  d6,  d7,  q1,  \type

        subs            r12, r12, #4
        beq             8f

        vld1.16         {q8},  [r2], r3
        vld1.16         {q9},  [r2], r3
        vld1.16         {q10}, [r2], r3
        vld1.16         {q11}, [r2], r3
        convolve8       q2,  q3,  q12, q13, d26, d27, d28, d29, d30, d31, d8,  d9,  d10, d11, d12, d13, d14, d15, d16, d17, d18, d19
        do_store8       q2,  q3,  q12, q13, d4,  d5,  d6,  d7,  q1,  \type
        convolve8       q2,  q3,  q12, q13, d30, d31, d8,  d9,  d10, d11, d12, d13, d14, d15, d16, d17, d18, d19, d20, d21, d22, d23
        do_store8       q2,  q3,  q12, q13, d4,  d5,  d6,  d7,  q1,  \type

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
        add             r2,  r2,  #16
        add             r0,  r0,  #16
        b               1b
9:
        vpop            {q4-q7}
        pop             {r4-r6}
        bx              lr
endfunc
.endm

do_8tap_8v put
do_8tap_8v avg

@ Instantiate a vertical filter function for filtering a 4 pixels wide
@ slice. This only is designed to work for 4 or 8 output lines.
.macro do_8tap_4v type
function \type\()_8tap_4v
        sub             r2,  r2,  r3, lsl #1
        sub             r2,  r2,  r3
        vld1.16         {q0},  [r12, :128]
.ifc \type,avg
        mov             r6,  r0
.endif

        vld1.16         {d16}, [r2], r3
        vld1.16         {d17}, [r2], r3
        vld1.16         {d18}, [r2], r3
        vld1.16         {d19}, [r2], r3
        vld1.16         {d20}, [r2], r3
        vld1.16         {d21}, [r2], r3
        vld1.16         {d22}, [r2], r3
        vld1.16         {d23}, [r2], r3
        vld1.16         {d24}, [r2], r3
        vld1.16         {d25}, [r2], r3
        vld1.16         {d26}, [r2], r3
        convolve4       q2,  q3,  d16, d17, d18, d19, d20, d21, d22, d23, d24, q14, q15
        convolve4       q14, q15, d18, d19, d20, d21, d22, d23, d24, d25, d26, q8,  q9
        do_store4       q2,  d4,  q3,  d6,  q14, d28, q15, d30, d5,  d7,  d29, d31, d2,  \type

        subs            r4,  r4,  #4
        beq             9f

        vld1.16         {d27}, [r2], r3
        vld1.16         {d28}, [r2], r3
        vld1.16         {d29}, [r2], r3
        vld1.16         {d30}, [r2], r3
        convolve4       q2,  q3,  d20, d21, d22, d23, d24, d25, d26, d27, d28, q8,  q9
        convolve4       q8,  q9,  d22, d23, d24, d25, d26, d27, d28, d29, d30, q10, q11
        do_store4       q2,  d4,  q3,  d6,  q8,  d16, q9,  d18, d5,  d7,  d17, d19, d2,  \type

9:
        pop             {r4-r6}
        bx              lr
endfunc
.endm

do_8tap_4v put
do_8tap_4v avg

.macro do_8tap_v_func type, filter, offset, size, bpp
function ff_vp9_\type\()_\filter\()\size\()_v_\bpp\()_neon, export=1
        push            {r4-r6}
        ldr             r4,  [sp, #12]
        ldr             r5,  [sp, #20]
.if \size >= 8
        vpush           {q4-q7}
.endif
        vmvn.u16        q1,  #((0xffff << \bpp) & 0xffff)
        movrelx         r12, X(ff_vp9_subpel_filters), r6
        add             r12, r12, 256*\offset
        add             r12, r12, r5, lsl #4
        mov             r5,  #\size
.if \size >= 8
        b               \type\()_8tap_8v
.else
        b               \type\()_8tap_4v
.endif
endfunc
.endm

.macro do_8tap_v_filters size, bpp
do_8tap_v_func put, regular, 1, \size, \bpp
do_8tap_v_func avg, regular, 1, \size, \bpp
do_8tap_v_func put, sharp,   2, \size, \bpp
do_8tap_v_func avg, sharp,   2, \size, \bpp
do_8tap_v_func put, smooth,  0, \size, \bpp
do_8tap_v_func avg, smooth,  0, \size, \bpp
.endm

.macro do_8tap_v_filters_bpp bpp
do_8tap_v_filters 64, \bpp
do_8tap_v_filters 32, \bpp
do_8tap_v_filters 16, \bpp
do_8tap_v_filters 8,  \bpp
do_8tap_v_filters 4,  \bpp
.endm

do_8tap_v_filters_bpp 10
do_8tap_v_filters_bpp 12
