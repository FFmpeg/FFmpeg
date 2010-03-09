/*
 * ARM NEON IDCT
 *
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * Based on Simple IDCT
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
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

#define W1  22725  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W2  21407  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W3  19266  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W4  16383  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W5  12873  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W6  8867   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W7  4520   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W4c ((1<<(COL_SHIFT-1))/W4)
#define ROW_SHIFT 11
#define COL_SHIFT 20

#define w1 d0[0]
#define w2 d0[1]
#define w3 d0[2]
#define w4 d0[3]
#define w5 d1[0]
#define w6 d1[1]
#define w7 d1[2]
#define w4c d1[3]

        .macro idct_col4_top
        vmull.s16       q7,  d6,  w2    /* q9   = W2 * col[2] */
        vmull.s16       q8,  d6,  w6    /* q10  = W6 * col[2] */
        vmull.s16       q9,  d4,  w1    /* q9   = W1 * col[1] */
        vadd.i32        q11, q15, q7
        vmull.s16       q10, d4,  w3    /* q10  = W3 * col[1] */
        vadd.i32        q12, q15, q8
        vmull.s16       q5,  d4,  w5    /* q5   = W5 * col[1] */
        vsub.i32        q13, q15, q8
        vmull.s16       q6,  d4,  w7    /* q6   = W7 * col[1] */
        vsub.i32        q14, q15, q7

        vmlal.s16       q9,  d8,  w3    /* q9  += W3 * col[3] */
        vmlsl.s16       q10, d8,  w7    /* q10 -= W7 * col[3] */
        vmlsl.s16       q5,  d8,  w1    /* q5  -= W1 * col[3] */
        vmlsl.s16       q6,  d8,  w5    /* q6  -= W5 * col[3] */
        .endm

        .text
        .align 6

