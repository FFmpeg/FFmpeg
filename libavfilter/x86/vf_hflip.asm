;*****************************************************************************
;* x86-optimized functions for hflip filter
;*
;* Copyright (C) 2017 Paul B Mahol
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
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pb_flip_byte:  db 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
pb_flip_short: db 14,15,12,13,10,11,8,9,6,7,4,5,2,3,0,1

SECTION .text

;%1 byte or short, %2 b or w, %3 size in byte (1 for byte, 2 for short)
%macro HFLIP 3
cglobal hflip_%1, 3, 5, 3, src, dst, w, r, x
    VBROADCASTI128    m0, [pb_flip_%1]
    xor               xq, xq
%if %3 == 1
    movsxdifnidn wq, wd
%else ; short
    add     wd, wd
%endif
    mov     rq, wq
    and     rq, 2 * mmsize - 1
    cmp     wq, 2 * mmsize
    jl .loop1
    sub     wq, rq

    .loop0:
        neg     xq
%if mmsize == 32
        vpermq  m1, [srcq + xq -     mmsize + %3], 0x4e; flip each lane at load
        vpermq  m2, [srcq + xq - 2 * mmsize + %3], 0x4e; flip each lane at load
%else
        movu    m1, [srcq + xq -     mmsize + %3]
        movu    m2, [srcq + xq - 2 * mmsize + %3]
%endif
        pshufb  m1, m0
        pshufb  m2, m0
        neg     xq
        movu    [dstq + xq         ], m1
        movu    [dstq + xq + mmsize], m2
        add     xq, mmsize * 2
        cmp     xq, wq
        jl .loop0

    cmp    rq, 0
    je .end
    add    wq, rq

    .loop1:
        neg    xq
        mov    r%2, [srcq + xq]
        neg    xq
        mov    [dstq + xq], r%2
        add    xq, %3
        cmp    xq, wq
        jl .loop1
    .end:
        RET
%endmacro

INIT_XMM ssse3
HFLIP byte, b, 1
HFLIP short, w, 2

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
HFLIP byte, b, 1
HFLIP short, w, 2
%endif
