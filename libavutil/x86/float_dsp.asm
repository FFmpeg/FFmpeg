;*****************************************************************************
;* x86-optimized Float DSP functions
;*
;* Copyright 2006 Loren Merritt
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

%include "x86inc.asm"

SECTION .text

;-----------------------------------------------------------------------------
; void vector_fmul(float *dst, const float *src0, const float *src1, int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL 0
cglobal vector_fmul, 4,4,2, dst, src0, src1, len
    lea       lenq, [lend*4 - 2*mmsize]
ALIGN 16
.loop
    mova      m0,   [src0q + lenq]
    mova      m1,   [src0q + lenq + mmsize]
    mulps     m0, m0, [src1q + lenq]
    mulps     m1, m1, [src1q + lenq + mmsize]
    mova      [dstq + lenq], m0
    mova      [dstq + lenq + mmsize], m1

    sub       lenq, 2*mmsize
    jge       .loop
%if mmsize == 32
    vzeroupper
    RET
%else
    REP_RET
%endif
%endmacro

INIT_XMM sse
VECTOR_FMUL
%if HAVE_AVX
INIT_YMM avx
VECTOR_FMUL
%endif
