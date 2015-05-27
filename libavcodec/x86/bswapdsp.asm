;******************************************************************************
;* optimized bswap buffer functions
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2003-2013 Michael Niedermayer
;* Copyright (c) 2013 Daniel Kang
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
pb_bswap32: db 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12

cextern pb_80

SECTION .text

; %1 = aligned/unaligned
%macro BSWAP_LOOPS  1
    mov      r3d, r2d
    sar      r2d, 3
    jz       .left4_%1
.loop8_%1:
    mov%1    m0, [r1 +  0]
    mov%1    m1, [r1 + 16]
%if cpuflag(ssse3)
    pshufb   m0, m2
    pshufb   m1, m2
    mov%1    [r0 +  0], m0
    mov%1    [r0 + 16], m1
%else
    pshuflw  m0, m0, 10110001b
    pshuflw  m1, m1, 10110001b
    pshufhw  m0, m0, 10110001b
    pshufhw  m1, m1, 10110001b
    mova     m2, m0
    mova     m3, m1
    psllw    m0, 8
    psllw    m1, 8
    psrlw    m2, 8
    psrlw    m3, 8
    por      m2, m0
    por      m3, m1
    mov%1    [r0 +  0], m2
    mov%1    [r0 + 16], m3
%endif
    add      r0, 32
    add      r1, 32
    dec      r2d
    jnz      .loop8_%1
.left4_%1:
    mov      r2d, r3d
    test     r3d, 4
    jz       .left
    mov%1    m0, [r1]
%if cpuflag(ssse3)
    pshufb   m0, m2
    mov%1    [r0], m0
%else
    pshuflw  m0, m0, 10110001b
    pshufhw  m0, m0, 10110001b
    mova     m2, m0
    psllw    m0, 8
    psrlw    m2, 8
    por      m2, m0
    mov%1    [r0], m2
%endif
    add      r1, 16
    add      r0, 16
%endmacro

; void ff_bswap_buf(uint32_t *dst, const uint32_t *src, int w);
%macro BSWAP32_BUF 0
%if cpuflag(ssse3)
cglobal bswap32_buf, 3,4,3
    mov      r3, r1
    mova     m2, [pb_bswap32]
%else
cglobal bswap32_buf, 3,4,5
    mov      r3, r1
%endif
    or       r3, r0
    test     r3, 15
    jz       .start_align
    BSWAP_LOOPS  u
    jmp      .left
.start_align:
    BSWAP_LOOPS  a
.left:
%if cpuflag(ssse3)
    test     r2d, 2
    jz       .left1
    movq     m0, [r1]
    pshufb   m0, m2
    movq     [r0], m0
    add      r1, 8
    add      r0, 8
.left1:
    test     r2d, 1
    jz       .end
    mov      r2d, [r1]
    bswap    r2d
    mov      [r0], r2d
%else
    and      r2d, 3
    jz       .end
.loop2:
    mov      r3d, [r1]
    bswap    r3d
    mov      [r0], r3d
    add      r1, 4
    add      r0, 4
    dec      r2d
    jnz      .loop2
%endif
.end:
    RET
%endmacro

INIT_XMM sse2
BSWAP32_BUF

INIT_XMM ssse3
BSWAP32_BUF
