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

SECTION .text

%include "libavcodec/x86/huffyuvdsp_template.asm"

;------------------------------------------------------------------------------
; void (*add_int16)(uint16_t *dst, const uint16_t *src, unsigned mask, int w);
;------------------------------------------------------------------------------

%macro ADD_INT16 0
cglobal add_int16, 4,4,5, dst, src, mask, w, tmp
    INT16_LOOP a, add
%endmacro

INIT_XMM sse2
ADD_INT16

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
ADD_INT16
%endif

; void add_hfyu_left_pred_bgr32(uint8_t *dst, const uint8_t *src,
;                               intptr_t w, uint8_t *left)
INIT_XMM sse2
cglobal add_hfyu_left_pred_bgr32, 4,4,3, dst, src, w, left
    shl           wq, 2
    movd          m0, [leftq]
    lea         dstq, [dstq + wq]
    lea         srcq, [srcq + wq]
    pslldq        m0, mmsize-4
    neg           wq
.loop:
    movu          m1, [srcq+wq]
    mova          m2, m1
    pslldq        m1, 4
    paddb         m1, m2
    pshufd        m0, m0, q3333
    mova          m2, m1
    pslldq        m1, 8
    paddb         m1, m2
    paddb         m0, m1
    movu   [dstq+wq], m0
    add           wq, mmsize
    jl         .loop
    movd          m0, [dstq-4]
    movd     [leftq], m0
    RET


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

; void ff_add_hfyu_median_pred_sse4(uint16_t *dst, const uint16_t *top,
;                                   const uint16_t *diff, int mask,
;                                   int w, int *left, int *left_top)
INIT_XMM sse4
cglobal add_hfyu_median_pred_int16, 7,7,8, dst, top, diff, mask, w, left, left_top
    movq     m1, [topq]
    movd     m4, [left_topq]
    add      wd, wd
    movd     m6, maskd
    add   diffq, wq
    psllq    m2, m1, 16
    movd     m3, [leftq]
    add    topq, wq
    por      m4, m2
    add    dstq, wq
    psubw    m0, m1, m4         ; t-tl
    neg      wq
    jmp .skip
.loop:
%if avx_enabled
    movq     m1, [topq+wq]  ; t
    psllq    m0, m1, 16
    por      m0, m4
    psubw    m0, m1, m0     ; t-tl
%else
    movq     m4, [topq+wq]
    mova     m0, m4
    psllq    m4, 16
    por      m4, m1
    mova     m1, m0         ; t
    psubw    m0, m4         ; t-tl
%endif
.skip:
    movq     m2, [diffq+wq]
%assign i 0
%rep 4
%if i<3
    paddw    m4, m0, m3     ; t-tl+l
%else
    SWAP     0, 4
    paddw    m4, m3         ; t-tl+l
%endif
%if avx_enabled
    SWAP     3,5
    pmaxuw   m3, m5, m1
%else
    mova     m5, m3
    pmaxuw   m3, m1
%endif
%if i==2
    punpcklwd m7, m5
%elif i==3
    punpckldq m7, m5
%endif
    pand     m4, m6
    pminuw   m5, m1
    pminuw   m3, m4
    pmaxuw   m3, m5         ; median
    paddw    m3, m2         ; +residual
    pand     m3, m6
%if i==0
    mova     m7, m3
%elif i == 3
    pshuflw  m4, m3, 0
    pblendw  m7, m4, 1000b
%endif
%if i<3
    psrlq    m0, 16
%if avx_enabled && i == 2
    psrlq    m4, m1, 16
    SWAP      1, 4
%else
    psrlq    m1, 16
%endif
    psrlq    m2, 16
%endif
%assign i i+1
%endrep
    movq [dstq+wq], m7
    add      wq, 8
    jl .loop
    movzx   r2d, word [dstq-2]
    mov [leftq], r2d
    movzx   r2d, word [topq-2]
    mov [left_topq], r2d
    RET
