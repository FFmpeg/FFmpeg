;*****************************************************************************
;* x86-optimized functions for removegrain filter
;*
;* Copyright (C) 2015 James Darnley
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License along
;* with FFmpeg; if not, write to the Free Software Foundation, Inc.,
;* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;*****************************************************************************

; column: -1  0 +1
; row -1: a1 a2 a3
; row  0: a4  c a5
; row +1: a6 a7 a8

%include "libavutil/x86/x86util.asm"

SECTION_RODATA 32

pw_4:    times 16 dw 4
pw_8:    times 16 dw 8
pw_div9: times 16 dw ((1<<16)+4)/9

SECTION .text

;*** Preprocessor helpers

%define a1 srcq+stride_n-1
%define a2 srcq+stride_n
%define a3 srcq+stride_n+1
%define a4 srcq-1
%define c  srcq
%define a5 srcq+1
%define a6 srcq+stride_p-1
%define a7 srcq+stride_p
%define a8 srcq+stride_p+1

; %1 dest simd register
; %2 source memory location
; %3 zero location (simd register/memory)
%macro LOAD 3
    movh %1, %2
    punpcklbw %1, %3
%endmacro

%macro LOAD_SQUARE 0
    movu m1, [a1]
    movu m2, [a2]
    movu m3, [a3]
    movu m4, [a4]
    movu m0, [c]
    movu m5, [a5]
    movu m6, [a6]
    movu m7, [a7]
    movu m8, [a8]
%endmacro

; %1 zero location (simd register/memory)
%macro LOAD_SQUARE_16 1
    LOAD m1, [a1], %1
    LOAD m2, [a2], %1
    LOAD m3, [a3], %1
    LOAD m4, [a4], %1
    LOAD m0, [c], %1
    LOAD m5, [a5], %1
    LOAD m6, [a6], %1
    LOAD m7, [a7], %1
    LOAD m8, [a8], %1
%endmacro

; %1 data type
; %2 simd register to hold maximums
; %3 simd register to hold minimums
; %4 temp location (simd register/memory)
%macro SORT_PAIR 4
    mova   %4, %2
    pmin%1 %2, %3
    pmax%1 %3, %4
%endmacro

%macro SORT_AXIS 0
    SORT_PAIR ub, m1, m8, m9
    SORT_PAIR ub, m2, m7, m10
    SORT_PAIR ub, m3, m6, m11
    SORT_PAIR ub, m4, m5, m12
%endmacro


%macro SORT_AXIS_16 0
    SORT_PAIR sw, m1, m8, m9
    SORT_PAIR sw, m2, m7, m10
    SORT_PAIR sw, m3, m6, m11
    SORT_PAIR sw, m4, m5, m12
%endmacro

; The loop doesn't need to do all the iterations.  It could stop when the right
; pixels are in the right registers.
%macro SORT_SQUARE 0
    %assign k 7
    %rep 7
        %assign i 1
        %assign j 2
        %rep k
            SORT_PAIR ub, m %+ i , m %+ j , m9
            %assign i i+1
            %assign j j+1
        %endrep
        %assign k k-1
    %endrep
%endmacro

; %1 dest simd register
; %2 source (simd register/memory)
; %3 temp simd register
%macro ABS_DIFF 3
    mova %3, %2
    psubusb %3, %1
    psubusb %1, %2
    por %1, %3
%endmacro

; %1 dest simd register
; %2 source (simd register/memory)
; %3 temp simd register
%macro ABS_DIFF_W 3
    mova %3, %2
    psubusw %3, %1
    psubusw %1, %2
    por %1, %3
%endmacro

; %1 simd register that holds the "false" values and will hold the result
; %2 simd register that holds the "true" values
; %3 location (simd register/memory) that hold the mask
%macro BLEND 3
%if cpuflag(avx2)
    vpblendvb %1, %1, %2, %3
%else
    pand      %2, %3
    pandn     %3, %1
    por       %3, %2
    SWAP      %1, %3
%endif
%endmacro

; Functions

INIT_XMM sse2
cglobal rg_fl_mode_1, 4, 5, 3, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        movu m0, [a1]
        mova m1, m0

        movu m2, [a2]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [a3]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [a4]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [a5]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [a6]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [a7]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [a8]
        pmaxub m0, m2
        pminub m1, m2

        movu m2, [c]
        pminub m2, m0
        pmaxub m2, m1

        movu [dstq], m2
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

