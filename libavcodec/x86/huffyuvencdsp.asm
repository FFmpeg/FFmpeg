;************************************************************************
;* SIMD-optimized HuffYUV encoding functions
;* Copyright (c) 2000, 2001 Fabrice Bellard
;* Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
;*
;* MMX optimization by Nick Kurshev <nickols_k@mail.ru>
;* Conversion to NASM format by Tiancheng "Timothy" Gu <timothygu99@gmail.com>
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

section .text

; void ff_diff_bytes(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                    intptr_t w);
%macro DIFF_BYTES_PROLOGUE 0
%if ARCH_X86_32
cglobal diff_bytes, 3,5,2, dst, src1, src2
%define wq r4q
    DECLARE_REG_TMP 3
    mov               wq, r3mp
%else
cglobal diff_bytes, 4,5,2, dst, src1, src2, w
    DECLARE_REG_TMP 4
%endif ; ARCH_X86_32
%define i t0q
%endmacro

; label to jump to if w < regsize
%macro DIFF_BYTES_LOOP_PREP 1
    mov                i, wq
    and                i, -2 * regsize
        jz            %1
    add             dstq, i
    add            src1q, i
    add            src2q, i
    neg                i
%endmacro

; mov type used for src1q, dstq, first reg, second reg
%macro DIFF_BYTES_LOOP_CORE 4
%if mmsize != 16
    mov%1             %3, [src1q + i]
    mov%1             %4, [src1q + i + regsize]
    psubb             %3, [src2q + i]
    psubb             %4, [src2q + i + regsize]
    mov%2           [dstq + i], %3
    mov%2 [regsize + dstq + i], %4
%else
    ; SSE enforces alignment of psubb operand
    mov%1             %3, [src1q + i]
    movu              %4, [src2q + i]
    psubb             %3, %4
    mov%2     [dstq + i], %3
    mov%1             %3, [src1q + i + regsize]
    movu              %4, [src2q + i + regsize]
    psubb             %3, %4
    mov%2 [regsize + dstq + i], %3
%endif
%endmacro

%macro DIFF_BYTES_BODY 2 ; mov type used for src1q, for dstq
    %define regsize mmsize
.loop_%1%2:
    DIFF_BYTES_LOOP_CORE %1, %2, m0, m1
    add                i, 2 * regsize
        jl    .loop_%1%2
.skip_main_%1%2:
    and               wq, 2 * regsize - 1
        jz     .end_%1%2
%if mmsize > 16
    ; fall back to narrower xmm
    %define regsize mmsize / 2
    DIFF_BYTES_LOOP_PREP .setup_loop_gpr_aa
.loop2_%1%2:
    DIFF_BYTES_LOOP_CORE %1, %2, xm0, xm1
    add                i, 2 * regsize
        jl   .loop2_%1%2
.setup_loop_gpr_%1%2:
    and               wq, 2 * regsize - 1
        jz     .end_%1%2
%endif
    add             dstq, wq
    add            src1q, wq
    add            src2q, wq
    neg               wq
.loop_gpr_%1%2:
    mov              t0b, [src1q + wq]
    sub              t0b, [src2q + wq]
    mov      [dstq + wq], t0b
    inc               wq
        jl .loop_gpr_%1%2
.end_%1%2:
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
DIFF_BYTES_PROLOGUE
    %define regsize mmsize
    DIFF_BYTES_LOOP_PREP .skip_main_aa
    DIFF_BYTES_BODY    a, a
%endif

INIT_XMM sse2
DIFF_BYTES_PROLOGUE
    %define regsize mmsize
    DIFF_BYTES_LOOP_PREP .skip_main_aa
    test            dstq, regsize - 1
        jnz     .loop_uu
    test           src1q, regsize - 1
        jnz     .loop_ua
    DIFF_BYTES_BODY    a, a
    DIFF_BYTES_BODY    u, a
    DIFF_BYTES_BODY    u, u

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
DIFF_BYTES_PROLOGUE
    %define regsize mmsize
    ; Directly using unaligned SSE2 version is marginally faster than
    ; branching based on arguments.
    DIFF_BYTES_LOOP_PREP .skip_main_uu
    test            dstq, regsize - 1
        jnz     .loop_uu
    test           src1q, regsize - 1
        jnz     .loop_ua
    DIFF_BYTES_BODY    a, a
    DIFF_BYTES_BODY    u, a
    DIFF_BYTES_BODY    u, u
%endif
