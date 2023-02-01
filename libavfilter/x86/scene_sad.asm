;*****************************************************************************
;* x86-optimized functions for scene SAD
;*
;* Copyright (C) 2018 Marton Balint
;*
;* Based on vf_blend.asm, Copyright (C) 2015 Paul B Mahol
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


%macro SAD_INIT 0
cglobal scene_sad, 6, 7, 2, src1, stride1, src2, stride2, width, end, x
    add     src1q, widthq
    add     src2q, widthq
    neg    widthq
    pxor       m1, m1
%endmacro


%macro SAD_LOOP 0
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [src1q + xq]
        psadbw          m0, [src2q + xq]
        paddq           m1, m0
        add             xq, mmsize
    jl .loop
    add     src1q, stride1q
    add     src2q, stride2q
    sub      endd, 1
    jg .nextrow

    mov         r0q, r6mp
    movu      [r0q], m1      ; sum
RET
%endmacro


%macro SAD_FRAMES 0
    SAD_INIT
    SAD_LOOP
%endmacro


INIT_XMM sse2
SAD_FRAMES

%if HAVE_AVX2_EXTERNAL

INIT_YMM avx2
SAD_FRAMES

%endif
