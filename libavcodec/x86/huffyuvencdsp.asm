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

INIT_MMX mmx
; void ff_diff_bytes_mmx(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                        intptr_t w);
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
    mov                i, wq
    and                i, -2 * mmsize
        jz  .setup_loop2
    add             dstq, i
    add            src1q, i
    add            src2q, i
    neg                i
.loop:
    mova              m0, [src1q + i]
    mova              m1, [src1q + i + mmsize]
    psubb             m0, [src2q + i]
    psubb             m1, [src2q + i + mmsize]
    mova          [dstq + i], m0
    mova [mmsize + dstq + i], m1
    add                i, 2 * mmsize
        jl         .loop
.setup_loop2:
    and               wq, 2 * mmsize - 1
        jz          .end
    add             dstq, wq
    add            src1q, wq
    add            src2q, wq
    neg               wq
.loop2:
    mov              t0b, [src1q + wq]
    sub              t0b, [src2q + wq]
    mov      [dstq + wq], t0b
    inc               wq
        jl        .loop2
.end:
    REP_RET
