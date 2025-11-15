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

SECTION .text

; void ff_add_8x8basis_ssse3(int16_t rem[64], const int16_t basis[64], int scale)
INIT_XMM ssse3
cglobal add_8x8basis, 3, 3+ARCH_X86_64, 4, rem, basis, scale
    movd            m0, scaled
    add         scaled, 1024
    add         basisq, 128
    add           remq, 128
%if ARCH_X86_64
%define OFF r3q
    mov            r3q, -128
    cmp         scaled, 2047
%else
%define OFF r2q
    cmp         scaled, 2047
    mov            r2q, -128
%endif
    ja     .huge_scale

    punpcklwd       m0, m0
    pshufd          m0, m0, 0x0
    psllw           m0, 5
.loop1:
    mova            m1, [basisq+OFF]
    mova            m2, [basisq+OFF+16]
    pmulhrsw        m1, m0
    pmulhrsw        m2, m0
    paddw           m1, [remq+OFF]
    paddw           m2, [remq+OFF+16]
    mova    [remq+OFF], m1
    mova [remq+OFF+16], m2
    add            OFF, 32
    js          .loop1
    RET

.huge_scale:
    pslld           m0, 6
    punpcklwd       m0, m0
    pshufd          m1, m0, 0x55
    psrlw           m0, 1
    pshufd          m0, m0, 0x0
.loop2:
    mova            m2, [basisq+OFF]
    pmulhrsw        m3, m2, m0
    pmullw          m2, m1
    paddw           m2, m3
    paddw           m2, [remq+OFF]
    mova    [remq+OFF], m2
    add            OFF, 16
    js          .loop2
    RET


INIT_XMM sse2
cglobal mpv_denoise_dct, 3, 4, 7, block, sum, offset
    pxor            m6, m6
    lea             r3, [sumq+256]
.loop:
    mova            m2, [blockq]
    mova            m3, [blockq+16]
    mova            m0, m6
    mova            m1, m6
    pcmpgtw         m0, m2
    pcmpgtw         m1, m3
    pxor            m2, m0
    pxor            m3, m1
    psubw           m2, m0
    psubw           m3, m1
    psubusw         m4, m2, [offsetq]
    psubusw         m5, m3, [offsetq+16]
    pxor            m4, m0
    pxor            m5, m1
    add        offsetq, 32
    psubw           m4, m0
    psubw           m5, m1
    mova      [blockq], m4
    mova   [blockq+16], m5
    mova            m0, m2
    mova            m1, m3
    add         blockq, 32
    punpcklwd       m0, m6
    punpckhwd       m2, m6
    punpcklwd       m1, m6
    punpckhwd       m3, m6
    paddd           m0, [sumq]
    paddd           m2, [sumq+16]
    paddd           m1, [sumq+32]
    paddd           m3, [sumq+48]
    mova        [sumq], m0
    mova     [sumq+16], m2
    mova     [sumq+32], m1
    mova     [sumq+48], m3
    add           sumq, 64
    cmp           sumq, r3
    jb           .loop
    RET


; int ff_pix_sum16(const uint8_t *pix, ptrdiff_t line_size)
; %1 = number of loops
; %2 = number of GPRs used
%macro PIX_SUM16 3
cglobal pix_sum16, 2, %2, 6
    mov          r2, %1
    lea          r3, [r1*3]
%if notcpuflag(xop)
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
    mova         m1, [r0+r1]
    mova         m2, [r0+r1*2]
    mova         m3, [r0+r3]
    psadbw       m0, m5
    psadbw       m1, m5
    psadbw       m2, m5
    psadbw       m3, m5
%endif ; cpuflag(xop)
    paddw        m1, m0
    paddw        m3, m2
    paddw        m3, m1
    paddw        m4, m3
    lea          r0, [r0+r1*%3]
    dec r2
    jne .loop
    pshufd       m0, m4, q0032
    paddd        m4, m0
    movd        eax, m4
    RET
%endmacro

INIT_XMM sse2
PIX_SUM16  4, 4, 4
%if HAVE_XOP_EXTERNAL
INIT_XMM xop
PIX_SUM16  4, 4, 4
%endif

; int ff_pix_norm1(const uint8_t *pix, ptrdiff_t line_size)
; %1 = number of xmm registers used
; %2 = number of loops
%macro PIX_NORM1 2
cglobal pix_norm1, 2, 3, %1
    mov          r2, %2
    pxor         m0, m0
    pxor         m5, m5
.loop:
    mova         m2, [r0+0]
    mova         m3, [r0+r1]
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
    lea          r0, [r0+r1*2]
    dec r2
    jne .loop
    HADDD        m5, m1
    movd        eax, m5
    RET
%endmacro

INIT_XMM sse2
PIX_NORM1 6, 8
