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
; void ff_reorder_pixels(uint8_t *dst, const uint8_t *src, ptrdiff_t size);
;------------------------------------------------------------------------------

%macro REORDER_PIXELS 0
cglobal reorder_pixels, 3,4,3, dst, src1, size, src2
    lea                              src2q, [src1q+sizeq] ; src2 = src + 2 * half_size
    add                               dstq, sizeq         ; dst offset by size
    shr                              sizeq, 1             ; half_size
    add                              src1q, sizeq         ; offset src by half_size
    neg                              sizeq                ; size = offset for dst, src1, src2
.loop:

    mova                                m0, [src1q+sizeq]        ; load first part
    movu                                m1, [src2q+sizeq]        ; load second part
    SBUTTERFLY bw, 0, 1, 2                                       ; interleaved
    mova                 [dstq+2*sizeq   ], xm0                  ; copy to dst
    mova                 [dstq+2*sizeq+16], xm1
%if cpuflag(avx2)
    vperm2i128                          m0, m0, m1, q0301
    mova                 [dstq+2*sizeq+32], m0
%endif
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
