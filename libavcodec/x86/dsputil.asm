;******************************************************************************
;* MMX optimized DSP utils
;* Copyright (c) 2008 Loren Merritt
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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
pb_bswap32: db 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12

SECTION_TEXT

%macro SCALARPRODUCT 0
; int ff_scalarproduct_int16(int16_t *v1, int16_t *v2, int order)
cglobal scalarproduct_int16, 3,3,3, v1, v2, order
    shl orderq, 1
    add v1q, orderq
    add v2q, orderq
    neg orderq
    pxor    m2, m2
.loop:
    movu    m0, [v1q + orderq]
    movu    m1, [v1q + orderq + mmsize]
    pmaddwd m0, [v2q + orderq]
    pmaddwd m1, [v2q + orderq + mmsize]
    paddd   m2, m0
    paddd   m2, m1
    add     orderq, mmsize*2
    jl .loop
%if mmsize == 16
    movhlps m0, m2
    paddd   m2, m0
    pshuflw m0, m2, 0x4e
%else
    pshufw  m0, m2, 0x4e
%endif
    paddd   m2, m0
    movd   eax, m2
    RET
%endmacro

INIT_MMX mmxext
SCALARPRODUCT
INIT_XMM sse2
SCALARPRODUCT


;-----------------------------------------------------------------------------
; void ff_vector_clip_int32(int32_t *dst, const int32_t *src, int32_t min,
;                           int32_t max, unsigned int len)
;-----------------------------------------------------------------------------

; %1 = number of xmm registers used
; %2 = number of inline load/process/store loops per asm loop
; %3 = process 4*mmsize (%3=0) or 8*mmsize (%3=1) bytes per loop
; %4 = CLIPD function takes min/max as float instead of int (CLIPD_SSE2)
; %5 = suffix
%macro VECTOR_CLIP_INT32 4-5
cglobal vector_clip_int32%5, 5,5,%1, dst, src, min, max, len
%if %4
    cvtsi2ss  m4, minm
    cvtsi2ss  m5, maxm
%else
    movd      m4, minm
    movd      m5, maxm
%endif
    SPLATD    m4
    SPLATD    m5
.loop:
%assign %%i 1
%rep %2
    mova      m0,  [srcq+mmsize*0*%%i]
    mova      m1,  [srcq+mmsize*1*%%i]
    mova      m2,  [srcq+mmsize*2*%%i]
    mova      m3,  [srcq+mmsize*3*%%i]
%if %3
    mova      m7,  [srcq+mmsize*4*%%i]
    mova      m8,  [srcq+mmsize*5*%%i]
    mova      m9,  [srcq+mmsize*6*%%i]
    mova      m10, [srcq+mmsize*7*%%i]
%endif
    CLIPD  m0,  m4, m5, m6
    CLIPD  m1,  m4, m5, m6
    CLIPD  m2,  m4, m5, m6
    CLIPD  m3,  m4, m5, m6
%if %3
    CLIPD  m7,  m4, m5, m6
    CLIPD  m8,  m4, m5, m6
    CLIPD  m9,  m4, m5, m6
    CLIPD  m10, m4, m5, m6
%endif
    mova  [dstq+mmsize*0*%%i], m0
    mova  [dstq+mmsize*1*%%i], m1
    mova  [dstq+mmsize*2*%%i], m2
    mova  [dstq+mmsize*3*%%i], m3
%if %3
    mova  [dstq+mmsize*4*%%i], m7
    mova  [dstq+mmsize*5*%%i], m8
    mova  [dstq+mmsize*6*%%i], m9
    mova  [dstq+mmsize*7*%%i], m10
%endif
%assign %%i %%i+1
%endrep
    add     srcq, mmsize*4*(%2+%3)
    add     dstq, mmsize*4*(%2+%3)
    sub     lend, mmsize*(%2+%3)
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmx
%define CLIPD CLIPD_MMX
VECTOR_CLIP_INT32 0, 1, 0, 0
INIT_XMM sse2
VECTOR_CLIP_INT32 6, 1, 0, 0, _int
%define CLIPD CLIPD_SSE2
VECTOR_CLIP_INT32 6, 2, 0, 1
INIT_XMM sse4
%define CLIPD CLIPD_SSE41
%ifdef m8
VECTOR_CLIP_INT32 11, 1, 1, 0
%else
VECTOR_CLIP_INT32 6, 1, 0, 0
%endif

; %1 = aligned/unaligned
%macro BSWAP_LOOPS  1
    mov      r3, r2
    sar      r2, 3
    jz       .left4_%1
.loop8_%1:
    mov%1    m0, [r1 +  0]
    mov%1    m1, [r1 + 16]
%if cpuflag(ssse3)
    pshufb   m0, m2
    pshufb   m1, m2
    mov%1    [r0 +  0], m0
    mov%1    [r0 + 16], m1
%else
    pshuflw  m0, m0, 10110001b
    pshuflw  m1, m1, 10110001b
    pshufhw  m0, m0, 10110001b
    pshufhw  m1, m1, 10110001b
    mova     m2, m0
    mova     m3, m1
    psllw    m0, 8
    psllw    m1, 8
    psrlw    m2, 8
    psrlw    m3, 8
    por      m2, m0
    por      m3, m1
    mov%1    [r0 +  0], m2
    mov%1    [r0 + 16], m3
%endif
    add      r0, 32
    add      r1, 32
    dec      r2
    jnz      .loop8_%1
.left4_%1:
    mov      r2, r3
    and      r3, 4
    jz       .left
    mov%1    m0, [r1]
%if cpuflag(ssse3)
    pshufb   m0, m2
    mov%1    [r0], m0
%else
    pshuflw  m0, m0, 10110001b
    pshufhw  m0, m0, 10110001b
    mova     m2, m0
    psllw    m0, 8
    psrlw    m2, 8
    por      m2, m0
    mov%1    [r0], m2
%endif
    add      r1, 16
    add      r0, 16
%endmacro

; void ff_bswap_buf(uint32_t *dst, const uint32_t *src, int w);
%macro BSWAP32_BUF 0
%if cpuflag(ssse3)
cglobal bswap32_buf, 3,4,3
    mov      r3, r1
    mova     m2, [pb_bswap32]
%else
cglobal bswap32_buf, 3,4,5
    mov      r3, r1
%endif
    and      r3, 15
    jz       .start_align
    BSWAP_LOOPS  u
    jmp      .left
.start_align:
    BSWAP_LOOPS  a
.left:
%if cpuflag(ssse3)
    mov      r3, r2
    and      r2, 2
    jz       .left1
    movq     m0, [r1]
    pshufb   m0, m2
    movq     [r0], m0
    add      r1, 8
    add      r0, 8
.left1:
    and      r3, 1
    jz       .end
    mov      r2d, [r1]
    bswap    r2d
    mov      [r0], r2d
%else
    and      r2, 3
    jz       .end
.loop2:
    mov      r3d, [r1]
    bswap    r3d
    mov      [r0], r3d
    add      r1, 4
    add      r0, 4
    dec      r2
    jnz      .loop2
%endif
.end:
    RET
%endmacro

INIT_XMM sse2
BSWAP32_BUF

INIT_XMM ssse3
BSWAP32_BUF
