;*****************************************************************************
;* SSE2-optimized weighted prediction code
;*****************************************************************************
;* Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
;* Copyright (C) 2010 Eli Friedman <eli.friedman@gmail.com>
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

SECTION .text

;-----------------------------------------------------------------------------
; biweight pred:
;
; void h264_biweight_16x16_sse2(uint8_t *dst, uint8_t *src, int stride,
;                               int log2_denom, int weightd, int weights,
;                               int offset);
; and
; void h264_weight_16x16_sse2(uint8_t *dst, int stride,
;                             int log2_denom, int weight,
;                             int offset);
;-----------------------------------------------------------------------------

%macro WEIGHT_SETUP 0
    add        r4, r4
    inc        r4
    movd       m3, r3d
    movd       m5, r4d
    movd       m6, r2d
    pslld      m5, m6
    psrld      m5, 1
%if mmsize == 16
    pshuflw    m3, m3, 0
    pshuflw    m5, m5, 0
    punpcklqdq m3, m3
    punpcklqdq m5, m5
%else
    pshufw     m3, m3, 0
    pshufw     m5, m5, 0
%endif
    pxor       m7, m7
%endmacro

%macro WEIGHT_OP 2
    movh          m0, [r0+%1]
    movh          m1, [r0+%2]
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    pmullw        m0, m3
    pmullw        m1, m3
    paddsw        m0, m5
    paddsw        m1, m5
    psraw         m0, m6
    psraw         m1, m6
    packuswb      m0, m1
%endmacro

%macro WEIGHT_FUNC_DBL_MM 1
cglobal h264_weight_16x%1_mmx2, 5, 5, 0
    WEIGHT_SETUP
    mov        r2, %1
%if %1 == 16
.nextrow
    WEIGHT_OP 0,  4
    mova     [r0  ], m0
    WEIGHT_OP 8, 12
    mova     [r0+8], m0
    add        r0, r1
    dec        r2
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_weight_16x16_mmx2.nextrow)
%endif
%endmacro

INIT_MMX
WEIGHT_FUNC_DBL_MM 16
WEIGHT_FUNC_DBL_MM  8

%macro WEIGHT_FUNC_MM 4
cglobal h264_weight_%1x%2_%4, 7, 7, %3
    WEIGHT_SETUP
    mov        r2, %2
%if %2 == 16
.nextrow
    WEIGHT_OP 0, mmsize/2
    mova     [r0], m0
    add        r0, r1
    dec        r2
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_weight_%1x16_%4.nextrow)
%endif
%endmacro

INIT_MMX
WEIGHT_FUNC_MM  8, 16,  0, mmx2
WEIGHT_FUNC_MM  8,  8,  0, mmx2
WEIGHT_FUNC_MM  8,  4,  0, mmx2
INIT_XMM
WEIGHT_FUNC_MM 16, 16,  8, sse2
WEIGHT_FUNC_MM 16,  8,  8, sse2

%macro WEIGHT_FUNC_HALF_MM 5
cglobal h264_weight_%1x%2_%5, 5, 5, %4
    WEIGHT_SETUP
    mov        r2, %2/2
    lea        r3, [r1*2]
%if %2 == mmsize
.nextrow
    WEIGHT_OP 0, r1
    movh     [r0], m0
%if mmsize == 16
    movhps   [r0+r1], m0
%else
    psrlq      m0, 32
    movh     [r0+r1], m0
%endif
    add        r0, r3
    dec        r2
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_weight_%1x%3_%5.nextrow)
%endif
%endmacro

INIT_MMX
WEIGHT_FUNC_HALF_MM 4,  8,  8, 0, mmx2
WEIGHT_FUNC_HALF_MM 4,  4,  8, 0, mmx2
WEIGHT_FUNC_HALF_MM 4,  2,  8, 0, mmx2
INIT_XMM
WEIGHT_FUNC_HALF_MM 8, 16, 16, 8, sse2
WEIGHT_FUNC_HALF_MM 8,  8, 16, 8, sse2
WEIGHT_FUNC_HALF_MM 8,  4, 16, 8, sse2

%macro BIWEIGHT_SETUP 0
    add        r6, 1
    or         r6, 1
    add        r3, 1
    movd       m3, r4d
    movd       m4, r5d
    movd       m5, r6d
    movd       m6, r3d
    pslld      m5, m6
    psrld      m5, 1
%if mmsize == 16
    pshuflw    m3, m3, 0
    pshuflw    m4, m4, 0
    pshuflw    m5, m5, 0
    punpcklqdq m3, m3
    punpcklqdq m4, m4
    punpcklqdq m5, m5
%else
    pshufw     m3, m3, 0
    pshufw     m4, m4, 0
    pshufw     m5, m5, 0
%endif
    pxor       m7, m7
%endmacro

%macro BIWEIGHT_STEPA 3
    movh       m%1, [r0+%3]
    movh       m%2, [r1+%3]
    punpcklbw  m%1, m7
    punpcklbw  m%2, m7
    pmullw     m%1, m3
    pmullw     m%2, m4
    paddsw     m%1, m%2
%endmacro

%macro BIWEIGHT_STEPB 0
    paddsw     m0, m5
    paddsw     m1, m5
    psraw      m0, m6
    psraw      m1, m6
    packuswb   m0, m1
%endmacro

%macro BIWEIGHT_FUNC_DBL_MM 1
cglobal h264_biweight_16x%1_mmx2, 7, 7, 0
    BIWEIGHT_SETUP
    mov        r3, %1
