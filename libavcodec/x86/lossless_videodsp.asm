;******************************************************************************
;* SIMD lossless video DSP utils
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2014 Michael Niedermayer
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
pb_ef: times 8 db 14,15
pb_67: times 8 db  6, 7
pb_zzzz3333zzzzbbbb: db -1,-1,-1,-1,3,3,3,3,-1,-1,-1,-1,11,11,11,11
pb_zz11zz55zz99zzdd: db -1,-1,1,1,-1,-1,5,5,-1,-1,9,9,-1,-1,13,13
pb_zzzz2323zzzzabab: db -1,-1,-1,-1, 2, 3, 2, 3,-1,-1,-1,-1,10,11,10,11
pb_zzzzzzzz67676767: db -1,-1,-1,-1,-1,-1,-1,-1, 6, 7, 6, 7, 6, 7, 6, 7

SECTION .text

; void ff_add_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
;                                const uint8_t *diff, int w,
;                                int *left, int *left_top)
%macro MEDIAN_PRED 0
cglobal add_median_pred, 6,6,8, dst, top, diff, w, left, left_top
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
MEDIAN_PRED
%endif
INIT_XMM sse2
MEDIAN_PRED


%macro ADD_LEFT_LOOP 2 ; %1 = dst_is_aligned, %2 = src_is_aligned
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

; int ff_add_left_pred(uint8_t *dst, const uint8_t *src, int w, int left)
INIT_MMX ssse3
cglobal add_left_pred, 3,3,7, dst, src, w, left
.skip_prologue:
    mova    m5, [pb_7]
    mova    m4, [pb_zzzz3333zzzzbbbb]
    mova    m3, [pb_zz11zz55zz99zzdd]
    movd    m0, leftm
    psllq   m0, 56
    ADD_LEFT_LOOP 1, 1

INIT_XMM sse4
cglobal add_left_pred, 3,3,7, dst, src, w, left
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
    ADD_LEFT_LOOP 1, 1
.dst_unaligned:
    ADD_LEFT_LOOP 0, 1
.src_unaligned:
    ADD_LEFT_LOOP 0, 0

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
.3:
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

%macro INT16_LOOP 2 ; %1 = a/u (aligned/unaligned), %2 = add/sub
    movd    m4, maskd
    SPLATW  m4, m4
    add     wd, wd
    test    wq, 2*mmsize - 1
    jz %%.tomainloop
    push  tmpq
%%.wordloop:
    sub     wq, 2
%ifidn %2, add
    mov   tmpw, [srcq+wq]
    add   tmpw, [dstq+wq]
%else
    mov   tmpw, [src1q+wq]
    sub   tmpw, [src2q+wq]
%endif
    and   tmpw, maskw
    mov     [dstq+wq], tmpw
    test    wq, 2*mmsize - 1
    jnz %%.wordloop
    pop   tmpq
%%.tomainloop:
%ifidn %2, add
    add     srcq, wq
%else
    add     src1q, wq
    add     src2q, wq
%endif
    add     dstq, wq
    neg     wq
    jz      %%.end
%%.loop:
%ifidn %2, add
    mov%1   m0, [srcq+wq]
    mov%1   m1, [dstq+wq]
    mov%1   m2, [srcq+wq+mmsize]
    mov%1   m3, [dstq+wq+mmsize]
%else
    mov%1   m0, [src1q+wq]
    mov%1   m1, [src2q+wq]
    mov%1   m2, [src1q+wq+mmsize]
    mov%1   m3, [src2q+wq+mmsize]
%endif
    p%2w    m0, m1
    p%2w    m2, m3
    pand    m0, m4
    pand    m2, m4
    mov%1   [dstq+wq]       , m0
    mov%1   [dstq+wq+mmsize], m2
    add     wq, 2*mmsize
    jl %%.loop
%%.end:
    RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
cglobal add_int16, 4,4,5, dst, src, mask, w, tmp
    INT16_LOOP a, add
%endif

INIT_XMM sse2
cglobal add_int16, 4,4,5, dst, src, mask, w, tmp
    test srcq, mmsize-1
    jnz .unaligned
    test dstq, mmsize-1
    jnz .unaligned
    INT16_LOOP a, add
.unaligned:
    INT16_LOOP u, add

%if ARCH_X86_32
INIT_MMX mmx
cglobal diff_int16, 5,5,5, dst, src1, src2, mask, w, tmp
    INT16_LOOP a, sub
%endif

INIT_XMM sse2
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


