;******************************************************************************
;* MMX optimized DSP utils
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"

section .text align=16

%macro PSWAPD_SSE 2
    pshufw %1, %2, 0x4e
%endmacro
%macro PSWAPD_3DN1 2
    movq  %1, %2
    psrlq %1, 32
    punpckldq %1, %2
%endmacro

%macro FLOAT_TO_INT16_INTERLEAVE6 1
; void ff_float_to_int16_interleave6_sse(int16_t *dst, const float **src, int len)
cglobal float_to_int16_interleave6_%1, 2,7,0, dst, src, src1, src2, src3, src4, src5
%ifdef ARCH_X86_64
    %define lend r10d
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov src1q, [srcq+1*gprsize]
    mov src2q, [srcq+2*gprsize]
    mov src3q, [srcq+3*gprsize]
    mov src4q, [srcq+4*gprsize]
    mov src5q, [srcq+5*gprsize]
    mov srcq,  [srcq]
    sub src1q, srcq
    sub src2q, srcq
    sub src3q, srcq
    sub src4q, srcq
    sub src5q, srcq
.loop:
    cvtps2pi   mm0, [srcq]
    cvtps2pi   mm1, [srcq+src1q]
    cvtps2pi   mm2, [srcq+src2q]
    cvtps2pi   mm3, [srcq+src3q]
    cvtps2pi   mm4, [srcq+src4q]
    cvtps2pi   mm5, [srcq+src5q]
    packssdw   mm0, mm3
    packssdw   mm1, mm4
    packssdw   mm2, mm5
    pswapd     mm3, mm0
    punpcklwd  mm0, mm1
    punpckhwd  mm1, mm2
    punpcklwd  mm2, mm3
    pswapd     mm3, mm0
    punpckldq  mm0, mm2
    punpckhdq  mm2, mm1
    punpckldq  mm1, mm3
    movq [dstq   ], mm0
    movq [dstq+16], mm2
    movq [dstq+ 8], mm1
    add srcq, 8
    add dstq, 24
    sub lend, 2
    jg .loop
    emms
    RET
%endmacro ; FLOAT_TO_INT16_INTERLEAVE6

%define pswapd PSWAPD_SSE
FLOAT_TO_INT16_INTERLEAVE6 sse
%define cvtps2pi pf2id
%define pswapd PSWAPD_3DN1
FLOAT_TO_INT16_INTERLEAVE6 3dnow
%undef pswapd
FLOAT_TO_INT16_INTERLEAVE6 3dn2
%undef cvtps2pi



; void ff_add_hfyu_median_prediction_mmx2(uint8_t *dst, uint8_t *top, uint8_t *diff, int w, int *left, int *left_top)
cglobal add_hfyu_median_prediction_mmx2, 6,6,0, dst, top, diff, w, left, left_top
    movq    mm0, [topq]
    movq    mm2, mm0
    movd    mm4, [left_topq]
    psllq   mm2, 8
    movq    mm1, mm0
    por     mm4, mm2
    movd    mm3, [leftq]
    psubb   mm0, mm4 ; t-tl
    add    dstq, wq
    add    topq, wq
    add   diffq, wq
    neg      wq
    jmp .skip
.loop:
    movq    mm4, [topq+wq]
    movq    mm0, mm4
    psllq   mm4, 8
    por     mm4, mm1
    movq    mm1, mm0 ; t
    psubb   mm0, mm4 ; t-tl
.skip:
    movq    mm2, [diffq+wq]
%assign i 0
%rep 8
    movq    mm4, mm0
    paddb   mm4, mm3 ; t-tl+l
    movq    mm5, mm3
    pmaxub  mm3, mm1
    pminub  mm5, mm1
    pminub  mm3, mm4
    pmaxub  mm3, mm5 ; median
    paddb   mm3, mm2 ; +residual
%if i==0
    movq    mm7, mm3
    psllq   mm7, 56
%else
    movq    mm6, mm3
    psrlq   mm7, 8
    psllq   mm6, 56
    por     mm7, mm6
%endif
%if i<7
    psrlq   mm0, 8
    psrlq   mm1, 8
    psrlq   mm2, 8
%endif
%assign i i+1
%endrep
    movq [dstq+wq], mm7
    add      wq, 8
    jl .loop
    movzx   r2d, byte [dstq-1]
    mov [leftq], r2d
    movzx   r2d, byte [topq-1]
    mov [left_topq], r2d
    RET
