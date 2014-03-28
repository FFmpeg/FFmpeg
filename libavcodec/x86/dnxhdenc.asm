;************************************************************************
;* VC3/DNxHD SIMD functions
;* Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
;* Copyright (c) 2014 Tiancheng "Timothy" Gu <timothygu99@gmail.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

section .text

; void get_pixels_8x4_sym_sse2(int16_t *block, const uint8_t *pixels,
;                              ptrdiff_t line_size)
INIT_XMM sse2
cglobal get_pixels_8x4_sym, 3,3,5, block, pixels, linesize
    pxor      m4,       m4
    movq      m0,       [pixelsq]
    add       pixelsq,  linesizeq
    movq      m1,       [pixelsq]
    movq      m2,       [pixelsq+linesizeq]
    movq      m3,       [pixelsq+linesizeq*2]
    punpcklbw m0,       m4
    punpcklbw m1,       m4
    punpcklbw m2,       m4
    punpcklbw m3,       m4
    mova  [blockq    ], m0
    mova  [blockq+16 ], m1
    mova  [blockq+32 ], m2
    mova  [blockq+48 ], m3
    mova  [blockq+64 ], m3
    mova  [blockq+80 ], m2
    mova  [blockq+96 ], m1
    mova  [blockq+112], m0
    RET
