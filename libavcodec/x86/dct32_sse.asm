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
%include "x86util.asm"

SECTION_RODATA 32

align 32
ps_cos_vec: dd   0.500603,  0.505471,  0.515447,  0.531043
            dd   0.553104,  0.582935,  0.622504,  0.674808
            dd -10.190008, -3.407609, -2.057781, -1.484165
            dd  -1.169440, -0.972568, -0.839350, -0.744536
            dd   0.502419,  0.522499,  0.566944,  0.646822
            dd   0.788155,  1.060678,  1.722447,  5.101149
            dd   0.509796,  0.601345,  0.899976,  2.562916
            dd   0.509796,  0.601345,  0.899976,  2.562916
            dd   1.000000,  1.000000,  1.306563,  0.541196
            dd   1.000000,  1.000000,  1.306563,  0.541196
            dd   1.000000,  0.707107,  1.000000, -0.707107
            dd   1.000000,  0.707107,  1.000000, -0.707107
            dd   0.707107,  0.707107,  0.707107,  0.707107

align 32
ps_p1p1m1m1: dd 0, 0, 0x80000000, 0x80000000, 0, 0, 0x80000000, 0x80000000

%macro BUTTERFLY_SSE 4
    movaps %4, %1
    subps  %1, %2
    addps  %2, %4
    mulps  %1, %3
%endmacro

%macro BUTTERFLY_AVX 4
    vsubps  %4, %1, %2
    vaddps  %2, %2, %1
    vmulps  %1, %4, %3
%endmacro

%macro BUTTERFLY0_SSE 5
    movaps %4, %1
    shufps %1, %1, %5
    xorps  %4, %2
    addps  %1, %4
    mulps  %1, %3
%endmacro

%macro BUTTERFLY0_SSE2 5
    pshufd %4, %1, %5
    xorps  %1, %2
    addps  %1, %4
    mulps  %1, %3
%endmacro

%macro BUTTERFLY0_AVX 5
    vshufps %4, %1, %1, %5
    vxorps  %1, %1, %2
    vaddps  %4, %4, %1
    vmulps  %1, %4, %3
%endmacro

%macro BUTTERFLY2 4
    BUTTERFLY0 %1, %2, %3, %4, 0x1b
%endmacro

%macro BUTTERFLY3 4
    BUTTERFLY0 %1, %2, %3, %4, 0xb1
%endmacro

%macro BUTTERFLY3V 5
    movaps m%5, m%1
    addps  m%1, m%2
    subps  m%5, m%2
    SWAP %2, %5
    mulps  m%2, [ps_cos_vec+192]
    movaps m%5, m%3
    addps  m%3, m%4
    subps  m%4, m%5
    mulps  m%4, [ps_cos_vec+192]
%endmacro

