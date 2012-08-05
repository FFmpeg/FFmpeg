;*****************************************************************************
;* MMX/SSE2/AVX-optimized 10-bit H.264 weighted prediction code
;*****************************************************************************
;* Copyright (C) 2005-2011 x264 project
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA 32

pw_pixel_max: times 8 dw ((1 << 10)-1)
sq_1: dq 1
      dq 0

cextern pw_1

SECTION .text

;-----------------------------------------------------------------------------
; void h264_weight(uint8_t *dst, int stride, int height, int log2_denom,
;                  int weight, int offset);
;-----------------------------------------------------------------------------
%macro WEIGHT_PROLOGUE 0
.prologue:
    PROLOGUE 0,6,8
    movifnidn  r0, r0mp
    movifnidn r1d, r1m
    movifnidn r2d, r2m
    movifnidn r4d, r4m
    movifnidn r5d, r5m
%endmacro

%macro WEIGHT_SETUP 1
    mova       m0, [pw_1]
    movd       m2, r3m
    pslld      m0, m2       ; 1<<log2_denom
    SPLATW     m0, m0
    shl        r5, 19       ; *8, move to upper half of dword
    lea        r5, [r5+r4*2+0x10000]
    movd       m3, r5d      ; weight<<1 | 1+(offset<<(3))
    pshufd     m3, m3, 0
    mova       m4, [pw_pixel_max]
    paddw      m2, [sq_1]   ; log2_denom+1
%ifnidn %1, sse4
    pxor       m7, m7
%endif
%endmacro

%macro WEIGHT_OP 2-3
%if %0==2
    mova        m5, [r0+%2]
    punpckhwd   m6, m5, m0
    punpcklwd   m5, m0
%else
    movq        m5, [r0+%2]
    movq        m6, [r0+%3]
    punpcklwd   m5, m0
    punpcklwd   m6, m0
%endif
    pmaddwd     m5, m3
    pmaddwd     m6, m3
    psrad       m5, m2
    psrad       m6, m2
%ifidn %1, sse4
    packusdw    m5, m6
    pminsw      m5, m4
%else
    packssdw    m5, m6
    CLIPW       m5, m7, m4
%endif
%endmacro

%macro WEIGHT_FUNC_DBL 1
cglobal h264_weight_16_10_%1
    WEIGHT_PROLOGUE
    WEIGHT_SETUP %1
.nextrow:
    WEIGHT_OP %1,  0
    mova [r0   ], m5
    WEIGHT_OP %1, 16
    mova [r0+16], m5
    add       r0, r1
    dec       r2d
    jnz .nextrow
    REP_RET
%endmacro

INIT_XMM
WEIGHT_FUNC_DBL sse2
WEIGHT_FUNC_DBL sse4


%macro WEIGHT_FUNC_MM 1
cglobal h264_weight_8_10_%1
    WEIGHT_PROLOGUE
    WEIGHT_SETUP %1
.nextrow:
    WEIGHT_OP  %1, 0
    mova     [r0], m5
    add        r0, r1
    dec        r2d
    jnz .nextrow
    REP_RET
%endmacro

INIT_XMM
WEIGHT_FUNC_MM sse2
WEIGHT_FUNC_MM sse4


%macro WEIGHT_FUNC_HALF_MM 1
cglobal h264_weight_4_10_%1
    WEIGHT_PROLOGUE
    sar         r2d, 1
    WEIGHT_SETUP %1
    lea         r3, [r1*2]
.nextrow:
    WEIGHT_OP   %1, 0, r1
    movh      [r0], m5
    movhps [r0+r1], m5
    add         r0, r3
    dec         r2d
    jnz .nextrow
    REP_RET
%endmacro

INIT_XMM
WEIGHT_FUNC_HALF_MM sse2
WEIGHT_FUNC_HALF_MM sse4


