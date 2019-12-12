;******************************************************************************
;* x86-optimized functions for gradfun filter
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

pw_7f: times 8 dw 0x7F
pw_ff: times 8 dw 0xFF

SECTION .text

%macro FILTER_LINE 1
    movh       m0, [r2+r0]
    movh       m1, [r3+r0]
    punpcklbw  m0, m7
    punpcklwd  m1, m1
    psllw      m0, 7
    psubw      m1, m0
    PABSW      m2, m1
    pmulhuw    m2, m5
    psubw      m2, m6
    pminsw     m2, m7
    pmullw     m2, m2
    psllw      m1, 2
    paddw      m0, %1
    pmulhw     m1, m2
    paddw      m0, m1
    psraw      m0, 7
    packuswb   m0, m0
    movh  [r1+r0], m0
%endmacro

INIT_MMX mmxext
cglobal gradfun_filter_line, 6, 6
    movh      m5, r4d
    pxor      m7, m7
    pshufw    m5, m5,0
    mova      m6, [pw_7f]
    mova      m3, [r5]
    mova      m4, [r5+8]
.loop:
    FILTER_LINE m3
    add       r0, 4
    jge .end
    FILTER_LINE m4
    add       r0, 4
    jl .loop
.end:
    REP_RET

INIT_XMM ssse3
cglobal gradfun_filter_line, 6, 6, 8
    movd       m5, r4d
    pxor       m7, m7
    pshuflw    m5, m5, 0
    mova       m6, [pw_7f]
    punpcklqdq m5, m5
    mova       m4, [r5]
.loop:
    FILTER_LINE m4
    add        r0, 8
    jl .loop
    REP_RET

%macro BLUR_LINE 1
cglobal gradfun_blur_line_%1, 6, 6, 8
    mova        m7, [pw_ff]
.loop:
    %1          m0, [r4+r0]
    %1          m1, [r5+r0]
    mova        m2, m0
    mova        m3, m1
    psrlw       m0, 8
    psrlw       m1, 8
    pand        m2, m7
    pand        m3, m7
    paddw       m0, m1
    paddw       m2, m3
    paddw       m0, m2
    paddw       m0, [r2+r0]
    mova        m1, [r1+r0]
    mova   [r1+r0], m0
    psubw       m0, m1
    mova   [r3+r0], m0
    add         r0, 16
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
BLUR_LINE movdqa
BLUR_LINE movdqu
