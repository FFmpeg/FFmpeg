;******************************************************************************
;* SIMD-optimized IDCT-related routines
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

SECTION_RODATA

cextern pb_80

SECTION_TEXT

;--------------------------------------------------------------------------
;void ff_put_signed_pixels_clamped(const int16_t *block, uint8_t *pixels,
;                                  int line_size)
;--------------------------------------------------------------------------

%macro PUT_SIGNED_PIXELS_CLAMPED_HALF 1
    mova     m1, [blockq+mmsize*0+%1]
    mova     m2, [blockq+mmsize*2+%1]
%if mmsize == 8
    mova     m3, [blockq+mmsize*4+%1]
    mova     m4, [blockq+mmsize*6+%1]
%endif
    packsswb m1, [blockq+mmsize*1+%1]
    packsswb m2, [blockq+mmsize*3+%1]
%if mmsize == 8
    packsswb m3, [blockq+mmsize*5+%1]
    packsswb m4, [blockq+mmsize*7+%1]
%endif
    paddb    m1, m0
    paddb    m2, m0
%if mmsize == 8
    paddb    m3, m0
    paddb    m4, m0
    movq     [pixelsq+lsizeq*0], m1
    movq     [pixelsq+lsizeq*1], m2
    movq     [pixelsq+lsizeq*2], m3
    movq     [pixelsq+lsize3q ], m4
%else
    movq     [pixelsq+lsizeq*0], m1
    movhps   [pixelsq+lsizeq*1], m1
    movq     [pixelsq+lsizeq*2], m2
    movhps   [pixelsq+lsize3q ], m2
%endif
%endmacro

%macro PUT_SIGNED_PIXELS_CLAMPED 1
cglobal put_signed_pixels_clamped, 3, 4, %1, block, pixels, lsize, lsize3
    mova     m0, [pb_80]
    lea      lsize3q, [lsizeq*3]
    PUT_SIGNED_PIXELS_CLAMPED_HALF 0
    lea      pixelsq, [pixelsq+lsizeq*4]
    PUT_SIGNED_PIXELS_CLAMPED_HALF 64
    RET
%endmacro

INIT_MMX mmx
PUT_SIGNED_PIXELS_CLAMPED 0
INIT_XMM sse2
PUT_SIGNED_PIXELS_CLAMPED 3