;-----------------------------------------------------------------------------
; void h264_biweight(uint8_t *dst, uint8_t *src, int stride, int height,
;                    int log2_denom, int weightd, int weights, int offset);
;-----------------------------------------------------------------------------
%if ARCH_X86_32
DECLARE_REG_TMP 3
%else
DECLARE_REG_TMP 7
%endif

%macro BIWEIGHT_PROLOGUE 0
.prologue:
    PROLOGUE 0,8,8
    movifnidn  r0, r0mp
    movifnidn  r1, r1mp
    movifnidn r2d, r2m
    movifnidn r5d, r5m
    movifnidn r6d, r6m
    movifnidn t0d, r7m
%endmacro

%macro BIWEIGHT_SETUP 1
    lea        t0, [t0*4+1] ; (offset<<2)+1
    or         t0, 1
    shl        r6, 16
    or         r5, r6
    movd       m4, r5d      ; weightd | weights
    movd       m5, t0d      ; (offset+1)|1
    movd       m6, r4m      ; log2_denom
    pslld      m5, m6       ; (((offset<<2)+1)|1)<<log2_denom
    paddd      m6, [sq_1]
    pshufd     m4, m4, 0
    pshufd     m5, m5, 0
    mova       m3, [pw_pixel_max]
    movifnidn r3d, r3m
%ifnidn %1, sse4
    pxor       m7, m7
%endif
%endmacro

%macro BIWEIGHT 2-3
%if %0==2
    mova       m0, [r0+%2]
    mova       m1, [r1+%2]
    punpckhwd  m2, m0, m1
    punpcklwd  m0, m1
%else
    movq       m0, [r0+%2]
    movq       m1, [r1+%2]
    punpcklwd  m0, m1
    movq       m2, [r0+%3]
    movq       m1, [r1+%3]
    punpcklwd  m2, m1
%endif
    pmaddwd    m0, m4
    pmaddwd    m2, m4
    paddd      m0, m5
    paddd      m2, m5
    psrad      m0, m6
    psrad      m2, m6
%ifidn %1, sse4
    packusdw   m0, m2
    pminsw     m0, m3
%else
    packssdw   m0, m2
    CLIPW      m0, m7, m3
%endif
%endmacro

%macro BIWEIGHT_FUNC_DBL 1
cglobal h264_biweight_16_10_%1
    BIWEIGHT_PROLOGUE
    BIWEIGHT_SETUP %1
.nextrow:
    BIWEIGHT  %1,  0
    mova [r0   ], m0
    BIWEIGHT  %1, 16
    mova [r0+16], m0
    add       r0, r2
    add       r1, r2
    dec       r3d
    jnz .nextrow
    REP_RET
%endmacro

INIT_XMM
BIWEIGHT_FUNC_DBL sse2
BIWEIGHT_FUNC_DBL sse4

%macro BIWEIGHT_FUNC 1
cglobal h264_biweight_8_10_%1
    BIWEIGHT_PROLOGUE
    BIWEIGHT_SETUP %1
.nextrow:
    BIWEIGHT %1, 0
    mova   [r0], m0
    add      r0, r2
    add      r1, r2
    dec      r3d
    jnz .nextrow
    REP_RET
%endmacro

INIT_XMM
BIWEIGHT_FUNC sse2
BIWEIGHT_FUNC sse4

%macro BIWEIGHT_FUNC_HALF 1
cglobal h264_biweight_4_10_%1
    BIWEIGHT_PROLOGUE
    BIWEIGHT_SETUP %1
    sar        r3d, 1
    lea        r4, [r2*2]
.nextrow:
    BIWEIGHT    %1, 0, r2
    movh   [r0   ], m0
    movhps [r0+r2], m0
    add         r0, r4
    add         r1, r4
    dec         r3d
    jnz .nextrow
    REP_RET
%endmacro

INIT_XMM
BIWEIGHT_FUNC_HALF sse2
BIWEIGHT_FUNC_HALF sse4
