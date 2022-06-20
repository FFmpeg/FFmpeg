;************************************************************************
;* SIMD-optimized lossless video encoding functions
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

cextern pb_80

SECTION .text

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

; labels to jump to if w < regsize and w < 0
%macro DIFF_BYTES_LOOP_PREP 2
    mov                i, wq
    and                i, -2 * regsize
        js            %2
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
    %define regsize (mmsize / 2)
    DIFF_BYTES_LOOP_PREP .setup_loop_gpr_aa, .end_aa
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

INIT_XMM sse2
DIFF_BYTES_PROLOGUE
    %define regsize mmsize
    DIFF_BYTES_LOOP_PREP .skip_main_aa, .end_aa
    test            dstq, regsize - 1
        jnz     .loop_uu
    test           src1q, regsize - 1
        jnz     .loop_ua
    DIFF_BYTES_BODY    a, a
    DIFF_BYTES_BODY    u, a
    DIFF_BYTES_BODY    u, u
%undef i

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
DIFF_BYTES_PROLOGUE
    %define regsize mmsize
    ; Directly using unaligned SSE2 version is marginally faster than
    ; branching based on arguments.
    DIFF_BYTES_LOOP_PREP .skip_main_uu, .end_uu
    test            dstq, regsize - 1
        jnz     .loop_uu
    test           src1q, regsize - 1
        jnz     .loop_ua
    DIFF_BYTES_BODY    a, a
    DIFF_BYTES_BODY    u, a
    DIFF_BYTES_BODY    u, u
%undef i
%endif


;--------------------------------------------------------------------------------------------------
;void sub_left_predict(uint8_t *dst, uint8_t *src, ptrdiff_t stride, ptrdiff_t width, int height)
;--------------------------------------------------------------------------------------------------

INIT_XMM avx
cglobal sub_left_predict, 5,6,5, dst, src, stride, width, height, x
    mova             m1, [pb_80] ; prev initial
    add            dstq, widthq
    add            srcq, widthq
    lea              xd, [widthq-1]
    neg          widthq
    and              xd, 15
    pinsrb           m4, m1, xd, 15
    mov              xq, widthq

    .loop:
        movu                     m0, [srcq + widthq]
        palignr                  m2, m0, m1, 15
        movu                     m1, [srcq + widthq + 16]
        palignr                  m3, m1, m0, 15
        psubb                    m2, m0, m2
        psubb                    m3, m1, m3
        movu        [dstq + widthq], m2
        movu   [dstq + widthq + 16], m3
        add                  widthq, 2 * 16
        jl .loop

    add   srcq, strideq
    sub   dstq, xq ; dst + width
    test    xd, 16
    jz .mod32
    mova    m1, m0

.mod32:
    pshufb    m1, m4
    mov   widthq, xq
    dec  heightd
    jg .loop
    RET
