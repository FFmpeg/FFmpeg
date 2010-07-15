/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

        .macro transpose_8x8 r0 r1 r2 r3 r4 r5 r6 r7
        vtrn.32         \r0, \r4
        vtrn.32         \r1, \r5
        vtrn.32         \r2, \r6
        vtrn.32         \r3, \r7
        vtrn.16         \r0, \r2
        vtrn.16         \r1, \r3
        vtrn.16         \r4, \r6
        vtrn.16         \r5, \r7
        vtrn.8          \r0, \r1
        vtrn.8          \r2, \r3
        vtrn.8          \r4, \r5
        vtrn.8          \r6, \r7
        .endm

        .macro transpose_4x4 r0 r1 r2 r3
        vtrn.16         \r0, \r2
        vtrn.16         \r1, \r3
        vtrn.8          \r0, \r1
        vtrn.8          \r2, \r3
        .endm

        .macro swap4 r0 r1 r2 r3 r4 r5 r6 r7
        vswp            \r0, \r4
        vswp            \r1, \r5
        vswp            \r2, \r6
        vswp            \r3, \r7
        .endm

        .macro transpose16_4x4 r0 r1 r2 r3 r4 r5 r6 r7
        vtrn.32         \r0, \r2
        vtrn.32         \r1, \r3
        vtrn.32         \r4, \r6
        vtrn.32         \r5, \r7
        vtrn.16         \r0, \r1
        vtrn.16         \r2, \r3
        vtrn.16         \r4, \r5
        vtrn.16         \r6, \r7
        .endm

