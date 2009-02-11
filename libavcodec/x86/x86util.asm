;*****************************************************************************
;* x86util.asm
;*****************************************************************************
;* Copyright (C) 2008 Loren Merritt <lorenm@u.washington.edu>
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* This program is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License
;* along with this program; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
;*****************************************************************************

%macro SBUTTERFLY 4
    mova      m%4, m%2
    punpckl%1 m%2, m%3
    punpckh%1 m%4, m%3
    SWAP %3, %4
%endmacro

%macro TRANSPOSE4x4W 5
    SBUTTERFLY wd, %1, %2, %5
    SBUTTERFLY wd, %3, %4, %5
    SBUTTERFLY dq, %1, %3, %5
    SBUTTERFLY dq, %2, %4, %5
    SWAP %2, %3
%endmacro

%macro TRANSPOSE2x4x4W 5
    SBUTTERFLY wd,  %1, %2, %5
    SBUTTERFLY wd,  %3, %4, %5
    SBUTTERFLY dq,  %1, %3, %5
    SBUTTERFLY dq,  %2, %4, %5
    SBUTTERFLY qdq, %1, %2, %5
    SBUTTERFLY qdq, %3, %4, %5
%endmacro

%macro TRANSPOSE4x4D 5
    SBUTTERFLY dq,  %1, %2, %5
    SBUTTERFLY dq,  %3, %4, %5
    SBUTTERFLY qdq, %1, %3, %5
    SBUTTERFLY qdq, %2, %4, %5
    SWAP %2, %3
%endmacro

%macro TRANSPOSE8x8W 9-11
%ifdef ARCH_X86_64
    SBUTTERFLY wd,  %1, %2, %9
    SBUTTERFLY wd,  %3, %4, %9
    SBUTTERFLY wd,  %5, %6, %9
    SBUTTERFLY wd,  %7, %8, %9
    SBUTTERFLY dq,  %1, %3, %9
    SBUTTERFLY dq,  %2, %4, %9
    SBUTTERFLY dq,  %5, %7, %9
    SBUTTERFLY dq,  %6, %8, %9
    SBUTTERFLY qdq, %1, %5, %9
    SBUTTERFLY qdq, %2, %6, %9
    SBUTTERFLY qdq, %3, %7, %9
    SBUTTERFLY qdq, %4, %8, %9
    SWAP %2, %5
    SWAP %4, %7
%else
; in:  m0..m7, unless %11 in which case m6 is in %9
; out: m0..m7, unless %11 in which case m4 is in %10
; spills into %9 and %10
%if %0<11
    movdqa %9, m%7
%endif
    SBUTTERFLY wd,  %1, %2, %7
    movdqa %10, m%2
    movdqa m%7, %9
    SBUTTERFLY wd,  %3, %4, %2
    SBUTTERFLY wd,  %5, %6, %2
    SBUTTERFLY wd,  %7, %8, %2
    SBUTTERFLY dq,  %1, %3, %2
    movdqa %9, m%3
    movdqa m%2, %10
    SBUTTERFLY dq,  %2, %4, %3
    SBUTTERFLY dq,  %5, %7, %3
    SBUTTERFLY dq,  %6, %8, %3
    SBUTTERFLY qdq, %1, %5, %3
    SBUTTERFLY qdq, %2, %6, %3
    movdqa %10, m%2
    movdqa m%3, %9
    SBUTTERFLY qdq, %3, %7, %2
    SBUTTERFLY qdq, %4, %8, %2
    SWAP %2, %5
    SWAP %4, %7
%if 0<11
    movdqa m%5, %10
%endif
%endif
%endmacro

%macro ABS1_MMX 2    ; a, tmp
    pxor    %2, %2
    psubw   %2, %1
    pmaxsw  %1, %2
%endmacro

%macro ABS2_MMX 4    ; a, b, tmp0, tmp1
    pxor    %3, %3
    pxor    %4, %4
    psubw   %3, %1
    psubw   %4, %2
    pmaxsw  %1, %3
    pmaxsw  %2, %4
%endmacro

%macro ABS1_SSSE3 2
    pabsw   %1, %1
%endmacro

%macro ABS2_SSSE3 4
    pabsw   %1, %1
    pabsw   %2, %2
%endmacro

%define ABS1 ABS1_MMX
%define ABS2 ABS2_MMX

%macro ABS4 6
    ABS2 %1, %2, %5, %6
    ABS2 %3, %4, %5, %6
%endmacro

%macro SPLATB_MMX 3
    movd      %1, [%2-3] ;to avoid crossing a cacheline
    punpcklbw %1, %1
%if mmsize==16
    pshuflw   %1, %1, 0xff
    punpcklqdq %1, %1
%else
    pshufw    %1, %1, 0xff
%endif
%endmacro

%macro SPLATB_SSSE3 3
    movd      %1, [%2-3]
    pshufb    %1, %3
%endmacro

%macro PALIGNR_MMX 4
    %ifnidn %4, %2
    mova    %4, %2
    %endif
    %if mmsize == 8
    psllq   %1, (8-%3)*8
    psrlq   %4, %3*8
    %else
    pslldq  %1, 16-%3
    psrldq  %4, %3
    %endif
    por     %1, %4
%endmacro

%macro PALIGNR_SSSE3 4
    palignr %1, %2, %3
%endmacro

%macro SUMSUB_BA 2
    paddw   %1, %2
    paddw   %2, %2
    psubw   %2, %1
%endmacro

%macro SUMSUB_BADC 4
    paddw   %1, %2
    paddw   %3, %4
    paddw   %2, %2
    paddw   %4, %4
    psubw   %2, %1
    psubw   %4, %3
%endmacro

%macro HADAMARD8_1D 8
    SUMSUB_BADC %1, %5, %2, %6
    SUMSUB_BADC %3, %7, %4, %8
    SUMSUB_BADC %1, %3, %2, %4
    SUMSUB_BADC %5, %7, %6, %8
    SUMSUB_BADC %1, %2, %3, %4
    SUMSUB_BADC %5, %6, %7, %8
%endmacro

%macro SUMSUB2_AB 3
    mova    %3, %1
    paddw   %1, %1
    paddw   %1, %2
    psubw   %3, %2
    psubw   %3, %2
%endmacro

%macro SUMSUBD2_AB 4
    mova    %4, %1
    mova    %3, %2
    psraw   %2, 1
    psraw   %4, 1
    paddw   %1, %2
    psubw   %4, %3
%endmacro

%macro LOAD_DIFF 5
%ifidn %3, none
    movh       %1, %4
    movh       %2, %5
    punpcklbw  %1, %2
    punpcklbw  %2, %2
    psubw      %1, %2
%else
    movh       %1, %4
    punpcklbw  %1, %3
    movh       %2, %5
    punpcklbw  %2, %3
    psubw      %1, %2
%endif
%endmacro

%macro LOAD_DIFF_8x4P 6-8 r0,r2 ; 4x dest, 2x temp, 2x pointer
    LOAD_DIFF %1, %5, none, [%7],      [%8]
    LOAD_DIFF %2, %6, none, [%7+r1],   [%8+r3]
    LOAD_DIFF %3, %5, none, [%7+2*r1], [%8+2*r3]
    LOAD_DIFF %4, %6, none, [%7+r4],   [%8+r5]
%endmacro

%macro STORE_DIFF 4
    psraw      %1, 6
    movh       %2, %4
    punpcklbw  %2, %3
    paddsw     %1, %2
    packuswb   %1, %1
    movh       %4, %1
%endmacro

