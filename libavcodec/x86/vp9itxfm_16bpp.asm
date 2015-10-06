;******************************************************************************
;* VP9 inverse transform x86 SIMD optimizations
;*
;* Copyright (C) 2015 Ronald S. Bultje <rsbultje gmail com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"
%include "vp9itxfm_template.asm"

SECTION_RODATA

cextern pw_1023
cextern pw_4095

SECTION .text

%macro VP9_STORE_2X 6-7 dstq ; reg1, reg2, tmp1, tmp2, min, max, dst
    mova               m%3, [%7]
    mova               m%4, [%7+strideq]
    paddw              m%3, m%1
    paddw              m%4, m%2
    pmaxsw             m%3, m%5
    pmaxsw             m%4, m%5
    pminsw             m%3, m%6
    pminsw             m%4, m%6
    mova              [%7], m%3
    mova      [%7+strideq], m%4
%endmacro

%macro ZERO_BLOCK 4 ; mem, stride, nnzcpl, zero_reg
%assign %%y 0
%rep %3
%assign %%x 0
%rep %3*4/mmsize
    mova      [%1+%%y+%%x], %4
%assign %%x (%%x+mmsize)
%endrep
%assign %%y (%%y+%2)
%endrep
%endmacro

; the input coefficients are scaled up by 2 bit (which we downscale immediately
; in the iwht), and is otherwise orthonormally increased by 1 bit per iwht_1d.
; therefore, a diff of 10-12+sign bit will fit in 12-14+sign bit after scaling,
; i.e. everything can be done in 15+1bpp words. Since the quant fractional bits
; add 2 bits, we need to scale before converting to word in 12bpp, since the
; input will be 16+sign bit which doesn't fit in 15+sign words, but in 10bpp
; we can scale after converting to words (which is half the instructions),
; since the input is only 14+sign bit, which fits in 15+sign words directly.

%macro IWHT4_FN 2 ; bpp, max
cglobal vp9_iwht_iwht_4x4_add_%1, 3, 3, 8, dst, stride, block, eob
    mova                m7, [pw_%2]
    mova                m0, [blockq+0*16+0]
    mova                m1, [blockq+1*16+0]
%if %1 >= 12
    mova                m4, [blockq+0*16+8]
    mova                m5, [blockq+1*16+8]
    psrad               m0, 2
    psrad               m1, 2
    psrad               m4, 2
    psrad               m5, 2
    packssdw            m0, m4
    packssdw            m1, m5
%else
    packssdw            m0, [blockq+0*16+8]
    packssdw            m1, [blockq+1*16+8]
    psraw               m0, 2
    psraw               m1, 2
%endif
    mova                m2, [blockq+2*16+0]
    mova                m3, [blockq+3*16+0]
%if %1 >= 12
    mova                m4, [blockq+2*16+8]
    mova                m5, [blockq+3*16+8]
    psrad               m2, 2
    psrad               m3, 2
    psrad               m4, 2
    psrad               m5, 2
    packssdw            m2, m4
    packssdw            m3, m5
%else
    packssdw            m2, [blockq+2*16+8]
    packssdw            m3, [blockq+3*16+8]
    psraw               m2, 2
    psraw               m3, 2
%endif

    VP9_IWHT4_1D
    TRANSPOSE4x4W        0, 1, 2, 3, 4
    VP9_IWHT4_1D

    pxor                m6, m6
    VP9_STORE_2X         0, 1, 4, 5, 6, 7
    lea               dstq, [dstq+strideq*2]
    VP9_STORE_2X         2, 3, 4, 5, 6, 7
    ZERO_BLOCK      blockq, 16, 4, m6
    RET
%endmacro

INIT_MMX mmxext
IWHT4_FN 10, 1023
INIT_MMX mmxext
IWHT4_FN 12, 4095
