;******************************************************************************
;* MMX/SSE2-optimized functions for the RV40 decoder
;* Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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

align 16
shift_round:   times 8 dw 1 << (16 - 6)
cextern pw_16

SECTION .text

; %1=5bits weights?, %2=dst %3=src1 %4=src3 %5=stride if sse2
%macro RV40_WCORE  4-5
    movh       m4, [%3 + r6 + 0]
    movh       m5, [%4 + r6 + 0]
%if %0 == 4
%define OFFSET r6 + mmsize / 2
%else
    ; 8x8 block and sse2, stride was provided
%define OFFSET r6
    add        r6, r5
%endif
    movh       m6, [%3 + OFFSET]
    movh       m7, [%4 + OFFSET]

%if %1 == 0
    ; 14bits weights
    punpcklbw  m4, m0
    punpcklbw  m5, m0
    punpcklbw  m6, m0
    punpcklbw  m7, m0

    psllw      m4, 7
    psllw      m5, 7
    psllw      m6, 7
    psllw      m7, 7
    pmulhw     m4, m3
    pmulhw     m5, m2
    pmulhw     m6, m3
    pmulhw     m7, m2

    paddw      m4, m5
    paddw      m6, m7
%else
    ; 5bits weights
%if cpuflag(ssse3)
    punpcklbw  m4, m5
    punpcklbw  m6, m7

    pmaddubsw  m4, m3
    pmaddubsw  m6, m3
%else
    punpcklbw  m4, m0
    punpcklbw  m5, m0
    punpcklbw  m6, m0
    punpcklbw  m7, m0

    pmullw     m4, m3
    pmullw     m5, m2
    pmullw     m6, m3
    pmullw     m7, m2
    paddw      m4, m5
    paddw      m6, m7
%endif

%endif

    ; bias and shift down
%if cpuflag(ssse3)
    pmulhrsw   m4, m1
    pmulhrsw   m6, m1
%else
    paddw      m4, m1
    paddw      m6, m1
    psrlw      m4, 5
    psrlw      m6, 5
%endif

    packuswb   m4, m6
%if %0 == 5
    ; Only called for 8x8 blocks and sse2
    sub        r6, r5
    movh       [%2 + r6], m4
    add        r6, r5
    movhps     [%2 + r6], m4
%else
    mova       [%2 + r6], m4
%endif
%endmacro


%macro MAIN_LOOP   2
%if mmsize == 8
    RV40_WCORE %2, r0, r1, r2
%if %1 == 16
    RV40_WCORE %2, r0 + 8, r1 + 8, r2 + 8
%endif

    ; Prepare for next loop
    add        r6, r5
%else
%ifidn %1, 8
    RV40_WCORE %2, r0, r1, r2, r5
    ; Prepare 2 next lines
    add        r6, r5
%else
    RV40_WCORE %2, r0, r1, r2
    ; Prepare single next line
    add        r6, r5
%endif
%endif

%endmacro

; rv40_weight_func_%1(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w1, int w2, int stride)
; %1=size  %2=num of xmm regs
; The weights are FP0.14 notation of fractions depending on pts.
; For timebases without rounding error (i.e. PAL), the fractions
; can be simplified, and several operations can be avoided.
; Therefore, we check here whether they are multiples of 2^9 for
; those simplifications to occur.
%macro RV40_WEIGHT  3
cglobal rv40_weight_func_%1_%2, 6, 7, 8
%if cpuflag(ssse3)
    mova       m1, [shift_round]
%else
    mova       m1, [pw_16]
%endif
    pxor       m0, m0
    ; Set loop counter and increments
    mov        r6, r5
    shl        r6, %3
    add        r0, r6
    add        r1, r6
    add        r2, r6
    neg        r6

    movd       m2, r3
    movd       m3, r4
%ifidn %1,rnd
%define  RND   0
    SPLATW     m2, m2
%else
%define  RND   1
%if cpuflag(ssse3)
    punpcklbw  m3, m2
%else
    SPLATW     m2, m2
%endif
%endif
    SPLATW     m3, m3

.loop:
    MAIN_LOOP  %2, RND
    jnz        .loop
    REP_RET
%endmacro

INIT_MMX mmx
RV40_WEIGHT   rnd,    8, 3
RV40_WEIGHT   rnd,   16, 4
RV40_WEIGHT   nornd,  8, 3
RV40_WEIGHT   nornd, 16, 4

INIT_XMM sse2
RV40_WEIGHT   rnd,    8, 3
RV40_WEIGHT   rnd,   16, 4
RV40_WEIGHT   nornd,  8, 3
RV40_WEIGHT   nornd, 16, 4

INIT_XMM ssse3
RV40_WEIGHT   rnd,    8, 3
RV40_WEIGHT   rnd,   16, 4
RV40_WEIGHT   nornd,  8, 3
RV40_WEIGHT   nornd, 16, 4
