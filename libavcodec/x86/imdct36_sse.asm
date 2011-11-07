;******************************************************************************
;* 36 point SSE-optimized IMDCT transform
;* Copyright (c) 2011 Vitor Sessak
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

%include "libavutil/x86/x86inc.asm"
%include "libavutil/x86/x86util.asm"

SECTION_RODATA

align 16
ps_mask:  dd 0, ~0, ~0, ~0
ps_mask2: dd 0, ~0,  0, ~0
ps_mask3: dd 0,  0,  0, ~0
ps_mask4: dd 0, ~0,  0,  0

ps_val1:  dd          -0.5,          -0.5, -0.8660254038, -0.8660254038
ps_val2:  dd           1.0,           1.0,  0.8660254038,  0.8660254038
ps_val3:  dd  0.1736481777,  0.1736481777,  0.3420201433,  0.3420201433
ps_val4:  dd -0.7660444431, -0.7660444431,  0.8660254038,  0.8660254038
ps_val5:  dd -0.9396926208, -0.9396926208, -0.9848077530, -0.9848077530
ps_val6:  dd           0.5,           0.5, -0.6427876097, -0.6427876097
ps_val7:  dd           1.0,           1.0, -0.6427876097, -0.6427876097

ps_p1p1m1m1: dd 0,          0, 0x80000000, 0x80000000
ps_p1m1p1m1: dd 0, 0x80000000,          0, 0x80000000

ps_cosh:       dd 1.0, 0.50190991877167369479,  1.0,  5.73685662283492756461
               dd 1.0, 0.51763809020504152469,  1.0,  1.93185165257813657349
               dd 1.0, 0.55168895948124587824, -1.0, -1.18310079157624925896
               dd 1.0, 0.61038729438072803416, -1.0, -0.87172339781054900991
               dd 1.0, 0.70710678118654752439,  0.0,  0.0

ps_cosh_sse3:  dd 1.0, -0.50190991877167369479,  1.0, -5.73685662283492756461
               dd 1.0, -0.51763809020504152469,  1.0, -1.93185165257813657349
               dd 1.0, -0.55168895948124587824, -1.0,  1.18310079157624925896
               dd 1.0, -0.61038729438072803416, -1.0,  0.87172339781054900991
               dd 1.0,  0.70710678118654752439,  0.0,  0.0

%define SBLIMIT 32
SECTION_TEXT

%macro PSHUFD_SSE_AVX 3
    shufps %1, %2, %2, %3
%endmacro
%macro PSHUFD_SSE2 3
    pshufd %1, %2, %3
%endmacro

; input  %1={x1,x2,x3,x4}, %2={y1,y2,y3,y4}
; output %3={x3,x4,y1,y2}
%macro BUILDINVHIGHLOW_SSE 3
    movlhps %3, %2
    movhlps %3, %1
%endmacro
%macro BUILDINVHIGHLOW_AVX 3
    shufps %3, %1, %2, 0x4e
%endmacro

; input  %1={x1,x2,x3,x4}, %2={y1,y2,y3,y4}
; output %3={x4,y1,y2,y3}
%macro ROTLEFT_SSE 3
    BUILDINVHIGHLOW %1, %2, %3
    shufps  %3, %3, %2, 0x99
%endmacro

%macro ROTLEFT_SSSE3 3
    palignr  %3, %2, %1, 12
%endmacro

%macro INVERTHL_SSE1 2
    movhlps %1, %2
    movlhps %1, %2
%endmacro

%macro INVERTHL_SSE2 2
    PSHUFD  %1, %2, 0x4e
%endmacro

%macro BUTTERF_SSE12 3
    INVERTHL %2, %1
    xorps    %1, [ps_p1p1m1m1]
    addps    %1, %2
    mulps    %1, [ps_cosh + %3]
    PSHUFD   %2, %1, 0xb1
    xorps    %1, [ps_p1m1p1m1]
    addps    %1, %2
