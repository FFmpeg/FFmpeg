;******************************************************************************
;* Copyright (c) 2008 Loren Merritt
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

; int ff_scalarproduct_and_madd_int16(int16_t *v1, int16_t *v2, int16_t *v3,
;                                     int order, int mul)
INIT_XMM sse2
cglobal scalarproduct_and_madd_int16, 4,4,8, v1, v2, v3, order, mul
    shl orderq, 1
    movd    m7, mulm
    pshuflw m7, m7, 0
    punpcklqdq m7, m7
    pxor    m6, m6
    add v1q, orderq
    add v2q, orderq
    add v3q, orderq
    neg orderq
.loop:
    movu    m0, [v2q + orderq]
    movu    m1, [v2q + orderq + mmsize]
    mova    m4, [v1q + orderq]
    mova    m5, [v1q + orderq + mmsize]
    movu    m2, [v3q + orderq]
    movu    m3, [v3q + orderq + mmsize]
    pmaddwd m0, m4
    pmaddwd m1, m5
    pmullw  m2, m7
    pmullw  m3, m7
    paddd   m6, m0
    paddd   m6, m1
    paddw   m2, m4
    paddw   m3, m5
    mova    [v1q + orderq], m2
    mova    [v1q + orderq + mmsize], m3
    add     orderq, mmsize*2
    jl .loop
    HADDD   m6, m0
    movd   eax, m6
    RET

INIT_XMM sse4
; int ff_scalarproduct_and_madd_int32(int16_t *v1, int32_t *v2, int16_t *v3,
;                                     int order, int mul)
cglobal scalarproduct_and_madd_int32, 4,4,8, v1, v2, v3, order, mul
    shl orderq, 1
    movd    m7, mulm
    SPLATW  m7, m7
    pxor    m6, m6
    add v1q, orderq
    lea v2q, [v2q + 2*orderq]
    add v3q, orderq
    neg orderq
.loop:
    mova    m3, [v1q + orderq]
    movu    m0, [v2q + 2*orderq]
    pmovsxwd m4, m3
    movu    m1, [v2q + 2*orderq + mmsize]
    movhlps m5, m3
    movu    m2, [v3q + orderq]
    pmovsxwd m5, m5
    pmullw  m2, m7
    pmulld  m0, m4
    pmulld  m1, m5
    paddw   m2, m3
    paddd   m6, m0
    paddd   m6, m1
    mova    [v1q + orderq], m2
    add     orderq, 16
    jl .loop
    HADDD   m6, m0
    movd   eax, m6
    RET

%macro SCALARPRODUCT_LOOP 1
align 16
.loop%1:
    sub     orderq, mmsize*2
%if %1
    mova    m1, m4
    mova    m4, [v2q + orderq]
    mova    m0, [v2q + orderq + mmsize]
    palignr m1, m0, %1
    palignr m0, m4, %1
    mova    m3, m5
    mova    m5, [v3q + orderq]
    mova    m2, [v3q + orderq + mmsize]
    palignr m3, m2, %1
    palignr m2, m5, %1
%else
    mova    m0, [v2q + orderq]
    mova    m1, [v2q + orderq + mmsize]
    mova    m2, [v3q + orderq]
    mova    m3, [v3q + orderq + mmsize]
%endif
    %define t0  [v1q + orderq]
    %define t1  [v1q + orderq + mmsize]
%if ARCH_X86_64
    mova    m8, t0
    mova    m9, t1
    %define t0  m8
    %define t1  m9
%endif
    pmaddwd m0, t0
    pmaddwd m1, t1
    pmullw  m2, m7
    pmullw  m3, m7
    paddw   m2, t0
    paddw   m3, t1
    paddd   m6, m0
    paddd   m6, m1
    mova    [v1q + orderq], m2
    mova    [v1q + orderq + mmsize], m3
    jg .loop%1
%if %1
    jmp .end
%endif
%endmacro

; int ff_scalarproduct_and_madd_int16(int16_t *v1, int16_t *v2, int16_t *v3,
;                                     int order, int mul)
INIT_XMM ssse3
cglobal scalarproduct_and_madd_int16, 4,5,10, v1, v2, v3, order, mul
    shl orderq, 1
    movd    m7, mulm
    pshuflw m7, m7, 0
    punpcklqdq m7, m7
    pxor    m6, m6
    mov    r4d, v2d
    and    r4d, 15
    and    v2q, ~15
    and    v3q, ~15
    mova    m4, [v2q + orderq]
    mova    m5, [v3q + orderq]
    ; linear is faster than branch tree or jump table, because the branches taken are cyclic (i.e. predictable)
    cmp    r4d, 0
    je .loop0
    cmp    r4d, 2
    je .loop2
    cmp    r4d, 4
    je .loop4
    cmp    r4d, 6
    je .loop6
    cmp    r4d, 8
    je .loop8
    cmp    r4d, 10
    je .loop10
    cmp    r4d, 12
    je .loop12
SCALARPRODUCT_LOOP 14
SCALARPRODUCT_LOOP 12
SCALARPRODUCT_LOOP 10
SCALARPRODUCT_LOOP 8
SCALARPRODUCT_LOOP 6
SCALARPRODUCT_LOOP 4
SCALARPRODUCT_LOOP 2
SCALARPRODUCT_LOOP 0
.end:
    HADDD   m6, m0
    movd   eax, m6
    RET