%if %1 == 16
.nextrow
    BIWEIGHT_STEPA 0, 1, 0
    BIWEIGHT_STEPA 1, 2, 4
    BIWEIGHT_STEPB
    mova       [r0], m0
    BIWEIGHT_STEPA 0, 1, 8
    BIWEIGHT_STEPA 1, 2, 12
    BIWEIGHT_STEPB
    mova     [r0+8], m0
    add        r0, r2
    add        r1, r2
    dec        r3
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_biweight_16x16_mmx2.nextrow)
%endif
%endmacro

INIT_MMX
BIWEIGHT_FUNC_DBL_MM 16
BIWEIGHT_FUNC_DBL_MM  8

%macro BIWEIGHT_FUNC_MM 4
cglobal h264_biweight_%1x%2_%4, 7, 7, %3
    BIWEIGHT_SETUP
    mov        r3, %2
%if %2 == 16
.nextrow
    BIWEIGHT_STEPA 0, 1, 0
    BIWEIGHT_STEPA 1, 2, mmsize/2
    BIWEIGHT_STEPB
    mova       [r0], m0
    add        r0, r2
    add        r1, r2
    dec        r3
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_biweight_%1x16_%4.nextrow)
%endif
%endmacro

INIT_MMX
BIWEIGHT_FUNC_MM  8, 16,  0, mmx2
BIWEIGHT_FUNC_MM  8,  8,  0, mmx2
BIWEIGHT_FUNC_MM  8,  4,  0, mmx2
INIT_XMM
BIWEIGHT_FUNC_MM 16, 16,  8, sse2
BIWEIGHT_FUNC_MM 16,  8,  8, sse2

%macro BIWEIGHT_FUNC_HALF_MM 5
cglobal h264_biweight_%1x%2_%5, 7, 7, %4
    BIWEIGHT_SETUP
    mov        r3, %2/2
    lea        r4, [r2*2]
%if %2 == mmsize
.nextrow
    BIWEIGHT_STEPA 0, 1, 0
    BIWEIGHT_STEPA 1, 2, r2
    BIWEIGHT_STEPB
    movh       [r0], m0
%if mmsize == 16
    movhps     [r0+r2], m0
%else
    psrlq      m0, 32
    movh       [r0+r2], m0
%endif
    add        r0, r4
    add        r1, r4
    dec        r3
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_biweight_%1x%3_%5.nextrow)
%endif
%endmacro

INIT_MMX
BIWEIGHT_FUNC_HALF_MM 4,  8,  8, 0, mmx2
BIWEIGHT_FUNC_HALF_MM 4,  4,  8, 0, mmx2
BIWEIGHT_FUNC_HALF_MM 4,  2,  8, 0, mmx2
INIT_XMM
BIWEIGHT_FUNC_HALF_MM 8, 16, 16, 8, sse2
BIWEIGHT_FUNC_HALF_MM 8,  8, 16, 8, sse2
BIWEIGHT_FUNC_HALF_MM 8,  4, 16, 8, sse2

%macro BIWEIGHT_SSSE3_SETUP 0
    add        r6, 1
    or         r6, 1
    add        r3, 1
    movd       m4, r4d
    movd       m0, r5d
    movd       m5, r6d
    movd       m6, r3d
    pslld      m5, m6
    psrld      m5, 1
    punpcklbw  m4, m0
    pshuflw    m4, m4, 0
    pshuflw    m5, m5, 0
    punpcklqdq m4, m4
    punpcklqdq m5, m5
%endmacro

%macro BIWEIGHT_SSSE3_OP 0
    pmaddubsw  m0, m4
    pmaddubsw  m2, m4
    paddsw     m0, m5
    paddsw     m2, m5
    psraw      m0, m6
    psraw      m2, m6
    packuswb   m0, m2
%endmacro

%macro BIWEIGHT_SSSE3_16 1
cglobal h264_biweight_16x%1_ssse3, 7, 7, 8
    BIWEIGHT_SSSE3_SETUP
    mov        r3, %1

%if %1 == 16
.nextrow
    movh       m0, [r0]
    movh       m2, [r0+8]
    movh       m3, [r1+8]
    punpcklbw  m0, [r1]
    punpcklbw  m2, m3
    BIWEIGHT_SSSE3_OP
    mova       [r0], m0
    add        r0, r2
    add        r1, r2
    dec        r3
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_biweight_16x16_ssse3.nextrow)
%endif
%endmacro

INIT_XMM
BIWEIGHT_SSSE3_16 16
BIWEIGHT_SSSE3_16  8

%macro BIWEIGHT_SSSE3_8 1
cglobal h264_biweight_8x%1_ssse3, 7, 7, 8
    BIWEIGHT_SSSE3_SETUP
    mov        r3, %1/2
    lea        r4, [r2*2]

%if %1 == 16
.nextrow
    movh       m0, [r0]
    movh       m1, [r1]
    movh       m2, [r0+r2]
    movh       m3, [r1+r2]
    punpcklbw  m0, m1
    punpcklbw  m2, m3
    BIWEIGHT_SSSE3_OP
    movh       [r0], m0
    movhps     [r0+r2], m0
    add        r0, r4
    add        r1, r4
    dec        r3
    jnz .nextrow
    REP_RET
%else
    jmp mangle(ff_h264_biweight_8x16_ssse3.nextrow)
%endif
%endmacro

INIT_XMM
BIWEIGHT_SSSE3_8 16
BIWEIGHT_SSSE3_8  8
BIWEIGHT_SSSE3_8  4
