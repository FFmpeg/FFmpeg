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

#include "config.h"
#if HAVE_AS_DN_DIRECTIVE
#include "rgb2yuv_neon_common.S"

/* downsampled R16G16B16 x8 */
alias_qw    r16x8,  q7
alias_qw    g16x8,  q8
alias_qw    b16x8,  q9

alias   n16x16_l,   q11
alias   n16x16_h,   q12

alias   y16x16_l,   q13
alias   y16x16_h,   q14

alias_qw    y8x16,  q15

.macro init     src
    vld3.i32    {q13_l, q14_l, q15_l},          [\src]!
    vld3.i32    {q13_h[0], q14_h[0], q15_h[0]}, [\src]
    vrshrn.i32  CO_R,   q13, #7
    vrshrn.i32  CO_G,   q14, #7
    vrshrn.i32  CO_B,   q15, #7

    vmov.u8     BIAS_Y, #16
    vmov.u8     BIAS_U, #128
.endm


.macro compute_y_16x1_step  action, s8x16, coeff
    vmovl.u8    n16x16_l,   \s8x16\()_l
    vmovl.u8    n16x16_h,   \s8x16\()_h

    \action     y16x16_l,   n16x16_l,   \coeff
    \action     y16x16_h,   n16x16_h,   \coeff
.endm

.macro compute_y_16x1
    compute_y_16x1_step vmul, r8x16, CO_RY
    compute_y_16x1_step vmla, g8x16, CO_GY
    compute_y_16x1_step vmla, b8x16, CO_BY

    vrshrn.i16  y8x16_l,    y16x16_l,   #8
    vrshrn.i16  y8x16_h,    y16x16_h,   #8

    vadd.u8     y8x16,      y8x16,      BIAS_Y
.endm

alias   c16x8,      q15
alias_qw    c8x8x2, q10


.macro compute_chroma_8x1   c, C
    vmul    c16x8,  r16x8,  CO_R\C
    vmla    c16x8,  g16x8,  CO_G\C
    vmla    c16x8,  b16x8,  CO_B\C

    vrshrn.i16  \c\()8x8,   c16x8,      #8
    vadd.u8     \c\()8x8,   \c\()8x8,   BIAS_\C
.endm

    loop_420sp  rgbx, nv12, init, kernel_420_16x2, 16
#endif
