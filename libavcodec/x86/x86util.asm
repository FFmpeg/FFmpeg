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
%if %0<11
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

%macro HADAMARD4_V 4+
    SUMSUB_BADC %1, %2, %3, %4
    SUMSUB_BADC %1, %3, %2, %4
%endmacro

%macro HADAMARD8_V 8+
    SUMSUB_BADC %1, %2, %3, %4
    SUMSUB_BADC %5, %6, %7, %8
    SUMSUB_BADC %1, %3, %2, %4
    SUMSUB_BADC %5, %7, %6, %8
    SUMSUB_BADC %1, %5, %2, %6
    SUMSUB_BADC %3, %7, %4, %8
%endmacro

%macro TRANS_SSE2 5-6
; TRANSPOSE2x2
; %1: transpose width (d/q) - use SBUTTERFLY qdq for dq
; %2: ord/unord (for compat with sse4, unused)
; %3/%4: source regs
; %5/%6: tmp regs
%ifidn %1, d
%define mask [mask_10 GLOBAL]
%define shift 16
%elifidn %1, q
%define mask [mask_1100 GLOBAL]
%define shift 32
%endif
%if %0==6 ; less dependency if we have two tmp
    mova   m%5, mask   ; ff00
    mova   m%6, m%4    ; x5x4
    psll%1 m%4, shift  ; x4..
    pand   m%6, m%5    ; x5..
    pandn  m%5, m%3    ; ..x0
    psrl%1 m%3, shift  ; ..x1
    por    m%4, m%5    ; x4x0
    por    m%3, m%6    ; x5x1
%else ; more dependency, one insn less. sometimes faster, sometimes not
    mova   m%5, m%4    ; x5x4
    psll%1 m%4, shift  ; x4..
    pxor   m%4, m%3    ; (x4^x1)x0
    pand   m%4, mask   ; (x4^x1)..
    pxor   m%3, m%4    ; x4x0
    psrl%1 m%4, shift  ; ..(x1^x4)
    pxor   m%5, m%4    ; x5x1
    SWAP   %4, %3, %5
%endif
%endmacro

%macro TRANS_SSE4 5-6 ; see above
%ifidn %1, d
    mova   m%5, m%3
%ifidn %2, ord
    psrl%1 m%3, 16
%endif
    pblendw m%3, m%4, 10101010b
    psll%1 m%4, 16
%ifidn %2, ord
    pblendw m%4, m%5, 01010101b
%else
    psrl%1 m%5, 16
    por    m%4, m%5
%endif
%elifidn %1, q
    mova   m%5, m%3
    shufps m%3, m%4, 10001000b
    shufps m%5, m%4, 11011101b
    SWAP   %4, %5
%endif
%endmacro

%macro HADAMARD 5-6
; %1=distance in words (0 for vertical pass, 1/2/4 for horizontal passes)
; %2=sumsub/max/amax (sum and diff / maximum / maximum of absolutes)
; %3/%4: regs
; %5(%6): tmpregs
%if %1!=0 ; have to reorder stuff for horizontal op
    %ifidn %2, sumsub
         %define ORDER ord
         ; sumsub needs order because a-b != b-a unless a=b
    %else
         %define ORDER unord
         ; if we just max, order doesn't matter (allows pblendw+or in sse4)
    %endif
    %if %1==1
         TRANS d, ORDER, %3, %4, %5, %6
    %elif %1==2
         %if mmsize==8
             SBUTTERFLY dq, %3, %4, %5
         %else
             TRANS q, ORDER, %3, %4, %5, %6
         %endif
    %elif %1==4
         SBUTTERFLY qdq, %3, %4, %5
    %endif
%endif
%ifidn %2, sumsub
    SUMSUB_BA m%3, m%4, m%5
%else
    %ifidn %2, amax
        %if %0==6
            ABS2 m%3, m%4, m%5, m%6
        %else
            ABS1 m%3, m%5
            ABS1 m%4, m%5
        %endif
    %endif
    pmaxsw m%3, m%4
%endif
%endmacro


%macro HADAMARD2_2D 6-7 sumsub
    HADAMARD 0, sumsub, %1, %2, %5
    HADAMARD 0, sumsub, %3, %4, %5
    SBUTTERFLY %6, %1, %2, %5
%ifnum %7
    HADAMARD 0, amax, %1, %2, %5, %7
%else
    HADAMARD 0, %7, %1, %2, %5
%endif
    SBUTTERFLY %6, %3, %4, %5
%ifnum %7
    HADAMARD 0, amax, %3, %4, %5, %7
