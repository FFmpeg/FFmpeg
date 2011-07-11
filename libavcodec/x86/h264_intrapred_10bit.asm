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

cextern pw_16
cextern pw_8
cextern pw_4
cextern pw_2
cextern pw_1

pw_m32101234: dw -3, -2, -1, 0, 1, 2, 3, 4
pw_m3:        times 8 dw -3
pw_pixel_max: times 8 dw ((1 << 10)-1)
pw_512:       times 8 dw 512
pd_17:        times 4 dd 17
pd_16:        times 4 dd 16

; dest, left, right, src
; output: %1 = (t[n-1] + t[n]*2 + t[n+1] + 2) >> 2
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

;-----------------------------------------------------------------------------
; void predict_8x8_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro MOV8 2-3
; sort of a hack, but it works
%if mmsize==8
    movq    [%1+0], %2
    movq    [%1+8], %3
%else
    movdqa    [%1], %2
%endif
%endmacro

%macro PRED8x8_DC 2
cglobal pred8x8_dc_10_%1, 2,4
%ifdef ARCH_X86_64
%define t0 r10
%else
%define t0 r0m
%endif
    sub         r0, r1
    pxor        m4, m4
    movq        m0, [r0+0]
    movq        m1, [r0+8]
    HADDW       m0, m2
    mov         t0, r0
    HADDW       m1, m2

    movzx      r2d, word [r0+r1*1-2]
    movzx      r3d, word [r0+r1*2-2]
    lea         r0, [r0+r1*2]
    add        r2d, r3d
    movzx      r3d, word [r0+r1*1-2]
    add        r2d, r3d
    movzx      r3d, word [r0+r1*2-2]
    add        r2d, r3d
    lea         r0, [r0+r1*2]
    movd        m2, r2d            ; s2

    movzx      r2d, word [r0+r1*1-2]
    movzx      r3d, word [r0+r1*2-2]
    lea         r0, [r0+r1*2]
    add        r2d, r3d
    movzx      r3d, word [r0+r1*1-2]
    add        r2d, r3d
    movzx      r3d, word [r0+r1*2-2]
    add        r2d, r3d
    movd        m3, r2d            ; s3

    punpcklwd   m0, m1
    mov         r0, t0
    punpcklwd   m2, m3
    punpckldq   m0, m2            ; s0, s1, s2, s3
    %2          m3, m0, 11110110b ; s2, s1, s3, s3
    lea         r2, [r1+r1*2]
    %2          m0, m0, 01110100b ; s0, s1, s3, s1
    paddw       m0, m3
    lea         r3, [r0+r1*4]
    psrlw       m0, 2
    pavgw       m0, m4            ; s0+s2, s1, s3, s1+s3
%ifidn %1, sse2
    punpcklwd   m0, m0
    pshufd      m3, m0, 11111010b
    punpckldq   m0, m0
    SWAP         0,1
%else
    pshufw      m1, m0, 0x00
    pshufw      m2, m0, 0x55
    pshufw      m3, m0, 0xaa
    pshufw      m4, m0, 0xff
%endif
    MOV8   r0+r1*1, m1, m2
    MOV8   r0+r1*2, m1, m2
    MOV8   r0+r2*1, m1, m2
    MOV8   r0+r1*4, m1, m2
    MOV8   r3+r1*1, m3, m4
    MOV8   r3+r1*2, m3, m4
    MOV8   r3+r2*1, m3, m4
    MOV8   r3+r1*4, m3, m4
    RET
%endmacro

INIT_MMX
PRED8x8_DC mmxext, pshufw
INIT_XMM
PRED8x8_DC sse2  , pshuflw

;-----------------------------------------------------------------------------
; void pred8x8_top_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8_TOP_DC 2
cglobal pred8x8_top_dc_10_%1, 2,4
    sub         r0, r1
    movq        m0, [r0+0]
    movq        m1, [r0+8]
    HADDW       m0, m2
    HADDW       m1, m3
    lea         r2, [r1+r1*2]
    paddw       m0, [pw_2]
    paddw       m1, [pw_2]
    lea         r3, [r0+r1*4]
    psrlw       m0, 2
    psrlw       m1, 2
    %2          m0, m0, 0
    %2          m1, m1, 0
%ifidn %1, sse2
    punpcklqdq  m0, m1
%endif
    MOV8   r0+r1*1, m0, m1
    MOV8   r0+r1*2, m0, m1
    MOV8   r0+r2*1, m0, m1
    MOV8   r0+r1*4, m0, m1
    MOV8   r3+r1*1, m0, m1
    MOV8   r3+r1*2, m0, m1
    MOV8   r3+r2*1, m0, m1
    MOV8   r3+r1*4, m0, m1
    RET
