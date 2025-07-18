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

%macro count_pixels_fn 1 ; depth
cglobal blackdetect_%1, 5, 7, 2, src, stride, width, height, threshold
        movd xm1, thresholdd
    %if %1 == 8
        vpbroadcastb m1, xm1
    %else
        vpbroadcastw m1, xm1
        shl widthq, 1
    %endif
        add srcq, widthq
        neg widthq
        xor r4d, r4d
        mov r5, widthq
        jmp .start
.loop:
        popcnt r6d, r6d
        add r4d, r6d
.start:
        movu m0, [srcq + r5]
    %if %1 == 8
        pmaxub m0, m1
        pcmpeqb m0, m1
    %else
        pmaxuw m0, m1
        pcmpeqw m0, m1
    %endif
        pmovmskb r6d, m0
        add r5, mmsize
        jl .loop
        ; handle tail by shifting away unused high elements
        shlx r6d, r6d, r5d
        popcnt r6d, r6d
        add r4d, r6d
        add srcq, strideq
        mov r5, widthq
        dec heightq
        jg .start
    %if %1 > 8
        shr r4d, 1
    %endif
        mov eax, r4d
        RET
%endmacro

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
count_pixels_fn 8
count_pixels_fn 16
%endif
