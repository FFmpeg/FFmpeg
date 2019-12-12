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
pd_32768 : times 4 dd 32768
pd_65535 : times 4 dd 65535
pw_1:   times 8 dw 1
pw_128: times 8 dw 128
pw_255: times 8 dw 255
pb_127: times 16 db 127
pb_128: times 16 db 128
pb_255: times 16 db 255

SECTION .text

%macro BLEND_INIT 2-3
%if ARCH_X86_64
cglobal blend_%1, 6, 9, %2, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, end, x
    mov    widthd, dword widthm
    %if %0 == 3; is 16 bit
        add    widthq, widthq ; doesn't compile on x86_32
    %endif
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

%macro BLEND_SIMPLE 2-3
BLEND_INIT %1, 2, %3
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

; %1 name , %2 src (b or w), %3 inter (w or d), %4 (1 if 16bit, not set if 8 bit)
%macro GRAINEXTRACT 3-4
BLEND_INIT %1, 6, %4
    pxor           m4, m4
%if %0 == 4 ; 16 bit
    VBROADCASTI128 m5, [pd_32768]
%else
    VBROADCASTI128 m5, [pw_128]
%endif
.nextrow:
    mov        xq, widthq
    .loop:
        movu           m1, [topq + xq]
        movu           m3, [bottomq + xq]

        punpckl%2%3      m0, m1, m4
        punpckh%2%3      m1, m4
        punpckl%2%3      m2, m3, m4
        punpckh%2%3      m3, m4

        padd%3          m0, m5
        padd%3          m1, m5
        psub%3          m0, m2
        psub%3          m1, m3

        packus%3%2       m0, m1

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

;%1 name, %2 (b or w), %3 (set if 16 bit)
%macro AVERAGE 2-3
BLEND_INIT %1, 3, %3
    pcmpeqb        m2, m2

.nextrow:
    mov        xq, widthq

.loop:
    movu           m0, [topq + xq]
    movu           m1, [bottomq + xq]
    pxor           m0, m2
    pxor           m1, m2
    pavg%2         m0, m1
    pxor           m0, m2
    mova  [dstq + xq], m0
    add            xq, mmsize
    jl .loop
BLEND_END
%endmacro

; %1 name , %2 src (b or w), %3 inter (w or d), %4 (1 if 16bit, not set if 8 bit)
%macro GRAINMERGE 3-4
BLEND_INIT %1, 6, %4
    pxor       m4, m4
%if %0 == 4 ; 16 bit
    VBROADCASTI128       m5, [pd_32768]
%else
    VBROADCASTI128       m5, [pw_128]
%endif
.nextrow:
    mov        xq, widthq

    .loop:
        movu           m1, [topq + xq]
        movu           m3, [bottomq + xq]

        punpckl%2%3    m0, m1, m4
        punpckh%2%3    m1, m4
        punpckl%2%3    m2, m3, m4
        punpckh%2%3    m3, m4

        padd%3         m0, m2
        padd%3         m1, m3
        psub%3         m0, m5
        psub%3         m1, m5

        packus%3%2     m0, m1

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

%macro PHOENIX 2-3
; %1 name, %2 b or w, %3 (opt) 1 if 16 bit
BLEND_INIT %1, 4, %3
    VBROADCASTI128       m3, [pb_255]
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        mova            m2, m0
        pminu%2         m0, m1
        pmaxu%2         m1, m2
        mova            m2, m3
        psubus%2        m2, m1
        paddus%2        m2, m0
        mova   [dstq + xq], m2
        add             xq, mmsize
    jl .loop
BLEND_END
%endmacro

; %1 name , %2 src (b or w), %3 inter (w or d), %4 (1 if 16bit, not set if 8 bit)
%macro DIFFERENCE 3-4
BLEND_INIT %1, 5, %4
    pxor       m2, m2
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        punpckh%2%3     m3, m0, m2
        punpckl%2%3     m0, m2
        punpckh%2%3     m4, m1, m2
        punpckl%2%3     m1, m2
        psub%3          m0, m1
        psub%3          m3, m4
%if %0 == 4; 16 bit
        pabsd           m0, m0
        pabsd           m3, m3
%else
        ABS2            m0, m3, m1, m4
%endif
        packus%3%2      m0, m3
        mova   [dstq + xq], m0
        add             xq, mmsize
    jl .loop
BLEND_END
%endmacro

; %1 name , %2 src (b or w), %3 inter (w or d), %4 (1 if 16bit, not set if 8 bit)
%macro EXTREMITY 3-4
BLEND_INIT %1, 8, %4
    pxor       m2, m2
