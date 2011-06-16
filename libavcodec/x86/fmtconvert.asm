;******************************************************************************
;* x86 optimized Format Conversion Utils
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

%include "x86inc.asm"
%include "x86util.asm"

SECTION_TEXT

%macro PSWAPD_SSE 2
    pshufw %1, %2, 0x4e
%endmacro
%macro PSWAPD_3DN1 2
    movq  %1, %2
    psrlq %1, 32
    punpckldq %1, %2
%endmacro

%macro FLOAT_TO_INT16_INTERLEAVE6 1
; void float_to_int16_interleave6_sse(int16_t *dst, const float **src, int len)
cglobal float_to_int16_interleave6_%1, 2,7,0, dst, src, src1, src2, src3, src4, src5
%ifdef ARCH_X86_64
    %define lend r10d
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov src1q, [srcq+1*gprsize]
    mov src2q, [srcq+2*gprsize]
    mov src3q, [srcq+3*gprsize]
    mov src4q, [srcq+4*gprsize]
    mov src5q, [srcq+5*gprsize]
    mov srcq,  [srcq]
    sub src1q, srcq
    sub src2q, srcq
    sub src3q, srcq
    sub src4q, srcq
    sub src5q, srcq
.loop:
    cvtps2pi   mm0, [srcq]
    cvtps2pi   mm1, [srcq+src1q]
    cvtps2pi   mm2, [srcq+src2q]
    cvtps2pi   mm3, [srcq+src3q]
    cvtps2pi   mm4, [srcq+src4q]
    cvtps2pi   mm5, [srcq+src5q]
    packssdw   mm0, mm3
    packssdw   mm1, mm4
    packssdw   mm2, mm5
    pswapd     mm3, mm0
    punpcklwd  mm0, mm1
    punpckhwd  mm1, mm2
    punpcklwd  mm2, mm3
    pswapd     mm3, mm0
    punpckldq  mm0, mm2
    punpckhdq  mm2, mm1
    punpckldq  mm1, mm3
    movq [dstq   ], mm0
    movq [dstq+16], mm2
    movq [dstq+ 8], mm1
    add srcq, 8
    add dstq, 24
    sub lend, 2
    jg .loop
    emms
    RET
%endmacro ; FLOAT_TO_INT16_INTERLEAVE6

%define pswapd PSWAPD_SSE
FLOAT_TO_INT16_INTERLEAVE6 sse
%define cvtps2pi pf2id
%define pswapd PSWAPD_3DN1
FLOAT_TO_INT16_INTERLEAVE6 3dnow
%undef pswapd
FLOAT_TO_INT16_INTERLEAVE6 3dn2
%undef cvtps2pi

;-----------------------------------------------------------------------------
; void ff_float_interleave6(float *dst, const float **src, unsigned int len);
;-----------------------------------------------------------------------------

%macro FLOAT_INTERLEAVE6 2
cglobal float_interleave6_%1, 2,7,%2, dst, src, src1, src2, src3, src4, src5
%ifdef ARCH_X86_64
    %define lend r10d
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov    src1q, [srcq+1*gprsize]
    mov    src2q, [srcq+2*gprsize]
    mov    src3q, [srcq+3*gprsize]
    mov    src4q, [srcq+4*gprsize]
    mov    src5q, [srcq+5*gprsize]
    mov     srcq, [srcq]
    sub    src1q, srcq
    sub    src2q, srcq
    sub    src3q, srcq
    sub    src4q, srcq
    sub    src5q, srcq
.loop:
%ifidn %1, sse
    movaps    m0, [srcq]
    movaps    m1, [srcq+src1q]
    movaps    m2, [srcq+src2q]
    movaps    m3, [srcq+src3q]
    movaps    m4, [srcq+src4q]
    movaps    m5, [srcq+src5q]

    SBUTTERFLYPS 0, 1, 6
    SBUTTERFLYPS 2, 3, 6
    SBUTTERFLYPS 4, 5, 6

    movaps    m6, m4
    shufps    m4, m0, 0xe4
    movlhps   m0, m2
    movhlps   m6, m2
    movaps [dstq   ], m0
    movaps [dstq+16], m4
    movaps [dstq+32], m6

    movaps    m6, m5
    shufps    m5, m1, 0xe4
    movlhps   m1, m3
    movhlps   m6, m3
    movaps [dstq+48], m1
    movaps [dstq+64], m5
    movaps [dstq+80], m6
%else ; mmx
    movq       m0, [srcq]
    movq       m1, [srcq+src1q]
    movq       m2, [srcq+src2q]
    movq       m3, [srcq+src3q]
    movq       m4, [srcq+src4q]
    movq       m5, [srcq+src5q]

    SBUTTERFLY dq, 0, 1, 6
    SBUTTERFLY dq, 2, 3, 6
    SBUTTERFLY dq, 4, 5, 6
    movq [dstq   ], m0
    movq [dstq+ 8], m2
    movq [dstq+16], m4
    movq [dstq+24], m1
    movq [dstq+32], m3
    movq [dstq+40], m5
%endif
    add      srcq, mmsize
    add      dstq, mmsize*6
    sub      lend, mmsize/4
    jg .loop
%ifidn %1, mmx
    emms
%endif
    REP_RET
%endmacro

INIT_MMX
FLOAT_INTERLEAVE6 mmx, 0
INIT_XMM
FLOAT_INTERLEAVE6 sse, 7

;-----------------------------------------------------------------------------
; void ff_float_interleave2(float *dst, const float **src, unsigned int len);
;-----------------------------------------------------------------------------

%macro FLOAT_INTERLEAVE2 2
cglobal float_interleave2_%1, 3,4,%2, dst, src, len, src1
    mov     src1q, [srcq+gprsize]
    mov      srcq, [srcq        ]
    sub     src1q, srcq
.loop
    MOVPS      m0, [srcq             ]
    MOVPS      m1, [srcq+src1q       ]
    MOVPS      m3, [srcq      +mmsize]
    MOVPS      m4, [srcq+src1q+mmsize]

    MOVPS      m2, m0
    PUNPCKLDQ  m0, m1
    PUNPCKHDQ  m2, m1

    MOVPS      m1, m3
    PUNPCKLDQ  m3, m4
    PUNPCKHDQ  m1, m4

    MOVPS [dstq         ], m0
    MOVPS [dstq+1*mmsize], m2
    MOVPS [dstq+2*mmsize], m3
    MOVPS [dstq+3*mmsize], m1

    add      srcq, mmsize*2
    add      dstq, mmsize*4
    sub      lend, mmsize/2
    jg .loop
%ifidn %1, mmx
    emms
%endif
    REP_RET
%endmacro

INIT_MMX
%define MOVPS     movq
%define PUNPCKLDQ punpckldq
%define PUNPCKHDQ punpckhdq
FLOAT_INTERLEAVE2 mmx, 0
INIT_XMM
%define MOVPS     movaps
%define PUNPCKLDQ unpcklps
%define PUNPCKHDQ unpckhps
FLOAT_INTERLEAVE2 sse, 5
