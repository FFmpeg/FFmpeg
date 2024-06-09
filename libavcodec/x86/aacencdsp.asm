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
    shl    sized, 2
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
%macro AAC_QUANTIZE_BANDS 0
cglobal aac_quantize_bands, 5, 5, 6, out, in, scaled, size, is_signed, maxval, Q34, rounding
%if UNIX64 == 0
%if mmsize == 32
    vbroadcastss m0, Q34m
    vbroadcastss m1, roundingm
%else
    movss     m0, Q34m
    movss     m1, roundingm
    shufps    m0, m0, 0
    shufps    m1, m1, 0
%endif
    cvtsi2ss xm3, dword maxvalm
    shufps   xm3, xm3, xm3, 0
%else ; UNIX64
    shufps   xm0, xm0, 0
    shufps   xm1, xm1, 0
    cvtsi2ss xm3, maxvald
    shufps   xm3, xm3, xm3, 0
%if mmsize == 32
    vinsertf128 m0, m0, xm0, 1
    vinsertf128 m1, m1, xm1, 1
%endif
%endif
%if mmsize == 32
    vinsertf128 m3, m3, xm3, 1
%endif
    shl       is_signedd, 31
    movd     xm4, is_signedd
    shufps   xm4, xm4, xm4, 0
%if mmsize == 32
    vinsertf128 m4, m4, xm4, 1
%endif
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
%endmacro

INIT_XMM sse2
AAC_QUANTIZE_BANDS
INIT_YMM avx
AAC_QUANTIZE_BANDS
