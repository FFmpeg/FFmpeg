;*****************************************************************************
;* x86-optimized functions for bwdif filter
;*
;* Copyright (C) 2016 Thomas Mundt <loudmax@yahoo.de>
;*
;* Based on yadif simd code
;* Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
;*               2013 Daniel Kang <daniel.d.kang@gmail.com>
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

pw_coefhf:  times 4 dw  1016, 5570
pw_coefhf1: times 8 dw -3801
pw_coefsp:  times 4 dw  5077, -981
pw_splfdif: times 4 dw  -768,  768

SECTION .text

%macro LOAD8 2
    movh         %1, %2
    punpcklbw    %1, m7
%endmacro

%macro LOAD12 2
    movu         %1, %2
%endmacro

%macro DISP8 0
    packuswb     m2, m2
    movh     [dstq], m2
%endmacro

%macro DISP12 0
    CLIPW        m2, m7, m12
    movu     [dstq], m2
%endmacro

%macro FILTER 5
    pxor         m7, m7
.loop%1:
    LOAD%4       m0, [curq+t0*%5]
    LOAD%4       m1, [curq+t1*%5]
    LOAD%4       m2, [%2]
    LOAD%4       m3, [%3]
    mova         m4, m3
    paddw        m3, m2
    psubw        m2, m4
    ABS1         m2, m4
    mova         m8, m3
    mova         m9, m2
    LOAD%4       m3, [prevq+t0*%5]
    LOAD%4       m4, [prevq+t1*%5]
    psubw        m3, m0
    psubw        m4, m1
    ABS2         m3, m4, m5, m6
    paddw        m3, m4
    psrlw        m2, 1
    psrlw        m3, 1
    pmaxsw       m2, m3
    LOAD%4       m3, [nextq+t0*%5]
    LOAD%4       m4, [nextq+t1*%5]
    psubw        m3, m0
    psubw        m4, m1
    ABS2         m3, m4, m5, m6
    paddw        m3, m4
    psrlw        m3, 1
    pmaxsw       m2, m3

    LOAD%4       m3, [%2+t0*2*%5]
    LOAD%4       m4, [%3+t0*2*%5]
    LOAD%4       m5, [%2+t1*2*%5]
    LOAD%4       m6, [%3+t1*2*%5]
    paddw        m3, m4
    paddw        m5, m6
    mova         m6, m3
    paddw        m6, m5
    mova        m10, m6
    psrlw        m3, 1
    psrlw        m5, 1
    psubw        m3, m0
    psubw        m5, m1
    mova         m6, m3
    pminsw       m3, m5
    pmaxsw       m5, m6
    mova         m4, m8
    psraw        m4, 1
    mova         m6, m4
    psubw        m6, m0
    psubw        m4, m1
    pmaxsw       m3, m6
    pminsw       m5, m6
    pmaxsw       m3, m4
    pminsw       m5, m4
    mova         m6, m7
    psubw        m6, m3
    pmaxsw       m6, m5
    mova         m3, m2
    pcmpgtw      m3, m7
    pand         m6, m3
    pmaxsw       m2, m6
    mova        m11, m2

    LOAD%4       m2, [%2+t0*4*%5]
    LOAD%4       m3, [%3+t0*4*%5]
    LOAD%4       m4, [%2+t1*4*%5]
    LOAD%4       m5, [%3+t1*4*%5]
    paddw        m2, m3
    paddw        m4, m5
    paddw        m2, m4
    mova         m3, m2
    punpcklwd    m2, m8
    punpckhwd    m3, m8
    pmaddwd      m2, [pw_coefhf]
    pmaddwd      m3, [pw_coefhf]
    mova         m4, m10
    mova         m6, m4
    pmullw       m4, [pw_coefhf1]
    pmulhw       m6, [pw_coefhf1]
    mova         m5, m4
    punpcklwd    m4, m6
    punpckhwd    m5, m6
    paddd        m2, m4
    paddd        m3, m5
    psrad        m2, 2
    psrad        m3, 2

    mova         m4, m0
    paddw        m0, m1