/* chroma_mc8(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
        .macro  h264_chroma_mc8 type
function ff_\type\()_h264_chroma_mc8_neon, export=1
        push            {r4-r7, lr}
        ldrd            r4,  [sp, #20]
.ifc \type,avg
        mov             lr,  r0
.endif
        pld             [r1]
        pld             [r1, r2]

        muls            r7,  r4,  r5
        rsb             r6,  r7,  r5,  lsl #3
        rsb             ip,  r7,  r4,  lsl #3
        sub             r4,  r7,  r4,  lsl #3
        sub             r4,  r4,  r5,  lsl #3
        add             r4,  r4,  #64

        beq             2f

        add             r5,  r1,  r2

        vdup.8          d0,  r4
        lsl             r4,  r2,  #1
        vdup.8          d1,  ip
        vld1.64         {d4, d5}, [r1], r4
        vdup.8          d2,  r6
        vld1.64         {d6, d7}, [r5], r4
        vdup.8          d3,  r7

        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1

1:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
        vld1.64         {d4, d5}, [r1], r4
        vmlal.u8        q8,  d6,  d2
        vext.8          d5,  d4,  d5,  #1
        vmlal.u8        q8,  d7,  d3
        vmull.u8        q9,  d6,  d0
        subs            r3,  r3,  #2
        vmlal.u8        q9,  d7,  d1
        vmlal.u8        q9,  d4,  d2
        vmlal.u8        q9,  d5,  d3
        vrshrn.u16      d16, q8,  #6
        vld1.64         {d6, d7}, [r5], r4
        pld             [r1]
        vrshrn.u16      d17, q9,  #6
.ifc \type,avg
        vld1.64         {d20}, [lr,:64], r2
        vld1.64         {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
.endif
        vext.8          d7,  d6,  d7,  #1
        vst1.64         {d16}, [r0,:64], r2
        vst1.64         {d17}, [r0,:64], r2
        bgt             1b

        pop             {r4-r7, pc}

2:      tst             r6,  r6
        add             ip,  ip,  r6
        vdup.8          d0,  r4
        vdup.8          d1,  ip

        beq             4f

        add             r5,  r1,  r2
        lsl             r4,  r2,  #1
        vld1.64         {d4}, [r1], r4
        vld1.64         {d6}, [r5], r4

3:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d6,  d1
        vld1.64         {d4}, [r1], r4
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d4,  d1
        vld1.64         {d6}, [r5], r4
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
.ifc \type,avg
        vld1.64         {d20}, [lr,:64], r2
        vld1.64         {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
.endif
        subs            r3,  r3,  #2
        pld             [r1]
        vst1.64         {d16}, [r0,:64], r2
        vst1.64         {d17}, [r0,:64], r2
        bgt             3b

        pop             {r4-r7, pc}

4:      vld1.64         {d4, d5}, [r1], r2
        vld1.64         {d6, d7}, [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1

5:      pld             [r1]
        subs            r3,  r3,  #2
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
        vld1.64         {d4, d5}, [r1], r2
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d7,  d1
        pld             [r1]
        vext.8          d5,  d4,  d5,  #1
        vrshrn.u16      d16, q8,  #6
        vrshrn.u16      d17, q9,  #6
.ifc \type,avg
        vld1.64         {d20}, [lr,:64], r2
        vld1.64         {d21}, [lr,:64], r2
        vrhadd.u8       q8,  q8,  q10
.endif
        vld1.64         {d6, d7}, [r1], r2
        vext.8          d7,  d6,  d7,  #1
        vst1.64         {d16}, [r0,:64], r2
        vst1.64         {d17}, [r0,:64], r2
        bgt             5b

        pop             {r4-r7, pc}
endfunc
        .endm

/* chroma_mc4(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
        .macro  h264_chroma_mc4 type
function ff_\type\()_h264_chroma_mc4_neon, export=1
        push            {r4-r7, lr}
        ldrd            r4,  [sp, #20]
.ifc \type,avg
        mov             lr,  r0
.endif
        pld             [r1]
        pld             [r1, r2]

        muls            r7,  r4,  r5
        rsb             r6,  r7,  r5,  lsl #3
        rsb             ip,  r7,  r4,  lsl #3
        sub             r4,  r7,  r4,  lsl #3
        sub             r4,  r4,  r5,  lsl #3
        add             r4,  r4,  #64

        beq             2f

        add             r5,  r1,  r2

        vdup.8          d0,  r4
        lsl             r4,  r2,  #1
        vdup.8          d1,  ip
        vld1.64         {d4},     [r1], r4
        vdup.8          d2,  r6
        vld1.64         {d6},     [r5], r4
        vdup.8          d3,  r7

        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d4,  d5
        vtrn.32         d6,  d7

        vtrn.32         d0,  d1
        vtrn.32         d2,  d3

1:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d6,  d2
        vld1.64         {d4},     [r1], r4
        vext.8          d5,  d4,  d5,  #1
        vtrn.32         d4,  d5
        vmull.u8        q9,  d6,  d0
        vmlal.u8        q9,  d4,  d2
        vld1.64         {d6},     [r5], r4
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        vrshrn.u16      d16, q8,  #6
        subs            r3,  r3,  #2
        pld             [r1]
.ifc \type,avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
.endif
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d6,  d7
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             1b

        pop             {r4-r7, pc}

2:      tst             r6,  r6
        add             ip,  ip,  r6
        vdup.8          d0,  r4
        vdup.8          d1,  ip
        vtrn.32         d0,  d1

        beq             4f

        vext.32         d1,  d0,  d1,  #1
        add             r5,  r1,  r2
        lsl             r4,  r2,  #1
        vld1.32         {d4[0]},  [r1], r4
        vld1.32         {d4[1]},  [r5], r4

3:      pld             [r5]
        vmull.u8        q8,  d4,  d0
        vld1.32         {d4[0]},  [r1], r4
        vmull.u8        q9,  d4,  d1
        vld1.32         {d4[1]},  [r5], r4
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        vrshrn.u16      d16, q8,  #6
.ifc \type,avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
.endif
        subs            r3,  r3,  #2
        pld             [r1]
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             3b

        pop             {r4-r7, pc}

4:      vld1.64         {d4},     [r1], r2
        vld1.64         {d6},     [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d4,  d5
        vtrn.32         d6,  d7

5:      vmull.u8        q8,  d4,  d0
        vmull.u8        q9,  d6,  d0
        subs            r3,  r3,  #2
        vld1.64         {d4},     [r1], r2
        vext.8          d5,  d4,  d5,  #1
        vtrn.32         d4,  d5
        vadd.i16        d16, d16, d17
        vadd.i16        d17, d18, d19
        pld             [r1]
        vrshrn.u16      d16, q8,  #6
.ifc \type,avg
        vld1.32         {d20[0]}, [lr,:32], r2
        vld1.32         {d20[1]}, [lr,:32], r2
        vrhadd.u8       d16, d16, d20
.endif
        vld1.64         {d6},     [r1], r2
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         d6,  d7
        pld             [r1]
        vst1.32         {d16[0]}, [r0,:32], r2
        vst1.32         {d16[1]}, [r0,:32], r2
        bgt             5b

        pop             {r4-r7, pc}
endfunc
        .endm

        .macro  h264_chroma_mc2 type
function ff_\type\()_h264_chroma_mc2_neon, export=1
        push            {r4-r6, lr}
        ldr             r4,  [sp, #16]
        ldr             lr,  [sp, #20]
        pld             [r1]
        pld             [r1, r2]
        orrs            r5,  r4,  lr
        beq             2f

        mul             r5,  r4,  lr
        rsb             r6,  r5,  lr,  lsl #3
        rsb             r12, r5,  r4,  lsl #3
        sub             r4,  r5,  r4,  lsl #3
        sub             r4,  r4,  lr,  lsl #3
        add             r4,  r4,  #64
        vdup.8          d0,  r4
        vdup.8          d2,  r12
        vdup.8          d1,  r6
        vdup.8          d3,  r5
        vtrn.16         q0,  q1
1:
        vld1.32         {d4[0]},  [r1], r2
        vld1.32         {d4[1]},  [r1], r2
        vrev64.32       d5,  d4
        vld1.32         {d5[1]},  [r1]
        vext.8          q3,  q2,  q2,  #1
        vtrn.16         q2,  q3
        vmull.u8        q8,  d4,  d0
        vmlal.u8        q8,  d5,  d1
.ifc \type,avg
        vld1.16         {d18[0]}, [r0,:16], r2
        vld1.16         {d18[1]}, [r0,:16]
        sub             r0,  r0,  r2
.endif
        vtrn.32         d16, d17
        vadd.i16        d16, d16, d17
        vrshrn.u16      d16, q8,  #6
.ifc \type,avg
        vrhadd.u8       d16, d16, d18
.endif
        vst1.16         {d16[0]}, [r0,:16], r2
        vst1.16         {d16[1]}, [r0,:16], r2
        subs            r3,  r3,  #2
        bgt             1b
        pop             {r4-r6, pc}
2:
.ifc \type,put
        ldrh            r5,  [r1], r2
        strh            r5,  [r0], r2
        ldrh            r6,  [r1], r2
        strh            r6,  [r0], r2
.else
        vld1.16         {d16[0]}, [r1], r2
        vld1.16         {d16[1]}, [r1], r2
        vld1.16         {d18[0]}, [r0,:16], r2
        vld1.16         {d18[1]}, [r0,:16]
        sub             r0,  r0,  r2
        vrhadd.u8       d16, d16, d18
        vst1.16         {d16[0]}, [r0,:16], r2
        vst1.16         {d16[1]}, [r0,:16], r2
.endif
        subs            r3,  r3,  #2
        bgt             2b
        pop             {r4-r6, pc}
endfunc
.endm

        .text
        .align

        h264_chroma_mc8 put
        h264_chroma_mc8 avg
        h264_chroma_mc4 put
        h264_chroma_mc4 avg
        h264_chroma_mc2 put
        h264_chroma_mc2 avg

        /* H.264 loop filter */

        .macro h264_loop_filter_start
        ldr             ip,  [sp]
        tst             r2,  r2
        ldr             ip,  [ip]
        tstne           r3,  r3
        vmov.32         d24[0], ip
        and             ip,  ip,  ip, lsl #16
        bxeq            lr
        ands            ip,  ip,  ip, lsl #8
        bxlt            lr
        .endm

        .macro align_push_regs
        and             ip,  sp,  #15
        add             ip,  ip,  #32
        sub             sp,  sp,  ip
        vst1.64         {d12-d15}, [sp,:128]
        sub             sp,  sp,  #32
        vst1.64         {d8-d11},  [sp,:128]
        .endm

        .macro align_pop_regs
        vld1.64         {d8-d11},  [sp,:128]!
        vld1.64         {d12-d15}, [sp,:128], ip
        .endm

        .macro h264_loop_filter_luma
        vdup.8          q11, r2         @ alpha
        vmovl.u8        q12, d24
        vabd.u8         q6,  q8,  q0    @ abs(p0 - q0)
        vmovl.u16       q12, d24
        vabd.u8         q14, q9,  q8    @ abs(p1 - p0)
        vsli.16         q12, q12, #8
        vabd.u8         q15, q1,  q0    @ abs(q1 - q0)
        vsli.32         q12, q12, #16
        vclt.u8         q6,  q6,  q11   @ < alpha
        vdup.8          q11, r3         @ beta
        vclt.s8         q7,  q12, #0
        vclt.u8         q14, q14, q11   @ < beta
        vclt.u8         q15, q15, q11   @ < beta
        vbic            q6,  q6,  q7
        vabd.u8         q4,  q10, q8    @ abs(p2 - p0)
        vand            q6,  q6,  q14
        vabd.u8         q5,  q2,  q0    @ abs(q2 - q0)
        vclt.u8         q4,  q4,  q11   @ < beta
        vand            q6,  q6,  q15
        vclt.u8         q5,  q5,  q11   @ < beta
        vand            q4,  q4,  q6
        vand            q5,  q5,  q6
        vand            q12, q12, q6
        vrhadd.u8       q14, q8,  q0
        vsub.i8         q6,  q12, q4
        vqadd.u8        q7,  q9,  q12
        vhadd.u8        q10, q10, q14
        vsub.i8         q6,  q6,  q5
        vhadd.u8        q14, q2,  q14
        vmin.u8         q7,  q7,  q10
        vqsub.u8        q11, q9,  q12
        vqadd.u8        q2,  q1,  q12
        vmax.u8         q7,  q7,  q11
        vqsub.u8        q11, q1,  q12
        vmin.u8         q14, q2,  q14
        vmovl.u8        q2,  d0
        vmax.u8         q14, q14, q11
        vmovl.u8        q10, d1
        vsubw.u8        q2,  q2,  d16
        vsubw.u8        q10, q10, d17
        vshl.i16        q2,  q2,  #2
        vshl.i16        q10, q10, #2
        vaddw.u8        q2,  q2,  d18
        vaddw.u8        q10, q10, d19
        vsubw.u8        q2,  q2,  d2
        vsubw.u8        q10, q10, d3
        vrshrn.i16      d4,  q2,  #3
        vrshrn.i16      d5,  q10, #3
        vbsl            q4,  q7,  q9
        vbsl            q5,  q14, q1
        vneg.s8         q7,  q6
        vmovl.u8        q14, d16
        vmin.s8         q2,  q2,  q6
        vmovl.u8        q6,  d17
        vmax.s8         q2,  q2,  q7
        vmovl.u8        q11, d0
        vmovl.u8        q12, d1
        vaddw.s8        q14, q14, d4
        vaddw.s8        q6,  q6,  d5
        vsubw.s8        q11, q11, d4
        vsubw.s8        q12, q12, d5
        vqmovun.s16     d16, q14
        vqmovun.s16     d17, q6
        vqmovun.s16     d0,  q11
        vqmovun.s16     d1,  q12
        .endm

