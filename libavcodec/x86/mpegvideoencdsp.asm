;*****************************************************************************
;* SIMD-optimized MPEG encoding functions
;*****************************************************************************
;* Copyright (c) 2000, 2001 Fabrice Bellard
;* Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

cextern pw_1

SECTION .text
; int ff_pix_sum16_mmx(uint8_t *pix, int line_size)
; %1 = number of xmm registers used
; %2 = number of loops
; %3 = number of GPRs used
%macro PIX_SUM16 4
cglobal pix_sum16, 2, %3, %1
    movsxdifnidn r1, r1d
    mov          r2, %2
%if cpuflag(xop)
    lea          r3, [r1*3]
%else
    pxor         m5, m5
%endif
    pxor         m4, m4
.loop:
%if cpuflag(xop)
    vphaddubq    m0, [r0]
    vphaddubq    m1, [r0+r1]
    vphaddubq    m2, [r0+r1*2]
    vphaddubq    m3, [r0+r3]
%else
    mova         m0, [r0]
%if mmsize == 8
    mova         m1, [r0+8]
%else
    mova         m1, [r0+r1]
%endif
    punpckhbw    m2, m0, m5
    punpcklbw    m0, m5
    punpckhbw    m3, m1, m5
    punpcklbw    m1, m5
%endif ; cpuflag(xop)
    paddw        m1, m0
    paddw        m3, m2
    paddw        m3, m1
    paddw        m4, m3
%if mmsize == 8
    add          r0, r1
%else
    lea          r0, [r0+r1*%4]
%endif
    dec r2
    jne .loop
%if cpuflag(xop)
    pshufd       m0, m4, q0032
    paddd        m4, m0
%else
    HADDW        m4, m5
%endif
    movd        eax, m4
    RET
%endmacro

INIT_MMX mmx
PIX_SUM16 0, 16, 3, 0
INIT_XMM sse2
PIX_SUM16 6, 8,  3, 2
%if HAVE_XOP_EXTERNAL
INIT_XMM xop
PIX_SUM16 5, 4,  4, 4
%endif

; int ff_pix_norm1_mmx(uint8_t *pix, int line_size)
; %1 = number of xmm registers used
; %2 = number of loops
%macro PIX_NORM1 2
cglobal pix_norm1, 2, 3, %1
    movsxdifnidn r1, r1d
    mov          r2, %2
    pxor         m0, m0
    pxor         m5, m5
.loop:
    mova         m2, [r0+0]
%if mmsize == 8
    mova         m3, [r0+8]
%else
    mova         m3, [r0+r1]
%endif
    punpckhbw    m1, m2, m0
    punpcklbw    m2, m0
    punpckhbw    m4, m3, m0
    punpcklbw    m3, m0
    pmaddwd      m1, m1
    pmaddwd      m2, m2
    pmaddwd      m3, m3
    pmaddwd      m4, m4
    paddd        m2, m1
    paddd        m4, m3
    paddd        m5, m2
    paddd        m5, m4
%if mmsize == 8
    add          r0, r1
%else
    lea          r0, [r0+r1*2]
%endif
    dec r2
    jne .loop
    HADDD        m5, m1
    movd        eax, m5
    RET
%endmacro

INIT_MMX mmx
PIX_NORM1 0, 16
INIT_XMM sse2
PIX_NORM1 6, 8