%if ARCH_X86_64
cglobal rg_fl_mode_2, 4, 5, 10, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE
        SORT_SQUARE

        CLIPUB m0, m2, m7

        movu [dstq], m0
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_3, 4, 5, 10, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE
        SORT_SQUARE

        CLIPUB m0, m3, m6

        movu [dstq], m0
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_4, 4, 5, 10, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE
        SORT_SQUARE

        CLIPUB m0, m4, m5

        movu [dstq], m0
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_5, 4, 5, 13, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE
        SORT_AXIS

        mova m9, m0
        mova m10, m0
        mova m11, m0
        mova m12, m0

        CLIPUB m9, m1, m8
        CLIPUB m10, m2, m7
        CLIPUB m11, m3, m6
        CLIPUB m12, m4, m5

        mova m8, m9  ; clip1
        mova m7, m10 ; clip2
        mova m6, m11 ; clip3
        mova m5, m12 ; clip4

        ABS_DIFF m9, m0, m1  ; c1
        ABS_DIFF m10, m0, m2 ; c2
        ABS_DIFF m11, m0, m3 ; c3
        ABS_DIFF m12, m0, m4 ; c4

        pminub m9, m10
        pminub m9, m11
        pminub m9, m12 ; mindiff

        pcmpeqb m10, m9
        pcmpeqb m11, m9
        pcmpeqb m12, m9

        ; Notice the order here: c1, c3, c2, c4
        BLEND m8, m6, m11
        BLEND m8, m7, m10
        BLEND m8, m5, m12

        movu [dstq], m8
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_6, 4, 5, 16, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    ; Some register saving suggestions: the zero can be somewhere other than a
    ; register, the center pixels could be on the stack.

    pxor m15, m15
    .loop:
        LOAD_SQUARE_16 m15
        SORT_AXIS_16

        mova m9, m0
        mova m10, m0
        mova m11, m0
        mova m12, m0
        CLIPW m9, m1, m8  ; clip1
        CLIPW m10, m2, m7 ; clip2
        CLIPW m11, m3, m6 ; clip3
        CLIPW m12, m4, m5 ; clip4

        psubw m8, m1 ; d1
        psubw m7, m2 ; d2
        psubw m6, m3 ; d3
        psubw m5, m4 ; d4

        mova m1, m9
        mova m2, m10
        mova m3, m11
        mova m4, m12
        ABS_DIFF_W m1, m0, m13
        ABS_DIFF_W m2, m0, m14
        ABS_DIFF_W m3, m0, m13
        ABS_DIFF_W m4, m0, m14
        psllw m1, 1
        psllw m2, 1
        psllw m3, 1
        psllw m4, 1
        paddw m1, m8 ; c1
        paddw m2, m7 ; c2
        paddw m3, m6 ; c3
        paddw m4, m5 ; c4
        ; As the differences (d1..d4) can only be positive, there is no need to
        ; clip to zero.  Also, the maximum positive value is less than 768.

        pminsw m1, m2
        pminsw m1, m3
        pminsw m1, m4

        pcmpeqw m2, m1
        pcmpeqw m3, m1
        pcmpeqw m4, m1

        BLEND m9, m11, m3
        BLEND m9, m10, m2
        BLEND m9, m12, m4
        packuswb m9, m9

        movh [dstq], m9
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

; This is just copy-pasted straight from mode 6 with the left shifts removed.
cglobal rg_fl_mode_7, 4, 5, 16, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    ; Can this be done without unpacking?

    pxor m15, m15
    .loop:
        LOAD_SQUARE_16 m15
        SORT_AXIS_16

        mova m9, m0
        mova m10, m0
        mova m11, m0
        mova m12, m0
        CLIPW m9, m1, m8  ; clip1
        CLIPW m10, m2, m7 ; clip2
        CLIPW m11, m3, m6 ; clip3
        CLIPW m12, m4, m5 ; clip4

        psubw m8, m1 ; d1
        psubw m7, m2 ; d2
        psubw m6, m3 ; d3
        psubw m5, m4 ; d4

        mova m1, m9
        mova m2, m10
        mova m3, m11
        mova m4, m12
        ABS_DIFF_W m1, m0, m13
        ABS_DIFF_W m2, m0, m14
        ABS_DIFF_W m3, m0, m13
        ABS_DIFF_W m4, m0, m14
        paddw m1, m8 ; c1
        paddw m2, m7 ; c2
        paddw m3, m6 ; c3
        paddw m4, m5 ; c4

        pminsw m1, m2
        pminsw m1, m3
        pminsw m1, m4

        pcmpeqw m2, m1
        pcmpeqw m3, m1
        pcmpeqw m4, m1

        BLEND m9, m11, m3
        BLEND m9, m10, m2
        BLEND m9, m12, m4
        packuswb m9, m9

        movh [dstq], m9
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

