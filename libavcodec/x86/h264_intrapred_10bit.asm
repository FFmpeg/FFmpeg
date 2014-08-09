;*****************************************************************************
;* MMX/SSE2/AVX-optimized 10-bit H.264 intra prediction code
;*****************************************************************************
;* Copyright (C) 2005-2011 x264 project
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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

cextern pw_512
cextern pw_16
cextern pw_8
cextern pw_4
cextern pw_2
cextern pw_1

pw_m32101234: dw -3, -2, -1, 0, 1, 2, 3, 4
pw_m3:        times 8 dw -3
pw_pixel_max: times 8 dw ((1 << 10)-1)
pd_17:        times 4 dd 17
pd_16:        times 4 dd 16

SECTION .text

; dest, left, right, src
; output: %1 = (t[n-1] + t[n]*2 + t[n+1] + 2) >> 2
%macro PRED4x4_LOWPASS 4
    paddw       %2, %3
    psrlw       %2, 1
    pavgw       %1, %4, %2
%endmacro

;-----------------------------------------------------------------------------
; void ff_pred4x4_down_right(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_DR 0
cglobal pred4x4_down_right_10, 3, 3
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movhps    m1, [r1-8]
    movhps    m2, [r0+r2*1-8]
    movhps    m4, [r0-8]
    punpckhwd m2, m4
    movq      m3, [r0]
    punpckhdq m1, m2
    PALIGNR   m3, m1, 10, m1
    movhps    m4, [r1+r2*1-8]
    PALIGNR   m0, m3, m4, 14, m4
    movhps    m4, [r1+r2*2-8]
    PALIGNR   m2, m0, m4, 14, m4
    PRED4x4_LOWPASS m0, m2, m3, m0
    movq      [r1+r2*2], m0
    psrldq    m0, 2
    movq      [r1+r2*1], m0
    psrldq    m0, 2
    movq      [r0+r2*2], m0
    psrldq    m0, 2
    movq      [r0+r2*1], m0
    RET
%endmacro

INIT_XMM sse2
PRED4x4_DR
INIT_XMM ssse3
PRED4x4_DR
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED4x4_DR
%endif

;------------------------------------------------------------------------------
; void ff_pred4x4_vertical_right(pixel *src, const pixel *topright, int stride)
;------------------------------------------------------------------------------
%macro PRED4x4_VR 0
cglobal pred4x4_vertical_right_10, 3, 3, 6
    sub     r0, r2
    lea     r1, [r0+r2*2]
    movq    m5, [r0]            ; ........t3t2t1t0
    movhps  m1, [r0-8]
    PALIGNR m0, m5, m1, 14, m1  ; ......t3t2t1t0lt
    pavgw   m5, m0
    movhps  m1, [r0+r2*1-8]
    PALIGNR m0, m1, 14, m1      ; ....t3t2t1t0ltl0
    movhps  m2, [r0+r2*2-8]
    PALIGNR m1, m0, m2, 14, m2  ; ..t3t2t1t0ltl0l1
    movhps  m3, [r1+r2*1-8]
    PALIGNR m2, m1, m3, 14, m3  ; t3t2t1t0ltl0l1l2
    PRED4x4_LOWPASS m1, m0, m2, m1
    pslldq  m0, m1, 12
    psrldq  m1, 4
    movq    [r0+r2*1], m5
    movq    [r0+r2*2], m1
    PALIGNR m5, m0, 14, m2
    pslldq  m0, 2
    movq    [r1+r2*1], m5
    PALIGNR m1, m0, 14, m0
    movq    [r1+r2*2], m1
    RET
%endmacro

INIT_XMM sse2
PRED4x4_VR
INIT_XMM ssse3
PRED4x4_VR
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED4x4_VR
%endif

;-------------------------------------------------------------------------------
; void ff_pred4x4_horizontal_down(pixel *src, const pixel *topright, int stride)
;-------------------------------------------------------------------------------
%macro PRED4x4_HD 0
cglobal pred4x4_horizontal_down_10, 3, 3
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
    psrldq     m3, m1, 2       ; .. t2 t1 t0 lt l0 l1 l2
    pavgw      m5, m1, m3
    PRED4x4_LOWPASS m3, m1, m0, m3
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

INIT_XMM sse2
PRED4x4_HD
INIT_XMM ssse3
PRED4x4_HD
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED4x4_HD
%endif

;-----------------------------------------------------------------------------
; void ff_pred4x4_dc(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX mmxext
cglobal pred4x4_dc_10, 3, 3
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
; void ff_pred4x4_down_left(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_DL 0
cglobal pred4x4_down_left_10, 3, 3
    sub        r0, r2
    movq       m0, [r0]
    movhps     m0, [r1]
    psrldq     m2, m0, 2
    pslldq     m3, m0, 2
    pshufhw    m2, m2, 10100100b
    PRED4x4_LOWPASS m0, m3, m2, m0
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

INIT_XMM sse2
PRED4x4_DL
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED4x4_DL
%endif

;-----------------------------------------------------------------------------
; void ff_pred4x4_vertical_left(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED4x4_VL 0
cglobal pred4x4_vertical_left_10, 3, 3
    sub        r0, r2
    movu       m1, [r0]
    movhps     m1, [r1]
    psrldq     m0, m1, 2
    psrldq     m2, m1, 4
    pavgw      m4, m0, m1
    PRED4x4_LOWPASS m0, m1, m2, m0
    lea        r1, [r0+r2*2]
    movq       [r0+r2*1], m4
    movq       [r0+r2*2], m0
    psrldq     m4, 2
    psrldq     m0, 2
    movq       [r1+r2*1], m4
    movq       [r1+r2*2], m0
    RET
%endmacro

INIT_XMM sse2
PRED4x4_VL
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED4x4_VL
%endif

;-----------------------------------------------------------------------------
; void ff_pred4x4_horizontal_up(pixel *src, const pixel *topright, int stride)
;-----------------------------------------------------------------------------
INIT_MMX mmxext
cglobal pred4x4_horizontal_up_10, 3, 3
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
    PRED4x4_LOWPASS m1, m0, m5, m1
    movq      m6, m2
    punpcklwd m6, m1
    movq      [r0+r2*1], m6
    psrlq     m2, 16
    psrlq     m1, 16
    punpcklwd m2, m1
    movq      [r0+r2*2], m2
    psrlq     m2, 32
    movd      [r1+r2*1], m2
    RET



;-----------------------------------------------------------------------------
; void ff_pred8x8_vertical(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred8x8_vertical_10, 2, 2
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
; void ff_pred8x8_horizontal(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred8x8_horizontal_10, 2, 3
    mov         r2d, 4
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
    dec          r2d
    jg .loop
    REP_RET

;-----------------------------------------------------------------------------
; void ff_predict_8x8_dc(pixel *src, int stride)
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

%macro PRED8x8_DC 1
cglobal pred8x8_dc_10, 2, 6
    sub         r0, r1
    pxor        m4, m4
    movq        m0, [r0+0]
    movq        m1, [r0+8]
%if mmsize==16
    punpcklwd   m0, m1
    movhlps     m1, m0
    paddw       m0, m1
%else
    pshufw      m2, m0, 00001110b
    pshufw      m3, m1, 00001110b
    paddw       m0, m2
    paddw       m1, m3
    punpcklwd   m0, m1
%endif
    %1          m2, m0, 00001110b
    paddw       m0, m2

    lea         r5, [r1*3]
    lea         r4, [r0+r1*4]
    movzx      r2d, word [r0+r1*1-2]
    movzx      r3d, word [r0+r1*2-2]
    add        r2d, r3d
    movzx      r3d, word [r0+r5*1-2]
    add        r2d, r3d
    movzx      r3d, word [r4-2]
    add        r2d, r3d
    movd        m2, r2d            ; s2

    movzx      r2d, word [r4+r1*1-2]
    movzx      r3d, word [r4+r1*2-2]
    add        r2d, r3d
    movzx      r3d, word [r4+r5*1-2]
    add        r2d, r3d
    movzx      r3d, word [r4+r1*4-2]
    add        r2d, r3d
    movd        m3, r2d            ; s3

    punpcklwd   m2, m3
    punpckldq   m0, m2            ; s0, s1, s2, s3
    %1          m3, m0, 11110110b ; s2, s1, s3, s3
    %1          m0, m0, 01110100b ; s0, s1, s3, s1
    paddw       m0, m3
    psrlw       m0, 2
    pavgw       m0, m4            ; s0+s2, s1, s3, s1+s3
%if mmsize==16
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
    MOV8   r0+r5*1, m1, m2
    MOV8   r0+r1*4, m1, m2
    MOV8   r4+r1*1, m3, m4
    MOV8   r4+r1*2, m3, m4
    MOV8   r4+r5*1, m3, m4
    MOV8   r4+r1*4, m3, m4
    RET
%endmacro

INIT_MMX mmxext
PRED8x8_DC pshufw
INIT_XMM sse2
PRED8x8_DC pshuflw

;-----------------------------------------------------------------------------
; void ff_pred8x8_top_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred8x8_top_dc_10, 2, 4
    sub         r0, r1
    mova        m0, [r0]
    pshuflw     m1, m0, 0x4e
    pshufhw     m1, m1, 0x4e
    paddw       m0, m1
    pshuflw     m1, m0, 0xb1
    pshufhw     m1, m1, 0xb1
    paddw       m0, m1
    lea         r2, [r1*3]
    lea         r3, [r0+r1*4]
    paddw       m0, [pw_2]
    psrlw       m0, 2
    mova [r0+r1*1], m0
    mova [r0+r1*2], m0
    mova [r0+r2*1], m0
    mova [r0+r1*4], m0
    mova [r3+r1*1], m0
    mova [r3+r1*2], m0
    mova [r3+r2*1], m0
    mova [r3+r1*4], m0
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8_plane(pixel *src, int stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred8x8_plane_10, 2, 7, 7
    sub       r0, r1
    lea       r2, [r1*3]
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
    lea      r5d, [r5*3]
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
; void ff_pred8x8l_128_dc(pixel *src, int has_topleft, int has_topright,
;                         int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_128_DC 0
cglobal pred8x8l_128_dc_10, 4, 4
    mova      m0, [pw_512] ; (1<<(BIT_DEPTH-1))
    lea       r1, [r3*3]
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

INIT_MMX mmxext
PRED8x8L_128_DC
INIT_XMM sse2
PRED8x8L_128_DC

;-----------------------------------------------------------------------------
; void ff_pred8x8l_top_dc(pixel *src, int has_topleft, int has_topright,
;                         int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_TOP_DC 0
cglobal pred8x8l_top_dc_10, 4, 4, 6
    sub         r0, r3
    mova        m0, [r0]
    shr        r1d, 14
    shr        r2d, 13
    neg         r1
    pslldq      m1, m0, 2
    psrldq      m2, m0, 2
    pinsrw      m1, [r0+r1], 0
    pinsrw      m2, [r0+r2+14], 7
    lea         r1, [r3*3]
    lea         r2, [r0+r3*4]
    PRED4x4_LOWPASS m0, m2, m1, m0
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

INIT_XMM sse2
PRED8x8L_TOP_DC
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_TOP_DC
%endif

;-------------------------------------------------------------------------------
; void ff_pred8x8l_dc(pixel *src, int has_topleft, int has_topright, int stride)
;-------------------------------------------------------------------------------
;TODO: see if scalar is faster
%macro PRED8x8L_DC 0
cglobal pred8x8l_dc_10, 4, 6, 6
    sub         r0, r3
    lea         r4, [r0+r3*4]
    lea         r5, [r3*3]
    mova        m0, [r0+r3*2-16]
    punpckhwd   m0, [r0+r3*1-16]
    mova        m1, [r4+r3*0-16]
    punpckhwd   m1, [r0+r5*1-16]
    punpckhdq   m1, m0
    mova        m2, [r4+r3*2-16]
    punpckhwd   m2, [r4+r3*1-16]
    mova        m3, [r4+r3*4-16]
    punpckhwd   m3, [r4+r5*1-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    mova        m0, [r0]
    shr        r1d, 14
    shr        r2d, 13
    neg         r1
    pslldq      m1, m0, 2
    psrldq      m2, m0, 2
    pinsrw      m1, [r0+r1], 0
    pinsrw      m2, [r0+r2+14], 7
    not         r1
    and         r1, r3
    pslldq      m4, m3, 2
    psrldq      m5, m3, 2
    pshuflw     m4, m4, 11100101b
    pinsrw      m5, [r0+r1-2], 7
    PRED4x4_LOWPASS m3, m4, m5, m3
    PRED4x4_LOWPASS m0, m2, m1, m0
    paddw       m0, m3
    HADDW       m0, m1
    paddw       m0, [pw_8]
    psrlw       m0, 4
    SPLATW      m0, m0
    mova [r0+r3*1], m0
    mova [r0+r3*2], m0
    mova [r0+r5*1], m0
    mova [r0+r3*4], m0
    mova [r4+r3*1], m0
    mova [r4+r3*2], m0
    mova [r4+r5*1], m0
    mova [r4+r3*4], m0
    RET
%endmacro

INIT_XMM sse2
PRED8x8L_DC
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_DC
%endif

;-----------------------------------------------------------------------------
; void ff_pred8x8l_vertical(pixel *src, int has_topleft, int has_topright,
;                           int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_VERTICAL 0
cglobal pred8x8l_vertical_10, 4, 4, 6
    sub         r0, r3
    mova        m0, [r0]
    shr        r1d, 14
    shr        r2d, 13
    neg         r1
    pslldq      m1, m0, 2
    psrldq      m2, m0, 2
    pinsrw      m1, [r0+r1], 0
    pinsrw      m2, [r0+r2+14], 7
    lea         r1, [r3*3]
    lea         r2, [r0+r3*4]
    PRED4x4_LOWPASS m0, m2, m1, m0
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

INIT_XMM sse2
PRED8x8L_VERTICAL
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_VERTICAL
%endif

;-----------------------------------------------------------------------------
; void ff_pred8x8l_horizontal(uint8_t *src, int has_topleft, int has_topright,
;                             int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_HORIZONTAL 0
cglobal pred8x8l_horizontal_10, 4, 4, 5
    mova        m0, [r0-16]
    shr        r1d, 14
    dec         r1
    and         r1, r3
    sub         r1, r3
    punpckhwd   m0, [r0+r1-16]
    mova        m1, [r0+r3*2-16]
    punpckhwd   m1, [r0+r3*1-16]
    lea         r2, [r0+r3*4]
    lea         r1, [r3*3]
    punpckhdq   m1, m0
    mova        m2, [r2+r3*0-16]
    punpckhwd   m2, [r0+r1-16]
    mova        m3, [r2+r3*2-16]
    punpckhwd   m3, [r2+r3*1-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    PALIGNR     m4, m3, [r2+r1-16], 14, m0
    pslldq      m0, m4, 2
    pshuflw     m0, m0, 11100101b
    PRED4x4_LOWPASS m4, m3, m0, m4
    punpckhwd   m3, m4, m4
    punpcklwd   m4, m4
    pshufd      m0, m3, 0xff
    pshufd      m1, m3, 0xaa
    pshufd      m2, m3, 0x55
    pshufd      m3, m3, 0x00
    mova [r0+r3*0], m0
    mova [r0+r3*1], m1
    mova [r0+r3*2], m2
    mova [r0+r1*1], m3
    pshufd      m0, m4, 0xff
    pshufd      m1, m4, 0xaa
    pshufd      m2, m4, 0x55
    pshufd      m3, m4, 0x00
    mova [r2+r3*0], m0
    mova [r2+r3*1], m1
    mova [r2+r3*2], m2
    mova [r2+r1*1], m3
    RET
%endmacro

INIT_XMM sse2
PRED8x8L_HORIZONTAL
INIT_XMM ssse3
PRED8x8L_HORIZONTAL
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_HORIZONTAL
%endif

;-----------------------------------------------------------------------------
; void ff_pred8x8l_down_left(pixel *src, int has_topleft, int has_topright,
;                            int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_DOWN_LEFT 0
cglobal pred8x8l_down_left_10, 4, 4, 7
    sub         r0, r3
    mova        m3, [r0]
    shr        r1d, 14
    neg         r1
    shr        r2d, 13
    pslldq      m1, m3, 2
    psrldq      m2, m3, 2
    pinsrw      m1, [r0+r1], 0
    pinsrw      m2, [r0+r2+14], 7
    PRED4x4_LOWPASS m6, m2, m1, m3
    jz .fix_tr ; flags from shr r2d
    mova        m1, [r0+16]
    psrldq      m5, m1, 2
    PALIGNR     m2, m1, m3, 14, m3
    pshufhw     m5, m5, 10100100b
    PRED4x4_LOWPASS m1, m2, m5, m1
.do_topright:
    lea         r1, [r3*3]
    psrldq      m5, m1, 14
    lea         r2, [r0+r3*4]
    PALIGNR     m2, m1, m6,  2, m0
    PALIGNR     m3, m1, m6, 14, m0
    PALIGNR     m5, m1,  2, m0
    pslldq      m4, m6, 2
    PRED4x4_LOWPASS m6, m4, m2, m6
    PRED4x4_LOWPASS m1, m3, m5, m1
    mova [r2+r3*4], m1
    PALIGNR     m1, m6, 14, m2
    pslldq      m6, 2
    mova [r2+r1*1], m1
    PALIGNR     m1, m6, 14, m2
    pslldq      m6, 2
    mova [r2+r3*2], m1
    PALIGNR     m1, m6, 14, m2
    pslldq      m6, 2
    mova [r2+r3*1], m1
    PALIGNR     m1, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r3*4], m1
    PALIGNR     m1, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r1*1], m1
    PALIGNR     m1, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r3*2], m1
    PALIGNR     m1, m6, 14, m6
    mova [r0+r3*1], m1
    RET
.fix_tr:
    punpckhwd   m3, m3
    pshufd      m1, m3, 0xFF
    jmp .do_topright
%endmacro

INIT_XMM sse2
PRED8x8L_DOWN_LEFT
INIT_XMM ssse3
PRED8x8L_DOWN_LEFT
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_DOWN_LEFT
%endif

;-----------------------------------------------------------------------------
; void ff_pred8x8l_down_right(pixel *src, int has_topleft, int has_topright,
;                             int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_DOWN_RIGHT 0
; standard forbids this when has_topleft is false
; no need to check
cglobal pred8x8l_down_right_10, 4, 5, 8
    sub         r0, r3
    lea         r4, [r0+r3*4]
    lea         r1, [r3*3]
    mova        m0, [r0+r3*1-16]
    punpckhwd   m0, [r0+r3*0-16]
    mova        m1, [r0+r1*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    punpckhdq   m1, m0
    mova        m2, [r4+r3*1-16]
    punpckhwd   m2, [r4+r3*0-16]
    mova        m3, [r4+r1*1-16]
    punpckhwd   m3, [r4+r3*2-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    mova        m0, [r4+r3*4-16]
    mova        m1, [r0]
    PALIGNR     m4, m3, m0, 14, m0
    PALIGNR     m1, m3,  2, m2
    pslldq      m0, m4, 2
    pshuflw     m0, m0, 11100101b
    PRED4x4_LOWPASS m6, m1, m4, m3
    PRED4x4_LOWPASS m4, m3, m0, m4
    mova        m3, [r0]
    shr        r2d, 13
    pslldq      m1, m3, 2
    psrldq      m2, m3, 2
    pinsrw      m1, [r0-2], 0
    pinsrw      m2, [r0+r2+14], 7
    PRED4x4_LOWPASS m3, m2, m1, m3
    PALIGNR     m2, m3, m6,  2, m0
    PALIGNR     m5, m3, m6, 14, m0
    psrldq      m7, m3, 2
    PRED4x4_LOWPASS m6, m4, m2, m6
    PRED4x4_LOWPASS m3, m5, m7, m3
    mova [r4+r3*4], m6
    PALIGNR     m3, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r3*1], m3
    PALIGNR     m3, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r3*2], m3
    PALIGNR     m3, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r1*1], m3
    PALIGNR     m3, m6, 14, m2
    pslldq      m6, 2
    mova [r0+r3*4], m3
    PALIGNR     m3, m6, 14, m2
    pslldq      m6, 2
    mova [r4+r3*1], m3
    PALIGNR     m3, m6, 14, m2
    pslldq      m6, 2
    mova [r4+r3*2], m3
    PALIGNR     m3, m6, 14, m6
    mova [r4+r1*1], m3
    RET
%endmacro

INIT_XMM sse2
PRED8x8L_DOWN_RIGHT
INIT_XMM ssse3
PRED8x8L_DOWN_RIGHT
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_DOWN_RIGHT
%endif

;-----------------------------------------------------------------------------
; void ff_pred8x8l_vertical_right(pixel *src, int has_topleft,
;                                 int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_VERTICAL_RIGHT 0
; likewise with 8x8l_down_right
cglobal pred8x8l_vertical_right_10, 4, 5, 7
    sub         r0, r3
    lea         r4, [r0+r3*4]
    lea         r1, [r3*3]
    mova        m0, [r0+r3*1-16]
    punpckhwd   m0, [r0+r3*0-16]
    mova        m1, [r0+r1*1-16]
    punpckhwd   m1, [r0+r3*2-16]
    punpckhdq   m1, m0
    mova        m2, [r4+r3*1-16]
    punpckhwd   m2, [r4+r3*0-16]
    mova        m3, [r4+r1*1-16]
    punpckhwd   m3, [r4+r3*2-16]
    punpckhdq   m3, m2
    punpckhqdq  m3, m1
    mova        m0, [r4+r3*4-16]
    mova        m1, [r0]
    PALIGNR     m4, m3, m0, 14, m0
    PALIGNR     m1, m3,  2, m2
    PRED4x4_LOWPASS m3, m1, m4, m3
    mova        m2, [r0]
    shr        r2d, 13
    pslldq      m1, m2, 2
    psrldq      m5, m2, 2
    pinsrw      m1, [r0-2], 0
    pinsrw      m5, [r0+r2+14], 7
    PRED4x4_LOWPASS m2, m5, m1, m2
    PALIGNR     m6, m2, m3, 12, m1
    PALIGNR     m5, m2, m3, 14, m0
    PRED4x4_LOWPASS m0, m6, m2, m5
    pavgw       m2, m5
    mova [r0+r3*2], m0
    mova [r0+r3*1], m2
    pslldq      m6, m3, 4
    pslldq      m1, m3, 2
    PRED4x4_LOWPASS m1, m3, m6, m1
    PALIGNR     m2, m1, 14, m4
    mova [r0+r1*1], m2
    pslldq      m1, 2
    PALIGNR     m0, m1, 14, m3
    mova [r0+r3*4], m0
    pslldq      m1, 2
    PALIGNR     m2, m1, 14, m4
    mova [r4+r3*1], m2
    pslldq      m1, 2
    PALIGNR     m0, m1, 14, m3
    mova [r4+r3*2], m0
    pslldq      m1, 2
    PALIGNR     m2, m1, 14, m4
    mova [r4+r1*1], m2
    pslldq      m1, 2
    PALIGNR     m0, m1, 14, m1
    mova [r4+r3*4], m0
    RET
%endmacro

INIT_XMM sse2
PRED8x8L_VERTICAL_RIGHT
INIT_XMM ssse3
PRED8x8L_VERTICAL_RIGHT
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_VERTICAL_RIGHT
%endif

;-----------------------------------------------------------------------------
; void ff_pred8x8l_horizontal_up(pixel *src, int has_topleft,
;                                int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_HORIZONTAL_UP 0
cglobal pred8x8l_horizontal_up_10, 4, 4, 6
    mova        m0, [r0+r3*0-16]
    punpckhwd   m0, [r0+r3*1-16]
    shr        r1d, 14
    dec         r1
    and         r1, r3
    sub         r1, r3
    mova        m4, [r0+r1*1-16]
    lea         r1, [r3*3]
    lea         r2, [r0+r3*4]
    mova        m1, [r0+r3*2-16]
    punpckhwd   m1, [r0+r1*1-16]
    punpckhdq   m0, m1
    mova        m2, [r2+r3*0-16]
    punpckhwd   m2, [r2+r3*1-16]
    mova        m3, [r2+r3*2-16]
    punpckhwd   m3, [r2+r1*1-16]
    punpckhdq   m2, m3
    punpckhqdq  m0, m2
    PALIGNR     m1, m0, m4, 14, m4
    psrldq      m2, m0, 2
    pshufhw     m2, m2, 10100100b
    PRED4x4_LOWPASS m0, m1, m2, m0
    psrldq      m1, m0, 2
    psrldq      m2, m0, 4
    pshufhw     m1, m1, 10100100b
    pshufhw     m2, m2, 01010100b
    pavgw       m4, m0, m1
    PRED4x4_LOWPASS m1, m2, m0, m1
    punpckhwd   m5, m4, m1
    punpcklwd   m4, m1
    mova [r2+r3*0], m5
    mova [r0+r3*0], m4
    pshufd      m0, m5, 11111001b
    pshufd      m1, m5, 11111110b
    pshufd      m2, m5, 11111111b
    mova [r2+r3*1], m0
    mova [r2+r3*2], m1
    mova [r2+r1*1], m2
    PALIGNR     m2, m5, m4, 4, m0
    PALIGNR     m3, m5, m4, 8, m1
    PALIGNR     m5, m5, m4, 12, m4
    mova [r0+r3*1], m2
    mova [r0+r3*2], m3
    mova [r0+r1*1], m5
    RET
%endmacro

INIT_XMM sse2
PRED8x8L_HORIZONTAL_UP
INIT_XMM ssse3
PRED8x8L_HORIZONTAL_UP
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PRED8x8L_HORIZONTAL_UP
%endif


;-----------------------------------------------------------------------------
; void ff_pred16x16_vertical(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro MOV16 3-5
    mova [%1+     0], %2
    mova [%1+mmsize], %3
%if mmsize==8
    mova [%1+    16], %4
    mova [%1+    24], %5
%endif
%endmacro

%macro PRED16x16_VERTICAL 0
cglobal pred16x16_vertical_10, 2, 3
    sub   r0, r1
    mov  r2d, 8
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
    dec   r2d
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PRED16x16_VERTICAL
INIT_XMM sse2
PRED16x16_VERTICAL

;-----------------------------------------------------------------------------
; void ff_pred16x16_horizontal(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_HORIZONTAL 0
cglobal pred16x16_horizontal_10, 2, 3
    mov   r2d, 8
.vloop:
    movd   m0, [r0+r1*0-4]
    movd   m1, [r0+r1*1-4]
    SPLATW m0, m0, 1
    SPLATW m1, m1, 1
    MOV16  r0+r1*0, m0, m0, m0, m0
    MOV16  r0+r1*1, m1, m1, m1, m1
    lea    r0, [r0+r1*2]
    dec    r2d
    jg .vloop
    REP_RET
%endmacro

INIT_MMX mmxext
PRED16x16_HORIZONTAL
INIT_XMM sse2
PRED16x16_HORIZONTAL

;-----------------------------------------------------------------------------
; void ff_pred16x16_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_DC 0
cglobal pred16x16_dc_10, 2, 6
    mov        r5, r0
    sub        r0, r1
    mova       m0, [r0+0]
    paddw      m0, [r0+mmsize]
%if mmsize==8
    paddw      m0, [r0+16]
    paddw      m0, [r0+24]
%endif
    HADDW      m0, m2

    lea        r0, [r0+r1-2]
    movzx     r3d, word [r0]
    movzx     r4d, word [r0+r1]
%rep 7
    lea        r0, [r0+r1*2]
    movzx     r2d, word [r0]
    add       r3d, r2d
    movzx     r2d, word [r0+r1]
    add       r4d, r2d
%endrep
    lea       r3d, [r3+r4+16]

    movd       m1, r3d
    paddw      m0, m1
    psrlw      m0, 5
    SPLATW     m0, m0
    mov       r3d, 8
.loop:
    MOV16 r5+r1*0, m0, m0, m0, m0
    MOV16 r5+r1*1, m0, m0, m0, m0
    lea        r5, [r5+r1*2]
    dec       r3d
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PRED16x16_DC
INIT_XMM sse2
PRED16x16_DC

;-----------------------------------------------------------------------------
; void ff_pred16x16_top_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_TOP_DC 0
cglobal pred16x16_top_dc_10, 2, 3
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

INIT_MMX mmxext
PRED16x16_TOP_DC
INIT_XMM sse2
PRED16x16_TOP_DC

;-----------------------------------------------------------------------------
; void ff_pred16x16_left_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_LEFT_DC 0
cglobal pred16x16_left_dc_10, 2, 6
    mov        r5, r0

    sub        r0, 2
    movzx     r3d, word [r0]
    movzx     r4d, word [r0+r1]
%rep 7
    lea        r0, [r0+r1*2]
    movzx     r2d, word [r0]
    add       r3d, r2d
    movzx     r2d, word [r0+r1]
    add       r4d, r2d
%endrep
    lea       r3d, [r3+r4+8]
    shr       r3d, 4

    movd       m0, r3d
    SPLATW     m0, m0
    mov       r3d, 8
.loop:
    MOV16 r5+r1*0, m0, m0, m0, m0
    MOV16 r5+r1*1, m0, m0, m0, m0
    lea        r5, [r5+r1*2]
    dec       r3d
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PRED16x16_LEFT_DC
INIT_XMM sse2
PRED16x16_LEFT_DC

;-----------------------------------------------------------------------------
; void ff_pred16x16_128_dc(pixel *src, int stride)
;-----------------------------------------------------------------------------
%macro PRED16x16_128_DC 0
cglobal pred16x16_128_dc_10, 2,3
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

INIT_MMX mmxext
PRED16x16_128_DC
INIT_XMM sse2
PRED16x16_128_DC
