;******************************************************************************
;* MMX optimized DSP utils
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2003-2013 Michael Niedermayer
;* Copyright (c) 2013 Daniel Kang
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
pb_f: times 16 db 15
pb_zzzzzzzz77777777: times 8 db -1
pb_7: times 8 db 7
pb_zzzz3333zzzzbbbb: db -1,-1,-1,-1,3,3,3,3,-1,-1,-1,-1,11,11,11,11
pb_zz11zz55zz99zzdd: db -1,-1,1,1,-1,-1,5,5,-1,-1,9,9,-1,-1,13,13
pb_revwords: SHUFFLE_MASK_W 7, 6, 5, 4, 3, 2, 1, 0
pd_16384: times 4 dd 16384
pb_bswap32: db 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12

SECTION_TEXT

%macro SCALARPRODUCT 0
; int scalarproduct_int16(int16_t *v1, int16_t *v2, int order)
cglobal scalarproduct_int16, 3,3,3, v1, v2, order
    shl orderq, 1
    add v1q, orderq
    add v2q, orderq
    neg orderq
    pxor    m2, m2
.loop:
    movu    m0, [v1q + orderq]
    movu    m1, [v1q + orderq + mmsize]
    pmaddwd m0, [v2q + orderq]
    pmaddwd m1, [v2q + orderq + mmsize]
    paddd   m2, m0
    paddd   m2, m1
    add     orderq, mmsize*2
    jl .loop
%if mmsize == 16
    movhlps m0, m2
    paddd   m2, m0
    pshuflw m0, m2, 0x4e
%else
    pshufw  m0, m2, 0x4e
%endif
    paddd   m2, m0
    movd   eax, m2
%if mmsize == 8
    emms
%endif
    RET

; int scalarproduct_and_madd_int16(int16_t *v1, int16_t *v2, int16_t *v3, int order, int mul)
cglobal scalarproduct_and_madd_int16, 4,4,8, v1, v2, v3, order, mul
    shl orderq, 1
    movd    m7, mulm
%if mmsize == 16
    pshuflw m7, m7, 0
    punpcklqdq m7, m7
%else
    pshufw  m7, m7, 0
%endif
    pxor    m6, m6
    add v1q, orderq
    add v2q, orderq
    add v3q, orderq
    neg orderq
.loop:
    movu    m0, [v2q + orderq]
    movu    m1, [v2q + orderq + mmsize]
    mova    m4, [v1q + orderq]
    mova    m5, [v1q + orderq + mmsize]
    movu    m2, [v3q + orderq]
    movu    m3, [v3q + orderq + mmsize]
    pmaddwd m0, m4
    pmaddwd m1, m5
    pmullw  m2, m7
    pmullw  m3, m7
    paddd   m6, m0
    paddd   m6, m1
    paddw   m2, m4
    paddw   m3, m5
    mova    [v1q + orderq], m2
    mova    [v1q + orderq + mmsize], m3
    add     orderq, mmsize*2
    jl .loop
%if mmsize == 16
    movhlps m0, m6
    paddd   m6, m0
    pshuflw m0, m6, 0x4e
%else
    pshufw  m0, m6, 0x4e
%endif
    paddd   m6, m0
    movd   eax, m6
    RET
%endmacro

INIT_MMX mmxext
SCALARPRODUCT
INIT_XMM sse2
SCALARPRODUCT

%macro SCALARPRODUCT_LOOP 1
align 16
.loop%1:
    sub     orderq, mmsize*2
%if %1
    mova    m1, m4
    mova    m4, [v2q + orderq]
    mova    m0, [v2q + orderq + mmsize]
    palignr m1, m0, %1
    palignr m0, m4, %1
    mova    m3, m5
    mova    m5, [v3q + orderq]
    mova    m2, [v3q + orderq + mmsize]
    palignr m3, m2, %1
    palignr m2, m5, %1
%else
    mova    m0, [v2q + orderq]
    mova    m1, [v2q + orderq + mmsize]
    mova    m2, [v3q + orderq]
    mova    m3, [v3q + orderq + mmsize]
%endif
    %define t0  [v1q + orderq]
    %define t1  [v1q + orderq + mmsize]
%if ARCH_X86_64
    mova    m8, t0
    mova    m9, t1
    %define t0  m8
    %define t1  m9
%endif
    pmaddwd m0, t0
    pmaddwd m1, t1
    pmullw  m2, m7
    pmullw  m3, m7
    paddw   m2, t0
    paddw   m3, t1
    paddd   m6, m0
    paddd   m6, m1
    mova    [v1q + orderq], m2
    mova    [v1q + orderq + mmsize], m3
    jg .loop%1
