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
; void h264_weight(uint8_t *dst, int stride, int log2_denom,
;                  int weight, int offset);
;-----------------------------------------------------------------------------
%ifdef ARCH_X86_32
DECLARE_REG_TMP 2
%else
DECLARE_REG_TMP 10
%endif

%macro WEIGHT_PROLOGUE 1
    mov t0, %1
.prologue
    PROLOGUE 0,5,8
    movifnidn  r0, r0mp
    movifnidn r1d, r1m
    movifnidn r3d, r3m
    movifnidn r4d, r4m
%endmacro

%macro WEIGHT_SETUP 1
    mova       m0, [pw_1]
    movd       m2, r2m
    pslld      m0, m2       ; 1<<log2_denom
    SPLATW     m0, m0
    shl        r4, 19       ; *8, move to upper half of dword
    lea        r4, [r4+r3*2+0x10000]
    movd       m3, r4d      ; weight<<1 | 1+(offset<<(3))
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
cglobal h264_weight_16x16_10_%1
    WEIGHT_PROLOGUE 16
    WEIGHT_SETUP %1
.nextrow
    WEIGHT_OP %1,  0
    mova [r0   ], m5
    WEIGHT_OP %1, 16
    mova [r0+16], m5
    add       r0, r1
    dec       t0
    jnz .nextrow
    REP_RET

cglobal h264_weight_16x8_10_%1
    mov t0, 8
    jmp mangle(ff_h264_weight_16x16_10_%1.prologue)
%endmacro

INIT_XMM
WEIGHT_FUNC_DBL sse2
WEIGHT_FUNC_DBL sse4


%macro WEIGHT_FUNC_MM 1
cglobal h264_weight_8x16_10_%1
    WEIGHT_PROLOGUE 16
    WEIGHT_SETUP %1
.nextrow
    WEIGHT_OP  %1, 0
    mova     [r0], m5
    add        r0, r1
    dec        t0
    jnz .nextrow
    REP_RET

cglobal h264_weight_8x8_10_%1
    mov t0, 8
    jmp mangle(ff_h264_weight_8x16_10_%1.prologue)

cglobal h264_weight_8x4_10_%1
    mov t0, 4
    jmp mangle(ff_h264_weight_8x16_10_%1.prologue)
%endmacro

INIT_XMM
WEIGHT_FUNC_MM sse2
WEIGHT_FUNC_MM sse4


%macro WEIGHT_FUNC_HALF_MM 1
cglobal h264_weight_4x8_10_%1
    WEIGHT_PROLOGUE 4
    WEIGHT_SETUP %1
    lea         r3, [r1*2]
.nextrow
    WEIGHT_OP   %1, 0, r1
    movh      [r0], m5
    movhps [r0+r1], m5
    add         r0, r3
    dec         t0
    jnz .nextrow
    REP_RET

cglobal h264_weight_4x4_10_%1
    mov t0, 2
    jmp mangle(ff_h264_weight_4x8_10_%1.prologue)

cglobal h264_weight_4x2_10_%1
    mov t0, 1
    jmp mangle(ff_h264_weight_4x8_10_%1.prologue)
%endmacro

INIT_XMM
WEIGHT_FUNC_HALF_MM sse2
WEIGHT_FUNC_HALF_MM sse4


;-----------------------------------------------------------------------------
; void h264_biweight(uint8_t *dst, uint8_t *src, int stride, int log2_denom,
;                    int weightd, int weights, int offset);
;-----------------------------------------------------------------------------
%ifdef ARCH_X86_32
DECLARE_REG_TMP 2,3
%else
DECLARE_REG_TMP 10,2
%endif

%macro BIWEIGHT_PROLOGUE 1
    mov t0, %1
.prologue
    PROLOGUE 0,7,8
    movifnidn  r0, r0mp
    movifnidn  r1, r1mp
    movifnidn t1d, r2m
    movifnidn r4d, r4m
    movifnidn r5d, r5m
    movifnidn r6d, r6m
%endmacro

%macro BIWEIGHT_SETUP 1
    lea        r6, [r6*4+1] ; (offset<<2)+1
    or         r6, 1
    shl        r5, 16
    or         r4, r5
    movd       m4, r4d      ; weightd | weights
    movd       m5, r6d      ; (offset+1)|1
    movd       m6, r3m      ; log2_denom
    pslld      m5, m6       ; (((offset<<2)+1)|1)<<log2_denom
    paddd      m6, [sq_1]
    pshufd     m4, m4, 0
    pshufd     m5, m5, 0
    mova       m3, [pw_pixel_max]
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
cglobal h264_biweight_16x16_10_%1
    BIWEIGHT_PROLOGUE 16
    BIWEIGHT_SETUP %1
.nextrow
    BIWEIGHT  %1,  0
    mova [r0   ], m0
    BIWEIGHT  %1, 16
    mova [r0+16], m0
    add       r0, t1
    add       r1, t1
    dec       t0
    jnz .nextrow
    REP_RET

cglobal h264_biweight_16x8_10_%1
    mov t0, 8
    jmp mangle(ff_h264_biweight_16x16_10_%1.prologue)
%endmacro

INIT_XMM
BIWEIGHT_FUNC_DBL sse2
BIWEIGHT_FUNC_DBL sse4

%macro BIWEIGHT_FUNC 1
cglobal h264_biweight_8x16_10_%1
    BIWEIGHT_PROLOGUE 16
    BIWEIGHT_SETUP %1
.nextrow
    BIWEIGHT %1, 0
    mova   [r0], m0
    add      r0, t1
    add      r1, t1
    dec      t0
    jnz .nextrow
    REP_RET

cglobal h264_biweight_8x8_10_%1
    mov t0, 8
    jmp mangle(ff_h264_biweight_8x16_10_%1.prologue)

cglobal h264_biweight_8x4_10_%1
    mov t0, 4
    jmp mangle(ff_h264_biweight_8x16_10_%1.prologue)
%endmacro

INIT_XMM
BIWEIGHT_FUNC sse2
BIWEIGHT_FUNC sse4

%macro BIWEIGHT_FUNC_HALF 1
cglobal h264_biweight_4x8_10_%1
    BIWEIGHT_PROLOGUE 4
    BIWEIGHT_SETUP %1
    lea        r4, [t1*2]
.nextrow
    BIWEIGHT    %1, 0, t1
    movh   [r0   ], m0
    movhps [r0+t1], m0
    add         r0, r4
    add         r1, r4
    dec         t0
    jnz .nextrow
    REP_RET

cglobal h264_biweight_4x4_10_%1
    mov t0, 2
    jmp mangle(ff_h264_biweight_4x8_10_%1.prologue)

cglobal h264_biweight_4x2_10_%1
    mov t0, 1
    jmp mangle(ff_h264_biweight_4x8_10_%1.prologue)
%endmacro

INIT_XMM
BIWEIGHT_FUNC_HALF sse2
BIWEIGHT_FUNC_HALF sse4
