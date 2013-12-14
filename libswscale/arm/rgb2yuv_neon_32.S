/*
 * Copyright (C) 2013 Xiaolei Yu <dreifachstein@gmail.com>
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

#include "rgb2yuv_neon_common.S"

/* downsampled R16G16B16 x8 */
alias_qw    r16x8,  q7
alias_qw    g16x8,  q8
alias_qw    b16x8,  q9

alias   n16x16_o,   q11
alias   n16x16_ol,  q11_l
alias   n16x16_oh,  q11_h

alias   y32x16_el,  q12
alias   y32x16_eh,  q13
alias   y32x16_ol,  q14
alias   y32x16_oh,  q15

alias   y16x16_e,   q12
alias   y16x16_el,  q12_l
alias   y16x16_eh,  q12_h
alias   y16x16_o,   q13
alias   y16x16_ol,  q13_l
alias   y16x16_oh,  q13_h


alias   y8x16,  y16x16_e


.macro init     src
    // load s32x3x3, narrow to s16x3x3
    vld3.i32    {q13_l, q14_l, q15_l},          [\src]!
    vld3.i32    {q13_h[0], q14_h[0], q15_h[0]}, [\src]

    vmovn.i32   CO_R, q13
    vmovn.i32   CO_G, q14
    vmovn.i32   CO_B, q15

    vmov.u8     BIAS_Y, #16
    vmov.u8     BIAS_U, #128
.endm


.macro compute_y_16x1_step  action, s8x16, coeff
    vmov.u8     n16x16_o,   #0
    vtrn.u8     \s8x16,     n16x16_o

    \action     y32x16_el,  \s8x16\()_l,    \coeff
    \action     y32x16_eh,  \s8x16\()_h,    \coeff
    \action     y32x16_ol,  n16x16_ol,      \coeff
    \action     y32x16_oh,  n16x16_oh,      \coeff
.endm

/*
 * in:      r8x16, g8x16, b8x16
 * out:     y8x16
 * clobber: q11-q15, r8x16, g8x16, b8x16
 */
.macro compute_y_16x1
    compute_y_16x1_step vmull, r8x16, CO_RY
    compute_y_16x1_step vmlal, g8x16, CO_GY
    compute_y_16x1_step vmlal, b8x16, CO_BY

    vrshrn.i32  y16x16_el,  y32x16_el,  #15
    vrshrn.i32  y16x16_eh,  y32x16_eh,  #15
    vrshrn.i32  y16x16_ol,  y32x16_ol,  #15
    vrshrn.i32  y16x16_oh,  y32x16_oh,  #15

    vtrn.8      y16x16_e,   y16x16_o
    vadd.u8     y8x16,      y8x16,      BIAS_Y
.endm

alias   c32x8_l,    q14
alias   c32x8_h,    q15

alias_qw    c16x8,  q13
alias_qw    c8x8x2, q10

.macro compute_chroma_8x1_step  action, s16x8, coeff
    \action     c32x8_l,    \s16x8\()_l,    \coeff
    \action     c32x8_h,    \s16x8\()_h,    \coeff
.endm

/*
 * in:      r16x8, g16x8, b16x8
 * out:     c8x8
 * clobber: q14-q15
 */
.macro compute_chroma_8x1   c, C
    compute_chroma_8x1_step vmull, r16x8, CO_R\C
    compute_chroma_8x1_step vmlal, g16x8, CO_G\C
    compute_chroma_8x1_step vmlal, b16x8, CO_B\C

    vrshrn.i32  c16x8_l,    c32x8_l,    #15
    vrshrn.i32  c16x8_h,    c32x8_h,    #15
    vmovn.i16   \c\()8x8,   c16x8
    vadd.u8     \c\()8x8,   \c\()8x8,   BIAS_\C
.endm


    loop_420sp  rgbx, nv12, init, kernel_420_16x2, 32
