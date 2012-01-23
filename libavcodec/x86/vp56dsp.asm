;******************************************************************************
;* MMX/SSE2-optimized functions for the VP6 decoder
;* Copyright (C) 2009  Sebastien Lucas <sebastien.lucas@gmail.com>
;* Copyright (C) 2009  Zuxy Meng <zuxy.meng@gmail.com>
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

cextern pw_64

SECTION .text

%macro DIAG4_MMX 6
    movq          m0, [%1+%2]
    movq          m1, [%1+%3]
    movq          m3, m0
    movq          m4, m1
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    punpckhbw     m3, m7
    punpckhbw     m4, m7
    pmullw        m0, [rsp+8*11] ; src[x-8 ] * biweight [0]
    pmullw        m1, [rsp+8*12] ; src[x   ] * biweight [1]
    pmullw        m3, [rsp+8*11] ; src[x-8 ] * biweight [0]
    pmullw        m4, [rsp+8*12] ; src[x   ] * biweight [1]
    paddw         m0, m1
    paddw         m3, m4
    movq          m1, [%1+%4]
    movq          m2, [%1+%5]
    movq          m4, m1
    movq          m5, m2
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpckhbw     m4, m7
    punpckhbw     m5, m7
    pmullw        m1, [rsp+8*13] ; src[x+8 ] * biweight [2]
    pmullw        m2, [rsp+8*14] ; src[x+16] * biweight [3]
    pmullw        m4, [rsp+8*13] ; src[x+8 ] * biweight [2]
    pmullw        m5, [rsp+8*14] ; src[x+16] * biweight [3]
    paddw         m1, m2
    paddw         m4, m5
    paddsw        m0, m1
    paddsw        m3, m4
    paddsw        m0, m6         ; Add 64
    paddsw        m3, m6         ; Add 64
    psraw         m0, 7
    psraw         m3, 7
    packuswb      m0, m3
    movq        [%6], m0
%endmacro

%macro DIAG4_SSE2 6
    movq          m0, [%1+%2]
    movq          m1, [%1+%3]
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    pmullw        m0, m4         ; src[x-8 ] * biweight [0]
    pmullw        m1, m5         ; src[x   ] * biweight [1]
    paddw         m0, m1
    movq          m1, [%1+%4]
    movq          m2, [%1+%5]
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    pmullw        m1, m6         ; src[x+8 ] * biweight [2]
    pmullw        m2, m3         ; src[x+16] * biweight [3]
    paddw         m1, m2
    paddsw        m0, m1
    paddsw        m0, [pw_64]    ; Add 64
    psraw         m0, 7
    packuswb      m0, m0
    movq        [%6], m0
%endmacro

%macro SPLAT4REGS_MMX 0
    movq         m5, m3
    punpcklwd    m3, m3
    movq         m4, m3
    punpckldq    m3, m3
    punpckhdq    m4, m4
    punpckhwd    m5, m5
    movq         m2, m5
    punpckhdq    m2, m2
    punpckldq    m5, m5
    movq [rsp+8*11], m3
    movq [rsp+8*12], m4
    movq [rsp+8*13], m5
    movq [rsp+8*14], m2
%endmacro

%macro SPLAT4REGS_SSE2 0
    pshuflw      m4, m3, 0x0
    pshuflw      m5, m3, 0x55
    pshuflw      m6, m3, 0xAA
    pshuflw      m3, m3, 0xFF
    punpcklqdq   m4, m4
    punpcklqdq   m5, m5
    punpcklqdq   m6, m6
    punpcklqdq   m3, m3
%endmacro

%macro vp6_filter_diag4 2
; void ff_vp6_filter_diag4_<opt>(uint8_t *dst, uint8_t *src, int stride,
;                                const int16_t h_weight[4], const int16_t v_weights[4])
cglobal vp6_filter_diag4_%1, 5, 7, %2
    mov          r5, rsp         ; backup stack pointer
    and         rsp, ~(mmsize-1) ; align stack
%ifidn %1, sse2
    sub         rsp, 8*11
%else
    sub         rsp, 8*15
    movq         m6, [pw_64]
%endif
%if ARCH_X86_64
    movsxd       r2, r2d
%endif

    sub          r1, r2

    pxor         m7, m7
    movq         m3, [r3]
    SPLAT4REGS

    mov          r3, rsp
    mov          r6, 11
.nextrow
    DIAG4        r1, -1, 0, 1, 2, r3
    add          r3, 8
    add          r1, r2
    dec          r6
    jnz .nextrow

    movq         m3, [r4]
    SPLAT4REGS

    lea          r3, [rsp+8]
    mov          r6, 8
.nextcol
    DIAG4        r3, -8, 0, 8, 16, r0
    add          r3, 8
    add          r0, r2
    dec          r6
    jnz .nextcol

    mov         rsp, r5          ; restore stack pointer
    RET
%endmacro

INIT_MMX
%define DIAG4      DIAG4_MMX
%define SPLAT4REGS SPLAT4REGS_MMX
vp6_filter_diag4 mmx,  0

INIT_XMM
%define DIAG4      DIAG4_SSE2
%define SPLAT4REGS SPLAT4REGS_SSE2
vp6_filter_diag4 sse2, 8
