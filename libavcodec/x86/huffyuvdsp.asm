;******************************************************************************
;* SIMD-optimized HuffYUV functions
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
pb_f: times 16 db 15
pb_zzzzzzzz77777777: times 8 db -1
pb_7: times 8 db 7
pb_zzzz3333zzzzbbbb: db -1,-1,-1,-1,3,3,3,3,-1,-1,-1,-1,11,11,11,11
pb_zz11zz55zz99zzdd: db -1,-1,1,1,-1,-1,5,5,-1,-1,9,9,-1,-1,13,13

SECTION .text

; void ff_add_hfyu_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
;                                     const uint8_t *diff, int w,
;                                     int *left, int *left_top)
INIT_MMX mmxext
cglobal add_hfyu_median_pred, 6,6,0, dst, top, diff, w, left, left_top
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


%macro ADD_HFYU_LEFT_LOOP 2 ; %1 = dst_is_aligned, %2 = src_is_aligned
    add     srcq, wq
    add     dstq, wq
    neg     wq
%%.loop:
%if %2
    mova    m1, [srcq+wq]
%else
    movu    m1, [srcq+wq]
%endif
    mova    m2, m1
    psllw   m1, 8
    paddb   m1, m2
    mova    m2, m1
    pshufb  m1, m3
    paddb   m1, m2
    pshufb  m0, m5
    mova    m2, m1
    pshufb  m1, m4
    paddb   m1, m2
%if mmsize == 16
    mova    m2, m1
    pshufb  m1, m6
    paddb   m1, m2
%endif
    paddb   m0, m1
%if %1
    mova    [dstq+wq], m0
%else
    movq    [dstq+wq], m0
    movhps  [dstq+wq+8], m0
%endif
    add     wq, mmsize
    jl %%.loop
    mov     eax, mmsize-1
    sub     eax, wd
    movd    m1, eax
    pshufb  m0, m1
    movd    eax, m0
    RET
%endmacro

; int ff_add_hfyu_left_pred(uint8_t *dst, const uint8_t *src, int w, int left)
INIT_MMX ssse3
cglobal add_hfyu_left_pred, 3,3,7, dst, src, w, left
.skip_prologue:
    mova    m5, [pb_7]
    mova    m4, [pb_zzzz3333zzzzbbbb]
    mova    m3, [pb_zz11zz55zz99zzdd]
    movd    m0, leftm
    psllq   m0, 56
    ADD_HFYU_LEFT_LOOP 1, 1

INIT_XMM ssse3
cglobal add_hfyu_left_pred_unaligned, 3,3,7, dst, src, w, left
    mova    m5, [pb_f]
    mova    m6, [pb_zzzzzzzz77777777]
    mova    m4, [pb_zzzz3333zzzzbbbb]
    mova    m3, [pb_zz11zz55zz99zzdd]
    movd    m0, leftm
    pslldq  m0, 15
    test    srcq, 15
    jnz .src_unaligned
    test    dstq, 15
    jnz .dst_unaligned
    ADD_HFYU_LEFT_LOOP 1, 1
.dst_unaligned:
    ADD_HFYU_LEFT_LOOP 0, 1
.src_unaligned:
    ADD_HFYU_LEFT_LOOP 0, 0
