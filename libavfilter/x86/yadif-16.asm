;*****************************************************************************
;* x86-optimized functions for yadif filter
;*
;* Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (c) 2013 Daniel Kang <daniel.d.kang@gmail.com>
;* Copyright (c) 2011-2013 James Darnley <james.darnley@gmail.com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License along
;* with FFmpeg; if not, write to the Free Software Foundation, Inc.,
;* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pw_1:    times 8 dw 1
pw_8000: times 8 dw 0x8000
pd_1:    times 4 dd 1
pd_8000: times 4 dd 0x8000

SECTION .text

%macro PIXSHIFT1 1
%if cpuflag(sse2)
    psrldq %1, 2
%else
    psrlq %1, 16
%endif
%endmacro

%macro PIXSHIFT2 1
%if cpuflag(sse2)
    psrldq %1, 4
%else
    psrlq %1, 32
%endif
%endmacro

%macro PABS 2
%if cpuflag(ssse3)
    pabsd %1, %1
%else
    pxor    %2, %2
    pcmpgtd %2, %1
    pxor    %1, %2
    psubd   %1, %2
%endif
%endmacro

%macro PACK 1
%if cpuflag(sse4)
    packusdw %1, %1
%else
    psubd    %1, [pd_8000]
    packssdw %1, %1
    paddw    %1, [pw_8000]
%endif
%endmacro

%macro PMINSD 3
%if cpuflag(sse4)
    pminsd %1, %2
%else
    mova    %3, %2
    pcmpgtd %3, %1
    pand    %1, %3
    pandn   %3, %2
    por     %1, %3
%endif
%endmacro

%macro PMAXSD 3
%if cpuflag(sse4)
    pmaxsd %1, %2
%else
    mova    %3, %1
    pcmpgtd %3, %2
    pand    %1, %3
    pandn   %3, %2
    por     %1, %3
%endif
%endmacro

%macro PMAXUW 2
%if cpuflag(sse4)
    pmaxuw %1, %2
%else
    psubusw %1, %2
    paddusw %1, %2
%endif
%endmacro

%macro CHECK 2
    movu      m2, [curq+t1+%1*2]
    movu      m3, [curq+t0+%2*2]
    mova      m4, m2
    mova      m5, m2
    pxor      m4, m3
    pavgw     m5, m3
    pand      m4, [pw_1]
    psubusw   m5, m4
%if mmsize == 16
    psrldq    m5, 2
%else
    psrlq     m5, 16
%endif
    punpcklwd m5, m7
    mova      m4, m2
    psubusw   m2, m3
    psubusw   m3, m4
    PMAXUW    m2, m3
    mova      m3, m2
    mova      m4, m2
%if mmsize == 16
    psrldq    m3, 2
    psrldq    m4, 4
%else
    psrlq     m3, 16
    psrlq     m4, 32
%endif
    punpcklwd m2, m7
    punpcklwd m3, m7
    punpcklwd m4, m7
    paddd     m2, m3
    paddd     m2, m4
%endmacro

%macro CHECK1 0
    mova    m3, m0
    pcmpgtd m3, m2
    PMINSD  m0, m2, m6
    mova    m6, m3
    pand    m5, m3
    pandn   m3, m1
    por     m3, m5
    mova    m1, m3
%endmacro

%macro CHECK2 0
    paddd   m6, [pd_1]
    pslld   m6, 30
    paddd   m2, m6
    mova    m3, m0
    pcmpgtd m3, m2
    PMINSD  m0, m2, m4
    pand    m5, m3
    pandn   m3, m1
    por     m3, m5
    mova    m1, m3
%endmacro

; This version of CHECK2 has 3 fewer instructions on sets older than SSE4 but I
; am not sure whether it is any faster.  A rewrite or refactor of the filter
; code should make it possible to eliminate the move intruction at the end.  It
; exists to satisfy the expectation that the "score" values are in m1.

; %macro CHECK2 0
;     mova    m3, m0
;     pcmpgtd m0, m2
;     pand    m0, m6
;     mova    m6, m0
;     pand    m5, m6
;     pand    m2, m0
;     pandn   m6, m1
;     pandn   m0, m3
;     por     m6, m5
;     por     m0, m2
;     mova    m1, m6
; %endmacro

%macro LOAD 2
    movh      %1, %2
    punpcklwd %1, m7
%endmacro

