;******************************************************************************
;* x86-optimized functions for the CFHD decoder
;* Copyright (c) 2020 Paul B Mahol
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

factor_p1_n1: dw 1, -1, 1, -1, 1, -1, 1, -1,
factor_n1_p1: dw -1, 1, -1, 1, -1, 1, -1, 1,
factor_p11_n4: dw 11, -4, 11, -4, 11, -4, 11, -4,
factor_p5_p4: dw 5, 4, 5, 4, 5, 4, 5, 4,
pd_4: times 4 dd 4
pw_1: times 8 dw 1
pw_0: times 8 dw 0
pw_1023: times 8 dw 1023
pw_4095: times 8 dw 4095

SECTION .text

%macro CFHD_HORIZ_FILTER 1
%if %1 == 1023
cglobal cfhd_horiz_filter_clip10, 5, 6, 8 + 4 * ARCH_X86_64, output, low, high, width, x, temp
    shl        widthd, 1
%define ostrideq widthq
%define lwidthq  widthq
%define hwidthq  widthq
%elif %1 == 4095
cglobal cfhd_horiz_filter_clip12, 5, 6, 8 + 4 * ARCH_X86_64, output, low, high, width, x, temp
    shl        widthd, 1
%define ostrideq widthq
%define lwidthq  widthq
%define hwidthq  widthq
%else
%if ARCH_X86_64
cglobal cfhd_horiz_filter, 8, 11, 12, output, ostride, low, lwidth, high, hwidth, width, height, x, y, temp
    shl  ostrided, 1
    shl   lwidthd, 1
    shl   hwidthd, 1
    shl    widthd, 1

    mov        yd, heightd
    neg        yq
%else
cglobal cfhd_horiz_filter, 7, 7, 8, output, x, low, y, high, temp, width, height
    shl        xd, 1
    shl        yd, 1
    shl     tempd, 1
    shl    widthd, 1

    mov       xmp, xq
    mov       ymp, yq
    mov    tempmp, tempq

    mov        yd, r7m
    neg        yq

%define ostrideq xm
%define lwidthq  ym
%define hwidthq  tempm
%endif
%endif

%if ARCH_X86_64
    mova       m8, [factor_p1_n1]
    mova       m9, [factor_n1_p1]
    mova      m10, [pw_1]
    mova      m11, [pd_4]
%endif

%if %1 == 0
.looph:
%endif
    movsx          xq, word [lowq]
    imul           xq, 11

    movsx       tempq, word [lowq + 2]
    imul        tempq, -4
    add         tempq, xq

    movsx          xq, word [lowq + 4]
    add         tempq, xq
    add         tempq, 4
    sar         tempq, 3

    movsx          xq, word [highq]
    add         tempq, xq
    sar         tempq, 1

%if %1
    movd          xm0, tempd
    CLIPW          m0, [pw_0], [pw_%1]
    pextrw      tempd, xm0, 0
%endif
    mov  word [outputq], tempw

    movsx          xq, word [lowq]
    imul           xq, 5

    movsx       tempq, word [lowq + 2]
    imul        tempq, 4
    add         tempq, xq

    movsx          xq, word [lowq + 4]
    sub         tempq, xq
    add         tempq, 4
    sar         tempq, 3

    movsx          xq, word [highq]
    sub         tempq, xq
    sar         tempq, 1

%if %1
    movd          xm0, tempd
    CLIPW          m0, [pw_0], [pw_%1]
    pextrw      tempd, xm0, 0
%endif
    mov  word [outputq + 2], tempw

    mov            xq, 0

.loop:
    movu           m4, [lowq + xq]
    movu           m1, [lowq + xq + 4]

    mova           m5, m4
    punpcklwd      m4, m1
    punpckhwd      m5, m1

    mova           m6, m4
    mova           m7, m5

%if ARCH_X86_64
    pmaddwd        m4, m8
    pmaddwd        m5, m8
    pmaddwd        m6, m9
    pmaddwd        m7, m9

    paddd          m4, m11
    paddd          m5, m11
    paddd          m6, m11
    paddd          m7, m11