%if %1
    jmp .end
%endif
%endmacro

; int scalarproduct_and_madd_int16(int16_t *v1, int16_t *v2, int16_t *v3, int order, int mul)
INIT_XMM ssse3
cglobal scalarproduct_and_madd_int16, 4,5,10, v1, v2, v3, order, mul
    shl orderq, 1
    movd    m7, mulm
    pshuflw m7, m7, 0
    punpcklqdq m7, m7
    pxor    m6, m6
    mov    r4d, v2d
    and    r4d, 15
    and    v2q, ~15
    and    v3q, ~15
    mova    m4, [v2q + orderq]
    mova    m5, [v3q + orderq]
    ; linear is faster than branch tree or jump table, because the branches taken are cyclic (i.e. predictable)
    cmp    r4d, 0
    je .loop0
    cmp    r4d, 2
    je .loop2
    cmp    r4d, 4
    je .loop4
    cmp    r4d, 6
    je .loop6
    cmp    r4d, 8
    je .loop8
    cmp    r4d, 10
    je .loop10
    cmp    r4d, 12
    je .loop12
SCALARPRODUCT_LOOP 14
SCALARPRODUCT_LOOP 12
SCALARPRODUCT_LOOP 10
SCALARPRODUCT_LOOP 8
SCALARPRODUCT_LOOP 6
SCALARPRODUCT_LOOP 4
SCALARPRODUCT_LOOP 2
SCALARPRODUCT_LOOP 0
.end:
    movhlps m0, m6
    paddd   m6, m0
    pshuflw m0, m6, 0x4e
    paddd   m6, m0
    movd   eax, m6
    RET


;-----------------------------------------------------------------------------
; void ff_apply_window_int16(int16_t *output, const int16_t *input,
;                            const int16_t *window, unsigned int len)
;-----------------------------------------------------------------------------

%macro REVERSE_WORDS 1-2
%if cpuflag(ssse3) && notcpuflag(atom)
    pshufb  %1, %2
%elif cpuflag(sse2)
    pshuflw  %1, %1, 0x1B
    pshufhw  %1, %1, 0x1B
    pshufd   %1, %1, 0x4E
%elif cpuflag(mmxext)
    pshufw   %1, %1, 0x1B
%endif
%endmacro

%macro MUL16FIXED 3
%if cpuflag(ssse3) ; dst, src, unused
; dst = ((dst * src) + (1<<14)) >> 15
    pmulhrsw   %1, %2
%elif cpuflag(mmxext) ; dst, src, temp
; dst = (dst * src) >> 15
; pmulhw cuts off the bottom bit, so we have to lshift by 1 and add it back
; in from the pmullw result.
    mova    %3, %1
    pmulhw  %1, %2
    pmullw  %3, %2
    psrlw   %3, 15
    psllw   %1, 1
    por     %1, %3
%endif
%endmacro

%macro APPLY_WINDOW_INT16 1 ; %1 bitexact version
%if %1
cglobal apply_window_int16, 4,5,6, output, input, window, offset, offset2
%else
cglobal apply_window_int16_round, 4,5,6, output, input, window, offset, offset2
%endif
    lea     offset2q, [offsetq-mmsize]
%if cpuflag(ssse3) && notcpuflag(atom)
    mova          m5, [pb_revwords]
    ALIGN 16
%elif %1
    mova          m5, [pd_16384]
%endif
.loop:
%if cpuflag(ssse3)
    ; This version does the 16x16->16 multiplication in-place without expanding
    ; to 32-bit. The ssse3 version is bit-identical.
    mova          m0, [windowq+offset2q]
    mova          m1, [ inputq+offset2q]
    pmulhrsw      m1, m0
    REVERSE_WORDS m0, m5
    pmulhrsw      m0, [ inputq+offsetq ]
    mova  [outputq+offset2q], m1
    mova  [outputq+offsetq ], m0
