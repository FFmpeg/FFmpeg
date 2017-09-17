;******************************************************************************
;* X86 Optimized functions for Open Exr Decoder
;* Copyright (c) 2006 Industrial Light & Magic, a division of Lucas Digital Ltd. LLC
;*
;* reorder_pixels based on patch by John Loy
;* port to ASM by Jokyo Images support by CNC - French National Center for Cinema
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

;------------------------------------------------------------------------------
; void ff_reorder_pixels(uint8_t *src, uint8_t *dst, ptrdiff_t size)
;------------------------------------------------------------------------------

%macro REORDER_PIXELS 0
cglobal reorder_pixels, 3,4,3, src1, dst, size, src2
    lea                              src2q, [src1q+sizeq] ; src2 = src + 2 * half_size
    add                               dstq, sizeq         ; dst offset by size
    shr                              sizeq, 1             ; half_size
    add                              src1q, sizeq         ; offset src by half_size
    neg                              sizeq                ; size = offset for dst, src1, src2
.loop:

%if cpuflag(avx2)
    vpermq                              m0, [src1q + sizeq], 0xd8; load first part
    vpermq                              m1, [src2q + sizeq], 0xd8; load second part
%else
    mova                                m0, [src1q+sizeq]        ; load first part
    movu                                m1, [src2q+sizeq]        ; load second part
%endif
    SBUTTERFLY bw, 0, 1, 2                                       ; interleaved
    mova                 [dstq+2*sizeq   ], m0                   ; copy to dst
    mova             [dstq+2*sizeq+mmsize], m1
    add     sizeq, mmsize
    jl .loop
    RET
%endmacro

INIT_XMM sse2
REORDER_PIXELS

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
REORDER_PIXELS
%endif
