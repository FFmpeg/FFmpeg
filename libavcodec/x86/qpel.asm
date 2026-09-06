;******************************************************************************
;* SIMD-optimized quarterpel functions
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

%macro AVG8 2
    movq   m2, %2
    pavgb  %1, m2
%endmacro

%macro AVG16 2
    pavgb  %1, %2
%endmacro

%macro op_avg_8 2
    movq   m2, %2
    pavgb  %1, m2
    movq   %2, %1
%endmacro

%macro op_avg_16 2
    pavgb  %1, %2
    mova   %2, %1
%endmacro

%macro op_put_8 2
    movq   %2, %1
%endmacro

%macro op_put_16 2
    mova   %2, %1
%endmacro

%define MOV8  movq
%define MOV16 movu

%macro PIXELS_L2 3 ; avg vs put, size
%define OP op_%1_%2
; void ff_avg/put_pixels8x8_l2_sse2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                                   ptrdiff_t dstStride, ptrdiff_t src1Stride)
cglobal %1_pixels%2x%2_l2, 5,6,%3
    mov         r5d, %2
.loop:
    MOV%2        m0, [r1]
    MOV%2        m1, [r1+r4]
    lea          r1, [r1+2*r4]
    AVG%2        m0, [r2]
    AVG%2        m1, [r2+%2]
    OP           m0, [r0]
    OP           m1, [r0+r3]
    lea          r0, [r0+2*r3]
    MOV%2        m0, [r1]
    MOV%2        m1, [r1+r4]
    lea          r1, [r1+2*r4]
    AVG%2        m0, [r2+2*%2]
    AVG%2        m1, [r2+3*%2]
    OP           m0, [r0]
    OP           m1, [r0+r3]
    lea          r0, [r0+2*r3]
    add          r2, 4*%2
    sub         r5d, 4
    jne       .loop
    RET
%endmacro

INIT_XMM sse2
PIXELS_L2 put, 8, 3
PIXELS_L2 avg, 8, 3
PIXELS_L2 put, 16, 2
PIXELS_L2 avg, 16, 2