%macro FILTER 3
.loop%1:
    pxor         m7, m7
    LOAD         m0, [curq+t1]
    LOAD         m1, [curq+t0]
    LOAD         m2, [%2]
    LOAD         m3, [%3]
    mova         m4, m3
    paddd        m3, m2
    psrad        m3, 1
    mova   [rsp+ 0], m0
    mova   [rsp+16], m3
    mova   [rsp+32], m1
    psubd        m2, m4
    PABS         m2, m4
    LOAD         m3, [prevq+t1]
    LOAD         m4, [prevq+t0]
    psubd        m3, m0
    psubd        m4, m1
    PABS         m3, m5
    PABS         m4, m5
    paddd        m3, m4
    psrld        m2, 1
    psrld        m3, 1
    PMAXSD       m2, m3, m6
    LOAD         m3, [nextq+t1]
    LOAD         m4, [nextq+t0]
    psubd        m3, m0
    psubd        m4, m1
    PABS         m3, m5
    PABS         m4, m5
    paddd        m3, m4
    psrld        m3, 1
    PMAXSD       m2, m3, m6
    mova   [rsp+48], m2

    paddd        m1, m0
    paddd        m0, m0
    psubd        m0, m1
    psrld        m1, 1
    PABS         m0, m2

    movu         m2, [curq+t1-1*2]
    movu         m3, [curq+t0-1*2]
    mova         m4, m2
    psubusw      m2, m3
    psubusw      m3, m4
    PMAXUW       m2, m3
%if mmsize == 16
    mova         m3, m2
    psrldq       m3, 4
%else
    mova         m3, m2
    psrlq        m3, 32
%endif
    punpcklwd    m2, m7
    punpcklwd    m3, m7
    paddd        m0, m2
    paddd        m0, m3
    psubd        m0, [pd_1]

    CHECK -2, 0
    CHECK1
    CHECK -3, 1
    CHECK2
    CHECK 0, -2
    CHECK1
    CHECK 1, -3
    CHECK2

    mova         m6, [rsp+48]
    cmp   DWORD r8m, 2
    jge .end%1
    LOAD         m2, [%2+t1*2]
    LOAD         m4, [%3+t1*2]
    LOAD         m3, [%2+t0*2]
    LOAD         m5, [%3+t0*2]
    paddd        m2, m4
    paddd        m3, m5
    psrld        m2, 1
    psrld        m3, 1
    mova         m4, [rsp+ 0]
    mova         m5, [rsp+16]
    mova         m7, [rsp+32]
    psubd        m2, m4
    psubd        m3, m7
    mova         m0, m5
    psubd        m5, m4
    psubd        m0, m7
    mova         m4, m2
    PMINSD       m2, m3, m7
    PMAXSD       m3, m4, m7
    PMAXSD       m2, m5, m7
    PMINSD       m3, m5, m7
    PMAXSD       m2, m0, m7
    PMINSD       m3, m0, m7
    pxor         m4, m4
    PMAXSD       m6, m3, m7
    psubd        m4, m2
    PMAXSD       m6, m4, m7

.end%1:
    mova         m2, [rsp+16]
    mova         m3, m2
    psubd        m2, m6
    paddd        m3, m6
    PMAXSD       m1, m2, m7
    PMINSD       m1, m3, m7
    PACK         m1

    movh     [dstq], m1
    add        dstq, mmsize/2
    add       prevq, mmsize/2
    add        curq, mmsize/2
    add       nextq, mmsize/2
    sub   DWORD r4m, mmsize/4
    jg .loop%1
%endmacro

%macro YADIF 0
%if ARCH_X86_32
cglobal yadif_filter_line_16bit, 4, 6, 8, 80, dst, prev, cur, next, w, \
                                              prefs, mrefs, parity, mode
%else
cglobal yadif_filter_line_16bit, 4, 7, 8, 80, dst, prev, cur, next, w, \
                                              prefs, mrefs, parity, mode
%endif
%if ARCH_X86_32
    mov            r4, r5mp
    mov            r5, r6mp
    DECLARE_REG_TMP 4,5
%else
    movsxd         r5, DWORD r5m
    movsxd         r6, DWORD r6m
    DECLARE_REG_TMP 5,6
%endif

    cmp DWORD paritym, 0
    je .parity0
    FILTER 1, prevq, curq
    jmp .ret

.parity0:
    FILTER 0, curq, nextq

.ret:
    RET
%endmacro

INIT_XMM sse4
YADIF
INIT_XMM ssse3
YADIF
INIT_XMM sse2
YADIF
%if ARCH_X86_32
INIT_MMX mmxext
YADIF
%endif
