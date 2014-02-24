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
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%define private_prefix ff
%define public_prefix  avpriv
%define cpuflags_mmxext cpuflags_mmx2

%include "libavutil/x86/x86inc.asm"

%macro SBUTTERFLY 4
%if avx_enabled == 0
    mova      m%4, m%2
    punpckl%1 m%2, m%3
    punpckh%1 m%4, m%3
%else
    punpckh%1 m%4, m%2, m%3
    punpckl%1 m%2, m%3
%endif
    SWAP %3, %4
%endmacro

%macro SBUTTERFLY2 4
    punpckl%1 m%4, m%2, m%3
    punpckh%1 m%2, m%2, m%3
    SWAP %2, %4, %3
%endmacro

%macro SBUTTERFLYPS 3
    unpcklps m%3, m%1, m%2
    unpckhps m%1, m%1, m%2
    SWAP %1, %3, %2
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

; identical behavior to TRANSPOSE4x4D, but using SSE1 float ops
%macro TRANSPOSE4x4PS 5
    SBUTTERFLYPS %1, %2, %5
    SBUTTERFLYPS %3, %4, %5
    movlhps m%5, m%1, m%3
    movhlps m%3, m%1
    SWAP %5, %1
    movlhps m%5, m%2, m%4
    movhlps m%4, m%2
    SWAP %5, %2, %3
%endmacro

%macro TRANSPOSE8x8W 9-11
%if ARCH_X86_64
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

; PABSW macro assumes %1 != %2, while ABS1/2 macros work in-place
%macro PABSW 2
%if cpuflag(ssse3)
    pabsw      %1, %2
%elif cpuflag(mmxext)
    pxor    %1, %1
    psubw   %1, %2
    pmaxsw  %1, %2
%else
    pxor       %1, %1
    pcmpgtw    %1, %2
    pxor       %2, %1
    psubw      %2, %1
    SWAP       %1, %2
%endif
%endmacro

%macro PSIGNW_MMX 2
    pxor       %1, %2
    psubw      %1, %2
%endmacro

%macro PSIGNW_SSSE3 2
    psignw     %1, %2
%endmacro

%macro ABS1 2
%if cpuflag(ssse3)
    pabsw   %1, %1
%elif cpuflag(mmxext) ; a, tmp
    pxor    %2, %2
    psubw   %2, %1
    pmaxsw  %1, %2
%else ; a, tmp
    pxor       %2, %2
    pcmpgtw    %2, %1
    pxor       %1, %2
    psubw      %1, %2
%endif
%endmacro

%macro ABS2 4
%if cpuflag(ssse3)
    pabsw   %1, %1
    pabsw   %2, %2
%elif cpuflag(mmxext) ; a, b, tmp0, tmp1
    pxor    %3, %3
    pxor    %4, %4
    psubw   %3, %1
    psubw   %4, %2
    pmaxsw  %1, %3
    pmaxsw  %2, %4
%else ; a, b, tmp0, tmp1
    pxor       %3, %3
    pxor       %4, %4
    pcmpgtw    %3, %1
    pcmpgtw    %4, %2
    pxor       %1, %3
    pxor       %2, %4
    psubw      %1, %3
    psubw      %2, %4
%endif
%endmacro

%macro ABSB 2 ; source mmreg, temp mmreg (unused for ssse3)
%if cpuflag(ssse3)
    pabsb   %1, %1
%else
    pxor    %2, %2
    psubb   %2, %1
    pminub  %1, %2
%endif
%endmacro

%macro ABSB2 4 ; src1, src2, tmp1, tmp2 (tmp1/2 unused for SSSE3)
%if cpuflag(ssse3)
    pabsb   %1, %1
    pabsb   %2, %2
%else
    pxor    %3, %3
    pxor    %4, %4
    psubb   %3, %1
    psubb   %4, %2
    pminub  %1, %3
    pminub  %2, %4
%endif
%endmacro

