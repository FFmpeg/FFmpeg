;*****************************************************************************
;* MMX/SSE2/AVX-optimized 10-bit H.264 intra prediction code
;*****************************************************************************
;* Copyright (C) 2005-2011 x264 project
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA

SECTION .text

cextern pw_4
cextern pw_1

%macro PRED4x4_LOWPASS 4
    paddw       %2, %3
    psrlw       %2, 1
    pavgw       %1, %4, %2
%endmacro

;-----------------------------------------------------------------------------
; void pred4x4_down_right(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_DR 1
cglobal pred4x4_down_right_10_%1, 3,3
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movhps    m1, [r1-8]
    movhps    m2, [r0+r2*1-8]
    movhps    m4, [r0-8]
    punpckhwd m2, m4
    movq      m3, [r0]
    punpckhdq m1, m2
    PALIGNR   m3, m1, 10, m1
    mova      m1, m3
    movhps    m4, [r1+r2*1-8]
    PALIGNR   m3, m4, 14, m4
    mova      m2, m3
    movhps    m4, [r1+r2*2-8]
    PALIGNR   m3, m4, 14, m4
    PRED4x4_LOWPASS m0, m3, m1, m2
    movq      [r1+r2*2], m0
    psrldq    m0, 2
    movq      [r1+r2*1], m0
    psrldq    m0, 2
    movq      [r0+r2*2], m0
    psrldq    m0, 2
    movq      [r0+r2*1], m0
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED4x4_DR sse2
%define PALIGNR PALIGNR_SSSE3
PRED4x4_DR ssse3
%ifdef HAVE_AVX
INIT_AVX
PRED4x4_DR avx
%endif

;-----------------------------------------------------------------------------
; void pred4x4_vertical_right(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_VR 1
cglobal pred4x4_vertical_right_10_%1, 3,3,6
    sub     r0, r2
    lea     r1, [r0+r2*2]
    movq    m5, [r0]            ; ........t3t2t1t0
    movhps  m1, [r0-8]
    PALIGNR m0, m5, m1, 14, m1  ; ......t3t2t1t0lt
    pavgw   m5, m0
    movhps  m1, [r0+r2*1-8]
    PALIGNR m0, m1, 14, m1      ; ....t3t2t1t0ltl0
    mova    m1, m0
    movhps  m2, [r0+r2*2-8]
    PALIGNR m0, m2, 14, m2      ; ..t3t2t1t0ltl0l1
    mova    m2, m0
    movhps  m3, [r1+r2*1-8]
    PALIGNR m0, m3, 14, m3      ; t3t2t1t0ltl0l1l2
    PRED4x4_LOWPASS m3, m1, m0, m2
    pslldq  m1, m3, 12
    psrldq  m3, 4
    movq    [r0+r2*1], m5
    movq    [r0+r2*2], m3
    PALIGNR m5, m1, 14, m2
    pslldq  m1, 2
    movq    [r1+r2*1], m5
    PALIGNR m3, m1, 14, m1
    movq    [r1+r2*2], m3
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED4x4_VR sse2
%define PALIGNR PALIGNR_SSSE3
PRED4x4_VR ssse3
%ifdef HAVE_AVX
INIT_AVX
PRED4x4_VR avx
%endif

;-----------------------------------------------------------------------------
; void pred4x4_horizontal_down(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_HD 1
cglobal pred4x4_horizontal_down_10_%1, 3,3
    sub        r0, r2
    lea        r1, [r0+r2*2]
    movq       m0, [r0-8]      ; lt ..
    movhps     m0, [r0]
    pslldq     m0, 2           ; t2 t1 t0 lt .. .. .. ..
    movq       m1, [r1+r2*2-8] ; l3
    movq       m3, [r1+r2*1-8]
    punpcklwd  m1, m3          ; l2 l3
    movq       m2, [r0+r2*2-8] ; l1
    movq       m3, [r0+r2*1-8]
    punpcklwd  m2, m3          ; l0 l1
    punpckhdq  m1, m2          ; l0 l1 l2 l3
    punpckhqdq m1, m0          ; t2 t1 t0 lt l0 l1 l2 l3
    psrldq     m0, m1, 4       ; .. .. t2 t1 t0 lt l0 l1
    psrldq     m2, m1, 2       ; .. t2 t1 t0 lt l0 l1 l2
    pavgw      m5, m1, m2
    PRED4x4_LOWPASS m3, m1, m0, m2
    punpcklwd  m5, m3
    psrldq     m3, 8
    PALIGNR    m3, m5, 12, m4
    movq       [r1+r2*2], m5
    movhps     [r0+r2*2], m5
    psrldq     m5, 4
    movq       [r1+r2*1], m5
    movq       [r0+r2*1], m3
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED4x4_HD sse2
%define PALIGNR PALIGNR_SSSE3
PRED4x4_HD ssse3
%ifdef HAVE_AVX
INIT_AVX
PRED4x4_HD avx
%endif

;-----------------------------------------------------------------------------
; void pred4x4_dc(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro HADDD 2 ; sum junk
%if mmsize == 16
    movhlps %2, %1
    paddd   %1, %2
    pshuflw %2, %1, 0xE
    paddd   %1, %2
%else
    pshufw  %2, %1, 0xE
    paddd   %1, %2
%endif
%endmacro

%macro HADDW 2
    pmaddwd %1, [pw_1]
    HADDD   %1, %2
%endmacro

INIT_MMX
cglobal pred4x4_dc_10_mmxext, 3,3
    sub    r0, r2
    lea    r1, [r0+r2*2]
    movq   m2, [r0+r2*1-8]
    paddw  m2, [r0+r2*2-8]
    paddw  m2, [r1+r2*1-8]
    paddw  m2, [r1+r2*2-8]
    psrlq  m2, 48
    movq   m0, [r0]
    HADDW  m0, m1
    paddw  m0, [pw_4]
    paddw  m0, m2
    psrlw  m0, 3
    SPLATW m0, m0, 0
    movq   [r0+r2*1], m0
    movq   [r0+r2*2], m0
    movq   [r1+r2*1], m0
    movq   [r1+r2*2], m0
    RET

;-----------------------------------------------------------------------------
; void pred4x4_down_left(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
;TODO: more AVX here
%macro PRED4x4_DL 1
cglobal pred4x4_down_left_10_%1, 3,3
    sub        r0, r2
    movq       m1, [r0]
    movhps     m1, [r1]
    pslldq     m5, m1, 2
    pxor       m2, m5, m1
    psrldq     m2, 2
    pxor       m3, m1, m2
    PRED4x4_LOWPASS m0, m5, m3, m1
    lea        r1, [r0+r2*2]
    movhps     [r1+r2*2], m0
    psrldq     m0, 2
    movq       [r0+r2*1], m0
    psrldq     m0, 2
    movq       [r0+r2*2], m0
    psrldq     m0, 2
    movq       [r1+r2*1], m0
    RET
%endmacro

INIT_XMM
PRED4x4_DL sse2
%ifdef HAVE_AVX
INIT_AVX
PRED4x4_DL avx
%endif

;-----------------------------------------------------------------------------
; void pred4x4_vertical_left(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_VL 1
cglobal pred4x4_vertical_left_10_%1, 3,3
    sub        r0, r2
    movu       m1, [r0]
    movhps     m1, [r1]
    psrldq     m3, m1, 2
    psrldq     m2, m1, 4
    pavgw      m4, m3, m1
    PRED4x4_LOWPASS m0, m1, m2, m3
    lea        r1, [r0+r2*2]
    movq       [r0+r2*1], m4
    movq       [r0+r2*2], m0
    psrldq     m4, 2
    psrldq     m0, 2
    movq       [r1+r2*1], m4
    movq       [r1+r2*2], m0
    RET
%endmacro

INIT_XMM
PRED4x4_VL sse2
%ifdef HAVE_AVX
INIT_AVX
PRED4x4_VL avx
%endif

;-----------------------------------------------------------------------------
; void pred4x4_horizontal_up(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
INIT_MMX
cglobal pred4x4_horizontal_up_10_mmxext, 3,3
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movq      m0, [r0+r2*1-8]
    punpckhwd m0, [r0+r2*2-8]
    movq      m1, [r1+r2*1-8]
    punpckhwd m1, [r1+r2*2-8]
    punpckhdq m0, m1
    pshufw    m1, m1, 0xFF
    movq      [r1+r2*2], m1
    movd      [r1+r2*1+4], m1
    pshufw    m2, m0, 11111001b
    movq      m1, m2
    pavgw     m2, m0

    pshufw    m5, m0, 11111110b
    PRED4x4_LOWPASS m3, m0, m5, m1
    movq      m6, m2
    punpcklwd m6, m3
    movq      [r0+r2*1], m6
    psrlq     m2, 16
    psrlq     m3, 16
    punpcklwd m2, m3
    movq      [r0+r2*2], m2
    psrlq     m2, 32
    movd      [r1+r2*1], m2
    RET



;-----------------------------------------------------------------------------
; void pred8x8_vertical(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM
cglobal pred8x8_vertical_10_sse2, 2,2
    sub  r0, r1
    mova m0, [r0]
%rep 3
    mova [r0+r1*1], m0
    mova [r0+r1*2], m0
    lea  r0, [r0+r1*2]
%endrep
    mova [r0+r1*1], m0
    mova [r0+r1*2], m0
    RET

;-----------------------------------------------------------------------------
; void pred8x8_horizontal(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM
cglobal pred8x8_horizontal_10_sse2, 2,3
    mov          r2, 4
.loop:
    movq         m0, [r0+r1*0-8]
    movq         m1, [r0+r1*1-8]
    pshuflw      m0, m0, 0xff
    pshuflw      m1, m1, 0xff
    punpcklqdq   m0, m0
    punpcklqdq   m1, m1
    mova  [r0+r1*0], m0
    mova  [r0+r1*1], m1
    lea          r0, [r0+r1*2]
    dec          r2
    jg .loop
    REP_RET
