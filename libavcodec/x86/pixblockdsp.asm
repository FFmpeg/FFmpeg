;*****************************************************************************
;* SIMD-optimized pixel operations
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

INIT_MMX mmx
; void ff_get_pixels_mmx(int16_t *block, const uint8_t *pixels, ptrdiff_t stride)
cglobal get_pixels, 3,4
    add          r0, 128
    mov          r3, -128
    pxor         m7, m7
.loop:
    mova         m0, [r1]
    mova         m2, [r1+r2]
    mova         m1, m0
    mova         m3, m2
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    mova [r0+r3+ 0], m0
    mova [r0+r3+ 8], m1
    mova [r0+r3+16], m2
    mova [r0+r3+24], m3
    lea          r1, [r1+r2*2]
    add          r3, 32
    js .loop
    REP_RET

INIT_XMM sse2
cglobal get_pixels, 3, 4, 5
    lea          r3, [r2*3]
    pxor         m4, m4
    movh         m0, [r1]
    movh         m1, [r1+r2]
    movh         m2, [r1+r2*2]
    movh         m3, [r1+r3]
    lea          r1, [r1+r2*4]
    punpcklbw    m0, m4
    punpcklbw    m1, m4
    punpcklbw    m2, m4
    punpcklbw    m3, m4
    mova       [r0], m0
    mova  [r0+0x10], m1
    mova  [r0+0x20], m2
    mova  [r0+0x30], m3
    movh         m0, [r1]
    movh         m1, [r1+r2*1]
    movh         m2, [r1+r2*2]
    movh         m3, [r1+r3]
    punpcklbw    m0, m4
    punpcklbw    m1, m4
    punpcklbw    m2, m4
    punpcklbw    m3, m4
    mova  [r0+0x40], m0
    mova  [r0+0x50], m1
    mova  [r0+0x60], m2
    mova  [r0+0x70], m3
    RET

; void ff_diff_pixels_mmx(int16_t *block, const uint8_t *s1, const uint8_t *s2,
;                         ptrdiff_t stride);
%macro DIFF_PIXELS 0
cglobal diff_pixels, 4,5,5
    pxor         m4, m4
    add          r0,  128
    mov          r4, -128
.loop:
    movq         m0, [r1]
    movq         m2, [r2]
%if mmsize == 8
    movq         m1, m0
    movq         m3, m2
    punpcklbw    m0, m4
    punpckhbw    m1, m4
    punpcklbw    m2, m4
    punpckhbw    m3, m4
%else
    movq         m1, [r1+r3]
    movq         m3, [r2+r3]
    punpcklbw    m0, m4
    punpcklbw    m1, m4
    punpcklbw    m2, m4
    punpcklbw    m3, m4
%endif
    psubw        m0, m2
    psubw        m1, m3
    mova  [r0+r4+0], m0
    mova  [r0+r4+mmsize], m1
%if mmsize == 8
    add          r1, r3
    add          r2, r3
%else
    lea          r1, [r1+r3*2]
    lea          r2, [r2+r3*2]
%endif
    add          r4, 2 * mmsize
    jne .loop
    RET
%endmacro

INIT_MMX mmx
DIFF_PIXELS

INIT_XMM sse2
DIFF_PIXELS