%if ARCH_X86_64
    LOAD%4       m5, [curq+t2*%5]
    LOAD%4       m6, [curq+t3*%5]
%else
    mov          r4, prefs3mp
    mov          r5, mrefs3mp
    LOAD%4       m5, [curq+t0*%5]
    LOAD%4       m6, [curq+t1*%5]
    mov          r4, prefsmp
    mov          r5, mrefsmp
%endif
    paddw        m6, m5
    psubw        m1, m4
    ABS1         m1, m4
    pcmpgtw      m1, m9
    mova         m4, m1
    punpcklwd    m1, m4
    punpckhwd    m4, m4
    pand         m2, m1
    pand         m3, m4
    mova         m5, [pw_splfdif]
    mova         m7, m5
    pand         m5, m1
    pand         m7, m4
    paddw        m5, [pw_coefsp]
    paddw        m7, [pw_coefsp]
    mova         m4, m0
    punpcklwd    m0, m6
    punpckhwd    m4, m6
    pmaddwd      m0, m5
    pmaddwd      m4, m7
    paddd        m2, m0
    paddd        m3, m4
    psrad        m2, 13
    psrad        m3, 13
    packssdw     m2, m3

    mova         m4, m8
    psraw        m4, 1
    mova         m0, m11
    mova         m3, m4
    psubw        m4, m0
    paddw        m3, m0
    CLIPW        m2, m4, m3
    pxor         m7, m7
    DISP%4

    add        dstq, STEP
    add       prevq, STEP
    add        curq, STEP
    add       nextq, STEP
    sub    DWORD wm, mmsize/2
    jg .loop%1
%endmacro

%macro PROC 2
%if ARCH_X86_64
    movsxd       r5, DWORD prefsm
    movsxd       r6, DWORD mrefsm
    movsxd       r7, DWORD prefs3m
    movsxd       r8, DWORD mrefs3m
    DECLARE_REG_TMP 5, 6, 7, 8
%else
    %define m8  [rsp+ 0]
    %define m9  [rsp+16]
    %define m10 [rsp+32]
    %define m11 [rsp+48]
    mov          r4, prefsmp
    mov          r5, mrefsmp
    DECLARE_REG_TMP 4, 5
%endif
    cmp DWORD paritym, 0
    je .parity0
    FILTER 1, prevq, curq, %1, %2
    jmp .ret
.parity0:
    FILTER 0, curq, nextq, %1, %2
.ret:
    RET
%endmacro

%macro BWDIF 0
%if ARCH_X86_64
cglobal bwdif_filter_line, 4, 9, 12, 0, dst, prev, cur, next, w, prefs, \
                                        mrefs, prefs2, mrefs2, prefs3, mrefs3, \
                                        prefs4, mrefs4, parity, clip_max
%else
cglobal bwdif_filter_line, 4, 6, 8, 64, dst, prev, cur, next, w, prefs, \
                                        mrefs, prefs2, mrefs2, prefs3, mrefs3, \
                                        prefs4, mrefs4, parity, clip_max
%endif
    %define STEP mmsize/2
    PROC 8, 1

%if ARCH_X86_64
cglobal bwdif_filter_line_12bit, 4, 9, 13, 0, dst, prev, cur, next, w, \
                                              prefs, mrefs, prefs2, mrefs2, \
                                              prefs3, mrefs3, prefs4, \
                                              mrefs4, parity, clip_max
    movd        m12, DWORD clip_maxm
    SPLATW      m12, m12, 0
%else
cglobal bwdif_filter_line_12bit, 4, 6, 8, 80, dst, prev, cur, next, w, \
                                              prefs, mrefs, prefs2, mrefs2, \
                                              prefs3, mrefs3, prefs4, \
                                              mrefs4, parity, clip_max
    %define m12 [rsp+64]
    movd         m0, DWORD clip_maxm
    SPLATW       m0, m0, 0
    mova        m12, m0
%endif
    %define STEP mmsize
    PROC 12, 2
%endmacro

INIT_XMM ssse3
BWDIF
INIT_XMM sse2
BWDIF
