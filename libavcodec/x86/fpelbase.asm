;******************************************************************************
;* MMX optimized DSP utils
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

SECTION .text

INIT_MMX mmxext
; void pixels(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PIXELS48 2
%if %2 == 4
%define OP movh
%else
%define OP mova
%endif
cglobal %1_pixels%2, 4,5
    movsxdifnidn r2, r2d
    lea          r4, [r2*3]
.loop:
    OP           m0, [r1]
    OP           m1, [r1+r2]
    OP           m2, [r1+r2*2]
    OP           m3, [r1+r4]
    lea          r1, [r1+r2*4]
%ifidn %1, avg
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
    pavgb        m2, [r0+r2*2]
    pavgb        m3, [r0+r4]
%endif
    OP         [r0], m0
    OP      [r0+r2], m1
    OP    [r0+r2*2], m2
    OP      [r0+r4], m3
    sub         r3d, 4
    lea          r0, [r0+r2*4]
    jne       .loop
    RET
%endmacro

PIXELS48 put, 4
PIXELS48 avg, 4
PIXELS48 put, 8
PIXELS48 avg, 8


INIT_XMM sse2
; void put_pixels16_sse2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
cglobal put_pixels16, 4,5,4
    lea          r4, [r2*3]
.loop:
    movu         m0, [r1]
    movu         m1, [r1+r2]
    movu         m2, [r1+r2*2]
    movu         m3, [r1+r4]
    lea          r1, [r1+r2*4]
    mova       [r0], m0
    mova    [r0+r2], m1
    mova  [r0+r2*2], m2
    mova    [r0+r4], m3
    sub         r3d, 4
    lea          r0, [r0+r2*4]
    jnz       .loop
    REP_RET

; void avg_pixels16_sse2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
cglobal avg_pixels16, 4,5,4
    lea          r4, [r2*3]
.loop:
    movu         m0, [r1]
    movu         m1, [r1+r2]
    movu         m2, [r1+r2*2]
    movu         m3, [r1+r4]
    lea          r1, [r1+r2*4]
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
    pavgb        m2, [r0+r2*2]
    pavgb        m3, [r0+r4]
    mova       [r0], m0
    mova    [r0+r2], m1
    mova  [r0+r2*2], m2
    mova    [r0+r4], m3
    sub         r3d, 4
    lea          r0, [r0+r2*4]
    jnz       .loop
    REP_RET
