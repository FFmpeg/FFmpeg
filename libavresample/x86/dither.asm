;******************************************************************************
;* x86 optimized dithering format conversion
;* Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

SECTION_RODATA 32

; 1.0f / (2.0f * INT32_MAX)
pf_dither_scale: times 8 dd 2.32830643762e-10

pf_s16_scale: times 4 dd 32753.0

SECTION .text

;------------------------------------------------------------------------------
; void ff_quantize(int16_t *dst, float *src, float *dither, int len);
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal quantize, 4,4,3, dst, src, dither, len
    lea         lenq, [2*lend]
    add         dstq, lenq
    lea         srcq, [srcq+2*lenq]
    lea      ditherq, [ditherq+2*lenq]
    neg         lenq
    mova          m2, [pf_s16_scale]
.loop:
    mulps         m0, m2, [srcq+2*lenq]
    mulps         m1, m2, [srcq+2*lenq+mmsize]
    addps         m0, [ditherq+2*lenq]
    addps         m1, [ditherq+2*lenq+mmsize]
    cvtps2dq      m0, m0
    cvtps2dq      m1, m1
    packssdw      m0, m1
    mova     [dstq+lenq], m0
    add         lenq, mmsize
    jl .loop
    REP_RET

;------------------------------------------------------------------------------
; void ff_dither_int_to_float_rectangular(float *dst, int *src, int len)
;------------------------------------------------------------------------------

%macro DITHER_INT_TO_FLOAT_RECTANGULAR 0
cglobal dither_int_to_float_rectangular, 3,3,3, dst, src, len
    lea         lenq, [4*lend]
    add         srcq, lenq
    add         dstq, lenq
    neg         lenq
    mova          m0, [pf_dither_scale]
.loop:
    cvtdq2ps      m1, [srcq+lenq]
    cvtdq2ps      m2, [srcq+lenq+mmsize]
    mulps         m1, m1, m0
    mulps         m2, m2, m0
    mova  [dstq+lenq], m1
    mova  [dstq+lenq+mmsize], m2
    add         lenq, 2*mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
DITHER_INT_TO_FLOAT_RECTANGULAR
INIT_YMM avx
DITHER_INT_TO_FLOAT_RECTANGULAR

;------------------------------------------------------------------------------
; void ff_dither_int_to_float_triangular(float *dst, int *src0, int len)
;------------------------------------------------------------------------------

%macro DITHER_INT_TO_FLOAT_TRIANGULAR 0
cglobal dither_int_to_float_triangular, 3,4,5, dst, src0, len, src1
    lea         lenq, [4*lend]
    lea        src1q, [src0q+2*lenq]
    add        src0q, lenq
    add         dstq, lenq
    neg         lenq
    mova          m0, [pf_dither_scale]
.loop:
    cvtdq2ps      m1, [src0q+lenq]
    cvtdq2ps      m2, [src0q+lenq+mmsize]
    cvtdq2ps      m3, [src1q+lenq]
    cvtdq2ps      m4, [src1q+lenq+mmsize]
    addps         m1, m1, m3
    addps         m2, m2, m4
    mulps         m1, m1, m0
    mulps         m2, m2, m0
    mova  [dstq+lenq], m1
    mova  [dstq+lenq+mmsize], m2
    add         lenq, 2*mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
DITHER_INT_TO_FLOAT_TRIANGULAR
INIT_YMM avx
DITHER_INT_TO_FLOAT_TRIANGULAR