%macro PASS6_AND_PERMUTE 0
    mov         tmpd, [outq+4]
    movss         m7, [outq+72]
    addss         m7, [outq+76]
    movss         m3, [outq+56]
    addss         m3, [outq+60]
    addss         m4, m3
    movss         m2, [outq+52]
    addss         m2, m3
    movss         m3, [outq+104]
    addss         m3, [outq+108]
    addss         m1, m3
    addss         m5, m4
    movss [outq+ 16], m1
    movss         m1, [outq+100]
    addss         m1, m3
    movss         m3, [outq+40]
    movss [outq+ 48], m1
    addss         m3, [outq+44]
    movss         m1, [outq+100]
    addss         m4, m3
    addss         m3, m2
    addss         m1, [outq+108]
    movss [outq+ 40], m3
    addss         m2, [outq+36]
    movss         m3, [outq+8]
    movss [outq+ 56], m2
    addss         m3, [outq+12]
    movss [outq+ 32], m3
    movss         m3, [outq+80]
    movss [outq+  8], m5
    movss [outq+ 80], m1
    movss         m2, [outq+52]
    movss         m5, [outq+120]
    addss         m5, [outq+124]
    movss         m1, [outq+64]
    addss         m2, [outq+60]
    addss         m0, m5
    addss         m5, [outq+116]
    mov    [outq+64], tmpd
    addss         m6, m0
    addss         m1, m6
    mov         tmpd, [outq+12]
    mov   [outq+ 96], tmpd
    movss [outq+  4], m1
    movss         m1, [outq+24]
    movss [outq+ 24], m4
    movss         m4, [outq+88]
    addss         m4, [outq+92]
    addss         m3, m4
    addss         m4, [outq+84]
    mov         tmpd, [outq+108]
    addss         m1, [outq+28]
    addss         m0, m1
    addss         m1, m5
    addss         m6, m3
    addss         m3, m0
    addss         m0, m7
    addss         m5, [outq+20]
    addss         m7, m1
    movss [outq+ 12], m6
    mov   [outq+112], tmpd
    movss         m6, [outq+28]
    movss [outq+ 28], m0
    movss         m0, [outq+36]
    movss [outq+ 36], m7
    addss         m1, m4
    movss         m7, [outq+116]
    addss         m0, m2
    addss         m7, [outq+124]
    movss [outq+ 72], m0
    movss         m0, [outq+44]
    addss         m2, m0
    movss [outq+ 44], m1
    movss [outq+ 88], m2
    addss         m0, [outq+60]
    mov         tmpd, [outq+60]
    mov   [outq+120], tmpd
    movss [outq+104], m0
    addss         m4, m5
    addss         m5, [outq+68]
    movss  [outq+52], m4
    movss  [outq+60], m5
    movss         m4, [outq+68]
    movss         m5, [outq+20]
    movss [outq+ 20], m3
    addss         m5, m7
    addss         m7, m6
    addss         m4, m5
    movss         m2, [outq+84]
    addss         m2, [outq+92]
    addss         m5, m2
    movss [outq+ 68], m4
    addss         m2, m7
    movss         m4, [outq+76]
    movss [outq+ 84], m2
    movss [outq+ 76], m5
    addss         m7, m4
    addss         m6, [outq+124]
    addss         m4, m6
    addss         m6, [outq+92]
    movss [outq+100], m4
    movss [outq+108], m6
    movss         m6, [outq+92]
    movss  [outq+92], m7
    addss         m6, [outq+124]
    movss [outq+116], m6
%endmacro

%define BUTTERFLY  BUTTERFLY_AVX
%define BUTTERFLY0 BUTTERFLY0_AVX

INIT_YMM
SECTION_TEXT
%if HAVE_AVX
; void ff_dct32_float_avx(FFTSample *out, const FFTSample *in)
cglobal dct32_float_avx, 2,3,8, out, in, tmp
    ; pass 1
    vmovaps     m4, [inq+0]
    vinsertf128 m5, m5, [inq+96], 1
    vinsertf128 m5, m5, [inq+112], 0
    vshufps     m5, m5, m5, 0x1b
    BUTTERFLY   m4, m5, [ps_cos_vec], m6

    vmovaps     m2, [inq+64]
    vinsertf128 m6, m6, [inq+32], 1
    vinsertf128 m6, m6, [inq+48], 0
    vshufps     m6, m6, m6, 0x1b
    BUTTERFLY   m2, m6, [ps_cos_vec+32], m0

    ; pass 2

    BUTTERFLY  m5, m6, [ps_cos_vec+64], m0
    BUTTERFLY  m4, m2, [ps_cos_vec+64], m7


    ; pass 3
    vperm2f128  m3, m6, m4, 0x31
    vperm2f128  m1, m6, m4, 0x20
    vshufps     m3, m3, m3, 0x1b

    BUTTERFLY   m1, m3, [ps_cos_vec+96], m6


    vperm2f128  m4, m5, m2, 0x20
    vperm2f128  m5, m5, m2, 0x31
    vshufps     m5, m5, m5, 0x1b

    BUTTERFLY   m4, m5, [ps_cos_vec+96], m6

    ; pass 4
    vmovaps m6, [ps_p1p1m1m1+0]
    vmovaps m2, [ps_cos_vec+128]

    BUTTERFLY2  m5, m6, m2, m7
    BUTTERFLY2  m4, m6, m2, m7
    BUTTERFLY2  m1, m6, m2, m7
    BUTTERFLY2  m3, m6, m2, m7


    ; pass 5
    vshufps m6, m6, m6, 0xcc
    vmovaps m2, [ps_cos_vec+160]

    BUTTERFLY3  m5, m6, m2, m7
    BUTTERFLY3  m4, m6, m2, m7
    BUTTERFLY3  m1, m6, m2, m7
    BUTTERFLY3  m3, m6, m2, m7

    vperm2f128  m6, m3, m3, 0x31
    vmovaps [outq], m3

    vextractf128  [outq+64], m5, 1
    vextractf128  [outq+32], m5, 0

    vextractf128  [outq+80], m4, 1
    vextractf128  [outq+48], m4, 0

    vperm2f128  m0, m1, m1, 0x31
    vmovaps [outq+96], m1

    vzeroupper

    ;    pass 6, no SIMD...