%macro ABSD2_MMX 4
    pxor    %3, %3
    pxor    %4, %4
    pcmpgtd %3, %1
    pcmpgtd %4, %2
    pxor    %1, %3
    pxor    %2, %4
    psubd   %1, %3
    psubd   %2, %4
%endmacro

%macro ABS4 6
    ABS2 %1, %2, %5, %6
    ABS2 %3, %4, %5, %6
%endmacro

%macro SPLATB_LOAD 3
%if cpuflag(ssse3)
    movd      %1, [%2-3]
    pshufb    %1, %3
%else
    movd      %1, [%2-3] ;to avoid crossing a cacheline
    punpcklbw %1, %1
    SPLATW    %1, %1, 3
%endif
%endmacro

%macro SPLATB_REG 3
%if cpuflag(ssse3)
    movd      %1, %2d
    pshufb    %1, %3
%else
    movd      %1, %2d
    punpcklbw %1, %1
    SPLATW    %1, %1, 0
%endif
%endmacro

%macro PALIGNR 4-5
%if cpuflag(ssse3)
%if %0==5
    palignr %1, %2, %3, %4
%else
    palignr %1, %2, %3
%endif
%elif cpuflag(mmx) ; [dst,] src1, src2, imm, tmp
    %define %%dst %1
%if %0==5
%ifnidn %1, %2
    mova    %%dst, %2
%endif
    %rotate 1
%endif
%ifnidn %4, %2
    mova    %4, %2
%endif
%if mmsize==8
    psllq   %%dst, (8-%3)*8
    psrlq   %4, %3*8
%else
    pslldq  %%dst, 16-%3
    psrldq  %4, %3
%endif
    por     %%dst, %4
%endif
%endmacro

%macro PAVGB 2
%if cpuflag(mmxext)
    pavgb   %1, %2
%elif cpuflag(3dnow)
    pavgusb %1, %2
%endif
%endmacro

%macro PSHUFLW 1+
    %if mmsize == 8
        pshufw %1
    %else
        pshuflw %1
    %endif
%endmacro

%macro PSWAPD 2
%if cpuflag(mmxext)
    pshufw    %1, %2, q1032
%elif cpuflag(3dnowext)
    pswapd    %1, %2
%elif cpuflag(3dnow)
    movq      %1, %2
    psrlq     %1, 32
    punpckldq %1, %2
%endif
%endmacro

%macro DEINTB 5 ; mask, reg1, mask, reg2, optional src to fill masks from
%ifnum %5
    pand   m%3, m%5, m%4 ; src .. y6 .. y4
    pand   m%1, m%5, m%2 ; dst .. y6 .. y4
%else
    mova   m%1, %5
    pand   m%3, m%1, m%4 ; src .. y6 .. y4
    pand   m%1, m%1, m%2 ; dst .. y6 .. y4
%endif
    psrlw  m%2, 8        ; dst .. y7 .. y5
    psrlw  m%4, 8        ; src .. y7 .. y5
%endmacro

%macro SUMSUB_BA 3-4
%if %0==3
    padd%1  m%2, m%3
    padd%1  m%3, m%3
    psub%1  m%3, m%2
%else
%if avx_enabled == 0
    mova    m%4, m%2
    padd%1  m%2, m%3
    psub%1  m%3, m%4
%else
    padd%1  m%4, m%2, m%3
    psub%1  m%3, m%2
    SWAP    %2, %4
%endif
%endif
%endmacro

%macro SUMSUB_BADC 5-6
%if %0==6
    SUMSUB_BA %1, %2, %3, %6
    SUMSUB_BA %1, %4, %5, %6
%else
    padd%1  m%2, m%3
    padd%1  m%4, m%5
    padd%1  m%3, m%3
    padd%1  m%5, m%5
    psub%1  m%3, m%2
    psub%1  m%5, m%4
%endif
%endmacro

%macro SUMSUB2_AB 4
%ifnum %3
    psub%1  m%4, m%2, m%3
    psub%1  m%4, m%3
    padd%1  m%2, m%2
    padd%1  m%2, m%3