%else
    pmaddwd        m4, [factor_p1_n1]
    pmaddwd        m5, [factor_p1_n1]
    pmaddwd        m6, [factor_n1_p1]
    pmaddwd        m7, [factor_n1_p1]

    paddd          m4, [pd_4]
    paddd          m5, [pd_4]
    paddd          m6, [pd_4]
    paddd          m7, [pd_4]
%endif

    psrad          m4, 3
    psrad          m5, 3
    psrad          m6, 3
    psrad          m7, 3

    movu           m2, [lowq + xq + 2]
    movu           m3, [highq + xq + 2]

    mova           m0, m2
    punpcklwd      m2, m3
    punpckhwd      m0, m3

    mova           m1, m2
    mova           m3, m0

%if ARCH_X86_64
    pmaddwd        m2, m10
    pmaddwd        m0, m10
    pmaddwd        m1, m8
    pmaddwd        m3, m8
%else
    pmaddwd        m2, [pw_1]
    pmaddwd        m0, [pw_1]
    pmaddwd        m1, [factor_p1_n1]
    pmaddwd        m3, [factor_p1_n1]
%endif

    paddd          m2, m4
    paddd          m0, m5
    paddd          m1, m6
    paddd          m3, m7

    psrad          m2, 1
    psrad          m0, 1
    psrad          m1, 1
    psrad          m3, 1

    packssdw       m2, m0
    packssdw       m1, m3

    mova           m0, m2
    punpcklwd      m2, m1
    punpckhwd      m0, m1

%if %1
    CLIPW          m2, [pw_0], [pw_%1]
    CLIPW          m0, [pw_0], [pw_%1]
%endif

    movu  [outputq + xq * 2 + 4], m2
    movu  [outputq + xq * 2 + mmsize + 4], m0

    add            xq, mmsize
    cmp            xq, widthq
    jl .loop

    add          lowq, widthq
    add         highq, widthq
    add       outputq, widthq
    add       outputq, widthq

    movsx          xq, word [lowq - 2]
    imul           xq, 5

    movsx       tempq, word [lowq - 4]
    imul        tempq, 4
    add         tempq, xq

    movsx          xq, word [lowq - 6]
    sub         tempq, xq
    add         tempq, 4
    sar         tempq, 3

    movsx          xq, word [highq - 2]
    add         tempq, xq
    sar         tempq, 1

%if %1
    movd          xm0, tempd
    CLIPW          m0, [pw_0], [pw_%1]
    pextrw      tempd, xm0, 0
%endif
    mov  word [outputq - 4], tempw

    movsx          xq, word [lowq - 2]
    imul           xq, 11

    movsx       tempq, word [lowq - 4]
    imul        tempq, -4
    add         tempq, xq

    movsx          xq, word [lowq - 6]
    add         tempq, xq
    add         tempq, 4
    sar         tempq, 3

    movsx          xq, word [highq - 2]
    sub         tempq, xq
    sar         tempq, 1

%if %1
    movd          xm0, tempd
    CLIPW          m0, [pw_0], [pw_%1]
    pextrw      tempd, xm0, 0
%endif
    mov  word [outputq - 2], tempw

%if %1 == 0
    sub          lowq, widthq
    sub         highq, widthq
    sub       outputq, widthq
    sub       outputq, widthq

    add          lowq, lwidthq
    add         highq, hwidthq
    add       outputq, ostrideq
    add       outputq, ostrideq
    add            yq, 1
    jl .looph
%endif

    RET
%endmacro

INIT_XMM sse2
CFHD_HORIZ_FILTER 0

INIT_XMM sse2
CFHD_HORIZ_FILTER 1023

INIT_XMM sse2
CFHD_HORIZ_FILTER 4095