INIT_XMM
    PASS6_AND_PERMUTE
    RET
%endif

%define BUTTERFLY  BUTTERFLY_SSE
%define BUTTERFLY0 BUTTERFLY0_SSE

%if ARCH_X86_64
%define SPILL SWAP
%define UNSPILL SWAP

%macro PASS5 0
    nop ; FIXME code alignment
    SWAP 5, 8
    SWAP 4, 12
    SWAP 6, 14
    SWAP 7, 13
    SWAP 0, 15
    PERMUTE 9,10, 10,12, 11,14, 12,9, 13,11, 14,13
    TRANSPOSE4x4PS 8, 9, 10, 11, 0
    BUTTERFLY3V    8, 9, 10, 11, 0
    addps   m10, m11
    TRANSPOSE4x4PS 12, 13, 14, 15, 0
    BUTTERFLY3V    12, 13, 14, 15, 0
    addps   m14, m15
    addps   m12, m14
    addps   m14, m13
    addps   m13, m15
%endmacro

%macro PASS6 0
    SWAP 9, 12
    SWAP 11, 14
    movss [outq+0x00], m8
    pshuflw m0, m8, 0xe
    movss [outq+0x10], m9
    pshuflw m1, m9, 0xe
    movss [outq+0x20], m10
    pshuflw m2, m10, 0xe
    movss [outq+0x30], m11
    pshuflw m3, m11, 0xe
    movss [outq+0x40], m12
    pshuflw m4, m12, 0xe
    movss [outq+0x50], m13
    pshuflw m5, m13, 0xe
    movss [outq+0x60], m14
    pshuflw m6, m14, 0xe
    movaps [outq+0x70], m15
    pshuflw m7, m15, 0xe
    addss   m0, m1
    addss   m1, m2
    movss [outq+0x08], m0
    addss   m2, m3
    movss [outq+0x18], m1
    addss   m3, m4
    movss [outq+0x28], m2
    addss   m4, m5
    movss [outq+0x38], m3
    addss   m5, m6
    movss [outq+0x48], m4
    addss   m6, m7
    movss [outq+0x58], m5
    movss [outq+0x68], m6
    movss [outq+0x78], m7

    PERMUTE 1,8, 3,9, 5,10, 7,11, 9,12, 11,13, 13,14, 8,1, 10,3, 12,5, 14,7
    movhlps m0, m1
    pshufd  m1, m1, 3
    SWAP 0, 2, 4, 6, 8, 10, 12, 14
    SWAP 1, 3, 5, 7, 9, 11, 13, 15
%rep 7
    movhlps m0, m1
    pshufd  m1, m1, 3
    addss   m15, m1
    SWAP 0, 2, 4, 6, 8, 10, 12, 14
    SWAP 1, 3, 5, 7, 9, 11, 13, 15
%endrep
%assign i 4
%rep 15
    addss m0, m1
    movss [outq+i], m0
    SWAP 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    %assign i i+8
%endrep
%endmacro

%else ; ARCH_X86_32
%macro SPILL 2 ; xmm#, mempos
    movaps [outq+(%2-8)*16], m%1
%endmacro
%macro UNSPILL 2
    movaps m%1, [outq+(%2-8)*16]
%endmacro

