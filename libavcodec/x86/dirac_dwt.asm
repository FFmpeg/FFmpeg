;******************************************************************************
;* x86 optimized discrete wavelet trasnform
;* Copyright (c) 2010 David Conrad
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
pw_1991: times 4 dw 9,-1

cextern pw_1
cextern pw_2
cextern pw_8
cextern pw_16

SECTION .text

; %1 -= (%2 + %3 + 2)>>2     %4 is pw_2
%macro COMPOSE_53iL0 4
    paddw   %2, %3
    paddw   %2, %4
    psraw   %2, 2
    psubw   %1, %2
%endm

; m1 = %1 + (-m0 + 9*m1 + 9*%2 -%3 + 8)>>4
; if %4 is supplied, %1 is loaded unaligned from there
; m2: clobbered  m3: pw_8  m4: pw_1991
%macro COMPOSE_DD97iH0 3-4
    paddw   m0, %3
    paddw   m1, %2
    psubw   m0, m3
    mova    m2, m1
    punpcklwd m1, m0
    punpckhwd m2, m0
    pmaddwd m1, m4
    pmaddwd m2, m4
%if %0 > 3
    movu    %1, %4
%endif
    psrad   m1, 4
    psrad   m2, 4
    packssdw m1, m2
    paddw   m1, %1
%endm

%macro COMPOSE_VERTICAL 1
; void vertical_compose53iL0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2,
;                                  int width)
cglobal vertical_compose53iL0_%1, 4,4,1, b0, b1, b2, width
    mova    m2, [pw_2]
%if ARCH_X86_64
    mov     widthd, widthd
%endif
.loop:
    sub     widthq, mmsize/2
    mova    m1, [b0q+2*widthq]
    mova    m0, [b1q+2*widthq]
    COMPOSE_53iL0 m0, m1, [b2q+2*widthq], m2
    mova    [b1q+2*widthq], m0
    jg      .loop
    REP_RET

; void vertical_compose_dirac53iH0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2,
;                                  int width)
cglobal vertical_compose_dirac53iH0_%1, 4,4,1, b0, b1, b2, width
    mova    m1, [pw_1]
%if ARCH_X86_64
    mov     widthd, widthd
%endif
.loop:
    sub     widthq, mmsize/2
    mova    m0, [b0q+2*widthq]
    paddw   m0, [b2q+2*widthq]
    paddw   m0, m1
    psraw   m0, 1
    paddw   m0, [b1q+2*widthq]
    mova    [b1q+2*widthq], m0
    jg      .loop
    REP_RET

; void vertical_compose_dd97iH0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2,
;                               IDWTELEM *b3, IDWTELEM *b4, int width)
cglobal vertical_compose_dd97iH0_%1, 6,6,5, b0, b1, b2, b3, b4, width
    mova    m3, [pw_8]
    mova    m4, [pw_1991]
%if ARCH_X86_64
    mov     widthd, widthd
%endif
.loop:
    sub     widthq, mmsize/2
    mova    m0, [b0q+2*widthq]
    mova    m1, [b1q+2*widthq]
    COMPOSE_DD97iH0 [b2q+2*widthq], [b3q+2*widthq], [b4q+2*widthq]
    mova    [b2q+2*widthq], m1
    jg      .loop
    REP_RET

; void vertical_compose_dd137iL0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2,
;                                IDWTELEM *b3, IDWTELEM *b4, int width)
cglobal vertical_compose_dd137iL0_%1, 6,6,6, b0, b1, b2, b3, b4, width
    mova    m3, [pw_16]
    mova    m4, [pw_1991]
%if ARCH_X86_64
    mov     widthd, widthd
%endif
.loop:
    sub     widthq, mmsize/2
    mova    m0, [b0q+2*widthq]
    mova    m1, [b1q+2*widthq]
    mova    m5, [b2q+2*widthq]
    paddw   m0, [b4q+2*widthq]
    paddw   m1, [b3q+2*widthq]
    psubw   m0, m3
    mova    m2, m1
    punpcklwd m1, m0
    punpckhwd m2, m0
    pmaddwd m1, m4
    pmaddwd m2, m4
    psrad   m1, 5
    psrad   m2, 5
    packssdw m1, m2
    psubw   m5, m1
    mova    [b2q+2*widthq], m5
    jg      .loop
    REP_RET

; void vertical_compose_haar(IDWTELEM *b0, IDWTELEM *b1, int width)
cglobal vertical_compose_haar_%1, 3,4,3, b0, b1, width
    mova    m3, [pw_1]
%if ARCH_X86_64
    mov     widthd, widthd
