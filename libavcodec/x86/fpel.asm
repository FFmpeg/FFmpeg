;******************************************************************************
;* SIMD-optimized fullpel functions
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

; void ff_put/avg_pixels(uint8_t *block, const uint8_t *pixels,
;                        ptrdiff_t line_size, int h)
%macro OP_PIXELS 2-3 0
%if %2 == mmsize/2
%define LOAD movh
%define SAVE movh
%else
%define LOAD movu
%define SAVE mova
%endif
cglobal %1_pixels%2x%2, 3,5+4*%3,%3 ? 4 : 0
    mov         r3d, %2
    jmp         %1_pixels%2_after_prologue

cglobal %1_pixels%2, 4,5+4*%3,%3 ? 4 : 0
%1_pixels%2_after_prologue:
    lea          r4, [r2*3]
.loop:
%if %3
; Use GPRs on UNIX64 for put8, but not on Win64 due to a lack of volatile GPRs
    mov         r5q, [r1]
    mov         r6q, [r1+r2]
    mov         r7q, [r1+r2*2]
    mov         r8q, [r1+r4]
    mov        [r0], r5q
    mov     [r0+r2], r6q
    mov   [r0+r2*2], r7q
    mov     [r0+r4], r8q
%else
    LOAD         m0, [r1]
    LOAD         m1, [r1+r2]
    LOAD         m2, [r1+r2*2]
    LOAD         m3, [r1+r4]
%ifidn %1, avg
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
    pavgb        m2, [r0+r2*2]
    pavgb        m3, [r0+r4]
%endif
    SAVE       [r0], m0
    SAVE    [r0+r2], m1
    SAVE  [r0+r2*2], m2
    SAVE    [r0+r4], m3
%endif
    sub         r3d, 4
    lea          r1, [r1+r2*4]
    lea          r0, [r0+r2*4]
    jne       .loop
    RET
%endmacro

INIT_MMX mmxext
OP_PIXELS avg, 8

INIT_XMM sse2
OP_PIXELS put, 8, UNIX64
OP_PIXELS put, 16
OP_PIXELS avg, 16
