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
INIT_XMM

;-----------------------------------------------------------------------------
; biweight pred:
;
; void h264_biweight_16x16_sse2(uint8_t *dst, uint8_t *src, int stride,
;                               int log2_denom, int weightd, int weights,
;                               int offset);
;-----------------------------------------------------------------------------

%macro BIWEIGHT_SSE2_SETUP 0
    add        r6, 1
    or         r6, 1
    add        r3, 1
    movd       m3, r4
    movd       m4, r5
    movd       m5, r6
    movd       m6, r3
    pslld      m5, m6
    psrld      m5, 1
    pshuflw    m3, m3, 0
    pshuflw    m4, m4, 0
    pshuflw    m5, m5, 0
    punpcklqdq m3, m3
    punpcklqdq m4, m4
    punpcklqdq m5, m5
    pxor       m7, m7
%endmacro

%macro BIWEIGHT_SSE2_STEPA 3
    movh       m%1, [r0+%3]
    movh       m%2, [r1+%3]
    punpcklbw  m%1, m7
    punpcklbw  m%2, m7
    pmullw     m%1, m3
    pmullw     m%2, m4
    paddsw     m%1, m%2
%endmacro

%macro BIWEIGHT_SSE2_STEPB 0
    paddsw     m0, m5
    paddsw     m1, m5
    psraw      m0, m6
    psraw      m1, m6
    packuswb   m0, m1
%endmacro

cglobal h264_biweight_16x16_sse2, 7, 7, 8
    BIWEIGHT_SSE2_SETUP
    mov        r3, 16

.nextrow
    BIWEIGHT_SSE2_STEPA 0, 1, 0
    BIWEIGHT_SSE2_STEPA 1, 2, 8
    BIWEIGHT_SSE2_STEPB
    mova       [r0], m0
    add        r0, r2
    add        r1, r2
    dec        r3
    jnz .nextrow
    REP_RET

cglobal h264_biweight_8x8_sse2, 7, 7, 8
    BIWEIGHT_SSE2_SETUP
    mov        r3, 4
    lea        r4, [r2*2]

.nextrow
    BIWEIGHT_SSE2_STEPA 0, 1, 0
    BIWEIGHT_SSE2_STEPA 1, 2, r2
    BIWEIGHT_SSE2_STEPB
    movh       [r0], m0
    movhps     [r0+r2], m0
    add        r0, r4
    add        r1, r4
    dec        r3
    jnz .nextrow
    REP_RET

%macro BIWEIGHT_SSSE3_SETUP 0
    add        r6, 1
    or         r6, 1
    add        r3, 1
    movd       m4, r4
    movd       m0, r5
    movd       m5, r6
    movd       m6, r3
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

cglobal h264_biweight_16x16_ssse3, 7, 7, 8
    BIWEIGHT_SSSE3_SETUP
    mov        r3, 16

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

cglobal h264_biweight_8x8_ssse3, 7, 7, 8
    BIWEIGHT_SSSE3_SETUP
    mov        r3, 4
    lea        r4, [r2*2]

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
