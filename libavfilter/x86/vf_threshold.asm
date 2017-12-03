;*****************************************************************************
;* x86-optimized functions for threshold filter
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

%if ARCH_X86_64

SECTION_RODATA

pb_128: times 16 db 128

SECTION .text

%macro THRESHOLD_8 0
cglobal threshold8, 10, 13, 5, in, threshold, min, max, out, ilinesize, tlinesize, flinesize, slinesize, olinesize, w, h, x
    mov             wd, dword wm
    mov             hd, dword hm
    VBROADCASTI128  m4, [pb_128]
    add            inq, wq
    add     thresholdq, wq
    add           minq, wq
    add           maxq, wq
    add           outq, wq
    neg             wq
.nextrow:
    mov         xq, wq

    .loop:
        movu            m1, [inq + xq]
        movu            m0, [thresholdq + xq]
        movu            m2, [minq + xq]
        movu            m3, [maxq + xq]
        pxor            m0, m4
        pxor            m1, m4
        pcmpgtb         m0, m1
        pblendvb        m3, m2, m0
        movu   [outq + xq], m3
        add             xq, mmsize
    jl .loop

    add          inq, ilinesizeq
    add   thresholdq, tlinesizeq
    add         minq, flinesizeq
    add         maxq, slinesizeq
    add         outq, olinesizeq
    sub         hd, 1
    jg .nextrow
RET
%endmacro

INIT_XMM sse4
THRESHOLD_8

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
THRESHOLD_8
%endif

%endif