%endmacro
%macro BUTTERF_SSE3 3
    INVERTHL %2, %1
    xorps    %1, %1, [ps_p1p1m1m1]
    addps    %1, %1, %2
    mulps    %1, %1, [ps_cosh_sse3 + %3]
    PSHUFD   %2, %1, 0xb1
    addsubps %1, %1, %2
%endmacro

%macro STORE 3
    movhlps %2, %1
    movss   [%3             ], %1
    movss   [%3 +  8*SBLIMIT], %2
    shufps  %1, %1, 0xb1
    movss   [%3 +  4*SBLIMIT], %1
    movhlps %2, %1
    movss   [%3 + 12*SBLIMIT], %2
%endmacro

%macro LOADA64 2
   movlps   %1, [%2]
   movhps   %1, [%2 + 8]
%endmacro

%macro STOREA64 2
   movlps   [%1    ], %2
   movhps   [%1 + 8], %2
%endmacro

%macro DEFINE_IMDCT 1
cglobal imdct36_float_%1, 4,4,9, out, buf, in, win

    ; for(i=17;i>=1;i--) in[i] += in[i-1];
    LOADA64 m0, inq
    LOADA64 m1, inq + 16

    ROTLEFT m0, m1, m5

    PSHUFD  m6, m0, 0x93
    andps   m6, m6, [ps_mask]
    addps   m0, m0, m6

    LOADA64 m2, inq + 32

    ROTLEFT m1, m2, m7

    addps   m1, m1, m5
    LOADA64 m3, inq + 48

    ROTLEFT m2, m3, m5

    xorps   m4, m4, m4
    movlps  m4, [inq+64]
    BUILDINVHIGHLOW m3, m4, m6
    shufps  m6, m6, m4, 0xa9

    addps   m4, m4, m6
    addps   m2, m2, m7
    addps   m3, m3, m5

    ; for(i=17;i>=3;i-=2) in[i] += in[i-2];
    movlhps m5, m5, m0
    andps   m5, m5, [ps_mask3]

    BUILDINVHIGHLOW m0, m1, m7
    andps   m7, m7, [ps_mask2]

    addps   m0, m0, m5

    BUILDINVHIGHLOW m1, m2, m6
    andps   m6, m6, [ps_mask2]

    addps  m1, m1, m7

    BUILDINVHIGHLOW m2, m3, m7
    andps   m7, m7, [ps_mask2]

    addps   m2, m2, m6

    movhlps m6, m6, m3
    andps   m6, m6, [ps_mask4]

    addps  m3, m3, m7
    addps  m4, m4, m6

    ; Populate tmp[]
    movlhps m6, m1, m5    ; zero out high values
    subps   m6, m6, m4

    subps  m5, m0, m3

%ifdef ARCH_X86_64
    SWAP   m5, m8
%endif

    mulps  m7, m2, [ps_val1]

%ifdef ARCH_X86_64
    mulps  m5, m8, [ps_val2]
%else
    mulps  m5, m5, [ps_val2]
%endif
    addps  m7, m7, m5

    mulps  m5, m6, [ps_val1]
    subps  m7, m7, m5

%ifndef ARCH_X86_64
    subps  m5, m0, m3
%else
    SWAP   m5, m8
