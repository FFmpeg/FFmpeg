;*****************************************************************************
;* x86-optimized functions for anlmdn filter
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
; float ff_compute_distance_ssd(float *f1, const float *f2, ptrdiff_t len)
;------------------------------------------------------------------------------

INIT_XMM sse
cglobal compute_distance_ssd, 3,5,3, f1, f2, len, r, x
    mov       xq, lenq
    shl       xq, 2
    neg       xq
    add       f1q, xq
    add       f2q, xq
    xor       xq, xq
    shl       lenq, 1
    add       lenq, 1
    shl       lenq, 2
    mov       rq, lenq
    and       rq, mmsize - 1
    xorps     m0, m0
    cmp       lenq, mmsize
    jl .loop1
    sub       lenq, rq
ALIGN 16
    .loop0:
        movups    m1, [f1q + xq]
        movups    m2, [f2q + xq]
        subps     m1, m2
        mulps     m1, m1
        addps     m0, m1
        add       xq, mmsize
        cmp       xq, lenq
        jl .loop0

    movhlps   xmm1, xmm0
    addps     xmm0, xmm1
    movss     xmm1, xmm0
    shufps    xmm0, xmm0, 1
    addss     xmm0, xmm1

    cmp       rq, 0
    je .end
    add       lenq, rq
    .loop1:
        movss    xm1, [f1q + xq]
        subss    xm1, [f2q + xq]
        mulss    xm1, xm1
        addss    xm0, xm1
        add       xq, 4
        cmp       xq, lenq
        jl .loop1
    .end:
%if ARCH_X86_64 == 0
    movss     r0m, xm0
    fld dword r0m
%endif
    RET
