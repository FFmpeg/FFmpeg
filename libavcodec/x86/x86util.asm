;*****************************************************************************
;* x86util.asm
;*****************************************************************************
;* Copyright (C) 2008-2010 x264 project
;*
;* Authors: Loren Merritt <lorenm@u.washington.edu>
;*          Holger Lubitz <holger@lubitz.org>
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%macro SBUTTERFLY 4
    mova      m%4, m%2
    punpckl%1 m%2, m%3
    punpckh%1 m%4, m%3
    SWAP %3, %4
%endmacro

%macro SBUTTERFLY2 4
    mova      m%4, m%2
    punpckh%1 m%2, m%3
    punpckl%1 m%4, m%3
    SWAP %2, %4, %3
%endmacro

%macro TRANSPOSE4x4B 5
    SBUTTERFLY bw, %1, %2, %5
    SBUTTERFLY bw, %3, %4, %5
    SBUTTERFLY wd, %1, %3, %5
    SBUTTERFLY wd, %2, %4, %5
    SWAP %2, %3
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
%if %0<11
    movdqa m%5, %10
%endif
%endif
%endmacro

; PABSW macros assume %1 != %2, while ABS1/2 macros work in-place
%macro PABSW_MMX 2
    pxor       %1, %1
    pcmpgtw    %1, %2
    pxor       %2, %1
    psubw      %2, %1
    SWAP       %1, %2
%endmacro

%macro PSIGNW_MMX 2
    pxor       %1, %2
    psubw      %1, %2
%endmacro

%macro PABSW_MMX2 2
    pxor    %1, %1
    psubw   %1, %2
    pmaxsw  %1, %2
%endmacro

%macro PABSW_SSSE3 2
    pabsw      %1, %2
%endmacro

%macro PSIGNW_SSSE3 2
    psignw     %1, %2
%endmacro

%macro ABS1_MMX 2    ; a, tmp
    pxor       %2, %2
    pcmpgtw    %2, %1
    pxor       %1, %2
    psubw      %1, %2
%endmacro

%macro ABS2_MMX 4    ; a, b, tmp0, tmp1
    pxor       %3, %3
    pxor       %4, %4
    pcmpgtw    %3, %1
    pcmpgtw    %4, %2
    pxor       %1, %3
    pxor       %2, %4
    psubw      %1, %3
    psubw      %2, %4
%endmacro

%macro ABS1_MMX2 2   ; a, tmp
    pxor    %2, %2
    psubw   %2, %1
    pmaxsw  %1, %2
%endmacro

%macro ABS2_MMX2 4   ; a, b, tmp0, tmp1
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

%macro ABSB_MMX 2
    pxor    %2, %2
    psubb   %2, %1
    pminub  %1, %2
%endmacro

%macro ABSB2_MMX 4
    pxor    %3, %3
    pxor    %4, %4
    psubb   %3, %1
    psubb   %4, %2
    pminub  %1, %3
    pminub  %2, %4
%endmacro

%macro ABSB_SSSE3 2
    pabsb   %1, %1
%endmacro

%macro ABSB2_SSSE3 4
    pabsb   %1, %1
    pabsb   %2, %2
%endmacro

%macro ABS4 6
    ABS2 %1, %2, %5, %6
    ABS2 %3, %4, %5, %6
%endmacro

%define ABS1 ABS1_MMX
%define ABS2 ABS2_MMX
%define ABSB ABSB_MMX
%define ABSB2 ABSB2_MMX

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

%macro DEINTB 5 ; mask, reg1, mask, reg2, optional src to fill masks from
%ifnum %5
    mova   m%1, m%5
    mova   m%3, m%5
%else
    mova   m%1, %5
    mova   m%3, m%1
%endif
    pand   m%1, m%2 ; dst .. y6 .. y4
    pand   m%3, m%4 ; src .. y6 .. y4
    psrlw  m%2, 8   ; dst .. y7 .. y5
    psrlw  m%4, 8   ; src .. y7 .. y5
%endmacro

%macro SUMSUB_BA 2-3
%if %0==2
    paddw   %1, %2
    paddw   %2, %2
    psubw   %2, %1
%else
    mova    %3, %1
    paddw   %1, %2
    psubw   %2, %3
%endif
%endmacro

%macro SUMSUB_BADC 4-5
%if %0==5
    SUMSUB_BA %1, %2, %5
    SUMSUB_BA %3, %4, %5
%else
    paddw   %1, %2
    paddw   %3, %4
    paddw   %2, %2
    paddw   %4, %4
    psubw   %2, %1
    psubw   %4, %3