%endif
.loop:
    sub     widthq, mmsize/2
    mova    m1, [b1q+2*widthq]
    mova    m0, [b0q+2*widthq]
    mova    m2, m1
    paddw   m1, m3
    psraw   m1, 1
    psubw   m0, m1
    mova    [b0q+2*widthq], m0
    paddw   m2, m0
    mova    [b1q+2*widthq], m2
    jg      .loop
    REP_RET
%endmacro

; extend the left and right edges of the tmp array by %1 and %2 respectively
%macro EDGE_EXTENSION 3
    mov     %3, [tmpq]
%assign %%i 1
%rep %1
    mov     [tmpq-2*%%i], %3
    %assign %%i %%i+1
%endrep
    mov     %3, [tmpq+2*w2q-2]
%assign %%i 0
%rep %2
    mov     [tmpq+2*w2q+2*%%i], %3
    %assign %%i %%i+1
%endrep
%endmacro


%macro HAAR_HORIZONTAL 2
; void horizontal_compose_haari(IDWTELEM *b, IDWTELEM *tmp, int width)
cglobal horizontal_compose_haar%2i_%1, 3,6,4, b, tmp, w, x, w2, b_w2
    mov    w2d, wd
    xor     xq, xq
    shr    w2d, 1
    lea  b_w2q, [bq+wq]
    mova    m3, [pw_1]
.lowpass_loop:
    movu    m1, [b_w2q + 2*xq]
    mova    m0, [bq    + 2*xq]
    paddw   m1, m3
    psraw   m1, 1
    psubw   m0, m1
    mova    [tmpq + 2*xq], m0
    add     xq, mmsize/2
    cmp     xq, w2q
    jl      .lowpass_loop

    xor     xq, xq
    and    w2q, ~(mmsize/2 - 1)
    cmp    w2q, mmsize/2
    jl      .end

.highpass_loop:
    movu    m1, [b_w2q + 2*xq]
    mova    m0, [tmpq  + 2*xq]
    paddw   m1, m0

    ; shift and interleave
%if %2 == 1
    paddw   m0, m3
    paddw   m1, m3
    psraw   m0, 1
    psraw   m1, 1
%endif
    mova    m2, m0
    punpcklwd m0, m1
    punpckhwd m2, m1
    mova    [bq+4*xq], m0
    mova    [bq+4*xq+mmsize], m2

    add     xq, mmsize/2
    cmp     xq, w2q
    jl      .highpass_loop
.end:
    REP_RET
%endmacro


INIT_XMM
; void horizontal_compose_dd97i(IDWTELEM *b, IDWTELEM *tmp, int width)
cglobal horizontal_compose_dd97i_ssse3, 3,6,8, b, tmp, w, x, w2, b_w2
    mov    w2d, wd
    xor     xd, xd
    shr    w2d, 1
    lea  b_w2q, [bq+wq]
    movu    m4, [bq+wq]
    mova    m7, [pw_2]
    pslldq  m4, 14
.lowpass_loop:
    movu    m1, [b_w2q + 2*xq]
    mova    m0, [bq    + 2*xq]
    mova    m2, m1
    palignr m1, m4, 14
    mova    m4, m2
    COMPOSE_53iL0 m0, m1, m2, m7
    mova    [tmpq + 2*xq], m0
    add     xd, mmsize/2
    cmp     xd, w2d
    jl      .lowpass_loop

    EDGE_EXTENSION 1, 2, xw
    ; leave the last up to 7 (sse) or 3 (mmx) values for C
    xor     xd, xd
    and    w2d, ~(mmsize/2 - 1)
    cmp    w2d, mmsize/2
    jl      .end

    mova    m7, [tmpq-mmsize]
    mova    m0, [tmpq]
    mova    m5, [pw_1]
    mova    m3, [pw_8]
    mova    m4, [pw_1991]
.highpass_loop:
    mova    m6, m0
    palignr m0, m7, 14
    mova    m7, [tmpq + 2*xq + 16]
    mova    m1, m7
    mova    m2, m7
    palignr m1, m6, 2
    palignr m2, m6, 4
    COMPOSE_DD97iH0 m0, m6, m2, [b_w2q + 2*xq]
    mova    m0, m7
    mova    m7, m6

    ; shift and interleave
    paddw   m6, m5
    paddw   m1, m5
    psraw   m6, 1
    psraw   m1, 1
    mova    m2, m6
    punpcklwd m6, m1
    punpckhwd m2, m1
    mova    [bq+4*xq], m6
    mova    [bq+4*xq+mmsize], m2

    add     xd, mmsize/2
    cmp     xd, w2d
    jl      .highpass_loop
.end:
    REP_RET


INIT_XMM
COMPOSE_VERTICAL sse2
HAAR_HORIZONTAL sse2, 0
HAAR_HORIZONTAL sse2, 1
