;******************************************************************************
;* x86-optimized functions for the CFHD encoder
;* Copyright (c) 2021 Paul B Mahol
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

pw_p1_n1:  dw  1, -1, 1, -1, 1, -1, 1, -1
pw_n1_p1:  dw  -1, 1, -1, 1, -1, 1, -1, 1
pw_p5_n11: dw  5, -11, 5, -11, 5, -11, 5, -11
pw_n5_p11: dw -5, 11, -5, 11, -5, 11, -5, 11
pw_p11_n5: dw 11, -5, 11, -5, 11, -5, 11, -5
pw_n11_p5: dw -11, 5, -11, 5, -11, 5, -11, 5
pd_4:  times 4 dd  4
pw_n4: times 8 dw -4
cextern pw_m1
cextern pw_1
cextern pw_4

SECTION .text

%if ARCH_X86_64
INIT_XMM sse2
cglobal cfhdenc_horiz_filter, 8, 10, 11, input, low, high, istride, lwidth, hwidth, width, y, x, temp
    shl  istrideq, 1
    shl   lwidthq, 1
    shl   hwidthq, 1
    mova       m7, [pd_4]
    mova       m8, [pw_1]
    mova       m9, [pw_m1]
    mova       m10,[pw_p1_n1]
    movsxdifnidn yq, yd
    movsxdifnidn widthq, widthd
    neg        yq
.looph:
    movsx          xq, word [inputq]

    movsx       tempq, word [inputq + 2]
    add         tempq, xq

    movd          xm0, tempd
    packssdw       m0, m0
    movd        tempd, m0
    mov   word [lowq], tempw

    movsx          xq, word [inputq]
    imul           xq, 5
    movsx       tempq, word [inputq + 2]
    imul        tempq, -11
    add         tempq, xq

    movsx          xq, word [inputq + 4]
    imul           xq, 4
    add         tempq, xq

    movsx          xq, word [inputq + 6]
    imul           xq, 4
    add         tempq, xq

    movsx          xq, word [inputq + 8]
    imul           xq, -1
    add         tempq, xq

    movsx          xq, word [inputq + 10]
    imul           xq, -1
    add         tempq, xq

    add         tempq, 4
    sar         tempq, 3

    movd          xm0, tempd
    packssdw       m0, m0
    movd        tempd, m0
    mov  word [highq], tempw

    mov            xq, 2

.loopw:
    movu           m0, [inputq + xq * 2]
    movu           m1, [inputq + xq * 2 + mmsize]

    pmaddwd        m0, m8
    pmaddwd        m1, m8

    packssdw       m0, m1
    movu    [lowq+xq], m0

    movu           m2, [inputq + xq * 2 - 4]
    movu           m3, [inputq + xq * 2 - 4 + mmsize]

    pmaddwd        m2, m9
    pmaddwd        m3, m9

    movu           m0, [inputq + xq * 2 + 4]
    movu           m1, [inputq + xq * 2 + 4 + mmsize]

    pmaddwd        m0, m8
    pmaddwd        m1, m8

    paddd          m0, m2
    paddd          m1, m3

    paddd          m0, m7
    paddd          m1, m7

    psrad          m0, 3
    psrad          m1, 3

    movu           m5, [inputq + xq * 2 + 0]
    movu           m6, [inputq + xq * 2 + mmsize]

    pmaddwd        m5, m10
    pmaddwd        m6, m10

    paddd          m0, m5
    paddd          m1, m6

    packssdw       m0, m1
    movu   [highq+xq], m0

    add            xq, mmsize
    cmp            xq, widthq
    jl .loopw

    add          lowq, widthq
    add         highq, widthq
    lea        inputq, [inputq + widthq * 2]

    movsx          xq, word [inputq - 4]
    movsx       tempq, word [inputq - 2]
    add         tempq, xq

    movd          xm0, tempd
    packssdw       m0, m0
    movd        tempd, m0
    mov word [lowq-2], tempw

    movsx       tempq, word [inputq - 4]
    imul        tempq, 11
    movsx          xq, word [inputq - 2]
    imul           xq, -5
    add         tempq, xq

    movsx          xq, word [inputq - 6]
    imul           xq, -4
    add         tempq, xq

    movsx          xq, word [inputq - 8]
    imul           xq, -4
    add         tempq, xq

    movsx          xq, word [inputq - 10]
    add         tempq, xq

    movsx          xq, word [inputq - 12]
    add         tempq, xq

    add         tempq, 4
    sar         tempq, 3

    movd          xm0, tempd
    packssdw       m0, m0
    movd        tempd, m0
    mov word [highq-2], tempw

    sub        inputq, widthq
    sub        inputq, widthq
    sub         highq, widthq
    sub          lowq, widthq

    add          lowq, lwidthq
    add         highq, hwidthq
    add        inputq, istrideq
    add            yq, 1
    jl .looph

    RET
%endif

%if ARCH_X86_64
INIT_XMM sse2
cglobal cfhdenc_vert_filter, 8, 11, 14, input, low, high, istride, lwidth, hwidth, width, height, x, y, pos
    shl  istrideq, 1

    shl    widthd, 1
    sub   heightd, 2

    xor        xq, xq

    mova       m7, [pd_4]
    mova       m8, [pw_1]
    mova       m9, [pw_m1]
    mova       m10,[pw_p1_n1]
    mova       m11,[pw_n1_p1]
    mova       m12,[pw_4]
    mova       m13,[pw_n4]
