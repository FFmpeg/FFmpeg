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
align 32
flt2pm31: times 8 dd 4.6566129e-10
flt2p31 : times 8 dd 2147483648.0
flt2p15 : times 8 dd 32768.0

word_unpack_shuf : db  0, 1, 4, 5, 8, 9,12,13, 2, 3, 6, 7,10,11,14,15

SECTION .text


;to, from, a/u, log2_outsize, log_intsize, const
%macro PACK_2CH 5-7
cglobal pack_2ch_%2_to_%1_%3, 3, 4, 6, dst, src, len, src2
    mov src2q   , [srcq+gprsize]
    mov srcq    , [srcq]
    mov dstq    , [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne pack_2ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne pack_2ch_%2_to_%1_u_int %+ SUFFIX
    test src2q, mmsize-1
        jne pack_2ch_%2_to_%1_u_int %+ SUFFIX
%else
pack_2ch_%2_to_%1_u_int %+ SUFFIX
%endif
    lea     srcq , [srcq  + (1<<%5)*lenq]
    lea     src2q, [src2q + (1<<%5)*lenq]
    lea     dstq , [dstq  + (2<<%4)*lenq]
    neg     lenq
    %7
.next:
%if %4 >= %5
    mov%3     m0, [         srcq +(1<<%5)*lenq]
    mova      m1, m0
    mov%3     m2, [         src2q+(1<<%5)*lenq]
%if %5 == 1
    punpcklwd m0, m2
    punpckhwd m1, m2
%else
    punpckldq m0, m2
    punpckhdq m1, m2
%endif
    %6
%else
    mov%3     m0, [         srcq +(1<<%5)*lenq]
    mov%3     m1, [mmsize + srcq +(1<<%5)*lenq]
    mov%3     m2, [         src2q+(1<<%5)*lenq]
    mov%3     m3, [mmsize + src2q+(1<<%5)*lenq]
    %6
    mova      m2, m0
    punpcklwd m0, m1
    punpckhwd m2, m1
    SWAP 1,2
%endif
    mov%3 [           dstq+(2<<%4)*lenq], m0
    mov%3 [  mmsize + dstq+(2<<%4)*lenq], m1
%if %4 > %5
    mov%3 [2*mmsize + dstq+(2<<%4)*lenq], m2
    mov%3 [3*mmsize + dstq+(2<<%4)*lenq], m3
    add lenq, 4*mmsize/(2<<%4)
%else
    add lenq, 2*mmsize/(2<<%4)
%endif
        jl .next
    REP_RET
%endmacro

%macro UNPACK_2CH 5-7
cglobal unpack_2ch_%2_to_%1_%3, 3, 4, 7, dst, src, len, dst2
    mov dst2q   , [dstq+gprsize]
    mov srcq    , [srcq]
    mov dstq    , [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne unpack_2ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne unpack_2ch_%2_to_%1_u_int %+ SUFFIX
    test dst2q, mmsize-1
        jne unpack_2ch_%2_to_%1_u_int %+ SUFFIX
%else
unpack_2ch_%2_to_%1_u_int %+ SUFFIX
%endif
    lea     srcq , [srcq  + (2<<%5)*lenq]
    lea     dstq , [dstq  + (1<<%4)*lenq]
    lea     dst2q, [dst2q + (1<<%4)*lenq]
    neg     lenq
    %7
    mova      m6, [word_unpack_shuf]
.next:
    mov%3     m0, [           srcq +(2<<%5)*lenq]
    mov%3     m2, [  mmsize + srcq +(2<<%5)*lenq]
%if %5 == 1
%ifidn SUFFIX, _ssse3
    pshufb    m0, m6
    mova      m1, m0
    pshufb    m2, m6
    punpcklqdq m0,m2
    punpckhqdq m1,m2
%else
    mova      m1, m0
    punpcklwd m0,m2
    punpckhwd m1,m2

    mova      m2, m0
    punpcklwd m0,m1
    punpckhwd m2,m1

    mova      m1, m0
    punpcklwd m0,m2
    punpckhwd m1,m2
%endif
%else
    mova      m1, m0
    shufps    m0, m2, 10001000b
    shufps    m1, m2, 11011101b
%endif
%if %4 < %5
    mov%3     m2, [2*mmsize + srcq +(2<<%5)*lenq]
    mova      m3, m2
    mov%3     m4, [3*mmsize + srcq +(2<<%5)*lenq]
    shufps    m2, m4, 10001000b
    shufps    m3, m4, 11011101b
    SWAP 1,2
%endif
    %6
    mov%3 [           dstq+(1<<%4)*lenq], m0
%if %4 > %5
    mov%3 [          dst2q+(1<<%4)*lenq], m2
    mov%3 [ mmsize +  dstq+(1<<%4)*lenq], m1
    mov%3 [ mmsize + dst2q+(1<<%4)*lenq], m3
    add lenq, 2*mmsize/(1<<%4)
%else
    mov%3 [          dst2q+(1<<%4)*lenq], m1
    add lenq, mmsize/(1<<%4)
%endif
        jl .next
    REP_RET
%endmacro

%macro CONV 5-7
cglobal %2_to_%1_%3, 3, 3, 6, dst, src, len
    mov srcq    , [srcq]
    mov dstq    , [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne %2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne %2_to_%1_u_int %+ SUFFIX
%else
%2_to_%1_u_int %+ SUFFIX
%endif
    lea     srcq , [srcq  + (1<<%5)*lenq]
    lea     dstq , [dstq  + (1<<%4)*lenq]
    neg     lenq
    %7
.next:
    mov%3     m0, [           srcq +(1<<%5)*lenq]
    mov%3     m1, [  mmsize + srcq +(1<<%5)*lenq]
%if %4 < %5
    mov%3     m2, [2*mmsize + srcq +(1<<%5)*lenq]
    mov%3     m3, [3*mmsize + srcq +(1<<%5)*lenq]
%endif
    %6
    mov%3 [           dstq+(1<<%4)*lenq], m0
    mov%3 [  mmsize + dstq+(1<<%4)*lenq], m1
%if %4 > %5
    mov%3 [2*mmsize + dstq+(1<<%4)*lenq], m2
    mov%3 [3*mmsize + dstq+(1<<%4)*lenq], m3
    add lenq, 4*mmsize/(1<<%4)
%else
    add lenq, 2*mmsize/(1<<%4)
%endif
        jl .next
    REP_RET
%endmacro

%macro INT16_TO_INT32_N 0
    pxor      m2, m2
    pxor      m3, m3
    punpcklwd m2, m1
    punpckhwd m3, m1
    SWAP 4,0
    pxor      m0, m0
    pxor      m1, m1
    punpcklwd m0, m4
    punpckhwd m1, m4
%endmacro

%macro INT32_TO_INT16_N 0
    psrad     m0, 16
    psrad     m1, 16
    psrad     m2, 16
    psrad     m3, 16
    packssdw  m0, m1
    packssdw  m2, m3
    SWAP 1,2
%endmacro

%macro INT32_TO_FLOAT_INIT 0
    mova      m3, [flt2pm31]
%endmacro
%macro INT32_TO_FLOAT_N 0
    cvtdq2ps  m0, m0
    cvtdq2ps  m1, m1
    mulps m0, m0, m3
    mulps m1, m1, m3
%endmacro

%macro FLOAT_TO_INT32_INIT 0
    mova      m3, [flt2p31]
%endmacro
%macro FLOAT_TO_INT32_N 0
    mulps m0, m3
    mulps m1, m3
    cvtps2dq  m2, m0
    cvtps2dq  m4, m1
    cmpnltps m0, m3
    cmpnltps m1, m3
    paddd m0, m2
    paddd m1, m4
%endmacro

%macro INT16_TO_FLOAT_INIT 0
    mova      m5, [flt2pm31]
%endmacro
%macro INT16_TO_FLOAT_N 0
    INT16_TO_INT32_N
    cvtdq2ps  m0, m0
    cvtdq2ps  m1, m1
    cvtdq2ps  m2, m2
    cvtdq2ps  m3, m3
    mulps m0, m0, m5
    mulps m1, m1, m5
    mulps m2, m2, m5
    mulps m3, m3, m5
%endmacro

%macro FLOAT_TO_INT16_INIT 0
    mova      m5, [flt2p15]
%endmacro
%macro FLOAT_TO_INT16_N 0
    mulps m0, m5
    mulps m1, m5
    mulps m2, m5
    mulps m3, m5
    cvtps2dq  m0, m0
    cvtps2dq  m1, m1
    packssdw  m0, m1
    cvtps2dq  m1, m2
    cvtps2dq  m3, m3
    packssdw  m1, m3
%endmacro

INIT_MMX mmx
CONV int32, int16, u, 2, 1, INT16_TO_INT32_N
CONV int32, int16, a, 2, 1, INT16_TO_INT32_N
CONV int16, int32, u, 1, 2, INT32_TO_INT16_N
CONV int16, int32, a, 1, 2, INT32_TO_INT16_N

INIT_XMM sse
CONV int32, int16, u, 2, 1, INT16_TO_INT32_N
CONV int32, int16, a, 2, 1, INT16_TO_INT32_N
CONV int16, int32, u, 1, 2, INT32_TO_INT16_N
CONV int16, int32, a, 1, 2, INT32_TO_INT16_N

PACK_2CH int16, int16, u, 1, 1
PACK_2CH int16, int16, a, 1, 1
PACK_2CH int32, int32, u, 2, 2
PACK_2CH int32, int32, a, 2, 2
PACK_2CH int32, int16, u, 2, 1, INT16_TO_INT32_N
PACK_2CH int32, int16, a, 2, 1, INT16_TO_INT32_N
PACK_2CH int16, int32, u, 1, 2, INT32_TO_INT16_N
PACK_2CH int16, int32, a, 1, 2, INT32_TO_INT16_N

UNPACK_2CH int16, int16, u, 1, 1
UNPACK_2CH int16, int16, a, 1, 1
UNPACK_2CH int32, int32, u, 2, 2
UNPACK_2CH int32, int32, a, 2, 2
UNPACK_2CH int32, int16, u, 2, 1, INT16_TO_INT32_N
UNPACK_2CH int32, int16, a, 2, 1, INT16_TO_INT32_N
UNPACK_2CH int16, int32, u, 1, 2, INT32_TO_INT16_N
UNPACK_2CH int16, int32, a, 1, 2, INT32_TO_INT16_N

INIT_XMM sse2
CONV float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
CONV float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
CONV int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
CONV int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
CONV float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
CONV float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
CONV int16, float, u, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT
CONV int16, float, a, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT

PACK_2CH float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_2CH float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_2CH int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_2CH int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_2CH float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
PACK_2CH float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
PACK_2CH int16, float, u, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT
PACK_2CH int16, float, a, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT

UNPACK_2CH float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_2CH float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_2CH int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
UNPACK_2CH int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
UNPACK_2CH float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
UNPACK_2CH float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
UNPACK_2CH int16, float, u, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT
UNPACK_2CH int16, float, a, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT


INIT_XMM ssse3
UNPACK_2CH int16, int16, u, 1, 1
UNPACK_2CH int16, int16, a, 1, 1
UNPACK_2CH int32, int16, u, 2, 1, INT16_TO_INT32_N
UNPACK_2CH int32, int16, a, 2, 1, INT16_TO_INT32_N
UNPACK_2CH float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
UNPACK_2CH float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT

%if HAVE_AVX
INIT_YMM avx
CONV float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
CONV float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
%endif
