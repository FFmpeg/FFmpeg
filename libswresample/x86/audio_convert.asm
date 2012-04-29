;******************************************************************************
;* Copyright (c) 2012 Michael Niedermayer
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

%include "libavutil/x86/x86inc.asm"
%include "libavutil/x86/x86util.asm"

SECTION_RODATA

flt2pm31: times 8 dd 4.6566129e-10
flt2p31 : times 8 dd 2147483648.0
flt2p15 : times 8 dd 32768.0

SECTION .text

%macro INT16_TO_INT32 1
cglobal int16_to_int32_%1, 3, 3, 3, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne int16_to_int32_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne int16_to_int32_u_int %+ SUFFIX
%else
int16_to_int32_u_int %+ SUFFIX
%endif
    add     dstq, lenq
    shr     lenq, 1
    add     srcq, lenq
    neg     lenq
.next
    mov%1     m2, [srcq+lenq]
    pxor      m0, m0
    pxor      m1, m1
    punpcklwd m0, m2
    punpckhwd m1, m2
    mov%1 [         dstq+2*lenq], m0
    mov%1 [mmsize + dstq+2*lenq], m1
    add lenq, mmsize
        jl .next
%if mmsize == 8
    emms
%endif
    REP_RET
%endmacro

%macro INT32_TO_FLOAT 1
cglobal int32_to_float_%1, 3, 3, 3, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne int32_to_float_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne int32_to_float_u_int %+ SUFFIX
%else
int32_to_float_u_int %+ SUFFIX
%endif
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
    mova      m2, [flt2pm31]
.next:
%ifidn %1, a
    cvtdq2ps  m0, [         srcq+lenq]
    cvtdq2ps  m1, [mmsize + srcq+lenq]
%else
    movu      m0, [         srcq+lenq]
    movu      m1, [mmsize + srcq+lenq]
    cvtdq2ps  m0, m0
    cvtdq2ps  m1, m1
%endif
    mulps m0, m2
    mulps m1, m2
    mov%1 [         dstq+lenq], m0
    mov%1 [mmsize + dstq+lenq], m1
    add lenq, 2*mmsize
        jl .next
    REP_RET
%endmacro

%macro INT16_TO_FLOAT 1
cglobal int16_to_float_%1, 3, 3, 4, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne int16_to_float_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne int16_to_float_u_int %+ SUFFIX
%else
int16_to_float_u_int %+ SUFFIX
%endif
    add     dstq, lenq
    shr     lenq, 1
    add     srcq, lenq
    neg     lenq
    mova      m3, [flt2pm31]
.next:
    mov%1     m2, [srcq+lenq]
    pxor      m0, m0
    pxor      m1, m1
    punpcklwd m0, m2
    punpckhwd m1, m2
    cvtdq2ps  m0, m0
    cvtdq2ps  m1, m1
    mulps m0, m3
    mulps m1, m3
    mov%1 [         dstq+2*lenq], m0
    mov%1 [mmsize + dstq+2*lenq], m1
    add lenq, mmsize
        jl .next
    REP_RET
%endmacro

%macro FLOAT_TO_INT32 1
cglobal float_to_int32_%1, 3, 3, 5, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne float_to_int32_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne float_to_int32_u_int %+ SUFFIX
%else
float_to_int32_u_int %+ SUFFIX
%endif
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
    mova      m2, [flt2p31]
.next:
    mov%1     m0, [         srcq+lenq]
    mov%1     m1, [mmsize + srcq+lenq]
    mulps m0, m2
    mulps m1, m2
    cvtps2dq  m3, m0
    cvtps2dq  m4, m1
    cmpnltps m0, m2
    cmpnltps m1, m2
    paddd m0, m3
    paddd m1, m4
    mov%1 [         dstq+lenq], m0
    mov%1 [mmsize + dstq+lenq], m1
    add lenq, 2*mmsize
        jl .next
    REP_RET
%endmacro

%macro FLOAT_TO_INT16 1
cglobal float_to_int16_%1, 3, 3, 3, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne float_to_int16_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne float_to_int16_u_int %+ SUFFIX
%else
float_to_int16_u_int %+ SUFFIX
%endif
    lea     srcq, [srcq + 2*lenq]
    add     dstq, lenq
    neg     lenq
    mova      m2, [flt2p15]
.next:
    mov%1     m0, [         srcq+2*lenq]
    mov%1     m1, [mmsize + srcq+2*lenq]
    mulps m0, m2
    mulps m1, m2
    cvtps2dq  m0, m0
    cvtps2dq  m1, m1
    packssdw  m0, m1
    mov%1 [         dstq+lenq], m0
    add lenq, mmsize
        jl .next
    REP_RET
%endmacro

%macro INT32_TO_INT16 1
cglobal int32_to_int16_%1, 3, 3, 2, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne int32_to_int16_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne int32_to_int16_u_int %+ SUFFIX
%else
int32_to_int16_u_int %+ SUFFIX
%endif
    lea     srcq, [srcq + 2*lenq]
    add     dstq, lenq
    neg     lenq
.next:
    mov%1     m0, [         srcq+2*lenq]
    mov%1     m1, [mmsize + srcq+2*lenq]
    psrad     m0, 16
    psrad     m1, 16
    packssdw  m0, m1
    mov%1 [         dstq+lenq], m0
    add lenq, mmsize
        jl .next
    REP_RET
%endmacro


INIT_MMX mmx
INT16_TO_INT32 u
INT16_TO_INT32 a
INT32_TO_INT16 u
INT32_TO_INT16 a

INIT_XMM sse
INT16_TO_INT32 u
INT16_TO_INT32 a
INT32_TO_INT16 u
INT32_TO_INT16 a

INIT_XMM sse2
INT32_TO_FLOAT u
INT32_TO_FLOAT a
INT16_TO_FLOAT u
INT16_TO_FLOAT a
FLOAT_TO_INT32 u
FLOAT_TO_INT32 a
FLOAT_TO_INT16 u
FLOAT_TO_INT16 a
