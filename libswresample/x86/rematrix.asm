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

%include "libavutil/x86/x86util.asm"


SECTION_RODATA
align 32
dw1: times 8  dd 1
w1 : times 16 dw 1

SECTION .text

%macro MIX2_FLT 1
cglobal mix_2_1_%1_float, 7, 7, 6, out, in1, in2, coeffp, index1, index2, len
%ifidn %1, a
    test in1q, mmsize-1
        jne mix_2_1_float_u_int %+ SUFFIX
    test in2q, mmsize-1
        jne mix_2_1_float_u_int %+ SUFFIX
    test outq, mmsize-1
        jne mix_2_1_float_u_int %+ SUFFIX
%else
mix_2_1_float_u_int %+ SUFFIX
%endif
    VBROADCASTSS m4, [coeffpq + 4*index1q]
    VBROADCASTSS m5, [coeffpq + 4*index2q]
    shl lend    , 2
    add in1q    , lenq
    add in2q    , lenq
    add outq    , lenq
    neg lenq
.next:
%ifidn %1, a
    mulps        m0, m4, [in1q + lenq         ]
    mulps        m1, m5, [in2q + lenq         ]
    mulps        m2, m4, [in1q + lenq + mmsize]
    mulps        m3, m5, [in2q + lenq + mmsize]
%else
    movu         m0, [in1q + lenq         ]
    movu         m1, [in2q + lenq         ]
    movu         m2, [in1q + lenq + mmsize]
    movu         m3, [in2q + lenq + mmsize]
    mulps        m0, m0, m4
    mulps        m1, m1, m5
    mulps        m2, m2, m4
    mulps        m3, m3, m5
%endif
    addps        m0, m0, m1
    addps        m2, m2, m3
    mov%1  [outq + lenq         ], m0
    mov%1  [outq + lenq + mmsize], m2
    add        lenq, mmsize*2
        jl .next
    REP_RET
%endmacro

%macro MIX1_FLT 1
cglobal mix_1_1_%1_float, 5, 5, 3, out, in, coeffp, index, len
%ifidn %1, a
    test inq, mmsize-1
        jne mix_1_1_float_u_int %+ SUFFIX
    test outq, mmsize-1
        jne mix_1_1_float_u_int %+ SUFFIX
%else
mix_1_1_float_u_int %+ SUFFIX
%endif
    VBROADCASTSS m2, [coeffpq + 4*indexq]
    shl lenq    , 2
    add inq     , lenq
    add outq    , lenq
    neg lenq
.next:
%ifidn %1, a
    mulps        m0, m2, [inq + lenq         ]
    mulps        m1, m2, [inq + lenq + mmsize]
%else
    movu         m0, [inq + lenq         ]
    movu         m1, [inq + lenq + mmsize]
    mulps        m0, m0, m2
    mulps        m1, m1, m2
%endif
    mov%1  [outq + lenq         ], m0
    mov%1  [outq + lenq + mmsize], m1
    add        lenq, mmsize*2
        jl .next
    REP_RET
%endmacro

%macro MIX1_INT16 1
cglobal mix_1_1_%1_int16, 5, 5, 6, out, in, coeffp, index, len
%ifidn %1, a
    test inq, mmsize-1
        jne mix_1_1_int16_u_int %+ SUFFIX
    test outq, mmsize-1
        jne mix_1_1_int16_u_int %+ SUFFIX
%else
mix_1_1_int16_u_int %+ SUFFIX
%endif
    movd   m4, [coeffpq + 4*indexq]
    SPLATW m5, m4
    psllq  m4, 32
    psrlq  m4, 48
    mova   m0, [w1]
    psllw  m0, m4
    psrlw  m0, 1
    punpcklwd m5, m0
    add lenq    , lenq
    add inq     , lenq
    add outq    , lenq
    neg lenq
.next:
    mov%1        m0, [inq + lenq         ]
    mov%1        m2, [inq + lenq + mmsize]
    mova         m1, m0
    mova         m3, m2
    punpcklwd    m0, [w1]
    punpckhwd    m1, [w1]
    punpcklwd    m2, [w1]
    punpckhwd    m3, [w1]
    pmaddwd      m0, m5
    pmaddwd      m1, m5
    pmaddwd      m2, m5
    pmaddwd      m3, m5
    psrad        m0, m4
    psrad        m1, m4
    psrad        m2, m4
    psrad        m3, m4
    packssdw     m0, m1
    packssdw     m2, m3
    mov%1  [outq + lenq         ], m0
    mov%1  [outq + lenq + mmsize], m2
    add        lenq, mmsize*2
        jl .next
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro

%macro MIX2_INT16 1
cglobal mix_2_1_%1_int16, 7, 7, 8, out, in1, in2, coeffp, index1, index2, len
%ifidn %1, a
    test in1q, mmsize-1
        jne mix_2_1_int16_u_int %+ SUFFIX
    test in2q, mmsize-1
        jne mix_2_1_int16_u_int %+ SUFFIX
    test outq, mmsize-1
        jne mix_2_1_int16_u_int %+ SUFFIX
%else
mix_2_1_int16_u_int %+ SUFFIX
%endif
    movd   m4, [coeffpq + 4*index1q]
    movd   m6, [coeffpq + 4*index2q]
    SPLATW m5, m4
    SPLATW m6, m6
    psllq  m4, 32
    psrlq  m4, 48
    mova   m7, [dw1]
    pslld  m7, m4
    psrld  m7, 1
    punpcklwd m5, m6
    add lend    , lend
    add in1q    , lenq
    add in2q    , lenq
    add outq    , lenq
    neg lenq
.next:
    mov%1        m0, [in1q + lenq         ]
    mov%1        m2, [in2q + lenq         ]
    mova         m1, m0
    punpcklwd    m0, m2
    punpckhwd    m1, m2

    mov%1        m2, [in1q + lenq + mmsize]
    mov%1        m6, [in2q + lenq + mmsize]
    mova         m3, m2
    punpcklwd    m2, m6
    punpckhwd    m3, m6

    pmaddwd      m0, m5
    pmaddwd      m1, m5
    pmaddwd      m2, m5
    pmaddwd      m3, m5
    paddd        m0, m7
    paddd        m1, m7
    paddd        m2, m7
    paddd        m3, m7
    psrad        m0, m4
    psrad        m1, m4
    psrad        m2, m4
    psrad        m3, m4
    packssdw     m0, m1
    packssdw     m2, m3
    mov%1  [outq + lenq         ], m0
    mov%1  [outq + lenq + mmsize], m2
    add        lenq, mmsize*2
        jl .next
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro


INIT_MMX mmx
MIX1_INT16 u
MIX1_INT16 a
MIX2_INT16 u
MIX2_INT16 a

INIT_XMM sse
MIX2_FLT u
MIX2_FLT a
MIX1_FLT u
MIX1_FLT a

INIT_XMM sse2
MIX1_INT16 u
MIX1_INT16 a
MIX2_INT16 u
MIX2_INT16 a

%if HAVE_AVX_EXTERNAL
INIT_YMM avx
MIX2_FLT u
MIX2_FLT a
MIX1_FLT u
MIX1_FLT a
%endif
