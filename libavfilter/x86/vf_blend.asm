;*****************************************************************************
;* x86-optimized functions for blend filter
;*
;* Copyright (C) 2015 Paul B Mahol
;* Copyright (C) 2018 Henrik Gramner
;* Copyright (C) 2018 Jokyo Images
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

ps_255: times 4 dd 255.0
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
    mov      endd, dword r7m
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
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

%macro GRAINEXTRACT 0
BLEND_INIT grainextract, 6
    pxor           m4, m4
    VBROADCASTI128 m5, [pw_128]
.nextrow:
    mov        xq, widthq
    .loop:
        movu           m1, [topq + xq]
        movu           m3, [bottomq + xq]
        punpcklbw      m0, m1, m4
        punpckhbw      m1, m4
        punpcklbw      m2, m3, m4
        punpckhbw      m3, m4

        paddw          m0, m5
        paddw          m1, m5
        psubw          m0, m2
        psubw          m1, m3

        packuswb       m0, m1
        mova  [dstq + xq], m0
        add            xq, mmsize
    jl .loop
BLEND_END
%endmacro

%macro MULTIPLY 3 ; a, b, pw_1
    pmullw          %1, %2               ; xxxxxxxx  a * b
    paddw           %1, %3
    psrlw           %2, %1, 8
    paddw           %1, %2
    psrlw           %1, 8                ; 00xx00xx  a * b / 255
%endmacro

%macro SCREEN 4   ; a, b, pw_1, pw_255
    pxor            %1, %4               ; 00xx00xx  255 - a
    pxor            %2, %4
    MULTIPLY        %1, %2, %3
    pxor            %1, %4               ; 00xx00xx  255 - x / 255
%endmacro

%macro BLEND_MULTIPLY 0
BLEND_INIT multiply, 6
    pxor       m4, m4
    VBROADCASTI128       m5, [pw_1]
.nextrow:
    mov        xq, widthq

    .loop:
        movu           m1, [topq + xq]
        movu           m3, [bottomq + xq]
        punpcklbw      m0, m1, m4
        punpckhbw      m1, m4
        punpcklbw      m2, m3, m4
        punpckhbw      m3, m4

        MULTIPLY        m0, m2, m5
        MULTIPLY        m1, m3, m5

        packuswb       m0, m1
        mova  [dstq + xq], m0
        add            xq, mmsize
    jl .loop
BLEND_END
%endmacro