%else
    HADAMARD 0, %7, %3, %4, %5
%endif
%endmacro

%macro HADAMARD4_2D 5-6 sumsub
    HADAMARD2_2D %1, %2, %3, %4, %5, wd
    HADAMARD2_2D %1, %3, %2, %4, %5, dq, %6
    SWAP %2, %3
%endmacro

%macro HADAMARD4_2D_SSE 5-6 sumsub
    HADAMARD  0, sumsub, %1, %2, %5 ; 1st V row 0 + 1
    HADAMARD  0, sumsub, %3, %4, %5 ; 1st V row 2 + 3
    SBUTTERFLY   wd, %1, %2, %5     ; %1: m0 1+0 %2: m1 1+0
    SBUTTERFLY   wd, %3, %4, %5     ; %3: m0 3+2 %4: m1 3+2
    HADAMARD2_2D %1, %3, %2, %4, %5, dq
    SBUTTERFLY  qdq, %1, %2, %5
    HADAMARD  0, %6, %1, %2, %5     ; 2nd H m1/m0 row 0+1
    SBUTTERFLY  qdq, %3, %4, %5
    HADAMARD  0, %6, %3, %4, %5     ; 2nd H m1/m0 row 2+3
%endmacro

%macro HADAMARD8_2D 9-10 sumsub
    HADAMARD2_2D %1, %2, %3, %4, %9, wd
    HADAMARD2_2D %5, %6, %7, %8, %9, wd
    HADAMARD2_2D %1, %3, %2, %4, %9, dq
    HADAMARD2_2D %5, %7, %6, %8, %9, dq
    HADAMARD2_2D %1, %5, %3, %7, %9, qdq, %10
    HADAMARD2_2D %2, %6, %4, %8, %9, qdq, %10
%ifnidn %10, amax
    SWAP %2, %5
    SWAP %4, %7
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
    psraw   %2, 1
    psraw   %1, 1
    paddw   %2, %4
    psubw   %1, %3
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
    SUMSUB_BA   m%3, m%1, m%6
    SUMSUB_BADC m%4, m%3, m%2, m%1, m%6
%else
    SUMSUBD2_AB m%2, m%4, [%5], [%5+16]
    SUMSUB_BA   m%3, m%1
    SUMSUB_BADC m%4, m%3, m%2, m%1
%endif
    SWAP %1, %4, %3
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

%macro LOAD_DIFF8x4_SSE2 8
    LOAD_DIFF  m%1, m%5, m%6, [%7+%1*FENC_STRIDE], [%8+%1*FDEC_STRIDE]
    LOAD_DIFF  m%2, m%5, m%6, [%7+%2*FENC_STRIDE], [%8+%2*FDEC_STRIDE]
    LOAD_DIFF  m%3, m%5, m%6, [%7+%3*FENC_STRIDE], [%8+%3*FDEC_STRIDE]
    LOAD_DIFF  m%4, m%5, m%6, [%7+%4*FENC_STRIDE], [%8+%4*FDEC_STRIDE]
%endmacro

%macro LOAD_DIFF8x4_SSSE3 8 ; 4x dst, 1x tmp, 1x mul, 2x ptr
    movh       m%2, [%8+%1*FDEC_STRIDE]
    movh       m%1, [%7+%1*FENC_STRIDE]
    punpcklbw  m%1, m%2
    movh       m%3, [%8+%2*FDEC_STRIDE]
    movh       m%2, [%7+%2*FENC_STRIDE]
    punpcklbw  m%2, m%3
    movh       m%4, [%8+%3*FDEC_STRIDE]
    movh       m%3, [%7+%3*FENC_STRIDE]
    punpcklbw  m%3, m%4
    movh       m%5, [%8+%4*FDEC_STRIDE]
    movh       m%4, [%7+%4*FENC_STRIDE]
    punpcklbw  m%4, m%5
    pmaddubsw  m%1, m%6
    pmaddubsw  m%2, m%6
    pmaddubsw  m%3, m%6
    pmaddubsw  m%4, m%6
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

%macro STORE_IDCT 4
    movhps [r0-4*FDEC_STRIDE], %1
    movh   [r0-3*FDEC_STRIDE], %1
    movhps [r0-2*FDEC_STRIDE], %2
    movh   [r0-1*FDEC_STRIDE], %2
    movhps [r0+0*FDEC_STRIDE], %3
    movh   [r0+1*FDEC_STRIDE], %3
    movhps [r0+2*FDEC_STRIDE], %4
    movh   [r0+3*FDEC_STRIDE], %4
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