%endmacro

INIT_MMX
PRED8x8_TOP_DC mmxext, pshufw
INIT_XMM
PRED8x8_TOP_DC sse2  , pshuflw

;-----------------------------------------------------------------------------
; void pred8x8_plane(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM
cglobal pred8x8_plane_10_sse2, 2,7,7
    sub       r0, r1
    lea       r2, [r1+r1*2]
    lea       r3, [r0+r1*4]
    mova      m2, [r0]
    pmaddwd   m2, [pw_m32101234]
    HADDD     m2, m1
    movd      m0, [r0-4]
    psrld     m0, 14
    psubw     m2, m0               ; H
    movd      m0, [r3+r1*4-4]
    movd      m1, [r0+12]
    paddw     m0, m1
    psllw     m0, 4                ; 16*(src[7*stride-1] + src[-stride+7])
    movzx    r4d, word [r3+r1*1-2] ; src[4*stride-1]
    movzx    r5d, word [r0+r2*1-2] ; src[2*stride-1]
    sub      r4d, r5d
    movzx    r6d, word [r3+r1*2-2] ; src[5*stride-1]
    movzx    r5d, word [r0+r1*2-2] ; src[1*stride-1]
    sub      r6d, r5d
    lea      r4d, [r4+r6*2]
    movzx    r5d, word [r3+r2*1-2] ; src[6*stride-1]
    movzx    r6d, word [r0+r1*1-2] ; src[0*stride-1]
    sub      r5d, r6d
    lea      r5d, [r5+r5*2]
    add      r4d, r5d
    movzx    r6d, word [r3+r1*4-2] ; src[7*stride-1]
    movzx    r5d, word [r0+r1*0-2] ; src[ -stride-1]
    sub      r6d, r5d
    lea      r4d, [r4+r6*4]
    movd      m3, r4d              ; V
    punpckldq m2, m3
    pmaddwd   m2, [pd_17]
    paddd     m2, [pd_16]
    psrad     m2, 5                ; b, c

    mova      m3, [pw_pixel_max]
    pxor      m1, m1
    SPLATW    m0, m0, 1
    SPLATW    m4, m2, 2
    SPLATW    m2, m2, 0
    pmullw    m2, [pw_m32101234]   ; b
    pmullw    m5, m4, [pw_m3]      ; c
    paddw     m5, [pw_16]
    mov      r2d, 8
    add       r0, r1
.loop:
    paddsw    m6, m2, m5
    paddsw    m6, m0
    psraw     m6, 5
    CLIPW     m6, m1, m3
    mova    [r0], m6
    paddw     m5, m4
    add       r0, r1
    dec r2d
    jg .loop
    REP_RET


;-----------------------------------------------------------------------------
; void pred8x8l_128_dc(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_128_DC 1
cglobal pred8x8l_128_dc_10_%1, 4,4
    mova      m0, [pw_512]
    lea       r1, [r3+r3*2]
    lea       r2, [r0+r3*4]
    MOV8 r0+r3*0, m0, m0
    MOV8 r0+r3*1, m0, m0
    MOV8 r0+r3*2, m0, m0
    MOV8 r0+r1*1, m0, m0
    MOV8 r2+r3*0, m0, m0
    MOV8 r2+r3*1, m0, m0
    MOV8 r2+r3*2, m0, m0
    MOV8 r2+r1*1, m0, m0
    RET
%endmacro

INIT_MMX
PRED8x8L_128_DC mmxext
INIT_XMM
PRED8x8L_128_DC sse2

;-----------------------------------------------------------------------------
; void pred8x8l_top_dc(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_TOP_DC 1
cglobal pred8x8l_top_dc_10_%1, 4,4,6
    sub         r0, r3
    pxor        m7, m7
    mova        m0, [r0-16]
    mova        m3, [r0]
    mova        m1, [r0+16]
    mova        m2, m3
    mova        m4, m3
    PALIGNR     m2, m0, 14, m0
    PALIGNR     m1, m4,  2, m4
    test        r1, r1 ; top_left
    jz .fix_lt_2
    test        r2, r2 ; top_right
    jz .fix_tr_1
    jmp .body
.fix_lt_2:
    mova        m5, m3
    pxor        m5, m2
    pslldq      m5, 14
    psrldq      m5, 14
    pxor        m2, m5
    test        r2, r2 ; top_right
    jnz .body
.fix_tr_1:
    mova        m5, m3
    pxor        m5, m1
    psrldq      m5, 14
    pslldq      m5, 14
    pxor        m1, m5
.body
    lea         r1, [r3+r3*2]
    lea         r2, [r0+r3*4]
    PRED4x4_LOWPASS m0, m2, m1, m3
    HADDW       m0, m1
    paddw       m0, [pw_4]
    psrlw       m0, 3
    SPLATW      m0, m0, 0
    mova [r0+r3*1], m0
    mova [r0+r3*2], m0
    mova [r0+r1*1], m0
    mova [r0+r3*4], m0
    mova [r2+r3*1], m0
    mova [r2+r3*2], m0
    mova [r2+r1*1], m0
    mova [r2+r3*4], m0
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_TOP_DC sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_TOP_DC ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_dc(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
;TODO: see if scalar is faster
%macro PRED8x8L_DC 1
cglobal pred8x8l_dc_10_%1, 4,5,8
    sub         r0, r3
    lea         r4, [r0+r3*2]
    mova        m0, [r0+r3*1-16]
    punpckhwd   m0, [r0+r3*0-16]
    mova        m1, [r4+r3*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    mov         r4, r0
    punpckhdq   m1, m0
    lea         r0, [r0+r3*4]
    mova        m2, [r0+r3*1-16]
    punpckhwd   m2, [r0+r3*0-16]
    lea         r0, [r0+r3*2]
    mova        m3, [r0+r3*1-16]
    punpckhwd   m3, [r0+r3*0-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    lea         r0, [r0+r3*2]
    mova        m0, [r0+r3*0-16]
    mova        m1, [r4]
    mov         r0, r4
    mova        m4, m3
    mova        m2, m3
    PALIGNR     m4, m0, 14, m0
    PALIGNR     m1, m2,  2, m2
    test        r1, r1
    jnz .do_left
.fix_lt_1:
    mova        m5, m3
    pxor        m5, m4
    psrldq      m5, 14
    pslldq      m5, 12
    pxor        m1, m5
    jmp .do_left
.fix_lt_2:
    mova        m5, m3
    pxor        m5, m2
    pslldq      m5, 14
    psrldq      m5, 14
    pxor        m2, m5
    test        r2, r2
    jnz .body
.fix_tr_1:
    mova        m5, m3
    pxor        m5, m1
    psrldq      m5, 14
    pslldq      m5, 14
    pxor        m1, m5
    jmp .body
.do_left:
    mova        m0, m4
    PRED4x4_LOWPASS m2, m1, m4, m3
    mova        m4, m0
    mova        m7, m2
    PRED4x4_LOWPASS m1, m3, m0, m4
    pslldq      m1, 14
    PALIGNR     m7, m1, 14, m3
    mova        m0, [r0-16]
    mova        m3, [r0]
    mova        m1, [r0+16]
    mova        m2, m3
    mova        m4, m3
    PALIGNR     m2, m0, 14, m0
    PALIGNR     m1, m4,  2, m4
    test        r1, r1
    jz .fix_lt_2
    test        r2, r2
    jz .fix_tr_1
.body
    lea         r1, [r3+r3*2]
    PRED4x4_LOWPASS m6, m2, m1, m3
    HADDW       m7, m0
    HADDW       m6, m0
    lea         r2, [r0+r3*4]
    paddw       m7, [pw_8]
    paddw       m7, m6
    psrlw       m7, 4
    SPLATW      m7, m7
    mova [r0+r3*1], m7
    mova [r0+r3*2], m7
    mova [r0+r1*1], m7
    mova [r0+r3*4], m7
    mova [r2+r3*1], m7
    mova [r2+r3*2], m7
    mova [r2+r1*1], m7
    mova [r2+r3*4], m7
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_DC sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_DC ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_vertical(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_VERTICAL 1
cglobal pred8x8l_vertical_10_%1, 4,4,6
    sub         r0, r3
    mova        m0, [r0-16]
    mova        m3, [r0]
    mova        m1, [r0+16]
    mova        m2, m3
    mova        m4, m3
    PALIGNR     m2, m0, 14, m0
    PALIGNR     m1, m4,  2, m4
    test        r1, r1 ; top_left
    jz .fix_lt_2
    test        r2, r2 ; top_right
    jz .fix_tr_1
    jmp .body
.fix_lt_2:
    mova        m5, m3
    pxor        m5, m2
    pslldq      m5, 14
    psrldq      m5, 14
    pxor        m2, m5
    test        r2, r2 ; top_right
    jnz .body
.fix_tr_1:
    mova        m5, m3
    pxor        m5, m1
    psrldq      m5, 14
    pslldq      m5, 14
    pxor        m1, m5
.body
    lea         r1, [r3+r3*2]
    lea         r2, [r0+r3*4]
    PRED4x4_LOWPASS m0, m2, m1, m3
    mova [r0+r3*1], m0
    mova [r0+r3*2], m0
    mova [r0+r1*1], m0
    mova [r0+r3*4], m0
    mova [r2+r3*1], m0
    mova [r2+r3*2], m0
    mova [r2+r1*1], m0
    mova [r2+r3*4], m0
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_VERTICAL sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_VERTICAL ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_horizontal(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_HORIZONTAL 1
cglobal pred8x8l_horizontal_10_%1, 4,4,8
    sub         r0, r3
    lea         r2, [r0+r3*2]
    mova        m0, [r0+r3*1-16]
    test        r1, r1
    lea         r1, [r0+r3]
    cmovnz      r1, r0
    punpckhwd   m0, [r1+r3*0-16]
    mova        m1, [r2+r3*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    mov         r2, r0
    punpckhdq   m1, m0
    lea         r0, [r0+r3*4]
    mova        m2, [r0+r3*1-16]
    punpckhwd   m2, [r0+r3*0-16]
    lea         r0, [r0+r3*2]
    mova        m3, [r0+r3*1-16]
    punpckhwd   m3, [r0+r3*0-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    lea         r0, [r0+r3*2]
    mova        m0, [r0+r3*0-16]
    mova        m1, [r1+r3*0-16]
    mov         r0, r2
    mova        m4, m3
    mova        m2, m3
    PALIGNR     m4, m0, 14, m0
    PALIGNR     m1, m2,  2, m2
    mova        m0, m4
    PRED4x4_LOWPASS m2, m1, m4, m3
    mova        m4, m0
    mova        m7, m2
    PRED4x4_LOWPASS m1, m3, m0, m4
    pslldq      m1, 14
    PALIGNR     m7, m1, 14, m3
    lea         r1, [r3+r3*2]
    punpckhwd   m3, m7, m7
    punpcklwd   m7, m7
    pshufd      m0, m3, 0xff
    pshufd      m1, m3, 0xaa
    lea         r2, [r0+r3*4]
    pshufd      m2, m3, 0x55
    pshufd      m3, m3, 0x00
    pshufd      m4, m7, 0xff
    pshufd      m5, m7, 0xaa
    pshufd      m6, m7, 0x55
    pshufd      m7, m7, 0x00
    mova [r0+r3*1], m0
    mova [r0+r3*2], m1
    mova [r0+r1*1], m2
    mova [r0+r3*4], m3
    mova [r2+r3*1], m4
    mova [r2+r3*2], m5
    mova [r2+r1*1], m6
    mova [r2+r3*4], m7
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_HORIZONTAL sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_HORIZONTAL ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_down_left(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_DOWN_LEFT 1
cglobal pred8x8l_down_left_10_%1, 4,4,8
    sub         r0, r3
    mova        m0, [r0-16]
    mova        m3, [r0]
    mova        m1, [r0+16]
    mova        m2, m3
    mova        m4, m3
    PALIGNR     m2, m0, 14, m0
    PALIGNR     m1, m4,  2, m4
    test        r1, r1
    jz .fix_lt_2
    test        r2, r2
    jz .fix_tr_1
    jmp .do_top
.fix_lt_2:
    mova        m5, m3
    pxor        m5, m2
    pslldq      m5, 14
    psrldq      m5, 14
    pxor        m2, m5
    test        r2, r2
    jnz .do_top
.fix_tr_1:
    mova        m5, m3
    pxor        m5, m1
    psrldq      m5, 14
    pslldq      m5, 14
    pxor        m1, m5
    jmp .do_top
.fix_tr_2:
    punpckhwd   m3, m3
    pshufd      m1, m3, 0xFF
    jmp .do_topright
.do_top:
    PRED4x4_LOWPASS m4, m2, m1, m3
    mova        m7, m4
    test        r2, r2
    jz .fix_tr_2
    mova        m0, [r0+16]
    mova        m5, m0
    mova        m2, m0
    mova        m4, m0
    psrldq      m5, 14
    PALIGNR     m2, m3, 14, m3
    PALIGNR     m5, m4,  2, m4
    PRED4x4_LOWPASS m1, m2, m5, m0
.do_topright:
    lea         r1, [r3+r3*2]
    mova        m6, m1
    psrldq      m1, 14
    mova        m4, m1
    lea         r2, [r0+r3*4]
    mova        m2, m6
    PALIGNR     m2, m7,  2, m0
    mova        m3, m6
    PALIGNR     m3, m7, 14, m0
    PALIGNR     m4, m6,  2, m0
    mova        m5, m7
    mova        m1, m7
    mova        m7, m6
    pslldq      m1, 2
    PRED4x4_LOWPASS m0, m1, m2, m5
    PRED4x4_LOWPASS m1, m3, m4, m7
    mova [r2+r3*4], m1
    mova        m2, m0
    pslldq      m1, 2
    psrldq      m2, 14
    pslldq      m0, 2
    por         m1, m2
    mova [r2+r1*1], m1
    mova        m2, m0
    pslldq      m1, 2
    psrldq      m2, 14
    pslldq      m0, 2
    por         m1, m2
    mova [r2+r3*2], m1
    mova        m2, m0
    pslldq      m1, 2
    psrldq      m2, 14
    pslldq      m0, 2
    por         m1, m2
    mova [r2+r3*1], m1
    mova        m2, m0
    pslldq      m1, 2
    psrldq      m2, 14
    pslldq      m0, 2
    por         m1, m2
    mova [r0+r3*4], m1
    mova        m2, m0
    pslldq      m1, 2
    psrldq      m2, 14
    pslldq      m0, 2
    por         m1, m2
    mova [r0+r1*1], m1
    mova        m2, m0
    pslldq      m1, 2
    psrldq      m2, 14
    pslldq      m0, 2
    por         m1, m2
    mova [r0+r3*2], m1
    pslldq      m1, 2
    psrldq      m0, 14
    por         m1, m0
    mova [r0+r3*1], m1
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_DOWN_LEFT sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_DOWN_LEFT ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_down_right_mxext(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_DOWN_RIGHT 1
cglobal pred8x8l_down_right_10_%1, 4,5,8
    sub         r0, r3
    lea         r4, [r0+r3*2]
    mova        m0, [r0+r3*1-16]
    punpckhwd   m0, [r0+r3*0-16]
    mova        m1, [r4+r3*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    mov         r4, r0
    punpckhdq   m1, m0
    lea         r0, [r0+r3*4]
    mova        m2, [r0+r3*1-16]
    punpckhwd   m2, [r0+r3*0-16]
    lea         r0, [r0+r3*2]
    mova        m3, [r0+r3*1-16]
    punpckhwd   m3, [r0+r3*0-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    lea         r0, [r0+r3*2]
    mova        m0, [r0+r3*0-16]
    mova        m1, [r4]
    mov         r0, r4
    mova        m4, m3
    mova        m2, m3
    PALIGNR     m4, m0, 14, m0
    PALIGNR     m1, m2,  2, m2
    test        r1, r1 ; top_left
    jz .fix_lt_1
.do_left:
    mova        m0, m4
    PRED4x4_LOWPASS m2, m1, m4, m3
    mova        m4, m0
    mova        m7, m2
    mova        m6, m2
    PRED4x4_LOWPASS m1, m3, m0, m4
    pslldq      m1, 14
    PALIGNR     m7, m1, 14, m3
    mova        m0, [r0-16]
    mova        m3, [r0]
    mova        m1, [r0+16]
    mova        m2, m3
    mova        m4, m3
    PALIGNR     m2, m0, 14, m0
    PALIGNR     m1, m4,  2, m4
    test        r1, r1 ; top_left
    jz .fix_lt_2
    test        r2, r2 ; top_right
    jz .fix_tr_1
.do_top:
    PRED4x4_LOWPASS m4, m2, m1, m3
    mova        m5, m4
    jmp .body
.fix_lt_1:
    mova        m5, m3
    pxor        m5, m4
    psrldq      m5, 14
    pslldq      m5, 12
    pxor        m1, m5
    jmp .do_left
.fix_lt_2:
    mova        m5, m3
    pxor        m5, m2
    pslldq      m5, 14
    psrldq      m5, 14
    pxor        m2, m5
    test        r2, r2 ; top_right
    jnz .do_top
.fix_tr_1:
    mova        m5, m3
    pxor        m5, m1
    psrldq      m5, 14
    pslldq      m5, 14
    pxor        m1, m5
    jmp .do_top
.body
    lea         r1, [r3+r3*2]
    mova        m1, m7
    mova        m7, m5
    mova        m5, m6
    mova        m2, m7
    lea         r2, [r0+r3*4]
    PALIGNR     m2, m6,  2, m0
    mova        m3, m7
    PALIGNR     m3, m6, 14, m0
    mova        m4, m7
    psrldq      m4, 2
    PRED4x4_LOWPASS m0, m1, m2, m5
    PRED4x4_LOWPASS m1, m3, m4, m7
    mova [r2+r3*4], m0
    mova        m2, m1
    psrldq      m0, 2
    pslldq      m2, 14
    psrldq      m1, 2
    por         m0, m2
    mova [r2+r1*1], m0
    mova        m2, m1
    psrldq      m0, 2
    pslldq      m2, 14
    psrldq      m1, 2
    por         m0, m2
    mova [r2+r3*2], m0
    mova        m2, m1
    psrldq      m0, 2
    pslldq      m2, 14
    psrldq      m1, 2
    por         m0, m2
    mova [r2+r3*1], m0
    mova        m2, m1
    psrldq      m0, 2
    pslldq      m2, 14
    psrldq      m1, 2
    por         m0, m2
    mova [r0+r3*4], m0
    mova        m2, m1
    psrldq      m0, 2
    pslldq      m2, 14
    psrldq      m1, 2
    por         m0, m2
    mova [r0+r1*1], m0
    mova        m2, m1
    psrldq      m0, 2
    pslldq      m2, 14
    psrldq      m1, 2
    por         m0, m2
    mova [r0+r3*2], m0
    psrldq      m0, 2
    pslldq      m1, 14
    por         m0, m1
    mova [r0+r3*1], m0
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_DOWN_RIGHT sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_DOWN_RIGHT ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_vertical_right(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_VERTICAL_RIGHT 1
cglobal pred8x8l_vertical_right_10_%1, 4,5,8
    sub         r0, r3
    lea         r4, [r0+r3*2]
    mova        m0, [r0+r3*1-16]
    punpckhwd   m0, [r0+r3*0-16]
    mova        m1, [r4+r3*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    mov         r4, r0
    punpckhdq   m1, m0
    lea         r0, [r0+r3*4]
    mova        m2, [r0+r3*1-16]
    punpckhwd   m2, [r0+r3*0-16]
    lea         r0, [r0+r3*2]
    mova        m3, [r0+r3*1-16]
    punpckhwd   m3, [r0+r3*0-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    lea         r0, [r0+r3*2]
    mova        m0, [r0+r3*0-16]
    mova        m1, [r4]
    mov         r0, r4
    mova        m4, m3
    mova        m2, m3
    PALIGNR     m4, m0, 14, m0
    PALIGNR     m1, m2,  2, m2
    test        r1, r1
    jz .fix_lt_1
    jmp .do_left
.fix_lt_1:
    mova        m5, m3
    pxor        m5, m4
    psrldq      m5, 14
    pslldq      m5, 12
    pxor        m1, m5
    jmp .do_left
.fix_lt_2:
    mova        m5, m3
    pxor        m5, m2
    pslldq      m5, 14
    psrldq      m5, 14
    pxor        m2, m5
    test        r2, r2
    jnz .do_top
.fix_tr_1:
    mova        m5, m3
    pxor        m5, m1
    psrldq      m5, 14
    pslldq      m5, 14
    pxor        m1, m5
    jmp .do_top
.do_left:
    mova        m0, m4
    PRED4x4_LOWPASS m2, m1, m4, m3
    mova        m7, m2
    mova        m0, [r0-16]
    mova        m3, [r0]
    mova        m1, [r0+16]
    mova        m2, m3
    mova        m4, m3
    PALIGNR     m2, m0, 14, m0
    PALIGNR     m1, m4,  2, m4
    test        r1, r1
    jz .fix_lt_2
    test        r2, r2
    jz .fix_tr_1
.do_top
    PRED4x4_LOWPASS m6, m2, m1, m3
    lea         r1, [r3+r3*2]
    mova        m2, m6
    mova        m3, m6
    PALIGNR     m3, m7, 14, m0
    PALIGNR     m6, m7, 12, m1
    mova        m4, m3
    pavgw       m3, m2
    lea         r2, [r0+r3*4]
    PRED4x4_LOWPASS m0, m6, m2, m4
    mova [r0+r3*1], m3
    mova [r0+r3*2], m0
    mova        m5, m0
    mova        m6, m3
    mova        m1, m7
    mova        m2, m1
    pslldq      m2, 2
    mova        m3, m1
    pslldq      m3, 4
    PRED4x4_LOWPASS m0, m1, m3, m2
    PALIGNR     m6, m0, 14, m2
    mova [r0+r1*1], m6
    pslldq      m0, 2
    PALIGNR     m5, m0, 14, m1
    mova [r0+r3*4], m5
    pslldq      m0, 2
    PALIGNR     m6, m0, 14, m2
    mova [r2+r3*1], m6
    pslldq      m0, 2
    PALIGNR     m5, m0, 14, m1
    mova [r2+r3*2], m5
    pslldq      m0, 2
    PALIGNR     m6, m0, 14, m2
    mova [r2+r1*1], m6
    pslldq      m0, 2
    PALIGNR     m5, m0, 14, m1
    mova [r2+r3*4], m5
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_VERTICAL_RIGHT sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_VERTICAL_RIGHT ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_horizontal_up(pixel *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_HORIZONTAL_UP 1
cglobal pred8x8l_horizontal_up_10_%1, 4,4,8
    sub         r0, r3
    lea         r2, [r0+r3*2]
    mova        m0, [r0+r3*1-16]
    test        r1, r1
    lea         r1, [r0+r3]
    cmovnz      r1, r0
    punpckhwd   m0, [r1+r3*0-16]
    mova        m1, [r2+r3*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    mov         r2, r0
    punpckhdq   m1, m0
    lea         r0, [r0+r3*4]
    mova        m2, [r0+r3*1-16]
    punpckhwd   m2, [r0+r3*0-16]
    lea         r0, [r0+r3*2]
    mova        m3, [r0+r3*1-16]
    punpckhwd   m3, [r0+r3*0-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    lea         r0, [r0+r3*2]
    mova        m0, [r0+r3*0-16]
    mova        m1, [r1+r3*0-16]
    mov         r0, r2
    mova        m4, m3
    mova        m2, m3
    PALIGNR     m4, m0, 14, m0
    PALIGNR     m1, m2,  2, m2
    mova        m0, m4
    PRED4x4_LOWPASS m2, m1, m4, m3
    mova        m4, m0
    mova        m7, m2
    PRED4x4_LOWPASS m1, m3, m0, m4
    pslldq      m1, 14
    PALIGNR     m7, m1, 14, m3
    lea         r1, [r3+r3*2]
    pshufd      m0, m7, 00011011b ; l6 l7 l4 l5 l2 l3 l0 l1
    pslldq      m7, 14             ; l7 .. .. .. .. .. .. ..
    mova        m2, m0
    pslld       m0, 16
    psrld       m2, 16
    por         m2, m0            ; l7 l6 l5 l4 l3 l2 l1 l0
    mova        m3, m2
    mova        m4, m2
    mova        m5, m2
    psrldq      m2, 2
    psrldq      m3, 4
    lea         r2, [r0+r3*4]
    por         m2, m7            ; l7 l7 l6 l5 l4 l3 l2 l1
    punpckhwd   m7, m7
    por         m3, m7            ; l7 l7 l7 l6 l5 l4 l3 l2
    pavgw       m4, m2
    PRED4x4_LOWPASS m1, m3, m5, m2
    mova        m5, m4
    punpcklwd   m4, m1            ; p4 p3 p2 p1
    punpckhwd   m5, m1            ; p8 p7 p6 p5
    mova        m6, m5
    mova        m7, m5
    mova        m0, m5
    PALIGNR     m5, m4, 4, m1
    pshufd      m1, m6, 11111001b
    PALIGNR     m6, m4, 8, m2
    pshufd      m2, m7, 11111110b
    PALIGNR     m7, m4, 12, m3
    pshufd      m3, m0, 11111111b
    mova [r0+r3*1], m4
    mova [r0+r3*2], m5
    mova [r0+r1*1], m6
    mova [r0+r3*4], m7
    mova [r2+r3*1], m0
    mova [r2+r3*2], m1
    mova [r2+r1*1], m2
    mova [r2+r3*4], m3
    RET
%endmacro

INIT_XMM
%define PALIGNR PALIGNR_MMX
PRED8x8L_HORIZONTAL_UP sse2
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_HORIZONTAL_UP ssse3



;-----------------------------------------------------------------------------
; void pred16x16_vertical(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro MOV16 3-5
    mova [%1+     0], %2
    mova [%1+mmsize], %3
%if mmsize==8
    mova [%1+    16], %4
    mova [%1+    24], %5
%endif
%endmacro

%macro PRED16x16_VERTICAL 1
cglobal pred16x16_vertical_10_%1, 2,3
    sub   r0, r1
    mov   r2, 8
    mova  m0, [r0+ 0]
    mova  m1, [r0+mmsize]
%if mmsize==8
    mova  m2, [r0+16]
    mova  m3, [r0+24]
%endif
.loop:
    MOV16 r0+r1*1, m0, m1, m2, m3
    MOV16 r0+r1*2, m0, m1, m2, m3
    lea   r0, [r0+r1*2]
    dec   r2
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_VERTICAL mmxext
INIT_XMM
PRED16x16_VERTICAL sse2

;-----------------------------------------------------------------------------
; void pred16x16_horizontal(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_HORIZONTAL 1
cglobal pred16x16_horizontal_10_%1, 2,3
    mov    r2, 8
.vloop:
    movd   m0, [r0+r1*0-4]
    movd   m1, [r0+r1*1-4]
    SPLATW m0, m0, 1
    SPLATW m1, m1, 1
    MOV16  r0+r1*0, m0, m0, m0, m0
    MOV16  r0+r1*1, m1, m1, m1, m1
    lea    r0, [r0+r1*2]
    dec    r2
    jg .vloop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_HORIZONTAL mmxext
INIT_XMM
PRED16x16_HORIZONTAL sse2

;-----------------------------------------------------------------------------
; void pred16x16_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_DC 1
cglobal pred16x16_dc_10_%1, 2,7
    mov        r4, r0
    sub        r0, r1
    mova       m0, [r0+0]
    paddw      m0, [r0+mmsize]
%if mmsize==8
    paddw      m0, [r0+16]
    paddw      m0, [r0+24]
%endif
    HADDW      m0, m2

    sub        r0, 2
    movzx     r3d, word [r0+r1*1]
    movzx     r5d, word [r0+r1*2]
%rep 7
    lea        r0, [r0+r1*2]
    movzx     r2d, word [r0+r1*1]
    add       r3d, r2d
    movzx     r2d, word [r0+r1*2]
    add       r5d, r2d
%endrep
    lea       r3d, [r3+r5+16]

    movd       m1, r3d
    paddw      m0, m1
    psrlw      m0, 5
    SPLATW     m0, m0
    mov       r3d, 8
.loop:
    MOV16 r4+r1*0, m0, m0, m0, m0
    MOV16 r4+r1*1, m0, m0, m0, m0
    lea        r4, [r4+r1*2]
    dec       r3d
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_DC mmxext
INIT_XMM
PRED16x16_DC sse2

;-----------------------------------------------------------------------------
; void pred16x16_top_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_TOP_DC 1
cglobal pred16x16_top_dc_10_%1, 2,3
    sub        r0, r1
    mova       m0, [r0+0]
    paddw      m0, [r0+mmsize]
%if mmsize==8
    paddw      m0, [r0+16]
    paddw      m0, [r0+24]
%endif
    HADDW      m0, m2

    SPLATW     m0, m0
    paddw      m0, [pw_8]
    psrlw      m0, 4
    mov       r2d, 8
.loop:
    MOV16 r0+r1*1, m0, m0, m0, m0
    MOV16 r0+r1*2, m0, m0, m0, m0
    lea        r0, [r0+r1*2]
    dec       r2d
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_TOP_DC mmxext
INIT_XMM
PRED16x16_TOP_DC sse2

;-----------------------------------------------------------------------------
; void pred16x16_left_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_LEFT_DC 1
cglobal pred16x16_left_dc_10_%1, 2,7
    mov        r4, r0

    sub        r0, 2
    movzx     r5d, word [r0+r1*0]
    movzx     r6d, word [r0+r1*1]
%rep 7
    lea        r0, [r0+r1*2]
    movzx     r2d, word [r0+r1*0]
    movzx     r3d, word [r0+r1*1]
    add       r5d, r2d
    add       r6d, r3d
%endrep
    lea       r2d, [r5+r6+8]
    shr       r2d, 4

    movd       m0, r2d
    SPLATW     m0, m0
    mov       r3d, 8
.loop:
    MOV16 r4+r1*0, m0, m0, m0, m0
    MOV16 r4+r1*1, m0, m0, m0, m0
    lea        r4, [r4+r1*2]
    dec       r3d
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_LEFT_DC mmxext
INIT_XMM
PRED16x16_LEFT_DC sse2

;-----------------------------------------------------------------------------
; void pred16x16_128_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_128_DC 1
cglobal pred16x16_128_dc_10_%1, 2,3
    mova       m0, [pw_512]
    mov       r2d, 8
.loop:
    MOV16 r0+r1*0, m0, m0, m0, m0
    MOV16 r0+r1*1, m0, m0, m0, m0
    lea        r0, [r0+r1*2]
    dec       r2d
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_128_DC mmxext
INIT_XMM
PRED16x16_128_DC sse2