%else
    mova    m%4, m%2
    padd%1  m%2, m%2
    padd%1  m%2, %3
    psub%1  m%4, %3
    psub%1  m%4, %3
%endif
%endmacro

%macro SUMSUB2_BA 4
%if avx_enabled == 0
    mova    m%4, m%2
    padd%1  m%2, m%3
    padd%1  m%2, m%3
    psub%1  m%3, m%4
    psub%1  m%3, m%4
%else
    padd%1  m%4, m%2, m%3
    padd%1  m%4, m%3
    psub%1  m%3, m%2
    psub%1  m%3, m%2
    SWAP     %2,  %4
%endif
%endmacro

%macro SUMSUBD2_AB 5
%ifnum %4
    psra%1  m%5, m%2, 1  ; %3: %3>>1
    psra%1  m%4, m%3, 1  ; %2: %2>>1
    padd%1  m%4, m%2     ; %3: %3>>1+%2
    psub%1  m%5, m%3     ; %2: %2>>1-%3
    SWAP     %2, %5
    SWAP     %3, %4
%else
    mova    %5, m%2
    mova    %4, m%3
    psra%1  m%3, 1  ; %3: %3>>1
    psra%1  m%2, 1  ; %2: %2>>1
    padd%1  m%3, %5 ; %3: %3>>1+%2
    psub%1  m%2, %4 ; %2: %2>>1-%3
%endif
%endmacro

%macro DCT4_1D 5
%ifnum %5
    SUMSUB_BADC w, %4, %1, %3, %2, %5
    SUMSUB_BA   w, %3, %4, %5
    SUMSUB2_AB  w, %1, %2, %5
    SWAP %1, %3, %4, %5, %2
%else
    SUMSUB_BADC w, %4, %1, %3, %2
    SUMSUB_BA   w, %3, %4
    mova     [%5], m%2
    SUMSUB2_AB  w, %1, [%5], %2
    SWAP %1, %3, %4, %2
%endif
%endmacro

%macro IDCT4_1D 6-7
%ifnum %6
    SUMSUBD2_AB %1, %3, %5, %7, %6
    ; %3: %3>>1-%5 %5: %3+%5>>1
    SUMSUB_BA   %1, %4, %2, %7
    ; %4: %2+%4 %2: %2-%4
    SUMSUB_BADC %1, %5, %4, %3, %2, %7
    ; %5: %2+%4 + (%3+%5>>1)
    ; %4: %2+%4 - (%3+%5>>1)
    ; %3: %2-%4 + (%3>>1-%5)
    ; %2: %2-%4 - (%3>>1-%5)
%else
%ifidn %1, w
    SUMSUBD2_AB %1, %3, %5, [%6], [%6+16]
%else
    SUMSUBD2_AB %1, %3, %5, [%6], [%6+32]
%endif
    SUMSUB_BA   %1, %4, %2
    SUMSUB_BADC %1, %5, %4, %3, %2
%endif
    SWAP %2, %5, %4
    ; %2: %2+%4 + (%3+%5>>1) row0
    ; %3: %2-%4 + (%3>>1-%5) row1
    ; %4: %2-%4 - (%3>>1-%5) row2
    ; %5: %2+%4 - (%3+%5>>1) row3
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
    psraw      %1, %6
    psraw      %2, %6
    punpcklbw  %3, %5
    punpcklbw  %4, %5
    paddw      %3, %1
    paddw      %4, %2
    packuswb   %3, %5
    packuswb   %4, %5
    movh     [%7], %3
    movh  [%7+%8], %4
%endmacro

%macro PMINUB 3 ; dst, src, ignored
%if cpuflag(mmxext)
    pminub   %1, %2
%else ; dst, src, tmp
    mova     %3, %1
    psubusb  %3, %2
    psubb    %1, %3
%endif
%endmacro

%macro SPLATW 2-3 0
%if mmsize == 16
    pshuflw    %1, %2, (%3)*0x55
    punpcklqdq %1, %1
%elif cpuflag(mmxext)
    pshufw     %1, %2, (%3)*0x55