%elif %1
    ; This version expands 16-bit to 32-bit, multiplies by the window,
    ; adds 16384 for rounding, right shifts 15, then repacks back to words to
    ; save to the output. The window is reversed for the second half.
    mova          m3, [windowq+offset2q]
    mova          m4, [ inputq+offset2q]
    pxor          m0, m0
    punpcklwd     m0, m3
    punpcklwd     m1, m4
    pmaddwd       m0, m1
    paddd         m0, m5
    psrad         m0, 15
    pxor          m2, m2
    punpckhwd     m2, m3
    punpckhwd     m1, m4
    pmaddwd       m2, m1
    paddd         m2, m5
    psrad         m2, 15
    packssdw      m0, m2
    mova  [outputq+offset2q], m0
    REVERSE_WORDS m3
    mova          m4, [ inputq+offsetq]
    pxor          m0, m0
    punpcklwd     m0, m3
    punpcklwd     m1, m4
    pmaddwd       m0, m1
    paddd         m0, m5
    psrad         m0, 15
    pxor          m2, m2
    punpckhwd     m2, m3
    punpckhwd     m1, m4
    pmaddwd       m2, m1
    paddd         m2, m5
    psrad         m2, 15
    packssdw      m0, m2
    mova  [outputq+offsetq], m0
%else
    ; This version does the 16x16->16 multiplication in-place without expanding
    ; to 32-bit. The mmxext and sse2 versions do not use rounding, and
    ; therefore are not bit-identical to the C version.
    mova          m0, [windowq+offset2q]
    mova          m1, [ inputq+offset2q]
    mova          m2, [ inputq+offsetq ]
    MUL16FIXED    m1, m0, m3
    REVERSE_WORDS m0
    MUL16FIXED    m2, m0, m3
    mova  [outputq+offset2q], m1
    mova  [outputq+offsetq ], m2
%endif
    add      offsetd, mmsize
    sub     offset2d, mmsize
    jae .loop
    REP_RET
%endmacro

INIT_MMX mmxext
APPLY_WINDOW_INT16 0
INIT_XMM sse2
APPLY_WINDOW_INT16 0

INIT_MMX mmxext
APPLY_WINDOW_INT16 1
INIT_XMM sse2
APPLY_WINDOW_INT16 1
INIT_XMM ssse3
APPLY_WINDOW_INT16 1
INIT_XMM ssse3, atom
APPLY_WINDOW_INT16 1


; void add_hfyu_median_prediction_mmxext(uint8_t *dst, const uint8_t *top, const uint8_t *diff, int w, int *left, int *left_top)
INIT_MMX mmxext
cglobal add_hfyu_median_prediction, 6,6,0, dst, top, diff, w, left, left_top
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

; int add_hfyu_left_prediction(uint8_t *dst, const uint8_t *src, int w, int left)
INIT_MMX ssse3
cglobal add_hfyu_left_prediction, 3,3,7, dst, src, w, left
.skip_prologue:
    mova    m5, [pb_7]
    mova    m4, [pb_zzzz3333zzzzbbbb]
    mova    m3, [pb_zz11zz55zz99zzdd]
    movd    m0, leftm
    psllq   m0, 56
    ADD_HFYU_LEFT_LOOP 1, 1

INIT_XMM sse4
cglobal add_hfyu_left_prediction, 3,3,7, dst, src, w, left
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


;-----------------------------------------------------------------------------
; void ff_vector_clip_int32(int32_t *dst, const int32_t *src, int32_t min,
;                           int32_t max, unsigned int len)
;-----------------------------------------------------------------------------

; %1 = number of xmm registers used
; %2 = number of inline load/process/store loops per asm loop
; %3 = process 4*mmsize (%3=0) or 8*mmsize (%3=1) bytes per loop
; %4 = CLIPD function takes min/max as float instead of int (CLIPD_SSE2)
; %5 = suffix
%macro VECTOR_CLIP_INT32 4-5
cglobal vector_clip_int32%5, 5,5,%1, dst, src, min, max, len
%if %4
    cvtsi2ss  m4, minm
    cvtsi2ss  m5, maxm
%else
    movd      m4, minm
    movd      m5, maxm
%endif
    SPLATD    m4
    SPLATD    m5
.loop:
%assign %%i 1
%rep %2
    mova      m0,  [srcq+mmsize*0*%%i]
    mova      m1,  [srcq+mmsize*1*%%i]
    mova      m2,  [srcq+mmsize*2*%%i]
    mova      m3,  [srcq+mmsize*3*%%i]
%if %3
    mova      m7,  [srcq+mmsize*4*%%i]
    mova      m8,  [srcq+mmsize*5*%%i]
    mova      m9,  [srcq+mmsize*6*%%i]
    mova      m10, [srcq+mmsize*7*%%i]
%endif
    CLIPD  m0,  m4, m5, m6
    CLIPD  m1,  m4, m5, m6
    CLIPD  m2,  m4, m5, m6
    CLIPD  m3,  m4, m5, m6