INIT_XMM sse2
%if ARCH_X86_64
cglobal cfhd_vert_filter, 8, 11, 14, output, ostride, low, lwidth, high, hwidth, width, height, x, y, pos
    shl        ostrided, 1
    shl         lwidthd, 1
    shl         hwidthd, 1
    shl          widthd, 1

    dec   heightd

    mova       m8, [factor_p1_n1]
    mova       m9, [factor_n1_p1]
    mova      m10, [pw_1]
    mova      m11, [pd_4]
    mova      m12, [factor_p11_n4]
    mova      m13, [factor_p5_p4]
%else
cglobal cfhd_vert_filter, 7, 7, 8, output, x, low, y, high, pos, width, height
    shl        xd, 1
    shl        yd, 1
    shl      posd, 1
    shl    widthd, 1

    mov       xmp, xq
    mov       ymp, yq
    mov     posmp, posq

    mov        xq, r7m
    dec        xq
    mov   widthmp, xq

%define ostrideq xm
%define lwidthq  ym
%define hwidthq  posm
%define heightq  widthm

%endif

    xor        xq, xq
.loopw:
    xor        yq, yq

    mov      posq, xq
    movu       m0, [lowq + posq]
    add      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m2, m0
    punpcklwd  m0, m1
    punpckhwd  m2, m1

%if ARCH_X86_64
    pmaddwd    m0, m12
    pmaddwd    m2, m12
%else
    pmaddwd    m0, [factor_p11_n4]
    pmaddwd    m2, [factor_p11_n4]
