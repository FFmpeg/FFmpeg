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

#include "libavutil/arm/asm.S"

.macro alias name, tgt, set=1
.if \set != 0
    \name   .req    \tgt
.else
    .unreq  \name
.endif
.endm

.altmacro

.macro alias_dw_all qw, dw_l, dw_h
    alias   q\qw\()_l, d\dw_l
    alias   q\qw\()_h, d\dw_h
    .if \qw < 15
        alias_dw_all  %(\qw + 1), %(\dw_l + 2), %(\dw_h + 2)
    .endif
.endm

alias_dw_all    0, 0, 1

.noaltmacro

.macro alias_qw     name, qw, set=1
    alias   \name\(), \qw, \set
    alias   \name\()_l, \qw\()_l, \set
    alias   \name\()_h, \qw\()_h, \set
.endm

.macro prologue
    push            {r4-r12, lr}
    vpush           {q4-q7}
.endm

.macro epilogue
    vpop            {q4-q7}
    pop             {r4-r12, pc}
.endm

.macro  load_arg    reg, ix
    ldr     \reg,   [sp, #((10 * 4 + 4 * 16) + (\ix - 4) * 4)]
.endm


/* ()_to_()_neon(const uint8_t *src, uint8_t *y, uint8_t *chroma
 *                  int width, int height,
 *                  int y_stride, int c_stride, int src_stride,
 *                  int32_t coeff_table[9]);
 */
.macro  alias_loop_420sp set=1
    alias   src,        r0, \set
    alias   src0,       src, \set
    alias   y,          r1, \set
    alias   y0,         y, \set
    alias   chroma,     r2, \set
    alias   width,      r3, \set
    alias   header,     width, \set

    alias   height,     r4, \set
    alias   y_stride,   r5, \set
    alias   c_stride,   r6, \set
    alias   c_padding,  c_stride, \set
    alias   src_stride, r7, \set

    alias   y0_end,     r8, \set

    alias   src_padding,r9, \set
    alias   y_padding,  r10, \set

    alias   src1,       r11, \set
    alias   y1,         r12, \set

    alias   coeff_table,r12, \set
.endm


.macro  loop_420sp s_fmt, d_fmt, init, kernel, precision

function \s_fmt\()_to_\d_fmt\()_neon_\precision, export=1
    prologue

    alias_loop_420sp

    load_arg    height,         4
    load_arg    y_stride,       5
    load_arg    c_stride,       6
    load_arg    src_stride,     7
    load_arg    coeff_table,    8

    \init       coeff_table

    sub         y_padding,      y_stride,       width
    sub         c_padding,      c_stride,       width
    sub         src_padding,    src_stride,     width, LSL #2

    add         y0_end,         y0,             width
    and         header,         width,          #15

    add         y1,             y0,             y_stride
    add         src1,           src0,           src_stride

0:
    cmp         header,     #0
    beq         1f

    \kernel     \s_fmt, \d_fmt, src0, src1, y0, y1, chroma, header

1:
    \kernel     \s_fmt, \d_fmt, src0, src1, y0, y1, chroma

    cmp         y0,         y0_end
    blt         1b
2:
    add         y0,         y1,         y_padding
    add         y0_end,     y1,         y_stride
    add         chroma,     chroma,     c_padding
    add         src0,       src1,       src_padding

    add         y1,         y0,         y_stride
    add         src1,       src0,       src_stride

    subs        height,     height,     #2

    bgt         0b

    epilogue

    alias_loop_420sp 0

endfunc
.endm

.macro downsample
    vpaddl.u8   r16x8,  r8x16
    vpaddl.u8   g16x8,  g8x16
    vpaddl.u8   b16x8,  b8x16
.endm


/* acculumate and right shift by 2 */
.macro downsample_ars2
    vpadal.u8   r16x8,  r8x16
    vpadal.u8   g16x8,  g8x16
    vpadal.u8   b16x8,  b8x16

    vrshr.u16   r16x8,  r16x8,  #2
    vrshr.u16   g16x8,  g16x8,  #2
    vrshr.u16   b16x8,  b16x8,  #2
.endm

.macro store_y8_16x1            dst, count
.ifc "\count",""
    vstmia      \dst!,  {y8x16}
.else
    vstmia      \dst,   {y8x16}
    add         \dst,   \dst,           \count
.endif
.endm

.macro store_chroma_nv12_8x1    dst, count
.ifc "\count",""
    vst2.i8     {u8x8, v8x8},   [\dst]!
.else
    vst2.i8     {u8x8, v8x8},   [\dst], \count
.endif
.endm

.macro store_chroma_nv21_8x1    dst, count
.ifc "\count",""
    vst2.i8     {v8x8, u8x8},   [\dst]!
.else
    vst2.i8     {v8x8, u8x8},   [\dst], \count
.endif
.endm

.macro load_8888_16x1   a, b, c, d, src, count
.ifc "\count",""
    vld4.8      {\a\()8x16_l, \b\()8x16_l, \c\()8x16_l, \d\()8x16_l},  [\src]!
    vld4.8      {\a\()8x16_h, \b\()8x16_h, \c\()8x16_h, \d\()8x16_h},  [\src]!
.else
    vld4.8      {\a\()8x16_l, \b\()8x16_l, \c\()8x16_l, \d\()8x16_l},  [\src]!
    vld4.8      {\a\()8x16_h, \b\()8x16_h, \c\()8x16_h, \d\()8x16_h},  [\src]
    sub         \src,   \src,   #32
    add         \src,   \src,   \count, LSL #2
.endif
.endm

.macro load_rgbx_16x1   src, count
    load_8888_16x1  r, g, b, x, \src, \count
.endm

.macro load_bgrx_16x1   src, count
    load_8888_16x1  b, g, r, x, \src, \count
.endm

.macro alias_src_rgbx   set=1
    alias_src_8888  r, g, b, x, \set
.endm

.macro alias_src_bgrx   set=1
    alias_src_8888  b, g, r, x, \set
.endm

.macro alias_dst_nv12   set=1
    alias   u8x8, c8x8x2_l, \set
    alias   v8x8, c8x8x2_h, \set
.endm

.macro alias_dst_nv21   set=1
    alias   v8x8, c8x8x2_l, \set
    alias   u8x8, c8x8x2_h, \set
.endm


// common aliases

alias   CO_R    d0
CO_RY   .dn     d0.s16[0]
CO_RU   .dn     d0.s16[1]
CO_RV   .dn     d0.s16[2]

alias   CO_G    d1
CO_GY   .dn     d1.s16[0]
CO_GU   .dn     d1.s16[1]
CO_GV   .dn     d1.s16[2]

alias   CO_B    d2
CO_BY   .dn     d2.s16[0]
CO_BU   .dn     d2.s16[1]
CO_BV   .dn     d2.s16[2]

alias   BIAS_U, d3
alias   BIAS_V, BIAS_U

alias   BIAS_Y, q2


/* q3-q6 R8G8B8X8 x16 */

.macro alias_src_8888   a, b, c, d, set
    alias_qw  \a\()8x16, q3, \set
    alias_qw  \b\()8x16, q4, \set
    alias_qw  \c\()8x16, q5, \set
    alias_qw  \d\()8x16, q6, \set
.endm

.macro kernel_420_16x2  rgb_fmt, yuv_fmt, rgb0, rgb1, y0, y1, chroma, count
    alias_src_\rgb_fmt
    alias_dst_\yuv_fmt

    load_\rgb_fmt\()_16x1   \rgb0, \count

    downsample
    compute_y_16x1
    store_y8_16x1   \y0, \count


    load_\rgb_fmt\()_16x1   \rgb1, \count
    downsample_ars2
    compute_y_16x1
    store_y8_16x1   \y1, \count

    compute_chroma_8x1  u, U
    compute_chroma_8x1  v, V

    store_chroma_\yuv_fmt\()_8x1 \chroma, \count

    alias_dst_\yuv_fmt 0
    alias_src_\rgb_fmt 0
.endm