%macro BLEND_SCREEN 0
BLEND_INIT screen, 7
    pxor       m4, m4

    VBROADCASTI128       m5, [pw_1]
    VBROADCASTI128       m6, [pw_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movu           m1, [topq + xq]
        movu           m3, [bottomq + xq]
        punpcklbw      m0, m1, m4
        punpckhbw      m1, m4
        punpcklbw      m2, m3, m4
        punpckhbw      m3, m4

        SCREEN          m0, m2, m5, m6
        SCREEN          m1, m3, m5, m6

        packuswb       m0, m1
        mova  [dstq + xq], m0
        add            xq, mmsize
    jl .loop
BLEND_END
%endmacro

%macro AVERAGE 0
BLEND_INIT average, 3
    pcmpeqb        m2, m2

.nextrow:
    mov        xq, widthq

.loop:
    movu           m0, [topq + xq]
    movu           m1, [bottomq + xq]
    pxor           m0, m2
    pxor           m1, m2
    pavgb          m0, m1
    pxor           m0, m2
    mova  [dstq + xq], m0
    add            xq, mmsize
    jl .loop
BLEND_END
%endmacro


%macro GRAINMERGE 0
BLEND_INIT grainmerge, 6
    pxor       m4, m4

    VBROADCASTI128       m5, [pw_128]
.nextrow:
    mov        xq, widthq

    .loop:
        movu           m1, [topq + xq]
        movu           m3, [bottomq + xq]
        punpcklbw      m0, m1, m4
        punpckhbw      m1, m4
        punpcklbw      m2, m3, m4
        punpckhbw      m3, m4

        paddw           m0, m2
        paddw           m1, m3
        psubw           m0, m5
        psubw           m1, m5

        packuswb       m0, m1
        mova  [dstq + xq], m0
        add            xq, mmsize
    jl .loop
BLEND_END
%endmacro

%macro HARDMIX 0
BLEND_INIT hardmix, 5
    VBROADCASTI128       m2, [pb_255]
    VBROADCASTI128       m3, [pb_128]
    VBROADCASTI128       m4, [pb_127]
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
%endmacro

%macro DIVIDE 0
BLEND_INIT divide, 4
    pxor       m2, m2
    mova       m3, [ps_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movd            m0, [topq + xq]      ; 000000xx
        movd            m1, [bottomq + xq]
        punpcklbw       m0, m2               ; 00000x0x
        punpcklbw       m1, m2
        punpcklwd       m0, m2               ; 000x000x
        punpcklwd       m1, m2

        cvtdq2ps        m0, m0
        cvtdq2ps        m1, m1
        divps           m0, m1               ; a / b
        mulps           m0, m3               ; a / b * 255
        minps           m0, m3
        cvttps2dq       m0, m0

        packssdw        m0, m0               ; 00000x0x
        packuswb        m0, m0               ; 000000xx
        movd   [dstq + xq], m0
        add             xq, mmsize / 4

    jl .loop
BLEND_END
%endmacro

%macro PHOENIX 0
BLEND_INIT phoenix, 4
    VBROADCASTI128       m3, [pb_255]
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
%endmacro

%macro BLEND_ABS 0
BLEND_INIT difference, 5
    pxor       m2, m2
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        punpckhbw       m3, m0, m2
        punpcklbw       m0, m2
        punpckhbw       m4, m1, m2
        punpcklbw       m1, m2
        psubw           m0, m1
        psubw           m3, m4
        ABS2            m0, m3, m1, m4
        packuswb        m0, m3
        mova   [dstq + xq], m0
        add             xq, mmsize
    jl .loop
BLEND_END

BLEND_INIT extremity, 8
    pxor       m2, m2
    VBROADCASTI128       m4, [pw_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        punpckhbw       m5, m0, m2
        punpcklbw       m0, m2
        punpckhbw       m6, m1, m2
        punpcklbw       m1, m2
        psubw           m3, m4, m0
        psubw           m7, m4, m5
        psubw           m3, m1
        psubw           m7, m6
        ABS2            m3, m7, m1, m6
        packuswb        m3, m7
        mova   [dstq + xq], m3
        add             xq, mmsize
    jl .loop
BLEND_END

BLEND_INIT negation, 8
    pxor       m2, m2
    VBROADCASTI128       m4, [pw_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        punpckhbw       m5, m0, m2
        punpcklbw       m0, m2
        punpckhbw       m6, m1, m2
        punpcklbw       m1, m2
        psubw           m3, m4, m0
        psubw           m7, m4, m5
        psubw           m3, m1
        psubw           m7, m6
        ABS2            m3, m7, m1, m6
        psubw           m0, m4, m3
        psubw           m1, m4, m7
        packuswb        m0, m1
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
GRAINEXTRACT
BLEND_MULTIPLY
BLEND_SCREEN
AVERAGE
GRAINMERGE
HARDMIX
PHOENIX
DIVIDE

BLEND_ABS

INIT_XMM ssse3
BLEND_ABS

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
BLEND_SIMPLE xor,      xor
BLEND_SIMPLE or,       or
BLEND_SIMPLE and,      and
BLEND_SIMPLE addition, addusb
BLEND_SIMPLE subtract, subusb
BLEND_SIMPLE darken,   minub
BLEND_SIMPLE lighten,  maxub
GRAINEXTRACT
BLEND_MULTIPLY
BLEND_SCREEN
AVERAGE
GRAINMERGE
HARDMIX
PHOENIX

BLEND_ABS
%endif