function ff_h264_v_loop_filter_luma_neon, export=1
        h264_loop_filter_start

        vld1.64         {d0, d1},  [r0,:128], r1
        vld1.64         {d2, d3},  [r0,:128], r1
        vld1.64         {d4, d5},  [r0,:128], r1
        sub             r0,  r0,  r1, lsl #2
        sub             r0,  r0,  r1, lsl #1
        vld1.64         {d20,d21}, [r0,:128], r1
        vld1.64         {d18,d19}, [r0,:128], r1
        vld1.64         {d16,d17}, [r0,:128], r1

        align_push_regs

        h264_loop_filter_luma

        sub             r0,  r0,  r1, lsl #1
        vst1.64         {d8, d9},  [r0,:128], r1
        vst1.64         {d16,d17}, [r0,:128], r1
        vst1.64         {d0, d1},  [r0,:128], r1
        vst1.64         {d10,d11}, [r0,:128]

        align_pop_regs
        bx              lr
endfunc

function ff_h264_h_loop_filter_luma_neon, export=1
        h264_loop_filter_start

        sub             r0,  r0,  #4
        vld1.64         {d6},  [r0], r1
        vld1.64         {d20}, [r0], r1
        vld1.64         {d18}, [r0], r1
        vld1.64         {d16}, [r0], r1
        vld1.64         {d0},  [r0], r1
        vld1.64         {d2},  [r0], r1
        vld1.64         {d4},  [r0], r1
        vld1.64         {d26}, [r0], r1
        vld1.64         {d7},  [r0], r1
        vld1.64         {d21}, [r0], r1
        vld1.64         {d19}, [r0], r1
        vld1.64         {d17}, [r0], r1
        vld1.64         {d1},  [r0], r1
        vld1.64         {d3},  [r0], r1
        vld1.64         {d5},  [r0], r1
        vld1.64         {d27}, [r0], r1

        transpose_8x8   q3, q10, q9, q8, q0, q1, q2, q13

        align_push_regs

        h264_loop_filter_luma

        transpose_4x4   q4, q8, q0, q5

        sub             r0,  r0,  r1, lsl #4
        add             r0,  r0,  #2
        vst1.32         {d8[0]},  [r0], r1
        vst1.32         {d16[0]}, [r0], r1
        vst1.32         {d0[0]},  [r0], r1
        vst1.32         {d10[0]}, [r0], r1
        vst1.32         {d8[1]},  [r0], r1
        vst1.32         {d16[1]}, [r0], r1
        vst1.32         {d0[1]},  [r0], r1
        vst1.32         {d10[1]}, [r0], r1
        vst1.32         {d9[0]},  [r0], r1
        vst1.32         {d17[0]}, [r0], r1
        vst1.32         {d1[0]},  [r0], r1
        vst1.32         {d11[0]}, [r0], r1
        vst1.32         {d9[1]},  [r0], r1
        vst1.32         {d17[1]}, [r0], r1
        vst1.32         {d1[1]},  [r0], r1
        vst1.32         {d11[1]}, [r0], r1

        align_pop_regs
        bx              lr
endfunc

        .macro h264_loop_filter_chroma
        vdup.8          d22, r2         @ alpha
        vmovl.u8        q12, d24
        vabd.u8         d26, d16, d0    @ abs(p0 - q0)
        vmovl.u8        q2,  d0
        vabd.u8         d28, d18, d16   @ abs(p1 - p0)
        vsubw.u8        q2,  q2,  d16
        vsli.16         d24, d24, #8
        vshl.i16        q2,  q2,  #2
        vabd.u8         d30, d2,  d0    @ abs(q1 - q0)
        vaddw.u8        q2,  q2,  d18
        vclt.u8         d26, d26, d22   @ < alpha
        vsubw.u8        q2,  q2,  d2
        vdup.8          d22, r3         @ beta
        vrshrn.i16      d4,  q2,  #3
        vclt.u8         d28, d28, d22   @ < beta
        vclt.u8         d30, d30, d22   @ < beta
        vmin.s8         d4,  d4,  d24
        vneg.s8         d25, d24
        vand            d26, d26, d28
        vmax.s8         d4,  d4,  d25
        vand            d26, d26, d30
        vmovl.u8        q11, d0
        vand            d4,  d4,  d26
        vmovl.u8        q14, d16
        vaddw.s8        q14, q14, d4
        vsubw.s8        q11, q11, d4
        vqmovun.s16     d16, q14
        vqmovun.s16     d0,  q11
        .endm

function ff_h264_v_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             r0,  r0,  r1, lsl #1
        vld1.64         {d18}, [r0,:64], r1
        vld1.64         {d16}, [r0,:64], r1
        vld1.64         {d0},  [r0,:64], r1
        vld1.64         {d2},  [r0,:64]

        h264_loop_filter_chroma

        sub             r0,  r0,  r1, lsl #1
        vst1.64         {d16}, [r0,:64], r1
        vst1.64         {d0},  [r0,:64], r1

        bx              lr
endfunc

function ff_h264_h_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             r0,  r0,  #2
        vld1.32         {d18[0]}, [r0], r1
        vld1.32         {d16[0]}, [r0], r1
        vld1.32         {d0[0]},  [r0], r1
        vld1.32         {d2[0]},  [r0], r1
        vld1.32         {d18[1]}, [r0], r1
        vld1.32         {d16[1]}, [r0], r1
        vld1.32         {d0[1]},  [r0], r1
        vld1.32         {d2[1]},  [r0], r1

        vtrn.16         d18, d0
        vtrn.16         d16, d2
        vtrn.8          d18, d16
        vtrn.8          d0,  d2

        h264_loop_filter_chroma

        vtrn.16         d18, d0
        vtrn.16         d16, d2
        vtrn.8          d18, d16
        vtrn.8          d0,  d2

        sub             r0,  r0,  r1, lsl #3
        vst1.32         {d18[0]}, [r0], r1
        vst1.32         {d16[0]}, [r0], r1
        vst1.32         {d0[0]},  [r0], r1
        vst1.32         {d2[0]},  [r0], r1
        vst1.32         {d18[1]}, [r0], r1
        vst1.32         {d16[1]}, [r0], r1
        vst1.32         {d0[1]},  [r0], r1
        vst1.32         {d2[1]},  [r0], r1

        bx              lr
endfunc

        /* H.264 qpel MC */

        .macro  lowpass_const r
        movw            \r,  #5
        movt            \r,  #20
        vmov.32         d6[0], \r
        .endm

        .macro  lowpass_8 r0, r1, r2, r3, d0, d1, narrow=1
.if \narrow
        t0 .req q0
        t1 .req q8
.else
        t0 .req \d0
        t1 .req \d1
.endif
        vext.8          d2,  \r0, \r1, #2
        vext.8          d3,  \r0, \r1, #3
        vaddl.u8        q1,  d2,  d3
        vext.8          d4,  \r0, \r1, #1
        vext.8          d5,  \r0, \r1, #4
        vaddl.u8        q2,  d4,  d5
        vext.8          d30, \r0, \r1, #5
        vaddl.u8        t0,  \r0, d30
        vext.8          d18, \r2, \r3, #2
        vmla.i16        t0,  q1,  d6[1]
        vext.8          d19, \r2, \r3, #3
        vaddl.u8        q9,  d18, d19
        vext.8          d20, \r2, \r3, #1
        vmls.i16        t0,  q2,  d6[0]
        vext.8          d21, \r2, \r3, #4
        vaddl.u8        q10, d20, d21
        vext.8          d31, \r2, \r3, #5
        vaddl.u8        t1,  \r2, d31
        vmla.i16        t1,  q9,  d6[1]
        vmls.i16        t1,  q10, d6[0]
