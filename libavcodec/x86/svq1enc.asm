;******************************************************************************
;* SIMD-optimized SVQ1 encoder functions
;* Copyright (c) 2007 Loren Merritt
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

INIT_XMM sse2
cglobal ssd_int8_vs_int16, 3, 3, 3, pix1, pix2, size
    pxor m0, m0
.loop:
    sub       sizeq, 8
    movq      m1, [pix1q + sizeq]
    mova      m2, [pix2q + sizeq*2]
    punpcklbw m1, m1
    psraw     m1, 8
    psubw     m2, m1
    pmaddwd   m2, m2
    paddd     m0, m2
    jg .loop
    HADDD     m0, m1
    movd     eax, m0
    RET
