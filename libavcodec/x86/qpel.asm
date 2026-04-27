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

%macro op_avg 2
    pavgb  %1, %2
    mova   %2, %1
%endmacro

%macro op_put 2
    mova   %2, %1
%endmacro

%macro PIXELS_L2 2 ; avg vs put, size
%define OP op_%1
; void ff_avg/put_pixels8x8_l2_mmxext(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                                     ptrdiff_t dstStride, ptrdiff_t src1Stride)
cglobal %1_pixels%2x%2_l2, 5,6,2
    mov         r5d, %2
.loop:
    movu         m0, [r1]
    movu         m1, [r1+r4]
    lea          r1, [r1+2*r4]
    pavgb        m0, [r2]
    pavgb        m1, [r2+mmsize]
    OP           m0, [r0]
    OP           m1, [r0+r3]
    lea          r0, [r0+2*r3]
    movu         m0, [r1]
    movu         m1, [r1+r4]
    lea          r1, [r1+2*r4]
    pavgb        m0, [r2+2*mmsize]
    pavgb        m1, [r2+3*mmsize]
    OP           m0, [r0]
    OP           m1, [r0+r3]
    lea          r0, [r0+2*r3]
    add          r2, 4*mmsize
    sub         r5d, 4
    jne       .loop
    RET
%endmacro

INIT_MMX mmxext
PIXELS_L2 put, 8
PIXELS_L2 avg, 8

INIT_XMM sse2
PIXELS_L2 put, 16
PIXELS_L2 avg, 16