.loopw:
    mov        yq, 2

    mov      posq, xq
    movu       m0, [inputq + posq]
    add      posq, istrideq
    movu       m1, [inputq + posq]

    paddsw     m0, m1

    movu    [lowq + xq], m0

    mov      posq, xq

    movu       m0, [inputq + posq]
    add      posq, istrideq
    movu       m1, [inputq + posq]
    add      posq, istrideq
    movu       m2, [inputq + posq]
    add      posq, istrideq
    movu       m3, [inputq + posq]
    add      posq, istrideq
    movu       m4, [inputq + posq]
    add      posq, istrideq
    movu       m5, [inputq + posq]

    mova       m6, m0
    punpcklwd  m0, m1
    punpckhwd  m1, m6

    mova       m6, m2
    punpcklwd  m2, m3
    punpckhwd  m3, m6

    mova       m6, m4
    punpcklwd  m4, m5
    punpckhwd  m5, m6

    pmaddwd    m0, [pw_p5_n11]
    pmaddwd    m1, [pw_n11_p5]
    pmaddwd    m2, m12
    pmaddwd    m3, m12
    pmaddwd    m4, m9
    pmaddwd    m5, m9

    paddd      m0, m2
    paddd      m1, m3
    paddd      m0, m4
    paddd      m1, m5

    paddd      m0, m7
    paddd      m1, m7

    psrad      m0, 3
    psrad      m1, 3
    packssdw   m0, m1

    movu   [highq + xq], m0

.looph:

    mov      posq, istrideq
    imul     posq, yq
    add      posq, xq

    movu       m0, [inputq + posq]

    add      posq, istrideq
    movu       m1, [inputq + posq]

    paddsw     m0, m1

    mov      posq, lwidthq
    imul     posq, yq
    add      posq, xq

    movu    [lowq + posq], m0

    add        yq, -2

    mov      posq, istrideq
    imul     posq, yq
    add      posq, xq

    movu       m0, [inputq + posq]
    add      posq, istrideq
    movu       m1, [inputq + posq]
    add      posq, istrideq
    movu       m2, [inputq + posq]
    add      posq, istrideq
    movu       m3, [inputq + posq]
    add      posq, istrideq
    movu       m4, [inputq + posq]
    add      posq, istrideq
    movu       m5, [inputq + posq]

    add        yq, 2

    mova       m6, m0
    punpcklwd  m0, m1
    punpckhwd  m1, m6

    mova       m6, m2
    punpcklwd  m2, m3
    punpckhwd  m3, m6

    mova       m6, m4
    punpcklwd  m4, m5
    punpckhwd  m5, m6

    pmaddwd    m0, m9
    pmaddwd    m1, m9
    pmaddwd    m2, m10
    pmaddwd    m3, m11
    pmaddwd    m4, m8
    pmaddwd    m5, m8

    paddd      m0, m4
    paddd      m1, m5

    paddd      m0, m7
    paddd      m1, m7

    psrad      m0, 3
    psrad      m1, 3
    paddd      m0, m2
    paddd      m1, m3
    packssdw   m0, m1

    mov      posq, hwidthq
    imul     posq, yq
    add      posq, xq

    movu   [highq + posq], m0

    add        yq, 2
    cmp        yq, heightq
    jl .looph

    mov      posq, istrideq
    imul     posq, yq
    add      posq, xq

    movu       m0, [inputq + posq]
    add      posq, istrideq
    movu       m1, [inputq + posq]

    paddsw     m0, m1

    mov      posq, lwidthq
    imul     posq, yq
    add      posq, xq

    movu    [lowq + posq], m0

    sub        yq, 4

    mov      posq, istrideq
    imul     posq, yq
    add      posq, xq

    movu       m0, [inputq + posq]
    add      posq, istrideq
    movu       m1, [inputq + posq]
    add      posq, istrideq
    movu       m2, [inputq + posq]
    add      posq, istrideq
    movu       m3, [inputq + posq]
    add      posq, istrideq
    movu       m4, [inputq + posq]
    add      posq, istrideq
    movu       m5, [inputq + posq]

    add        yq, 4

    mova       m6, m0
    punpcklwd  m0, m1
    punpckhwd  m1, m6

    mova       m6, m2
    punpcklwd  m2, m3
    punpckhwd  m3, m6

    mova       m6, m4
    punpcklwd  m4, m5
    punpckhwd  m5, m6

    pmaddwd    m0, m8
    pmaddwd    m1, m8
    pmaddwd    m2, m13
    pmaddwd    m3, m13
    pmaddwd    m4, [pw_p11_n5]
    pmaddwd    m5, [pw_n5_p11]

    paddd      m4, m2
    paddd      m5, m3

    paddd      m4, m0
    paddd      m5, m1

    paddd      m4, m7
    paddd      m5, m7

    psrad      m4, 3
    psrad      m5, 3
    packssdw   m4, m5

    mov      posq, hwidthq
    imul     posq, yq
    add      posq, xq

    movu   [highq + posq], m4

    add        xq, mmsize
    cmp        xq, widthq
    jl .loopw
    RET
%endif