function idct_row4_pld_neon
        pld             [r0]
        add             r3,  r0,  r1,  lsl #2
        pld             [r0, r1]
        pld             [r0, r1, lsl #1]
        pld             [r3, -r1]
        pld             [r3]
        pld             [r3, r1]
        add             r3,  r3,  r1,  lsl #1
        pld             [r3]
        pld             [r3, r1]
endfunc

function idct_row4_neon
        vmov.i32        q15, #(1<<(ROW_SHIFT-1))
        vld1.64         {d2-d5},  [r2,:128]!
        vmlal.s16       q15, d2,  w4    /* q15  += W4 * col[0] */
        vld1.64         {d6,d7},  [r2,:128]!
        vorr            d10, d3,  d5
        vld1.64         {d8,d9},  [r2,:128]!
        add             r2,  r2,  #-64

        vorr            d11, d7,  d9
        vorr            d10, d10, d11
        vmov            r3,  r4,  d10

        idct_col4_top

        orrs            r3,  r3,  r4
        beq             1f

        vmull.s16       q7,  d3,  w4    /* q7   = W4 * col[4] */
        vmlal.s16       q9,  d5,  w5    /* q9  += W5 * col[5] */
        vmlsl.s16       q10, d5,  w1    /* q10 -= W1 * col[5] */
        vmull.s16       q8,  d7,  w2    /* q8   = W2 * col[6] */
        vmlal.s16       q5,  d5,  w7    /* q5  += W7 * col[5] */
        vadd.i32        q11, q11, q7
        vsub.i32        q12, q12, q7
        vsub.i32        q13, q13, q7
        vadd.i32        q14, q14, q7
        vmlal.s16       q6,  d5,  w3    /* q6  += W3 * col[5] */
        vmull.s16       q7,  d7,  w6    /* q7   = W6 * col[6] */
        vmlal.s16       q9,  d9,  w7
        vmlsl.s16       q10, d9,  w5
        vmlal.s16       q5,  d9,  w3
        vmlsl.s16       q6,  d9,  w1
        vadd.i32        q11, q11, q7
        vsub.i32        q12, q12, q8
        vadd.i32        q13, q13, q8
        vsub.i32        q14, q14, q7

1:      vadd.i32        q3,  q11, q9
        vadd.i32        q4,  q12, q10
        vshrn.i32       d2,  q3,  #ROW_SHIFT
        vshrn.i32       d4,  q4,  #ROW_SHIFT
        vadd.i32        q7,  q13, q5
        vadd.i32        q8,  q14, q6
        vtrn.16         d2,  d4
        vshrn.i32       d6,  q7,  #ROW_SHIFT
        vshrn.i32       d8,  q8,  #ROW_SHIFT
        vsub.i32        q14, q14, q6
        vsub.i32        q11, q11, q9
        vtrn.16         d6,  d8
        vsub.i32        q13, q13, q5
        vshrn.i32       d3,  q14, #ROW_SHIFT
        vtrn.32         d2,  d6
        vsub.i32        q12, q12, q10
        vtrn.32         d4,  d8
        vshrn.i32       d5,  q13, #ROW_SHIFT
        vshrn.i32       d7,  q12, #ROW_SHIFT
        vshrn.i32       d9,  q11, #ROW_SHIFT

        vtrn.16         d3,  d5
        vtrn.16         d7,  d9
        vtrn.32         d3,  d7
        vtrn.32         d5,  d9

        vst1.64         {d2-d5},  [r2,:128]!
        vst1.64         {d6-d9},  [r2,:128]!

        bx              lr
endfunc

function idct_col4_neon
        mov             ip,  #16
        vld1.64         {d2}, [r2,:64], ip /* d2 = col[0] */
        vdup.16         d30, w4c
        vld1.64         {d4}, [r2,:64], ip /* d3 = col[1] */
        vadd.i16        d30, d30, d2
        vld1.64         {d6}, [r2,:64], ip /* d4 = col[2] */
        vmull.s16       q15, d30, w4 /* q15 = W4*(col[0]+(1<<COL_SHIFT-1)/W4)*/
        vld1.64         {d8}, [r2,:64], ip /* d5 = col[3] */

        ldrd            r4,  [r2]
        ldrd            r6,  [r2, #16]
        orrs            r4,  r4,  r5

        idct_col4_top
        addeq           r2,  r2,  #16
        beq             1f

        vld1.64         {d3}, [r2,:64], ip /* d6 = col[4] */
        vmull.s16       q7,  d3,  w4    /* q7   = W4 * col[4] */
        vadd.i32        q11, q11, q7
        vsub.i32        q12, q12, q7
        vsub.i32        q13, q13, q7
        vadd.i32        q14, q14, q7

1:      orrs            r6,  r6,  r7
        ldrd            r4,  [r2, #16]
        addeq           r2,  r2,  #16
        beq             2f

        vld1.64         {d5}, [r2,:64], ip /* d7 = col[5] */
        vmlal.s16       q9,  d5,  w5    /* q9  += W5 * col[5] */
        vmlsl.s16       q10, d5,  w1    /* q10 -= W1 * col[5] */
        vmlal.s16       q5,  d5,  w7    /* q5  += W7 * col[5] */
        vmlal.s16       q6,  d5,  w3    /* q6  += W3 * col[5] */

2:      orrs            r4,  r4,  r5
        ldrd            r4,  [r2, #16]
        addeq           r2,  r2,  #16
        beq             3f

        vld1.64         {d7}, [r2,:64], ip /* d8 = col[6] */
        vmull.s16       q7,  d7,  w6    /* q7   = W6 * col[6] */
        vmull.s16       q8,  d7,  w2    /* q8   = W2 * col[6] */
        vadd.i32        q11, q11, q7
        vsub.i32        q14, q14, q7
        vsub.i32        q12, q12, q8
        vadd.i32        q13, q13, q8

3:      orrs            r4,  r4,  r5
        addeq           r2,  r2,  #16
        beq             4f

        vld1.64         {d9}, [r2,:64], ip /* d9 = col[7] */
        vmlal.s16       q9,  d9,  w7
        vmlsl.s16       q10, d9,  w5
        vmlal.s16       q5,  d9,  w3
        vmlsl.s16       q6,  d9,  w1

4:      vaddhn.i32      d2,  q11, q9
        vaddhn.i32      d3,  q12, q10
        vaddhn.i32      d4,  q13, q5
        vaddhn.i32      d5,  q14, q6
        vsubhn.i32      d9,  q11, q9
        vsubhn.i32      d8,  q12, q10
        vsubhn.i32      d7,  q13, q5
        vsubhn.i32      d6,  q14, q6

        bx              lr
endfunc

        .align 6

function idct_col4_st8_neon
        vqshrun.s16     d2,  q1,  #COL_SHIFT-16
        vqshrun.s16     d3,  q2,  #COL_SHIFT-16
        vqshrun.s16     d4,  q3,  #COL_SHIFT-16
        vqshrun.s16     d5,  q4,  #COL_SHIFT-16
        vst1.32         {d2[0]}, [r0,:32], r1
        vst1.32         {d2[1]}, [r0,:32], r1
        vst1.32         {d3[0]}, [r0,:32], r1
        vst1.32         {d3[1]}, [r0,:32], r1
        vst1.32         {d4[0]}, [r0,:32], r1
        vst1.32         {d4[1]}, [r0,:32], r1
        vst1.32         {d5[0]}, [r0,:32], r1
        vst1.32         {d5[1]}, [r0,:32], r1

        bx              lr
endfunc

        .section .rodata
        .align 4
idct_coeff_neon:
        .short W1, W2, W3, W4, W5, W6, W7, W4c
        .previous

        .macro idct_start data
        push            {r4-r7, lr}
        pld             [\data]
        pld             [\data, #64]
        vpush           {d8-d15}
        movrel          r3,  idct_coeff_neon
        vld1.64         {d0,d1}, [r3,:128]
        .endm

        .macro idct_end
        vpop            {d8-d15}
        pop             {r4-r7, pc}
        .endm

/* void ff_simple_idct_put_neon(uint8_t *dst, int line_size, DCTELEM *data); */
function ff_simple_idct_put_neon, export=1
        idct_start      r2

        bl              idct_row4_pld_neon
        bl              idct_row4_neon
        add             r2,  r2,  #-128
        bl              idct_col4_neon
        bl              idct_col4_st8_neon
        sub             r0,  r0,  r1, lsl #3
        add             r0,  r0,  #4
        add             r2,  r2,  #-120
        bl              idct_col4_neon
        bl              idct_col4_st8_neon

        idct_end
endfunc

        .align 6

function idct_col4_add8_neon
        mov             ip,  r0

        vld1.32         {d10[0]}, [r0,:32], r1
        vshr.s16        q1,  q1,  #COL_SHIFT-16
        vld1.32         {d10[1]}, [r0,:32], r1
        vshr.s16        q2,  q2,  #COL_SHIFT-16
        vld1.32         {d11[0]}, [r0,:32], r1
        vshr.s16        q3,  q3,  #COL_SHIFT-16
        vld1.32         {d11[1]}, [r0,:32], r1
        vshr.s16        q4,  q4,  #COL_SHIFT-16
        vld1.32         {d12[0]}, [r0,:32], r1
        vaddw.u8        q1,  q1,  d10
        vld1.32         {d12[1]}, [r0,:32], r1
        vaddw.u8        q2,  q2,  d11
        vld1.32         {d13[0]}, [r0,:32], r1
        vqmovun.s16     d2,  q1
        vld1.32         {d13[1]}, [r0,:32], r1
        vaddw.u8        q3,  q3,  d12
        vst1.32         {d2[0]},  [ip,:32], r1
        vqmovun.s16     d3,  q2
        vst1.32         {d2[1]},  [ip,:32], r1
        vaddw.u8        q4,  q4,  d13
        vst1.32         {d3[0]},  [ip,:32], r1
        vqmovun.s16     d4,  q3
        vst1.32         {d3[1]},  [ip,:32], r1
        vqmovun.s16     d5,  q4
        vst1.32         {d4[0]},  [ip,:32], r1
        vst1.32         {d4[1]},  [ip,:32], r1
        vst1.32         {d5[0]},  [ip,:32], r1
        vst1.32         {d5[1]},  [ip,:32], r1

        bx              lr
endfunc

/* void ff_simple_idct_add_neon(uint8_t *dst, int line_size, DCTELEM *data); */
function ff_simple_idct_add_neon, export=1
        idct_start      r2

        bl              idct_row4_pld_neon
        bl              idct_row4_neon
        add             r2,  r2,  #-128
        bl              idct_col4_neon
        bl              idct_col4_add8_neon
        sub             r0,  r0,  r1, lsl #3
        add             r0,  r0,  #4
        add             r2,  r2,  #-120
        bl              idct_col4_neon
        bl              idct_col4_add8_neon

        idct_end
endfunc

        .align 6

function idct_col4_st16_neon
        mov             ip,  #16

        vshr.s16        q1,  q1,  #COL_SHIFT-16
        vshr.s16        q2,  q2,  #COL_SHIFT-16
        vst1.64         {d2}, [r2,:64], ip
        vshr.s16        q3,  q3,  #COL_SHIFT-16
        vst1.64         {d3}, [r2,:64], ip
        vshr.s16        q4,  q4,  #COL_SHIFT-16
        vst1.64         {d4}, [r2,:64], ip
        vst1.64         {d5}, [r2,:64], ip
        vst1.64         {d6}, [r2,:64], ip
        vst1.64         {d7}, [r2,:64], ip
        vst1.64         {d8}, [r2,:64], ip
        vst1.64         {d9}, [r2,:64], ip

        bx              lr
endfunc

/* void ff_simple_idct_neon(DCTELEM *data); */
function ff_simple_idct_neon, export=1
        idct_start      r0

        mov             r2,  r0
        bl              idct_row4_neon
        bl              idct_row4_neon
        add             r2,  r2,  #-128
        bl              idct_col4_neon
        add             r2,  r2,  #-128
        bl              idct_col4_st16_neon
        add             r2,  r2,  #-120
        bl              idct_col4_neon
        add             r2,  r2,  #-128
        bl              idct_col4_st16_neon

        idct_end
endfunc
