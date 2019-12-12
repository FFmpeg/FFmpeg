;******************************************************************************
;* SIMD optimized AAC encoder DSP functions
;*
;* Copyright (C) 2016 Rostislav Pehlivanov <atomnuker@gmail.com>
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

SECTION_RODATA

float_abs_mask: times 4 dd 0x7fffffff

SECTION .text

;*******************************************************************
;void ff_abs_pow34(float *out, const float *in, const int size);
;*******************************************************************
INIT_XMM sse
cglobal abs_pow34, 3, 3, 3, out, in, size
    mova   m2, [float_abs_mask]
    shl    sizeq, 2
    add    inq, sizeq
    add    outq, sizeq
    neg    sizeq
.loop:
    andps  m0, m2, [inq+sizeq]
    sqrtps m1, m0
    mulps  m0, m1
    sqrtps m0, m0
    mova   [outq+sizeq], m0
    add    sizeq, mmsize
    jl    .loop
    RET

;*******************************************************************
;void ff_aac_quantize_bands(int *out, const float *in, const float *scaled,
;                           int size, int is_signed, int maxval, const float Q34,
;                           const float rounding)
;*******************************************************************
INIT_XMM sse2
cglobal aac_quantize_bands, 5, 5, 6, out, in, scaled, size, is_signed, maxval, Q34, rounding
%if UNIX64 == 0
    movss     m0, Q34m
    movss     m1, roundingm
    cvtsi2ss  m3, dword maxvalm
%else
    cvtsi2ss  m3, maxvald
%endif
    shufps    m0, m0, 0
    shufps    m1, m1, 0
    shufps    m3, m3, 0
    shl       is_signedd, 31
    movd      m4, is_signedd
    shufps    m4, m4, 0
    shl       sized,   2
    add       inq, sizeq
    add       outq, sizeq
    add       scaledq, sizeq
    neg       sizeq
.loop:
    mulps     m2, m0, [scaledq+sizeq]
    addps     m2, m1
    minps     m2, m3
    andps     m5, m4, [inq+sizeq]
    orps      m2, m5
    cvttps2dq m2, m2
    mova      [outq+sizeq], m2
    add       sizeq, mmsize
    jl       .loop
    RET