%endif
%endmacro

%macro SUMSUB2_AB 3
    mova    %3, %1
    paddw   %1, %1
    paddw   %1, %2
    psubw   %3, %2
    psubw   %3, %2
%endmacro

%macro SUMSUB2_BA 3
    mova    m%3, m%1
    paddw   m%1, m%2
    paddw   m%1, m%2
    psubw   m%2, m%3
    psubw   m%2, m%3
%endmacro

%macro SUMSUBD2_AB 4
    mova    %4, %1
    mova    %3, %2
    psraw   %2, 1  ; %2: %2>>1
    psraw   %1, 1  ; %1: %1>>1
    paddw   %2, %4 ; %2: %2>>1+%1
    psubw   %1, %3 ; %1: %1>>1-%2
%endmacro

%macro DCT4_1D 5
%ifnum %5
    SUMSUB_BADC m%4, m%1, m%3, m%2; m%5
    SUMSUB_BA   m%3, m%4, m%5
    SUMSUB2_AB  m%1, m%2, m%5
    SWAP %1, %3, %4, %5, %2
%else
    SUMSUB_BADC m%4, m%1, m%3, m%2
    SUMSUB_BA   m%3, m%4
    mova       [%5], m%2
    SUMSUB2_AB m%1, [%5], m%2
    SWAP %1, %3, %4, %2
%endif
%endmacro

%macro IDCT4_1D 5-6
%ifnum %5
    SUMSUBD2_AB m%2, m%4, m%6, m%5
    ; %2: %2>>1-%4 %4: %2+%4>>1
    SUMSUB_BA   m%3, m%1, m%6
    ; %3: %1+%3 %1: %1-%3
    SUMSUB_BADC m%4, m%3, m%2, m%1, m%6
    ; %4: %1+%3 + (%2+%4>>1)
    ; %3: %1+%3 - (%2+%4>>1)
    ; %2: %1-%3 + (%2>>1-%4)
    ; %1: %1-%3 - (%2>>1-%4)
%else
    SUMSUBD2_AB m%2, m%4, [%5], [%5+16]
    SUMSUB_BA   m%3, m%1
    SUMSUB_BADC m%4, m%3, m%2, m%1
%endif
    SWAP %1, %4, %3
    ; %1: %1+%3 + (%2+%4>>1) row0
    ; %2: %1-%3 + (%2>>1-%4) row1
    ; %3: %1-%3 - (%2>>1-%4) row2
    ; %4: %1+%3 - (%2+%4>>1) row3
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

%macro STORE_DCT 6
    movq   [%5+%6+ 0], m%1
    movq   [%5+%6+ 8], m%2
    movq   [%5+%6+16], m%3
    movq   [%5+%6+24], m%4
    movhps [%5+%6+32], m%1
    movhps [%5+%6+40], m%2
    movhps [%5+%6+48], m%3
    movhps [%5+%6+56], m%4
%endmacro

%macro LOAD_DIFF_8x4P 7-10 r0,r2,0 ; 4x dest, 2x temp, 2x pointer, increment?
    LOAD_DIFF m%1, m%5, m%7, [%8],      [%9]
    LOAD_DIFF m%2, m%6, m%7, [%8+r1],   [%9+r3]
    LOAD_DIFF m%3, m%5, m%7, [%8+2*r1], [%9+2*r3]
    LOAD_DIFF m%4, m%6, m%7, [%8+r4],   [%9+r5]
%if %10
    lea %8, [%8+4*r1]
    lea %9, [%9+4*r3]
%endif
%endmacro

%macro DIFFx2 6-7
    movh       %3, %5
    punpcklbw  %3, %4
    psraw      %1, 6
    paddsw     %1, %3
    movh       %3, %6
    punpcklbw  %3, %4
    psraw      %2, 6
    paddsw     %2, %3
    packuswb   %2, %1
%endmacro

%macro STORE_DIFF 4
    movh       %2, %4
    punpcklbw  %2, %3
    psraw      %1, 6
    paddsw     %1, %2
    packuswb   %1, %1
    movh       %4, %1
%endmacro

%macro STORE_DIFFx2 8 ; add1, add2, reg1, reg2, zero, shift, source, stride
    movh       %3, [%7]
    movh       %4, [%7+%8]
    punpcklbw  %3, %5
    punpcklbw  %4, %5
    psraw      %1, %6
    psraw      %2, %6
    paddw      %3, %1
    paddw      %4, %2
    packuswb   %3, %5
    packuswb   %4, %5
    movh     [%7], %3
    movh  [%7+%8], %4
%endmacro