; This is just copy-pasted straight from mode 6 with a few changes.
cglobal rg_fl_mode_8, 4, 5, 16, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m15, m15
    .loop:
        LOAD_SQUARE_16 m15
        SORT_AXIS_16

        mova m9, m0
        mova m10, m0
        mova m11, m0
        mova m12, m0
        CLIPW m9, m1, m8  ; clip1
        CLIPW m10, m2, m7 ; clip2
        CLIPW m11, m3, m6 ; clip3
        CLIPW m12, m4, m5 ; clip4

        psubw m8, m1 ; d1
        psubw m7, m2 ; d2
        psubw m6, m3 ; d3
        psubw m5, m4 ; d4
        psllw m8, 1
        psllw m7, 1
        psllw m6, 1
        psllw m5, 1

        mova m1, m9
        mova m2, m10
        mova m3, m11
        mova m4, m12
        ABS_DIFF_W m1, m0, m13
        ABS_DIFF_W m2, m0, m14
        ABS_DIFF_W m3, m0, m13
        ABS_DIFF_W m4, m0, m14
        paddw m1, m8 ; c1
        paddw m2, m7 ; c1
        paddw m3, m6 ; c1
        paddw m4, m5 ; c1
        ; As the differences (d1..d4) can only be positive, there is no need to
        ; clip to zero.  Also, the maximum positive value is less than 768.

        pminsw m1, m2
        pminsw m1, m3
        pminsw m1, m4

        pcmpeqw m2, m1
        pcmpeqw m3, m1
        pcmpeqw m4, m1

        BLEND m9, m11, m3
        BLEND m9, m10, m2
        BLEND m9, m12, m4
        packuswb m9, m9

        movh [dstq], m9
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

cglobal rg_fl_mode_9, 4, 5, 13, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE
        SORT_AXIS

        mova m9, m0
        mova m10, m0
        mova m11, m0
        mova m12, m0
        CLIPUB m9, m1, m8  ; clip1
        CLIPUB m10, m2, m7 ; clip2
        CLIPUB m11, m3, m6 ; clip3
        CLIPUB m12, m4, m5 ; clip4

        psubb m8, m1 ; d1
        psubb m7, m2 ; d2
        psubb m6, m3 ; d3
        psubb m5, m4 ; d4

        pminub m8, m7
        pminub m8, m6
        pminub m8, m5

        pcmpeqb m7, m8
        pcmpeqb m6, m8
        pcmpeqb m5, m8

        BLEND m9, m11, m6
        BLEND m9, m10, m7
        BLEND m9, m12, m5

        movu [dstq], m9
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET
%endif

cglobal rg_fl_mode_10, 4, 5, 8, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        movu m0, [c]

        movu m1, [a4]
        mova m2, m1
        ABS_DIFF m1, m0, m7

        movu m3, [a5]       ; load pixel
        mova m4, m3
        ABS_DIFF m4, m0, m7 ; absolute difference from center
        pminub m1, m4       ; mindiff
        pcmpeqb m4, m1      ; if (difference == mindiff)
        BLEND m2, m3, m4    ;     return pixel

        movu m5, [a1]
        mova m6, m5
        ABS_DIFF m6, m0, m7
        pminub m1, m6
        pcmpeqb m6, m1
        BLEND m2, m5, m6

        movu m3, [a3]
        mova m4, m3
        ABS_DIFF m4, m0, m7
        pminub m1, m4
        pcmpeqb m4, m1
        BLEND m2, m3, m4

        movu m5, [a2]
        mova m6, m5
        ABS_DIFF m6, m0, m7
        pminub m1, m6
        pcmpeqb m6, m1
        BLEND m2, m5, m6

        movu m3, [a6]
        mova m4, m3
        ABS_DIFF m4, m0, m7
        pminub m1, m4
        pcmpeqb m4, m1
        BLEND m2, m3, m4

        movu m5, [a8]
        mova m6, m5
        ABS_DIFF m6, m0, m7
        pminub m1, m6
        pcmpeqb m6, m1
        BLEND m2, m5, m6

        movu m3, [a7]
        mova m4, m3
        ABS_DIFF m4, m0, m7
        pminub m1, m4
        pcmpeqb m4, m1
        BLEND m2, m3, m4

        movu [dstq], m2
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_11_12, 4, 5, 7, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m0, m0
    .loop:
        LOAD m1, [c], m0
        LOAD m2, [a2], m0
        LOAD m3, [a4], m0
        LOAD m4, [a5], m0
        LOAD m5, [a7], m0

        psllw m1, 2
        paddw m2, m3
        paddw m4, m5
        paddw m2, m4
        psllw m2, 1

        LOAD m3, [a1], m0
        LOAD m4, [a3], m0
        LOAD m5, [a6], m0
        LOAD m6, [a8], m0
        paddw m1, m2
        paddw m3, m4
        paddw m5, m6
        paddw m1, m3
        paddw m1, m5

        paddw m1, [pw_8]
        psraw m1, 4

        packuswb m1, m1

        movh [dstq], m1
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

