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
;                                  ptrdiff_t line_size)
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

;--------------------------------------------------------------------------
; void ff_put_pixels_clamped(const int16_t *block, uint8_t *pixels,
;                            ptrdiff_t line_size);
;--------------------------------------------------------------------------
; %1 = block offset
%macro PUT_PIXELS_CLAMPED_HALF 1
    mova     m0, [blockq+mmsize*0+%1]
    mova     m1, [blockq+mmsize*2+%1]
%if mmsize == 8
    mova     m2, [blockq+mmsize*4+%1]
    mova     m3, [blockq+mmsize*6+%1]
%endif
    packuswb m0, [blockq+mmsize*1+%1]
    packuswb m1, [blockq+mmsize*3+%1]
%if mmsize == 8
    packuswb m2, [blockq+mmsize*5+%1]
    packuswb m3, [blockq+mmsize*7+%1]
    movq           [pixelsq], m0
    movq    [lsizeq+pixelsq], m1
    movq  [2*lsizeq+pixelsq], m2
    movq   [lsize3q+pixelsq], m3
%else
    movq           [pixelsq], m0
    movhps  [lsizeq+pixelsq], m0
    movq  [2*lsizeq+pixelsq], m1
    movhps [lsize3q+pixelsq], m1
%endif
%endmacro

%macro PUT_PIXELS_CLAMPED 0
cglobal put_pixels_clamped, 3, 4, 2, block, pixels, lsize, lsize3
    lea lsize3q, [lsizeq*3]
    PUT_PIXELS_CLAMPED_HALF 0
    lea pixelsq, [pixelsq+lsizeq*4]
    PUT_PIXELS_CLAMPED_HALF 64
    RET
%endmacro

INIT_MMX mmx
PUT_PIXELS_CLAMPED
INIT_XMM sse2
PUT_PIXELS_CLAMPED

;--------------------------------------------------------------------------
; void ff_add_pixels_clamped(const int16_t *block, uint8_t *pixels,
;                            ptrdiff_t line_size);
;--------------------------------------------------------------------------
; %1 = block offset
%macro ADD_PIXELS_CLAMPED 1
    mova       m0, [blockq+mmsize*0+%1]
    mova       m1, [blockq+mmsize*1+%1]
%if mmsize == 8
    mova       m5, [blockq+mmsize*2+%1]
    mova       m6, [blockq+mmsize*3+%1]
%endif
    movq       m2, [pixelsq]
    movq       m3, [pixelsq+lsizeq]
%if mmsize == 8
    mova       m7, m2
    punpcklbw  m2, m4
    punpckhbw  m7, m4
    paddsw     m0, m2
    paddsw     m1, m7
    mova       m7, m3
    punpcklbw  m3, m4
    punpckhbw  m7, m4
    paddsw     m5, m3
    paddsw     m6, m7
%else
    punpcklbw  m2, m4
    punpcklbw  m3, m4
    paddsw     m0, m2
    paddsw     m1, m3
%endif
    packuswb   m0, m1
%if mmsize == 8
    packuswb   m5, m6
    movq       [pixelsq], m0
    movq       [pixelsq+lsizeq], m5
%else
    movq       [pixelsq], m0
    movhps     [pixelsq+lsizeq], m0
%endif
%endmacro

%macro ADD_PIXELS_CLAMPED 0
cglobal add_pixels_clamped, 3, 3, 5, block, pixels, lsize
    pxor       m4, m4
    ADD_PIXELS_CLAMPED 0
    lea        pixelsq, [pixelsq+lsizeq*2]
    ADD_PIXELS_CLAMPED 32
    lea        pixelsq, [pixelsq+lsizeq*2]
    ADD_PIXELS_CLAMPED 64
    lea        pixelsq, [pixelsq+lsizeq*2]
    ADD_PIXELS_CLAMPED 96
    RET
%endmacro

INIT_MMX mmx
ADD_PIXELS_CLAMPED
INIT_XMM sse2
ADD_PIXELS_CLAMPED
