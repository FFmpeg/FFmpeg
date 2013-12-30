;*****************************************************************************
;* SIMD-optimized MPEG encoding functions
;*****************************************************************************
;* Copyright (c) 2000, 2001 Fabrice Bellard
;* Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

INIT_MMX mmx
; int ff_pix_sum16_mmx(uint8_t *pix, int line_size)
cglobal pix_sum16, 2, 3
    movsxdifnidn r1, r1d
    mov          r2, r1
    neg          r2
    shl          r2, 4
    sub          r0, r2
    pxor         m7, m7
    pxor         m6, m6
.loop:
    mova         m0, [r0+r2+0]
    mova         m1, [r0+r2+0]
    mova         m2, [r0+r2+8]
    mova         m3, [r0+r2+8]
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    paddw        m1, m0
    paddw        m3, m2
    paddw        m3, m1
    paddw        m6, m3
    add          r2, r1
    js .loop
    mova         m5, m6
    psrlq        m6, 32
    paddw        m6, m5
    mova         m5, m6
    psrlq        m6, 16
    paddw        m6, m5
    movd        eax, m6
    and         eax, 0xffff
    RET

INIT_MMX mmx
; int ff_pix_norm1_mmx(uint8_t *pix, int line_size)
cglobal pix_norm1, 2, 4
    movsxdifnidn r1, r1d
    mov          r2, 16
    pxor         m0, m0
    pxor         m7, m7
.loop:
    mova         m2, [r0+0]
    mova         m3, [r0+8]
    mova         m1, m2
    punpckhbw    m1, m0
    punpcklbw    m2, m0
    mova         m4, m3
    punpckhbw    m3, m0
    punpcklbw    m4, m0
    pmaddwd      m1, m1
    pmaddwd      m2, m2
    pmaddwd      m3, m3
    pmaddwd      m4, m4
    paddd        m2, m1
    paddd        m4, m3
    paddd        m7, m2
    add          r0, r1
    paddd        m7, m4
    dec r2
    jne .loop
    mova         m1, m7
    psrlq        m7, 32
    paddd        m1, m7
    movd        eax, m1
    RET