cglobal rg_fl_mode_13_14, 4, 5, 8, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        movu m1, [a1]
        movu m2, [a8]
        mova m0, m1
        pavgb m1, m2
        ABS_DIFF m0, m2, m6

        movu m3, [a3]
        movu m4, [a6]
        mova m5, m3
        pavgb m3, m4
        ABS_DIFF m5, m4, m7
        pminub m0, m5
        pcmpeqb m5, m0
        BLEND m1, m3, m5

        movu m2, [a2]
        movu m3, [a7]
        mova m4, m2
        pavgb m2, m3
        ABS_DIFF m4, m3, m6
        pminub m0, m4
        pcmpeqb m4, m0
        BLEND m1, m2, m4

        movu [dstq], m1
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

%if ARCH_X86_64
cglobal rg_fl_mode_15_16, 4, 5, 16, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m15, m15
    .loop:
        LOAD_SQUARE_16 m15

        mova m9, m1
        mova m10, m2
        mova m11, m3
        ABS_DIFF_W m9, m8, m12
        ABS_DIFF_W m10, m7, m13
        ABS_DIFF_W m11, m6, m14
        pminsw m9, m10
        pminsw m9, m11
        pcmpeqw m10, m9
        pcmpeqw m11, m9

        mova m12, m2
        mova m13, m1
        mova m14, m6
        paddw m12, m7
        psllw m12, 1
        paddw m13, m3
        paddw m14, m8
        paddw m12, [pw_4]
        paddw m13, m14
        paddw m12, m13
        psrlw m12, 3

        SORT_PAIR ub, m1, m8, m0
        SORT_PAIR ub, m2, m7, m9
        SORT_PAIR ub, m3, m6, m14
        mova m4, m12
        mova m5, m12
        CLIPW m4, m1, m8
        CLIPW m5, m2, m7
        CLIPW m12, m3, m6

        BLEND m4, m12, m11
        BLEND m4,  m5, m10
        packuswb m4, m4

        movh [dstq], m4
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

cglobal rg_fl_mode_17, 4, 5, 9, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE
        SORT_AXIS

        pmaxub m1, m2
        pmaxub m3, m4

        pminub m8, m7
        pminub m5, m6

        pmaxub m1, m3
        pminub m8, m5

        mova m2, m1
        pminub m1, m8
        pmaxub m8, m2

        CLIPUB m0, m1, m8

        movu [dstq], m0
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_18, 4, 5, 16, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        LOAD_SQUARE

        mova m9, m1
        mova m10, m8
        ABS_DIFF m9, m0, m11
        ABS_DIFF m10, m0, m12
        pmaxub m9, m10 ; m9 = d1

        mova m10, m2
        mova m11, m7
        ABS_DIFF m10, m0, m12
        ABS_DIFF m11, m0, m13
        pmaxub m10, m11 ; m10 = d2

        mova m11, m3
        mova m12, m6
        ABS_DIFF m11, m0, m13
        ABS_DIFF m12, m0, m14
        pmaxub m11, m12 ; m11 = d3

        mova m12, m4
        mova m13, m5
        ABS_DIFF m12, m0, m14
        ABS_DIFF m13, m0, m15
        pmaxub m12, m13 ; m12 = d4

        mova m13, m9
        pminub m13, m10
        pminub m13, m11
        pminub m13, m12 ; m13 = mindiff

        pcmpeqb m10, m13
        pcmpeqb m11, m13
        pcmpeqb m12, m13

        mova m14, m1
        pminub m1, m8
        pmaxub m8, m14

        mova m13, m0
        mova m14, m1
        pminub m1, m8
        pmaxub m8, m14
        CLIPUB m13, m1, m8 ; m13 = ret...d1

        mova m14, m0
        mova m15, m3
        pminub m3, m6
        pmaxub m6, m15
        CLIPUB m14, m3, m6
        pand m14, m11
        pandn m11, m13
        por m14, m11 ; m14 = ret...d3

        mova m15, m0
        mova m1, m2
        pminub m2, m7
        pmaxub m7, m1
        CLIPUB m15, m2, m7
        pand m15, m10
        pandn m10, m14
        por m15, m10 ; m15 = ret...d2

        mova m1, m0
        mova m2, m4
        pminub m4, m5
        pmaxub m5, m2
        CLIPUB m1, m4, m5
        pand m1, m12
        pandn m12, m15
        por m1, m12 ; m15 = ret...d4

        movu [dstq], m1
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET
%endif

