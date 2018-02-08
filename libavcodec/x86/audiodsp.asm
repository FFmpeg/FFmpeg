;******************************************************************************
;* optimized audio functions
;* Copyright (c) 2008 Loren Merritt
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

SECTION .text

%macro SCALARPRODUCT 0
; int ff_scalarproduct_int16(int16_t *v1, int16_t *v2, int order)
cglobal scalarproduct_int16, 3,3,3, v1, v2, order
    add orderd, orderd
    add v1q, orderq
    add v2q, orderq
    neg orderq
    pxor    m2, m2
.loop:
    movu    m0, [v1q + orderq]
    movu    m1, [v1q + orderq + mmsize]
    pmaddwd m0, [v2q + orderq]
    pmaddwd m1, [v2q + orderq + mmsize]
    paddd   m2, m0
    paddd   m2, m1
    add     orderq, mmsize*2
    jl .loop
    HADDD   m2, m0
    movd   eax, m2
%if mmsize == 8
    emms
%endif
    RET
%endmacro

INIT_MMX mmxext
SCALARPRODUCT
INIT_XMM sse2
SCALARPRODUCT


;-----------------------------------------------------------------------------
; void ff_vector_clip_int32(int32_t *dst, const int32_t *src, int32_t min,
;                           int32_t max, unsigned int len)
;-----------------------------------------------------------------------------

; %1 = number of xmm registers used
; %2 = number of inline load/process/store loops per asm loop
; %3 = process 4*mmsize (%3=0) or 8*mmsize (%3=1) bytes per loop
; %4 = CLIPD function takes min/max as float instead of int (SSE2 version)
; %5 = suffix
%macro VECTOR_CLIP_INT32 4-5
cglobal vector_clip_int32%5, 5,5,%1, dst, src, min, max, len
%if %4
    cvtsi2ss  m4, minm
    cvtsi2ss  m5, maxm
%else
    movd      m4, minm
    movd      m5, maxm
%endif
    SPLATD    m4
    SPLATD    m5
.loop:
%assign %%i 0
%rep %2
    mova      m0,  [srcq + mmsize * (0 + %%i)]
    mova      m1,  [srcq + mmsize * (1 + %%i)]
    mova      m2,  [srcq + mmsize * (2 + %%i)]
    mova      m3,  [srcq + mmsize * (3 + %%i)]
%if %3
    mova      m7,  [srcq + mmsize * (4 + %%i)]
    mova      m8,  [srcq + mmsize * (5 + %%i)]
    mova      m9,  [srcq + mmsize * (6 + %%i)]
    mova      m10, [srcq + mmsize * (7 + %%i)]
%endif
    CLIPD  m0,  m4, m5, m6
    CLIPD  m1,  m4, m5, m6
    CLIPD  m2,  m4, m5, m6
    CLIPD  m3,  m4, m5, m6
%if %3
    CLIPD  m7,  m4, m5, m6
    CLIPD  m8,  m4, m5, m6
    CLIPD  m9,  m4, m5, m6
    CLIPD  m10, m4, m5, m6
%endif
    mova  [dstq + mmsize * (0 + %%i)], m0
    mova  [dstq + mmsize * (1 + %%i)], m1
    mova  [dstq + mmsize * (2 + %%i)], m2
    mova  [dstq + mmsize * (3 + %%i)], m3
%if %3
    mova  [dstq + mmsize * (4 + %%i)], m7
    mova  [dstq + mmsize * (5 + %%i)], m8
    mova  [dstq + mmsize * (6 + %%i)], m9
    mova  [dstq + mmsize * (7 + %%i)], m10
%endif
%assign %%i (%%i + 4 * (1 + %3))
%endrep
    add     srcq, mmsize*4*(%2+%3)
    add     dstq, mmsize*4*(%2+%3)
    sub     lend, mmsize*(%2+%3)
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmx
VECTOR_CLIP_INT32 0, 1, 0, 0
INIT_XMM sse2
VECTOR_CLIP_INT32 6, 1, 0, 0, _int
VECTOR_CLIP_INT32 6, 2, 0, 1
INIT_XMM sse4
%ifdef m8
VECTOR_CLIP_INT32 11, 1, 1, 0
%else
VECTOR_CLIP_INT32 6, 1, 0, 0
%endif

; void ff_vector_clipf_sse(float *dst, const float *src,
;                          int len, float min, float max)
INIT_XMM sse
cglobal vector_clipf, 3, 3, 6, dst, src, len, min, max
%if ARCH_X86_32
    VBROADCASTSS m0, minm
    VBROADCASTSS m1, maxm
%elif WIN64
    SWAP 0, 3
    VBROADCASTSS m0, m0
    VBROADCASTSS m1, maxm
%else ; 64bit sysv
    VBROADCASTSS m0, m0
    VBROADCASTSS m1, m1
%endif

    movsxdifnidn lenq, lend

.loop:
    mova m2, [srcq + 4 * lenq - 4 * mmsize]
    mova m3, [srcq + 4 * lenq - 3 * mmsize]
    mova m4, [srcq + 4 * lenq - 2 * mmsize]
    mova m5, [srcq + 4 * lenq - 1 * mmsize]

    maxps m2, m0
    maxps m3, m0
    maxps m4, m0
    maxps m5, m0

    minps m2, m1
    minps m3, m1
    minps m4, m1
    minps m5, m1

    mova [dstq + 4 * lenq - 4 * mmsize], m2
    mova [dstq + 4 * lenq - 3 * mmsize], m3
    mova [dstq + 4 * lenq - 2 * mmsize], m4
    mova [dstq + 4 * lenq - 1 * mmsize], m5

    sub lenq, mmsize
    jg .loop

    RET