.if \narrow
        vqrshrun.s16    \d0, t0,  #5
        vqrshrun.s16    \d1, t1,  #5
.endif
        .unreq  t0
        .unreq  t1
        .endm

        .macro  lowpass_8_1 r0, r1, d0, narrow=1
.if \narrow
        t0 .req q0
.else
        t0 .req \d0
.endif
        vext.8          d2,  \r0, \r1, #2
        vext.8          d3,  \r0, \r1, #3
        vaddl.u8        q1,  d2,  d3
        vext.8          d4,  \r0, \r1, #1
        vext.8          d5,  \r0, \r1, #4
        vaddl.u8        q2,  d4,  d5
        vext.8          d30, \r0, \r1, #5
        vaddl.u8        t0,  \r0, d30
        vmla.i16        t0,  q1,  d6[1]
        vmls.i16        t0,  q2,  d6[0]
.if \narrow
        vqrshrun.s16    \d0, t0,  #5
.endif
        .unreq  t0
        .endm

        .macro  lowpass_8.16 r0, r1, l0, h0, l1, h1, d
        vext.16         q1,  \r0, \r1, #2
        vext.16         q0,  \r0, \r1, #3
        vaddl.s16       q9,  d2,  d0
        vext.16         q2,  \r0, \r1, #1
        vaddl.s16       q1,  d3,  d1
        vext.16         q3,  \r0, \r1, #4
        vaddl.s16       q10, d4,  d6
        vext.16         \r1, \r0, \r1, #5
        vaddl.s16       q2,  d5,  d7
        vaddl.s16       q0,  \h0, \h1
        vaddl.s16       q8,  \l0, \l1

        vshl.i32        q3,  q9,  #4
        vshl.i32        q9,  q9,  #2
        vshl.i32        q15, q10, #2
        vadd.i32        q9,  q9,  q3
        vadd.i32        q10, q10, q15

        vshl.i32        q3,  q1,  #4
        vshl.i32        q1,  q1,  #2
        vshl.i32        q15, q2,  #2
        vadd.i32        q1,  q1,  q3
        vadd.i32        q2,  q2,  q15

        vadd.i32        q9,  q9,  q8
        vsub.i32        q9,  q9,  q10

        vadd.i32        q1,  q1,  q0
        vsub.i32        q1,  q1,  q2

        vrshrn.s32      d18, q9,  #10
        vrshrn.s32      d19, q1,  #10

        vqmovun.s16     \d,  q9
        .endm

function put_h264_qpel16_h_lowpass_neon_packed
        mov             r4,  lr
        mov             ip,  #16
        mov             r3,  #8
        bl              put_h264_qpel8_h_lowpass_neon
        sub             r1,  r1,  r2, lsl #4
        add             r1,  r1,  #8
        mov             ip,  #16
        mov             lr,  r4
        b               put_h264_qpel8_h_lowpass_neon
endfunc

        .macro h264_qpel_h_lowpass type
function \type\()_h264_qpel16_h_lowpass_neon
        push            {lr}
        mov             ip,  #16
        bl              \type\()_h264_qpel8_h_lowpass_neon
        sub             r0,  r0,  r3, lsl #4
        sub             r1,  r1,  r2, lsl #4
        add             r0,  r0,  #8
        add             r1,  r1,  #8
        mov             ip,  #16
        pop             {lr}
endfunc

function \type\()_h264_qpel8_h_lowpass_neon
1:      vld1.64         {d0, d1},  [r1], r2
        vld1.64         {d16,d17}, [r1], r2
        subs            ip,  ip,  #2
        lowpass_8       d0,  d1,  d16, d17, d0,  d16
.ifc \type,avg
        vld1.8          {d2},     [r0,:64], r3
        vrhadd.u8       d0,  d0,  d2
        vld1.8          {d3},     [r0,:64]
        vrhadd.u8       d16, d16, d3
        sub             r0,  r0,  r3
.endif
        vst1.64         {d0},     [r0,:64], r3
        vst1.64         {d16},    [r0,:64], r3
        bne             1b
        bx              lr
endfunc
        .endm

        h264_qpel_h_lowpass put
        h264_qpel_h_lowpass avg

        .macro h264_qpel_h_lowpass_l2 type
function \type\()_h264_qpel16_h_lowpass_l2_neon
        push            {lr}
        mov             ip,  #16
        bl              \type\()_h264_qpel8_h_lowpass_l2_neon
        sub             r0,  r0,  r2, lsl #4
        sub             r1,  r1,  r2, lsl #4
        sub             r3,  r3,  r2, lsl #4
        add             r0,  r0,  #8
        add             r1,  r1,  #8
        add             r3,  r3,  #8
        mov             ip,  #16
        pop             {lr}
endfunc

function \type\()_h264_qpel8_h_lowpass_l2_neon
1:      vld1.64         {d0, d1},  [r1], r2
        vld1.64         {d16,d17}, [r1], r2
        vld1.64         {d28},     [r3], r2
        vld1.64         {d29},     [r3], r2
        subs            ip,  ip,  #2
        lowpass_8       d0,  d1,  d16, d17, d0,  d1
        vrhadd.u8       q0,  q0,  q14
.ifc \type,avg
        vld1.8          {d2},      [r0,:64], r2
        vrhadd.u8       d0,  d0,  d2
        vld1.8          {d3},      [r0,:64]
        vrhadd.u8       d1,  d1,  d3
        sub             r0,  r0,  r2
.endif
        vst1.64         {d0},      [r0,:64], r2
        vst1.64         {d1},      [r0,:64], r2
        bne             1b
        bx              lr
endfunc
        .endm

        h264_qpel_h_lowpass_l2 put
        h264_qpel_h_lowpass_l2 avg

function put_h264_qpel16_v_lowpass_neon_packed
        mov             r4,  lr
        mov             r2,  #8
        bl              put_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        bl              put_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        bl              put_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r4
        b               put_h264_qpel8_v_lowpass_neon
endfunc

        .macro h264_qpel_v_lowpass type
function \type\()_h264_qpel16_v_lowpass_neon
        mov             r4,  lr
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             r0,  r0,  r2, lsl #4
        add             r0,  r0,  #8
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r4
endfunc

function \type\()_h264_qpel8_v_lowpass_neon
        vld1.64         {d8},  [r1], r3
        vld1.64         {d10}, [r1], r3
        vld1.64         {d12}, [r1], r3
        vld1.64         {d14}, [r1], r3
        vld1.64         {d22}, [r1], r3
        vld1.64         {d24}, [r1], r3
        vld1.64         {d26}, [r1], r3
        vld1.64         {d28}, [r1], r3
        vld1.64         {d9},  [r1], r3
        vld1.64         {d11}, [r1], r3
        vld1.64         {d13}, [r1], r3
        vld1.64         {d15}, [r1], r3
        vld1.64         {d23}, [r1]

        transpose_8x8   q4,  q5,  q6,  q7,  q11, q12, q13, q14
        lowpass_8       d8,  d9,  d10, d11, d8,  d10
        lowpass_8       d12, d13, d14, d15, d12, d14
        lowpass_8       d22, d23, d24, d25, d22, d24
        lowpass_8       d26, d27, d28, d29, d26, d28
        transpose_8x8   d8,  d10, d12, d14, d22, d24, d26, d28