cglobal rg_fl_mode_19, 4, 5, 7, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m0, m0
    .loop:
        LOAD m1, [a1], m0
        LOAD m2, [a2], m0
        paddw m1, m2

        LOAD m3, [a3], m0
        LOAD m4, [a4], m0
        paddw m3, m4

        LOAD m5, [a5], m0
        LOAD m6, [a6], m0
        paddw m5, m6

        LOAD m2, [a7], m0
        LOAD m4, [a8], m0
        paddw m2, m4

        paddw m1, m3
        paddw m2, m5
        paddw m1, m2

        paddw m1, [pw_4]
        psraw m1, 3

        packuswb m1, m1

        movh [dstq], m1
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

cglobal rg_fl_mode_20, 4, 5, 7, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m0, m0
    .loop:
        LOAD m1, [a1], m0
        LOAD m2, [a2], m0
        paddw m1, m2

        LOAD m3, [a3], m0
        LOAD m4, [a4], m0
        paddw m3, m4

        LOAD m5, [a5], m0
        LOAD m6, [a6], m0
        paddw m5, m6

        LOAD m2, [a7], m0
        LOAD m4, [a8], m0
        paddw m2, m4

        LOAD m6, [c], m0
        paddw m1, m3
        paddw m2, m5
        paddw m6, [pw_4]

        paddw m1, m2
        paddw m1, m6

        pmulhuw m1, [pw_div9]

        packuswb m1, m1

        movh [dstq], m1
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

cglobal rg_fl_mode_21, 4, 5, 8, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m0, m0
    .loop:
        movu m1, [a1]
        movu m2, [a8]
        pavgb m7, m1, m2
        punpckhbw m3, m1, m0
        punpcklbw m1, m0
        punpckhbw m4, m2, m0
        punpcklbw m2, m0
        paddw m3, m4
        paddw m1, m2
        psrlw m3, 1
        psrlw m1, 1
        packuswb m1, m3

        movu m2, [a2]
        movu m3, [a7]
        pavgb m6, m2, m3
        punpckhbw m4, m2, m0
        punpcklbw m2, m0
        punpckhbw m5, m3, m0
        punpcklbw m3, m0
        paddw m4, m5
        paddw m2, m3
        psrlw m4, 1
        psrlw m2, 1
        packuswb m2, m4

        pminub m1, m2
        pmaxub m7, m6

        movu m2, [a3]
        movu m3, [a6]
        pavgb m6, m2, m3
        punpckhbw m4, m2, m0
        punpcklbw m2, m0
        punpckhbw m5, m3, m0
        punpcklbw m3, m0
        paddw m4, m5
        paddw m2, m3
        psrlw m4, 1
        psrlw m2, 1
        packuswb m2, m4

        pminub m1, m2
        pmaxub m7, m6

        movu m2, [a4]
        movu m3, [a5]
        pavgb m6, m2, m3
        punpckhbw m4, m2, m0
        punpcklbw m2, m0
        punpckhbw m5, m3, m0
        punpcklbw m3, m0
        paddw m4, m5
        paddw m2, m3
        psrlw m4, 1
        psrlw m2, 1
        packuswb m2, m4

        pminub m1, m2
        pmaxub m7, m6

        movu m3, [c]
        CLIPUB m3, m1, m7

        movu [dstq], m3
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

