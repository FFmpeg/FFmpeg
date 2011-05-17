;******************************************************************************
;* 32 point SSE-optimized DCT transform
;* Copyright (c) 2010 Vitor Sessak
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"

SECTION_RODATA 32

align 32
ps_cos_vec: dd   0.500603,  0.505471,  0.515447,  0.531043
            dd   0.553104,  0.582935,  0.622504,  0.674808
            dd  -1.169440, -0.972568, -0.839350, -0.744536
            dd -10.190008, -3.407609, -2.057781, -1.484165
            dd   0.502419,  0.522499,  0.566944,  0.646822
            dd   0.788155,  1.060678,  1.722447,  5.101149
            dd   0.509796,  0.601345,  0.899976,  2.562916
            dd   1.000000,  1.000000,  1.306563,  0.541196
            dd   1.000000,  0.707107,  1.000000, -0.707107


ps_p1p1m1m1: dd 0, 0, 0x80000000, 0x80000000

%macro BUTTERFLY 4
    movaps %4, %1
    subps  %1, %2
    addps  %2, %4
    mulps  %1, %3
%endmacro

%macro BUTTERFLY0 5
    movaps %4, %1
    shufps %1, %1, %5
    xorps  %4, %2
    addps  %1, %4
    mulps  %1, %3
%endmacro

%macro BUTTERFLY2 4
    BUTTERFLY0 %1, %2, %3, %4, 0x1b
%endmacro

%macro BUTTERFLY3 4
    BUTTERFLY0 %1, %2, %3, %4, 0xb1
%endmacro

