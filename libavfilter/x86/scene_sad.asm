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
pb_bswap16: times 4 db 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14

SECTION .text


%macro SAD_INIT 1-2 0 ; depth, bswap
%if %2
cglobal scene_sad%1_bswap, 6, 7, 6, src1, stride1, src2, stride2, width, end, x
    movu       m3, [pb_bswap16]
    pxor       m5, m5
%else
cglobal scene_sad%1, 6, 7, 3, src1, stride1, src2, stride2, width, end, x
%endif
    add     src1q, widthq
    add     src2q, widthq
    neg    widthq
    pxor       m1, m1
%endmacro

%macro PSADQ 4-5 0 ; depth, dst, [src2], tmp, bswap
%if %1 == 8
        ; PSADBW requires an aligned memory operand.
        movu   %4, %3
        psadbw %2, %4
%else
    %if %5
        ; byte-swapped samples, safe for the full 16-bit range
        movu      %4, %3
        pshufb    %2, m3
        pshufb    %4, m3
        pminuw    m4, %2, %4
        pmaxuw    %2, %4
        psubw     %2, m4
        punpckhwd %4, %2, m5
        punpcklwd %2, m5
        paddd     %2, %4
    %else
        psubw     %2, %3
        pabsw     %2, %2
        pmaddwd   %2, [pw_1]
    %endif
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

%macro SAD_LOOP 1-2 0 ; depth, bswap
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [src1q + xq]
        PSADQ %1,       m0, [src2q + xq], m2, %2
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


%macro SAD_FRAMES 1-2 0 ; depth, bswap
    SAD_INIT %1, %2
    SAD_LOOP %1, %2
%endmacro


INIT_XMM sse2
SAD_FRAMES 8

%if HAVE_AVX2_EXTERNAL

INIT_YMM avx2
SAD_FRAMES 8
SAD_FRAMES 16
SAD_FRAMES 16, 1

%endif

%if HAVE_AVX512_EXTERNAL

INIT_ZMM avx512
SAD_FRAMES 8
SAD_FRAMES 16
SAD_FRAMES 16, 1

%endif
