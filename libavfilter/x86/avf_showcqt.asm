;*****************************************************************************
;* x86-optimized functions for showcqt filter
;*
;* Copyright (C) 2016 Muhammad Faiz <mfcc64@gmail.com>
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

%if ARCH_X86_64
%define pointer resq
%else
%define pointer resd
%endif

struc Coeffs
    .val:   pointer 1
    .start: resd 1
    .len:   resd 1
    .sizeof:
endstruc

%macro CQT_CALC 9
; %1 = a_re, %2 = a_im, %3 = b_re, %4 = b_im
; %5 = m_re, %6 = m_im, %7 = tmp, %8 = coeffval, %9 = coeffsq_offset
    mov     id, xd
    add     id, [coeffsq + Coeffs.start + %9]
    movaps  m%5, [srcq + 8 * iq]
    movaps  m%7, [srcq + 8 * iq + mmsize]
    shufps  m%6, m%5, m%7, q3131
    shufps  m%5, m%5, m%7, q2020
    sub     id, fft_lend
    FMULADD_PS m%2, m%6, m%8, m%2, m%6
    neg     id
    FMULADD_PS m%1, m%5, m%8, m%1, m%5
    movups  m%5, [srcq + 8 * iq - mmsize + 8]
    movups  m%7, [srcq + 8 * iq - 2*mmsize + 8]
    %if mmsize == 32
    vperm2f128 m%5, m%5, m%5, 1
    vperm2f128 m%7, m%7, m%7, 1
    %endif
    shufps  m%6, m%5, m%7, q1313
    shufps  m%5, m%5, m%7, q0202
    FMULADD_PS m%4, m%6, m%8, m%4, m%6
    FMULADD_PS m%3, m%5, m%8, m%3, m%5
%endmacro ; CQT_CALC

%macro CQT_SEPARATE 6 ; a_re, a_im, b_re, b_im, tmp, tmp2
    addps   m%5, m%4, m%2
    subps   m%6, m%3, m%1
    addps   m%1, m%1, m%3
    subps   m%2, m%2, m%4
    HADDPS  m%5, m%6, m%3
    HADDPS  m%1, m%2, m%3
    HADDPS  m%1, m%5, m%2
    %if mmsize == 32
    vextractf128 xmm%2, m%1, 1
    addps   xmm%1, xmm%2
    %endif
%endmacro ; CQT_SEPARATE

%macro DECLARE_CQT_CALC 0
; ff_showcqt_cqt_calc_*(dst, src, coeffs, len, fft_len)
%if ARCH_X86_64
cglobal showcqt_cqt_calc, 5, 10, 12, dst, src, coeffs, len, fft_len, x, coeffs_val, coeffs_val2, i, coeffs_len
    align   16
    .loop_k:
        mov     xd, [coeffsq + Coeffs.len]
        xorps   m0, m0, m0
        movaps  m1, m0
        movaps  m2, m0
        mov     coeffs_lend, [coeffsq + Coeffs.len + Coeffs.sizeof]
        movaps  m3, m0
        movaps  m8, m0
        cmp     coeffs_lend, xd
        movaps  m9, m0
        movaps  m10, m0
        movaps  m11, m0
        cmova   coeffs_lend, xd
        xor     xd, xd
        test    coeffs_lend, coeffs_lend
        jz      .check_loop_b
        mov     coeffs_valq, [coeffsq + Coeffs.val]
        mov     coeffs_val2q, [coeffsq + Coeffs.val + Coeffs.sizeof]
        align   16
        .loop_ab:
            movaps  m7, [coeffs_valq + 4 * xq]
            CQT_CALC 0, 1, 2, 3, 4, 5, 6, 7, 0
            movaps  m7, [coeffs_val2q + 4 * xq]
            CQT_CALC 8, 9, 10, 11, 4, 5, 6, 7, Coeffs.sizeof
            add     xd, mmsize/4
            cmp     xd, coeffs_lend
            jb      .loop_ab
        .check_loop_b:
        cmp     xd, [coeffsq + Coeffs.len + Coeffs.sizeof]
        jae     .check_loop_a
        align   16
        .loop_b:
            movaps  m7, [coeffs_val2q + 4 * xq]
            CQT_CALC 8, 9, 10, 11, 4, 5, 6, 7, Coeffs.sizeof
            add     xd, mmsize/4
            cmp     xd, [coeffsq + Coeffs.len + Coeffs.sizeof]
            jb      .loop_b
        .loop_end:
        CQT_SEPARATE 0, 1, 2, 3, 4, 5
        CQT_SEPARATE 8, 9, 10, 11, 4, 5
        mulps   xmm0, xmm0
        mulps   xmm8, xmm8
        HADDPS  xmm0, xmm8, xmm1
        movaps  [dstq], xmm0
        sub     lend, 2
        lea     dstq, [dstq + 16]
        lea     coeffsq, [coeffsq + 2*Coeffs.sizeof]
        jnz     .loop_k
        REP_RET
        align   16
        .check_loop_a:
        cmp     xd, [coeffsq + Coeffs.len]
        jae     .loop_end
        align   16
        .loop_a:
            movaps  m7, [coeffs_valq + 4 * xq]
            CQT_CALC 0, 1, 2, 3, 4, 5, 6, 7, 0
            add     xd, mmsize/4
            cmp     xd, [coeffsq + Coeffs.len]
            jb      .loop_a
        jmp     .loop_end
%else
cglobal showcqt_cqt_calc, 4, 7, 8, dst, src, coeffs, len, x, coeffs_val, i
%define fft_lend r4m
    align   16
    .loop_k:
        mov     xd, [coeffsq + Coeffs.len]
        xorps   m0, m0, m0
        movaps  m1, m0
        movaps  m2, m0
        movaps  m3, m0
        test    xd, xd
        jz      .store
        mov     coeffs_valq, [coeffsq + Coeffs.val]
        xor     xd, xd
        align   16
        .loop_x:
            movaps  m7, [coeffs_valq + 4 * xq]
            CQT_CALC 0, 1, 2, 3, 4, 5, 6, 7, 0
            add     xd, mmsize/4
            cmp     xd, [coeffsq + Coeffs.len]
            jb      .loop_x
        CQT_SEPARATE 0, 1, 2, 3, 4, 5
        mulps   xmm0, xmm0
        HADDPS  xmm0, xmm0, xmm1
        .store:
        movlps  [dstq], xmm0
        sub     lend, 1
        lea     dstq, [dstq + 8]
        lea     coeffsq, [coeffsq + Coeffs.sizeof]
        jnz     .loop_k
        REP_RET
%endif ; ARCH_X86_64
%endmacro ; DECLARE_CQT_CALC

INIT_XMM sse
DECLARE_CQT_CALC
INIT_XMM sse3
DECLARE_CQT_CALC
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
DECLARE_CQT_CALC
%endif
%if HAVE_FMA3_EXTERNAL
INIT_YMM fma3
DECLARE_CQT_CALC
%endif
%if HAVE_FMA4_EXTERNAL
INIT_XMM fma4
DECLARE_CQT_CALC
%endif