%if %3
    CLIPD  m7,  m4, m5, m6
    CLIPD  m8,  m4, m5, m6
    CLIPD  m9,  m4, m5, m6
    CLIPD  m10, m4, m5, m6
%endif
    mova  [dstq+mmsize*0*%%i], m0
    mova  [dstq+mmsize*1*%%i], m1
    mova  [dstq+mmsize*2*%%i], m2
    mova  [dstq+mmsize*3*%%i], m3
%if %3
    mova  [dstq+mmsize*4*%%i], m7
    mova  [dstq+mmsize*5*%%i], m8
    mova  [dstq+mmsize*6*%%i], m9
    mova  [dstq+mmsize*7*%%i], m10
%endif
%assign %%i %%i+1
%endrep
    add     srcq, mmsize*4*(%2+%3)
    add     dstq, mmsize*4*(%2+%3)
    sub     lend, mmsize*(%2+%3)
    jg .loop
    REP_RET
%endmacro

INIT_MMX mmx
%define CLIPD CLIPD_MMX
VECTOR_CLIP_INT32 0, 1, 0, 0
INIT_XMM sse2
VECTOR_CLIP_INT32 6, 1, 0, 0, _int
%define CLIPD CLIPD_SSE2
VECTOR_CLIP_INT32 6, 2, 0, 1
INIT_XMM sse4
%define CLIPD CLIPD_SSE41
%ifdef m8
VECTOR_CLIP_INT32 11, 1, 1, 0
%else
VECTOR_CLIP_INT32 6, 1, 0, 0
%endif

; %1 = aligned/unaligned
%macro BSWAP_LOOPS  1
    mov      r3, r2
    sar      r2, 3
    jz       .left4_%1
.loop8_%1:
    mov%1    m0, [r1 +  0]
    mov%1    m1, [r1 + 16]
%if cpuflag(ssse3)
    pshufb   m0, m2
    pshufb   m1, m2
    mov%1    [r0 +  0], m0
    mov%1    [r0 + 16], m1
%else
    pshuflw  m0, m0, 10110001b
    pshuflw  m1, m1, 10110001b
    pshufhw  m0, m0, 10110001b
    pshufhw  m1, m1, 10110001b
    mova     m2, m0
    mova     m3, m1
    psllw    m0, 8
    psllw    m1, 8
    psrlw    m2, 8
    psrlw    m3, 8
    por      m2, m0
    por      m3, m1
    mov%1    [r0 +  0], m2
    mov%1    [r0 + 16], m3
%endif
    add      r0, 32
    add      r1, 32
    dec      r2
    jnz      .loop8_%1
.left4_%1:
    mov      r2, r3
    and      r3, 4
    jz       .left
    mov%1    m0, [r1]
%if cpuflag(ssse3)
    pshufb   m0, m2
    mov%1    [r0], m0
%else
    pshuflw  m0, m0, 10110001b
    pshufhw  m0, m0, 10110001b
    mova     m2, m0
    psllw    m0, 8
    psrlw    m2, 8
    por      m2, m0
    mov%1    [r0], m2
%endif
    add      r1, 16
    add      r0, 16
%endmacro

; void bswap_buf(uint32_t *dst, const uint32_t *src, int w);
%macro BSWAP32_BUF 0
%if cpuflag(ssse3)
cglobal bswap32_buf, 3,4,3
    mov      r3, r1
    mova     m2, [pb_bswap32]
%else
cglobal bswap32_buf, 3,4,5
    mov      r3, r1
%endif
    or       r3, r0
    and      r3, 15
    jz       .start_align
    BSWAP_LOOPS  u
    jmp      .left
.start_align:
    BSWAP_LOOPS  a
.left:
%if cpuflag(ssse3)
    mov      r3, r2
    and      r2, 2
    jz       .left1
    movq     m0, [r1]
    pshufb   m0, m2
    movq     [r0], m0
    add      r1, 8
    add      r0, 8
.left1:
    and      r3, 1
    jz       .end
    mov      r2d, [r1]
    bswap    r2d
    mov      [r0], r2d
%else
    and      r2, 3
    jz       .end
.loop2:
    mov      r3d, [r1]
    bswap    r3d
    mov      [r0], r3d
    add      r1, 4
    add      r0, 4
    dec      r2
    jnz      .loop2
%endif
.end:
    RET
%endmacro

INIT_XMM sse2
BSWAP32_BUF

INIT_XMM ssse3
BSWAP32_BUF