.ifc \type,avg
        vld1.8          {d9},  [r0,:64], r2
        vrhadd.u8       d8,  d8,  d9
        vld1.8          {d11}, [r0,:64], r2
        vrhadd.u8       d10, d10, d11
        vld1.8          {d13}, [r0,:64], r2
        vrhadd.u8       d12, d12, d13
        vld1.8          {d15}, [r0,:64], r2
        vrhadd.u8       d14, d14, d15
        vld1.8          {d23}, [r0,:64], r2
        vrhadd.u8       d22, d22, d23
        vld1.8          {d25}, [r0,:64], r2
        vrhadd.u8       d24, d24, d25
        vld1.8          {d27}, [r0,:64], r2
        vrhadd.u8       d26, d26, d27
        vld1.8          {d29}, [r0,:64], r2
        vrhadd.u8       d28, d28, d29
        sub             r0,  r0,  r2,  lsl #3
.endif

        vst1.64         {d8},  [r0,:64], r2
        vst1.64         {d10}, [r0,:64], r2
        vst1.64         {d12}, [r0,:64], r2
        vst1.64         {d14}, [r0,:64], r2
        vst1.64         {d22}, [r0,:64], r2
        vst1.64         {d24}, [r0,:64], r2
        vst1.64         {d26}, [r0,:64], r2
        vst1.64         {d28}, [r0,:64], r2

        bx              lr
endfunc
        .endm

        h264_qpel_v_lowpass put
        h264_qpel_v_lowpass avg

        .macro h264_qpel_v_lowpass_l2 type
function \type\()_h264_qpel16_v_lowpass_l2_neon
        mov             r4,  lr
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             r0,  r0,  r3, lsl #4
        sub             ip,  ip,  r2, lsl #4
        add             r0,  r0,  #8
        add             ip,  ip,  #8
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r4
endfunc

function \type\()_h264_qpel8_v_lowpass_l2_neon
        vld1.64         {d8},  [r1], r3
        vld1.64         {d10}, [r1], r3
        vld1.64         {d12}, [r1], r3
        vld1.64         {d14}, [r1], r3
        vld1.64         {d22}, [r1], r3
        vld1.64         {d24}, [r1], r3
        vld1.64         {d26}, [r1], r3
        vld1.64         {d28}, [r1], r3
        vld1.64         {d9},  [r1], r3
        vld1.64         {d11}, [r1], r3
        vld1.64         {d13}, [r1], r3
        vld1.64         {d15}, [r1], r3
        vld1.64         {d23}, [r1]

        transpose_8x8   q4,  q5,  q6,  q7,  q11, q12, q13, q14
        lowpass_8       d8,  d9,  d10, d11, d8,  d9
        lowpass_8       d12, d13, d14, d15, d12, d13
        lowpass_8       d22, d23, d24, d25, d22, d23
        lowpass_8       d26, d27, d28, d29, d26, d27
        transpose_8x8   d8,  d9,  d12, d13, d22, d23, d26, d27

        vld1.64         {d0},  [ip], r2
        vld1.64         {d1},  [ip], r2
        vld1.64         {d2},  [ip], r2
        vld1.64         {d3},  [ip], r2
        vld1.64         {d4},  [ip], r2
        vrhadd.u8       q0,  q0,  q4
        vld1.64         {d5},  [ip], r2
        vrhadd.u8       q1,  q1,  q6
        vld1.64         {d10}, [ip], r2
        vrhadd.u8       q2,  q2,  q11
        vld1.64         {d11}, [ip], r2
        vrhadd.u8       q5,  q5,  q13

.ifc \type,avg
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d0,  d0,  d16
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d1,  d1,  d17
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d2,  d2,  d16
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d3,  d3,  d17
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d4,  d4,  d16
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d5,  d5,  d17
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d10, d10, d16
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d11, d11, d17
        sub             r0,  r0,  r3,  lsl #3
.endif

        vst1.64         {d0},  [r0,:64], r3
        vst1.64         {d1},  [r0,:64], r3
        vst1.64         {d2},  [r0,:64], r3
        vst1.64         {d3},  [r0,:64], r3
        vst1.64         {d4},  [r0,:64], r3
        vst1.64         {d5},  [r0,:64], r3
        vst1.64         {d10}, [r0,:64], r3
        vst1.64         {d11}, [r0,:64], r3

        bx              lr
endfunc
        .endm

        h264_qpel_v_lowpass_l2 put
        h264_qpel_v_lowpass_l2 avg

function put_h264_qpel8_hv_lowpass_neon_top
        lowpass_const   ip
        mov             ip,  #12
1:      vld1.64         {d0, d1},  [r1], r3
        vld1.64         {d16,d17}, [r1], r3
        subs            ip,  ip,  #2
        lowpass_8       d0,  d1,  d16, d17, q11, q12, narrow=0
        vst1.64         {d22-d25}, [r4,:128]!
        bne             1b

        vld1.64         {d0, d1},  [r1]
        lowpass_8_1     d0,  d1,  q12, narrow=0

        mov             ip,  #-16
        add             r4,  r4,  ip
        vld1.64         {d30,d31}, [r4,:128], ip
        vld1.64         {d20,d21}, [r4,:128], ip
        vld1.64         {d18,d19}, [r4,:128], ip
        vld1.64         {d16,d17}, [r4,:128], ip
        vld1.64         {d14,d15}, [r4,:128], ip
        vld1.64         {d12,d13}, [r4,:128], ip
        vld1.64         {d10,d11}, [r4,:128], ip
        vld1.64         {d8, d9},  [r4,:128], ip
        vld1.64         {d6, d7},  [r4,:128], ip
        vld1.64         {d4, d5},  [r4,:128], ip
        vld1.64         {d2, d3},  [r4,:128], ip
        vld1.64         {d0, d1},  [r4,:128]

        swap4           d1,  d3,  d5,  d7,  d8,  d10, d12, d14
        transpose16_4x4 q0,  q1,  q2,  q3,  q4,  q5,  q6,  q7

        swap4           d17, d19, d21, d31, d24, d26, d28, d22
        transpose16_4x4 q8,  q9,  q10, q15, q12, q13, q14, q11

        vst1.64         {d30,d31}, [r4,:128]!
        vst1.64         {d6, d7},  [r4,:128]!
        vst1.64         {d20,d21}, [r4,:128]!
        vst1.64         {d4, d5},  [r4,:128]!
        vst1.64         {d18,d19}, [r4,:128]!
        vst1.64         {d2, d3},  [r4,:128]!
        vst1.64         {d16,d17}, [r4,:128]!
        vst1.64         {d0, d1},  [r4,:128]

        lowpass_8.16    q4,  q12, d8,  d9,  d24, d25, d8
        lowpass_8.16    q5,  q13, d10, d11, d26, d27, d9
        lowpass_8.16    q6,  q14, d12, d13, d28, d29, d10
        lowpass_8.16    q7,  q11, d14, d15, d22, d23, d11

        vld1.64         {d16,d17}, [r4,:128], ip
        vld1.64         {d30,d31}, [r4,:128], ip
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d12
        vld1.64         {d16,d17}, [r4,:128], ip
        vld1.64         {d30,d31}, [r4,:128], ip
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d13
        vld1.64         {d16,d17}, [r4,:128], ip
        vld1.64         {d30,d31}, [r4,:128], ip
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d14
        vld1.64         {d16,d17}, [r4,:128], ip
        vld1.64         {d30,d31}, [r4,:128]
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d15

        transpose_8x8   d12, d13, d14, d15, d8,  d9,  d10, d11

        bx              lr
endfunc

        .macro h264_qpel8_hv_lowpass type
function \type\()_h264_qpel8_hv_lowpass_neon
        mov             r10, lr
        bl              put_h264_qpel8_hv_lowpass_neon_top