%if %0 == 4; 16 bit
    VBROADCASTI128       m4, [pd_65535]
%else
    VBROADCASTI128       m4, [pw_255]
%endif
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        punpckh%2%3     m5, m0, m2
        punpckl%2%3     m0, m2
        punpckh%2%3     m6, m1, m2
        punpckl%2%3     m1, m2
        psub%3          m3, m4, m0
        psub%3          m7, m4, m5
        psub%3          m3, m1
        psub%3          m7, m6
%if %0 == 4; 16 bit
        pabsd           m3, m3
        pabsd           m7, m7
%else
        ABS2            m3, m7, m1, m6
%endif
        packus%3%2      m3, m7
        mova   [dstq + xq], m3
        add             xq, mmsize
    jl .loop
BLEND_END
%endmacro

%macro NEGATION 3-4
BLEND_INIT %1, 8, %4
    pxor       m2, m2
%if %0 == 4; 16 bit
    VBROADCASTI128       m4, [pd_65535]
%else
    VBROADCASTI128       m4, [pw_255]
%endif
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [topq + xq]
        movu            m1, [bottomq + xq]
        punpckh%2%3     m5, m0, m2
        punpckl%2%3     m0, m2
        punpckh%2%3     m6, m1, m2
        punpckl%2%3     m1, m2
        psub%3          m3, m4, m0
        psub%3          m7, m4, m5
        psub%3          m3, m1
        psub%3          m7, m6
%if %0 == 4; 16 bit
        pabsd           m3, m3
        pabsd           m7, m7
%else
        ABS2            m3, m7, m1, m6
%endif
        psub%3          m0, m4, m3
        psub%3          m1, m4, m7
        packus%3%2      m0, m1
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
GRAINEXTRACT grainextract, b, w
BLEND_MULTIPLY
BLEND_SCREEN
AVERAGE       average,    b
GRAINMERGE    grainmerge, b, w
HARDMIX
PHOENIX phoenix, b
DIFFERENCE difference, b, w
DIVIDE
EXTREMITY extremity, b, w
NEGATION negation, b, w

%if ARCH_X86_64
BLEND_SIMPLE addition_16, addusw, 1
BLEND_SIMPLE and_16,      and,    1
BLEND_SIMPLE or_16,       or,     1
BLEND_SIMPLE subtract_16, subusw, 1
BLEND_SIMPLE xor_16,      xor,    1
AVERAGE      average_16,  w,      1
%endif

INIT_XMM ssse3
DIFFERENCE difference, b, w
EXTREMITY extremity, b, w
NEGATION negation, b, w

INIT_XMM sse4
%if ARCH_X86_64
BLEND_SIMPLE darken_16,   minuw, 1
BLEND_SIMPLE lighten_16,  maxuw, 1
GRAINEXTRACT grainextract_16, w, d, 1
GRAINMERGE   grainmerge_16, w, d, 1
PHOENIX      phoenix_16,      w, 1
DIFFERENCE   difference_16, w, d, 1
EXTREMITY    extremity_16, w, d, 1
NEGATION     negation_16, w, d, 1
%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
BLEND_SIMPLE xor,      xor
BLEND_SIMPLE or,       or
BLEND_SIMPLE and,      and
BLEND_SIMPLE addition, addusb
BLEND_SIMPLE subtract, subusb
BLEND_SIMPLE darken,   minub
BLEND_SIMPLE lighten,  maxub
GRAINEXTRACT grainextract, b, w
BLEND_MULTIPLY
BLEND_SCREEN
AVERAGE    average,    b
GRAINMERGE grainmerge, b, w
HARDMIX
PHOENIX phoenix, b

DIFFERENCE difference, b, w
EXTREMITY extremity, b, w
NEGATION negation, b, w

%if ARCH_X86_64
BLEND_SIMPLE addition_16, addusw, 1
BLEND_SIMPLE and_16,      and,    1
BLEND_SIMPLE darken_16,   minuw,  1
BLEND_SIMPLE lighten_16,  maxuw,  1
BLEND_SIMPLE or_16,       or,     1
BLEND_SIMPLE subtract_16, subusw, 1
BLEND_SIMPLE xor_16,      xor,    1
GRAINEXTRACT grainextract_16, w, d, 1
AVERAGE      average_16,  w,      1
GRAINMERGE   grainmerge_16, w, d, 1
PHOENIX      phoenix_16,       w, 1
DIFFERENCE   difference_16, w, d, 1
EXTREMITY    extremity_16, w, d, 1
NEGATION     negation_16, w, d, 1
%endif
%endif