%define PASS6 PASS6_AND_PERMUTE
%macro PASS5 0
    movaps      m2, [ps_cos_vec+160]
    shufps      m3, m3, 0xcc

    BUTTERFLY3  m5, m3, m2, m1
    SPILL 5, 8

    UNSPILL 1, 9
    BUTTERFLY3  m1, m3, m2, m5
    SPILL 1, 14

    BUTTERFLY3  m4, m3, m2, m5
    SPILL 4, 12

    BUTTERFLY3  m7, m3, m2, m5
    SPILL 7, 13

    UNSPILL 5, 10
    BUTTERFLY3  m5, m3, m2, m7
    SPILL 5, 10

    UNSPILL 4, 11
    BUTTERFLY3  m4, m3, m2, m7
    SPILL 4, 11

    BUTTERFLY3  m6, m3, m2, m7
    SPILL 6, 9

    BUTTERFLY3  m0, m3, m2, m7
    SPILL 0, 15
%endmacro
%endif


INIT_XMM
%macro DCT32_FUNC 1
; void ff_dct32_float_sse(FFTSample *out, const FFTSample *in)
cglobal dct32_float_%1, 2,3,16, out, in, tmp
    ; pass 1

    movaps      m0, [inq+0]
    LOAD_INV    m1, [inq+112]
    BUTTERFLY   m0, m1, [ps_cos_vec], m3

    movaps      m7, [inq+64]
    LOAD_INV    m4, [inq+48]
    BUTTERFLY   m7, m4, [ps_cos_vec+32], m3

    ; pass 2
    movaps      m2, [ps_cos_vec+64]
    BUTTERFLY   m1, m4, m2, m3
    SPILL 1, 11
    SPILL 4, 8

    ; pass 1
    movaps      m1, [inq+16]
    LOAD_INV    m6, [inq+96]
    BUTTERFLY   m1, m6, [ps_cos_vec+16], m3

    movaps      m4, [inq+80]
    LOAD_INV    m5, [inq+32]
    BUTTERFLY   m4, m5, [ps_cos_vec+48], m3

    ; pass 2
    BUTTERFLY   m0, m7, m2, m3

    movaps      m2, [ps_cos_vec+80]
    BUTTERFLY   m6, m5, m2, m3

    BUTTERFLY   m1, m4, m2, m3

    ; pass 3
    movaps      m2, [ps_cos_vec+96]
    shufps      m1, m1, 0x1b
    BUTTERFLY   m0, m1, m2, m3
    SPILL 0, 15
    SPILL 1, 14

    UNSPILL 0, 8
    shufps      m5, m5, 0x1b
    BUTTERFLY   m0, m5, m2, m3

    UNSPILL 1, 11
    shufps      m6, m6, 0x1b
    BUTTERFLY   m1, m6, m2, m3
    SPILL 1, 11

    shufps      m4, m4, 0x1b
    BUTTERFLY   m7, m4, m2, m3

    ; pass 4
    movaps      m3, [ps_p1p1m1m1+0]
    movaps      m2, [ps_cos_vec+128]

    BUTTERFLY2  m5, m3, m2, m1

    BUTTERFLY2  m0, m3, m2, m1
    SPILL 0, 9

    BUTTERFLY2  m6, m3, m2, m1
    SPILL 6, 10

    UNSPILL 0, 11
    BUTTERFLY2  m0, m3, m2, m1
    SPILL 0, 11

    BUTTERFLY2  m4, m3, m2, m1

    BUTTERFLY2  m7, m3, m2, m1

    UNSPILL 6, 14
    BUTTERFLY2  m6, m3, m2, m1

    UNSPILL 0, 15
    BUTTERFLY2  m0, m3, m2, m1

    PASS5
    PASS6
    RET
%endmacro

%macro LOAD_INV_SSE 2
    movaps      %1, %2
    shufps      %1, %1, 0x1b
%endmacro

%define LOAD_INV LOAD_INV_SSE
DCT32_FUNC sse

%macro LOAD_INV_SSE2 2
    pshufd      %1, %2, 0x1b
%endmacro

%define LOAD_INV LOAD_INV_SSE2
%define BUTTERFLY0 BUTTERFLY0_SSE2
DCT32_FUNC sse2
