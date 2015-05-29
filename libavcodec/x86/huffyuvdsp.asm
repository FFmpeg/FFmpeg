;******************************************************************************
;* SIMD-optimized HuffYUV functions
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2014 Christophe Gisquet
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

SECTION_RODATA
cextern pb_15
pb_zzzzzzzz77777777: times 8 db -1
pb_7: times 8 db 7
pb_zzzz3333zzzzbbbb: db -1,-1,-1,-1,3,3,3,3,-1,-1,-1,-1,11,11,11,11
pb_zz11zz55zz99zzdd: db -1,-1,1,1,-1,-1,5,5,-1,-1,9,9,-1,-1,13,13

SECTION_TEXT

; void ff_add_hfyu_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
;                                     const uint8_t *diff, int w,
;                                     int *left, int *left_top)
%macro HFYU_MEDIAN 0
cglobal add_hfyu_median_pred, 6,6,8, dst, top, diff, w, left, left_top
    movu    m0, [topq]
    mova    m2, m0
    movd    m4, [left_topq]
    LSHIFT  m2, 1
    mova    m1, m0
    por     m4, m2
    movd    m3, [leftq]
    psubb   m0, m4 ; t-tl
    add    dstq, wq
    add    topq, wq
    add   diffq, wq
    neg      wq
    jmp .skip
.loop:
    movu    m4, [topq+wq]
    mova    m0, m4
    LSHIFT  m4, 1
    por     m4, m1
    mova    m1, m0 ; t
    psubb   m0, m4 ; t-tl
.skip:
    movu    m2, [diffq+wq]
%assign i 0
%rep mmsize
    mova    m4, m0
    paddb   m4, m3 ; t-tl+l
    mova    m5, m3
    pmaxub  m3, m1
    pminub  m5, m1
    pminub  m3, m4
    pmaxub  m3, m5 ; median
    paddb   m3, m2 ; +residual
%if i==0
    mova    m7, m3
    LSHIFT  m7, mmsize-1
%else
    mova    m6, m3
    RSHIFT  m7, 1
    LSHIFT  m6, mmsize-1
    por     m7, m6
%endif
%if i<mmsize-1
    RSHIFT  m0, 1
    RSHIFT  m1, 1
    RSHIFT  m2, 1
%endif
%assign i i+1
%endrep
    movu [dstq+wq], m7
    add      wq, mmsize
    jl .loop
    movzx   r2d, byte [dstq-1]
    mov [leftq], r2d
    movzx   r2d, byte [topq-1]
    mov [left_topq], r2d
    RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmxext
HFYU_MEDIAN
%endif
INIT_XMM sse2
HFYU_MEDIAN


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

INIT_XMM sse4
cglobal add_hfyu_left_pred, 3,3,7, dst, src, w, left
    mova    m5, [pb_15]
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

%macro ADD_BYTES 0
cglobal add_bytes, 3,4,2, dst, src, w, size
    mov  sizeq, wq
    and  sizeq, -2*mmsize
    jz  .2
    add   dstq, sizeq
    add   srcq, sizeq
    neg  sizeq
.1:
    mova    m0, [srcq + sizeq]
    mova    m1, [srcq + sizeq + mmsize]
    paddb   m0, [dstq + sizeq]
    paddb   m1, [dstq + sizeq + mmsize]
    mova   [dstq + sizeq], m0
    mova   [dstq + sizeq + mmsize], m1
    add  sizeq, 2*mmsize
    jl .1
.2:
    and     wq, 2*mmsize-1
    jz    .end
    add   dstq, wq
    add   srcq, wq
    neg     wq
.3
    mov  sizeb, [srcq + wq]
    add [dstq + wq], sizeb
    inc     wq
    jl .3
.end:
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
ADD_BYTES
%endif
INIT_XMM sse2
ADD_BYTES

; void add_hfyu_left_pred_bgr32(uint8_t *dst, const uint8_t *src,
;                               intptr_t w, uint8_t *left)
%macro LEFT_BGR32 0
cglobal add_hfyu_left_pred_bgr32, 4,4,3, dst, src, w, left
    shl           wq, 2
    movd          m0, [leftq]
    lea         dstq, [dstq + wq]
    lea         srcq, [srcq + wq]
    LSHIFT        m0, mmsize-4
    neg           wq
.loop:
    movu          m1, [srcq+wq]
    mova          m2, m1
%if mmsize == 8
    punpckhdq     m0, m0
%endif
    LSHIFT        m1, 4
    paddb         m1, m2
%if mmsize == 16
    pshufd        m0, m0, q3333
    mova          m2, m1
    LSHIFT        m1, 8
    paddb         m1, m2
%endif
    paddb         m0, m1
    movu   [dstq+wq], m0
    add           wq, mmsize
    jl         .loop
    movd          m0, [dstq-4]
    movd     [leftq], m0
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
LEFT_BGR32
%endif
INIT_XMM sse2
LEFT_BGR32
