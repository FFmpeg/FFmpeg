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

INIT_MMX mmxext
cglobal sub_hfyu_median_pred_int16, 7,7,0, dst, src1, src2, mask, w, left, left_top
    add      wd, wd
    movd    mm7, maskd
    SPLATW  mm7, mm7
    movq    mm0, [src1q]
    movq    mm2, [src2q]
    psllq   mm0, 16
    psllq   mm2, 16
    movd    mm6, [left_topq]
    por     mm0, mm6
    movd    mm6, [leftq]
    por     mm2, mm6
    xor     maskq, maskq
.loop:
    movq    mm1, [src1q + maskq]
    movq    mm3, [src2q + maskq]
    movq    mm4, mm2
    psubw   mm2, mm0
    paddw   mm2, mm1
    pand    mm2, mm7
    movq    mm5, mm4
    pmaxsw  mm4, mm1
    pminsw  mm1, mm5
    pminsw  mm4, mm2
    pmaxsw  mm4, mm1
    psubw   mm3, mm4
    pand    mm3, mm7
    movq    [dstq + maskq], mm3
    add     maskq, 8
    movq    mm0, [src1q + maskq - 2]
    movq    mm2, [src2q + maskq - 2]
    cmp     maskq, wq
        jb .loop
    movzx maskd, word [src1q + wq - 2]
    mov [left_topq], maskd
    movzx maskd, word [src2q + wq - 2]
    mov [leftq], maskd
    RET

INIT_XMM sse2
cglobal sub_hfyu_median_pred_int16, 7,7,6, dst, src1, src2, mask, w, left, left_top
    movd         m5, maskd
    lea          wd, [wd+wd-(mmsize-1)]
    movu         m0, [src1q]
    movu         m2, [src2q]
    SPLATW       m5, m5
    add        dstq, wq
    movd         m1, [left_topq]
    neg          wq
    movd         m3, [leftq]
    sub       src1q, wq
    sub       src2q, wq
    pslldq       m0, 2
    pslldq       m2, 2
    por          m0, m1
    por          m2, m3
    jmp       .init

.loop:
    movu         m0, [src1q + wq - 2]   ; lt
    movu         m2, [src2q + wq - 2]   ; l
.init:
    movu         m1, [src1q + wq]       ; t
    movu         m3, [src2q + wq]
    psubw        m4, m2, m0             ; l - lt
    pmaxsw       m0, m1, m2
    paddw        m4, m1                 ; l - lt + t
    pminsw       m2, m1
    pand         m4, m5                 ; (l - lt + t)&mask
    pminsw       m4, m0
    pmaxsw       m4, m2                 ; pred
    psubw        m3, m4                 ; l - pred
    pand         m3, m5
    movu [dstq + wq], m3
    add          wq, 16
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