%macro ADD_HFYU_LEFT_LOOP_INT16 2 ; %1 = dst alignment (a/u), %2 = src alignment (a/u)
    add     wd, wd
    add     srcq, wq
    add     dstq, wq
    neg     wq
%%.loop:
    mov%2   m1, [srcq+wq]
    mova    m2, m1
    pslld   m1, 16
    paddw   m1, m2
    mova    m2, m1

    pshufb  m1, m3
    paddw   m1, m2
    pshufb  m0, m5
%if mmsize == 16
    mova    m2, m1
    pshufb  m1, m4
    paddw   m1, m2
%endif
    paddw   m0, m1
    pand    m0, m7
%ifidn %1, a
    mova    [dstq+wq], m0
%else
    movq    [dstq+wq], m0
    movhps  [dstq+wq+8], m0
%endif
    add     wq, mmsize
    jl %%.loop
    mov     eax, mmsize-1
    sub     eax, wd
    mov     wd, eax
    shl     wd, 8
    lea     eax, [wd+eax-1]
    movd    m1, eax
    pshufb  m0, m1
    movd    eax, m0
    RET
%endmacro

; int add_hfyu_left_pred_int16(uint16_t *dst, const uint16_t *src, unsigned mask, int w, int left)
INIT_MMX ssse3
cglobal add_hfyu_left_pred_int16, 4,4,8, dst, src, mask, w, left
.skip_prologue:
    mova    m5, [pb_67]
    mova    m3, [pb_zzzz2323zzzzabab]
    movd    m0, leftm
    psllq   m0, 48
    movd    m7, maskm
    SPLATW  m7 ,m7
    ADD_HFYU_LEFT_LOOP_INT16 a, a

INIT_XMM sse4
cglobal add_hfyu_left_pred_int16, 4,4,8, dst, src, mask, w, left
    mova    m5, [pb_ef]
    mova    m4, [pb_zzzzzzzz67676767]
    mova    m3, [pb_zzzz2323zzzzabab]
    movd    m0, leftm
    pslldq  m0, 14
    movd    m7, maskm
    SPLATW  m7 ,m7
    test    srcq, 15
    jnz .src_unaligned
    test    dstq, 15
    jnz .dst_unaligned
    ADD_HFYU_LEFT_LOOP_INT16 a, a
.dst_unaligned:
    ADD_HFYU_LEFT_LOOP_INT16 u, a
.src_unaligned:
    ADD_HFYU_LEFT_LOOP_INT16 u, u

; void add_hfyu_median_prediction_mmxext(uint8_t *dst, const uint8_t *top, const uint8_t *diff, int mask, int w, int *left, int *left_top)
INIT_MMX mmxext
cglobal add_hfyu_median_pred_int16, 7,7,0, dst, top, diff, mask, w, left, left_top
    add      wd, wd
    movd    mm6, maskd
    SPLATW  mm6, mm6
    movq    mm0, [topq]
    movq    mm2, mm0
    movd    mm4, [left_topq]
    psllq   mm2, 16
    movq    mm1, mm0
    por     mm4, mm2
    movd    mm3, [leftq]
    psubw   mm0, mm4 ; t-tl
    add    dstq, wq
    add    topq, wq
    add   diffq, wq
    neg      wq
    jmp .skip
.loop:
    movq    mm4, [topq+wq]
    movq    mm0, mm4
    psllq   mm4, 16
    por     mm4, mm1
    movq    mm1, mm0 ; t
    psubw   mm0, mm4 ; t-tl
.skip:
    movq    mm2, [diffq+wq]
%assign i 0
%rep 4
    movq    mm4, mm0
    paddw   mm4, mm3 ; t-tl+l
    pand    mm4, mm6
    movq    mm5, mm3
    pmaxsw  mm3, mm1
    pminsw  mm5, mm1
    pminsw  mm3, mm4
    pmaxsw  mm3, mm5 ; median
    paddw   mm3, mm2 ; +residual
    pand    mm3, mm6
%if i==0
    movq    mm7, mm3
    psllq   mm7, 48
%else
    movq    mm4, mm3
    psrlq   mm7, 16
    psllq   mm4, 48
    por     mm7, mm4
%endif
%if i<3
    psrlq   mm0, 16
    psrlq   mm1, 16
    psrlq   mm2, 16
%endif
%assign i i+1
%endrep
    movq [dstq+wq], mm7
    add      wq, 8
    jl .loop
    movzx   r2d, word [dstq-2]
    mov [leftq], r2d
    movzx   r2d, word [topq-2]
    mov [left_topq], r2d
    RET

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
