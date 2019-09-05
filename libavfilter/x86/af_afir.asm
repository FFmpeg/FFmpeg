;*****************************************************************************
;* x86-optimized functions for afir filter
;* Copyright (c) 2017 Paul B Mahol
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

;------------------------------------------------------------------------------
; void ff_fcmul_add(float *sum, const float *t, const float *c, int len)
;------------------------------------------------------------------------------

%macro FCMUL_ADD 0
cglobal fcmul_add, 4,4,6, sum, t, c, len
    shl       lend, 3
    add         tq, lenq
    add         cq, lenq
    add       sumq, lenq
    neg       lenq
ALIGN 16
.loop:
    movsldup  m0, [tq + lenq]
    movsldup  m3, [tq + lenq+mmsize]
    movaps    m1, [cq + lenq]
    movaps    m4, [cq + lenq+mmsize]
    mulps     m0, m0, m1
    mulps     m3, m3, m4
    shufps    m1, m1, m1, 0xb1
    shufps    m4, m4, m4, 0xb1
    movshdup  m2, [tq + lenq]
    movshdup  m5, [tq + lenq+mmsize]
    mulps     m2, m2, m1
    mulps     m5, m5, m4
    addsubps  m0, m0, m2
    addsubps  m3, m3, m5
    addps     m0, m0, [sumq + lenq]
    addps     m3, m3, [sumq + lenq+mmsize]
    movaps    [sumq + lenq], m0
    movaps    [sumq + lenq+mmsize], m3
    add       lenq, mmsize*2
    jl .loop
    movss xm0, [tq + lenq]
    mulss xm0, [cq + lenq]
    addss xm0, [sumq + lenq]
    movss [sumq + lenq], xm0
    RET
%endmacro

INIT_XMM sse3
FCMUL_ADD
INIT_YMM avx
FCMUL_ADD