%endif

    pxor       m4, m4
    add      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    paddd      m0, m4
    paddd      m2, m3

    paddd      m0, [pd_4]
    paddd      m2, [pd_4]

    psrad      m0, 3
    psrad      m2, 3

    mov      posq, xq
    pxor       m4, m4
    movu       m1, [highq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    paddd      m0, m4
    paddd      m2, m3

    psrad      m0, 1
    psrad      m2, 1

    packssdw   m0, m2

    movu    [outputq + posq], m0

    movu       m0, [lowq + posq]
    add      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m2, m0
    punpcklwd  m0, m1
    punpckhwd  m2, m1

%if ARCH_X86_64
    pmaddwd    m0, m13
    pmaddwd    m2, m13
%else
    pmaddwd    m0, [factor_p5_p4]
    pmaddwd    m2, [factor_p5_p4]
%endif

    pxor       m4, m4
    add      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    psubd      m0, m4
    psubd      m2, m3

    paddd      m0, [pd_4]
    paddd      m2, [pd_4]

    psrad      m0, 3
    psrad      m2, 3

    mov      posq, xq
    pxor       m4, m4
    movu       m1, [highq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    psubd      m0, m4
    psubd      m2, m3

    psrad      m0, 1
    psrad      m2, 1

    packssdw   m0, m2

    add      posq, ostrideq
    movu    [outputq + posq], m0

    add        yq, 1
.looph:
    mov      posq, lwidthq
    imul     posq, yq
    sub      posq, lwidthq
    add      posq, xq

    movu       m4, [lowq + posq]

    add      posq, lwidthq
    add      posq, lwidthq
    movu       m1, [lowq + posq]

    mova       m5, m4
    punpcklwd  m4, m1
    punpckhwd  m5, m1

    mova       m6, m4
    mova       m7, m5

%if ARCH_X86_64
    pmaddwd    m4, m8
    pmaddwd    m5, m8
    pmaddwd    m6, m9
    pmaddwd    m7, m9

    paddd      m4, m11
    paddd      m5, m11
    paddd      m6, m11
    paddd      m7, m11
%else
    pmaddwd    m4, [factor_p1_n1]
    pmaddwd    m5, [factor_p1_n1]
    pmaddwd    m6, [factor_n1_p1]
    pmaddwd    m7, [factor_n1_p1]

    paddd      m4, [pd_4]
    paddd      m5, [pd_4]
    paddd      m6, [pd_4]
    paddd      m7, [pd_4]
%endif

    psrad      m4, 3
    psrad      m5, 3
    psrad      m6, 3
    psrad      m7, 3

    sub      posq, lwidthq
    movu       m0, [lowq + posq]

    mov      posq, hwidthq
    imul     posq, yq
    add      posq, xq
    movu       m1, [highq + posq]

    mova       m2, m0
    punpcklwd  m0, m1
    punpckhwd  m2, m1

    mova       m1, m0
    mova       m3, m2

%if ARCH_X86_64
    pmaddwd    m0, m10
    pmaddwd    m2, m10
    pmaddwd    m1, m8
    pmaddwd    m3, m8
%else
    pmaddwd    m0, [pw_1]
    pmaddwd    m2, [pw_1]
    pmaddwd    m1, [factor_p1_n1]
    pmaddwd    m3, [factor_p1_n1]
%endif

    paddd      m0, m4
    paddd      m2, m5
    paddd      m1, m6
    paddd      m3, m7

    psrad      m0, 1
    psrad      m2, 1
    psrad      m1, 1
    psrad      m3, 1

    packssdw   m0, m2
    packssdw   m1, m3

    mov      posq, ostrideq
    imul     posq, 2
    imul     posq, yq
    add      posq, xq

    movu    [outputq + posq], m0
    add      posq, ostrideq
    movu    [outputq + posq], m1

    add        yq, 1
    cmp        yq, heightq
    jl .looph

    mov      posq, lwidthq
    imul     posq, yq
    add      posq, xq
    movu       m0, [lowq + posq]
    sub      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m2, m0
    punpcklwd  m0, m1
    punpckhwd  m2, m1

%if ARCH_X86_64
    pmaddwd    m0, m13
    pmaddwd    m2, m13
%else
    pmaddwd    m0, [factor_p5_p4]
    pmaddwd    m2, [factor_p5_p4]
%endif

    pxor       m4, m4
    sub      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    psubd      m0, m4
    psubd      m2, m3

%if ARCH_X86_64
    paddd      m0, m11
    paddd      m2, m11
%else
    paddd      m0, [pd_4]
    paddd      m2, [pd_4]
%endif

    psrad      m0, 3
    psrad      m2, 3

    mov      posq, hwidthq
    imul     posq, yq
    add      posq, xq
    pxor       m4, m4
    movu       m1, [highq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    paddd      m0, m4
    paddd      m2, m3

    psrad      m0, 1
    psrad      m2, 1

    packssdw   m0, m2

    mov      posq, ostrideq
    imul     posq, 2
    imul     posq, yq
    add      posq, xq
    movu    [outputq + posq], m0

    mov      posq, lwidthq
    imul     posq, yq
    add      posq, xq
    movu       m0, [lowq + posq]
    sub      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m2, m0
    punpcklwd  m0, m1
    punpckhwd  m2, m1

%if ARCH_X86_64
    pmaddwd    m0, m12
    pmaddwd    m2, m12
%else
    pmaddwd    m0, [factor_p11_n4]
    pmaddwd    m2, [factor_p11_n4]
%endif

    pxor       m4, m4
    sub      posq, lwidthq
    movu       m1, [lowq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    paddd      m0, m4
    paddd      m2, m3

%if ARCH_X86_64
    paddd      m0, m11
    paddd      m2, m11
%else
    paddd      m0, [pd_4]
    paddd      m2, [pd_4]
%endif

    psrad      m0, 3
    psrad      m2, 3

    mov      posq, hwidthq
    imul     posq, yq
    add      posq, xq
    pxor       m4, m4
    movu       m1, [highq + posq]
    mova       m3, m4
    punpcklwd  m4, m1
    punpckhwd  m3, m1

    psrad      m4, 16
    psrad      m3, 16

    psubd      m0, m4
    psubd      m2, m3

    psrad      m0, 1
    psrad      m2, 1

    packssdw   m0, m2

    mov      posq, ostrideq
    imul     posq, 2
    imul     posq, yq
    add      posq, ostrideq
    add      posq, xq
    movu    [outputq + posq], m0

    add        xq, mmsize
    cmp        xq, widthq
    jl .loopw
    RET