.ifc \type,avg
        vld1.8          {d0},      [r0,:64], r2
        vrhadd.u8       d12, d12, d0
        vld1.8          {d1},      [r0,:64], r2
        vrhadd.u8       d13, d13, d1
        vld1.8          {d2},      [r0,:64], r2
        vrhadd.u8       d14, d14, d2
        vld1.8          {d3},      [r0,:64], r2
        vrhadd.u8       d15, d15, d3
        vld1.8          {d4},      [r0,:64], r2
        vrhadd.u8       d8,  d8,  d4
        vld1.8          {d5},      [r0,:64], r2
        vrhadd.u8       d9,  d9,  d5
        vld1.8          {d6},      [r0,:64], r2
        vrhadd.u8       d10, d10, d6
        vld1.8          {d7},      [r0,:64], r2
        vrhadd.u8       d11, d11, d7
        sub             r0,  r0,  r2,  lsl #3
.endif
        vst1.64         {d12},     [r0,:64], r2
        vst1.64         {d13},     [r0,:64], r2
        vst1.64         {d14},     [r0,:64], r2
        vst1.64         {d15},     [r0,:64], r2
        vst1.64         {d8},      [r0,:64], r2
        vst1.64         {d9},      [r0,:64], r2
        vst1.64         {d10},     [r0,:64], r2
        vst1.64         {d11},     [r0,:64], r2

        mov             lr,  r10
        bx              lr
endfunc
        .endm

        h264_qpel8_hv_lowpass put
        h264_qpel8_hv_lowpass avg

        .macro h264_qpel8_hv_lowpass_l2 type
function \type\()_h264_qpel8_hv_lowpass_l2_neon
        mov             r10, lr
        bl              put_h264_qpel8_hv_lowpass_neon_top

        vld1.64         {d0, d1},  [r2,:128]!
        vld1.64         {d2, d3},  [r2,:128]!
        vrhadd.u8       q0,  q0,  q6
        vld1.64         {d4, d5},  [r2,:128]!
        vrhadd.u8       q1,  q1,  q7
        vld1.64         {d6, d7},  [r2,:128]!
        vrhadd.u8       q2,  q2,  q4
        vrhadd.u8       q3,  q3,  q5
.ifc \type,avg
        vld1.8          {d16},     [r0,:64], r3
        vrhadd.u8       d0,  d0,  d16
        vld1.8          {d17},     [r0,:64], r3
        vrhadd.u8       d1,  d1,  d17
        vld1.8          {d18},     [r0,:64], r3
        vrhadd.u8       d2,  d2,  d18
        vld1.8          {d19},     [r0,:64], r3
        vrhadd.u8       d3,  d3,  d19
        vld1.8          {d20},     [r0,:64], r3
        vrhadd.u8       d4,  d4,  d20
        vld1.8          {d21},     [r0,:64], r3
        vrhadd.u8       d5,  d5,  d21
        vld1.8          {d22},     [r0,:64], r3
        vrhadd.u8       d6,  d6,  d22
        vld1.8          {d23},     [r0,:64], r3
        vrhadd.u8       d7,  d7,  d23
        sub             r0,  r0,  r3,  lsl #3
.endif
        vst1.64         {d0},      [r0,:64], r3
        vst1.64         {d1},      [r0,:64], r3
        vst1.64         {d2},      [r0,:64], r3
        vst1.64         {d3},      [r0,:64], r3
        vst1.64         {d4},      [r0,:64], r3
        vst1.64         {d5},      [r0,:64], r3
        vst1.64         {d6},      [r0,:64], r3
        vst1.64         {d7},      [r0,:64], r3

        mov             lr,  r10
        bx              lr
endfunc
        .endm

        h264_qpel8_hv_lowpass_l2 put
        h264_qpel8_hv_lowpass_l2 avg

        .macro h264_qpel16_hv type
function \type\()_h264_qpel16_hv_lowpass_neon
        mov             r9,  lr
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        sub             r0,  r0,  r2, lsl #4
        add             r0,  r0,  #8
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r9
        b               \type\()_h264_qpel8_hv_lowpass_neon
endfunc

function \type\()_h264_qpel16_hv_lowpass_l2_neon
        mov             r9,  lr
        sub             r2,  r4,  #256
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        sub             r0,  r0,  r3, lsl #4
        add             r0,  r0,  #8
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r9
        b               \type\()_h264_qpel8_hv_lowpass_l2_neon
endfunc
        .endm

        h264_qpel16_hv put
        h264_qpel16_hv avg

        .macro h264_qpel8 type
function ff_\type\()_h264_qpel8_mc10_neon, export=1
        lowpass_const   r3
        mov             r3,  r1
        sub             r1,  r1,  #2
        mov             ip,  #8
        b               \type\()_h264_qpel8_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel8_mc20_neon, export=1
        lowpass_const   r3
        sub             r1,  r1,  #2
        mov             r3,  r2
        mov             ip,  #8
        b               \type\()_h264_qpel8_h_lowpass_neon
endfunc

function ff_\type\()_h264_qpel8_mc30_neon, export=1
        lowpass_const   r3
        add             r3,  r1,  #1
        sub             r1,  r1,  #2
        mov             ip,  #8
        b               \type\()_h264_qpel8_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel8_mc01_neon, export=1
        push            {lr}
        mov             ip,  r1
\type\()_h264_qpel8_mc01:
        lowpass_const   r3
        mov             r3,  r2
        sub             r1,  r1,  r2, lsl #1
        vpush           {d8-d15}
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        vpop            {d8-d15}
        pop             {pc}
endfunc

function ff_\type\()_h264_qpel8_mc11_neon, export=1
        push            {r0, r1, r11, lr}