cglobal rg_fl_mode_22, 4, 5, 8, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    .loop:
        movu m0, [a1]
        movu m1, [a8]
        pavgb m0, m1
        movu m2, [a2]
        movu m3, [a7]
        pavgb m2, m3
        movu m4, [a3]
        movu m5, [a6]
        pavgb m4, m5
        movu m6, [a4]
        movu m7, [a5]
        pavgb m6, m7

        mova m1, m0
        mova m3, m2
        mova m5, m4
        mova m7, m6
        pminub m0, m2
        pminub m4, m6
        pmaxub m1, m3
        pmaxub m5, m7
        pminub m0, m4
        pmaxub m1, m5

        movu m2, [c]
        CLIPUB m2, m0, m1

        movu [dstq], m2
        add srcq, mmsize
        add dstq, mmsize
        sub pixelsd, mmsize
    jg .loop
RET

%if ARCH_X86_64
cglobal rg_fl_mode_23, 4, 5, 16, 0, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m15, m15
    .loop:
        LOAD_SQUARE_16 m15
        SORT_AXIS_16

        mova m9, m8
        mova m10, m7
        mova m11, m6
        mova m12, m5
        psubw m9, m1  ; linediff1
        psubw m10, m2 ; linediff2
        psubw m11, m3 ; linediff3
        psubw m12, m4 ; linediff4

        psubw m1, m0
        psubw m2, m0
        psubw m3, m0
        psubw m4, m0
        pminsw m1, m9  ; d1
        pminsw m2, m10 ; d2
        pminsw m3, m11 ; d3
        pminsw m4, m12 ; d4
        pmaxsw m1, m2
        pmaxsw m3, m4
        pmaxsw m1, m3
        pmaxsw m1, m15 ; d

        mova m13, m0
        mova m14, m0
        mova m2, m0
        mova m4, m0
        psubw m13, m8
        psubw m14, m7
        psubw m2, m6
        psubw m4, m5
        pminsw m9, m13  ; u1
        pminsw m10, m14 ; u2
        pminsw m11, m2  ; u3
        pminsw m12, m4  ; u4
        pmaxsw m9, m10
        pmaxsw m11, m12
        pmaxsw m9, m11
        pmaxsw m9, m15  ; u

        paddw m0, m1
        psubw m0, m9
        packuswb m0, m0

        movh [dstq], m0
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET

cglobal rg_fl_mode_24, 4, 5, 16, mmsize, dst, src, stride, pixels
    mov r4q, strideq
    neg r4q
    %define stride_p strideq
    %define stride_n r4q

    pxor m15, m15
    .loop:
        LOAD_SQUARE_16 m15
        mova [rsp], m0
        SORT_AXIS_16

        mova m9, m8
        mova m10, m7
        mova m11, m6
        mova m12, m5
        psubw m9, m1  ; linediff1
        psubw m10, m2 ; linediff2
        psubw m11, m3 ; linediff3
        psubw m12, m4 ; linediff4

        psubw m1, [rsp] ; td1
        psubw m2, [rsp] ; td2
        psubw m3, [rsp] ; td3
        psubw m4, [rsp] ; td4
        mova m0, m9
        mova m13, m10
        mova m14, m11
        mova m15, m12
        psubw m0, m1
        psubw m13, m2
        psubw m14, m3
        psubw m15, m4
        pminsw m1, m0  ; d1
        pminsw m2, m13 ; d2
        pminsw m3, m14 ; d3
        pminsw m4, m15 ; d4
        pmaxsw m1, m2
        pmaxsw m3, m4

        mova m0, [rsp]
        mova m13, [rsp]
        mova m14, [rsp]
        mova m15, [rsp]
        psubw m0, m8  ; tu1
        psubw m13, m7 ; tu2
        psubw m14, m6 ; tu3
        psubw m15, m5 ; tu4
        psubw m9, m0
        psubw m10, m13
        psubw m11, m14
        psubw m12, m15
        pminsw m9, m0   ; u1
        pminsw m10, m13 ; u2
        pminsw m11, m14 ; u3
        pminsw m12, m15 ; u4
        pmaxsw m9, m10
        pmaxsw m11, m12

        pmaxsw m1, m3  ; d without max(d,0)
        pmaxsw m9, m11  ; u without max(u,0)
        pxor m15, m15
        pmaxsw m1, m15
        pmaxsw m9, m15

        mova m0, [rsp]
        paddw m0, m1
        psubw m0, m9
        packuswb m0, m0

        movh [dstq], m0
        add srcq, mmsize/2
        add dstq, mmsize/2
        sub pixelsd, mmsize/2
    jg .loop
RET
%endif
