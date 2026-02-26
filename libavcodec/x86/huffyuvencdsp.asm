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

SECTION .text

%include "libavcodec/x86/huffyuvdsp_template.asm"

;------------------------------------------------------------------------------
; void ff_diff_int16(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                    unsigned mask, int w);
;------------------------------------------------------------------------------

%macro DIFF_INT16 0
cglobal diff_int16, 5,5,5, dst, src1, src2, mask, w, tmp
    test src1q, mmsize-1
    jnz .unaligned
    test src2q, mmsize-1
    jnz .unaligned
    test dstq, mmsize-1
    jnz .unaligned
    INT16_LOOP a, sub
.unaligned:
    INT16_LOOP u, sub
%endmacro

INIT_XMM sse2
DIFF_INT16

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
DIFF_INT16
%endif

%macro SUB_HFYU_MEDIAN_PRED_INT16 1 ; u,s for pmaxuw vs pmaxsw
cglobal sub_hfyu_median_pred_int16, 7,7,6, dst, src1, src2, mask, w, left, left_top
    movd        xm5, maskd
    lea          wd, [wd+wd-(mmsize-1)]
    movu        xm0, [src1q]
    movu        xm2, [src2q]
    SPLATW       m5, xm5
    add        dstq, wq
    movd        xm1, [left_topq]
    neg          wq
    movd        xm3, [leftq]
%if mmsize >= 32
    movu        xm4, [src1q+14]
%endif
    sub       src1q, wq
    pslldq      xm0, 2
    pslldq      xm2, 2
    por         xm0, xm1
%if mmsize >= 32
    vinserti128  m0, xm4, 1
%endif
    por         xm2, xm3
%if mmsize >= 32
    vinserti128  m2, [src2q+14], 1
%endif
    sub       src2q, wq
    jmp       .init

.loop:
    movu         m0, [src1q + wq - 2]   ; lt
    movu         m2, [src2q + wq - 2]   ; l
.init:
    movu         m1, [src1q + wq]       ; t
    movu         m3, [src2q + wq]
    psubw        m4, m2, m0             ; l - lt
    pmax%1w      m0, m1, m2
    paddw        m4, m1                 ; l - lt + t
    pmin%1w      m2, m1
    pand         m4, m5                 ; (l - lt + t)&mask
    pmin%1w      m4, m0
    pmax%1w      m4, m2                 ; pred
    psubw        m3, m4                 ; l - pred
    pand         m3, m5
    movu [dstq + wq], m3
    add          wq, mmsize
    js        .loop

    cmp          wd, mmsize-1
    jne       .tail

    movzx     src1d, word [src1q + (mmsize-1) - 2]
    movzx     src2d, word [src2q + (mmsize-1) - 2]
    mov [left_topq], src1d
    mov     [leftq], src2d
    RET
.tail:
    mov          wq, -1
    jmp       .loop
%endmacro

INIT_XMM sse2
SUB_HFYU_MEDIAN_PRED_INT16 s

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
SUB_HFYU_MEDIAN_PRED_INT16 u
%endif