\type\()_h264_qpel8_mc11:
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             sp,  sp,  #64
        mov             r0,  sp
        sub             r1,  r1,  #2
        mov             r3,  #8
        mov             ip,  #8
        vpush           {d8-d15}
        bl              put_h264_qpel8_h_lowpass_neon
        ldrd            r0,  [r11]
        mov             r3,  r2
        add             ip,  sp,  #64
        sub             r1,  r1,  r2, lsl #1
        mov             r2,  #8
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        vpop            {d8-d15}
        add             sp,  r11, #8
        pop             {r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc21_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
\type\()_h264_qpel8_mc21:
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             sp,  sp,  #(8*8+16*12)
        sub             r1,  r1,  #2
        mov             r3,  #8
        mov             r0,  sp
        mov             ip,  #8
        vpush           {d8-d15}
        bl              put_h264_qpel8_h_lowpass_neon
        mov             r4,  r0
        ldrd            r0,  [r11]
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        sub             r2,  r4,  #64
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        vpop            {d8-d15}
        add             sp,  r11,  #8
        pop             {r4, r10, r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc31_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r11, lr}
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel8_mc11
endfunc

function ff_\type\()_h264_qpel8_mc02_neon, export=1
        push            {lr}
        lowpass_const   r3
        sub             r1,  r1,  r2, lsl #1
        mov             r3,  r2
        vpush           {d8-d15}
        bl              \type\()_h264_qpel8_v_lowpass_neon
        vpop            {d8-d15}
        pop             {pc}
endfunc

function ff_\type\()_h264_qpel8_mc12_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
\type\()_h264_qpel8_mc12:
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             sp,  sp,  #(8*8+16*12)
        sub             r1,  r1,  r2, lsl #1
        mov             r3,  r2
        mov             r2,  #8
        mov             r0,  sp
        vpush           {d8-d15}
        bl              put_h264_qpel8_v_lowpass_neon
        mov             r4,  r0
        ldrd            r0,  [r11]
        sub             r1,  r1,  r3, lsl #1
        sub             r1,  r1,  #2
        sub             r2,  r4,  #64
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        vpop            {d8-d15}
        add             sp,  r11,  #8
        pop             {r4, r10, r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc22_neon, export=1
        push            {r4, r10, r11, lr}
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        sub             sp,  sp,  #(16*12)
        mov             r4,  sp
        vpush           {d8-d15}
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r10, r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc32_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
        add             r1,  r1,  #1
        b               \type\()_h264_qpel8_mc12
endfunc

function ff_\type\()_h264_qpel8_mc03_neon, export=1
        push            {lr}
        add             ip,  r1,  r2
        b               \type\()_h264_qpel8_mc01
endfunc

function ff_\type\()_h264_qpel8_mc13_neon, export=1
        push            {r0, r1, r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel8_mc11
endfunc

function ff_\type\()_h264_qpel8_mc23_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel8_mc21
endfunc

function ff_\type\()_h264_qpel8_mc33_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r11, lr}
        add             r1,  r1,  r2
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel8_mc11
endfunc
        .endm

        h264_qpel8 put
        h264_qpel8 avg

        .macro h264_qpel16 type
function ff_\type\()_h264_qpel16_mc10_neon, export=1
        lowpass_const   r3
        mov             r3,  r1
        sub             r1,  r1,  #2
        b               \type\()_h264_qpel16_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel16_mc20_neon, export=1
        lowpass_const   r3
        sub             r1,  r1,  #2
        mov             r3,  r2
        b               \type\()_h264_qpel16_h_lowpass_neon
endfunc

function ff_\type\()_h264_qpel16_mc30_neon, export=1
        lowpass_const   r3
        add             r3,  r1,  #1
        sub             r1,  r1,  #2
        b               \type\()_h264_qpel16_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel16_mc01_neon, export=1
        push            {r4, lr}
        mov             ip,  r1
\type\()_h264_qpel16_mc01:
        lowpass_const   r3
        mov             r3,  r2
        sub             r1,  r1,  r2, lsl #1
        vpush           {d8-d15}
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_h264_qpel16_mc11_neon, export=1
        push            {r0, r1, r4, r11, lr}
\type\()_h264_qpel16_mc11:
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             sp,  sp,  #256
        mov             r0,  sp
        sub             r1,  r1,  #2
        mov             r3,  #16
        vpush           {d8-d15}
        bl              put_h264_qpel16_h_lowpass_neon
        ldrd            r0,  [r11]
        mov             r3,  r2
        add             ip,  sp,  #64
        sub             r1,  r1,  r2, lsl #1
        mov             r2,  #16
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon
        vpop            {d8-d15}
        add             sp,  r11, #8
        pop             {r4, r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc21_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
\type\()_h264_qpel16_mc21:
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             sp,  sp,  #(16*16+16*12)
        sub             r1,  r1,  #2
        mov             r0,  sp
        vpush           {d8-d15}
        bl              put_h264_qpel16_h_lowpass_neon_packed
        mov             r4,  r0
        ldrd            r0,  [r11]
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        bl              \type\()_h264_qpel16_hv_lowpass_l2_neon
        vpop            {d8-d15}
        add             sp,  r11,  #8
        pop             {r4-r5, r9-r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc31_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r4, r11, lr}
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel16_mc11
endfunc

function ff_\type\()_h264_qpel16_mc02_neon, export=1
        push            {r4, lr}
        lowpass_const   r3
        sub             r1,  r1,  r2, lsl #1
        mov             r3,  r2
        vpush           {d8-d15}
        bl              \type\()_h264_qpel16_v_lowpass_neon
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_h264_qpel16_mc12_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
\type\()_h264_qpel16_mc12:
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             sp,  sp,  #(16*16+16*12)
        sub             r1,  r1,  r2, lsl #1
        mov             r0,  sp
        mov             r3,  r2
        vpush           {d8-d15}
        bl              put_h264_qpel16_v_lowpass_neon_packed
        mov             r4,  r0
        ldrd            r0,  [r11]
        sub             r1,  r1,  r3, lsl #1
        sub             r1,  r1,  #2
        mov             r2,  r3
        bl              \type\()_h264_qpel16_hv_lowpass_l2_neon
        vpop            {d8-d15}
        add             sp,  r11,  #8
        pop             {r4-r5, r9-r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc22_neon, export=1
        push            {r4, r9-r11, lr}
        lowpass_const   r3
        mov             r11, sp
        bic             sp,  sp,  #15
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        sub             sp,  sp,  #(16*12)
        mov             r4,  sp
        vpush           {d8-d15}
        bl              \type\()_h264_qpel16_hv_lowpass_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r9-r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc32_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
        add             r1,  r1,  #1
        b               \type\()_h264_qpel16_mc12
endfunc

function ff_\type\()_h264_qpel16_mc03_neon, export=1
        push            {r4, lr}
        add             ip,  r1,  r2
        b               \type\()_h264_qpel16_mc01
endfunc

function ff_\type\()_h264_qpel16_mc13_neon, export=1
        push            {r0, r1, r4, r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel16_mc11
endfunc

function ff_\type\()_h264_qpel16_mc23_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel16_mc21
endfunc

function ff_\type\()_h264_qpel16_mc33_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r4, r11, lr}
        add             r1,  r1,  r2
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel16_mc11
endfunc
        .endm

        h264_qpel16 put
        h264_qpel16 avg

@ Biweighted prediction

        .macro  biweight_16 macs, macd
        vdup.8          d0,  r4
        vdup.8          d1,  r5
        vmov            q2,  q8
        vmov            q3,  q8
1:      subs            ip,  ip,  #2
        vld1.8          {d20-d21},[r0,:128], r2
        \macd           q2,  d0,  d20
        pld             [r0]
        \macd           q3,  d0,  d21
        vld1.8          {d22-d23},[r1,:128], r2
        \macs           q2,  d1,  d22
        pld             [r1]
        \macs           q3,  d1,  d23
        vmov            q12, q8
        vld1.8          {d28-d29},[r0,:128], r2
        vmov            q13, q8
        \macd           q12, d0,  d28
        pld             [r0]
        \macd           q13, d0,  d29
        vld1.8          {d30-d31},[r1,:128], r2
        \macs           q12, d1,  d30
        pld             [r1]
        \macs           q13, d1,  d31
        vshl.s16        q2,  q2,  q9
        vshl.s16        q3,  q3,  q9
        vqmovun.s16     d4,  q2
        vqmovun.s16     d5,  q3
        vshl.s16        q12, q12, q9
        vshl.s16        q13, q13, q9
        vqmovun.s16     d24, q12
        vqmovun.s16     d25, q13
        vmov            q3,  q8
        vst1.8          {d4- d5}, [r6,:128], r2
        vmov            q2,  q8
        vst1.8          {d24-d25},[r6,:128], r2
        bne             1b
        pop             {r4-r6, pc}
        .endm

        .macro  biweight_8 macs, macd
        vdup.8          d0,  r4
        vdup.8          d1,  r5
        vmov            q1,  q8
        vmov            q10, q8
1:      subs            ip,  ip,  #2
        vld1.8          {d4},[r0,:64], r2
        \macd           q1,  d0,  d4
        pld             [r0]
        vld1.8          {d5},[r1,:64], r2
        \macs           q1,  d1,  d5
        pld             [r1]
        vld1.8          {d6},[r0,:64], r2
        \macd           q10, d0,  d6
        pld             [r0]
        vld1.8          {d7},[r1,:64], r2
        \macs           q10, d1,  d7
        pld             [r1]
        vshl.s16        q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vshl.s16        q10, q10, q9
        vqmovun.s16     d4,  q10
        vmov            q10, q8
        vst1.8          {d2},[r6,:64], r2
        vmov            q1,  q8
        vst1.8          {d4},[r6,:64], r2
        bne             1b
        pop             {r4-r6, pc}
        .endm

        .macro  biweight_4 macs, macd
        vdup.8          d0,  r4
        vdup.8          d1,  r5
        vmov            q1,  q8
        vmov            q10, q8
1:      subs            ip,  ip,  #4
        vld1.32         {d4[0]},[r0,:32], r2
        vld1.32         {d4[1]},[r0,:32], r2
        \macd           q1,  d0,  d4
        pld             [r0]
        vld1.32         {d5[0]},[r1,:32], r2
        vld1.32         {d5[1]},[r1,:32], r2
        \macs           q1,  d1,  d5
        pld             [r1]
        blt             2f
        vld1.32         {d6[0]},[r0,:32], r2
        vld1.32         {d6[1]},[r0,:32], r2
        \macd           q10, d0,  d6
        pld             [r0]
        vld1.32         {d7[0]},[r1,:32], r2
        vld1.32         {d7[1]},[r1,:32], r2
        \macs           q10, d1,  d7
        pld             [r1]
        vshl.s16        q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vshl.s16        q10, q10, q9
        vqmovun.s16     d4,  q10
        vmov            q10, q8
        vst1.32         {d2[0]},[r6,:32], r2
        vst1.32         {d2[1]},[r6,:32], r2
        vmov            q1,  q8
        vst1.32         {d4[0]},[r6,:32], r2
        vst1.32         {d4[1]},[r6,:32], r2
        bne             1b
        pop             {r4-r6, pc}
2:      vshl.s16        q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vst1.32         {d2[0]},[r6,:32], r2
        vst1.32         {d2[1]},[r6,:32], r2
        pop             {r4-r6, pc}
        .endm

        .macro  biweight_func w
function biweight_h264_pixels_\w\()_neon
        push            {r4-r6, lr}
        add             r4,  sp,  #16
        ldm             r4,  {r4-r6}
        lsr             lr,  r4,  #31
        add             r6,  r6,  #1
        eors            lr,  lr,  r5,  lsr #30
        orr             r6,  r6,  #1
        vdup.16         q9,  r3
        lsl             r6,  r6,  r3
        vmvn            q9,  q9
        vdup.16         q8,  r6
        mov             r6,  r0
        beq             10f
        subs            lr,  lr,  #1
        beq             20f
        subs            lr,  lr,  #1
        beq             30f
        b               40f
10:     biweight_\w     vmlal.u8, vmlal.u8
20:     rsb             r4,  r4,  #0
        biweight_\w     vmlal.u8, vmlsl.u8
30:     rsb             r4,  r4,  #0
        rsb             r5,  r5,  #0
        biweight_\w     vmlsl.u8, vmlsl.u8
40:     rsb             r5,  r5,  #0
        biweight_\w     vmlsl.u8, vmlal.u8
endfunc
        .endm

        .macro  biweight_entry w, h, b=1
function ff_biweight_h264_pixels_\w\()x\h\()_neon, export=1
        mov             ip,  #\h
.if \b
        b               biweight_h264_pixels_\w\()_neon
.endif
endfunc
        .endm

        biweight_entry  16, 8
        biweight_entry  16, 16, b=0
        biweight_func   16

        biweight_entry  8,  16
        biweight_entry  8,  4
        biweight_entry  8,  8,  b=0
        biweight_func   8

        biweight_entry  4,  8
        biweight_entry  4,  2
        biweight_entry  4,  4,  b=0
        biweight_func   4

@ Weighted prediction

        .macro  weight_16 add
        vdup.8          d0,  r3
1:      subs            ip,  ip,  #2
        vld1.8          {d20-d21},[r0,:128], r1
        vmull.u8        q2,  d0,  d20
        pld             [r0]
        vmull.u8        q3,  d0,  d21
        vld1.8          {d28-d29},[r0,:128], r1
        vmull.u8        q12, d0,  d28
        pld             [r0]
        vmull.u8        q13, d0,  d29
        \add            q2,  q8,  q2
        vrshl.s16       q2,  q2,  q9
        \add            q3,  q8,  q3
        vrshl.s16       q3,  q3,  q9
        vqmovun.s16     d4,  q2
        vqmovun.s16     d5,  q3
        \add            q12, q8,  q12
        vrshl.s16       q12, q12, q9
        \add            q13, q8,  q13
        vrshl.s16       q13, q13, q9
        vqmovun.s16     d24, q12
        vqmovun.s16     d25, q13
        vst1.8          {d4- d5}, [r4,:128], r1
        vst1.8          {d24-d25},[r4,:128], r1
        bne             1b
        pop             {r4, pc}
        .endm

        .macro  weight_8 add
        vdup.8          d0,  r3
1:      subs            ip,  ip,  #2
        vld1.8          {d4},[r0,:64], r1
        vmull.u8        q1,  d0,  d4
        pld             [r0]
        vld1.8          {d6},[r0,:64], r1
        vmull.u8        q10, d0,  d6
        \add            q1,  q8,  q1
        pld             [r0]
        vrshl.s16       q1,  q1,  q9
        vqmovun.s16     d2,  q1
        \add            q10, q8,  q10
        vrshl.s16       q10, q10, q9
        vqmovun.s16     d4,  q10
        vst1.8          {d2},[r4,:64], r1
        vst1.8          {d4},[r4,:64], r1
        bne             1b
        pop             {r4, pc}
        .endm

        .macro  weight_4 add
        vdup.8          d0,  r3
        vmov            q1,  q8
        vmov            q10, q8
1:      subs            ip,  ip,  #4
        vld1.32         {d4[0]},[r0,:32], r1
        vld1.32         {d4[1]},[r0,:32], r1
        vmull.u8        q1,  d0,  d4
        pld             [r0]
        blt             2f
        vld1.32         {d6[0]},[r0,:32], r1
        vld1.32         {d6[1]},[r0,:32], r1
        vmull.u8        q10, d0,  d6
        pld             [r0]
        \add            q1,  q8,  q1
        vrshl.s16       q1,  q1,  q9
        vqmovun.s16     d2,  q1
        \add            q10, q8,  q10
        vrshl.s16       q10, q10, q9
        vqmovun.s16     d4,  q10
        vmov            q10, q8
        vst1.32         {d2[0]},[r4,:32], r1
        vst1.32         {d2[1]},[r4,:32], r1
        vmov            q1,  q8
        vst1.32         {d4[0]},[r4,:32], r1
        vst1.32         {d4[1]},[r4,:32], r1
        bne             1b
        pop             {r4, pc}
2:      \add            q1,  q8,  q1
        vrshl.s16       q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vst1.32         {d2[0]},[r4,:32], r1
        vst1.32         {d2[1]},[r4,:32], r1
        pop             {r4, pc}
        .endm

        .macro  weight_func w
function weight_h264_pixels_\w\()_neon
        push            {r4, lr}
        ldr             r4,  [sp, #8]
        cmp             r2,  #1
        lsl             r4,  r4,  r2
        vdup.16         q8,  r4
        mov             r4,  r0
        ble             20f
        rsb             lr,  r2,  #1
        vdup.16         q9,  lr
        cmp             r3,  #0
        blt             10f
        weight_\w       vhadd.s16
10:     rsb             r3,  r3,  #0
        weight_\w       vhsub.s16
20:     rsb             lr,  r2,  #0
        vdup.16         q9,  lr
        cmp             r3,  #0
        blt             10f
        weight_\w       vadd.s16
10:     rsb             r3,  r3,  #0
        weight_\w       vsub.s16
endfunc
        .endm

        .macro  weight_entry w, h, b=1
function ff_weight_h264_pixels_\w\()x\h\()_neon, export=1
        mov             ip,  #\h
.if \b
        b               weight_h264_pixels_\w\()_neon
.endif
endfunc
        .endm

        weight_entry    16, 8
        weight_entry    16, 16, b=0
        weight_func     16

        weight_entry    8,  16
        weight_entry    8,  4
        weight_entry    8,  8,  b=0
        weight_func     8

        weight_entry    4,  8
        weight_entry    4,  2
        weight_entry    4,  4,  b=0
        weight_func     4
