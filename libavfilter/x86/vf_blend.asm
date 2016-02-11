;*****************************************************************************
;* x86-optimized functions for blend filter
;*
;* Copyright (C) 2015 Paul B Mahol
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

pw_1:   times 8 dw 1
pw_128: times 8 dw 128
pw_255: times 8 dw 255
pb_127: times 16 db 127
pb_128: times 16 db 128
pb_255: times 16 db 255

SECTION .text

%macro BLEND_INIT 2
%if ARCH_X86_64
cglobal blend_%1, 6, 9, %2, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, end, x
    mov    widthd, dword widthm
%else
cglobal blend_%1, 5, 7, %2, top, top_linesize, bottom, bottom_linesize, dst, end, x
%define dst_linesizeq r5mp
%define widthq r6mp
%endif
    mov      endd, dword r8m
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endd, dword r7m ; start
    neg    widthq
%endmacro

%macro BLEND_END 0
    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET
%endmacro

%macro BLEND_SIMPLE 2
BLEND_INIT %1, 2
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        p%2             m0, m1
        mova   [dstq + xq], m0
        add             xq, mmsize
    jl .loop
BLEND_END
%endmacro

INIT_XMM sse2
BLEND_SIMPLE xor,      xor
BLEND_SIMPLE or,       or
BLEND_SIMPLE and,      and
BLEND_SIMPLE addition, addusb
BLEND_SIMPLE subtract, subusb
BLEND_SIMPLE darken,   minub
BLEND_SIMPLE lighten,  maxub

BLEND_INIT difference128, 4
    pxor       m2, m2
    mova       m3, [pw_128]
.nextrow:
    mov        xq, widthq

    .loop:
        movh            m0, [topq + xq]
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        paddw           m0, m3
        psubw           m0, m1
        packuswb        m0, m0
        movh   [dstq + xq], m0
        add             xq, mmsize / 2
    jl .loop
BLEND_END

%macro MULTIPLY 3 ; a, b, pw_1
    pmullw          %1, %2               ; xxxxxxxx  a * b
    paddw           %1, %3
    mova            %2, %1
    psrlw           %2, 8
    paddw           %1, %2
    psrlw           %1, 8                ; 00xx00xx  a * b / 255
%endmacro

%macro SCREEN 4   ; a, b, pw_1, pw_255
    pxor            %1, %4               ; 00xx00xx  255 - a
    pxor            %2, %4
    MULTIPLY        %1, %2, %3
    pxor            %1, %4               ; 00xx00xx  255 - x / 255
%endmacro

BLEND_INIT multiply, 4
    pxor       m2, m2
    mova       m3, [pw_1]
.nextrow:
    mov        xq, widthq

    .loop:
                                             ;     word
                                             ;     |--|
        movh            m0, [topq + xq]      ; 0000xxxx
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2               ; 00xx00xx
        punpcklbw       m1, m2

        MULTIPLY        m0, m1, m3

        packuswb        m0, m0               ; 0000xxxx
        movh   [dstq + xq], m0
        add             xq, mmsize / 2

    jl .loop
BLEND_END

BLEND_INIT screen, 5
    pxor       m2, m2
    mova       m3, [pw_1]
    mova       m4, [pw_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movh            m0, [topq + xq]      ; 0000xxxx
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2               ; 00xx00xx
        punpcklbw       m1, m2

        SCREEN          m0, m1, m3, m4

        packuswb        m0, m0               ; 0000xxxx
        movh   [dstq + xq], m0
        add             xq, mmsize / 2

    jl .loop
BLEND_END

BLEND_INIT average, 3
    pxor       m2, m2
.nextrow:
    mov        xq, widthq

    .loop:
        movh            m0, [topq + xq]
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        paddw           m0, m1
        psrlw           m0, 1
        packuswb        m0, m0
        movh   [dstq + xq], m0
        add             xq, mmsize / 2
    jl .loop
BLEND_END

BLEND_INIT addition128, 4
    pxor       m2, m2
    mova       m3, [pw_128]
.nextrow:
    mov        xq, widthq

    .loop:
        movh            m0, [topq + xq]
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        paddw           m0, m1
        psubw           m0, m3
        packuswb        m0, m0
        movh   [dstq + xq], m0
        add             xq, mmsize / 2
    jl .loop
BLEND_END

BLEND_INIT hardmix, 5
    mova       m2, [pb_255]
    mova       m3, [pb_128]
    mova       m4, [pb_127]
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        pxor            m1, m4
        pxor            m0, m3
        pcmpgtb         m1, m0
        pxor            m1, m2
        mova   [dstq + xq], m1
        add             xq, mmsize
    jl .loop
BLEND_END

BLEND_INIT phoenix, 4
    mova       m3, [pb_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        mova            m2, m0
        pminub          m0, m1
        pmaxub          m1, m2
        mova            m2, m3
        psubusb         m2, m1
        paddusb         m2, m0
        mova   [dstq + xq], m2
        add             xq, mmsize
    jl .loop
BLEND_END

%macro BLEND_ABS 0
BLEND_INIT difference, 3
    pxor       m2, m2
.nextrow:
    mov        xq, widthq

    .loop:
        movh            m0, [topq + xq]
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        psubw           m0, m1
        ABS1            m0, m1
        packuswb        m0, m0
        movh   [dstq + xq], m0
        add             xq, mmsize / 2
    jl .loop
BLEND_END

BLEND_INIT negation, 5
    pxor       m2, m2
    mova       m4, [pw_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movh            m0, [topq + xq]
        movh            m1, [bottomq + xq]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        mova            m3, m4
        psubw           m3, m0
        psubw           m3, m1
        ABS1            m3, m1
        mova            m0, m4
        psubw           m0, m3
        packuswb        m0, m0
        movh   [dstq + xq], m0
        add             xq, mmsize / 2
    jl .loop
BLEND_END
%endmacro

INIT_XMM sse2
BLEND_ABS
INIT_XMM ssse3
BLEND_ABS
