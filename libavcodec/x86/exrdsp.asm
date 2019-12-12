;******************************************************************************
;* X86 Optimized functions for Open Exr Decoder
;* Copyright (c) 2006 Industrial Light & Magic, a division of Lucas Digital Ltd. LLC
;*
;* reorder_pixels, predictor based on patch by John Loy
;* port to ASM by Jokyo Images support by CNC - French National Center for Cinema
;*
;* predictor AVX/AVX2 by Henrik Gramner
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

cextern pb_15
cextern pb_80

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


;------------------------------------------------------------------------------
; void ff_predictor(uint8_t *src, ptrdiff_t size);
;------------------------------------------------------------------------------

%macro PREDICTOR 0
cglobal predictor, 2,2,5, src, size
    mova             m0, [pb_80]
    mova            xm1, [pb_15]
    mova            xm2, xm0
    add            srcq, sizeq
    neg           sizeq
.loop:
    pxor             m3, m0, [srcq + sizeq]
    pslldq           m4, m3, 1
    paddb            m3, m4
    pslldq           m4, m3, 2
    paddb            m3, m4
    pslldq           m4, m3, 4
    paddb            m3, m4
    pslldq           m4, m3, 8
%if mmsize == 32
    paddb            m3, m4
    paddb           xm2, xm3
    vextracti128    xm4, m3, 1
    mova [srcq + sizeq], xm2
    pshufb          xm2, xm1
    paddb           xm2, xm4
    mova [srcq + sizeq + 16], xm2
%else
    paddb            m2, m3
    paddb            m2, m4
    mova [srcq + sizeq], m2
%endif
    pshufb          xm2, xm1
    add           sizeq, mmsize
    jl .loop
    RET
%endmacro

INIT_XMM ssse3
PREDICTOR

INIT_XMM avx
PREDICTOR

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
PREDICTOR
%endif
