;*****************************************************************************
;* x86-optimized functions for maskedmerge filter
;*
;* Copyright (C) 2015 Paul B Mahol
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
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pw_127: times 8 dw 127
pw_255: times 8 dw 255
pw_32897: times 8 dw 32897

SECTION .text

INIT_XMM sse2
%if ARCH_X86_64
cglobal maskedmerge8, 8, 11, 8, bsrc, osrc, msrc, dst, blinesize, olinesize, mlinesize, dlinesize, w, h, x
    mov         wd, dword wm
    mov         hd, dword hm
%else
cglobal maskedmerge8, 5, 7, 8, bsrc, osrc, msrc, dst, blinesize, w, x
    mov         wd, r8m
%define olinesizeq r5mp
%define mlinesizeq r6mp
%define dlinesizeq r7mp
%define hd r9mp
%endif
    mova        m4, [pw_255]
    mova        m5, [pw_127]
    mova        m7, [pw_32897]
    pxor        m6, m6
    add      bsrcq, wq
    add      osrcq, wq
    add      msrcq, wq
    add       dstq, wq
    neg         wq
.nextrow:
    mov         xq, wq

    .loop:
        movh            m0, [bsrcq + xq]
        movh            m1, [osrcq + xq]
        movh            m3, [msrcq + xq]
        mova            m2, m4
        punpcklbw       m0, m6
        punpcklbw       m1, m6
        punpcklbw       m3, m6
        psubw           m2, m3
        pmullw          m2, m0
        pmullw          m1, m3
        paddw           m1, m2
        paddw           m1, m5
        pmulhuw         m1, m7
        psrlw           m1, 7
        packuswb        m1, m1
        movh   [dstq + xq], m1
        add             xq, mmsize / 2
    jl .loop

    add         bsrcq, blinesizeq
    add         osrcq, olinesizeq
    add         msrcq, mlinesizeq
    add          dstq, dlinesizeq
    sub         hd, 1
    jg .nextrow
REP_RET