%endif

    subps  m5, m5, m6
    addps  m5, m5, m2

    shufps m6, m4, m3, 0xe4
    subps  m6, m6, m2
    mulps  m6, m6, [ps_val3]

    addps  m4, m4, m1
    mulps  m4, m4, [ps_val4]

    shufps m1, m1, m0, 0xe4
    addps  m1, m1, m2
    mulps  m1, m1, [ps_val5]

    mulps  m3, m3, [ps_val6]
    mulps  m0, m0, [ps_val7]
    addps  m0, m0, m3

    xorps  m2, m1, [ps_p1p1m1m1]
    subps  m2, m2, m4
    addps  m2, m2, m0

    addps  m3, m4, m0
    subps  m3, m3, m6
    xorps  m3, m3, [ps_p1p1m1m1]

    shufps m0, m0, m4, 0xe4
    subps  m0, m0, m1
    addps  m0, m0, m6

    BUILDINVHIGHLOW m2, m3, m4
    shufps  m3, m3, m2, 0x4e

    ; we have tmp = {SwAPLH(m0), SwAPLH(m7), m3, m4, m5}

    BUTTERF  m0, m1, 0
    BUTTERF  m7, m2, 16
    BUTTERF  m3, m6, 32
    BUTTERF  m4, m1, 48

    mulps   m5, m5, [ps_cosh + 64]
    PSHUFD  m1, m5, 0xe1
    xorps   m5, m5, [ps_p1m1p1m1]
    addps   m5, m5, m1

    ; permutates:
    ; m0    0  1  2  3     =>     2  6 10 14   m1
    ; m7    4  5  6  7     =>     3  7 11 15   m2
    ; m3    8  9 10 11     =>    17 13  9  5   m3
    ; m4   12 13 14 15     =>    16 12  8  4   m5
    ; m5   16 17 xx xx     =>     0  1 xx xx   m0

    unpckhps m1, m0, m7
    unpckhps m6, m3, m4
    movhlps  m2, m6, m1
    movlhps  m1, m1, m6

    unpcklps m5, m5, m4
    unpcklps m3, m3, m7
    movhlps  m4, m3, m5
    movlhps  m5, m5, m3
    SWAP m4, m3
    ; permutation done

    PSHUFD  m6, m2, 0xb1
    movlps  m7, [bufq + 64]
    mulps   m6, m6, [winq + 16*4]
    addps   m6, m6, m7
    movss   [outq + 64*SBLIMIT], m6
    shufps  m6, m6, m6, 0xb1
    movss   [outq + 68*SBLIMIT], m6

    mulps   m6, m3, [winq + 4*4]
    LOADA64 m4, bufq + 16
    addps   m6, m6, m4
    STORE   m6, m7, outq + 16*SBLIMIT

    shufps  m4, m0, m3, 0xb5
    mulps   m4, m4, [winq + 8*4]
    LOADA64 m7, bufq + 32
    addps   m4, m4, m7
    STORE   m4, m6, outq + 32*SBLIMIT

    shufps  m3, m3, m2, 0xb1
    mulps   m3, m3, [winq + 12*4]
    LOADA64 m7, bufq + 48
    addps   m3, m3, m7
    STORE   m3, m7, outq + 48*SBLIMIT

    mulps   m2, m2, [winq]
    LOADA64 m6, bufq
    addps   m2, m2, m6
    STORE   m2, m7, outq

    mulps    m4, m1, [winq + 20*4]
    STOREA64 bufq, m4

    mulps    m3, m5, [winq + 24*4]
    STOREA64 bufq + 16, m3

    shufps   m0, m0, m5, 0xb0
    mulps    m0, m0, [winq + 28*4]
    STOREA64 bufq + 32, m0

    shufps   m5, m5, m1, 0xb1
    mulps    m5, m5, [winq + 32*4]
    STOREA64 bufq + 48, m5

    shufps   m1, m1, m1, 0xb1
    mulps    m1, m1, [winq + 36*4]
    movlps  [bufq + 64], m1
    RET
%endmacro

%define PSHUFD PSHUFD_SSE_AVX
%define INVERTHL INVERTHL_SSE1
%define BUTTERF  BUTTERF_SSE12
%define BUTTERF0 BUTTERF0_SSE12
%define BUILDINVHIGHLOW BUILDINVHIGHLOW_SSE
%define ROTLEFT ROTLEFT_SSE

INIT_XMM

DEFINE_IMDCT sse

%define PSHUFD PSHUFD_SSE2
%define INVERTHL INVERTHL_SSE2

DEFINE_IMDCT sse2

%define BUTTERF  BUTTERF_SSE3
%define BUTTERF0 BUTTERF0_SSE3

DEFINE_IMDCT sse3

%define ROTLEFT ROTLEFT_SSSE3

DEFINE_IMDCT ssse3

%define BUILDINVHIGHLOW BUILDINVHIGHLOW_AVX
%define PSHUFD PSHUFD_SSE_AVX

INIT_AVX
DEFINE_IMDCT avx
