;*****************************************************************************
;* x86-optimized Float DSP functions
;*
;* Copyright 2016 James Almer
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

;-----------------------------------------------------------------------------
; void ff_butterflies_fixed(float *src0, float *src1, int len);
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal butterflies_fixed, 3,3,3, src0, src1, len
    shl       lend, 2
    add      src0q, lenq
    add      src1q, lenq
    neg       lenq

align 16
.loop:
    mova        m0, [src0q + lenq]
    mova        m1, [src1q + lenq]
    mova        m2, m0
    paddd       m0, m1
    psubd       m2, m1
    mova        [src0q + lenq], m0
    mova        [src1q + lenq], m2
    add       lenq, mmsize
    jl .loop
    RET
