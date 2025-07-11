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

SECTION_RODATA

pw_1: times 32 dw 1

SECTION .text


%macro SAD_INIT 1 ; depth
cglobal scene_sad%1, 6, 7, 3, src1, stride1, src2, stride2, width, end, x
    add     src1q, widthq
    add     src2q, widthq
    neg    widthq
    pxor       m1, m1
%endmacro

%macro PSADQ 4 ; depth, dst, [src2], tmp
%if %1 == 8
        psadbw %2, %3
%else
        psubw     %2, %3
        pabsw     %2, %2
        pmaddwd   %2, [pw_1]
    %if mmsize == 64
        vextracti32x8 ymm%4, %2, 1
        paddd     ymm%2, ymm%4
        pmovzxdq  %2, ymm%2
    %else
        vextracti128 xmm%4, %2, 1
        paddd     xmm%2, xmm%4
        pmovzxdq  %2, xmm%2
    %endif
%endif
%endmacro

%macro SAD_LOOP 1 ; depth
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [src1q + xq]
        PSADQ %1,       m0, [src2q + xq], m2
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


%macro SAD_FRAMES 1 ; depth
    SAD_INIT %1
    SAD_LOOP %1
%endmacro


INIT_XMM sse2
SAD_FRAMES 8

%if HAVE_AVX2_EXTERNAL

INIT_YMM avx2
SAD_FRAMES 8
SAD_FRAMES 16

%endif

%if HAVE_AVX512_EXTERNAL

INIT_ZMM avx512
SAD_FRAMES 8
SAD_FRAMES 16

%endif
