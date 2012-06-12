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

SECTION .text

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

INIT_XMM sse
MIX1_FLT u
MIX1_FLT a

%if HAVE_AVX
INIT_YMM avx
MIX1_FLT u
MIX1_FLT a
%endif