INIT_XMM
section .text align=16
; void ff_dct32_float_sse(FFTSample *out, const FFTSample *in)
cglobal dct32_float_sse, 2,3,8, out, in, tmp
    ; pass 1

    movaps      m0, [inq+0]
    movaps      m1, [inq+112]
    shufps      m1, m1, 0x1b
    BUTTERFLY   m0, m1, [ps_cos_vec], m3

    movaps      m7, [inq+64]
    movaps      m4, [inq+48]
    shufps      m4, m4, 0x1b
    BUTTERFLY   m7,  m4, [ps_cos_vec+48], m3


    ; pass 2
    movaps      m2, [ps_cos_vec+64]
    BUTTERFLY   m1, m4, m2, m3
    movaps      [outq+48], m1
    movaps      [outq+ 0], m4

    ; pass 1
    movaps      m1, [inq+16]
    movaps      m6, [inq+96]
    shufps      m6, m6, 0x1b
    BUTTERFLY   m1, m6, [ps_cos_vec+16], m3

    movaps      m4, [inq+80]
    movaps      m5, [inq+32]
    shufps      m5, m5, 0x1b
    BUTTERFLY   m4, m5, [ps_cos_vec+32], m3

    ; pass 2
    BUTTERFLY   m0, m7, m2, m3

    movaps      m2, [ps_cos_vec+80]
    BUTTERFLY   m6, m5, m2, m3

    BUTTERFLY   m1, m4, m2, m3

    ; pass 3
    movaps      m2, [ps_cos_vec+96]
    shufps      m1, m1, 0x1b
    BUTTERFLY   m0, m1, m2, m3
    movaps      [outq+112], m0
    movaps      [outq+ 96], m1

    movaps      m0, [outq+0]
    shufps      m5, m5, 0x1b
    BUTTERFLY   m0, m5, m2, m3

    movaps      m1, [outq+48]
    shufps      m6, m6, 0x1b
    BUTTERFLY   m1, m6, m2, m3
    movaps      [outq+48], m1

    shufps      m4, m4, 0x1b
    BUTTERFLY   m7, m4, m2, m3

    ; pass 4
    movaps      m3, [ps_p1p1m1m1+0]
    movaps      m2, [ps_cos_vec+112]

    BUTTERFLY2  m5, m3, m2, m1

    BUTTERFLY2  m0, m3, m2, m1
    movaps      [outq+16], m0

    BUTTERFLY2  m6, m3, m2, m1
    movaps      [outq+32], m6

    movaps      m0, [outq+48]
    BUTTERFLY2  m0, m3, m2, m1
    movaps      [outq+48], m0

    BUTTERFLY2  m4, m3, m2, m1

    BUTTERFLY2  m7, m3, m2, m1

    movaps      m6, [outq+96]
    BUTTERFLY2  m6, m3, m2, m1

    movaps      m0, [outq+112]
    BUTTERFLY2  m0, m3, m2, m1

    ; pass 5
    movaps      m2, [ps_cos_vec+128]
    shufps      m3, m3, 0xcc

    BUTTERFLY3  m5, m3, m2, m1
    movaps      [outq+0], m5

    movaps      m1, [outq+16]
    BUTTERFLY3  m1, m3, m2, m5
    movaps      [outq+16], m1

    BUTTERFLY3  m4, m3, m2, m5
    movaps      [outq+64], m4

    BUTTERFLY3  m7, m3, m2, m5
    movaps      [outq+80], m7

    movaps      m5, [outq+32]
    BUTTERFLY3  m5, m3, m2, m7
    movaps      [outq+32], m5

    movaps      m4, [outq+48]
    BUTTERFLY3  m4, m3, m2, m7
    movaps      [outq+48], m4

    BUTTERFLY3  m6, m3, m2, m7
    movaps      [outq+96], m6

    BUTTERFLY3  m0, m3, m2, m7
    movaps      [outq+112], m0


    ;    pass 6, no SIMD...
    movss         m3, [outq+56]
    mov         tmpd, [outq+4]
    addss         m3, [outq+60]
    movss         m7, [outq+72]
    addss         m4, m3
    movss         m2, [outq+52]
    addss         m2, m3
    movss         m3, [outq+24]
    addss         m3, [outq+28]
    addss         m7, [outq+76]
    addss         m1, m3
    addss         m5, m4
    movss [outq+ 16], m1
    movss         m1, [outq+20]
    addss         m1, m3
    movss         m3, [outq+40]
    movss [outq+ 48], m1
    addss         m3, [outq+44]
    movss         m1, [outq+20]
    addss         m4, m3
    addss         m3, m2
    addss         m1, [outq+28]
    movss [outq+ 40], m3
    addss         m2, [outq+36]
    movss         m3, [outq+8]
    movss [outq+ 56], m2
    addss         m3, [outq+12]
    movss [outq+  8], m5
    movss [outq+ 32], m3
    movss         m2, [outq+52]
    movss         m3, [outq+80]
    movss         m5, [outq+120]
    movss [outq+ 80], m1
    movss [outq+ 24], m4
    addss         m5, [outq+124]
    movss         m1, [outq+64]
    addss         m2, [outq+60]
    addss         m0, m5
    addss         m5, [outq+116]
    mov    [outq+64], tmpd
    addss         m6, m0
    addss         m1, m6
    mov         tmpd, [outq+12]
    movss [outq+  4], m1
    movss         m1, [outq+88]
    mov   [outq+ 96], tmpd
    addss         m1, [outq+92]
    movss         m4, [outq+104]
    mov         tmpd, [outq+28]
    addss         m4, [outq+108]
    addss         m0, m4
    addss         m3, m1
    addss         m1, [outq+84]
    addss         m4, m5
    addss         m6, m3
    addss         m3, m0
    addss         m0, m7
    addss         m5, [outq+100]
    addss         m7, m4
    mov   [outq+112], tmpd
    movss [outq+ 28], m0
    movss         m0, [outq+36]
    movss [outq+ 36], m7
    addss         m4, m1
    movss         m7, [outq+116]
    addss         m0, m2
    addss         m7, [outq+124]
    movss [outq+ 72], m0
    movss         m0, [outq+44]
    movss [outq+ 12], m6
    movss [outq+ 20], m3
    addss         m2, m0
    movss [outq+ 44], m4
    movss [outq+ 88], m2
    addss         m0, [outq+60]
    mov         tmpd, [outq+60]
    mov   [outq+120], tmpd
    movss [outq+104], m0
    addss         m1, m5
    addss         m5, [outq+68]
    movss  [outq+52], m1
    movss  [outq+60], m5
    movss         m1, [outq+68]
    movss         m5, [outq+100]
    addss         m5, m7
    addss         m7, [outq+108]
    addss         m1, m5
    movss         m2, [outq+84]
    addss         m2, [outq+92]
    addss         m5, m2
    movss [outq+ 68], m1
    addss         m2, m7
    movss         m1, [outq+76]
    movss [outq+ 84], m2
    movss [outq+ 76], m5
    movss         m2, [outq+108]
    addss         m7, m1
    addss         m2, [outq+124]
    addss         m1, m2
    addss         m2, [outq+92]
    movss [outq+100], m1
    movss [outq+108], m2
    movss         m2, [outq+92]
    movss [outq+ 92], m7
    addss         m2, [outq+124]
    movss [outq+116], m2
    RET