%else
    %ifnidn %1, %2
        mova       %1, %2
    %endif
    %if %3 & 2
        punpckhwd  %1, %1
    %else
        punpcklwd  %1, %1
    %endif
    %if %3 & 1
        punpckhwd  %1, %1
    %else
        punpcklwd  %1, %1
    %endif
%endif
%endmacro

%macro SPLATD 1
%if mmsize == 8
    punpckldq  %1, %1
%elif cpuflag(sse2)
    pshufd  %1, %1, 0
%elif cpuflag(sse)
    shufps  %1, %1, 0
%endif
%endmacro

%macro CLIPW 3 ;(dst, min, max)
    pmaxsw %1, %2
    pminsw %1, %3
%endmacro

%macro PMINSD_MMX 3 ; dst, src, tmp
    mova      %3, %2
    pcmpgtd   %3, %1
    pxor      %1, %2
    pand      %1, %3
    pxor      %1, %2
%endmacro

%macro PMAXSD_MMX 3 ; dst, src, tmp
    mova      %3, %1
    pcmpgtd   %3, %2
    pand      %1, %3
    pandn     %3, %2
    por       %1, %3
%endmacro

%macro CLIPD_MMX 3-4 ; src/dst, min, max, tmp
    PMINSD_MMX %1, %3, %4
    PMAXSD_MMX %1, %2, %4
%endmacro

%macro CLIPD_SSE2 3-4 ; src/dst, min (float), max (float), unused
    cvtdq2ps  %1, %1
    minps     %1, %3
    maxps     %1, %2
    cvtps2dq  %1, %1
%endmacro

%macro CLIPD_SSE41 3-4 ;  src/dst, min, max, unused
    pminsd  %1, %3
    pmaxsd  %1, %2
%endmacro

%macro VBROADCASTSS 2 ; dst xmm/ymm, src m32
%if cpuflag(avx)
    vbroadcastss %1, %2
%else ; sse
    movss        %1, %2
    shufps       %1, %1, 0
%endif
%endmacro

%macro VBROADCASTSD 2 ; dst xmm/ymm, src m64
%if cpuflag(avx) && mmsize == 32
    vbroadcastsd %1, %2
%elif cpuflag(sse3)
    movddup      %1, %2
%else ; sse2
    movsd        %1, %2
    movlhps      %1, %1
%endif
%endmacro

%macro SHUFFLE_MASK_W 8
    %rep 8
        %if %1>=0x80
            db %1, %1
        %else
            db %1*2
            db %1*2+1
        %endif
        %rotate 1
    %endrep
%endmacro

%macro PMOVSXWD 2; dst, src
%if cpuflag(sse4)
    pmovsxwd     %1, %2
%else
    %ifnidn %1, %2
    mova         %1, %2
    %endif
    punpcklwd    %1, %1
    psrad        %1, 16
%endif
%endmacro

%macro PMA_EMU 4
    %macro %1 5-8 %2, %3, %4
        %if cpuflag(xop)
            v%6 %1, %2, %3, %4
        %elifidn %1, %4
            %7 %5, %2, %3
            %8 %1, %4, %5
        %else
            %7 %1, %2, %3
            %8 %1, %4
        %endif
    %endmacro
%endmacro

PMA_EMU  PMACSWW,  pmacsww,  pmullw, paddw
PMA_EMU  PMACSDD,  pmacsdd,  pmulld, paddd ; sse4 emulation
PMA_EMU PMACSDQL, pmacsdql,  pmuldq, paddq ; sse4 emulation
PMA_EMU PMADCSWD, pmadcswd, pmaddwd, paddd

; Wrapper for non-FMA version of fmaddps
%macro FMULADD_PS 5
    %if cpuflag(fma3) || cpuflag(fma4)
        fmaddps %1, %2, %3, %4
    %elifidn %1, %4
        mulps   %5, %2, %3
        addps   %1, %4, %5
    %else
        mulps   %1, %2, %3
        addps   %1, %4
    %endif
%endmacro
