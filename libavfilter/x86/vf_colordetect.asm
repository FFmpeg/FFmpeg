;*****************************************************************************
;* x86-optimized functions for blackdetect filter
;*
;* Copyright (C) 2025 Niklas Haas
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

SECTION .text

%macro detect_range_fn 1 ; suffix
cglobal detect_range%1, 4, 7, 5, data, stride, width, height, mpeg_min, mpeg_max, x
%if UNIX64 && notcpuflag(avx512)
    movd xm0, mpeg_mind
    movd xm1, mpeg_maxd
    vpbroadcast%1 m0, xm0
    vpbroadcast%1 m1, xm1
%else
    vpbroadcast%1 m0, mpeg_minm
    vpbroadcast%1 m1, mpeg_maxm
%endif
    add dataq, widthq
    neg widthq
.lineloop:
    mova m2, m0
    mova m3, m1
    mov xq, widthq
    .loop:
        movu m4, [dataq + xq]
        pminu%1 m2, m4
        pmaxu%1 m3, m4
        add xq, mmsize
        jl .loop

    ; test if the data is out of range
    pxor m2, m0
%if cpuflag(avx512)
    vpternlogq m2, m3, m1, 0xF6 ; m2 |= m3 ^ m1
    vptestmq k1, m2, m2
    kortestb k1, k1
%else
    pxor m3, m1
    por m2, m3
    ptest m2, m2
%endif
    jnz .end
    add dataq, strideq
    dec heightq
    jg .lineloop
.end:
    setnz al
    movzx eax, al
    RET
%endmacro

%macro detect_alpha_fn 3 ; suffix, hsuffix, range
cglobal detect_alpha%1_%3, 6, 7, 6, color, color_stride, alpha, alpha_stride, width, height, x
    pxor m0, m0
    add colorq, widthq
    add alphaq, widthq
    neg widthq
%ifidn %3, limited
    vpbroadcast%2 m3, r6m ; p
    vpbroadcast%2 m4, r7m ; q
    vpbroadcast%2 m5, r8m ; k
%endif
.lineloop:
    mov xq, widthq
    .loop:
    %ifidn %3, full
        movu m1, [colorq + xq]
        movu m2, [alphaq + xq]
        pmaxu%1 m1, m2
    %else
        pmovzx%1%2 m1, [colorq + xq]
        pmovzx%1%2 m2, [alphaq + xq]
        pmull%2 m1, m3
        pmull%2 m2, m4
    %ifidn %1, b
        psubusw m1, m5
    %else
        pmaxud m1, m5
        psubd m1, m5
    %endif
        pmaxu%2 m1, m2
    %endif
    %if cpuflag(avx512)
        vpternlogq m0, m1, m2, 0xF6 ; m0 |= m1 ^ m2
    %else
        pxor m1, m2
        por m0, m1
    %endif
    %ifidn %3, full
        add xq, mmsize
    %else
        add xq, mmsize >> 1
    %endif
        jl .loop

%if cpuflag(avx512)
    vptestmq k1, m0, m0
    kortestb k1, k1
%else
    ptest m0, m0
%endif
    jnz .found

    add colorq, color_strideq
    add alphaq, alpha_strideq
    dec heightq
    jg .lineloop
    xor eax, eax
    RET

.found:
    mov eax, 1
    RET
%endmacro

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
detect_range_fn b
detect_range_fn w
detect_alpha_fn b, w, full
detect_alpha_fn w, d, full
detect_alpha_fn b, w, limited
detect_alpha_fn w, d, limited
%endif

%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
detect_range_fn b
detect_range_fn w
detect_alpha_fn b, w, full
detect_alpha_fn w, d, full
detect_alpha_fn b, w, limited
detect_alpha_fn w, d, limited
%endif
